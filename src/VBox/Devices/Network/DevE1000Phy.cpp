/** $Id: DevE1000Phy.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevE1000Phy - Intel 82540EM Ethernet Controller Internal PHY Emulation.
 *
 * Implemented in accordance with the specification:
 *      PCI/PCI-X Family of Gigabit Ethernet Controllers Software Developer's
 *      Manual 82540EP/EM, 82541xx, 82544GC/EI, 82545GM/EM, 82546GB/EB, and
 *      82547xx
 *
 *      317453-002 Revision 3.5
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

#define LOG_GROUP LOG_GROUP_DEV_E1000

/** @todo Remove me! For now I want asserts to work in release code. */
// #ifndef RT_STRICT
// #define RT_STRICT
#include <iprt/assert.h>
// #undef  RT_STRICT
// #endif

#include <iprt/errcore.h>
#include <VBox/log.h>
#ifdef IN_RING3
# include <VBox/vmm/pdmdev.h>
#endif
#include "DevE1000Phy.h"

/* Little helpers ************************************************************/
#ifdef PHY_UNIT_TEST
# ifdef CPP_UNIT
#  include <stdio.h>
#  define PhyLog(a)               printf a
# else
#  include <iprt/test.h>
#  define PhyLogC99(...)         RTTestIPrintf(RTTESTLVL_ALWAYS, __VA_ARGS__)
#  define PhyLog(a)              PhyLogC99 a
# endif
#else  /* !PHY_UNIT_TEST */
# define PhyLog(a)               Log(a)
#endif /* !PHY_UNIT_TEST */

#define REG(x) pPhy->au16Regs[x##_IDX]


/* Internals */
namespace Phy {
#if defined(LOG_ENABLED) || defined(PHY_UNIT_TEST)
    /** Retrieves state name by id */
    static const char * getStateName(uint16_t u16State);
#endif
    /** Look up register index by address. */
    static int lookupRegister(uint32_t u32Address);
    /** Software-triggered reset. */
    static void softReset(PPHY pPhy, PPDMDEVINS pDevIns);

    /** Read callback. */
    typedef uint16_t FNREAD(PPHY pPhy, uint32_t index, PPDMDEVINS pDevIns);
    /** Write callback. */
    typedef void     FNWRITE(PPHY pPhy, uint32_t index, uint16_t u16Value, PPDMDEVINS pDevIns);

    /** @name Generic handlers
     * @{ */
    static FNREAD  regReadDefault;
    static FNWRITE regWriteDefault;
    static FNREAD  regReadForbidden;
    static FNWRITE regWriteForbidden;
    static FNREAD  regReadUnimplemented;
    static FNWRITE regWriteUnimplemented;
    /** @} */
    /** @name Register-specific handlers
     * @{ */
    static FNWRITE regWritePCTRL;
    static FNREAD  regReadPSTATUS;
    static FNREAD  regReadGSTATUS;
    /** @} */

