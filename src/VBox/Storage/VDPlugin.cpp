/* $Id: VDPlugin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VD - Virtual disk container implementation, plugin related bits.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_VD
#include <VBox/err.h>
#include <VBox/sup.h>
#include <VBox/log.h>
#include <VBox/vd-plugin.h>

#include <iprt/dir.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/path.h>

#include "VDInternal.h"
#include "VDBackends.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Plugin structure.
 */
typedef struct VDPLUGIN
{
    /** Pointer to the next plugin structure. */
    RTLISTNODE NodePlugin;
    /** Handle of loaded plugin library. */
    RTLDRMOD   hPlugin;
    /** Filename of the loaded plugin. */
    char       *pszFilename;
} VDPLUGIN;
/** Pointer to a plugin structure. */
typedef VDPLUGIN *PVDPLUGIN;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
/** Head of loaded plugin list. */
static RTLISTANCHOR g_ListPluginsLoaded;
#endif

/** Number of image backends supported. */
static unsigned g_cBackends = 0;
/** Array of pointers to the image backends. */
static PCVDIMAGEBACKEND *g_apBackends = NULL;
#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
/** Array of handles to the corresponding plugin. */
static RTLDRMOD *g_ahBackendPlugins = NULL;
#endif
/**
 * Builtin image backends.
 *
 * @note As long as the pfnProb() calls aren't scored, the ordering influences
 *       which backend take precedence.  In particular, the RAW backend should
 *       be thowards the end of the list.
 */
static PCVDIMAGEBACKEND aStaticBackends[] =
{
    &g_VmdkBackend,
    &g_VDIBackend,
    &g_VhdBackend,
    &g_ParallelsBackend,
    &g_DmgBackend,
    &g_QedBackend,
    &g_QCowBackend,
    &g_VhdxBackend,
    &g_CueBackend,
    &g_VBoxIsoMakerBackend,
    &g_RawBackend,
    &g_ISCSIBackend
};

/** Number of supported cache backends. */
static unsigned g_cCacheBackends = 0;
/** Array of pointers to the cache backends. */
static PCVDCACHEBACKEND *g_apCacheBackends = NULL;
#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
/** Array of handles to the corresponding plugin.
 *
 * @todo r=bird: This looks rather pointless.
 */
static RTLDRMOD *g_ahCacheBackendPlugins = NULL;
#endif
/** Builtin cache backends. */
static PCVDCACHEBACKEND aStaticCacheBackends[] =
{
    &g_VciCacheBackend
};

/** Number of supported filter backends. */
static unsigned g_cFilterBackends = 0;
/** Array of pointers to the filters backends. */
static PCVDFILTERBACKEND *g_apFilterBackends = NULL;
#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
/** Array of handles to the corresponding plugin. */
static PRTLDRMOD g_pahFilterBackendPlugins = NULL;
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Add an array of image format backends from the given plugin to the list of known
 * image formats.
 *
 * @returns VBox status code.
 * @param   hPlugin         The plugin handle the backends belong to, can be NIL_RTLDRMOD
 *                          for compiled in backends.
 * @param   ppBackends      The array of image backend descriptors to add.
 * @param   cBackends       Number of descriptors in the array.
 */
static int vdAddBackends(RTLDRMOD hPlugin, PCVDIMAGEBACKEND *ppBackends, unsigned cBackends)
{
    PCVDIMAGEBACKEND *pTmp = (PCVDIMAGEBACKEND *)RTMemRealloc(g_apBackends,
           (g_cBackends + cBackends) * sizeof(PCVDIMAGEBACKEND));
    if (RT_UNLIKELY(!pTmp))
        return VERR_NO_MEMORY;
    g_apBackends = pTmp;
    memcpy(&g_apBackends[g_cBackends], ppBackends, cBackends * sizeof(PCVDIMAGEBACKEND));

#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
    RTLDRMOD *pTmpPlugins = (RTLDRMOD*)RTMemRealloc(g_ahBackendPlugins,
           (g_cBackends + cBackends) * sizeof(RTLDRMOD));
    if (RT_UNLIKELY(!pTmpPlugins))
        return VERR_NO_MEMORY;
    g_ahBackendPlugins = pTmpPlugins;
    for (unsigned i = g_cBackends; i < g_cBackends + cBackends; i++)
        g_ahBackendPlugins[i] = hPlugin;
#else
    RT_NOREF(hPlugin);
#endif

    g_cBackends += cBackends;
    return VINF_SUCCESS;
}


