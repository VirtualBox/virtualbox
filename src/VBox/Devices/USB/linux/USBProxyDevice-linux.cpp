/* $Id: USBProxyDevice-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USB device proxy - the Linux backend.
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_USBPROXY

#include <iprt/stdint.h>
#include <iprt/err.h>
#include <iprt/pipe.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#ifdef VBOX_WITH_LINUX_COMPILER_H
# include <linux/compiler.h>
#endif
#include <linux/usbdevice_fs.h>

#ifndef RDESKTOP
# include <VBox/vmm/pdm.h>
#else
# define RTCRITSECT          void *
static inline int rtcsNoop() { return VINF_SUCCESS; }
static inline bool rtcsTrue() { return true; }
# define RTCritSectInit(a)   rtcsNoop()
# define RTCritSectDelete(a) rtcsNoop()
# define RTCritSectEnter(a)  rtcsNoop()
# define RTCritSectLeave(a)  rtcsNoop()
# define RTCritSectIsOwner(a) rtcsTrue()
#endif
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/linux/sysfs.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/list.h>
#include <iprt/time.h>
#include "../USBProxyDevice.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Wrapper around the linux urb request structure.
 * This is required to track in-flight and landed URBs.
 */
typedef struct USBPROXYURBLNX
{
    /** Node to link the URB in of the existing lists. */
    RTLISTNODE                      NodeList;
    /** If we've split the VUSBURB up into multiple linux URBs, this is points to the head. */
    struct USBPROXYURBLNX          *pSplitHead;
    /** The next linux URB if split up. */
    struct USBPROXYURBLNX          *pSplitNext;
    /** Don't report these back. */
    bool                            fCanceledBySubmit;
    /** This split element is reaped. */
    bool                            fSplitElementReaped;
    /** This URB was discarded. */
    bool                            fDiscarded;
    /** Size to transfer in remaining fragments of a split URB */
    uint32_t                        cbSplitRemaining;

#if RT_GNUC_PREREQ(6, 0) /* gcc 6.2 complains about the [] member of KUrb */
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wpedantic"
#endif
    /** The kernel URB data (variable size array included). */
    struct usbdevfs_urb             KUrb;
#if RT_GNUC_PREREQ(6, 0)
# pragma GCC diagnostic pop
#endif
} USBPROXYURBLNX, *PUSBPROXYURBLNX;

/**
 * Data for the linux usb proxy backend.
 */
typedef struct USBPROXYDEVLNX
{
    /** The open file. */
    RTFILE              hFile;
    /** Critical section protecting the lists. */
    RTCRITSECT          CritSect;
    /** The list of free linux URBs (USBPROXYURBLNX). */
    RTLISTANCHOR        ListFree;
    /** The list of active linux URBs.
     * We must maintain this so we can properly reap URBs of a detached device.
     * Only the split head will appear in this list. (USBPROXYURBLNX) */
    RTLISTANCHOR        ListInFlight;
    /** Are we using sysfs to find the active configuration? */
    bool                fUsingSysfs;
    /** Pipe handle for waking up - writing end. */
    RTPIPE              hPipeWakeupW;
    /** Pipe handle for waking up - reading end. */
    RTPIPE              hPipeWakeupR;
    /** The device node/sysfs path of the device.
     * Used to figure out the configuration after a reset. */
    char                *pszPath;
    /** Mask of claimed interfaces. */
    uint32_t            fClaimedIfsMask;
} USBPROXYDEVLNX, *PUSBPROXYDEVLNX;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void usbProxLinuxUrbUnplugged(PUSBPROXYDEV pProxyDev);
static DECLCALLBACK(int) usbProxyLinuxClaimInterface(PUSBPROXYDEV pProxyDev, int iIf);
static DECLCALLBACK(int) usbProxyLinuxReleaseInterface(PUSBPROXYDEV pProxyDev, int iIf);


/**
 * Wrapper for the ioctl call.
 *
 * This wrapper will repeat the call if we get an EINTR or EAGAIN. It can also
 * handle ENODEV (detached device) errors.
 *
 * @returns whatever ioctl returns.
 * @param   pProxyDev       The proxy device.
 * @param   iCmd            The ioctl command / function.
 * @param   pvArg           The ioctl argument / data.
 * @param   fHandleNoDev    Whether to handle ENODEV.
 * @param   cTries          The number of retries. Use UINT32_MAX for (kind of) indefinite retries.
 * @internal
 */
static int usbProxyLinuxDoIoCtl(PUSBPROXYDEV pProxyDev, unsigned long iCmd, void *pvArg, bool fHandleNoDev, uint32_t cTries)
{
    int rc;
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);
    do
    {
        do
        {
            rc = ioctl(RTFileToNative(pDevLnx->hFile), iCmd, pvArg);
            if (rc >= 0)
                return rc;
        } while (errno == EINTR);

        if (errno == ENODEV && fHandleNoDev)
        {
            usbProxLinuxUrbUnplugged(pProxyDev);
            Log(("usb-linux: ENODEV -> unplugged. pProxyDev=%s\n", usbProxyGetName(pProxyDev)));
            errno = ENODEV;
            break;
        }
        if (errno != EAGAIN)
            break;
    } while (cTries-- > 0);

    return rc;
}


/**
 * The device has been unplugged.
 * Cancel all in-flight URBs and put them up for reaping.
 */
static void usbProxLinuxUrbUnplugged(PUSBPROXYDEV pProxyDev)
{
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);

    /*
     * Shoot down all flying URBs.
     */
    RTCritSectEnter(&pDevLnx->CritSect);
    pProxyDev->fDetached = true;

    PUSBPROXYURBLNX pUrbLnx;
    PUSBPROXYURBLNX pUrbLnxNext;
    RTListForEachSafe(&pDevLnx->ListInFlight, pUrbLnx, pUrbLnxNext, USBPROXYURBLNX, NodeList)
    {
        if (!pUrbLnx->fDiscarded)
        {
            pUrbLnx->fDiscarded = true;
            /* Cancel the URB. It will be reaped normally. */
            ioctl(RTFileToNative(pDevLnx->hFile), USBDEVFS_DISCARDURB, &pUrbLnx->KUrb);
            if (!pUrbLnx->KUrb.status)
                pUrbLnx->KUrb.status = -ENODEV;
        }
    }

    RTCritSectLeave(&pDevLnx->CritSect);
}


/**
 * Set the connect state seen by kernel drivers
 * @internal
 */
static void usbProxyLinuxSetConnected(PUSBPROXYDEV pProxyDev, int iIf, bool fConnect, bool fQuiet)
{
    if (    iIf >= 32
        ||  !(pProxyDev->fMaskedIfs & RT_BIT(iIf)))
    {
        struct usbdevfs_ioctl IoCtl;
        if (!fQuiet)
            LogFlow(("usbProxyLinuxSetConnected: pProxyDev=%s iIf=%#x fConnect=%s\n",
                     usbProxyGetName(pProxyDev), iIf, fConnect ? "true" : "false"));

        IoCtl.ifno = iIf;
        IoCtl.ioctl_code = fConnect ? USBDEVFS_CONNECT : USBDEVFS_DISCONNECT;
        IoCtl.data = NULL;
        if (    usbProxyLinuxDoIoCtl(pProxyDev, USBDEVFS_IOCTL, &IoCtl, true, UINT32_MAX)
            &&  !fQuiet)
            Log(("usbProxyLinuxSetConnected: failure, errno=%d. pProxyDev=%s\n",
                 errno, usbProxyGetName(pProxyDev)));
    }
}


/**
 * Links the given URB into the in flight list.
 *
 * @param   pDevLnx         The proxy device instance - Linux specific data.
 * @param   pUrbLnx         The URB to link into the in flight list.
 */
static void usbProxyLinuxUrbLinkInFlight(PUSBPROXYDEVLNX pDevLnx, PUSBPROXYURBLNX pUrbLnx)
{
    LogFlowFunc(("pDevLnx=%p pUrbLnx=%p\n", pDevLnx, pUrbLnx));
    Assert(RTCritSectIsOwner(&pDevLnx->CritSect));
    Assert(!pUrbLnx->pSplitHead || pUrbLnx->pSplitHead == pUrbLnx);
    RTListAppend(&pDevLnx->ListInFlight, &pUrbLnx->NodeList);
}


/**
 * Unlinks the given URB from the in flight list.
 *
 * @param   pDevLnx         The proxy device instance - Linux specific data.
 * @param   pUrbLnx         The URB to link into the in flight list.
 */
