/* $Id: CFGM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * CFGM - Configuration Manager.
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

/** @page pg_cfgm       CFGM - The Configuration Manager
 *
 * The configuration manager is a directory containing the VM configuration at
 * run time. It works in a manner similar to the windows registry - it's like a
 * file system hierarchy, but the files (values) live in a separate name space
 * and can include the path separators.
 *
 * The configuration is normally created via a callback passed to VMR3Create()
 * via the pfnCFGMConstructor parameter. To make testcase writing a bit simpler,
 * we allow the callback to be NULL, in which case a simple default
 * configuration will be created by CFGMR3ConstructDefaultTree(). The
 * Console::configConstructor() method in Main/ConsoleImpl2.cpp creates the
 * configuration from the XML.
 *
 * Devices, drivers, services and other PDM stuff are given their own subtree
 * where they are protected from accessing information of any parents. This is
 * is implemented via the CFGMR3SetRestrictedRoot() API.
 *
 * Data validation beyond the basic primitives is left to the caller. The caller
 * is in a better position to know the proper validation rules of the individual
 * properties.
 *
 * @see grp_cfgm
 *
 *
 * @section sec_cfgm_primitives     Data Primitives
 *
 * CFGM supports the following data primitives:
 *      - Integers. Representation is unsigned 64-bit. Boolean, unsigned and
 *        small integers, and pointers are all represented using this primitive.
 *      - Zero terminated character strings. These are of course UTF-8.
 *      - Variable length byte strings. This can be used to get/put binary
 *        objects like for instance RTMAC.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_CFGM
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/vmm.h>
#include "CFGMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/memsafer.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <iprt/uuid.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void cfgmR3DumpPath(PCFGMNODE pNode, PCDBGFINFOHLP pHlp);
static void cfgmR3Dump(PCFGMNODE pRoot, unsigned iLevel, PCDBGFINFOHLP pHlp);
static DECLCALLBACK(void) cfgmR3Info(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
static int  cfgmR3ResolveNode(PCFGMNODE pNode, const char *pszPath, PCFGMNODE *ppChild);
static int  cfgmR3ResolveLeaf(PCFGMNODE pNode, const char *pszName, PCFGMLEAF *ppLeaf);
static int  cfgmR3InsertLeaf(PCFGMNODE pNode, const char *pszName, PCFGMLEAF *ppLeaf);
static void cfgmR3RemoveLeaf(PCFGMNODE pNode, PCFGMLEAF pLeaf);
static void cfgmR3FreeValue(PVM pVM, PCFGMLEAF pLeaf);


/** @todo replace pVM for pUVM !*/

/**
 * Allocator wrapper.
 *
 * @returns Pointer to the allocated memory, NULL on failure.
 * @param   pVM         The cross context VM structure, if the tree
 *                      is associated with one.
 * @param   enmTag      The allocation tag.
 * @param   cb          The size of the allocation.
 */
static void *cfgmR3MemAlloc(PVM pVM, MMTAG enmTag, size_t cb)
{
    if (pVM)
        return MMR3HeapAlloc(pVM, enmTag, cb);
    return RTMemAlloc(cb);
}


/**
 * Free wrapper.
 *
 * @param   pVM         The cross context VM structure, if the tree
 *                      is associated with one.
 * @param   pv          The memory block to free.
 */
static void cfgmR3MemFree(PVM pVM, void *pv)
{
    if (pVM)
        MMR3HeapFree(pv);
    else
        RTMemFree(pv);
}


/**
 * String allocator wrapper.
 *
 * @returns Pointer to the allocated memory, NULL on failure.
 * @param   pVM         The cross context VM structure, if the tree
 *                      is associated with one.
 * @param   enmTag      The allocation tag.
 * @param   cbString    The size of the allocation, terminator included.
 */
static char *cfgmR3StrAlloc(PVM pVM, MMTAG enmTag,  size_t cbString)
{
    if (pVM)
        return (char *)MMR3HeapAlloc(pVM, enmTag, cbString);
    return (char *)RTStrAlloc(cbString);
}


/**
 * String free wrapper.
 *
 * @param   pVM         The cross context VM structure, if the tree
 *                      is associated with one.
 * @param   pszString   The memory block to free.
 */
static void cfgmR3StrFree(PVM pVM, char *pszString)
{
    if (pVM)
        MMR3HeapFree(pszString);
    else
        RTStrFree(pszString);
}


/**
 * Frees one node, leaving any children or leaves to the caller.
 *
 * @param   pNode               The node structure to free.
 */
static void cfgmR3FreeNodeOnly(PCFGMNODE pNode)
{
    pNode->pFirstLeaf    = NULL;
    pNode->pFirstChild   = NULL;
    pNode->pNext         = NULL;
    pNode->pPrev         = NULL;
    if (!pNode->pVM)
        RTMemFree(pNode);
    else
    {
        pNode->pVM       = NULL;
        MMR3HeapFree(pNode);
    }
}




/**
 * Constructs the configuration for the VM.
 *
 * This should only be called used once.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pfnCFGMConstructor  Pointer to callback function for constructing
 *                              the VM configuration tree.  This is called on
 *                              the EMT.
 * @param   pvUser              The user argument passed to pfnCFGMConstructor.
 * @thread  EMT.
 * @internal
 */
VMMR3DECL(int) CFGMR3Init(PVM pVM, PFNCFGMCONSTRUCTOR pfnCFGMConstructor, void *pvUser)
{
    LogFlow(("CFGMR3Init: pfnCFGMConstructor=%p pvUser=%p\n", pfnCFGMConstructor, pvUser));

    /*
     * Init data members.
     */
    pVM->cfgm.s.pRoot = NULL;

    /*
     * Register DBGF into item.
     */
    int rc = DBGFR3InfoRegisterInternal(pVM, "cfgm", "Dumps a part of the CFGM tree. The argument indicates where to start.",
                                        cfgmR3Info);
    AssertRCReturn(rc,rc);

    /*
     * Root Node.
     */
    PCFGMNODE pRoot = (PCFGMNODE)MMR3HeapAllocZ(pVM, MM_TAG_CFGM, sizeof(*pRoot));
    if (!pRoot)
        return VERR_NO_MEMORY;
    pRoot->pVM        = pVM;
    pRoot->cchName    = 0;
    pVM->cfgm.s.pRoot = pRoot;

    /*
     * Call the constructor if specified, if not use the default one.
     */
    if (pfnCFGMConstructor)
        rc = pfnCFGMConstructor(pVM->pUVM, pVM, VMMR3GetVTable(), pvUser);
    else
        rc = CFGMR3ConstructDefaultTree(pVM);
    if (RT_SUCCESS(rc))
    {
        Log(("CFGMR3Init: Successfully constructed the configuration\n"));
        CFGMR3Dump(CFGMR3GetRoot(pVM));
    }
    else
        LogRel(("Constructor failed with rc=%Rrc pfnCFGMConstructor=%p\n", rc, pfnCFGMConstructor));

    return rc;
}


/**
 * Terminates the configuration manager.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @internal
 */
VMMR3DECL(int) CFGMR3Term(PVM pVM)
{
    CFGMR3RemoveNode(pVM->cfgm.s.pRoot);
    pVM->cfgm.s.pRoot = NULL;
    return 0;
}


/**
 * Gets the root node for the VM.
 *
 * @returns Pointer to root node.
 * @param   pVM             The cross context VM structure.
 */
VMMR3DECL(PCFGMNODE) CFGMR3GetRoot(PVM pVM)
{
    return pVM->cfgm.s.pRoot;
}


/**
 * Gets the root node for the VM.
 *
 * @returns Pointer to root node.
 * @param   pUVM        The user mode VM structure.
 */
VMMR3DECL(PCFGMNODE) CFGMR3GetRootU(PUVM pUVM)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, NULL);
    PVM pVM = pUVM->pVM;
    AssertReturn(pVM, NULL);
    return pVM->cfgm.s.pRoot;
}


/**
 * Gets the parent of a CFGM node.
 *
 * @returns Pointer to the parent node.
 * @returns NULL if pNode is Root or pNode is the start of a
 *          restricted subtree (use CFGMR3GetParentEx() for that).
 *
 * @param   pNode           The node which parent we query.
 */
VMMR3DECL(PCFGMNODE) CFGMR3GetParent(PCFGMNODE pNode)
{
    if (pNode && !pNode->fRestrictedRoot)
        return pNode->pParent;
    return NULL;
}


/**
 * Gets the parent of a CFGM node.
 *
 * @returns Pointer to the parent node.
 * @returns NULL if pNode is Root or pVM is not correct.
 *
 * @param   pVM             The cross context VM structure.  Used as token that
 *                          the caller is trusted.
 * @param   pNode           The node which parent we query.
 */
VMMR3DECL(PCFGMNODE) CFGMR3GetParentEx(PVM pVM, PCFGMNODE pNode)
{
    if (pNode && pNode->pVM == pVM)
        return pNode->pParent;
    return NULL;
}


/**
 * Query a child node.
 *
 * @returns Pointer to the specified node.
 * @returns NULL if node was not found or pNode is NULL.
 * @param   pNode           Node pszPath is relative to.
 * @param   pszPath         Path to the child node or pNode.
 *                          It's good style to end this with '/'.
 */
VMMR3DECL(PCFGMNODE) CFGMR3GetChild(PCFGMNODE pNode, const char *pszPath)
{
    PCFGMNODE pChild;
    int rc = cfgmR3ResolveNode(pNode, pszPath, &pChild);
    if (RT_SUCCESS(rc))
        return pChild;
    return NULL;
}


/**
 * Query a child node by a format string.
 *
 * @returns Pointer to the specified node.
 * @returns NULL if node was not found or pNode is NULL.
 * @param   pNode           Node pszPath is relative to.
 * @param   pszPathFormat   Path to the child node or pNode.
 *                          It's good style to end this with '/'.
 * @param   ...             Arguments to pszPathFormat.
 */
VMMR3DECL(PCFGMNODE) CFGMR3GetChildF(PCFGMNODE pNode, const char *pszPathFormat, ...)
{
    va_list Args;
    va_start(Args,  pszPathFormat);
    PCFGMNODE pRet = CFGMR3GetChildFV(pNode, pszPathFormat, Args);
    va_end(Args);
    return pRet;
}


/**
 * Query a child node by a format string.
 *
 * @returns Pointer to the specified node.
 * @returns NULL if node was not found or pNode is NULL.
 * @param   pNode           Node pszPath is relative to.
 * @param   pszPathFormat   Path to the child node or pNode.
 *                          It's good style to end this with '/'.
 * @param   Args            Arguments to pszPathFormat.
 */
VMMR3DECL(PCFGMNODE) CFGMR3GetChildFV(PCFGMNODE pNode, const char *pszPathFormat, va_list Args)
{
    char *pszPath;
    RTStrAPrintfV(&pszPath, pszPathFormat, Args);
    if (pszPath)
    {
        PCFGMNODE pChild;
        int rc = cfgmR3ResolveNode(pNode, pszPath, &pChild);
        RTStrFree(pszPath);
        if (RT_SUCCESS(rc))
            return pChild;
    }
    return NULL;
}


/**
 * Gets the first child node.
 * Use this to start an enumeration of child nodes.
 *
 * @returns Pointer to the first child.
 * @returns NULL if no children.
 * @param   pNode           Node to enumerate children for.
 */
VMMR3DECL(PCFGMNODE) CFGMR3GetFirstChild(PCFGMNODE pNode)
{
    return pNode ? pNode->pFirstChild : NULL;
}


/**
 * Gets the next sibling node.
 * Use this to continue an enumeration.
 *
 * @returns Pointer to the first child.
 * @returns NULL if no children.
 * @param   pCur            Node to returned by a call to CFGMR3GetFirstChild()
 *                          or successive calls to this function.
 */
VMMR3DECL(PCFGMNODE) CFGMR3GetNextChild(PCFGMNODE pCur)
{
    return pCur ? pCur->pNext : NULL;
}


/**
 * Gets the name of the current node.
 * (Needed for enumeration.)
 *
 * @returns VBox status code.
 * @param   pCur            Node to returned by a call to CFGMR3GetFirstChild()
 *                          or successive calls to CFGMR3GetNextChild().
 * @param   pszName         Where to store the node name.
 * @param   cchName         Size of the buffer pointed to by pszName (with terminator).
 */
VMMR3DECL(int) CFGMR3GetName(PCFGMNODE pCur, char *pszName, size_t cchName)
{
    int rc;
    if (pCur)
    {
        if (cchName > pCur->cchName)
        {
            rc = VINF_SUCCESS;
            memcpy(pszName, pCur->szName, pCur->cchName + 1);
        }
        else
            rc = VERR_CFGM_NOT_ENOUGH_SPACE;
    }
    else
        rc = VERR_CFGM_NO_NODE;
    return rc;
}


