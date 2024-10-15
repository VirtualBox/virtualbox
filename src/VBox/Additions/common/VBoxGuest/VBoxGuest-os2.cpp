/* $Id: VBoxGuest-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuest - OS/2 specifics.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
 * ---------------------------------------------------------------------------
 * This code is based on:
 *
 * VBoxDrv - OS/2 specifics.
 *
 * Copyright (c) 2007-2012 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <os2ddk/bsekee.h>

#include "VBoxGuestInternal.h"
#include <VBox/version.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/log.h>
#include <iprt/memobj.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/process.h>
#include <iprt/spinlock.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Device extention & session data association structure.
 */
static VBOXGUESTDEVEXT      g_DevExt;
/** The memory object for the MMIO memory.  */
static RTR0MEMOBJ           g_MemObjMMIO = NIL_RTR0MEMOBJ;
/** The memory mapping object the MMIO memory. */
static RTR0MEMOBJ           g_MemMapMMIO = NIL_RTR0MEMOBJ;

/** Spinlock protecting g_apSessionHashTab. */
static RTSPINLOCK           g_Spinlock = NIL_RTSPINLOCK;
/** Hash table */
static PVBOXGUESTSESSION    g_apSessionHashTab[19];
/** Calculates the index into g_apSessionHashTab.*/
#define SESSION_HASH(sfn) ((sfn) % RT_ELEMENTS(g_apSessionHashTab))

RT_C_DECLS_BEGIN
/* Defined in VBoxGuestA-os2.asm */
extern uint32_t             g_PhysMMIOBase;
extern uint32_t             g_cbMMIO; /* 0 currently not set. */
extern uint16_t             g_IOPortBase;
extern uint8_t              g_bInterruptLine;
extern uint8_t              g_bPciBusNo;
extern uint8_t              g_bPciDevFunNo;
extern RTFAR16              g_fpfnVBoxGuestOs2IDCService16;
extern RTFAR16              g_fpfnVBoxGuestOs2IDCService16Asm;
#ifdef DEBUG_READ
/* (debugging) */
extern uint16_t             g_offLogHead;
extern uint16_t volatile    g_offLogTail;
extern uint16_t const       g_cchLogMax;
extern char                 g_szLog[];
#endif
/* (init only:) */
extern char                 g_szInitText[];
extern uint16_t             g_cchInitText;
extern uint16_t             g_cchInitTextMax;
RT_C_DECLS_END


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int vgdrvOS2MapMemory(void);
static VBOXOSTYPE vgdrvOS2DetectVersion(void);

/* in VBoxGuestA-os2.asm */
DECLASM(int) vgdrvOS2DevHlpSetIRQ(uint8_t bIRQ);


/**
 * 32-bit Ring-0 initialization.
 *
 * This is called from VBoxGuestA-os2.asm upon the first open call to the vboxgst$ device.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   pszArgs     Pointer to the device arguments.
 */
