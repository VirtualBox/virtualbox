/* $Id: IOMAll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IOM - Input / Output Monitor - Any Context.
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
#define LOG_GROUP LOG_GROUP_IOM_IOPORT
#include <VBox/vmm/iom.h>
#include <VBox/vmm/mm.h>
#include <VBox/param.h>
#include "IOMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include "IOMInline.h"


/**
 * Reads an I/O port register.
 *
 * @returns Strict VBox status code. Informational status codes other than the one documented
 *          here are to be treated as internal failure. Use IOM_SUCCESS() to check for success.
 * @retval  VINF_SUCCESS                Success.
 * @retval  VINF_EM_FIRST-VINF_EM_LAST  Success with some exceptions (see IOM_SUCCESS()), the
 *                                      status code must be passed on to EM.
 * @retval  VINF_IOM_R3_IOPORT_READ     Defer the read to ring-3. (R0/RC only)
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   Port        The port to read.
 * @param   pu32Value   Where to store the value read.
 * @param   cbValue     The size of the register to read in bytes. 1, 2 or 4 bytes.
 */
VMMDECL(VBOXSTRICTRC) IOMIOPortRead(PVMCC pVM, PVMCPU pVCpu, RTIOPORT Port, uint32_t *pu32Value, size_t cbValue)
{
    STAM_COUNTER_INC(&pVM->iom.s.StatIoPortIn);
    Assert(pVCpu->iom.s.PendingIOPortWrite.cbValue == 0);

/** @todo should initialize *pu32Value here because it can happen that some
 *        handle is buggy and doesn't handle all cases. */

    /* For lookups we need to share lock IOM. */
    int rc2 = IOM_LOCK_SHARED(pVM);
    if (RT_SUCCESS(rc2))
    { /* likely */ }
#ifndef IN_RING3
    else if (rc2 == VERR_SEM_BUSY)
        return VINF_IOM_R3_IOPORT_READ;
#endif
    else
        AssertMsgFailedReturn(("rc2=%Rrc\n", rc2), rc2);

    /*
     * Get the entry for the current context.
     */
    uint16_t offPort;
    CTX_SUFF(PIOMIOPORTENTRY) pRegEntry = iomIoPortGetEntry(pVM, Port, &offPort, &pVCpu->iom.s.idxIoPortLastRead);
    if (pRegEntry)
    {
#ifdef VBOX_WITH_STATISTICS
        PIOMIOPORTSTATSENTRY  pStats    = iomIoPortGetStats(pVM, pRegEntry, offPort);
#endif

        /*
         * Found an entry, get the data so we can leave the IOM lock.
         */
        uint16_t const    fFlags        = pRegEntry->fFlags;
        PFNIOMIOPORTNEWIN pfnInCallback = pRegEntry->pfnInCallback;
        PPDMDEVINS        pDevIns       = pRegEntry->pDevIns;
#ifndef IN_RING3
        if (   pfnInCallback
            && pDevIns
            && pRegEntry->cPorts > 0)
        { /* likely */ }
        else
        {
            STAM_COUNTER_INC(&pStats->InRZToR3);
            IOM_UNLOCK_SHARED(pVM);
            return VINF_IOM_R3_IOPORT_READ;
        }
#endif
        void           *pvUser       = pRegEntry->pvUser;
        IOM_UNLOCK_SHARED(pVM);
        AssertPtr(pDevIns);
        AssertPtr(pfnInCallback);

        /*
         * Call the device.
         */
        VBOXSTRICTRC rcStrict = PDMCritSectEnter(pVM, pDevIns->CTX_SUFF(pCritSectRo), VINF_IOM_R3_IOPORT_READ);
        if (rcStrict == VINF_SUCCESS)
        {
            STAM_PROFILE_START(&pStats->CTX_SUFF_Z(ProfIn), a);
            rcStrict = pfnInCallback(pDevIns, pvUser, fFlags & IOM_IOPORT_F_ABS ? Port : offPort, pu32Value, (unsigned)cbValue);
            STAM_PROFILE_STOP(&pStats->CTX_SUFF_Z(ProfIn), a);
            PDMCritSectLeave(pVM, pDevIns->CTX_SUFF(pCritSectRo));

#ifndef IN_RING3
            if (rcStrict == VINF_IOM_R3_IOPORT_READ)
                STAM_COUNTER_INC(&pStats->InRZToR3);
            else
#endif
            {
                STAM_COUNTER_INC(&pStats->CTX_SUFF_Z(In));
                STAM_COUNTER_INC(&iomIoPortGetStats(pVM, pRegEntry, 0)->Total);
                if (rcStrict == VERR_IOM_IOPORT_UNUSED)
                {
                    /* make return value */
                    rcStrict = VINF_SUCCESS;
                    switch (cbValue)
                    {
                        case 1: *(uint8_t  *)pu32Value = 0xff; break;
                        case 2: *(uint16_t *)pu32Value = 0xffff; break;
                        case 4: *(uint32_t *)pu32Value = UINT32_C(0xffffffff); break;
                        default:
                            AssertMsgFailedReturn(("Invalid I/O port size %d. Port=%d\n", cbValue, Port), VERR_IOM_INVALID_IOPORT_SIZE);
                    }
                }
           }
            Log3(("IOMIOPortRead: Port=%RTiop *pu32=%08RX32 cb=%d rc=%Rrc\n", Port, *pu32Value, cbValue, VBOXSTRICTRC_VAL(rcStrict)));
            STAM_COUNTER_INC(&iomIoPortGetStats(pVM, pRegEntry, 0)->Total);
        }
        else
            STAM_COUNTER_INC(&pStats->InRZToR3);
        return rcStrict;
    }

    /*
     * Ok, no handler for this port.
     */
    IOM_UNLOCK_SHARED(pVM);
    switch (cbValue)
    {
        case 1: *(uint8_t  *)pu32Value = 0xff; break;
        case 2: *(uint16_t *)pu32Value = 0xffff; break;
        case 4: *(uint32_t *)pu32Value = UINT32_C(0xffffffff); break;
        default:
            AssertMsgFailedReturn(("Invalid I/O port size %d. Port=%d\n", cbValue, Port), VERR_IOM_INVALID_IOPORT_SIZE);
    }
    Log3(("IOMIOPortRead: Port=%RTiop *pu32=%08RX32 cb=%d rc=VINF_SUCCESS\n", Port, *pu32Value, cbValue));
    return VINF_SUCCESS;
}