/**
 * Gets the length of the current node's name.
 * (Needed for enumeration.)
 *
 * @returns Node name length in bytes including the terminating null char.
 * @returns 0 if pCur is NULL.
 * @param   pCur            Node to returned by a call to CFGMR3GetFirstChild()
 *                          or successive calls to CFGMR3GetNextChild().
 */
VMMR3DECL(size_t) CFGMR3GetNameLen(PCFGMNODE pCur)
{
    return pCur ? pCur->cchName + 1 : 0;
}


/**
 * Validates that the child nodes are within a set of valid names.
 *
 * @returns true if all names are found in pszzAllowed.
 * @returns false if not.
 * @param   pNode           The node which children should be examined.
 * @param   pszzValid       List of valid names separated by '\\0' and ending with
 *                          a double '\\0'.
 *
 * @deprecated Use CFGMR3ValidateConfig.
 */
VMMR3DECL(bool) CFGMR3AreChildrenValid(PCFGMNODE pNode, const char *pszzValid)
{
    if (pNode)
    {
        for (PCFGMNODE pChild = pNode->pFirstChild; pChild; pChild = pChild->pNext)
        {
            /* search pszzValid for the name */
            const char *psz = pszzValid;
            while (*psz)
            {
                size_t cch = strlen(psz);
                if (    cch == pChild->cchName
                    &&  !memcmp(psz, pChild->szName, cch))
                    break;

                /* next */
                psz += cch + 1;
            }

            /* if at end of pszzValid we didn't find it => failure */
            if (!*psz)
            {
                AssertMsgFailed(("Couldn't find '%s' in the valid values\n", pChild->szName));
                return false;
            }
        }
    }

    /* all ok. */
    return true;
}


/**
 * Gets the first value of a node.
 * Use this to start an enumeration of values.
 *
 * @returns Pointer to the first value.
 * @param   pCur            The node (Key) which values to enumerate.
 */
VMMR3DECL(PCFGMLEAF) CFGMR3GetFirstValue(PCFGMNODE pCur)
{
    return pCur ? pCur->pFirstLeaf : NULL;
}

/**
 * Gets the next value in enumeration.
 *
 * @returns Pointer to the next value.
 * @param   pCur            The current value as returned by this function or CFGMR3GetFirstValue().
 */
VMMR3DECL(PCFGMLEAF) CFGMR3GetNextValue(PCFGMLEAF pCur)
{
    return pCur ? pCur->pNext : NULL;
}

/**
 * Get the value name.
 * (Needed for enumeration.)
 *
 * @returns VBox status code.
 * @param   pCur            Value returned by a call to CFGMR3GetFirstValue()
 *                          or successive calls to CFGMR3GetNextValue().
 * @param   pszName         Where to store the value name.
 * @param   cchName         Size of the buffer pointed to by pszName (with terminator).
 */
VMMR3DECL(int) CFGMR3GetValueName(PCFGMLEAF pCur, char *pszName, size_t cchName)
{
    int rc;
    if (pCur)
    {
        if (cchName > pCur->cchName)
        {
            rc = VINF_SUCCESS;
            memcpy(pszName, pCur->szName, pCur->cchName + 1);
        }
        else
            rc = VERR_CFGM_NOT_ENOUGH_SPACE;
    }
    else
        rc = VERR_CFGM_NO_NODE;
    return rc;
}


/**
 * Gets the length of the current node's name.
 * (Needed for enumeration.)
 *
 * @returns Value name length in bytes including the terminating null char.
 * @returns 0 if pCur is NULL.
 * @param   pCur            Value returned by a call to CFGMR3GetFirstValue()
 *                          or successive calls to CFGMR3GetNextValue().
 */
VMMR3DECL(size_t) CFGMR3GetValueNameLen(PCFGMLEAF pCur)
{
    return pCur ? pCur->cchName + 1 : 0;
}


/**
 * Gets the value type.
 * (For enumeration.)
 *
 * @returns VBox status code.
 * @param   pCur            Value returned by a call to CFGMR3GetFirstValue()
 *                          or successive calls to CFGMR3GetNextValue().
 */
VMMR3DECL(CFGMVALUETYPE) CFGMR3GetValueType(PCFGMLEAF pCur)
{
    Assert(pCur);
    return pCur->enmType;
}


/**
 * Validates that the values are within a set of valid names.
 *
 * @returns true if all names are found in pszzValid.
 * @returns false if not.
 * @param   pNode           The node which values should be examined.
 * @param   pszzValid       List of valid names separated by '\\0' and ending with
 *                          a double '\\0'.
 * @deprecated Use CFGMR3ValidateConfig.
 */
VMMR3DECL(bool) CFGMR3AreValuesValid(PCFGMNODE pNode, const char *pszzValid)
{
    if (pNode)
    {
        for (PCFGMLEAF pLeaf = pNode->pFirstLeaf; pLeaf; pLeaf = pLeaf->pNext)
        {
            /* search pszzValid for the name */
            const char *psz = pszzValid;
            while (*psz)
            {
                size_t cch = strlen(psz);
                if (    cch == pLeaf->cchName
                    &&  !memcmp(psz, pLeaf->szName, cch))
                    break;

                /* next */
                psz += cch + 1;
            }

            /* if at end of pszzValid we didn't find it => failure */
            if (!*psz)
            {
                AssertMsgFailed(("Couldn't find '%s' in the valid values\n", pLeaf->szName));
                return false;
            }
        }
    }

    /* all ok. */
    return true;
}


/**
 * Checks if the given value exists.
 *
 * @returns true if it exists, false if not.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         The name of the value we seek.
 */
VMMR3DECL(bool) CFGMR3Exists(PCFGMNODE pNode, const char *pszName)
{
    PCFGMLEAF pLeaf;
    int rc = cfgmR3ResolveLeaf(pNode, pszName, &pLeaf);
    return RT_SUCCESS_NP(rc);
}


/**
 * Query value type.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   penmType        Where to store the type.
 */
VMMR3DECL(int) CFGMR3QueryType(PCFGMNODE pNode, const char *pszName, PCFGMVALUETYPE penmType)
{
    PCFGMLEAF pLeaf;
    int rc = cfgmR3ResolveLeaf(pNode, pszName, &pLeaf);
    if (RT_SUCCESS(rc))
    {
        if (penmType)
            *penmType = pLeaf->enmType;
    }
    return rc;
}


/**
 * Query value size.
 * This works on all types of values.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pcb             Where to store the value size.
 */
VMMR3DECL(int) CFGMR3QuerySize(PCFGMNODE pNode, const char *pszName, size_t *pcb)
{
    PCFGMLEAF pLeaf;
    int rc = cfgmR3ResolveLeaf(pNode, pszName, &pLeaf);
    if (RT_SUCCESS(rc))
    {
        switch (pLeaf->enmType)
        {
            case CFGMVALUETYPE_INTEGER:
                *pcb = sizeof(pLeaf->Value.Integer.u64);
                break;

            case CFGMVALUETYPE_STRING:
            case CFGMVALUETYPE_PASSWORD:
                *pcb = pLeaf->Value.String.cb;
                break;

            case CFGMVALUETYPE_BYTES:
                *pcb = pLeaf->Value.Bytes.cb;
                break;

            default:
                rc = VERR_CFGM_IPE_1;
                AssertMsgFailed(("Invalid value type %d\n", pLeaf->enmType));
                break;
        }
    }
    return rc;
}


/**
 * Query integer value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pu64            Where to store the integer value.
 */
VMMR3DECL(int) CFGMR3QueryInteger(PCFGMNODE pNode, const char *pszName, uint64_t *pu64)
{
    PCFGMLEAF pLeaf;
    int rc = cfgmR3ResolveLeaf(pNode, pszName, &pLeaf);
    if (RT_SUCCESS(rc))
    {
        if (pLeaf->enmType == CFGMVALUETYPE_INTEGER)
            *pu64 = pLeaf->Value.Integer.u64;
        else
            rc = VERR_CFGM_NOT_INTEGER;
    }
    return rc;
}


/**
 * Query integer value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pu64            Where to store the integer value. This is set to the default on failure.
 * @param   u64Def          The default value. This is always set.
 */
VMMR3DECL(int) CFGMR3QueryIntegerDef(PCFGMNODE pNode, const char *pszName, uint64_t *pu64, uint64_t u64Def)
{
    PCFGMLEAF pLeaf;
    int rc = cfgmR3ResolveLeaf(pNode, pszName, &pLeaf);
    if (RT_SUCCESS(rc))
    {
        if (pLeaf->enmType == CFGMVALUETYPE_INTEGER)
            *pu64 = pLeaf->Value.Integer.u64;
        else
            rc = VERR_CFGM_NOT_INTEGER;
    }

    if (RT_FAILURE(rc))
    {
        *pu64 = u64Def;
        if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
            rc = VINF_SUCCESS;
    }

    return rc;
}


/**
 * Query zero terminated character value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of a zero terminate character value.
 * @param   pszString       Where to store the string.
 * @param   cchString       Size of the string buffer. (Includes terminator.)
 */
VMMR3DECL(int) CFGMR3QueryString(PCFGMNODE pNode, const char *pszName, char *pszString, size_t cchString)
{
    PCFGMLEAF pLeaf;
    int rc = cfgmR3ResolveLeaf(pNode, pszName, &pLeaf);
    if (RT_SUCCESS(rc))
    {
        if (pLeaf->enmType == CFGMVALUETYPE_STRING)
        {
            size_t cbSrc = pLeaf->Value.String.cb;
            if (cchString >= cbSrc)
            {
                memcpy(pszString, pLeaf->Value.String.psz, cbSrc);
                memset(pszString + cbSrc, 0, cchString - cbSrc);
            }
            else
                rc = VERR_CFGM_NOT_ENOUGH_SPACE;
        }
        else
            rc = VERR_CFGM_NOT_STRING;
    }
    return rc;
}


/**
 * Query zero terminated character value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of a zero terminate character value.
 * @param   pszString       Where to store the string. This will not be set on overflow error.
 * @param   cchString       Size of the string buffer. (Includes terminator.)
 * @param   pszDef          The default value.
 */
VMMR3DECL(int) CFGMR3QueryStringDef(PCFGMNODE pNode, const char *pszName, char *pszString, size_t cchString, const char *pszDef)
{
    PCFGMLEAF pLeaf;
    int rc = cfgmR3ResolveLeaf(pNode, pszName, &pLeaf);
    if (RT_SUCCESS(rc))
    {
        if (pLeaf->enmType == CFGMVALUETYPE_STRING)
        {
            size_t cbSrc = pLeaf->Value.String.cb;
            if (cchString >= cbSrc)
            {
                memcpy(pszString, pLeaf->Value.String.psz, cbSrc);
                memset(pszString + cbSrc, 0, cchString - cbSrc);
            }
            else
                rc = VERR_CFGM_NOT_ENOUGH_SPACE;
        }
        else
            rc = VERR_CFGM_NOT_STRING;
    }

    if (RT_FAILURE(rc) && rc != VERR_CFGM_NOT_ENOUGH_SPACE)
    {
        size_t cchDef = strlen(pszDef);
        if (cchString > cchDef)
        {
            memcpy(pszString, pszDef, cchDef);
            memset(pszString + cchDef, 0, cchString - cchDef);
            if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
                rc = VINF_SUCCESS;
        }
        else if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
            rc = VERR_CFGM_NOT_ENOUGH_SPACE;
    }

    return rc;
}


/**
 * Query byte string value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of a byte string value.
 * @param   pvData          Where to store the binary data.
 * @param   cbData          Size of buffer pvData points too.
 */
VMMR3DECL(int) CFGMR3QueryBytes(PCFGMNODE pNode, const char *pszName, void *pvData, size_t cbData)
{
    PCFGMLEAF pLeaf;
    int rc = cfgmR3ResolveLeaf(pNode, pszName, &pLeaf);
    if (RT_SUCCESS(rc))
    {
        if (pLeaf->enmType == CFGMVALUETYPE_BYTES)
        {
            if (cbData >= pLeaf->Value.Bytes.cb)
            {
                memcpy(pvData, pLeaf->Value.Bytes.pau8, pLeaf->Value.Bytes.cb);
                memset((char *)pvData + pLeaf->Value.Bytes.cb, 0, cbData - pLeaf->Value.Bytes.cb);
            }
            else
                rc = VERR_CFGM_NOT_ENOUGH_SPACE;
        }
        else
            rc = VERR_CFGM_NOT_BYTES;
    }
    return rc;
}


