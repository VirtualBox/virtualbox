/* $Id: DevEEPROM.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevEEPROM - Microwire-compatible 64x16-bit 93C46 EEPROM Emulation, Header.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Network_DevEEPROM_h
#define VBOX_INCLUDED_SRC_Network_DevEEPROM_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

/** The current Saved state version. */
#define EEPROM93C46_SAVEDSTATE_VERSION 1

/**
 * 93C46-compatible EEPROM device emulation.
 *
 * @remarks This class is intended to be used in device
 *          emulation which imposes some restrictions if the
 *          device supports GC execution. This is why it is a
 *          plain-old-data structure.
 */
struct EEPROM93C46
{
    /** General definitions */
    enum {
        /** Size of EEPROM in words */
        SIZE        = 64,
        /** Number of bits per word */
        WORD_SIZE   = 16,
        /** Number of address bits */
        ADDR_SIZE   = 6,
        /** Number of bits in opcode */
        OPCODE_SIZE = 2,
        /** The most significant bit mask in data word */
        DATA_MSB    = 1<<(WORD_SIZE-1),
        /** Address mask */
        ADDR_MASK   = (1<<ADDR_SIZE)-1,
        /** The most significant bit mask in op+addr bit sequence */
        OPADDR_MSB  = 1<<(OPCODE_SIZE+ADDR_SIZE-1)
    };

    enum OP {
        OP_READ,
        OP_WRITE,
        OP_WRITE_ALL,
        OP_DECODE,
        OP_32BIT_HACK = 0x7fffffff
    };

    /**
     * Names of signal wires
     */
    enum Wires {
        WIRES_SK=0x1,    ///< Clock
        WIRES_CS=0x2,    ///< Chip Select
        WIRES_DI=0x4,    ///< Data In
        WIRES_DO=0x8     ///< Data Out
    };


    /** @todo save and load methods */
    void save(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM);
    int  load(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM);

    /** Actual content of EEPROM */
    uint16_t m_au16Data[SIZE];

    /** current state.
     *
     * EEPROM operates as a simple state machine. Events are primarily
     * triggered at positive edge of clock signal (SK). Refer to the
     * timing diagrams of 93C46 to get better understanding.
     */
    enum State {
        /** Initial state. Waiting for start condition (CS, SK, DI high). */
        STANDBY,
        /** Reading data in, shifting in the bits into 'word'. */
        READING_DI,
        /** Writing data out, shifting out the bits from 'word'. */
        WRITING_DO,
        /** Waiting for CS=0 to indicate we are busy (DO=0). */
        WAITING_CS_FALL,
        /** Waiting for CS=1 to indicate we are ready (DO=1). */
        WAITING_CS_RISE,
        /** Make this enum 4-byte */
        STATE_MAKE_32BIT_HACK = 0x7fffffff
    } m_eState;
    /** setting writeEnable to false prevents write and erase operations */
    bool m_fWriteEnabled;
    uint8_t Alignment1;
    /** intermediate storage */
    uint16_t m_u16Word;
    /** currently processed bit in 'word' */
    uint16_t m_u16Mask;
    /** decoded address */
    uint16_t m_u16Addr;
    /** Data Out, Data In, Chip Select, Clock */
    uint32_t m_u32InternalWires;

    /** Current opcode decoder. When no operation has been decoded yet
     *  it is set to OP_DECODE.
     */
    OP m_eOp;
#if HC_ARCH_BITS == 64
    uint32_t Alignment2;
#endif

#ifdef IN_RING3
    uint32_t read();
    void     write(uint32_t u32Wires);
    bool     readWord(uint32_t u32Addr, uint16_t *pu16Value);

    void init(const uint16_t *pu16Initial = 0);

    // Operation handlers
    State opDecode();
    State opRead();
    State opWrite();
    State opWriteAll();

    /** Helper method to implement write protection */
    void storeWord(uint32_t u32Addr, uint16_t u16Value);
#endif /* IN_RING3 */
};

#endif /* !VBOX_INCLUDED_SRC_Network_DevEEPROM_h */
