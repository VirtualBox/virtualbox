/* $Id: DevPL011.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevSerialPL011 - ARM PL011 PrimeCell UART.
 *
 * The documentation for this device was taken from
 * https://developer.arm.com/documentation/ddi0183/g/programmers-model/summary-of-registers (2023-03-21).
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
#define LOG_GROUP LOG_GROUP_DEV_SERIAL
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmserialifs.h>
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
#define PL011_SAVED_STATE_VERSION               1

/** PL011 MMIO region size in bytes. */
#define PL011_MMIO_SIZE                         _4K
/** Maximum size of a FIFO. */
#define PL011_FIFO_LENGTH_MAX                   32

/** The offset of the UARTDR register from the beginning of the region. */
#define PL011_REG_UARTDR_INDEX                  0x0
/** Framing error. */
# define PL011_REG_UARTDR_FE                    RT_BIT(8)
/** Parity error. */
# define PL011_REG_UARTDR_PE                    RT_BIT(9)
/** Break error. */
# define PL011_REG_UARTDR_BE                    RT_BIT(10)
/** Overrun error. */
# define PL011_REG_UARTDR_OE                    RT_BIT(11)

/** The offset of the UARTRSR/UARTECR register from the beginning of the region. */
#define PL011_REG_UARTRSR_ECR_INDEX             0x4
/** Framing error. */
# define PL011_REG_UARTRSR_ECR_FE               RT_BIT(0)
/** Parity error. */
# define PL011_REG_UARTRSR_ECR_PE               RT_BIT(1)
/** Break error. */
# define PL011_REG_UARTRSR_ECR_BE               RT_BIT(2)
/** Overrun error. */
# define PL011_REG_UARTRSR_ECR_OE               RT_BIT(3)

/** The offset of the UARTFR register from the beginning of the region. */
#define PL011_REG_UARTFR_INDEX                  0x18
/** Clear to send. */
# define PL011_REG_UARTFR_CTS                   RT_BIT(0)
/** Data set ready. */
# define PL011_REG_UARTFR_DSR                   RT_BIT(1)
/** Data carrier detect. */
# define PL011_REG_UARTFR_DCD                   RT_BIT(2)
/** UART busy. */
# define PL011_REG_UARTFR_BUSY                  RT_BIT(3)
/** Receive FIFO empty. */
# define PL011_REG_UARTFR_RXFE                  RT_BIT(4)
/** Transmit FIFO full. */
# define PL011_REG_UARTFR_TXFF                  RT_BIT(5)
/** Receive FIFO full. */
# define PL011_REG_UARTFR_RXFF                  RT_BIT(6)
/** Transmit FIFO empty. */
# define PL011_REG_UARTFR_TXFE                  RT_BIT(7)
/** Ring indicator. */
# define PL011_REG_UARTFR_RI                    RT_BIT(8)

/** The offset of the UARTILPR register from the beginning of the region. */
#define PL011_REG_UARTILPR_INDEX                0x20

/** The offset of the UARTIBRD register from the beginning of the region. */
#define PL011_REG_UARTIBRD_INDEX                0x24

/** The offset of the UARTFBRD register from the beginning of the region. */
#define PL011_REG_UARTFBRD_INDEX                0x28

/** The offset of the UARTLCR_H register from the beginning of the region. */
#define PL011_REG_UARTLCR_H_INDEX               0x2c
/** Send break. */
# define PL011_REG_UARTLCR_H_BRK                RT_BIT(0)
/** Parity enable. */
# define PL011_REG_UARTLCR_H_PEN                RT_BIT(1)
/** Even parity select. */
# define PL011_REG_UARTLCR_H_EPS                RT_BIT(2)
/** Two stop bits select. */
# define PL011_REG_UARTLCR_H_STP2               RT_BIT(3)
/** Enable FIFOs. */
# define PL011_REG_UARTLCR_H_FEN                RT_BIT(4)
/** Word length. */
# define PL011_REG_UARTLCR_H_WLEN               (RT_BIT(5) | RT_BIT(6))
# define PL011_REG_UARTLCR_H_WLEN_GET(a_Lcr)    (((a_Lcr) & PL011_REG_UARTLCR_H_WLEN) >> 5)
# define PL011_REG_UARTLCR_H_WLEN_SET(a_Wlen)   (((a_Wlen) << 5) & PL011_REG_UARTLCR_H_WLEN)
/** 5 bits word length. */
#  define PL011_REG_UARTLCR_H_WLEN_5BITS        0
/** 6 bits word length. */
#  define PL011_REG_UARTLCR_H_WLEN_6BITS        1
/** 7 bits word length. */
#  define PL011_REG_UARTLCR_H_WLEN_7BITS        2
/** 8 bits word length. */
#  define PL011_REG_UARTLCR_H_WLEN_8BITS        3
/** Stick parity select. */
# define PL011_REG_UARTLCR_H_SPS                RT_BIT(7)

/** The offset of the UARTCR register from the beginning of the region. */
#define PL011_REG_UARTCR_INDEX                  0x30
/** UART enable. */
# define PL011_REG_UARTCR_UARTEN                RT_BIT(0)
/** SIR enable. */
# define PL011_REG_UARTCR_SIREN                 RT_BIT(1)
/** SIR low-power IrDA mode. */
# define PL011_REG_UARTCR_SIRLP                 RT_BIT(2)
/** Loopback enable. */
# define PL011_REG_UARTCR_LBE                   RT_BIT(7)
/** UART transmit enable flag. */
# define PL011_REG_UARTCR_TXE                   RT_BIT(8)
/** UART receive enable flag. */
# define PL011_REG_UARTCR_RXE                   RT_BIT(9)
/** Data transmit ready. */
# define PL011_REG_UARTCR_DTR                   RT_BIT(10)
/** Request to send. */
# define PL011_REG_UARTCR_RTS                   RT_BIT(11)
/** UART Out1 modem status output (DCD). */
# define PL011_REG_UARTCR_OUT1_DCD              RT_BIT(12)
/** UART Out2 modem status output (RI). */
# define PL011_REG_UARTCR_OUT2_RI               RT_BIT(13)
/** RTS hardware flow control enable. */
# define PL011_REG_UARTCR_OUT1_RTSEn            RT_BIT(14)
/** CTS hardware flow control enable. */
# define PL011_REG_UARTCR_OUT1_CTSEn            RT_BIT(15)

/** The offset of the UARTIFLS register from the beginning of the region. */
#define PL011_REG_UARTIFLS_INDEX                0x34
/** Returns the Transmit Interrupt FIFO level. */
# define PL011_REG_UARTIFLS_TXFIFO_GET(a_Ifls)  ((a_Ifls) & 0x7)
/** Returns the Receive Interrupt FIFO level. */
# define PL011_REG_UARTIFLS_RXFIFO_GET(a_Ifls)  (((a_Ifls) >> 3) & 0x7)
/** 1/8 Fifo level. */
# define PL011_REG_UARTIFLS_LVL_1_8             0x0
/** 1/4 Fifo level. */
# define PL011_REG_UARTIFLS_LVL_1_4             0x1
/** 1/2 Fifo level. */
# define PL011_REG_UARTIFLS_LVL_1_2             0x2
/** 3/4 Fifo level. */
# define PL011_REG_UARTIFLS_LVL_3_4             0x3
/** 7/8 Fifo level. */
# define PL011_REG_UARTIFLS_LVL_7_8             0x4