/**
 * Reads the string buffer of an I/O port register.
 *
 * @returns Strict VBox status code. Informational status codes other than the one documented
 *          here are to be treated as internal failure. Use IOM_SUCCESS() to check for success.
 * @retval  VINF_SUCCESS                Success or no string I/O callback in
 *                                      this context.
 * @retval  VINF_EM_FIRST-VINF_EM_LAST  Success with some exceptions (see IOM_SUCCESS()), the
 *                                      status code must be passed on to EM.
 * @retval  VINF_IOM_R3_IOPORT_READ     Defer the read to ring-3. (R0/RC only)
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   uPort       The port to read.
 * @param   pvDst       Pointer to the destination buffer.
 * @param   pcTransfers Pointer to the number of transfer units to read, on return remaining transfer units.
 * @param   cb          Size of the transfer unit (1, 2 or 4 bytes).
 */
VMM_INT_DECL(VBOXSTRICTRC) IOMIOPortReadString(PVMCC pVM, PVMCPU pVCpu, RTIOPORT uPort,
                                               void *pvDst, uint32_t *pcTransfers, unsigned cb)
{
    STAM_COUNTER_INC(&pVM->iom.s.StatIoPortInS);
    Assert(pVCpu->iom.s.PendingIOPortWrite.cbValue == 0);

    /* For lookups we need to share lock IOM. */
    int rc2 = IOM_LOCK_SHARED(pVM);
    if (RT_SUCCESS(rc2))
    { /* likely */ }
#ifndef IN_RING3
    else if (rc2 == VERR_SEM_BUSY)
        return VINF_IOM_R3_IOPORT_READ;
#endif
    else
        AssertMsgFailedReturn(("rc2=%Rrc\n", rc2), rc2);

    const uint32_t cRequestedTransfers = *pcTransfers;
    Assert(cRequestedTransfers > 0);

    /*
     * Get the entry for the current context.
     */
    uint16_t offPort;
    CTX_SUFF(PIOMIOPORTENTRY) pRegEntry = iomIoPortGetEntry(pVM, uPort, &offPort, &pVCpu->iom.s.idxIoPortLastReadStr);
    if (pRegEntry)
    {
#ifdef VBOX_WITH_STATISTICS
        PIOMIOPORTSTATSENTRY  pStats    = iomIoPortGetStats(pVM, pRegEntry, offPort);
#endif

        /*
         * Found an entry, get the data so we can leave the IOM lock.
         */
        uint16_t const          fFlags           = pRegEntry->fFlags;
        PFNIOMIOPORTNEWINSTRING pfnInStrCallback = pRegEntry->pfnInStrCallback;
        PFNIOMIOPORTNEWIN       pfnInCallback    = pRegEntry->pfnInCallback;
        PPDMDEVINS              pDevIns          = pRegEntry->pDevIns;
#ifndef IN_RING3
        if (   pfnInCallback
            && pDevIns
            && pRegEntry->cPorts > 0)
        { /* likely */ }
        else
        {
            STAM_COUNTER_INC(&pStats->InRZToR3);
            IOM_UNLOCK_SHARED(pVM);
            return VINF_IOM_R3_IOPORT_READ;
        }
#endif
        void           *pvUser       = pRegEntry->pvUser;
        IOM_UNLOCK_SHARED(pVM);
        AssertPtr(pDevIns);
        AssertPtr(pfnInCallback);

        /*
         * Call the device.
         */
        VBOXSTRICTRC rcStrict = PDMCritSectEnter(pVM, pDevIns->CTX_SUFF(pCritSectRo), VINF_IOM_R3_IOPORT_READ);
        if (rcStrict == VINF_SUCCESS)
        {
            /*
             * First using the string I/O callback.
             */
            if (pfnInStrCallback)
            {
                STAM_PROFILE_START(&pStats->CTX_SUFF_Z(ProfIn), a);
                rcStrict = pfnInStrCallback(pDevIns, pvUser, fFlags & IOM_IOPORT_F_ABS ? uPort : offPort,
                                            (uint8_t *)pvDst, pcTransfers, cb);
                STAM_PROFILE_STOP(&pStats->CTX_SUFF_Z(ProfIn), a);
            }

            /*
             * Then doing the single I/O fallback.
             */
            if (   *pcTransfers > 0
                && rcStrict == VINF_SUCCESS)
            {
                pvDst = (uint8_t *)pvDst + (cRequestedTransfers - *pcTransfers) * cb;
                do
                {
                    uint32_t u32Value = 0;
                    STAM_PROFILE_START(&pStats->CTX_SUFF_Z(ProfIn), a);
                    rcStrict = pfnInCallback(pDevIns, pvUser, fFlags & IOM_IOPORT_F_ABS ? uPort : offPort, &u32Value, cb);
                    STAM_PROFILE_STOP(&pStats->CTX_SUFF_Z(ProfIn), a);
                    if (rcStrict == VERR_IOM_IOPORT_UNUSED)
                    {
                        u32Value = UINT32_MAX;
                        rcStrict = VINF_SUCCESS;
                    }
                    if (IOM_SUCCESS(rcStrict))
                    {
                        switch (cb)
                        {
                            case 4: *(uint32_t *)pvDst =           u32Value; pvDst = (uint8_t *)pvDst + 4; break;
                            case 2: *(uint16_t *)pvDst = (uint16_t)u32Value; pvDst = (uint8_t *)pvDst + 2; break;
                            case 1: *(uint8_t  *)pvDst = (uint8_t )u32Value; pvDst = (uint8_t *)pvDst + 1; break;
                            default: AssertFailed();
                        }
                        *pcTransfers -= 1;
                    }
                } while (   *pcTransfers > 0
                         && rcStrict == VINF_SUCCESS);
            }
            PDMCritSectLeave(pVM, pDevIns->CTX_SUFF(pCritSectRo));

#ifdef VBOX_WITH_STATISTICS
# ifndef IN_RING3
            if (rcStrict == VINF_IOM_R3_IOPORT_READ)
                STAM_COUNTER_INC(&pStats->InRZToR3);
            else
# endif
            {
                STAM_COUNTER_INC(&pStats->CTX_SUFF_Z(In));
                STAM_COUNTER_INC(&iomIoPortGetStats(pVM, pRegEntry, 0)->Total);
            }
#endif
            Log3(("IOMIOPortReadStr: uPort=%RTiop pvDst=%p pcTransfer=%p:{%#x->%#x} cb=%d rc=%Rrc\n",
                  uPort, pvDst, pcTransfers, cRequestedTransfers, *pcTransfers, cb, VBOXSTRICTRC_VAL(rcStrict)));
        }
#ifndef IN_RING3
        else
            STAM_COUNTER_INC(&pStats->InRZToR3);
#endif
        return rcStrict;
    }

    /*
     * Ok, no handler for this port.
     */
    IOM_UNLOCK_SHARED(pVM);
    *pcTransfers = 0;
    memset(pvDst, 0xff, cRequestedTransfers * cb);
    Log3(("IOMIOPortReadStr: uPort=%RTiop (unused) pvDst=%p pcTransfer=%p:{%#x->%#x} cb=%d rc=VINF_SUCCESS\n",
          uPort, pvDst, pcTransfers, cRequestedTransfers, *pcTransfers, cb));
    return VINF_SUCCESS;
}


