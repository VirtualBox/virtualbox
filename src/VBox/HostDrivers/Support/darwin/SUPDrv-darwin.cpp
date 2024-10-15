/* $Id: SUPDrv-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Driver - Darwin Specific Code.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SUP_DRV
#include "../../../Runtime/r0drv/darwin/the-darwin-kernel.h"

#include "../SUPDrvInternal.h"
#include <VBox/version.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/ctype.h>
#include <iprt/dbg.h>
#include <iprt/initterm.h>
#include <iprt/file.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/power.h>
#include <iprt/process.h>
#include <iprt/spinlock.h>
#include <iprt/semaphore.h>
#include <iprt/x86.h>
#include <iprt/crypto/applecodesign.h>
#include <iprt/crypto/store.h>
#include <iprt/crypto/pkcs7.h>
#include <iprt/crypto/x509.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <mach/kmod.h>
#include <miscfs/devfs/devfs.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/ioccom.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/kauth.h>
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <IOKit/IODeviceTreeSupport.h>
#if MAC_OS_X_VERSION_MIN_REQUIRED < 101100
# include <IOKit/usb/IOUSBHIDDriver.h>
#endif
#include <IOKit/bluetooth/IOBluetoothHIDDriver.h>
#include <IOKit/bluetooth/IOBluetoothHIDDriverTypes.h>

#ifdef VBOX_WITH_HOST_VMX
# include <libkern/version.h>
RT_C_DECLS_BEGIN
# include <i386/vmx.h>
RT_C_DECLS_END
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The system device node name. */
#define DEVICE_NAME_SYS     "vboxdrv"
/** The user device node name. */
#define DEVICE_NAME_USR     "vboxdrvu"


/** @name For debugging/whatever, now permanent.
 * @{  */
#define VBOX_PROC_SELFNAME_LEN              31
#define VBOX_RETRIEVE_CUR_PROC_NAME(a_Name) char a_Name[VBOX_PROC_SELFNAME_LEN + 1]; \
                                            proc_selfname(a_Name, VBOX_PROC_SELFNAME_LEN)
/** @} */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
static kern_return_t    VBoxDrvDarwinStart(struct kmod_info *pKModInfo, void *pvData);
static kern_return_t    VBoxDrvDarwinStop(struct kmod_info *pKModInfo, void *pvData);
#ifdef VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION
static int              supdrvDarwinInitCertStores(PSUPDRVDEVEXT pDevExt);
static void             supdrvDarwinDestroyCertStores(PSUPDRVDEVEXT pDevExt);
#endif

static int              VBoxDrvDarwinOpen(dev_t Dev, int fFlags, int fDevType, struct proc *pProcess);
static int              VBoxDrvDarwinClose(dev_t Dev, int fFlags, int fDevType, struct proc *pProcess);
static int              VBoxDrvDarwinIOCtl(dev_t Dev, u_long iCmd, caddr_t pData, int fFlags, struct proc *pProcess);
#ifndef VBOX_WITHOUT_EFLAGS_AC_SET_IN_VBOXDRV
static int              VBoxDrvDarwinIOCtlSMAP(dev_t Dev, u_long iCmd, caddr_t pData, int fFlags, struct proc *pProcess);
#endif
static int              VBoxDrvDarwinIOCtlSlow(PSUPDRVSESSION pSession, u_long iCmd, caddr_t pData, struct proc *pProcess);

static int              VBoxDrvDarwinErr2DarwinErr(int rc);

static IOReturn         VBoxDrvDarwinSleepHandler(void *pvTarget, void *pvRefCon, UInt32 uMessageType, IOService *pProvider, void *pvMessageArgument, vm_size_t argSize);
RT_C_DECLS_END

static int              vboxdrvDarwinResolveSymbols(void);
static bool             vboxdrvDarwinCpuHasSMAP(void);


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The service class.
 * This is just a formality really.
 */
class org_virtualbox_SupDrv : public IOService
{
    OSDeclareDefaultStructors(org_virtualbox_SupDrv);

public:
    virtual bool init(OSDictionary *pDictionary = 0);
    virtual void free(void);
    virtual bool start(IOService *pProvider);
    virtual void stop(IOService *pProvider);
    virtual IOService *probe(IOService *pProvider, SInt32 *pi32Score);
    virtual bool terminate(IOOptionBits fOptions);

    RTR0MEMEF_NEW_AND_DELETE_OPERATORS_IOKIT();

private:
    /** Guard against the parent class growing and us using outdated headers. */
    uint8_t m_abSafetyPadding[256];
};

OSDefineMetaClassAndStructors(org_virtualbox_SupDrv, IOService);


/**
 * An attempt at getting that clientDied() notification.
 * I don't think it'll work as I cannot figure out where/what creates the correct
 * port right.
 */
class org_virtualbox_SupDrvClient : public IOUserClient
{
    OSDeclareDefaultStructors(org_virtualbox_SupDrvClient);

private:
    /** Guard against the parent class growing and us using outdated headers. */
    uint8_t m_abSafetyPadding[256];

    PSUPDRVSESSION          m_pSession;     /**< The session. */
    task_t                  m_Task;         /**< The client task. */
    org_virtualbox_SupDrv  *m_pProvider;    /**< The service provider. */

public:
    virtual bool initWithTask(task_t OwningTask, void *pvSecurityId, UInt32 u32Type);
    virtual bool start(IOService *pProvider);
    static  void sessionClose(RTPROCESS Process);
    virtual IOReturn clientClose(void);
    virtual IOReturn clientDied(void);
    virtual bool terminate(IOOptionBits fOptions = 0);
    virtual bool finalize(IOOptionBits fOptions);
    virtual void stop(IOService *pProvider);

    RTR0MEMEF_NEW_AND_DELETE_OPERATORS_IOKIT();
};

OSDefineMetaClassAndStructors(org_virtualbox_SupDrvClient, IOUserClient);



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Declare the module stuff.
 */
RT_C_DECLS_BEGIN
extern kern_return_t _start(struct kmod_info *pKModInfo, void *pvData);
extern kern_return_t _stop(struct kmod_info *pKModInfo, void *pvData);

KMOD_EXPLICIT_DECL(VBoxDrv, VBOX_VERSION_STRING, _start, _stop)
DECL_HIDDEN_DATA(kmod_start_func_t *) _realmain = VBoxDrvDarwinStart;
DECL_HIDDEN_DATA(kmod_stop_func_t *)  _antimain = VBoxDrvDarwinStop;
DECL_HIDDEN_DATA(int)                 _kext_apple_cc = __APPLE_CC__;
RT_C_DECLS_END


/**
 * Device extention & session data association structure.
 */
static SUPDRVDEVEXT     g_DevExt;

/**
 * The character device switch table for the driver.
 */
static struct cdevsw    g_DevCW =
{
    /** @todo g++ doesn't like this syntax - it worked with gcc before renaming to .cpp. */
    /*.d_open  = */VBoxDrvDarwinOpen,
    /*.d_close = */VBoxDrvDarwinClose,
    /*.d_read  = */eno_rdwrt,
    /*.d_write = */eno_rdwrt,
    /*.d_ioctl = */VBoxDrvDarwinIOCtl,
    /*.d_stop  = */eno_stop,
    /*.d_reset = */eno_reset,
    /*.d_ttys  = */NULL,
    /*.d_select= */eno_select,
    /*.d_mmap  = */eno_mmap,
    /*.d_strategy = */eno_strat,
    /*.d_getc  = */(void *)(uintptr_t)&enodev, //eno_getc,
    /*.d_putc  = */(void *)(uintptr_t)&enodev, //eno_putc,
    /*.d_type  = */0
};

/** Major device number. */
static int                  g_iMajorDeviceNo = -1;
/** Registered devfs device handle for the system device. */
static void                *g_hDevFsDeviceSys = NULL;
/** Registered devfs device handle for the user device. */
static void                *g_hDevFsDeviceUsr = NULL;

/** Spinlock protecting g_apSessionHashTab. */
static RTSPINLOCK           g_Spinlock = NIL_RTSPINLOCK;
/** Hash table */
static PSUPDRVSESSION       g_apSessionHashTab[19];
/** Calculates the index into g_apSessionHashTab.*/
#define SESSION_HASH(pid)   ((pid) % RT_ELEMENTS(g_apSessionHashTab))
/** The number of open sessions. */
static int32_t volatile     g_cSessions = 0;
/** The notifier handle for the sleep callback handler. */
static IONotifier          *g_pSleepNotifier = NULL;

/** Pointer to vmx_suspend(). */
static PFNRT            g_pfnVmxSuspend = NULL;
/** Pointer to vmx_resume(). */
static PFNRT            g_pfnVmxResume = NULL;
/** Pointer to vmx_use_count. */
static int volatile    *g_pVmxUseCount = NULL;

#ifdef SUPDRV_WITH_MSR_PROBER
/** Pointer to rdmsr_carefully if found. Returns 0 on success. */
static int             (*g_pfnRdMsrCarefully)(uint32_t uMsr, uint32_t *puLow, uint32_t *puHigh) = NULL;
/** Pointer to rdmsr64_carefully if found. Returns 0 on success. */
static int             (*g_pfnRdMsr64Carefully)(uint32_t uMsr, uint64_t *uValue) = NULL;
/** Pointer to wrmsr[64]_carefully if found. Returns 0 on success. */
static int             (*g_pfnWrMsr64Carefully)(uint32_t uMsr, uint64_t uValue) = NULL;
#endif

/** SUPKERNELFEATURES_XXX */
static uint32_t         g_fKernelFeatures = 0;

/**
 * Start the kernel module.
 */