/** The offset of the UARTIMSC register from the beginning of the region. */
#define PL011_REG_UARTIMSC_INDEX                0x38
/** Bit 0 - Ring Indicator Modem interrupt mask. */
# define PL011_REG_UARTIMSC_RIMIM               RT_BIT(0)
/** Bit 1 - Clear To Send Modem interrupt mask. */
# define PL011_REG_UARTIMSC_CTSMIM              RT_BIT(1)
/** Bit 2 - Data Carrier Detect Modem interrupt mask. */
# define PL011_REG_UARTIMSC_DCDMIM              RT_BIT(2)
/** Bit 3 - Data Set Ready Modem interrupt mask. */
# define PL011_REG_UARTIMSC_DSRMIM              RT_BIT(3)
/** Bit 4 - Receive interrupt mask. */
# define PL011_REG_UARTIMSC_RXIM                RT_BIT(4)
/** Bit 5 - Transmit interrupt mask. */
# define PL011_REG_UARTIMSC_TXIM                RT_BIT(5)
/** Bit 6 - Receive timeout interrupt mask. */
# define PL011_REG_UARTIMSC_RTIM                RT_BIT(6)
/** Bit 7 - Frameing Error interrupt mask. */
# define PL011_REG_UARTIMSC_FEIM                RT_BIT(7)
/** Bit 8 - Parity Error interrupt mask. */
# define PL011_REG_UARTIMSC_PEIM                RT_BIT(8)
/** Bit 9 - Break Error interrupt mask. */
# define PL011_REG_UARTIMSC_BEIM                RT_BIT(9)
/** Bit 10 - Overrun Error interrupt mask. */
# define PL011_REG_UARTIMSC_OEIM                RT_BIT(10)

/** The offset of the UARTRIS register from the beginning of the region. */
#define PL011_REG_UARTRIS_INDEX                 0x3c
/** Bit 0 - Ring Indicator Modem raw interrupt status. */
# define PL011_REG_UARTRIS_RIRMIS               RT_BIT(0)
/** Bit 1 - Clear To Send Modem raw interrupt status. */
# define PL011_REG_UARTRIS_CTSRMIS              RT_BIT(1)
/** Bit 2 - Data Carrier Detect Modem raw interrupt status. */
# define PL011_REG_UARTRIS_DCDRMIS              RT_BIT(2)
/** Bit 3 - Data Set Ready Modem raw interrupt status. */
# define PL011_REG_UARTRIS_DSRRMIS              RT_BIT(3)
/** Bit 4 - Receive raw interrupt status. */
# define PL011_REG_UARTRIS_RXRIS                RT_BIT(4)
/** Bit 5 - Transmit raw interrupt status. */
# define PL011_REG_UARTRIS_TXRIS                RT_BIT(5)
/** Bit 6 - Receive timeout raw interrupt status. */
# define PL011_REG_UARTRIS_RTRIS                RT_BIT(6)
/** Bit 7 - Frameing Error raw interrupt status. */
# define PL011_REG_UARTRIS_FERIS                RT_BIT(7)
/** Bit 8 - Parity Error raw interrupt status. */
# define PL011_REG_UARTRIS_PERIS                RT_BIT(8)
/** Bit 9 - Break Error raw interrupt status. */
# define PL011_REG_UARTRIS_BERIS                RT_BIT(9)
/** Bit 10 - Overrun Error raw interrupt status. */
# define PL011_REG_UARTRIS_OERIS                RT_BIT(10)

/** The offset of the UARTMIS register from the beginning of the region. */
#define PL011_REG_UARTMIS_INDEX                 0x40
/** Bit 0 - Ring Indicator Modem masked interrupt status. */
# define PL011_REG_UARTRIS_RIMMIS               RT_BIT(0)
/** Bit 1 - Clear To Send Modem masked interrupt status. */
# define PL011_REG_UARTRIS_CTSMMIS              RT_BIT(1)
/** Bit 2 - Data Carrier Detect Modem masked interrupt status. */
# define PL011_REG_UARTRIS_DCDMMIS              RT_BIT(2)
/** Bit 3 - Data Set Ready Modem masked interrupt status. */
# define PL011_REG_UARTRIS_DSRMMIS              RT_BIT(3)
/** Bit 4 - Receive masked interrupt status. */
# define PL011_REG_UARTRIS_RXMIS                RT_BIT(4)
/** Bit 5 - Transmit masked interrupt status. */
# define PL011_REG_UARTRIS_TXMIS                RT_BIT(5)
/** Bit 6 - Receive timeout masked interrupt status. */
# define PL011_REG_UARTRIS_RTMIS                RT_BIT(6)
/** Bit 7 - Frameing Error masked interrupt status. */
# define PL011_REG_UARTRIS_FEMIS                RT_BIT(7)
/** Bit 8 - Parity Error masked interrupt status. */
# define PL011_REG_UARTRIS_PEMIS                RT_BIT(8)
/** Bit 9 - Break Error masked interrupt status. */
# define PL011_REG_UARTRIS_BEMIS                RT_BIT(9)
/** Bit 10 - Overrun Error masked interrupt status. */
# define PL011_REG_UARTRIS_OEMIS                RT_BIT(10)

/** The offset of the UARTICR register from the beginning of the region. */
#define PL011_REG_UARTICR_INDEX                 0x44
/** Bit 0 - Ring Indicator Modem interrupt clear. */
# define PL011_REG_UARTICR_RIMIC                RT_BIT(0)
/** Bit 1 - Clear To Send Modem interrupt clear. */
# define PL011_REG_UARTICR_CTSMIC               RT_BIT(1)
/** Bit 2 - Data Carrier Detect Modem interrupt clear. */
# define PL011_REG_UARTICR_DCDMIC               RT_BIT(2)
/** Bit 3 - Data Set Ready Modem interrupt clear. */
# define PL011_REG_UARTICR_DSRMIC               RT_BIT(3)
/** Bit 4 - Receive interrupt clear. */
# define PL011_REG_UARTICR_RXIC                 RT_BIT(4)
/** Bit 5 - Transmit interrupt clear. */
# define PL011_REG_UARTICR_TXIC                 RT_BIT(5)
/** Bit 6 - Receive timeout interrupt clear. */
# define PL011_REG_UARTICR_RTIC                 RT_BIT(6)
/** Bit 7 - Frameing Error interrupt clear. */
# define PL011_REG_UARTICR_FEIC                 RT_BIT(7)
/** Bit 8 - Parity Error interrupt clear. */
# define PL011_REG_UARTICR_PEIC                 RT_BIT(8)
/** Bit 9 - Break Error interrupt clear. */
# define PL011_REG_UARTICR_BEIC                 RT_BIT(9)
/** Bit 10 - Overrun Error interrupt clear. */
# define PL011_REG_UARTICR_OEIC                 RT_BIT(10)

/** The offset of the UARTDMACR register from the beginning of the region. */
#define PL011_REG_UARTDMACR_INDEX               0x48

/** The offset of the UARTPeriphID0 register from the beginning of the region. */
#define PL011_REG_UART_PERIPH_ID0_INDEX         0xfe0
/** The offset of the UARTPeriphID1 register from the beginning of the region. */
#define PL011_REG_UART_PERIPH_ID1_INDEX         0xfe4
/** The offset of the UARTPeriphID2 register from the beginning of the region. */
#define PL011_REG_UART_PERIPH_ID2_INDEX         0xfe8
/** The offset of the UARTPeriphID3 register from the beginning of the region. */
#define PL011_REG_UART_PERIPH_ID3_INDEX         0xfec
/** The offset of the UARTPCellID0 register from the beginning of the region. */
#define PL011_REG_UART_PCELL_ID0_INDEX          0xff0
/** The offset of the UARTPCellID1 register from the beginning of the region. */
#define PL011_REG_UART_PCELL_ID1_INDEX          0xff4
/** The offset of the UARTPCellID2 register from the beginning of the region. */
#define PL011_REG_UART_PCELL_ID2_INDEX          0xff8
/** The offset of the UARTPCellID3 register from the beginning of the region. */
#define PL011_REG_UART_PCELL_ID3_INDEX          0xffc