static void usbProxyLinuxUrbUnlinkInFlight(PUSBPROXYDEVLNX pDevLnx, PUSBPROXYURBLNX pUrbLnx)
{
    LogFlowFunc(("pDevLnx=%p pUrbLnx=%p\n", pDevLnx, pUrbLnx));
    RTCritSectEnter(&pDevLnx->CritSect);

    /*
     * Remove from the active list.
     */
    Assert(!pUrbLnx->pSplitHead || pUrbLnx->pSplitHead == pUrbLnx);

    RTListNodeRemove(&pUrbLnx->NodeList);

    RTCritSectLeave(&pDevLnx->CritSect);
}


/**
 * Allocates a linux URB request structure.
 *
 * @returns Pointer to an active URB request.
 * @returns NULL on failure.
 * @param   pProxyDev       The proxy device instance.
 * @param   pSplitHead      The split list head if allocating for a split list.
 */
static PUSBPROXYURBLNX usbProxyLinuxUrbAlloc(PUSBPROXYDEV pProxyDev, PUSBPROXYURBLNX pSplitHead)
{
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);
    PUSBPROXYURBLNX pUrbLnx;

    LogFlowFunc(("pProxyDev=%p pSplitHead=%p\n", pProxyDev, pSplitHead));

    RTCritSectEnter(&pDevLnx->CritSect);

    /*
     * Try remove a linux URB from the free list, if none there allocate a new one.
     */
    pUrbLnx = RTListGetFirst(&pDevLnx->ListFree, USBPROXYURBLNX, NodeList);
    if (pUrbLnx)
    {
        RTListNodeRemove(&pUrbLnx->NodeList);
        RTCritSectLeave(&pDevLnx->CritSect);
    }
    else
    {
        RTCritSectLeave(&pDevLnx->CritSect);
        PVUSBURB pVUrbDummy; RT_NOREF(pVUrbDummy);
        pUrbLnx = (PUSBPROXYURBLNX)RTMemAlloc(RT_UOFFSETOF_DYN(USBPROXYURBLNX,
                                                               KUrb.iso_frame_desc[RT_ELEMENTS(pVUrbDummy->aIsocPkts)]));
        if (!pUrbLnx)
            return NULL;
    }

    pUrbLnx->pSplitHead = pSplitHead;
    pUrbLnx->pSplitNext = NULL;
    pUrbLnx->fCanceledBySubmit = false;
    pUrbLnx->fSplitElementReaped = false;
    pUrbLnx->fDiscarded = false;
    LogFlowFunc(("returns pUrbLnx=%p\n", pUrbLnx));
    return pUrbLnx;
}


/**
 * Frees a linux URB request structure.
 *
 * @param   pProxyDev       The proxy device instance.
 * @param   pUrbLnx         The linux URB to free.
 */
static void usbProxyLinuxUrbFree(PUSBPROXYDEV pProxyDev, PUSBPROXYURBLNX pUrbLnx)
{
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);

    LogFlowFunc(("pProxyDev=%p pUrbLnx=%p\n", pProxyDev, pUrbLnx));

    /*
     * Link it into the free list.
     */
    RTCritSectEnter(&pDevLnx->CritSect);
    RTListAppend(&pDevLnx->ListFree, &pUrbLnx->NodeList);
    RTCritSectLeave(&pDevLnx->CritSect);
}


/**
 * Frees split list of a linux URB request structure.
 *
 * @param   pProxyDev       The proxy device instance.
 * @param   pUrbLnx         A linux URB to in the split list to be freed.
 */
static void usbProxyLinuxUrbFreeSplitList(PUSBPROXYDEV pProxyDev, PUSBPROXYURBLNX pUrbLnx)
{
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);

    LogFlowFunc(("pProxyDev=%p pUrbLnx=%p\n", pProxyDev, pUrbLnx));

    RTCritSectEnter(&pDevLnx->CritSect);

    pUrbLnx = pUrbLnx->pSplitHead;
    Assert(pUrbLnx);
    while (pUrbLnx)
    {
        PUSBPROXYURBLNX pFree = pUrbLnx;
        pUrbLnx = pUrbLnx->pSplitNext;
        Assert(pFree->pSplitHead);
        pFree->pSplitHead = pFree->pSplitNext = NULL;
        usbProxyLinuxUrbFree(pProxyDev, pFree);
    }

    RTCritSectLeave(&pDevLnx->CritSect);
}


/**
 * This finds the device in the /proc/bus/usb/bus/addr file and finds
 * the config with an asterix.
 *
 * @returns The Cfg#.
 * @returns -1 if no active config.
 * @param   pProxyDev       The proxy device instance.
 * @param   pszDevNode      The path to the device. We infere the location of
 *                          the devices file, which bus and device number we're
 *                          looking for.
 * @param   piFirstCfg      The first configuration. (optional)
 * @internal
 */
static int usbProxyLinuxFindActiveConfigUsbfs(PUSBPROXYDEV pProxyDev, const char *pszDevNode, int *piFirstCfg)
{
    RT_NOREF(pProxyDev);

    /*
     * Set return defaults.
     */
    int iActiveCfg = -1;
    if (piFirstCfg)
        *piFirstCfg = 1;

    /*
     * Parse the usbfs device node path and turn it into a path to the "devices" file,
     * picking up the device number and bus along the way.
     */
    size_t cchDevNode = strlen(pszDevNode);
    char *pszDevices = (char *)RTMemDupEx(pszDevNode, cchDevNode, sizeof("devices"));
    AssertReturn(pszDevices, iActiveCfg);

    /* the device number */
    char *psz = pszDevices + cchDevNode;
    while (*psz != '/')
        psz--;
    Assert(pszDevices < psz);
    uint32_t uDev;
    int rc = RTStrToUInt32Ex(psz + 1, NULL, 10, &uDev);
    if (RT_SUCCESS(rc))
    {
        /* the bus number */
        *psz-- = '\0';
        while (*psz != '/')
            psz--;
        Assert(pszDevices < psz);
        uint32_t uBus;
        rc = RTStrToUInt32Ex(psz + 1, NULL, 10, &uBus);
        if (RT_SUCCESS(rc))
        {
            strcpy(psz + 1, "devices");

            /*
             * Open and scan the devices file.
             * We're ASSUMING that each device starts off with a 'T:' line.
             */
            PRTSTREAM pFile;
            rc = RTStrmOpen(pszDevices, "r", &pFile);
            if (RT_SUCCESS(rc))
            {
                char szLine[1024];
                while (RT_SUCCESS(RTStrmGetLine(pFile, szLine, sizeof(szLine))))
                {
                    /* we're only interested in 'T:' lines. */
                    psz = RTStrStripL(szLine);
                    if (psz[0] != 'T' || psz[1] != ':')
                        continue;

                    /* Skip ahead to 'Bus' and compare */
                    psz = RTStrStripL(psz + 2); Assert(!strncmp(psz, RT_STR_TUPLE("Bus=")));
                    psz = RTStrStripL(psz + 4);
                    char *pszNext;
                    uint32_t u;
                    rc = RTStrToUInt32Ex(psz, &pszNext, 10, &u); AssertRC(rc);
                    if (RT_FAILURE(rc))
                        continue;
                    if (u != uBus)
                        continue;

                    /* Skip ahead to 'Dev#' and compare */
                    psz = strstr(psz, "Dev#="); Assert(psz);
                    if (!psz)
                        continue;
                    psz = RTStrStripL(psz + 5);
                    rc = RTStrToUInt32Ex(psz, &pszNext, 10, &u); AssertRC(rc);
                    if (RT_FAILURE(rc))
                        continue;
                    if (u != uDev)
                        continue;

                    /*
                     * Ok, we've found the device.
                     * Scan until we find a selected configuration, the next device, or EOF.
                     */
                    while (RT_SUCCESS(RTStrmGetLine(pFile, szLine, sizeof(szLine))))
                    {
                        psz = RTStrStripL(szLine);
                        if (psz[0] == 'T')
                            break;
                        if (psz[0] != 'C' || psz[1] != ':')
                            continue;
                        const bool fActive = psz[2] == '*';
                        if (!fActive && !piFirstCfg)
                            continue;

                        /* Get the 'Cfg#' value. */
                        psz = strstr(psz, "Cfg#="); Assert(psz);
                        if (psz)
                        {
                            psz = RTStrStripL(psz + 5);
                            rc = RTStrToUInt32Ex(psz, &pszNext, 10, &u); AssertRC(rc);
                            if (RT_SUCCESS(rc))
                            {
                                if (piFirstCfg)
                                {
                                    *piFirstCfg = u;
                                    piFirstCfg = NULL;
                                }
                                if (fActive)
                                    iActiveCfg = u;
                            }
                        }
                        if (fActive)
                            break;
                    }
                    break;
                }
                RTStrmClose(pFile);
            }
        }
    }
    RTMemFree(pszDevices);

    return iActiveCfg;
}


