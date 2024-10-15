/* $Id: DrvHostParallel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Host Parallel Port Driver.
 *
 * Initial Linux-only code contributed by: Alexander Eichner
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
#define LOG_GROUP LOG_GROUP_DRV_HOST_PARALLEL
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmthread.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/pipe.h>
#include <iprt/semaphore.h>
#include <iprt/stream.h>
#include <iprt/uuid.h>
#include <iprt/cdefs.h>
#include <iprt/ctype.h>

#ifdef RT_OS_LINUX
# include <sys/ioctl.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <sys/poll.h>
# include <fcntl.h>
# include <unistd.h>
# include <linux/ppdev.h>
# include <linux/parport.h>
# include <errno.h>
#endif

/** @def VBOX_WITH_WIN_PARPORT_SUP
 * Indicates whether to use the generic direct hardware access or host specific
 * code to access the parallel port.
 */
#if defined(DOXYGEN_RUNNING)
# define VBOX_WITH_WIN_PARPORT_SUP
#endif
#if defined(RT_OS_LINUX)
# undef VBOX_WITH_WIN_PARPORT_SUP
#elif defined(RT_OS_WINDOWS)
#else
# error "Not ported"
#endif

#if defined(VBOX_WITH_WIN_PARPORT_SUP) && defined(IN_RING0)
# include <iprt/asm-amd64-x86.h>
#endif

#if defined(VBOX_WITH_WIN_PARPORT_SUP) && defined(IN_RING3)
# include <iprt/win/windows.h>
# include <iprt/win/setupapi.h>
# include <cfgmgr32.h>
# include <iprt/mem.h>
# include <iprt/ctype.h>
# include <iprt/path.h>
# include <iprt/string.h>
#endif

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Host parallel port driver instance data.
 * @implements PDMIHOSTPARALLELCONNECTOR
 */
typedef struct DRVHOSTPARALLEL
{
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Pointer to the driver instance. */
    PPDMDRVINSR3                pDrvInsR3;
    PPDMDRVINSR0                pDrvInsR0;
    /** Pointer to the char port interface of the driver/device above us. */
    PPDMIHOSTPARALLELPORT       pDrvHostParallelPort;
    /** Our host device interface. */
    PDMIHOSTPARALLELCONNECTOR   IHostParallelConnector;
    /** Our host device interface. */
    PDMIHOSTPARALLELCONNECTOR   IHostParallelConnectorR3;
    /** Device Path */
    char                       *pszDevicePath;
    /** Device Handle */
    RTFILE                      hFileDevice;
#ifndef VBOX_WITH_WIN_PARPORT_SUP
    /** Thread waiting for interrupts. */
    PPDMTHREAD                  pMonitorThread;
    /** Wakeup pipe read end. */
    RTPIPE                      hWakeupPipeR;
    /** Wakeup pipe write end. */
    RTPIPE                      hWakeupPipeW;
    /** Current mode the parallel port is in. */
    PDMPARALLELPORTMODE         enmModeCur;
#endif

#ifdef VBOX_WITH_WIN_PARPORT_SUP
    /** Data register. */
    RTIOPORT                    PortDirectData;
    /** Status register. */
    RTIOPORT                    PortDirectStatus;
    /** Control register.  */
    RTIOPORT                    PortDirectControl;
    /** Control read result buffer. */
    uint8_t                     bReadInControl;
    /** Status read result buffer. */
    uint8_t                     bReadInStatus;
    /** Data buffer for reads and writes. */
    uint8_t                     abDataBuf[32];
#endif /* VBOX_WITH_WIN_PARPORT_SUP */
} DRVHOSTPARALLEL, *PDRVHOSTPARALLEL;


/**
 * Ring-0 operations.
 */
typedef enum DRVHOSTPARALLELR0OP
{
    /** Invalid zero value. */
    DRVHOSTPARALLELR0OP_INVALID = 0,
    /** Perform R0 initialization. */
    DRVHOSTPARALLELR0OP_INITR0STUFF,
    /** Read data into the data buffer (abDataBuf). */
    DRVHOSTPARALLELR0OP_READ,
    /** Read status register. */
    DRVHOSTPARALLELR0OP_READSTATUS,
    /** Read control register. */
    DRVHOSTPARALLELR0OP_READCONTROL,
    /** Write data from the data buffer (abDataBuf). */
    DRVHOSTPARALLELR0OP_WRITE,
    /** Write control register. */
    DRVHOSTPARALLELR0OP_WRITECONTROL,
    /** Set port direction. */
    DRVHOSTPARALLELR0OP_SETPORTDIRECTION
} DRVHOSTPARALLELR0OP;

/** Converts a pointer to DRVHOSTPARALLEL::IHostDeviceConnector to a PDRHOSTPARALLEL. */
#define PDMIHOSTPARALLELCONNECTOR_2_DRVHOSTPARALLEL(pInterface) ( (PDRVHOSTPARALLEL)((uintptr_t)pInterface - RT_UOFFSETOF(DRVHOSTPARALLEL, CTX_SUFF(IHostParallelConnector))) )


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define CTRL_REG_OFFSET                 2
#define STATUS_REG_OFFSET               1
#define LPT_CONTROL_ENABLE_BIDIRECT     0x20