DECLASM(int) vgdrvOS2Init(const char *pszArgs)
{
    //Log(("vgdrvOS2Init: pszArgs='%s' MMIO=0x%RX32 IOPort=0x%RX16 Int=%#x Bus=%#x Dev=%#x Fun=%d\n",
    //     pszArgs, g_PhysMMIOBase, g_IOPortBase, g_bInterruptLine, g_bPciBusNo, g_bPciDevFunNo >> 3, g_bPciDevFunNo & 7));

    /*
     * Initialize the runtime.
     */
    int rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        /*
         * Process the command line.
         */
        bool fVerbose = true;
        if (pszArgs)
        {
            char ch;
            while ((ch = *pszArgs++) != '\0')
                if (ch == '-' || ch == '/')
                {
                    ch = *pszArgs++;
                    if (ch == 'Q' || ch == 'q')
                        fVerbose = false;
                    else if (ch == 'V' || ch == 'v')
                        fVerbose = true;
                    else if (ch == '\0')
                        break;
                    /*else: ignore stuff we don't know what is */
                }
                /* else: skip spaces and unknown stuff */
        }

        /*
         * Map the MMIO memory if found.
         */
        rc = vgdrvOS2MapMemory();
        if (RT_SUCCESS(rc))
        {
            /*
             * Initialize the device extension.
             */
            if (g_MemMapMMIO != NIL_RTR0MEMOBJ)
                rc = VGDrvCommonInitDevExt(&g_DevExt, g_IOPortBase,
                                           NULL /*pvMmioReq*/,
                                           RTR0MemObjAddress(g_MemMapMMIO),
                                           RTR0MemObjSize(g_MemMapMMIO),
                                           vgdrvOS2DetectVersion(),
                                           0);
            else
                rc = VGDrvCommonInitDevExt(&g_DevExt, g_IOPortBase, NULL /*pvMmioReq*/, NULL, 0, vgdrvOS2DetectVersion(), 0);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Initialize the session hash table.
                 */
                rc = RTSpinlockCreate(&g_Spinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "VBoxGuestOS2");
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Configure the interrupt handler.
                     */
                    if (g_bInterruptLine)
                    {
                        rc = vgdrvOS2DevHlpSetIRQ(g_bInterruptLine);
                        if (rc)
                        {
                            Log(("vgdrvOS2DevHlpSetIRQ(%d) -> %d\n", g_bInterruptLine, rc));
                            rc = RTErrConvertFromOS2(rc);
                        }
                    }
                    if (RT_SUCCESS(rc))
                    {
                        /*
                         * Read host configuration.
                         */
                        VGDrvCommonProcessOptionsFromHost(&g_DevExt);

                        /*
                         * Success
                         */
                        if (fVerbose)
                        {
                            strcpy(&g_szInitText[0],
                                   "\r\n"
                                   "VirtualBox Guest Additions Driver for OS/2 version " VBOX_VERSION_STRING "\r\n"
                                   "Copyright (C) 2008-" VBOX_C_YEAR " " VBOX_VENDOR "\r\n");
                            g_cchInitText = strlen(&g_szInitText[0]);
                        }
                        Log(("vgdrvOS2Init: Successfully loaded\n%s", g_szInitText));
                        return VINF_SUCCESS;
                    }

                    g_cchInitText = RTStrPrintf(&g_szInitText[0], g_cchInitTextMax, "VBoxGuest.sys: SetIrq failed for IRQ %#d, rc=%Rrc\n",
                                                g_bInterruptLine, rc);
                }
                else
                    g_cchInitText = RTStrPrintf(&g_szInitText[0], g_cchInitTextMax, "VBoxGuest.sys: RTSpinlockCreate failed, rc=%Rrc\n", rc);
                VGDrvCommonDeleteDevExt(&g_DevExt);
            }
            else
                g_cchInitText = RTStrPrintf(&g_szInitText[0], g_cchInitTextMax, "VBoxGuest.sys: vgdrvOS2InitDevExt failed, rc=%Rrc\n", rc);

            int rc2 = RTR0MemObjFree(g_MemObjMMIO, true /* fFreeMappings */); AssertRC(rc2);
            g_MemObjMMIO = g_MemMapMMIO = NIL_RTR0MEMOBJ;
        }
        else
            g_cchInitText = RTStrPrintf(&g_szInitText[0], g_cchInitTextMax, "VBoxGuest.sys: VBoxGuestOS2MapMMIO failed, rc=%Rrc\n", rc);
        RTR0Term();
    }
    else
        g_cchInitText = RTStrPrintf(&g_szInitText[0], g_cchInitTextMax, "VBoxGuest.sys: RTR0Init failed, rc=%Rrc\n", rc);

    RTLogBackdoorPrintf("vgdrvOS2Init: failed rc=%Rrc - %s", rc, &g_szInitText[0]);
    return rc;
}


/**
 * Maps the VMMDev memory.
 *
 * @returns VBox status code.
 * @retval  VERR_VERSION_MISMATCH       The VMMDev memory didn't meet our expectations.
 */
