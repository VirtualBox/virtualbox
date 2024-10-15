/* $Id: VBoxPci-linux.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxPci - PCI Driver (Host), Linux Specific Code.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include "the-linux-kernel.h"
#include "version-generated.h"
#include "revision-generated.h"
#include "product-generated.h"

#define LOG_GROUP LOG_GROUP_DEV_PCI_RAW
#include <VBox/log.h>
#include <VBox/err.h>
#include <iprt/process.h>
#include <iprt/initterm.h>
#include <iprt/string.h>
#include <iprt/mem.h>

#include "../VBoxPciInternal.h"

#ifdef VBOX_WITH_IOMMU
# include <linux/dmar.h>
# include <linux/intel-iommu.h>
# include <linux/pci.h>
# if RTLNX_VER_MAX(3,1,0) && \
     (RTLNX_VER_MAX(2,6,41) || RTLNX_VER_MIN(3,0,0))
#  include <asm/amd_iommu.h>
# else
#  include <linux/amd-iommu.h>
# endif
# if RTLNX_VER_MAX(3,2,0)
#  define IOMMU_PRESENT()      iommu_found()
#  define IOMMU_DOMAIN_ALLOC() iommu_domain_alloc()
# else
#  define IOMMU_PRESENT()      iommu_present(&pci_bus_type)
#  define IOMMU_DOMAIN_ALLOC() iommu_domain_alloc(&pci_bus_type)
# endif
#endif /* VBOX_WITH_IOMMU */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int  __init VBoxPciLinuxInit(void);
static void __exit VBoxPciLinuxUnload(void);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static VBOXRAWPCIGLOBALS g_VBoxPciGlobals;

module_init(VBoxPciLinuxInit);
module_exit(VBoxPciLinuxUnload);

MODULE_AUTHOR(VBOX_VENDOR);
MODULE_DESCRIPTION(VBOX_PRODUCT " PCI access Driver");
MODULE_LICENSE("GPL");
#ifdef MODULE_VERSION
MODULE_VERSION(VBOX_VERSION_STRING " r" RT_XSTR(VBOX_SVN_REV));
#endif


#if RTLNX_VER_MIN(2,6,20)
# define PCI_DEV_GET(v,d,p)            pci_get_device(v,d,p)
# define PCI_DEV_PUT(x)                pci_dev_put(x)
#if RTLNX_VER_MIN(4,17,0)
/* assume the domain number to be zero - exactly the same assumption of
 * pci_get_bus_and_slot()
 */
# define PCI_DEV_GET_SLOT(bus, devfn)  pci_get_domain_bus_and_slot(0, bus, devfn)
#else
# define PCI_DEV_GET_SLOT(bus, devfn)  pci_get_bus_and_slot(bus, devfn)
#endif
#else
# define PCI_DEV_GET(v,d,p)            pci_find_device(v,d,p)
# define PCI_DEV_PUT(x)                do { } while (0)
# define PCI_DEV_GET_SLOT(bus, devfn)  pci_find_slot(bus, devfn)
#endif

/**
 * Name of module used to attach to the host PCI device, when
 * PCI device passthrough is used.
 */
#define PCI_STUB_MODULE      "pci-stub"
/* For some reasons my kernel names module for find_module() this way,
 * while device name seems to be above one.
 */
#define PCI_STUB_MODULE_NAME "pci_stub"

/**
 * Our driver name.
 */
#define DRIVER_NAME      "vboxpci"

/*
 * Currently we keep the device bound to pci stub driver, so
 * dev_printk() &co would report that instead of our name. They also
 * expect non-NULL dev pointer in older kernels.
 */