#ifdef VBOX_WITH_WIN_PARPORT_SUP
# ifdef IN_RING0

/**
 * R0 mode function to write byte value to data port.
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to the instance data.
 * @param   u64Arg      The number of bytes to write (from abDataBuf).
 */
static int drvR0HostParallelReqWrite(PDRVHOSTPARALLEL pThis, uint64_t u64Arg)
{
    LogFlowFunc(("write %#RX64 bytes to data (%#x)\n", u64Arg, pThis->PortDirectData));

    AssertReturn(u64Arg > 0 && u64Arg <= sizeof(pThis->abDataBuf), VERR_OUT_OF_RANGE);
    uint8_t const *pbSrc = pThis->abDataBuf;
    while (u64Arg-- > 0)
    {
        ASMOutU8(pThis->PortDirectData, *pbSrc);
        pbSrc++;
    }

    return VINF_SUCCESS;
}

/**
 * R0 mode function to write byte value to parallel port control register.
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to the instance data.
 * @param   u64Arg      Data to be written to control register.
 */
static int drvR0HostParallelReqWriteControl(PDRVHOSTPARALLEL pThis, uint64_t u64Arg)
{
    LogFlowFunc(("write to ctrl port=%#x val=%#x\n", pThis->PortDirectControl, u64Arg));
    ASMOutU8(pThis->PortDirectControl, (uint8_t)(u64Arg));
    return VINF_SUCCESS;
}

/**
 * R0 mode function to ready byte value from the parallel port data register.
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to the instance data.
 * @param   u64Arg      The number of bytes to read into abDataBuf.
 */
static int drvR0HostParallelReqRead(PDRVHOSTPARALLEL pThis, uint64_t u64Arg)
{
    LogFlowFunc(("read %#RX64 bytes to data (%#x)\n", u64Arg, pThis->PortDirectData));

    AssertReturn(u64Arg > 0 && u64Arg <= sizeof(pThis->abDataBuf), VERR_OUT_OF_RANGE);
    uint8_t *pbDst = pThis->abDataBuf;
    while (u64Arg-- > 0)
        *pbDst++ = ASMInU8(pThis->PortDirectData);

    return VINF_SUCCESS;
}

/**
 * R0 mode function to ready byte value from the parallel port control register.
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to the instance data.
 */
static int drvR0HostParallelReqReadControl(PDRVHOSTPARALLEL pThis)
{
    uint8_t u8Data = ASMInU8(pThis->PortDirectControl);
    LogFlowFunc(("read from ctrl port=%#x val=%#x\n", pThis->PortDirectControl, u8Data));
    pThis->bReadInControl = u8Data;
    return VINF_SUCCESS;
}

/**
 * R0 mode function to ready byte value from the parallel port status register.
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to the instance data.
 */
static int drvR0HostParallelReqReadStatus(PDRVHOSTPARALLEL pThis)
{
    uint8_t u8Data = ASMInU8(pThis->PortDirectStatus);
    LogFlowFunc(("read from status port=%#x val=%#x\n", pThis->PortDirectStatus, u8Data));
    pThis->bReadInStatus = u8Data;
    return VINF_SUCCESS;
}

/**
 * R0 mode function to set the direction of parallel port -
 * operate in bidirectional mode or single direction.
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to the instance data.
 * @param   u64Arg      Mode.
 */
static int drvR0HostParallelReqSetPortDir(PDRVHOSTPARALLEL pThis, uint64_t u64Arg)
{
    uint8_t bCtl = ASMInU8(pThis->PortDirectControl);
    if (u64Arg)
        bCtl |= LPT_CONTROL_ENABLE_BIDIRECT;  /* enable input direction */
    else
        bCtl &= ~LPT_CONTROL_ENABLE_BIDIRECT; /* disable input direction */
    ASMOutU8(pThis->PortDirectControl, bCtl);

    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNPDMDRVREQHANDLERR0}
 */
PDMBOTHCBDECL(int) drvR0HostParallelReqHandler(PPDMDRVINS pDrvIns, uint32_t uOperation, uint64_t u64Arg)
{
    PDRVHOSTPARALLEL pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTPARALLEL);
    int rc;
    LogFlowFuncEnter();

    if (pThis->PortDirectData != 0)
    {
        switch ((DRVHOSTPARALLELR0OP)uOperation)
        {
            case DRVHOSTPARALLELR0OP_READ:
                rc = drvR0HostParallelReqRead(pThis, u64Arg);
                break;
            case DRVHOSTPARALLELR0OP_READSTATUS:
                rc = drvR0HostParallelReqReadStatus(pThis);
                break;
            case DRVHOSTPARALLELR0OP_READCONTROL:
                rc = drvR0HostParallelReqReadControl(pThis);
                break;
            case DRVHOSTPARALLELR0OP_WRITE:
                rc = drvR0HostParallelReqWrite(pThis, u64Arg);
                break;
            case DRVHOSTPARALLELR0OP_WRITECONTROL:
                rc = drvR0HostParallelReqWriteControl(pThis, u64Arg);
                break;
            case DRVHOSTPARALLELR0OP_SETPORTDIRECTION:
                rc = drvR0HostParallelReqSetPortDir(pThis, u64Arg);
                break;
            default:
                rc = VERR_INVALID_FUNCTION;
                break;
        }
    }
    else
        rc = VERR_WRONG_ORDER;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

