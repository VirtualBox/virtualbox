/* $Id: GITSAll.cpp 110334 2025-07-21 09:47:48Z ramshankar.venkataraman@oracle.com $ */
/** @file
 * GITS - GIC Interrupt Translation Service (ITS) - All Contexts.
 */

/*
 * Copyright (C) 2025 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_GIC
#include "GICInternal.h"
#include <VBox/vmm/vm.h>        /* pVM->cCpus */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** GITS diagnostic enum description expansion.
 * The below construct ensures typos in the input to this macro are caught
 * during compile time. */
#define GITSDIAG_DESC(a_Name)                       RT_CONCAT(kGitsDiag_, a_Name) < kGitsDiag_End ? RT_STR(a_Name) : "Ignored"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** GITS diagnostics description for members in GITSDIAG. */
static const char *const g_apszGitsDiagDesc[] =
{
    /* No error. */
    GITSDIAG_DESC(None),

    /* Command queue: basic operation errors. */
    GITSDIAG_DESC(CmdQueue_Basic_Unknown_Cmd),
    GITSDIAG_DESC(CmdQueue_Basic_Invalid_PhysAddr),

    /* Command: DISCARD. */
    GITSDIAG_DESC(CmdQueue_Cmd_Discard_CpuId_OutOfRange),
    GITSDIAG_DESC(CmdQueue_Cmd_Discard_DevId_OutOfRange),
    GITSDIAG_DESC(CmdQueue_Cmd_Discard_Dte_Rd_Failed),
    GITSDIAG_DESC(CmdQueue_Cmd_Discard_Dte_Unmapped),
    GITSDIAG_DESC(CmdQueue_Cmd_Discard_EventId_OutOfRange),
    GITSDIAG_DESC(CmdQueue_Cmd_Discard_Ite_Invalid),
    GITSDIAG_DESC(CmdQueue_Cmd_Discard_Ite_Rd_Failed),
    GITSDIAG_DESC(CmdQueue_Cmd_Discard_Ite_Unmapped),

    /* Command: INVALL. */
    GITSDIAG_DESC(CmdQueue_Cmd_Invall_Cte_Unmapped),
    GITSDIAG_DESC(CmdQueue_Cmd_Invall_IcId_OutOfRange),

    /* Command: MAPV. */
    GITSDIAG_DESC(CmdQueue_Cmd_Mapc_IcId_OutOfRange),

    /* Command: MAPD. */
    GITSDIAG_DESC(CmdQueue_Cmd_Mapd_DevId_OutOfRange),
    GITSDIAG_DESC(CmdQueue_Cmd_Mapd_Size_Invalid),

    /* Command: MAPI. */
    GITSDIAG_DESC(CmdQueue_Cmd_Mapi_DevId_OutOfRange),
    GITSDIAG_DESC(CmdQueue_Cmd_Mapi_DevId_Unmapped),
    GITSDIAG_DESC(CmdQueue_Cmd_Mapi_Dte_Rd_Failed),
    GITSDIAG_DESC(CmdQueue_Cmd_Mapi_EventId_OutOfRange),
    GITSDIAG_DESC(CmdQueue_Cmd_Mapi_IcId_OutOfRange),
    GITSDIAG_DESC(CmdQueue_Cmd_Mapi_Ite_Wr_Failed),
    GITSDIAG_DESC(CmdQueue_Cmd_Mapi_PhysLpi_OutOfRange),

    /* Command: MAPTI. */
    GITSDIAG_DESC(CmdQueue_Cmd_Mapti_DevId_OutOfRange),
    GITSDIAG_DESC(CmdQueue_Cmd_Mapti_DevId_Unmapped),
    GITSDIAG_DESC(CmdQueue_Cmd_Mapti_Dte_Rd_Failed),
    GITSDIAG_DESC(CmdQueue_Cmd_Mapti_EventId_OutOfRange),
    GITSDIAG_DESC(CmdQueue_Cmd_Mapti_IcId_OutOfRange),
    GITSDIAG_DESC(CmdQueue_Cmd_Mapti_Ite_Wr_Failed),
    GITSDIAG_DESC(CmdQueue_Cmd_Mapti_PhysLpi_OutOfRange),

    /* LPI Trigger. */
    GITSDIAG_DESC(LpiTrigger_CpuId_OutOfRange),
    GITSDIAG_DESC(LpiTrigger_Dte_Rd_Failed),
    GITSDIAG_DESC(LpiTrigger_Dte_Unmapped),
    GITSDIAG_DESC(LpiTrigger_EventId_OutOfRange),
    GITSDIAG_DESC(LpiTrigger_IcId_OutOfRange),
    GITSDIAG_DESC(LpiTrigger_Ite_Invalid),
    GITSDIAG_DESC(LpiTrigger_Ite_Rd_Failed),
    GITSDIAG_DESC(LpiTrigger_Ite_Unmapped),

    /* kGitsDiag_End */
};
AssertCompile(RT_ELEMENTS(g_apszGitsDiagDesc) == kGitsDiag_End);
#undef GITSDIAG_DESC


#ifndef VBOX_DEVICE_STRUCT_TESTCASE

/**
 * Gets the descriptive name of an control register.
 *
 * @returns The description.
 * @param   offReg  The register offset.
 */
DECLHIDDEN(const char *) gitsGetCtrlRegDescription(uint16_t offReg)
{
    if (GIC_IS_REG_IN_RANGE(offReg, GITS_CTRL_REG_BASER_OFF_FIRST, GITS_CTRL_REG_BASER_RANGE_SIZE))
        return "GITS_BASER<n>";
    switch (offReg)
    {
        case GITS_CTRL_REG_CTLR_OFF:    return "GITS_CTLR";
        case GITS_CTRL_REG_IIDR_OFF:    return "GITS_IIDR";
        case GITS_CTRL_REG_TYPER_OFF:   return "GITS_TYPER";
        case GITS_CTRL_REG_MPAMIDR_OFF: return "GITS_MPAMIDR";
        case GITS_CTRL_REG_PARTIDR_OFF: return "GITS_PARTIDR";
        case GITS_CTRL_REG_MPIDR_OFF:   return "GITS_MPIDR";
        case GITS_CTRL_REG_STATUSR_OFF: return "GITS_STATUSR";
        case GITS_CTRL_REG_UMSIR_OFF:   return "GITS_UMSIR";
        case GITS_CTRL_REG_CBASER_OFF:  return "GITS_CBASER";
        case GITS_CTRL_REG_CWRITER_OFF: return "GITS_CWRITER";
        case GITS_CTRL_REG_CREADR_OFF:  return "GITS_CREADR";
        default:
            return "<UNKNOWN>";
    }
}


/**
 * Gets the descriptive name of an interrupt translation register.
 *
 * @returns The description.
 * @param   offReg  The register offset.
 */
DECLHIDDEN(const char *) gitsGetTranslationRegDescription(uint16_t offReg)
{
    switch (offReg)
    {
        case GITS_TRANSLATION_REG_TRANSLATER:   return "GITS_TRANSLATER";
        default:
            return "<UNKNOWN>";
    }
}


/**
 * Gets the descriptive name for a command.
 *
 * @returns The description.
 * @param   uCmdId      The command ID.
 */
static const char *gitsGetCommandName(uint8_t uCmdId)
{
    switch (uCmdId)
    {
        case GITS_CMD_ID_CLEAR:     return "CLEAR";
        case GITS_CMD_ID_DISCARD:   return "DISCARD";
        case GITS_CMD_ID_INT:       return "INT";
        case GITS_CMD_ID_INV:       return "INV";
        case GITS_CMD_ID_INVALL:    return "INVALL";
        case GITS_CMD_ID_INVDB:     return "INVDB";
        case GITS_CMD_ID_MAPC:      return "MAPC";
        case GITS_CMD_ID_MAPD:      return "MAPD";
        case GITS_CMD_ID_MAPI:      return "MAPI";
        case GITS_CMD_ID_MAPTI:     return "MAPTI";
        case GITS_CMD_ID_MOVALL:    return "MOVALL";
        case GITS_CMD_ID_MOVI:      return "MOVI";
        case GITS_CMD_ID_SYNC:      return "SYNC";
        case GITS_CMD_ID_VINVALL:   return "VINVALL";
        case GITS_CMD_ID_VMAPI:     return "VMAPI";
        case GITS_CMD_ID_VMAPP:     return "VMAPP";
        case GITS_CMD_ID_VMAPTI:    return "VMAPTI";
        case GITS_CMD_ID_VMOVI:     return "VMOVI";
        case GITS_CMD_ID_VMOVP:     return "VMOVP";
        case GITS_CMD_ID_VSGI:      return "VSGI";
        case GITS_CMD_ID_VSYNC:     return "VSYNC";
        default:
            return "<UNKNOWN>";
    }
}


/**
 * Gets description for an error diagnostic.
 *
 * @returns The description.
 * @param   enmDiag     The error diagnostic.
 */
DECL_FORCE_INLINE(const char *) gitsGetDiagDescription(GITSDIAG enmDiag)
{
    if (enmDiag < RT_ELEMENTS(g_apszGitsDiagDesc))
        return g_apszGitsDiagDesc[enmDiag];
    return "<Unknown>";
}


/**
 * Gets the guest physical address from a GITS_BASER register.
 *
 * @returns The guest physical address.
 * @param   uGitsBaseReg    The GITS_BASER register.
 */
static RTGCPHYS gitsGetBaseRegPhysAddr(uint64_t uGitsBaseReg)
{
    /* Mask for physical address bits [47:12]. */
    static uint64_t const s_auPhysAddrLoMasks[] =
    {
        UINT64_C(0x0000fffffffff000), /*  4K bits[47:12] */
        UINT64_C(0x0000ffffffffc000), /* 16K bits[47:14] */
        UINT64_C(0x0000ffffffff0000), /* 64K bits[47:16] */
        UINT64_C(0x0000ffffffff0000)  /* 64K bits[47:16] */
    };

    /* Mask for physical address bits [51:48]. */
    static uint64_t const s_auPhysAddrHiMasks[] =
    {
        UINT64_C(0x0),                /*  4K bits[51:48] = 0 */
        UINT64_C(0x0),                /* 16K bits[51:48] = 0 */
        UINT64_C(0x000000000000f000), /* 64K bits[51:48] = bits[15:12] */
        UINT64_C(0x000000000000f000)  /* 64K bits[51:48] = bits[15:12] */
    };
    AssertCompile(RT_ELEMENTS(s_auPhysAddrLoMasks) == RT_ELEMENTS(s_auPhysAddrHiMasks));

    uint8_t const idxPageSize = RT_BF_GET(uGitsBaseReg, GITS_BF_CTRL_REG_BASER_PAGESIZE);
    Assert(idxPageSize < RT_ELEMENTS(s_auPhysAddrLoMasks));
    RTGCPHYS const GCPhys =  (uGitsBaseReg & s_auPhysAddrLoMasks[idxPageSize])
                          | ((uGitsBaseReg & s_auPhysAddrHiMasks[idxPageSize]) << (48 - 12));
    return GCPhys;
}


