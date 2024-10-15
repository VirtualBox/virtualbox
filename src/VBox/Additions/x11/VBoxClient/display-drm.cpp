/* $Id: display-drm.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Guest Additions - VMSVGA guest screen resize service.
 *
 * A user space daemon which communicates with VirtualBox host interface
 * and performs VMSVGA-specific guest screen resize and communicates with
 * Desktop Environment helper daemon over IPC.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

/** @page pg_vboxdrmcliet    VBoxDRMClient - The VMSVGA Guest Screen Resize Service
 *
 * The VMSVGA Guest Screen Resize Service is a service which communicates with a
 * guest VMSVGA driver and triggers it to perform screen resize on a guest side.
 *
 * This service supposed to be started on early boot. On start it will try to find
 * compatible VMSVGA graphics card and terminate immediately if not found.
 * VMSVGA functionality implemented here is only supported starting from vmgfx
 * driver version 2.10 which was introduced in Linux kernel 4.6. When compatible
 * graphics card is found, service will start a worker loop in order to receive screen
 * update data from host and apply it to local DRM stack.
 *
 * In addition, it will start a local IPC server in order to communicate with Desktop
 * Environment specific service(s). Currently, it will propagate to IPC client information regarding to
 * which display should be set as primary on Desktop Environment level. As well as
 * receive screen layout change events obtained on Desktop Environment level and send it
 * back to host, so host and guest will have the same screen layout representation.
 *
 * By default, access to IPC server socket is granted to all users. It can be restricted to
 * only root and users from group 'vboxdrmipc' if '/VirtualBox/GuestAdd/DRMIpcRestricted' guest
 * property is set and READ-ONLY for guest. User group 'vboxdrmipc' is created during Guest
 * Additions installation. If this group is removed (or not found due to any reason) prior to
 * service start, access to IPC server socket will be granted to root only regardless
 * if '/VirtualBox/GuestAdd/DRMIpcRestricted' guest property is set or not. If guest property
 * is set, but is not READ-ONLY for guest, property is ignored and IPC socket access is granted
 * to all users.
 *
 * Logging is implemented in a way that errors are always printed out, VBClLogVerbose(1) and
 * VBClLogVerbose(2) are used for debugging purposes. Verbosity level 1 is for messages related
 * to service itself (excluding IPC), level 2 is for IPC communication debugging. In order to see
 * logging on a host side it is enough to do:
 *
 *     echo 1 > /sys/module/vboxguest/parameters/r3_log_to_host.
 *
 *
 * Service is running the following threads:
 *
 * DrmResizeThread - this thread listens for display layout update events from host.
 *     Once event is received, it either injects new screen layout data into DRM stack,
 *     and/or asks IPC client(s) to set primary display. This thread is accessing IPC
 *     client connection list when it needs to sent new primary display data to all the
 *     connected clients.
 *
 * DrmIpcSRV - this thread is a main loop for IPC server. It accepts new connection(s),
 *     authenticates it and starts new client thread IpcCLT-XXX for processing client
 *     requests. This thread is accessing IPC client connection list by adding a new
 *     connection data into it.
 *
 * IpcCLT-%u - this thread processes all the client data. Suffix '-%u' in thread name is PID
 *     of a remote client process. Typical name for client thread would be IpcCLT-1234. This
 *     thread is accessing IPC client connection list when it removes connection data from it
 *     when actual IPC connection is closed. Due to IPRT thread name limitation, actual thread
 *     name will be cropped by 15 characters.
 *
 *
 * The following locks are utilized:
 *
 * #g_ipcClientConnectionsListCritSect - protects access to list of IPC client connections.
 *     It is used by each thread - DrmResizeThread, DrmIpcSRV and IpcCLT-XXX.
 *
 * #g_monitorPositionsCritSect - protects access to display layout data cache and vmwgfx driver
 *      handle, serializes access to host interface and vmwgfx driver handle between
 *      DrmResizeThread and IpcCLT-%u.
 */

#include "VBoxClient.h"
#include "display-ipc.h"

#include <VBox/VBoxGuestLib.h>
#include <VBox/HostServices/GuestPropertySvc.h>

#include <iprt/getopt.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/thread.h>
#include <iprt/asm.h>
#include <iprt/localipc.h>
#include <iprt/timer.h>

#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <signal.h>
#include <grp.h>
#include <errno.h>

#ifdef RT_OS_LINUX
# include <sys/ioctl.h>
#else  /* Solaris and BSDs, in case they ever adopt the DRM driver. */
# include <sys/ioccom.h>
#endif

/** Ioctl command to query vmwgfx version information. */
#define DRM_IOCTL_VERSION               _IOWR('d', 0x00, struct DRMVERSION)
/** Ioctl command to set new screen layout. */
#define DRM_IOCTL_VMW_UPDATE_LAYOUT     _IOW('d', 0x40 + 20, struct DRMVMWUPDATELAYOUT)
/** A driver name which identifies VMWare driver. */
#define DRM_DRIVER_NAME "vmwgfx"
/** VMWare driver compatible version number. On previous versions resizing does not seem work. */
#define DRM_DRIVER_VERSION_MAJOR_MIN    (2)
#define DRM_DRIVER_VERSION_MINOR_MIN    (10)

/** VMWare char device driver minor numbers range. */
#define VMW_CONTROL_DEVICE_MINOR_START  (64)
#define VMW_RENDER_DEVICE_MINOR_START   (128)
#define VMW_RENDER_DEVICE_MINOR_END     (192)

/** Name of DRM resize thread. */
#define DRM_RESIZE_THREAD_NAME          "DrmResizeThread"

/** Name of DRM IPC server thread. */
#define DRM_IPC_SERVER_THREAD_NAME      "DrmIpcSRV"
/** Maximum length of thread name. */
#define DRM_IPC_THREAD_NAME_MAX         (16)
/** Name pattern of DRM IPC client thread. */
#define DRM_IPC_CLIENT_THREAD_NAME_PTR  "IpcCLT-%u"
/** Maximum number of simultaneous IPC client connections. */
#define DRM_IPC_SERVER_CONNECTIONS_MAX  (16)
/** Interval between attempts to send resize events to DRM stack. */
#define DRM_RESIZE_INTERVAL_MS          (250)

/** IPC client connections counter. */
static volatile uint32_t g_cDrmIpcConnections = 0;
/* A flag which indicates whether access to IPC socket should be restricted.
 * This flag caches '/VirtualBox/GuestAdd/DRMIpcRestricted' guest property
 * in order to prevent its retrieving from the host side each time a new IPC
 * client connects to server. This flag is updated each time when property is
 * changed on the host side. */
static volatile bool g_fDrmIpcRestricted;

/** Global handle to vmwgfx file descriptor (protected by #g_monitorPositionsCritSect). */
static RTFILE g_hDevice = NIL_RTFILE;

/** DRM version structure. */
struct DRMVERSION
{
    int cMajor;
    int cMinor;
    int cPatchLevel;
    size_t cbName;
    char *pszName;
    size_t cbDate;
    char *pszDate;
    size_t cbDescription;
    char *pszDescription;
};
AssertCompileSize(struct DRMVERSION, 8 + 7 * sizeof(void *));

/** Preferred screen layout information for DRM_VMW_UPDATE_LAYOUT IoCtl.  The
 *  rects argument is a cast pointer to an array of drm_vmw_rect. */
struct DRMVMWUPDATELAYOUT
{
    uint32_t cOutputs;
    uint32_t u32Pad;
    uint64_t ptrRects;
};
AssertCompileSize(struct DRMVMWUPDATELAYOUT, 16);

/** A node of IPC client connections list. */
typedef struct VBOX_DRMIPC_CLIENT_CONNECTION_LIST_NODE
{
    /** The list node. */
    RTLISTNODE      Node;
    /** List node payload. */
    PVBOX_DRMIPC_CLIENT   pClient;
} VBOX_DRMIPC_CLIENT_CONNECTION_LIST_NODE;