/**
 * This finds the active configuration from sysfs.
 *
 * @returns The Cfg#.
 * @returns -1 if no active config.
 * @param   pProxyDev       The proxy device instance.
 * @param   pszPath         The sysfs path for the device.
 * @param   piFirstCfg      The first configuration. (optional)
 * @internal
 */
static int usbProxyLinuxFindActiveConfigSysfs(PUSBPROXYDEV pProxyDev, const char *pszPath, int *piFirstCfg)
{
#ifdef VBOX_USB_WITH_SYSFS
    if (piFirstCfg != NULL)
        *piFirstCfg = pProxyDev->paCfgDescs != NULL
                    ? pProxyDev->paCfgDescs[0].Core.bConfigurationValue
                    : 1;
    int64_t bCfg = 0;
    int rc = RTLinuxSysFsReadIntFile(10, &bCfg, "%s/bConfigurationValue", pszPath);
    if (RT_FAILURE(rc))
        bCfg = -1;
    return (int)bCfg;
#else  /* !VBOX_USB_WITH_SYSFS */
    return -1;
#endif /* !VBOX_USB_WITH_SYSFS */
}


/**
 * This finds the active configuration.
 *
 * @returns The Cfg#.
 * @returns -1 if no active config.
 * @param   pProxyDev       The proxy device instance.
 * @param   pszPath         The sysfs path for the device, or the usbfs device
 *                          node path.
 * @param   piFirstCfg      The first configuration. (optional)
 * @internal
 */
static int usbProxyLinuxFindActiveConfig(PUSBPROXYDEV pProxyDev, const char *pszPath, int *piFirstCfg)
{
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);
    if (pDevLnx->fUsingSysfs)
        return usbProxyLinuxFindActiveConfigSysfs(pProxyDev, pszPath, piFirstCfg);
    return usbProxyLinuxFindActiveConfigUsbfs(pProxyDev, pszPath, piFirstCfg);
}


/**
 * Extracts the Linux file descriptor associated with the kernel USB device.
 * This is used by rdesktop-vrdp for polling for events.
 * @returns  the FD, or asserts and returns -1 on error
 * @param    pProxyDev    The device instance
 */
RTDECL(int) USBProxyDeviceLinuxGetFD(PUSBPROXYDEV pProxyDev)
{
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);
    AssertReturn(pDevLnx->hFile != NIL_RTFILE, -1);
    return RTFileToNative(pDevLnx->hFile);
}


/**
 * Opens the device file.
 *
 * @returns VBox status code.
 * @param   pProxyDev       The device instance.
 * @param   pszAddress      If we are using usbfs, this is the path to the
 *                          device.  If we are using sysfs, this is a string of
 *                          the form "sysfs:<sysfs path>//device:<device node>".
 *                          In the second case, the two paths are guaranteed
 *                          not to contain the substring "//".
 */
static DECLCALLBACK(int) usbProxyLinuxOpen(PUSBPROXYDEV pProxyDev, const char *pszAddress)
{
    LogFlow(("usbProxyLinuxOpen: pProxyDev=%p pszAddress=%s\n", pProxyDev, pszAddress));
    const char *pszDevNode;
    const char *pszPath;
    size_t      cchPath;
    bool        fUsingSysfs;

    /*
     * Are we using sysfs or usbfs?
     */
#ifdef VBOX_USB_WITH_SYSFS
    fUsingSysfs = strncmp(pszAddress, RT_STR_TUPLE("sysfs:")) == 0;
    if (fUsingSysfs)
    {
        pszDevNode = strstr(pszAddress, "//device:");
        if (!pszDevNode)
        {
            LogRel(("usbProxyLinuxOpen: Invalid device address: '%s'\n", pszAddress));
            return VERR_INVALID_PARAMETER;
        }

        pszPath = pszAddress + sizeof("sysfs:") - 1;
        cchPath = pszDevNode - pszPath;
        pszDevNode += sizeof("//device:") - 1;
    }
    else
#endif  /* VBOX_USB_WITH_SYSFS */
    {
#ifndef VBOX_USB_WITH_SYSFS
        fUsingSysfs = false;
#endif
        pszPath = pszDevNode = pszAddress;
        cchPath = strlen(pszPath);
    }

    /*
     * Try open the device node.
     */
    RTFILE hFile;
    int rc = RTFileOpen(&hFile, pszDevNode, RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_SUCCESS(rc))
    {
        /*
         * Initialize the linux backend data.
         */
        PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);

        RTListInit(&pDevLnx->ListFree);
        RTListInit(&pDevLnx->ListInFlight);
        pDevLnx->pszPath = RTStrDupN(pszPath, cchPath);
        if (pDevLnx->pszPath)
        {
            rc = RTPipeCreate(&pDevLnx->hPipeWakeupR, &pDevLnx->hPipeWakeupW, 0);
            if (RT_SUCCESS(rc))
            {
                pDevLnx->fUsingSysfs = fUsingSysfs;
                pDevLnx->hFile = hFile;
                pDevLnx->fClaimedIfsMask = 0;
                rc = RTCritSectInit(&pDevLnx->CritSect);
                if (RT_SUCCESS(rc))
                {
                    LogFlow(("usbProxyLinuxOpen(%p, %s): returns successfully File=%RTfile iActiveCfg=%d\n",
                             pProxyDev, pszAddress, pDevLnx->hFile, pProxyDev->iActiveCfg));

                    return VINF_SUCCESS;
                }
                RTPipeClose(pDevLnx->hPipeWakeupR);
                RTPipeClose(pDevLnx->hPipeWakeupW);
            }
        }
        else
            rc = VERR_NO_MEMORY;

        RTFileClose(hFile);
    }
    else if (rc == VERR_ACCESS_DENIED)
        rc = VERR_VUSB_USBFS_PERMISSION;

    Log(("usbProxyLinuxOpen(%p, %s) failed, rc=%Rrc!\n", pProxyDev, pszAddress, rc));
    return rc;
}


/**
 * Claims all the interfaces and figures out the
 * current configuration.
 *
 * @returns VINF_SUCCESS.
 * @param   pProxyDev       The proxy device.
 */
static DECLCALLBACK(int) usbProxyLinuxInit(PUSBPROXYDEV pProxyDev)
{
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);

    /*
     * Brute force rulez.
     * usbProxyLinuxSetConnected check for masked interfaces.
     */
    unsigned iIf;
    for (iIf = 0; iIf < 256; iIf++)
        usbProxyLinuxSetConnected(pProxyDev, iIf, false, true);

    /*
     * Determine the active configuration.
     *
     * If there isn't any active configuration, we will get EHOSTUNREACH (113) errors
     * when trying to read the device descriptors in usbProxyDevCreate. So, we'll make
     * the first one active (usually 1) then.
     */
    pProxyDev->cIgnoreSetConfigs = 1;
    int iFirstCfg;
    pProxyDev->iActiveCfg = usbProxyLinuxFindActiveConfig(pProxyDev, pDevLnx->pszPath, &iFirstCfg);
    if (pProxyDev->iActiveCfg == -1)
    {
        usbProxyLinuxDoIoCtl(pProxyDev, USBDEVFS_SETCONFIGURATION, &iFirstCfg, false, UINT32_MAX);
        pProxyDev->iActiveCfg = usbProxyLinuxFindActiveConfig(pProxyDev, pDevLnx->pszPath, NULL);
        Log(("usbProxyLinuxInit: No active config! Tried to set %d: iActiveCfg=%d\n", iFirstCfg, pProxyDev->iActiveCfg));
    }
    else
        Log(("usbProxyLinuxInit(%p): iActiveCfg=%d\n", pProxyDev, pProxyDev->iActiveCfg));
    return VINF_SUCCESS;
}


/**
 * Closes the proxy device.
 */
