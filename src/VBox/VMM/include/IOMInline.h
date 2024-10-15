/* $Id: IOMInline.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IOM - Inlined functions.
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

#ifndef VMM_INCLUDED_SRC_include_IOMInline_h
#define VMM_INCLUDED_SRC_include_IOMInline_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/errcore.h>

/** @addtogroup grp_iom_int   Internals
 * @internal
 * @{
 */


/**
 * Gets the I/O port entry for the specified I/O port in the current context.
 *
 * @returns Pointer to I/O port entry.
 * @returns NULL if no port registered.
 *
 * @param   pVM             The cross context VM structure.
 * @param   uPort           The I/O port to lookup.
 * @param   poffPort        Where to return the port offset relative to the
 *                          start of the I/O port range.
 * @param   pidxLastHint    Pointer to IOMCPU::idxIoPortLastRead or
 *                          IOMCPU::idxIoPortLastWrite.
 *
 * @note    In ring-0 it is possible to get an uninitialized entry (pDevIns is
 *          NULL, cPorts is 0), in which case there should be ring-3 handlers
 *          for the entry.  Use IOMIOPORTENTRYR0::idxSelf to get the ring-3
 *          entry.
 *
 * @note    This code is almost identical to iomMmioGetEntry, so keep in sync.
 */
DECLINLINE(CTX_SUFF(PIOMIOPORTENTRY)) iomIoPortGetEntry(PVMCC pVM, RTIOPORT uPort, PRTIOPORT poffPort, uint16_t *pidxLastHint)
{
    Assert(IOM_IS_SHARED_LOCK_OWNER(pVM));

#ifdef IN_RING0
    uint32_t               iEnd      = RT_MIN(pVM->iom.s.cIoPortLookupEntries, pVM->iomr0.s.cIoPortAlloc);
    PCIOMIOPORTLOOKUPENTRY paLookup  = pVM->iomr0.s.paIoPortLookup;
#else
    uint32_t               iEnd      = pVM->iom.s.cIoPortLookupEntries;
    PCIOMIOPORTLOOKUPENTRY paLookup  = pVM->iom.s.paIoPortLookup;
#endif
    if (iEnd > 0)
    {
        uint32_t iFirst = 0;
        uint32_t i      = *pidxLastHint;
        if (i < iEnd)
        { /* likely */ }
        else
            i = iEnd / 2;
        for (;;)
        {
            PCIOMIOPORTLOOKUPENTRY pCur = &paLookup[i];
            if (pCur->uFirstPort > uPort)
            {
                if (i > iFirst)
                    iEnd = i;
                else
                    break;
            }
            else if (pCur->uLastPort < uPort)
            {
                i += 1;
                if (i < iEnd)
                    iFirst = i;
                else
                    break;
            }
            else
            {
                *pidxLastHint = (uint16_t)i;
                *poffPort     = uPort - pCur->uFirstPort;

                /*
                 * Translate the 'idx' member into a pointer.
                 */
                size_t const idx = pCur->idx;
#ifdef IN_RING0
                AssertMsg(idx < pVM->iom.s.cIoPortRegs && idx < pVM->iomr0.s.cIoPortAlloc,
                          ("%#zx vs %#x/%x (port %#x)\n", idx, pVM->iom.s.cIoPortRegs, pVM->iomr0.s.cIoPortMax, uPort));
                if (idx < pVM->iomr0.s.cIoPortAlloc)
                    return &pVM->iomr0.s.paIoPortRegs[idx];
#else
                if (idx < pVM->iom.s.cIoPortRegs)
                    return &pVM->iom.s.paIoPortRegs[idx];
                AssertMsgFailed(("%#zx vs %#x (port %#x)\n", idx, pVM->iom.s.cIoPortRegs, uPort));
#endif
                break;
            }

            i = iFirst + (iEnd - iFirst) / 2;
        }
    }
    *poffPort = 0;
    return NULL;
}


#ifdef VBOX_WITH_STATISTICS
/**
 * Gets the statistics entry for an I/O port.
 *
 * @returns Pointer to stats.  Instead of NULL, a pointer to IoPortDummyStats is
 *          returned, so the caller does not need to check for NULL.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pRegEntry   The I/O port entry to get stats for.
 * @param   offPort     The offset of the  port relative to the start of the
 *                      registration entry.
 */
DECLINLINE(PIOMIOPORTSTATSENTRY) iomIoPortGetStats(PVMCC pVM, CTX_SUFF(PIOMIOPORTENTRY) pRegEntry, uint16_t offPort)
{
    size_t idxStats = pRegEntry->idxStats;
    idxStats += offPort;
# ifdef IN_RING0
    if (idxStats < pVM->iomr0.s.cIoPortStatsAllocation)
        return &pVM->iomr0.s.paIoPortStats[idxStats];
# else
    if (idxStats < pVM->iom.s.cIoPortStats)
        return &pVM->iom.s.paIoPortStats[idxStats];
# endif
    return &pVM->iom.s.IoPortDummyStats;
}
#endif