# endif /* IN_RING0 */
#endif /* VBOX_WITH_WIN_PARPORT_SUP */

#ifdef IN_RING3
# ifdef VBOX_WITH_WIN_PARPORT_SUP

/**
 * Find IO port range for the parallel port and return the lower address.
 *
 * @returns Parallel base I/O port.
 * @param   DevInst    Device instance (dword/handle) for the parallel port.
 */
static RTIOPORT drvHostParallelGetWinHostIoPortsSub(const DEVINST DevInst)
{
    RTIOPORT PortBase = 0;

    /* Get handle of the first logical configuration. */
    LOG_CONF  hFirstLogConf;
    CONFIGRET rcCm = CM_Get_First_Log_Conf(&hFirstLogConf, DevInst, ALLOC_LOG_CONF);
    if (rcCm != CR_SUCCESS)
        rcCm = CM_Get_First_Log_Conf(&hFirstLogConf, DevInst, BOOT_LOG_CONF);
    if (rcCm == CR_SUCCESS)
    {
        /*
         * This loop is based on the "fact" that only one I/O resource is assigned
         * to the LPT port.  Should there ever be multiple resources, we'll pick
         * the last one for some silly reason.
         */

        /* Get the first resource descriptor handle. */
        LOG_CONF hCurLogConf = 0;
        rcCm = CM_Get_Next_Res_Des(&hCurLogConf, hFirstLogConf, ResType_IO, 0, 0);
        if (rcCm == CR_SUCCESS)
        {
            for (;;)
            {
                ULONG cbData;
                rcCm = CM_Get_Res_Des_Data_Size(&cbData, hCurLogConf, 0);
                if (rcCm != CR_SUCCESS)
                    cbData = 0;
                cbData = RT_MAX(cbData, sizeof(IO_DES));
                IO_DES *pIoDesc = (IO_DES *)RTMemAllocZ(cbData);
                if (pIoDesc)
                {
                    rcCm = CM_Get_Res_Des_Data(hCurLogConf, pIoDesc, cbData, 0L);
                    if (rcCm == CR_SUCCESS)
                    {
                        LogRel(("drvHostParallelGetWinHostIoPortsSub: Count=%#u Type=%#x Base=%#RX64 End=%#RX64 Flags=%#x\n",
                                pIoDesc->IOD_Count, pIoDesc->IOD_Type, (uint64_t)pIoDesc->IOD_Alloc_Base,
                                (uint64_t)pIoDesc->IOD_Alloc_End,  pIoDesc->IOD_DesFlags));
                        PortBase = (RTIOPORT)pIoDesc->IOD_Alloc_Base;
                    }
                    else
                        LogRel(("drvHostParallelGetWinHostIoPortsSub: CM_Get_Res_Des_Data(,,%u,0) failed: %u\n", cbData, rcCm));
                    RTMemFree(pIoDesc);
                }
                else
                    LogRel(("drvHostParallelGetWinHostIoPortsSub: failed to allocate %#x bytes\n", cbData));

                /* Next */
                RES_DES hFreeResDesc = hCurLogConf;
                rcCm = CM_Get_Next_Res_Des(&hCurLogConf, hCurLogConf, ResType_IO, 0, 0);
                CM_Free_Res_Des_Handle(hFreeResDesc);
                if (rcCm != CR_SUCCESS)
                {
                    if (rcCm != CR_NO_MORE_RES_DES)
                        LogRel(("drvHostParallelGetWinHostIoPortsSub: CM_Get_Next_Res_Des failed: %u\n", rcCm));
                    break;
                }
            }
        }
        else
            LogRel(("drvHostParallelGetWinHostIoPortsSub: Initial CM_Get_Next_Res_Des failed: %u\n", rcCm));
        CM_Free_Log_Conf_Handle(hFirstLogConf);
    }
    LogFlowFunc(("return PortBase=%#x", PortBase));
    return PortBase;
}

/**
 * Get Parallel port address and update the shared data structure.
 *
 * @returns VBox status code.
 * @param   pThis    The host parallel port instance data.
 */