static kern_return_t    VBoxDrvDarwinStart(struct kmod_info *pKModInfo, void *pvData)
{
    RT_NOREF(pKModInfo, pvData);
#ifdef DEBUG
    printf("VBoxDrvDarwinStart\n");
#endif

    /*
     * Initialize IPRT.
     */
    int rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        /*
         * Initialize the device extension.
         */
        rc = supdrvInitDevExt(&g_DevExt, sizeof(SUPDRVSESSION));
        if (RT_SUCCESS(rc))
        {
#ifdef VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION
            supdrvDarwinInitCertStores(&g_DevExt);
#endif

            /*
             * Initialize the session hash table.
             */
            memset(g_apSessionHashTab, 0, sizeof(g_apSessionHashTab)); /* paranoia */
            rc = RTSpinlockCreate(&g_Spinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "VBoxDrvDarwin");
            if (RT_SUCCESS(rc))
            {
                if (vboxdrvDarwinCpuHasSMAP())
                {
                    g_fKernelFeatures |= SUPKERNELFEATURES_SMAP;
#ifndef VBOX_WITHOUT_EFLAGS_AC_SET_IN_VBOXDRV
                    LogRel(("disabling SMAP for VBoxDrvDarwinIOCtl\n"));
                    g_DevCW.d_ioctl = VBoxDrvDarwinIOCtlSMAP;
#endif
                }

                /*
                 * Resolve some extra kernel symbols.
                 */
                rc = vboxdrvDarwinResolveSymbols();
                if (RT_SUCCESS(rc))
                {

                    /*
                     * Registering ourselves as a character device.
                     */
                    g_iMajorDeviceNo = cdevsw_add(-1, &g_DevCW);
                    if (g_iMajorDeviceNo >= 0)
                    {
#ifdef VBOX_WITH_HARDENING
                        g_hDevFsDeviceSys = devfs_make_node(makedev(g_iMajorDeviceNo, 0), DEVFS_CHAR,
                                                            UID_ROOT, GID_WHEEL, 0600, DEVICE_NAME_SYS);
#else
                        g_hDevFsDeviceSys = devfs_make_node(makedev(g_iMajorDeviceNo, 0), DEVFS_CHAR,
                                                            UID_ROOT, GID_WHEEL, 0666, DEVICE_NAME_SYS);
#endif
                        if (g_hDevFsDeviceSys)
                        {
                            g_hDevFsDeviceUsr = devfs_make_node(makedev(g_iMajorDeviceNo, 1), DEVFS_CHAR,
                                                                UID_ROOT, GID_WHEEL, 0666, DEVICE_NAME_USR);
                            if (g_hDevFsDeviceUsr)
                            {
                                LogRel(("VBoxDrv: version " VBOX_VERSION_STRING " r%d; IOCtl version %#x; IDC version %#x; dev major=%d\n",
                                        VBOX_SVN_REV, SUPDRV_IOC_VERSION, SUPDRV_IDC_VERSION, g_iMajorDeviceNo));

                                /* Register a sleep/wakeup notification callback */
                                g_pSleepNotifier = registerPrioritySleepWakeInterest(&VBoxDrvDarwinSleepHandler, &g_DevExt, NULL);
                                if (g_pSleepNotifier == NULL)
                                    LogRel(("VBoxDrv: register for sleep/wakeup events failed\n"));

                                return KMOD_RETURN_SUCCESS;
                            }

                            LogRel(("VBoxDrv: devfs_make_node(makedev(%d,1),,,,%s) failed\n", g_iMajorDeviceNo, DEVICE_NAME_USR));
                            devfs_remove(g_hDevFsDeviceSys);
                            g_hDevFsDeviceSys = NULL;
                        }
                        else
                            LogRel(("VBoxDrv: devfs_make_node(makedev(%d,0),,,,%s) failed\n", g_iMajorDeviceNo, DEVICE_NAME_SYS));

                        cdevsw_remove(g_iMajorDeviceNo, &g_DevCW);
                        g_iMajorDeviceNo = -1;
                    }
                    else
                        LogRel(("VBoxDrv: cdevsw_add failed (%d)\n", g_iMajorDeviceNo));
                }
#ifdef VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION
                supdrvDarwinDestroyCertStores(&g_DevExt);
#endif
                RTSpinlockDestroy(g_Spinlock);
                g_Spinlock = NIL_RTSPINLOCK;
            }
            else
                LogRel(("VBoxDrv: RTSpinlockCreate failed (rc=%d)\n", rc));
            supdrvDeleteDevExt(&g_DevExt);
        }
        else
            printf("VBoxDrv: failed to initialize device extension (rc=%d)\n", rc);
        RTR0TermForced();
    }
    else
        printf("VBoxDrv: failed to initialize IPRT (rc=%d)\n", rc);

    memset(&g_DevExt, 0, sizeof(g_DevExt));
    return KMOD_RETURN_FAILURE;
}


/**
 * Resolves kernel symbols we need and some we just would like to have.
 */
static int vboxdrvDarwinResolveSymbols(void)
{
    RTDBGKRNLINFO hKrnlInfo;
    int rc = RTR0DbgKrnlInfoOpen(&hKrnlInfo, 0);
    if (RT_SUCCESS(rc))
    {
        /*
         * The VMX stuff - required with raw-mode (in theory for 64-bit on
         * 32-bit too, but we never did that on darwin).
         */
        int rc1 = RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, NULL, "vmx_resume", (void **)&g_pfnVmxResume);
        int rc2 = RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, NULL, "vmx_suspend", (void **)&g_pfnVmxSuspend);
        int rc3 = RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, NULL, "vmx_use_count", (void **)&g_pVmxUseCount);
        if (RT_SUCCESS(rc1) && RT_SUCCESS(rc2) && RT_SUCCESS(rc3))
        {
            LogRel(("VBoxDrv: vmx_resume=%p vmx_suspend=%p vmx_use_count=%p (%d) cr4=%#x\n",
                    g_pfnVmxResume, g_pfnVmxSuspend, g_pVmxUseCount, *g_pVmxUseCount, ASMGetCR4() ));
        }
        else
        {
            LogRel(("VBoxDrv: failed to resolve vmx stuff: vmx_resume=%Rrc vmx_suspend=%Rrc vmx_use_count=%Rrc", rc1, rc2, rc3));
            g_pfnVmxResume  = NULL;
            g_pfnVmxSuspend = NULL;
            g_pVmxUseCount  = NULL;
#ifdef VBOX_WITH_RAW_MODE
            rc = VERR_SYMBOL_NOT_FOUND;
#endif
        }

        if (RT_SUCCESS(rc))
        {
#ifdef SUPDRV_WITH_MSR_PROBER
            /*
             * MSR prober stuff - optional!
             */
            rc2 = RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, NULL, "rdmsr_carefully", (void **)&g_pfnRdMsrCarefully);
            if (RT_FAILURE(rc2))
                g_pfnRdMsrCarefully = NULL;
            rc2 = RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, NULL, "rdmsr64_carefully", (void **)&g_pfnRdMsr64Carefully);
            if (RT_FAILURE(rc2))
                g_pfnRdMsr64Carefully = NULL;
# ifdef RT_ARCH_AMD64 /* Missing 64 in name, so if implemented on 32-bit it could have different signature. */
            rc2 = RTR0DbgKrnlInfoQuerySymbol(hKrnlInfo, NULL, "wrmsr_carefully", (void **)&g_pfnWrMsr64Carefully);
            if (RT_FAILURE(rc2))
# endif
                g_pfnWrMsr64Carefully = NULL;

            LogRel(("VBoxDrv: g_pfnRdMsrCarefully=%p g_pfnRdMsr64Carefully=%p g_pfnWrMsr64Carefully=%p\n",
                    g_pfnRdMsrCarefully, g_pfnRdMsr64Carefully, g_pfnWrMsr64Carefully));

#endif /* SUPDRV_WITH_MSR_PROBER */
        }

        RTR0DbgKrnlInfoRelease(hKrnlInfo);
    }
    else
        LogRel(("VBoxDrv: Failed to open kernel symbols, rc=%Rrc\n", rc));
    return rc;
}


#ifdef VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION

/**
 * Initalizes the certificate stores (code signing) in the device extension.
 */
static int supdrvDarwinInitCertStores(PSUPDRVDEVEXT pDevExt)
{
    pDevExt->hAdditionalStore = NIL_RTCRSTORE;

    pDevExt->hRootStore       = NIL_RTCRSTORE;
    int rc = RTCrStoreCreateInMem(&pDevExt->hRootStore, g_cSUPTrustedTAs + 1);
    if (RT_SUCCESS(rc))
    {
        for (uint32_t i = 0; i < g_cSUPTrustedTAs; i++)
        {
            int rc2 = RTCrStoreCertAddEncoded(pDevExt->hRootStore, RTCRCERTCTX_F_ENC_TAF_DER,
                                              g_aSUPTrustedTAs[i].pch, g_aSUPTrustedTAs[i].cb, NULL);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            {
                printf("VBoxDrv: Error loading g_aSUPTrustedTAs[%u]: %d\n", i, rc);
                rc = rc2;
            }
        }

        /* We implicitly trust the build certificate. */
        int rc2 = RTCrStoreCertAddEncoded(pDevExt->hRootStore, RTCRCERTCTX_F_ENC_X509_DER,
                                          g_abSUPBuildCert, g_cbSUPBuildCert, NULL);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
        {
            printf("VBoxDrv: Error loading g_cbSUPBuildCert: %d\n", rc);
            rc = rc2;
        }
    }
    return rc;
}


/**
 * Releases the certificate stores in the device extension.
 */
static void supdrvDarwinDestroyCertStores(PSUPDRVDEVEXT pDevExt)
{
    if (pDevExt->hRootStore != NIL_RTCRSTORE)
    {
        uint32_t cRefs = RTCrStoreRelease(pDevExt->hRootStore);
        Assert(cRefs == 0); RT_NOREF(cRefs);
        pDevExt->hRootStore = NIL_RTCRSTORE;
    }
    if (pDevExt->hAdditionalStore != NIL_RTCRSTORE)
    {
        uint32_t cRefs = RTCrStoreRelease(pDevExt->hAdditionalStore);
        Assert(cRefs == 0); RT_NOREF(cRefs);
        pDevExt->hAdditionalStore = NIL_RTCRSTORE;
    }
}

#endif /* VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION */

/**
 * Stop the kernel module.
 */
static kern_return_t    VBoxDrvDarwinStop(struct kmod_info *pKModInfo, void *pvData)
{
    RT_NOREF(pKModInfo, pvData);
    int rc;
    LogFlow(("VBoxDrvDarwinStop\n"));

    /** @todo I've got a nagging feeling that we'll have to keep track of users and refuse
     * unloading if we're busy. Investigate and implement this! */

    /*
     * Undo the work done during start (in reverse order).
     */
    if (g_pSleepNotifier)
    {
        g_pSleepNotifier->remove();
        g_pSleepNotifier = NULL;
    }

    devfs_remove(g_hDevFsDeviceUsr);
    g_hDevFsDeviceUsr = NULL;

    devfs_remove(g_hDevFsDeviceSys);
    g_hDevFsDeviceSys = NULL;

    rc = cdevsw_remove(g_iMajorDeviceNo, &g_DevCW);
    Assert(rc == g_iMajorDeviceNo);
    g_iMajorDeviceNo = -1;

    supdrvDeleteDevExt(&g_DevExt);

    rc = RTSpinlockDestroy(g_Spinlock);
    AssertRC(rc);
    g_Spinlock = NIL_RTSPINLOCK;

#ifdef VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION
    supdrvDarwinDestroyCertStores(&g_DevExt);
#endif

    RTR0TermForced();

    memset(&g_DevExt, 0, sizeof(g_DevExt));
#ifdef DEBUG
    printf("VBoxDrvDarwinStop - done\n");
#endif
    return KMOD_RETURN_SUCCESS;
}


/**
 * Device open. Called on open /dev/vboxdrv
 *
 * @param   Dev         The device number.
 * @param   fFlags      ???.
 * @param   fDevType    ???.
 * @param   pProcess    The process issuing this request.
 */
