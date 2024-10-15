/* $Id: HostHardwareFreeBSD.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Code for handling hardware detection under FreeBSD, VBoxSVC.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MAIN
#include "HostHardwareLinux.h"

#include <VBox/log.h>

#include <iprt/dir.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <camlib.h>
#include <cam/scsi/scsi_pass.h>

#include <vector>


/*********************************************************************************************************************************
*   Typedefs and Defines                                                                                                         *
*********************************************************************************************************************************/
typedef enum DriveType_T
{
    Fixed,
    DVD,
    Any
} DriveType_T;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int getDriveInfoFromEnv(const char *pcszVar, DriveInfoList *pList, bool isDVD, bool *pfSuccess) RT_NOTHROW_DEF;
static int getDriveInfoFromCAM(DriveInfoList *pList, DriveType_T enmDriveType, bool *pfSuccess) RT_NOTHROW_DEF;


/** Find the length of a string, ignoring trailing non-ascii or control
 * characters
 * @note Code duplicated in HostHardwareLinux.cpp  */
static size_t strLenStripped(const char *pcsz) RT_NOTHROW_DEF
{
    size_t cch = 0;
    for (size_t i = 0; pcsz[i] != '\0'; ++i)
        if (pcsz[i] > 32 /*space*/ && pcsz[i] < 127 /*delete*/)
            cch = i;
    return cch + 1;
}


/**
 * Initialize the device description for a drive based on vendor and model name
 * strings.
 *
 * @param   pcszVendor  The raw vendor ID string.
 * @param   pcszModel   The raw product ID string.
 * @param   pszDesc     Where to store the description string (optional)
 * @param   cbDesc      The size of the buffer in @pszDesc
 *
 * @note    Used for disks as well as DVDs.
 */
/* static */
void dvdCreateDeviceString(const char *pcszVendor, const char *pcszModel, char *pszDesc, size_t cbDesc) RT_NOTHROW_DEF
{
    AssertPtrReturnVoid(pcszVendor);
    AssertPtrReturnVoid(pcszModel);
    AssertPtrNullReturnVoid(pszDesc);
    AssertReturnVoid(!pszDesc || cbDesc > 0);
    size_t cchVendor = strLenStripped(pcszVendor);
    size_t cchModel = strLenStripped(pcszModel);

    /* Construct the description string as "Vendor Product" */
    if (pszDesc)
    {
        if (cchVendor > 0)
            RTStrPrintf(pszDesc, cbDesc, "%.*s %s", cchVendor, pcszVendor,
                        cchModel > 0 ? pcszModel : "(unknown drive model)");
        else
            RTStrPrintf(pszDesc, cbDesc, "%s", pcszModel);
        RTStrPurgeEncoding(pszDesc);
    }
}


int VBoxMainDriveInfo::updateDVDs() RT_NOEXCEPT
{
    LogFlowThisFunc(("entered\n"));
    int vrc;
    try
    {
        mDVDList.clear();
        /* Always allow the user to override our auto-detection using an
         * environment variable. */
        bool fSuccess = false;  /* Have we succeeded in finding anything yet? */
        vrc = getDriveInfoFromEnv("VBOX_CDROM", &mDVDList, true /* isDVD */, &fSuccess);
        if (RT_SUCCESS(vrc) && !fSuccess)
            vrc = getDriveInfoFromCAM(&mDVDList, DVD, &fSuccess);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }
    LogFlowThisFunc(("vrc=%Rrc\n", vrc));
    return vrc;
}

int VBoxMainDriveInfo::updateFloppies() RT_NOEXCEPT
{
    LogFlowThisFunc(("entered\n"));
    int vrc;
    try
    {
        /* Only got the enviornment variable here... */
        mFloppyList.clear();
        bool fSuccess = false;  /* ignored */
        vrc = getDriveInfoFromEnv("VBOX_FLOPPY", &mFloppyList, false /* isDVD */, &fSuccess);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }
    LogFlowThisFunc(("vrc=%Rrc\n", vrc));
    return vrc;
}

int VBoxMainDriveInfo::updateFixedDrives() RT_NOEXCEPT
{
    LogFlowThisFunc(("entered\n"));
    int vrc;
    try
    {
        mFixedDriveList.clear();
        bool fSuccess = false;  /* ignored */
        vrc = getDriveInfoFromCAM(&mFixedDriveList, Fixed, &fSuccess);
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }
    LogFlowThisFunc(("vrc=%Rrc\n", vrc));
    return vrc;
}

