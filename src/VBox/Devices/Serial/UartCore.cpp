/* $Id: UartCore.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UartCore - UART  (16550A up to 16950) emulation.
 *
 * The documentation for this device was taken from the PC16550D spec from TI.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_SERIAL
#include <VBox/vmm/tm.h>
#include <iprt/log.h>
#include <iprt/uuid.h>
#include <iprt/assert.h>

#include "VBoxDD.h"
#include "UartCore.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The RBR/DLL register index (from the base of the port range). */
#define UART_REG_RBR_DLL_INDEX               0

/** The THR/DLL register index (from the base of the port range). */
#define UART_REG_THR_DLL_INDEX               0

/** The IER/DLM register index (from the base of the port range). */
#define UART_REG_IER_DLM_INDEX               1
/** Enable received data available interrupt */
# define UART_REG_IER_ERBFI                  RT_BIT(0)
/** Enable transmitter holding register empty interrupt */
# define UART_REG_IER_ETBEI                  RT_BIT(1)
/** Enable receiver line status interrupt */
# define UART_REG_IER_ELSI                   RT_BIT(2)
/** Enable modem status interrupt. */
# define UART_REG_IER_EDSSI                  RT_BIT(3)
/** Sleep mode enable. */
# define UART_REG_IER_SLEEP_MODE_EN          RT_BIT(4)
/** Low power mode enable. */
# define UART_REG_IER_LP_MODE_EN             RT_BIT(5)
/** Mask of writeable bits. */
# define UART_REG_IER_MASK_WR                0x0f
/** Mask of writeable bits for 16750+. */
# define UART_REG_IER_MASK_WR_16750          0x3f

/** The IIR register index (from the base of the port range). */
#define UART_REG_IIR_INDEX                   2
/** Interrupt Pending - high means no interrupt pending. */
# define UART_REG_IIR_IP_NO_INT              RT_BIT(0)
/** Interrupt identification mask. */
# define UART_REG_IIR_ID_MASK                0x0e
/** Sets the interrupt identification to the given value. */
# define UART_REG_IIR_ID_SET(a_Val)          (((a_Val) << 1) & UART_REG_IIR_ID_MASK)
/** Gets the interrupt identification from the given IIR register value. */
# define UART_REG_IIR_ID_GET(a_Val)          (((a_Val) & UART_REG_IIR_ID_MASK) >> 1)
/** Receiver Line Status interrupt. */
#  define UART_REG_IIR_ID_RCL                0x3
/** Received Data Available interrupt. */
#  define UART_REG_IIR_ID_RDA                0x2
/** Character Timeou Indicator interrupt. */
#  define UART_REG_IIR_ID_CTI                0x6
/** Transmitter Holding Register Empty interrupt. */
#  define UART_REG_IIR_ID_THRE               0x1
/** Modem Status interrupt. */
#  define UART_REG_IIR_ID_MS                 0x0
/** 64 byte FIFOs enabled (15750+ only). */
# define UART_REG_IIR_64BYTE_FIFOS_EN        RT_BIT(5)
/** FIFOs enabled. */
# define UART_REG_IIR_FIFOS_EN               0xc0
/** Bits relevant for checking whether the interrupt status has changed. */
# define UART_REG_IIR_CHANGED_MASK           0x0f

/** The FCR register index (from the base of the port range). */
#define UART_REG_FCR_INDEX                   2
/** Enable the TX/RX FIFOs. */
# define UART_REG_FCR_FIFO_EN                RT_BIT(0)
/** Reset the receive FIFO. */
# define UART_REG_FCR_RCV_FIFO_RST           RT_BIT(1)
/** Reset the transmit FIFO. */
# define UART_REG_FCR_XMIT_FIFO_RST          RT_BIT(2)
/** DMA Mode Select. */
# define UART_REG_FCR_DMA_MODE_SEL           RT_BIT(3)
/** 64 Byte FIFO enable (15750+ only). */
# define UART_REG_FCR_64BYTE_FIFO_EN         RT_BIT(5)
/** Receiver level interrupt trigger. */
# define UART_REG_FCR_RCV_LVL_IRQ_MASK       0xc0
/** Returns the receive level trigger value from the given FCR register. */
# define UART_REG_FCR_RCV_LVL_IRQ_GET(a_Fcr) (((a_Fcr) & UART_REG_FCR_RCV_LVL_IRQ_MASK) >> 6)
/** RCV Interrupt trigger level - 1 byte. */
# define UART_REG_FCR_RCV_LVL_IRQ_1          0x0
/** RCV Interrupt trigger level - 4 bytes. */
# define UART_REG_FCR_RCV_LVL_IRQ_4          0x1
/** RCV Interrupt trigger level - 8 bytes. */
# define UART_REG_FCR_RCV_LVL_IRQ_8          0x2
/** RCV Interrupt trigger level - 14 bytes. */
# define UART_REG_FCR_RCV_LVL_IRQ_14         0x3
/** Mask of writeable bits. */
# define UART_REG_FCR_MASK_WR                0xcf
/** Mask of sticky bits. */
# define UART_REG_FCR_MASK_STICKY            0xe9

/** The LCR register index (from the base of the port range). */
#define UART_REG_LCR_INDEX                   3
/** Word Length Select Mask. */
# define UART_REG_LCR_WLS_MASK               0x3
/** Returns the WLS value form the given LCR register value. */
# define UART_REG_LCR_WLS_GET(a_Lcr)         ((a_Lcr) & UART_REG_LCR_WLS_MASK)
/** Number of stop bits. */
# define UART_REG_LCR_STB                    RT_BIT(2)
/** Parity Enable. */
# define UART_REG_LCR_PEN                    RT_BIT(3)
/** Even Parity. */
# define UART_REG_LCR_EPS                    RT_BIT(4)
/** Stick parity. */
# define UART_REG_LCR_PAR_STICK              RT_BIT(5)
/** Set Break. */
# define UART_REG_LCR_BRK_SET                RT_BIT(6)
/** Divisor Latch Access Bit. */
# define UART_REG_LCR_DLAB                   RT_BIT(7)

/** The MCR register index (from the base of the port range). */
#define UART_REG_MCR_INDEX                   4
/** Data Terminal Ready. */
# define UART_REG_MCR_DTR                    RT_BIT(0)
/** Request To Send. */
# define UART_REG_MCR_RTS                    RT_BIT(1)
/** Out1. */
# define UART_REG_MCR_OUT1                   RT_BIT(2)
/** Out2. */
# define UART_REG_MCR_OUT2                   RT_BIT(3)
/** Loopback connection. */
# define UART_REG_MCR_LOOP                   RT_BIT(4)
/** Flow Control Enable (15750+ only). */
# define UART_REG_MCR_AFE                    RT_BIT(5)
/** Mask of writeable bits (15450 and 15550A). */
# define UART_REG_MCR_MASK_WR                0x1f
/** Mask of writeable bits (15750+). */
# define UART_REG_MCR_MASK_WR_15750          0x3f

/** The LSR register index (from the base of the port range). */
#define UART_REG_LSR_INDEX                   5
/** Data Ready. */
# define UART_REG_LSR_DR                     RT_BIT(0)
/** Overrun Error. */
# define UART_REG_LSR_OE                     RT_BIT(1)
/** Parity Error. */
# define UART_REG_LSR_PE                     RT_BIT(2)
/** Framing Error. */
# define UART_REG_LSR_FE                     RT_BIT(3)
/** Break Interrupt. */
# define UART_REG_LSR_BI                     RT_BIT(4)
/** Transmitter Holding Register. */
# define UART_REG_LSR_THRE                   RT_BIT(5)
/** Transmitter Empty. */
# define UART_REG_LSR_TEMT                   RT_BIT(6)
/** Error in receiver FIFO. */
# define UART_REG_LSR_RCV_FIFO_ERR           RT_BIT(7)
/** The bits to check in this register when checking for the RCL interrupt. */
# define UART_REG_LSR_BITS_IIR_RCL           0x1e

/** The MSR register index (from the base of the port range). */
#define UART_REG_MSR_INDEX                   6
/** Delta Clear to Send. */
# define UART_REG_MSR_DCTS                   RT_BIT(0)
/** Delta Data Set Ready. */
# define UART_REG_MSR_DDSR                   RT_BIT(1)
/** Trailing Edge Ring Indicator. */
# define UART_REG_MSR_TERI                   RT_BIT(2)
/** Delta Data Carrier Detect. */
# define UART_REG_MSR_DDCD                   RT_BIT(3)
/** Clear to Send. */
# define UART_REG_MSR_CTS                    RT_BIT(4)
/** Data Set Ready. */
# define UART_REG_MSR_DSR                    RT_BIT(5)
/** Ring Indicator. */
# define UART_REG_MSR_RI                     RT_BIT(6)
/** Data Carrier Detect. */
# define UART_REG_MSR_DCD                    RT_BIT(7)
/** The bits to check in this register when checking for the MS interrupt. */
# define UART_REG_MSR_BITS_IIR_MS            0x0f

/** The SCR register index (from the base of the port range). */
#define UART_REG_SCR_INDEX                   7

/** Set the specified bits in the given register. */
#define UART_REG_SET(a_Reg, a_Set)           ((a_Reg) |= (a_Set))
/** Clear the specified bits in the given register. */
#define UART_REG_CLR(a_Reg, a_Clr)           ((a_Reg) &= ~(a_Clr))


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

#ifdef IN_RING3
/**
 * FIFO ITL levels.
 */
static struct
{
    /** ITL level for a 16byte FIFO. */
    uint8_t                     cbItl16;
    /** ITL level for a 64byte FIFO. */
    uint8_t                     cbItl64;
} s_aFifoItl[] =
{
    /* cbItl16     cbItl64 */
    {     1,          1    },
    {     4,         16    },
    {     8,         32    },
    {    14,         56    }
};


/**
 * String versions of the parity enum.
 */
static const char *s_aszParity[] =
{
    "INVALID",
    "NONE",
    "EVEN",
    "ODD",
    "MARK",
    "SPACE",
    "INVALID"
};


/**
 * String versions of the stop bits enum.
 */
