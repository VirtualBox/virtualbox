/* $Id: SUPDrv-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDrv - The VirtualBox Support Driver - OS/2 specifics.
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
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
#define LOG_GROUP LOG_GROUP_SUP_DRV
#define __STDC_CONSTANT_MACROS
#define __STDC_LIMIT_MACROS

#include <os2ddk/bsekee.h>
#undef RT_MAX

#include "SUPDrvInternal.h"
#include <VBox/version.h>
#include <iprt/initterm.h>
#include <iprt/string.h>
#include <iprt/spinlock.h>
#include <iprt/process.h>
#include <iprt/assert.h>
#include <VBox/log.h>
#include <iprt/param.h>
#include <VBox/version.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Device extention & session data association structure.
 */
static SUPDRVDEVEXT         g_DevExt;
/** Spinlock protecting g_apSessionHashTab. */
static RTSPINLOCK           g_Spinlock = NIL_RTSPINLOCK;
/** Hash table */
static PSUPDRVSESSION       g_apSessionHashTab[19];
/** Calculates the index into g_apSessionHashTab.*/
#define SESSION_HASH(sfn) ((sfn) % RT_ELEMENTS(g_apSessionHashTab))

RT_C_DECLS_BEGIN
/* Defined in SUPDrvA-os2.asm */
extern uint16_t             g_offLogHead;
extern uint16_t volatile    g_offLogTail;
extern uint16_t const       g_cchLogMax;
extern char                 g_szLog[];
/* (init only:) */
extern char                 g_szInitText[];
extern uint16_t             g_cchInitText;
extern uint16_t             g_cchInitTextMax;
RT_C_DECLS_END


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/



/**
 * 32-bit Ring-0 initialization.
 *
 * @returns 0 on success, non-zero on failure.
 * @param   pszArgs     Pointer to the device arguments.
 */
DECLASM(int) VBoxDrvInit(const char *pszArgs)
{
    /*
     * Initialize the runtime.
     */
    int rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        Log(("VBoxDrvInit: pszArgs=%s\n", pszArgs));

        /*
         * Initialize the device extension.
         */
        rc = supdrvInitDevExt(&g_DevExt, sizeof(SUPDRVSESSION));
        if (RT_SUCCESS(rc))
        {
            /*
             * Initialize the session hash table.
             */
            rc = RTSpinlockCreate(&g_Spinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "VBoxDrvOS2");
            if (RT_SUCCESS(rc))
            {
                /*
                 * Process the commandline. Later.
                 */
                bool fVerbose = true;

                /*
                 * Success
                 */
                if (fVerbose)
                {
                    strcpy(&g_szInitText[0],
                           "\r\n"
                           "VirtualBox.org Support Driver for OS/2 version " VBOX_VERSION_STRING "\r\n"
                           "Copyright (C) 2007 Knut St. Osmundsen\r\n"
                           "Copyright (C) 2007-" VBOX_C_YEAR " Oracle Corporation\r\n");
                    g_cchInitText = strlen(&g_szInitText[0]);
                }
                return VINF_SUCCESS;
            }
            g_cchInitText = RTStrPrintf(&g_szInitText[0], g_cchInitTextMax, "VBoxDrv.sys: RTSpinlockCreate failed, rc=%Rrc\n", rc);
            supdrvDeleteDevExt(&g_DevExt);
        }
        else
            g_cchInitText = RTStrPrintf(&g_szInitText[0], g_cchInitTextMax, "VBoxDrv.sys: supdrvInitDevExt failed, rc=%Rrc\n", rc);
        RTR0Term();
    }
    else
        g_cchInitText = RTStrPrintf(&g_szInitText[0], g_cchInitTextMax, "VBoxDrv.sys: RTR0Init failed, rc=%Rrc\n", rc);
    return rc;
}


DECLASM(int) VBoxDrvOpen(uint16_t sfn)
{
    int                 rc;
    PSUPDRVSESSION      pSession;

    /*
     * Create a new session.
     */
    rc = supdrvCreateSession(&g_DevExt, true /* fUser */, true /*fUnrestricted*/, &pSession);
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

    Log(("VBoxDrvOpen: g_DevExt=%p pSession=%p rc=%d pid=%d\n", &g_DevExt, pSession, rc, (int)RTProcSelf()));
    return rc;
}


