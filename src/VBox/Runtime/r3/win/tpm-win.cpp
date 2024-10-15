/* $Id: tpm-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Trusted Platform Module (TPM) access, Windows variant.
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
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/once.h>

#include "internal-r3-win.h"

#include <iprt/win/windows.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/* tbs.dll: */
typedef struct TBS_CONTEXT_PARAMS2
{
    UINT32 version;
    union
    {
        struct
        {
            UINT32 requestRaw:   1;
            UINT32 includeTpm12: 1;
            UINT32 includeTpm20: 1;
        } Fields;

        UINT32 asUINT32;
    } u;
} TBS_CONTEXT_PARAMS2;

typedef struct TBS_DEVICE_INFO
{
    UINT32 structVersion;
    UINT32 tpmVersion;
    UINT32 tpmInterfaceType;
    UINT32 tpmImpRevision;
} TBS_DEVICE_INFO;

#define TPM_VERSION_12 1
#define TPM_VERSION_20 2

#define TBS_SUCCESS                 S_OK
#define TBS_COMMAND_PRIORITY_NORMAL 200

typedef UINT32 TBS_RESULT;
typedef void *TBS_HCONTEXT;
typedef TBS_RESULT  (WINAPI *PFNTBSI_CONTEXT_CREATE)(const TBS_CONTEXT_PARAMS2 *, TBS_HCONTEXT *);
typedef TBS_RESULT  (WINAPI *PFNTBSI_CONTEXT_CLOSE)(TBS_HCONTEXT);
typedef TBS_RESULT  (WINAPI *PFNTBSI_GET_DEVICE_INFO)(UINT32, TBS_DEVICE_INFO *);
typedef TBS_RESULT  (WINAPI *PFNTBSI_CANCEL_COMMANDS)(TBS_HCONTEXT);
typedef TBS_RESULT  (WINAPI *PFNTBSI_SUBMIT_COMMANDS)(TBS_HCONTEXT, UINT32, UINT32, const BYTE *, UINT32, PBYTE, PUINT32);


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Internal TPM instance data.
 */
typedef struct RTTPMINT
{
    /** Handle to the TPM context. */
    TBS_HCONTEXT                hCtx;
    /** The deduced TPM version. */
    RTTPMVERSION                enmTpmVers;
} RTTPMINT;
/** Pointer to the internal TPM instance data. */
typedef RTTPMINT *PRTTPMINT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Init once structure. */
static RTONCE                   g_rtTpmWinInitOnce              = RTONCE_INITIALIZER;
/* tbs.dll: */
static PFNTBSI_CONTEXT_CREATE   g_pfnTbsiContextCreate  = NULL;
static PFNTBSI_CONTEXT_CLOSE    g_pfnTbsiContextClose   = NULL;
static PFNTBSI_GET_DEVICE_INFO  g_pfnTbsiGetDeviceInfo  = NULL;
static PFNTBSI_CANCEL_COMMANDS  g_pfnTbsiCancelCommands = NULL;
static PFNTBSI_SUBMIT_COMMANDS  g_pfnTbsiSubmitCommands = NULL;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Initialize the globals.
 *
 * @returns IPRT status code.
 * @param   pvUser              Ignored.
 */
static DECLCALLBACK(int32_t) rtTpmWinInitOnce(void *pvUser)
{
    RT_NOREF(pvUser);
    RTLDRMOD hMod;

    int rc = RTLdrLoadSystem("tbs.dll", true /*fNoUnload*/, &hMod);
    if (RT_SUCCESS(rc))
    {
        rc = RTLdrGetSymbol(hMod, "Tbsi_Context_Create", (void **)&g_pfnTbsiContextCreate);
        if (RT_FAILURE(rc)) return rc;

        rc = RTLdrGetSymbol(hMod, "Tbsip_Context_Close", (void **)&g_pfnTbsiContextClose);
        if (RT_FAILURE(rc)) return rc;

        rc = RTLdrGetSymbol(hMod, "Tbsip_Cancel_Commands", (void **)&g_pfnTbsiCancelCommands);
        if (RT_FAILURE(rc)) return rc;

        rc = RTLdrGetSymbol(hMod, "Tbsip_Submit_Command", (void **)&g_pfnTbsiSubmitCommands);
        if (RT_FAILURE(rc)) return rc;

        rc = RTLdrGetSymbol(hMod, "Tbsi_GetDeviceInfo", (void **)&g_pfnTbsiGetDeviceInfo);
        if (RT_FAILURE(rc)) { g_pfnTbsiGetDeviceInfo = NULL; Assert(g_enmWinVer < kRTWinOSType_8); }

        RTLdrClose(hMod);
    }

    return rc;
}