static int drvHostParallelGetWinHostIoPorts(PDRVHOSTPARALLEL pThis)
{
    /*
     * Assume the host device path is on the form "\\.\PIPE\LPT1", then get the "LPT1" part.
     */
    const char * const pszCfgPortName = RTPathFilename(pThis->pszDevicePath);
    AssertReturn(pszCfgPortName, VERR_INTERNAL_ERROR_3);
    size_t const       cchCfgPortName = strlen(pszCfgPortName);
    if (   cchCfgPortName != 4
        || RTStrNICmp(pszCfgPortName, "LPT", 3) != 0
        || !RT_C_IS_DIGIT(pszCfgPortName[3]) )
    {
        LogRel(("drvHostParallelGetWinHostIoPorts: The configured device name '%s' is not on the expected 'LPTx' form!\n",
                pszCfgPortName));
        return VERR_INVALID_NAME;
    }

    /*
     * Get a list of devices then enumerate it looking for the LPT port we're using.
     */
    HDEVINFO hDevInfo = SetupDiGetClassDevs(NULL, 0, 0, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (hDevInfo == INVALID_HANDLE_VALUE)
    {
        DWORD dwErr = GetLastError();
        LogRel(("drvHostParallelGetWinHostIoPorts: SetupDiGetClassDevs failed: %u\n", dwErr));
        return RTErrConvertFromWin32(dwErr);
    }

    int   rc     = VINF_SUCCESS;
    char *pszBuf = NULL;
    DWORD cbBuf  = 0;
    for (int32_t idxDevInfo = 0;; idxDevInfo++)
    {
        /*
         * Query the next device info.
         */
        SP_DEVINFO_DATA DeviceInfoData;
        DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
        if (!SetupDiEnumDeviceInfo(hDevInfo, idxDevInfo, &DeviceInfoData))
        {
            DWORD dwErr = GetLastError();
            if (dwErr != ERROR_NO_MORE_ITEMS && dwErr != NO_ERROR)
            {
                LogRel(("drvHostParallelGetWinHostIoPorts: SetupDiEnumDeviceInfo failed: %u\n", dwErr));
                rc = RTErrConvertFromWin32(dwErr);
            }
            break;
        }

        /* Get the friendly name of the device. */
        DWORD dwDataType;
        DWORD cbBufActual;
        BOOL  fOk;
        while (!(fOk = SetupDiGetDeviceRegistryProperty(hDevInfo, &DeviceInfoData, SPDRP_FRIENDLYNAME,
                                                       &dwDataType, (PBYTE)pszBuf, cbBuf, &cbBufActual)))
        {
            DWORD dwErr = GetLastError();
            if (dwErr == ERROR_INSUFFICIENT_BUFFER)
            {
                LogFlow(("ERROR_INSUFF_BUFF = %d. dwBufSz = %d\n", dwErr, cbBuf));
                cbBuf = RT_MAX(RT_ALIGN_Z(cbBufActual + 16, 64), 256);
                void *pvNew = RTMemRealloc(pszBuf, cbBuf);
                if (pvNew)
                    pszBuf = (char *)pvNew;
                else
                {
                    LogFlow(("GetDevProp Error = %d & cbBufActual = %d\n", dwErr, cbBufActual));
                    break;
                }
            }
            else
            {
                /* No need to bother about this error (in most cases its errno=13,
                 * INVALID_DATA . Just break from here and proceed to next device
                 * enumerated item
                 */
                LogFlow(("GetDevProp Error = %d & cbBufActual = %d\n", dwErr, cbBufActual));
                break;
            }
        }
        if (   fOk
            && pszBuf)
        {
            pszBuf[cbBuf - 1] = '\0';

            /*
             * Does this look like the port name we're looking for.
             *
             * We're expecting either "Parallel Port (LPT1)" or just "LPT1", though we'll make do
             * with anything that includes the name we're looking for as a separate word.
             */
            char *pszMatch;
            do
                pszMatch = RTStrIStr(pszBuf, pszCfgPortName);
            while (   pszMatch != NULL
                   && !(  (   pszMatch     == pszBuf
                           || pszMatch[-1] == '('
                           || RT_C_IS_BLANK(pszMatch[-1]))
                        && (   pszMatch[cchCfgPortName] == '\0'
                            || pszMatch[cchCfgPortName] == ')'
                            || RT_C_IS_BLANK(pszMatch[cchCfgPortName])) ) );
            if (pszMatch != NULL)
            {
                RTIOPORT Port = drvHostParallelGetWinHostIoPortsSub(DeviceInfoData.DevInst);
                if (Port != 0)
                {
                    pThis->PortDirectData    = Port;
                    pThis->PortDirectControl = Port + CTRL_REG_OFFSET;
                    pThis->PortDirectStatus  = Port + STATUS_REG_OFFSET;
                    break;
                }
                LogRel(("drvHostParallelGetWinHostIoPorts: Addr not found for '%s'\n", pszBuf));
            }
        }
    }

    /* Cleanup. */
    RTMemFree(pszBuf);
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return rc;

}
# endif /* VBOX_WITH_WIN_PARPORT_SUP */

/**
 * Changes the current mode of the host parallel port.
 *
 * @returns VBox status code.
 * @param   pThis    The host parallel port instance data.
 * @param   enmMode  The mode to change the port to.
 */
static int drvHostParallelSetMode(PDRVHOSTPARALLEL pThis, PDMPARALLELPORTMODE enmMode)
{
    LogFlowFunc(("mode=%d\n", enmMode));
# ifndef VBOX_WITH_WIN_PARPORT_SUP
    int rc = VINF_SUCCESS;
    int iMode = 0;
    int rcLnx;
    if (pThis->enmModeCur != enmMode)
    {
        switch (enmMode)
        {
            case PDM_PARALLEL_PORT_MODE_SPP:
                iMode = IEEE1284_MODE_COMPAT;
                break;
            case PDM_PARALLEL_PORT_MODE_EPP_DATA:
                iMode = IEEE1284_MODE_EPP | IEEE1284_DATA;
                break;
            case PDM_PARALLEL_PORT_MODE_EPP_ADDR:
                iMode = IEEE1284_MODE_EPP | IEEE1284_ADDR;
                break;
            case PDM_PARALLEL_PORT_MODE_ECP:
            case PDM_PARALLEL_PORT_MODE_INVALID:
            default:
                return VERR_NOT_SUPPORTED;
        }

        rcLnx = ioctl(RTFileToNative(pThis->hFileDevice), PPSETMODE, &iMode);
        if (RT_UNLIKELY(rcLnx < 0))
            rc = RTErrConvertFromErrno(errno);
        else
            pThis->enmModeCur = enmMode;
    }

    return rc;
# else  /* VBOX_WITH_WIN_PARPORT_SUP */
    RT_NOREF(pThis, enmMode);
    return VINF_SUCCESS;
# endif /* VBOX_WITH_WIN_PARPORT_SUP */
}

/* -=-=-=-=- IBase -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvHostParallelQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS          pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVHOSTPARALLEL    pThis   = PDMINS_2_DATA(pDrvIns, PDRVHOSTPARALLEL);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHOSTPARALLELCONNECTOR, &pThis->CTX_SUFF(IHostParallelConnector));
    return NULL;
}


/* -=-=-=-=- IHostDeviceConnector -=-=-=-=- */

/**
 * @interface_method_impl{PDMIHOSTPARALLELCONNECTOR,pfnWrite}
 */
static DECLCALLBACK(int)
drvHostParallelWrite(PPDMIHOSTPARALLELCONNECTOR pInterface, const void *pvBuf, size_t cbWrite, PDMPARALLELPORTMODE enmMode)
{
    PDRVHOSTPARALLEL pThis = RT_FROM_MEMBER(pInterface, DRVHOSTPARALLEL, CTX_SUFF(IHostParallelConnector));
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pvBuf=%#p cbWrite=%d\n", pvBuf, cbWrite));

    rc = drvHostParallelSetMode(pThis, enmMode);
    if (RT_FAILURE(rc))
        return rc;
# ifndef VBOX_WITH_WIN_PARPORT_SUP
    int rcLnx = 0;
    if (enmMode == PDM_PARALLEL_PORT_MODE_SPP)
    {
        /* Set the data lines directly. */
        rcLnx = ioctl(RTFileToNative(pThis->hFileDevice), PPWDATA, pvBuf);
    }
    else
    {
        /* Use write interface. */
        rcLnx = write(RTFileToNative(pThis->hFileDevice), pvBuf, cbWrite);
    }
    if (RT_UNLIKELY(rcLnx < 0))
        rc = RTErrConvertFromErrno(errno);

# else /* VBOX_WITH_WIN_PARPORT_SUP */
    if (pThis->PortDirectData != 0)
    {
        while (cbWrite > 0)
        {
            size_t cbToWrite = RT_MIN(cbWrite, sizeof(pThis->abDataBuf));
            LogFlowFunc(("Calling R0 to write %#zu bytes of data\n", cbToWrite));
            memcpy(pThis->abDataBuf, pvBuf, cbToWrite);
            rc = PDMDrvHlpCallR0(pThis->CTX_SUFF(pDrvIns), DRVHOSTPARALLELR0OP_WRITE, cbToWrite);
            AssertRC(rc);
            pvBuf = (uint8_t const *)pvBuf + cbToWrite;
            cbWrite -= cbToWrite;
        }
    }
# endif /* VBOX_WITH_WIN_PARPORT_SUP */
    return rc;
}

/**
 * @interface_method_impl{PDMIHOSTPARALLELCONNECTOR,pfnRead}
 */
static DECLCALLBACK(int)
drvHostParallelRead(PPDMIHOSTPARALLELCONNECTOR pInterface, void *pvBuf, size_t cbRead, PDMPARALLELPORTMODE enmMode)
{
    PDRVHOSTPARALLEL pThis = RT_FROM_MEMBER(pInterface, DRVHOSTPARALLEL, CTX_SUFF(IHostParallelConnector));
    int rc = VINF_SUCCESS;

# ifndef VBOX_WITH_WIN_PARPORT_SUP
    int rcLnx = 0;
    LogFlowFunc(("pvBuf=%#p cbRead=%d\n", pvBuf, cbRead));

    rc = drvHostParallelSetMode(pThis, enmMode);
    if (RT_FAILURE(rc))
        return rc;

    if (enmMode == PDM_PARALLEL_PORT_MODE_SPP)
    {
        /* Set the data lines directly. */
        rcLnx = ioctl(RTFileToNative(pThis->hFileDevice), PPWDATA, pvBuf);
    }
    else
    {
        /* Use write interface. */
        rcLnx = read(RTFileToNative(pThis->hFileDevice), pvBuf, cbRead);
    }
    if (RT_UNLIKELY(rcLnx < 0))
        rc = RTErrConvertFromErrno(errno);

# else  /* VBOX_WITH_WIN_PARPORT_SUP */
    RT_NOREF(enmMode);
    if (pThis->PortDirectData != 0)
    {
        while (cbRead > 0)
        {
            size_t cbToRead = RT_MIN(cbRead, sizeof(pThis->abDataBuf));
            LogFlowFunc(("Calling R0 to read %#zu bytes of data\n", cbToRead));
            memset(pThis->abDataBuf, 0, cbToRead);
            rc = PDMDrvHlpCallR0(pThis->CTX_SUFF(pDrvIns), DRVHOSTPARALLELR0OP_READ, cbToRead);
            AssertRC(rc);
            memcpy(pvBuf, pThis->abDataBuf, cbToRead);
            pvBuf   = (uint8_t *)pvBuf + cbToRead;
            cbRead -= cbToRead;
        }
    }
# endif /* VBOX_WITH_WIN_PARPORT_SUP */
    return rc;
}

/**
 * @interface_method_impl{PDMIHOSTPARALLELCONNECTOR,pfnSetPortDirection}
 */
static DECLCALLBACK(int) drvHostParallelSetPortDirection(PPDMIHOSTPARALLELCONNECTOR pInterface, bool fForward)
{
    PDRVHOSTPARALLEL    pThis   = RT_FROM_MEMBER(pInterface, DRVHOSTPARALLEL, CTX_SUFF(IHostParallelConnector));
    int rc = VINF_SUCCESS;
    int iMode = 0;
    if (!fForward)
        iMode = 1;
# ifndef VBOX_WITH_WIN_PARPORT_SUP
    int rcLnx = ioctl(RTFileToNative(pThis->hFileDevice), PPDATADIR, &iMode);
    if (RT_UNLIKELY(rcLnx < 0))
        rc = RTErrConvertFromErrno(errno);

# else /* VBOX_WITH_WIN_PARPORT_SUP */
    if (pThis->PortDirectData != 0)
    {
        LogFlowFunc(("calling R0 to write CTRL, data=%#x\n", iMode));
        rc = PDMDrvHlpCallR0(pThis->CTX_SUFF(pDrvIns), DRVHOSTPARALLELR0OP_SETPORTDIRECTION, iMode);
        AssertRC(rc);
    }
# endif /* VBOX_WITH_WIN_PARPORT_SUP */
    return rc;
}

/**
 * @interface_method_impl{PDMIHOSTPARALLELCONNECTOR,pfnWriteControl}
 */
static DECLCALLBACK(int) drvHostParallelWriteControl(PPDMIHOSTPARALLELCONNECTOR pInterface, uint8_t fReg)
{
    PDRVHOSTPARALLEL    pThis   = RT_FROM_MEMBER(pInterface, DRVHOSTPARALLEL, CTX_SUFF(IHostParallelConnector));
    int rc = VINF_SUCCESS;

    LogFlowFunc(("fReg=%#x\n", fReg));
# ifndef VBOX_WITH_WIN_PARPORT_SUP
    int rcLnx = ioctl(RTFileToNative(pThis->hFileDevice), PPWCONTROL, &fReg);
    if (RT_UNLIKELY(rcLnx < 0))
        rc = RTErrConvertFromErrno(errno);
# else /* VBOX_WITH_WIN_PARPORT_SUP */
    if (pThis->PortDirectData != 0)
    {
        LogFlowFunc(("calling R0 to write CTRL, data=%#x\n", fReg));
        rc = PDMDrvHlpCallR0(pThis->CTX_SUFF(pDrvIns), DRVHOSTPARALLELR0OP_WRITECONTROL, fReg);
        AssertRC(rc);
    }
# endif /* VBOX_WITH_WIN_PARPORT_SUP */
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTPARALLELCONNECTOR,pfnReadControl}
 */
static DECLCALLBACK(int) drvHostParallelReadControl(PPDMIHOSTPARALLELCONNECTOR pInterface, uint8_t *pfReg)
{
    PDRVHOSTPARALLEL    pThis   = RT_FROM_MEMBER(pInterface, DRVHOSTPARALLEL, CTX_SUFF(IHostParallelConnector));
    int rc = VINF_SUCCESS;

# ifndef VBOX_WITH_WIN_PARPORT_SUP
    uint8_t fReg = 0;
    int rcLnx = ioctl(RTFileToNative(pThis->hFileDevice), PPRCONTROL, &fReg);
    if (RT_UNLIKELY(rcLnx < 0))
        rc = RTErrConvertFromErrno(errno);
    else
    {
        LogFlowFunc(("fReg=%#x\n", fReg));
        *pfReg = fReg;
    }
# else /* VBOX_WITH_WIN_PARPORT_SUP */
    *pfReg = 0; /* Initialize the buffer*/
    if (pThis->PortDirectData != 0)
    {
        LogFlowFunc(("calling R0 to read control from parallel port\n"));
        rc = PDMDrvHlpCallR0(pThis-> CTX_SUFF(pDrvIns), DRVHOSTPARALLELR0OP_READCONTROL, 0);
        AssertRC(rc);
        *pfReg = pThis->bReadInControl;
    }
# endif /* VBOX_WITH_WIN_PARPORT_SUP */
    return rc;
}

/**
 * @interface_method_impl{PDMIHOSTPARALLELCONNECTOR,pfnReadStatus}
 */
static DECLCALLBACK(int) drvHostParallelReadStatus(PPDMIHOSTPARALLELCONNECTOR pInterface, uint8_t *pfReg)
{
    PDRVHOSTPARALLEL    pThis   = RT_FROM_MEMBER(pInterface, DRVHOSTPARALLEL, CTX_SUFF(IHostParallelConnector));
    int rc = VINF_SUCCESS;
# ifndef  VBOX_WITH_WIN_PARPORT_SUP
    uint8_t fReg = 0;
    int rcLnx = ioctl(RTFileToNative(pThis->hFileDevice), PPRSTATUS, &fReg);
    if (RT_UNLIKELY(rcLnx < 0))
        rc = RTErrConvertFromErrno(errno);
    else
    {
        LogFlowFunc(("fReg=%#x\n", fReg));
        *pfReg = fReg;
    }
# else /* VBOX_WITH_WIN_PARPORT_SUP */
    *pfReg = 0; /* Intialize the buffer. */
    if (pThis->PortDirectData != 0)
    {
        LogFlowFunc(("calling R0 to read status from parallel port\n"));
        rc = PDMDrvHlpCallR0(pThis->CTX_SUFF(pDrvIns), DRVHOSTPARALLELR0OP_READSTATUS, 0);
        AssertRC(rc);
        *pfReg = pThis->bReadInStatus;
    }
# endif /* VBOX_WITH_WIN_PARPORT_SUP */
    return rc;
}

# ifndef VBOX_WITH_WIN_PARPORT_SUP

static DECLCALLBACK(int) drvHostParallelMonitorThread(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVHOSTPARALLEL pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTPARALLEL);
    struct pollfd aFDs[2];

    /*
     * We can wait for interrupts using poll on linux hosts.
     */
    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        int rc;

        aFDs[0].fd      = RTFileToNative(pThis->hFileDevice);
        aFDs[0].events  = POLLIN;
        aFDs[0].revents = 0;
        aFDs[1].fd      = RTPipeToNative(pThis->hWakeupPipeR);
        aFDs[1].events  = POLLIN | POLLERR | POLLHUP;
        aFDs[1].revents = 0;
        rc = poll(aFDs, RT_ELEMENTS(aFDs), -1);
        if (rc < 0)
        {
            AssertMsgFailed(("poll failed with rc=%d\n", RTErrConvertFromErrno(errno)));
            return RTErrConvertFromErrno(errno);
        }

        if (pThread->enmState != PDMTHREADSTATE_RUNNING)
            break;
        if (rc > 0 && aFDs[1].revents)
        {
            if (aFDs[1].revents & (POLLHUP | POLLERR | POLLNVAL))
                break;
            /* notification to terminate -- drain the pipe */
            char ch;
            size_t cbRead;
            RTPipeRead(pThis->hWakeupPipeR, &ch, 1, &cbRead);
            continue;
        }

        /* Interrupt occurred. */
        rc = pThis->pDrvHostParallelPort->pfnNotifyInterrupt(pThis->pDrvHostParallelPort);
        AssertRC(rc);
    }

    return VINF_SUCCESS;
}

