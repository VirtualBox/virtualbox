/* $Id: pdmaudiohostenuminline.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM - Audio Helpers for host audio device enumeration, Inlined Code. (DEV,++)
 *
 * This is all inlined because it's too tedious to create a couple libraries to
 * contain it all (same bad excuse as for intnetinline.h & pdmnetinline.h).
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_vmm_pdmaudiohostenuminline_h
#define VBOX_INCLUDED_vmm_pdmaudiohostenuminline_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>

#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>


/** @defgroup grp_pdm_audio_host_enum_inline    The PDM Host Audio Enumeration Helper APIs
 * @ingroup grp_pdm
 * @{
 */


/**
 * Allocates a host audio device for an enumeration result.
 *
 * @returns Newly allocated audio device, or NULL on failure.
 * @param   cb      The total device structure size.   This must be at least the
 *                  size of PDMAUDIOHOSTDEV.  The idea is that the caller extends
 *                  the PDMAUDIOHOSTDEV structure and appends additional data
 *                  after it in its private structure.
 * @param   cbName  The number of bytes to allocate for the name field
 *                  (including the terminator). Pass zero if RTStrAlloc and
 *                  friends will be used.
 * @param   cbId    The number of bytes to allocate for the ID field. Pass
 *                  zero if RTStrAlloc and friends will be used.
 */
DECLINLINE(PPDMAUDIOHOSTDEV) PDMAudioHostDevAlloc(size_t cb, size_t cbName, size_t cbId)
{
    AssertReturn(cb >= sizeof(PDMAUDIOHOSTDEV), NULL);
    AssertReturn(cb < _4M, NULL);
    AssertReturn(cbName < _4K, NULL);
    AssertReturn(cbId < _16K, NULL);

    PPDMAUDIOHOSTDEV pDev = (PPDMAUDIOHOSTDEV)RTMemAllocZ(RT_ALIGN_Z(cb + cbName + cbId, 64));
    if (pDev)
    {
        pDev->uMagic  = PDMAUDIOHOSTDEV_MAGIC;
        pDev->cbSelf  = (uint32_t)cb;
        RTListInit(&pDev->ListEntry);
        if (cbName)
            pDev->pszName = (char *)pDev + cb;
        if (cbId)
            pDev->pszId   = (char *)pDev + cb + cbName;
    }
    return pDev;
}

/**
 * Frees a host audio device allocated by PDMAudioHostDevAlloc.
 *
 * @param   pDev    The device to free.  NULL is ignored.
 */
DECLINLINE(void) PDMAudioHostDevFree(PPDMAUDIOHOSTDEV pDev)
{
    if (pDev)
    {
        Assert(pDev->uMagic == PDMAUDIOHOSTDEV_MAGIC);
        pDev->uMagic = ~PDMAUDIOHOSTDEV_MAGIC;
        pDev->cbSelf = 0;

        if (pDev->fFlags & PDMAUDIOHOSTDEV_F_NAME_ALLOC)
        {
            RTStrFree(pDev->pszName);
            pDev->pszName = NULL;
        }

        if (pDev->fFlags & PDMAUDIOHOSTDEV_F_ID_ALLOC)
        {
            RTStrFree(pDev->pszId);
            pDev->pszId = NULL;
        }

        RTMemFree(pDev);
    }
}

/**
 * Duplicates a host audio device enumeration entry.
 *
 * @returns Duplicated audio device entry on success, or NULL on failure.
 * @param   pDev            The audio device enum entry to duplicate.
 * @param   fOnlyCoreData
 */