    /**
    * PHY register map table.
    *
    * Override pfnRead and pfnWrite to implement register-specific behavior.
    */
    static struct RegMap_st
    {
        /** PHY register address. */
        uint32_t    u32Address;
        /** Read callback. */
        FNREAD     *pfnRead;
        /** Write callback. */
        FNWRITE    *pfnWrite;
        /** Abbreviated name. */
        const char *pszAbbrev;
        /** Full name. */
        const char *pszName;
    } s_regMap[NUM_OF_PHY_REGS] =
    {
        /*ra  read callback              write callback              abbrev      full name                     */
        /*--  -------------------------  --------------------------  ----------  ------------------------------*/
        {  0, Phy::regReadDefault      , Phy::regWritePCTRL        , "PCTRL"    , "PHY Control" },
        {  1, Phy::regReadPSTATUS      , Phy::regWriteForbidden    , "PSTATUS"  , "PHY Status" },
        {  2, Phy::regReadDefault      , Phy::regWriteForbidden    , "PID"      , "PHY Identifier" },
        {  3, Phy::regReadDefault      , Phy::regWriteForbidden    , "EPID"     , "Extended PHY Identifier" },
        {  4, Phy::regReadDefault      , Phy::regWriteDefault      , "ANA"      , "Auto-Negotiation Advertisement" },
        {  5, Phy::regReadDefault      , Phy::regWriteForbidden    , "LPA"      , "Link Partner Ability" },
        {  6, Phy::regReadUnimplemented, Phy::regWriteForbidden    , "ANE"      , "Auto-Negotiation Expansion" },
        {  7, Phy::regReadUnimplemented, Phy::regWriteUnimplemented, "NPT"      , "Next Page Transmit" },
        {  8, Phy::regReadUnimplemented, Phy::regWriteForbidden    , "LPN"      , "Link Partner Next Page" },
        {  9, Phy::regReadDefault      , Phy::regWriteUnimplemented, "GCON"     , "1000BASE-T Control" },
        { 10, Phy::regReadGSTATUS      , Phy::regWriteForbidden    , "GSTATUS"  , "1000BASE-T Status" },
        { 15, Phy::regReadUnimplemented, Phy::regWriteForbidden    , "EPSTATUS" , "Extended PHY Status" },
        { 16, Phy::regReadDefault      , Phy::regWriteDefault      , "PSCON"    , "PHY Specific Control" },
        { 17, Phy::regReadDefault      , Phy::regWriteForbidden    , "PSSTAT"   , "PHY Specific Status" },
        { 18, Phy::regReadUnimplemented, Phy::regWriteUnimplemented, "PINTE"    , "PHY Interrupt Enable" },
        { 19, Phy::regReadUnimplemented, Phy::regWriteForbidden    , "PINTS"    , "PHY Interrupt Status" },
        { 20, Phy::regReadUnimplemented, Phy::regWriteUnimplemented, "EPSCON1"  , "Extended PHY Specific Control 1" },
        { 21, Phy::regReadUnimplemented, Phy::regWriteForbidden    , "PREC"     , "PHY Receive Error Counter" },
        { 26, Phy::regReadUnimplemented, Phy::regWriteUnimplemented, "EPSCON2"  , "Extended PHY Specific Control 2" },
        { 29, Phy::regReadForbidden    , Phy::regWriteUnimplemented, "R30PS"    , "MDI Register 30 Page Select" },
        { 30, Phy::regReadUnimplemented, Phy::regWriteUnimplemented, "R30AW"    , "MDI Register 30 Access Window" }
    };
}

/**
 * Default read handler.
 *
 * Fetches register value from the state structure.
 *
 * @returns Register value
 *
 * @param   index       Register index in register array.
 */
static uint16_t Phy::regReadDefault(PPHY pPhy, uint32_t index, PPDMDEVINS pDevIns)
{
    RT_NOREF(pDevIns);
    AssertReturn(index<Phy::NUM_OF_PHY_REGS, 0);
    return pPhy->au16Regs[index];
}

/**
 * Default write handler.
 *
 * Writes the specified register value to the state structure.
 *
 * @param   index       Register index in register array.
 * @param   value       The value to store (ignored).
 */
static void Phy::regWriteDefault(PPHY pPhy, uint32_t index, uint16_t u16Value, PPDMDEVINS pDevIns)
{
    RT_NOREF(pDevIns);
    AssertReturnVoid(index < NUM_OF_PHY_REGS);
    pPhy->au16Regs[index] = u16Value;
}

/**
 * Read handler for write-only registers.
 *
 * Merely reports reads from write-only registers.
 *
 * @returns Register value (always 0)
 *
 * @param   index       Register index in register array.
 */
static uint16_t Phy::regReadForbidden(PPHY pPhy, uint32_t index, PPDMDEVINS pDevIns)
{
    RT_NOREF(pPhy, index, pDevIns);
    PhyLog(("PHY#%d At %02d read attempted from write-only '%s'\n",
            pPhy->iInstance, s_regMap[index].u32Address, s_regMap[index].pszName));
    return 0;
}