static const char *s_aszStopBits[] =
{
    "INVALID",
    "1",
    "1.5",
    "2",
    "INVALID"
};
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Updates the IRQ state based on the current device state.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 */
static void uartIrqUpdate(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC)
{
    LogFlowFunc(("pThis=%#p\n", pThis));

    /*
     * The interrupt uses a priority scheme, only the interrupt with the
     * highest priority is indicated in the interrupt identification register.
     *
     * The priorities are as follows (high to low):
     *      * Receiver line status
     *      * Received data available
     *      * Character timeout indication (only in FIFO mode).
     *      * Transmitter holding register empty
     *      * Modem status change.
     */
    uint8_t uRegIirNew = UART_REG_IIR_IP_NO_INT;
    if (   (pThis->uRegLsr & UART_REG_LSR_BITS_IIR_RCL)
        && (pThis->uRegIer & UART_REG_IER_ELSI))
        uRegIirNew = UART_REG_IIR_ID_SET(UART_REG_IIR_ID_RCL);
    else if (   (pThis->uRegIer & UART_REG_IER_ERBFI)
             && pThis->fIrqCtiPending)
        uRegIirNew = UART_REG_IIR_ID_SET(UART_REG_IIR_ID_CTI);
    else if (   (pThis->uRegLsr & UART_REG_LSR_DR)
             && (pThis->uRegIer & UART_REG_IER_ERBFI)
             && (   !(pThis->uRegFcr & UART_REG_FCR_FIFO_EN)
                 || pThis->FifoRecv.cbUsed >= pThis->FifoRecv.cbItl))
        uRegIirNew = UART_REG_IIR_ID_SET(UART_REG_IIR_ID_RDA);
    else if (   (pThis->uRegIer & UART_REG_IER_ETBEI)
             && pThis->fThreEmptyPending)
        uRegIirNew = UART_REG_IIR_ID_SET(UART_REG_IIR_ID_THRE);
    else if (   (pThis->uRegMsr & UART_REG_MSR_BITS_IIR_MS)
             && (pThis->uRegIer & UART_REG_IER_EDSSI))
        uRegIirNew = UART_REG_IIR_ID_SET(UART_REG_IIR_ID_MS);

    LogFlowFunc(("    uRegIirNew=%#x uRegIir=%#x\n", uRegIirNew, pThis->uRegIir));

    if (uRegIirNew != (pThis->uRegIir & UART_REG_IIR_CHANGED_MASK))
        LogFlow(("    Interrupt source changed from %#x -> %#x (IRQ %d -> %d)\n",
                 pThis->uRegIir, uRegIirNew,
                 pThis->uRegIir == UART_REG_IIR_IP_NO_INT ? 0 : 1,
                 uRegIirNew == UART_REG_IIR_IP_NO_INT ? 0 : 1));
    else
        LogFlow(("    No change in interrupt source\n"));

    /*
     * Set interrupt value accordingly. As this is an ISA device most guests
     * configure the IRQ as edge triggered instead of level triggered.
     * So this needs to be done everytime, even if the internal interrupt state
     * doesn't change in order to avoid the guest losing interrupts (reading one byte at
     * a time from the FIFO for instance which doesn't change the interrupt source).
     */
    if (uRegIirNew == UART_REG_IIR_IP_NO_INT)
        pThisCC->pfnUartIrqReq(pDevIns, pThis, pThis->iLUN, 0);
    else
        pThisCC->pfnUartIrqReq(pDevIns, pThis, pThis->iLUN, 1);

    if (pThis->uRegFcr & UART_REG_FCR_FIFO_EN)
        uRegIirNew |= UART_REG_IIR_FIFOS_EN;
    if (pThis->uRegFcr & UART_REG_FCR_64BYTE_FIFO_EN)
        uRegIirNew |= UART_REG_IIR_64BYTE_FIFOS_EN;

    pThis->uRegIir = uRegIirNew;
}


/**
 * Returns the amount of bytes stored in the given FIFO.
 *
 * @returns Amount of bytes stored in the FIFO.
 * @param   pFifo               The FIFO.
 */
DECLINLINE(size_t) uartFifoUsedGet(PUARTFIFO pFifo)
{
    return pFifo->cbUsed;
}


/**
 * Puts a new character into the given FIFO.
 *
 * @returns Flag whether the FIFO overflowed.
 * @param   pFifo               The FIFO to put the data into.
 * @param   fOvrWr              Flag whether to overwrite data if the FIFO is full.
 * @param   bData               The data to add.
 */
DECLINLINE(bool) uartFifoPut(PUARTFIFO pFifo, bool fOvrWr, uint8_t bData)
{
    if (fOvrWr || pFifo->cbUsed < pFifo->cbMax)
    {
        pFifo->abBuf[pFifo->offWrite] = bData;
        pFifo->offWrite = (pFifo->offWrite + 1) % pFifo->cbMax;
    }

    bool fOverFlow = false;
    if (pFifo->cbUsed < pFifo->cbMax)
        pFifo->cbUsed++;
    else
    {
        fOverFlow = true;
        if (fOvrWr) /* Advance the read position to account for the lost character. */
           pFifo->offRead = (pFifo->offRead + 1) % pFifo->cbMax;
    }

    return fOverFlow;
}


/**
 * Returns the next character in the FIFO.
 *
 * @return Next byte in the FIFO.
 * @param   pFifo               The FIFO to get data from.
 */
DECLINLINE(uint8_t) uartFifoGet(PUARTFIFO pFifo)
{
    uint8_t bRet = 0;

    if (pFifo->cbUsed)
    {
        bRet = pFifo->abBuf[pFifo->offRead];
        pFifo->offRead = (pFifo->offRead + 1) % pFifo->cbMax;
        pFifo->cbUsed--;
    }

    return bRet;
}

#ifdef IN_RING3

/**
 * Clears the given FIFO.
 *
 * @param   pFifo               The FIFO to clear.
 */
DECLINLINE(void) uartFifoClear(PUARTFIFO pFifo)
{
    memset(&pFifo->abBuf[0], 0, sizeof(pFifo->abBuf));
    pFifo->cbUsed   = 0;
    pFifo->offWrite = 0;
    pFifo->offRead  = 0;
}


/**
 * Returns the amount of free bytes in the given FIFO.
 *
 * @returns The amount of bytes free in the given FIFO.
 * @param   pFifo               The FIFO.
 */
DECLINLINE(size_t) uartFifoFreeGet(PUARTFIFO pFifo)
{
    return pFifo->cbMax - pFifo->cbUsed;
}


/**
 * Tries to copy the requested amount of data from the given FIFO into the provided buffer.
 *
 * @returns Amount of bytes actually copied.
 * @param   pFifo               The FIFO to copy data from.
 * @param   pvDst               Where to copy the data to.
 * @param   cbCopy              How much to copy.
 */
DECLINLINE(size_t) uartFifoCopyTo(PUARTFIFO pFifo, void *pvDst, size_t cbCopy)
{
    size_t cbCopied = 0;
    uint8_t *pbDst = (uint8_t *)pvDst;

    cbCopy = RT_MIN(cbCopy, pFifo->cbUsed);
    while (cbCopy)
    {
        uint8_t cbThisCopy = (uint8_t)RT_MIN(cbCopy, (uint8_t)(pFifo->cbMax - pFifo->offRead));
        memcpy(pbDst, &pFifo->abBuf[pFifo->offRead], cbThisCopy);

        pFifo->offRead = (pFifo->offRead + cbThisCopy) % pFifo->cbMax;
        pFifo->cbUsed -= cbThisCopy;
        pbDst    += cbThisCopy;
        cbCopied += cbThisCopy;
        cbCopy   -= cbThisCopy;
    }

    return cbCopied;
}


#if 0 /* unused */
/**
 * Tries to copy the requested amount of data from the provided buffer into the given FIFO.
 *
 * @returns Amount of bytes actually copied.
 * @param   pFifo               The FIFO to copy data to.
 * @param   pvSrc               Where to copy the data from.
 * @param   cbCopy              How much to copy.
 */
DECLINLINE(size_t) uartFifoCopyFrom(PUARTFIFO pFifo, void *pvSrc, size_t cbCopy)
{
    size_t cbCopied = 0;
    uint8_t *pbSrc = (uint8_t *)pvSrc;

    cbCopy = RT_MIN(cbCopy, uartFifoFreeGet(pFifo));
    while (cbCopy)
    {
        uint8_t cbThisCopy = (uint8_t)RT_MIN(cbCopy, (uint8_t)(pFifo->cbMax - pFifo->offWrite));
        memcpy(&pFifo->abBuf[pFifo->offWrite], pbSrc, cbThisCopy);

        pFifo->offWrite = (pFifo->offWrite + cbThisCopy) % pFifo->cbMax;
        pFifo->cbUsed += cbThisCopy;
        pbSrc    += cbThisCopy;
        cbCopied += cbThisCopy;
        cbCopy   -= cbThisCopy;
    }

    return cbCopied;
}
#endif


/**
 * Updates the delta bits for the given MSR register value which has the status line
 * bits set.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   uMsrSts             MSR value with the appropriate status bits set.
 */
static void uartR3MsrUpdate(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, uint8_t uMsrSts)
{
    /* Compare current and new states and set remaining bits accordingly. */
    if ((uMsrSts & UART_REG_MSR_CTS) != (pThis->uRegMsr & UART_REG_MSR_CTS))
        uMsrSts |= UART_REG_MSR_DCTS;
    if ((uMsrSts & UART_REG_MSR_DSR) != (pThis->uRegMsr & UART_REG_MSR_DSR))
        uMsrSts |= UART_REG_MSR_DDSR;
    if ((uMsrSts & UART_REG_MSR_RI) != 0 && (pThis->uRegMsr & UART_REG_MSR_RI) == 0)
        uMsrSts |= UART_REG_MSR_TERI;
    if ((uMsrSts & UART_REG_MSR_DCD) != (pThis->uRegMsr & UART_REG_MSR_DCD))
        uMsrSts |= UART_REG_MSR_DDCD;

    pThis->uRegMsr = uMsrSts;

    uartIrqUpdate(pDevIns, pThis, pThisCC);
}