/** Set the specified bits in the given register. */
#define PL011_REG_SET(a_Reg, a_Set)             ((a_Reg) |= (a_Set))
/** Clear the specified bits in the given register. */
#define PL011_REG_CLR(a_Reg, a_Clr)             ((a_Reg) &= ~(a_Clr))


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * UART FIFO.
 */
typedef struct PL011FIFO
{
    /** Fifo size configured. */
    uint8_t                         cbMax;
    /** Current amount of bytes used. */
    uint8_t                         cbUsed;
    /** Next index to write to. */
    uint8_t                         offWrite;
    /** Next index to read from. */
    uint8_t                         offRead;
    /** The interrupt trigger level (only used for the receive FIFO). */
    uint8_t                         cbItl;
    /** The data in the FIFO. */
    uint8_t                         abBuf[PL011_FIFO_LENGTH_MAX];
    /** Alignment to a 4 byte boundary. */
    uint8_t                         au8Alignment0[3];
} PL011FIFO;
/** Pointer to a FIFO. */
typedef PL011FIFO *PPL011FIFO;


/**
 * Shared serial device state.
 */
typedef struct DEVPL011
{
    /** The MMIO handle. */
    IOMMMIOHANDLE                   hMmio;
    /** The base MMIO address the device is registered at. */
    RTGCPHYS                        GCPhysMmioBase;
    /** The IRQ value. */
    uint16_t                        u16Irq;

    /** @name Registers.
     * @{ */
    /** UART data register when written. */
    uint8_t                         uRegDr;
    /** UART data register when read. */
    uint8_t                         uRegDrRd;
    /** UART control register. */
    uint16_t                        uRegCr;
    /** UART flag register. */
    uint16_t                        uRegFr;
    /** UART integer baud rate register. */
    uint16_t                        uRegIbrd;
    /** UART fractional baud rate register. */
    uint16_t                        uRegFbrd;
    /** UART line control register. */
    uint16_t                        uRegLcrH;
    /** Interrupt FIFO Level Select register. */
    uint16_t                        uRegFifoLvlSel;
    /** Interrupt mask register. */
    uint16_t                        uRegIrqMask;
    /** Raw interrupt status register. */
    uint16_t                        uRegIrqSts;
    /** @} */

    /** Time it takes to transmit/receive a single symbol in timer ticks. */
    uint64_t                        cSymbolXferTicks;
    /** Number of bytes available for reading from the layer below. */
    volatile uint32_t               cbAvailRdr;

    /** Timer handle for the send loop if no driver is connected/loopback mode is active. */
    TMTIMERHANDLE                   hTimerTxUnconnected;

    /** The transmit FIFO. */
    PL011FIFO                       FifoXmit;
    /** The receive FIFO. */
    PL011FIFO                       FifoRecv;
} DEVPL011;
/** Pointer to the shared serial device state. */
typedef DEVPL011 *PDEVPL011;


/**
 * Serial device state for ring-3.
 */
typedef struct DEVPL011R3
{
    /** LUN\#0: The base interface. */
    PDMIBASE                        IBase;
    /** LUN\#0: The serial port interface. */
    PDMISERIALPORT                  ISerialPort;
    /** Pointer to the attached base driver. */
    R3PTRTYPE(PPDMIBASE)            pDrvBase;
    /** Pointer to the attached serial driver. */
    R3PTRTYPE(PPDMISERIALCONNECTOR) pDrvSerial;
    /** Pointer to the device instance - only for getting our bearings in
     *  interface methods. */
    PPDMDEVINS                      pDevIns;
} DEVPL011R3;
/** Pointer to the serial device state for ring-3. */
typedef DEVPL011R3 *PDEVPL011R3;


/**
 * Serial device state for ring-0.
 */
typedef struct DEVPL011R0
{
    /** Dummy .*/
    uint8_t                         bDummy;
} DEVPL011R0;
/** Pointer to the serial device state for ring-0. */
typedef DEVPL011R0 *PDEVPL011R0;


/**
 * Serial device state for raw-mode.
 */
typedef struct DEVPL011RC
{
    /** Dummy .*/
    uint8_t                         bDummy;
} DEVPL011RC;
/** Pointer to the serial device state for raw-mode. */
typedef DEVPL011RC *PDEVPL011RC;

/** The serial device state for the current context. */
typedef CTX_SUFF(DEVPL011) DEVPL011CC;
/** Pointer to the serial device state for the current context. */
typedef CTX_SUFF(PDEVPL011) PDEVPL011CC;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

#ifdef IN_RING3
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
    "INVALID",
    "2",
    "INVALID"
};
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

#ifndef VBOX_DEVICE_STRUCT_TESTCASE

/**
 * Updates the IRQ state based on the current device state.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 */
DECLINLINE(void) pl011IrqUpdate(PPDMDEVINS pDevIns, PDEVPL011 pThis, PDEVPL011CC pThisCC)
{
    LogFlowFunc(("pThis=%#p uRegIrqSts=%#RX16 uRegIrqMask=%#RX16\n",
                 pThis, pThis->uRegIrqSts, pThis->uRegIrqMask));

    RT_NOREF(pThisCC);
    if (pThis->uRegIrqSts & ~pThis->uRegIrqMask)
        PDMDevHlpISASetIrqNoWait(pDevIns, pThis->u16Irq, 1);
    else
        PDMDevHlpISASetIrqNoWait(pDevIns, pThis->u16Irq, 0);
}


/**
 * Returns the amount of bytes stored in the given FIFO.
 *
 * @returns Amount of bytes stored in the FIFO.
 * @param   pFifo               The FIFO.
 */
DECLINLINE(size_t) pl011FifoUsedGet(PPL011FIFO pFifo)
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
DECLINLINE(bool) pl011FifoPut(PPL011FIFO pFifo, bool fOvrWr, uint8_t bData)
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
DECLINLINE(uint8_t) pl011FifoGet(PPL011FIFO pFifo)
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


/**
 * Clears the given FIFO.
 *
 * @param   pFifo               The FIFO to clear.
 */
DECLINLINE(void) pl011FifoClear(PPL011FIFO pFifo)
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
DECLINLINE(size_t) pl011FifoFreeGet(PPL011FIFO pFifo)
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
DECLINLINE(size_t) pl011FifoCopyTo(PPL011FIFO pFifo, void *pvDst, size_t cbCopy)
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


/**
 * Transmits the given byte.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 * @param   bVal                Byte to transmit.
 */