/**
 * Write handler for read-only registers.
 *
 * Merely reports writes to read-only registers.
 *
 * @param   index       Register index in register array.
 * @param   value       The value to store (ignored).
 */
static void Phy::regWriteForbidden(PPHY pPhy, uint32_t index, uint16_t u16Value, PPDMDEVINS pDevIns)
{
    RT_NOREF(pPhy, index, u16Value, pDevIns);
    PhyLog(("PHY#%d At %02d write attempted to read-only '%s'\n",
            pPhy->iInstance, s_regMap[index].u32Address, s_regMap[index].pszName));
}

/**
 * Read handler for unimplemented registers.
 *
 * Merely reports reads from unimplemented registers.
 *
 * @returns Register value (always 0)
 *
 * @param   index       Register index in register array.
 */
static uint16_t Phy::regReadUnimplemented(PPHY pPhy, uint32_t index, PPDMDEVINS pDevIns)
{
    RT_NOREF(pPhy, index, pDevIns);
    PhyLog(("PHY#%d At %02d read attempted from unimplemented '%s'\n",
            pPhy->iInstance, s_regMap[index].u32Address, s_regMap[index].pszName));
    return 0;
}

/**
 * Write handler for unimplemented registers.
 *
 * Merely reports writes to unimplemented registers.
 *
 * @param   index       Register index in register array.
 * @param   value       The value to store (ignored).
 */
static void Phy::regWriteUnimplemented(PPHY pPhy, uint32_t index, uint16_t u16Value, PPDMDEVINS pDevIns)
{
    RT_NOREF(pPhy, index, u16Value, pDevIns);
    PhyLog(("PHY#%d At %02d write attempted to unimplemented '%s'\n",
            pPhy->iInstance, s_regMap[index].u32Address, s_regMap[index].pszName));
}


/**
 * Search PHY register table for register with matching address.
 *
 * @returns Index in the register table or -1 if not found.
 *
 * @param   u32Address  Register address.
 */
static int Phy::lookupRegister(uint32_t u32Address)
{
    unsigned int index;

    for (index = 0; index < RT_ELEMENTS(s_regMap); index++)
    {
        if (s_regMap[index].u32Address == u32Address)
        {
            return (int)index;
        }
    }

    return -1;
}

/**
 * Read PHY register.
 *
 * @returns Value of specified PHY register.
 *
 * @param   u32Address  Register address.
 */
uint16_t Phy::readRegister(PPHY pPhy, uint32_t u32Address, PPDMDEVINS pDevIns)
{
    int      index = Phy::lookupRegister(u32Address);
    uint16_t u16   = 0;

    if (index >= 0)
    {
        u16 = s_regMap[index].pfnRead(pPhy, (uint32_t)index, pDevIns);
        PhyLog(("PHY#%d At %02d read  %04X      from %s (%s)\n",
                pPhy->iInstance, s_regMap[index].u32Address, u16,
                s_regMap[index].pszAbbrev, s_regMap[index].pszName));
    }
    else
    {
        PhyLog(("PHY#%d read attempted from non-existing register %08x\n",
                pPhy->iInstance, u32Address));
    }
    return u16;
}

/**
 * Write to PHY register.
 *
 * @param   u32Address  Register address.
 * @param   u16Value    Value to store.
 */
void Phy::writeRegister(PPHY pPhy, uint32_t u32Address, uint16_t u16Value, PPDMDEVINS pDevIns)
{
    int index = Phy::lookupRegister(u32Address);

    if (index >= 0)
    {
        PhyLog(("PHY#%d At %02d write      %04X  to  %s (%s)\n",
                pPhy->iInstance, s_regMap[index].u32Address, u16Value,
                s_regMap[index].pszAbbrev, s_regMap[index].pszName));
        s_regMap[index].pfnWrite(pPhy, (uint32_t)index, u16Value, pDevIns);
    }
    else
    {
        PhyLog(("PHY#%d write attempted to non-existing register %08x\n",
                pPhy->iInstance, u32Address));
    }
}

