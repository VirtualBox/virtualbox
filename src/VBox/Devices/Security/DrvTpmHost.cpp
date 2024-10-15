/* $Id: DrvTpmHost.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * TPM host access driver.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_TPM_HOST
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmtpmifs.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/uuid.h>
#include <iprt/tpm.h>

#include <iprt/formats/tpm.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * TPM 1.2 buffer size capability response.
 */
#pragma pack(1)
typedef struct TPMRESPGETBUFSZ
{
    TPMRESPHDR                  Hdr;
    uint32_t                    u32Length;
    uint32_t                    cbBuf;
} TPMRESPGETBUFSZ;
#pragma pack()
typedef TPMRESPGETBUFSZ *PTPMRESPGETBUFSZ;
typedef const TPMRESPGETBUFSZ *PCTPMRESPGETBUFSZ;


/**
 * TPM 2.0 buffer size capability response.
 */
#pragma pack(1)
typedef struct TPM2RESPGETBUFSZ
{
    TPMRESPHDR                  Hdr;
    uint8_t                     fMore;
    uint32_t                    u32Cap;
    uint32_t                    u32Count;
    uint32_t                    u32Prop;
    uint32_t                    u32Value;
} TPM2RESPGETBUFSZ;
#pragma pack()
typedef TPM2RESPGETBUFSZ *PTPM2RESPGETBUFSZ;
typedef const TPM2RESPGETBUFSZ *PCTPM2RESPGETBUFSZ;


/**
 * TPM Host driver instance data.
 *
 * @implements PDMITPMCONNECTOR
 */
typedef struct DRVTPMHOST
{
    /** The stream interface. */
    PDMITPMCONNECTOR    ITpmConnector;
    /** Pointer to the driver instance. */
    PPDMDRVINS          pDrvIns;

    /** Handle to the host TPM. */
    RTTPM               hTpm;
    /** Cached TPM version. */
    TPMVERSION          enmTpmVersion;
    /** Cached buffer size of the host TPM. */
    uint32_t            cbBuffer;
} DRVTPMHOST;
/** Pointer to the TPM emulator instance data. */
typedef DRVTPMHOST *PDRVTPMHOST;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Queries the buffer size of the host TPM.
 *
 * @returns VBox status code.
 * @param   pThis               The host TPM driver instance data.
 */
static int drvTpmHostQueryBufferSize(PDRVTPMHOST pThis)
{
    uint8_t abResp[_1K];
    int rc = VINF_SUCCESS;

    switch (pThis->enmTpmVersion)
    {
        case TPMVERSION_1_2:
        {
            TPMREQGETCAPABILITY Req;

            Req.Hdr.u16Tag     = RT_H2BE_U16(TPM_TAG_RQU_COMMAND);
            Req.Hdr.cbReq      = RT_H2BE_U32(sizeof(Req));
            Req.Hdr.u32Ordinal = RT_H2BE_U32(TPM_ORD_GETCAPABILITY);
            Req.u32Cap         = RT_H2BE_U32(TPM_CAP_PROPERTY);
            Req.u32Length      = RT_H2BE_U32(sizeof(uint32_t));
            Req.u32SubCap      = RT_H2BE_U32(TPM_CAP_PROP_INPUT_BUFFER);
            rc = RTTpmReqExec(pThis->hTpm, 0 /*bLoc*/, &Req, sizeof(Req), &abResp[0], sizeof(abResp), NULL /*pcbResp*/);
            break;
        }
        case TPMVERSION_2_0:
        {
            TPM2REQGETCAPABILITY Req;

            Req.Hdr.u16Tag     = RT_H2BE_U16(TPM2_ST_NO_SESSIONS);
            Req.Hdr.cbReq      = RT_H2BE_U32(sizeof(Req));
            Req.Hdr.u32Ordinal = RT_H2BE_U32(TPM2_CC_GET_CAPABILITY);
            Req.u32Cap         = RT_H2BE_U32(TPM2_CAP_TPM_PROPERTIES);
            Req.u32Property    = RT_H2BE_U32(TPM2_PT_INPUT_BUFFER);
            Req.u32Count       = RT_H2BE_U32(1);
            rc = RTTpmReqExec(pThis->hTpm, 0 /*bLoc*/, &Req, sizeof(Req), &abResp[0], sizeof(abResp), NULL /*pcbResp*/);
            break;
        }
        default:
            AssertFailed();
    }

    if (RT_SUCCESS(rc))
    {
        switch (pThis->enmTpmVersion)
        {
            case TPMVERSION_1_2:
            {
                PCTPMRESPGETBUFSZ pResp = (PCTPMRESPGETBUFSZ)&abResp[0];

                if (   RTTpmRespGetSz(&pResp->Hdr) == sizeof(*pResp)
                    && RT_BE2H_U32(pResp->u32Length) == sizeof(uint32_t))
                    pThis->cbBuffer = RT_BE2H_U32(pResp->cbBuf);
                else
                    rc = VERR_INVALID_PARAMETER;
                break;
            }
            case TPMVERSION_2_0:
            {
                PCTPM2RESPGETBUFSZ pResp = (PCTPM2RESPGETBUFSZ)&abResp[0];

                if (   RTTpmRespGetSz(&pResp->Hdr) == sizeof(*pResp)
                    && RT_BE2H_U32(pResp->u32Count) == 1)
                    pThis->cbBuffer = RT_BE2H_U32(pResp->u32Value);
                else
                    rc = VERR_INVALID_PARAMETER;
                break;
            }
            default:
                AssertFailed();
        }
    }

    return rc;
}