/**
 * Add an array of cache format backends from the given plugin to the list of known
 * cache formats.
 *
 * @returns VBox status code.
 * @param   hPlugin         The plugin handle the backends belong to, can be NIL_RTLDRMOD
 *                          for compiled in backends.
 * @param   ppBackends      The array of cache backend descriptors to add.
 * @param   cBackends       Number of descriptors in the array.
 */
static int vdAddCacheBackends(RTLDRMOD hPlugin, PCVDCACHEBACKEND *ppBackends, unsigned cBackends)
{
    PCVDCACHEBACKEND *pTmp = (PCVDCACHEBACKEND*)RTMemReallocTag(g_apCacheBackends,
                                                                (g_cCacheBackends + cBackends) * sizeof(PCVDCACHEBACKEND),
                                                                "may-leak:vdAddCacheBackend");
    if (RT_UNLIKELY(!pTmp))
        return VERR_NO_MEMORY;
    g_apCacheBackends = pTmp;
    memcpy(&g_apCacheBackends[g_cCacheBackends], ppBackends, cBackends * sizeof(PCVDCACHEBACKEND));

#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
    RTLDRMOD *pTmpPlugins = (RTLDRMOD*)RTMemReallocTag(g_ahCacheBackendPlugins,
                                                       (g_cCacheBackends + cBackends) * sizeof(RTLDRMOD),
                                                       "may-leak:vdAddCacheBackend");
    if (RT_UNLIKELY(!pTmpPlugins))
        return VERR_NO_MEMORY;
    g_ahCacheBackendPlugins = pTmpPlugins;
    for (unsigned i = g_cCacheBackends; i < g_cCacheBackends + cBackends; i++)
        g_ahCacheBackendPlugins[i] = hPlugin;
#else
    RT_NOREF(hPlugin);
#endif

    g_cCacheBackends += cBackends;
    return VINF_SUCCESS;
}

#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
/**
 * Add a single image format backend to the list of known image formats.
 *
 * @returns VBox status code.
 * @param   hPlugin         The plugin handle the backend belongs to, can be NIL_RTLDRMOD
 *                          for compiled in backends.
 * @param   pBackend        The image backend descriptors to add.
 */
DECLINLINE(int) vdAddBackend(RTLDRMOD hPlugin, PCVDIMAGEBACKEND pBackend)
{
    return vdAddBackends(hPlugin, &pBackend, 1);
}


/**
 * Add a single cache format backend to the list of known cache formats.
 *
 * @returns VBox status code.
 * @param   hPlugin         The plugin handle the backend belongs to, can be NIL_RTLDRMOD
 *                          for compiled in backends.
 * @param   pBackend        The cache backend descriptors to add.
 */
DECLINLINE(int) vdAddCacheBackend(RTLDRMOD hPlugin, PCVDCACHEBACKEND pBackend)
{
    return vdAddCacheBackends(hPlugin, &pBackend, 1);
}


/**
 * Add several filter backends.
 *
 * @returns VBox status code.
 * @param   hPlugin       Plugin handle to add.
 * @param   ppBackends    Array of filter backends to add.
 * @param   cBackends     Number of backends to add.
 */