static void strDeviceStringSCSI(device_match_result *pDevResult, char *pszDesc, size_t cbDesc) RT_NOTHROW_DEF
{
    char szVendor[128];
    cam_strvis((uint8_t *)szVendor, (const uint8_t *)pDevResult->inq_data.vendor,
               sizeof(pDevResult->inq_data.vendor), sizeof(szVendor));
    char szProduct[128];
    cam_strvis((uint8_t *)szProduct, (const uint8_t *)pDevResult->inq_data.product,
               sizeof(pDevResult->inq_data.product), sizeof(szProduct));
    dvdCreateDeviceString(szVendor, szProduct, pszDesc, cbDesc);
}

static void strDeviceStringATA(device_match_result *pDevResult, char *pszDesc, size_t cbDesc) RT_NOTHROW_DEF
{
    char szProduct[256];
    cam_strvis((uint8_t *)szProduct, (const uint8_t *)pDevResult->ident_data.model,
               sizeof(pDevResult->ident_data.model), sizeof(szProduct));
    dvdCreateDeviceString("", szProduct, pszDesc, cbDesc);
}

static void strDeviceStringSEMB(device_match_result *pDevResult, char *pszDesc, size_t cbDesc) RT_NOTHROW_DEF
{
    sep_identify_data *pSid = (sep_identify_data *)&pDevResult->ident_data;

    char szVendor[128];
    cam_strvis((uint8_t *)szVendor, (const uint8_t *)pSid->vendor_id,
               sizeof(pSid->vendor_id), sizeof(szVendor));
    char szProduct[128];
    cam_strvis((uint8_t *)szProduct, (const uint8_t *)pSid->product_id,
               sizeof(pSid->product_id), sizeof(szProduct));
    dvdCreateDeviceString(szVendor, szProduct, pszDesc, cbDesc);
}

static void strDeviceStringMMCSD(device_match_result *pDevResult, char *pszDesc, size_t cbDesc)  RT_NOTHROW_DEF
{
    struct cam_device *pDev = cam_open_btl(pDevResult->path_id, pDevResult->target_id,
                                           pDevResult->target_lun, O_RDWR, NULL);
    if (pDev == NULL)
    {
        Log(("Error while opening drive device. Error: %s\n", cam_errbuf));
        return;
    }

    union ccb *pCcb = cam_getccb(pDev);
    if (pCcb != NULL)
    {
        struct mmc_params mmcIdentData;
        RT_ZERO(mmcIdentData);

        struct ccb_dev_advinfo *pAdvi = &pCcb->cdai;
        pAdvi->ccb_h.flags = CAM_DIR_IN;
        pAdvi->ccb_h.func_code = XPT_DEV_ADVINFO;
        pAdvi->flags = CDAI_FLAG_NONE;
        pAdvi->buftype = CDAI_TYPE_MMC_PARAMS;
        pAdvi->bufsiz = sizeof(mmcIdentData);
        pAdvi->buf = (uint8_t *)&mmcIdentData;

        if (cam_send_ccb(pDev, pCcb) >= 0)
        {
            if (strlen((char *)mmcIdentData.model) > 0)
                dvdCreateDeviceString("", (const char *)mmcIdentData.model, pszDesc, cbDesc);
            else
                dvdCreateDeviceString("", mmcIdentData.card_features & CARD_FEATURE_SDIO ? "SDIO card" : "Unknown card",
                                      pszDesc, cbDesc);
        }
        else
            Log(("error sending XPT_DEV_ADVINFO CCB\n"));

        cam_freeccb(pCcb);
    }
    else
        Log(("Could not allocate CCB\n"));
    cam_close_device(pDev);
}

/** @returns boolean success indicator (true/false). */
static int nvmeGetCData(struct cam_device *pDev, struct nvme_controller_data *pCData) RT_NOTHROW_DEF
{
    bool fSuccess = false;
    union ccb *pCcb = cam_getccb(pDev);
    if (pCcb != NULL)
    {
        struct ccb_dev_advinfo *pAdvi = &pCcb->cdai;
        pAdvi->ccb_h.flags = CAM_DIR_IN;
        pAdvi->ccb_h.func_code = XPT_DEV_ADVINFO;
        pAdvi->flags = CDAI_FLAG_NONE;
        pAdvi->buftype = CDAI_TYPE_NVME_CNTRL;
        pAdvi->bufsiz = sizeof(struct nvme_controller_data);
        pAdvi->buf = (uint8_t *)pCData;
        RT_BZERO(pAdvi->buf, pAdvi->bufsiz);

        if (cam_send_ccb(pDev, pCcb) >= 0)
        {
            if (pAdvi->ccb_h.status == CAM_REQ_CMP)
                fSuccess = true;
            else
                Log(("Got CAM error %#x\n", pAdvi->ccb_h.status));
        }
        else
            Log(("Error sending XPT_DEV_ADVINFO CC\n"));
        cam_freeccb(pCcb);
    }
    else
        Log(("Could not allocate CCB\n"));
    return fSuccess;
}