/**
 * Query zero terminated character value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of a zero terminate character value.
 * @param   pszString       Where to store the string.
 * @param   cchString       Size of the string buffer. (Includes terminator.)
 *
 * @note    Concurrent calls to this function and CFGMR3QueryPasswordDef are not
 *          supported.
 */
VMMR3DECL(int) CFGMR3QueryPassword(PCFGMNODE pNode, const char *pszName, char *pszString, size_t cchString)
{
    PCFGMLEAF pLeaf;
    int rc = cfgmR3ResolveLeaf(pNode, pszName, &pLeaf);
    if (RT_SUCCESS(rc))
    {
        if (pLeaf->enmType == CFGMVALUETYPE_PASSWORD)
        {
            size_t cbSrc = pLeaf->Value.String.cb;
            if (cchString >= cbSrc)
            {
                RTMemSaferUnscramble(pLeaf->Value.String.psz, cbSrc);
                memcpy(pszString, pLeaf->Value.String.psz, cbSrc);
                memset(pszString + cbSrc, 0, cchString - cbSrc);
                RTMemSaferScramble(pLeaf->Value.String.psz, cbSrc);

                Assert(pszString[cbSrc - 1] == '\0');
            }
            else
                rc = VERR_CFGM_NOT_ENOUGH_SPACE;
        }
        else
            rc = VERR_CFGM_NOT_PASSWORD;
    }
    return rc;
}


/**
 * Query zero terminated character value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of a zero terminate character value.
 * @param   pszString       Where to store the string. This will not be set on overflow error.
 * @param   cchString       Size of the string buffer. (Includes terminator.)
 * @param   pszDef          The default value.
 *
 * @note    Concurrent calls to this function and CFGMR3QueryPassword are not
 *          supported.
 */
VMMR3DECL(int) CFGMR3QueryPasswordDef(PCFGMNODE pNode, const char *pszName, char *pszString, size_t cchString, const char *pszDef)
{
    PCFGMLEAF pLeaf;
    int rc = cfgmR3ResolveLeaf(pNode, pszName, &pLeaf);
    if (RT_SUCCESS(rc))
    {
        if (pLeaf->enmType == CFGMVALUETYPE_PASSWORD)
        {
            size_t cbSrc = pLeaf->Value.String.cb;
            if (cchString >= cbSrc)
            {
                RTMemSaferUnscramble(pLeaf->Value.String.psz, cbSrc);
                memcpy(pszString, pLeaf->Value.String.psz, cbSrc);
                memset(pszString + cbSrc, 0, cchString - cbSrc);
                RTMemSaferScramble(pLeaf->Value.String.psz, cbSrc);

                Assert(pszString[cbSrc - 1] == '\0');
            }
            else
                rc = VERR_CFGM_NOT_ENOUGH_SPACE;
        }
        else
            rc = VERR_CFGM_NOT_PASSWORD;
    }

    if (RT_FAILURE(rc) && rc != VERR_CFGM_NOT_ENOUGH_SPACE)
    {
        size_t cchDef = strlen(pszDef);
        if (cchString > cchDef)
        {
            memcpy(pszString, pszDef, cchDef);
            memset(pszString + cchDef, 0, cchString - cchDef);
            if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
                rc = VINF_SUCCESS;
        }
        else if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
            rc = VERR_CFGM_NOT_ENOUGH_SPACE;
    }

    return rc;
}


/**
 * Validate one level of a configuration node.
 *
 * This replaces the CFGMR3AreChildrenValid and CFGMR3AreValuesValid APIs.
 *
 * @returns VBox status code.
 *
 *          When an error is returned, both VMSetError and AssertLogRelMsgFailed
 *          have been called.  So, all the caller needs to do is to propagate
 *          the error status up to PDM.
 *
 * @param   pNode               The node to validate.
 * @param   pszNode             The node path, always ends with a slash.  Use
 *                              "/" for the root config node.
 * @param   pszValidValues      Patterns describing the valid value names.  See
 *                              RTStrSimplePatternMultiMatch for details on the
 *                              pattern syntax.
 * @param   pszValidNodes       Patterns describing the valid node (key) names.
 *                              See RTStrSimplePatternMultiMatch for details on
 *                              the pattern syntax.
 * @param   pszWho              Who is calling.
 * @param   uInstance           The instance number of the caller.
 */
VMMR3DECL(int) CFGMR3ValidateConfig(PCFGMNODE pNode, const char *pszNode,
                                    const char *pszValidValues, const char *pszValidNodes,
                                    const char *pszWho, uint32_t uInstance)
{
    /* Input validation. */
    AssertPtrNullReturn(pNode, VERR_INVALID_POINTER);
    AssertPtrReturn(pszNode, VERR_INVALID_POINTER);
    Assert(*pszNode && pszNode[strlen(pszNode) - 1] == '/');
    AssertPtrReturn(pszValidValues, VERR_INVALID_POINTER);
    AssertPtrReturn(pszValidNodes, VERR_INVALID_POINTER);
    AssertPtrReturn(pszWho, VERR_INVALID_POINTER);

    if (pNode)
    {
        /*
         * Enumerate the leaves and check them against pszValidValues.
         */
        for (PCFGMLEAF pLeaf = pNode->pFirstLeaf; pLeaf; pLeaf = pLeaf->pNext)
        {
            if (!RTStrSimplePatternMultiMatch(pszValidValues, RTSTR_MAX,
                                              pLeaf->szName, pLeaf->cchName,
                                              NULL))
            {
                AssertLogRelMsgFailed(("%s/%u: Value '%s%s' didn't match '%s'\n",
                                       pszWho, uInstance, pszNode, pLeaf->szName, pszValidValues));
                return VMSetError(pNode->pVM, VERR_CFGM_CONFIG_UNKNOWN_VALUE, RT_SRC_POS,
                                  N_("Unknown configuration value '%s%s' found in the configuration of %s instance #%u"),
                                  pszNode, pLeaf->szName, pszWho, uInstance);
            }

        }

        /*
         * Enumerate the child nodes and check them against pszValidNodes.
         */
        for (PCFGMNODE pChild = pNode->pFirstChild; pChild; pChild = pChild->pNext)
        {
            if (!RTStrSimplePatternMultiMatch(pszValidNodes, RTSTR_MAX,
                                              pChild->szName, pChild->cchName,
                                              NULL))
            {
                AssertLogRelMsgFailed(("%s/%u: Node '%s%s' didn't match '%s'\n",
                                       pszWho, uInstance, pszNode, pChild->szName, pszValidNodes));
                return VMSetError(pNode->pVM, VERR_CFGM_CONFIG_UNKNOWN_NODE, RT_SRC_POS,
                                  N_("Unknown configuration node '%s%s' found in the configuration of %s instance #%u"),
                                  pszNode, pChild->szName, pszWho, uInstance);
            }
        }
    }

    /* All is well. */
    return VINF_SUCCESS;
}



/**
 * Populates the CFGM tree with the default configuration.
 *
 * This assumes an empty tree and is intended for testcases and such that only
 * need to do very small adjustments to the config.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 * @internal
 */
VMMR3DECL(int) CFGMR3ConstructDefaultTree(PVM pVM)
{
    int rc;
    int rcAll = VINF_SUCCESS;
#define UPDATERC() do { if (RT_FAILURE(rc) && RT_SUCCESS(rcAll)) rcAll = rc; } while (0)

    PCFGMNODE pRoot = CFGMR3GetRoot(pVM);
    AssertReturn(pRoot, VERR_WRONG_ORDER);

    /*
     * Create VM default values.
     */
    rc = CFGMR3InsertString(pRoot,  "Name",                 "Default VM");
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "RamSize",              128U * _1M);
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "RamHoleSize",          512U * _1M);
    UPDATERC();
    rc = CFGMR3InsertInteger(pRoot, "TimerMillies",         10);
    UPDATERC();

    /*
     * HM.
     */
    PCFGMNODE pHm;
    rc = CFGMR3InsertNode(pRoot, "HM", &pHm);
    UPDATERC();
    rc = CFGMR3InsertInteger(pHm, "FallbackToIEM",          1); /* boolean */
    UPDATERC();


    /*
     * PDM.
     */
    PCFGMNODE pPdm;
    rc = CFGMR3InsertNode(pRoot, "PDM", &pPdm);
    UPDATERC();
    PCFGMNODE pDevices = NULL;
    rc = CFGMR3InsertNode(pPdm, "Devices", &pDevices);
    UPDATERC();
    rc = CFGMR3InsertInteger(pDevices, "LoadBuiltin",       1); /* boolean */
    UPDATERC();
    PCFGMNODE pDrivers = NULL;
    rc = CFGMR3InsertNode(pPdm, "Drivers", &pDrivers);
    UPDATERC();
    rc = CFGMR3InsertInteger(pDrivers, "LoadBuiltin",       1); /* boolean */
    UPDATERC();


    /*
     * Devices
     */
    pDevices = NULL;
    rc = CFGMR3InsertNode(pRoot, "Devices", &pDevices);
    UPDATERC();
    /* device */
    PCFGMNODE pDev = NULL;
    PCFGMNODE pInst = NULL;
    PCFGMNODE pCfg = NULL;
#if 0
    PCFGMNODE pLunL0 = NULL;
    PCFGMNODE pLunL1 = NULL;
#endif

    /*
     * PC Arch.
     */
    rc = CFGMR3InsertNode(pDevices, "pcarch", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();

    /*
     * PC Bios.
     */
    rc = CFGMR3InsertNode(pDevices, "pcbios", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "BootDevice0",          "IDE");
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "BootDevice1",          "NONE");
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "BootDevice2",          "NONE");
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "BootDevice3",          "NONE");
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "HardDiskDevice",       "piix3ide");
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "FloppyDevice",         "");
    UPDATERC();
    RTUUID Uuid;
    RTUuidClear(&Uuid);
    rc = CFGMR3InsertBytes(pCfg,    "UUID", &Uuid, sizeof(Uuid));
    UPDATERC();

    /*
     * PCI bus.
     */
    rc = CFGMR3InsertNode(pDevices, "pci", &pDev); /* piix3 */
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();

    /*
     * PS/2 keyboard & mouse
     */
    rc = CFGMR3InsertNode(pDevices, "pckbd", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();
#if 0
    rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);
    UPDATERC();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "KeyboardQueue");
    UPDATERC();
    rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);
    UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "QueueSize",            64);
    UPDATERC();
    rc = CFGMR3InsertNode(pLunL0,   "AttachedDriver", &pLunL1);
    UPDATERC();
    rc = CFGMR3InsertString(pLunL1, "Driver",               "MainKeyboard");
    UPDATERC();
    rc = CFGMR3InsertNode(pLunL1,   "Config", &pCfg);
    UPDATERC();
#endif
#if 0
    rc = CFGMR3InsertNode(pInst,    "LUN#1", &pLunL0);
    UPDATERC();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "MouseQueue");
    UPDATERC();
    rc = CFGMR3InsertNode(pLunL0,   "Config", &pCfg);
    UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "QueueSize",            128);
    UPDATERC();
    rc = CFGMR3InsertNode(pLunL0,   "AttachedDriver", &pLunL1);
    UPDATERC();
    rc = CFGMR3InsertString(pLunL1, "Driver",               "MainMouse");
    UPDATERC();
    rc = CFGMR3InsertNode(pLunL1,   "Config", &pCfg);
    UPDATERC();
#endif

    /*
     * i8254 Programmable Interval Timer And Dummy Speaker
     */
    rc = CFGMR3InsertNode(pDevices, "i8254", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
#ifdef DEBUG
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
#endif
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();

    /*
     * i8259 Programmable Interrupt Controller.
     */
    rc = CFGMR3InsertNode(pDevices, "i8259", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();

    /*
     * RTC MC146818.
     */
    rc = CFGMR3InsertNode(pDevices, "mc146818", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();

    /*
     * VGA.
     */
    rc = CFGMR3InsertNode(pDevices, "vga", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "VRamSize",             4 * _1M);
    UPDATERC();

    /* Bios logo. */
    rc = CFGMR3InsertInteger(pCfg,  "FadeIn",               1);
    UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "FadeOut",              1);
    UPDATERC();
    rc = CFGMR3InsertInteger(pCfg,  "LogoTime",             0);
    UPDATERC();
    rc = CFGMR3InsertString(pCfg,   "LogoFile",             "");
    UPDATERC();

#if 0
    rc = CFGMR3InsertNode(pInst,    "LUN#0", &pLunL0);
    UPDATERC();
    rc = CFGMR3InsertString(pLunL0, "Driver",               "MainDisplay");
    UPDATERC();
#endif

    /*
     * IDE controller.
     */
    rc = CFGMR3InsertNode(pDevices, "piix3ide", &pDev); /* piix3 */
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1);         /* boolean */
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();

    /*
     * VMMDev.
     */
    rc = CFGMR3InsertNode(pDevices, "VMMDev", &pDev);
    UPDATERC();
    rc = CFGMR3InsertNode(pDev,     "0", &pInst);
    UPDATERC();
    rc = CFGMR3InsertNode(pInst,    "Config", &pCfg);
    UPDATERC();
    rc = CFGMR3InsertInteger(pInst, "Trusted",              1); /* boolean */
    UPDATERC();


    /*
     * ...
     */

#undef UPDATERC
    return rcAll;
}