/**
 * Records an error while processing a command in the command queue.
 *
 * @param   pDevIns         The device instance.
 * @param   pGitsDev        The GIC ITS state.
 * @param   enmDiag         The error diagnostic.
 * @param   fStallQueue     Whether to stall the command queue or not.
 */
static void gitsCmdQueueSetError(PPDMDEVINS pDevIns, PGITSDEV pGitsDev, GITSDIAG enmDiag, bool fStallQueue)
{
    Log4Func(("enmDiag=%#RX32 (%s) fStallQueue=%RTbool\n", enmDiag, gitsGetDiagDescription(enmDiag), fStallQueue));

    GIC_CRIT_SECT_ENTER(pDevIns);

    /* Record the error and stall the queue. */
    pGitsDev->enmDiag = enmDiag;
    pGitsDev->cCmdQueueErrors++;
    if (fStallQueue)
        pGitsDev->uCmdReadReg |= GITS_BF_CTRL_REG_CREADR_STALLED_MASK;

    GIC_CRIT_SECT_LEAVE(pDevIns);

    /* Since we don't support SEIs, so there should be nothing more to do here. */
    Assert(!RT_BF_GET(pGitsDev->uTypeReg.u, GITS_BF_CTRL_REG_TYPER_SEIS));
}


/**
 * Returns whether the command queue is empty and the read and write offset in the
 * command queue.
 *
 * @returns @c true if queue is empty, @c false otherwise.
 * @param   pGitsDev    The GIC ITS state.
 * @param   poffRead    Where to store the command queue read offset.
 * @param   poffWrite   Where to store the command queue write offset.
 */
DECL_FORCE_INLINE(bool) gitsCmdQueueIsEmptyEx(PCGITSDEV pGitsDev, uint32_t *poffRead, uint32_t *poffWrite)
{
    *poffRead  = pGitsDev->uCmdReadReg  & GITS_BF_CTRL_REG_CREADR_OFFSET_MASK;
    *poffWrite = pGitsDev->uCmdWriteReg & GITS_BF_CTRL_REG_CWRITER_OFFSET_MASK;
    return *poffRead == *poffWrite;
}


/**
 * Returns whether the command queue is empty.
 *
 * @returns @c true if queue is empty, @c false otherwise.
 * @param   pGitsDev    The GIC ITS state.
 */
DECL_FORCE_INLINE(bool) gitsCmdQueueIsEmpty(PCGITSDEV pGitsDev)
{
    uint32_t offRead;
    uint32_t offWrite;
    return gitsCmdQueueIsEmptyEx(pGitsDev, &offRead, &offWrite);
}


/**
 * Returns whether the command queue can process requests.
 *
 * @returns @c true if requests can be processed, @c false otherwise.
 * @param   pGitsDev    The GIC ITS state.
 */
DECL_FORCE_INLINE(bool) gitsCmdQueueCanProcessRequests(PCGITSDEV pGitsDev)
{
    if (    (pGitsDev->uTypeReg.u    & GITS_BF_CTRL_REG_CTLR_ENABLED_MASK)
        &&  (pGitsDev->uCmdBaseReg.u & GITS_BF_CTRL_REG_CBASER_VALID_MASK)
        && !(pGitsDev->uCmdReadReg   & GITS_BF_CTRL_REG_CREADR_STALLED_MASK))
        return true;
    return false;
}


/**
 * Wakes up the command queue thread if there are commands to be processed.
 *
 * @param   pDevIns     The device instance.
 * @param   pGitsDev    The GIC ITS state.
 */
static void gitsCmdQueueThreadWakeUpIfNeeded(PPDMDEVINS pDevIns, PGITSDEV pGitsDev)
{
    Log4Func(("\n"));
    Assert(GIC_CRIT_SECT_IS_OWNER(pDevIns));
    if (    gitsCmdQueueCanProcessRequests(pGitsDev)
        && !gitsCmdQueueIsEmpty(pGitsDev))
    {
        Log4Func(("Waking up the command queue thread\n"));
        int const rc = PDMDevHlpSUPSemEventSignal(pDevIns, pGitsDev->hEvtCmdQueue);
        AssertRC(rc);
    }
}


/**
 * Reads a register in the control registers MMIO region.
 *
 * @returns The register value.
 * @param   pGitsDev    The GIC ITS state.
 * @param   offReg      The offset of the register being written.
 * @param   cb          Number of bytes written.
 */
DECLHIDDEN(uint64_t) gitsMmioReadCtrl(PCGITSDEV pGitsDev, uint16_t offReg, unsigned cb)
{
    Assert(cb == 4 || cb == 8);
    Assert(!(offReg & 3));
    RT_NOREF(cb);

    /*
     * GITS_BASER<n>.
     */
    uint64_t uReg;
    if (GIC_IS_REG_IN_RANGE(offReg, GITS_CTRL_REG_BASER_OFF_FIRST, GITS_CTRL_REG_BASER_RANGE_SIZE))
    {
        uint16_t const cbReg  = sizeof(uint64_t);
        uint16_t const idxReg = (offReg - GITS_CTRL_REG_BASER_OFF_FIRST) / cbReg;
        uReg = pGitsDev->aItsTableRegs[idxReg].u >> ((offReg & 7) << 3 /* to bits */);
        return uReg;
    }

    switch (offReg)
    {
        case GITS_CTRL_REG_CTLR_OFF:
            Assert(cb == 4);
            uReg = pGitsDev->uCtrlReg;
            break;

        case GITS_CTRL_REG_PIDR2_OFF:
            Assert(cb == 4);
            Assert(pGitsDev->uArchRev <= GITS_CTRL_REG_PIDR2_ARCHREV_GICV4);
            uReg = RT_BF_MAKE(GITS_BF_CTRL_REG_PIDR2_DES_1,   GIC_JEDEC_JEP10_DES_1(GIC_JEDEC_JEP106_IDENTIFICATION_CODE))
                 | RT_BF_MAKE(GITS_BF_CTRL_REG_PIDR2_JEDEC,   1)
                 | RT_BF_MAKE(GITS_BF_CTRL_REG_PIDR2_ARCHREV, pGitsDev->uArchRev);
            break;

        case GITS_CTRL_REG_IIDR_OFF:
            Assert(cb == 4);
            uReg = RT_BF_MAKE(GITS_BF_CTRL_REG_IIDR_IMPL_ID_CODE,   GIC_JEDEC_JEP106_IDENTIFICATION_CODE)
                 | RT_BF_MAKE(GITS_BF_CTRL_REG_IIDR_IMPL_CONT_CODE, GIC_JEDEC_JEP106_CONTINUATION_CODE);
            break;

        case GITS_CTRL_REG_TYPER_OFF:
        case GITS_CTRL_REG_TYPER_OFF + 4:
            uReg = pGitsDev->uTypeReg.u >> ((offReg & 7) << 3 /* to bits */);
            break;

        case GITS_CTRL_REG_CBASER_OFF:
            uReg = pGitsDev->uCmdBaseReg.u;
            break;

        case GITS_CTRL_REG_CBASER_OFF + 4:
            Assert(cb == 4);
            uReg = pGitsDev->uCmdBaseReg.s.Hi;
            break;

        case GITS_CTRL_REG_CREADR_OFF:
            uReg = pGitsDev->uCmdReadReg;
            break;

        case GITS_CTRL_REG_CREADR_OFF + 4:
            uReg = 0;   /* Upper 32-bits are reserved, MBZ. */
            break;

        case GITS_CTRL_REG_CWRITER_OFF:
            uReg = pGitsDev->uCmdWriteReg;
            break;

        case GITS_CTRL_REG_CWRITER_OFF + 4:
            uReg = 0;   /* Upper 32-bits are reserved, MBZ. */
            break;

        default:
            AssertReleaseMsgFailed(("offReg=%#x (%s)\n", offReg, gitsGetCtrlRegDescription(offReg)));
            uReg = 0;
            break;
    }

    Log4Func(("offReg=%#RX16 (%s) uReg=%#RX64 [%u-bit]\n", offReg, gitsGetCtrlRegDescription(offReg), uReg, cb << 3));
    return uReg;
}


/**
 * Writes a register in the control registers MMIO region.
 *
 * @param   pDevIns     The device instance.
 * @param   pGitsDev    The GIC ITS state.
 * @param   offReg      The offset of the register being written.
 * @param   uValue      The register value.
 * @param   cb          Number of bytes written.
 */
