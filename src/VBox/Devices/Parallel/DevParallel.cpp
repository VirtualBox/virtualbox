/* $Id: DevParallel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevParallel - Parallel (Port) Device Emulation.
 *
 * Contributed by: Alexander Eichner
 * Based on DevSerial.cpp
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
#define LOG_GROUP LOG_GROUP_DEV_PARALLEL
#include <VBox/vmm/pdmdev.h>
#include <VBox/AssertGuest.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define PARALLEL_SAVED_STATE_VERSION 1

/* defines for accessing the register bits */
#define LPT_STATUS_BUSY                0x80
#define LPT_STATUS_ACK                 0x40
#define LPT_STATUS_PAPER_OUT           0x20
#define LPT_STATUS_SELECT_IN           0x10
#define LPT_STATUS_ERROR               0x08
#define LPT_STATUS_IRQ                 0x04
#define LPT_STATUS_BIT1                0x02 /* reserved (only for completeness) */
#define LPT_STATUS_EPP_TIMEOUT         0x01

#define LPT_CONTROL_BIT7               0x80 /* reserved (only for completeness) */
#define LPT_CONTROL_BIT6               0x40 /* reserved (only for completeness) */
#define LPT_CONTROL_ENABLE_BIDIRECT    0x20
#define LPT_CONTROL_ENABLE_IRQ_VIA_ACK 0x10
#define LPT_CONTROL_SELECT_PRINTER     0x08
#define LPT_CONTROL_RESET              0x04
#define LPT_CONTROL_AUTO_LINEFEED      0x02
#define LPT_CONTROL_STROBE             0x01

/** mode defines for the extended control register */
#define LPT_ECP_ECR_CHIPMODE_MASK      0xe0
#define LPT_ECP_ECR_CHIPMODE_GET_BITS(reg) ((reg) >> 5)
#define LPT_ECP_ECR_CHIPMODE_SET_BITS(val) ((val) << 5)
#define LPT_ECP_ECR_CHIPMODE_CONFIGURATION 0x07
#define LPT_ECP_ECR_CHIPMODE_FIFO_TEST 0x06
#define LPT_ECP_ECR_CHIPMODE_RESERVED  0x05
#define LPT_ECP_ECR_CHIPMODE_EPP       0x04
#define LPT_ECP_ECR_CHIPMODE_ECP_FIFO  0x03
#define LPT_ECP_ECR_CHIPMODE_PP_FIFO   0x02
#define LPT_ECP_ECR_CHIPMODE_BYTE      0x01
#define LPT_ECP_ECR_CHIPMODE_COMPAT    0x00

/** FIFO status bits in extended control register */
#define LPT_ECP_ECR_FIFO_MASK          0x03
#define LPT_ECP_ECR_FIFO_SOME_DATA     0x00
#define LPT_ECP_ECR_FIFO_FULL          0x02
#define LPT_ECP_ECR_FIFO_EMPTY         0x01

#define LPT_ECP_CONFIGA_FIFO_WITDH_MASK 0x70
#define LPT_ECP_CONFIGA_FIFO_WIDTH_GET_BITS(reg) ((reg) >> 4)
#define LPT_ECP_CONFIGA_FIFO_WIDTH_SET_BITS(val) ((val) << 4)
#define LPT_ECP_CONFIGA_FIFO_WIDTH_16   0x00
#define LPT_ECP_CONFIGA_FIFO_WIDTH_32   0x20
#define LPT_ECP_CONFIGA_FIFO_WIDTH_8    0x10

#define LPT_ECP_FIFO_DEPTH 2


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The shared parallel device state.
 */