DECLINLINE(PPDMAUDIOHOSTDEV) PDMAudioHostDevDup(PCPDMAUDIOHOSTDEV pDev, bool fOnlyCoreData)
{
    AssertPtrReturn(pDev, NULL);
    Assert(pDev->uMagic == PDMAUDIOHOSTDEV_MAGIC);
    Assert(fOnlyCoreData || !(pDev->fFlags & PDMAUDIOHOSTDEV_F_NO_DUP));

    uint32_t cbToDup = fOnlyCoreData ? sizeof(PDMAUDIOHOSTDEV) : pDev->cbSelf;
    AssertReturn(cbToDup >= sizeof(*pDev), NULL);

    PPDMAUDIOHOSTDEV pDevDup = PDMAudioHostDevAlloc(cbToDup, 0, 0);
    if (pDevDup)
    {
        memcpy(pDevDup, pDev, cbToDup);
        RTListInit(&pDevDup->ListEntry);
        pDevDup->cbSelf = cbToDup;

        if (pDev->pszName)
        {
            uintptr_t off;
            if (   (pDevDup->fFlags & PDMAUDIOHOSTDEV_F_NAME_ALLOC)
                || (off = (uintptr_t)pDev->pszName - (uintptr_t)pDev) >= pDevDup->cbSelf)
            {
                pDevDup->fFlags |= PDMAUDIOHOSTDEV_F_NAME_ALLOC;
                pDevDup->pszName = RTStrDup(pDev->pszName);
                AssertReturnStmt(pDevDup->pszName, PDMAudioHostDevFree(pDevDup), NULL);
            }
            else
                pDevDup->pszName = (char *)pDevDup + off;
        }

        if (pDev->pszId)
        {
            uintptr_t off;
            if (   (pDevDup->fFlags & PDMAUDIOHOSTDEV_F_ID_ALLOC)
                || (off = (uintptr_t)pDev->pszId - (uintptr_t)pDev) >= pDevDup->cbSelf)
            {
                pDevDup->fFlags |= PDMAUDIOHOSTDEV_F_ID_ALLOC;
                pDevDup->pszId   = RTStrDup(pDev->pszId);
                AssertReturnStmt(pDevDup->pszId, PDMAudioHostDevFree(pDevDup), NULL);
            }
            else
                pDevDup->pszId = (char *)pDevDup + off;
        }
    }

    return pDevDup;
}

/**
 * Initializes a host audio device enumeration.
 *
 * @param   pDevEnm     The enumeration to initialize.
 */
DECLINLINE(void) PDMAudioHostEnumInit(PPDMAUDIOHOSTENUM pDevEnm)
{
    AssertPtr(pDevEnm);

    pDevEnm->uMagic   = PDMAUDIOHOSTENUM_MAGIC;
    pDevEnm->cDevices = 0;
    RTListInit(&pDevEnm->LstDevices);
}

/**
 * Deletes the host audio device enumeration and frees all device entries
 * associated with it.
 *
 * The user must call PDMAudioHostEnumInit again to use it again.
 *
 * @param   pDevEnm     The host audio device enumeration to delete.
 */
DECLINLINE(void) PDMAudioHostEnumDelete(PPDMAUDIOHOSTENUM pDevEnm)
{
    if (pDevEnm)
    {
        AssertPtr(pDevEnm);
        AssertReturnVoid(pDevEnm->uMagic == PDMAUDIOHOSTENUM_MAGIC);

        PPDMAUDIOHOSTDEV pDev, pDevNext;
        RTListForEachSafe(&pDevEnm->LstDevices, pDev, pDevNext, PDMAUDIOHOSTDEV, ListEntry)
        {
            RTListNodeRemove(&pDev->ListEntry);

            PDMAudioHostDevFree(pDev);

            pDevEnm->cDevices--;
        }

        /* Sanity. */
        Assert(RTListIsEmpty(&pDevEnm->LstDevices));
        Assert(pDevEnm->cDevices == 0);

        pDevEnm->uMagic = ~PDMAUDIOHOSTENUM_MAGIC;
    }
}

/**
 * Adds an audio device to a device enumeration.
 *
 * @param  pDevEnm              Device enumeration to add device to.
 * @param  pDev                 Device to add. The pointer will be owned by the device enumeration  then.
 */
DECLINLINE(void) PDMAudioHostEnumAppend(PPDMAUDIOHOSTENUM pDevEnm, PPDMAUDIOHOSTDEV pDev)
{
    AssertPtr(pDevEnm);
    AssertPtr(pDev);
    Assert(pDevEnm->uMagic == PDMAUDIOHOSTENUM_MAGIC);

    RTListAppend(&pDevEnm->LstDevices, &pDev->ListEntry);
    pDevEnm->cDevices++;
}