/**
 * Resolves a path reference to a child node.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszPath         Path to the child node.
 * @param   ppChild         Where to store the pointer to the child node.
 */
static int cfgmR3ResolveNode(PCFGMNODE pNode, const char *pszPath, PCFGMNODE *ppChild)
{
    *ppChild = NULL;
    if (!pNode)
        return VERR_CFGM_NO_PARENT;
    PCFGMNODE pChild = NULL;
    for (;;)
    {
        /* skip leading slashes. */
        while (*pszPath == '/')
            pszPath++;

        /* End of path? */
        if (!*pszPath)
        {
            if (!pChild)
                return VERR_CFGM_INVALID_CHILD_PATH;
            *ppChild = pChild;
            return VINF_SUCCESS;
        }

        /* find end of component. */
        const char *pszNext = strchr(pszPath, '/');
        if (!pszNext)
            pszNext = strchr(pszPath,  '\0');
        RTUINT cchName = pszNext - pszPath;

        /* search child list. */
        pChild = pNode->pFirstChild;
        for ( ; pChild; pChild = pChild->pNext)
            if (pChild->cchName == cchName)
            {
                int iDiff = memcmp(pszPath, pChild->szName, cchName);
                if (iDiff <= 0)
                {
                    if (iDiff != 0)
                        pChild = NULL;
                    break;
                }
            }
        if (!pChild)
            return VERR_CFGM_CHILD_NOT_FOUND;

        /* next iteration */
        pNode = pChild;
        pszPath = pszNext;
    }

    /* won't get here */
}


/**
 * Resolves a path reference to a child node.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of a byte string value.
 * @param   ppLeaf          Where to store the pointer to the leaf node.
 */
static int cfgmR3ResolveLeaf(PCFGMNODE pNode, const char *pszName, PCFGMLEAF *ppLeaf)
{
    *ppLeaf = NULL;
    if (!pNode)
        return VERR_CFGM_NO_PARENT;

    size_t      cchName = strlen(pszName);
    PCFGMLEAF   pLeaf   = pNode->pFirstLeaf;
    while (pLeaf)
    {
        if (cchName == pLeaf->cchName)
        {
            int iDiff = memcmp(pszName, pLeaf->szName, cchName);
            if (iDiff <= 0)
            {
                if (iDiff != 0)
                    break;
                *ppLeaf = pLeaf;
                return VINF_SUCCESS;
            }
        }

        /* next */
        pLeaf = pLeaf->pNext;
    }
    return VERR_CFGM_VALUE_NOT_FOUND;
}



/**
 * Creates a CFGM tree.
 *
 * This is intended for creating device/driver configs can be
 * passed around and later attached to the main tree in the
 * correct location.
 *
 * @returns Pointer to the root node, NULL on error (out of memory or invalid
 *          VM handle).
 * @param   pUVM            The user mode VM handle.  For testcase (and other
 *                          purposes, NULL can be used.  However, the resulting
 *                          tree cannot be inserted into a tree that has a
 *                          non-NULL value.  Using NULL can be usedful for
 *                          testcases and similar, non VMM uses.
 */
VMMR3DECL(PCFGMNODE) CFGMR3CreateTree(PUVM pUVM)
{
    if (pUVM)
    {
        UVM_ASSERT_VALID_EXT_RETURN(pUVM, NULL);
        VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, NULL);
    }

    PCFGMNODE pNew;
    if (pUVM)
        pNew = (PCFGMNODE)MMR3HeapAllocU(pUVM, MM_TAG_CFGM, sizeof(*pNew));
    else
        pNew = (PCFGMNODE)RTMemAlloc(sizeof(*pNew));
    if (pNew)
    {
        pNew->pPrev         = NULL;
        pNew->pNext         = NULL;
        pNew->pParent       = NULL;
        pNew->pFirstChild   = NULL;
        pNew->pFirstLeaf    = NULL;
        pNew->pVM           = pUVM ? pUVM->pVM : NULL;
        pNew->fRestrictedRoot = false;
        pNew->cchName       = 0;
        pNew->szName[0]     = 0;
    }
    return pNew;
}


/**
 * Duplicates a CFGM sub-tree or a full tree.
 *
 * @returns Pointer to the root node.  NULL if we run out of memory or the
 *          input parameter is NULL.
 * @param   pRoot       The root of the tree to duplicate.
 * @param   ppCopy      Where to return the root of the duplicate.
 */
VMMR3DECL(int) CFGMR3DuplicateSubTree(PCFGMNODE pRoot, PCFGMNODE *ppCopy)
{
    AssertPtrReturn(pRoot, VERR_INVALID_POINTER);

    /*
     * Create a new tree.
     */
    PCFGMNODE pNewRoot = CFGMR3CreateTree(pRoot->pVM ? pRoot->pVM->pUVM : NULL);
    if (!pNewRoot)
        return VERR_NO_MEMORY;

    /*
     * Duplicate the content.
     */
    int         rc      = VINF_SUCCESS;
    PCFGMNODE   pSrcCur = pRoot;
    PCFGMNODE   pDstCur = pNewRoot;
    for (;;)
    {
        if (   !pDstCur->pFirstChild
            && !pDstCur->pFirstLeaf)
        {
            /*
             * Values first.
             */
            /** @todo this isn't the most efficient way to do it. */
            for (PCFGMLEAF pLeaf = pSrcCur->pFirstLeaf; pLeaf && RT_SUCCESS(rc); pLeaf = pLeaf->pNext)
                rc = CFGMR3InsertValue(pDstCur, pLeaf);

            /*
             * Insert immediate child nodes.
             */
            /** @todo this isn't the most efficient way to do it. */
            for (PCFGMNODE pChild = pSrcCur->pFirstChild; pChild && RT_SUCCESS(rc); pChild = pChild->pNext)
                rc = CFGMR3InsertNode(pDstCur, pChild->szName, NULL);

            AssertLogRelRCBreak(rc);
        }

        /*
         * Deep copy of the children.
         */
        if (pSrcCur->pFirstChild)
        {
            Assert(pDstCur->pFirstChild && !strcmp(pDstCur->pFirstChild->szName, pSrcCur->pFirstChild->szName));
            pSrcCur = pSrcCur->pFirstChild;
            pDstCur = pDstCur->pFirstChild;
        }
        /*
         * If it's the root node, we're done.
         */
        else if (pSrcCur == pRoot)
            break;
        else
        {
            /*
             * Upon reaching the end of a sibling list, we must ascend and
             * resume the sibiling walk on an previous level.
             */
            if (!pSrcCur->pNext)
            {
                do
                {
                    pSrcCur = pSrcCur->pParent;
                    pDstCur = pDstCur->pParent;
                } while (!pSrcCur->pNext && pSrcCur != pRoot);
                if (pSrcCur == pRoot)
                    break;
            }

            /*
             * Next sibling.
             */
            Assert(pDstCur->pNext && !strcmp(pDstCur->pNext->szName, pSrcCur->pNext->szName));
            pSrcCur = pSrcCur->pNext;
            pDstCur = pDstCur->pNext;
        }
    }

    if (RT_FAILURE(rc))
    {
        CFGMR3RemoveNode(pNewRoot);
        return rc;
    }

    *ppCopy = pNewRoot;
    return VINF_SUCCESS;
}


/**
 * Insert subtree.
 *
 * This function inserts (no duplication) a tree created by CFGMR3CreateTree()
 * into the main tree.
 *
 * The root node of the inserted subtree will need to be reallocated, which
 * effectually means that the passed in pSubTree handle becomes invalid
 * upon successful return. Use the value returned in ppChild instead
 * of pSubTree.
 *
 * @returns VBox status code.
 * @returns VERR_CFGM_NODE_EXISTS if the final child node name component exists.
 * @param   pNode       Parent node.
 * @param   pszName     Name or path of the new child node.
 * @param   pSubTree    The subtree to insert. Must be returned by CFGMR3CreateTree().
 * @param   ppChild     Where to store the address of the new child node. (optional)
 */
VMMR3DECL(int) CFGMR3InsertSubTree(PCFGMNODE pNode, const char *pszName, PCFGMNODE pSubTree, PCFGMNODE *ppChild)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pNode, VERR_INVALID_POINTER);
    AssertPtrReturn(pSubTree, VERR_INVALID_POINTER);
    AssertReturn(pNode != pSubTree, VERR_INVALID_PARAMETER);
    AssertReturn(!pSubTree->pParent, VERR_INVALID_PARAMETER);
    AssertReturn(pNode->pVM == pSubTree->pVM, VERR_INVALID_PARAMETER);
    Assert(!pSubTree->pNext);
    Assert(!pSubTree->pPrev);

    /*
     * Use CFGMR3InsertNode to create a new node and then
     * re-attach the children and leaves of the subtree to it.
     */
    PCFGMNODE pNewChild;
    int rc = CFGMR3InsertNode(pNode, pszName, &pNewChild);
    if (RT_SUCCESS(rc))
    {
        Assert(!pNewChild->pFirstChild);
        Assert(!pNewChild->pFirstLeaf);

        pNewChild->pFirstChild = pSubTree->pFirstChild;
        pNewChild->pFirstLeaf = pSubTree->pFirstLeaf;
        for (PCFGMNODE pChild = pNewChild->pFirstChild; pChild; pChild = pChild->pNext)
            pChild->pParent = pNewChild;

        if (ppChild)
            *ppChild = pNewChild;

        /* free the old subtree root */
        cfgmR3FreeNodeOnly(pSubTree);
    }
    return rc;
}


/**
 * Replaces a (sub-)tree with new one.
 *
 * This function removes the exiting (sub-)tree, completely freeing it in the
 * process, and inserts (no duplication) the specified tree.  The tree can
 * either be created by CFGMR3CreateTree or CFGMR3DuplicateSubTree.
 *
 * @returns VBox status code.
 * @param   pRoot       The sub-tree to replace.  This node will remain valid
 *                      after the call.
 * @param   pNewRoot    The tree to replace @a pRoot with.  This not will
 *                      become invalid after a successful call.
 */
VMMR3DECL(int) CFGMR3ReplaceSubTree(PCFGMNODE pRoot, PCFGMNODE pNewRoot)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pRoot, VERR_INVALID_POINTER);
    AssertPtrReturn(pNewRoot, VERR_INVALID_POINTER);
    AssertReturn(pRoot != pNewRoot, VERR_INVALID_PARAMETER);
    AssertReturn(!pNewRoot->pParent, VERR_INVALID_PARAMETER);
    AssertReturn(pNewRoot->pVM == pRoot->pVM, VERR_INVALID_PARAMETER);
    AssertReturn(!pNewRoot->pNext, VERR_INVALID_PARAMETER);
    AssertReturn(!pNewRoot->pPrev, VERR_INVALID_PARAMETER);

    /*
     * Free the current properties fo pRoot.
     */
    while (pRoot->pFirstChild)
        CFGMR3RemoveNode(pRoot->pFirstChild);

    while (pRoot->pFirstLeaf)
        cfgmR3RemoveLeaf(pRoot, pRoot->pFirstLeaf);

    /*
     * Copy all the properties from the new root to the current one.
     */
    pRoot->pFirstLeaf       = pNewRoot->pFirstLeaf;
    pRoot->pFirstChild      = pNewRoot->pFirstChild;
    for (PCFGMNODE pChild = pRoot->pFirstChild; pChild; pChild = pChild->pNext)
        pChild->pParent     = pRoot;

    cfgmR3FreeNodeOnly(pNewRoot);

    return VINF_SUCCESS;
}


/**
 * Copies all values and keys from one tree onto another.
 *
 * The flags control what happens to keys and values with the same name
 * existing in both source and destination.
 *
 * @returns VBox status code.
 * @param   pDstTree            The destination tree.
 * @param   pSrcTree            The source tree.
 * @param   fFlags              Copy flags, see CFGM_COPY_FLAGS_XXX.
 */