/**
 * Updates the serial port parameters of the attached driver with the current configuration.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 */
static void uartR3ParamsUpdate(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC)
{
    if (   pThis->uRegDivisor != 0
        && pThisCC->pDrvSerial)
    {
        uint32_t uBps = 115200 / pThis->uRegDivisor; /* This is for PC compatible serial port with a 1.8432 MHz crystal. */
        unsigned cDataBits = UART_REG_LCR_WLS_GET(pThis->uRegLcr) + 5;
        uint32_t cFrameBits = cDataBits;
        PDMSERIALSTOPBITS enmStopBits = PDMSERIALSTOPBITS_ONE;
        PDMSERIALPARITY enmParity = PDMSERIALPARITY_NONE;

        if (pThis->uRegLcr & UART_REG_LCR_STB)
        {
            enmStopBits = cDataBits == 5 ? PDMSERIALSTOPBITS_ONEPOINTFIVE : PDMSERIALSTOPBITS_TWO;
            cFrameBits += 2;
        }
        else
            cFrameBits++;

        if (pThis->uRegLcr & UART_REG_LCR_PEN)
        {
            /* Select the correct parity mode based on the even and stick parity bits. */
            switch (pThis->uRegLcr & (UART_REG_LCR_EPS | UART_REG_LCR_PAR_STICK))
            {
                case 0:
                    enmParity = PDMSERIALPARITY_ODD;
                    break;
                case UART_REG_LCR_EPS:
                    enmParity = PDMSERIALPARITY_EVEN;
                    break;
                case UART_REG_LCR_EPS | UART_REG_LCR_PAR_STICK:
                    enmParity = PDMSERIALPARITY_SPACE;
                    break;
                case UART_REG_LCR_PAR_STICK:
                    enmParity = PDMSERIALPARITY_MARK;
                    break;
                default:
                    /* We should never get here as all cases where caught earlier. */
                    AssertMsgFailed(("This shouldn't happen at all: %#x\n",
                                     pThis->uRegLcr & (UART_REG_LCR_EPS | UART_REG_LCR_PAR_STICK)));
            }

            cFrameBits++;
        }

        uint64_t uTimerFreq = PDMDevHlpTimerGetFreq(pDevIns, pThis->hTimerRcvFifoTimeout);
        pThis->cSymbolXferTicks = (uTimerFreq / uBps) * cFrameBits;

        LogFlowFunc(("Changing parameters to: %u,%s,%u,%s\n",
                     uBps, s_aszParity[enmParity], cDataBits, s_aszStopBits[enmStopBits]));

        int rc = pThisCC->pDrvSerial->pfnChgParams(pThisCC->pDrvSerial, uBps, enmParity, cDataBits, enmStopBits);
        if (RT_FAILURE(rc))
            LogRelMax(10, ("Serial#%d: Failed to change parameters to %u,%s,%u,%s -> %Rrc\n",
                           pDevIns->iInstance, uBps, s_aszParity[enmParity], cDataBits, s_aszStopBits[enmStopBits], rc));

        /* Changed parameters will flush all receive queues, so there won't be any data to read even if indicated. */
        pThisCC->pDrvSerial->pfnQueuesFlush(pThisCC->pDrvSerial, true /*fQueueRecv*/, false /*fQueueXmit*/);
        ASMAtomicWriteU32(&pThis->cbAvailRdr, 0);
        UART_REG_CLR(pThis->uRegLsr, UART_REG_LSR_DR);
    }
}


/**
 * Updates the internal device state with the given PDM status line states.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   fStsLines           The PDM status line states.
 */
static void uartR3StsLinesUpdate(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, uint32_t fStsLines)
{
    uint8_t uRegMsrNew = 0; /* The new MSR value. */

    if (fStsLines & PDMISERIALPORT_STS_LINE_DCD)
        uRegMsrNew |= UART_REG_MSR_DCD;
    if (fStsLines & PDMISERIALPORT_STS_LINE_RI)
        uRegMsrNew |= UART_REG_MSR_RI;
    if (fStsLines & PDMISERIALPORT_STS_LINE_DSR)
        uRegMsrNew |= UART_REG_MSR_DSR;
    if (fStsLines & PDMISERIALPORT_STS_LINE_CTS)
        uRegMsrNew |= UART_REG_MSR_CTS;

    uartR3MsrUpdate(pDevIns, pThis, pThisCC, uRegMsrNew);
}


/**
 * Fills up the receive FIFO with as much data as possible.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 */
static void uartR3RecvFifoFill(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC)
{
    LogFlowFunc(("pThis=%#p\n", pThis));

    PUARTFIFO pFifo = &pThis->FifoRecv;
    size_t const cbFifoFree = uartFifoFreeGet(pFifo);
    uint32_t const cbAvailRdr = ASMAtomicReadU32(&pThis->cbAvailRdr);
    size_t const cbFill = RT_MIN(cbFifoFree, cbAvailRdr);
    size_t cbFilled = 0;

    while (cbFilled < cbFill)
    {
        size_t cbThisRead = cbFill - cbFilled;

        if (pFifo->offRead <= pFifo->offWrite)
            cbThisRead = RT_MIN(cbThisRead, (uint8_t)(pFifo->cbMax - pFifo->offWrite));
        else
            cbThisRead = RT_MIN(cbThisRead, (uint8_t)(pFifo->offRead - pFifo->offWrite));

        size_t cbRead = 0;
        int rc = pThisCC->pDrvSerial->pfnReadRdr(pThisCC->pDrvSerial, &pFifo->abBuf[pFifo->offWrite], cbThisRead, &cbRead);
        AssertRC(rc); Assert(cbRead <= UINT8_MAX); RT_NOREF(rc);

        pFifo->offWrite = (pFifo->offWrite + (uint8_t)cbRead) % pFifo->cbMax;
        pFifo->cbUsed   += (uint8_t)cbRead;
        cbFilled        += cbRead;

        if (cbRead < cbThisRead)
            break;
    }

    if (cbFilled)
    {
        UART_REG_SET(pThis->uRegLsr, UART_REG_LSR_DR);
        if (pFifo->cbUsed < pFifo->cbItl)
        {
            pThis->fIrqCtiPending = false;
            PDMDevHlpTimerSetRelative(pDevIns, pThis->hTimerRcvFifoTimeout, pThis->cSymbolXferTicks * 4, NULL);
        }
        uartIrqUpdate(pDevIns, pThis, pThisCC);
    }

    Assert(cbFilled <= (size_t)pThis->cbAvailRdr);
    ASMAtomicSubU32(&pThis->cbAvailRdr, (uint32_t)cbFilled);
}


/**
 * Fetches a single byte and writes it to RBR.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 */
static void uartR3ByteFetch(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC)
{
    if (ASMAtomicReadU32(&pThis->cbAvailRdr))
    {
        size_t cbRead = 0;
        int rc2 = pThisCC->pDrvSerial->pfnReadRdr(pThisCC->pDrvSerial, &pThis->uRegRbr, 1, &cbRead);
        AssertMsg(RT_SUCCESS(rc2) && cbRead == 1, ("This shouldn't fail and always return one byte!\n")); RT_NOREF(rc2);
        UART_REG_SET(pThis->uRegLsr, UART_REG_LSR_DR);
        uartIrqUpdate(pDevIns, pThis, pThisCC);
    }
}


/**
 * Fetches a ready data based on the FIFO setting.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 */
static void uartR3DataFetch(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC)
{
    AssertPtrReturnVoid(pThisCC->pDrvSerial);

    if (pThis->uRegFcr & UART_REG_FCR_FIFO_EN)
        uartR3RecvFifoFill(pDevIns, pThis, pThisCC);
    else
        uartR3ByteFetch(pDevIns, pThis, pThisCC);
}


/**
 * Reset the transmit/receive related bits to the standard values
 * (after a detach/attach/reset event).
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 */
static void uartR3XferReset(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC)
{
    PDMDevHlpTimerStop(pDevIns, pThis->hTimerRcvFifoTimeout);
    PDMDevHlpTimerStop(pDevIns, pThis->hTimerTxUnconnected);
    pThis->uRegLsr = UART_REG_LSR_THRE | UART_REG_LSR_TEMT;
    pThis->fThreEmptyPending = false;

    uartFifoClear(&pThis->FifoXmit);
    uartFifoClear(&pThis->FifoRecv);
    uartR3ParamsUpdate(pDevIns, pThis, pThisCC);
    uartIrqUpdate(pDevIns, pThis, pThisCC);

    if (pThisCC->pDrvSerial)
    {
        /* Set the modem lines to reflect the current state. */
        int rc = pThisCC->pDrvSerial->pfnChgModemLines(pThisCC->pDrvSerial, false /*fRts*/, false /*fDtr*/);
        if (RT_FAILURE(rc))
            LogRel(("Serial#%d: Failed to set modem lines with %Rrc during reset\n",
                    pDevIns->iInstance, rc));

        uint32_t fStsLines = 0;
        rc = pThisCC->pDrvSerial->pfnQueryStsLines(pThisCC->pDrvSerial, &fStsLines);
        if (RT_SUCCESS(rc))
            uartR3StsLinesUpdate(pDevIns, pThis, pThisCC, fStsLines);
        else
            LogRel(("Serial#%d: Failed to query status line status with %Rrc during reset\n",
                    pDevIns->iInstance, rc));
    }

}


/**
 * Tries to copy the specified amount of data from the active TX queue (register or FIFO).
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   pvBuf               Where to store the data.
 * @param   cbRead              How much to read from the TX queue.
 * @param   pcbRead             Where to store the amount of data read.
 */