RTDECL(int) RTTpmOpen(PRTTPM phTpm, uint32_t idTpm)
{
    AssertPtrReturn(phTpm, VERR_INVALID_POINTER);
    if (idTpm == RTTPM_ID_DEFAULT)
        idTpm = 0;

    AssertReturn(idTpm == 0, VERR_NOT_SUPPORTED);

    /*
     * Initialize the globals.
     */
    int rc = RTOnce(&g_rtTpmWinInitOnce, rtTpmWinInitOnce, NULL);
    AssertRCReturn(rc, rc);

    PRTTPMINT pThis = (PRTTPMINT)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
    {
        TBS_CONTEXT_PARAMS2 CtxParams; RT_ZERO(CtxParams);

        CtxParams.version = TPM_VERSION_12;
        if (g_pfnTbsiGetDeviceInfo)
        {
            /* TPM2 support is available starting with Win8 which has Tbsi_GetDeviceInfo available. */
            TBS_DEVICE_INFO DevInfo; RT_ZERO(DevInfo);

            DevInfo.structVersion = TPM_VERSION_20;
            TBS_RESULT rcTbs = g_pfnTbsiGetDeviceInfo(sizeof(DevInfo), &DevInfo);
            if (rcTbs == TBS_SUCCESS)
            {
                CtxParams.version = TPM_VERSION_20;
                if (DevInfo.tpmVersion == TPM_VERSION_20)
                {
                    pThis->enmTpmVers = RTTPMVERSION_2_0;
                    CtxParams.u.Fields.includeTpm20 = 1;
                }
                else
                {
                    Assert(DevInfo.tpmVersion == TPM_VERSION_12);
                    pThis->enmTpmVers = RTTPMVERSION_1_2;
                    CtxParams.u.Fields.includeTpm12 = 1;
                }
            }
            else
                rc = VERR_NOT_FOUND;
        }
        else
            pThis->enmTpmVers = RTTPMVERSION_1_2;

        if (RT_SUCCESS(rc))
        {
            TBS_RESULT rcTbs = g_pfnTbsiContextCreate(&CtxParams, &pThis->hCtx);
            if (rcTbs == TBS_SUCCESS)
            {
                *phTpm = pThis;
                return VINF_SUCCESS;
            }
            else
                rc = VERR_NOT_FOUND;
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

    TBS_RESULT rcTbs = g_pfnTbsiContextClose(pThis->hCtx);
    Assert(rcTbs == TBS_SUCCESS); RT_NOREF(rcTbs);

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
    return 0; /* Only TPM locality 0 is supported. */
}


RTDECL(int) RTTpmReqCancel(RTTPM hTpm)
{
    PRTTPMINT pThis = hTpm;

    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    TBS_RESULT rcTbs = g_pfnTbsiCancelCommands(pThis->hCtx);
    if (rcTbs != TBS_SUCCESS)
        return VERR_DEV_IO_ERROR;

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
    AssertReturn(cbReq == (UINT32)cbReq && cbRespMax == (UINT32)cbRespMax, VERR_BUFFER_OVERFLOW);
    AssertReturn(bLoc == 0, VERR_NOT_SUPPORTED); /* TBS doesn't support another locality than 0. */

    UINT32 cbResult = (UINT32)cbRespMax;
    TBS_RESULT rcTbs = g_pfnTbsiSubmitCommands(pThis->hCtx, 0 /*Locality*/, TBS_COMMAND_PRIORITY_NORMAL,
                                               (const BYTE *)pvReq, (UINT32)cbReq, (BYTE *)pvResp, &cbResult);
    if (rcTbs == TBS_SUCCESS)
    {
        if (pcbResp)
            *pcbResp = cbResult;
        return VINF_SUCCESS;
    }

    return VERR_DEV_IO_ERROR;
}

