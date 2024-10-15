/* $Id: UartCore.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef VBOX_INCLUDED_SRC_Serial_UartCore_h
#define VBOX_INCLUDED_SRC_Serial_UartCore_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmserialifs.h>
#include <VBox/vmm/ssm.h>
#include <iprt/assert.h>

RT_C_DECLS_BEGIN

/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** The current serial code saved state version. */
#define UART_SAVED_STATE_VERSION                                        7
/** Saved state version before the TX timer for the connected device case was added. */
#define UART_SAVED_STATE_VERSION_PRE_UNCONNECTED_TX_TIMER               6
/** Saved state version of the legacy code which got replaced after 5.2. */
#define UART_SAVED_STATE_VERSION_LEGACY_CODE                            5
/** Includes some missing bits from the previous saved state. */
#define UART_SAVED_STATE_VERSION_MISSING_BITS                           4
/** Saved state version when only the 16450 variant was implemented. */
#define UART_SAVED_STATE_VERSION_16450                                  3

/** Maximum size of a FIFO. */
#define UART_FIFO_LENGTH_MAX                 128


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** Pointer to the UART core state. */
typedef struct UARTCORE *PUARTCORE;


/**
 * UART core IRQ request callback to let the core instance raise/clear interrupt requests.
 *
 * @param   pDevIns             The owning device instance.
 * @param   pThis               The shared UART core instance data.
 * @param   iLUN                The LUN associated with the UART core.
 * @param   iLvl                The interrupt level.
 */
typedef DECLCALLBACKTYPE(void, FNUARTCOREIRQREQ,(PPDMDEVINS pDevIns, PUARTCORE pThis, unsigned iLUN, int iLvl));
/** Pointer to a UART core IRQ request callback. */
typedef FNUARTCOREIRQREQ *PFNUARTCOREIRQREQ;


/**
 * UART type.
 */
typedef enum UARTTYPE
{
    /** Invalid UART type. */
    UARTTYPE_INVALID = 0,
    /** 16450 UART type. */
    UARTTYPE_16450,
    /** 16550A UART type. */
    UARTTYPE_16550A,
    /** 16750 UART type. */
    UARTTYPE_16750,
        /** 32bit hack. */
    UARTTYPE_32BIT_HACK = 0x7fffffff
} UARTTYPE;


/**
 * UART FIFO.
 */
typedef struct UARTFIFO
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
    uint8_t                         abBuf[UART_FIFO_LENGTH_MAX];
    /** Alignment to a 4 byte boundary. */
    uint8_t                         au8Alignment0[3];
} UARTFIFO;
/** Pointer to a FIFO. */
typedef UARTFIFO *PUARTFIFO;


/**
 * Shared UART core device state.
 *
 * @implements  PDMIBASE
 * @implements  PDMISERIALPORT
 */
typedef struct UARTCORE
{
    /** Access critical section. */
    PDMCRITSECT                     CritSect;
    /** The LUN on the owning device instance for this core. */
    uint32_t                        iLUN;
    /** Configuration flags. */
    uint32_t                        fFlags;
    /** The selected UART type. */
    UARTTYPE                        enmType;

    /** The divisor register (DLAB = 1). */
    uint16_t                        uRegDivisor;
    /** The Receiver Buffer Register (RBR, DLAB = 0). */
    uint8_t                         uRegRbr;
    /** The Transmitter Holding Register (THR, DLAB = 0). */
    uint8_t                         uRegThr;
    /** The Interrupt Enable Register (IER, DLAB = 0). */
    uint8_t                         uRegIer;
    /** The Interrupt Identification Register (IIR). */
    uint8_t                         uRegIir;
    /** The FIFO Control Register (FCR). */
    uint8_t                         uRegFcr;
    /** The Line Control Register (LCR). */
    uint8_t                         uRegLcr;
    /** The Modem Control Register (MCR). */
    uint8_t                         uRegMcr;
    /** The Line Status Register (LSR). */
    uint8_t                         uRegLsr;
    /** The Modem Status Register (MSR). */
    uint8_t                         uRegMsr;
    /** The Scratch Register (SCR). */
    uint8_t                         uRegScr;

    /** Timer handle for the character timeout indication. */
    TMTIMERHANDLE                   hTimerRcvFifoTimeout;
    /** Timer handle for the send loop if no driver is connected/loopback mode is active. */
    TMTIMERHANDLE                   hTimerTxUnconnected;

    /** Flag whether a character timeout interrupt is pending
     * (no symbols were inserted or removed from the receive FIFO
     * during an 4 times the character transmit/receive period and the FIFO
     * is not empty). */
    bool                            fIrqCtiPending;
    /** Flag whether the transmitter holding register went empty since last time the
     * IIR register was read. This gets reset when IIR is read so the guest will get this
     * interrupt ID only once. */
    bool                            fThreEmptyPending;
    /** Explicit alignment. */
    bool                            afAlignment1[2];
    /** The transmit FIFO. */
    UARTFIFO                        FifoXmit;
    /** The receive FIFO. */
    UARTFIFO                        FifoRecv;

    /** Time it takes to transmit/receive a single symbol in timer ticks. */
    uint64_t                        cSymbolXferTicks;
    /** Number of bytes available for reading from the layer below. */
    volatile uint32_t               cbAvailRdr;
    /** Explicit alignment. */
    uint32_t                        u32Alignment2;
} UARTCORE;
AssertCompileSizeAlignment(UARTCORE, 8);


