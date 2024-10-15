/* $Id: VBoxServicePropCache.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxServicePropCache - Guest property cache.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/assert.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include <VBox/VBoxGuestLib.h>
#include <VBox/HostServices/GuestPropertySvc.h> /* For GUEST_PROP_MAX_VALUE_LEN */
#include "VBoxServiceInternal.h"
#include "VBoxServiceUtils.h"
#include "VBoxServicePropCache.h"



/** @todo Docs */
static PVBOXSERVICEVEPROPCACHEENTRY vgsvcPropCacheFindInternal(PVBOXSERVICEVEPROPCACHE pCache, const char *pszName,
                                                               uint32_t fFlags)
{
    RT_NOREF1(fFlags);
    AssertPtrReturn(pCache, NULL);
    AssertPtrReturn(pszName, NULL);

    /** @todo This is a O(n) lookup, maybe improve this later to O(1) using a
     *        map.
     *  r=bird: Use a string space (RTstrSpace*). That is O(log n) in its current
     *        implementation (AVL tree). However, this is not important at the
     *        moment. */
    PVBOXSERVICEVEPROPCACHEENTRY pNode = NULL;
    if (RT_SUCCESS(RTCritSectEnter(&pCache->CritSect)))
    {
        PVBOXSERVICEVEPROPCACHEENTRY pNodeIt;
        RTListForEach(&pCache->NodeHead, pNodeIt, VBOXSERVICEVEPROPCACHEENTRY, NodeSucc)
        {
            if (strcmp(pNodeIt->pszName, pszName) == 0)
            {
                pNode = pNodeIt;
                break;
            }
        }
        RTCritSectLeave(&pCache->CritSect);
    }
    return pNode;
}


/** @todo Docs */
static PVBOXSERVICEVEPROPCACHEENTRY vgsvcPropCacheInsertEntryInternal(PVBOXSERVICEVEPROPCACHE pCache, const char *pszName)
{
    AssertPtrReturn(pCache, NULL);
    AssertPtrReturn(pszName, NULL);

    PVBOXSERVICEVEPROPCACHEENTRY pNode = (PVBOXSERVICEVEPROPCACHEENTRY)RTMemAlloc(sizeof(VBOXSERVICEVEPROPCACHEENTRY));
    if (pNode)
    {
        pNode->pszName = RTStrDup(pszName);
        if (!pNode->pszName)
        {
            RTMemFree(pNode);
            return NULL;
        }
        pNode->pszValue = NULL;
        pNode->fFlags = 0;
        pNode->pszValueReset = NULL;

        int rc = RTCritSectEnter(&pCache->CritSect);
        if (RT_SUCCESS(rc))
        {
            RTListAppend(&pCache->NodeHead, &pNode->NodeSucc);
            rc = RTCritSectLeave(&pCache->CritSect);
        }
    }
    return pNode;
}


/** @todo Docs */
static int vgsvcPropCacheWritePropF(uint32_t u32ClientId, const char *pszName, uint32_t fFlags, const char *pszValueFormat, ...)
{
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);

    int rc;
    if (pszValueFormat != NULL)
    {
        va_list va;
        va_start(va, pszValueFormat);

        char *pszValue;
        if (RTStrAPrintfV(&pszValue, pszValueFormat, va) >= 0)
        {
            if (fFlags & VGSVCPROPCACHE_FLAGS_TRANSIENT)
            {
                /*
                 * Because a value can be temporary we have to make sure it also
                 * gets deleted when the property cache did not have the chance to
                 * gracefully clean it up (due to a hard VM reset etc), so set this
                 * guest property using the TRANSRESET flag..
                 */
                rc = VbglR3GuestPropWrite(u32ClientId, pszName, pszValue, "TRANSRESET");
                if (rc == VERR_PARSE_ERROR)
                {
                    /* Host does not support the "TRANSRESET" flag, so only
                     * use the "TRANSIENT" flag -- better than nothing :-). */
                    rc = VbglR3GuestPropWrite(u32ClientId, pszName, pszValue, "TRANSIENT");
                    /** @todo r=bird: Remember that the host doesn't support
                     * this. */
                }
            }
            else
                rc = VbglR3GuestPropWriteValue(u32ClientId, pszName, pszValue /* No transient flags set */);
            RTStrFree(pszValue);
        }
        else
            rc = VERR_NO_MEMORY;
        va_end(va);
    }
    else
        rc = VbglR3GuestPropWriteValue(u32ClientId, pszName, NULL);
    return rc;
}