static void uartR3TxQueueCopyFrom(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC,
                                  void *pvBuf, size_t cbRead, size_t *pcbRead)
{
    if (pThis->uRegFcr & UART_REG_FCR_FIFO_EN)
    {
        *pcbRead = uartFifoCopyTo(&pThis->FifoXmit, pvBuf, cbRead);
        if (!pThis->FifoXmit.cbUsed)
        {
            UART_REG_SET(pThis->uRegLsr, UART_REG_LSR_THRE);
            pThis->fThreEmptyPending = true;
        }
        if (*pcbRead)
            UART_REG_CLR(pThis->uRegLsr, UART_REG_LSR_TEMT);
        uartIrqUpdate(pDevIns, pThis, pThisCC);
    }
    else if (!(pThis->uRegLsr & UART_REG_LSR_THRE))
    {
        *(uint8_t *)pvBuf = pThis->uRegThr;
        *pcbRead = 1;
        UART_REG_SET(pThis->uRegLsr, UART_REG_LSR_THRE);
        UART_REG_CLR(pThis->uRegLsr, UART_REG_LSR_TEMT);
        pThis->fThreEmptyPending = true;
        uartIrqUpdate(pDevIns, pThis, pThisCC);
    }
    else
    {
        /*
         * This can happen if there was data in the FIFO when the connection was closed,
         * indicate this condition to the lower driver by returning 0 bytes.
         */
        *pcbRead = 0;
    }
}

#endif /* IN_RING3 */


/**
 * Transmits the given byte.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   bVal                Byte to transmit.
 */
static VBOXSTRICTRC uartXmit(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, uint8_t bVal)
{
    int rc = VINF_SUCCESS;
#ifdef IN_RING3
    bool fNotifyDrv = false;
#endif

    if (pThis->uRegFcr & UART_REG_FCR_FIFO_EN)
    {
#ifndef IN_RING3
        RT_NOREF(pDevIns, pThisCC);
        if (!uartFifoUsedGet(&pThis->FifoXmit))
            rc = VINF_IOM_R3_IOPORT_WRITE;
        else
        {
            uartFifoPut(&pThis->FifoXmit, true /*fOvrWr*/, bVal);
            UART_REG_CLR(pThis->uRegLsr, UART_REG_LSR_THRE | UART_REG_LSR_TEMT);
        }
#else
        uartFifoPut(&pThis->FifoXmit, true /*fOvrWr*/, bVal);
        UART_REG_CLR(pThis->uRegLsr, UART_REG_LSR_THRE | UART_REG_LSR_TEMT);
        pThis->fThreEmptyPending = false;
        uartIrqUpdate(pDevIns, pThis, pThisCC);
        if (uartFifoUsedGet(&pThis->FifoXmit) == 1)
            fNotifyDrv = true;
#endif
    }
    else
    {
        /* Notify the lower driver about available data only if the register was empty before. */
        if (pThis->uRegLsr & UART_REG_LSR_THRE)
        {
#ifndef IN_RING3
            rc = VINF_IOM_R3_IOPORT_WRITE;
#else
            pThis->uRegThr = bVal;
            UART_REG_CLR(pThis->uRegLsr, UART_REG_LSR_THRE | UART_REG_LSR_TEMT);
            pThis->fThreEmptyPending = false;
            uartIrqUpdate(pDevIns, pThis, pThisCC);
            fNotifyDrv = true;
#endif
        }
        else
            pThis->uRegThr = bVal;
    }

#ifdef IN_RING3
    if (fNotifyDrv)
    {
        /* Leave the device critical section before calling into the lower driver. */
        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);

        if (   pThisCC->pDrvSerial
            && !(pThis->uRegMcr & UART_REG_MCR_LOOP))
        {
            int rc2 = pThisCC->pDrvSerial->pfnDataAvailWrNotify(pThisCC->pDrvSerial);
            if (RT_FAILURE(rc2))
                LogRelMax(10, ("Serial#%d: Failed to send data with %Rrc\n", pDevIns->iInstance, rc2));
        }
        else
            PDMDevHlpTimerSetRelative(pDevIns, pThis->hTimerTxUnconnected, pThis->cSymbolXferTicks, NULL);

        rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VINF_SUCCESS);
    }
#endif

    return rc;
}


/**
 * Write handler for the THR/DLL register (depending on the DLAB bit in LCR).
 *
 * @returns Strict VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   uVal                The value to write.
 */
DECLINLINE(VBOXSTRICTRC) uartRegThrDllWrite(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, uint8_t uVal)
{
    VBOXSTRICTRC rc = VINF_SUCCESS;

    /* A set DLAB causes a write to the lower 8bits of the divisor latch. */
    if (pThis->uRegLcr & UART_REG_LCR_DLAB)
    {
        if (uVal != (pThis->uRegDivisor & 0xff))
        {
#ifndef IN_RING3
            rc = VINF_IOM_R3_IOPORT_WRITE;
#else
            pThis->uRegDivisor = (pThis->uRegDivisor & 0xff00) | uVal;
            uartR3ParamsUpdate(pDevIns, pThis, pThisCC);
#endif
        }
    }
    else
        rc = uartXmit(pDevIns, pThis, pThisCC, uVal);

    return rc;
}


/**
 * Write handler for the IER/DLM register (depending on the DLAB bit in LCR).
 *
 * @returns Strict VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   uVal                The value to write.
 */
DECLINLINE(VBOXSTRICTRC) uartRegIerDlmWrite(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, uint8_t uVal)
{
    /* A set DLAB causes a write to the higher 8bits of the divisor latch. */
    if (pThis->uRegLcr & UART_REG_LCR_DLAB)
    {
        if (uVal != (pThis->uRegDivisor & 0xff00) >> 8)
        {
#ifndef IN_RING3
            return VINF_IOM_R3_IOPORT_WRITE;
#else
            pThis->uRegDivisor = (pThis->uRegDivisor & 0xff) | (uVal << 8);
            uartR3ParamsUpdate(pDevIns, pThis, pThisCC);
#endif
        }
    }
    else
    {
        if (pThis->enmType < UARTTYPE_16750)
            pThis->uRegIer = uVal & UART_REG_IER_MASK_WR;
        else
            pThis->uRegIer = uVal & UART_REG_IER_MASK_WR_16750;

        if (pThis->uRegLsr & UART_REG_LSR_THRE)
            pThis->fThreEmptyPending = true;

        uartIrqUpdate(pDevIns, pThis, pThisCC);
    }
    return VINF_SUCCESS;
}


/**
 * Write handler for the FCR register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   uVal                The value to write.
 */
DECLINLINE(VBOXSTRICTRC) uartRegFcrWrite(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, uint8_t uVal)
{
#ifndef IN_RING3
    RT_NOREF(pDevIns, pThis, pThisCC, uVal);
    return VINF_IOM_R3_IOPORT_WRITE;
#else /* IN_RING3 */
    if (   pThis->enmType >= UARTTYPE_16550A
        && uVal != pThis->uRegFcr)
    {
        /* A change in the FIFO enable bit clears both FIFOs automatically. */
        if ((uVal ^ pThis->uRegFcr) & UART_REG_FCR_FIFO_EN)
        {
            uartFifoClear(&pThis->FifoXmit);
            uartFifoClear(&pThis->FifoRecv);

            /*
             * If the FIFO is about to be enabled and the DR bit is ready we have an unacknowledged
             * byte in the RBR register which will be lost so we have to adjust the available bytes.
             */
            if (   ASMAtomicReadU32(&pThis->cbAvailRdr) > 0
                && (uVal & UART_REG_FCR_FIFO_EN))
                ASMAtomicDecU32(&pThis->cbAvailRdr);

            /* Clear the DR bit too. */
            UART_REG_CLR(pThis->uRegLsr, UART_REG_LSR_DR);
        }

        /** @todo r=bird: Why was this here: if (rc == VINF_SUCCESS) */
        {
            if (uVal & UART_REG_FCR_RCV_FIFO_RST)
            {
                PDMDevHlpTimerStop(pDevIns, pThis->hTimerRcvFifoTimeout);
                pThis->fIrqCtiPending = false;
                uartFifoClear(&pThis->FifoRecv);
            }
            if (uVal & UART_REG_FCR_XMIT_FIFO_RST)
                uartFifoClear(&pThis->FifoXmit);

            /*
             * The 64byte FIFO enable bit is only changeable for 16750
             * and if the DLAB bit in LCR is set.
             */
            if (   pThis->enmType < UARTTYPE_16750
                || !(pThis->uRegLcr & UART_REG_LCR_DLAB))
                uVal &= ~UART_REG_FCR_64BYTE_FIFO_EN;
            else /* Use previous value. */
                uVal |= pThis->uRegFcr & UART_REG_FCR_64BYTE_FIFO_EN;

            if (uVal & UART_REG_FCR_64BYTE_FIFO_EN)
            {
                pThis->FifoRecv.cbMax = 64;
                pThis->FifoXmit.cbMax = 64;
            }
            else
            {
                pThis->FifoRecv.cbMax = 16;
                pThis->FifoXmit.cbMax = 16;
            }

            if (uVal & UART_REG_FCR_FIFO_EN)
            {
                uint8_t idxItl = UART_REG_FCR_RCV_LVL_IRQ_GET(uVal);
                if (uVal & UART_REG_FCR_64BYTE_FIFO_EN)
                    pThis->FifoRecv.cbItl = s_aFifoItl[idxItl].cbItl64;
                else
                    pThis->FifoRecv.cbItl = s_aFifoItl[idxItl].cbItl16;
            }

            /* The FIFO reset bits are self clearing. */
            pThis->uRegFcr = uVal & UART_REG_FCR_MASK_STICKY;
            uartIrqUpdate(pDevIns, pThis, pThisCC);
        }

        /* Fill in the next data. */
        if (ASMAtomicReadU32(&pThis->cbAvailRdr))
            uartR3DataFetch(pDevIns, pThis, pThisCC);
    }

    return VINF_SUCCESS;
#endif /* IN_RING3 */
}


/**
 * Write handler for the LCR register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   uVal                The value to write.
 */
DECLINLINE(VBOXSTRICTRC) uartRegLcrWrite(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, uint8_t uVal)
{
    /* Any change except the DLAB bit causes a switch to R3. */
    if ((pThis->uRegLcr & ~UART_REG_LCR_DLAB) != (uVal & ~UART_REG_LCR_DLAB))
    {
#ifndef IN_RING3
        RT_NOREF(pThisCC, pDevIns);
        return VINF_IOM_R3_IOPORT_WRITE;
#else
        /* Check whether the BREAK bit changed before updating the LCR value. */
        bool fBrkEn = RT_BOOL(uVal & UART_REG_LCR_BRK_SET);
        bool fBrkChg = fBrkEn != RT_BOOL(pThis->uRegLcr & UART_REG_LCR_BRK_SET);
        pThis->uRegLcr = uVal;
        uartR3ParamsUpdate(pDevIns, pThis, pThisCC);

        if (   fBrkChg
            && pThisCC->pDrvSerial)
            pThisCC->pDrvSerial->pfnChgBrk(pThisCC->pDrvSerial, fBrkEn);
#endif
    }
    else
        pThis->uRegLcr = uVal;

    return VINF_SUCCESS;
}