/**
 * Unblock the monitor thread so it can respond to a state change.
 *
 * @returns a VBox status code.
 * @param     pDrvIns     The driver instance.
 * @param     pThread     The send thread.
 */
static DECLCALLBACK(int) drvHostParallelWakeupMonitorThread(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    PDRVHOSTPARALLEL pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTPARALLEL);
    size_t cbIgnored;
    return RTPipeWrite(pThis->hWakeupPipeW, "", 1, &cbIgnored);
}

# endif /* VBOX_WITH_WIN_PARPORT_SUP */

/**
 * Destruct a host parallel driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that
 * any non-VM resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvHostParallelDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    LogFlowFunc(("iInstance=%d\n", pDrvIns->iInstance));

#ifndef VBOX_WITH_WIN_PARPORT_SUP
    PDRVHOSTPARALLEL pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTPARALLEL);
    if (pThis->hFileDevice != NIL_RTFILE)
        ioctl(RTFileToNative(pThis->hFileDevice), PPRELEASE);

    if (pThis->hWakeupPipeW != NIL_RTPIPE)
    {
        int rc = RTPipeClose(pThis->hWakeupPipeW); AssertRC(rc);
        pThis->hWakeupPipeW = NIL_RTPIPE;
    }

    if (pThis->hWakeupPipeR != NIL_RTPIPE)
    {
        int rc = RTPipeClose(pThis->hWakeupPipeR); AssertRC(rc);
        pThis->hWakeupPipeR = NIL_RTPIPE;
    }

    if (pThis->hFileDevice != NIL_RTFILE)
    {
        int rc = RTFileClose(pThis->hFileDevice); AssertRC(rc);
        pThis->hFileDevice = NIL_RTFILE;
    }

    if (pThis->pszDevicePath)
    {
        PDMDrvHlpMMHeapFree(pDrvIns, pThis->pszDevicePath);
        pThis->pszDevicePath = NULL;
    }
#endif /* !VBOX_WITH_WIN_PARPORT_SUP */
}