static DECLCALLBACK(void) usbProxyLinuxClose(PUSBPROXYDEV pProxyDev)
{
    LogFlow(("usbProxyLinuxClose: pProxyDev=%s\n", usbProxyGetName(pProxyDev)));
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);
    AssertPtrReturnVoid(pDevLnx);

    /*
     * Try put the device in a state which linux can cope with before we release it.
     * Resetting it would be a nice start, although we must remember
     * that it might have been disconnected...
     *
     * Don't reset if we're masking interfaces or if construction failed.
     */
    if (pProxyDev->fInited)
    {
        /* ASSUMES: thread == EMT */
        if (    pProxyDev->fMaskedIfs
            ||  !usbProxyLinuxDoIoCtl(pProxyDev, USBDEVFS_RESET, NULL, false, 10))
        {
            /* Connect drivers. */
            unsigned iIf;
            for (iIf = 0; iIf < 256; iIf++)
                usbProxyLinuxSetConnected(pProxyDev, iIf, true, true);
            Log(("USB: Successfully reset device pProxyDev=%s\n", usbProxyGetName(pProxyDev)));
        }
        else if (errno != ENODEV)
            LogRel(("USB: Reset failed, errno=%d, pProxyDev=%s.\n", errno, usbProxyGetName(pProxyDev)));
        else    /* This will happen if device was detached. */
            Log(("USB: Reset failed, errno=%d (ENODEV), pProxyDev=%s.\n", errno, usbProxyGetName(pProxyDev)));
    }

    /*
     * Now we can free all the resources and close the device.
     */
    RTCritSectDelete(&pDevLnx->CritSect);

    PUSBPROXYURBLNX pUrbLnx;
    PUSBPROXYURBLNX pUrbLnxNext;
    RTListForEachSafe(&pDevLnx->ListInFlight, pUrbLnx, pUrbLnxNext, USBPROXYURBLNX, NodeList)
    {
        RTListNodeRemove(&pUrbLnx->NodeList);

        if (    usbProxyLinuxDoIoCtl(pProxyDev, USBDEVFS_DISCARDURB, &pUrbLnx->KUrb, false, UINT32_MAX)
            &&  errno != ENODEV
            &&  errno != ENOENT)
            AssertMsgFailed(("errno=%d\n", errno));

        if (pUrbLnx->pSplitHead)
        {
            PUSBPROXYURBLNX pCur = pUrbLnx->pSplitNext;
            while (pCur)
            {
                PUSBPROXYURBLNX pFree = pCur;
                pCur = pFree->pSplitNext;
                if (    !pFree->fSplitElementReaped
                    &&  usbProxyLinuxDoIoCtl(pProxyDev, USBDEVFS_DISCARDURB, &pFree->KUrb, false, UINT32_MAX)
                    &&  errno != ENODEV
                    &&  errno != ENOENT)
                    AssertMsgFailed(("errno=%d\n", errno));
                RTMemFree(pFree);
            }
        }
        else
            Assert(!pUrbLnx->pSplitNext);
        RTMemFree(pUrbLnx);
    }

    RTListForEachSafe(&pDevLnx->ListFree, pUrbLnx, pUrbLnxNext, USBPROXYURBLNX, NodeList)
    {
        RTListNodeRemove(&pUrbLnx->NodeList);
        RTMemFree(pUrbLnx);
    }

    RTFileClose(pDevLnx->hFile);
    pDevLnx->hFile = NIL_RTFILE;

    RTPipeClose(pDevLnx->hPipeWakeupR);
    RTPipeClose(pDevLnx->hPipeWakeupW);

    RTStrFree(pDevLnx->pszPath);

    LogFlow(("usbProxyLinuxClose: returns\n"));
}


/** @interface_method_impl{USBPROXYBACK,pfnReset} */
static DECLCALLBACK(int) usbProxyLinuxReset(PUSBPROXYDEV pProxyDev, bool fResetOnLinux)
{
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);
    RT_NOREF(fResetOnLinux);
    Assert(!pProxyDev->fMaskedIfs);
    LogFlow(("usbProxyLinuxReset: pProxyDev=%s\n", usbProxyGetName(pProxyDev)));

    uint32_t fActiveIfsMask = pDevLnx->fClaimedIfsMask;
    unsigned i;

    /*
     * Before reset, release claimed interfaces. This less than obvious move
     * prevents Linux from rebinding in-kernel drivers to the device after reset.
     */
    for (i = 0; i < (sizeof(fActiveIfsMask) * 8); ++i)
    {
        if (fActiveIfsMask & RT_BIT(i))
        {
            usbProxyLinuxReleaseInterface(pProxyDev, i);
        }
    }

    if (usbProxyLinuxDoIoCtl(pProxyDev, USBDEVFS_RESET, NULL, false, 10))
    {
        int rc = errno;
        LogRel(("usb-linux: Reset failed, rc=%Rrc errno=%d.\n", RTErrConvertFromErrno(rc), rc));
        pProxyDev->iActiveCfg = -1;
        return RTErrConvertFromErrno(rc);
    }

    /*
     * Now reclaim previously claimed interfaces. If that doesn't work, let's hope
     * the guest/VUSB can recover from that. Can happen if reset changes configuration.
     */
    for (i = 0; i < (sizeof(fActiveIfsMask) * 8); ++i)
    {
        if (fActiveIfsMask & RT_BIT(i))
        {
            usbProxyLinuxClaimInterface(pProxyDev, i);
        }
    }

    /* find the active config - damn annoying. */
    pProxyDev->iActiveCfg = usbProxyLinuxFindActiveConfig(pProxyDev, pDevLnx->pszPath, NULL);
    LogFlow(("usbProxyLinuxReset: returns successfully iActiveCfg=%d\n", pProxyDev->iActiveCfg));

    pProxyDev->cIgnoreSetConfigs = 2;
    return VINF_SUCCESS;
}


/**
 * SET_CONFIGURATION.
 *
 * The caller makes sure that it's not called first time after open or reset
 * with the active interface.
 *
 * @returns success indicator.
 * @param   pProxyDev       The device instance data.
 * @param   iCfg            The configuration to set.
 */
static DECLCALLBACK(int) usbProxyLinuxSetConfig(PUSBPROXYDEV pProxyDev, int iCfg)
{
    LogFlow(("usbProxyLinuxSetConfig: pProxyDev=%s cfg=%#x\n",
             usbProxyGetName(pProxyDev), iCfg));

    if (usbProxyLinuxDoIoCtl(pProxyDev, USBDEVFS_SETCONFIGURATION, &iCfg, true, UINT32_MAX))
    {
        Log(("usb-linux: Set configuration. errno=%d\n", errno));
        return RTErrConvertFromErrno(errno);
    }
    return VINF_SUCCESS;
}


/**
 * Claims an interface.
 * @returns success indicator.
 */
static DECLCALLBACK(int) usbProxyLinuxClaimInterface(PUSBPROXYDEV pProxyDev, int iIf)
{
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);

    LogFlow(("usbProxyLinuxClaimInterface: pProxyDev=%s ifnum=%#x\n", usbProxyGetName(pProxyDev), iIf));
    usbProxyLinuxSetConnected(pProxyDev, iIf, false, false);

    if (usbProxyLinuxDoIoCtl(pProxyDev, USBDEVFS_CLAIMINTERFACE, &iIf, true, UINT32_MAX))
    {
        pDevLnx->fClaimedIfsMask &= ~RT_BIT(iIf);
        LogRel(("usb-linux: Claim interface. errno=%d pProxyDev=%s\n", errno, usbProxyGetName(pProxyDev)));
        return RTErrConvertFromErrno(errno);
    }
    pDevLnx->fClaimedIfsMask |= RT_BIT(iIf);
    return VINF_SUCCESS;
}


/**
 * Releases an interface.
 * @returns success indicator.
 */
static DECLCALLBACK(int) usbProxyLinuxReleaseInterface(PUSBPROXYDEV pProxyDev, int iIf)
{
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);

    LogFlow(("usbProxyLinuxReleaseInterface: pProxyDev=%s ifnum=%#x\n", usbProxyGetName(pProxyDev), iIf));

    if (usbProxyLinuxDoIoCtl(pProxyDev, USBDEVFS_RELEASEINTERFACE, &iIf, true, UINT32_MAX))
    {
        LogRel(("usb-linux: Release interface, errno=%d. pProxyDev=%s\n", errno, usbProxyGetName(pProxyDev)));
        return RTErrConvertFromErrno(errno);
    }
    pDevLnx->fClaimedIfsMask &= ~RT_BIT(iIf);
    return VINF_SUCCESS;
}