VMMR3DECL(int) CFGMR3CopyTree(PCFGMNODE pDstTree, PCFGMNODE pSrcTree, uint32_t fFlags)
{
    /*
     * Input validation.
     */
    AssertPtrReturn(pSrcTree, VERR_INVALID_POINTER);
    AssertPtrReturn(pDstTree, VERR_INVALID_POINTER);
    AssertReturn(pDstTree != pSrcTree, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & ~(CFGM_COPY_FLAGS_VALUE_DISP_MASK | CFGM_COPY_FLAGS_KEY_DISP_MASK)), VERR_INVALID_PARAMETER);
    AssertReturn(   (fFlags & CFGM_COPY_FLAGS_VALUE_DISP_MASK) != CFGM_COPY_FLAGS_RESERVED_VALUE_DISP_0
                 && (fFlags & CFGM_COPY_FLAGS_VALUE_DISP_MASK) != CFGM_COPY_FLAGS_RESERVED_VALUE_DISP_1,
                 VERR_INVALID_PARAMETER);
    AssertReturn((fFlags & CFGM_COPY_FLAGS_KEY_DISP_MASK) != CFGM_COPY_FLAGS_RESERVED_KEY_DISP,
                 VERR_INVALID_PARAMETER);

    /*
     * Copy the values.
     */
    int rc;
    for (PCFGMLEAF pValue = CFGMR3GetFirstValue(pSrcTree); pValue; pValue = CFGMR3GetNextValue(pValue))
    {
        rc = CFGMR3InsertValue(pDstTree, pValue);
        if (rc == VERR_CFGM_LEAF_EXISTS)
        {
            if ((fFlags & CFGM_COPY_FLAGS_VALUE_DISP_MASK) == CFGM_COPY_FLAGS_REPLACE_VALUES)
            {
                rc = CFGMR3RemoveValue(pDstTree, pValue->szName);
                if (RT_FAILURE(rc))
                    break;
                rc = CFGMR3InsertValue(pDstTree, pValue);
            }
            else
                rc = VINF_SUCCESS;
        }
        AssertRCReturn(rc, rc);
    }

    /*
     * Copy/merge the keys - merging results in recursion.
     */
    for (PCFGMNODE pSrcChild = CFGMR3GetFirstChild(pSrcTree); pSrcChild; pSrcChild = CFGMR3GetNextChild(pSrcChild))
    {
        PCFGMNODE pDstChild = CFGMR3GetChild(pDstTree, pSrcChild->szName);
        if (   pDstChild
            && (fFlags & CFGM_COPY_FLAGS_KEY_DISP_MASK) == CFGM_COPY_FLAGS_REPLACE_KEYS)
        {
            CFGMR3RemoveNode(pDstChild);
            pDstChild = NULL;
        }
        if (!pDstChild)
        {
            PCFGMNODE pChildCopy;
            rc = CFGMR3DuplicateSubTree(pSrcChild, &pChildCopy);
            AssertRCReturn(rc, rc);
            rc = CFGMR3InsertSubTree(pDstTree, pSrcChild->szName, pChildCopy, NULL);
            AssertRCReturnStmt(rc, CFGMR3RemoveNode(pChildCopy), rc);
        }
        else if ((fFlags & CFGM_COPY_FLAGS_KEY_DISP_MASK) == CFGM_COPY_FLAGS_MERGE_KEYS)
        {
            rc = CFGMR3CopyTree(pDstChild, pSrcChild, fFlags);
            AssertRCReturn(rc, rc);
        }
    }

    return VINF_SUCCESS;
}



/**
 * Compares two names.
 *
 * @returns Similar to memcpy.
 * @param   pszName1            The first name.
 * @param   cchName1            The length of the first name.
 * @param   pszName2            The second name.
 * @param   cchName2            The length of the second name.
 */
DECLINLINE(int) cfgmR3CompareNames(const char *pszName1, size_t cchName1, const char *pszName2, size_t cchName2)
{
    int iDiff;
    if (cchName1 <= cchName2)
    {
        iDiff = memcmp(pszName1, pszName2, cchName1);
        if (!iDiff && cchName1 < cchName2)
            iDiff = -1;
    }
    else
    {
        iDiff = memcmp(pszName1, pszName2, cchName2);
        if (!iDiff)
            iDiff = 1;
    }
    return iDiff;
}


/**
 * Insert a node.
 *
 * @returns VBox status code.
 * @returns VERR_CFGM_NODE_EXISTS if the final child node name component exists.
 * @param   pNode       Parent node.
 * @param   pszName     Name or path of the new child node.
 * @param   ppChild     Where to store the address of the new child node. (optional)
 */