static VBOXSTRICTRC pl011Xmit(PPDMDEVINS pDevIns, PDEVPL011CC pThisCC, PDEVPL011 pThis, uint8_t bVal)
{
    int rc = VINF_SUCCESS;
#ifdef IN_RING3
    bool fNotifyDrv = false;
#endif

    if (pThis->uRegLcrH & PL011_REG_UARTLCR_H_FEN)
    {
#ifndef IN_RING3
        RT_NOREF(pDevIns, pThisCC);
        if (!pl011FifoUsedGet(&pThis->FifoXmit))
            rc = VINF_IOM_R3_MMIO_WRITE;
        else
        {
            pl011FifoPut(&pThis->FifoXmit, true /*fOvrWr*/, bVal);
            PL011_REG_CLR(pThis->uRegFr, PL011_REG_UARTFR_TXFE);
            PL011_REG_SET(pThis->uRegFr, PL011_REG_UARTFR_BUSY);
        }
#else
        pl011FifoPut(&pThis->FifoXmit, true /*fOvrWr*/, bVal);
        PL011_REG_CLR(pThis->uRegFr, PL011_REG_UARTFR_TXFE);
        PL011_REG_SET(pThis->uRegFr, PL011_REG_UARTFR_BUSY);
        pl011IrqUpdate(pDevIns, pThis, pThisCC);
        if (pl011FifoUsedGet(&pThis->FifoXmit) == 1)
            fNotifyDrv = true;
#endif
    }
    else
    {
        /* Notify the lower driver about available data only if the register was empty before. */
        if (!(pThis->uRegFr & PL011_REG_UARTFR_BUSY))
        {
#ifndef IN_RING3
            rc = VINF_IOM_R3_IOPORT_WRITE;
#else
            pThis->uRegDr = bVal;
            pThis->uRegFr |= PL011_REG_UARTFR_BUSY | PL011_REG_UARTFR_TXFF;
            pl011IrqUpdate(pDevIns, pThis, pThisCC);
            fNotifyDrv = true;
#endif
        }
        else
            pThis->uRegDr = bVal;
    }

#ifdef IN_RING3
    if (fNotifyDrv)
    {
        /* Leave the device critical section before calling into the lower driver. */
        PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);

        if (   pThisCC->pDrvSerial
            && !(pThis->uRegCr & PL011_REG_UARTCR_LBE))
        {
            int rc2 = pThisCC->pDrvSerial->pfnDataAvailWrNotify(pThisCC->pDrvSerial);
            if (RT_FAILURE(rc2))
                LogRelMax(10, ("PL011#%d: Failed to send data with %Rrc\n", pDevIns->iInstance, rc2));
        }
        else
            PDMDevHlpTimerSetRelative(pDevIns, pThis->hTimerTxUnconnected, pThis->cSymbolXferTicks, NULL);

        rc = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VINF_SUCCESS);
    }
#endif

    return rc;
}


#ifdef IN_RING3
/**
 * Fills up the receive FIFO with as much data as possible.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 */