static void strDeviceStringNVME(device_match_result *pDevResult, char *pszDesc, size_t cbDesc) RT_NOTHROW_DEF
{
    struct cam_device *pDev = cam_open_btl(pDevResult->path_id, pDevResult->target_id,
                                           pDevResult->target_lun, O_RDWR, NULL);
    if (pDev)
    {
        struct nvme_controller_data CData;
        if (nvmeGetCData(pDev, &CData))
        {
            char szVendor[128];
            cam_strvis((uint8_t *)szVendor, CData.mn, sizeof(CData.mn), sizeof(szVendor));
            char szProduct[128];
            cam_strvis((uint8_t *)szProduct, CData.fr, sizeof(CData.fr), sizeof(szProduct));
            dvdCreateDeviceString(szVendor, szProduct, pszDesc, cbDesc);
        }
        else
            Log(("Error while getting NVME drive info\n"));
        cam_close_device(pDev);
    }
    else
        Log(("Error while opening drive device. Error: %s\n", cam_errbuf));
}


/**
 * Search for available drives using the CAM layer.
 *
 * @returns iprt status code
 * @param   pList         the list to append the drives found to
 * @param   enmDriveType  search drives of specified type
 * @param   pfSuccess     this will be set to true if we found at least one drive
 *                        and to false otherwise.  Optional.
 */
