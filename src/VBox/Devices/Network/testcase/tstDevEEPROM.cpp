/* $Id: tstDevEEPROM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * EEPROM 93C46 unit tests.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#ifdef USE_CPPUNIT
# include <cppunit/ui/text/TestRunner.h>
# include <cppunit/extensions/HelperMacros.h>
#else
# include "CppUnitEmulation.h"
#endif
#include <VBox/vmm/pdmdev.h>
#include "../DevEEPROM.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const uint16_t g_abInitialContent[] =
{
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x000f,
    0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    0x0018, 0x0019, 0x001a, 0x001b, 0x001c, 0x001d, 0x001e, 0x001f,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f
};


/**
 * Test fixture for 93C46-compatible EEPROM device emulation.
 */
class EEPROMTest
#ifdef USE_CPPUNIT
    : public CppUnit::TestFixture
#endif
{
    CPPUNIT_TEST_SUITE( tstDevEEPROM );

    CPPUNIT_TEST( testRead );
    CPPUNIT_TEST( testSequentialRead );
    CPPUNIT_TEST( testWrite );
    CPPUNIT_TEST( testWriteAll );
    CPPUNIT_TEST( testWriteDisabled );
    CPPUNIT_TEST( testErase );
    CPPUNIT_TEST( testEraseAll );

    CPPUNIT_TEST_SUITE_END();

private:
    enum Wires { DO=8, DI=4, CS=2, SK=0x01 };
    enum OpCodes {
        READ_OPCODE  = 0x6,
        WRITE_OPCODE = 0x5,
        ERASE_OPCODE = 0x7,
        EWDS_OPCODE  = 0x10, // erase/write disable
        WRAL_OPCODE  = 0x11, // write all
        ERAL_OPCODE  = 0x12, // erase all
        EWEN_OPCODE  = 0x13  // erase/write enable
    };
    enum BitWidths {
        READ_OPCODE_BITS  =  3,
        WRITE_OPCODE_BITS =  3,
        ERASE_OPCODE_BITS =  3,
        EWDS_OPCODE_BITS  =  5,
        WRAL_OPCODE_BITS  =  5,
        ERAL_OPCODE_BITS  =  5,
        EWEN_OPCODE_BITS  =  5,
        READ_ADDR_BITS    =  6,
        WRITE_ADDR_BITS   =  6,
        ERASE_ADDR_BITS   =  6,
        EWDS_ADDR_BITS    =  4,
        WRAL_ADDR_BITS    =  4,
        ERAL_ADDR_BITS    =  4,
        EWEN_ADDR_BITS    =  4,
        DATA_BITS         = 16
    };

    EEPROM93C46 *eeprom;

    // Helper methods
    void shiftOutBits(uint16_t data, uint16_t count);
    uint16_t shiftInBits(uint16_t count);
    void getReady();
    void standby();
    void stop();
    uint16_t readAt(uint16_t addr);
    bool writeTo(uint16_t addr, uint16_t value);
    void writeOpAddr(int opCode, int opCodeBits, uint16_t addr, int addrBits);
    void writeData(uint16_t value) { shiftOutBits(value, DATA_BITS); }
    bool waitForCompletion();

public:
    void setUp()
    {
        eeprom = new EEPROM93C46;
        eeprom->init(g_abInitialContent);
    }

    void tearDown()
    {
        delete eeprom;
        eeprom = NULL;
    }

    void testSize()
    {
        CPPUNIT_ASSERT_EQUAL( sizeof(g_abInitialContent), (size_t)EEPROM93C46::SIZE );
    }

    void testRead()
    {
        getReady();
        for ( uint32_t wordAddr=0; wordAddr < EEPROM93C46::SIZE; wordAddr++ ) {
            shiftOutBits(READ_OPCODE, READ_OPCODE_BITS);
            shiftOutBits(wordAddr, READ_ADDR_BITS);

            CPPUNIT_ASSERT_EQUAL( g_abInitialContent[wordAddr], (uint16_t)wordAddr );
            CPPUNIT_ASSERT_EQUAL( g_abInitialContent[wordAddr], shiftInBits(DATA_BITS) );
            standby();
        }
        stop();
    }

    void testSequentialRead()
    {
        getReady();
        shiftOutBits(READ_OPCODE, READ_OPCODE_BITS);
        shiftOutBits(0, READ_ADDR_BITS);
        for ( int wordAddr=0; wordAddr < EEPROM93C46::SIZE; wordAddr++ ) {
            CPPUNIT_ASSERT_EQUAL( g_abInitialContent[wordAddr], shiftInBits(DATA_BITS) );
        }
        stop();
    }

    void testWrite()
    {
        //unused: int i;
        uint16_t wordAddr;

        getReady();
        // Enable write
        writeOpAddr(EWEN_OPCODE, EWEN_OPCODE_BITS, 0, EWEN_ADDR_BITS);
        standby();

        for ( wordAddr=0; wordAddr < EEPROM93C46::SIZE; wordAddr++ ) {
            //writeOpAddr(WRITE_OPCODE, WRITE_OPCODE_BITS, (uint16_t)wordAddr, WRITE_ADDR_BITS);
            writeTo(wordAddr, 0x3F00 - (wordAddr<<8));
            standby();

            if (!waitForCompletion()) {
                CPPUNIT_FAIL("EEPROM write was not completed");
                stop();
                return;
            }
            standby();
        }

        // Disable write
        writeOpAddr(EWDS_OPCODE, EWDS_OPCODE_BITS, 0, EWDS_ADDR_BITS);

        stop();

        // Now check the result
        getReady();
        writeOpAddr(READ_OPCODE, READ_OPCODE_BITS, 0, READ_ADDR_BITS);
        for ( wordAddr=0; wordAddr < EEPROM93C46::SIZE; wordAddr++ ) {
            CPPUNIT_ASSERT_EQUAL((uint16_t)(0x3F00 - (wordAddr<<8)), shiftInBits(DATA_BITS) );
        }
        stop();
    }

    void testWriteDisabled()
    {
        getReady();

        uint16_t addr = 0;
        uint16_t oldValue = readAt(addr);
        stop();
        getReady();
        if (writeTo(addr, ~oldValue)) {
            // Write appears to be successful -- continue
            CPPUNIT_ASSERT_EQUAL(oldValue, readAt(addr));
        }
        else {
            CPPUNIT_FAIL("EEPROM write was not completed");
        }
        stop();
    }

    void testErase()
    {
        int i;
        uint16_t addr = 0x1F;

        getReady();
        // Enable write
        shiftOutBits(EWEN_OPCODE, EWEN_OPCODE_BITS);
        shiftOutBits(0, EWEN_ADDR_BITS);
        standby();

        if (writeTo(addr, addr)) {
            stop();
            getReady();
            // Write successful -- continue
            CPPUNIT_ASSERT_EQUAL(addr, readAt(addr));
            stop();
            getReady();

            shiftOutBits(ERASE_OPCODE, ERASE_OPCODE_BITS);
            shiftOutBits(addr, ERASE_ADDR_BITS);

            standby();

            for (i = 0; i < 200; i++) {
                if (eeprom->read() & DO)
                    break;
                //usec_delay(50);
            }

            if (i == 200) {
                CPPUNIT_FAIL("EEPROM erase was not completed");
                stop();
                return;
            }

            standby();

            shiftOutBits(EWDS_OPCODE, EWDS_OPCODE_BITS);
            shiftOutBits(0, EWDS_ADDR_BITS);

            stop();
            getReady();
            CPPUNIT_ASSERT_EQUAL((uint16_t)0xFFFF, readAt(addr));
        }
        else {
            CPPUNIT_FAIL("EEPROM write was not completed");
        }
        stop();
    }

    void testWriteAll()
    {
        uint16_t addr;

        getReady();
        // Enable write
        writeOpAddr(EWEN_OPCODE, EWEN_OPCODE_BITS, 0, EWEN_ADDR_BITS);
        standby();
        // Fill all memory
        writeOpAddr(WRAL_OPCODE, WRAL_OPCODE_BITS, 0, WRAL_ADDR_BITS);
        writeData(0xABBA);
        standby();

        if (waitForCompletion()) {
            stop();
            getReady();
            // Write successful -- verify all memory
            for ( addr=0; addr < EEPROM93C46::SIZE; addr++ ) {
                CPPUNIT_ASSERT_EQUAL((uint16_t)0xABBA, readAt(addr));
            }
        }
        else {
            CPPUNIT_FAIL("EEPROM write was not completed");
        }
        stop();
    }

    void testEraseAll()
    {
        //unused: int i;
        uint16_t addr = 0x1F;

        getReady();
        // Enable write
        writeOpAddr(EWEN_OPCODE, EWEN_OPCODE_BITS, 0, EWEN_ADDR_BITS);
        standby();
        // Fill all memory
        writeOpAddr(WRITE_OPCODE, WRITE_OPCODE_BITS, addr, WRITE_ADDR_BITS);
        writeData(0);
        standby();

        if (waitForCompletion()) {
            stop();
            getReady();
            // Write successful -- verify random location
            CPPUNIT_ASSERT_EQUAL((uint16_t)0, readAt(addr));
            stop();
            getReady();

            writeOpAddr(ERAL_OPCODE, ERAL_OPCODE_BITS, addr, ERAL_ADDR_BITS);
            standby();

            if (!waitForCompletion()) {
                CPPUNIT_FAIL("EEPROM erase was not completed");
                stop();
                return;
            }

            standby();

            writeOpAddr(EWDS_OPCODE, EWDS_OPCODE_BITS, 0, EWDS_ADDR_BITS);
            stop();

            getReady();
            for ( addr=0; addr < EEPROM93C46::SIZE; addr++ ) {
                CPPUNIT_ASSERT_EQUAL((uint16_t)0xFFFF, readAt(addr));
            }
        }
        else {
            CPPUNIT_FAIL("EEPROM write was not completed");
        }
        stop();
    }
};