DECLHIDDEN(void) gitsMmioWriteCtrl(PPDMDEVINS pDevIns, PGITSDEV pGitsDev, uint16_t offReg, uint64_t uValue, unsigned cb)
{
    Assert(cb == 8 || cb == 4);
    Assert(!(offReg & 3));
    Log4Func(("offReg=%u uValue=%#RX64 cb=%u\n", offReg, uValue, cb));

    /*
     * GITS_BASER<n>.
     */
    if (GIC_IS_REG_IN_RANGE(offReg, GITS_CTRL_REG_BASER_OFF_FIRST, GITS_CTRL_REG_BASER_RANGE_SIZE))
    {
        uint16_t const cbReg   = sizeof(uint64_t);
        uint16_t const idxReg  = (offReg - GITS_CTRL_REG_BASER_OFF_FIRST) / cbReg;
        uint64_t const fRwMask = GITS_CTRL_REG_BASER_RW_MASK;
        if (!(offReg & 7))
        {
            if (cb == 8)
            {
                uint64_t const uOldReg = pGitsDev->aItsTableRegs[idxReg].u;
                GIC_SET_REG_U64_FULL(pGitsDev->aItsTableRegs[idxReg].u, uValue, fRwMask);
                uint64_t const uNewReg = pGitsDev->aItsTableRegs[idxReg].u;
                if (    (uOldReg ^ uNewReg) & GITS_BF_CTRL_REG_BASER_VALID_MASK
                    && !(uNewReg & GITS_BF_CTRL_REG_BASER_VALID_MASK))
                    gitsLpiCacheInvalidateAll(pGitsDev);
            }
            else
                GIC_SET_REG_U64_LO(pGitsDev->aItsTableRegs[idxReg].s.Lo, uValue, fRwMask);
        }
        else
        {
            Assert(cb == 4);
            uint64_t const uOldReg = pGitsDev->aItsTableRegs[idxReg].u;
            GIC_SET_REG_U64_HI(pGitsDev->aItsTableRegs[idxReg].s.Hi, uValue, fRwMask);
            uint64_t const uNewReg = pGitsDev->aItsTableRegs[idxReg].u;
            if (    (uOldReg ^ uNewReg) & GITS_BF_CTRL_REG_BASER_VALID_MASK
                && !(uNewReg & GITS_BF_CTRL_REG_BASER_VALID_MASK))
                gitsLpiCacheInvalidateAll(pGitsDev);
        }
        return;
    }

    switch (offReg)
    {
        case GITS_CTRL_REG_CTLR_OFF:
            Assert(cb == 4);
            Assert(!(pGitsDev->uTypeReg.u & GITS_BF_CTRL_REG_TYPER_UMSI_IRQ_MASK));
            GIC_SET_REG_U32(pGitsDev->uCtrlReg, uValue, GITS_BF_CTRL_REG_CTLR_RW_MASK);
            if (pGitsDev->uCtrlReg & GITS_BF_CTRL_REG_CTLR_ENABLED_MASK)
                gitsCmdQueueThreadWakeUpIfNeeded(pDevIns, pGitsDev);
            break;

        case GITS_CTRL_REG_CBASER_OFF:
            if (cb == 8)
                GIC_SET_REG_U64_FULL(pGitsDev->uCmdBaseReg.u, uValue, GITS_CTRL_REG_CBASER_RW_MASK);
            else
                GIC_SET_REG_U64_LO(pGitsDev->uCmdBaseReg.s.Lo, uValue, GITS_CTRL_REG_CBASER_RW_MASK);
            gitsCmdQueueThreadWakeUpIfNeeded(pDevIns, pGitsDev);
            break;

        case GITS_CTRL_REG_CBASER_OFF + 4:
            Assert(cb == 4);
            GIC_SET_REG_U64_HI(pGitsDev->uCmdBaseReg.s.Hi, uValue, GITS_CTRL_REG_CBASER_RW_MASK);
            gitsCmdQueueThreadWakeUpIfNeeded(pDevIns, pGitsDev);
            break;

        case GITS_CTRL_REG_CWRITER_OFF:
            GIC_SET_REG_U32(pGitsDev->uCmdWriteReg, uValue, GITS_CTRL_REG_CWRITER_RW_MASK);
            gitsCmdQueueThreadWakeUpIfNeeded(pDevIns, pGitsDev);
            break;

        case GITS_CTRL_REG_CWRITER_OFF + 4:
            /* Upper 32-bits are all reserved, ignore write. Fedora 40 arm64 guests (and probably others) do this. */
            Assert(uValue == 0);
            gitsCmdQueueThreadWakeUpIfNeeded(pDevIns, pGitsDev);
            break;

        default:
            AssertReleaseMsgFailed(("offReg=%#x (%s) uValue=%#RX32\n", offReg, gitsGetCtrlRegDescription(offReg), uValue));
            break;
    }

    Log4Func(("offReg=%#RX16 (%s) uValue=%#RX32 [%u-bit]\n", offReg, gitsGetCtrlRegDescription(offReg), uValue, cb << 3));
}


/**
 * Reads a register in the interrupt translation MMIO region.
 *
 * @returns The register value.
 * @param   pGitsDev    The GIC ITS state.
 * @param   offReg      The offset of the register being written.
 * @param   cb          Number of bytes read.
 */
DECLHIDDEN(uint64_t) gitsMmioReadTranslate(PCGITSDEV pGitsDev, uint16_t offReg, unsigned cb)
{
    Assert(cb == 8 || cb == 4);
    Assert(!(offReg & 3));
    RT_NOREF(pGitsDev, cb);

    uint64_t uReg = 0;
    AssertReleaseMsgFailed(("offReg=%#x (%s) uReg=%#RX64 [%u-bit]\n", offReg, gitsGetTranslationRegDescription(offReg), uReg, cb << 3));
    return uReg;
}


/**
 * Writes an register in the interrupt translation MMIO region.
 *
 * @param   pGitsDev    The GIC ITS state.
 * @param   offReg      The offset of the register being written.
 * @param   uValue      The register value.
 * @param   cb          Number of bytes written.
 */
DECLHIDDEN(void) gitsMmioWriteTranslate(PGITSDEV pGitsDev, uint16_t offReg, uint64_t uValue, unsigned cb)
{
    RT_NOREF(pGitsDev);
    Assert(cb == 8 || cb == 4);
    Assert(!(offReg & 3));
    Log4Func(("offReg=%u uValue=%#RX64 cb=%u\n", offReg, uValue, cb));
    /** @todo Call gitsSetLpi for GITS_TRANSLATER register offset write. */
    AssertReleaseMsgFailed(("offReg=%#x uValue=%#RX64 [%u-bit]\n", offReg, uValue, cb << 3));
}


/**
 * Looks up an device ID and event ID combination from the LPI map cache.
 *
 * @returns @c true if a mapping was found, @c false otherwise.
 * @param   pGitsDev        The  GIC ITS state.
 * @param   uDevId          The device ID.
 * @param   uEventId        The event ID.
 * @param   pLpiMapEntry    Where to store the LPI map entry. This is initialized in
 *                          both failure and success.
 */
static bool gitstLpiCacheLookup(PGITSDEV pGitsDev, uint16_t uDevId, uint16_t uEventId, PGITSLPIMAPENTRY pLpiMapEntry)
{
    PCGITSLPIMAP pLpiMap = &pGitsDev->LpiMap;
    AssertCompile(RT_ELEMENTS(pLpiMap->uDevIdEventId) == GITS_LPI_MAP_CACHE_COUNT);
    AssertCompile(RT_ELEMENTS(pLpiMap->uIcId)  == GITS_LPI_MAP_CACHE_COUNT);
    AssertCompile(RT_ELEMENTS(pLpiMap->uIntId) == GITS_LPI_MAP_CACHE_COUNT);
    AssertCompile(RT_ELEMENTS(pLpiMap->idCpu) == GITS_LPI_MAP_CACHE_COUNT);

    /*
     * Lookup the entry in the cache that matches the device ID and event ID combo.
     * If multiple entries with the same device ID and event IDs map to the same
     * physical INTID, the behavior is implementation defined. We always return the
     * last added entry.
     *
     * See ARM GIC spec. 5.2.10 "Restrictions for INTID mapping rules".
     */
    uint32_t const uDevIdEventId = RT_MAKE_U32(uDevId, uEventId);
    uint8_t const  cEntries      = pGitsDev->cLpiMap;
    Assert(cEntries < RT_ELEMENTS(pLpiMap->uDevIdEventId));
    for (uint8_t i = 0; i < cEntries; i++)
    {
        if (pLpiMap->uDevIdEventId[i].u == uDevIdEventId)
        {
            pLpiMapEntry->uDevIdEventId = pLpiMap->uDevIdEventId[i];
            pLpiMapEntry->uIntId        = pLpiMap->uIntId[i];
            pLpiMapEntry->uIcId         = pLpiMap->uIcId[i];
            pLpiMapEntry->idCpu         = pLpiMap->idCpu[i];
            STAM_COUNTER_INC(&pGitsDev->StatLpiCacheHit);
            return true;
        }
    }
    pLpiMapEntry->uDevIdEventId.u = 0;
    pLpiMapEntry->uIntId          = 0;
    pLpiMapEntry->uIcId           = 0;
    pLpiMapEntry->idCpu           = NIL_VMCPUID;
    STAM_COUNTER_INC(&pGitsDev->StatLpiCacheMiss);
    return false;
}


/**
 * Adds an entry to the LPI map cache.
 *
 * @param   pGitsDev        The GIC ITS state.
 * @param   pLpiMapEntry    The LPI map entry to add.
 * @remarks The caller must ensure the fields in the entry are valid.
 */
static void gitsLpiCacheAdd(PGITSDEV pGitsDev, PGITSLPIMAPENTRY pLpiMapEntry)
{
    PGITSLPIMAP     pLpiMap  = &pGitsDev->LpiMap;
    uint32_t const cCapacity = RT_ELEMENTS(pLpiMap->uDevIdEventId);

    uint8_t const idxMap = pGitsDev->idxLpiMap;
    Assert(idxMap < cCapacity);
    pLpiMap->uDevIdEventId[idxMap].u = pLpiMapEntry->uDevIdEventId.u;
    pLpiMap->uIcId[idxMap]           = pLpiMapEntry->uIcId;
    pLpiMap->uIntId[idxMap]          = pLpiMapEntry->uIntId;
    pLpiMap->idCpu[idxMap]           = pLpiMapEntry->idCpu;

    pGitsDev->idxLpiMap = (pGitsDev->idxLpiMap + 1) % cCapacity;
    if (pGitsDev->cLpiMap < cCapacity)
        ++pGitsDev->cLpiMap;
    STAM_COUNTER_INC(&pGitsDev->StatLpiCacheAdd);
}


/**
 * Invalidates an entry that matches the device ID/event ID combination.
 *
 * @param   pGitsDev    The GIC ITS state.
 * @param   uDevId      The device ID.
 * @param   uEventId    The event ID.
 * @remarks This does not invalidate multiple entries that may match the device
 *          ID/event ID combination because our lookup always only returns the
 *          first matching entry.
 */