/**
 * Ring-3 UART core device state.
 *
 * @implements  PDMIBASE
 * @implements  PDMISERIALPORT
 */
typedef struct UARTCORER3
{
    /** The LUN on the owning device instance for this core. */
    uint32_t                        iLUN;
    uint32_t                        u32Padding;
    /** LUN\#0: The base interface. */
    PDMIBASE                        IBase;
    /** LUN\#0: The serial port interface. */
    PDMISERIALPORT                  ISerialPort;
    /** Pointer to the attached base driver. */
    R3PTRTYPE(PPDMIBASE)            pDrvBase;
    /** Pointer to the attached serial driver. */
    R3PTRTYPE(PPDMISERIALCONNECTOR) pDrvSerial;

    /** Interrupt request callback of the owning device. */
    R3PTRTYPE(PFNUARTCOREIRQREQ)    pfnUartIrqReq;

    /** Pointer to the shared data - for timers callbacks and interface methods
     *  only. */
    R3PTRTYPE(PUARTCORE)            pShared;
    /** Pointer to the device instance - only for getting our bearings in
     *  interface methods. */
    PPDMDEVINS                      pDevIns;
} UARTCORER3;
/** Pointer to the core ring-3 UART device state. */
typedef UARTCORER3 *PUARTCORER3;


/**
 * Ring-0 UART core device state.
 */
typedef struct UARTCORER0
{
    /** Interrupt request callback of the owning device. */
    R0PTRTYPE(PFNUARTCOREIRQREQ)    pfnUartIrqReq;
} UARTCORER0;
/** Pointer to the core ring-0 UART device state. */
typedef UARTCORER0 *PUARTCORER0;


/**
 * Raw-mode UART core device state.
 */
typedef struct UARTCORERC
{
    /** Interrupt request callback of the owning device. */
    R0PTRTYPE(PFNUARTCOREIRQREQ)    pfnUartIrqReq;
} UARTCORERC;
/** Pointer to the core raw-mode UART device state. */
typedef UARTCORERC *PUARTCORERC;


/** Current context UAR core device state. */
typedef CTX_SUFF(UARTCORE) UARTCORECC;
/** Pointer to the current context UAR core device state. */
typedef CTX_SUFF(PUARTCORE) PUARTCORECC;


#ifndef VBOX_DEVICE_STRUCT_TESTCASE

/** Flag whether to yield the CPU on an LSR read. */
#define UART_CORE_YIELD_ON_LSR_READ      RT_BIT_32(0)

DECLHIDDEN(VBOXSTRICTRC) uartRegWrite(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC,
                                      uint32_t uReg, uint32_t u32, size_t cb);
DECLHIDDEN(VBOXSTRICTRC) uartRegRead(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC,
                                     uint32_t uReg, uint32_t *pu32, size_t cb);

# ifdef IN_RING3
DECLHIDDEN(int)  uartR3Init(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC,
                            UARTTYPE enmType, unsigned iLUN, uint32_t fFlags, PFNUARTCOREIRQREQ pfnUartIrqReq);
DECLHIDDEN(void) uartR3Destruct(PPDMDEVINS pDevIns, PUARTCORE pThis);
DECLHIDDEN(void) uartR3Detach(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC);
DECLHIDDEN(int)  uartR3Attach(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, unsigned iLUN);
DECLHIDDEN(void) uartR3Reset(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC);
DECLHIDDEN(int)  uartR3SaveExec(PPDMDEVINS pDevIns, PUARTCORE pThis, PSSMHANDLE pSSM);
DECLHIDDEN(int)  uartR3LoadExec(PPDMDEVINS pDevIns, PUARTCORE pThis, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass,
                                uint8_t *puIrq, RTIOPORT *pPortBase);
DECLHIDDEN(int)  uartR3LoadDone(PPDMDEVINS pDevIns, PUARTCORE pThis, PUARTCORECC pThisCC, PSSMHANDLE pSSM);

# endif /* IN_RING3 */
# if !defined(IN_RING3) || defined(DOXYGEN_RUNNING)
DECLHIDDEN(int) uartRZInit(PUARTCORECC pThisCC, PFNUARTCOREIRQREQ pfnUartIrqReq);
# endif

#endif

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Serial_UartCore_h */