typedef struct PARALLELPORT
{
    /** Flag whether an EPP timeout occurred (error handling). */
    bool                fEppTimeout;
    bool                fAlignment1;
    /** Base I/O port of the parallel port. */
    RTIOPORT            IOBase;
    /** IRQ number assigned ot the parallel port. */
    int32_t             iIrq;
    /** Data register. */
    uint8_t             regData;
    /** Status register. */
    uint8_t             regStatus;
    /** Control register. */
    uint8_t             regControl;
    /** EPP address register. */
    uint8_t             regEppAddr;
    /** EPP data register. */
    uint8_t             regEppData;
    /** More alignment. */
    uint8_t             abAlignment2[3];

#if 0 /* Data for ECP implementation, currently unused. */
    uint8_t             reg_ecp_ecr;
    uint8_t             reg_ecp_base_plus_400h; /* has different meanings */
    uint8_t             reg_ecp_config_b;

    /** The ECP FIFO implementation*/
    uint8_t             ecp_fifo[LPT_ECP_FIFO_DEPTH];
    uint8_t             abAlignemnt[3];
    int32_t             act_fifo_pos_write;
    int32_t             act_fifo_pos_read;
#endif
    /** Handle to the regular I/O ports. */
    IOMIOPORTHANDLE     hIoPorts;
    /** Handle to the ECP I/O ports. */
    IOMIOPORTHANDLE     hIoPortsEcp;
} PARALLELPORT;
/** Pointer to the shared parallel device state. */
typedef PARALLELPORT *PPARALLELPORT;


/**
 * The parallel device state for ring-3.
 *
 * @implements  PDMIBASE
 * @implements  PDMIHOSTPARALLELPORT
 */
typedef struct PARALLELPORTR3
{
    /** Pointer to the device instance.
     * @note Only for getting our bearings when arriving here via an interface
     *       method. */
    PPDMDEVINSR3                            pDevIns;
    /** LUN\#0: The base interface. */
    PDMIBASE                                IBase;
    /** LUN\#0: The host device port interface. */
    PDMIHOSTPARALLELPORT                    IHostParallelPort;
    /** Pointer to the attached base driver. */
    R3PTRTYPE(PPDMIBASE)                    pDrvBase;
    /** Pointer to the attached host device. */
    R3PTRTYPE(PPDMIHOSTPARALLELCONNECTOR)   pDrvHostParallelConnector;
} PARALLELPORTR3;
/** Pointer to the parallel device state for ring-3. */
typedef PARALLELPORTR3 *PPARALLELPORTR3;


#ifndef VBOX_DEVICE_STRUCT_TESTCASE /* Rest of file, does not count wrt indentation. */

#ifdef IN_RING3

static void parallelR3IrqSet(PPDMDEVINS pDevIns, PARALLELPORT *pThis)
{
    if (pThis->regControl & LPT_CONTROL_ENABLE_IRQ_VIA_ACK)
    {
        LogFlowFunc(("%d 1\n", pThis->iIrq));
        PDMDevHlpISASetIrqNoWait(pDevIns, pThis->iIrq, 1);
    }
}

static void parallelR3IrqClear(PPDMDEVINS pDevIns, PARALLELPORT *pThis)
{
    LogFlowFunc(("%d 0\n", pThis->iIrq));
    PDMDevHlpISASetIrqNoWait(pDevIns, pThis->iIrq, 0);
}

#endif /* IN_RING3 */

