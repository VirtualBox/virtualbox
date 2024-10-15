/* $Id: DevPL031.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevPL031 - ARM PL011 PrimeCell RTC.
 *
 * The documentation for this device was taken from
 * https://developer.arm.com/documentation/ddi0224/c (2023-04-27).
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_RTC
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmifs.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/critsect.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The current serial code saved state version. */
#define PL031_SAVED_STATE_VERSION               1

/** PL011 MMIO region size in bytes. */
#define PL031_MMIO_SIZE                         _4K

/** The offset of the RTCDR register from the beginning of the region. */
#define PL031_REG_RTCDR_INDEX                   0x0
/** The offset of the RTCMR register from the beginning of the region. */
#define PL031_REG_RTCMR_INDEX                   0x4
/** The offset of the RTCLR register from the beginning of the region. */
#define PL031_REG_RTCLR_INDEX                   0x8

/** The offset of the RTCCR register from the beginning of the region. */
#define PL031_REG_RTCCR_INDEX                   0xc
/** RTC start bit. */
# define PL031_REG_RTCCR_RTC_START              RT_BIT(0)

/** The offset of the RTCIMSC register from the beginning of the region. */
#define PL031_REG_RTCIMSC_INDEX                 0x10
/** Interrupt mask bit. */
# define PL031_REG_RTCIMSC_MASK                 RT_BIT(0)

/** The offset of the RTCRIS register from the beginning of the region. */
#define PL031_REG_RTCRIS_INDEX                  0x14
/** Raw interrupt status bit. */
# define PL031_REG_RTCRIS_STS                   RT_BIT(0)

/** The offset of the RTCMIS register from the beginning of the region. */
#define PL031_REG_RTCMIS_INDEX                  0x18
/** Masked interrupt status bit. */
# define PL031_REG_RTCMIS_STS                   RT_BIT(0)

/** The offset of the RTCICR register from the beginning of the region. */
#define PL031_REG_RTCICR_INDEX                  0x1c
/** Interrupt clear bit. */
# define PL031_REG_RTCICR_CLR                   RT_BIT(0)

/** The offset of the UARTPeriphID0 register from the beginning of the region. */
#define PL031_REG_RTC_PERIPH_ID0_INDEX          0xfe0
/** The offset of the UARTPeriphID1 register from the beginning of the region. */
#define PL031_REG_RTC_PERIPH_ID1_INDEX          0xfe4
/** The offset of the UARTPeriphID2 register from the beginning of the region. */
#define PL031_REG_RTC_PERIPH_ID2_INDEX          0xfe8
/** The offset of the UARTPeriphID3 register from the beginning of the region. */
#define PL031_REG_RTC_PERIPH_ID3_INDEX          0xfec
/** The offset of the UARTPCellID0 register from the beginning of the region. */
#define PL031_REG_RTC_PCELL_ID0_INDEX           0xff0
/** The offset of the UARTPCellID1 register from the beginning of the region. */
#define PL031_REG_RTC_PCELL_ID1_INDEX           0xff4
/** The offset of the UARTPCellID2 register from the beginning of the region. */
#define PL031_REG_RTC_PCELL_ID2_INDEX           0xff8
/** The offset of the UARTPCellID3 register from the beginning of the region. */
#define PL031_REG_RTC_PCELL_ID3_INDEX           0xffc


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Shared RTC device state.
 */
typedef struct DEVPL031
{
    /** The MMIO handle. */
    IOMMMIOHANDLE                   hMmio;
    /** The second timer (pl031TimerSecond). */
    TMTIMERHANDLE                   hTimerSecond;
    /** The base MMIO address the device is registered at. */
    RTGCPHYS                        GCPhysMmioBase;
    /** The IRQ value. */
    uint16_t                        u16Irq;
    /** Flag whether to preload the load register with the current time. */
    bool                            fLoadTime;
    /** Flag whether to use UTC for the time offset. */
    bool                            fUtcOffset;

    /** @name Registers.
     * @{ */
    /** Data register. */
    uint32_t                        u32RtcDr;
    /** Match register. */
    uint32_t                        u32RtcMr;
    /** Load register. */
    uint32_t                        u32RtcLr;
    /** RTC start bit from the control register. */
    bool                            fRtcStarted;
    /** RTC interrupt masked status. */
    bool                            fRtcIrqMasked;
    /** RTC raw interrupt status. */
    bool                            fRtcIrqSts;
    /** @} */

} DEVPL031;
/** Pointer to the shared RTC device state. */
typedef DEVPL031 *PDEVPL031;


