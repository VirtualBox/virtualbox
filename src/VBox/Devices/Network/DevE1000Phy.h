/** $Id: DevE1000Phy.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevE1000Phy - Intel 82540EM Ethernet Controller Internal PHY Emulation, Header.
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

#ifndef VBOX_INCLUDED_SRC_Network_DevE1000Phy_h
#define VBOX_INCLUDED_SRC_Network_DevE1000Phy_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>

#define PHY_EPID_M881000 0xC50
#define PHY_EPID_M881011 0xC24

#define PCTRL_SPDSELM 0x0040
#define PCTRL_DUPMOD  0x0100
#define PCTRL_ANEG    0x1000
#define PCTRL_SPDSELL 0x2000
#define PCTRL_RESET   0x8000

#define PSTATUS_LNKSTAT 0x0004
#define PSTATUS_NEGCOMP 0x0020

/*
 * Speed: 1000 Mb/s
 * Duplex: full
 * Page received
 * Resolved
 * Link up
 * Receive Pause Enable
 */
#define PSSTAT_LINK_ALL 0xBC08
#define PSSTAT_LINK     0x0400

namespace Phy
{
    /**
     * Indices of memory-mapped registers in register table
     */
    enum enmRegIdx
    {
        PCTRL_IDX,
        PSTATUS_IDX,
        PID_IDX,
        EPID_IDX,
        ANA_IDX,
        LPA_IDX,
        ANE_IDX,
        NPT_IDX,
        LPN_IDX,
        GCON_IDX,
        GSTATUS_IDX,
        EPSTATUS_IDX,
        PSCON_IDX,
        PSSTAT_IDX,
        PINTE_IDX,
        PINTS_IDX,
        EPSCON1_IDX,
        PREC_IDX,
        EPSCON2_IDX,
        R30PS_IDX,
        R30AW_IDX,
        NUM_OF_PHY_REGS
    };
    /**
     * Emulation state of PHY.
     */
    struct Phy_st
    {
        /** Network controller instance this PHY is attached to. */
        int      iInstance;
        /** Register storage. */
        uint16_t au16Regs[NUM_OF_PHY_REGS];
        /** Current state of serial MDIO interface. */
        uint16_t u16State;
        /** Current state of serial MDIO interface. */
        uint16_t u16Acc;
        /** Number of bits remaining to be shifted into/out of accumulator. */
        uint16_t u16Cnt;
        /** PHY register offset selected for MDIO operation. */
        uint16_t u16RegAdr;
    };
}

#define MDIO_IDLE   0
#define MDIO_ST     1
#define MDIO_OP_ADR 2
#define MDIO_TA_RD  3
#define MDIO_TA_WR  4
#define MDIO_READ   5
#define MDIO_WRITE  6

#define MDIO_READ_OP  2
#define MDIO_WRITE_OP 1

/* External callback declaration */
void e1kPhyLinkResetCallback(PPDMDEVINS pDevIns);


typedef struct Phy::Phy_st PHY;
typedef PHY *PPHY;

/* Interface *****************************************************************/
namespace Phy
{
    /** Initialize PHY. */
    void     init(PPHY pPhy, int iNICInstance, uint16_t u16EPid);
    /** Read PHY register at specified address. */
    uint16_t readRegister(PPHY pPhy, uint32_t u32Address, PPDMDEVINS pDevIns);
    /** Write to PHY register at specified address. */
    void     writeRegister(PPHY pPhy, uint32_t u32Address, uint16_t u16Value, PPDMDEVINS pDevIns);
    /** Read the value on MDIO pin. */
    bool     readMDIO(PPHY pPhy);
    /** Set the value of MDIO pin. */
    void     writeMDIO(PPHY pPhy, bool fPin, PPDMDEVINS pDevIns);
    /** Hardware reset. */
    void     hardReset(PPHY pPhy);
    /** Query link status. */
    bool     isLinkUp(PPHY pPhy);
    /** Set link status. */
    void     setLinkStatus(PPHY pPhy, bool fLinkIsUp);
    /** Save PHY state. */
    int      saveState(struct PDMDEVHLPR3 const *pHlp, PSSMHANDLE pSSM, PPHY pPhy);
    /** Restore previously saved PHY state. */
    int      loadState(struct PDMDEVHLPR3 const *pHlp, PSSMHANDLE pSSM, PPHY pPhy);
}

#endif /* !VBOX_INCLUDED_SRC_Network_DevE1000Phy_h */