static int vdAddFilterBackends(RTLDRMOD hPlugin, PCVDFILTERBACKEND *ppBackends, unsigned cBackends)
{
    PCVDFILTERBACKEND *pTmp = (PCVDFILTERBACKEND *)RTMemRealloc(g_apFilterBackends,
           (g_cFilterBackends + cBackends) * sizeof(PCVDFILTERBACKEND));
    if (RT_UNLIKELY(!pTmp))
        return VERR_NO_MEMORY;
    g_apFilterBackends = pTmp;

#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
    PRTLDRMOD pTmpPlugins = (PRTLDRMOD)RTMemRealloc(g_pahFilterBackendPlugins,
                                                    (g_cFilterBackends + cBackends) * sizeof(RTLDRMOD));
    if (RT_UNLIKELY(!pTmpPlugins))
        return VERR_NO_MEMORY;

    g_pahFilterBackendPlugins = pTmpPlugins;
    memcpy(&g_apFilterBackends[g_cFilterBackends], ppBackends, cBackends * sizeof(PCVDFILTERBACKEND));
    for (unsigned i = g_cFilterBackends; i < g_cFilterBackends + cBackends; i++)
        g_pahFilterBackendPlugins[i] = hPlugin;
#else
    RT_NOREF(hPlugin);
#endif

    g_cFilterBackends += cBackends;
    return VINF_SUCCESS;
}


/**
 * Add a single filter backend to the list of supported filters.
 *
 * @returns VBox status code.
 * @param   hPlugin     Plugin handle to add.
 * @param   pBackend    The backend to add.
 */
DECLINLINE(int) vdAddFilterBackend(RTLDRMOD hPlugin, PCVDFILTERBACKEND pBackend)
{
    return vdAddFilterBackends(hPlugin, &pBackend, 1);
}

/**
 * @interface_method_impl{VDBACKENDREGISTER,pfnRegisterImage}
 */
static DECLCALLBACK(int) vdPluginRegisterImage(void *pvUser, PCVDIMAGEBACKEND pBackend)
{
    int rc = VINF_SUCCESS;

    if (VD_VERSION_ARE_COMPATIBLE(VD_IMGBACKEND_VERSION, pBackend->u32Version))
        vdAddBackend((RTLDRMOD)pvUser, pBackend);
    else
    {
        LogFunc(("ignored plugin: pBackend->u32Version=%u rc=%Rrc\n", pBackend->u32Version, rc));
        rc = VERR_IGNORED;
    }

    return rc;
}

/**
 * @interface_method_impl{VDBACKENDREGISTER,pfnRegisterCache}
 */
static DECLCALLBACK(int) vdPluginRegisterCache(void *pvUser, PCVDCACHEBACKEND pBackend)
{
    int rc = VINF_SUCCESS;

    if (VD_VERSION_ARE_COMPATIBLE(VD_CACHEBACKEND_VERSION, pBackend->u32Version))
        vdAddCacheBackend((RTLDRMOD)pvUser, pBackend);
    else
    {
        LogFunc(("ignored plugin: pBackend->u32Version=%u rc=%Rrc\n", pBackend->u32Version, rc));
        rc = VERR_IGNORED;
    }

    return rc;
}

/**
 * @interface_method_impl{VDBACKENDREGISTER,pfnRegisterFilter}
 */
static DECLCALLBACK(int) vdPluginRegisterFilter(void *pvUser, PCVDFILTERBACKEND pBackend)
{
    int rc = VINF_SUCCESS;

    if (VD_VERSION_ARE_COMPATIBLE(VD_FLTBACKEND_VERSION, pBackend->u32Version))
        vdAddFilterBackend((RTLDRMOD)pvUser, pBackend);
    else
    {
        LogFunc(("ignored plugin: pBackend->u32Version=%u rc=%Rrc\n", pBackend->u32Version, rc));
        rc = VERR_IGNORED;
    }

    return rc;
}

/**
 * Checks whether the given plugin filename was already loaded.
 *
 * @returns Pointer to already loaded plugin, NULL if not found.
 * @param   pszFilename    The filename to check.
 */
static PVDPLUGIN vdPluginFind(const char *pszFilename)
{
    PVDPLUGIN pIt;
    RTListForEach(&g_ListPluginsLoaded, pIt, VDPLUGIN, NodePlugin)
    {
        if (!RTStrCmp(pIt->pszFilename, pszFilename))
            return pIt;
    }

    return NULL;
}

/**
 * Adds a plugin to the list of loaded plugins.
 *
 * @returns VBox status code.
 * @param   hPlugin     Plugin handle to add.
 * @param   pszFilename The associated filename, used for finding duplicates.
 */