/**
 * Gets the MMIO region entry for the specified address in the current context.
 *
 * @returns Pointer to MMIO region entry.
 * @returns NULL if no MMIO region registered for the given address.
 *
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys          The address to lookup.
 * @param   poffRegion      Where to return the byte offset into the MMIO
 *                          region that corresponds to @a GCPhys.
 * @param   pidxLastHint    Pointer to IOMCPU::idxMmioLastRead,
 *                          IOMCPU::idxMmioLastWrite, or similar.
 *
 * @note    In ring-0 it is possible to get an uninitialized entry (pDevIns is
 *          NULL, cbRegion is 0), in which case there should be ring-3 handlers
 *          for the entry.  Use IOMMMIOENTRYR0::idxSelf to get the ring-3 entry.
 *
 * @note    This code is almost identical to iomIoPortGetEntry, so keep in sync.
 */
DECLINLINE(CTX_SUFF(PIOMMMIOENTRY)) iomMmioGetEntry(PVMCC pVM, RTGCPHYS GCPhys, PRTGCPHYS poffRegion, uint16_t *pidxLastHint)
{
    Assert(IOM_IS_SHARED_LOCK_OWNER(pVM));

#ifdef IN_RING0
    uint32_t               iEnd      = RT_MIN(pVM->iom.s.cMmioLookupEntries, pVM->iomr0.s.cMmioAlloc);
    PCIOMMMIOLOOKUPENTRY   paLookup  = pVM->iomr0.s.paMmioLookup;
#else
    uint32_t               iEnd      = pVM->iom.s.cMmioLookupEntries;
    PCIOMMMIOLOOKUPENTRY   paLookup  = pVM->iom.s.paMmioLookup;
#endif
    if (iEnd > 0)
    {
        uint32_t iFirst = 0;
        uint32_t i      = *pidxLastHint;
        if (i < iEnd)
        { /* likely */ }
        else
            i = iEnd / 2;
        for (;;)
        {
            PCIOMMMIOLOOKUPENTRY pCur = &paLookup[i];
            if (pCur->GCPhysFirst > GCPhys)
            {
                if (i > iFirst)
                    iEnd = i;
                else
                    break;
            }
            else if (pCur->GCPhysLast < GCPhys)
            {
                i += 1;
                if (i < iEnd)
                    iFirst = i;
                else
                    break;
            }
            else
            {
                *pidxLastHint = (uint16_t)i;
                *poffRegion   = GCPhys - pCur->GCPhysFirst;

                /*
                 * Translate the 'idx' member into a pointer.
                 */
                size_t const idx = pCur->idx;
#ifdef IN_RING0
                AssertMsg(idx < pVM->iom.s.cMmioRegs && idx < pVM->iomr0.s.cMmioAlloc,
                          ("%#zx vs %#x/%x (GCPhys=%RGp)\n", idx, pVM->iom.s.cMmioRegs, pVM->iomr0.s.cMmioMax, GCPhys));
                if (idx < pVM->iomr0.s.cMmioAlloc)
                    return &pVM->iomr0.s.paMmioRegs[idx];
#else
                if (idx < pVM->iom.s.cMmioRegs)
                    return &pVM->iom.s.paMmioRegs[idx];
                AssertMsgFailed(("%#zx vs %#x (GCPhys=%RGp)\n", idx, pVM->iom.s.cMmioRegs, GCPhys));
#endif
                break;
            }

            i = iFirst + (iEnd - iFirst) / 2;
        }
    }
    *poffRegion = 0;
    return NULL;
}


#ifdef VBOX_WITH_STATISTICS
/**
 * Gets the statistics entry for an MMIO region.
 *
 * @returns Pointer to stats.  Instead of NULL, a pointer to MmioDummyStats is
 *          returned, so the caller does not need to check for NULL.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pRegEntry   The I/O port entry to get stats for.
 */
DECLINLINE(PIOMMMIOSTATSENTRY) iomMmioGetStats(PVMCC pVM, CTX_SUFF(PIOMMMIOENTRY) pRegEntry)
{
    size_t idxStats = pRegEntry->idxStats;
# ifdef IN_RING0
    if (idxStats < pVM->iomr0.s.cMmioStatsAllocation)
        return &pVM->iomr0.s.paMmioStats[idxStats];
# else
    if (idxStats < pVM->iom.s.cMmioStats)
        return &pVM->iom.s.paMmioStats[idxStats];
# endif
    return &pVM->iom.s.MmioDummyStats;
}
#endif

/** @}  */

#endif /* !VMM_INCLUDED_SRC_include_IOMInline_h */