#if 0
static int parallel_ioport_write_ecp(void *opaque, uint32_t addr, uint32_t val)
{
    PARALLELPORT *s = (PARALLELPORT *)opaque;
    unsigned char ch;

    addr &= 7;
    LogFlow(("parallel: write ecp addr=0x%02x val=0x%02x\n", addr, val));
    ch = val;
    switch (addr) {
    default:
    case 0:
        if (LPT_ECP_ECR_CHIPMODE_GET_BITS(s->reg_ecp_ecr) == LPT_ECP_ECR_CHIPMODE_FIFO_TEST) {
            s->ecp_fifo[s->act_fifo_pos_write] = ch;
            s->act_fifo_pos_write++;
            if (s->act_fifo_pos_write < LPT_ECP_FIFO_DEPTH) {
                /* FIFO has some data (clear both FIFO bits) */
                s->reg_ecp_ecr &= ~(LPT_ECP_ECR_FIFO_EMPTY | LPT_ECP_ECR_FIFO_FULL);
            } else {
                /* FIFO is full */
                /* Clear FIFO empty bit */
                s->reg_ecp_ecr &= ~LPT_ECP_ECR_FIFO_EMPTY;
                /* Set FIFO full bit */
                s->reg_ecp_ecr |= LPT_ECP_ECR_FIFO_FULL;
                s->act_fifo_pos_write = 0;
            }
        } else {
            s->reg_ecp_base_plus_400h = ch;
        }
        break;
    case 1:
        s->reg_ecp_config_b = ch;
        break;
    case 2:
        /* If we change the mode clear FIFO */
        if ((ch & LPT_ECP_ECR_CHIPMODE_MASK) != (s->reg_ecp_ecr & LPT_ECP_ECR_CHIPMODE_MASK)) {
            /* reset the fifo */
            s->act_fifo_pos_write = 0;
            s->act_fifo_pos_read = 0;
            /* Set FIFO empty bit */
            s->reg_ecp_ecr |= LPT_ECP_ECR_FIFO_EMPTY;
            /* Clear FIFO full bit */
            s->reg_ecp_ecr &= ~LPT_ECP_ECR_FIFO_FULL;
        }
        /* Set new mode */
        s->reg_ecp_ecr |= LPT_ECP_ECR_CHIPMODE_SET_BITS(LPT_ECP_ECR_CHIPMODE_GET_BITS(ch));
        break;
    case 3:
        break;
    case 4:
        break;
    case 5:
        break;
    case 6:
        break;
    case 7:
        break;
    }
    return VINF_SUCCESS;
}

static uint32_t parallel_ioport_read_ecp(void *opaque, uint32_t addr, int *pRC)
{
    PARALLELPORT *s = (PARALLELPORT *)opaque;
    uint32_t ret = ~0U;

    *pRC = VINF_SUCCESS;

    addr &= 7;
    switch (addr) {
    default:
    case 0:
        if (LPT_ECP_ECR_CHIPMODE_GET_BITS(s->reg_ecp_ecr) == LPT_ECP_ECR_CHIPMODE_FIFO_TEST) {
            ret = s->ecp_fifo[s->act_fifo_pos_read];
            s->act_fifo_pos_read++;
            if (s->act_fifo_pos_read == LPT_ECP_FIFO_DEPTH)
                s->act_fifo_pos_read = 0; /* end of FIFO, start at beginning */
            if (s->act_fifo_pos_read == s->act_fifo_pos_write) {
                /* FIFO is empty */
                /* Set FIFO empty bit */
                s->reg_ecp_ecr |= LPT_ECP_ECR_FIFO_EMPTY;
                /* Clear FIFO full bit */
                s->reg_ecp_ecr &= ~LPT_ECP_ECR_FIFO_FULL;
            } else {
                /* FIFO has some data (clear all FIFO bits) */
                s->reg_ecp_ecr &= ~(LPT_ECP_ECR_FIFO_EMPTY | LPT_ECP_ECR_FIFO_FULL);
            }
        } else {
            ret = s->reg_ecp_base_plus_400h;
        }
        break;
    case 1:
        ret = s->reg_ecp_config_b;
        break;
    case 2:
        ret = s->reg_ecp_ecr;
        break;
    case 3:
        break;
    case 4:
        break;
    case 5:
        break;
    case 6:
        break;
    case 7:
        break;
    }
    LogFlow(("parallel: read ecp addr=0x%02x val=0x%02x\n", addr, ret));
    return ret;
}
#endif

#ifdef IN_RING3
/**
 * @interface_method_impl{PDMIHOSTPARALLELPORT,pfnNotifyInterrupt}
 */
static DECLCALLBACK(int) parallelR3NotifyInterrupt(PPDMIHOSTPARALLELPORT pInterface)
{
    PPARALLELPORTR3 pThisCC = RT_FROM_MEMBER(pInterface, PARALLELPORTR3, IHostParallelPort);
    PPDMDEVINS      pDevIns = pThisCC->pDevIns;
    PPARALLELPORT   pThis   = PDMDEVINS_2_DATA(pDevIns, PPARALLELPORT);

    int rc = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VINF_SUCCESS);
    AssertRCReturn(rc, rc);

    parallelR3IrqSet(pDevIns, pThis);

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);

    return VINF_SUCCESS;
}
#endif /* IN_RING3 */


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC)
parallelIoPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PPARALLELPORT   pThis   = PDMDEVINS_2_DATA(pDevIns, PPARALLELPORT);
#ifdef IN_RING3
    PPARALLELPORTR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPARALLELPORTR3);