VMMR3DECL(int) CFGMR3InsertNode(PCFGMNODE pNode, const char *pszName, PCFGMNODE *ppChild)
{
    int rc;
    if (pNode)
    {
        /*
         * If given a path we have to deal with it component by component.
         */
        while (*pszName == '/')
            pszName++;
        if (strchr(pszName, '/'))
        {
            char *pszDup = RTStrDup(pszName);
            if (pszDup)
            {
                char *psz = pszDup;
                for (;;)
                {
                    /* Terminate at '/' and find the next component. */
                    char *pszNext = strchr(psz, '/');
                    if (pszNext)
                    {
                        *pszNext++ = '\0';
                        while (*pszNext == '/')
                            pszNext++;
                        if (*pszNext == '\0')
                            pszNext = NULL;
                    }

                    /* does it exist? */
                    PCFGMNODE pChild = CFGMR3GetChild(pNode, psz);
                    if (!pChild)
                    {
                        /* no, insert it */
                        rc = CFGMR3InsertNode(pNode, psz, &pChild);
                        if (RT_FAILURE(rc))
                            break;
                        if (!pszNext)
                        {
                            if (ppChild)
                                *ppChild = pChild;
                            break;
                        }

                    }
                    /* if last component fail */
                    else if (!pszNext)
                    {
                        rc = VERR_CFGM_NODE_EXISTS;
                        break;
                    }

                    /* next */
                    pNode = pChild;
                    psz = pszNext;
                }
                RTStrFree(pszDup);
            }
            else
                rc = VERR_NO_TMP_MEMORY;
        }
        /*
         * Not multicomponent, just make sure it's a non-zero name.
         */
        else if (*pszName)
        {
            /*
             * Check if already exists and find last node in chain.
             */
            size_t    cchName = strlen(pszName);
            PCFGMNODE pPrev   = NULL;
            PCFGMNODE pNext   = pNode->pFirstChild;
            if (pNext)
            {
                for ( ; pNext; pPrev = pNext, pNext = pNext->pNext)
                {
                    int iDiff = cfgmR3CompareNames(pszName, cchName, pNext->szName, pNext->cchName);
                    if (iDiff <= 0)
                    {
                        if (!iDiff)
                            return VERR_CFGM_NODE_EXISTS;
                        break;
                    }
                }
            }

            /*
             * Allocate and init node.
             */
            PCFGMNODE pNew = (PCFGMNODE)cfgmR3MemAlloc(pNode->pVM, MM_TAG_CFGM, sizeof(*pNew) + cchName);
            if (pNew)
            {
                pNew->pParent       = pNode;
                pNew->pFirstChild   = NULL;
                pNew->pFirstLeaf    = NULL;
                pNew->pVM           = pNode->pVM;
                pNew->fRestrictedRoot = false;
                pNew->cchName       = cchName;
                memcpy(pNew->szName, pszName, cchName + 1);

                /*
                 * Insert into child list.
                 */
                pNew->pPrev         = pPrev;
                if (pPrev)
                    pPrev->pNext    = pNew;
                else
                    pNode->pFirstChild = pNew;
                pNew->pNext         = pNext;
                if (pNext)
                    pNext->pPrev    = pNew;

                if (ppChild)
                    *ppChild = pNew;
                rc = VINF_SUCCESS;
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
        {
            rc = VERR_CFGM_INVALID_NODE_PATH;
            AssertMsgFailed(("Invalid path %s\n", pszName));
        }
    }
    else
    {
        rc = VERR_CFGM_NO_PARENT;
        AssertMsgFailed(("No parent! path %s\n", pszName));
    }

    return rc;
}


/**
 * Insert a node, format string name.
 *
 * @returns VBox status     code.
 * @param   pNode           Parent node.
 * @param   ppChild         Where to store the address of the new child node. (optional)
 * @param   pszNameFormat   Name of or path the new child node.
 * @param   ...             Name format arguments.
 */
VMMR3DECL(int) CFGMR3InsertNodeF(PCFGMNODE pNode, PCFGMNODE *ppChild, const char *pszNameFormat, ...)
{
    va_list Args;
    va_start(Args,  pszNameFormat);
    int rc = CFGMR3InsertNodeFV(pNode, ppChild, pszNameFormat, Args);
    va_end(Args);
    return rc;
}


/**
 * Insert a node, format string name.
 *
 * @returns VBox status code.
 * @param   pNode           Parent node.
 * @param   ppChild         Where to store the address of the new child node. (optional)
 * @param   pszNameFormat   Name or path of the new child node.
 * @param   Args            Name format arguments.
 */
VMMR3DECL(int) CFGMR3InsertNodeFV(PCFGMNODE pNode, PCFGMNODE *ppChild, const char *pszNameFormat, va_list Args)
{
    int     rc;
    char   *pszName;
    RTStrAPrintfV(&pszName, pszNameFormat, Args);
    if (pszName)
    {
        rc = CFGMR3InsertNode(pNode, pszName, ppChild);
        RTStrFree(pszName);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Marks the node as the root of a restricted subtree, i.e. the end of
 * a CFGMR3GetParent() journey.
 *
 * @param   pNode       The node to mark.
 */
VMMR3DECL(void) CFGMR3SetRestrictedRoot(PCFGMNODE pNode)
{
    if (pNode)
        pNode->fRestrictedRoot = true;
}


/**
 * Insert a node.
 *
 * @returns VBox status code.
 * @param   pNode       Parent node.
 * @param   pszName     Name of the new child node.
 * @param   ppLeaf      Where to store the new leaf.
 *                      The caller must fill in the enmType and Value fields!
 */
static int cfgmR3InsertLeaf(PCFGMNODE pNode, const char *pszName, PCFGMLEAF *ppLeaf)
{
    int rc;
    if (*pszName)
    {
        if (pNode)
        {
            /*
             * Check if already exists and find last node in chain.
             */
            size_t    cchName  = strlen(pszName);
            PCFGMLEAF pPrev    = NULL;
            PCFGMLEAF pNext    = pNode->pFirstLeaf;
            if (pNext)
            {
                for ( ; pNext; pPrev = pNext, pNext = pNext->pNext)
                {
                    int iDiff = cfgmR3CompareNames(pszName, cchName, pNext->szName, pNext->cchName);
                    if (iDiff <= 0)
                    {
                        if (!iDiff)
                            return VERR_CFGM_LEAF_EXISTS;
                        break;
                    }
                }
            }

            /*
             * Allocate and init node.
             */
            PCFGMLEAF pNew = (PCFGMLEAF)cfgmR3MemAlloc(pNode->pVM, MM_TAG_CFGM, sizeof(*pNew) + cchName);
            if (pNew)
            {
                pNew->cchName       = cchName;
                memcpy(pNew->szName, pszName, cchName + 1);

                /*
                 * Insert into child list.
                 */
                pNew->pPrev         = pPrev;
                if (pPrev)
                    pPrev->pNext    = pNew;
                else
                    pNode->pFirstLeaf = pNew;
                pNew->pNext         = pNext;
                if (pNext)
                    pNext->pPrev    = pNew;

                *ppLeaf = pNew;
                rc = VINF_SUCCESS;
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_CFGM_NO_PARENT;
    }
    else
        rc = VERR_CFGM_INVALID_CHILD_PATH;
    return rc;
}


/**
 * Removes a node.
 *
 * @param   pNode       The node to remove.
 */
VMMR3DECL(void) CFGMR3RemoveNode(PCFGMNODE pNode)
{
    if (pNode)
    {
        /*
         * Free children.
         */
        while (pNode->pFirstChild)
            CFGMR3RemoveNode(pNode->pFirstChild);

        /*
         * Free leaves.
         */
        while (pNode->pFirstLeaf)
            cfgmR3RemoveLeaf(pNode, pNode->pFirstLeaf);

        /*
         * Unlink ourselves.
         */
        if (pNode->pPrev)
            pNode->pPrev->pNext = pNode->pNext;
        else
        {
            if (pNode->pParent)
                pNode->pParent->pFirstChild = pNode->pNext;
            else if (   pNode->pVM                         /* might be a different tree */
                     && pNode == pNode->pVM->cfgm.s.pRoot)
                pNode->pVM->cfgm.s.pRoot = NULL;
        }
        if (pNode->pNext)
            pNode->pNext->pPrev = pNode->pPrev;

        /*
         * Free ourselves.
         */
        cfgmR3FreeNodeOnly(pNode);
    }
}


/**
 * Removes a leaf.
 *
 * @param   pNode   Parent node.
 * @param   pLeaf   Leaf to remove.
 */
static void cfgmR3RemoveLeaf(PCFGMNODE pNode, PCFGMLEAF pLeaf)
{
    if (pNode && pLeaf)
    {
        /*
         * Unlink.
         */
        if (pLeaf->pPrev)
            pLeaf->pPrev->pNext = pLeaf->pNext;
        else
            pNode->pFirstLeaf = pLeaf->pNext;
        if (pLeaf->pNext)
            pLeaf->pNext->pPrev = pLeaf->pPrev;

        /*
         * Free value and node.
         */
        cfgmR3FreeValue(pNode->pVM, pLeaf);
        pLeaf->pNext = NULL;
        pLeaf->pPrev = NULL;
        cfgmR3MemFree(pNode->pVM, pLeaf);
    }
}


/**
 * Frees whatever resources the leaf value is owning.
 *
 * Use this before assigning a new value to a leaf.
 * The caller must either free the leaf or assign a new value to it.
 *
 * @param   pVM         The cross context VM structure, if the tree
 *                      is associated with one.
 * @param   pLeaf       Pointer to the leaf which value should be free.
 */
static void cfgmR3FreeValue(PVM pVM, PCFGMLEAF pLeaf)
{
    if (pLeaf)
    {
        switch (pLeaf->enmType)
        {
            case CFGMVALUETYPE_BYTES:
                cfgmR3MemFree(pVM, pLeaf->Value.Bytes.pau8);
                pLeaf->Value.Bytes.pau8 = NULL;
                pLeaf->Value.Bytes.cb = 0;
                break;

            case CFGMVALUETYPE_STRING:
                cfgmR3StrFree(pVM, pLeaf->Value.String.psz);
                pLeaf->Value.String.psz = NULL;
                pLeaf->Value.String.cb = 0;
                break;

            case CFGMVALUETYPE_PASSWORD:
                RTMemSaferFree(pLeaf->Value.String.psz, pLeaf->Value.String.cb);
                pLeaf->Value.String.psz = NULL;
                pLeaf->Value.String.cb = 0;
                break;

            case CFGMVALUETYPE_INTEGER:
                break;
        }
        pLeaf->enmType = (CFGMVALUETYPE)0;
    }
}

/**
 * Destroys a tree created with CFGMR3CreateTree or CFGMR3DuplicateSubTree.
 *
 * @returns VBox status code.
 * @param   pRoot       The root node of the tree.
 */
VMMR3DECL(int) CFGMR3DestroyTree(PCFGMNODE pRoot)
{
    if (!pRoot)
        return VINF_SUCCESS;
    AssertReturn(!pRoot->pParent, VERR_INVALID_PARAMETER);
    AssertReturn(!pRoot->pVM || pRoot != pRoot->pVM->cfgm.s.pRoot, VERR_ACCESS_DENIED);

    CFGMR3RemoveNode(pRoot);
    return VINF_SUCCESS;
}


/**
 * Inserts a new integer value.
 *
 * @returns VBox status code.
 * @param   pNode           Parent node.
 * @param   pszName         Value name.
 * @param   u64Integer      The value.
 */
VMMR3DECL(int) CFGMR3InsertInteger(PCFGMNODE pNode, const char *pszName, uint64_t u64Integer)
{
    PCFGMLEAF pLeaf;
    int rc = cfgmR3InsertLeaf(pNode, pszName, &pLeaf);
    if (RT_SUCCESS(rc))
    {
        pLeaf->enmType = CFGMVALUETYPE_INTEGER;
        pLeaf->Value.Integer.u64 = u64Integer;
    }
    return rc;
}


/**
 * Inserts a new string value.
 *
 * This variant expects that the caller know the length of the string already so
 * we can avoid calling strlen() here.
 *
 * @returns VBox status code.
 * @param   pNode           Parent node.
 * @param   pszName         Value name.
 * @param   pszString       The value. Must not be NULL.
 * @param   cchString       The length of the string excluding the
 *                          terminator.
 */
VMMR3DECL(int) CFGMR3InsertStringN(PCFGMNODE pNode, const char *pszName, const char *pszString, size_t cchString)
{
    Assert(RTStrNLen(pszString, cchString) == cchString);

    int rc;
    if (pNode)
    {
        /*
         * Allocate string object first.
         */
        char *pszStringCopy = (char *)cfgmR3StrAlloc(pNode->pVM, MM_TAG_CFGM_STRING, cchString + 1);
        if (pszStringCopy)
        {
            memcpy(pszStringCopy, pszString, cchString);
            pszStringCopy[cchString] = '\0';

            /*
             * Create value leaf and set it to string type.
             */
            PCFGMLEAF pLeaf;
            rc = cfgmR3InsertLeaf(pNode, pszName, &pLeaf);
            if (RT_SUCCESS(rc))
            {
                pLeaf->enmType = CFGMVALUETYPE_STRING;
                pLeaf->Value.String.psz = pszStringCopy;
                pLeaf->Value.String.cb  = cchString + 1;
            }
            else
                cfgmR3StrFree(pNode->pVM, pszStringCopy);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_CFGM_NO_PARENT;

    return rc;
}


/**
 * Inserts a new string value.
 *
 * Calls strlen(pszString) internally; if you know the length of the string,
 * CFGMR3InsertStringLengthKnown() is faster.
 *
 * @returns VBox status code.
 * @param   pNode           Parent node.
 * @param   pszName         Value name.
 * @param   pszString       The value.
 */
VMMR3DECL(int) CFGMR3InsertString(PCFGMNODE pNode, const char *pszName, const char *pszString)
{
    return CFGMR3InsertStringN(pNode, pszName, pszString, strlen(pszString));
}


/**
 * Same as CFGMR3InsertString except the string value given in RTStrPrintfV
 * fashion.
 *
 * @returns VBox status code.
 * @param   pNode           Parent node.
 * @param   pszName         Value name.
 * @param   pszFormat       The value given as a format string.
 * @param   va              Argument to pszFormat.
 */
VMMR3DECL(int) CFGMR3InsertStringFV(PCFGMNODE pNode, const char *pszName, const char *pszFormat, va_list va)
{
    int rc;
    if (pNode)
    {
        /*
         * Allocate string object first.
         */
        char *pszString;
        if (!pNode->pVM)
            pszString = RTStrAPrintf2(pszFormat, va);
        else
            pszString = MMR3HeapAPrintfVU(pNode->pVM->pUVM, MM_TAG_CFGM_STRING, pszFormat, va);
        if (pszString)
        {
            /*
             * Create value leaf and set it to string type.
             */
            PCFGMLEAF pLeaf;
            rc = cfgmR3InsertLeaf(pNode, pszName, &pLeaf);
            if (RT_SUCCESS(rc))
            {
                pLeaf->enmType = CFGMVALUETYPE_STRING;
                pLeaf->Value.String.psz = pszString;
                pLeaf->Value.String.cb  = strlen(pszString) + 1;
            }
            else
                cfgmR3StrFree(pNode->pVM, pszString);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_CFGM_NO_PARENT;

    return rc;
}


/**
 * Same as CFGMR3InsertString except the string value given in RTStrPrintf
 * fashion.
 *
 * @returns VBox status code.
 * @param   pNode           Parent node.
 * @param   pszName         Value name.
 * @param   pszFormat       The value given as a format string.
 * @param   ...             Argument to pszFormat.
 */
VMMR3DECL(int) CFGMR3InsertStringF(PCFGMNODE pNode, const char *pszName, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int rc = CFGMR3InsertStringFV(pNode, pszName, pszFormat, va);
    va_end(va);
    return rc;
}


/**
 * Same as CFGMR3InsertString except the string value given as a UTF-16 string.
 *
 * @returns VBox status code.
 * @param   pNode           Parent node.
 * @param   pszName         Value name.
 * @param   pwszValue       The string value (UTF-16).
 */
VMMR3DECL(int) CFGMR3InsertStringW(PCFGMNODE pNode, const char *pszName, PCRTUTF16 pwszValue)
{
    char *pszValue;
    int rc = RTUtf16ToUtf8(pwszValue, &pszValue);
    if (RT_SUCCESS(rc))
    {
        rc = CFGMR3InsertString(pNode, pszName, pszValue);
        RTStrFree(pszValue);
    }
    return rc;
}


/**
 * Inserts a new bytes value.
 *
 * @returns VBox status code.
 * @param   pNode           Parent node.
 * @param   pszName         Value name.
 * @param   pvBytes         The value.
 * @param   cbBytes         The value size.
 */
VMMR3DECL(int) CFGMR3InsertBytes(PCFGMNODE pNode, const char *pszName, const void *pvBytes, size_t cbBytes)
{
    int rc;
    if (pNode)
    {
        if (cbBytes == (RTUINT)cbBytes)
        {
            /*
             * Allocate string object first.
             */
            void *pvCopy = cfgmR3MemAlloc(pNode->pVM, MM_TAG_CFGM_STRING, cbBytes);
            if (pvCopy || !cbBytes)
            {
                memcpy(pvCopy, pvBytes, cbBytes);

                /*
                 * Create value leaf and set it to string type.
                 */
                PCFGMLEAF pLeaf;
                rc = cfgmR3InsertLeaf(pNode, pszName, &pLeaf);
                if (RT_SUCCESS(rc))
                {
                    pLeaf->enmType = CFGMVALUETYPE_BYTES;
                    pLeaf->Value.Bytes.cb   = cbBytes;
                    pLeaf->Value.Bytes.pau8 = (uint8_t *)pvCopy;
                }
                else
                    cfgmR3MemFree(pNode->pVM, pvCopy);
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_OUT_OF_RANGE;
    }
    else
        rc = VERR_CFGM_NO_PARENT;

    return rc;
}


/**
 * Inserts a new password value.
 *
 * This variant expects that the caller know the length of the password string
 * already so we can avoid calling strlen() here.
 *
 * @returns VBox status code.
 * @param   pNode           Parent node.
 * @param   pszName         Value name.
 * @param   pszString       The value. Must not be NULL.
 * @param   cchString       The length of the string excluding the terminator.
 */
VMMR3DECL(int) CFGMR3InsertPasswordN(PCFGMNODE pNode, const char *pszName, const char *pszString, size_t cchString)
{
    Assert(RTStrNLen(pszString, cchString) == cchString);

    int rc;
    if (pNode)
    {
        /*
         * Allocate string object first using the safer memory API since this
         * is considered sensitive information.
         */
        char *pszStringCopy = (char *)RTMemSaferAllocZ(cchString + 1);
        if (pszStringCopy)
        {
            memcpy(pszStringCopy, pszString, cchString);
            pszStringCopy[cchString] = '\0';
            RTMemSaferScramble(pszStringCopy, cchString + 1);

            /*
             * Create value leaf and set it to string type.
             */
            PCFGMLEAF pLeaf;
            rc = cfgmR3InsertLeaf(pNode, pszName, &pLeaf);
            if (RT_SUCCESS(rc))
            {
                pLeaf->enmType = CFGMVALUETYPE_PASSWORD;
                pLeaf->Value.String.psz = pszStringCopy;
                pLeaf->Value.String.cb  = cchString + 1;
            }
            else
                RTMemSaferFree(pszStringCopy, cchString + 1);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_CFGM_NO_PARENT;

    return rc;
}


/**
 * Inserts a new password value.
 *
 * Calls strlen(pszString) internally; if you know the length of the string,
 * CFGMR3InsertStringLengthKnown() is faster.
 *
 * @returns VBox status code.
 * @param   pNode           Parent node.
 * @param   pszName         Value name.
 * @param   pszString       The value.
 */
VMMR3DECL(int) CFGMR3InsertPassword(PCFGMNODE pNode, const char *pszName, const char *pszString)
{
    return CFGMR3InsertPasswordN(pNode, pszName, pszString, strlen(pszString));
}


/**
 * Make a copy of the specified value under the given node.
 *
 * @returns VBox status code.
 * @param   pNode               Parent node.
 * @param   pValue              The value to copy and insert.
 */
VMMR3DECL(int) CFGMR3InsertValue(PCFGMNODE pNode, PCFGMLEAF pValue)
{
    int rc;
    switch (pValue->enmType)
    {
        case CFGMVALUETYPE_INTEGER:
            rc = CFGMR3InsertInteger(pNode, pValue->szName, pValue->Value.Integer.u64);
            break;

        case CFGMVALUETYPE_BYTES:
            rc = CFGMR3InsertBytes(pNode, pValue->szName, pValue->Value.Bytes.pau8, pValue->Value.Bytes.cb);
            break;

        case CFGMVALUETYPE_STRING:
            rc = CFGMR3InsertStringN(pNode, pValue->szName, pValue->Value.String.psz, pValue->Value.String.cb - 1);
            break;

        case CFGMVALUETYPE_PASSWORD:
            rc = CFGMR3InsertPasswordN(pNode, pValue->szName, pValue->Value.String.psz, pValue->Value.String.cb - 1);
            break;

        default:
            rc = VERR_CFGM_IPE_1;
            AssertMsgFailed(("Invalid value type %d\n", pValue->enmType));
            break;
    }
    return rc;
}


/**
 * Remove a value.
 *
 * @returns VBox status code.
 * @param   pNode       Parent node.
 * @param   pszName     Name of the new child node.
 */
VMMR3DECL(int) CFGMR3RemoveValue(PCFGMNODE pNode, const char *pszName)
{
    PCFGMLEAF pLeaf;
    int rc = cfgmR3ResolveLeaf(pNode, pszName, &pLeaf);
    if (RT_SUCCESS(rc))
        cfgmR3RemoveLeaf(pNode, pLeaf);
    return rc;
}



/*
 *  -+- helper apis -+-
 */


/**
 * Query unsigned 64-bit integer value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pu64            Where to store the integer value.
 */
VMMR3DECL(int) CFGMR3QueryU64(PCFGMNODE pNode, const char *pszName, uint64_t *pu64)
{
    return CFGMR3QueryInteger(pNode, pszName, pu64);
}


/**
 * Query unsigned 64-bit integer value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pu64            Where to store the integer value. Set to default on failure.
 * @param   u64Def          The default value.
 */
VMMR3DECL(int) CFGMR3QueryU64Def(PCFGMNODE pNode, const char *pszName, uint64_t *pu64, uint64_t u64Def)
{
    return CFGMR3QueryIntegerDef(pNode, pszName, pu64, u64Def);
}


/**
 * Query signed 64-bit integer value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pi64            Where to store the value.
 */
VMMR3DECL(int) CFGMR3QueryS64(PCFGMNODE pNode, const char *pszName, int64_t *pi64)
{
    uint64_t u64;
    int rc = CFGMR3QueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
        *pi64 = (int64_t)u64;
    return rc;
}


/**
 * Query signed 64-bit integer value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pi64            Where to store the value. Set to default on failure.
 * @param   i64Def          The default value.
 */
VMMR3DECL(int) CFGMR3QueryS64Def(PCFGMNODE pNode, const char *pszName, int64_t *pi64, int64_t i64Def)
{
    uint64_t u64;
    int rc = CFGMR3QueryIntegerDef(pNode, pszName, &u64, i64Def);
    *pi64 = (int64_t)u64;
    return rc;
}


/**
 * Query unsigned 32-bit integer value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pu32            Where to store the value.
 */
VMMR3DECL(int) CFGMR3QueryU32(PCFGMNODE pNode, const char *pszName, uint32_t *pu32)
{
    uint64_t u64;
    int rc = CFGMR3QueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
    {
        if (!(u64 & UINT64_C(0xffffffff00000000)))
            *pu32 = (uint32_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    return rc;
}


/**
 * Query unsigned 32-bit integer value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pu32            Where to store the value. Set to default on failure.
 * @param   u32Def          The default value.
 */
VMMR3DECL(int) CFGMR3QueryU32Def(PCFGMNODE pNode, const char *pszName, uint32_t *pu32, uint32_t u32Def)
{
    uint64_t u64;
    int rc = CFGMR3QueryIntegerDef(pNode, pszName, &u64, u32Def);
    if (RT_SUCCESS(rc))
    {
        if (!(u64 & UINT64_C(0xffffffff00000000)))
            *pu32 = (uint32_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    if (RT_FAILURE(rc))
        *pu32 = u32Def;
    return rc;
}


/**
 * Query signed 32-bit integer value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pi32            Where to store the value.
 */
VMMR3DECL(int) CFGMR3QueryS32(PCFGMNODE pNode, const char *pszName, int32_t *pi32)
{
    uint64_t u64;
    int rc = CFGMR3QueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
    {
        if (   !(u64 & UINT64_C(0xffffffff80000000))
            ||  (u64 & UINT64_C(0xffffffff80000000)) == UINT64_C(0xffffffff80000000))
            *pi32 = (int32_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    return rc;
}


/**
 * Query signed 32-bit integer value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pi32            Where to store the value. Set to default on failure.
 * @param   i32Def          The default value.
 */
VMMR3DECL(int) CFGMR3QueryS32Def(PCFGMNODE pNode, const char *pszName, int32_t *pi32, int32_t i32Def)
{
    uint64_t u64;
    int rc = CFGMR3QueryIntegerDef(pNode, pszName, &u64, i32Def);
    if (RT_SUCCESS(rc))
    {
        if (   !(u64 & UINT64_C(0xffffffff80000000))
            ||  (u64 & UINT64_C(0xffffffff80000000)) == UINT64_C(0xffffffff80000000))
            *pi32 = (int32_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    if (RT_FAILURE(rc))
        *pi32 = i32Def;
    return rc;
}


/**
 * Query unsigned 16-bit integer value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pu16            Where to store the value.
 */
VMMR3DECL(int) CFGMR3QueryU16(PCFGMNODE pNode, const char *pszName, uint16_t *pu16)
{
    uint64_t u64;
    int rc = CFGMR3QueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
    {
        if (!(u64 & UINT64_C(0xffffffffffff0000)))
            *pu16 = (int16_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    return rc;
}


/**
 * Query unsigned 16-bit integer value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pu16            Where to store the value. Set to default on failure.
 * @param   u16Def          The default value.
 */
VMMR3DECL(int) CFGMR3QueryU16Def(PCFGMNODE pNode, const char *pszName, uint16_t *pu16, uint16_t u16Def)
{
    uint64_t u64;
    int rc = CFGMR3QueryIntegerDef(pNode, pszName, &u64, u16Def);
    if (RT_SUCCESS(rc))
    {
        if (!(u64 & UINT64_C(0xffffffffffff0000)))
            *pu16 = (int16_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    if (RT_FAILURE(rc))
        *pu16 = u16Def;
    return rc;
}


/**
 * Query signed 16-bit integer value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pi16            Where to store the value.
 */
VMMR3DECL(int) CFGMR3QueryS16(PCFGMNODE pNode, const char *pszName, int16_t *pi16)
{
    uint64_t u64;
    int rc = CFGMR3QueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
    {
        if (   !(u64 & UINT64_C(0xffffffffffff8000))
            ||  (u64 & UINT64_C(0xffffffffffff8000)) == UINT64_C(0xffffffffffff8000))
            *pi16 = (int16_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    return rc;
}


/**
 * Query signed 16-bit integer value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pi16            Where to store the value. Set to default on failure.
 * @param   i16Def          The default value.
 */
VMMR3DECL(int) CFGMR3QueryS16Def(PCFGMNODE pNode, const char *pszName, int16_t *pi16, int16_t i16Def)
{
    uint64_t u64;
    int rc = CFGMR3QueryIntegerDef(pNode, pszName, &u64, i16Def);
    if (RT_SUCCESS(rc))
    {
        if (   !(u64 & UINT64_C(0xffffffffffff8000))
            ||  (u64 & UINT64_C(0xffffffffffff8000)) == UINT64_C(0xffffffffffff8000))
            *pi16 = (int16_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    if (RT_FAILURE(rc))
        *pi16 = i16Def;
    return rc;
}


/**
 * Query unsigned 8-bit integer value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pu8             Where to store the value.
 */
VMMR3DECL(int) CFGMR3QueryU8(PCFGMNODE pNode, const char *pszName, uint8_t *pu8)
{
    uint64_t u64;
    int rc = CFGMR3QueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
    {
        if (!(u64 & UINT64_C(0xffffffffffffff00)))
            *pu8 = (uint8_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    return rc;
}


/**
 * Query unsigned 8-bit integer value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pu8             Where to store the value. Set to default on failure.
 * @param   u8Def           The default value.
 */
VMMR3DECL(int) CFGMR3QueryU8Def(PCFGMNODE pNode, const char *pszName, uint8_t *pu8, uint8_t u8Def)
{
    uint64_t u64;
    int rc = CFGMR3QueryIntegerDef(pNode, pszName, &u64, u8Def);
    if (RT_SUCCESS(rc))
    {
        if (!(u64 & UINT64_C(0xffffffffffffff00)))
            *pu8 = (uint8_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    if (RT_FAILURE(rc))
        *pu8 = u8Def;
    return rc;
}


/**
 * Query signed 8-bit integer value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pi8             Where to store the value.
 */
VMMR3DECL(int) CFGMR3QueryS8(PCFGMNODE pNode, const char *pszName, int8_t *pi8)
{
    uint64_t u64;
    int rc = CFGMR3QueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
    {
        if (   !(u64 & UINT64_C(0xffffffffffffff80))
            ||  (u64 & UINT64_C(0xffffffffffffff80)) == UINT64_C(0xffffffffffffff80))
            *pi8 = (int8_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    return rc;
}


/**
 * Query signed 8-bit integer value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pi8             Where to store the value. Set to default on failure.
 * @param   i8Def           The default value.
 */
VMMR3DECL(int) CFGMR3QueryS8Def(PCFGMNODE pNode, const char *pszName, int8_t *pi8, int8_t i8Def)
{
    uint64_t u64;
    int rc = CFGMR3QueryIntegerDef(pNode, pszName, &u64, i8Def);
    if (RT_SUCCESS(rc))
    {
        if (   !(u64 & UINT64_C(0xffffffffffffff80))
            ||  (u64 & UINT64_C(0xffffffffffffff80)) == UINT64_C(0xffffffffffffff80))
            *pi8 = (int8_t)u64;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    if (RT_FAILURE(rc))
        *pi8 = i8Def;
    return rc;
}


/**
 * Query boolean integer value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pf              Where to store the value.
 * @remark  This function will interpret any non-zero value as true.
 */
VMMR3DECL(int) CFGMR3QueryBool(PCFGMNODE pNode, const char *pszName, bool *pf)
{
    uint64_t u64;
    int rc = CFGMR3QueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
        *pf = u64 ? true : false;
    return rc;
}


/**
 * Query boolean integer value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pf              Where to store the value. Set to default on failure.
 * @param   fDef            The default value.
 * @remark  This function will interpret any non-zero value as true.
 */
VMMR3DECL(int) CFGMR3QueryBoolDef(PCFGMNODE pNode, const char *pszName, bool *pf, bool fDef)
{
    uint64_t u64;
    int rc = CFGMR3QueryIntegerDef(pNode, pszName, &u64, fDef);
    *pf = u64 ? true : false;
    return rc;
}


/**
 * Query I/O port address value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pPort           Where to store the value.
 */
VMMR3DECL(int) CFGMR3QueryPort(PCFGMNODE pNode, const char *pszName, PRTIOPORT pPort)
{
    AssertCompileSize(RTIOPORT, 2);
    return CFGMR3QueryU16(pNode, pszName, pPort);
}


/**
 * Query I/O port address value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pPort           Where to store the value. Set to default on failure.
 * @param   PortDef         The default value.
 */
VMMR3DECL(int) CFGMR3QueryPortDef(PCFGMNODE pNode, const char *pszName, PRTIOPORT pPort, RTIOPORT PortDef)
{
    AssertCompileSize(RTIOPORT, 2);
    return CFGMR3QueryU16Def(pNode, pszName, pPort, PortDef);
}


/**
 * Query unsigned int address value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pu              Where to store the value.
 */
VMMR3DECL(int) CFGMR3QueryUInt(PCFGMNODE pNode, const char *pszName, unsigned int *pu)
{
    AssertCompileSize(unsigned int, 4);
    return CFGMR3QueryU32(pNode, pszName, (uint32_t *)pu);
}


/**
 * Query unsigned int address value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pu              Where to store the value. Set to default on failure.
 * @param   uDef            The default value.
 */
VMMR3DECL(int) CFGMR3QueryUIntDef(PCFGMNODE pNode, const char *pszName, unsigned int *pu, unsigned int uDef)
{
    AssertCompileSize(unsigned int, 4);
    return CFGMR3QueryU32Def(pNode, pszName, (uint32_t *)pu, uDef);
}


/**
 * Query signed int address value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pi              Where to store the value.
 */
VMMR3DECL(int) CFGMR3QuerySInt(PCFGMNODE pNode, const char *pszName, signed int *pi)
{
    AssertCompileSize(signed int, 4);
    return CFGMR3QueryS32(pNode, pszName, (int32_t *)pi);
}


/**
 * Query unsigned int address value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pi              Where to store the value. Set to default on failure.
 * @param   iDef            The default value.
 */
VMMR3DECL(int) CFGMR3QuerySIntDef(PCFGMNODE pNode, const char *pszName, signed int *pi, signed int iDef)
{
    AssertCompileSize(signed int, 4);
    return CFGMR3QueryS32Def(pNode, pszName, (int32_t *)pi, iDef);
}


/**
 * Query Guest Context pointer integer value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pGCPtr          Where to store the value.
 */
VMMR3DECL(int) CFGMR3QueryGCPtr(PCFGMNODE pNode, const char *pszName, PRTGCPTR pGCPtr)
{
    uint64_t u64;
    int rc = CFGMR3QueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
    {
        RTGCPTR u = (RTGCPTR)u64;
        if (u64 == u)
            *pGCPtr = u;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    return rc;
}


/**
 * Query Guest Context pointer integer value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pGCPtr          Where to store the value. Set to default on failure.
 * @param   GCPtrDef        The default value.
 */
VMMR3DECL(int) CFGMR3QueryGCPtrDef(PCFGMNODE pNode, const char *pszName, PRTGCPTR pGCPtr, RTGCPTR GCPtrDef)
{
    uint64_t u64;
    int rc = CFGMR3QueryIntegerDef(pNode, pszName, &u64, GCPtrDef);
    if (RT_SUCCESS(rc))
    {
        RTGCPTR u = (RTGCPTR)u64;
        if (u64 == u)
            *pGCPtr = u;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    if (RT_FAILURE(rc))
        *pGCPtr = GCPtrDef;
    return rc;
}


/**
 * Query Guest Context unsigned pointer value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pGCPtr          Where to store the value.
 */
VMMR3DECL(int) CFGMR3QueryGCPtrU(PCFGMNODE pNode, const char *pszName, PRTGCUINTPTR pGCPtr)
{
    uint64_t u64;
    int rc = CFGMR3QueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
    {
        RTGCUINTPTR u = (RTGCUINTPTR)u64;
        if (u64 == u)
            *pGCPtr = u;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    return rc;
}


/**
 * Query Guest Context unsigned pointer value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pGCPtr          Where to store the value. Set to default on failure.
 * @param   GCPtrDef        The default value.
 */
VMMR3DECL(int) CFGMR3QueryGCPtrUDef(PCFGMNODE pNode, const char *pszName, PRTGCUINTPTR pGCPtr, RTGCUINTPTR GCPtrDef)
{
    uint64_t u64;
    int rc = CFGMR3QueryIntegerDef(pNode, pszName, &u64, GCPtrDef);
    if (RT_SUCCESS(rc))
    {
        RTGCUINTPTR u = (RTGCUINTPTR)u64;
        if (u64 == u)
            *pGCPtr = u;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    if (RT_FAILURE(rc))
        *pGCPtr = GCPtrDef;
    return rc;
}


/**
 * Query Guest Context signed pointer value.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pGCPtr          Where to store the value.
 */
VMMR3DECL(int) CFGMR3QueryGCPtrS(PCFGMNODE pNode, const char *pszName, PRTGCINTPTR pGCPtr)
{
    uint64_t u64;
    int rc = CFGMR3QueryInteger(pNode, pszName, &u64);
    if (RT_SUCCESS(rc))
    {
        RTGCINTPTR u = (RTGCINTPTR)u64;
        if (u64 == (uint64_t)u)
            *pGCPtr = u;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    return rc;
}


/**
 * Query Guest Context signed pointer value with default.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Name of an integer value.
 * @param   pGCPtr          Where to store the value. Set to default on failure.
 * @param   GCPtrDef        The default value.
 */
VMMR3DECL(int) CFGMR3QueryGCPtrSDef(PCFGMNODE pNode, const char *pszName, PRTGCINTPTR pGCPtr, RTGCINTPTR GCPtrDef)
{
    uint64_t u64;
    int rc = CFGMR3QueryIntegerDef(pNode, pszName, &u64, GCPtrDef);
    if (RT_SUCCESS(rc))
    {
        RTGCINTPTR u = (RTGCINTPTR)u64;
        if (u64 == (uint64_t)u)
            *pGCPtr = u;
        else
            rc = VERR_CFGM_INTEGER_TOO_BIG;
    }
    if (RT_FAILURE(rc))
        *pGCPtr = GCPtrDef;
    return rc;
}


/**
 * Query zero terminated character value storing it in a
 * buffer allocated from the MM heap.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.
 * @param   pszName         Value name. This value must be of zero terminated character string type.
 * @param   ppszString      Where to store the string pointer.
 *                          Free this using MMR3HeapFree() (or RTStrFree if not
 *                          associated with a pUVM - see CFGMR3CreateTree).
 */
VMMR3DECL(int) CFGMR3QueryStringAlloc(PCFGMNODE pNode, const char *pszName, char **ppszString)
{
    size_t cbString;
    int rc = CFGMR3QuerySize(pNode, pszName, &cbString);
    if (RT_SUCCESS(rc))
    {
        char *pszString = cfgmR3StrAlloc(pNode->pVM, MM_TAG_CFGM_USER, cbString);
        if (pszString)
        {
            rc = CFGMR3QueryString(pNode, pszName, pszString, cbString);
            if (RT_SUCCESS(rc))
                *ppszString = pszString;
            else
                cfgmR3StrFree(pNode->pVM, pszString);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    return rc;
}


/**
 * Query zero terminated character value storing it in a
 * buffer allocated from the MM heap.
 *
 * @returns VBox status code.
 * @param   pNode           Which node to search for pszName in.  This cannot be
 *                          NULL if @a pszDef is not NULL, because we need
 *                          somewhere way to get to the VM in order to call
 *                          MMR3HeapStrDup.
 * @param   pszName         Value name. This value must be of zero terminated character string type.
 * @param   ppszString      Where to store the string pointer. Not set on failure.
 *                          Free this using MMR3HeapFree() (or RTStrFree if not
 *                          associated with a pUVM - see CFGMR3CreateTree).
 * @param   pszDef          The default return value.  This can be NULL.
 */
VMMR3DECL(int) CFGMR3QueryStringAllocDef(PCFGMNODE pNode, const char *pszName, char **ppszString, const char *pszDef)
{
    Assert(pNode || !pszDef); /* We need pVM if we need to duplicate the string later. */

    /*
     * (Don't call CFGMR3QuerySize and CFGMR3QueryStringDef here as the latter
     * cannot handle pszDef being NULL.)
     */
    PCFGMLEAF pLeaf;
    int rc = cfgmR3ResolveLeaf(pNode, pszName, &pLeaf);
    if (RT_SUCCESS(rc))
    {
        if (pLeaf->enmType == CFGMVALUETYPE_STRING)
        {
            size_t const cbSrc = pLeaf->Value.String.cb;
            char *pszString = cfgmR3StrAlloc(pNode->pVM, MM_TAG_CFGM_USER, cbSrc);
            if (pszString)
            {
                memcpy(pszString, pLeaf->Value.String.psz, cbSrc);
                *ppszString = pszString;
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_CFGM_NOT_STRING;
    }
    if (RT_FAILURE(rc))
    {
        if (!pszDef)
            *ppszString = NULL;
        else
        {
            size_t const cbDef = strlen(pszDef) + 1;
            *ppszString = cfgmR3StrAlloc(pNode->pVM, MM_TAG_CFGM_USER, cbDef);
            memcpy(*ppszString, pszDef, cbDef);
        }
        if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT)
            rc = VINF_SUCCESS;
    }

    return rc;
}


/**
 * Dumps the configuration (sub)tree to the release log.
 *
 * @param   pRoot   The root node of the dump.
 */
VMMR3DECL(void) CFGMR3Dump(PCFGMNODE pRoot)
{
    bool fOldBuffered = RTLogRelSetBuffering(true /*fBuffered*/);
    LogRel(("************************* CFGM dump *************************\n"));
    cfgmR3Dump(pRoot, 0, DBGFR3InfoLogRelHlp());
#ifdef LOG_ENABLED
    if (LogIsEnabled())
        cfgmR3Dump(pRoot, 0, DBGFR3InfoLogHlp());
#endif
    LogRel(("********************* End of CFGM dump **********************\n"));
    RTLogRelSetBuffering(fOldBuffered);
}


/**
 * Info handler, internal version.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        Callback functions for doing output.
 * @param   pszArgs     Argument string. Optional and specific to the handler.
 */
static DECLCALLBACK(void) cfgmR3Info(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    /*
     * Figure where to start.
     */
    PCFGMNODE pRoot = pVM->cfgm.s.pRoot;
    if (pszArgs && *pszArgs)
    {
        int rc = cfgmR3ResolveNode(pRoot, pszArgs, &pRoot);
        if (RT_FAILURE(rc))
        {
            pHlp->pfnPrintf(pHlp, "Failed to resolve CFGM path '%s', %Rrc", pszArgs, rc);
            return;
        }
    }

    /*
     * Dump the specified tree.
     */
    pHlp->pfnPrintf(pHlp, "pRoot=%p:{", pRoot);
    cfgmR3DumpPath(pRoot, pHlp);
    pHlp->pfnPrintf(pHlp, "}\n");
    cfgmR3Dump(pRoot, 0, pHlp);
}


/**
 * Recursively prints a path name.
 */
static void cfgmR3DumpPath(PCFGMNODE pNode, PCDBGFINFOHLP pHlp)
{
    if (pNode->pParent)
        cfgmR3DumpPath(pNode->pParent, pHlp);
    pHlp->pfnPrintf(pHlp, "%s/", pNode->szName);
}


/**
 * Dumps a branch of a tree.
 */
static void cfgmR3Dump(PCFGMNODE pRoot, unsigned iLevel, PCDBGFINFOHLP pHlp)
{
    /*
     * Path.
     */
    pHlp->pfnPrintf(pHlp, "[");
    cfgmR3DumpPath(pRoot, pHlp);
    pHlp->pfnPrintf(pHlp, "] (level %d)%s\n", iLevel, pRoot->fRestrictedRoot ? " (restricted root)" : "");

    /*
     * Values.
     */
    PCFGMLEAF pLeaf;
    size_t cchMax = 0;
    for (pLeaf = CFGMR3GetFirstValue(pRoot); pLeaf; pLeaf = CFGMR3GetNextValue(pLeaf))
        cchMax = RT_MAX(cchMax, pLeaf->cchName);
    for (pLeaf = CFGMR3GetFirstValue(pRoot); pLeaf; pLeaf = CFGMR3GetNextValue(pLeaf))
    {
        switch (CFGMR3GetValueType(pLeaf))
        {
            case CFGMVALUETYPE_INTEGER:
            {
                pHlp->pfnPrintf(pHlp, "  %-*s <integer> = %#018llx (%'lld",
                                (int)cchMax, pLeaf->szName, pLeaf->Value.Integer.u64, pLeaf->Value.Integer.u64);
                if (   (   pLeaf->cchName >= 4
                        && !RTStrCmp(&pLeaf->szName[pLeaf->cchName - 4], "Size"))
                    || (   pLeaf->cchName >= 2
                        && !RTStrNCmp(pLeaf->szName, "cb", 2)) )
                    pHlp->pfnPrintf(pHlp, ", %' Rhcb)\n", pLeaf->Value.Integer.u64);
                else
                    pHlp->pfnPrintf(pHlp, ")\n");
                break;
            }

            case CFGMVALUETYPE_STRING:
                pHlp->pfnPrintf(pHlp, "  %-*s <string>  = \"%s\" (cb=%zu)\n",
                                (int)cchMax, pLeaf->szName, pLeaf->Value.String.psz, pLeaf->Value.String.cb);
                break;

            case CFGMVALUETYPE_BYTES:
                pHlp->pfnPrintf(pHlp, "  %-*s <bytes>   = \"%.*Rhxs\" (cb=%zu)\n",
                                (int)cchMax, pLeaf->szName, pLeaf->Value.Bytes.cb, pLeaf->Value.Bytes.pau8, pLeaf->Value.Bytes.cb);
                break;

            case CFGMVALUETYPE_PASSWORD:
                pHlp->pfnPrintf(pHlp, "  %-*s <password>= \"***REDACTED***\" (cb=%zu)\n",
                                (int)cchMax, pLeaf->szName, pLeaf->Value.String.cb);
                break;

            default:
                AssertMsgFailed(("bad leaf!\n"));
                break;
        }
    }
    pHlp->pfnPrintf(pHlp, "\n");

    /*
     * Children.
     */
    for (PCFGMNODE pChild = CFGMR3GetFirstChild(pRoot); pChild; pChild = CFGMR3GetNextChild(pChild))
    {
        Assert(pChild->pNext != pChild);
        Assert(pChild->pPrev != pChild);
        Assert(pChild->pPrev != pChild->pNext || !pChild->pPrev);
        Assert(pChild->pFirstChild != pChild);
        Assert(pChild->pParent == pRoot);
        cfgmR3Dump(pChild, iLevel + 1, pHlp);
    }
}