static void gitsLpiCacheInvalidateOne(PGITSDEV pGitsDev, uint16_t uDevId, uint16_t uEventId)
{
    uint64_t const uDevIdEventId = RT_MAKE_U32(uDevId, uEventId);
    uint8_t const  cEntries      = pGitsDev->cLpiMap;
    PGITSLPIMAP    pLpiMap       = &pGitsDev->LpiMap;
    for (uint8_t i = 0; i < cEntries; i++)
    {
        if (pLpiMap->uDevIdEventId[i].u == uDevIdEventId)
        {
            uint8_t const idxLast = pGitsDev->idxLpiMap;
            if (idxLast == cEntries)
            {
                /* The cache is not full, swap the last entry with this one. */
                pLpiMap->uDevIdEventId[i].u = pLpiMap->uDevIdEventId[idxLast].u;
                pLpiMap->uIntId[i]          = pLpiMap->uIntId[idxLast];
                pLpiMap->uIcId[i]           = pLpiMap->uIcId[idxLast];
                pLpiMap->idCpu[i]           = pLpiMap->idCpu[idxLast];
                --pGitsDev->idxLpiMap;
            }
            else
            {
                /* The cache is full, clear the current entry and mark it as the next-free index. */
                Assert(idxLast < cEntries);
                pLpiMap->uDevIdEventId[i].u = 0;
                pLpiMap->uIntId[i]          = 0;
                pLpiMap->uIcId[i]           = 0;
                pLpiMap->idCpu[i]           = NIL_VMCPUID;
                pGitsDev->idxLpiMap = i;
            }
            --pGitsDev->cLpiMap;
            break;
        }
    }
    STAM_COUNTER_INC(&pGitsDev->StatLpiCacheInvOne);
}


/**
 * Invalidates all entries in the LPI map cache.
 *
 * @param   pGitsDev        The GIC ITS state.
 */
DECLHIDDEN(void) gitsLpiCacheInvalidateAll(PGITSDEV pGitsDev)
{
    PGITSLPIMAP pLpiMap = &pGitsDev->LpiMap;
    for (uint8_t i = 0; i < RT_ELEMENTS(pGitsDev->LpiMap.uDevIdEventId); i++)
    {
        pLpiMap->uDevIdEventId[i].u = 0;
        pLpiMap->uIntId[i]          = 0;
        pLpiMap->uIcId[i]           = 0;
        pLpiMap->idCpu[i]           = NIL_VMCPUID;
    }
    pGitsDev->idxLpiMap = 0;
    pGitsDev->cLpiMap   = 0;
    STAM_COUNTER_INC(&pGitsDev->StatLpiCacheInvAll);
}


/**
 * Initializes the GIC ITS state.
 *
 * @param   pGitsDev    The GIC ITS state.
 * @remarks This is also called during VM reset, so do NOT remove values that are
 *          cleared to zero!
 */
DECLHIDDEN(void) gitsInit(PGITSDEV pGitsDev)
{
    Log4Func(("\n"));

    /* GITS_CTLR.*/
    pGitsDev->uCtrlReg = RT_BF_MAKE(GITS_BF_CTRL_REG_CTLR_QUIESCENT, 1);

    /* GITS_TYPER. */
    pGitsDev->uTypeReg.u = RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_PHYSICAL,  1)     /* Physical LPIs supported. */
                       /*| RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_VIRTUAL,   0) */  /* Virtual LPIs not supported. */
                       /*| RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_CCT,       0) */  /* Collections in memory not supported. */
                         | RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_ITT_ENTRY_SIZE, sizeof(GITSITE) - 1) /* ITE size in bytes minus 1. */
                         | RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_ID_BITS,   GITS_EVENT_ID_BITS - 1)   /* Event ID bits minus 1. */
                         | RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_DEV_BITS,  GITS_DEV_ID_BITS - 1)     /* Device ID bits minus 1. */
                       /*| RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_SEIS,      0) */  /* Locally generated errors not recommended. */
                       /*| RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_PTA,       0) */  /* Target is VCPU ID not address. */
                         | RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_HCC,       255)   /* Collection count. */
                       /*| RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_CID_BITS,  0) */  /* Collections in memory not supported. */
                       /*| RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_CIL,       0) */  /* Collections in memory not supported. */
                       /*| RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_VMOVP,     0) */  /* VMOVP not supported. */
                       /*| RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_MPAM,      0) */  /* MPAM no supported. */
                       /*| RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_VSGI,      0) */  /* VSGI not supported. */
                       /*| RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_VMAPP,     0) */  /* VMAPP not supported. */
                       /*| RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_SVPET,     0) */  /* SVPET not supported. */
                       /*| RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_NID,       0) */  /* NID (doorbell) not supported. */
                       /*| RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_UMSI,      0) */  /** @todo Reporting receipt of unmapped MSIs. */
                       /*| RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_UMSI_IRQ,  0) */  /** @todo Generating interrupt on unmapped MSI. */
                         | RT_BF_MAKE(GITS_BF_CTRL_REG_TYPER_INV,       1);    /* ITS caches invalidated when clearing
                                                                                  GITS_CTLR.Enabled and GITS_BASER<n>.Valid. */
    Assert(RT_ELEMENTS(pGitsDev->aCtes) == RT_BF_GET(pGitsDev->uTypeReg.u, GITS_BF_CTRL_REG_TYPER_HCC));
    Assert(RT_SIZEOFMEMB(GITSLPIMAPENTRY, uDevIdEventId) * 8 >= RT_BF_GET(pGitsDev->uTypeReg.u, GITS_BF_CTRL_REG_TYPER_ID_BITS) + 1
                                                              + RT_BF_GET(pGitsDev->uTypeReg.u, GITS_BF_CTRL_REG_TYPER_DEV_BITS) + 1);

    /* GITS_BASER<n>. */
    RT_ZERO(pGitsDev->aItsTableRegs);
    pGitsDev->aItsTableRegs[0].u = RT_BF_MAKE(GITS_BF_CTRL_REG_BASER_ENTRY_SIZE, sizeof(GITSDTE) - 1)
                                 | RT_BF_MAKE(GITS_BF_CTRL_REG_BASER_TYPE,       GITS_BASER_TYPE_DEVICES);

    /* GITS_CBASER, GITS_CREADR, GITS_CWRITER. */
    pGitsDev->uCmdBaseReg.u = 0;
    pGitsDev->uCmdReadReg   = 0;
    pGitsDev->uCmdWriteReg  = 0;

    /* Collection Table. */
    for (unsigned i = 0; i < RT_ELEMENTS(pGitsDev->aCtes); i++)
        pGitsDev->aCtes[i] = NIL_VMCPUID;

    /* Misc. stuff. */
    pGitsDev->cCmdQueueErrors = 0;

    /* LPI cache. */
    gitsLpiCacheInvalidateAll(pGitsDev);
}



/**
 * Gets the guest physical address of the device-table entry given its device ID.
 * This handles the device table being a flat (direct) or a two-level (indirect)
 * structure.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pGitsDev        The GIC ITS state.
 * @param   uDevId          The device ID.
 * @param   pGCPhysDte      Where to store the guest physical address of the
 *                          device-table entry.
 */
static int gitsR3DteGetAddr(PPDMDEVINS pDevIns, PGITSDEV pGitsDev, uint16_t uDevId, PRTGCPHYS pGCPhysDte)
{
    uint64_t const uBaseReg       = pGitsDev->aItsTableRegs[0].u;
    bool const     fIndirect      = RT_BF_GET(uBaseReg, GITS_BF_CTRL_REG_BASER_INDIRECT);
    RTGCPHYS       GCPhysDevTable = gitsGetBaseRegPhysAddr(uBaseReg);
    if (!fIndirect)
    {
        *pGCPhysDte = GCPhysDevTable + uDevId * sizeof(GITSDTE);
        return VINF_SUCCESS;
    }

    RTGCPHYS offDte = 0;
    static uint32_t const s_acbPageSizes[]    = { _4K, _16K, _64K, _64K };
    static uint64_t const s_auPhysAddrMasks[] =
    {
        UINT64_C(0x000ffffffffff000), /*  4K bits[51:12] */
        UINT64_C(0x000fffffffffc000), /* 16K bits[51:14] */
        UINT64_C(0x000fffffffff0000), /* 64K bits[51:16] */
        UINT64_C(0x000fffffffff0000)  /* 64K bits[51:16] */
    };

    uint8_t const  idxPageSize = RT_BF_GET(uBaseReg, GITS_BF_CTRL_REG_BASER_PAGESIZE);
    uint32_t const cbPage      = s_acbPageSizes[idxPageSize];

    /* Read the the level 1 table device-table entry. */
    uint32_t const cLevel1Entries = cbPage / GITS_ITE_INDIRECT_LVL1_SIZE;
    RTGCPHYS const offLevel1Dte   = (uDevId % cLevel1Entries) * GITS_ITE_INDIRECT_LVL1_SIZE;
    uint64_t       uLevel1Dte     = 0;
    int rc = PDMDevHlpPhysReadMeta(pDevIns, GCPhysDevTable + offLevel1Dte, &uLevel1Dte, sizeof(uLevel1Dte));
    if (RT_SUCCESS(rc))
    {
        /* Check if the entry is valid. */
        bool const fValid = RT_BF_GET(uLevel1Dte, GITS_BF_ITE_INDIRECT_LVL1_4K_VALID);
        if (fValid)
        {
            /* Compute the physical address of the device-table entry from the level 1 entry. */
            uint32_t const cEntries = cbPage / sizeof(GITSDTE);
            GCPhysDevTable          = uLevel1Dte & s_auPhysAddrMasks[idxPageSize];
            offDte                  = (uDevId % cEntries) * sizeof(GITSDTE);

            *pGCPhysDte = GCPhysDevTable + offDte;
            return VINF_SUCCESS;
        }
        rc = VERR_NOT_FOUND;
    }

    /* Something went wrong (usually shouldn't happen but could be faulty/misbehaving guest). */
    *pGCPhysDte = NIL_RTGCPHYS;
    return rc;
}


/**
 * Reads a device-table entry (DTE) from guest memory.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pGitsDev    The GIC ITS state.
 * @param   uDevId      The device ID.
 * @param   puDte       Where to store the device-table entry.
 */
static int gitsDteRead(PPDMDEVINS pDevIns, PGITSDEV pGitsDev, uint16_t uDevId, GITSDTE *puDte)
{
    RTGCPHYS GCPhysDte;
    int const rc = gitsR3DteGetAddr(pDevIns, pGitsDev, uDevId, &GCPhysDte);
    if (RT_SUCCESS(rc))
        return PDMDevHlpPhysReadMeta(pDevIns, GCPhysDte, (void *)puDte, sizeof(*puDte));
    AssertMsgFailed(("Failed to get device-table entry address for device ID %#RX16 rc=%Rrc\n", uDevId, rc));
    return rc;
}