static void pl011R3RecvFifoFill(PPDMDEVINS pDevIns, PDEVPL011CC pThisCC, PDEVPL011 pThis)
{
    LogFlowFunc(("pThis=%#p\n", pThis));

    PPL011FIFO pFifo = &pThis->FifoRecv;
    size_t const cbFifoFree = pl011FifoFreeGet(pFifo);
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
        PL011_REG_CLR(pThis->uRegFr, PL011_REG_UARTFR_RXFE);
        PL011_REG_SET(pThis->uRegIrqSts, PL011_REG_UARTRIS_RXRIS);
        if (pFifo->cbUsed < pFifo->cbItl)
        {
            //pThis->fIrqCtiPending = false;
            //PDMDevHlpTimerSetRelative(pDevIns, pThis->hTimerRcvFifoTimeout, pThis->cSymbolXferTicks * 4, NULL);
        }
        pl011IrqUpdate(pDevIns, pThis, pThisCC);
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
static void pl011R3ByteFetch(PPDMDEVINS pDevIns, PDEVPL011CC pThisCC, PDEVPL011 pThis)
{
    if (ASMAtomicReadU32(&pThis->cbAvailRdr))
    {
        size_t cbRead = 0;
        int rc2 = pThisCC->pDrvSerial->pfnReadRdr(pThisCC->pDrvSerial, &pThis->uRegDrRd, 1, &cbRead);
        AssertMsg(RT_SUCCESS(rc2) && cbRead == 1, ("This shouldn't fail and always return one byte!\n")); RT_NOREF(rc2);
        PL011_REG_CLR(pThis->uRegFr, PL011_REG_UARTFR_RXFE);
        PL011_REG_SET(pThis->uRegIrqSts, PL011_REG_UARTRIS_RXRIS);
        pl011IrqUpdate(pDevIns, pThis, pThisCC);
    }
}


/**
 * Fetches a ready data based on the FIFO setting.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 */
static void pl011R3DataFetch(PPDMDEVINS pDevIns, PDEVPL011CC pThisCC, PDEVPL011 pThis)
{
    AssertPtrReturnVoid(pThisCC->pDrvSerial);

    if (pThis->uRegLcrH & PL011_REG_UARTLCR_H_FEN)
        pl011R3RecvFifoFill(pDevIns, pThisCC, pThis);
    else
        pl011R3ByteFetch(pDevIns, pThisCC, pThis);
}


/**
 * Updates the serial port parameters of the attached driver with the current configuration.
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 */
static void pl011R3ParamsUpdate(PPDMDEVINS pDevIns, PDEVPL011 pThis, PDEVPL011CC pThisCC)
{
    if (   pThis->uRegIbrd != 0
        && pThisCC->pDrvSerial)
    {
        uint32_t uBps = 460800 / pThis->uRegIbrd; /** @todo This is for a 7.3728MHz clock. */
        unsigned cDataBits = PL011_REG_UARTLCR_H_WLEN_GET(pThis->uRegLcrH) + 5;
        uint32_t cFrameBits = cDataBits;
        PDMSERIALSTOPBITS enmStopBits = PDMSERIALSTOPBITS_ONE;
        PDMSERIALPARITY enmParity = PDMSERIALPARITY_NONE;

        if (pThis->uRegLcrH & PL011_REG_UARTLCR_H_STP2)
        {
            enmStopBits = PDMSERIALSTOPBITS_TWO;
            cFrameBits += 2;
        }
        else
            cFrameBits++;

        if (pThis->uRegLcrH & PL011_REG_UARTLCR_H_PEN)
        {
            /* Select the correct parity mode based on the even and stick parity bits. */
            switch (pThis->uRegLcrH & (PL011_REG_UARTLCR_H_EPS | PL011_REG_UARTLCR_H_SPS))
            {
                case 0:
                    enmParity = PDMSERIALPARITY_ODD;
                    break;
                case PL011_REG_UARTLCR_H_EPS:
                    enmParity = PDMSERIALPARITY_EVEN;
                    break;
                case PL011_REG_UARTLCR_H_EPS | PL011_REG_UARTLCR_H_SPS:
                    enmParity = PDMSERIALPARITY_SPACE;
                    break;
                case PL011_REG_UARTLCR_H_SPS:
                    enmParity = PDMSERIALPARITY_MARK;
                    break;
                default:
                    /* We should never get here as all cases where caught earlier. */
                    AssertMsgFailed(("This shouldn't happen at all: %#x\n",
                                     pThis->uRegLcrH & (PL011_REG_UARTLCR_H_EPS | PL011_REG_UARTLCR_H_SPS)));
            }

            cFrameBits++;
        }

        uint64_t uTimerFreq = PDMDevHlpTimerGetFreq(pDevIns, pThis->hTimerTxUnconnected);
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
        PL011_REG_CLR(pThis->uRegFr, PL011_REG_UARTFR_BUSY | PL011_REG_UARTFR_TXFF);
    }
}


/**
 * Reset the transmit/receive related bits to the standard values
 * (after a detach/attach/reset event).
 *
 * @param   pDevIns             The device instance.
 * @param   pThis               The shared serial port instance data.
 * @param   pThisCC             The serial port instance data for the current context.
 */
static void pl011R3XferReset(PPDMDEVINS pDevIns, PDEVPL011 pThis, PDEVPL011CC pThisCC)
{
    //PDMDevHlpTimerStop(pDevIns, pThis->hTimerRcvFifoTimeout);
    PDMDevHlpTimerStop(pDevIns, pThis->hTimerTxUnconnected);
    pThis->uRegFr = PL011_REG_UARTFR_TXFE | PL011_REG_UARTFR_RXFE;

    pl011FifoClear(&pThis->FifoXmit);
    pl011FifoClear(&pThis->FifoRecv);
    pl011R3ParamsUpdate(pDevIns, pThis, pThisCC);
    pl011IrqUpdate(pDevIns, pThis, pThisCC);

    if (pThisCC->pDrvSerial)
    {
        /* Set the modem lines to reflect the current state. */
        int rc = pThisCC->pDrvSerial->pfnChgModemLines(pThisCC->pDrvSerial, false /*fRts*/, false /*fDtr*/);
        if (RT_FAILURE(rc))
            LogRel(("PL011#%d: Failed to set modem lines with %Rrc during reset\n",
                    pDevIns->iInstance, rc));

        uint32_t fStsLines = 0;
        rc = pThisCC->pDrvSerial->pfnQueryStsLines(pThisCC->pDrvSerial, &fStsLines);
        if (RT_SUCCESS(rc))
        {} //uartR3StsLinesUpdate(pDevIns, pThis, pThisCC, fStsLines);
        else
            LogRel(("PL011#%d: Failed to query status line status with %Rrc during reset\n",
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
static void pl011R3TxQueueCopyFrom(PPDMDEVINS pDevIns, PDEVPL011 pThis, PDEVPL011CC pThisCC,
                                   void *pvBuf, size_t cbRead, size_t *pcbRead)
{
    if (pThis->uRegLcrH & PL011_REG_UARTLCR_H_FEN)
    {
        *pcbRead = pl011FifoCopyTo(&pThis->FifoXmit, pvBuf, cbRead);
        if (!pThis->FifoXmit.cbUsed)
        {
            PL011_REG_SET(pThis->uRegFr, PL011_REG_UARTFR_TXFE);
            PL011_REG_CLR(pThis->uRegFr, PL011_REG_UARTFR_BUSY);
            //pThis->fThreEmptyPending = true;
        }
        if (*pcbRead)
            PL011_REG_CLR(pThis->uRegFr, PL011_REG_UARTFR_TXFF);
        pl011IrqUpdate(pDevIns, pThis, pThisCC);
    }
    else if (pThis->uRegFr & PL011_REG_UARTFR_BUSY)
    {
        *(uint8_t *)pvBuf = pThis->uRegDr;
        *pcbRead = 1;
        PL011_REG_CLR(pThis->uRegFr, PL011_REG_UARTFR_BUSY | PL011_REG_UARTFR_TXFF);
        pl011IrqUpdate(pDevIns, pThis, pThisCC);
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
#endif


/* -=-=-=-=-=- MMIO callbacks -=-=-=-=-=- */


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) pl011MmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PDEVPL011 pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);
    PDEVPL011CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPL011CC);
    NOREF(pvUser);
    Assert(cb == 4 || cb == 8);
    Assert(!(off & (cb - 1))); RT_NOREF(cb);

    uint32_t u32Val = 0;
    VBOXSTRICTRC rc = VINF_SUCCESS;
    switch (off)
    {
        case PL011_REG_UARTDR_INDEX:
        {
            if (pThis->uRegLcrH & PL011_REG_UARTLCR_H_FEN)
            {
                /*
                 * Only go back to R3 if there is new data available for the FIFO
                 * and we would clear the interrupt to fill it up again.
                 */
                if (   pThis->FifoRecv.cbUsed <= pThis->FifoRecv.cbItl
                    && ASMAtomicReadU32(&pThis->cbAvailRdr) > 0)
                {
#ifndef IN_RING3
                    rc = VINF_IOM_R3_MMIO_READ;
#else
                    pl011R3RecvFifoFill(pDevIns, pThisCC, pThis);
#endif
                }

                if (rc == VINF_SUCCESS)
                {
                    u32Val = pl011FifoGet(&pThis->FifoRecv);
                    //pThis->fIrqCtiPending = false;
                    if (!pThis->FifoRecv.cbUsed)
                    {
                        //PDMDevHlpTimerStop(pDevIns, pThis->hTimerRcvFifoTimeout);
                        PL011_REG_CLR(pThis->uRegFr, PL011_REG_UARTFR_RXFF);
                        PL011_REG_SET(pThis->uRegFr, PL011_REG_UARTFR_RXFE);
                    }
                    pl011IrqUpdate(pDevIns, pThis, pThisCC);
                }
            }
            else
            {
                u32Val = pThis->uRegDrRd;
                if (pThis->cbAvailRdr)
                {
                    uint32_t cbAvail = ASMAtomicDecU32(&pThis->cbAvailRdr);
                    if (!cbAvail)
                    {
                        PL011_REG_SET(pThis->uRegFr, PL011_REG_UARTFR_RXFE);
                        PL011_REG_CLR(pThis->uRegIrqSts, PL011_REG_UARTRIS_RXRIS);
                        pl011IrqUpdate(pDevIns, pThis, pThisCC);
                    }
                    else
                    {
    #ifndef IN_RING3
                        /* Restore state and go back to R3. */
                        ASMAtomicIncU32(&pThis->cbAvailRdr);
                        rc = VINF_IOM_R3_MMIO_READ;
    #else
                        /* Fetch new data and keep the DR bit set. */
                        pl011R3DataFetch(pDevIns, pThisCC, pThis);
    #endif
                    }
                }
            }
            break;
        }
        case PL011_REG_UARTRSR_ECR_INDEX:
        {
            AssertReleaseFailed();
            break;
        }
        case PL011_REG_UARTFR_INDEX:
            u32Val = pThis->uRegFr;
            break;
        case PL011_REG_UARTILPR_INDEX:
            u32Val = 0;
            AssertReleaseFailed();
            break;
        case PL011_REG_UARTIBRD_INDEX:
            u32Val = pThis->uRegIbrd;
            break;
        case PL011_REG_UARTFBRD_INDEX:
            u32Val = pThis->uRegFbrd;
            break;
        case PL011_REG_UARTLCR_H_INDEX:
            u32Val = pThis->uRegLcrH;
            break;
        case PL011_REG_UARTCR_INDEX:
            u32Val = pThis->uRegCr;
            break;
        case PL011_REG_UARTIFLS_INDEX:
            u32Val = pThis->uRegFifoLvlSel;
            break;
        case PL011_REG_UARTIMSC_INDEX:
            u32Val = pThis->uRegIrqMask;
            break;
        case PL011_REG_UARTRIS_INDEX:
            u32Val = pThis->uRegIrqSts;
            break;
        case PL011_REG_UARTMIS_INDEX:
            u32Val = pThis->uRegIrqSts & ~pThis->uRegIrqMask;
            break;
        case PL011_REG_UART_PERIPH_ID0_INDEX:
            u32Val = 0x11;
            break;
        case PL011_REG_UART_PERIPH_ID1_INDEX:
            u32Val = 0x10;
            break;
        case PL011_REG_UART_PERIPH_ID2_INDEX:
            u32Val = 0x34; /* r1p5 */
            break;
        case PL011_REG_UART_PERIPH_ID3_INDEX:
            u32Val = 0x00;
            break;
        case PL011_REG_UART_PCELL_ID0_INDEX:
            u32Val = 0x0d;
            break;
        case PL011_REG_UART_PCELL_ID1_INDEX:
            u32Val = 0xf0;
            break;
        case PL011_REG_UART_PCELL_ID2_INDEX:
            u32Val = 0x05;
            break;
        case PL011_REG_UART_PCELL_ID3_INDEX:
            u32Val = 0xb1;
            break;
        default:
            break;
    }

    if (rc == VINF_SUCCESS)
        *(uint32_t *)pv = u32Val;

    LogFlowFunc(("%RGp cb=%u u32Val=%#RX32 -> %Rrc\n", off, cb, u32Val, rc));

    return rc;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) pl011MmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PDEVPL011   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);
    PDEVPL011CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPL011CC);
    LogFlowFunc(("cb=%u reg=%RGp val=%llx\n", cb, off, cb == 4 ? *(uint32_t *)pv : cb == 8 ? *(uint64_t *)pv : 0xdeadbeef));
    RT_NOREF(pvUser);
    Assert(cb == 4 || cb == 8);
    Assert(!(off & (cb - 1))); RT_NOREF(cb);

    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    uint32_t u32Val = *(uint32_t *)pv;
    switch (off)
    {
        case PL011_REG_UARTDR_INDEX:
            if (   (pThis->uRegCr & PL011_REG_UARTCR_UARTEN)
                && (pThis->uRegCr & PL011_REG_UARTCR_TXE))
                rcStrict = pl011Xmit(pDevIns, pThisCC, pThis, (uint8_t)u32Val);
            break;
        case PL011_REG_UARTLCR_H_INDEX:
            pThis->uRegLcrH = (uint16_t)u32Val;
            pl011R3ParamsUpdate(pDevIns, pThis, pThisCC);
            break;
        case PL011_REG_UARTIMSC_INDEX:
            pThis->uRegIrqMask = (uint16_t)u32Val;
            break;
        case PL011_REG_UARTICR_INDEX:
            pThis->uRegIrqSts = 0;
            pl011IrqUpdate(pDevIns, pThis, pThisCC);
            break;
        default:
            break;

    }
    return rcStrict;
}