#endif
    VBOXSTRICTRC      rc    = VINF_SUCCESS;
    RT_NOREF_PV(pvUser);

    if (cb == 1)
    {
        uint8_t u8 = u32;

        Log2(("%s: Port=%#06x+%x val %#04x\n", __FUNCTION__, pThis->IOBase, offPort, u32));

        offPort &= 7;
        switch (offPort)
        {
            case 0:
#ifndef IN_RING3
                NOREF(u8);
                rc = VINF_IOM_R3_IOPORT_WRITE;
#else
                pThis->regData = u8;
                if (RT_LIKELY(pThisCC->pDrvHostParallelConnector))
                {
                    LogFlowFunc(("Set data lines 0x%X\n", u8));
                    rc = pThisCC->pDrvHostParallelConnector->pfnWrite(pThisCC->pDrvHostParallelConnector, &u8, 1, PDM_PARALLEL_PORT_MODE_SPP);
                    AssertRC(VBOXSTRICTRC_VAL(rc));
                }
#endif
                break;
            case 1:
                break;
            case 2:
                /* Set the reserved bits to one */
                u8 |= (LPT_CONTROL_BIT6 | LPT_CONTROL_BIT7);
                if (u8 != pThis->regControl)
                {
#ifndef IN_RING3
                    return VINF_IOM_R3_IOPORT_WRITE;
#else
                    if (RT_LIKELY(pThisCC->pDrvHostParallelConnector))
                    {
                        /* Set data direction. */
                        if (u8 & LPT_CONTROL_ENABLE_BIDIRECT)
                            rc = pThisCC->pDrvHostParallelConnector->pfnSetPortDirection(pThisCC->pDrvHostParallelConnector, false /* fForward */);
                        else
                            rc = pThisCC->pDrvHostParallelConnector->pfnSetPortDirection(pThisCC->pDrvHostParallelConnector, true /* fForward */);
                        AssertRC(VBOXSTRICTRC_VAL(rc));

                        u8 &= ~LPT_CONTROL_ENABLE_BIDIRECT; /* Clear bit. */

                        rc = pThisCC->pDrvHostParallelConnector->pfnWriteControl(pThisCC->pDrvHostParallelConnector, u8);
                        AssertRC(VBOXSTRICTRC_VAL(rc));
                    }
                    else
                        u8 &= ~LPT_CONTROL_ENABLE_BIDIRECT; /* Clear bit. */

                    pThis->regControl = u8;
#endif
                }
                break;
            case 3:
#ifndef IN_RING3
                NOREF(u8);
                rc = VINF_IOM_R3_IOPORT_WRITE;
#else
                pThis->regEppAddr = u8;
                if (RT_LIKELY(pThisCC->pDrvHostParallelConnector))
                {
                    LogFlowFunc(("Write EPP address 0x%X\n", u8));
                    rc = pThisCC->pDrvHostParallelConnector->pfnWrite(pThisCC->pDrvHostParallelConnector, &u8, 1, PDM_PARALLEL_PORT_MODE_EPP_ADDR);
                    AssertRC(VBOXSTRICTRC_VAL(rc));
                }
#endif
                break;
            case 4:
#ifndef IN_RING3
                NOREF(u8);
                rc = VINF_IOM_R3_IOPORT_WRITE;
#else
                pThis->regEppData = u8;
                if (RT_LIKELY(pThisCC->pDrvHostParallelConnector))
                {
                    LogFlowFunc(("Write EPP data 0x%X\n", u8));
                    rc = pThisCC->pDrvHostParallelConnector->pfnWrite(pThisCC->pDrvHostParallelConnector, &u8, 1, PDM_PARALLEL_PORT_MODE_EPP_DATA);
                    AssertRC(VBOXSTRICTRC_VAL(rc));
                }
#endif
                break;
            case 5:
                break;
            case 6:
                break;
            case 7:
            default:
                break;
        }
    }
    else
        ASSERT_GUEST_MSG_FAILED(("Port=%#x+%x cb=%d u32=%#x\n", pThis->IOBase, offPort, cb, u32));

    return rc;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 */