DECLASM(int) VBoxDrvClose(uint16_t sfn)
{
    Log(("VBoxDrvClose: pid=%d sfn=%d\n", (int)RTProcSelf(), sfn));

    /*
     * Remove from the hash table.
     */
    PSUPDRVSESSION  pSession;
    const RTPROCESS Process = RTProcSelf();
    const unsigned  iHash = SESSION_HASH(sfn);
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
            PSUPDRVSESSION pPrev = pSession;
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
        OSDBGPRINT(("VBoxDrvIoctl: WHUT?!? pSession == NULL! This must be a mistake... pid=%d sfn=%d\n", (int)Process, sfn));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Close the session.
     */
    supdrvSessionRelease(pSession);
    return 0;
}


DECLASM(int) VBoxDrvIOCtlFast(uint16_t sfn, uint8_t iFunction)
{
    /*
     * Find the session.
     */
    const RTPROCESS     Process = RTProcSelf();
    const unsigned      iHash = SESSION_HASH(sfn);
    PSUPDRVSESSION      pSession;

    RTSpinlockAcquire(g_Spinlock);
    pSession = g_apSessionHashTab[iHash];
    if (pSession && pSession->Process != Process)
    {
        do pSession = pSession->pNextHash;
        while (     pSession
               &&   (   pSession->sfn != sfn
                     || pSession->Process != Process));

        if (RT_LIKELY(pSession))
            supdrvSessionRetain(pSession);
    }
    RTSpinlockRelease(g_Spinlock);
    if (RT_UNLIKELY(!pSession))
    {
        OSDBGPRINT(("VBoxDrvIoctl: WHUT?!? pSession == NULL! This must be a mistake... pid=%d\n", (int)Process));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Dispatch the fast IOCtl.
     */
    int rc;
    if ((unsigned)(iFunction - SUP_IOCTL_FAST_DO_FIRST) < (unsigned)32)
        rc = supdrvIOCtlFast(iFunction, 0, &g_DevExt, pSession);
    else
        rc = VERR_INVALID_FUNCTION;
    supdrvSessionRelease(pSession);
    return rc;
}


DECLASM(int) VBoxDrvIOCtl(uint16_t sfn, uint8_t iCat, uint8_t iFunction, void *pvParm, void *pvData, uint16_t *pcbParm, uint16_t *pcbData)
{
    /*
     * Find the session.
     */
    const RTPROCESS     Process = RTProcSelf();
    const unsigned      iHash = SESSION_HASH(sfn);
    PSUPDRVSESSION      pSession;

    RTSpinlockAcquire(g_Spinlock);
    pSession = g_apSessionHashTab[iHash];
    if (pSession && pSession->Process != Process)
    {
        do pSession = pSession->pNextHash;
        while (     pSession
               &&   (   pSession->sfn != sfn
                     || pSession->Process != Process));

        if (RT_LIKELY(pSession))
            supdrvSessionRetain(pSession);
    }
    RTSpinlockRelease(g_Spinlock);
    if (!pSession)
    {
        OSDBGPRINT(("VBoxDrvIoctl: WHUT?!? pSession == NULL! This must be a mistake... pid=%d\n", (int)Process));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Verify the category and dispatch the IOCtl.
     */
    int rc;
    if (RT_LIKELY(iCat == SUP_CTL_CATEGORY))
    {
        Log(("VBoxDrvIOCtl: pSession=%p iFunction=%#x pvParm=%p pvData=%p *pcbParm=%d *pcbData=%d\n", pSession, iFunction, pvParm, pvData, *pcbParm, *pcbData));
        Assert(pvParm);
        Assert(!pvData);

        /*
         * Lock the header.
         */
        PSUPREQHDR pHdr = (PSUPREQHDR)pvParm;
        AssertReturn(*pcbParm == sizeof(*pHdr), VERR_INVALID_PARAMETER);
        KernVMLock_t Lock;
        rc = KernVMLock(VMDHL_WRITE, pHdr, *pcbParm, &Lock, (KernPageList_t *)-1, NULL);
        AssertMsgReturn(!rc, ("KernVMLock(VMDHL_WRITE, %p, %#x, &p, NULL, NULL) -> %d\n", pHdr, *pcbParm, &Lock, rc), VERR_LOCK_FAILED);

        /*
         * Validate the header.
         */
        if (RT_LIKELY((pHdr->fFlags & SUPREQHDR_FLAGS_MAGIC_MASK) == SUPREQHDR_FLAGS_MAGIC))
        {
            uint32_t cbReq = RT_MAX(pHdr->cbIn, pHdr->cbOut);
            if (RT_LIKELY(    pHdr->cbIn >= sizeof(*pHdr)
                          &&  pHdr->cbOut >= sizeof(*pHdr)
                          &&  cbReq <= _1M*16))
            {
                /*
                 * Lock the rest of the buffer if necessary.
                 */
                if (((uintptr_t)pHdr & PAGE_OFFSET_MASK) + cbReq > PAGE_SIZE)
                {
                    rc = KernVMUnlock(&Lock);
                    AssertMsgReturn(!rc, ("KernVMUnlock(Lock) -> %#x\n", rc), VERR_LOCK_FAILED);

                    rc = KernVMLock(VMDHL_WRITE, pHdr, cbReq, &Lock, (KernPageList_t *)-1, NULL);
                    AssertMsgReturn(!rc, ("KernVMLock(VMDHL_WRITE, %p, %#x, &p, NULL, NULL) -> %d\n", pHdr, cbReq, &Lock, rc), VERR_LOCK_FAILED);
                }

                /*
                 * Process the IOCtl.
                 */
                rc = supdrvIOCtl(iFunction, &g_DevExt, pSession, pHdr, cbReq);
            }
            else
            {
                OSDBGPRINT(("VBoxDrvIOCtl: max(%#x,%#x); iCmd=%#x\n", pHdr->cbIn, pHdr->cbOut, iFunction));
                rc = VERR_INVALID_PARAMETER;
            }
        }
        else
        {
            OSDBGPRINT(("VBoxDrvIOCtl: bad magic fFlags=%#x; iCmd=%#x\n", pHdr->fFlags, iFunction));
            rc = VERR_INVALID_PARAMETER;
        }

        /*
         * Unlock and return.
         */
        int rc2 = KernVMUnlock(&Lock);
        AssertMsg(!rc2, ("rc2=%d\n", rc2)); NOREF(rc2);
    }
    else
        rc = VERR_NOT_SUPPORTED;

    supdrvSessionRelease(pSession);
    Log2(("VBoxDrvIOCtl: returns %d\n", rc));
    return rc;
}


void VBOXCALL supdrvOSCleanupSession(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession)
{
    NOREF(pDevExt);
    NOREF(pSession);
}


void VBOXCALL supdrvOSSessionHashTabInserted(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, void *pvUser)
{
    NOREF(pDevExt); NOREF(pSession); NOREF(pvUser);
}


void VBOXCALL supdrvOSSessionHashTabRemoved(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, void *pvUser)
{
    NOREF(pDevExt); NOREF(pSession); NOREF(pvUser);
}


void VBOXCALL   supdrvOSObjInitCreator(PSUPDRVOBJ pObj, PSUPDRVSESSION pSession)
{
    NOREF(pObj);
    NOREF(pSession);
}


bool VBOXCALL   supdrvOSObjCanAccess(PSUPDRVOBJ pObj, PSUPDRVSESSION pSession, const char *pszObjName, int *prc)
{
    NOREF(pObj);
    NOREF(pSession);
    NOREF(pszObjName);
    NOREF(prc);
    return false;
}


bool VBOXCALL  supdrvOSGetForcedAsyncTscMode(PSUPDRVDEVEXT pDevExt)
{
    NOREF(pDevExt);
    return false;
}


bool VBOXCALL  supdrvOSAreCpusOfflinedOnSuspend(void)
{
    return false;
}


bool VBOXCALL  supdrvOSAreTscDeltasInSync(void)
{
    return false;
}


int  VBOXCALL   supdrvOSLdrOpen(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const char *pszFilename)
{
    NOREF(pDevExt); NOREF(pImage); NOREF(pszFilename);
    return VERR_NOT_SUPPORTED;
}


int  VBOXCALL   supdrvOSLdrValidatePointer(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, void *pv,
                                           const uint8_t *pbImageBits, const char *pszSymbol)
{
    NOREF(pDevExt); NOREF(pImage); NOREF(pv); NOREF(pbImageBits); NOREF(pszSymbol);
    return VERR_NOT_SUPPORTED;
}


int  VBOXCALL   supdrvOSLdrLoad(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const uint8_t *pbImageBits, PSUPLDRLOAD pReq)
{
    NOREF(pDevExt); NOREF(pImage); NOREF(pbImageBits); NOREF(pReq);
    return VERR_NOT_SUPPORTED;
}


void VBOXCALL   supdrvOSLdrUnload(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage)
{
    NOREF(pDevExt); NOREF(pImage);
}


void VBOXCALL   supdrvOSLdrNotifyOpened(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const char *pszFilename)
{
    NOREF(pDevExt); NOREF(pImage); NOREF(pszFilename);
}


void VBOXCALL   supdrvOSLdrNotifyUnloaded(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage)
{
    NOREF(pDevExt); NOREF(pImage);
}


int  VBOXCALL   supdrvOSLdrQuerySymbol(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage,
                                       const char *pszSymbol, size_t cchSymbol, void **ppvSymbol)
{
    RT_NOREF(pDevExt, pImage, pszSymbol, cchSymbol, ppvSymbol);
    return VERR_WRONG_ORDER;
}


void VBOXCALL   supdrvOSLdrRetainWrapperModule(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage)
{
    RT_NOREF(pDevExt, pImage);
    AssertFailed();
}


void VBOXCALL   supdrvOSLdrReleaseWrapperModule(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage)
{
    RT_NOREF(pDevExt, pImage);
    AssertFailed();
}

#ifdef SUPDRV_WITH_MSR_PROBER

int VBOXCALL    supdrvOSMsrProberRead(uint32_t uMsr, RTCPUID idCpu, uint64_t *puValue)
{
    NOREF(uMsr); NOREF(idCpu); NOREF(puValue);
    return VERR_NOT_SUPPORTED;
}


int VBOXCALL    supdrvOSMsrProberWrite(uint32_t uMsr, RTCPUID idCpu, uint64_t uValue)
{
    NOREF(uMsr); NOREF(idCpu); NOREF(uValue);
    return VERR_NOT_SUPPORTED;
}


int VBOXCALL    supdrvOSMsrProberModify(RTCPUID idCpu, PSUPMSRPROBER pReq)
{
    NOREF(idCpu); NOREF(pReq);
    return VERR_NOT_SUPPORTED;
}

#endif /* SUPDRV_WITH_MSR_PROBER */


/**
 * Callback for writing to the log buffer.
 *
 * @returns number of bytes written.
 * @param   pvArg       Unused.
 * @param   pachChars   Pointer to an array of utf-8 characters.
 * @param   cbChars     Number of bytes in the character array pointed to by pachChars.
 */
static DECLCALLBACK(size_t) VBoxDrvLogOutput(void *pvArg, const char *pachChars, size_t cbChars)
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


SUPR0DECL(int) SUPR0PrintfV(const char *pszFormat, va_list va)
{
#if 0 //def DEBUG_bird
    va_list va2;
    va_copy(va2, va);
    RTLogComPrintfV(pszFormat, va2);
    va_end(va2);
#endif

    RTLogFormatV(VBoxDrvLogOutput, NULL, pszFormat, va);
    return 0;
}


SUPR0DECL(uint32_t) SUPR0GetKernelFeatures(void)
{
    return 0;
}


SUPR0DECL(bool) SUPR0FpuBegin(bool fCtxHook)
{
    RT_NOREF(fCtxHook);
    return false;
}


SUPR0DECL(void) SUPR0FpuEnd(bool fCtxHook)
{
    RT_NOREF(fCtxHook);
}