/**
 * Serial device state for ring-3.
 */
typedef struct DEVPL031R3
{
    uint32_t                        u32Dummy;
} DEVPL031R3;
/** Pointer to the serial device state for ring-3. */
typedef DEVPL031R3 *PDEVPL031R3;


/**
 * Serial device state for ring-0.
 */
typedef struct DEVPL031R0
{
    /** Dummy .*/
    uint8_t                         bDummy;
} DEVPL031R0;
/** Pointer to the serial device state for ring-0. */
typedef DEVPL031R0 *PDEVPL031R0;


/**
 * Serial device state for raw-mode.
 */
typedef struct DEVPL031RC
{
    /** Dummy .*/
    uint8_t                         bDummy;
} DEVPL031RC;
/** Pointer to the serial device state for raw-mode. */
typedef DEVPL031RC *PDEVPL031RC;

/** The serial device state for the current context. */
typedef CTX_SUFF(DEVPL031) DEVPL031CC;
/** Pointer to the serial device state for the current context. */
typedef CTX_SUFF(PDEVPL031) PDEVPL031CC;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

#ifndef VBOX_DEVICE_STRUCT_TESTCASE

/**
 * Updates the IRQ state based on the current device state.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared RTC instance data.
 */
DECLINLINE(void) pl031IrqUpdate(PPDMDEVINS pDevIns, PDEVPL031 pThis)
{
    LogFlowFunc(("pThis=%#p\n", pThis));
    if (pThis->fRtcIrqSts && !pThis->fRtcIrqMasked) /** @todo ISA is x86 specific. */
        PDMDevHlpISASetIrqNoWait(pDevIns, pThis->u16Irq, 1);
    else
        PDMDevHlpISASetIrqNoWait(pDevIns, pThis->u16Irq, 0);
}


/**
 * @callback_method_impl{FNTMTIMERDEV, Second timer.}
 */
static DECLCALLBACK(void) pl031TimerSecond(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PDEVPL031 pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL031);

    Assert(PDMDevHlpTimerIsLockOwner(pDevIns, hTimer));
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->CTX_SUFF(pCritSectRo)));
    RT_NOREF(pvUser, hTimer);

    if (pThis->fRtcStarted)
    {
        pThis->u32RtcDr++;
        if (pThis->u32RtcDr + pThis->u32RtcLr == pThis->u32RtcMr)
        {
            /* Set interrupt. */
            pThis->fRtcIrqSts = true;
            pl031IrqUpdate(pDevIns, pThis);
        }

        PDMDevHlpTimerSetMillies(pDevIns, hTimer, RT_MS_1SEC);
    }
}