static int getDriveInfoFromCAM(DriveInfoList *pList, DriveType_T enmDriveType, bool *pfSuccess) RT_NOTHROW_DEF
{
    RTFILE hFileXpt = NIL_RTFILE;
    int vrc = RTFileOpen(&hFileXpt, "/dev/xpt0", RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_SUCCESS(vrc))
    {
        union ccb DeviceCCB;
        struct dev_match_pattern DeviceMatchPattern;
        struct dev_match_result *paMatches = NULL;

        RT_ZERO(DeviceCCB);
        RT_ZERO(DeviceMatchPattern);

        /* We want to get all devices. */
        DeviceCCB.ccb_h.func_code  = XPT_DEV_MATCH;
        DeviceCCB.ccb_h.path_id    = CAM_XPT_PATH_ID;
        DeviceCCB.ccb_h.target_id  = CAM_TARGET_WILDCARD;
        DeviceCCB.ccb_h.target_lun = CAM_LUN_WILDCARD;

        /* Setup the pattern */
        DeviceMatchPattern.type = DEV_MATCH_DEVICE;
        DeviceMatchPattern.pattern.device_pattern.path_id    = CAM_XPT_PATH_ID;
        DeviceMatchPattern.pattern.device_pattern.target_id  = CAM_TARGET_WILDCARD;
        DeviceMatchPattern.pattern.device_pattern.target_lun = CAM_LUN_WILDCARD;
        DeviceMatchPattern.pattern.device_pattern.flags      = DEV_MATCH_INQUIRY;

#if __FreeBSD_version >= 900000
# define INQ_PAT data.inq_pat
#else
 #define INQ_PAT inq_pat
#endif
        DeviceMatchPattern.pattern.device_pattern.INQ_PAT.type = enmDriveType == Fixed ? T_DIRECT
                                                               : enmDriveType == DVD   ? T_CDROM : T_ANY;
        DeviceMatchPattern.pattern.device_pattern.INQ_PAT.media_type  = SIP_MEDIA_REMOVABLE | SIP_MEDIA_FIXED;
        DeviceMatchPattern.pattern.device_pattern.INQ_PAT.vendor[0]   = '*'; /* Matches anything */
        DeviceMatchPattern.pattern.device_pattern.INQ_PAT.product[0]  = '*'; /* Matches anything */
        DeviceMatchPattern.pattern.device_pattern.INQ_PAT.revision[0] = '*'; /* Matches anything */
#undef INQ_PAT
        DeviceCCB.cdm.num_patterns    = 1;
        DeviceCCB.cdm.pattern_buf_len = sizeof(struct dev_match_result);
        DeviceCCB.cdm.patterns        = &DeviceMatchPattern;

        /*
         * Allocate the buffer holding the matches.
         * We will allocate for 10 results and call
         * CAM multiple times if we have more results.
         */
        paMatches = (struct dev_match_result *)RTMemAllocZ(10 * sizeof(struct dev_match_result));
        if (paMatches)
        {
            DeviceCCB.cdm.num_matches   = 0;
            DeviceCCB.cdm.match_buf_len = 10 * sizeof(struct dev_match_result);
            DeviceCCB.cdm.matches       = paMatches;

            do
            {
                vrc = RTFileIoCtl(hFileXpt, CAMIOCOMMAND, &DeviceCCB, sizeof(union ccb), NULL);
                if (RT_FAILURE(vrc))
                {
                    Log(("Error while querying available CD/DVD devices vrc=%Rrc\n", vrc));
                    break;
                }

                for (unsigned i = 0; i < DeviceCCB.cdm.num_matches; i++)
                {
                    if (paMatches[i].type == DEV_MATCH_DEVICE)
                    {
                        /*
                         * The result list can contain some empty entries with DEV_RESULT_UNCONFIGURED
                         * flag set, e.g. in case of T_DIRECT. Ignore them.
                         */
                        if (   (paMatches[i].result.device_result.flags & DEV_RESULT_UNCONFIGURED)
                            == DEV_RESULT_UNCONFIGURED)
                            continue;

                        /* We have the drive now but need the appropriate device node */
                        struct device_match_result *pDevResult = &paMatches[i].result.device_result;
                        union ccb PeriphCCB;
                        struct dev_match_pattern PeriphMatchPattern;
                        struct dev_match_result aPeriphMatches[2];
                        struct periph_match_result *pPeriphResult = NULL;
                        unsigned iPeriphMatch = 0;

                        RT_ZERO(PeriphCCB);
                        RT_ZERO(PeriphMatchPattern);
                        RT_ZERO(aPeriphMatches);

                        /* This time we only want the specific nodes for the device. */
                        PeriphCCB.ccb_h.func_code  = XPT_DEV_MATCH;
                        PeriphCCB.ccb_h.path_id    = paMatches[i].result.device_result.path_id;
                        PeriphCCB.ccb_h.target_id  = paMatches[i].result.device_result.target_id;
                        PeriphCCB.ccb_h.target_lun = paMatches[i].result.device_result.target_lun;

                        /* Setup the pattern */
                        PeriphMatchPattern.type = DEV_MATCH_PERIPH;
                        PeriphMatchPattern.pattern.periph_pattern.path_id    = paMatches[i].result.device_result.path_id;
                        PeriphMatchPattern.pattern.periph_pattern.target_id  = paMatches[i].result.device_result.target_id;
                        PeriphMatchPattern.pattern.periph_pattern.target_lun = paMatches[i].result.device_result.target_lun;
                        PeriphMatchPattern.pattern.periph_pattern.flags      = (periph_pattern_flags)(  PERIPH_MATCH_PATH
                                                                                                      | PERIPH_MATCH_TARGET
                                                                                                      | PERIPH_MATCH_LUN);
                        PeriphCCB.cdm.num_patterns    = 1;
                        PeriphCCB.cdm.pattern_buf_len = sizeof(struct dev_match_result);
                        PeriphCCB.cdm.patterns        = &PeriphMatchPattern;
                        PeriphCCB.cdm.num_matches   = 0;
                        PeriphCCB.cdm.match_buf_len = sizeof(aPeriphMatches);
                        PeriphCCB.cdm.matches       = aPeriphMatches;

                        do
                        {
                            vrc = RTFileIoCtl(hFileXpt, CAMIOCOMMAND, &PeriphCCB, sizeof(union ccb), NULL);
                            if (RT_FAILURE(vrc))
                            {
                                Log(("Error while querying available periph devices vrc=%Rrc\n", vrc));
                                break;
                            }

                            for (iPeriphMatch = 0; iPeriphMatch < PeriphCCB.cdm.num_matches; iPeriphMatch++)
                            {
                                /* Ignore "passthrough mode" paths */
                                if (   aPeriphMatches[iPeriphMatch].type == DEV_MATCH_PERIPH
                                    && strcmp(aPeriphMatches[iPeriphMatch].result.periph_result.periph_name, "pass"))
                                {
                                    pPeriphResult = &aPeriphMatches[iPeriphMatch].result.periph_result;
                                    break; /* We found the periph device */
                                }
                            }

                            if (iPeriphMatch < PeriphCCB.cdm.num_matches)
                                break;

                        } while (   DeviceCCB.ccb_h.status == CAM_REQ_CMP
                                 && DeviceCCB.cdm.status == CAM_DEV_MATCH_MORE);

                        if (pPeriphResult)
                        {
                            char szPath[RTPATH_MAX];
                            RTStrPrintf(szPath, sizeof(szPath), "/dev/%s%d",
                                        pPeriphResult->periph_name, pPeriphResult->unit_number);

                            char szDesc[256] = { 0 };
                            switch (pDevResult->protocol)
                            {
                                case PROTO_SCSI:  strDeviceStringSCSI( pDevResult, szDesc, sizeof(szDesc)); break;
                                case PROTO_ATA:   strDeviceStringATA(  pDevResult, szDesc, sizeof(szDesc)); break;
                                case PROTO_MMCSD: strDeviceStringMMCSD(pDevResult, szDesc, sizeof(szDesc)); break;
                                case PROTO_SEMB:  strDeviceStringSEMB( pDevResult, szDesc, sizeof(szDesc)); break;
                                case PROTO_NVME:  strDeviceStringNVME( pDevResult, szDesc, sizeof(szDesc)); break;
                                default: break;
                            }

                            try
                            {
                                pList->push_back(DriveInfo(szPath, "", szDesc));
                            }
                            catch (std::bad_alloc &)
                            {
                                pList->clear();
                                vrc = VERR_NO_MEMORY;
                                break;
                            }
                            if (pfSuccess)
                                *pfSuccess = true;
                        }
                    }
                }
            } while (   DeviceCCB.ccb_h.status == CAM_REQ_CMP
                     && DeviceCCB.cdm.status == CAM_DEV_MATCH_MORE
                     && RT_SUCCESS(vrc));

            RTMemFree(paMatches);
        }
        else
            vrc = VERR_NO_MEMORY;

        RTFileClose(hFileXpt);
    }

    return vrc;
}


/**
 * Extract the names of drives from an environment variable and add them to a
 * list if they are valid.
 *
 * @returns iprt status code
 * @param   pcszVar     the name of the environment variable.  The variable
 *                     value should be a list of device node names, separated
 *                     by ':' characters.
 * @param   pList      the list to append the drives found to
 * @param   isDVD      are we looking for DVD drives or for floppies?
 * @param   pfSuccess  this will be set to true if we found at least one drive
 *                     and to false otherwise.  Optional.
 *
 * @note    This is duplicated in HostHardwareLinux.cpp.
 */
static int getDriveInfoFromEnv(const char *pcszVar, DriveInfoList *pList, bool isDVD, bool *pfSuccess) RT_NOTHROW_DEF
{
    AssertPtrReturn(pcszVar, VERR_INVALID_POINTER);
    AssertPtrReturn(pList, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfSuccess, VERR_INVALID_POINTER);
    LogFlowFunc(("pcszVar=%s, pList=%p, isDVD=%d, pfSuccess=%p\n", pcszVar, pList, isDVD, pfSuccess));
    int vrc = VINF_SUCCESS;
    bool success = false;
    char *pszFreeMe = RTEnvDupEx(RTENV_DEFAULT, pcszVar);

    try
    {
        char *pszCurrent = pszFreeMe;
        while (pszCurrent && *pszCurrent != '\0')
        {
            char *pszNext = strchr(pszCurrent, ':');
            if (pszNext)
                *pszNext++ = '\0';

            char szReal[RTPATH_MAX];
            char szDesc[1] = "", szUdi[1] = ""; /* differs on freebsd because no devValidateDevice */
            if (   RT_SUCCESS(RTPathReal(pszCurrent, szReal, sizeof(szReal)))
                /*&& devValidateDevice(szReal, isDVD, NULL, szDesc, sizeof(szDesc), szUdi, sizeof(szUdi)) - linux only */)
            {
                pList->push_back(DriveInfo(szReal, szUdi, szDesc));
                success = true;
            }
            pszCurrent = pszNext;
        }
        if (pfSuccess != NULL)
            *pfSuccess = success;
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }
    RTStrFree(pszFreeMe);
    LogFlowFunc(("vrc=%Rrc, success=%d\n", vrc, success));
    return vrc;
}