/**
 * Construct a host parallel driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvHostParallelConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVHOSTPARALLEL    pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTPARALLEL);
    PCPDMDRVHLPR3       pHlp  = pDrvIns->pHlpR3;
    LogFlowFunc(("iInstance=%d\n", pDrvIns->iInstance));


    /*
     * Init basic data members and interfaces.
     *
     * Must be done before returning any failure because we've got a destructor.
     */
    pThis->hFileDevice                                  = NIL_RTFILE;
#ifndef VBOX_WITH_WIN_PARPORT_SUP
    pThis->hWakeupPipeR                                 = NIL_RTPIPE;
    pThis->hWakeupPipeW                                 = NIL_RTPIPE;
#endif

    pThis->pDrvInsR3                                    = pDrvIns;
#ifdef VBOX_WITH_DRVINTNET_IN_R0
    pThis->pDrvInsR0                                    = PDMDRVINS_2_R0PTR(pDrvIns);
#endif

    /* IBase. */
    pDrvIns->IBase.pfnQueryInterface                    = drvHostParallelQueryInterface;
    /* IHostParallelConnector. */
    pThis->IHostParallelConnectorR3.pfnWrite            = drvHostParallelWrite;
    pThis->IHostParallelConnectorR3.pfnRead             = drvHostParallelRead;
    pThis->IHostParallelConnectorR3.pfnSetPortDirection = drvHostParallelSetPortDirection;
    pThis->IHostParallelConnectorR3.pfnWriteControl     = drvHostParallelWriteControl;
    pThis->IHostParallelConnectorR3.pfnReadControl      = drvHostParallelReadControl;
    pThis->IHostParallelConnectorR3.pfnReadStatus       = drvHostParallelReadStatus;

    /*
     * Validate the config.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "DevicePath", "");

    /*
     * Query configuration.
     */
    /* Device */
    int rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "DevicePath", &pThis->pszDevicePath);
    if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: query for \"DevicePath\" string returned %Rra.\n", rc));
        return rc;
    }

    /*
     * Open the device
     */
    /** @todo exclusive access on windows?   */
    rc = RTFileOpen(&pThis->hFileDevice, pThis->pszDevicePath, RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS, N_("Parallel#%d could not open '%s'"),
                                   pDrvIns->iInstance, pThis->pszDevicePath);