static int vdAddPlugin(RTLDRMOD hPlugin, const char *pszFilename)
{
    int rc = VINF_SUCCESS;
    PVDPLUGIN pPlugin = (PVDPLUGIN)RTMemAllocZ(sizeof(VDPLUGIN));

    if (pPlugin)
    {
        pPlugin->hPlugin = hPlugin;
        pPlugin->pszFilename = RTStrDup(pszFilename);
        if (pPlugin->pszFilename)
            RTListAppend(&g_ListPluginsLoaded, &pPlugin->NodePlugin);
        else
        {
            RTMemFree(pPlugin);
            rc = VERR_NO_MEMORY;
        }
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/**
 * Removes a single plugin given by the filename.
 *
 * @returns VBox status code.
 * @param   pszFilename     The plugin filename to remove.
 */
static int vdRemovePlugin(const char *pszFilename)
{
    /* Find plugin to be removed from the list. */
    PVDPLUGIN pIt = vdPluginFind(pszFilename);
    if (!pIt)
        return VINF_SUCCESS;

    /** @todo r=klaus: need to add a plugin entry point for unregistering the
     * backends. Only if this doesn't exist (or fails to work) we should fall
     * back to the following uncoordinated backend cleanup. */
    for (unsigned i = 0; i < g_cBackends; i++)
    {
        while (i < g_cBackends && g_ahBackendPlugins[i] == pIt->hPlugin)
        {
            memmove(&g_apBackends[i], &g_apBackends[i + 1], (g_cBackends - i - 1) * sizeof(PCVDIMAGEBACKEND));
            memmove(&g_ahBackendPlugins[i], &g_ahBackendPlugins[i + 1], (g_cBackends - i - 1) * sizeof(RTLDRMOD));
            /** @todo for now skip reallocating, doesn't save much */
            g_cBackends--;
        }
    }
    for (unsigned i = 0; i < g_cCacheBackends; i++)
    {
        while (i < g_cCacheBackends && g_ahCacheBackendPlugins[i] == pIt->hPlugin)
        {
            memmove(&g_apCacheBackends[i], &g_apCacheBackends[i + 1], (g_cCacheBackends - i - 1) * sizeof(PCVDCACHEBACKEND));
            memmove(&g_ahCacheBackendPlugins[i], &g_ahCacheBackendPlugins[i + 1], (g_cCacheBackends - i - 1) * sizeof(RTLDRMOD));
            /** @todo for now skip reallocating, doesn't save much */
            g_cCacheBackends--;
        }
    }
    for (unsigned i = 0; i < g_cFilterBackends; i++)
    {
        while (i < g_cFilterBackends && g_pahFilterBackendPlugins[i] == pIt->hPlugin)
        {
            memmove(&g_apFilterBackends[i], &g_apFilterBackends[i + 1], (g_cFilterBackends - i - 1) * sizeof(PCVDFILTERBACKEND));
            memmove(&g_pahFilterBackendPlugins[i], &g_pahFilterBackendPlugins[i + 1], (g_cFilterBackends - i - 1) * sizeof(RTLDRMOD));
            /** @todo for now skip reallocating, doesn't save much */
            g_cFilterBackends--;
        }
    }

    /* Remove the plugin node now, all traces of it are gone. */
    RTListNodeRemove(&pIt->NodePlugin);
    RTLdrClose(pIt->hPlugin);
    RTStrFree(pIt->pszFilename);
    RTMemFree(pIt);

    return VINF_SUCCESS;
}

#endif /* VBOX_HDD_NO_DYNAMIC_BACKENDS*/

/**
 * Returns the number of known image format backends.
 *
 * @returns Number of image formats known.
 */
DECLHIDDEN(uint32_t) vdGetImageBackendCount(void)
{
    return g_cBackends;
}


/**
 * Queries a image backend descriptor by the index.
 *
 * @returns VBox status code.
 * @param   idx             The index of the backend to query.
 * @param   ppBackend       Where to store the pointer to the backend descriptor on success.
 */
DECLHIDDEN(int) vdQueryImageBackend(uint32_t idx, PCVDIMAGEBACKEND *ppBackend)
{
    if (idx >= g_cBackends)
        return VERR_OUT_OF_RANGE;

    *ppBackend = g_apBackends[idx];
    return VINF_SUCCESS;
}


/**
 * Returns the image backend descriptor matching the given identifier if known.
 *
 * @returns VBox status code.
 * @param   pszBackend      The backend identifier to look for.
 * @param   ppBackend       Where to store the pointer to the backend descriptor on success.
 */
DECLHIDDEN(int) vdFindImageBackend(const char *pszBackend, PCVDIMAGEBACKEND *ppBackend)
{
    int rc = VERR_NOT_FOUND;
    PCVDIMAGEBACKEND pBackend = NULL;

    if (!g_apBackends)
        VDInit();

    for (unsigned i = 0; i < g_cBackends; i++)
    {
        if (!RTStrICmp(pszBackend, g_apBackends[i]->pszBackendName))
        {
            pBackend = g_apBackends[i];
            rc = VINF_SUCCESS;
            break;
        }
    }
    *ppBackend = pBackend;
    return rc;
}

/**
 * Returns the number of known cache format backends.
 *
 * @returns Number of image formats known.
 */
DECLHIDDEN(uint32_t) vdGetCacheBackendCount(void)
{
    return g_cCacheBackends;
}


/**
 * Queries a cache backend descriptor by the index.
 *
 * @returns VBox status code.
 * @param   idx             The index of the backend to query.
 * @param   ppBackend       Where to store the pointer to the backend descriptor on success.
 */
DECLHIDDEN(int) vdQueryCacheBackend(uint32_t idx, PCVDCACHEBACKEND *ppBackend)
{
    if (idx >= g_cCacheBackends)
        return VERR_OUT_OF_RANGE;

    *ppBackend = g_apCacheBackends[idx];
    return VINF_SUCCESS;
}


/**
 * Returns the cache backend descriptor matching the given identifier if known.
 *
 * @returns VBox status code.
 * @param   pszBackend      The backend identifier to look for.
 * @param   ppBackend       Where to store the pointer to the backend descriptor on success.
 */
DECLHIDDEN(int) vdFindCacheBackend(const char *pszBackend, PCVDCACHEBACKEND *ppBackend)
{
    int rc = VERR_NOT_FOUND;
    PCVDCACHEBACKEND pBackend = NULL;

    if (!g_apCacheBackends)
        VDInit();

    for (unsigned i = 0; i < g_cCacheBackends; i++)
    {
        if (!RTStrICmp(pszBackend, g_apCacheBackends[i]->pszBackendName))
        {
            pBackend = g_apCacheBackends[i];
            rc = VINF_SUCCESS;
            break;
        }
    }
    *ppBackend = pBackend;
    return rc;
}


/**
 * Returns the number of known filter backends.
 *
 * @returns Number of image formats known.
 */
DECLHIDDEN(uint32_t) vdGetFilterBackendCount(void)
{
    return g_cFilterBackends;
}


/**
 * Queries a filter backend descriptor by the index.
 *
 * @returns VBox status code.
 * @param   idx             The index of the backend to query.
 * @param   ppBackend       Where to store the pointer to the backend descriptor on success.
 */
DECLHIDDEN(int) vdQueryFilterBackend(uint32_t idx, PCVDFILTERBACKEND *ppBackend)
{
    if (idx >= g_cFilterBackends)
        return VERR_OUT_OF_RANGE;

    *ppBackend = g_apFilterBackends[idx];
    return VINF_SUCCESS;
}


/**
 * Returns the filter backend descriptor matching the given identifier if known.
 *
 * @returns VBox status code.
 * @param   pszFilter       The filter identifier to look for.
 * @param   ppBackend       Where to store the pointer to the backend descriptor on success.
 */
DECLHIDDEN(int) vdFindFilterBackend(const char *pszFilter, PCVDFILTERBACKEND *ppBackend)
{
    int rc = VERR_NOT_FOUND;
    PCVDFILTERBACKEND pBackend = NULL;

    for (unsigned i = 0; i < g_cFilterBackends; i++)
    {
        if (!RTStrICmp(pszFilter, g_apFilterBackends[i]->pszBackendName))
        {
            pBackend = g_apFilterBackends[i];
            rc = VINF_SUCCESS;
            break;
        }
    }
    *ppBackend = pBackend;
    return rc;
}


/**
 * Worker for VDPluginLoadFromFilename() and vdPluginLoadFromPath().
 *
 * @returns VBox status code.
 * @param   pszFilename    The plugin filename to load.
 */
DECLHIDDEN(int) vdPluginLoadFromFilename(const char *pszFilename)
{
#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
    /* Plugin loaded? Nothing to do. */
    if (vdPluginFind(pszFilename))
    {
        LogFlowFunc(("Plugin '%s' already loaded\n", pszFilename));
        return VINF_SUCCESS;
    }

    RTLDRMOD hPlugin = NIL_RTLDRMOD;
    int rc = SUPR3HardenedLdrLoadPlugIn(pszFilename, &hPlugin, NULL);
    LogFlowFunc(("SUPR3HardenedLdrLoadPlugIn('%s') -> %Rrc\n", pszFilename, rc));
    if (RT_SUCCESS(rc))
    {
        VDBACKENDREGISTER BackendRegister;
        PFNVDPLUGINLOAD pfnVDPluginLoad = NULL;

        BackendRegister.u32Version        = VD_BACKENDREG_CB_VERSION;
        BackendRegister.pfnRegisterImage  = vdPluginRegisterImage;
        BackendRegister.pfnRegisterCache  = vdPluginRegisterCache;
        BackendRegister.pfnRegisterFilter = vdPluginRegisterFilter;

        rc = RTLdrGetSymbol(hPlugin, VD_PLUGIN_LOAD_NAME, (void**)&pfnVDPluginLoad);
        if (RT_FAILURE(rc) || !pfnVDPluginLoad)
        {
            LogFunc(("error resolving the entry point %s in plugin %s, rc=%Rrc, pfnVDPluginLoad=%#p\n",
                     VD_PLUGIN_LOAD_NAME, pszFilename, rc, pfnVDPluginLoad));
            if (RT_SUCCESS(rc))
                rc = VERR_SYMBOL_NOT_FOUND;
        }

        if (RT_SUCCESS(rc))
        {
            /* Get the function table. */
            rc = pfnVDPluginLoad(hPlugin, &BackendRegister);
        }
        else
            LogFunc(("ignored plugin '%s': rc=%Rrc\n", pszFilename, rc));

        /* Create a plugin entry on success. */
        if (RT_SUCCESS(rc))
            vdAddPlugin(hPlugin, pszFilename);
        else
            RTLdrClose(hPlugin);
    }

    return rc;
#else
    RT_NOREF1(pszFilename);
    return VERR_NOT_IMPLEMENTED;
#endif
}

/**
 * Worker for VDPluginLoadFromPath() and vdLoadDynamicBackends().
 *
 * @returns VBox status code.
 * @param   pszPath        The path to load plugins from.
 */
DECLHIDDEN(int) vdPluginLoadFromPath(const char *pszPath)
{
#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
    /* To get all entries with VBoxHDD as prefix. */
    char *pszPluginFilter = RTPathJoinA(pszPath, VD_PLUGIN_PREFIX "*");
    if (!pszPluginFilter)
        return VERR_NO_STR_MEMORY;

    PRTDIRENTRYEX pPluginDirEntry = NULL;
    RTDIR hPluginDir;
    size_t cbPluginDirEntry = sizeof(RTDIRENTRYEX);
    int rc = RTDirOpenFiltered(&hPluginDir, pszPluginFilter, RTDIRFILTER_WINNT, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        pPluginDirEntry = (PRTDIRENTRYEX)RTMemAllocZ(sizeof(RTDIRENTRYEX));
        if (pPluginDirEntry)
        {
            while (   (rc = RTDirReadEx(hPluginDir, pPluginDirEntry, &cbPluginDirEntry, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK))
                   != VERR_NO_MORE_FILES)
            {
                char *pszPluginPath = NULL;

                if (rc == VERR_BUFFER_OVERFLOW)
                {
                    /* allocate new buffer. */
                    RTMemFree(pPluginDirEntry);
                    pPluginDirEntry = (PRTDIRENTRYEX)RTMemAllocZ(cbPluginDirEntry);
                    if (!pPluginDirEntry)
                    {
                        rc = VERR_NO_MEMORY;
                        break;
                    }
                    /* Retry. */
                    rc = RTDirReadEx(hPluginDir, pPluginDirEntry, &cbPluginDirEntry, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
                    if (RT_FAILURE(rc))
                        break;
                }
                else if (RT_FAILURE(rc))
                    break;

                /* We got the new entry. */
                if (!RTFS_IS_FILE(pPluginDirEntry->Info.Attr.fMode))
                    continue;

                /* Prepend the path to the libraries. */
                pszPluginPath = RTPathJoinA(pszPath, pPluginDirEntry->szName);
                if (!pszPluginPath)
                {
                    rc = VERR_NO_STR_MEMORY;
                    break;
                }

                vdPluginLoadFromFilename(pszPluginPath);
                RTStrFree(pszPluginPath);
            }

            RTMemFree(pPluginDirEntry);
        }
        else
            rc = VERR_NO_MEMORY;

        RTDirClose(hPluginDir);
    }
    else
    {
        /* On Windows the above immediately signals that there are no
         * files matching, while on other platforms enumerating the
         * files below fails. Either way: no plugins. */
    }

    if (rc == VERR_NO_MORE_FILES)
        rc = VINF_SUCCESS;
    RTStrFree(pszPluginFilter);
    return rc;
#else
    RT_NOREF1(pszPath);
    return VERR_NOT_IMPLEMENTED;
#endif
}

#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
/**
 * internal: scans plugin directory and loads found plugins.
 */
static int vdLoadDynamicBackends(void)
{
    /*
     * Enumerate plugin backends from the application directory where the other
     * shared libraries are.
     */
    char szPath[RTPATH_MAX];
    int rc = RTPathAppPrivateArch(szPath, sizeof(szPath));
    if (RT_FAILURE(rc))
        return rc;

    return vdPluginLoadFromPath(szPath);
}
#endif

/**
 * Worker for VDPluginUnloadFromFilename() and vdPluginUnloadFromPath().
 *
 * @returns VBox status code.
 * @param   pszFilename    The plugin filename to unload.
 */
DECLHIDDEN(int) vdPluginUnloadFromFilename(const char *pszFilename)
{
#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
    return vdRemovePlugin(pszFilename);
#else
    RT_NOREF1(pszFilename);
    return VERR_NOT_IMPLEMENTED;
#endif
}

/**
 * Worker for VDPluginUnloadFromPath().
 *
 * @returns VBox status code.
 * @param   pszPath        The path to unload plugins from.
 */
DECLHIDDEN(int) vdPluginUnloadFromPath(const char *pszPath)
{
#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
    /* To get all entries with VBoxHDD as prefix. */
    char *pszPluginFilter = RTPathJoinA(pszPath, VD_PLUGIN_PREFIX "*");
    if (!pszPluginFilter)
        return VERR_NO_STR_MEMORY;

    PRTDIRENTRYEX pPluginDirEntry = NULL;
    RTDIR hPluginDir;
    size_t cbPluginDirEntry = sizeof(RTDIRENTRYEX);
    int rc = RTDirOpenFiltered(&hPluginDir, pszPluginFilter, RTDIRFILTER_WINNT, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        pPluginDirEntry = (PRTDIRENTRYEX)RTMemAllocZ(sizeof(RTDIRENTRYEX));
        if (pPluginDirEntry)
        {
            while ((rc = RTDirReadEx(hPluginDir, pPluginDirEntry, &cbPluginDirEntry, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK)) != VERR_NO_MORE_FILES)
            {
                char *pszPluginPath = NULL;

                if (rc == VERR_BUFFER_OVERFLOW)
                {
                    /* allocate new buffer. */
                    RTMemFree(pPluginDirEntry);
                    pPluginDirEntry = (PRTDIRENTRYEX)RTMemAllocZ(cbPluginDirEntry);
                    if (!pPluginDirEntry)
                    {
                        rc = VERR_NO_MEMORY;
                        break;
                    }
                    /* Retry. */
                    rc = RTDirReadEx(hPluginDir, pPluginDirEntry, &cbPluginDirEntry, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
                    if (RT_FAILURE(rc))
                        break;
                }
                else if (RT_FAILURE(rc))
                    break;

                /* We got the new entry. */
                if (!RTFS_IS_FILE(pPluginDirEntry->Info.Attr.fMode))
                    continue;

                /* Prepend the path to the libraries. */
                pszPluginPath = RTPathJoinA(pszPath, pPluginDirEntry->szName);
                if (!pszPluginPath)
                {
                    rc = VERR_NO_STR_MEMORY;
                    break;
                }

                vdPluginUnloadFromFilename(pszPluginPath);
                RTStrFree(pszPluginPath);
            }

            RTMemFree(pPluginDirEntry);
        }
        else
            rc = VERR_NO_MEMORY;

        RTDirClose(hPluginDir);
    }
    else
    {
        /* On Windows the above immediately signals that there are no
         * files matching, while on other platforms enumerating the
         * files below fails. Either way: no plugins. */
    }

    if (rc == VERR_NO_MORE_FILES)
        rc = VINF_SUCCESS;
    RTStrFree(pszPluginFilter);
    return rc;
#else
    RT_NOREF1(pszPath);
    return VERR_NOT_IMPLEMENTED;
#endif
}


/**
 * Initializes the plugin state to be able to load further plugins and populates
 * the backend lists with the compiled in backends.
 *
 * @returns VBox status code.
 */
DECLHIDDEN(int) vdPluginInit(void)
{
    int rc = vdAddBackends(NIL_RTLDRMOD, aStaticBackends, RT_ELEMENTS(aStaticBackends));
    if (RT_SUCCESS(rc))
    {
        rc = vdAddCacheBackends(NIL_RTLDRMOD, aStaticCacheBackends, RT_ELEMENTS(aStaticCacheBackends));
#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
        if (RT_SUCCESS(rc))
        {
            RTListInit(&g_ListPluginsLoaded);
            rc = vdLoadDynamicBackends();
        }
#endif
    }

    return rc;
}


/**
 * Tears down the plugin related state.
 *
 * @returns VBox status code.
 */
DECLHIDDEN(int) vdPluginTerm(void)
{
    if (!g_apBackends)
        return VERR_INTERNAL_ERROR;

#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
    if (g_pahFilterBackendPlugins)
        RTMemFree(g_pahFilterBackendPlugins);
#endif
    if (g_apFilterBackends)
        RTMemFree(g_apFilterBackends);
#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
    if (g_ahCacheBackendPlugins)
        RTMemFree(g_ahCacheBackendPlugins);
#endif
    if (g_apCacheBackends)
        RTMemFree(g_apCacheBackends);
    RTMemFree(g_apBackends);

    g_cBackends = 0;
    g_apBackends = NULL;

    /* Clear the supported cache backends. */
    g_cCacheBackends = 0;
    g_apCacheBackends = NULL;
#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
    g_ahCacheBackendPlugins = NULL;
#endif

    /* Clear the supported filter backends. */
    g_cFilterBackends = 0;
    g_apFilterBackends = NULL;
#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
    g_pahFilterBackendPlugins = NULL;
#endif

#ifndef VBOX_HDD_NO_DYNAMIC_BACKENDS
    PVDPLUGIN pPlugin, pPluginNext;
    RTListForEachSafe(&g_ListPluginsLoaded, pPlugin, pPluginNext, VDPLUGIN, NodePlugin)
    {
        RTLdrClose(pPlugin->hPlugin);
        RTStrFree(pPlugin->pszFilename);
        RTListNodeRemove(&pPlugin->NodePlugin);
        RTMemFree(pPlugin);
    }
#endif

    return VINF_SUCCESS;
}


/**
 * Returns whether the plugin related state is initialized.
 *
 * @returns true if the plugin state is initialized and plugins can be loaded,
 *          false otherwise.
 */
DECLHIDDEN(bool) vdPluginIsInitialized(void)
{
    return g_apBackends != NULL;
}