static int VBoxDrvDarwinOpen(dev_t Dev, int fFlags, int fDevType, struct proc *pProcess)
{
    RT_NOREF(fFlags, fDevType);
#ifdef DEBUG_DARWIN_GIP
    char szName[128];
    szName[0] = '\0';
    proc_name(proc_pid(pProcess), szName, sizeof(szName));
    Log(("VBoxDrvDarwinOpen: pid=%d '%s'\n", proc_pid(pProcess), szName));
#endif

    /*
     * Only two minor devices numbers are allowed.
     */
    if (minor(Dev) != 0 && minor(Dev) != 1)
        return EACCES;

    /*
     * The process issuing the request must be the current process.
     */
    RTPROCESS Process = RTProcSelf();
    if ((int)Process != proc_pid(pProcess))
        return EIO;

    /*
     * Find the session created by org_virtualbox_SupDrvClient, fail
     * if no such session, and mark it as opened. We set the uid & gid
     * here too, since that is more straight forward at this point.
     */
    const bool      fUnrestricted = minor(Dev) == 0;
    int             rc = VINF_SUCCESS;
    PSUPDRVSESSION  pSession = NULL;
    kauth_cred_t    pCred = kauth_cred_proc_ref(pProcess);
    if (pCred)
    {
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1070
        RTUID           Uid = kauth_cred_getruid(pCred);
        RTGID           Gid = kauth_cred_getrgid(pCred);
#else
        RTUID           Uid = pCred->cr_ruid;
        RTGID           Gid = pCred->cr_rgid;
#endif
        unsigned        iHash = SESSION_HASH(Process);
        RTSpinlockAcquire(g_Spinlock);

        pSession = g_apSessionHashTab[iHash];
        while (pSession && pSession->Process != Process)
            pSession = pSession->pNextHash;
        if (pSession)
        {
            if (!pSession->fOpened)
            {
                pSession->fOpened = true;
                pSession->fUnrestricted = fUnrestricted;
                pSession->Uid = Uid;
                pSession->Gid = Gid;
            }
            else
                rc = VERR_ALREADY_LOADED;
        }
        else
            rc = VERR_GENERAL_FAILURE;

        RTSpinlockRelease(g_Spinlock);
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
        kauth_cred_unref(&pCred);
#else  /* 10.4 */
        /* The 10.4u SDK headers and 10.4.11 kernel source have inconsistent definitions
           of kauth_cred_unref(), so use the other (now deprecated) API for releasing it. */
        kauth_cred_rele(pCred);
#endif /* 10.4 */
    }
    else
        rc = VERR_INVALID_PARAMETER;

#ifdef DEBUG_DARWIN_GIP
    OSDBGPRINT(("VBoxDrvDarwinOpen: pid=%d '%s' pSession=%p rc=%d\n", proc_pid(pProcess), szName, pSession, rc));
#else
    Log(("VBoxDrvDarwinOpen: g_DevExt=%p pSession=%p rc=%d pid=%d\n", &g_DevExt, pSession, rc, proc_pid(pProcess)));
#endif
    return VBoxDrvDarwinErr2DarwinErr(rc);
}


/**
 * Close device.
 */
static int VBoxDrvDarwinClose(dev_t Dev, int fFlags, int fDevType, struct proc *pProcess)
{
    RT_NOREF(Dev, fFlags, fDevType, pProcess);
    Log(("VBoxDrvDarwinClose: pid=%d\n", (int)RTProcSelf()));
    Assert(proc_pid(pProcess) == (int)RTProcSelf());

    /*
     * Hand the session closing to org_virtualbox_SupDrvClient.
     */
    org_virtualbox_SupDrvClient::sessionClose(RTProcSelf());
    return 0;
}


/**
 * Device I/O Control entry point.
 *
 * @returns Darwin for slow IOCtls and VBox status code for the fast ones.
 * @param   Dev         The device number (major+minor).
 * @param   iCmd        The IOCtl command.
 * @param   pData       Pointer to the data (if any it's a SUPDRVIOCTLDATA (kernel copy)).
 * @param   fFlags      Flag saying we're a character device (like we didn't know already).
 * @param   pProcess    The process issuing this request.
 */
static int VBoxDrvDarwinIOCtl(dev_t Dev, u_long iCmd, caddr_t pData, int fFlags, struct proc *pProcess)
{
    RT_NOREF(fFlags);
    const bool          fUnrestricted = minor(Dev) == 0;
    const RTPROCESS     Process = proc_pid(pProcess);
    const unsigned      iHash = SESSION_HASH(Process);
    PSUPDRVSESSION      pSession;

#ifdef VBOX_WITH_EFLAGS_AC_SET_IN_VBOXDRV
    /*
     * Refuse all I/O control calls if we've ever detected EFLAGS.AC being cleared.
     *
     * This isn't a problem, as there is absolutely nothing in the kernel context that
     * depend on user context triggering cleanups.  That would be pretty wild, right?
     */
    if (RT_UNLIKELY(g_DevExt.cBadContextCalls > 0))
    {
        SUPR0Printf("VBoxDrvDarwinIOCtl: EFLAGS.AC=0 detected %u times, refusing all I/O controls!\n", g_DevExt.cBadContextCalls);
        return EDEVERR;
    }
#endif

    /*
     * Find the session.
     */
    RTSpinlockAcquire(g_Spinlock);

    pSession = g_apSessionHashTab[iHash];
    while (pSession && (pSession->Process != Process || pSession->fUnrestricted != fUnrestricted || !pSession->fOpened))
        pSession = pSession->pNextHash;

    if (RT_LIKELY(pSession))
        supdrvSessionRetain(pSession);

    RTSpinlockRelease(g_Spinlock);
    if (RT_UNLIKELY(!pSession))
    {
        OSDBGPRINT(("VBoxDrvDarwinIOCtl: WHAT?!? pSession == NULL! This must be a mistake... pid=%d iCmd=%#lx\n",
                    (int)Process, iCmd));
        return EINVAL;
    }

    /*
     * Deal with the two high-speed IOCtl that takes it's arguments from
     * the session and iCmd, and only returns a VBox status code.
     */
    int rc;
    AssertCompile((SUP_IOCTL_FAST_DO_FIRST & 0xff) == (SUP_IOCTL_FLAG | 64));
    if (   (uintptr_t)(iCmd - SUP_IOCTL_FAST_DO_FIRST) < (uintptr_t)32
        && fUnrestricted)
        rc = supdrvIOCtlFast(iCmd - SUP_IOCTL_FAST_DO_FIRST, *(uint32_t *)pData, &g_DevExt, pSession);
    else
        rc = VBoxDrvDarwinIOCtlSlow(pSession, iCmd, pData, pProcess);

    supdrvSessionRelease(pSession);
    return rc;
}


#ifndef VBOX_WITHOUT_EFLAGS_AC_SET_IN_VBOXDRV
/**
 * Alternative Device I/O Control entry point on hosts with SMAP support.
 *
 * @returns Darwin for slow IOCtls and VBox status code for the fast ones.
 * @param   Dev         The device number (major+minor).
 * @param   iCmd        The IOCtl command.
 * @param   pData       Pointer to the data (if any it's a SUPDRVIOCTLDATA (kernel copy)).
 * @param   fFlags      Flag saying we're a character device (like we didn't know already).
 * @param   pProcess    The process issuing this request.
 */
static int VBoxDrvDarwinIOCtlSMAP(dev_t Dev, u_long iCmd, caddr_t pData, int fFlags, struct proc *pProcess)
{
    /*
     * Allow VBox R0 code to touch R3 memory. Setting the AC bit disables the
     * SMAP check.
     */
    RTCCUINTREG fSavedEfl = ASMAddFlags(X86_EFL_AC);

    int rc = VBoxDrvDarwinIOCtl(Dev, iCmd, pData, fFlags, pProcess);

# if defined(VBOX_STRICT) || defined(VBOX_WITH_EFLAGS_AC_SET_IN_VBOXDRV)
    /*
     * Before we restore AC and the rest of EFLAGS, check if the IOCtl handler code
     * accidentially modified it or some other important flag.
     */
    if (RT_UNLIKELY(   (ASMGetFlags() & (X86_EFL_AC | X86_EFL_IF | X86_EFL_DF | X86_EFL_IOPL))
                    != ((fSavedEfl    & (X86_EFL_AC | X86_EFL_IF | X86_EFL_DF | X86_EFL_IOPL)) | X86_EFL_AC) ))
    {
        char szTmp[48];
        RTStrPrintf(szTmp, sizeof(szTmp), "iCmd=%#x: %#x->%#x!", iCmd, (uint32_t)fSavedEfl, (uint32_t)ASMGetFlags());
        supdrvBadContext(&g_DevExt, "SUPDrv-darwin.cpp",  __LINE__, szTmp);
    }
# endif

    ASMSetFlags(fSavedEfl);
    return rc;
}
#endif /* VBOX_WITHOUT_EFLAGS_AC_SET_IN_VBOXDRV */


/**
 * Worker for VBoxDrvDarwinIOCtl that takes the slow IOCtl functions.
 *
 * @returns Darwin errno.
 *
 * @param pSession  The session.
 * @param iCmd      The IOCtl command.
 * @param pData     Pointer to the kernel copy of the SUPDRVIOCTLDATA buffer.
 * @param pProcess  The calling process.
 */