/**
 * Writes a device-table entry (DTE) to guest memory.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pGitsDev    The GIC ITS state.
 * @param   uDevId      The device ID.
 * @param   uDte        The device-table entry.
 */
static int gitsDteWrite(PPDMDEVINS pDevIns, PGITSDEV pGitsDev, uint16_t uDevId, GITSDTE uDte)
{
    RTGCPHYS GCPhysDte;
    int const rc = gitsR3DteGetAddr(pDevIns, pGitsDev, uDevId, &GCPhysDte);
    if (RT_SUCCESS(rc))
        return PDMDevHlpPhysWriteMeta(pDevIns, GCPhysDte, (const void *)&uDte, sizeof(uDte));
    AssertMsgFailed(("Failed to get device-table entry address for device ID %#RX16 rc=%Rrc\n", uDevId, rc));
    return rc;
}


/**
 * Reads an interrupt-translation table entry (ITE) from guest memory.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   uDte        The device-table entry.
 * @param   uEventId    The event ID.
 * @param   puIte       Where to store the interrupt-translation table entry.
 */
static int gitsIteRead(PPDMDEVINS pDevIns, GITSDTE uDte, uint16_t uEventId, GITSITE *puIte)
{
    RTGCPHYS const GCPhysIntrTable = uDte & GITS_BF_DTE_ITT_ADDR_MASK;
    RTGCPHYS const GCPhysIte       = GCPhysIntrTable + uEventId * sizeof(GITSITE);
    return PDMDevHlpPhysReadMeta(pDevIns, GCPhysIte, (void *)puIte, sizeof(*puIte));
}


/**
 * Writes an interrupt-translation table entry (ITE) to guest memory.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   uDte        The device-table entry.
 * @param   uEventId    The event ID.
 * @param   uIte        The interrupt-translation table entry.
 */
static int gitsIteWrite(PPDMDEVINS pDevIns, GITSDTE uDte, uint16_t uEventId, GITSITE uIte)
{
    RTGCPHYS const GCPhysIntrTable = uDte & GITS_BF_DTE_ITT_ADDR_MASK;
    RTGCPHYS const GCPhysIte       = GCPhysIntrTable + uEventId * sizeof(GITSITE);
    return PDMDevHlpPhysWriteMeta(pDevIns, GCPhysIte, (const void *)&uIte, sizeof(uIte));
}


#ifdef IN_RING3
/**
 * Dumps GIC ITS information.
 *
 * @param   pGitsDev    The GIC ITS state.
 * @param   pHlp        The info helpers.
 */
DECL_HIDDEN_CALLBACK(void) gitsR3DbgInfo(PCGITSDEV pGitsDev, PCDBGFINFOHLP pHlp)
{
    pHlp->pfnPrintf(pHlp, "GIC ITS:\n");

    /* Basic info, GITS_CTLR and GITS_TYPER. */
    {
        uint32_t const uCtrlReg = pGitsDev->uCtrlReg;
        GITSDIAG const enmDiag  = pGitsDev->enmDiag;
        pHlp->pfnPrintf(pHlp, "  uArchRev           = %u\n",          pGitsDev->uArchRev);
        pHlp->pfnPrintf(pHlp, "  Cmd queue errors   = %RU64\n",       pGitsDev->cCmdQueueErrors);
        pHlp->pfnPrintf(pHlp, "  Last error         = %#RX32 (%s)\n", enmDiag, gitsGetDiagDescription(enmDiag));
        pHlp->pfnPrintf(pHlp, "  GITS_CTLR          = %#RX32\n",      uCtrlReg);
        pHlp->pfnPrintf(pHlp, "    Enabled            = %RTbool\n",   RT_BF_GET(uCtrlReg, GITS_BF_CTRL_REG_CTLR_ENABLED));
        pHlp->pfnPrintf(pHlp, "    UMSI IRQ           = %RTbool\n",   RT_BF_GET(uCtrlReg, GITS_BF_CTRL_REG_CTLR_UMSI_IRQ));
        pHlp->pfnPrintf(pHlp, "    Quiescent          = %RTbool\n",   RT_BF_GET(uCtrlReg, GITS_BF_CTRL_REG_CTLR_QUIESCENT));
    }

    /* GITS_BASER<n>. */
    for (unsigned i = 0; i < RT_ELEMENTS(pGitsDev->aItsTableRegs); i++)
    {
        static uint32_t const s_acbPageSize[] = { _4K, _16K, _64K, _64K };
        static const char* const s_apszType[] = { "UnImpl", "Devices", "vPEs", "Intr Collections" };
        uint64_t const uReg    = pGitsDev->aItsTableRegs[i].u;
        bool const     fValid  = RT_BOOL(RT_BF_GET(uReg, GITS_BF_CTRL_REG_BASER_VALID));
        uint8_t const  idxType = RT_BF_GET(uReg, GITS_BF_CTRL_REG_BASER_TYPE);
        if (   fValid
            || idxType != GITS_BASER_TYPE_UNIMPL)
        {
            uint16_t const uSize       = RT_BF_GET(uReg, GITS_BF_CTRL_REG_BASER_SIZE);
            uint16_t const cPages      = uSize > 0 ? uSize + 1 : 0;
            uint8_t const  idxPageSize = RT_BF_GET(uReg, GITS_BF_CTRL_REG_BASER_PAGESIZE);
            uint64_t const cbItsTable  = cPages * s_acbPageSize[idxPageSize];
            uint8_t const  uEntrySize  = RT_BF_GET(uReg, GITS_BF_CTRL_REG_BASER_ENTRY_SIZE);
            bool const     fIndirect   = RT_BOOL(RT_BF_GET(uReg, GITS_BF_CTRL_REG_BASER_INDIRECT));
            const char *pszType        = s_apszType[idxType];
            pHlp->pfnPrintf(pHlp, "  GITS_BASER[%u]      = %#RX64\n", i, uReg);
            pHlp->pfnPrintf(pHlp, "    Size               = %#x (pages=%u total=%.0Rhcb)\n", uSize, cPages, cbItsTable);
            pHlp->pfnPrintf(pHlp, "    Page size          = %#x (%.0Rhcb)\n", idxPageSize, s_acbPageSize[idxPageSize]);
            pHlp->pfnPrintf(pHlp, "    Shareability       = %#x\n",      RT_BF_GET(uReg, GITS_BF_CTRL_REG_BASER_SHAREABILITY));
            pHlp->pfnPrintf(pHlp, "    Phys addr          = %#RX64 (addr=%#RX64)\n", uReg & GITS_BF_CTRL_REG_BASER_PHYS_ADDR_MASK,
                                                                                     gitsGetBaseRegPhysAddr(uReg));
            pHlp->pfnPrintf(pHlp, "    Entry size         = %#x (%u bytes)\n", uEntrySize, uEntrySize > 0 ? uEntrySize + 1 : 0);
            pHlp->pfnPrintf(pHlp, "    Outer cache        = %#x\n",      RT_BF_GET(uReg, GITS_BF_CTRL_REG_BASER_OUTER_CACHE));
            pHlp->pfnPrintf(pHlp, "    Type               = %#x (%s)\n", idxType, pszType);
            pHlp->pfnPrintf(pHlp, "    Inner cache        = %#x\n",      RT_BF_GET(uReg, GITS_BF_CTRL_REG_BASER_INNER_CACHE));
            pHlp->pfnPrintf(pHlp, "    Indirect           = %RTbool\n",  fIndirect);
            pHlp->pfnPrintf(pHlp, "    Valid              = %RTbool\n",  fValid);
        }
    }

    /* GITS_CBASER. */
    {
        uint64_t const uReg   = pGitsDev->uCmdBaseReg.u;
        uint8_t const  uSize  = RT_BF_GET(uReg, GITS_BF_CTRL_REG_CBASER_SIZE);
        uint16_t const cPages = uSize > 0 ? uSize + 1 : 0;
        pHlp->pfnPrintf(pHlp, "  GITS_CBASER        = %#RX64\n", uReg);
        pHlp->pfnPrintf(pHlp, "    Size               = %#x (pages=%u total=%.0Rhcb)\n", uSize, cPages, _4K * cPages);
        pHlp->pfnPrintf(pHlp, "    Shareability       = %#x\n",      RT_BF_GET(uReg, GITS_BF_CTRL_REG_CBASER_SHAREABILITY));
        pHlp->pfnPrintf(pHlp, "    Phys addr          = %#RX64\n",   uReg & GITS_BF_CTRL_REG_CBASER_PHYS_ADDR_MASK);
        pHlp->pfnPrintf(pHlp, "    Outer cache        = %#x\n",      RT_BF_GET(uReg, GITS_BF_CTRL_REG_CBASER_OUTER_CACHE));
        pHlp->pfnPrintf(pHlp, "    Inner cache        = %#x\n",      RT_BF_GET(uReg, GITS_BF_CTRL_REG_CBASER_INNER_CACHE));
        pHlp->pfnPrintf(pHlp, "    Valid              = %RTbool\n",  RT_BF_GET(uReg, GITS_BF_CTRL_REG_CBASER_VALID));
    }

    /* GITS_CREADR. */
    {
        uint32_t const uReg = pGitsDev->uCmdReadReg;
        pHlp->pfnPrintf(pHlp, "  GITS_CREADR        = 0x%05RX32 (stalled=%RTbool offset=%RU32)\n", uReg,
                        RT_BF_GET(uReg, GITS_BF_CTRL_REG_CREADR_STALLED), uReg & GITS_BF_CTRL_REG_CREADR_OFFSET_MASK);
    }

    /* GITS_CWRITER. */
    {
        uint32_t const uReg = pGitsDev->uCmdWriteReg;
        pHlp->pfnPrintf(pHlp, "  GITS_CWRITER       = 0x%05RX32 (  retry=%RTbool offset=%RU32)\n", uReg,
                        RT_BF_GET(uReg, GITS_BF_CTRL_REG_CWRITER_RETRY), uReg & GITS_BF_CTRL_REG_CWRITER_OFFSET_MASK);
    }

    /* Interrupt Collection Table. */
    {
        pHlp->pfnPrintf(pHlp, "  Collection Table:\n");
        bool fHasValidCtes = false;
        for (unsigned i = 0; i < RT_ELEMENTS(pGitsDev->aCtes); i++)
        {
            VMCPUID const idTargetCpu = pGitsDev->aCtes[i];
            if (idTargetCpu != NIL_VMCPUID)
            {
                pHlp->pfnPrintf(pHlp, "    [%3u] = %RU32\n", i, idTargetCpu);
                fHasValidCtes = true;
            }
        }
        if (!fHasValidCtes)
            pHlp->pfnPrintf(pHlp, "    Empty (no valid entries)\n");
    }

    /* LPI cache. */
    {
        PCGITSLPIMAP   pLpiMap  = &pGitsDev->LpiMap;
        uint8_t const  cEntries = pGitsDev->cLpiMap;
        pHlp->pfnPrintf(pHlp, "  LPI cache (capacity=%u entries=%u)\n", RT_ELEMENTS(pLpiMap->uDevIdEventId), cEntries);
        for (uint8_t i = 0; i < cEntries; i++)
        {
            uint16_t const uDevId   = pLpiMap->uDevIdEventId[i].s.Lo;
            uint16_t const uEventId = pLpiMap->uDevIdEventId[i].s.Hi;
            uint16_t const uIcId    = pLpiMap->uIcId[i];
            uint16_t const uIntId   = pLpiMap->uIntId[i];
            VMCPUID const  idCpu    = pLpiMap->idCpu[i];
            pHlp->pfnPrintf(pHlp, "    [%2u] = (devid=%#RX16 eventid=%#RX16 intid=%RU16 icid=%RU16 vcpu=%RU32)\n",
                                  i, uDevId, uEventId, uIntId, uIcId, idCpu);
        }
    }
}