/**
 * PHY constructor.
 *
 * Stores E1000 instance internally. Triggers PHY hard reset.
 *
 * @param   iNICInstance   Number of network controller instance this PHY is
 *                         attached to.
 * @param   u16EPid        Extended PHY Id.
 */
void Phy::init(PPHY pPhy, int iNICInstance, uint16_t u16EPid)
{
    pPhy->iInstance = iNICInstance;
    /* The PHY identifier composed of bits 3 through 18 of the OUI */
    /* (Organizationally Unique Identifier). OUI is 0x05043.       */
    REG(PID)      = 0x0141;
    /* Extended PHY identifier */
    REG(EPID)     = u16EPid;
    hardReset(pPhy);
}

/**
 * Hardware PHY reset.
 *
 * Sets all PHY registers to their initial values.
 */
void Phy::hardReset(PPHY pPhy)
{
    PhyLog(("PHY#%d Hard reset\n", pPhy->iInstance));
    REG(PCTRL) = PCTRL_SPDSELM | PCTRL_DUPMOD | PCTRL_ANEG;
    /*
     * 100 and 10 FD/HD, Extended Status, MF Preamble Suppression,
     * AUTO NEG AB, EXT CAP
     */
    REG(PSTATUS)  = 0x7949;
    REG(ANA)      = 0x01E1;
    /* No flow control by our link partner, all speeds */
    REG(LPA)      = 0x01E0;
    REG(ANE)      = 0x0000;
    REG(NPT)      = 0x2001;
    REG(LPN)      = 0x0000;
    REG(GCON)     = 0x1E00;
    REG(GSTATUS)  = 0x0000;
    REG(EPSTATUS) = 0x3000;
    REG(PSCON)    = 0x0068;
    REG(PSSTAT)   = 0x0000;
    REG(PINTE)    = 0x0000;
    REG(PINTS)    = 0x0000;
    REG(EPSCON1)  = 0x0D60;
    REG(PREC)     = 0x0000;
    REG(EPSCON2)  = 0x000C;
    REG(R30PS)    = 0x0000;
    REG(R30AW)    = 0x0000;

    pPhy->u16State = MDIO_IDLE;
}

/**
 * Software PHY reset.
 */
static void Phy::softReset(PPHY pPhy, PPDMDEVINS pDevIns)
{
    PhyLog(("PHY#%d Soft reset\n", pPhy->iInstance));

    REG(PCTRL)    = REG(PCTRL) & (PCTRL_SPDSELM | PCTRL_DUPMOD | PCTRL_ANEG | PCTRL_SPDSELL);
    /*
     * 100 and 10 FD/HD, Extended Status, MF Preamble Suppression,
     * AUTO NEG AB, EXT CAP
     */
    REG(PSTATUS)  = 0x7949;
    REG(PSSTAT)  &= 0xe001;
    PhyLog(("PHY#%d PSTATUS=%04x PSSTAT=%04x\n", pPhy->iInstance, REG(PSTATUS), REG(PSSTAT)));

#ifndef PHY_UNIT_TEST
    e1kPhyLinkResetCallback(pDevIns);
#else
    RT_NOREF(pDevIns);
#endif
}

/**
 * Get the current state of the link.
 *
 * @returns true if link is up.
 */
bool Phy::isLinkUp(PPHY pPhy)
{
    return (REG(PSSTAT) & PSSTAT_LINK) != 0;
}

/**
 * Set the current state of the link.
 *
 * @remarks Link Status bit in PHY Status register is latched-low and does
 *          not change the state when the link goes up.
 *
 * @param   fLinkIsUp   New state of the link.
 */