static DECLCALLBACK(VBOXSTRICTRC)
parallelIoPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PPARALLELPORT   pThis   = PDMDEVINS_2_DATA(pDevIns, PPARALLELPORT);
#ifdef IN_RING3
    PPARALLELPORTR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPARALLELPORTR3);
#endif
    VBOXSTRICTRC    rc      = VINF_SUCCESS;
    RT_NOREF_PV(pvUser);

    if (cb == 1)
    {
        offPort &= 7;
        switch (offPort)
        {
            case 0:
                if (!(pThis->regControl & LPT_CONTROL_ENABLE_BIDIRECT))
                    *pu32 = pThis->regData;
                else
                {
#ifndef IN_RING3
                    rc = VINF_IOM_R3_IOPORT_READ;
#else
                    if (RT_LIKELY(pThisCC->pDrvHostParallelConnector))
                    {
                        rc = pThisCC->pDrvHostParallelConnector->pfnRead(pThisCC->pDrvHostParallelConnector, &pThis->regData,
                                                                       1, PDM_PARALLEL_PORT_MODE_SPP);
                        Log(("Read data lines 0x%X\n", pThis->regData));
                        AssertRC(VBOXSTRICTRC_VAL(rc));
                    }
                    *pu32 = pThis->regData;
#endif
                }
                break;
            case 1:
#ifndef IN_RING3
                rc = VINF_IOM_R3_IOPORT_READ;
#else
                if (RT_LIKELY(pThisCC->pDrvHostParallelConnector))
                {
                    rc = pThisCC->pDrvHostParallelConnector->pfnReadStatus(pThisCC->pDrvHostParallelConnector, &pThis->regStatus);
                    AssertRC(VBOXSTRICTRC_VAL(rc));
                }
                *pu32 = pThis->regStatus;
                parallelR3IrqClear(pDevIns, pThis);
#endif
                break;
            case 2:
#ifndef IN_RING3
                rc = VINF_IOM_R3_IOPORT_READ;
#else
                if (RT_LIKELY(pThisCC->pDrvHostParallelConnector))
                {
                    rc = pThisCC->pDrvHostParallelConnector->pfnReadControl(pThisCC->pDrvHostParallelConnector, &pThis->regControl);
                    AssertRC(VBOXSTRICTRC_VAL(rc));
                    pThis->regControl |= LPT_CONTROL_BIT6 | LPT_CONTROL_BIT7;
                }

                *pu32 = pThis->regControl;
#endif
                break;
            case 3:
#ifndef IN_RING3
                rc = VINF_IOM_R3_IOPORT_READ;
#else
                if (RT_LIKELY(pThisCC->pDrvHostParallelConnector))
                {
                    rc = pThisCC->pDrvHostParallelConnector->pfnRead(pThisCC->pDrvHostParallelConnector, &pThis->regEppAddr,
                                                                   1, PDM_PARALLEL_PORT_MODE_EPP_ADDR);
                    Log(("Read EPP address 0x%X\n", pThis->regEppAddr));
                    AssertRC(VBOXSTRICTRC_VAL(rc));
                }
                *pu32 = pThis->regEppAddr;
#endif
                break;
            case 4:
#ifndef IN_RING3
                rc = VINF_IOM_R3_IOPORT_READ;
#else
                if (RT_LIKELY(pThisCC->pDrvHostParallelConnector))
                {
                    rc = pThisCC->pDrvHostParallelConnector->pfnRead(pThisCC->pDrvHostParallelConnector, &pThis->regEppData,
                                                                   1, PDM_PARALLEL_PORT_MODE_EPP_DATA);
                    Log(("Read EPP data 0x%X\n", pThis->regEppData));
                    AssertRC(VBOXSTRICTRC_VAL(rc));
                }
                *pu32 = pThis->regEppData;
#endif
                break;
            case 5:
                break;
            case 6:
                break;
            case 7:
                break;
        }
    }
    else
        rc = VERR_IOM_IOPORT_UNUSED;

    return rc;
}