/**
 * Creates a property cache.
 *
 * @returns IPRT status code.
 * @param   pCache          Pointer to the cache.
 * @param   uClientId       The HGCM handle of to the guest property service.
 */
int VGSvcPropCacheCreate(PVBOXSERVICEVEPROPCACHE pCache, uint32_t uClientId)
{
    AssertPtrReturn(pCache, VERR_INVALID_POINTER);
    /** @todo Prevent init the cache twice!
     *  r=bird: Use a magic. */
    RTListInit(&pCache->NodeHead);
    pCache->uClientID = uClientId;
    return RTCritSectInit(&pCache->CritSect);
}


/**
 * Updates a cache entry without submitting any changes to the host.
 *
 * This is handy for defining default values/flags.
 *
 * @returns VBox status code.
 *
 * @param   pCache          The property cache.
 * @param   pszName         The property name.
 * @param   fFlags          The property flags to set.
 * @param   pszValueReset   The property reset value.
 */
int VGSvcPropCacheUpdateEntry(PVBOXSERVICEVEPROPCACHE pCache, const char *pszName, uint32_t fFlags, const char *pszValueReset)
{
    AssertPtrReturn(pCache, VERR_INVALID_POINTER);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    PVBOXSERVICEVEPROPCACHEENTRY pNode = vgsvcPropCacheFindInternal(pCache, pszName, 0);
    if (pNode == NULL)
        pNode = vgsvcPropCacheInsertEntryInternal(pCache, pszName);

    int rc;
    if (pNode != NULL)
    {
        rc = RTCritSectEnter(&pCache->CritSect);
        if (RT_SUCCESS(rc))
        {
            pNode->fFlags = fFlags;
            if (pszValueReset)
            {
                if (pNode->pszValueReset)
                    RTStrFree(pNode->pszValueReset);
                pNode->pszValueReset = RTStrDup(pszValueReset);
                AssertPtr(pNode->pszValueReset);
            }
            rc = RTCritSectLeave(&pCache->CritSect);
        }
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Updates the local guest property cache and writes it to HGCM if outdated.
 *
 * @returns VBox status code.
 * @retval  VERR_BUFFER_OVERFLOW if the property name or value exceeds the limit.
 * @param   pCache          The property cache.
 * @param   pszName         The property name.
 * @param   pszValueFormat  The property format string.  If this is NULL then
 *                          the property will be deleted (if possible).
 * @param   ...             Format arguments.
 */
int VGSvcPropCacheUpdate(PVBOXSERVICEVEPROPCACHE pCache, const char *pszName, const char *pszValueFormat, ...)
{
    AssertPtrReturn(pCache, VERR_INVALID_POINTER);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);

    Assert(pCache->uClientID);

    if (RTStrNLen(pszName, GUEST_PROP_MAX_NAME_LEN) > GUEST_PROP_MAX_NAME_LEN - 1 /* Terminator */)
        return VERR_BUFFER_OVERFLOW;

    /*
     * Format the value first.
     */
    char *pszValue = NULL;
    if (pszValueFormat)
    {
        va_list va;
        va_start(va, pszValueFormat);
        RTStrAPrintfV(&pszValue, pszValueFormat, va);
        va_end(va);
        if (!pszValue)
            return VERR_NO_STR_MEMORY;
        if (RTStrNLen(pszValue, GUEST_PROP_MAX_VALUE_LEN) > GUEST_PROP_MAX_VALUE_LEN - 1 /* Terminator */)
        {
            RTStrFree(pszValue);
            return VERR_BUFFER_OVERFLOW;
        }
    }

    PVBOXSERVICEVEPROPCACHEENTRY pNode = vgsvcPropCacheFindInternal(pCache, pszName, 0);

    /* Lock the cache. */
    int rc = RTCritSectEnter(&pCache->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (pNode == NULL)
            pNode = vgsvcPropCacheInsertEntryInternal(pCache, pszName);

        AssertPtr(pNode);
        if (pszValue) /* Do we have a value to check for? */
        {
            bool fUpdate = false;
            /* Always update this property, no matter what? */
            if (pNode->fFlags & VGSVCPROPCACHE_FLAGS_ALWAYS_UPDATE)
                fUpdate = true;
            /* Did the value change so we have to update? */
            else if (pNode->pszValue && strcmp(pNode->pszValue, pszValue) != 0)
                fUpdate = true;
            /* No value stored at the moment but we have a value now? */
            else if (pNode->pszValue == NULL)
                fUpdate = true;

            if (fUpdate)
            {
                /* Write the update. */
                rc = vgsvcPropCacheWritePropF(pCache->uClientID, pNode->pszName, pNode->fFlags, pszValue);
                VGSvcVerbose(4, "[PropCache %p]: Written '%s'='%s' (flags: %x), rc=%Rrc\n",
                                   pCache, pNode->pszName, pszValue, pNode->fFlags, rc);
                if (RT_SUCCESS(rc)) /* Only update the node's value on successful write. */
                {
                    RTStrFree(pNode->pszValue);
                    pNode->pszValue = RTStrDup(pszValue);
                    if (!pNode->pszValue)
                        rc = VERR_NO_MEMORY;
                }
            }
            else
                rc = VINF_NO_CHANGE; /* No update needed. */
        }
        else
        {
            /* No value specified. Deletion (or no action required). */
            if (pNode->pszValue) /* Did we have a value before? Then the value needs to be deleted. */
            {
                rc = vgsvcPropCacheWritePropF(pCache->uClientID, pNode->pszName,
                                                    0, /* Flags */ NULL /* Value */);
                VGSvcVerbose(4, "[PropCache %p]: Deleted '%s'='%s' (flags: %x), rc=%Rrc\n",
                                   pCache, pNode->pszName, pNode->pszValue, pNode->fFlags, rc);
                if (RT_SUCCESS(rc)) /* Only delete property value on successful Vbgl deletion. */
                {
                    /* Delete property (but do not remove from cache) if not deleted yet. */
                    RTStrFree(pNode->pszValue);
                    pNode->pszValue = NULL;
                }
            }
            else
                rc = VINF_NO_CHANGE; /* No update needed. */
        }

        /* Release cache. */
        RTCritSectLeave(&pCache->CritSect);
    }

    VGSvcVerbose(4, "[PropCache %p]: Updating '%s' resulted in rc=%Rrc\n", pCache, pszName, rc);

    /* Delete temp stuff. */
    RTStrFree(pszValue);
    return rc;
}


/**
 * Updates all cache values which are matching the specified path.
 *
 * @returns VBox status code.
 *
 * @param   pCache          The property cache.
 * @param   pszValue        The value to set.  A NULL will delete the value.
 * @param   fFlags          Flags to set.
 * @param   pszPathFormat   The path format string.  May not be null and has
 *                          to be an absolute path.
 * @param   ...             Format arguments.
 */
int VGSvcPropCacheUpdateByPath(PVBOXSERVICEVEPROPCACHE pCache, const char *pszValue, uint32_t fFlags,
                               const char *pszPathFormat, ...)
{
    RT_NOREF1(fFlags);
    AssertPtrReturn(pCache, VERR_INVALID_POINTER);
    AssertPtrReturn(pszPathFormat, VERR_INVALID_POINTER);

    int rc = VERR_NOT_FOUND;
    if (RT_SUCCESS(RTCritSectEnter(&pCache->CritSect)))
    {
        /*
         * Format the value first.
         */
        char *pszPath = NULL;
        va_list va;
        va_start(va, pszPathFormat);
        RTStrAPrintfV(&pszPath, pszPathFormat, va);
        va_end(va);
        if (!pszPath)
        {
            rc = VERR_NO_STR_MEMORY;
        }
        else
        {
            /* Iterate through all nodes and compare their paths. */
            PVBOXSERVICEVEPROPCACHEENTRY pNodeIt;
            RTListForEach(&pCache->NodeHead, pNodeIt, VBOXSERVICEVEPROPCACHEENTRY, NodeSucc)
            {
                if (RTStrStr(pNodeIt->pszName, pszPath) == pNodeIt->pszName)
                {
                    /** @todo Use some internal function to update the node directly, this is slow atm. */
                    rc = VGSvcPropCacheUpdate(pCache, pNodeIt->pszName, pszValue);
                }
                if (RT_FAILURE(rc))
                    break;
            }
            RTStrFree(pszPath);
        }
        RTCritSectLeave(&pCache->CritSect);
    }
    return rc;
}


/**
 * Flushes the cache by writing every item regardless of its state.
 *
 * @param   pCache          The property cache.
 */
int VGSvcPropCacheFlush(PVBOXSERVICEVEPROPCACHE pCache)
{
    AssertPtrReturn(pCache, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    if (RT_SUCCESS(RTCritSectEnter(&pCache->CritSect)))
    {
        PVBOXSERVICEVEPROPCACHEENTRY pNodeIt;
        RTListForEach(&pCache->NodeHead, pNodeIt, VBOXSERVICEVEPROPCACHEENTRY, NodeSucc)
        {
            rc = vgsvcPropCacheWritePropF(pCache->uClientID, pNodeIt->pszName, pNodeIt->fFlags, pNodeIt->pszValue);
            if (RT_FAILURE(rc))
                break;
        }
        RTCritSectLeave(&pCache->CritSect);
    }
    return rc;
}


/**
 * Reset all temporary properties and destroy the cache.
 *
 * @param   pCache          The property cache.
 */
void VGSvcPropCacheDestroy(PVBOXSERVICEVEPROPCACHE pCache)
{
    AssertPtrReturnVoid(pCache);
    Assert(pCache->uClientID);

    /* Lock the cache. */
    int rc = RTCritSectEnter(&pCache->CritSect);
    if (RT_SUCCESS(rc))
    {
        PVBOXSERVICEVEPROPCACHEENTRY pNode = RTListGetFirst(&pCache->NodeHead, VBOXSERVICEVEPROPCACHEENTRY, NodeSucc);
        while (pNode)
        {
            PVBOXSERVICEVEPROPCACHEENTRY pNext = RTListNodeIsLast(&pCache->NodeHead, &pNode->NodeSucc)
                                                                  ? NULL :
                                                                    RTListNodeGetNext(&pNode->NodeSucc,
                                                                                      VBOXSERVICEVEPROPCACHEENTRY, NodeSucc);
            RTListNodeRemove(&pNode->NodeSucc);

            if (pNode->fFlags & VGSVCPROPCACHE_FLAGS_TEMPORARY)
                rc = vgsvcPropCacheWritePropF(pCache->uClientID, pNode->pszName, pNode->fFlags, pNode->pszValueReset);

            AssertPtr(pNode->pszName);
            RTStrFree(pNode->pszName);
            RTStrFree(pNode->pszValue);
            RTStrFree(pNode->pszValueReset);
            pNode->fFlags = 0;

            RTMemFree(pNode);

            pNode = pNext;
        }
        RTCritSectLeave(&pCache->CritSect);
    }

    /* Destroy critical section. */
    RTCritSectDelete(&pCache->CritSect);
}