/**
 * Write handler for the MCR register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   uVal                The value to write.
 */
DECLINLINE(VBOXSTRICTRC) uartRegMcrWrite(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, uint8_t uVal)
{
    if (pThis->enmType < UARTTYPE_16750)
        uVal &= UART_REG_MCR_MASK_WR;
    else
        uVal &= UART_REG_MCR_MASK_WR_15750;
    if (pThis->uRegMcr != uVal)
    {
#ifndef IN_RING3
        RT_NOREF(pThisCC, pDevIns);
        return VINF_IOM_R3_IOPORT_WRITE;
#else
        /*
         * When loopback mode is activated the RTS, DTR, OUT1 and OUT2 lines are
         * disconnected and looped back to MSR.
         */
        if (   (uVal & UART_REG_MCR_LOOP)
            && !(pThis->uRegMcr & UART_REG_MCR_LOOP)
            && pThisCC->pDrvSerial)
            pThisCC->pDrvSerial->pfnChgModemLines(pThisCC->pDrvSerial, false /*fRts*/, false /*fDtr*/);

        pThis->uRegMcr = uVal;
        if (uVal & UART_REG_MCR_LOOP)
        {
            uint8_t uRegMsrSts = 0;

            if (uVal & UART_REG_MCR_RTS)
                uRegMsrSts |= UART_REG_MSR_CTS;
            if (uVal & UART_REG_MCR_DTR)
                uRegMsrSts |= UART_REG_MSR_DSR;
            if (uVal & UART_REG_MCR_OUT1)
                uRegMsrSts |= UART_REG_MSR_RI;
            if (uVal & UART_REG_MCR_OUT2)
                uRegMsrSts |= UART_REG_MSR_DCD;
            uartR3MsrUpdate(pDevIns, pThis, pThisCC, uRegMsrSts);
        }
        else if (pThisCC->pDrvSerial)
        {
            pThisCC->pDrvSerial->pfnChgModemLines(pThisCC->pDrvSerial,
                                                  RT_BOOL(uVal & UART_REG_MCR_RTS),
                                                  RT_BOOL(uVal & UART_REG_MCR_DTR));

            uint32_t fStsLines = 0;
            int rc = pThisCC->pDrvSerial->pfnQueryStsLines(pThisCC->pDrvSerial, &fStsLines);
            if (RT_SUCCESS(rc))
                uartR3StsLinesUpdate(pDevIns, pThis, pThisCC, fStsLines);
            else
                LogRelMax(10, ("Serial#%d: Failed to query status line status with %Rrc during reset\n",
                               pDevIns->iInstance, rc));
        }
        else /* Loopback mode got disabled and no driver attached, fake presence. */
            uartR3MsrUpdate(pDevIns, pThis, pThisCC, UART_REG_MSR_DCD | UART_REG_MSR_CTS | UART_REG_MSR_DSR);
#endif
    }

    return VINF_SUCCESS;
}


/**
 * Read handler for the RBR/DLL register (depending on the DLAB bit in LCR).
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   puVal               Where to store the read value on success.
 */
DECLINLINE(VBOXSTRICTRC) uartRegRbrDllRead(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, uint32_t *puVal)
{
    VBOXSTRICTRC rc = VINF_SUCCESS;

    /* A set DLAB causes a read from the lower 8bits of the divisor latch. */
    if (pThis->uRegLcr & UART_REG_LCR_DLAB)
        *puVal = pThis->uRegDivisor & 0xff;
    else
    {
        if (pThis->uRegFcr & UART_REG_FCR_FIFO_EN)
        {
            /*
             * Only go back to R3 if there is new data available for the FIFO
             * and we would clear the interrupt to fill it up again.
             */
            if (   pThis->FifoRecv.cbUsed <= pThis->FifoRecv.cbItl
                && ASMAtomicReadU32(&pThis->cbAvailRdr) > 0)
            {
#ifndef IN_RING3
                rc = VINF_IOM_R3_IOPORT_READ;
#else
                uartR3RecvFifoFill(pDevIns, pThis, pThisCC);
#endif
            }

            if (rc == VINF_SUCCESS)
            {
                *puVal = uartFifoGet(&pThis->FifoRecv);
                pThis->fIrqCtiPending = false;
                if (!pThis->FifoRecv.cbUsed)
                {
                    PDMDevHlpTimerStop(pDevIns, pThis->hTimerRcvFifoTimeout);
                    UART_REG_CLR(pThis->uRegLsr, UART_REG_LSR_DR);
                }
                else if (pThis->FifoRecv.cbUsed < pThis->FifoRecv.cbItl)
                    PDMDevHlpTimerSetRelative(pDevIns, pThis->hTimerRcvFifoTimeout,
                                              pThis->cSymbolXferTicks * 4, NULL);
                uartIrqUpdate(pDevIns, pThis, pThisCC);
            }
        }
        else
        {
            *puVal = pThis->uRegRbr;

            if (pThis->uRegLsr & UART_REG_LSR_DR)
            {
                Assert(pThis->cbAvailRdr);
                uint32_t cbAvail = ASMAtomicDecU32(&pThis->cbAvailRdr);
                if (!cbAvail)
                {
                    UART_REG_CLR(pThis->uRegLsr, UART_REG_LSR_DR);
                    uartIrqUpdate(pDevIns, pThis, pThisCC);
                }
                else
                {
#ifndef IN_RING3
                    /* Restore state and go back to R3. */
                    ASMAtomicIncU32(&pThis->cbAvailRdr);
                    rc = VINF_IOM_R3_IOPORT_READ;
#else
                    /* Fetch new data and keep the DR bit set. */
                    uartR3DataFetch(pDevIns, pThis, pThisCC);
#endif
                }
            }
        }
    }

    return rc;
}


/**
 * Read handler for the IER/DLM register (depending on the DLAB bit in LCR).
 *
 * @param   pThis               The shared serial port instance data.
 * @param   puVal               Where to store the read value on success.
 */
DECLINLINE(void) uartRegIerDlmRead(PUARTCORE pThis, uint32_t *puVal)
{
    /* A set DLAB causes a read from the upper 8bits of the divisor latch. */
    if (pThis->uRegLcr & UART_REG_LCR_DLAB)
        *puVal = (pThis->uRegDivisor & 0xff00) >> 8;
    else
        *puVal = pThis->uRegIer;
}


/**
 * Read handler for the IIR register.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   puVal               Where to store the read value on success.
 */
DECLINLINE(void) uartRegIirRead(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, uint32_t *puVal)
{
    *puVal = pThis->uRegIir;
    /* Reset the THRE empty interrupt id when this gets returned to the guest (see table 3 UART Reset configuration). */
    if (UART_REG_IIR_ID_GET(pThis->uRegIir) == UART_REG_IIR_ID_THRE)
    {
        pThis->fThreEmptyPending = false;
        uartIrqUpdate(pDevIns, pThis, pThisCC);
    }
}


/**
 * Read handler for the LSR register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   puVal               Where to store the read value on success.
 */
DECLINLINE(VBOXSTRICTRC) uartRegLsrRead(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, uint32_t *puVal)
{
    /* Yield if configured and there is no data available. */
    if (   !(pThis->uRegLsr & UART_REG_LSR_DR)
        && (pThis->fFlags & UART_CORE_YIELD_ON_LSR_READ))
    {
#ifndef IN_RING3
        return VINF_IOM_R3_IOPORT_READ;
#else
        RTThreadYield();
#endif
    }

    *puVal = pThis->uRegLsr;
    /*
     * Reading this register clears the Overrun (OE), Parity (PE) and Framing (FE) error
     * as well as the Break Interrupt (BI).
     */
    UART_REG_CLR(pThis->uRegLsr, UART_REG_LSR_BITS_IIR_RCL);
    uartIrqUpdate(pDevIns, pThis, pThisCC);

    return VINF_SUCCESS;
}


/**
 * Read handler for the MSR register.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   puVal               Where to store the read value on success.
 */
DECLINLINE(void) uartRegMsrRead(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, uint32_t *puVal)
{
    *puVal = pThis->uRegMsr;

    /* Clear any of the delta bits. */
    UART_REG_CLR(pThis->uRegMsr, UART_REG_MSR_BITS_IIR_MS);
    uartIrqUpdate(pDevIns, pThis, pThisCC);
}


#ifdef LOG_ENABLED
/**
 * Converts the register index into a sensible memnonic.
 *
 * @returns Register memnonic.
 * @param   pThis               The shared serial port instance data.
 * @param   idxReg              Register index.
 * @param   fWrite              Flag whether the register gets written.
 */
DECLINLINE(const char *) uartRegIdx2Str(PUARTCORE pThis, uint8_t idxReg, bool fWrite)
{
    const char *psz = "INV";

    switch (idxReg)
    {
        /*case UART_REG_THR_DLL_INDEX:*/
        case UART_REG_RBR_DLL_INDEX:
            if (pThis->uRegLcr & UART_REG_LCR_DLAB)
                psz = "DLL";
            else if (fWrite)
                psz = "THR";
            else
                psz = "RBR";
            break;
        case UART_REG_IER_DLM_INDEX:
            if (pThis->uRegLcr & UART_REG_LCR_DLAB)
                psz = "DLM";
            else
                psz = "IER";
            break;
        /*case UART_REG_IIR_INDEX:*/
        case UART_REG_FCR_INDEX:
            if (fWrite)
                psz = "FCR";
            else
                psz = "IIR";
            break;
        case UART_REG_LCR_INDEX:
            psz = "LCR";
            break;
        case UART_REG_MCR_INDEX:
            psz = "MCR";
            break;
        case UART_REG_LSR_INDEX:
            psz = "LSR";
            break;
        case UART_REG_MSR_INDEX:
            psz = "MSR";
            break;
        case UART_REG_SCR_INDEX:
            psz = "SCR";
            break;
    }

    return psz;
}
#endif