#ifdef IN_RING3

/* -=-=-=-=-=-=-=-=- Timer callbacks -=-=-=-=-=-=-=-=- */

/**
 * @callback_method_impl{FNTMTIMERDEV,
 *      TX timer function when there is no driver connected for
 *      draining the THR/FIFO.}
 */
static DECLCALLBACK(void) pl011R3TxUnconnectedTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    RT_NOREF(pvUser);

    LogFlowFunc(("pDevIns=%#p hTimer=%#p pvUser=%#p\n", pDevIns, hTimer, pvUser));
    PDEVPL011   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);
    PDEVPL011CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPL011CC);
    Assert(hTimer == pThis->hTimerTxUnconnected);

    uint8_t bVal = 0;
    size_t cbRead = 0;
    pl011R3TxQueueCopyFrom(pDevIns, pThis, pThisCC, &bVal, sizeof(bVal), &cbRead);
    if (pThis->uRegCr & PL011_REG_UARTCR_LBE)
    {
        /* Loopback mode is active, feed in the data at the receiving end. */
        uint32_t cbAvailOld = ASMAtomicAddU32(&pThis->cbAvailRdr, 1);
        if (pThis->uRegLcrH & PL011_REG_UARTLCR_H_FEN)
        {
            AssertReleaseFailed();
            ASMAtomicSubU32(&pThis->cbAvailRdr, 1);
        }
        else if (!cbAvailOld)
        {
            pThis->uRegDr = bVal;
            PL011_REG_SET(pThis->uRegFr, PL011_REG_UARTFR_TXFF);
            PL011_REG_CLR(pThis->uRegFr, PL011_REG_UARTFR_TXFE);
            pl011IrqUpdate(pDevIns, pThis, pThisCC);
        }
        else
            ASMAtomicSubU32(&pThis->cbAvailRdr, 1);
    }

    if (cbRead == 1)
        PDMDevHlpTimerSetRelative(pDevIns, hTimer, pThis->cSymbolXferTicks, NULL);
    else
    {
        /* No data left, set the transmitter holding register as empty. */
        PL011_REG_CLR(pThis->uRegFr, PL011_REG_UARTFR_BUSY | PL011_REG_UARTFR_TXFF);
        PL011_REG_SET(pThis->uRegFr, PL011_REG_UARTFR_TXFE);
    }
}


/* -=-=-=-=-=-=-=-=- PDMISERIALPORT on LUN#0 -=-=-=-=-=-=-=-=- */


/**
 * @interface_method_impl{PDMISERIALPORT,pfnDataAvailRdrNotify}
 */
static DECLCALLBACK(int) pl011R3DataAvailRdrNotify(PPDMISERIALPORT pInterface, size_t cbAvail)
{
    LogFlowFunc(("pInterface=%#p cbAvail=%zu\n", pInterface, cbAvail));
    PDEVPL011CC pThisCC = RT_FROM_MEMBER(pInterface, DEVPL011CC, ISerialPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PDEVPL011   pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);

    AssertMsg((uint32_t)cbAvail == cbAvail, ("Too much data available\n"));

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    uint32_t cbAvailOld = ASMAtomicAddU32(&pThis->cbAvailRdr, (uint32_t)cbAvail);
    LogFlow(("    cbAvailRdr=%u -> cbAvailRdr=%u\n", cbAvailOld, cbAvail + cbAvailOld));
    if (pThis->uRegLcrH & PL011_REG_UARTLCR_H_FEN)
        pl011R3RecvFifoFill(pDevIns, pThisCC, pThis);
    else if (!cbAvailOld)
    {
        size_t cbRead = 0;
        int rc = pThisCC->pDrvSerial->pfnReadRdr(pThisCC->pDrvSerial, &pThis->uRegDrRd, 1, &cbRead);
        AssertRC(rc);

        if (cbRead)
        {
            PL011_REG_SET(pThis->uRegIrqSts, PL011_REG_UARTRIS_RXRIS);
            PL011_REG_CLR(pThis->uRegFr, PL011_REG_UARTFR_TXFE);
            PL011_REG_SET(pThis->uRegFr, PL011_REG_UARTFR_TXFF);

            pl011IrqUpdate(pDevIns, pThis, pThisCC);
        }
    }

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMISERIALPORT,pfnDataSentNotify}
 */
static DECLCALLBACK(int) pl011R3DataSentNotify(PPDMISERIALPORT pInterface)
{
    LogFlowFunc(("pInterface=%#p\n", pInterface));
    PDEVPL011CC pThisCC = RT_FROM_MEMBER(pInterface, DEVPL011CC, ISerialPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PDEVPL011   pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);

    /* Set the transmitter empty bit because everything was sent. */
    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    /** @todo */
    RT_NOREF(pThis);

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMISERIALPORT,pfnReadWr}
 */
static DECLCALLBACK(int) pl011R3ReadWr(PPDMISERIALPORT pInterface, void *pvBuf, size_t cbRead, size_t *pcbRead)
{
    LogFlowFunc(("pInterface=%#p pvBuf=%#p cbRead=%zu pcbRead=%#p\n", pInterface, pvBuf, cbRead, pcbRead));
    PDEVPL011CC pThisCC = RT_FROM_MEMBER(pInterface, DEVPL011CC, ISerialPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PDEVPL011   pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);

    AssertReturn(cbRead > 0, VERR_INVALID_PARAMETER);

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    pl011R3TxQueueCopyFrom(pDevIns, pThis, pThisCC, pvBuf, cbRead, pcbRead);

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    LogFlowFunc(("-> VINF_SUCCESS{*pcbRead=%zu}\n", *pcbRead));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMISERIALPORT,pfnNotifyStsLinesChanged}
 */