/** @interface_method_impl{PDMITPMCONNECTOR,pfnGetVersion} */
static DECLCALLBACK(TPMVERSION) drvTpmHostGetVersion(PPDMITPMCONNECTOR pInterface)
{
    PDRVTPMHOST pThis = RT_FROM_MEMBER(pInterface, DRVTPMHOST, ITpmConnector);
    return pThis->enmTpmVersion;
}


/** @interface_method_impl{PDMITPMCONNECTOR,pfnGetLocalityMax} */
static DECLCALLBACK(uint32_t) drvTpmHostGetLocalityMax(PPDMITPMCONNECTOR pInterface)
{
    PDRVTPMHOST pThis = RT_FROM_MEMBER(pInterface, DRVTPMHOST, ITpmConnector);
    return RTTpmGetLocalityMax(pThis->hTpm);
}


/** @interface_method_impl{PDMITPMCONNECTOR,pfnGetBufferSize} */
static DECLCALLBACK(uint32_t) drvTpmHostGetBufferSize(PPDMITPMCONNECTOR pInterface)
{
    PDRVTPMHOST pThis = RT_FROM_MEMBER(pInterface, DRVTPMHOST, ITpmConnector);
    return pThis->cbBuffer;
}


/** @interface_method_impl{PDMITPMCONNECTOR,pfnGetEstablishedFlag} */
static DECLCALLBACK(bool) drvTpmHostGetEstablishedFlag(PPDMITPMCONNECTOR pInterface)
{
    RT_NOREF(pInterface);
    return false;
}


/** @interface_method_impl{PDMITPMCONNECTOR,pfnResetEstablishedFlag} */
static DECLCALLBACK(int) drvTpmHostResetEstablishedFlag(PPDMITPMCONNECTOR pInterface, uint8_t bLoc)
{
    RT_NOREF(pInterface, bLoc);
    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMITPMCONNECTOR,pfnCmdExec} */
static DECLCALLBACK(int) drvTpmHostCmdExec(PPDMITPMCONNECTOR pInterface, uint8_t bLoc, const void *pvCmd, size_t cbCmd, void *pvResp, size_t cbResp)
{
    RT_NOREF(bLoc);
    PDRVTPMHOST pThis = RT_FROM_MEMBER(pInterface, DRVTPMHOST, ITpmConnector);

    return RTTpmReqExec(pThis->hTpm, 0 /*bLoc*/, pvCmd, cbCmd, pvResp, cbResp, NULL /*pcbResp*/);
}


/** @interface_method_impl{PDMITPMCONNECTOR,pfnCmdCancel} */
static DECLCALLBACK(int) drvTpmHostCmdCancel(PPDMITPMCONNECTOR pInterface)
{
    PDRVTPMHOST pThis = RT_FROM_MEMBER(pInterface, DRVTPMHOST, ITpmConnector);

    return RTTpmReqCancel(pThis->hTpm);
}