static int vgdrvOS2MapMemory(void)
{
    const RTCCPHYS PhysMMIOBase = g_PhysMMIOBase;

    /*
     * Did we find any MMIO region (0 or NIL)?
     */
    if (    !PhysMMIOBase
        ||  PhysMMIOBase == NIL_RTCCPHYS)
    {
        Assert(g_MemMapMMIO != NIL_RTR0MEMOBJ);
        return VINF_SUCCESS;
    }

    /*
     * Create a physical memory object for it.
     *
     * Since we don't know the actual size (OS/2 doesn't at least), we make
     * a qualified guess using the VMMDEV_RAM_SIZE.
     */
    size_t cb = RT_ALIGN_Z(VMMDEV_RAM_SIZE, PAGE_SIZE);
    int rc = RTR0MemObjEnterPhys(&g_MemObjMMIO, PhysMMIOBase, cb, RTMEM_CACHE_POLICY_DONT_CARE);
    if (RT_FAILURE(rc))
    {
        cb = _4K;
        rc = RTR0MemObjEnterPhys(&g_MemObjMMIO, PhysMMIOBase, cb, RTMEM_CACHE_POLICY_DONT_CARE);
    }
    if (RT_FAILURE(rc))
    {
        Log(("vgdrvOS2MapMemory: RTR0MemObjEnterPhys(,%RCp,%zx) -> %Rrc\n", PhysMMIOBase, cb, rc));
        return rc;
    }

    /*
     * Map the object into kernel space.
     *
     * We want a normal mapping with normal caching, which good in two ways. First
     * since the API doesn't have any flags indicating how the mapping should be cached.
     * And second, because PGM doesn't necessarily respect the cache/writethru bits
     * anyway for normal RAM.
     */
    rc = RTR0MemObjMapKernel(&g_MemMapMMIO, g_MemObjMMIO, (void *)-1, 0, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
    if (RT_SUCCESS(rc))
    {
        /*
         * Validate the VMM memory.
         */
        VMMDevMemory *pVMMDev = (VMMDevMemory *)RTR0MemObjAddress(g_MemMapMMIO);
        Assert(pVMMDev);
        if (    pVMMDev->u32Version == VMMDEV_MEMORY_VERSION
            &&  pVMMDev->u32Size >= 32 /* just for checking sanity */)
        {
            /*
             * Did we hit the correct size? If not we'll have to
             * redo the mapping using the correct size.
             */
            if (RT_ALIGN_32(pVMMDev->u32Size, PAGE_SIZE) == cb)
                return VINF_SUCCESS;

            Log(("vgdrvOS2MapMemory: Actual size %#RX32 (tried %#zx)\n", pVMMDev->u32Size, cb));
            cb = RT_ALIGN_32(pVMMDev->u32Size, PAGE_SIZE);

            rc = RTR0MemObjFree(g_MemObjMMIO, true); AssertRC(rc);
            g_MemObjMMIO = g_MemMapMMIO = NIL_RTR0MEMOBJ;

            rc = RTR0MemObjEnterPhys(&g_MemObjMMIO, PhysMMIOBase, cb, RTMEM_CACHE_POLICY_DONT_CARE);
            if (RT_SUCCESS(rc))
            {
                rc = RTR0MemObjMapKernel(&g_MemMapMMIO, g_MemObjMMIO, (void *)-1, 0, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
                if (RT_SUCCESS(rc))
                    return VINF_SUCCESS;

                Log(("vgdrvOS2MapMemory: RTR0MemObjMapKernel [%RCp,%zx] -> %Rrc (2nd)\n", PhysMMIOBase, cb, rc));
            }
            else
                Log(("vgdrvOS2MapMemory: RTR0MemObjEnterPhys(,%RCp,%zx) -> %Rrc (2nd)\n", PhysMMIOBase, cb, rc));
        }
        else
        {
            rc = VERR_VERSION_MISMATCH;
            LogRel(("vgdrvOS2MapMemory: Bogus VMMDev memory; u32Version=%RX32 (expected %RX32) u32Size=%RX32\n",
                    pVMMDev->u32Version, VMMDEV_MEMORY_VERSION, pVMMDev->u32Size));
        }
    }
    else
        Log(("vgdrvOS2MapMemory: RTR0MemObjMapKernel [%RCp,%zx] -> %Rrc\n", PhysMMIOBase, cb, rc));

    int rc2 = RTR0MemObjFree(g_MemObjMMIO, true /* fFreeMappings */); AssertRC(rc2);
    g_MemObjMMIO = g_MemMapMMIO = NIL_RTR0MEMOBJ;
    return rc;
}


/**
 * Called fromn vgdrvOS2Init to determine which OS/2 version this is.
 *
 * @returns VBox OS/2 type.
 */
static VBOXOSTYPE vgdrvOS2DetectVersion(void)
{
    VBOXOSTYPE enmOSType = VBOXOSTYPE_OS2;

#if 0 /** @todo dig up the version stuff from GIS later and verify that the numbers are actually decimal. */
    unsigned uMajor, uMinor;
    if (uMajor == 2)
    {
        if (uMinor >= 30 && uMinor < 40)
            enmOSType = VBOXOSTYPE_OS2Warp3;
        else if (uMinor >= 40 && uMinor < 45)
            enmOSType = VBOXOSTYPE_OS2Warp4;
        else if (uMinor >= 45 && uMinor < 50)
            enmOSType = VBOXOSTYPE_OS2Warp45;
    }
#endif
    return enmOSType;
}


DECLASM(int) vgdrvOS2Open(uint16_t sfn)
{
    int                 rc;
    PVBOXGUESTSESSION   pSession;

    /*
     * Create a new session.
     */
    uint32_t fRequestor = VMMDEV_REQUESTOR_USERMODE
                        | VMMDEV_REQUESTOR_TRUST_NOT_GIVEN
                        | VMMDEV_REQUESTOR_USR_ROOT  /* everyone is root on OS/2 */
                        | VMMDEV_REQUESTOR_GRP_WHEEL /* and their admins */
                        | VMMDEV_REQUESTOR_NO_USER_DEVICE /** @todo implement /dev/vboxuser? */
                        | VMMDEV_REQUESTOR_CON_DONT_KNOW; /** @todo check screen group/whatever of process to see if console */
    rc = VGDrvCommonCreateUserSession(&g_DevExt, fRequestor, &pSession);
    if (RT_SUCCESS(rc))
    {
        pSession->sfn = sfn;

        /*
         * Insert it into the hash table.
         */
        unsigned iHash = SESSION_HASH(sfn);
        RTSpinlockAcquire(g_Spinlock);
        pSession->pNextHash = g_apSessionHashTab[iHash];
        g_apSessionHashTab[iHash] = pSession;
        RTSpinlockRelease(g_Spinlock);
    }

    Log(("vgdrvOS2Open: g_DevExt=%p pSession=%p rc=%d pid=%d\n", &g_DevExt, pSession, rc, (int)RTProcSelf()));
    return rc;
}


DECLASM(int) vgdrvOS2Close(uint16_t sfn)
{
    Log(("vgdrvOS2Close: pid=%d sfn=%d\n", (int)RTProcSelf(), sfn));

    /*
     * Remove from the hash table.
     */
    PVBOXGUESTSESSION   pSession;
    const RTPROCESS     Process = RTProcSelf();
    const unsigned      iHash = SESSION_HASH(sfn);
    RTSpinlockAcquire(g_Spinlock);

    pSession = g_apSessionHashTab[iHash];
    if (pSession)
    {
        if (    pSession->sfn == sfn
            &&  pSession->Process == Process)
        {
            g_apSessionHashTab[iHash] = pSession->pNextHash;
            pSession->pNextHash = NULL;
        }
        else
        {
            PVBOXGUESTSESSION pPrev = pSession;
            pSession = pSession->pNextHash;
            while (pSession)
            {
                if (    pSession->sfn == sfn
                    &&  pSession->Process == Process)
                {
                    pPrev->pNextHash = pSession->pNextHash;
                    pSession->pNextHash = NULL;
                    break;
                }

                /* next */
                pPrev = pSession;
                pSession = pSession->pNextHash;
            }
        }
    }
    RTSpinlockRelease(g_Spinlock);
    if (!pSession)
    {
        Log(("VBoxGuestIoctl: WHUT?!? pSession == NULL! This must be a mistake... pid=%d sfn=%d\n", (int)Process, sfn));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Close the session.
     */
    VGDrvCommonCloseSession(&g_DevExt, pSession);
    return 0;
}


DECLASM(int) vgdrvOS2IOCtlFast(uint16_t sfn, uint8_t iFunction, int32_t *prc)
{
    /*
     * Find the session.
     */
    const RTPROCESS     Process = RTProcSelf();
    const unsigned      iHash = SESSION_HASH(sfn);
    PVBOXGUESTSESSION   pSession;

    RTSpinlockAcquire(g_Spinlock);
    pSession = g_apSessionHashTab[iHash];
    if (pSession && pSession->Process != Process)
    {
        do pSession = pSession->pNextHash;
        while (     pSession
               &&   (   pSession->sfn != sfn
                     || pSession->Process != Process));
    }
    RTSpinlockRelease(g_Spinlock);
    if (RT_UNLIKELY(!pSession))
    {
        Log(("VBoxGuestIoctl: WHAT?!? pSession == NULL! This must be a mistake... pid=%d\n", (int)Process));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Dispatch the fast IOCtl.
     */
    *prc = VGDrvCommonIoCtlFast(iFunction, &g_DevExt, pSession);
    return 0;
}


/**
 * 32-bit IDC service routine.
 *
 * @returns VBox status code.
 * @param   u32Session          The session handle (PVBOXGUESTSESSION).
 * @param   iFunction           The requested function.
 * @param   pReqHdr             The input/output data buffer.  The caller
 *                              ensures that this cannot be swapped out, or that
 *                              it's acceptable to take a page in fault in the
 *                              current context.  If the request doesn't take
 *                              input or produces output, apssing NULL is okay.
 * @param   cbReq               The size of the data buffer.
 *
 * @remark  This is called from the 16-bit thunker as well as directly from the 32-bit clients.
 */
DECLASM(int) VGDrvOS2IDCService(uint32_t u32Session, unsigned iFunction, PVBGLREQHDR pReqHdr, size_t cbReq)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)u32Session;
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertMsgReturn(pSession->sfn == 0xffff, ("%RX16\n", pSession->sfn), VERR_INVALID_HANDLE);
    AssertMsgReturn(pSession->pDevExt == &g_DevExt, ("%p != %p\n", pSession->pDevExt, &g_DevExt), VERR_INVALID_HANDLE);

    return VGDrvCommonIoCtl(iFunction, &g_DevExt, pSession, pReqHdr, cbReq);
}


/**
 * Worker for VBoxGuestOS2IDC, it creates the kernel session.
 *
 * @returns Pointer to the session.
 */
DECLASM(PVBOXGUESTSESSION) vgdrvOS2IDCConnect(void)
{
    PVBOXGUESTSESSION pSession;
    int rc = VGDrvCommonCreateKernelSession(&g_DevExt, &pSession);
    if (RT_SUCCESS(rc))
    {
        pSession->sfn = 0xffff;
        return pSession;
    }
    return NULL;
}


DECLASM(int) vgdrvOS2IOCtl(uint16_t sfn, uint8_t iCat, uint8_t iFunction, void *pvParm, void *pvData,
                           uint16_t *pcbParm, uint16_t *pcbData)
{
    /*
     * Find the session.
     */
    const RTPROCESS     Process = RTProcSelf();
    const unsigned      iHash = SESSION_HASH(sfn);
    PVBOXGUESTSESSION   pSession;

    RTSpinlockAcquire(g_Spinlock);
    pSession = g_apSessionHashTab[iHash];
    if (pSession && pSession->Process != Process)
    {
        do pSession = pSession->pNextHash;
        while (     pSession
               &&   (   pSession->sfn != sfn
                     || pSession->Process != Process));
    }
    RTSpinlockRelease(g_Spinlock);
    if (!pSession)
    {
        Log(("VBoxGuestIoctl: WHAT?!? pSession == NULL! This must be a mistake... pid=%d\n", (int)Process));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Verify the category and dispatch the IOCtl.
     *
     * The IOCtl call uses the parameter buffer as generic data input/output
     * buffer similar to the one unix ioctl buffer argument.  While the data
     * buffer is not used.
     */
    if (RT_LIKELY(iCat == VBGL_IOCTL_CATEGORY))
    {
        Log(("vgdrvOS2IOCtl: pSession=%p iFunction=%#x pvParm=%p pvData=%p *pcbParm=%d *pcbData=%d\n", pSession, iFunction, pvParm, pvData, *pcbParm, *pcbData));
        if (   pvParm
            && *pcbParm >= sizeof(VBGLREQHDR)
            && *pcbData == 0)
        {
            /*
             * Lock the buffer.
             */
            KernVMLock_t ParmLock;
            int32_t rc = KernVMLock(VMDHL_WRITE, pvParm, *pcbParm, &ParmLock, (KernPageList_t *)-1, NULL);
            if (rc == 0)
            {
                /*
                 * Process the IOCtl.
                 */
                PVBGLREQHDR pReqHdr = (PVBGLREQHDR)pvParm;
                rc = VGDrvCommonIoCtl(iFunction, &g_DevExt, pSession, pReqHdr, *pcbParm);

                /*
                 * Unlock the buffer.
                 */
                *pcbParm = RT_SUCCESS(rc) ? pReqHdr->cbOut : sizeof(*pReqHdr);
                int rc2 = KernVMUnlock(&ParmLock);
                AssertMsg(rc2 == 0, ("rc2=%d\n", rc2)); NOREF(rc2);

                Log2(("vgdrvOS2IOCtl: returns %d\n", rc));
                return rc;
            }
            AssertMsgFailed(("KernVMLock(VMDHL_WRITE, %p, %#x, &p, NULL, NULL) -> %d\n", pvParm, *pcbParm, &ParmLock, rc));
            return VERR_LOCK_FAILED;
        }
        Log2(("vgdrvOS2IOCtl: returns VERR_INVALID_PARAMETER (iFunction=%#x)\n", iFunction));
        return VERR_INVALID_PARAMETER;
    }
    return VERR_NOT_SUPPORTED;
}


/**
 * 32-bit ISR, called by 16-bit assembly thunker in VBoxGuestA-os2.asm.
 *
 * @returns true if it's our interrupt, false it isn't.
 */
DECLASM(bool) vgdrvOS2ISR(void)
{
    Log(("vgdrvOS2ISR\n"));

    return VGDrvCommonISR(&g_DevExt);
}


void VGDrvNativeISRMousePollEvent(PVBOXGUESTDEVEXT pDevExt)
{
    /* No polling on OS/2 */
    NOREF(pDevExt);
}


bool VGDrvNativeProcessOption(PVBOXGUESTDEVEXT pDevExt, const char *pszName, const char *pszValue)
{
    RT_NOREF(pDevExt); RT_NOREF(pszName); RT_NOREF(pszValue);
    return false;
}


#ifdef DEBUG_READ /** @todo figure out this one once and for all... */

/**
 * Callback for writing to the log buffer.
 *
 * @returns number of bytes written.
 * @param   pvArg       Unused.
 * @param   pachChars   Pointer to an array of utf-8 characters.
 * @param   cbChars     Number of bytes in the character array pointed to by pachChars.
 */
static DECLCALLBACK(size_t) vgdrvOS2LogOutput(void *pvArg, const char *pachChars, size_t cbChars)
{
    size_t cchWritten = 0;
    while (cbChars-- > 0)
    {
        const uint16_t offLogHead = g_offLogHead;
        const uint16_t offLogHeadNext = (offLogHead + 1) & (g_cchLogMax - 1);
        if (offLogHeadNext == g_offLogTail)
            break; /* no */
        g_szLog[offLogHead] = *pachChars++;
        g_offLogHead = offLogHeadNext;
        cchWritten++;
    }
    return cchWritten;
}


int SUPR0Printf(const char *pszFormat, ...)
{
    va_list va;

#if 0 //def DEBUG_bird
    va_start(va, pszFormat);
    RTLogComPrintfV(pszFormat, va);
    va_end(va);
#endif

    va_start(va, pszFormat);
    int cch = RTLogFormatV(vgdrvOS2LogOutput, NULL, pszFormat, va);
    va_end(va);

    return cch;
}

#endif /* DEBUG_READ */