static int VBoxDrvDarwinIOCtlSlow(PSUPDRVSESSION pSession, u_long iCmd, caddr_t pData, struct proc *pProcess)
{
    RT_NOREF(pProcess);
    LogFlow(("VBoxDrvDarwinIOCtlSlow: pSession=%p iCmd=%p pData=%p pProcess=%p\n", pSession, iCmd, pData, pProcess));


    /*
     * Buffered or unbuffered?
     */
    PSUPREQHDR pHdr;
    user_addr_t pUser = 0;
    void *pvPageBuf = NULL;
    uint32_t cbReq = IOCPARM_LEN(iCmd);
    if ((IOC_DIRMASK & iCmd) == IOC_INOUT)
    {
        pHdr = (PSUPREQHDR)pData;
        if (RT_UNLIKELY(cbReq < sizeof(*pHdr)))
        {
            OSDBGPRINT(("VBoxDrvDarwinIOCtlSlow: cbReq=%#x < %#x; iCmd=%#lx\n", cbReq, (int)sizeof(*pHdr), iCmd));
            return EINVAL;
        }
        if (RT_UNLIKELY((pHdr->fFlags & SUPREQHDR_FLAGS_MAGIC_MASK) != SUPREQHDR_FLAGS_MAGIC))
        {
            OSDBGPRINT(("VBoxDrvDarwinIOCtlSlow: bad magic fFlags=%#x; iCmd=%#lx\n", pHdr->fFlags, iCmd));
            return EINVAL;
        }
        if (RT_UNLIKELY(    RT_MAX(pHdr->cbIn, pHdr->cbOut) != cbReq
                        ||  pHdr->cbIn < sizeof(*pHdr)
                        ||  pHdr->cbOut < sizeof(*pHdr)))
        {
            OSDBGPRINT(("VBoxDrvDarwinIOCtlSlow: max(%#x,%#x) != %#x; iCmd=%#lx\n", pHdr->cbIn, pHdr->cbOut, cbReq, iCmd));
            return EINVAL;
        }
    }
    else if ((IOC_DIRMASK & iCmd) == IOC_VOID && !cbReq)
    {
        /*
         * Get the header and figure out how much we're gonna have to read.
         */
        IPRT_DARWIN_SAVE_EFL_AC();
        SUPREQHDR Hdr;
        pUser = (user_addr_t)*(void **)pData;
        int rc = copyin(pUser, &Hdr, sizeof(Hdr));
        if (RT_UNLIKELY(rc))
        {
            OSDBGPRINT(("VBoxDrvDarwinIOCtlSlow: copyin(%llx,Hdr,) -> %#x; iCmd=%#lx\n", (unsigned long long)pUser, rc, iCmd));
            IPRT_DARWIN_RESTORE_EFL_AC();
            return rc;
        }
        if (RT_UNLIKELY((Hdr.fFlags & SUPREQHDR_FLAGS_MAGIC_MASK) != SUPREQHDR_FLAGS_MAGIC))
        {
            OSDBGPRINT(("VBoxDrvDarwinIOCtlSlow: bad magic fFlags=%#x; iCmd=%#lx\n", Hdr.fFlags, iCmd));
            IPRT_DARWIN_RESTORE_EFL_AC();
            return EINVAL;
        }
        cbReq = RT_MAX(Hdr.cbIn, Hdr.cbOut);
        if (RT_UNLIKELY(    Hdr.cbIn < sizeof(Hdr)
                        ||  Hdr.cbOut < sizeof(Hdr)
                        ||  cbReq > _1M*16))
        {
            OSDBGPRINT(("VBoxDrvDarwinIOCtlSlow: max(%#x,%#x); iCmd=%#lx\n", Hdr.cbIn, Hdr.cbOut, iCmd));
            IPRT_DARWIN_RESTORE_EFL_AC();
            return EINVAL;
        }

        /*
         * Allocate buffer and copy in the data.
         */
        pHdr = (PSUPREQHDR)RTMemTmpAlloc(cbReq);
        if (!pHdr)
            pvPageBuf = pHdr = (PSUPREQHDR)IOMallocAligned(RT_ALIGN_Z(cbReq, PAGE_SIZE), 8);
        if (RT_UNLIKELY(!pHdr))
        {
            OSDBGPRINT(("VBoxDrvDarwinIOCtlSlow: failed to allocate buffer of %d bytes; iCmd=%#lx\n", cbReq, iCmd));
            IPRT_DARWIN_RESTORE_EFL_AC();
            return ENOMEM;
        }
        rc = copyin(pUser, pHdr, Hdr.cbIn);
        if (RT_UNLIKELY(rc))
        {
            OSDBGPRINT(("VBoxDrvDarwinIOCtlSlow: copyin(%llx,%p,%#x) -> %#x; iCmd=%#lx\n",
                        (unsigned long long)pUser, pHdr, Hdr.cbIn, rc, iCmd));
            if (pvPageBuf)
                IOFreeAligned(pvPageBuf, RT_ALIGN_Z(cbReq, PAGE_SIZE));
            else
                RTMemTmpFree(pHdr);
            IPRT_DARWIN_RESTORE_EFL_AC();
            return rc;
        }
        if (Hdr.cbIn < cbReq)
            RT_BZERO((uint8_t *)pHdr + Hdr.cbIn, cbReq - Hdr.cbIn);
        IPRT_DARWIN_RESTORE_EFL_AC();
    }
    else
    {
        Log(("VBoxDrvDarwinIOCtlSlow: huh? cbReq=%#x iCmd=%#lx\n", cbReq, iCmd));
        return EINVAL;
    }

    /*
     * Process the IOCtl.
     */
    int rc = supdrvIOCtl(iCmd, &g_DevExt, pSession, pHdr, cbReq);
    if (RT_LIKELY(!rc))
    {
        /*
         * If not buffered, copy back the buffer before returning.
         */
        if (pUser)
        {
            IPRT_DARWIN_SAVE_EFL_AC();
            uint32_t cbOut = pHdr->cbOut;
            if (cbOut > cbReq)
            {
                OSDBGPRINT(("VBoxDrvDarwinIOCtlSlow: too much output! %#x > %#x; uCmd=%#lx!\n", cbOut, cbReq, iCmd));
                cbOut = cbReq;
            }
            rc = copyout(pHdr, pUser, cbOut);
            if (RT_UNLIKELY(rc))
                OSDBGPRINT(("VBoxDrvDarwinIOCtlSlow: copyout(%p,%llx,%#x) -> %d; uCmd=%#lx!\n",
                            pHdr, (unsigned long long)pUser, cbOut, rc, iCmd));

            /* cleanup */
            if (pvPageBuf)
                IOFreeAligned(pvPageBuf, RT_ALIGN_Z(cbReq, PAGE_SIZE));
            else
                RTMemTmpFree(pHdr);
            IPRT_DARWIN_RESTORE_EFL_AC();
        }
    }
    else
    {
        /*
         * The request failed, just clean up.
         */
        if (pUser)
        {
            if (pvPageBuf)
            {
                IPRT_DARWIN_SAVE_EFL_AC();
                IOFreeAligned(pvPageBuf, RT_ALIGN_Z(cbReq, PAGE_SIZE));
                IPRT_DARWIN_RESTORE_EFL_AC();
            }
            else
                RTMemTmpFree(pHdr);
        }

        Log(("VBoxDrvDarwinIOCtlSlow: pid=%d iCmd=%lx pData=%p failed, rc=%d\n", proc_pid(pProcess), iCmd, (void *)pData, rc));
        rc = EINVAL;
    }

    Log2(("VBoxDrvDarwinIOCtlSlow: returns %d\n", rc));
    return rc;
}


/**
 * The SUPDRV IDC entry point.
 *
 * @returns VBox status code, see supdrvIDC.
 * @param   uReq        The request code.
 * @param   pReq        The request.
 */