/** @interface_method_impl{PDMIBASE,pfnQueryInterface} */
static DECLCALLBACK(void *) drvTpmHostQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS      pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVTPMHOST     pThis   = PDMINS_2_DATA(pDrvIns, PDRVTPMHOST);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMITPMCONNECTOR, &pThis->ITpmConnector);
    return NULL;
}


/* -=-=-=-=- PDMDRVREG -=-=-=-=- */

/** @copydoc FNPDMDRVDESTRUCT */
static DECLCALLBACK(void) drvTpmHostDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    PDRVTPMHOST pThis = PDMINS_2_DATA(pDrvIns, PDRVTPMHOST);
    LogFlow(("%s\n", __FUNCTION__));

    if (pThis->hTpm != NIL_RTTPM)
    {
        int rc = RTTpmClose(pThis->hTpm);
        AssertRC(rc);

        pThis->hTpm = NIL_RTTPM;
    }
}


/** @copydoc FNPDMDRVCONSTRUCT */
static DECLCALLBACK(int) drvTpmHostConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVTPMHOST     pThis = PDMINS_2_DATA(pDrvIns, PDRVTPMHOST);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                                  = pDrvIns;
    pThis->hTpm                                     = NIL_RTTPM;

    /* IBase */
    pDrvIns->IBase.pfnQueryInterface                = drvTpmHostQueryInterface;
    /* ITpmConnector */
    pThis->ITpmConnector.pfnGetVersion              = drvTpmHostGetVersion;
    pThis->ITpmConnector.pfnGetLocalityMax          = drvTpmHostGetLocalityMax;
    pThis->ITpmConnector.pfnGetBufferSize           = drvTpmHostGetBufferSize;
    pThis->ITpmConnector.pfnGetEstablishedFlag      = drvTpmHostGetEstablishedFlag;
    pThis->ITpmConnector.pfnResetEstablishedFlag    = drvTpmHostResetEstablishedFlag;
    pThis->ITpmConnector.pfnCmdExec                 = drvTpmHostCmdExec;
    pThis->ITpmConnector.pfnCmdCancel               = drvTpmHostCmdCancel;

    /*
     * Validate and read the configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "TpmId", "");

    uint32_t idTpm = RTTPM_ID_DEFAULT;
    int rc = pHlp->pfnCFGMQueryU32Def(pCfg, "TpmId", &idTpm, RTTPM_ID_DEFAULT);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("Configuration error: querying \"TpmId\" resulted in %Rrc"), rc);

    rc = RTTpmOpen(&pThis->hTpm, idTpm);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("DrvTpmHost%d: Opening TPM with id %u failed with %Rrc"), pDrvIns->iInstance, idTpm, rc);

    RTTPMVERSION enmVersion = RTTpmGetVersion(pThis->hTpm);

    switch (enmVersion)
    {
        case RTTPMVERSION_1_2:
            pThis->enmTpmVersion = TPMVERSION_1_2;
            break;
        case RTTPMVERSION_2_0:
            pThis->enmTpmVersion = TPMVERSION_2_0;
            break;
        case RTTPMVERSION_UNKNOWN:
        default:
            return PDMDrvHlpVMSetError(pDrvIns, VERR_NOT_SUPPORTED, RT_SRC_POS,
                                       N_("DrvTpmHost%d: TPM version %u of TPM id %u is not supported"),
                                       pDrvIns->iInstance, enmVersion, idTpm);
    }

    rc = drvTpmHostQueryBufferSize(pThis);
    if (RT_FAILURE(rc))
        return PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS,
                                   N_("DrvTpmHost%d: Querying input buffer size of TPM with id %u failed with %Rrc"),
                                   pDrvIns->iInstance, idTpm, rc);

    LogRel(("DrvTpmHost#%d: Connected to TPM %u.\n", pDrvIns->iInstance, idTpm));
    return VINF_SUCCESS;
}


/**
 * TPM host driver registration record.
 */
const PDMDRVREG g_DrvTpmHost =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "TpmHost",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "TPM host driver.",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_STREAM,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVTPMHOST),
    /* pfnConstruct */
    drvTpmHostConstruct,
    /* pfnDestruct */
    drvTpmHostDestruct,
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