#if 0
/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, ECP registers.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
parallelIoPortWriteECP(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PPARALLELPORT  pThis = PDMDEVINS_2_DATA(pDevIns, PPARALLELPORT);
    VBOXSTRICTRC   rc    = VINF_SUCCESS;

    if (cb == 1)
    {
        Log2(("%s: ecp port %#06x+%x val %#04x\n", __FUNCTION__, pThis->IOBase + 0x400, offPort, u32));
        rc = parallel_ioport_write_ecp(pThis, Port, u32);
    }
    else
        ASSERT_GUEST_MSG_FAILED(("Port=%#x cb=%d u32=%#x\n", Port, cb, u32));

    return rc;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, ECP registers.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
parallelIoPortReadECP(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PPARALLELPORT  pThis = PDMDEVINS_2_DATA(pDevIns, PPARALLELPORT);
    VBOXSTRICTRC   rc    = VINF_SUCCESS;

    if (cb == 1)
    {
        *pu32 = parallel_ioport_read_ecp(pThis, Port, &rc);
        Log2(("%s: ecp port %#06x+%x val %#04x\n", __FUNCTION__, pThis->IOBase + 0x400, offPort, *pu32));
    }
    else
        rc = VERR_IOM_IOPORT_UNUSED;

    return rc;
}
#endif

#ifdef IN_RING3

/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC}
 */
static DECLCALLBACK(int) parallelR3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PPARALLELPORT   pThis = PDMDEVINS_2_DATA(pDevIns, PPARALLELPORT);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    RT_NOREF(uPass);

    pHlp->pfnSSMPutS32(pSSM, pThis->iIrq);
    pHlp->pfnSSMPutU32(pSSM, pThis->IOBase);
    pHlp->pfnSSMPutU32(pSSM, UINT32_MAX); /* sanity/terminator */
    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) parallelR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PPARALLELPORT   pThis = PDMDEVINS_2_DATA(pDevIns, PPARALLELPORT);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    pHlp->pfnSSMPutU8(pSSM, pThis->regData);
    pHlp->pfnSSMPutU8(pSSM, pThis->regStatus);
    pHlp->pfnSSMPutU8(pSSM, pThis->regControl);

    parallelR3LiveExec(pDevIns, pSSM, 0);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) parallelR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PPARALLELPORT   pThis = PDMDEVINS_2_DATA(pDevIns, PPARALLELPORT);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    AssertMsgReturn(uVersion == PARALLEL_SAVED_STATE_VERSION, ("%d\n", uVersion), VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);
    if (uPass == SSM_PASS_FINAL)
    {
        pHlp->pfnSSMGetU8(pSSM, &pThis->regData);
        pHlp->pfnSSMGetU8(pSSM, &pThis->regStatus);
        pHlp->pfnSSMGetU8(pSSM, &pThis->regControl);
    }

    /* the config */
    int32_t  iIrq;
    pHlp->pfnSSMGetS32(pSSM, &iIrq);
    uint32_t uIoBase;
    pHlp->pfnSSMGetU32(pSSM, &uIoBase);
    uint32_t u32;
    int rc = pHlp->pfnSSMGetU32(pSSM, &u32);
    AssertRCReturn(rc, rc);
    AssertMsgReturn(u32 == UINT32_MAX, ("%#x\n", u32), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

    if (pThis->iIrq != iIrq)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("IRQ changed: config=%#x state=%#x"), pThis->iIrq, iIrq);

    if (pThis->IOBase != uIoBase)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("IOBase changed: config=%#x state=%#x"), pThis->IOBase, uIoBase);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) parallelR3QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPARALLELPORTR3 pThisCC = RT_FROM_MEMBER(pInterface, PARALLELPORTR3, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHOSTPARALLELPORT, &pThisCC->IHostParallelPort);
    return NULL;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) parallelR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PPARALLELPORT   pThis   = PDMDEVINS_2_DATA(pDevIns, PPARALLELPORT);
    PPARALLELPORTR3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PPARALLELPORTR3);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;

    Assert(iInstance < 4);

    /*
     * Init the data.
     */
    pThisCC->pDevIns = pDevIns;

    /* IBase */
    pThisCC->IBase.pfnQueryInterface = parallelR3QueryInterface;

    /* IHostParallelPort */
    pThisCC->IHostParallelPort.pfnNotifyInterrupt = parallelR3NotifyInterrupt;

    /* Init parallel state */
    pThis->regData = 0;