/**
 * Performs a register write to the given register offset.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared UART core instance data.
 * @param   pThisCC             The current context UART core instance data.
 * @param   uReg                The register offset (byte offset) to start writing to.
 * @param   u32                 The value to write.
 * @param   cb                  Number of bytes to write.
 */
DECLHIDDEN(VBOXSTRICTRC) uartRegWrite(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC,
                                      uint32_t uReg, uint32_t u32, size_t cb)
{
    AssertMsgReturn(cb == 1, ("uReg=%#x cb=%d u32=%#x\n", uReg, cb, u32), VINF_SUCCESS);

    VBOXSTRICTRC rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VINF_IOM_R3_IOPORT_WRITE);
    if (rc == VINF_SUCCESS)
    {
        uint8_t idxReg = uReg & 0x7;
        LogFlowFunc(("pThis=%#p uReg=%u{%s} u32=%#x cb=%u\n",
                     pThis, uReg, uartRegIdx2Str(pThis, idxReg, true /*fWrite*/), u32, cb));

        uint8_t uVal = (uint8_t)u32;
        switch (idxReg)
        {
            case UART_REG_THR_DLL_INDEX:
                rc = uartRegThrDllWrite(pDevIns, pThis, pThisCC, uVal);
                break;
            case UART_REG_IER_DLM_INDEX:
                rc = uartRegIerDlmWrite(pDevIns, pThis, pThisCC, uVal);
                break;
            case UART_REG_FCR_INDEX:
                rc = uartRegFcrWrite(pDevIns, pThis, pThisCC, uVal);
                break;
            case UART_REG_LCR_INDEX:
                rc = uartRegLcrWrite(pDevIns, pThis, pThisCC, uVal);
                break;
            case UART_REG_MCR_INDEX:
                rc = uartRegMcrWrite(pDevIns, pThis, pThisCC, uVal);
                break;
            case UART_REG_SCR_INDEX:
                pThis->uRegScr = uVal;
                break;
            default:
                break;
        }

        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    }
    LogFlowFunc(("-> %Rrc\n", VBOXSTRICTRC_VAL(rc)));
    return rc;
}


/**
 * Performs a register read from the given register offset.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared UART core instance data.
 * @param   pThisCC             The current context UART core instance data.
 * @param   uReg                The register offset (byte offset) to start reading from.
 * @param   pu32                Where to store the read value.
 * @param   cb                  Number of bytes to read.
 */
DECLHIDDEN(VBOXSTRICTRC) uartRegRead(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC,
                                     uint32_t uReg, uint32_t *pu32, size_t cb)
{
    if (cb != 1)
        return VERR_IOM_IOPORT_UNUSED;

    VBOXSTRICTRC rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VINF_IOM_R3_IOPORT_READ);
    if (rc == VINF_SUCCESS)
    {
        uint8_t idxReg = uReg & 0x7;
        switch (idxReg)
        {
            case UART_REG_RBR_DLL_INDEX:
                rc = uartRegRbrDllRead(pDevIns, pThis, pThisCC, pu32);
                break;
            case UART_REG_IER_DLM_INDEX:
                uartRegIerDlmRead(pThis, pu32);
                break;
            case UART_REG_IIR_INDEX:
                uartRegIirRead(pDevIns, pThis, pThisCC, pu32);
                break;
            case UART_REG_LCR_INDEX:
                *pu32 = pThis->uRegLcr;
                break;
            case UART_REG_MCR_INDEX:
                *pu32 = pThis->uRegMcr;
                break;
            case UART_REG_LSR_INDEX:
                rc = uartRegLsrRead(pDevIns, pThis, pThisCC, pu32);
                break;
            case UART_REG_MSR_INDEX:
                uartRegMsrRead(pDevIns, pThis, pThisCC, pu32);
                break;
            case UART_REG_SCR_INDEX:
                *pu32 = pThis->uRegScr;
                break;
            default:
                rc = VERR_IOM_IOPORT_UNUSED;
        }
        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
        LogFlowFunc(("pThis=%#p uReg=%u{%s} u32=%#x cb=%u -> %Rrc\n",
                     pThis, uReg, uartRegIdx2Str(pThis, idxReg, false /*fWrite*/), *pu32, cb, VBOXSTRICTRC_VAL(rc)));
    }
    else
        LogFlowFunc(("-> %Rrc\n", VBOXSTRICTRC_VAL(rc)));
    return rc;
}


#ifdef IN_RING3

/* -=-=-=-=-=-=-=-=- Timer callbacks -=-=-=-=-=-=-=-=- */

/**
 * @callback_method_impl{FNTMTIMERDEV, Fifo timer function.}
 */
static DECLCALLBACK(void) uartR3RcvFifoTimeoutTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    LogFlowFunc(("pDevIns=%#p hTimer=%#p pvUser=%#p\n", pDevIns, hTimer, pvUser));
    PUARTCORER3 pThisCC = (PUARTCORECC)pvUser;
    PUARTCORE   pThis   = pThisCC->pShared;
    RT_NOREF(hTimer);

    if (pThis->FifoRecv.cbUsed < pThis->FifoRecv.cbItl)
    {
        pThis->fIrqCtiPending = true;
        uartIrqUpdate(pDevIns, pThis, pThisCC);
    }
}

/**
 * @callback_method_impl{FNTMTIMERDEV,
 *      TX timer function when there is no driver connected for
 *      draining the THR/FIFO.}
 */
static DECLCALLBACK(void) uartR3TxUnconnectedTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    LogFlowFunc(("pDevIns=%#p hTimer=%#p pvUser=%#p\n", pDevIns, hTimer, pvUser));
    PUARTCORER3 pThisCC = (PUARTCORECC)pvUser;
    PUARTCORE   pThis   = pThisCC->pShared;
    Assert(hTimer == pThis->hTimerTxUnconnected);

    VBOXSTRICTRC rc1 = PDMDevHlpTimerLockClock2(pDevIns, hTimer, &pThis->CritSect, VINF_SUCCESS /* must get it */);
    AssertRCReturnVoid(VBOXSTRICTRC_VAL(rc1));

    uint8_t bVal = 0;
    size_t cbRead = 0;
    uartR3TxQueueCopyFrom(pDevIns, pThis, pThisCC, &bVal, sizeof(bVal), &cbRead);
    if (pThis->uRegMcr & UART_REG_MCR_LOOP)
    {
        /* Loopback mode is active, feed in the data at the receiving end. */
        uint32_t cbAvailOld = ASMAtomicAddU32(&pThis->cbAvailRdr, 1);
        if (pThis->uRegFcr & UART_REG_FCR_FIFO_EN)
        {
            PUARTFIFO pFifo = &pThis->FifoRecv;
            if (uartFifoFreeGet(pFifo) > 0)
            {
                pFifo->abBuf[pFifo->offWrite] = bVal;
                pFifo->offWrite = (pFifo->offWrite + 1) % pFifo->cbMax;
                pFifo->cbUsed++;

                UART_REG_SET(pThis->uRegLsr, UART_REG_LSR_DR);
                if (pFifo->cbUsed < pFifo->cbItl)
                {
                    pThis->fIrqCtiPending = false;
                    PDMDevHlpTimerSetRelative(pDevIns, pThis->hTimerRcvFifoTimeout,
                                              pThis->cSymbolXferTicks * 4, NULL);
                }
                uartIrqUpdate(pDevIns, pThis, pThisCC);
            }

            ASMAtomicSubU32(&pThis->cbAvailRdr, 1);
        }
        else if (!cbAvailOld)
        {
            pThis->uRegRbr = bVal;
            UART_REG_SET(pThis->uRegLsr, UART_REG_LSR_DR);
            uartIrqUpdate(pDevIns, pThis, pThisCC);
        }
        else
            ASMAtomicSubU32(&pThis->cbAvailRdr, 1);
    }

    if (cbRead == 1)
        PDMDevHlpTimerSetRelative(pDevIns, hTimer, pThis->cSymbolXferTicks, NULL);
    else
    {
        /* NO data left, set the transmitter holding register as empty. */
        UART_REG_SET(pThis->uRegLsr, UART_REG_LSR_TEMT);
    }

    PDMDevHlpTimerUnlockClock2(pDevIns, hTimer, &pThis->CritSect);
}


/* -=-=-=-=-=-=-=-=- PDMISERIALPORT on LUN#0 -=-=-=-=-=-=-=-=- */


/**
 * @interface_method_impl{PDMISERIALPORT,pfnDataAvailRdrNotify}
 */