/* Pointer to VBOX_DRMIPC_CLIENT_CONNECTION_LIST_NODE. */
typedef VBOX_DRMIPC_CLIENT_CONNECTION_LIST_NODE *PVBOX_DRMIPC_CLIENT_CONNECTION_LIST_NODE;

/** IPC client connections list.  */
static VBOX_DRMIPC_CLIENT_CONNECTION_LIST_NODE g_ipcClientConnectionsList;

/** IPC client connections list critical section. */
static RTCRITSECT g_ipcClientConnectionsListCritSect;

/** Critical section used for reporting monitors position back to host. */
static RTCRITSECT g_monitorPositionsCritSect;

/** Resize events suppression.
 *
 *  We tend to suppress incoming resizing events from host. Instead of
 *  applying them immediately to Linux DRM stack, we keep them and apply
 *  in recurring timer callback. This structure collects all the necessary
 * components to implement events suppression. */
static struct VBOX_DRM_SUPPRESSION
{
    /** Protection for concurrent access from host events loop and
     *  suppression timer contexts. */
    RTCRITSECT critSect;

    /** Recurring timer which is used in order to push resize events
     *  into DRM stack. */
    PRTTIMER pResizeSuppressionTimer;

    /** Resize parameters from the last host event: coordinates. */
    VMMDevDisplayDef aDisplaysIn[VBOX_DRMIPC_MONITORS_MAX];

    /** Resize parameters from the last host event: number of displays. */
    uint32_t cDisplaysIn;

    /** Flag to indicate that new events were suppressed since last
     *  time when we sent data to DRM stack. */
    bool fHaveSuppressedEvents;
} g_vboxDrmSuppression;

/** Counter of how often our daemon has been re-spawned. */
unsigned g_cRespawn = 0;
/** Logging verbosity level. */
unsigned g_cVerbosity = 0;

/** Path to the PID file. */
static const char *g_pszPidFile = "/var/run/VBoxDRMClient";

/** Global flag which is triggered when service requested to shutdown. */
static bool volatile g_fShutdown;

/**
 * Go over all existing IPC client connection and put set-primary-screen request
 * data into TX queue of each of them .
 *
 * @return  IPRT status code.
 * @param   u32PrimaryDisplay   Primary display ID.
 */
static int vbDrmIpcBroadcastPrimaryDisplay(uint32_t u32PrimaryDisplay);

/**
 * Attempts to open DRM device by given path and check if it is
 * capable for screen resize.
 *
 * @return  DRM device handle on success, NIL_RTFILE otherwise.
 * @param   szPathPattern       Path name pattern to the DRM device.
 * @param   uInstance           Driver / device instance.
 */