/**
 * SET_INTERFACE.
 *
 * @returns success indicator.
 */
static DECLCALLBACK(int) usbProxyLinuxSetInterface(PUSBPROXYDEV pProxyDev, int iIf, int iAlt)
{
    struct usbdevfs_setinterface SetIf;
    LogFlow(("usbProxyLinuxSetInterface: pProxyDev=%p iIf=%#x iAlt=%#x\n", pProxyDev, iIf, iAlt));

    SetIf.interface  = iIf;
    SetIf.altsetting = iAlt;
    if (usbProxyLinuxDoIoCtl(pProxyDev, USBDEVFS_SETINTERFACE, &SetIf, true, UINT32_MAX))
    {
        Log(("usb-linux: Set interface, errno=%d. pProxyDev=%s\n", errno, usbProxyGetName(pProxyDev)));
        return RTErrConvertFromErrno(errno);
    }
    return VINF_SUCCESS;
}


/**
 * Clears the halted endpoint 'EndPt'.
 */
static DECLCALLBACK(int) usbProxyLinuxClearHaltedEp(PUSBPROXYDEV pProxyDev, unsigned int EndPt)
{
    LogFlow(("usbProxyLinuxClearHaltedEp: pProxyDev=%s EndPt=%u\n", usbProxyGetName(pProxyDev), EndPt));

    if (usbProxyLinuxDoIoCtl(pProxyDev, USBDEVFS_CLEAR_HALT, &EndPt, true, UINT32_MAX))
    {
        /*
         * Unfortunately this doesn't work on control pipes.
         * Windows doing this on the default endpoint and possibly other pipes too,
         * so we'll feign success for ENOENT errors.
         */
        if (errno == ENOENT)
        {
            Log(("usb-linux: clear_halted_ep failed errno=%d. pProxyDev=%s ep=%d - IGNORED\n",
                 errno, usbProxyGetName(pProxyDev), EndPt));
            return VINF_SUCCESS;
        }
        Log(("usb-linux: clear_halted_ep failed errno=%d. pProxyDev=%s ep=%d\n",
             errno, usbProxyGetName(pProxyDev), EndPt));
        return RTErrConvertFromErrno(errno);
    }
    return VINF_SUCCESS;
}


/**
 * Setup packet byte-swapping routines.
 */
static void usbProxyLinuxUrbSwapSetup(PVUSBSETUP pSetup)
{
    pSetup->wValue = RT_H2LE_U16(pSetup->wValue);
    pSetup->wIndex = RT_H2LE_U16(pSetup->wIndex);
    pSetup->wLength = RT_H2LE_U16(pSetup->wLength);
}


/**
 * Clean up after a failed URB submit.
 */
static void usbProxyLinuxCleanupFailedSubmit(PUSBPROXYDEV pProxyDev, PUSBPROXYURBLNX pUrbLnx, PUSBPROXYURBLNX pCur,
                                             PVUSBURB pUrb, bool *pfUnplugged)
{
    if (pUrb->enmType == VUSBXFERTYPE_MSG)
        usbProxyLinuxUrbSwapSetup((PVUSBSETUP)pUrb->abData);

    /* discard and reap later (walking with pUrbLnx). */
    if (pUrbLnx != pCur)
    {
        for (;;)
        {
            pUrbLnx->fCanceledBySubmit = true;
            pUrbLnx->KUrb.usercontext = NULL;
            if (usbProxyLinuxDoIoCtl(pProxyDev, USBDEVFS_DISCARDURB, &pUrbLnx->KUrb, false, UINT32_MAX))
            {
                if (errno == ENODEV)
                    *pfUnplugged = true;
                else if (errno == ENOENT)
                    pUrbLnx->fSplitElementReaped = true;
                else
                    LogRel(("USB: Failed to discard %p! errno=%d (pUrb=%p)\n", pUrbLnx->KUrb.usercontext, errno, pUrb)); /* serious! */
            }
            if (pUrbLnx->pSplitNext == pCur)
            {
                pUrbLnx->pSplitNext = NULL;
                break;
            }
            pUrbLnx = pUrbLnx->pSplitNext; Assert(pUrbLnx);
        }
    }

    /* free the unsubmitted ones. */
    while (pCur)
    {
        PUSBPROXYURBLNX pFree = pCur;
        pCur = pCur->pSplitNext;
        usbProxyLinuxUrbFree(pProxyDev, pFree);
    }

    /* send unplug event if we failed with ENODEV originally. */
    if (*pfUnplugged)
        usbProxLinuxUrbUnplugged(pProxyDev);
}

/**
 * Submit one URB through the usbfs IOCTL interface, with
 * retries
 *
 * @returns VBox status code.
 */
static int usbProxyLinuxSubmitURB(PUSBPROXYDEV pProxyDev, PUSBPROXYURBLNX pCur, PVUSBURB pUrb, bool *pfUnplugged)
{
    RT_NOREF(pUrb);
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);
    unsigned        cTries = 0;

    while (ioctl(RTFileToNative(pDevLnx->hFile), USBDEVFS_SUBMITURB, &pCur->KUrb))
    {
        if (errno == EINTR)
            continue;
        if (errno == ENODEV)
        {
            Log(("usbProxyLinuxSubmitURB: ENODEV -> unplugged. pProxyDev=%s\n", usbProxyGetName(pProxyDev)));
            *pfUnplugged = true;
            return RTErrConvertFromErrno(errno);
        }

        Log(("usb-linux: Submit URB %p -> %d!!! type=%d ep=%#x buffer_length=%#x cTries=%d\n",
             pUrb, errno, pCur->KUrb.type, pCur->KUrb.endpoint, pCur->KUrb.buffer_length, cTries));
        if (errno != EBUSY && ++cTries < 3) /* this doesn't work for the floppy :/ */
            continue;

        return RTErrConvertFromErrno(errno);
    }
    return VINF_SUCCESS;
}

/** The split size. 16K in known Linux kernel versions. */
#define SPLIT_SIZE 0x4000

/**
 * Create a URB fragment of up to SPLIT_SIZE size and hook it
 * into the list of fragments.
 *
 * @returns pointer to newly allocated URB fragment or NULL.
 */
static PUSBPROXYURBLNX usbProxyLinuxSplitURBFragment(PUSBPROXYDEV pProxyDev, PUSBPROXYURBLNX pHead, PUSBPROXYURBLNX pCur)
{
    PUSBPROXYURBLNX     pNew;
    uint32_t            cbLeft = pCur->cbSplitRemaining;
    uint8_t             *pb = (uint8_t *)pCur->KUrb.buffer;

    LogFlowFunc(("pProxyDev=%p pHead=%p pCur=%p\n", pProxyDev, pHead, pCur));

    Assert(cbLeft != 0);
    pNew = pCur->pSplitNext = usbProxyLinuxUrbAlloc(pProxyDev, pHead);
    if (!pNew)
    {
        usbProxyLinuxUrbFreeSplitList(pProxyDev, pHead);
        return NULL;
    }
    Assert(pNew->pSplitHead == pHead);
    Assert(pNew->pSplitNext == NULL);

    pNew->KUrb = pHead->KUrb;
    pNew->KUrb.buffer = pb + pCur->KUrb.buffer_length;
    pNew->KUrb.buffer_length = RT_MIN(cbLeft, SPLIT_SIZE);
    pNew->KUrb.actual_length = 0;

    cbLeft -= pNew->KUrb.buffer_length;
    Assert(cbLeft < INT32_MAX);
    pNew->cbSplitRemaining = cbLeft;
    LogFlowFunc(("returns pNew=%p\n", pNew));
    return pNew;
}

/**
 * Try splitting up a VUSB URB into smaller URBs which the
 * linux kernel (usbfs) can deal with.
 *
 * NB: For ShortOK reads things get a little tricky - we don't
 * know how much data is going to arrive and not all the
 * fragment URBs might be filled. We can only safely set up one
 * URB at a time -> worse performance but correct behaviour.
 *
 * @returns VBox status code.
 * @param   pProxyDev   The proxy device.
 * @param   pUrbLnx     The linux URB which was rejected because of being too big.
 * @param   pUrb        The VUSB URB.
 */