void Phy::setLinkStatus(PPHY pPhy, bool fLinkIsUp)
{
    if (fLinkIsUp)
    {
        REG(PSSTAT)  |= PSSTAT_LINK_ALL;
        REG(PSTATUS) |= PSTATUS_NEGCOMP; /* PSTATUS_LNKSTAT is latched low */
    }
    else
    {
        REG(PSSTAT)  &= ~PSSTAT_LINK_ALL;
        REG(PSTATUS) &= ~(PSTATUS_LNKSTAT | PSTATUS_NEGCOMP);
    }
    PhyLog(("PHY#%d setLinkStatus: PSTATUS=%04x PSSTAT=%04x\n", pPhy->iInstance, REG(PSTATUS), REG(PSSTAT)));
}

#ifdef IN_RING3

/**
 * Save PHY state.
 *
 * @remarks Since PHY is aggregated into E1K it does not currently supports
 *          versioning of its own.
 *
 * @returns VBox status code.
 * @param   pHlp        Device helper table.
 * @param   pSSM        The handle to save the state to.
 * @param   pPhy        The pointer to this instance.
 */
int Phy::saveState(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PPHY pPhy)
{
    pHlp->pfnSSMPutMem(pSSM, pPhy->au16Regs, sizeof(pPhy->au16Regs));
    return VINF_SUCCESS;
}

/**
 * Restore previously saved PHY state.
 *
 * @remarks Since PHY is aggregated into E1K it does not currently supports
 *          versioning of its own.
 *
 * @returns VBox status code.
 * @param   pHlp        Device helper table.
 * @param   pSSM        The handle to save the state to.
 * @param   pPhy        The pointer to this instance.
 */
int Phy::loadState(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PPHY pPhy)
{
    return pHlp->pfnSSMGetMem(pSSM, pPhy->au16Regs, sizeof(pPhy->au16Regs));
}

#endif /* IN_RING3 */

/* Register-specific handlers ************************************************/

/**
 * Write handler for PHY Control register.
 *
 * Handles reset.
 *
 * @param   index       Register index in register array.
 * @param   value       The value to store (ignored).
 */
static void Phy::regWritePCTRL(PPHY pPhy, uint32_t index, uint16_t u16Value, PPDMDEVINS pDevIns)
{
    if (u16Value & PCTRL_RESET)
        softReset(pPhy, pDevIns);
    else
        regWriteDefault(pPhy, index, u16Value, pDevIns);
}

/**
 * Read handler for PHY Status register.
 *
 * Handles Latched-Low Link Status bit.
 *
 * @returns Register value
 *
 * @param   index       Register index in register array.
 */
static uint16_t Phy::regReadPSTATUS(PPHY pPhy, uint32_t index, PPDMDEVINS pDevIns)
{
    RT_NOREF(pPhy, index, pDevIns);

    /* Read latched value */
    uint16_t u16 = REG(PSTATUS);
    if (REG(PSSTAT) & PSSTAT_LINK)
        REG(PSTATUS) |= PSTATUS_LNKSTAT;
    else
        REG(PSTATUS) &= ~PSTATUS_LNKSTAT;
    return u16;
}

/**
 * Read handler for 1000BASE-T Status register.
 *
 * @returns Register value
 *
 * @param   index       Register index in register array.
 */
static uint16_t Phy::regReadGSTATUS(PPHY pPhy, uint32_t index, PPDMDEVINS pDevIns)
{
    RT_NOREF(pPhy, index, pDevIns);

    /*
     * - Link partner is capable of 1000BASE-T half duplex
     * - Link partner is capable of 1000BASE-T full duplex
     * - Remote receiver OK
     * - Local receiver OK
     * - Local PHY config resolved to SLAVE
     */
    return 0x3C00;
}

#if defined(LOG_ENABLED) || defined(PHY_UNIT_TEST)
static const char * Phy::getStateName(uint16_t u16State)
{
    static const char *pcszState[] =
    {
        "MDIO_IDLE",
        "MDIO_ST",
        "MDIO_OP_ADR",
        "MDIO_TA_RD",
        "MDIO_TA_WR",
        "MDIO_READ",
        "MDIO_WRITE"
    };

    return (u16State < RT_ELEMENTS(pcszState)) ? pcszState[u16State] : "<invalid>";
}
#endif