#ifndef IN_RING3
/**
 * Defers a pending I/O port write to ring-3.
 *
 * @returns VINF_IOM_R3_IOPORT_COMMIT_WRITE
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   Port        The port to write to.
 * @param   u32Value    The value to write.
 * @param   cbValue     The size of the value (1, 2, 4).
 */
static VBOXSTRICTRC iomIOPortRing3WritePending(PVMCPU pVCpu, RTIOPORT Port, uint32_t u32Value, size_t cbValue)
{
    Log5(("iomIOPortRing3WritePending: %#x LB %u -> %RTiop\n", u32Value, cbValue, Port));
    AssertReturn(pVCpu->iom.s.PendingIOPortWrite.cbValue == 0, VERR_IOM_IOPORT_IPE_1);
    pVCpu->iom.s.PendingIOPortWrite.IOPort   = Port;
    pVCpu->iom.s.PendingIOPortWrite.u32Value = u32Value;
    pVCpu->iom.s.PendingIOPortWrite.cbValue  = (uint32_t)cbValue;
    VMCPU_FF_SET(pVCpu, VMCPU_FF_IOM);
    return VINF_IOM_R3_IOPORT_COMMIT_WRITE;
}
#endif


/**
 * Writes to an I/O port register.
 *
 * @returns Strict VBox status code. Informational status codes other than the one documented
 *          here are to be treated as internal failure. Use IOM_SUCCESS() to check for success.
 * @retval  VINF_SUCCESS                Success.
 * @retval  VINF_EM_FIRST-VINF_EM_LAST  Success with some exceptions (see IOM_SUCCESS()), the
 *                                      status code must be passed on to EM.
 * @retval  VINF_IOM_R3_IOPORT_WRITE    Defer the write to ring-3. (R0/RC only)
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   Port        The port to write to.
 * @param   u32Value    The value to write.
 * @param   cbValue     The size of the register to read in bytes. 1, 2 or 4 bytes.
 */