static int usbProxyLinuxUrbQueueSplit(PUSBPROXYDEV pProxyDev, PUSBPROXYURBLNX pUrbLnx, PVUSBURB pUrb)
{
    /*
     * Split it up into SPLIT_SIZE sized blocks.
     */
    const unsigned cKUrbs = (pUrb->cbData + SPLIT_SIZE - 1) / SPLIT_SIZE;
    LogFlow(("usbProxyLinuxUrbQueueSplit: pUrb=%p cKUrbs=%d cbData=%d\n", pUrb, cKUrbs, pUrb->cbData));

    uint32_t cbLeft = pUrb->cbData;
    uint8_t *pb = &pUrb->abData[0];

    /* the first one (already allocated) */
    switch (pUrb->enmType)
    {
        default: /* shut up gcc */
        case VUSBXFERTYPE_BULK: pUrbLnx->KUrb.type = USBDEVFS_URB_TYPE_BULK; break;
        case VUSBXFERTYPE_INTR: pUrbLnx->KUrb.type = USBDEVFS_URB_TYPE_INTERRUPT; break;
        case VUSBXFERTYPE_MSG:  pUrbLnx->KUrb.type = USBDEVFS_URB_TYPE_CONTROL; break;
        case VUSBXFERTYPE_ISOC:
            AssertMsgFailed(("We can't split isochronous URBs!\n"));
            usbProxyLinuxUrbFree(pProxyDev, pUrbLnx);
            return VERR_INVALID_PARAMETER; /** @todo Better status code. */
    }
    pUrbLnx->KUrb.endpoint          = pUrb->EndPt;
    if (pUrb->enmDir == VUSBDIRECTION_IN)
        pUrbLnx->KUrb.endpoint |= 0x80;
    pUrbLnx->KUrb.flags             = 0;
    if (pUrb->enmDir == VUSBDIRECTION_IN && pUrb->fShortNotOk)
        pUrbLnx->KUrb.flags        |= USBDEVFS_URB_SHORT_NOT_OK;
    pUrbLnx->KUrb.status            = 0;
    pUrbLnx->KUrb.buffer            = pb;
    pUrbLnx->KUrb.buffer_length     = RT_MIN(cbLeft, SPLIT_SIZE);
    pUrbLnx->KUrb.actual_length     = 0;
    pUrbLnx->KUrb.start_frame       = 0;
    pUrbLnx->KUrb.number_of_packets = 0;
    pUrbLnx->KUrb.error_count       = 0;
    pUrbLnx->KUrb.signr             = 0;
    pUrbLnx->KUrb.usercontext       = pUrb;
    pUrbLnx->pSplitHead = pUrbLnx;
    pUrbLnx->pSplitNext = NULL;

    PUSBPROXYURBLNX pCur = pUrbLnx;

    cbLeft -= pUrbLnx->KUrb.buffer_length;
    pUrbLnx->cbSplitRemaining = cbLeft;

    int rc = VINF_SUCCESS;
    bool fUnplugged = false;
    if (pUrb->enmDir == VUSBDIRECTION_IN && !pUrb->fShortNotOk)
    {
        /* Subsequent fragments will be queued only after the previous fragment is reaped
         * and only if necessary.
         */
        Log(("usb-linux: Large ShortOK read, only queuing first fragment.\n"));
        Assert(pUrbLnx->cbSplitRemaining > 0 && pUrbLnx->cbSplitRemaining < 256 * _1K);
        rc = usbProxyLinuxSubmitURB(pProxyDev, pUrbLnx, pUrb, &fUnplugged);
    }
    else
    {
        /* the rest. */
        unsigned i;
        for (i = 1; i < cKUrbs; i++)
        {
            pCur = usbProxyLinuxSplitURBFragment(pProxyDev, pUrbLnx, pCur);
            if (!pCur)
                return VERR_NO_MEMORY;
        }
        Assert(pCur->cbSplitRemaining == 0);

        /* Submit the blocks. */
        pCur = pUrbLnx;
        for (i = 0; i < cKUrbs; i++, pCur = pCur->pSplitNext)
        {
            rc = usbProxyLinuxSubmitURB(pProxyDev, pCur, pUrb, &fUnplugged);
            if (RT_FAILURE(rc))
                break;
        }
    }

    if (RT_SUCCESS(rc))
    {
        pUrb->Dev.pvPrivate = pUrbLnx;
        usbProxyLinuxUrbLinkInFlight(USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX), pUrbLnx);
        LogFlow(("usbProxyLinuxUrbQueueSplit: ok\n"));
        return VINF_SUCCESS;
    }

    usbProxyLinuxCleanupFailedSubmit(pProxyDev, pUrbLnx, pCur, pUrb, &fUnplugged);
    return rc;
}


/**
 * @interface_method_impl{USBPROXYBACK,pfnUrbQueue}
 */
static DECLCALLBACK(int) usbProxyLinuxUrbQueue(PUSBPROXYDEV pProxyDev, PVUSBURB pUrb)
{
    int             rc = VINF_SUCCESS;
    unsigned        cTries;
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);
    LogFlow(("usbProxyLinuxUrbQueue: pProxyDev=%s pUrb=%p EndPt=%d cbData=%d\n",
             usbProxyGetName(pProxyDev), pUrb, pUrb->EndPt, pUrb->cbData));

    /*
     * Allocate a linux urb.
     */
    PUSBPROXYURBLNX pUrbLnx = usbProxyLinuxUrbAlloc(pProxyDev, NULL);
    if (!pUrbLnx)
        return VERR_NO_MEMORY;

    pUrbLnx->KUrb.endpoint          = pUrb->EndPt | (pUrb->enmDir == VUSBDIRECTION_IN ? 0x80 : 0);
    pUrbLnx->KUrb.status            = 0;
    pUrbLnx->KUrb.flags             = 0;
    if (pUrb->enmDir == VUSBDIRECTION_IN && pUrb->fShortNotOk)
        pUrbLnx->KUrb.flags        |= USBDEVFS_URB_SHORT_NOT_OK;
    pUrbLnx->KUrb.buffer            = pUrb->abData;
    pUrbLnx->KUrb.buffer_length     = pUrb->cbData;
    pUrbLnx->KUrb.actual_length     = 0;
    pUrbLnx->KUrb.start_frame       = 0;
    pUrbLnx->KUrb.number_of_packets = 0;
    pUrbLnx->KUrb.error_count       = 0;
    pUrbLnx->KUrb.signr             = 0;
    pUrbLnx->KUrb.usercontext       = pUrb;

    switch (pUrb->enmType)
    {
        case VUSBXFERTYPE_MSG:
            pUrbLnx->KUrb.type = USBDEVFS_URB_TYPE_CONTROL;
            if (pUrb->cbData < sizeof(VUSBSETUP))
            {
                usbProxyLinuxUrbFree(pProxyDev, pUrbLnx);
                return VERR_BUFFER_UNDERFLOW;
            }
            usbProxyLinuxUrbSwapSetup((PVUSBSETUP)pUrb->abData);
            LogFlow(("usbProxyLinuxUrbQueue: message\n"));
            break;
        case VUSBXFERTYPE_BULK:
            pUrbLnx->KUrb.type = USBDEVFS_URB_TYPE_BULK;
            break;
        case VUSBXFERTYPE_ISOC:
            pUrbLnx->KUrb.type = USBDEVFS_URB_TYPE_ISO;
            pUrbLnx->KUrb.flags |= USBDEVFS_URB_ISO_ASAP;
            pUrbLnx->KUrb.number_of_packets = pUrb->cIsocPkts;
            unsigned i;
            for (i = 0; i < pUrb->cIsocPkts; i++)
            {
#if RT_GNUC_PREREQ(4, 6)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Warray-bounds"
#endif
                pUrbLnx->KUrb.iso_frame_desc[i].length = pUrb->aIsocPkts[i].cb;
                pUrbLnx->KUrb.iso_frame_desc[i].actual_length = 0;
                pUrbLnx->KUrb.iso_frame_desc[i].status = 0x7fff;
#if RT_GNUC_PREREQ(4, 6)
# pragma GCC diagnostic pop
#endif
            }
            break;
        case VUSBXFERTYPE_INTR:
            pUrbLnx->KUrb.type = USBDEVFS_URB_TYPE_INTERRUPT;
            break;
        default:
            rc = VERR_INVALID_PARAMETER; /** @todo better status code. */
    }

    /*
     * We have to serialize access by using the critial section here because this
     * thread might be suspended after submitting the URB but before linking it into
     * the in flight list. This would get us in trouble when reaping the URB on another
     * thread while it isn't in the in flight list.
     *
     * Linking the URB into the list before submitting it like it was done in the past is not
     * possible either because submitting the URB might fail here because the device gets
     * detached. The reaper thread gets this event too and might race this thread before we
     * can unlink the URB from the active list and the common code might end up freeing
     * the common URB structure twice.
     */
    RTCritSectEnter(&pDevLnx->CritSect);
    /*
     * Submit it.
     */
    cTries = 0;
    while (ioctl(RTFileToNative(pDevLnx->hFile), USBDEVFS_SUBMITURB, &pUrbLnx->KUrb))
    {
        if (errno == EINTR)
            continue;
        if (errno == ENODEV)
        {
            rc = RTErrConvertFromErrno(errno);
            Log(("usbProxyLinuxUrbQueue: ENODEV -> unplugged. pProxyDev=%s\n", usbProxyGetName(pProxyDev)));
            if (pUrb->enmType == VUSBXFERTYPE_MSG)
                usbProxyLinuxUrbSwapSetup((PVUSBSETUP)pUrb->abData);

            RTCritSectLeave(&pDevLnx->CritSect);
            usbProxyLinuxUrbFree(pProxyDev, pUrbLnx);
            usbProxLinuxUrbUnplugged(pProxyDev);
            return rc;
        }

        /*
         * usbfs has or used to have a low buffer limit (16KB) in order to prevent
         * processes wasting kmalloc'ed memory. It will return EINVAL if break that
         * limit, and we'll have to split the VUSB URB up into multiple linux URBs.
         *
         * Since this is a limit which is subject to change, we cannot check for it
         * before submitting the URB. We just have to try and fail.
         */
        if (    errno == EINVAL
            &&  pUrb->cbData >= 8*_1K)
        {
            rc = usbProxyLinuxUrbQueueSplit(pProxyDev, pUrbLnx, pUrb);
            RTCritSectLeave(&pDevLnx->CritSect);
            return rc;
        }

        Log(("usb-linux: Queue URB %p -> %d!!! type=%d ep=%#x buffer_length=%#x cTries=%d\n",
             pUrb, errno, pUrbLnx->KUrb.type, pUrbLnx->KUrb.endpoint, pUrbLnx->KUrb.buffer_length, cTries));
        if (errno != EBUSY && ++cTries < 3) /* this doesn't work for the floppy :/ */
            continue;

        RTCritSectLeave(&pDevLnx->CritSect);
        rc = RTErrConvertFromErrno(errno);
        if (pUrb->enmType == VUSBXFERTYPE_MSG)
            usbProxyLinuxUrbSwapSetup((PVUSBSETUP)pUrb->abData);
        usbProxyLinuxUrbFree(pProxyDev, pUrbLnx);
        return rc;
    }

    usbProxyLinuxUrbLinkInFlight(pDevLnx, pUrbLnx);
    RTCritSectLeave(&pDevLnx->CritSect);

    LogFlow(("usbProxyLinuxUrbQueue: ok\n"));
    pUrb->Dev.pvPrivate = pUrbLnx;
    return rc;
}