/**
 *  shiftOutBits - Shift data bits our to the EEPROM
 *  @hw: pointer to the EEPROM object
 *  @data: data to send to the EEPROM
 *  @count: number of bits to shift out
 *
 *  We need to shift 'count' bits out to the EEPROM.  So, the value in the
 *  "data" parameter will be shifted out to the EEPROM one bit at a time.
 *  In order to do this, "data" must be broken down into bits.
 **/
void EEPROMTest::shiftOutBits(uint16_t data, uint16_t count) {
    uint32_t wires = eeprom->read();
    uint32_t mask;

    mask = 0x01 << (count - 1);
    wires &= ~DO;

    do {
        wires &= ~DI;

        if (data & mask)
            wires |= DI;

        eeprom->write(wires);

        // Raise clock
        eeprom->write(wires |= SK);
        // Lower clock
        eeprom->write(wires &= ~SK);

        mask >>= 1;
    } while (mask);

    wires &= ~DI;
    eeprom->write(wires);
}

/**
 *  shiftInBits - Shift data bits in from the EEPROM
 *  @count: number of bits to shift in
 *
 *  In order to read a register from the EEPROM, we need to shift 'count' bits
 *  in from the EEPROM.  Bits are "shifted in" by raising the clock input to
 *  the EEPROM (setting the SK bit), and then reading the value of the data out
 *  "DO" bit.  During this "shifting in" process the data in "DI" bit should
 *  always be clear.
 **/