static RTFILE vbDrmTryDevice(const char *szPathPattern, uint8_t uInstance)
{
    int rc = VERR_NOT_FOUND;
    char szPath[PATH_MAX];
    struct DRMVERSION vmwgfxVersion;
    RTFILE hDevice = NIL_RTFILE;

    RT_ZERO(szPath);
    RT_ZERO(vmwgfxVersion);

    rc = RTStrPrintf(szPath, sizeof(szPath), szPathPattern, uInstance);
    if (RT_SUCCESS(rc))
    {
        rc = RTFileOpen(&hDevice, szPath, RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
        if (RT_SUCCESS(rc))
        {
            char szVmwgfxDriverName[sizeof(DRM_DRIVER_NAME)];
            RT_ZERO(szVmwgfxDriverName);

            vmwgfxVersion.cbName = sizeof(szVmwgfxDriverName);
            vmwgfxVersion.pszName = szVmwgfxDriverName;

            /* Query driver version information and check if it can be used for screen resizing. */
            rc = RTFileIoCtl(hDevice, DRM_IOCTL_VERSION, &vmwgfxVersion, sizeof(vmwgfxVersion), NULL);
            if (   RT_SUCCESS(rc)
                && strncmp(szVmwgfxDriverName, DRM_DRIVER_NAME, sizeof(DRM_DRIVER_NAME) - 1) == 0
                && (   vmwgfxVersion.cMajor > DRM_DRIVER_VERSION_MAJOR_MIN
                    || (   vmwgfxVersion.cMajor == DRM_DRIVER_VERSION_MAJOR_MIN
                        && vmwgfxVersion.cMinor >= DRM_DRIVER_VERSION_MINOR_MIN)))
            {
                VBClLogInfo("found compatible device: %s\n", szPath);
            }
            else
            {
                RTFileClose(hDevice);
                hDevice = NIL_RTFILE;
                rc = VERR_NOT_FOUND;
            }
        }
    }
    else
    {
        VBClLogError("unable to construct path to DRM device: %Rrc\n", rc);
    }

    return RT_SUCCESS(rc) ? hDevice : NIL_RTFILE;
}

/**
 * Attempts to find and open DRM device to be used for screen resize.
 *
 * @return  DRM device handle on success, NIL_RTFILE otherwise.
 */
static RTFILE vbDrmOpenVmwgfx(void)
{
    /* Control devices for drm graphics driver control devices go from
     * controlD64 to controlD127.  Render node devices go from renderD128
     * to renderD192. The driver takes resize hints via the control device
     * on pre-4.10 (???) kernels and on the render device on newer ones.
     * At first, try to find control device and render one if not found.
     */
    uint8_t i;
    RTFILE hDevice = NIL_RTFILE;

    /* Lookup control device. */
    for (i = VMW_CONTROL_DEVICE_MINOR_START; i < VMW_RENDER_DEVICE_MINOR_START; i++)
    {
        hDevice = vbDrmTryDevice("/dev/dri/controlD%u", i);
        if (hDevice != NIL_RTFILE)
            return hDevice;
    }

    /* Lookup render device. */
    for (i = VMW_RENDER_DEVICE_MINOR_START; i <= VMW_RENDER_DEVICE_MINOR_END; i++)
    {
        hDevice = vbDrmTryDevice("/dev/dri/renderD%u", i);
        if (hDevice != NIL_RTFILE)
            return hDevice;
    }

    VBClLogError("unable to find DRM device\n");

    return hDevice;
}

/**
 * This function converts input monitors layout array passed from DevVMM
 * into monitors layout array to be passed to DRM stack. Last validation
 * request is cached.
 *
 * @return  VINF_SUCCESS on success, VERR_DUPLICATE if monitors layout was not changed, IPRT error code otherwise.
 * @param   aDisplaysIn         Input displays array.
 * @param   cDisplaysIn         Number of elements in input displays array.
 * @param   aDisplaysOut        Output displays array.
 * @param   cDisplaysOutMax     Number of elements in output displays array.
 * @param   pu32PrimaryDisplay  ID of a display which marked as primary.
 * @param   pcActualDisplays    Number of displays to report to DRM stack (number of enabled displays).
 * @param   fPartialLayout      Whether aDisplaysIn array contains complete display layout information or not.
 *                              When layout is reported by Desktop Environment helper, aDisplaysIn does not have
 *                              idDisplay, fDisplayFlags and cBitsPerPixel data (guest has no info about them).
 */
static int vbDrmValidateLayout(VMMDevDisplayDef *aDisplaysIn, uint32_t cDisplaysIn,
                               struct VBOX_DRMIPC_VMWRECT *aDisplaysOut, uint32_t *pu32PrimaryDisplay,
                               uint32_t cDisplaysOutMax, uint32_t *pcActualDisplays, bool fPartialLayout)
{
    /* This array is a cache of what was received from DevVMM so far.
     * DevVMM may send to us partial information bout scree layout. This
     * cache remembers entire picture. */
    static struct VMMDevDisplayDef aVmMonitorsCache[VBOX_DRMIPC_MONITORS_MAX];
    /* Number of valid (enabled) displays in output array. */
    uint32_t cDisplaysOut = 0;
    /* Flag indicates that current layout cache is consistent and can be passed to DRM stack. */
    bool fValid = true;

    /* Make sure input array fits cache size. */
    if (cDisplaysIn > VBOX_DRMIPC_MONITORS_MAX)
    {
        VBClLogError("unable to validate screen layout: input (%u) array does not fit to cache size (%u)\n",
                         cDisplaysIn, VBOX_DRMIPC_MONITORS_MAX);
        return VERR_INVALID_PARAMETER;
    }

    /* Make sure there is enough space in output array. */
    if (cDisplaysIn > cDisplaysOutMax)
    {
        VBClLogError("unable to validate screen layout: input array (%u) is bigger than output one (%u)\n",
                         cDisplaysIn, cDisplaysOut);
        return VERR_INVALID_PARAMETER;
    }

    /* Make sure input and output arrays are of non-zero size. */
    if (!(cDisplaysIn > 0 && cDisplaysOutMax > 0))
    {
        VBClLogError("unable to validate screen layout: invalid size of either input (%u) or output display array\n",
                         cDisplaysIn, cDisplaysOutMax);
        return VERR_INVALID_PARAMETER;
    }

    /* Update cache. */
    for (uint32_t i = 0; i < cDisplaysIn; i++)
    {
        uint32_t idDisplay = !fPartialLayout ? aDisplaysIn[i].idDisplay : i;
        if (idDisplay < VBOX_DRMIPC_MONITORS_MAX)
        {
            if (!fPartialLayout)
            {
                aVmMonitorsCache[idDisplay].idDisplay = idDisplay;
                aVmMonitorsCache[idDisplay].fDisplayFlags = aDisplaysIn[i].fDisplayFlags;
                aVmMonitorsCache[idDisplay].cBitsPerPixel = aDisplaysIn[i].cBitsPerPixel;
            }

            aVmMonitorsCache[idDisplay].cx = aDisplaysIn[i].cx;
            aVmMonitorsCache[idDisplay].cy = aDisplaysIn[i].cy;
            aVmMonitorsCache[idDisplay].xOrigin = aDisplaysIn[i].xOrigin;
            aVmMonitorsCache[idDisplay].yOrigin = aDisplaysIn[i].yOrigin;
        }
        else
        {
            VBClLogError("received display ID (0x%x, position %u) is invalid\n", idDisplay, i);
            /* If monitor configuration cannot be placed into cache, consider entire cache is invalid. */
            fValid = false;
        }
    }

    /* Now, go though complete cache and check if it is valid. */
    for (uint32_t i = 0; i < VBOX_DRMIPC_MONITORS_MAX; i++)
    {
        if (i == 0)
        {
            if (aVmMonitorsCache[i].fDisplayFlags & VMMDEV_DISPLAY_DISABLED)
            {
                VBClLogError("unable to validate screen layout: first monitor is not allowed to be disabled\n");
                fValid = false;
            }
            else
                cDisplaysOut++;
        }
        else
        {
            /* Check if there is no hole in between monitors (i.e., if current monitor is enabled, but previous one does not). */
            if (   !(aVmMonitorsCache[i].fDisplayFlags & VMMDEV_DISPLAY_DISABLED)
                && aVmMonitorsCache[i - 1].fDisplayFlags & VMMDEV_DISPLAY_DISABLED)
            {
                VBClLogError("unable to validate screen layout: there is a hole in displays layout config, "
                                 "monitor (%u) is ENABLED while (%u) does not\n", i, i - 1);
                fValid = false;
            }
            else
            {
                /* Always align screens since unaligned layout will result in disaster. */
                aVmMonitorsCache[i].xOrigin = aVmMonitorsCache[i - 1].xOrigin + aVmMonitorsCache[i - 1].cx;
                aVmMonitorsCache[i].yOrigin = aVmMonitorsCache[i - 1].yOrigin;

                /* Only count enabled monitors. */
                if (!(aVmMonitorsCache[i].fDisplayFlags & VMMDEV_DISPLAY_DISABLED))
                    cDisplaysOut++;
            }
        }
    }

    /* Copy out layout data. */
    if (fValid)
    {
        /* Start with invalid display ID. */
        uint32_t u32PrimaryDisplay = VBOX_DRMIPC_MONITORS_MAX;

        for (uint32_t i = 0; i < cDisplaysOut; i++)
        {
            aDisplaysOut[i].x = aVmMonitorsCache[i].xOrigin;
            aDisplaysOut[i].y = aVmMonitorsCache[i].yOrigin;
            aDisplaysOut[i].w = aVmMonitorsCache[i].cx;
            aDisplaysOut[i].h = aVmMonitorsCache[i].cy;

            if (aVmMonitorsCache[i].fDisplayFlags & VMMDEV_DISPLAY_PRIMARY)
            {
                /* Make sure display layout has only one primary display
                 * set (for display 0, host side sets primary flag, so exclude it). */
                Assert(u32PrimaryDisplay == 0 || u32PrimaryDisplay == VBOX_DRMIPC_MONITORS_MAX);
                u32PrimaryDisplay = i;
            }

            VBClLogVerbose(1, "update monitor %u parameters: %dx%d, (%d, %d)\n",
                           i, aDisplaysOut[i].w, aDisplaysOut[i].h, aDisplaysOut[i].x, aDisplaysOut[i].y);
        }

        *pu32PrimaryDisplay = u32PrimaryDisplay;
        *pcActualDisplays = cDisplaysOut;
    }

    return (fValid && cDisplaysOut > 0) ? VINF_SUCCESS : VERR_INVALID_PARAMETER;
}

/**
 * This function sends screen layout data to DRM stack.
 *
 * Helper function for vbDrmPushScreenLayout(). Should be called
 * under g_monitorPositionsCritSect lock.
 *
 * @return  VINF_SUCCESS on success, IPRT error code otherwise.
 * @param   hDevice     Handle to opened DRM device.
 * @param   paRects     Array of screen configuration data.
 * @param   cRects      Number of elements in screen configuration array.
 */
static int vbDrmSendHints(RTFILE hDevice, struct VBOX_DRMIPC_VMWRECT *paRects, uint32_t cRects)
{
    int rc = 0;
    uid_t curuid;

    /* Store real user id. */
    curuid = getuid();

    /* Change effective user id. */
    if (setreuid(0, 0) == 0)
    {
        struct DRMVMWUPDATELAYOUT ioctlLayout;

        RT_ZERO(ioctlLayout);
        ioctlLayout.cOutputs = cRects;
        ioctlLayout.ptrRects = (uint64_t)paRects;

        rc = RTFileIoCtl(hDevice, DRM_IOCTL_VMW_UPDATE_LAYOUT,
                         &ioctlLayout, sizeof(ioctlLayout), NULL);

        if (setreuid(curuid, 0) != 0)
        {
            VBClLogError("reset of setreuid failed after drm ioctl");
            rc = VERR_ACCESS_DENIED;
        }
    }
    else
    {
        VBClLogError("setreuid failed during drm ioctl\n");
        rc = VERR_ACCESS_DENIED;
    }

    return rc;
}

/**
 * This function converts vmwgfx monitors layout data into an array of monitor offsets
 * and sends it back to the host in order to ensure that host and guest have the same
 * monitors layout representation.
 *
 * @return  IPRT status code.
 * @param   cDisplays   Number of displays (elements in pDisplays).
 * @param   pDisplays   Displays parameters as it was sent to vmwgfx driver.
 */
static int drmSendMonitorPositions(uint32_t cDisplays, struct VBOX_DRMIPC_VMWRECT *pDisplays)
{
    static RTPOINT aPositions[VBOX_DRMIPC_MONITORS_MAX];

    if (!pDisplays || !cDisplays || cDisplays > VBOX_DRMIPC_MONITORS_MAX)
    {
        return VERR_INVALID_PARAMETER;
    }

    /* Prepare monitor offsets list to be sent to the host. */
    for (uint32_t i = 0; i < cDisplays; i++)
    {
        aPositions[i].x = pDisplays[i].x;
        aPositions[i].y = pDisplays[i].y;
    }

    return VbglR3SeamlessSendMonitorPositions(cDisplays, aPositions);
}

/**
 * Validate and apply screen layout data.
 *
 * @return  IPRT status code.
 * @param   aDisplaysIn     An array with screen layout data.
 * @param   cDisplaysIn     Number of elements in aDisplaysIn.
 * @param   fPartialLayout  Whether aDisplaysIn array contains complete display layout information or not.
 *                          When layout is reported by Desktop Environment helper, aDisplaysIn does not have
 *                          idDisplay, fDisplayFlags and cBitsPerPixel data (guest has no info about them).
 * @param   fApply          Whether to apply provided display layout data to the DRM stack or send display offsets only.
 */
static int vbDrmPushScreenLayout(VMMDevDisplayDef *aDisplaysIn, uint32_t cDisplaysIn, bool fPartialLayout, bool fApply)
{
    int rc;

    struct VBOX_DRMIPC_VMWRECT aDisplaysOut[VBOX_DRMIPC_MONITORS_MAX];
    uint32_t cDisplaysOut = 0;

    uint32_t u32PrimaryDisplay = VBOX_DRMIPC_MONITORS_MAX;

    rc = RTCritSectEnter(&g_monitorPositionsCritSect);
    if (RT_FAILURE(rc))
    {
        VBClLogError("unable to lock monitor data cache, rc=%Rrc\n", rc);
        return rc;
    }

    static uint32_t u32PrimaryDisplayLast = VBOX_DRMIPC_MONITORS_MAX;

    RT_ZERO(aDisplaysOut);

    /* Validate displays layout and push it to DRM stack if valid. */
    rc = vbDrmValidateLayout(aDisplaysIn, cDisplaysIn, aDisplaysOut, &u32PrimaryDisplay,
                             sizeof(aDisplaysOut), &cDisplaysOut, fPartialLayout);
    if (RT_SUCCESS(rc))
    {
        if (fApply)
        {
            rc = vbDrmSendHints(g_hDevice, aDisplaysOut, cDisplaysOut);
            VBClLogInfo("push screen layout data of %u display(s) to DRM stack, fPartialLayout=%RTbool, rc=%Rrc\n",
                        cDisplaysOut, fPartialLayout, rc);
        }

        /* In addition, notify host that configuration was successfully applied to the guest vmwgfx driver. */
        if (RT_SUCCESS(rc))
        {
            rc = drmSendMonitorPositions(cDisplaysOut, aDisplaysOut);
            if (RT_FAILURE(rc))
                VBClLogError("cannot send host notification: %Rrc\n", rc);

            /* If information about primary display is present in display layout, send it to DE over IPC. */
            if (u32PrimaryDisplay != VBOX_DRMIPC_MONITORS_MAX
                && u32PrimaryDisplayLast != u32PrimaryDisplay)
            {
                rc = vbDrmIpcBroadcastPrimaryDisplay(u32PrimaryDisplay);

                /* Cache last value in order to avoid sending duplicate data over IPC. */
                u32PrimaryDisplayLast = u32PrimaryDisplay;

                VBClLogVerbose(2, "DE was notified that display %u is now primary, rc=%Rrc\n", u32PrimaryDisplay, rc);
            }
            else
                VBClLogVerbose(2, "do not notify DE second time that display %u is now primary, rc=%Rrc\n", u32PrimaryDisplay, rc);
        }
    }
    else if (rc == VERR_DUPLICATE)
        VBClLogVerbose(2, "do not notify DRM stack about monitors layout change twice, rc=%Rrc\n", rc);
    else
        VBClLogError("displays layout is invalid, will not notify guest driver, rc=%Rrc\n", rc);

    int rc2 = RTCritSectLeave(&g_monitorPositionsCritSect);
    if (RT_FAILURE(rc2))
        VBClLogError("unable to unlock monitor data cache, rc=%Rrc\n", rc);

    return rc;
}

/**
 * Suppress incoming resize events from host.
 *
 * This function will put last resize event data into suppression
 * cache. This data will later be picked up by timer callback and
 * pushed into DRM stack.
 *
 * @returns IPRT status code.
 * @param   paDisplaysIn    Display resize data.
 * @param   cDisplaysIn     Number of displays.
 */
static int vbDrmResizeSuppress(VMMDevDisplayDef *paDisplaysIn, uint32_t cDisplaysIn)
{
    int rc;

    Assert((sizeof(VMMDevDisplayDef) * cDisplaysIn) <= sizeof(g_vboxDrmSuppression.aDisplaysIn));

    rc = RTCritSectEnter(&g_vboxDrmSuppression.critSect);
    if (RT_FAILURE(rc))
    {
        VBClLogError("unable to lock suppression data cache, rc=%Rrc\n", rc);
        return rc;
    }

    memcpy(g_vboxDrmSuppression.aDisplaysIn, paDisplaysIn, sizeof(VMMDevDisplayDef) * cDisplaysIn);
    g_vboxDrmSuppression.cDisplaysIn = cDisplaysIn;
    g_vboxDrmSuppression.fHaveSuppressedEvents = true;

    int rc2 = RTCritSectLeave(&g_vboxDrmSuppression.critSect);
    if (RT_FAILURE(rc2))
        VBClLogError("unable to unlock suppression data cache, rc=%Rrc\n", rc);

    if (RT_SUCCESS(rc))
        rc = rc2;

    return rc;
}

/** Recurring timer callback to push display resize events into DRM stack.
 *
 * @param   pTimer  IPRT timer structure (unused).
 * @param   pvUser  User data (unused).
 * @param   iTick   Timer tick data (unused).
 */
static DECLCALLBACK(void) vbDrmResizeSuppressionTimerCb(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    int rc;

    RT_NOREF(pTimer, pvUser, iTick);

    rc = RTCritSectEnter(&g_vboxDrmSuppression.critSect);
    if (RT_FAILURE(rc))
    {
        VBClLogError("unable to lock suppression data cache, rc=%Rrc\n", rc);
        return;
    }

    if (g_vboxDrmSuppression.fHaveSuppressedEvents)
    {
        rc = vbDrmPushScreenLayout(g_vboxDrmSuppression.aDisplaysIn, g_vboxDrmSuppression.cDisplaysIn, false, true);
        if (RT_FAILURE(rc))
            VBClLogError("Failed to push display change as requested by host, rc=%Rrc\n", rc);

        g_vboxDrmSuppression.fHaveSuppressedEvents = false;
    }

    int rc2 = RTCritSectLeave(&g_vboxDrmSuppression.critSect);
    if (RT_FAILURE(rc2))
        VBClLogError("unable to unlock suppression data cache, rc=%Rrc\n", rc);
}

/**
 * Initialize DRM events suppression data.
 *
 * @returns IPRT status code.
 * @param   pSuppression    Pointer to suppression data.
 */
static int vbDrmSuppressionInit(struct VBOX_DRM_SUPPRESSION *pSuppression)
{
    int rc;

    RT_BZERO(pSuppression, sizeof(*pSuppression));

    rc = RTCritSectInit(&pSuppression->critSect);
    if (RT_SUCCESS(rc))
    {
        rc = RTTimerCreate(&pSuppression->pResizeSuppressionTimer, DRM_RESIZE_INTERVAL_MS, vbDrmResizeSuppressionTimerCb, NULL);
        if (RT_SUCCESS(rc))
            VBClLogInfo("starting deferred resize events delivery\n");
        else
            VBClLogError("unable to enable deferred resize events delivery: no timer, rc=%Rrc\n", rc);
    }
    else
        VBClLogError("unable to enable deferred resize events delivery: no critsect, rc=%Rrc\n", rc);

    return rc;
}

/**
 * Destroy DRM events suppression data.
 *
 * @returns IPRT status code.
 * @param   pSuppression    Pointer to suppression data.
 */
static int vbDrmSuppressionDestroy(struct VBOX_DRM_SUPPRESSION *pSuppression)
{
    int rc;

    rc = RTTimerDestroy(pSuppression->pResizeSuppressionTimer);
    if (RT_SUCCESS(rc))
    {
        rc = RTCritSectDelete(&pSuppression->critSect);
        if (RT_FAILURE(rc))
            VBClLogError("unable to destroy suppression critsect, rc=%Rrc\n", rc);
    }
    else
        VBClLogError("unable to destroy suppression timer, rc=%Rrc\n", rc);

    return rc;
}

/** Worker thread for resize task. */
static DECLCALLBACK(int) vbDrmResizeWorker(RTTHREAD ThreadSelf, void *pvUser)
{
    int rc = VERR_GENERAL_FAILURE;

    RT_NOREF(ThreadSelf);
    RT_NOREF(pvUser);

    for (;;)
    {
        /* Do not acknowledge the first event we query for to pick up old events,
         * e.g. from before a guest reboot. */
        bool fAck = false;

        uint32_t events;

        VMMDevDisplayDef aDisplaysIn[VBOX_DRMIPC_MONITORS_MAX];
        uint32_t cDisplaysIn = 0;

        RT_ZERO(aDisplaysIn);

        /* Query the first size without waiting.  This lets us e.g. pick up
         * the last event before a guest reboot when we start again after. */
        rc = VbglR3GetDisplayChangeRequestMulti(VBOX_DRMIPC_MONITORS_MAX, &cDisplaysIn, aDisplaysIn, fAck);
        fAck = true;
        if (RT_SUCCESS(rc))
        {
            rc = vbDrmResizeSuppress(aDisplaysIn, cDisplaysIn);
            if (RT_FAILURE(rc))
                VBClLogError("Failed to push display change as requested by host, rc=%Rrc\n", rc);
        }
        else
            VBClLogError("Failed to get display change request, rc=%Rrc\n", rc);

        do
        {
            rc = VbglR3WaitEvent(VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST, VBOX_DRMIPC_RX_TIMEOUT_MS, &events);
        } while ((rc == VERR_TIMEOUT || rc == VERR_INTERRUPTED) && !ASMAtomicReadBool(&g_fShutdown));

        if (ASMAtomicReadBool(&g_fShutdown))
        {
            VBClLogInfo("exiting resize thread: shutdown requested\n");
            /* This is a case when we should return positive status. */
            rc = (rc == VERR_TIMEOUT) ? VINF_SUCCESS : rc;
            break;
        }
        else if (RT_FAILURE(rc))
            VBClLogFatalError("VBoxDRMClient: resize thread: failure waiting for event, rc=%Rrc\n", rc);
    }

    return rc;
}

/**
 * Go over all existing IPC client connection and put set-primary-screen request
 * data into TX queue of each of them .
 *
 * @return  IPRT status code.
 * @param   u32PrimaryDisplay   Primary display ID.
 */
static int vbDrmIpcBroadcastPrimaryDisplay(uint32_t u32PrimaryDisplay)
{
    int rc;

    rc = RTCritSectEnter(&g_ipcClientConnectionsListCritSect);
    if (RT_SUCCESS(rc))
    {
        if (!RTListIsEmpty(&g_ipcClientConnectionsList.Node))
        {
            PVBOX_DRMIPC_CLIENT_CONNECTION_LIST_NODE pEntry;
            RTListForEach(&g_ipcClientConnectionsList.Node, pEntry, VBOX_DRMIPC_CLIENT_CONNECTION_LIST_NODE, Node)
            {
                AssertReturn(pEntry, VERR_INVALID_PARAMETER);
                AssertReturn(pEntry->pClient, VERR_INVALID_PARAMETER);
                AssertReturn(pEntry->pClient->hThread, VERR_INVALID_PARAMETER);

                rc = vbDrmIpcSetPrimaryDisplay(pEntry->pClient, u32PrimaryDisplay);

                VBClLogInfo("thread %s notified IPC Client that display %u is now primary, rc=%Rrc\n",
                                RTThreadGetName(pEntry->pClient->hThread), u32PrimaryDisplay, rc);
            }
        }

        int rc2 = RTCritSectLeave(&g_ipcClientConnectionsListCritSect);
        if (RT_FAILURE(rc2))
            VBClLogError("notify DE: unable to leave critical section, rc=%Rrc\n", rc2);
    }
    else
        VBClLogError("notify DE: unable to enter critical section, rc=%Rrc\n", rc);

    return rc;
}

/**
 * Main loop for IPC client connection handling.
 *
 * @return  IPRT status code.
 * @param   pClient     Pointer to IPC client data.
 */
static int vbDrmIpcConnectionProc(PVBOX_DRMIPC_CLIENT pClient)
{
    int rc = VERR_GENERAL_FAILURE;

    AssertReturn(pClient, VERR_INVALID_PARAMETER);

    /* This loop handles incoming messages. */
    for (;;)
    {
        rc = vbDrmIpcConnectionHandler(pClient);

        /* Try to detect if we should shutdown as early as we can. */
        if (ASMAtomicReadBool(&g_fShutdown))
            break;

        /* Normal case. No data received within short interval. */
        if (rc == VERR_TIMEOUT)
        {
            continue;
        }
        else if (RT_FAILURE(rc))
        {
            /* Terminate connection handling in case of error. */
            VBClLogError("unable to handle IPC session, rc=%Rrc\n", rc);
            break;
        }
    }

    return rc;
}

/**
 * Add IPC client connection data into list of connections.
 *
 * List size is limited indirectly by DRM_IPC_SERVER_CONNECTIONS_MAX value.
 * This function should only be invoked from client thread context
 * (from vbDrmIpcClientWorker() in particular).
 *
 * @return  IPRT status code.
 * @param   pClientNode     Client connection information to add to the list.
 */
static int vbDrmIpcClientsListAdd(PVBOX_DRMIPC_CLIENT_CONNECTION_LIST_NODE pClientNode)
{
    int rc;

    AssertReturn(pClientNode, VERR_INVALID_PARAMETER);

    rc = RTCritSectEnter(&g_ipcClientConnectionsListCritSect);
    if (RT_SUCCESS(rc))
    {
        RTListAppend(&g_ipcClientConnectionsList.Node, &pClientNode->Node);

        int rc2 = RTCritSectLeave(&g_ipcClientConnectionsListCritSect);
        if (RT_FAILURE(rc2))
            VBClLogError("add client connection: unable to leave critical section, rc=%Rrc\n", rc2);
    }
    else
        VBClLogError("add client connection: unable to enter critical section, rc=%Rrc\n", rc);

    return rc;
}

/**
 * Remove IPC client connection data from list of connections.
 *
 * This function should only be invoked from client thread context
 * (from vbDrmIpcClientWorker() in particular).
 *
 * @return  IPRT status code.
 * @param   pClientNode     Client connection information to remove from the list.
 */
static int vbDrmIpcClientsListRemove(PVBOX_DRMIPC_CLIENT_CONNECTION_LIST_NODE pClientNode)
{
    int rc;
    PVBOX_DRMIPC_CLIENT_CONNECTION_LIST_NODE pEntry, pNextEntry, pFound = NULL;

    AssertReturn(pClientNode, VERR_INVALID_PARAMETER);

    rc = RTCritSectEnter(&g_ipcClientConnectionsListCritSect);
    if (RT_SUCCESS(rc))
    {

        if (!RTListIsEmpty(&g_ipcClientConnectionsList.Node))
        {
            RTListForEachSafe(&g_ipcClientConnectionsList.Node, pEntry, pNextEntry, VBOX_DRMIPC_CLIENT_CONNECTION_LIST_NODE, Node)
            {
                if (pEntry == pClientNode)
                    pFound = (PVBOX_DRMIPC_CLIENT_CONNECTION_LIST_NODE)RTListNodeRemoveRet(&pEntry->Node);
            }
        }
        else
            VBClLogError("remove client connection: connections list empty, node %p not there\n", pClientNode);

        int rc2 = RTCritSectLeave(&g_ipcClientConnectionsListCritSect);
        if (RT_FAILURE(rc2))
            VBClLogError("remove client connection: unable to leave critical section, rc=%Rrc\n", rc2);
    }
    else
        VBClLogError("remove client connection: unable to enter critical section, rc=%Rrc\n", rc);

    if (!pFound)
        VBClLogError("remove client connection: node not found\n");

    return !rc && pFound ? VINF_SUCCESS : VERR_INVALID_PARAMETER;
}

/**
 * Convert VBOX_DRMIPC_VMWRECT into VMMDevDisplayDef and check layout correctness.
 *
 * VBOX_DRMIPC_VMWRECT does not represent enough information needed for
 * VMMDevDisplayDef. Missing fields (fDisplayFlags, idDisplay, cBitsPerPixel)
 * are initialized with default (invalid) values due to this.
 *
 * @return  True if given screen layout is correct (i.e., has no displays which overlap), False
 *          if it needs to be adjusted before injecting into DRM stack.
 * @param   cDisplays Number of displays in configuration data.
 * @param   pIn       A pointer to display configuration data array in form of VBOX_DRMIPC_VMWRECT (input).
 * @param   pOut      A pointer to display configuration data array in form of VMMDevDisplayDef (output).
 */
static bool vbDrmVmwRectToDisplayDef(uint32_t cDisplays, struct VBOX_DRMIPC_VMWRECT *pIn, VMMDevDisplayDef *pOut)
{
    bool fCorrect = true;

    for (uint32_t i = 0; i < cDisplays; i++)
    {
        /* VBOX_DRMIPC_VMWRECT has no information about this fields. */
        pOut[i].fDisplayFlags = 0;
        pOut[i].idDisplay = VBOX_DRMIPC_MONITORS_MAX;
        pOut[i].cBitsPerPixel = 0;

        pOut[i].xOrigin = pIn[i].x;
        pOut[i].yOrigin = pIn[i].y;
        pOut[i].cx = pIn[i].w;
        pOut[i].cy = pIn[i].h;

        /* Make sure that displays do not overlap within reported screen layout. Ask IPC server to fix layout otherwise. */
        fCorrect =    i > 0
                  && pIn[i].x != (int32_t)pIn[i - 1].w + pIn[i - 1].x
                  ?  false
                  :  fCorrect;
    }

    return fCorrect;
}

/**
 * @interface_method_impl{VBOX_DRMIPC_CLIENT,pfnRxCb}
 */
static DECLCALLBACK(int) vbDrmIpcClientRxCallBack(uint8_t idCmd, void *pvData, uint32_t cbData)
{
    int rc = VERR_INVALID_PARAMETER;

    AssertReturn(pvData, VERR_INVALID_PARAMETER);
    AssertReturn(cbData, VERR_INVALID_PARAMETER);

    switch (idCmd)
    {
        case VBOXDRMIPCSRVCMD_REPORT_DISPLAY_OFFSETS:
        {
            VMMDevDisplayDef aDisplays[VBOX_DRMIPC_MONITORS_MAX];
            bool fCorrect;

            PVBOX_DRMIPC_COMMAND_REPORT_DISPLAY_OFFSETS pCmd = (PVBOX_DRMIPC_COMMAND_REPORT_DISPLAY_OFFSETS)pvData;
            AssertReturn(cbData == sizeof(VBOX_DRMIPC_COMMAND_REPORT_DISPLAY_OFFSETS), VERR_INVALID_PARAMETER);
            AssertReturn(pCmd->cDisplays < VBOX_DRMIPC_MONITORS_MAX, VERR_INVALID_PARAMETER);

            /* Convert input display config into VMMDevDisplayDef representation. */
            RT_ZERO(aDisplays);
            fCorrect = vbDrmVmwRectToDisplayDef(pCmd->cDisplays, pCmd->aDisplays, aDisplays);

            rc = vbDrmPushScreenLayout(aDisplays, pCmd->cDisplays, true, !fCorrect);
            if (RT_FAILURE(rc))
                VBClLogError("Failed to push display change as requested by Desktop Environment helper, rc=%Rrc\n", rc);

            break;
        }

        default:
        {
            VBClLogError("received unknown IPC command 0x%x\n", idCmd);
            break;
        }
    }

    return rc;
}

/** Worker thread for IPC client task. */
static DECLCALLBACK(int) vbDrmIpcClientWorker(RTTHREAD ThreadSelf, void *pvUser)
{
    VBOX_DRMIPC_CLIENT  hClient  = VBOX_DRMIPC_CLIENT_INITIALIZER;
    RTLOCALIPCSESSION   hSession = (RTLOCALIPCSESSION)pvUser;
    int                 rc;

    AssertReturn(RT_VALID_PTR(hSession), VERR_INVALID_PARAMETER);

    /* Initialize client session resources. */
    rc = vbDrmIpcClientInit(&hClient, ThreadSelf, hSession, VBOX_DRMIPC_TX_QUEUE_SIZE, vbDrmIpcClientRxCallBack);
    if (RT_SUCCESS(rc))
    {
        /* Add IPC client connection data into clients list. */
        VBOX_DRMIPC_CLIENT_CONNECTION_LIST_NODE hClientNode = { { 0, 0 } , &hClient };

        rc = vbDrmIpcClientsListAdd(&hClientNode);
        if (RT_SUCCESS(rc))
        {
            rc = RTThreadUserSignal(ThreadSelf);
            if (RT_SUCCESS(rc))
            {
                /* Start spinning the connection. */
                VBClLogInfo("IPC client connection started\n", rc);
                rc = vbDrmIpcConnectionProc(&hClient);
                VBClLogInfo("IPC client connection ended, rc=%Rrc\n", rc);
            }
            else
                VBClLogError("unable to report IPC client connection handler start, rc=%Rrc\n", rc);

            /* Remove IPC client connection data from clients list. */
            rc = vbDrmIpcClientsListRemove(&hClientNode);
            if (RT_FAILURE(rc))
                VBClLogError("unable to remove IPC client session from list of connections, rc=%Rrc\n", rc);
        }
        else
            VBClLogError("unable to add IPC client connection to the list, rc=%Rrc\n");

        /* Disconnect remote peer if still connected. */
        if (RT_VALID_PTR(hSession))
        {
            rc = RTLocalIpcSessionClose(hSession);
            VBClLogInfo("IPC session closed, rc=%Rrc\n", rc);
        }

        /* Connection handler loop has ended, release session resources. */
        rc = vbDrmIpcClientReleaseResources(&hClient);
        if (RT_FAILURE(rc))
            VBClLogError("unable to release IPC client session, rc=%Rrc\n", rc);

        ASMAtomicDecU32(&g_cDrmIpcConnections);
    }
    else
        VBClLogError("unable to initialize IPC client session, rc=%Rrc\n", rc);

    VBClLogInfo("closing IPC client session, rc=%Rrc\n", rc);

    return rc;
}

/**
 * Start processing thread for IPC client requests handling.
 *
 * @returns IPRT status code.
 * @param   hSession    IPC client connection handle.
 */
static int vbDrmIpcClientStart(RTLOCALIPCSESSION hSession)
{
    int         rc;
    RTTHREAD    hThread = 0;
    RTPROCESS   hProcess = 0;

    rc = RTLocalIpcSessionQueryProcess(hSession, &hProcess);
    if (RT_SUCCESS(rc))
    {
        char pszThreadName[DRM_IPC_THREAD_NAME_MAX];
        RT_ZERO(pszThreadName);

        RTStrPrintf2(pszThreadName, DRM_IPC_THREAD_NAME_MAX, DRM_IPC_CLIENT_THREAD_NAME_PTR, hProcess);

        /* Attempt to start IPC client connection handler task. */
        rc = RTThreadCreate(&hThread, vbDrmIpcClientWorker, (void *)hSession, 0,
                            RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, pszThreadName);
        if (RT_SUCCESS(rc))
        {
            rc = RTThreadUserWait(hThread, RT_MS_5SEC);
        }
    }

    return rc;
}

/** Worker thread for IPC server task. */
static DECLCALLBACK(int) vbDrmIpcServerWorker(RTTHREAD ThreadSelf, void *pvUser)
{
    int rc = VERR_GENERAL_FAILURE;
    RTLOCALIPCSERVER hIpcServer = (RTLOCALIPCSERVER)pvUser;

    RT_NOREF1(ThreadSelf);

    AssertReturn(hIpcServer, VERR_INVALID_PARAMETER);

    /* This loop accepts incoming connections. */
    for (;;)
    {
        RTLOCALIPCSESSION hClientSession;

        /* Wait for incoming connection. */
        rc = RTLocalIpcServerListen(hIpcServer, &hClientSession);
        if (RT_SUCCESS(rc))
        {
            VBClLogVerbose(2, "new IPC session\n");

            if (ASMAtomicIncU32(&g_cDrmIpcConnections) <= DRM_IPC_SERVER_CONNECTIONS_MAX)
            {
                /* Authenticate remote peer. */
                if (ASMAtomicReadBool(&g_fDrmIpcRestricted))
                    rc = vbDrmIpcAuth(hClientSession);

                if (RT_SUCCESS(rc))
                {
                    /* Start incoming connection handler thread. */
                    rc = vbDrmIpcClientStart(hClientSession);
                    VBClLogVerbose(2, "connection processing ended, rc=%Rrc\n", rc);
                }
                else
                    VBClLogError("IPC authentication failed, rc=%Rrc\n", rc);
            }
            else
                rc = VERR_RESOURCE_BUSY;

            /* Release resources in case of error. */
            if (RT_FAILURE(rc))
            {
                VBClLogError("maximum amount of IPC client connections reached, dropping connection\n");

                int rc2 = RTLocalIpcSessionClose(hClientSession);
                if (RT_FAILURE(rc2))
                    VBClLogError("unable to close IPC session, rc=%Rrc\n", rc2);

                ASMAtomicDecU32(&g_cDrmIpcConnections);
            }
        }
        else
            VBClLogError("IPC authentication failed, rc=%Rrc\n", rc);

        /* Check shutdown was requested. */
        if (ASMAtomicReadBool(&g_fShutdown))
        {
            VBClLogInfo("exiting IPC thread: shutdown requested\n");
            break;
        }

        /* Wait a bit before spinning a loop if something went wrong. */
        if (RT_FAILURE(rc))
            RTThreadSleep(VBOX_DRMIPC_RX_RELAX_MS);
    }

    return rc;
}

/** A signal handler. */
static void vbDrmRequestShutdown(int sig)
{
    RT_NOREF(sig);
    ASMAtomicWriteBool(&g_fShutdown, true);
}

/**
 * Grant access to DRM IPC server socket depending on VM configuration.
 *
 * If VM has '/VirtualBox/GuestAdd/DRMIpcRestricted' guest property set
 * and this property is READ-ONLY for the guest side, access will be
 * granted to root and users from 'vboxdrmipc' group only. If group does
 * not exists, only root will have access to the socket.  When property is
 * not set or not READ-ONLY, all users will have access to the socket.
 *
 * @param   hIpcServer  IPC server handle.
 * @param   fRestrict   Whether to restrict access to socket or not.
 */
static void vbDrmSetIpcServerAccessPermissions(RTLOCALIPCSERVER hIpcServer, bool fRestrict)
{
    int rc;

    if (fRestrict)
    {
        struct group *pGrp;
        pGrp = getgrnam(VBOX_DRMIPC_USER_GROUP);
        if (pGrp)
        {
            rc = RTLocalIpcServerGrantGroupAccess(hIpcServer, pGrp->gr_gid);
            if (RT_SUCCESS(rc))
                VBClLogInfo("IPC server socket access granted to '" VBOX_DRMIPC_USER_GROUP "' users\n");
            else
                VBClLogError("unable to grant IPC server socket access to '" VBOX_DRMIPC_USER_GROUP "' users, rc=%Rrc\n", rc);

        }
        else
            VBClLogError("unable to grant IPC server socket access to '" VBOX_DRMIPC_USER_GROUP "', group does not exist\n");
    }
    else
    {
        rc = RTLocalIpcServerSetAccessMode(hIpcServer,
                                           RTFS_UNIX_IRUSR | RTFS_UNIX_IWUSR |
                                           RTFS_UNIX_IRGRP | RTFS_UNIX_IWGRP |
                                           RTFS_UNIX_IROTH | RTFS_UNIX_IWOTH);
        if (RT_SUCCESS(rc))
            VBClLogInfo("IPC server socket access granted to all users\n");
        else
            VBClLogError("unable to grant IPC server socket access to all users, rc=%Rrc\n", rc);
    }

    /* Set flag for the thread which serves incoming IPC connections. */
    ASMAtomicWriteBool(&g_fDrmIpcRestricted, fRestrict);
}

/**
 * Wait and handle '/VirtualBox/GuestAdd/DRMIpcRestricted' guest property change.
 *
 * This function is executed in context of main().
 *
 * @param   hIpcServer  IPC server handle.
 */
static void vbDrmPollIpcServerAccessMode(RTLOCALIPCSERVER hIpcServer)
{
    HGCMCLIENTID idClient;
    int rc;

    rc = VbglR3GuestPropConnect(&idClient);
    if (RT_SUCCESS(rc))
    {
        do
        {
            /* Buffer should be big enough to fit guest property data layout: Name\0Value\0Flags\0fWasDeleted\0. */
            static char achBuf[GUEST_PROP_MAX_NAME_LEN];
            char *pszName = NULL;
            char *pszValue = NULL;
            char *pszFlags = NULL;
            bool fWasDeleted = false;
            uint64_t u64Timestamp = 0;

            rc = VbglR3GuestPropWait(idClient, VBGLR3DRMPROPPTR, achBuf, sizeof(achBuf), u64Timestamp,
                                     VBOX_DRMIPC_RX_TIMEOUT_MS, &pszName, &pszValue, &u64Timestamp,
                                     &pszFlags, NULL, &fWasDeleted);
            if (RT_SUCCESS(rc))
            {
                uint32_t fFlags = 0;

                VBClLogVerbose(1, "guest property change: name: %s, val: %s, flags: %s, fWasDeleted: %RTbool\n",
                               pszName, pszValue, pszFlags, fWasDeleted);

                if (RT_SUCCESS(GuestPropValidateFlags(pszFlags, &fFlags)))
                {
                    if (RTStrNCmp(pszName, VBGLR3DRMIPCPROPRESTRICT, GUEST_PROP_MAX_NAME_LEN) == 0)
                    {
                        /* Enforce restricted socket access until guest property exist and READ-ONLY for the guest. */
                        vbDrmSetIpcServerAccessPermissions(hIpcServer, !fWasDeleted && fFlags & GUEST_PROP_F_RDONLYGUEST);
                    }

                } else
                    VBClLogError("guest property change: name: %s, val: %s, flags: %s, fWasDeleted: %RTbool: bad flags\n",
                                 pszName, pszValue, pszFlags, fWasDeleted);

            } else if (   rc != VERR_TIMEOUT
                     && rc != VERR_INTERRUPTED)
            {
                VBClLogError("error on waiting guest property notification, rc=%Rrc\n", rc);
                RTThreadSleep(VBOX_DRMIPC_RX_RELAX_MS);
            }

        } while (!ASMAtomicReadBool(&g_fShutdown));

        VbglR3GuestPropDisconnect(idClient);
    }
    else
        VBClLogError("cannot connect to VM guest properties service, rc=%Rrc\n", rc);
}

int main(int argc, char *argv[])
{
    /** Custom log prefix to be used for logger instance of this process. */
    static const char *pszLogPrefix = "VBoxDRMClient:";

    static const RTGETOPTDEF s_aOptions[] = { { "--verbose", 'v', RTGETOPT_REQ_NOTHING }, };
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    int ch;

    RTFILE hPidFile;

    RTLOCALIPCSERVER hIpcServer;
    RTTHREAD vbDrmIpcThread;
    int rcDrmIpcThread = 0;

    RTTHREAD drmResizeThread;
    int rcDrmResizeThread = 0;
    int rc, rc2 = 0;

    rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    rc = VbglR3InitUser();
    if (RT_FAILURE(rc))
        VBClLogFatalError("VBoxDRMClient: VbglR3InitUser failed: %Rrc", rc);

    /* Process command line options. */
    rc = RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /* fFlags */);
    if (RT_FAILURE(rc))
        VBClLogFatalError("VBoxDRMClient: unable to process command line options, rc=%Rrc\n", rc);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'v':
            {
                g_cVerbosity++;
                break;
            }

            case VERR_GETOPT_UNKNOWN_OPTION:
            {
                VBClLogFatalError("unknown command line option '%s'\n", ValueUnion.psz);
                return RTEXITCODE_SYNTAX;

            }

            default:
                break;
        }
    }

    rc = VBClLogCreate("");
    if (RT_FAILURE(rc))
        VBClLogFatalError("VBoxDRMClient: failed to setup logging, rc=%Rrc\n", rc);
    VBClLogSetLogPrefix(pszLogPrefix);

    /* Check PID file before attempting to initialize anything. */
    rc = VbglR3PidFile(g_pszPidFile, &hPidFile);
    if (rc == VERR_FILE_LOCK_VIOLATION)
    {
        VBClLogInfo("already running, exiting\n");
        return RTEXITCODE_SUCCESS;
    }
    if (RT_FAILURE(rc))
    {
        VBClLogError("unable to lock PID file (%Rrc), exiting\n", rc);
        return RTEXITCODE_FAILURE;
    }

    g_hDevice = vbDrmOpenVmwgfx();
    if (g_hDevice == NIL_RTFILE)
        return RTEXITCODE_FAILURE;

    rc = VbglR3CtlFilterMask(VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST, 0);
    if (RT_FAILURE(rc))
    {
        VBClLogFatalError("Failed to request display change events, rc=%Rrc\n", rc);
        return RTEXITCODE_FAILURE;
    }
    rc = VbglR3AcquireGuestCaps(VMMDEV_GUEST_SUPPORTS_GRAPHICS, 0, false);
    if (RT_FAILURE(rc))
    {
        VBClLogFatalError("Failed to register resizing support, rc=%Rrc\n", rc);
        return RTEXITCODE_FAILURE;
    }

    /* Setup signals: gracefully terminate on SIGINT, SIGTERM. */
    if (   signal(SIGINT, vbDrmRequestShutdown) == SIG_ERR
        || signal(SIGTERM, vbDrmRequestShutdown) == SIG_ERR)
    {
        VBClLogError("unable to setup signals\n");
        return RTEXITCODE_FAILURE;
    }

    /* Init IPC client connection list. */
    RTListInit(&g_ipcClientConnectionsList.Node);
    rc = RTCritSectInit(&g_ipcClientConnectionsListCritSect);
    if (RT_FAILURE(rc))
    {
        VBClLogError("unable to initialize IPC client connection list critical section\n");
        return RTEXITCODE_FAILURE;
    }

    /* Init critical section which is used for reporting monitors offset back to host. */
    rc = RTCritSectInit(&g_monitorPositionsCritSect);
    if (RT_FAILURE(rc))
    {
        VBClLogError("unable to initialize monitors position critical section\n");
        return RTEXITCODE_FAILURE;
    }

    rc = vbDrmSuppressionInit(&g_vboxDrmSuppression);
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;

    /* Instantiate IPC server for VBoxClient service communication. */
    rc = RTLocalIpcServerCreate(&hIpcServer, VBOX_DRMIPC_SERVER_NAME, 0);
    if (RT_FAILURE(rc))
    {
        VBClLogError("unable to setup IPC server, rc=%Rrc\n", rc);
        return RTEXITCODE_FAILURE;
    }

    /* Set IPC server socket access permissions according to VM configuration. */
    vbDrmSetIpcServerAccessPermissions(hIpcServer, VbglR3DrmRestrictedIpcAccessIsNeeded());

    /* Attempt to start DRM resize task. */
    rc = RTThreadCreate(&drmResizeThread, vbDrmResizeWorker, NULL, 0,
                        RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, DRM_RESIZE_THREAD_NAME);
    if (RT_SUCCESS(rc))
    {
        /* Attempt to start IPC task. */
        rc = RTThreadCreate(&vbDrmIpcThread, vbDrmIpcServerWorker, (void *)hIpcServer, 0,
                            RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, DRM_IPC_SERVER_THREAD_NAME);
        if (RT_SUCCESS(rc))
        {
            /* Poll for host notification about IPC server socket access mode change. */
            vbDrmPollIpcServerAccessMode(hIpcServer);

            /* HACK ALERT!
             * The sequence of RTThreadWait(drmResizeThread) -> RTLocalIpcServerDestroy() -> RTThreadWait(vbDrmIpcThread)
             * is intentional! Once process received a signal, it will pull g_fShutdown flag, which in turn will cause
             * drmResizeThread to quit. The vbDrmIpcThread might hang on accept() call, so we terminate IPC server to
             * release it and then wait for its termination. */

            rc = RTThreadWait(drmResizeThread, RT_INDEFINITE_WAIT, &rcDrmResizeThread);
            VBClLogInfo("%s thread exited with status, rc=%Rrc\n", DRM_RESIZE_THREAD_NAME, rcDrmResizeThread);

            rc = RTLocalIpcServerCancel(hIpcServer);
            if (RT_FAILURE(rc))
                VBClLogError("unable to notify IPC server about shutdown, rc=%Rrc\n", rc);

            /* Wait for threads to terminate gracefully. */
            rc = RTThreadWait(vbDrmIpcThread, RT_INDEFINITE_WAIT, &rcDrmIpcThread);
            VBClLogInfo("%s thread exited with status, rc=%Rrc\n", DRM_IPC_SERVER_THREAD_NAME, rcDrmResizeThread);

        }
        else
            VBClLogError("unable to start IPC thread, rc=%Rrc\n", rc);
    }
    else
        VBClLogError("unable to start resize thread, rc=%Rrc\n", rc);

    rc = RTLocalIpcServerDestroy(hIpcServer);
    if (RT_FAILURE(rc))
        VBClLogError("unable to stop IPC server,  rc=%Rrc\n", rc);

    rc2 = vbDrmSuppressionDestroy(&g_vboxDrmSuppression);
    if (RT_FAILURE(rc2))
        VBClLogError("unable to destroy suppression data, rc=%Rrc\n", rc2);

    rc2 = RTCritSectDelete(&g_monitorPositionsCritSect);
    if (RT_FAILURE(rc2))
        VBClLogError("unable to destroy g_monitorPositionsCritSect critsect, rc=%Rrc\n", rc2);

    rc2 = RTCritSectDelete(&g_ipcClientConnectionsListCritSect);
    if (RT_FAILURE(rc2))
        VBClLogError("unable to destroy g_ipcClientConnectionsListCritSect critsect, rc=%Rrc\n", rc2);

    RTFileClose(g_hDevice);

    VBClLogInfo("releasing PID file lock\n");
    VbglR3ClosePidFile(g_pszPidFile, hPidFile);

    VBClLogDestroy();

    return rc == 0 ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}