static DECLCALLBACK(int) pl011R3NotifyStsLinesChanged(PPDMISERIALPORT pInterface, uint32_t fNewStatusLines)
{
    LogFlowFunc(("pInterface=%#p fNewStatusLines=%#x\n", pInterface, fNewStatusLines));
    PDEVPL011CC pThisCC = RT_FROM_MEMBER(pInterface, DEVPL011CC, ISerialPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PDEVPL011   pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    /** @todo */
    RT_NOREF(pThis, fNewStatusLines);

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMISERIALPORT,pfnNotifyBrk}
 */
static DECLCALLBACK(int) pl011R3NotifyBrk(PPDMISERIALPORT pInterface)
{
    LogFlowFunc(("pInterface=%#p\n", pInterface));
    PDEVPL011CC pThisCC = RT_FROM_MEMBER(pInterface, DEVPL011CC, ISerialPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PDEVPL011   pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    PL011_REG_SET(pThis->uRegIrqSts, PL011_REG_UARTRIS_BERIS);
    pl011IrqUpdate(pDevIns, pThis, pThisCC);

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return VINF_SUCCESS;
}


/* -=-=-=-=-=-=-=-=- PDMIBASE -=-=-=-=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) pl011R3QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PDEVPL011CC pThisCC = RT_FROM_MEMBER(pInterface, DEVPL011CC, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMISERIALPORT, &pThisCC->ISerialPort);
    return NULL;
}


/* -=-=-=-=-=-=-=-=- Saved State -=-=-=-=-=-=-=-=- */

/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC}
 */
static DECLCALLBACK(int) pl011R3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PDEVPL011       pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    RT_NOREF(uPass);

    pHlp->pfnSSMPutU16(pSSM, pThis->u16Irq);
    pHlp->pfnSSMPutGCPhys(pSSM, pThis->GCPhysMmioBase);
    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * Saves the given FIFO.
 *
 * @returns VBox status code.
 * @param   pHlp                The device helper table.
 * @param   pFifo               The FIFO to save.
 * @param   pSSM                The saved state handle to save the state to.
 */
static int pl011R3FifoSaveExec(PCPDMDEVHLPR3 pHlp, PPL011FIFO pFifo, PSSMHANDLE pSSM)
{
    pHlp->pfnSSMPutU8(pSSM, pFifo->cbMax);
    pHlp->pfnSSMPutU8(pSSM, pFifo->cbUsed);
    pHlp->pfnSSMPutU8(pSSM, pFifo->offWrite);
    pHlp->pfnSSMPutU8(pSSM, pFifo->offRead);
    pHlp->pfnSSMPutU8(pSSM, pFifo->cbItl);
    return pHlp->pfnSSMPutMem(pSSM, &pFifo->abBuf[0], sizeof(pFifo->abBuf));
}


/**
 * Loads the given FIFO.
 *
 * @returns VBox status code.
 * @param   pHlp                The device helper table.
 * @param   pFifo               The FIFO to load.
 * @param   pSSM                The saved state handle to load the state from.
 */
static int pl011R3FifoLoadExec(PCPDMDEVHLPR3 pHlp, PPL011FIFO pFifo, PSSMHANDLE pSSM)
{
    pHlp->pfnSSMGetU8(pSSM, &pFifo->cbMax);
    pHlp->pfnSSMGetU8(pSSM, &pFifo->cbUsed);
    pHlp->pfnSSMGetU8(pSSM, &pFifo->offWrite);
    pHlp->pfnSSMGetU8(pSSM, &pFifo->offRead);
    pHlp->pfnSSMGetU8(pSSM, &pFifo->cbItl);
    int rc = pHlp->pfnSSMGetMem(pSSM, &pFifo->abBuf[0], sizeof(pFifo->abBuf));
    if (RT_FAILURE(rc))
        return rc;

    AssertReturn (   pFifo->cbMax    <= sizeof(pFifo->abBuf)
                  && pFifo->cbUsed   <= sizeof(pFifo->abBuf)
                  && pFifo->offWrite <  sizeof(pFifo->abBuf)
                  && pFifo->offRead  <  sizeof(pFifo->abBuf)
                  && pFifo->cbItl    <= sizeof(pFifo->abBuf),
                  VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) pl011R3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVPL011       pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    /* The config. */
    pl011R3LiveExec(pDevIns, pSSM, SSM_PASS_FINAL);

    /* The state. */
    pHlp->pfnSSMPutU8( pSSM, pThis->uRegDr);
    pHlp->pfnSSMPutU8( pSSM, pThis->uRegDrRd);
    pHlp->pfnSSMPutU16(pSSM, pThis->uRegCr);
    pHlp->pfnSSMPutU16(pSSM, pThis->uRegFr);
    pHlp->pfnSSMPutU16(pSSM, pThis->uRegIbrd);
    pHlp->pfnSSMPutU16(pSSM, pThis->uRegFbrd);
    pHlp->pfnSSMPutU16(pSSM, pThis->uRegLcrH);
    pHlp->pfnSSMPutU16(pSSM, pThis->uRegFifoLvlSel);
    pHlp->pfnSSMPutU16(pSSM, pThis->uRegIrqMask);
    pHlp->pfnSSMPutU16(pSSM, pThis->uRegIrqSts);

    int rc = pl011R3FifoSaveExec(pHlp, &pThis->FifoXmit, pSSM);
    if (RT_SUCCESS(rc))
        rc =pl011R3FifoSaveExec(pHlp, &pThis->FifoRecv, pSSM);
    if (RT_SUCCESS(rc))
        rc = PDMDevHlpTimerSave(pDevIns, pThis->hTimerTxUnconnected, pSSM);

    if (RT_FAILURE(rc))
        return rc;

    return pHlp->pfnSSMPutU32(pSSM, UINT32_MAX); /* sanity/terminator */
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) pl011R3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDEVPL011       pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    if (uVersion != PL011_SAVED_STATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* The config. */
    uint16_t u16Irq;
    int rc = pHlp->pfnSSMGetU16(pSSM, &u16Irq);
    AssertRCReturn(rc, rc);
    if (u16Irq != pThis->u16Irq)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - u16Irq: saved=%#x config=%#x"), u16Irq, pThis->u16Irq);

    RTGCPHYS GCPhysMmioBase;
    rc = pHlp->pfnSSMGetGCPhys(pSSM, &GCPhysMmioBase);
    AssertRCReturn(rc, rc);
    if (GCPhysMmioBase != pThis->GCPhysMmioBase)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - GCPhysMmioBase: saved=%RGp config=%RGp"), GCPhysMmioBase, pThis->GCPhysMmioBase);

    if (uPass != SSM_PASS_FINAL)
        return VINF_SUCCESS;

    /* The state. */
    pHlp->pfnSSMGetU8( pSSM, &pThis->uRegDr);
    pHlp->pfnSSMGetU8( pSSM, &pThis->uRegDrRd);
    pHlp->pfnSSMGetU16(pSSM, &pThis->uRegCr);
    pHlp->pfnSSMGetU16(pSSM, &pThis->uRegFr);
    pHlp->pfnSSMGetU16(pSSM, &pThis->uRegIbrd);
    pHlp->pfnSSMGetU16(pSSM, &pThis->uRegFbrd);
    pHlp->pfnSSMGetU16(pSSM, &pThis->uRegLcrH);
    pHlp->pfnSSMGetU16(pSSM, &pThis->uRegFifoLvlSel);
    pHlp->pfnSSMGetU16(pSSM, &pThis->uRegIrqMask);
    pHlp->pfnSSMGetU16(pSSM, &pThis->uRegIrqSts);

    rc = pl011R3FifoLoadExec(pHlp, &pThis->FifoXmit, pSSM);
    if (RT_SUCCESS(rc))
        rc = pl011R3FifoLoadExec(pHlp, &pThis->FifoRecv, pSSM);
    if (RT_SUCCESS(rc))
    {
        rc = PDMDevHlpTimerLoad(pDevIns, pThis->hTimerTxUnconnected, pSSM);
        AssertRCReturn(rc, rc);
    }

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
static DECLCALLBACK(int) pl011R3LoadDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVPL011   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);
    PDEVPL011CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPL011CC);

    RT_NOREF(pSSM);

    pl011R3ParamsUpdate(pDevIns, pThis, pThisCC);
    pl011IrqUpdate(pDevIns, pThis, pThisCC);

    if (pThisCC->pDrvSerial)
    {
        /* Set the modem lines to reflect the current state. */
        /** @todo */
        int rc = VINF_SUCCESS;  //pThisCC->pDrvSerial->pfnChgModemLines(pThisCC->pDrvSerial,
                                //                       RT_BOOL(pThis->uRegMcr & UART_REG_MCR_RTS),
                                //                       RT_BOOL(pThis->uRegMcr & UART_REG_MCR_DTR));
        if (RT_FAILURE(rc))
            LogRel(("PL011#%d: Failed to set modem lines with %Rrc during saved state load\n",
                    pDevIns->iInstance, rc));

        uint32_t fStsLines = 0;
        rc = pThisCC->pDrvSerial->pfnQueryStsLines(pThisCC->pDrvSerial, &fStsLines);
        if (RT_SUCCESS(rc))
            ;//uartR3StsLinesUpdate(pDevIns, pThis, pThisCC, fStsLines);
        else
            LogRel(("PL011#%d: Failed to query status line status with %Rrc during reset\n",
                    pDevIns->iInstance, rc));
    }

    return VINF_SUCCESS;
}