/**
 * Translate the linux status to a VUSB status.
 *
 * @remarks see cc_to_error in ohci.h, uhci_map_status in uhci-q.c,
 *          sitd_complete+itd_complete in ehci-sched.c, and qtd_copy_status in
 *          ehci-q.c.
 */
static VUSBSTATUS vusbProxyLinuxStatusToVUsbStatus(int iStatus)
{
    switch (iStatus)
    {
        /** @todo VUSBSTATUS_NOT_ACCESSED */
        case -EXDEV: /* iso transfer, partial result. */
        case 0:
            return VUSBSTATUS_OK;

        case -EILSEQ:
            return VUSBSTATUS_CRC;

        case -EREMOTEIO: /* ehci and ohci uses this for underflow error. */
            return VUSBSTATUS_DATA_UNDERRUN;
        case -EOVERFLOW:
            return VUSBSTATUS_DATA_OVERRUN;

        case -ETIME:
        case -ENODEV:
            return VUSBSTATUS_DNR;

        //case -ECOMM:
        //    return VUSBSTATUS_BUFFER_OVERRUN;
        //case -ENOSR:
        //    return VUSBSTATUS_BUFFER_UNDERRUN;

        case -EPROTO:
            Log(("vusbProxyLinuxStatusToVUsbStatus: DNR/EPPROTO!!\n"));
            return VUSBSTATUS_DNR;

        case -EPIPE:
            Log(("vusbProxyLinuxStatusToVUsbStatus: STALL/EPIPE!!\n"));
            return VUSBSTATUS_STALL;

        case -ESHUTDOWN:
            Log(("vusbProxyLinuxStatusToVUsbStatus: SHUTDOWN!!\n"));
            return VUSBSTATUS_STALL;

        case -ENOENT:
            Log(("vusbProxyLinuxStatusToVUsbStatus: ENOENT!!\n"));
            return VUSBSTATUS_STALL;

        default:
            Log(("vusbProxyLinuxStatusToVUsbStatus: status %d!!\n", iStatus));
            return VUSBSTATUS_STALL;
    }
}


/**
 * Get and translates the linux status to a VUSB status.
 */
static VUSBSTATUS vusbProxyLinuxUrbGetStatus(PUSBPROXYURBLNX pUrbLnx)
{
    return vusbProxyLinuxStatusToVUsbStatus(pUrbLnx->KUrb.status);
}


/**
 * Reap URBs in-flight on a device.
 *
 * @returns Pointer to a completed URB.
 * @returns NULL if no URB was completed.
 * @param   pProxyDev   The device.
 * @param   cMillies    Number of milliseconds to wait. Use 0 to not wait at all.
 */