/**
 * Appends copies of matching host device entries from one to another enumeration.
 *
 * @returns VBox status code.
 * @param   pDstDevEnm      The target to append copies of matching device to.
 * @param   pSrcDevEnm      The source to copy matching devices from.
 * @param   enmUsage        The usage to match for copying.
 *                          Use PDMAUDIODIR_INVALID to match all entries.
 * @param   fOnlyCoreData   Set this to only copy the PDMAUDIOHOSTDEV part.
 *                          Careful with passing @c false here as not all
 *                          backends have data that can be copied.
 */
DECLINLINE(int) PDMAudioHostEnumCopy(PPDMAUDIOHOSTENUM pDstDevEnm, PCPDMAUDIOHOSTENUM pSrcDevEnm,
                                     PDMAUDIODIR enmUsage, bool fOnlyCoreData)
{
    AssertPtrReturn(pDstDevEnm, VERR_INVALID_POINTER);
    AssertReturn(pDstDevEnm->uMagic == PDMAUDIOHOSTENUM_MAGIC, VERR_WRONG_ORDER);

    AssertPtrReturn(pSrcDevEnm, VERR_INVALID_POINTER);
    AssertReturn(pSrcDevEnm->uMagic == PDMAUDIOHOSTENUM_MAGIC, VERR_WRONG_ORDER);

    PPDMAUDIOHOSTDEV pSrcDev;
    RTListForEach(&pSrcDevEnm->LstDevices, pSrcDev, PDMAUDIOHOSTDEV, ListEntry)
    {
        if (   enmUsage == pSrcDev->enmUsage
            || enmUsage == PDMAUDIODIR_INVALID /*all*/)
        {
            PPDMAUDIOHOSTDEV pDstDev = PDMAudioHostDevDup(pSrcDev, fOnlyCoreData);
            AssertReturn(pDstDev, VERR_NO_MEMORY);

            PDMAudioHostEnumAppend(pDstDevEnm, pDstDev);
        }
    }

    return VINF_SUCCESS;
}

/**
 * Moves all the device entries from one enumeration to another, destroying the
 * former.
 *
 * @returns VBox status code.
 * @param   pDstDevEnm          The target to put move @a pSrcDevEnm to.  This
 *                              does not need to be initialized, but if it is it
 *                              must not have any device entries.
 * @param   pSrcDevEnm          The source to move from.  This will be empty
 *                              upon successful return.
 */
DECLINLINE(int) PDMAudioHostEnumMove(PPDMAUDIOHOSTENUM pDstDevEnm, PPDMAUDIOHOSTENUM pSrcDevEnm)
{
    AssertPtrReturn(pDstDevEnm, VERR_INVALID_POINTER);
    AssertReturn(pDstDevEnm->uMagic != PDMAUDIOHOSTENUM_MAGIC || pDstDevEnm->cDevices == 0, VERR_WRONG_ORDER);

    AssertPtrReturn(pSrcDevEnm, VERR_INVALID_POINTER);
    AssertReturn(pSrcDevEnm->uMagic == PDMAUDIOHOSTENUM_MAGIC, VERR_WRONG_ORDER);

    pDstDevEnm->uMagic   = PDMAUDIOHOSTENUM_MAGIC;
    RTListInit(&pDstDevEnm->LstDevices);
    pDstDevEnm->cDevices = pSrcDevEnm->cDevices;
    if (pSrcDevEnm->cDevices)
    {
        PPDMAUDIOHOSTDEV pCur;
        while ((pCur = RTListRemoveFirst(&pSrcDevEnm->LstDevices, PDMAUDIOHOSTDEV, ListEntry)) != NULL)
            RTListAppend(&pDstDevEnm->LstDevices, &pCur->ListEntry);
    }
    return VINF_SUCCESS;
}