/* -=-=-=-=-=-=-=-=- PDMDEVREG -=-=-=-=-=-=-=-=- */

/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) pl011R3Reset(PPDMDEVINS pDevIns)
{
    PDEVPL011   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);
    PDEVPL011CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPL011CC);

    pThis->uRegDr   = 0;
    /* UARTEN is normally 0 on reset but UEFI doesn't set it causing the serial port to not log anything. */
    pThis->uRegCr   = PL011_REG_UARTCR_TXE | PL011_REG_UARTCR_RXE | PL011_REG_UARTCR_UARTEN;
    pThis->uRegFr   = PL011_REG_UARTFR_TXFE | PL011_REG_UARTFR_RXFE;
    pThis->uRegIbrd = 0;
    pThis->uRegFbrd = 0;
    pThis->uRegLcrH = 0;
    pThis->uRegFifoLvlSel = 0;
    pThis->uRegIrqSts     = 0; /** @todo Not entirely correct as the modem status bits should reflect the lower state. */
    pThis->uRegIrqMask    = 0;
    /** @todo */

    pThis->FifoXmit.cbMax = 32;
    pThis->FifoRecv.cbMax = 32;
    pThis->FifoRecv.cbItl = 1;

    pl011R3XferReset(pDevIns, pThis, pThisCC);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnAttach}
 */
static DECLCALLBACK(int) pl011R3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PDEVPL011   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);
    PDEVPL011CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPL011CC);
    RT_NOREF(fFlags);
    AssertReturn(iLUN == 0, VERR_PDM_LUN_NOT_FOUND);

    int rc = PDMDevHlpDriverAttach(pDevIns, iLUN, &pThisCC->IBase, &pThisCC->pDrvBase, "PL011 Char");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrvSerial = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMISERIALCONNECTOR);
        if (!pThisCC->pDrvSerial)
        {
            AssertLogRelMsgFailed(("Configuration error: instance %d has no serial interface!\n", pDevIns->iInstance));
            return VERR_PDM_MISSING_INTERFACE;
        }
        rc = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
        if (RT_SUCCESS(rc))
        {
            pl011R3XferReset(pDevIns, pThis, pThisCC);
            PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
        }
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
    {
        pThisCC->pDrvBase = NULL;
        pThisCC->pDrvSerial = NULL;
        rc = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
        if (RT_SUCCESS(rc))
        {
            pl011R3XferReset(pDevIns, pThis, pThisCC);
            PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
        }
        LogRel(("PL011#%d: no unit\n", pDevIns->iInstance));
    }
    else /* Don't call VMSetError here as we assume that the driver already set an appropriate error */
        LogRel(("PL011#%d: Failed to attach to serial driver. rc=%Rrc\n", pDevIns->iInstance, rc));

   return rc;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDetach}
 */
static DECLCALLBACK(void) pl011R3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PDEVPL011   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);
    PDEVPL011CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPL011CC);
    RT_NOREF(fFlags);
    AssertReturnVoid(iLUN == 0);

    /* Zero out important members. */
    pThisCC->pDrvBase   = NULL;
    pThisCC->pDrvSerial = NULL;

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    pl011R3XferReset(pDevIns, pThis, pThisCC);

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) pl011R3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);

    /* Nothing to do. */
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) pl011R3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVPL011       pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);
    PDEVPL011CC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPL011CC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;

    Assert(iInstance < 4);

    pThisCC->pDevIns                                = pDevIns;

    /* IBase */
    pThisCC->IBase.pfnQueryInterface                = pl011R3QueryInterface;

    /* ISerialPort */
    pThisCC->ISerialPort.pfnDataAvailRdrNotify      = pl011R3DataAvailRdrNotify;
    pThisCC->ISerialPort.pfnDataSentNotify          = pl011R3DataSentNotify;
    pThisCC->ISerialPort.pfnReadWr                  = pl011R3ReadWr;
    pThisCC->ISerialPort.pfnNotifyStsLinesChanged   = pl011R3NotifyStsLinesChanged;
    pThisCC->ISerialPort.pfnNotifyBrk               = pl011R3NotifyBrk;

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "Irq|MmioBase", "");

    uint16_t u16Irq = 0;
    rc = pHlp->pfnCFGMQueryU16(pCfg, "Irq", &u16Irq);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"Irq\" value"));

    RTGCPHYS GCPhysMmioBase = 0;
    rc = pHlp->pfnCFGMQueryU64(pCfg, "MmioBase", &GCPhysMmioBase);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"MmioBase\" value"));

    pThis->u16Irq         = u16Irq;
    pThis->GCPhysMmioBase = GCPhysMmioBase;

    /*
     * Register and map the MMIO region.
     */
    rc = PDMDevHlpMmioCreateAndMap(pDevIns, GCPhysMmioBase, PL011_MMIO_SIZE, pl011MmioWrite, pl011MmioRead,
                                   IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED, "PL011", &pThis->hMmio);
    AssertRCReturn(rc, rc);


    /*
     * Saved state.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, PL011_SAVED_STATE_VERSION, sizeof(*pThis), NULL,
                                NULL, pl011R3LiveExec, NULL,
                                NULL, pl011R3SaveExec, NULL,
                                NULL, pl011R3LoadExec, pl011R3LoadDone);
    AssertRCReturn(rc, rc);

    /*
     * Attach the char driver and get the interfaces.
     */
    rc = PDMDevHlpDriverAttach(pDevIns, 0 /*iLUN*/, &pThisCC->IBase, &pThisCC->pDrvBase, "UART");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrvSerial = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMISERIALCONNECTOR);
        if (!pThisCC->pDrvSerial)
        {
            AssertLogRelMsgFailed(("Configuration error: instance %d has no serial interface!\n", iInstance));
            return VERR_PDM_MISSING_INTERFACE;
        }
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
    {
        pThisCC->pDrvBase   = NULL;
        pThisCC->pDrvSerial = NULL;
        LogRel(("PL011#%d: no unit\n", iInstance));
    }
    else
    {
        AssertLogRelMsgFailed(("PL011#%d: Failed to attach to char driver. rc=%Rrc\n", iInstance, rc));
        /* Don't call VMSetError here as we assume that the driver already set an appropriate error */
        return rc;
    }

    /*
     * Create the transmit timer when no device is connected.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, pl011R3TxUnconnectedTimer, pThisCC,
                              TMTIMER_FLAGS_NO_RING0, "UART TX unconnect",
                              &pThis->hTimerTxUnconnected);
    AssertRCReturn(rc, rc);

    pl011R3Reset(pDevIns);
    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) pl011RZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVPL011   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPL011);
    PDEVPL011CC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPL011CC);

    int rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, pl011MmioWrite, pl011MmioRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DevicePl011 =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "arm-pl011",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_SERIAL,
    /* .cMaxInstances = */          UINT32_MAX,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVPL011),
    /* .cbInstanceCC = */           sizeof(DEVPL011CC),
    /* .cbInstanceRC = */           sizeof(DEVPL011RC),
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "ARM PL011 PrimeCell UART",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           pl011R3Construct,
    /* .pfnDestruct = */            pl011R3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               pl011R3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              pl011R3Attach,
    /* .pfnDetach = */              pl011R3Detach,
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
    /* .pfnConstruct = */           pl011RZConstruct,
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
    /* .pfnConstruct = */           pl011RZConstruct,
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