#if 0 /* ECP implementation not complete. */
    pThis->reg_ecp_ecr = LPT_ECP_ECR_CHIPMODE_COMPAT | LPT_ECP_ECR_FIFO_EMPTY;
    pThis->act_fifo_pos_read = 0;
    pThis->act_fifo_pos_write = 0;
#endif

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "IRQ|IOBase", "");

    rc = pHlp->pfnCFGMQueryS32Def(pCfg, "IRQ", &pThis->iIrq, 7);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"IRQ\" value"));

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "IOBase", &pThis->IOBase, 0x378);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"IOBase\" value"));

    int cPorts = (pThis->IOBase == 0x3BC) ? 4 : 8;
    /*
     * Register the I/O ports and saved state.
     */
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, pThis->IOBase, cPorts, parallelIoPortWrite, parallelIoPortRead,
                                     "Parallel", NULL /*paExtDesc*/, &pThis->hIoPorts);
    AssertRCReturn(rc, rc);

#if 0
    /* register ecp registers */
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, pThis->IOBase + 0x400, 8, parallelIoPortWriteECP, parallelIoPortReadECP,
                                     "Parallel ECP", NULL /*paExtDesc*/, &pThis->hIoPortsEcp);
    AssertRCReturn(rc, rc);
#endif


    rc = PDMDevHlpSSMRegister3(pDevIns, PARALLEL_SAVED_STATE_VERSION, sizeof(*pThis),
                               parallelR3LiveExec, parallelR3SaveExec, parallelR3LoadExec);
    AssertRCReturn(rc, rc);


    /*
     * Attach the parallel port driver and get the interfaces.
     * For now no run-time changes are supported.
     */
    rc = PDMDevHlpDriverAttach(pDevIns, 0, &pThisCC->IBase, &pThisCC->pDrvBase, "Parallel Host");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrvHostParallelConnector = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMIHOSTPARALLELCONNECTOR);

        /* Set compatibility mode */
        //pThisCC->pDrvHostParallelConnector->pfnSetMode(pThisCC->pDrvHostParallelConnector, PDM_PARALLEL_PORT_MODE_COMPAT);
        /* Get status of control register */
        pThisCC->pDrvHostParallelConnector->pfnReadControl(pThisCC->pDrvHostParallelConnector, &pThis->regControl);

        AssertMsgReturn(pThisCC->pDrvHostParallelConnector,
                        ("Configuration error: instance %d has no host parallel interface!\n", iInstance),
                        VERR_PDM_MISSING_INTERFACE);
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
    {
        pThisCC->pDrvBase = NULL;
        pThisCC->pDrvHostParallelConnector = NULL;
        LogRel(("Parallel%d: no unit\n", iInstance));
    }
    else
        AssertMsgFailedReturn(("Parallel%d: Failed to attach to host driver. rc=%Rrc\n", iInstance, rc),
                              PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                                  N_("Parallel device %d cannot attach to host driver"), iInstance));

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) parallelRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PPARALLELPORT pThis = PDMDEVINS_2_DATA(pDevIns, PPARALLELPORT);

    int rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPorts, parallelIoPortWrite, parallelIoPortRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

# if 0
    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortsEcp, parallelIoPortWriteECP, parallelIoPortReadECP, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);
# endif

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceParallelPort =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "parallel",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_PARALLEL,
    /* .cMaxInstances = */          2,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(PARALLELPORT),
    /* .cbInstanceCC = */           CTX_EXPR(sizeof(PARALLELPORTR3), 0, 0),
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Parallel Communication Port",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           parallelR3Construct,
    /* .pfnDestruct = */            NULL,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               NULL,
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
    /* .pfnConstruct = */           parallelRZConstruct,
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
    /* .pfnConstruct = */           parallelRZConstruct,
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