/**
 * Get the default device with the given usage.
 *
 * This assumes that only one default device per usage is set, if there should
 * be more than one, the first one is returned.
 *
 * @returns Default device if found, or NULL if not.
 * @param   pDevEnm     Device enumeration to get default device for.
 * @param   enmUsage    Usage to get default device for.
 *                      Pass PDMAUDIODIR_INVALID to get the first device with
 *                      either PDMAUDIOHOSTDEV_F_DEFAULT_OUT or
 *                      PDMAUDIOHOSTDEV_F_DEFAULT_IN set.
 */
DECLINLINE(PPDMAUDIOHOSTDEV) PDMAudioHostEnumGetDefault(PCPDMAUDIOHOSTENUM pDevEnm, PDMAUDIODIR enmUsage)
{
    AssertPtrReturn(pDevEnm, NULL);
    AssertReturn(pDevEnm->uMagic == PDMAUDIOHOSTENUM_MAGIC, NULL);

    Assert(enmUsage == PDMAUDIODIR_IN || enmUsage == PDMAUDIODIR_OUT || enmUsage == PDMAUDIODIR_INVALID);
    uint32_t const fFlags = enmUsage == PDMAUDIODIR_IN      ? PDMAUDIOHOSTDEV_F_DEFAULT_IN
                          : enmUsage == PDMAUDIODIR_OUT     ? PDMAUDIOHOSTDEV_F_DEFAULT_OUT
                          : enmUsage == PDMAUDIODIR_INVALID ? PDMAUDIOHOSTDEV_F_DEFAULT_IN | PDMAUDIOHOSTDEV_F_DEFAULT_OUT
                          : 0;

    PPDMAUDIOHOSTDEV pDev;
    RTListForEach(&pDevEnm->LstDevices, pDev, PDMAUDIOHOSTDEV, ListEntry)
    {
        if (pDev->fFlags & fFlags)
        {
            Assert(pDev->enmUsage == enmUsage || pDev->enmUsage == PDMAUDIODIR_DUPLEX || enmUsage == PDMAUDIODIR_INVALID);
            return pDev;
        }
    }

    return NULL;
}

/**
 * Get the number of device with the given usage.
 *
 * @returns Number of matching devices.
 * @param   pDevEnm     Device enumeration to get default device for.
 * @param   enmUsage    Usage to count devices for.
 *                      Pass PDMAUDIODIR_INVALID to get the total number of devices.
 */
DECLINLINE(uint32_t) PDMAudioHostEnumCountMatching(PCPDMAUDIOHOSTENUM pDevEnm, PDMAUDIODIR enmUsage)
{
    AssertPtrReturn(pDevEnm, 0);
    AssertReturn(pDevEnm->uMagic == PDMAUDIOHOSTENUM_MAGIC, 0);

    if (enmUsage == PDMAUDIODIR_INVALID)
        return pDevEnm->cDevices;

    uint32_t        cDevs = 0;
    PPDMAUDIOHOSTDEV pDev;
    RTListForEach(&pDevEnm->LstDevices, pDev, PDMAUDIOHOSTDEV, ListEntry)
    {
        if (enmUsage == pDev->enmUsage)
            cDevs++;
    }

    return cDevs;
}

/** The max string length for all PDMAUDIOHOSTDEV_F_XXX.
 * @sa PDMAudioHostDevFlagsToString */
#define PDMAUDIOHOSTDEV_MAX_FLAGS_STRING_LEN sizeof("DEFAULT_OUT DEFAULT_IN HOTPLUG BUGGY IGNORE LOCKED DEAD NAME_ALLOC ID_ALLOC NO_DUP ")

/**
 * Converts an audio device flags to a string.
 *
 * @returns
 * @param   pszDst      Destination buffer with a size of at least
 *                      PDMAUDIOHOSTDEV_MAX_FLAGS_STRING_LEN bytes (including
 *                      the string terminator).
 * @param   fFlags      Audio flags (PDMAUDIOHOSTDEV_F_XXX) to convert.
 */