/* -=-=-=-=-=- MMIO callbacks -=-=-=-=-=- */


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) pl031MmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PDEVPL031 pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL031);
    NOREF(pvUser);
    Assert(cb == 4 || cb == 8);
    Assert(!(off & (cb - 1))); RT_NOREF(cb);

    LogFlowFunc(("%RGp cb=%u\n", off, cb));

    uint32_t u32Val = 0;
    VBOXSTRICTRC rc = VINF_SUCCESS;
    switch (off)
    {
        case PL031_REG_RTCDR_INDEX:
            u32Val = pThis->u32RtcDr + pThis->u32RtcLr;
            break;
        case PL031_REG_RTCMR_INDEX:
            u32Val = pThis->u32RtcMr;
            break;
        case PL031_REG_RTCLR_INDEX:
            u32Val = pThis->u32RtcLr;
            break;
        case PL031_REG_RTCCR_INDEX:
            u32Val = pThis->fRtcStarted ? PL031_REG_RTCCR_RTC_START : 0;
            break;
        case PL031_REG_RTCIMSC_INDEX:
            u32Val = pThis->fRtcIrqMasked ? PL031_REG_RTCIMSC_MASK : 0;
            break;
        case PL031_REG_RTCRIS_INDEX:
            u32Val = pThis->fRtcIrqSts ? PL031_REG_RTCRIS_STS : 0;
            break;
        case PL031_REG_RTCMIS_INDEX:
            u32Val = (pThis->fRtcIrqSts && !pThis->fRtcIrqMasked) ? PL031_REG_RTCMIS_STS : 0;
            break;
        case PL031_REG_RTC_PERIPH_ID0_INDEX:
            u32Val = 0x31;
            break;
        case PL031_REG_RTC_PERIPH_ID1_INDEX:
            u32Val = 0x10;
            break;
        case PL031_REG_RTC_PERIPH_ID2_INDEX:
            u32Val = 0x04;
            break;
        case PL031_REG_RTC_PERIPH_ID3_INDEX:
            u32Val = 0x00;
            break;
        case PL031_REG_RTC_PCELL_ID0_INDEX:
            u32Val = 0x0d;
            break;
        case PL031_REG_RTC_PCELL_ID1_INDEX:
            u32Val = 0xf0;
            break;
        case PL031_REG_RTC_PCELL_ID2_INDEX:
            u32Val = 0x05;
            break;
        case PL031_REG_RTC_PCELL_ID3_INDEX:
            u32Val = 0xb1;
            break;
        case PL031_REG_RTCICR_INDEX: /* Writeonly */
        default:
            break;
    }

    if (rc == VINF_SUCCESS)
        *(uint32_t *)pv = u32Val;

    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) pl031MmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PDEVPL031   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL031);
    LogFlowFunc(("cb=%u reg=%RGp val=%llx\n", cb, off, cb == 4 ? *(uint32_t *)pv : cb == 8 ? *(uint64_t *)pv : 0xdeadbeef));
    RT_NOREF(pvUser);
    Assert(cb == 4 || cb == 8);
    Assert(!(off & (cb - 1))); RT_NOREF(cb);

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    uint32_t u32Val = *(uint32_t *)pv;
    switch (off)
    {
        case PL031_REG_RTCMR_INDEX:
            pThis->u32RtcMr = u32Val;
            break;
        case PL031_REG_RTCLR_INDEX:
            pThis->u32RtcLr = u32Val;
            break;
        case PL031_REG_RTCCR_INDEX:
        {
            /* Writing this resets the data register in any case. */
            pThis->u32RtcDr = 0;
            bool fRtcStart = RT_BOOL(u32Val & PL031_REG_RTCCR_RTC_START);
            if (fRtcStart ^ pThis->fRtcStarted)
            {
                pThis->fRtcStarted = fRtcStart;
                if (fRtcStart)
                {
                    PDMDevHlpTimerLockClock(pDevIns, pThis->hTimerSecond, VERR_IGNORED);
                    rcStrict = PDMDevHlpTimerSetMillies(pDevIns, pThis->hTimerSecond, RT_MS_1SEC);
                    PDMDevHlpTimerUnlockClock(pDevIns, pThis->hTimerSecond);
                }
                else
                    PDMDevHlpTimerStop(pDevIns, pThis->hTimerSecond);
            }
            break;
        }
        case PL031_REG_RTCIMSC_INDEX:
            pThis->fRtcIrqMasked = RT_BOOL(u32Val & PL031_REG_RTCIMSC_MASK);
            pl031IrqUpdate(pDevIns, pThis);
            break;
        case PL031_REG_RTCDR_INDEX: /* Readonly */
        case PL031_REG_RTCMIS_INDEX:
        case PL031_REG_RTCRIS_INDEX:
        default:
            break;
    }
    return rcStrict;
}