DECLEXPORT(int) VBOXCALL SUPDrvDarwinIDC(uint32_t uReq, PSUPDRVIDCREQHDR pReq)
{
    PSUPDRVSESSION  pSession;

    /*
     * Some quick validations.
     */
    if (RT_UNLIKELY(!RT_VALID_PTR(pReq)))
        return VERR_INVALID_POINTER;

    pSession = pReq->pSession;
    if (pSession)
    {
        if (RT_UNLIKELY(!RT_VALID_PTR(pSession)))
            return VERR_INVALID_PARAMETER;
        if (RT_UNLIKELY(pSession->pDevExt != &g_DevExt))
            return VERR_INVALID_PARAMETER;
    }
    else if (RT_UNLIKELY(uReq != SUPDRV_IDC_REQ_CONNECT))
        return VERR_INVALID_PARAMETER;

    /*
     * Do the job.
     */
    return supdrvIDC(uReq, &g_DevExt, pSession, pReq);
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


/**
 * Initializes any OS specific object creator fields.
 */
void VBOXCALL   supdrvOSObjInitCreator(PSUPDRVOBJ pObj, PSUPDRVSESSION pSession)
{
    NOREF(pObj);
    NOREF(pSession);
}


/**
 * Checks if the session can access the object.
 *
 * @returns true if a decision has been made.
 * @returns false if the default access policy should be applied.
 *
 * @param   pObj        The object in question.
 * @param   pSession    The session wanting to access the object.
 * @param   pszObjName  The object name, can be NULL.
 * @param   prc         Where to store the result when returning true.
 */
bool VBOXCALL   supdrvOSObjCanAccess(PSUPDRVOBJ pObj, PSUPDRVSESSION pSession, const char *pszObjName, int *prc)
{
    NOREF(pObj);
    NOREF(pSession);
    NOREF(pszObjName);
    NOREF(prc);
    return false;
}

/**
 * Callback for blah blah blah.
 */
IOReturn VBoxDrvDarwinSleepHandler(void * /* pvTarget */, void *pvRefCon, UInt32 uMessageType,
                                   IOService *pProvider, void *pvMsgArg, vm_size_t cbMsgArg)
{
    RT_NOREF(pProvider, pvMsgArg, cbMsgArg);
    LogFlow(("VBoxDrv: Got sleep/wake notice. Message type was %x\n", uMessageType));

    if (uMessageType == kIOMessageSystemWillSleep)
        RTPowerSignalEvent(RTPOWEREVENT_SUSPEND);
    else if (uMessageType == kIOMessageSystemHasPoweredOn)
        RTPowerSignalEvent(RTPOWEREVENT_RESUME);

    acknowledgeSleepWakeNotification(pvRefCon);

    return 0;
}


#ifdef VBOX_WITH_HOST_VMX
/**
 * For cleaning up the mess we left behind on Yosemite with 4.3.28 and earlier.
 *
 * We ASSUME VT-x is supported by the CPU.
 *
 * @param   idCpu       Unused.
 * @param   pvUser1     Unused.
 * @param   pvUser2     Unused.
 */
static DECLCALLBACK(void) vboxdrvDarwinVmxEnableFix(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    RT_NOREF(idCpu, pvUser1, pvUser2);
    RTCCUINTREG uCr4 = ASMGetCR4();
    if (!(uCr4 & X86_CR4_VMXE))
    {
        uCr4 |= X86_CR4_VMXE;
        ASMSetCR4(uCr4);
    }
}
#endif


/**
 * @copydoc SUPR0EnableVTx
 */
int VBOXCALL supdrvOSEnableVTx(bool fEnable)
{
#ifdef VBOX_WITH_HOST_VMX
    int rc;
    if (   version_major >= 10 /* 10 = 10.6.x = Snow Leopard */
# ifdef VBOX_WITH_RAW_MODE
        && g_pfnVmxSuspend
        && g_pfnVmxResume
        && g_pVmxUseCount
# endif
       )
    {
        IPRT_DARWIN_SAVE_EFL_AC();
        if (fEnable)
        {
            /*
             * We screwed up on Yosemite and didn't notice that we weren't
             * calling host_vmxon.  CR4.VMXE may therefore have been disabled
             * by us.  So, first time around we make sure it's set so we won't
             * crash in the pre-4.3.28/5.0RC1 upgrade scenario.
             * See @bugref{7907}.
             */
            static bool volatile g_fDoneCleanup = false;
            if (!g_fDoneCleanup)
            {
                if (version_major == 14 /* 14 = 10.10 = yosemite */)
                {
                    uint32_t fCaps;
                    rc = supdrvQueryVTCapsInternal(&fCaps);
                    if (RT_SUCCESS(rc))
                    {
                        if (fCaps & SUPVTCAPS_VT_X)
                            rc = RTMpOnAll(vboxdrvDarwinVmxEnableFix, NULL, NULL);
                        else
                            rc = VERR_VMX_NO_VMX;
                    }
                    if (RT_FAILURE(rc))
                    {
                        IPRT_DARWIN_RESTORE_EFL_AC();
                        return rc;
                    }
                }
                g_fDoneCleanup = true;
            }

            /*
             * Call the kernel.
             */
            AssertLogRelMsg(!g_pVmxUseCount || *g_pVmxUseCount >= 0,
                            ("vmx_use_count=%d (@ %p, expected it to be a positive number\n",
                             *g_pVmxUseCount, g_pVmxUseCount));

            rc = host_vmxon(false /* exclusive */);
            if (rc == VMX_OK)
                rc = VINF_SUCCESS;
            else if (rc == VMX_UNSUPPORTED)
                rc = VERR_VMX_NO_VMX;
            else if (rc == VMX_INUSE)
                rc = VERR_VMX_IN_VMX_ROOT_MODE;
            else /* shouldn't happen, but just in case. */
            {
                LogRel(("host_vmxon returned %d\n", rc));
                rc = VERR_UNRESOLVED_ERROR;
            }
            LogRel(("VBoxDrv: host_vmxon  -> vmx_use_count=%d rc=%Rrc\n", *g_pVmxUseCount, rc));
        }
        else
        {
            AssertLogRelMsgReturn(!g_pVmxUseCount || *g_pVmxUseCount >= 1,
                                  ("vmx_use_count=%d (@ %p, expected it to be a non-zero positive number\n",
                                   *g_pVmxUseCount, g_pVmxUseCount),
                                  VERR_WRONG_ORDER);
            host_vmxoff();
            rc = VINF_SUCCESS;
            LogRel(("VBoxDrv: host_vmxoff -> vmx_use_count=%d\n", *g_pVmxUseCount));
        }
        IPRT_DARWIN_RESTORE_EFL_AC();
    }
    else
    {
        /* In 10.5.x the host_vmxon is severely broken!  Don't use it, it will
           frequnetly panic the host. */
        rc = VERR_NOT_SUPPORTED;
    }
    return rc;
#else
    return VERR_NOT_SUPPORTED;
#endif
}


/**
 * @copydoc SUPR0SuspendVTxOnCpu
 */
bool VBOXCALL supdrvOSSuspendVTxOnCpu(void)
{
#ifdef VBOX_WITH_HOST_VMX
    /*
     * Consult the VMX usage counter, don't try suspend if not enabled.
     *
     * Note!  The host_vmxon/off code is still race prone since, but this is
     *        currently the best we can do without always enable VMX when
     *        loading the driver.
     */
    if (   g_pVmxUseCount
        && *g_pVmxUseCount > 0)
    {
        IPRT_DARWIN_SAVE_EFL_AC();
        g_pfnVmxSuspend();
        IPRT_DARWIN_RESTORE_EFL_AC();
        return true;
    }
    return false;
#else
    return false;
#endif
}


/**
 * @copydoc SUPR0ResumeVTxOnCpu
 */
void VBOXCALL   supdrvOSResumeVTxOnCpu(bool fSuspended)
{
#ifdef VBOX_WITH_HOST_VMX
    /*
     * Don't consult the counter here, the state knows better.
     * We're executing with interrupts disabled and anyone racing us with
     * disabling VT-x will be waiting in the rendezvous code.
     */
    if (   fSuspended
        && g_pfnVmxResume)
    {
        IPRT_DARWIN_SAVE_EFL_AC();
        g_pfnVmxResume();
        IPRT_DARWIN_RESTORE_EFL_AC();
    }
    else
        Assert(!fSuspended);
#else
    Assert(!fSuspended);
#endif
}


bool VBOXCALL supdrvOSGetForcedAsyncTscMode(PSUPDRVDEVEXT pDevExt)
{
    NOREF(pDevExt);
    return false;
}


bool VBOXCALL supdrvOSAreCpusOfflinedOnSuspend(void)
{
    /** @todo verify this. */
    return false;
}


bool VBOXCALL supdrvOSAreTscDeltasInSync(void)
{
    return false;
}


#ifdef VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION

/**
 * @callback_method_impl{FNRTLDRIMPORT}
 */
static DECLCALLBACK(int) supdrvDarwinLdrOpenImportCallback(RTLDRMOD hLdrMod, const char *pszModule, const char *pszSymbol,
                                                           unsigned uSymbol, PRTLDRADDR pValue, void *pvUser)
{
    PSUPDRVDEVEXT pDevExt = (PSUPDRVDEVEXT)pvUser;

    /*
     * First consult the VMMR0 module if there is one fully loaded.
     * This is necessary as VMMR0 may overload assertion and logger symbols.
     */
    if (pDevExt->pvVMMR0)
        for (PSUPDRVLDRIMAGE pImage = pDevExt->pLdrImages; pImage; pImage = pImage->pNext)
            if (pImage->pvImage == pDevExt->pvVMMR0)
            {
                if (   pImage->uState == SUP_IOCTL_LDR_LOAD
                    && pImage->hLdrMod != NIL_RTLDRMOD)
                {
                    int rc = RTLdrGetSymbolEx(pImage->hLdrMod, pImage->pvImage, (uintptr_t)pImage->pvImage,
                                              UINT32_MAX, pszSymbol, pValue);
                    if (RT_SUCCESS(rc))
                        return VINF_SUCCESS;
                }
                break;
            }

    /*
     * Then we consult the SUPDrv export table.
     */
    uintptr_t uValue = 0;
    int rc = supdrvLdrGetExportedSymbol(pszSymbol, &uValue);
    if (RT_SUCCESS(rc))
    {
        *pValue = uValue;
        return VINF_SUCCESS;
    }

    /*
     * Failed.
     */
    printf("VBoxDrv: Unable to resolve symbol '%s'.\n", pszSymbol);
    RT_NOREF(hLdrMod, pszModule, uSymbol);
    return VERR_SYMBOL_NOT_FOUND;
}


/**
 * @callback_method_impl{FNRTCRPKCS7VERIFYCERTCALLBACK,
 *      Verify that the signing certificate is sane.}
 */
static DECLCALLBACK(int) supdrvDarwinLdrOpenVerifyCertificatCallback(PCRTCRX509CERTIFICATE pCert, RTCRX509CERTPATHS hCertPaths,
                                                                     uint32_t fFlags, void *pvUser, PRTERRINFO pErrInfo)
{
    RT_NOREF(pvUser); //PSUPDRVDEVEXT pDevExt = (PSUPDRVDEVEXT)pvUser;
# ifdef DEBUG_bird
    printf("supdrvDarwinLdrOpenVerifyCertificatCallback: pCert=%p hCertPaths=%p\n", (void *)pCert, (void *)hCertPaths);
# endif

# if 0
    /*
     * Test signing certificates normally doesn't have all the necessary
     * features required below.  So, treat them as special cases.
     */
    if (   hCertPaths == NIL_RTCRX509CERTPATHS
        && RTCrX509Name_Compare(&pCert->TbsCertificate.Issuer, &pCert->TbsCertificate.Subject) == 0)
    {
        RTMsgInfo("Test signed.\n");
        return VINF_SUCCESS;
    }
# endif

    /*
     * Standard code signing capabilites required.
     */
    int rc = RTCrPkcs7VerifyCertCallbackCodeSigning(pCert, hCertPaths, fFlags, NULL, pErrInfo);
    if (   RT_SUCCESS(rc)
        && (fFlags & RTCRPKCS7VCC_F_SIGNED_DATA))
    {
        uint32_t cDevIdApp  = 0;
        uint32_t cDevIdKext = 0;
        uint32_t cDevIdMacDev = 0;
        for (uint32_t i = 0; i < pCert->TbsCertificate.T3.Extensions.cItems; i++)
        {
            PCRTCRX509EXTENSION pExt = pCert->TbsCertificate.T3.Extensions.papItems[i];
            if (RTAsn1ObjId_CompareWithString(&pExt->ExtnId, RTCR_APPLE_CS_DEVID_APPLICATION_OID) == 0)
            {
                cDevIdApp++;
                if (!pExt->Critical.fValue)
                    rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                                       "Dev ID Application certificate extension is not flagged critical");
            }
            else if (RTAsn1ObjId_CompareWithString(&pExt->ExtnId, RTCR_APPLE_CS_DEVID_KEXT_OID) == 0)
            {
                cDevIdKext++;
                if (!pExt->Critical.fValue)
                    rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                                       "Dev ID kext certificate extension is not flagged critical");
            }
            else if (RTAsn1ObjId_CompareWithString(&pExt->ExtnId, RTCR_APPLE_CS_DEVID_MAC_SW_DEV_OID) == 0)
            {
                cDevIdMacDev++;
                if (!pExt->Critical.fValue)
                    rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                                       "Dev ID MAC SW dev certificate extension is not flagged critical");
            }
        }
# ifdef VBOX_WITH_DARWIN_R0_TEST_SIGN
        /*
         * Mac application software development certs do not have the usually required extensions.
         */
        if (cDevIdMacDev)
        {
            cDevIdApp++;
            cDevIdKext++;
        }
# endif
        if (cDevIdApp == 0)
            rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                               "Certificate is missing the 'Dev ID Application' extension");
        if (cDevIdKext == 0)
            rc = RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE,
                               "Certificate is missing the 'Dev ID kext' extension");
    }

    return rc;
}


/**
 * @callback_method_impl{FNRTLDRVALIDATESIGNEDDATA}
 */
static DECLCALLBACK(int) supdrvDarwinLdrOpenVerifyCallback(RTLDRMOD hLdrMod, PCRTLDRSIGNATUREINFO pInfo,
                                                           PRTERRINFO pErrInfo, void *pvUser)
{
    PSUPDRVDEVEXT pDevExt = (PSUPDRVDEVEXT)pvUser;
    RT_NOREF_PV(hLdrMod);

    switch (pInfo->enmType)
    {
        case RTLDRSIGNATURETYPE_PKCS7_SIGNED_DATA:
            if (pInfo->pvExternalData)
            {
                PCRTCRPKCS7CONTENTINFO pContentInfo = (PCRTCRPKCS7CONTENTINFO)pInfo->pvSignature;
                RTTIMESPEC             ValidationTime;
                RTTimeNow(&ValidationTime);

                return RTCrPkcs7VerifySignedDataWithExternalData(pContentInfo,
                                                                 RTCRPKCS7VERIFY_SD_F_COUNTER_SIGNATURE_SIGNING_TIME_ONLY
                                                                 | RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_SIGNING_TIME_IF_PRESENT
                                                                 | RTCRPKCS7VERIFY_SD_F_ALWAYS_USE_MS_TIMESTAMP_IF_PRESENT,
                                                                 pDevExt->hAdditionalStore, pDevExt->hRootStore, &ValidationTime,
                                                                 supdrvDarwinLdrOpenVerifyCertificatCallback, pDevExt,
                                                                 pInfo->pvExternalData, pInfo->cbExternalData, pErrInfo);
            }
            return RTErrInfoSetF(pErrInfo, VERR_NOT_SUPPORTED, "Expected external data with signature!");

        default:
            return RTErrInfoSetF(pErrInfo, VERR_NOT_SUPPORTED, "Unsupported signature type: %d", pInfo->enmType);
    }
}

#endif /* VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION */

int  VBOXCALL   supdrvOSLdrOpen(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const char *pszFilename)
{
#ifdef VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION
    /*
     * Initialize our members.
     */
    pImage->hLdrMod   = NIL_RTLDRMOD;
    pImage->hMemAlloc = NIL_RTR0MEMOBJ;

    /*
     * We have to double buffer the file to be avoid a potential race between
     * validation and actual image loading.  This could be eliminated later by
     * baking the image validation into the RTLdrGetBits operation.
     *
     * Note! After calling RTLdrOpenInMemory, pvFile is owned by the loader and will be
     *       freed via the RTFileReadAllFree callback when the loader module is closed.
     */
    void     *pvFile = NULL;
    size_t    cbFile = 0;
    int rc = RTFileReadAllEx(pszFilename, 0, _32M, RTFILE_RDALL_O_DENY_WRITE, &pvFile, &cbFile);
    if (RT_SUCCESS(rc))
    {
        PRTERRINFOSTATIC pErrInfo = (PRTERRINFOSTATIC)RTMemTmpAlloc(sizeof(RTERRINFOSTATIC));
        RTLDRMOD         hLdrMod = NIL_RTLDRMOD;
        rc = RTLdrOpenInMemory(pszFilename, 0 /*fFlags*/, RTLDRARCH_HOST, cbFile,
                               NULL /*pfnRead*/, RTFileReadAllFree, pvFile,
                               &hLdrMod, pErrInfo ? RTErrInfoInitStatic(pErrInfo) : NULL);
        if (RT_SUCCESS(rc))
        {
            /*
             * Validate the image.
             */
            rc = RTLdrVerifySignature(hLdrMod, supdrvDarwinLdrOpenVerifyCallback, pDevExt,
                                      pErrInfo ? RTErrInfoInitStatic(pErrInfo) : NULL);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Allocate memory for the object and load it into it.
                 */
                size_t cbImage = RTLdrSize(hLdrMod);
                if (cbImage == pImage->cbImageBits)
                {
                    RTR0MEMOBJ hMemAlloc;
                    rc = RTR0MemObjAllocPage(&hMemAlloc, cbImage, true /*fExecutable*/);
                    if (RT_SUCCESS(rc))
                    {
                        void *pvImageBits = RTR0MemObjAddress(hMemAlloc);
                        rc = RTLdrGetBits(hLdrMod, pvImageBits, (uintptr_t)pvImageBits,
                                          supdrvDarwinLdrOpenImportCallback, pDevExt);
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Commit.
                             */
                            pImage->hMemAlloc   = hMemAlloc;
                            pImage->hLdrMod     = hLdrMod;
                            pImage->pvImage     = pvImageBits;
                            RTMemTmpFree(pErrInfo);
                            /** @todo Call RTLdrDone. */
                            kprintf("VBoxDrv: Loaded %s at %p\n", pImage->szName, pvImageBits);
                            return VINF_SUCCESS;
                        }

                        RTR0MemObjFree(hMemAlloc, true /*fFreeMappings*/);
                    }
                    else
                        printf("VBoxDrv: Failed to allocate %u bytes for %s: %d\n", (unsigned)cbImage, pszFilename, rc);
                }
                else
                {
                    printf("VBoxDrv: Image size mismatch for %s: %#x, ring-3 says %#x\n",
                           pszFilename, (unsigned)cbImage, (unsigned)pImage->cbImageBits);
                    rc = VERR_LDR_MISMATCH_NATIVE;
                }
            }
            else if (pErrInfo && RTErrInfoIsSet(&pErrInfo->Core))
                printf("VBoxDrv: RTLdrVerifySignature(%s) failed: %d - %s\n", pszFilename, rc, pErrInfo->Core.pszMsg);
            else
                printf("VBoxDrv: RTLdrVerifySignature(%s) failed: %d\n", pszFilename, rc);
            RTLdrClose(hLdrMod);
        }
        else if (pErrInfo && RTErrInfoIsSet(&pErrInfo->Core))
            printf("VBoxDrv: RTLdrOpenInMemory(%s) failed: %d - %s\n", pszFilename, rc, pErrInfo->Core.pszMsg);
        else
            printf("VBoxDrv: RTLdrOpenInMemory(%s) failed: %d\n", pszFilename, rc);
        RTMemTmpFree(pErrInfo);
    }
    return rc;
#else  /* !VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION */
    NOREF(pDevExt); NOREF(pImage); NOREF(pszFilename);
    return VERR_NOT_SUPPORTED;
#endif /* !VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION */
}


#ifdef VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION
/**
 *  @callback_method_impl{FNRTLDRENUMSYMS,
 *      Worker for supdrvOSLdrValidatePointer.
 */
static DECLCALLBACK(int) supdrvDarwinLdrValidatePointerCallback(RTLDRMOD hLdrMod, const char *pszSymbol, unsigned uSymbol,
                                                                RTLDRADDR Value, void *pvUser)
{
    RT_NOREF(hLdrMod, pszSymbol, uSymbol);
    if (Value == (uintptr_t)pvUser)
        return VINF_CALLBACK_RETURN;
    return VINF_SUCCESS;
}
#endif


int  VBOXCALL   supdrvOSLdrValidatePointer(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, void *pv,
                                           const uint8_t *pbImageBits, const char *pszSymbol)
{
#ifdef VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION
    AssertReturn(pImage->hLdrMod != NIL_RTLDRMOD, VERR_INVALID_STATE);

    /*
     * If we've got a symbol name, just to a lookup and compare addresses.
     */
    int rc;
    if (RT_C_IS_UPPER(*pszSymbol))
    {
        RTLDRADDR uValueFound;
        rc = RTLdrGetSymbolEx(pImage->hLdrMod, pImage->pvImage, (uintptr_t)pImage->pvImage, UINT32_MAX, pszSymbol, &uValueFound);
        if (RT_SUCCESS(rc))
        {
            if (uValueFound == (uintptr_t)pv)
                rc = VINF_SUCCESS;
            else
            {
                SUPR0Printf("SUPDrv: Different exports found for %s in %s: %RTptr, expected %p\n",
                            pszSymbol, pImage->szName, (RTUINTPTR)uValueFound, pv);
                rc = VERR_LDR_BAD_FIXUP;
            }
        }
        else
            SUPR0Printf("SUPDrv: No export named %s (%p) in %s!\n", pszSymbol, pv, pImage->szName);
    }
    /*
     * Otherwise do a symbol enumeration and look for the entrypoint.
     */
    else
    {
        rc = RTLdrEnumSymbols(pImage->hLdrMod, 0 /*fFlags*/, pImage->pvImage, (uintptr_t)pImage->pvImage,
                              supdrvDarwinLdrValidatePointerCallback, pv);
        if (rc == VINF_CALLBACK_RETURN)
            rc = VINF_SUCCESS;
        else if (RT_SUCCESS(rc))
        {
            SUPR0Printf("SUPDrv: No export with address %p (%s) in %s!\n", pv, pszSymbol, pImage->szName);
            rc = VERR_NOT_FOUND;
        }
        else
            SUPR0Printf("SUPDrv: RTLdrEnumSymbols failed on %s: %Rrc\n", pImage->szName, rc);
    }
    RT_NOREF(pDevExt, pbImageBits);
    return rc;
#else
    NOREF(pDevExt); NOREF(pImage); NOREF(pv); NOREF(pbImageBits); NOREF(pszSymbol);
    return VERR_NOT_SUPPORTED;
#endif
}


int  VBOXCALL   supdrvOSLdrQuerySymbol(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage,
                                       const char *pszSymbol, size_t cchSymbol, void **ppvSymbol)
{
#ifdef VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION
    /*
     * Just hand the problem to RTLdrGetSymbolEx.
     */
    RTLDRADDR uValueFound;
    int rc = RTLdrGetSymbolEx(pImage->hLdrMod, pImage->pvImage, (uintptr_t)pImage->pvImage, UINT32_MAX, pszSymbol, &uValueFound);
    if (RT_SUCCESS(rc))
    {
        *ppvSymbol = (void *)(uintptr_t)uValueFound;
        return VINF_SUCCESS;
    }
    RT_NOREF(pDevExt, cchSymbol);
    return rc;

#else
    RT_NOREF(pDevExt, pImage, pszSymbol, cchSymbol, ppvSymbol);
    return VERR_WRONG_ORDER;
#endif
}


int  VBOXCALL   supdrvOSLdrLoad(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const uint8_t *pbImageBits, PSUPLDRLOAD pReq)
{
#ifdef VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION
    /* State paranoia. */
    AssertReturn(pImage->hLdrMod != NIL_RTLDRMOD, VERR_INVALID_STATE);
    AssertReturn(pImage->hMemAlloc != NIL_RTR0MEMOBJ, VERR_INVALID_STATE);
    AssertReturn(pImage->pvImage, VERR_INVALID_STATE);

    /*
     * We should get an identical match with ring-3 here, so the code here is
     * trivial in comparision to SUPDrv-win.cpp.
     */
    if (!memcmp(pImage->pvImage, pbImageBits, pImage->cbImageBits))
        return VINF_SUCCESS;

    /*
     * Try show what when wrong (code is copied from supdrvNtCompare).
     */
    uint32_t        cbLeft       = pImage->cbImageBits;
    const uint8_t  *pbNativeBits = (const uint8_t *)pImage->pvImage;
    for (size_t off = 0; cbLeft > 0; off++, cbLeft--)
        if (pbNativeBits[off] != pbImageBits[off])
        {
            /* Note! We need to copy image bits into a temporary stack buffer here as we'd
                     otherwise risk overwriting them while formatting the error message. */
            uint8_t abBytes[64];
            memcpy(abBytes, &pbImageBits[off], RT_MIN(64, cbLeft));
            supdrvLdrLoadError(VERR_LDR_MISMATCH_NATIVE, pReq,
                               "Mismatch at %#x (%p) of %s loaded at %p:\n"
                               "ring-0: %.*Rhxs\n"
                               "ring-3: %.*Rhxs",
                               off, &pbNativeBits[off], pImage->szName, pImage->pvImage,
                               RT_MIN(64, cbLeft), &pbNativeBits[off],
                               RT_MIN(64, cbLeft), &abBytes[0]);
            printf("SUPDrv: %s\n", pReq->u.Out.szError);
            break;
        }

    RT_NOREF(pDevExt);
    return VERR_LDR_MISMATCH_NATIVE;

#else
    NOREF(pDevExt); NOREF(pImage); NOREF(pbImageBits); NOREF(pReq);
    return VERR_NOT_SUPPORTED;
#endif
}


void VBOXCALL   supdrvOSLdrUnload(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage)
{
#ifdef VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION
    if (pImage->hLdrMod != NIL_RTLDRMOD)
    {
        int rc = RTLdrClose(pImage->hLdrMod);
        AssertRC(rc);
        pImage->hLdrMod = NIL_RTLDRMOD;
    }
    if (pImage->hMemAlloc != NIL_RTR0MEMOBJ)
    {
        RTR0MemObjFree(pImage->hMemAlloc, true /*fFreeMappings*/);
        pImage->hMemAlloc = NIL_RTR0MEMOBJ;
    }
    NOREF(pDevExt);
#else
    NOREF(pDevExt); NOREF(pImage);
#endif
}


void VBOXCALL   supdrvOSLdrNotifyLoaded(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage)
{
    NOREF(pDevExt); NOREF(pImage);
}


void VBOXCALL   supdrvOSLdrNotifyOpened(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const char *pszFilename)
{
#if 1
    NOREF(pDevExt); NOREF(pImage); NOREF(pszFilename);
#else
    /*
     * Try store the image load address in NVRAM so we can retrived it on panic.
     * Note! This only works if you're root! - Acutally, it doesn't work at all at the moment. FIXME!
     */
    IORegistryEntry *pEntry = IORegistryEntry::fromPath("/options", gIODTPlane);
    if (pEntry)
    {
        char szVar[80];
        RTStrPrintf(szVar, sizeof(szVar), "vboximage"/*-%s*/, pImage->szName);
        char szValue[48];
        RTStrPrintf(szValue, sizeof(szValue), "%#llx,%#llx", (uint64_t)(uintptr_t)pImage->pvImage,
                    (uint64_t)(uintptr_t)pImage->pvImage + pImage->cbImageBits - 1);
        bool fRc = pEntry->setProperty(szVar, szValue); NOREF(fRc);
        pEntry->release();
        SUPR0Printf("fRc=%d '%s'='%s'\n", fRc, szVar, szValue);
    }
    /*else
        SUPR0Printf("failed to find /options in gIODTPlane\n");*/
#endif
}