static DECLCALLBACK(PVUSBURB) usbProxyLinuxUrbReap(PUSBPROXYDEV pProxyDev, RTMSINTERVAL cMillies)
{
    PUSBPROXYURBLNX pUrbLnx = NULL;
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);

     /*
     * Block for requested period.
     *
     * It seems to me that the path of poll() is shorter and
     * involves less semaphores than ioctl() on usbfs. So, we'll
     * do a poll regardless of whether cMillies == 0 or not.
     */
    if (cMillies)
    {
        int cMilliesWait = cMillies == RT_INDEFINITE_WAIT ? -1 : cMillies;

        for (;;)
        {
            struct pollfd pfd[2];
            pfd[0].fd = RTFileToNative(pDevLnx->hFile);
            pfd[0].events = POLLOUT | POLLWRNORM /* completed async */
                          | POLLERR | POLLHUP    /* disconnected */;
            pfd[0].revents = 0;

            pfd[1].fd = RTPipeToNative(pDevLnx->hPipeWakeupR);
            pfd[1].events = POLLIN | POLLHUP;
            pfd[1].revents = 0;

            int rc = poll(&pfd[0], 2, cMilliesWait);
            Log(("usbProxyLinuxUrbReap: poll rc = %d\n", rc));
            if (rc >= 1)
            {
                /* If the pipe caused the return drain it. */
                if (pfd[1].revents & POLLIN)
                {
                    uint8_t bRead;
                    size_t cbIgnored = 0;
                    RTPipeRead(pDevLnx->hPipeWakeupR, &bRead, 1, &cbIgnored);
                }
                break;
            }
            if (rc >= 0)
                return NULL;

            if (errno != EAGAIN)
            {
                Log(("usb-linux: Reap URB - poll -> %d errno=%d pProxyDev=%s\n", rc, errno, usbProxyGetName(pProxyDev)));
                return NULL;
            }
            Log(("usbProxyLinuxUrbReap: poll again - weird!!!\n"));
        }
    }

    /*
     * Reap URBs, non-blocking.
     */
    for (;;)
    {
        struct usbdevfs_urb *pKUrb;
        while (ioctl(RTFileToNative(pDevLnx->hFile), USBDEVFS_REAPURBNDELAY, &pKUrb))
            if (errno != EINTR)
            {
                if (errno == ENODEV)
                    usbProxLinuxUrbUnplugged(pProxyDev);
                else
                    Log(("usb-linux: Reap URB. errno=%d pProxyDev=%s\n", errno, usbProxyGetName(pProxyDev)));
                return NULL;
            }
        pUrbLnx = RT_FROM_MEMBER(pKUrb, USBPROXYURBLNX, KUrb);

        /* split list: Is the entire split list done yet? */
        if (pUrbLnx->pSplitHead)
        {
            pUrbLnx->fSplitElementReaped = true;

            /* for variable size URBs, we may need to queue more if the just-reaped URB was completely filled */
            if (pUrbLnx->cbSplitRemaining && (pKUrb->actual_length == pKUrb->buffer_length) && !pUrbLnx->pSplitNext)
            {
                bool fUnplugged = false;
                bool fSucceeded;

                Assert(pUrbLnx->pSplitHead);
                Assert((pKUrb->endpoint & 0x80) && !(pKUrb->flags & USBDEVFS_URB_SHORT_NOT_OK));
                PUSBPROXYURBLNX pNew = usbProxyLinuxSplitURBFragment(pProxyDev, pUrbLnx->pSplitHead, pUrbLnx);
                if (!pNew)
                {
                    Log(("usb-linux: Allocating URB fragment failed. errno=%d pProxyDev=%s\n", errno, usbProxyGetName(pProxyDev)));
                    return NULL;
                }
                PVUSBURB pUrb = (PVUSBURB)pUrbLnx->KUrb.usercontext;
                fSucceeded = usbProxyLinuxSubmitURB(pProxyDev, pNew, pUrb, &fUnplugged);
                if (fUnplugged)
                    usbProxLinuxUrbUnplugged(pProxyDev);
                if (!fSucceeded)
                    return NULL;
                continue;   /* try reaping another URB */
            }
            PUSBPROXYURBLNX pCur;
            for (pCur = pUrbLnx->pSplitHead; pCur; pCur = pCur->pSplitNext)
                if (!pCur->fSplitElementReaped)
                {
                    pUrbLnx = NULL;
                    break;
                }
            if (!pUrbLnx)
                continue;
            pUrbLnx = pUrbLnx->pSplitHead;
        }
        break;
    }

    /*
     * Ok, we got one!
     */
    PVUSBURB pUrb = (PVUSBURB)pUrbLnx->KUrb.usercontext;
    if (    pUrb
        &&  !pUrbLnx->fCanceledBySubmit)
    {
        if (pUrbLnx->pSplitHead)
        {
            /* split - find the end byte and the first error status. */
            Assert(pUrbLnx == pUrbLnx->pSplitHead);
            uint8_t *pbEnd = &pUrb->abData[0];
            pUrb->enmStatus = VUSBSTATUS_OK;
            PUSBPROXYURBLNX pCur;
            for (pCur = pUrbLnx; pCur; pCur = pCur->pSplitNext)
            {
                if (pCur->KUrb.actual_length)
                    pbEnd = (uint8_t *)pCur->KUrb.buffer + pCur->KUrb.actual_length;
                if (pUrb->enmStatus == VUSBSTATUS_OK)
                    pUrb->enmStatus = vusbProxyLinuxUrbGetStatus(pCur);
            }
            pUrb->cbData = pbEnd - &pUrb->abData[0];
            usbProxyLinuxUrbUnlinkInFlight(pDevLnx, pUrbLnx);
            usbProxyLinuxUrbFreeSplitList(pProxyDev, pUrbLnx);
        }
        else
        {
            /* unsplit. */
            pUrb->enmStatus = vusbProxyLinuxUrbGetStatus(pUrbLnx);
            pUrb->cbData = pUrbLnx->KUrb.actual_length;
            if (pUrb->enmType == VUSBXFERTYPE_ISOC)
            {
                unsigned i, off;
                for (i = 0, off = 0; i < pUrb->cIsocPkts; i++)
                {
#if RT_GNUC_PREREQ(4, 6)
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Warray-bounds"
#endif
                    pUrb->aIsocPkts[i].enmStatus = vusbProxyLinuxStatusToVUsbStatus(pUrbLnx->KUrb.iso_frame_desc[i].status);
                    Assert(pUrb->aIsocPkts[i].off == off);
                    pUrb->aIsocPkts[i].cb = pUrbLnx->KUrb.iso_frame_desc[i].actual_length;
                    off += pUrbLnx->KUrb.iso_frame_desc[i].length;
#if RT_GNUC_PREREQ(4, 6)
# pragma GCC diagnostic pop
#endif
                }
            }
            usbProxyLinuxUrbUnlinkInFlight(pDevLnx, pUrbLnx);
            usbProxyLinuxUrbFree(pProxyDev, pUrbLnx);
        }
        pUrb->Dev.pvPrivate = NULL;

        /* some adjustments for message transfers. */
        if (pUrb->enmType == VUSBXFERTYPE_MSG)
        {
            pUrb->cbData += sizeof(VUSBSETUP);
            usbProxyLinuxUrbSwapSetup((PVUSBSETUP)pUrb->abData);
        }
    }
    else
    {
        usbProxyLinuxUrbUnlinkInFlight(pDevLnx, pUrbLnx);
        usbProxyLinuxUrbFree(pProxyDev, pUrbLnx);
        pUrb = NULL;
    }

    LogFlow(("usbProxyLinuxUrbReap: pProxyDev=%s returns %p\n", usbProxyGetName(pProxyDev), pUrb));
    return pUrb;
}


/**
 * Cancels the URB.
 * The URB requires reaping, so we don't change its state.
 */
static DECLCALLBACK(int) usbProxyLinuxUrbCancel(PUSBPROXYDEV pProxyDev, PVUSBURB pUrb)
{
    int rc = VINF_SUCCESS;
    PUSBPROXYURBLNX pUrbLnx = (PUSBPROXYURBLNX)pUrb->Dev.pvPrivate;
    if (pUrbLnx->pSplitHead)
    {
        /* split */
        Assert(pUrbLnx == pUrbLnx->pSplitHead);
        PUSBPROXYURBLNX pCur;
        for (pCur = pUrbLnx; pCur; pCur = pCur->pSplitNext)
        {
            if (pCur->fSplitElementReaped)
                continue;
            if (    !usbProxyLinuxDoIoCtl(pProxyDev, USBDEVFS_DISCARDURB, &pCur->KUrb, true, UINT32_MAX)
                ||  errno == ENOENT)
                continue;
            if (errno == ENODEV)
                break;
            /** @todo Think about how to handle errors wrt. to the status code. */
            Log(("usb-linux: Discard URB %p failed, errno=%d. pProxyDev=%s!!! (split)\n",
                 pUrb, errno, usbProxyGetName(pProxyDev)));
        }
    }
    else
    {
        /* unsplit */
        if (    usbProxyLinuxDoIoCtl(pProxyDev, USBDEVFS_DISCARDURB, &pUrbLnx->KUrb, true, UINT32_MAX)
            &&  errno != ENODEV /* deal with elsewhere. */
            &&  errno != ENOENT)
        {
            Log(("usb-linux: Discard URB %p failed, errno=%d. pProxyDev=%s!!!\n",
                 pUrb, errno, usbProxyGetName(pProxyDev)));
            rc = RTErrConvertFromErrno(errno);
        }
    }

    return rc;
}


static DECLCALLBACK(int) usbProxyLinuxWakeup(PUSBPROXYDEV pProxyDev)
{
    PUSBPROXYDEVLNX pDevLnx = USBPROXYDEV_2_DATA(pProxyDev, PUSBPROXYDEVLNX);
    size_t cbIgnored;

    LogFlowFunc(("pProxyDev=%p\n", pProxyDev));

    return RTPipeWrite(pDevLnx->hPipeWakeupW, "", 1, &cbIgnored);
}

/**
 * The Linux USB Proxy Backend.
 */
const USBPROXYBACK g_USBProxyDeviceHost =
{
    /* pszName */
    "host",
    /* cbBackend */
    sizeof(USBPROXYDEVLNX),
    usbProxyLinuxOpen,
    usbProxyLinuxInit,
    usbProxyLinuxClose,
    usbProxyLinuxReset,
    usbProxyLinuxSetConfig,
    usbProxyLinuxClaimInterface,
    usbProxyLinuxReleaseInterface,
    usbProxyLinuxSetInterface,
    usbProxyLinuxClearHaltedEp,
    usbProxyLinuxUrbQueue,
    usbProxyLinuxUrbCancel,
    usbProxyLinuxUrbReap,
    usbProxyLinuxWakeup,
    0
};


/*
 * Local Variables:
 *  mode: c
 *  c-file-style: "bsd"
 *  c-basic-offset: 4
 * End:
 */