#ifdef IN_RING3

/* -=-=-=-=-=-=-=-=- Saved State -=-=-=-=-=-=-=-=- */

/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC}
 */
static DECLCALLBACK(int) pl031R3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PDEVPL031       pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL031);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    RT_NOREF(uPass);

    pHlp->pfnSSMPutU16(   pSSM, pThis->u16Irq);
    pHlp->pfnSSMPutGCPhys(pSSM, pThis->GCPhysMmioBase);
    pHlp->pfnSSMPutBool(  pSSM, pThis->fUtcOffset);
    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) pl031R3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVPL031       pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL031);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    /* The config. */
    pl031R3LiveExec(pDevIns, pSSM, SSM_PASS_FINAL);

    /* The state. */
    pHlp->pfnSSMPutU32( pSSM, pThis->u32RtcDr);
    pHlp->pfnSSMPutU32( pSSM, pThis->u32RtcMr);
    pHlp->pfnSSMPutU32( pSSM, pThis->u32RtcLr);
    pHlp->pfnSSMPutBool(pSSM, pThis->fRtcStarted);
    pHlp->pfnSSMPutBool(pSSM, pThis->fRtcIrqMasked);
    pHlp->pfnSSMPutBool(pSSM, pThis->fRtcIrqSts);

    return pHlp->pfnSSMPutU32(pSSM, UINT32_MAX); /* sanity/terminator */
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) pl031R3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDEVPL031       pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL031);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    int rc;

    if (uVersion != PL031_SAVED_STATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* The config. */
    uint16_t u16Irq;
    rc = pHlp->pfnSSMGetU16(pSSM, &u16Irq);
    AssertRCReturn(rc, rc);
    if (u16Irq != pThis->u16Irq)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - u16Irq: saved=%#x config=%#x"), u16Irq, pThis->u16Irq);

    RTGCPHYS GCPhysMmioBase;
    rc = pHlp->pfnSSMGetGCPhys(pSSM, &GCPhysMmioBase);
    AssertRCReturn(rc, rc);
    if (GCPhysMmioBase != pThis->GCPhysMmioBase)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - GCPhysMmioBase: saved=%RGp config=%RGp"), GCPhysMmioBase, pThis->GCPhysMmioBase);

    bool fUtcOffset;
    rc = pHlp->pfnSSMGetBool(pSSM, &fUtcOffset);
    AssertRCReturn(rc, rc);
    if (fUtcOffset != pThis->fUtcOffset)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - fUtcOffset: saved=%RTbool config=%RTbool"), fUtcOffset, pThis->fUtcOffset);

    if (uPass != SSM_PASS_FINAL)
        return VINF_SUCCESS;

    /* The state. */
    pHlp->pfnSSMGetU32( pSSM, &pThis->u32RtcDr);
    pHlp->pfnSSMGetU32( pSSM, &pThis->u32RtcMr);
    pHlp->pfnSSMGetU32( pSSM, &pThis->u32RtcLr);
    pHlp->pfnSSMGetBool(pSSM, &pThis->fRtcStarted);
    pHlp->pfnSSMGetBool(pSSM, &pThis->fRtcIrqMasked);
    pHlp->pfnSSMGetBool(pSSM, &pThis->fRtcIrqSts);

    /* The marker. */
    uint32_t u32;
    rc = pHlp->pfnSSMGetU32(pSSM, &u32);
    AssertRCReturn(rc, rc);
    AssertMsgReturn(u32 == UINT32_MAX, ("%#x\n", u32), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVLOADDONE}
 */
static DECLCALLBACK(int) pl031R3LoadDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVPL031 pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL031);

    RT_NOREF(pSSM);
    int rc = VINF_SUCCESS;
    if (pThis->fRtcStarted)
    {
        PDMDevHlpTimerLockClock(pDevIns, pThis->hTimerSecond, VERR_IGNORED);
        rc = PDMDevHlpTimerSetMillies(pDevIns, pThis->hTimerSecond, RT_MS_1SEC);
        PDMDevHlpTimerUnlockClock(pDevIns, pThis->hTimerSecond);
    }
    else
        PDMDevHlpTimerStop(pDevIns, pThis->hTimerSecond);

    return rc;
}