static DECLCALLBACK(int) uartR3DataAvailRdrNotify(PPDMISERIALPORT pInterface, size_t cbAvail)
{
    LogFlowFunc(("pInterface=%#p cbAvail=%zu\n", pInterface, cbAvail));
    PUARTCORECC pThisCC = RT_FROM_MEMBER(pInterface, UARTCORECC, ISerialPort);
    PUARTCORE   pThis   = pThisCC->pShared;
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;

    AssertMsg((uint32_t)cbAvail == cbAvail, ("Too much data available\n"));

    int rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    uint32_t cbAvailOld = ASMAtomicAddU32(&pThis->cbAvailRdr, (uint32_t)cbAvail);
    LogFlow(("    cbAvailRdr=%u -> cbAvailRdr=%u\n", cbAvailOld, cbAvail + cbAvailOld));
    if (pThis->uRegFcr & UART_REG_FCR_FIFO_EN)
        uartR3RecvFifoFill(pDevIns, pThis, pThisCC);
    else if (!cbAvailOld)
    {
        size_t cbRead = 0;
        int rc = pThisCC->pDrvSerial->pfnReadRdr(pThisCC->pDrvSerial, &pThis->uRegRbr, 1, &cbRead);
        AssertRC(rc);

        if (cbRead)
        {
            UART_REG_SET(pThis->uRegLsr, UART_REG_LSR_DR);
            uartIrqUpdate(pDevIns, pThis, pThisCC);
        }
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMISERIALPORT,pfnDataSentNotify}
 */
static DECLCALLBACK(int) uartR3DataSentNotify(PPDMISERIALPORT pInterface)
{
    LogFlowFunc(("pInterface=%#p\n", pInterface));
    PUARTCORECC pThisCC = RT_FROM_MEMBER(pInterface, UARTCORECC, ISerialPort);
    PUARTCORE   pThis   = pThisCC->pShared;
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;

    /* Set the transmitter empty bit because everything was sent. */
    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    UART_REG_SET(pThis->uRegLsr, UART_REG_LSR_TEMT);
    uartIrqUpdate(pDevIns, pThis, pThisCC);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMISERIALPORT,pfnReadWr}
 */
static DECLCALLBACK(int) uartR3ReadWr(PPDMISERIALPORT pInterface, void *pvBuf, size_t cbRead, size_t *pcbRead)
{
    LogFlowFunc(("pInterface=%#p pvBuf=%#p cbRead=%zu pcbRead=%#p\n", pInterface, pvBuf, cbRead, pcbRead));
    PUARTCORECC pThisCC = RT_FROM_MEMBER(pInterface, UARTCORECC, ISerialPort);
    PUARTCORE   pThis   = pThisCC->pShared;
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;

    AssertReturn(cbRead > 0, VERR_INVALID_PARAMETER);

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    uartR3TxQueueCopyFrom(pDevIns, pThis, pThisCC, pvBuf, cbRead, pcbRead);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    LogFlowFunc(("-> VINF_SUCCESS{*pcbRead=%zu}\n", *pcbRead));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMISERIALPORT,pfnNotifyStsLinesChanged}
 */
static DECLCALLBACK(int) uartR3NotifyStsLinesChanged(PPDMISERIALPORT pInterface, uint32_t fNewStatusLines)
{
    LogFlowFunc(("pInterface=%#p fNewStatusLines=%#x\n", pInterface, fNewStatusLines));
    PUARTCORECC pThisCC = RT_FROM_MEMBER(pInterface, UARTCORECC, ISerialPort);
    PUARTCORE   pThis   = pThisCC->pShared;
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    int const   rcLock  = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    uartR3StsLinesUpdate(pDevIns, pThis, pThisCC, fNewStatusLines);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMISERIALPORT,pfnNotifyBrk}
 */
static DECLCALLBACK(int) uartR3NotifyBrk(PPDMISERIALPORT pInterface)
{
    LogFlowFunc(("pInterface=%#p\n", pInterface));
    PUARTCORECC pThisCC = RT_FROM_MEMBER(pInterface, UARTCORECC, ISerialPort);
    PUARTCORE   pThis   = pThisCC->pShared;
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    int const   rcLock  = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    UART_REG_SET(pThis->uRegLsr, UART_REG_LSR_BI);
    uartIrqUpdate(pDevIns, pThis, pThisCC);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}


/* -=-=-=-=-=-=-=-=- PDMIBASE -=-=-=-=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) uartR3QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PUARTCORECC pThisCC = RT_FROM_MEMBER(pInterface, UARTCORECC, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMISERIALPORT, &pThisCC->ISerialPort);
    return NULL;
}


/**
 * Saves the UART state to the given SSM handle.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The UART core instance.
 * @param   pSSM                The SSM handle to save to.
 */
DECLHIDDEN(int) uartR3SaveExec(PPDMDEVINS pDevIns, PUARTCORE pThis, PSSMHANDLE pSSM)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    pHlp->pfnSSMPutU16(pSSM,  pThis->uRegDivisor);
    pHlp->pfnSSMPutU8(pSSM,   pThis->uRegRbr);
    pHlp->pfnSSMPutU8(pSSM,   pThis->uRegThr);
    pHlp->pfnSSMPutU8(pSSM,   pThis->uRegIer);
    pHlp->pfnSSMPutU8(pSSM,   pThis->uRegIir);
    pHlp->pfnSSMPutU8(pSSM,   pThis->uRegFcr);
    pHlp->pfnSSMPutU8(pSSM,   pThis->uRegLcr);
    pHlp->pfnSSMPutU8(pSSM,   pThis->uRegMcr);
    pHlp->pfnSSMPutU8(pSSM,   pThis->uRegLsr);
    pHlp->pfnSSMPutU8(pSSM,   pThis->uRegMsr);
    pHlp->pfnSSMPutU8(pSSM,   pThis->uRegScr);
    pHlp->pfnSSMPutBool(pSSM, pThis->fIrqCtiPending);
    pHlp->pfnSSMPutBool(pSSM, pThis->fThreEmptyPending);
    pHlp->pfnSSMPutU8(pSSM,   pThis->FifoXmit.cbMax);
    pHlp->pfnSSMPutU8(pSSM,   pThis->FifoXmit.cbItl);
    pHlp->pfnSSMPutU8(pSSM,   pThis->FifoRecv.cbMax);
    pHlp->pfnSSMPutU8(pSSM,   pThis->FifoRecv.cbItl);

    int rc = PDMDevHlpTimerSave(pDevIns, pThis->hTimerRcvFifoTimeout, pSSM);
    if (RT_SUCCESS(rc))
        rc = PDMDevHlpTimerSave(pDevIns, pThis->hTimerTxUnconnected, pSSM);

    return rc;
}


/**
 * Loads the UART state from the given SSM handle.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The UART core instance.
 * @param   pSSM                The SSM handle to load from.
 * @param   uVersion            Saved state version.
 * @param   uPass               The SSM pass the call is done in.
 * @param   pbIrq               Where to store the IRQ value for legacy
 *                              saved states - optional.
 * @param   pPortBase           Where to store the I/O port base for legacy
 *                              saved states - optional.
 */
DECLHIDDEN(int) uartR3LoadExec(PPDMDEVINS pDevIns, PUARTCORE pThis, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass,
                               uint8_t *pbIrq, RTIOPORT *pPortBase)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;
    int rc;
    RT_NOREF(uPass);

    if (uVersion > UART_SAVED_STATE_VERSION_LEGACY_CODE)
    {
        pHlp->pfnSSMGetU16(pSSM,  &pThis->uRegDivisor);
        pHlp->pfnSSMGetU8(pSSM,   &pThis->uRegRbr);
        pHlp->pfnSSMGetU8(pSSM,   &pThis->uRegThr);
        pHlp->pfnSSMGetU8(pSSM,   &pThis->uRegIer);
        pHlp->pfnSSMGetU8(pSSM,   &pThis->uRegIir);
        pHlp->pfnSSMGetU8(pSSM,   &pThis->uRegFcr);
        pHlp->pfnSSMGetU8(pSSM,   &pThis->uRegLcr);
        pHlp->pfnSSMGetU8(pSSM,   &pThis->uRegMcr);
        pHlp->pfnSSMGetU8(pSSM,   &pThis->uRegLsr);
        pHlp->pfnSSMGetU8(pSSM,   &pThis->uRegMsr);
        pHlp->pfnSSMGetU8(pSSM,   &pThis->uRegScr);
        pHlp->pfnSSMGetBool(pSSM, &pThis->fIrqCtiPending);
        pHlp->pfnSSMGetBool(pSSM, &pThis->fThreEmptyPending);
        pHlp->pfnSSMGetU8(pSSM,   &pThis->FifoXmit.cbMax);
        pHlp->pfnSSMGetU8(pSSM,   &pThis->FifoXmit.cbItl);
        pHlp->pfnSSMGetU8(pSSM,   &pThis->FifoRecv.cbMax);
        pHlp->pfnSSMGetU8(pSSM,   &pThis->FifoRecv.cbItl);

        rc = PDMDevHlpTimerLoad(pDevIns, pThis->hTimerRcvFifoTimeout, pSSM);
        if (uVersion > UART_SAVED_STATE_VERSION_PRE_UNCONNECTED_TX_TIMER)
            rc = PDMDevHlpTimerLoad(pDevIns, pThis->hTimerTxUnconnected, pSSM);
    }
    else
    {
        AssertPtr(pbIrq);
        AssertPtr(pPortBase);
        if (uVersion == UART_SAVED_STATE_VERSION_16450)
        {
            pThis->enmType = UARTTYPE_16450;
            LogRel(("Serial#%d: falling back to 16450 mode from load state\n", pDevIns->iInstance));
        }

        pHlp->pfnSSMGetU16(pSSM, &pThis->uRegDivisor);
        pHlp->pfnSSMGetU8(pSSM, &pThis->uRegRbr);
        pHlp->pfnSSMGetU8(pSSM, &pThis->uRegIer);
        pHlp->pfnSSMGetU8(pSSM, &pThis->uRegLcr);
        pHlp->pfnSSMGetU8(pSSM, &pThis->uRegMcr);
        pHlp->pfnSSMGetU8(pSSM, &pThis->uRegLsr);
        pHlp->pfnSSMGetU8(pSSM, &pThis->uRegMsr);
        pHlp->pfnSSMGetU8(pSSM, &pThis->uRegScr);
        if (uVersion > UART_SAVED_STATE_VERSION_16450)
            pHlp->pfnSSMGetU8(pSSM, &pThis->uRegFcr);

        int32_t iTmp = 0;
        pHlp->pfnSSMGetS32(pSSM, &iTmp);
        pThis->fThreEmptyPending = RT_BOOL(iTmp);

        rc = pHlp->pfnSSMGetS32(pSSM, &iTmp);
        AssertRCReturn(rc, rc);
        *pbIrq = (uint8_t)iTmp;

        pHlp->pfnSSMSkip(pSSM, sizeof(int32_t)); /* was: last_break_enable */

        uint32_t uPortBaseTmp = 0;
        rc = pHlp->pfnSSMGetU32(pSSM, &uPortBaseTmp);
        AssertRCReturn(rc, rc);
        *pPortBase = (RTIOPORT)uPortBaseTmp;

        rc = pHlp->pfnSSMSkip(pSSM, sizeof(bool)); /* was: msr_changed */
        if (   RT_SUCCESS(rc)
            && uVersion > UART_SAVED_STATE_VERSION_MISSING_BITS)
        {
            pHlp->pfnSSMGetU8(pSSM, &pThis->uRegThr);
            pHlp->pfnSSMSkip(pSSM, sizeof(uint8_t)); /* The old transmit shift register, not used anymore. */
            pHlp->pfnSSMGetU8(pSSM, &pThis->uRegIir);

            int32_t iTimeoutPending = 0;
            pHlp->pfnSSMGetS32(pSSM, &iTimeoutPending);
            pThis->fIrqCtiPending = RT_BOOL(iTimeoutPending);

            rc = PDMDevHlpTimerLoad(pDevIns, pThis->hTimerRcvFifoTimeout, pSSM);
            AssertRCReturn(rc, rc);

            bool fWasActiveIgn;
            rc = pHlp->pfnTimerSkipLoad(pSSM, &fWasActiveIgn);  /* was: transmit_timerR3 */
            AssertRCReturn(rc, rc);

            pHlp->pfnSSMGetU8(pSSM, &pThis->FifoRecv.cbItl);
            rc = pHlp->pfnSSMGetU8(pSSM, &pThis->FifoRecv.cbItl);
        }
    }

    return rc;
}