#ifndef VBOX_WITH_WIN_PARPORT_SUP
    /*
     * Try to get exclusive access to parallel port
     */
    rc = ioctl(RTFileToNative(pThis->hFileDevice), PPEXCL);
    if (rc < 0)
        return PDMDrvHlpVMSetError(pDrvIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                   N_("Parallel#%d could not get exclusive access for parallel port '%s'"
                                      "Be sure that no other process or driver accesses this port"),
                                   pDrvIns->iInstance, pThis->pszDevicePath);

    /*
     * Claim the parallel port
     */
    rc = ioctl(RTFileToNative(pThis->hFileDevice), PPCLAIM);
    if (rc < 0)
        return PDMDrvHlpVMSetError(pDrvIns, RTErrConvertFromErrno(errno), RT_SRC_POS,
                                   N_("Parallel#%d could not claim parallel port '%s'"
                                      "Be sure that no other process or driver accesses this port"),
                                   pDrvIns->iInstance, pThis->pszDevicePath);

    /*
     * Get the IHostParallelPort interface of the above driver/device.
     */
    pThis->pDrvHostParallelPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIHOSTPARALLELPORT);
    if (!pThis->pDrvHostParallelPort)
        return PDMDrvHlpVMSetError(pDrvIns, VERR_PDM_MISSING_INTERFACE_ABOVE, RT_SRC_POS, N_("Parallel#%d has no parallel port interface above"),
                                   pDrvIns->iInstance);

    /*
     * Create wakeup pipe.
     */
    rc = RTPipeCreate(&pThis->hWakeupPipeR, &pThis->hWakeupPipeW, 0 /*fFlags*/);
    AssertRCReturn(rc, rc);

    /*
     * Start in SPP mode.
     */
    pThis->enmModeCur = PDM_PARALLEL_PORT_MODE_INVALID;
    rc = drvHostParallelSetMode(pThis, PDM_PARALLEL_PORT_MODE_SPP);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS, N_("HostParallel#%d cannot change mode of parallel mode to SPP"), pDrvIns->iInstance);

    /*
     * Start waiting for interrupts.
     */
    rc = PDMDrvHlpThreadCreate(pDrvIns, &pThis->pMonitorThread, pThis, drvHostParallelMonitorThread, drvHostParallelWakeupMonitorThread, 0,
                               RTTHREADTYPE_IO, "ParMon");
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS, N_("HostParallel#%d cannot create monitor thread"), pDrvIns->iInstance);

#else  /* VBOX_WITH_WIN_PARPORT_SUP */

    pThis->PortDirectData    = 0;
    pThis->PortDirectControl = 0;
    pThis->PortDirectStatus  = 0;
    rc = drvHostParallelGetWinHostIoPorts(pThis);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("HostParallel#%d: Could not get direct access to the host parallel port!! (rc=%Rrc)"),
                                   pDrvIns->iInstance, rc);

#endif /* VBOX_WITH_WIN_PARPORT_SUP */
    return VINF_SUCCESS;
}


/**
 * Char driver registration record.
 */
const PDMDRVREG g_DrvHostParallel =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "HostParallel",
    /* szRCMod */
    "",
    /* szR0Mod */
    "VBoxDDR0.r0",
    /* pszDescription */
    "Parallel host driver.",
    /* fFlags */
# if defined(VBOX_WITH_WIN_PARPORT_SUP)
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT | PDM_DRVREG_FLAGS_R0,
# else
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
# endif
    /* fClass. */
    PDM_DRVREG_CLASS_CHAR,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVHOSTPARALLEL),
    /* pfnConstruct */
    drvHostParallelConstruct,
    /* pfnDestruct */
    drvHostParallelDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};
#endif /*IN_RING3*/