/* -=-=-=-=-=-=-=-=- PDMDEVREG -=-=-=-=-=-=-=-=- */

/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) pl031R3Reset(PPDMDEVINS pDevIns)
{
    PDEVPL031 pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL031);

    pThis->u32RtcDr         = 0;
    pThis->u32RtcMr         = 0;
    pThis->u32RtcLr         = 0;
    pThis->fRtcStarted      = false;
    pThis->fRtcIrqMasked    = false;
    pThis->fRtcIrqSts       = false;
    PDMDevHlpTimerStop(pDevIns, pThis->hTimerSecond);

    if (pThis->fLoadTime)
    {
        RTTIMESPEC  Now;
        PDMDevHlpTMUtcNow(pDevIns, &Now);
        if (!pThis->fUtcOffset)
        {
            RTTIME Time;
            RTTimeLocalExplode(&Time, &Now);
            RTTimeImplode(&Now, &Time);
        }

        pThis->u32RtcLr = (uint32_t)RTTimeSpecGetSeconds(&Now);
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) pl031R3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);

    /* Nothing to do. */
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) pl031R3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVPL031       pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL031);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;

    Assert(iInstance < 4);
    RT_NOREF(iInstance);

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "Irq|MmioBase|LoadTime|UtcOffset", "");

    uint16_t u16Irq = 0;
    rc = pHlp->pfnCFGMQueryU16(pCfg, "Irq", &u16Irq);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"Irq\" value"));

    RTGCPHYS GCPhysMmioBase = 0;
    rc = pHlp->pfnCFGMQueryU64(pCfg, "MmioBase", &GCPhysMmioBase);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"MmioBase\" value"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "LoadTime", &pThis->fLoadTime, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"LoadTime\" as a bool failed"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "UtcOffset", &pThis->fUtcOffset, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Querying \"UtcOffset\" as a bool failed"));

    pThis->u16Irq         = u16Irq;
    pThis->GCPhysMmioBase = GCPhysMmioBase;

    /*
     * Register and map the MMIO region.
     */
    rc = PDMDevHlpMmioCreateAndMap(pDevIns, GCPhysMmioBase, PL031_MMIO_SIZE, pl031MmioWrite, pl031MmioRead,
                                   IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED, "PL031-RTC", &pThis->hMmio);
    AssertRCReturn(rc, rc);

    /* Seconds timer. */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, pl031TimerSecond, pThis,
                              TMTIMER_FLAGS_DEFAULT_CRIT_SECT | TMTIMER_FLAGS_RING0,
                              "PL031 RTC Second", &pThis->hTimerSecond);
    AssertRCReturn(rc, rc);

    /*
     * Saved state.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, PL031_SAVED_STATE_VERSION, sizeof(*pThis), NULL,
                                NULL, pl031R3LiveExec, NULL,
                                NULL, pl031R3SaveExec, NULL,
                                NULL, pl031R3LoadExec, pl031R3LoadDone);
    AssertRCReturn(rc, rc);

    pl031R3Reset(pDevIns);
    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) pl031RZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVPL031   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL031);

    int rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, pl031MmioWrite, pl031MmioRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DevicePl031Rtc =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "arm-pl031-rtc",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_RTC,
    /* .cMaxInstances = */          UINT32_MAX,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVPL031),
    /* .cbInstanceCC = */           sizeof(DEVPL031CC),
    /* .cbInstanceRC = */           sizeof(DEVPL031RC),
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "ARM PL031 PrimeCell RTC",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           pl031R3Construct,
    /* .pfnDestruct = */            pl031R3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               pl031R3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            NULL,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           pl031RZConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           pl031RZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