void VBOXCALL   supdrvOSLdrNotifyUnloaded(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage)
{
    NOREF(pDevExt); NOREF(pImage);
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

typedef struct SUPDRVDARWINMSRARGS
{
    RTUINT64U       uValue;
    uint32_t        uMsr;
    int             rc;
} SUPDRVDARWINMSRARGS, *PSUPDRVDARWINMSRARGS;

/**
 * On CPU worker for supdrvOSMsrProberRead.
 *
 * @param   idCpu           Ignored.
 * @param   pvUser1         Pointer to a SUPDRVDARWINMSRARGS.
 * @param   pvUser2         Ignored.
 */
static DECLCALLBACK(void) supdrvDarwinMsrProberReadOnCpu(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    PSUPDRVDARWINMSRARGS pArgs = (PSUPDRVDARWINMSRARGS)pvUser1;
    if (g_pfnRdMsr64Carefully)
        pArgs->rc = g_pfnRdMsr64Carefully(pArgs->uMsr, &pArgs->uValue.u);
    else if (g_pfnRdMsrCarefully)
        pArgs->rc = g_pfnRdMsrCarefully(pArgs->uMsr, &pArgs->uValue.s.Lo, &pArgs->uValue.s.Hi);
    else
        pArgs->rc = 2;
    NOREF(idCpu); NOREF(pvUser2);
}


int VBOXCALL    supdrvOSMsrProberRead(uint32_t uMsr, RTCPUID idCpu, uint64_t *puValue)
{
    if (!g_pfnRdMsr64Carefully && !g_pfnRdMsrCarefully)
        return VERR_NOT_SUPPORTED;

    SUPDRVDARWINMSRARGS Args;
    Args.uMsr     = uMsr;
    Args.uValue.u = 0;
    Args.rc       = -1;

    if (idCpu == NIL_RTCPUID)
    {
        IPRT_DARWIN_SAVE_EFL_AC();
        supdrvDarwinMsrProberReadOnCpu(idCpu, &Args, NULL);
        IPRT_DARWIN_RESTORE_EFL_AC();
    }
    else
    {
        int rc = RTMpOnSpecific(idCpu, supdrvDarwinMsrProberReadOnCpu, &Args, NULL);
        if (RT_FAILURE(rc))
            return rc;
    }

    if (Args.rc)
        return VERR_ACCESS_DENIED;
    *puValue = Args.uValue.u;
    return VINF_SUCCESS;
}


/**
 * On CPU worker for supdrvOSMsrProberWrite.
 *
 * @param   idCpu           Ignored.
 * @param   pvUser1         Pointer to a SUPDRVDARWINMSRARGS.
 * @param   pvUser2         Ignored.
 */
static DECLCALLBACK(void) supdrvDarwinMsrProberWriteOnCpu(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    PSUPDRVDARWINMSRARGS pArgs = (PSUPDRVDARWINMSRARGS)pvUser1;
    if (g_pfnWrMsr64Carefully)
        pArgs->rc = g_pfnWrMsr64Carefully(pArgs->uMsr, pArgs->uValue.u);
    else
        pArgs->rc = 2;
    NOREF(idCpu); NOREF(pvUser2);
}


int VBOXCALL    supdrvOSMsrProberWrite(uint32_t uMsr, RTCPUID idCpu, uint64_t uValue)
{
    if (!g_pfnWrMsr64Carefully)
        return VERR_NOT_SUPPORTED;

    SUPDRVDARWINMSRARGS Args;
    Args.uMsr     = uMsr;
    Args.uValue.u = uValue;
    Args.rc       = -1;

    if (idCpu == NIL_RTCPUID)
    {
        IPRT_DARWIN_SAVE_EFL_AC();
        supdrvDarwinMsrProberWriteOnCpu(idCpu, &Args, NULL);
        IPRT_DARWIN_RESTORE_EFL_AC();
    }
    else
    {
        int rc = RTMpOnSpecific(idCpu, supdrvDarwinMsrProberWriteOnCpu, &Args, NULL);
        if (RT_FAILURE(rc))
            return rc;
    }

    if (Args.rc)
        return VERR_ACCESS_DENIED;
    return VINF_SUCCESS;
}


/**
 * Worker for supdrvOSMsrProberModify.
 */
static DECLCALLBACK(void) supdrvDarwinMsrProberModifyOnCpu(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    RT_NOREF(idCpu, pvUser2);
    PSUPMSRPROBER               pReq    = (PSUPMSRPROBER)pvUser1;
    register uint32_t           uMsr    = pReq->u.In.uMsr;
    bool const                  fFaster = pReq->u.In.enmOp == SUPMSRPROBEROP_MODIFY_FASTER;
    uint64_t                    uBefore;
    uint64_t                    uWritten;
    uint64_t                    uAfter;
    int                         rcBefore, rcWrite, rcAfter, rcRestore;
    RTCCUINTREG                 fOldFlags;

    /* Initialize result variables. */
    uBefore = uWritten = uAfter    = 0;
    rcWrite = rcAfter  = rcRestore = -1;

    /*
     * Do the job.
     */
    fOldFlags = ASMIntDisableFlags();
    ASMCompilerBarrier(); /* paranoia */
    if (!fFaster)
        ASMWriteBackAndInvalidateCaches();

    rcBefore = g_pfnRdMsr64Carefully(uMsr, &uBefore);
    if (rcBefore >= 0)
    {
        register uint64_t uRestore = uBefore;
        uWritten  = uRestore;
        uWritten &= pReq->u.In.uArgs.Modify.fAndMask;
        uWritten |= pReq->u.In.uArgs.Modify.fOrMask;

        rcWrite   = g_pfnWrMsr64Carefully(uMsr, uWritten);
        rcAfter   = g_pfnRdMsr64Carefully(uMsr, &uAfter);
        rcRestore = g_pfnWrMsr64Carefully(uMsr, uRestore);

        if (!fFaster)
        {
            ASMWriteBackAndInvalidateCaches();
            ASMReloadCR3();
            ASMNopPause();
        }
    }

    ASMCompilerBarrier(); /* paranoia */
    ASMSetFlags(fOldFlags);

    /*
     * Write out the results.
     */
    pReq->u.Out.uResults.Modify.uBefore    = uBefore;
    pReq->u.Out.uResults.Modify.uWritten   = uWritten;
    pReq->u.Out.uResults.Modify.uAfter     = uAfter;
    pReq->u.Out.uResults.Modify.fBeforeGp  = rcBefore  != 0;
    pReq->u.Out.uResults.Modify.fModifyGp  = rcWrite   != 0;
    pReq->u.Out.uResults.Modify.fAfterGp   = rcAfter   != 0;
    pReq->u.Out.uResults.Modify.fRestoreGp = rcRestore != 0;
    RT_ZERO(pReq->u.Out.uResults.Modify.afReserved);
}


int VBOXCALL    supdrvOSMsrProberModify(RTCPUID idCpu, PSUPMSRPROBER pReq)
{
    if (!g_pfnWrMsr64Carefully || !g_pfnRdMsr64Carefully)
        return VERR_NOT_SUPPORTED;
    if (idCpu == NIL_RTCPUID)
    {
        IPRT_DARWIN_SAVE_EFL_AC();
        supdrvDarwinMsrProberModifyOnCpu(idCpu, pReq, NULL);
        IPRT_DARWIN_RESTORE_EFL_AC();
        return VINF_SUCCESS;
    }
    return RTMpOnSpecific(idCpu, supdrvDarwinMsrProberModifyOnCpu, pReq, NULL);
}

#endif /* SUPDRV_WITH_MSR_PROBER */

/**
 * Resume Bluetooth keyboard.
 * If there is no Bluetooth keyboard device connected to the system we just ignore this.
 */
static void supdrvDarwinResumeBluetoothKbd(void)
{
    OSDictionary *pDictionary = IOService::serviceMatching("AppleBluetoothHIDKeyboard");
    if (pDictionary)
    {
        OSIterator     *pIter;
        IOBluetoothHIDDriver *pDriver;

        pIter = IOService::getMatchingServices(pDictionary);
        if (pIter)
        {
            while ((pDriver = (IOBluetoothHIDDriver *)pIter->getNextObject()))
                if (pDriver->isKeyboard())
                    (void)pDriver->hidControl(IOBTHID_CONTROL_EXIT_SUSPEND);

            pIter->release();
        }
        pDictionary->release();
    }
}

/**
 * Resume built-in keyboard on MacBook Air and Pro hosts.
 * If there is no built-in keyboard device attached to the system we just ignore this.
 */
static void supdrvDarwinResumeBuiltinKbd(void)
{
    /** @todo macbook pro 16 w/ 10.15.5 as the "Apple Internal Keyboard /
     *        Trackpad" hooked up to "HID Relay" / "AppleUserUSBHostHIDDevice"
     *        and "AppleUserUSBHostHIDDevice" among other things, but not
     *        "AppleUSBTCKeyboard". This change is probably older than 10.15,
     *        given that IOUSBHIDDriver not is present in the 10.11 SDK. */
#if MAC_OS_X_VERSION_MIN_REQUIRED < 101100
    /*
     * AppleUSBTCKeyboard KEXT is responsible for built-in keyboard management.
     * We resume keyboard by accessing to its IOService.
     */
    OSDictionary *pDictionary = IOService::serviceMatching("AppleUSBTCKeyboard");
    if (pDictionary)
    {
        OSIterator     *pIter;
        IOUSBHIDDriver *pDriver;

        pIter = IOService::getMatchingServices(pDictionary);
        if (pIter)
        {
            while ((pDriver = (IOUSBHIDDriver *)pIter->getNextObject()))
                if (pDriver->IsPortSuspended())
                    pDriver->SuspendPort(false, 0);

            pIter->release();
        }
        pDictionary->release();
    }
#endif
}


/**
 * Resume suspended keyboard devices (if any).
 */
int VBOXCALL    supdrvDarwinResumeSuspendedKbds(void)
{
    IPRT_DARWIN_SAVE_EFL_AC();
    supdrvDarwinResumeBuiltinKbd();
    supdrvDarwinResumeBluetoothKbd();
    IPRT_DARWIN_RESTORE_EFL_AC();
    return 0;
}


/**
 * Converts an IPRT error code to a darwin error code.
 *
 * @returns corresponding darwin error code.
 * @param   rc      IPRT status code.
 */
static int VBoxDrvDarwinErr2DarwinErr(int rc)
{
    switch (rc)
    {
        case VINF_SUCCESS:              return 0;
        case VERR_GENERAL_FAILURE:      return EACCES;
        case VERR_INVALID_PARAMETER:    return EINVAL;
        case VERR_INVALID_MAGIC:        return EILSEQ;
        case VERR_INVALID_HANDLE:       return ENXIO;
        case VERR_INVALID_POINTER:      return EFAULT;
        case VERR_LOCK_FAILED:          return ENOLCK;
        case VERR_ALREADY_LOADED:       return EEXIST;
        case VERR_PERMISSION_DENIED:    return EPERM;
        case VERR_VERSION_MISMATCH:     return ENOSYS;
    }

    return EPERM;
}


/**
 * Check if the CPU has SMAP support.
 */
static bool vboxdrvDarwinCpuHasSMAP(void)
{
    uint32_t uMaxId, uEAX, uEBX, uECX, uEDX;
    ASMCpuId(0, &uMaxId, &uEBX, &uECX, &uEDX);
    if (   RTX86IsValidStdRange(uMaxId)
        && uMaxId >= 0x00000007)
    {
        ASMCpuId_Idx_ECX(0x00000007, 0, &uEAX, &uEBX, &uECX, &uEDX);
        if (uEBX & X86_CPUID_STEXT_FEATURE_EBX_SMAP)
            return true;
    }
#ifdef VBOX_WITH_EFLAGS_AC_SET_IN_VBOXDRV
    return true;
#else
    return false;
#endif
}


RTDECL(int) SUPR0PrintfV(const char *pszFormat, va_list va)
{
    IPRT_DARWIN_SAVE_EFL_AC();

    char szMsg[512];
    RTStrPrintfV(szMsg, sizeof(szMsg) - 1, pszFormat, va);
    szMsg[sizeof(szMsg) - 1] = '\0';

    printf("%s", szMsg);
    kprintf("%s", szMsg);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return 0;
}


SUPR0DECL(uint32_t) SUPR0GetKernelFeatures(void)
{
    return g_fKernelFeatures;
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

/*
 *
 * org_virtualbox_SupDrv
 *
 * - IOService diff resync -
 * - IOService diff resync -
 * - IOService diff resync -
 *
 */


/**
 * Initialize the object.
 */
bool org_virtualbox_SupDrv::init(OSDictionary *pDictionary)
{
    LogFlow(("IOService::init([%p], %p)\n", this, pDictionary));
    if (IOService::init(pDictionary))
    {
        /* init members. */
        return true;
    }
    return false;
}


/**
 * Free the object.
 */
void org_virtualbox_SupDrv::free(void)
{
    LogFlow(("IOService::free([%p])\n", this));
    IOService::free();
}


/**
 * Check if it's ok to start this service.
 * It's always ok by us, so it's up to IOService to decide really.
 */
IOService *org_virtualbox_SupDrv::probe(IOService *pProvider, SInt32 *pi32Score)
{
    LogFlow(("IOService::probe([%p])\n", this));
    return IOService::probe(pProvider, pi32Score);
}


/**
 * Start this service.
 */
bool org_virtualbox_SupDrv::start(IOService *pProvider)
{
    LogFlow(("org_virtualbox_SupDrv::start([%p])\n", this));

    if (IOService::start(pProvider))
    {
        /* register the service. */
        registerService();
        return true;
    }
    return false;
}


/**
 * Stop this service.
 */
void org_virtualbox_SupDrv::stop(IOService *pProvider)
{
    LogFlow(("org_virtualbox_SupDrv::stop([%p], %p)\n", this, pProvider));
    IOService::stop(pProvider);
}


/**
 * Termination request.
 *
 * @return  true if we're ok with shutting down now, false if we're not.
 * @param   fOptions        Flags.
 */
bool org_virtualbox_SupDrv::terminate(IOOptionBits fOptions)
{
    bool fRc;
    LogFlow(("org_virtualbox_SupDrv::terminate: reference_count=%d g_cSessions=%d (fOptions=%#x)\n",
             KMOD_INFO_NAME.reference_count, ASMAtomicUoReadS32(&g_cSessions), fOptions));
    if (    KMOD_INFO_NAME.reference_count != 0
        ||  ASMAtomicUoReadS32(&g_cSessions))
        fRc = false;
    else
        fRc = IOService::terminate(fOptions);
    LogFlow(("org_virtualbox_SupDrv::terminate: returns %d\n", fRc));
    return fRc;
}


/*
 *
 * org_virtualbox_SupDrvClient
 *
 */


/**
 * Initializer called when the client opens the service.
 */
bool org_virtualbox_SupDrvClient::initWithTask(task_t OwningTask, void *pvSecurityId, UInt32 u32Type)
{
    LogFlow(("org_virtualbox_SupDrvClient::initWithTask([%p], %#x, %p, %#x) (cur pid=%d proc=%p)\n",
             this, OwningTask, pvSecurityId, u32Type, RTProcSelf(), RTR0ProcHandleSelf()));
    AssertMsg((RTR0PROCESS)OwningTask == RTR0ProcHandleSelf(), ("%p %p\n", OwningTask, RTR0ProcHandleSelf()));

    if (!OwningTask)
        return false;

    if (u32Type != SUP_DARWIN_IOSERVICE_COOKIE)
    {
        VBOX_RETRIEVE_CUR_PROC_NAME(szProcName);
        LogRelMax(10,("org_virtualbox_SupDrvClient::initWithTask: Bad cookie %#x (%s)\n", u32Type, szProcName));
        return false;
    }

    if (IOUserClient::initWithTask(OwningTask, pvSecurityId , u32Type))
    {
        /*
         * In theory we have to call task_reference() to make sure that the task is
         * valid during the lifetime of this object. The pointer is only used to check
         * for the context this object is called in though and never dereferenced
         * or passed to anything which might, so we just skip this step.
         */
        m_Task = OwningTask;
        m_pSession = NULL;
        m_pProvider = NULL;
        return true;
    }
    return false;
}


/**
 * Start the client service.
 */
bool org_virtualbox_SupDrvClient::start(IOService *pProvider)
{
    LogFlow(("org_virtualbox_SupDrvClient::start([%p], %p) (cur pid=%d proc=%p)\n",
             this, pProvider, RTProcSelf(), RTR0ProcHandleSelf() ));
    AssertMsgReturn((RTR0PROCESS)m_Task == RTR0ProcHandleSelf(),
                    ("%p %p\n", m_Task, RTR0ProcHandleSelf()),
                    false);

    if (IOUserClient::start(pProvider))
    {
        m_pProvider = OSDynamicCast(org_virtualbox_SupDrv, pProvider);
        if (m_pProvider)
        {
            Assert(!m_pSession);

            /*
             * Create a new session.
             */
            int rc = supdrvCreateSession(&g_DevExt, true /* fUser */, false /*fUnrestricted*/, &m_pSession);
            if (RT_SUCCESS(rc))
            {
                m_pSession->fOpened = false;
                /* The Uid, Gid and fUnrestricted fields are set on open. */

                /*
                 * Insert it into the hash table, checking that there isn't
                 * already one for this process first. (One session per proc!)
                 */
                unsigned iHash = SESSION_HASH(m_pSession->Process);
                RTSpinlockAcquire(g_Spinlock);

                PSUPDRVSESSION pCur = g_apSessionHashTab[iHash];
                while (pCur && pCur->Process != m_pSession->Process)
                    pCur = pCur->pNextHash;
                if (!pCur)
                {
                    m_pSession->pNextHash = g_apSessionHashTab[iHash];
                    g_apSessionHashTab[iHash] = m_pSession;
                    m_pSession->pvSupDrvClient = this;
                    ASMAtomicIncS32(&g_cSessions);
                    rc = VINF_SUCCESS;
                }
                else
                    rc = VERR_ALREADY_LOADED;

                RTSpinlockRelease(g_Spinlock);
                if (RT_SUCCESS(rc))
                {
                    Log(("org_virtualbox_SupDrvClient::start: created session %p for pid %d\n", m_pSession, (int)RTProcSelf()));
                    return true;
                }

                LogFlow(("org_virtualbox_SupDrvClient::start: already got a session for this process (%p)\n", pCur));
                supdrvSessionRelease(m_pSession);
            }

            m_pSession = NULL;
            LogFlow(("org_virtualbox_SupDrvClient::start: rc=%Rrc from supdrvCreateSession\n", rc));
        }
        else
            LogFlow(("org_virtualbox_SupDrvClient::start: %p isn't org_virtualbox_SupDrv\n", pProvider));
    }
    return false;
}


/**
 * Common worker for clientClose and VBoxDrvDarwinClose.
 */
/* static */ void org_virtualbox_SupDrvClient::sessionClose(RTPROCESS Process)
{
    /*
     * Find the session and remove it from the hash table.
     *
     * Note! Only one session per process. (Both start() and
     * VBoxDrvDarwinOpen makes sure this is so.)
     */
    const unsigned  iHash = SESSION_HASH(Process);
    RTSpinlockAcquire(g_Spinlock);
    PSUPDRVSESSION  pSession = g_apSessionHashTab[iHash];
    if (pSession)
    {
        if (pSession->Process == Process)
        {
            g_apSessionHashTab[iHash] = pSession->pNextHash;
            pSession->pNextHash = NULL;
            ASMAtomicDecS32(&g_cSessions);
        }
        else
        {
            PSUPDRVSESSION pPrev = pSession;
            pSession = pSession->pNextHash;
            while (pSession)
            {
                if (pSession->Process == Process)
                {
                    pPrev->pNextHash = pSession->pNextHash;
                    pSession->pNextHash = NULL;
                    ASMAtomicDecS32(&g_cSessions);
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
        Log(("SupDrvClient::sessionClose: pSession == NULL, pid=%d; freed already?\n", (int)Process));
        return;
    }

    /*
     * Remove it from the client object.
     */
    org_virtualbox_SupDrvClient *pThis = (org_virtualbox_SupDrvClient *)pSession->pvSupDrvClient;
    pSession->pvSupDrvClient = NULL;
    if (pThis)
    {
        Assert(pThis->m_pSession == pSession);
        pThis->m_pSession = NULL;
    }

    /*
     * Close the session.
     */
    supdrvSessionRelease(pSession);
}


/**
 * Client exits normally.
 */
IOReturn org_virtualbox_SupDrvClient::clientClose(void)
{
    LogFlow(("org_virtualbox_SupDrvClient::clientClose([%p]) (cur pid=%d proc=%p)\n", this, RTProcSelf(), RTR0ProcHandleSelf()));
    AssertMsg((RTR0PROCESS)m_Task == RTR0ProcHandleSelf(), ("%p %p\n", m_Task, RTR0ProcHandleSelf()));

    /*
     * Clean up the session if it's still around.
     *
     * We cannot rely 100% on close, and in the case of a dead client
     * we'll end up hanging inside vm_map_remove() if we postpone it.
     */
    if (m_pSession)
    {
        sessionClose(RTProcSelf());
        Assert(!m_pSession);
    }

    m_pProvider = NULL;
    terminate();

    return kIOReturnSuccess;
}


/**
 * The client exits abnormally / forgets to do cleanups. (logging)
 */
IOReturn org_virtualbox_SupDrvClient::clientDied(void)
{
    LogFlow(("IOService::clientDied([%p]) m_Task=%p R0Process=%p Process=%d\n", this, m_Task, RTR0ProcHandleSelf(), RTProcSelf()));

    /* IOUserClient::clientDied() calls clientClose, so we'll just do the work there. */
    return IOUserClient::clientDied();
}


/**
 * Terminate the service (initiate the destruction). (logging)
 */
bool org_virtualbox_SupDrvClient::terminate(IOOptionBits fOptions)
{
    LogFlow(("IOService::terminate([%p], %#x)\n", this, fOptions));
    return IOUserClient::terminate(fOptions);
}


/**
 * The final stage of the client service destruction. (logging)
 */
bool org_virtualbox_SupDrvClient::finalize(IOOptionBits fOptions)
{
    LogFlow(("IOService::finalize([%p], %#x)\n", this, fOptions));
    return IOUserClient::finalize(fOptions);
}


/**
 * Stop the client service. (logging)
 */
void org_virtualbox_SupDrvClient::stop(IOService *pProvider)
{
    LogFlow(("IOService::stop([%p])\n", this));
    IOUserClient::stop(pProvider);
}