VMMDECL(VBOXSTRICTRC) IOMIOPortWrite(PVMCC pVM, PVMCPU pVCpu, RTIOPORT Port, uint32_t u32Value, size_t cbValue)
{
    STAM_COUNTER_INC(&pVM->iom.s.StatIoPortOut);
#ifndef IN_RING3
    Assert(pVCpu->iom.s.PendingIOPortWrite.cbValue == 0);
#endif

    /* For lookups we need to share lock IOM. */
    int rc2 = IOM_LOCK_SHARED(pVM);
    if (RT_SUCCESS(rc2))
    { /* likely */ }
#ifndef IN_RING3
    else if (rc2 == VERR_SEM_BUSY)
        return iomIOPortRing3WritePending(pVCpu, Port, u32Value, cbValue);
#endif
    else
        AssertMsgFailedReturn(("rc2=%Rrc\n", rc2), rc2);

    /*
     * Get the entry for the current context.
     */
    uint16_t offPort;
    CTX_SUFF(PIOMIOPORTENTRY) pRegEntry = iomIoPortGetEntry(pVM, Port, &offPort, &pVCpu->iom.s.idxIoPortLastWrite);
    if (pRegEntry)
    {
#ifdef VBOX_WITH_STATISTICS
        PIOMIOPORTSTATSENTRY  pStats    = iomIoPortGetStats(pVM, pRegEntry, offPort);
#endif

        /*
         * Found an entry, get the data so we can leave the IOM lock.
         */
        uint16_t const     fFlags           = pRegEntry->fFlags;
        PFNIOMIOPORTNEWOUT pfnOutCallback   = pRegEntry->pfnOutCallback;
        PPDMDEVINS         pDevIns          = pRegEntry->pDevIns;
#ifndef IN_RING3
        if (   pfnOutCallback
            && pDevIns
            && pRegEntry->cPorts > 0)
        { /* likely */ }
        else
        {
            IOM_UNLOCK_SHARED(pVM);
            STAM_COUNTER_INC(&pStats->OutRZToR3);
            return iomIOPortRing3WritePending(pVCpu, Port, u32Value, cbValue);
        }
#endif
        void           *pvUser       = pRegEntry->pvUser;
        IOM_UNLOCK_SHARED(pVM);
        AssertPtr(pDevIns);
        AssertPtr(pfnOutCallback);

        /*
         * Call the device.
         */
        VBOXSTRICTRC rcStrict = PDMCritSectEnter(pVM, pDevIns->CTX_SUFF(pCritSectRo), VINF_IOM_R3_IOPORT_WRITE);
        if (rcStrict == VINF_SUCCESS)
        {
            STAM_PROFILE_START(&pStats->CTX_SUFF_Z(ProfOut), a);
            rcStrict = pfnOutCallback(pDevIns, pvUser, fFlags & IOM_IOPORT_F_ABS ? Port : offPort, u32Value, (unsigned)cbValue);
            STAM_PROFILE_STOP(&pStats->CTX_SUFF_Z(ProfOut), a);

            PDMCritSectLeave(pVM, pDevIns->CTX_SUFF(pCritSectRo));

#ifdef VBOX_WITH_STATISTICS
# ifndef IN_RING3
            if (rcStrict != VINF_IOM_R3_IOPORT_WRITE)
# endif
            {
                STAM_COUNTER_INC(&pStats->CTX_SUFF_Z(Out));
                STAM_COUNTER_INC(&iomIoPortGetStats(pVM, pRegEntry, 0)->Total);
            }
#endif
            Log3(("IOMIOPortWrite: Port=%RTiop u32=%08RX32 cb=%d rc=%Rrc\n", Port, u32Value, cbValue, VBOXSTRICTRC_VAL(rcStrict)));
        }
#ifndef IN_RING3
        if (rcStrict == VINF_IOM_R3_IOPORT_WRITE)
        {
            STAM_COUNTER_INC(&pStats->OutRZToR3);
            return iomIOPortRing3WritePending(pVCpu, Port, u32Value, cbValue);
        }
#endif
        return rcStrict;
    }

    /*
     * Ok, no handler for that port.
     */
    IOM_UNLOCK_SHARED(pVM);
    Log3(("IOMIOPortWrite: Port=%RTiop u32=%08RX32 cb=%d nop\n", Port, u32Value, cbValue));
    return VINF_SUCCESS;
}