DECLINLINE(const char *) PDMAudioHostDevFlagsToString(char pszDst[PDMAUDIOHOSTDEV_MAX_FLAGS_STRING_LEN], uint32_t fFlags)
{
    static const struct { const char *pszMnemonic; uint32_t cchMnemonic; uint32_t fFlag; } s_aFlags[] =
    {
        { RT_STR_TUPLE("DEFAULT_OUT "), PDMAUDIOHOSTDEV_F_DEFAULT_OUT },
        { RT_STR_TUPLE("DEFAULT_IN "),  PDMAUDIOHOSTDEV_F_DEFAULT_IN },
        { RT_STR_TUPLE("HOTPLUG "),     PDMAUDIOHOSTDEV_F_HOTPLUG },
        { RT_STR_TUPLE("BUGGY "),       PDMAUDIOHOSTDEV_F_BUGGY   },
        { RT_STR_TUPLE("IGNORE "),      PDMAUDIOHOSTDEV_F_IGNORE  },
        { RT_STR_TUPLE("LOCKED "),      PDMAUDIOHOSTDEV_F_LOCKED  },
        { RT_STR_TUPLE("DEAD "),        PDMAUDIOHOSTDEV_F_DEAD    },
        { RT_STR_TUPLE("NAME_ALLOC "),  PDMAUDIOHOSTDEV_F_NAME_ALLOC },
        { RT_STR_TUPLE("ID_ALLOC "),    PDMAUDIOHOSTDEV_F_ID_ALLOC },
        { RT_STR_TUPLE("NO_DUP "),      PDMAUDIOHOSTDEV_F_NO_DUP  },
    };
    size_t offDst = 0;
    for (uint32_t i = 0; i < RT_ELEMENTS(s_aFlags); i++)
        if (fFlags & s_aFlags[i].fFlag)
        {
            fFlags &= ~s_aFlags[i].fFlag;
            memcpy(&pszDst[offDst], s_aFlags[i].pszMnemonic, s_aFlags[i].cchMnemonic);
            offDst += s_aFlags[i].cchMnemonic;
        }
    Assert(fFlags == 0);
    Assert(offDst < PDMAUDIOHOSTDEV_MAX_FLAGS_STRING_LEN);

    if (offDst)
        pszDst[offDst - 1] = '\0';
    else
        memcpy(pszDst, "NONE", sizeof("NONE"));
    return pszDst;
}

/**
 * Logs an audio device enumeration.
 *
 * @param  pDevEnm  Device enumeration to log.
 * @param  pszDesc  Logging description (prefix).
 */
DECLINLINE(void) PDMAudioHostEnumLog(PCPDMAUDIOHOSTENUM pDevEnm, const char *pszDesc)
{
#ifdef LOG_ENABLED
    AssertPtrReturnVoid(pDevEnm);
    AssertPtrReturnVoid(pszDesc);
    AssertReturnVoid(pDevEnm->uMagic == PDMAUDIOHOSTENUM_MAGIC);

    if (LogIsEnabled())
    {
        LogFunc(("%s: %RU32 devices\n", pszDesc, pDevEnm->cDevices));

        PPDMAUDIOHOSTDEV pDev;
        RTListForEach(&pDevEnm->LstDevices, pDev, PDMAUDIOHOSTDEV, ListEntry)
        {
            char szFlags[PDMAUDIOHOSTDEV_MAX_FLAGS_STRING_LEN];
            LogFunc(("Device '%s':\n", pDev->pszName));
            LogFunc(("  ID              = %s\n",             pDev->pszId ? pDev->pszId : "<none>"));
            LogFunc(("  Usage           = %s\n",             PDMAudioDirGetName(pDev->enmUsage)));
            LogFunc(("  Flags           = %s\n",             PDMAudioHostDevFlagsToString(szFlags, pDev->fFlags)));
            LogFunc(("  Input channels  = %RU8\n",           pDev->cMaxInputChannels));
            LogFunc(("  Output channels = %RU8\n",           pDev->cMaxOutputChannels));
            LogFunc(("  cbExtra         = %RU32 bytes\n",    pDev->cbSelf - sizeof(PDMAUDIOHOSTDEV)));
        }
    }
#else
    RT_NOREF(pDevEnm, pszDesc);
#endif
}

/** @} */

#endif /* !VBOX_INCLUDED_vmm_pdmaudiohostenuminline_h */