uint16_t EEPROMTest::shiftInBits(uint16_t count)
{
    uint32_t wires;
    uint32_t i;
    uint16_t data;

    wires = eeprom->read();

    wires &= ~(DO | DI);
    data = 0;

    for (i = 0; i < count; i++) {
        data <<= 1;
        // Raise clock
        eeprom->write(wires |= SK);

        wires = eeprom->read();

        wires &= ~DI;
        if (wires & DO)
            data |= 1;

        // Lower clock
        eeprom->write(wires &= ~SK);
    }

    return data;
}

/**
 *  getReady - Prepares EEPROM for read/write
 *
 *  Setups the EEPROM for reading and writing.
 **/
void EEPROMTest::getReady()
{
    unsigned wires = eeprom->read();
    /* Clear SK and DI */
    eeprom->write(wires &= ~(DI | SK));
    /* Set CS */
    eeprom->write(wires | CS);
}

/**
 *  standby - Return EEPROM to standby state
 *
 *  Return the EEPROM to a standby state.
 **/
void EEPROMTest::standby()
{
    unsigned wires = eeprom->read();

    eeprom->write(wires &= ~(CS | SK));

    // Raise clock
    eeprom->write(wires |= SK);

    // Select EEPROM
    eeprom->write(wires |= CS);

    // Lower clock
    eeprom->write(wires &= ~SK);
}

/**
 *  stop - Terminate EEPROM command
 *
 *  Terminates the current command by inverting the EEPROM's chip select pin.
 **/
void EEPROMTest::stop()
{
    unsigned wires = eeprom->read();

    eeprom->write(wires &= ~(CS | DI));
    // Raise clock
    eeprom->write(wires |= SK);
    // Lower clock
    eeprom->write(wires &= ~SK);
}

/**
 *  readAt - Read a word at specified address
 *  @addr: address to read
 *
 *  Returns the value of the word specified in 'addr' parameter.
 **/
uint16_t EEPROMTest::readAt(uint16_t addr)
{
    getReady();
    shiftOutBits(READ_OPCODE, READ_OPCODE_BITS);
    shiftOutBits(addr, READ_ADDR_BITS);

    uint16_t value = shiftInBits(DATA_BITS);
    stop();

    return value;
}

/**
 *  writeTo - Write a word to specified address
 *  @addr: address to write to
 *  @value: value to store
 *
 *  Returns false if write did not complete.
 *
 *  Note: Make sure EEPROM is selected and writable before attempting
 *        to write. Use getReady() and stop() to select/deselect
 *        EEPROM.
 **/
bool EEPROMTest::writeTo(uint16_t addr, uint16_t value)
{
    writeOpAddr(WRITE_OPCODE, WRITE_OPCODE_BITS, addr, WRITE_ADDR_BITS);
    writeData(value);
    standby();
    return waitForCompletion();
}


/**
 *  waitForCompletion - Wait until EEPROM clears the busy bit
 *
 *  Returns false if the EEPROM is still busy.
 */
bool EEPROMTest::waitForCompletion() {
    for (int i = 0; i < 200; i++) {
        if (eeprom->read() & DO) {
            standby();
            return true;
        }
        // Wait 50 usec;
    }

    return false;
}

/**
 *  writeOpAddr - Write an opcode and address
 *  @opCode:     operation code
 *  @opCodeBits: number of bits in opCode
 *  @addr:       address to write to
 *  @addrBits:   number of bits in address
 **/
void EEPROMTest::writeOpAddr(int opCode, int opCodeBits, uint16_t addr, int addrBits)
{
    shiftOutBits(opCode, opCodeBits);
    shiftOutBits(addr, addrBits);
}

int main()
{
#ifdef USE_CPPUNIT
    CppUnit::TextUi::TestRunner runner;
    runner.addTest( EEPROMTest::suite() );
    return runner.run() ? 0 : 1;
#else
    EEPROMTest Test;
    return Test.run();
#endif
}