/**
 * Called when loading the state completed, updates the parameters of any driver underneath.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   pSSM                The SSM handle.
 */
DECLHIDDEN(int) uartR3LoadDone(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, PSSMHANDLE pSSM)
{
    RT_NOREF(pSSM);

    uartR3ParamsUpdate(pDevIns, pThis, pThisCC);
    uartIrqUpdate(pDevIns, pThis, pThisCC);

    if (pThisCC->pDrvSerial)
    {
        /* Set the modem lines to reflect the current state. */
        int rc = pThisCC->pDrvSerial->pfnChgModemLines(pThisCC->pDrvSerial,
                                                       RT_BOOL(pThis->uRegMcr & UART_REG_MCR_RTS),
                                                       RT_BOOL(pThis->uRegMcr & UART_REG_MCR_DTR));
        if (RT_FAILURE(rc))
            LogRel(("Serial#%d: Failed to set modem lines with %Rrc during saved state load\n",
                    pDevIns->iInstance, rc));

        uint32_t fStsLines = 0;
        rc = pThisCC->pDrvSerial->pfnQueryStsLines(pThisCC->pDrvSerial, &fStsLines);
        if (RT_SUCCESS(rc))
            uartR3StsLinesUpdate(pDevIns, pThis, pThisCC, fStsLines);
        else
            LogRel(("Serial#%d: Failed to query status line status with %Rrc during reset\n",
                    pDevIns->iInstance, rc));
    }

    return VINF_SUCCESS;
}


/**
 * Resets the given UART core instance.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 */
DECLHIDDEN(void) uartR3Reset(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC)
{
    pThis->uRegDivisor    = 0x0c; /* Default to 9600 Baud. */
    pThis->uRegRbr        = 0;
    pThis->uRegThr        = 0;
    pThis->uRegIer        = 0;
    pThis->uRegIir        = UART_REG_IIR_IP_NO_INT;
    pThis->uRegFcr        = 0;
    pThis->uRegLcr        = 0; /* 5 data bits, no parity, 1 stop bit. */
    pThis->uRegMcr        = 0;
    pThis->uRegLsr        = UART_REG_LSR_THRE | UART_REG_LSR_TEMT;
    pThis->uRegMsr        = UART_REG_MSR_DCD | UART_REG_MSR_CTS | UART_REG_MSR_DSR | UART_REG_MSR_DCTS | UART_REG_MSR_DDSR | UART_REG_MSR_DDCD;
    pThis->uRegScr        = 0;
    pThis->fIrqCtiPending = false;
    pThis->fThreEmptyPending = true;

    /* Standard FIFO size for 15550A. */
    pThis->FifoXmit.cbMax = 16;
    pThis->FifoRecv.cbMax = 16;
    pThis->FifoRecv.cbItl = 1;

    uartR3XferReset(pDevIns, pThis, pThisCC);
}


/**
 * Attaches the given UART core instance to the drivers at the given LUN.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   iLUN                The LUN being attached.
 */
DECLHIDDEN(int) uartR3Attach(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, unsigned iLUN)
{
    int rc = PDMDevHlpDriverAttach(pDevIns, iLUN, &pThisCC->IBase, &pThisCC->pDrvBase, "Serial Char");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrvSerial = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMISERIALCONNECTOR);
        if (!pThisCC->pDrvSerial)
        {
            AssertLogRelMsgFailed(("Configuration error: instance %d has no serial interface!\n", pDevIns->iInstance));
            return VERR_PDM_MISSING_INTERFACE;
        }
        rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
        if (RT_SUCCESS(rc))
        {
            uartR3XferReset(pDevIns, pThis, pThisCC);
            PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
        }
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
    {
        pThisCC->pDrvBase = NULL;
        pThisCC->pDrvSerial = NULL;
        rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
        if (RT_SUCCESS(rc))
        {
            uartR3XferReset(pDevIns, pThis, pThisCC);
            PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
        }
        LogRel(("Serial#%d: no unit\n", pDevIns->iInstance));
    }
    else /* Don't call VMSetError here as we assume that the driver already set an appropriate error */
        LogRel(("Serial#%d: Failed to attach to serial driver. rc=%Rrc\n", pDevIns->iInstance, rc));

   return rc;
}


/**
 * Detaches any attached driver from the given UART core instance.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 */
DECLHIDDEN(void) uartR3Detach(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC)
{
    /* Zero out important members. */
    pThisCC->pDrvBase   = NULL;
    pThisCC->pDrvSerial = NULL;
    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

    uartR3XferReset(pDevIns, pThis, pThisCC);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
}


/**
 * Destroys the given UART core instance freeing all allocated resources.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared UART core instance data..
 */
DECLHIDDEN(void) uartR3Destruct(PPDMDEVINS pDevIns, PUARTCORE pThis)
{
    PDMDevHlpCritSectDelete(pDevIns, &pThis->CritSect);
}


/**
 * Initializes the given UART core instance using the provided configuration.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance pointer.
 * @param   pThis               The shared UART core instance data to
 *                              initialize.
 * @param   pThisCC             The ring-3 UART core instance data to
 *                              initialize.
 * @param   enmType             The type of UART emulated.
 * @param   iLUN                The LUN the UART should look for attached drivers.
 * @param   fFlags              Additional flags controlling device behavior.
 * @param   pfnUartIrqReq       Pointer to the interrupt request callback.
 */
DECLHIDDEN(int) uartR3Init(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC,
                           UARTTYPE enmType, unsigned iLUN, uint32_t fFlags, PFNUARTCOREIRQREQ pfnUartIrqReq)
{
    /*
     * Initialize the instance data.
     * (Do this early or the destructor might choke on something!)
     */
    pThis->iLUN                                     = iLUN;
    pThis->enmType                                  = enmType;
    pThis->fFlags                                   = fFlags;

    pThisCC->iLUN                                   = iLUN;
    pThisCC->pDevIns                                = pDevIns;
    pThisCC->pShared                                = pThis;
    pThisCC->pfnUartIrqReq                          = pfnUartIrqReq;

    /* IBase */
    pThisCC->IBase.pfnQueryInterface                = uartR3QueryInterface;

    /* ISerialPort */
    pThisCC->ISerialPort.pfnDataAvailRdrNotify      = uartR3DataAvailRdrNotify;
    pThisCC->ISerialPort.pfnDataSentNotify          = uartR3DataSentNotify;
    pThisCC->ISerialPort.pfnReadWr                  = uartR3ReadWr;
    pThisCC->ISerialPort.pfnNotifyStsLinesChanged   = uartR3NotifyStsLinesChanged;
    pThisCC->ISerialPort.pfnNotifyBrk               = uartR3NotifyBrk;

    int rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSect, RT_SRC_POS, "Uart{%s#%d}#%d",
                                   pDevIns->pReg->szName, pDevIns->iInstance, iLUN);
    AssertRCReturn(rc, rc);

    /*
     * Attach the char driver and get the interfaces.
     */
    rc = PDMDevHlpDriverAttach(pDevIns, iLUN, &pThisCC->IBase, &pThisCC->pDrvBase, "UART");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrvSerial = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMISERIALCONNECTOR);
        if (!pThisCC->pDrvSerial)
        {
            AssertLogRelMsgFailed(("Configuration error: instance %d has no serial interface!\n", iLUN));
            return VERR_PDM_MISSING_INTERFACE;
        }
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
    {
        pThisCC->pDrvBase   = NULL;
        pThisCC->pDrvSerial = NULL;
        LogRel(("Serial#%d: no unit\n", iLUN));
    }
    else
    {
        AssertLogRelMsgFailed(("Serial#%d: Failed to attach to char driver. rc=%Rrc\n", iLUN, rc));
        /* Don't call VMSetError here as we assume that the driver already set an appropriate error */
        return rc;
    }

    /*
     * Create the receive FIFO character timeout indicator timer.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, uartR3RcvFifoTimeoutTimer, pThisCC,
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0, "UART Rcv FIFO",
                              &pThis->hTimerRcvFifoTimeout);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpTimerSetCritSect(pDevIns, pThis->hTimerRcvFifoTimeout, &pThis->CritSect);
    AssertRCReturn(rc, rc);

    /*
     * Create the transmit timer when no device is connected.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, uartR3TxUnconnectedTimer, pThisCC,
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_NO_RING0, "UART TX unconnect",
                              &pThis->hTimerTxUnconnected);
    AssertRCReturn(rc, rc);

    uartR3Reset(pDevIns, pThis, pThisCC);
    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * Initializes the ring-0 / raw-mode instance data.
 *
 * @returns VBox status code.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   pfnUartIrqReq       Pointer to the interrupt request callback.
 */
DECLHIDDEN(int) uartRZInit(PUARTCORECC pThisCC, PFNUARTCOREIRQREQ pfnUartIrqReq)
{
    AssertPtrReturn(pfnUartIrqReq, VERR_INVALID_POINTER);
    AssertPtrReturn(pThisCC, VERR_INVALID_POINTER);
    pThisCC->pfnUartIrqReq = pfnUartIrqReq;
    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */
