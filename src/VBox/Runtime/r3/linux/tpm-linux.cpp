/* $Id: tpm-linux.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Trusted Platform Module (TPM) access, Linux variant.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_DEFAULT
#include <iprt/tpm.h>

#include <iprt/assertcompile.h>
#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/linux/sysfs.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Internal TPM instance data.
 */
typedef struct RTTPMINT
{
    /** Handle to the /dev/tpmX device. */
    RTFILE                      hTpmDev;
    /** Handle to the sysfs cancel interface. */
    RTFILE                      hTpmCancel;
    /** The deduced TPM version. */
    RTTPMVERSION                enmTpmVers;
    /** Flag whether a request is currently being executed. */
    volatile bool               fReqExec;
} RTTPMINT;
/** Pointer to the internal TPM instance data. */
typedef RTTPMINT *PRTTPMINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

RTDECL(int) RTTpmOpen(PRTTPM phTpm, uint32_t idTpm)
{
    AssertPtrReturn(phTpm, VERR_INVALID_POINTER);
    if (idTpm == RTTPM_ID_DEFAULT)
        idTpm = 0;

    int rc = VINF_SUCCESS;
    PRTTPMINT pThis = (PRTTPMINT)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
    {
        pThis->hTpmDev    = NIL_RTFILE;
        pThis->hTpmCancel = NIL_RTFILE;
        pThis->enmTpmVers = RTTPMVERSION_UNKNOWN;
        pThis->fReqExec   = false;

        rc = RTFileOpenF(&pThis->hTpmDev, RTFILE_O_OPEN | RTFILE_O_READWRITE | RTFILE_O_DENY_NONE,
                         "/dev/tpm%u", idTpm);
        if (RT_SUCCESS(rc))
        {
            /* Open the sysfs path to cancel a request, either /sys/class/tpm/tpmX/device/cancel or /sys/class/misc/tpmX/device/cancel. */
            rc = RTFileOpenF(&pThis->hTpmCancel, RTFILE_O_OPEN | RTFILE_O_WRITE | RTFILE_O_DENY_NONE,
                             "/sys/class/tpm/tpm%u/device/cancel", idTpm);
            if (rc == VERR_FILE_NOT_FOUND)
                rc = RTFileOpenF(&pThis->hTpmCancel, RTFILE_O_OPEN | RTFILE_O_WRITE | RTFILE_O_DENY_NONE,
                                 "/sys/class/misc/tpm%u/device/cancel", idTpm);
            if (   RT_SUCCESS(rc)
                || rc == VERR_FILE_NOT_FOUND)
            {
                /* Try to figure out the TPM version. */
                int64_t iVersion = 0;
                rc = RTLinuxSysFsReadIntFile(10 /*uBase*/, &iVersion, "/sys/class/tpm/tpm%u/tpm_version_major", idTpm);
                if (rc == VERR_FILE_NOT_FOUND)
                    rc = RTLinuxSysFsReadIntFile(10 /*uBase*/, &iVersion, "/sys/class/misc/tpm%u/tpm_version_major", idTpm);
                if (RT_SUCCESS(rc))
                {
                    if (iVersion == 1)
                        pThis->enmTpmVers = RTTPMVERSION_1_2;
                    else if (iVersion == 2)
                        pThis->enmTpmVers = RTTPMVERSION_2_0;
                }

                *phTpm = pThis;
                return VINF_SUCCESS;
            }

            RTFileClose(pThis->hTpmDev);
            pThis->hTpmDev = NIL_RTFILE;
        }

        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


RTDECL(int) RTTpmClose(RTTPM hTpm)
{
    PRTTPMINT pThis = hTpm;

    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    RTFileClose(pThis->hTpmDev);
    if (pThis->hTpmCancel != NIL_RTFILE)
        RTFileClose(pThis->hTpmCancel);

    pThis->hTpmDev    = NIL_RTFILE;
    pThis->hTpmCancel = NIL_RTFILE;
    RTMemFree(pThis);
    return VINF_SUCCESS;
}


RTDECL(RTTPMVERSION) RTTpmGetVersion(RTTPM hTpm)
{
    PRTTPMINT pThis = hTpm;

    AssertPtrReturn(pThis, RTTPMVERSION_INVALID);
    return pThis->enmTpmVers;
}


RTDECL(uint32_t) RTTpmGetLocalityMax(RTTPM hTpm)
{
    RT_NOREF(hTpm);
    return 0; /* On Linux only TPM locality 0 is supported. */
}


RTDECL(int) RTTpmReqCancel(RTTPM hTpm)
{
    PRTTPMINT pThis = hTpm;

    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    if (pThis->hTpmCancel == NIL_RTFILE)
        return VERR_NOT_SUPPORTED;

    if (ASMAtomicReadBool(&pThis->fReqExec))
    {
        uint8_t bCancel = '-';
        return RTFileWrite(pThis->hTpmCancel, &bCancel, sizeof(bCancel), NULL /*pcbWritten*/);
    }

    return VINF_SUCCESS;
}


RTDECL(int) RTTpmReqExec(RTTPM hTpm, uint8_t bLoc, const void *pvReq, size_t cbReq,
                         void *pvResp, size_t cbRespMax, size_t *pcbResp)
{
    PRTTPMINT pThis = hTpm;

    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pvReq, VERR_INVALID_POINTER);
    AssertPtrReturn(pvResp, VERR_INVALID_POINTER);
    AssertReturn(cbReq && cbRespMax, VERR_INVALID_PARAMETER);
    AssertReturn(bLoc == 0, VERR_NOT_SUPPORTED); /** @todo There doesn't seem to be a way to use a different locality. */

    /* The request has to be supplied by a single blocking write. */
    ASMAtomicXchgBool(&pThis->fReqExec, true);
    int rc = RTFileWrite(pThis->hTpmDev, pvReq, cbReq, NULL /*pcbWritten*/);
    if (RT_SUCCESS(rc))
    {
        size_t cbResp = 0;
        /* The response has to be retrieved in a single read as well. */
        rc = RTFileRead(pThis->hTpmDev, pvResp, cbRespMax, &cbResp);
        ASMAtomicXchgBool(&pThis->fReqExec, false);
        if (RT_SUCCESS(rc))
        {
            /* Check whether the response is complete. */
            if (   cbResp >= sizeof(TPMRESPHDR)
                && RTTpmRespGetSz((PCTPMRESPHDR)pvResp) == cbResp)
            {
                if (pcbResp)
                    *pcbResp = cbResp;
            }
            else
                rc = VERR_BUFFER_OVERFLOW;
        }
    }

    return rc;
}