/**
 * MAPTI and MAPI command worker.
 *
 * @param   pDevIns     The device instance.
 * @param   pGitsDev    The GIC ITS state.
 * @param   uDevId      The device ID (full 32-bits as issued in the command).
 * @param   uEventId    The event ID (full 32-bits as issued in the command).
 * @param   uIntId      The physical LPI INTID.
 * @param   uIcId       The interrupt collection ID.
 * @param   fMapti      Whether this is the MAPTI command (@c true) or MAPI command
 *                      (@c false).
 */
static void gitsR3CmdMapIntr(PPDMDEVINS pDevIns, PGITSDEV pGitsDev, uint32_t uDevId, uint32_t uEventId, uint16_t uIntId,
                             uint16_t uIcId, bool fMapti)
{
#define GITS_CMD_QUEUE_SET_ERR(a_enmDiagSuffix) \
    do \
    { \
        gitsCmdQueueSetError(pDevIns, pGitsDev, \
                             fMapti ? kGitsDiag_CmdQueue_Cmd_ ## Mapti_ ## a_enmDiagSuffix \
                                    : kGitsDiag_CmdQueue_Cmd_ ## Mapi_  ## a_enmDiagSuffix, false /* fStall */); \
    } while (0)

    if (uDevId <= (uint32_t)GITS_DEV_ID_LAST)
    {
        if (uIcId < RT_ELEMENTS(pGitsDev->aCtes))
        {
            if (GIC_IS_INTR_LPI(uIntId))
            {
                GITSDTE uDte = 0;
                int rc = gitsDteRead(pDevIns, pGitsDev, uDevId, &uDte);
                if (RT_SUCCESS(rc))
                {
                    bool const fDteValid = RT_BF_GET(uDte, GITS_BF_DTE_VALID);
                    if (fDteValid)
                    {
                        uint32_t const cEntries = RT_BIT_32(RT_BF_GET(uDte, GITS_BF_DTE_ITT_RANGE) + 1);
                        if (uEventId < cEntries)
                        {
                            GITSITE const uIte = RT_BF_MAKE(GITS_BF_ITE_ICID,    uIcId)
                                               | RT_BF_MAKE(GITS_BF_ITE_INTID,   uIntId)
                                               | RT_BF_MAKE(GITS_BF_ITE_IS_PHYS, 1)
                                               | RT_BF_MAKE(GITS_BF_ITE_VALID,   1);
                            Log4Func(("uDte=%#RX64 uIte=%#RX64 uIcId=%RU16 uIntId=%RU16\n", uDte, uIte, uIcId, uIntId));
                            rc = gitsIteWrite(pDevIns, uDte, uEventId, uIte);
                            if (RT_SUCCESS(rc))
                                return;
                            GITS_CMD_QUEUE_SET_ERR(Ite_Wr_Failed);
                        }
                        else
                            GITS_CMD_QUEUE_SET_ERR(EventId_OutOfRange);
                    }
                    else
                        GITS_CMD_QUEUE_SET_ERR(DevId_Unmapped);
                }
                else
                    GITS_CMD_QUEUE_SET_ERR(Dte_Rd_Failed);
            }
            else
                GITS_CMD_QUEUE_SET_ERR(PhysLpi_OutOfRange);
        }
        else
            GITS_CMD_QUEUE_SET_ERR(IcId_OutOfRange);
    }
    else
        GITS_CMD_QUEUE_SET_ERR(DevId_OutOfRange);

#undef GITS_CMD_QUEUE_SET_ERR
}


/**
 * DISCARD command worker.
 *
 * @param   pVM         The cross context VM state.
 * @param   pDevIns     The device instance.
 * @param   pGitsDev    The GIC ITS state.
 * @param   uDevId      The device ID (full 32-bits as issued in the command).
 * @param   uEventId    The event ID (full 32-bits as issued in the command).
 */
static void gitsR3CmdMapDiscard(PCVMCC pVM, PPDMDEVINS pDevIns, PGITSDEV pGitsDev, uint32_t uDevId, uint32_t uEventId)
{
#define GITS_CMD_QUEUE_SET_ERR(a_enmDiag)           gitsCmdQueueSetError(pDevIns, pGitsDev, a_enmDiag, false /* fStall */)

    if (uDevId <= (uint32_t)GITS_DEV_ID_LAST)
    {
        GITSDTE uDte = 0;
        int rc = gitsDteRead(pDevIns, pGitsDev, uDevId, &uDte);
        if (RT_SUCCESS(rc))
        {
            bool const fDteValid = RT_BF_GET(uDte, GITS_BF_DTE_VALID);
            if (fDteValid)
            {
                uint32_t const cEntries = RT_BIT_32(RT_BF_GET(uDte, GITS_BF_DTE_ITT_RANGE) + 1);
                if (uEventId < cEntries)
                {
                    GITSITE uIte = 0;
                    rc = gitsIteRead(pDevIns, uDte, uEventId, &uIte);
                    if (RT_SUCCESS(rc))
                    {
                        bool const fIteValid  = RT_BF_GET(uIte, GITS_BF_ITE_VALID);
                        if (fIteValid)
                        {
                            uint16_t const uIntId = RT_BF_GET(uIte, GITS_BF_ITE_INTID);
                            uint16_t const uIcId  = RT_BF_GET(uIte, GITS_BF_ITE_ICID);
                            bool const fPhysIntr  = RT_BF_GET(uIte, GITS_BF_ITE_IS_PHYS);
                            bool const fLpiValid  = GIC_IS_INTR_LPI(uIntId);
                            if (   fLpiValid
                                && fPhysIntr
                                && uIcId < RT_ELEMENTS(pGitsDev->aCtes))
                            {
                                Assert(!RT_BF_GET(pGitsDev->uTypeReg.u, GITS_BF_CTRL_REG_TYPER_PTA));
                                VMCPUID const idCpu = pGitsDev->aCtes[uIcId];
                                if (idCpu < pVM->cCpus)
                                {
                                    PVMCPUCC pVCpu = pVM->CTX_SUFF(apCpus)[idCpu];
                                    gitsLpiCacheInvalidateOne(pGitsDev, uDevId, uEventId);
                                    gicReDistSetLpi(pDevIns, pVCpu, uIntId, false /* fAsserted */);

                                    uIte = 0;
                                    rc = gitsIteWrite(pDevIns, uDte, uEventId, uIte);
                                    if (RT_SUCCESS(rc))
                                        return;
                                }
                                else
                                    GITS_CMD_QUEUE_SET_ERR(kGitsDiag_CmdQueue_Cmd_Discard_CpuId_OutOfRange);
                            }
                            else
                                GITS_CMD_QUEUE_SET_ERR(kGitsDiag_CmdQueue_Cmd_Discard_Ite_Invalid);
                        }
                        else
                            GITS_CMD_QUEUE_SET_ERR(kGitsDiag_CmdQueue_Cmd_Discard_Ite_Unmapped);
                    }
                    else
                        GITS_CMD_QUEUE_SET_ERR(kGitsDiag_CmdQueue_Cmd_Discard_Ite_Rd_Failed);
                }
                else
                    GITS_CMD_QUEUE_SET_ERR(kGitsDiag_CmdQueue_Cmd_Discard_EventId_OutOfRange);
            }
            else
                GITS_CMD_QUEUE_SET_ERR(kGitsDiag_CmdQueue_Cmd_Discard_Dte_Unmapped);
        }
        else
            GITS_CMD_QUEUE_SET_ERR(kGitsDiag_CmdQueue_Cmd_Discard_Dte_Rd_Failed);
    }
    else
        GITS_CMD_QUEUE_SET_ERR(kGitsDiag_CmdQueue_Cmd_Discard_DevId_OutOfRange);

#undef GITS_CMD_QUEUE_SET_ERR
}


/**
 * Processes ITS commands.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM state.
 * @param   pDevIns     The device instance.
 * @param   pGitsDev    The GIC ITS state.
 * @param   pvBuf       The command queue buffer.
 * @param   cbBuf       The size of the command queue buffer in bytes.
 */
DECLHIDDEN(int) gitsR3CmdQueueProcess(PCVMCC pVM, PPDMDEVINS pDevIns, PGITSDEV pGitsDev, void *pvBuf, uint32_t cbBuf)
{
    Log4Func(("cbBuf=%RU32\n", cbBuf));

    /* Hold the critical section as we could be accessing the device state simultaneously with MMIO accesses. */
    GIC_CRIT_SECT_ENTER(pDevIns);

    if (gitsCmdQueueCanProcessRequests(pGitsDev))
    {
        uint32_t   offRead;
        uint32_t   offWrite;
        bool const fIsEmpty = gitsCmdQueueIsEmptyEx(pGitsDev, &offRead, &offWrite);
        if (!fIsEmpty)
        {
            uint32_t const cCmdQueuePages = RT_BF_GET(pGitsDev->uCmdBaseReg.u, GITS_BF_CTRL_REG_CBASER_SIZE) + 1;
            uint32_t const cbCmdQueue     = cCmdQueuePages << GITS_CMD_QUEUE_PAGE_SHIFT;
            AssertRelease(cbCmdQueue <= cbBuf); /** @todo Paranoia; make this a debug assert later. */

            /*
             * Read all the commands from guest memory into our command queue buffer.
             */
            int      rc;
            uint32_t cbCmds;
            RTGCPHYS const GCPhysCmds = pGitsDev->uCmdBaseReg.u & GITS_BF_CTRL_REG_CBASER_PHYS_ADDR_MASK;

            /* Leave the critical section while reading (a potentially large number of) commands from guest memory. */
            GIC_CRIT_SECT_LEAVE(pDevIns);

            if (offWrite > offRead)
            {
                /* The write offset has not wrapped around, read them in one go. */
                cbCmds = offWrite - offRead;
                Assert(cbCmds <= cbBuf);
                rc = PDMDevHlpPhysReadMeta(pDevIns, GCPhysCmds + offRead, pvBuf, cbCmds);
            }
            else
            {
                /* The write offset has wrapped around, read till end of buffer followed by wrapped-around data. */
                uint32_t const cbForward = cbCmdQueue - offRead;
                uint32_t const cbWrapped = offWrite;
                Assert(cbForward + cbWrapped <= cbBuf);
                rc = PDMDevHlpPhysReadMeta(pDevIns, GCPhysCmds + offRead, pvBuf, cbForward);
                if (   RT_SUCCESS(rc)
                    && cbWrapped > 0)
                    rc = PDMDevHlpPhysReadMeta(pDevIns, GCPhysCmds, (void *)((uintptr_t)pvBuf + cbForward), cbWrapped);
                cbCmds = cbForward + cbWrapped;
            }

            /*
             * Process the commands in the buffer.
             */
            if (RT_SUCCESS(rc))
            {
                /* Indicate to the guest we've fetched all commands. */
                GIC_CRIT_SECT_ENTER(pDevIns);
                pGitsDev->uCmdReadReg   = offWrite;
                pGitsDev->uCmdWriteReg &= ~GITS_BF_CTRL_REG_CWRITER_RETRY_MASK;

                /* Don't hold the critical section while processing commands. */
                GIC_CRIT_SECT_LEAVE(pDevIns);

                uint32_t const cCmds = cbCmds / sizeof(GITSCMD);
                for (uint32_t idxCmd = 0; idxCmd < cCmds; idxCmd++)
                {
                    PCGITSCMD pCmd = (PCGITSCMD)((uintptr_t)pvBuf + (idxCmd * sizeof(GITSCMD)));
                    uint8_t const uCmdId = pCmd->common.uCmdId;
                    switch (uCmdId)
                    {
                        case GITS_CMD_ID_MAPC:
                        {
                            /* Map interrupt collection with a target CPU ID. */
                            uint64_t const uDw2 = pCmd->au64[2].u;
                            uint8_t  const fValid  = RT_BF_GET(uDw2, GITS_BF_CMD_MAPC_DW2_VALID);
                            uint32_t const uRdBase = RT_BF_GET(uDw2, GITS_BF_CMD_MAPC_DW2_RDBASE);
                            uint16_t const uIcId   = RT_BF_GET(uDw2, GITS_BF_CMD_MAPC_DW2_IC_ID);

                            if (RT_LIKELY(uIcId < RT_ELEMENTS(pGitsDev->aCtes)))
                            {
                                GIC_CRIT_SECT_ENTER(pDevIns);
                                Assert(!RT_BF_GET(pGitsDev->uTypeReg.u, GITS_BF_CTRL_REG_TYPER_PTA));
                                VMCPUID idCpu;
                                if (   fValid
                                    && uRdBase < pVM->cCpus)
                                    idCpu = uRdBase;
                                else
                                    idCpu = NIL_VMCPUID;
                                pGitsDev->aCtes[uIcId] = idCpu;
                                GIC_CRIT_SECT_LEAVE(pDevIns);
                            }
                            else
                                gitsCmdQueueSetError(pDevIns, pGitsDev, kGitsDiag_CmdQueue_Cmd_Mapc_IcId_OutOfRange,
                                                     false /* fStall */);
                            STAM_COUNTER_INC(&pGitsDev->StatCmdMapc);
                            break;
                        }

                        case GITS_CMD_ID_MAPD:
                        {
                            /* Map device ID to an interrupt translation table. */
                            uint32_t const uDevId     = RT_BF_GET(pCmd->au64[0].u, GITS_BF_CMD_MAPD_DW0_DEV_ID);
                            uint8_t const  cDevIdBits = RT_BF_GET(pCmd->au64[1].u, GITS_BF_CMD_MAPD_DW1_SIZE);
                            bool const     fValid     = RT_BF_GET(pCmd->au64[2].u, GITS_BF_CMD_MAPD_DW2_VALID);
                            RTGCPHYS const GCPhysItt  = pCmd->au64[2].u & GITS_BF_CMD_MAPD_DW2_ITT_ADDR_MASK;

                            /* Check that the device ID is within the supported device ID range. */
                            if (uDevId <= (uint32_t)GITS_DEV_ID_LAST)
                            {
                                if (fValid)
                                {
                                    /* Check that size is within the supported event ID range. */
                                    uint8_t const cEventIdBits = RT_BF_GET(pGitsDev->uTypeReg.u, GITS_BF_CTRL_REG_TYPER_ID_BITS);
                                    if (cDevIdBits <= cEventIdBits)
                                    {
                                        GITSDTE const uDte = RT_BF_MAKE(GITS_BF_DTE_VALID,     1)
                                                           | RT_BF_MAKE(GITS_BF_DTE_ITT_RANGE, cDevIdBits)
                                                           | (GCPhysItt & GITS_BF_DTE_ITT_ADDR_MASK);

                                        GIC_CRIT_SECT_ENTER(pDevIns);
                                        rc = gitsDteWrite(pDevIns, pGitsDev, uDevId, uDte);
                                        GIC_CRIT_SECT_LEAVE(pDevIns);
                                        AssertRC(rc);
                                    }
                                    else
                                        gitsCmdQueueSetError(pDevIns, pGitsDev, kGitsDiag_CmdQueue_Cmd_Mapd_Size_Invalid,
                                                             false /* fStall */);
                                }
                                else
                                {
                                    uint64_t const uDte = 0;
                                    GIC_CRIT_SECT_ENTER(pDevIns);
                                    rc = gitsDteWrite(pDevIns, pGitsDev, uDevId, uDte);
                                    /*
                                     * Well-behaving guests don't typically keep modifying the device ID mapping,
                                     * so we simply invalidate the whole cache here. If the need arises, perform
                                     * selective invalidation of the cache.
                                     */
                                    gitsLpiCacheInvalidateAll(pGitsDev);
                                    GIC_CRIT_SECT_LEAVE(pDevIns);
                                    AssertRC(rc);
                                }
                            }
                            else
                                gitsCmdQueueSetError(pDevIns, pGitsDev, kGitsDiag_CmdQueue_Cmd_Mapd_DevId_OutOfRange,
                                                     false /* fStall */);
                            STAM_COUNTER_INC(&pGitsDev->StatCmdMapd);
                            break;
                        }

                        case GITS_CMD_ID_MAPTI:
                        {
                            /* Map device ID and event ID to corresponding ITE with ICID and the INTID. */
                            uint16_t const uIcId    = RT_BF_GET(pCmd->au64[2].u, GITS_BF_CMD_MAPTI_DW2_IC_ID);
                            uint32_t const uDevId   = RT_BF_GET(pCmd->au64[0].u, GITS_BF_CMD_MAPTI_DW0_DEV_ID);
                            uint32_t const uEventId = RT_BF_GET(pCmd->au64[1].u, GITS_BF_CMD_MAPTI_DW1_EVENT_ID);
                            uint32_t const uIntId   = RT_BF_GET(pCmd->au64[1].u, GITS_BF_CMD_MAPTI_DW1_PHYS_INTID);

                            GIC_CRIT_SECT_ENTER(pDevIns);
                            gitsR3CmdMapIntr(pDevIns, pGitsDev, uDevId, uEventId, uIntId, uIcId, true /* fMapti */);
                            GIC_CRIT_SECT_LEAVE(pDevIns);
                            STAM_COUNTER_INC(&pGitsDev->StatCmdMapti);
                            break;
                        }

                        case GITS_CMD_ID_MAPI:
                        {
                            /* Map device ID and event ID to corresponding ITE with ICID and the INTID same as the event ID. */
                            uint16_t const uIcId    = RT_BF_GET(pCmd->au64[2].u, GITS_BF_CMD_MAPTI_DW2_IC_ID);
                            uint32_t const uDevId   = RT_BF_GET(pCmd->au64[0].u, GITS_BF_CMD_MAPTI_DW0_DEV_ID);
                            uint32_t const uEventId = RT_BF_GET(pCmd->au64[1].u, GITS_BF_CMD_MAPTI_DW1_EVENT_ID);
                            uint32_t const uIntId   = uEventId;

                            GIC_CRIT_SECT_ENTER(pDevIns);
                            gitsR3CmdMapIntr(pDevIns, pGitsDev, uDevId, uEventId, uIntId, uIcId, false /* fMapti */);
                            GIC_CRIT_SECT_LEAVE(pDevIns);
                            STAM_COUNTER_INC(&pGitsDev->StatCmdMapi);
                            break;
                        }

                        case GITS_CMD_ID_INV:
                        {
                            /* Reading the table is likely to take the same time as reading just one entry. */
                            GIC_CRIT_SECT_ENTER(pDevIns);
                            gicDistReadLpiConfigTableFromMem(pDevIns);
                            gitsLpiCacheInvalidateAll(pGitsDev);
                            GIC_CRIT_SECT_LEAVE(pDevIns);
                            STAM_COUNTER_INC(&pGitsDev->StatCmdInv);
                            break;
                        }

                        case GITS_CMD_ID_SYNC:
                            /* Nothing to do since all previous commands have committed their changes to device state. */
                            STAM_COUNTER_INC(&pGitsDev->StatCmdSync);
                            break;

                        case GITS_CMD_ID_INVALL:
                        {
                            /* Reading the table is likely to take the same time as reading just one entry. */
                            uint64_t const uDw2  = pCmd->au64[2].u;
                            uint16_t const uIcId = RT_BF_GET(uDw2, GITS_BF_CMD_INVALL_DW2_IC_ID);
                            if (uIcId < RT_ELEMENTS(pGitsDev->aCtes))
                            {
                                if (pGitsDev->aCtes[uIcId] < pVM->cCpus)
                                {
                                    GIC_CRIT_SECT_ENTER(pDevIns);
                                    gicDistReadLpiConfigTableFromMem(pDevIns);
                                    gitsLpiCacheInvalidateAll(pGitsDev);
                                    GIC_CRIT_SECT_LEAVE(pDevIns);
                                }
                                else
                                    gitsCmdQueueSetError(pDevIns, pGitsDev, kGitsDiag_CmdQueue_Cmd_Invall_Cte_Unmapped,
                                                         false /* fStall */);
                            }
                            else
                                gitsCmdQueueSetError(pDevIns, pGitsDev, kGitsDiag_CmdQueue_Cmd_Invall_IcId_OutOfRange,
                                                     false /* fStall */);
                            STAM_COUNTER_INC(&pGitsDev->StatCmdInvall);
                            break;
                        }

                        case GITS_CMD_ID_DISCARD:
                        {
                            /* Clear the pending state for the LPI translated from the device ID and event ID. */
                            uint32_t const uDevId   = RT_BF_GET(pCmd->au64[0].u, GITS_BF_CMD_DISCARD_DW0_DEV_ID);
                            uint32_t const uEventId = RT_BF_GET(pCmd->au64[1].u, GITS_BF_CMD_DISCARD_DW1_EVENT_ID);
                            GIC_CRIT_SECT_ENTER(pDevIns);
                            gitsR3CmdMapDiscard(pVM, pDevIns, pGitsDev, uDevId, uEventId);
                            GIC_CRIT_SECT_LEAVE(pDevIns);
                            STAM_COUNTER_INC(&pGitsDev->StatCmdDiscard);
                            break;
                        }

                        default:
                        {
                            /* Record an internal error but do NOT stall queue as we have already advanced the read offset. */
                            gitsCmdQueueSetError(pDevIns, pGitsDev, kGitsDiag_CmdQueue_Basic_Unknown_Cmd, false /* fStall */);
                            AssertReleaseMsgFailed(("Cmd=%#x (%s) idxCmd=%u cCmds=%u offRead=%#RX32 offWrite=%#RX32\n",
                                                    uCmdId, gitsGetCommandName(uCmdId), idxCmd, cCmds, offRead, offWrite));
                            break;
                        }
                    }
                }
                return VINF_SUCCESS;
            }

            /* Failed to read command queue from the physical address specified by the guest, stall queue and retry later. */
            gitsCmdQueueSetError(pDevIns, pGitsDev, kGitsDiag_CmdQueue_Basic_Invalid_PhysAddr, true /* fStall */);
            return VINF_TRY_AGAIN;
        }
    }

    GIC_CRIT_SECT_LEAVE(pDevIns);
    return VINF_SUCCESS;
}
#endif /* IN_RING3 */


/**
 * Records an error while translating and forwarding an LPI to a redistributor.
 *
 * @param   pDevIns         The device instance.
 * @param   pGitsDev        The GIC ITS state.
 * @param   enmDiag         The error diagnostic.
 */
DECL_FORCE_INLINE(void) gitsLpiSetError(PPDMDEVINS pDevIns, PGITSDEV pGitsDev, GITSDIAG enmDiag)
{
    Log4Func(("enmDiag=%#RX32 (%s)\n", enmDiag, gitsGetDiagDescription(enmDiag)));
    Assert(GIC_CRIT_SECT_IS_OWNER(pDevIns)); NOREF(pDevIns);
    pGitsDev->enmDiag = enmDiag;
}


/**
 * Triggers an LPI by using the ITS to translate and route it to a specific
 * redistributor and connected PE.
 *
 * @param   pVM         The cross context VM state.
 * @param   pDevIns     The device instance.
 * @param   pGitsDev    The GIC ITS state.
 * @param   uDevId      The device ID.
 * @param   uEventId    The event ID.
 * @param   fAsserted   Whether the LPI is asserted/de-asserted.
 */
DECLHIDDEN(void) gitsLpiTrigger(PVMCC pVM, PPDMDEVINS pDevIns, PGITSDEV pGitsDev, uint32_t uDevId, uint32_t uEventId,
                                bool fAsserted)
{
    Assert(GIC_CRIT_SECT_IS_OWNER(pDevIns));

    /*
     * When the ITS is disabled, writes to the GITS_TRANSLATER register are ignored.
     * See ARM GIC spec. 12.19.4 "GITS_CTLR, ITS Control Register".
     */
    bool const fEnabled = RT_BF_GET(pGitsDev->uCtrlReg, GITS_BF_CTRL_REG_CTLR_ENABLED);
    if (fEnabled)
    { /* likely */ }
    else
    {
        Log4Func(("ITS disabled, not traversing any tables (uDevId=%#RX32 uEventId=%#RX32)\n", uDevId, uEventId));
        return;
    }

    /*
     * This operation is similar to a GITS_TRANSLATER write but initiated from PCI rather than MMIO.
     * If the device ID or event ID exceeds the supported range, the behavior is implementation defined.
     * We can either ignore the entire write or ignore out-of-range bits. We choose the latter as it
     * avoids conditional(s) and is something that should never happen with well-behaved guests.
     * See ARM GIC spec. 12.19.12 "GITS_TRANSLATER, ITS Translation Register".
     */
    uDevId   &= ~(uint32_t)(UINT32_C(1) << GITS_DEV_ID_BITS);
    uEventId &= ~(uint32_t)(UINT32_C(1) << GITS_EVENT_ID_BITS);

    /* Lookup the LPI from the cache first. */
    {
        GITSLPIMAPENTRY LpiMapEntry;
        bool const fFound = gitstLpiCacheLookup(pGitsDev, uDevId, uEventId, &LpiMapEntry);
        if (fFound)
        {
            uint16_t const uIntId = LpiMapEntry.uIntId;
            VMCPUID const  idCpu  = LpiMapEntry.idCpu;
            Assert(GIC_IS_INTR_LPI(uIntId));
            if (RT_LIKELY(idCpu < pVM->cCpus))
            {
                PVMCPUCC pVCpu = pVM->CTX_SUFF(apCpus)[idCpu];
                gicReDistSetLpi(pDevIns, pVCpu, uIntId, fAsserted);
                return;
            }
            else
                AssertMsgFailed(("CPU index out-of-bounds %RU32\n", idCpu));
        }
    }

    /** @todo Error recording. */

    /* Read the DTE */
    GITSDTE uDte;
    int rc = gitsDteRead(pDevIns, pGitsDev, uDevId, &uDte);
    if (RT_SUCCESS(rc))
    {
        /* Check if the DTE is mapped (valid). */
        bool const fDteValid = RT_BF_GET(uDte, GITS_BF_DTE_VALID);
        if (fDteValid)
        {
            /* Check if the event ID (which is the index) is within range. */
            uint32_t const cEntries = RT_BIT_32(RT_BF_GET(uDte, GITS_BF_DTE_ITT_RANGE) + 1);
            if (uEventId < cEntries)
            {
                /* Read the interrupt-translation entry from guest memory. */
                GITSITE uIte;
                rc = gitsIteRead(pDevIns, uDte, uEventId, &uIte);
                if (RT_SUCCESS(rc))
                {
                    /* Check if the interrupt ID is within range. */
                    bool const fIteValid  = RT_BF_GET(uIte, GITS_BF_ITE_VALID);
                    if (fIteValid)
                    {
                        /* Check if this is a valid physical interrupt and LPI. */
                        uint16_t const uIntId     = RT_BF_GET(uIte, GITS_BF_ITE_INTID);
                        bool     const fPhysIntr  = RT_BF_GET(uIte, GITS_BF_ITE_IS_PHYS);
                        bool const     fLpiValid  = GIC_IS_INTR_LPI(uIntId);
                        if (   fLpiValid
                            && fPhysIntr)
                        {
                            /* Check if the interrupt collection ID is valid. */
                            uint16_t const uIcId = RT_BF_GET(uIte, GITS_BF_ITE_ICID);
                            if (uIcId < RT_ELEMENTS(pGitsDev->aCtes))
                            {
                                /* Check that the target CPU is valid. */
                                Assert(!RT_BF_GET(pGitsDev->uTypeReg.u, GITS_BF_CTRL_REG_TYPER_PTA));
                                VMCPUID const idCpu = pGitsDev->aCtes[uIcId];
                                if (idCpu < pVM->cCpus)
                                {
                                    /* Set or clear the LPI pending state in the redistributor. */
                                    PVMCPUCC pVCpu = pVM->CTX_SUFF(apCpus)[idCpu];
                                    gicReDistSetLpi(pDevIns, pVCpu, uIntId, fAsserted);

                                    /* Add the LPI to the cache. */
                                    GITSLPIMAPENTRY LpiMapEntry;
                                    LpiMapEntry.uDevIdEventId.s.Lo = uDevId;
                                    LpiMapEntry.uDevIdEventId.s.Hi = uEventId;
                                    LpiMapEntry.uIntId             = uIntId;
                                    LpiMapEntry.uIcId              = uIcId;
                                    LpiMapEntry.idCpu              = idCpu;
                                    gitsLpiCacheAdd(pGitsDev, &LpiMapEntry);
                                }
                                else
                                    gitsLpiSetError(pDevIns, pGitsDev, kGitsDiag_LpiTrigger_CpuId_OutOfRange);
                            }
                            else
                                gitsLpiSetError(pDevIns, pGitsDev, kGitsDiag_LpiTrigger_IcId_OutOfRange);
                        }
                        else
                            gitsLpiSetError(pDevIns, pGitsDev, kGitsDiag_LpiTrigger_Ite_Invalid);
                    }
                    else
                        gitsLpiSetError(pDevIns, pGitsDev, kGitsDiag_LpiTrigger_Ite_Unmapped);
                }
                else
                    gitsLpiSetError(pDevIns, pGitsDev, kGitsDiag_LpiTrigger_Ite_Rd_Failed);
            }
            else
                gitsLpiSetError(pDevIns, pGitsDev, kGitsDiag_LpiTrigger_EventId_OutOfRange);
        }
        else
            gitsLpiSetError(pDevIns, pGitsDev, kGitsDiag_LpiTrigger_Dte_Unmapped);
    }
    else
        gitsLpiSetError(pDevIns, pGitsDev, kGitsDiag_LpiTrigger_Dte_Rd_Failed);
}

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