#define vbpci_printk(level, pdev, format, arg...)               \
       printk(level DRIVER_NAME "%s%s: " format,                \
              pdev ? " " : "", pdev ? pci_name(pdev) : "",      \
              ## arg)


/**
 * Initialize module.
 *
 * @returns appropriate status code.
 */
static int __init VBoxPciLinuxInit(void)
{
    int rc;
    /*
     * Initialize IPRT.
     */
    rc = RTR0Init(0);

    if (RT_FAILURE(rc))
        goto error;


    LogRel(("VBoxPciLinuxInit\n"));

    RT_ZERO(g_VBoxPciGlobals);

    rc = vboxPciInit(&g_VBoxPciGlobals);
    if (RT_FAILURE(rc))
    {
        LogRel(("cannot do VBoxPciInit: %Rc\n", rc));
        goto error;
    }

#if defined(CONFIG_PCI_STUB)
    /* nothing to do, pci_stub module part of the kernel */
    g_VBoxPciGlobals.fPciStubModuleAvail = true;

#elif defined(CONFIG_PCI_STUB_MODULE)
    if (request_module(PCI_STUB_MODULE) == 0)
    {
# if RTLNX_VER_MIN(2,6,30)
        /* find_module() is static before Linux 2.6.30 */
        mutex_lock(&module_mutex);
        g_VBoxPciGlobals.pciStubModule = find_module(PCI_STUB_MODULE_NAME);
        mutex_unlock(&module_mutex);
        if (g_VBoxPciGlobals.pciStubModule)
        {
            if (try_module_get(g_VBoxPciGlobals.pciStubModule))
                g_VBoxPciGlobals.fPciStubModuleAvail = true;
        }
        else
            printk(KERN_INFO "vboxpci: find_module %s failed\n", PCI_STUB_MODULE);
# endif
    }
    else
        printk(KERN_INFO "vboxpci: cannot load %s\n", PCI_STUB_MODULE);

#else
    printk(KERN_INFO "vboxpci: %s module not available, cannot detach PCI devices\n",
                      PCI_STUB_MODULE);
#endif

#ifdef VBOX_WITH_IOMMU
    if (IOMMU_PRESENT())
        printk(KERN_INFO "vboxpci: IOMMU found\n");
    else
        printk(KERN_INFO "vboxpci: IOMMU not found (not registered)\n");
#else
    printk(KERN_INFO "vboxpci: IOMMU not found (not compiled)\n");
#endif

    return 0;

  error:
    return -RTErrConvertToErrno(rc);
}

/**
 * Unload the module.
 */
static void __exit VBoxPciLinuxUnload(void)
{
    LogRel(("VBoxPciLinuxLinuxUnload\n"));

    /*
     * Undo the work done during start (in reverse order).
     */
    vboxPciShutdown(&g_VBoxPciGlobals);

    RTR0Term();

    if (g_VBoxPciGlobals.pciStubModule)
    {
        module_put(g_VBoxPciGlobals.pciStubModule);
        g_VBoxPciGlobals.pciStubModule = NULL;
    }

    Log(("VBoxPciLinuxUnload - done\n"));
}

static int vboxPciLinuxDevRegisterWithIommu(PVBOXRAWPCIINS pIns)
{
#ifdef VBOX_WITH_IOMMU
    int rc = VINF_SUCCESS;
    struct pci_dev *pPciDev = pIns->pPciDev;
    PVBOXRAWPCIDRVVM pData = VBOX_DRV_VMDATA(pIns);
    IPRT_LINUX_SAVE_EFL_AC();

    if (RT_LIKELY(pData))
    {
        if (RT_LIKELY(pData->pIommuDomain))
        {
            /** @todo KVM checks IOMMU_CAP_CACHE_COHERENCY and sets
             *  flag IOMMU_CACHE later used when mapping physical
             *  addresses, which could improve performance.
             */
            int rcLnx = iommu_attach_device(pData->pIommuDomain, &pPciDev->dev);
            if (!rcLnx)
            {
                vbpci_printk(KERN_DEBUG, pPciDev, "attached to IOMMU\n");
                pIns->fIommuUsed = true;
                rc = VINF_SUCCESS;
            }
            else
            {
                vbpci_printk(KERN_DEBUG, pPciDev, "failed to attach to IOMMU, error %d\n", rcLnx);
                rc = VERR_INTERNAL_ERROR;
            }
        }
        else
        {
           vbpci_printk(KERN_DEBUG, pIns->pPciDev, "cannot attach to IOMMU, no domain\n");
            rc = VERR_NOT_FOUND;
        }
    }
    else
    {
        vbpci_printk(KERN_DEBUG, pPciDev, "cannot attach to IOMMU, no VM data\n");
        rc = VERR_INVALID_PARAMETER;
    }

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
#else
    return VERR_NOT_SUPPORTED;
#endif
}

static int vboxPciLinuxDevUnregisterWithIommu(PVBOXRAWPCIINS pIns)
{
#ifdef VBOX_WITH_IOMMU
    int rc = VINF_SUCCESS;
    struct pci_dev *pPciDev = pIns->pPciDev;
    PVBOXRAWPCIDRVVM pData = VBOX_DRV_VMDATA(pIns);
    IPRT_LINUX_SAVE_EFL_AC();

    if (RT_LIKELY(pData))
    {
        if (RT_LIKELY(pData->pIommuDomain))
        {
            if (pIns->fIommuUsed)
            {
                iommu_detach_device(pData->pIommuDomain, &pIns->pPciDev->dev);
                vbpci_printk(KERN_DEBUG, pPciDev, "detached from IOMMU\n");
                pIns->fIommuUsed = false;
            }
        }
        else
        {
            vbpci_printk(KERN_DEBUG, pPciDev,
                         "cannot detach from IOMMU, no domain\n");
            rc = VERR_NOT_FOUND;
        }
    }
    else
    {
        vbpci_printk(KERN_DEBUG, pPciDev,
                     "cannot detach from IOMMU, no VM data\n");
        rc = VERR_INVALID_PARAMETER;
    }

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
#else
    return VERR_NOT_SUPPORTED;
#endif
}

static int vboxPciLinuxDevReset(PVBOXRAWPCIINS pIns)
{
    int rc = VINF_SUCCESS;
    IPRT_LINUX_SAVE_EFL_AC();

    if (RT_LIKELY(pIns->pPciDev))
    {
#if RTLNX_VER_MIN(2,6,28)
        if (pci_reset_function(pIns->pPciDev))
        {
            vbpci_printk(KERN_DEBUG, pIns->pPciDev,
                         "pci_reset_function() failed\n");
            rc = VERR_INTERNAL_ERROR;
        }
#else
        rc = VERR_NOT_SUPPORTED;
#endif
    }
    else
        rc = VERR_INVALID_PARAMETER;

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}

static struct file* vboxPciFileOpen(const char* path, int flags)
{
    struct file* filp = NULL;
    int err = 0;

    filp = filp_open(path, flags, 0);

    if (IS_ERR(filp))
    {
        err = PTR_ERR(filp);
        printk(KERN_DEBUG "vboxPciFileOpen: error %d\n", err);
        return NULL;
    }

    if (!filp->f_op || !filp->f_op->write)
    {
        printk(KERN_DEBUG "Not writable FS\n");
        filp_close(filp, NULL);
        return NULL;
    }

    return filp;
}

static void  vboxPciFileClose(struct file* file)
{
    filp_close(file, NULL);
}

static int vboxPciFileWrite(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size)
{
    int          ret;
    mm_segment_t fs_save;

    fs_save = get_fs();
    set_fs(KERNEL_DS);
#if RTLNX_VER_MIN(4,14,0)
    ret = kernel_write(file, data, size, &offset);
#else
    ret = vfs_write(file, data, size, &offset);
#endif
    set_fs(fs_save);
    if (ret < 0)
        printk(KERN_DEBUG "vboxPciFileWrite: error %d\n", ret);

    return ret;
}

static int vboxPciLinuxDevDetachHostDriver(PVBOXRAWPCIINS pIns)
{
    struct pci_dev *pPciDev = NULL;
    uint8_t uBus =   (pIns->HostPciAddress) >> 8;
    uint8_t uDevFn = (pIns->HostPciAddress) & 0xff;
    const char* currentDriver;
    uint16_t uVendor, uDevice;
    bool fDetach = 0;

    if (!g_VBoxPciGlobals.fPciStubModuleAvail)
    {
        printk(KERN_INFO "vboxpci: stub module %s not detected: cannot detach\n",
               PCI_STUB_MODULE);
        return VERR_ACCESS_DENIED;
    }

    pPciDev = PCI_DEV_GET_SLOT(uBus, uDevFn);

    if (!pPciDev)
    {
        printk(KERN_INFO "vboxpci: device at %02x:%02x.%d not found\n",
               uBus, uDevFn>>3, uDevFn&7);
        return VERR_NOT_FOUND;
    }

    uVendor = pPciDev->vendor;
    uDevice = pPciDev->device;

    currentDriver = pPciDev->driver ? pPciDev->driver->name : NULL;

    printk(KERN_DEBUG "vboxpci: detected device: %04x:%04x at %02x:%02x.%d, driver %s\n",
           uVendor, uDevice, uBus, uDevFn>>3, uDevFn&7,
           currentDriver ? currentDriver : "<none>");

    fDetach = (currentDriver == NULL || (strcmp(currentDriver, PCI_STUB_MODULE) != 0));

    /* Init previous driver data. */
    pIns->szPrevDriver[0] = '\0';

    if (fDetach && currentDriver)
    {
        /* Dangerous: if device name for some reasons contains slashes - arbitrary file could be written to. */
        if (strchr(currentDriver, '/') != 0)
        {
            printk(KERN_DEBUG "vboxpci: ERROR: %s contains invalid symbols\n", currentDriver);
            return VERR_ACCESS_DENIED;
        }
        /** @todo RTStrCopy not exported. */
        strncpy(pIns->szPrevDriver, currentDriver, sizeof(pIns->szPrevDriver) - 1);
        pIns->szPrevDriver[sizeof(pIns->szPrevDriver) - 1] = '\0';
    }

    PCI_DEV_PUT(pPciDev);
    pPciDev = NULL;

    if (fDetach)
    {
        char*              szCmdBuf;
        char*              szFileBuf;
        struct file*       pFile;
        int                iCmdLen;
        const int          cMaxBuf = 128;
#if RTLNX_VER_MIN(2,6,29)
        const struct cred *pOldCreds;
        struct cred       *pNewCreds;
#endif

        /*
         * Now perform kernel analog of:
         *
         * echo -n "10de 040a" > /sys/bus/pci/drivers/pci-stub/new_id
         * echo -n 0000:03:00.0 > /sys/bus/pci/drivers/nvidia/unbind
         * echo -n 0000:03:00.0 > /sys/bus/pci/drivers/pci-stub/bind
         *
         * We do this way, as this interface is presumingly more stable than
         * in-kernel ones.
         */
        szCmdBuf  = kmalloc(cMaxBuf, GFP_KERNEL);
        szFileBuf = kmalloc(cMaxBuf, GFP_KERNEL);
        if (!szCmdBuf || !szFileBuf)
            goto done;

        /* Somewhat ugly hack - override current credentials */
#if RTLNX_VER_MIN(2,6,29)
        pNewCreds = prepare_creds();
        if (!pNewCreds)
                goto done;

# if RTLNX_VER_MIN(3,5,0)
        pNewCreds->fsuid = GLOBAL_ROOT_UID;
# else
        pNewCreds->fsuid = 0;
# endif
        pOldCreds = override_creds(pNewCreds);
#endif

        RTStrPrintf(szFileBuf, cMaxBuf,
                              "/sys/bus/pci/drivers/%s/new_id",
                              PCI_STUB_MODULE);
        pFile = vboxPciFileOpen(szFileBuf, O_WRONLY);
        if (pFile)
        {
            iCmdLen = RTStrPrintf(szCmdBuf, cMaxBuf,
                                  "%04x %04x",
                                  uVendor, uDevice);
            /* Don't write trailing \0 */
            vboxPciFileWrite(pFile, 0, szCmdBuf, iCmdLen);
            vboxPciFileClose(pFile);
        }
        else
            printk(KERN_DEBUG "vboxpci: cannot open %s\n", szFileBuf);

        iCmdLen = RTStrPrintf(szCmdBuf, cMaxBuf,
                              "0000:%02x:%02x.%d",
                              uBus, uDevFn>>3, uDevFn&7);

        /* Unbind if bound to smth */
        if (pIns->szPrevDriver[0])
        {
            RTStrPrintf(szFileBuf, cMaxBuf,
                        "/sys/bus/pci/drivers/%s/unbind",
                         pIns->szPrevDriver);
            pFile = vboxPciFileOpen(szFileBuf, O_WRONLY);
            if (pFile)
            {

                /* Don't write trailing \0 */
                vboxPciFileWrite(pFile, 0, szCmdBuf, iCmdLen);
                vboxPciFileClose(pFile);
            }
            else
                printk(KERN_DEBUG "vboxpci: cannot open %s\n", szFileBuf);
        }

        RTStrPrintf(szFileBuf, cMaxBuf,
                    "/sys/bus/pci/drivers/%s/bind",
                    PCI_STUB_MODULE);
        pFile = vboxPciFileOpen(szFileBuf, O_WRONLY);
        if (pFile)
        {
            /* Don't write trailing \0 */
            vboxPciFileWrite(pFile, 0, szCmdBuf, iCmdLen);
            vboxPciFileClose(pFile);
        }
        else
            printk(KERN_DEBUG "vboxpci: cannot open %s\n", szFileBuf);

#if RTLNX_VER_MIN(2,6,29)
        revert_creds(pOldCreds);
        put_cred(pNewCreds);
#endif

      done:
        kfree(szCmdBuf);
        kfree(szFileBuf);
    }

    return 0;
}

static int vboxPciLinuxDevReattachHostDriver(PVBOXRAWPCIINS pIns)
{
    struct pci_dev *pPciDev = pIns->pPciDev;

    if (!pPciDev)
        return VINF_SUCCESS;

    if (pIns->szPrevDriver[0])
    {
        char*              szCmdBuf;
        char*              szFileBuf;
        struct file*       pFile;
        int                iCmdLen;
        const int          cMaxBuf = 128;
#if RTLNX_VER_MIN(2,6,29)
        const struct cred *pOldCreds;
        struct cred       *pNewCreds;
#endif
        uint8_t            uBus =   (pIns->HostPciAddress) >> 8;
        uint8_t            uDevFn = (pIns->HostPciAddress) & 0xff;

        vbpci_printk(KERN_DEBUG, pPciDev,
                     "reattaching old host driver %s\n", pIns->szPrevDriver);
        /*
         * Now perform kernel analog of:
         *
         * echo -n 0000:03:00.0 > /sys/bus/pci/drivers/pci-stub/unbind
         * echo -n 0000:03:00.0 > /sys/bus/pci/drivers/nvidia/bind
         */
        szCmdBuf  = kmalloc(cMaxBuf, GFP_KERNEL);
        szFileBuf = kmalloc(cMaxBuf, GFP_KERNEL);

        if (!szCmdBuf || !szFileBuf)
            goto done;

        iCmdLen = RTStrPrintf(szCmdBuf, cMaxBuf,
                              "0000:%02x:%02x.%d",
                              uBus, uDevFn>>3, uDevFn&7);

        /* Somewhat ugly hack - override current credentials */
#if RTLNX_VER_MIN(2,6,29)
        pNewCreds = prepare_creds();
        if (!pNewCreds)
            goto done;

# if RTLNX_VER_MIN(3,5,0)
        pNewCreds->fsuid = GLOBAL_ROOT_UID;
# else
        pNewCreds->fsuid = 0;
# endif
        pOldCreds = override_creds(pNewCreds);
#endif
        RTStrPrintf(szFileBuf, cMaxBuf,
                    "/sys/bus/pci/drivers/%s/unbind",
                    PCI_STUB_MODULE);
        pFile = vboxPciFileOpen(szFileBuf, O_WRONLY);
        if (pFile)
        {

            /* Don't write trailing \0 */
            vboxPciFileWrite(pFile, 0, szCmdBuf, iCmdLen);
            vboxPciFileClose(pFile);
        }
        else
            printk(KERN_DEBUG "vboxpci: cannot open %s\n", szFileBuf);

        RTStrPrintf(szFileBuf, cMaxBuf,
                    "/sys/bus/pci/drivers/%s/bind",
                    pIns->szPrevDriver);
        pFile = vboxPciFileOpen(szFileBuf, O_WRONLY);
        if (pFile)
        {

            /* Don't write trailing \0 */
            vboxPciFileWrite(pFile, 0, szCmdBuf, iCmdLen);
            vboxPciFileClose(pFile);
            pIns->szPrevDriver[0] = '\0';
        }
        else
            printk(KERN_DEBUG "vboxpci: cannot open %s\n", szFileBuf);

#if RTLNX_VER_MIN(2,6,29)
        revert_creds(pOldCreds);
        put_cred(pNewCreds);
#endif

      done:
        kfree(szCmdBuf);
        kfree(szFileBuf);
    }

    return VINF_SUCCESS;
}

DECLHIDDEN(int) vboxPciOsDevInit(PVBOXRAWPCIINS pIns, uint32_t fFlags)
{
    struct pci_dev *pPciDev = NULL;
    int rc = VINF_SUCCESS;
    IPRT_LINUX_SAVE_EFL_AC();

    if (fFlags & PCIRAWDRIVERRFLAG_DETACH_HOST_DRIVER)
    {
        rc = vboxPciLinuxDevDetachHostDriver(pIns);
        if (RT_FAILURE(rc))
        {
            printk(KERN_DEBUG "Cannot detach host driver for device %x: %d\n",
                   pIns->HostPciAddress, rc);
        }
    }

    if (RT_SUCCESS(rc))
    {
        pPciDev = PCI_DEV_GET_SLOT((pIns->HostPciAddress) >> 8,
                                   (pIns->HostPciAddress) & 0xff);

        if (RT_LIKELY(pPciDev))
        {
            int rcLnx = pci_enable_device(pPciDev);

            if (!rcLnx)
            {
                pIns->pPciDev = pPciDev;
                vbpci_printk(KERN_DEBUG, pPciDev, "%s\n", __func__);

#if RTLNX_VER_MIN(2,6,1)
                if (pci_enable_msi(pPciDev) == 0)
                    pIns->fMsiUsed = true;
#endif

                /** @todo
                 * pci_enable_msix(pPciDev, entries, nvec)
                 *
                 * In fact, if device uses interrupts, and cannot be forced to use MSI or MSI-X
                 * we have to refuse using it, as we cannot work with shared PCI interrupts (unless we're lucky
                 * to grab unshared PCI interrupt).
                 */
            }
            else
                rc = RTErrConvertFromErrno(RT_ABS(rcLnx));
        }
        else
            rc = VERR_NOT_FOUND;
    }

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}

DECLHIDDEN(int) vboxPciOsDevDeinit(PVBOXRAWPCIINS pIns, uint32_t fFlags)
{
    int rc = VINF_SUCCESS;
    struct pci_dev *pPciDev = pIns->pPciDev;
    IPRT_LINUX_SAVE_EFL_AC();

    vbpci_printk(KERN_DEBUG, pPciDev, "%s\n", __func__);

    if (RT_LIKELY(pPciDev))
    {
        int iRegion;
        for (iRegion = 0; iRegion < 7; ++iRegion)
        {
            if (pIns->aRegionR0Mapping[iRegion])
            {
                iounmap(pIns->aRegionR0Mapping[iRegion]);
                pIns->aRegionR0Mapping[iRegion] = 0;
                pci_release_region(pPciDev, iRegion);
            }
        }

        vboxPciLinuxDevUnregisterWithIommu(pIns);

#if RTLNX_VER_MIN(2,6,1)
        if (pIns->fMsiUsed)
            pci_disable_msi(pPciDev);
#endif
        // pci_disable_msix(pPciDev);
        pci_disable_device(pPciDev);
        vboxPciLinuxDevReattachHostDriver(pIns);

        PCI_DEV_PUT(pPciDev);
        pIns->pPciDev = NULL;
    }
    else
        rc = VERR_INVALID_PARAMETER;

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}

DECLHIDDEN(int) vboxPciOsDevDestroy(PVBOXRAWPCIINS pIns)
{
    return VINF_SUCCESS;
}

DECLHIDDEN(int) vboxPciOsDevGetRegionInfo(PVBOXRAWPCIINS pIns,
                                          int32_t        iRegion,
                                          RTHCPHYS       *pRegionStart,
                                          uint64_t       *pu64RegionSize,
                                          bool           *pfPresent,
                                          uint32_t       *pfFlags)
{
    int rc = VINF_SUCCESS;
    struct pci_dev *pPciDev = pIns->pPciDev;
    IPRT_LINUX_SAVE_EFL_AC();

    if (RT_LIKELY(pPciDev))
    {
        int fFlags = pci_resource_flags(pPciDev, iRegion);

        if (   ((fFlags & (IORESOURCE_MEM | IORESOURCE_IO)) == 0)
            || ((fFlags & IORESOURCE_DISABLED) != 0))
        {
            *pfPresent = false;
            rc = VERR_INVALID_PARAMETER;
        }
        else
        {
            uint32_t fResFlags = 0;
            *pfPresent = true;

            if (fFlags & IORESOURCE_MEM)
                fResFlags |= PCIRAW_ADDRESS_SPACE_MEM;

            if (fFlags & IORESOURCE_IO)
                fResFlags |= PCIRAW_ADDRESS_SPACE_IO;

#ifdef IORESOURCE_MEM_64
            if (fFlags & IORESOURCE_MEM_64)
                fResFlags |= PCIRAW_ADDRESS_SPACE_BAR64;
#endif

            if (fFlags & IORESOURCE_PREFETCH)
                fResFlags |=  PCIRAW_ADDRESS_SPACE_MEM_PREFETCH;

            *pfFlags        = fResFlags;
            *pRegionStart   = pci_resource_start(pPciDev, iRegion);
            *pu64RegionSize = pci_resource_len  (pPciDev, iRegion);

            vbpci_printk(KERN_DEBUG, pPciDev,
                         "region %d: %s %llx+%lld\n",
                         iRegion, (fFlags & IORESOURCE_MEM) ? "mmio" : "pio",
                         *pRegionStart, *pu64RegionSize);
        }
    }
    else
    {
        *pfPresent = false;
        rc = VERR_INVALID_PARAMETER;
    }

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}

DECLHIDDEN(int) vboxPciOsDevMapRegion(PVBOXRAWPCIINS pIns,
                                      int32_t        iRegion,
                                      RTHCPHYS       RegionStart,
                                      uint64_t       u64RegionSize,
                                      uint32_t       fFlags,
                                      RTR0PTR        *pRegionBase)
{
    int rc = VINF_SUCCESS;
    struct pci_dev  *pPciDev = pIns->pPciDev;
    IPRT_LINUX_SAVE_EFL_AC();

    if (!pPciDev || iRegion < 0 || iRegion > 0)
    {
        if (pPciDev)
            vbpci_printk(KERN_DEBUG, pPciDev, "invalid region %d\n", iRegion);

        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_INVALID_PARAMETER;
    }

    vbpci_printk(KERN_DEBUG, pPciDev, "reg=%d start=%llx size=%lld\n",
                 iRegion, RegionStart, u64RegionSize);

    if (   (pci_resource_flags(pPciDev, iRegion) & IORESOURCE_IO)
        || RegionStart != pci_resource_start(pPciDev, iRegion)
        || u64RegionSize != pci_resource_len(pPciDev, iRegion))
    {
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_INVALID_PARAMETER;
    }

    /*
     * XXX: Current code never calls unmap.  To avoid leaking mappings
     * only request and map resources once.
     */
    if (!pIns->aRegionR0Mapping[iRegion])
    {
        int     rcLnx;
        *pRegionBase = pIns->aRegionR0Mapping[iRegion];

        rcLnx = pci_request_region(pPciDev, iRegion, "vboxpci");
        if (!rcLnx)
        {
#if RTLNX_VER_MIN(2,6,25)
            /*
             * ioremap() defaults to no caching since the 2.6 kernels.
             * ioremap_nocache() has been removed finally in 5.6-rc1.
             */
            RTR0PTR R0PtrMapping = ioremap(pci_resource_start(pPciDev, iRegion),
                                           pci_resource_len(pPciDev, iRegion));
#else /* KERNEL_VERSION < 2.6.25 */
            /* For now no caching, try to optimize later. */
            RTR0PTR R0PtrMapping = ioremap_nocache(pci_resource_start(pPciDev, iRegion),
                                                   pci_resource_len(pPciDev, iRegion));
#endif /* KERNEL_VERSION < 2.6.25 */
            if (R0PtrMapping != NIL_RTR0PTR)
                pIns->aRegionR0Mapping[iRegion] = R0PtrMapping;
            else
            {
#if RTLNX_VER_MIN(2,6,25)
                vbpci_printk(KERN_DEBUG, pPciDev, "ioremap() failed\n");
#else
                vbpci_printk(KERN_DEBUG, pPciDev, "ioremap_nocache() failed\n");
#endif
                pci_release_region(pPciDev, iRegion);
                rc = VERR_MAP_FAILED;
            }
        }
        else
            rc = VERR_RESOURCE_BUSY;
    }

    if (RT_SUCCESS(rc))
        *pRegionBase = pIns->aRegionR0Mapping[iRegion];

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}

DECLHIDDEN(int) vboxPciOsDevUnmapRegion(PVBOXRAWPCIINS pIns,
                                        int32_t        iRegion,
                                        RTHCPHYS       RegionStart,
                                        uint64_t       u64RegionSize,
                                        RTR0PTR        RegionBase)
{
    /* XXX: Current code never calls unmap. */
    return VERR_NOT_IMPLEMENTED;
}

DECLHIDDEN(int) vboxPciOsDevPciCfgWrite(PVBOXRAWPCIINS pIns, uint32_t Register, PCIRAWMEMLOC *pValue)
{
    struct pci_dev *pPciDev = pIns->pPciDev;
    int rc = VINF_SUCCESS;
    IPRT_LINUX_SAVE_EFL_AC();

    if (RT_LIKELY(pPciDev))
    {
        switch (pValue->cb)
        {
            case 1:
                pci_write_config_byte(pPciDev,  Register, pValue->u.u8);
                break;
            case 2:
                pci_write_config_word(pPciDev,  Register, pValue->u.u16);
                break;
            case 4:
                pci_write_config_dword(pPciDev, Register, pValue->u.u32);
                break;
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}

DECLHIDDEN(int) vboxPciOsDevPciCfgRead(PVBOXRAWPCIINS pIns, uint32_t Register, PCIRAWMEMLOC *pValue)
{
    struct pci_dev *pPciDev = pIns->pPciDev;
    int rc = VINF_SUCCESS;

    if (RT_LIKELY(pPciDev))
    {
        IPRT_LINUX_SAVE_EFL_AC();

        switch (pValue->cb)
        {
            case 1:
                pci_read_config_byte(pPciDev, Register, &pValue->u.u8);
                break;
            case 2:
                pci_read_config_word(pPciDev, Register, &pValue->u.u16);
                break;
            case 4:
                pci_read_config_dword(pPciDev, Register, &pValue->u.u32);
                break;
        }

        IPRT_LINUX_RESTORE_EFL_AC();
    }
    else
        rc = VERR_INVALID_PARAMETER;

    return rc;
}

/**
 * Interrupt service routine.
 *
 * @returns In 2.6 we indicate whether we've handled the IRQ or not.
 *
 * @param   iIrq            The IRQ number.
 * @param   pvDevId         The device ID, a pointer to PVBOXRAWPCIINS.
 * @param   pRegs           Register set. Removed in 2.6.19.
 */
#if RTLNX_VER_MIN(2,6,19) && !defined(DOXYGEN_RUNNING)
static irqreturn_t vboxPciOsIrqHandler(int iIrq, void *pvDevId)
#else
static irqreturn_t vboxPciOsIrqHandler(int iIrq, void *pvDevId, struct pt_regs *pRegs)
#endif
{
    PVBOXRAWPCIINS pIns = (PVBOXRAWPCIINS)pvDevId;
    bool fTaken = true;

    if (pIns && pIns->IrqHandler.pfnIrqHandler)
        fTaken = pIns->IrqHandler.pfnIrqHandler(pIns->IrqHandler.pIrqContext, iIrq);
#ifndef VBOX_WITH_SHARED_PCI_INTERRUPTS
    /* If we don't allow interrupts sharing, we consider all interrupts as non-shared, thus targetted to us. */
    fTaken = true;
#endif

    return fTaken;
}

DECLHIDDEN(int) vboxPciOsDevRegisterIrqHandler(PVBOXRAWPCIINS pIns, PFNRAWPCIISR pfnHandler, void* pIrqContext, int32_t *piHostIrq)
{
    int rc;
    int32_t iIrq = pIns->pPciDev->irq;
    IPRT_LINUX_SAVE_EFL_AC();

    if (iIrq == 0)
    {
        vbpci_printk(KERN_NOTICE, pIns->pPciDev, "no irq assigned\n");
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_INVALID_PARAMETER;
    }

    rc = request_irq(iIrq,
                     vboxPciOsIrqHandler,
#ifdef VBOX_WITH_SHARED_PCI_INTERRUPTS
                     /* Allow interrupts sharing. */
# if RTLNX_VER_MIN(2,6,20)
                     IRQF_SHARED,
# else
                     SA_SHIRQ,
# endif

#else

                     /* We don't allow interrupts sharing */
                     /* XXX overhaul */
# if RTLNX_VER_MIN(2,6,20) && RTLNX_VER_MAX(4,1,0)
                     IRQF_DISABLED, /* keep irqs disabled when calling the action handler */
# else
                     0,
# endif
#endif
                     DRIVER_NAME,
                     pIns);
    if (rc)
    {
        vbpci_printk(KERN_DEBUG, pIns->pPciDev,
                     "could not request irq %d, error %d\n", iIrq, rc);
        IPRT_LINUX_RESTORE_EFL_AC();
        return VERR_RESOURCE_BUSY;
    }

    vbpci_printk(KERN_DEBUG, pIns->pPciDev, "got irq %d\n", iIrq);
    *piHostIrq = iIrq;

    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}

DECLHIDDEN(int) vboxPciOsDevUnregisterIrqHandler(PVBOXRAWPCIINS pIns, int32_t iHostIrq)
{
    IPRT_LINUX_SAVE_EFL_AC();

    vbpci_printk(KERN_DEBUG, pIns->pPciDev, "freeing irq %d\n", iHostIrq);
    free_irq(iHostIrq, pIns);

    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}

DECLHIDDEN(int) vboxPciOsDevPowerStateChange(PVBOXRAWPCIINS pIns, PCIRAWPOWERSTATE  aState)
{
    int rc;

    switch (aState)
    {
        case PCIRAW_POWER_ON:
            vbpci_printk(KERN_DEBUG, pIns->pPciDev, "PCIRAW_POWER_ON\n");
            /* Reset device, just in case. */
            vboxPciLinuxDevReset(pIns);
            /* register us with IOMMU */
            rc = vboxPciLinuxDevRegisterWithIommu(pIns);
            break;
        case PCIRAW_POWER_RESET:
            vbpci_printk(KERN_DEBUG, pIns->pPciDev, "PCIRAW_POWER_RESET\n");
            rc = vboxPciLinuxDevReset(pIns);
            break;
        case PCIRAW_POWER_OFF:
            vbpci_printk(KERN_DEBUG, pIns->pPciDev, "PCIRAW_POWER_OFF\n");
            /* unregister us from IOMMU */
            rc = vboxPciLinuxDevUnregisterWithIommu(pIns);
            break;
        case PCIRAW_POWER_SUSPEND:
            vbpci_printk(KERN_DEBUG, pIns->pPciDev, "PCIRAW_POWER_SUSPEND\n");
            rc = VINF_SUCCESS;
            /// @todo what do we do here?
            break;
        case PCIRAW_POWER_RESUME:
            vbpci_printk(KERN_DEBUG, pIns->pPciDev, "PCIRAW_POWER_RESUME\n");
            rc = VINF_SUCCESS;
            /// @todo what do we do here?
            break;
        default:
            vbpci_printk(KERN_DEBUG, pIns->pPciDev, "unknown power state %u\n", aState);
            /* to make compiler happy */
            rc = VERR_NOT_SUPPORTED;
            break;
    }

    return rc;
}


#ifdef VBOX_WITH_IOMMU
/** Callback for FNRAWPCICONTIGPHYSMEMINFO. */
static DECLCALLBACK(int) vboxPciOsContigMemInfo(PRAWPCIPERVM pVmCtx, RTHCPHYS HostStart, RTGCPHYS GuestStart,
                                                uint64_t cMemSize, PCIRAWMEMINFOACTION Action)
{
    struct iommu_domain* domain = ((PVBOXRAWPCIDRVVM)(pVmCtx->pDriverData))->pIommuDomain;
    int rc = VINF_SUCCESS;
    IPRT_LINUX_SAVE_EFL_AC();

    switch (Action)
    {
        case PCIRAW_MEMINFO_MAP:
        {
            int flags, r;

            if (iommu_iova_to_phys(domain, GuestStart))
                break;

            flags = IOMMU_READ | IOMMU_WRITE;
            /** @todo flags |= IOMMU_CACHE; */

            r = iommu_map(domain, GuestStart, HostStart, get_order(cMemSize), flags);
            if (r)
            {
                printk(KERN_ERR "vboxPciOsContigMemInfo:"
                       "iommu failed to map pfn=%llx\n", HostStart);
                rc = VERR_GENERAL_FAILURE;
                break;
            }
            rc =  VINF_SUCCESS;
            break;
        }
        case PCIRAW_MEMINFO_UNMAP:
        {
            int order;
            order = iommu_unmap(domain, GuestStart, get_order(cMemSize));
            NOREF(order);
            break;
        }

        default:
            printk(KERN_DEBUG "Unsupported action: %d\n", (int)Action);
            rc = VERR_NOT_SUPPORTED;
            break;
    }

    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}
#endif

DECLHIDDEN(int) vboxPciOsInitVm(PVBOXRAWPCIDRVVM pThis, PVM pVM, PRAWPCIPERVM pVmData)
{
    int rc = VINF_SUCCESS;

#ifdef VBOX_WITH_IOMMU
    IPRT_LINUX_SAVE_EFL_AC();

    if (IOMMU_PRESENT())
    {
        pThis->pIommuDomain = IOMMU_DOMAIN_ALLOC();
        if (!pThis->pIommuDomain)
        {
            vbpci_printk(KERN_DEBUG, NULL, "cannot allocate IOMMU domain\n");
            rc = VERR_NO_MEMORY;
        }
        else
        {
            pVmData->pfnContigMemInfo = vboxPciOsContigMemInfo;

            vbpci_printk(KERN_DEBUG, NULL, "created IOMMU domain %p\n",
                         pThis->pIommuDomain);
        }
    }

    IPRT_LINUX_RESTORE_EFL_AC();
#endif
    return rc;
}

DECLHIDDEN(void) vboxPciOsDeinitVm(PVBOXRAWPCIDRVVM pThis, PVM pVM)
{
#ifdef VBOX_WITH_IOMMU
    IPRT_LINUX_SAVE_EFL_AC();

    if (pThis->pIommuDomain)
    {
        vbpci_printk(KERN_DEBUG, NULL, "freeing IOMMU domain %p\n",
                     pThis->pIommuDomain);
        iommu_domain_free(pThis->pIommuDomain);
        pThis->pIommuDomain = NULL;
    }

    IPRT_LINUX_RESTORE_EFL_AC();
#endif
}