bool Phy::readMDIO(PPHY pPhy)
{
    bool fPin = false;

    switch (pPhy->u16State)
    {
        case MDIO_TA_RD:
            Assert(pPhy->u16Cnt == 1);
            fPin = false;
            pPhy->u16State = MDIO_READ;
            pPhy->u16Cnt   = 16;
            break;
        case MDIO_READ:
            /* Bits are shifted out in MSB to LSB order */
            fPin = (pPhy->u16Acc & 0x8000) != 0;
            pPhy->u16Acc <<= 1;
            if (--pPhy->u16Cnt == 0)
                pPhy->u16State = MDIO_IDLE;
            break;
        default:
            PhyLog(("PHY#%d WARNING! MDIO pin read in %s state\n", pPhy->iInstance, Phy::getStateName(pPhy->u16State)));
            pPhy->u16State = MDIO_IDLE;
    }
    return fPin;
}

/** Set the value of MDIO pin. */
void Phy::writeMDIO(PPHY pPhy, bool fPin, PPDMDEVINS pDevIns)
{
    switch (pPhy->u16State)
    {
        case MDIO_IDLE:
            if (!fPin)
                pPhy->u16State = MDIO_ST;
            break;
        case MDIO_ST:
            if (fPin)
            {
                pPhy->u16State = MDIO_OP_ADR;
                pPhy->u16Cnt   = 12; /* OP + PHYADR + REGADR */
                pPhy->u16Acc   = 0;
            }
            break;
        case MDIO_OP_ADR:
            Assert(pPhy->u16Cnt);
            /* Shift in 'u16Cnt' bits into accumulator */
            pPhy->u16Acc <<= 1;
            if (fPin)
                pPhy->u16Acc |= 1;
            if (--pPhy->u16Cnt == 0)
            {
                /* Got OP(2) + PHYADR(5) + REGADR(5) */
                /* Note: A single PHY is supported, ignore PHYADR */
                switch (pPhy->u16Acc >> 10)
                {
                    case MDIO_READ_OP:
                        pPhy->u16Acc = readRegister(pPhy, pPhy->u16Acc & 0x1F, pDevIns);
                        pPhy->u16State = MDIO_TA_RD;
                        pPhy->u16Cnt = 1;
                        break;
                    case MDIO_WRITE_OP:
                        pPhy->u16RegAdr = pPhy->u16Acc & 0x1F;
                        pPhy->u16State = MDIO_TA_WR;
                        pPhy->u16Cnt = 2;
                        break;
                    default:
                        PhyLog(("PHY#%d ERROR! Invalid MDIO op: %d\n", pPhy->iInstance, pPhy->u16Acc >> 10));
                        pPhy->u16State = MDIO_IDLE;
                        break;
                }
            }
            break;
        case MDIO_TA_WR:
            Assert(pPhy->u16Cnt <= 2);
            Assert(pPhy->u16Cnt > 0);
            if (--pPhy->u16Cnt == 0)
            {
                pPhy->u16State = MDIO_WRITE;
                pPhy->u16Cnt   = 16;
            }
            break;
        case MDIO_WRITE:
            Assert(pPhy->u16Cnt);
            pPhy->u16Acc <<= 1;
            if (fPin)
                pPhy->u16Acc |= 1;
            if (--pPhy->u16Cnt == 0)
            {
                writeRegister(pPhy, pPhy->u16RegAdr, pPhy->u16Acc, pDevIns);
                pPhy->u16State = MDIO_IDLE;
            }
            break;
        default:
            PhyLog(("PHY#%d ERROR! MDIO pin write in %s state\n", pPhy->iInstance, Phy::getStateName(pPhy->u16State)));
            pPhy->u16State = MDIO_IDLE;
            break;
    }
}