/**
 * Writes the string buffer of an I/O port register.
 *
 * @returns Strict VBox status code. Informational status codes other than the one documented
 *          here are to be treated as internal failure. Use IOM_SUCCESS() to check for success.
 * @retval  VINF_SUCCESS                Success or no string I/O callback in
 *                                      this context.
 * @retval  VINF_EM_FIRST-VINF_EM_LAST  Success with some exceptions (see IOM_SUCCESS()), the
 *                                      status code must be passed on to EM.
 * @retval  VINF_IOM_R3_IOPORT_WRITE    Defer the write to ring-3. (R0/RC only)
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   uPort       The port to write to.
 * @param   pvSrc       The guest page to read from.
 * @param   pcTransfers Pointer to the number of transfer units to write, on
 *                      return remaining transfer units.
 * @param   cb          Size of the transfer unit (1, 2 or 4 bytes).
 */
VMM_INT_DECL(VBOXSTRICTRC) IOMIOPortWriteString(PVMCC pVM, PVMCPU pVCpu, RTIOPORT uPort, void const *pvSrc,
                                                uint32_t *pcTransfers, unsigned cb)
{
    STAM_COUNTER_INC(&pVM->iom.s.StatIoPortOutS);
    Assert(pVCpu->iom.s.PendingIOPortWrite.cbValue == 0);
    Assert(cb == 1 || cb == 2 || cb == 4);

    /* Take the IOM lock before performing any device I/O. */
    int rc2 = IOM_LOCK_SHARED(pVM);
    if (RT_SUCCESS(rc2))
    { /* likely */ }
#ifndef IN_RING3
    else if (rc2 == VERR_SEM_BUSY)
        return VINF_IOM_R3_IOPORT_WRITE;
#endif
    else
        AssertMsgFailedReturn(("rc2=%Rrc\n", rc2), rc2);

    const uint32_t cRequestedTransfers = *pcTransfers;
    Assert(cRequestedTransfers > 0);

    /*
     * Get the entry for the current context.
     */
    uint16_t offPort;
    CTX_SUFF(PIOMIOPORTENTRY) pRegEntry = iomIoPortGetEntry(pVM, uPort, &offPort, &pVCpu->iom.s.idxIoPortLastWriteStr);
    if (pRegEntry)
    {
#ifdef VBOX_WITH_STATISTICS
        PIOMIOPORTSTATSENTRY  pStats    = iomIoPortGetStats(pVM, pRegEntry, offPort);
#endif

        /*
         * Found an entry, get the data so we can leave the IOM lock.
         */
        uint16_t const             fFlags            = pRegEntry->fFlags;
        PFNIOMIOPORTNEWOUTSTRING   pfnOutStrCallback = pRegEntry->pfnOutStrCallback;
        PFNIOMIOPORTNEWOUT         pfnOutCallback    = pRegEntry->pfnOutCallback;
        PPDMDEVINS                 pDevIns           = pRegEntry->pDevIns;
#ifndef IN_RING3
        if (   pfnOutCallback
            && pDevIns
            && pRegEntry->cPorts > 0)
        { /* likely */ }
        else
        {
            IOM_UNLOCK_SHARED(pVM);
            STAM_COUNTER_INC(&pStats->OutRZToR3);
            return VINF_IOM_R3_IOPORT_WRITE;
        }
#endif
        void           *pvUser       = pRegEntry->pvUser;
        IOM_UNLOCK_SHARED(pVM);
        AssertPtr(pDevIns);
        AssertPtr(pfnOutCallback);

        /*
         * Call the device.
         */
        VBOXSTRICTRC rcStrict = PDMCritSectEnter(pVM, pDevIns->CTX_SUFF(pCritSectRo), VINF_IOM_R3_IOPORT_WRITE);
        if (rcStrict == VINF_SUCCESS)
        {
            /*
             * First using string I/O if possible.
             */
            if (pfnOutStrCallback)
            {
                STAM_PROFILE_START(&pStats->CTX_SUFF_Z(ProfOut), a);
                rcStrict = pfnOutStrCallback(pDevIns, pvUser, fFlags & IOM_IOPORT_F_ABS ? uPort : offPort,
                                             (uint8_t const *)pvSrc, pcTransfers, cb);
                STAM_PROFILE_STOP(&pStats->CTX_SUFF_Z(ProfOut), a);
            }

            /*
             * Then doing the single I/O fallback.
             */
            if (   *pcTransfers > 0
                && rcStrict == VINF_SUCCESS)
            {
                pvSrc = (uint8_t *)pvSrc + (cRequestedTransfers - *pcTransfers) * cb;
                do
                {
                    uint32_t u32Value;
                    switch (cb)
                    {
                        case 4: u32Value = *(uint32_t *)pvSrc; pvSrc = (uint8_t const *)pvSrc + 4; break;
                        case 2: u32Value = *(uint16_t *)pvSrc; pvSrc = (uint8_t const *)pvSrc + 2; break;
                        case 1: u32Value = *(uint8_t  *)pvSrc; pvSrc = (uint8_t const *)pvSrc + 1; break;
                        default: AssertFailed(); u32Value = UINT32_MAX;
                    }
                    STAM_PROFILE_START(&pStats->CTX_SUFF_Z(ProfOut), a);
                    rcStrict = pfnOutCallback(pDevIns, pvUser, fFlags & IOM_IOPORT_F_ABS ? uPort : offPort, u32Value, cb);
                    STAM_PROFILE_STOP(&pStats->CTX_SUFF_Z(ProfOut), a);
                    if (IOM_SUCCESS(rcStrict))
                        *pcTransfers -= 1;
                } while (   *pcTransfers > 0
                         && rcStrict == VINF_SUCCESS);
            }

            PDMCritSectLeave(pVM, pDevIns->CTX_SUFF(pCritSectRo));

#ifdef VBOX_WITH_STATISTICS
# ifndef IN_RING3
            if (rcStrict == VINF_IOM_R3_IOPORT_WRITE)
                STAM_COUNTER_INC(&pStats->OutRZToR3);
            else
# endif
            {
                STAM_COUNTER_INC(&pStats->CTX_SUFF_Z(Out));
                STAM_COUNTER_INC(&iomIoPortGetStats(pVM, pRegEntry, 0)->Total);
            }
#endif
            Log3(("IOMIOPortWriteStr: uPort=%RTiop pvSrc=%p pcTransfer=%p:{%#x->%#x} cb=%d rcStrict=%Rrc\n",
                  uPort, pvSrc, pcTransfers, cRequestedTransfers, *pcTransfers, cb, VBOXSTRICTRC_VAL(rcStrict)));
        }
#ifndef IN_RING3
        else
            STAM_COUNTER_INC(&pStats->OutRZToR3);
#endif
        return rcStrict;
    }

    /*
     * Ok, no handler for this port.
     */
    IOM_UNLOCK_SHARED(pVM);
    *pcTransfers = 0;
    Log3(("IOMIOPortWriteStr: uPort=%RTiop (unused) pvSrc=%p pcTransfer=%p:{%#x->%#x} cb=%d rc=VINF_SUCCESS\n",
          uPort, pvSrc, pcTransfers, cRequestedTransfers, *pcTransfers, cb));
    return VINF_SUCCESS;
}

