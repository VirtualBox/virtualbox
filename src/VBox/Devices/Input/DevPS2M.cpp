/* $Id: DevPS2M.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PS2M - PS/2 auxiliary device (mouse) emulation.
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

/*
 * References:
 *
 * The Undocumented PC (2nd Ed.), Frank van Gilluwe, Addison-Wesley, 1996.
 * IBM TrackPoint System Version 4.0 Engineering Specification, 1999.
 * ELAN Microelectronics eKM8025 USB & PS/2 Mouse Controller, 2006.
 *
 *
 * Notes:
 *
 *  - The auxiliary device commands are very similar to keyboard commands.
 *    Most keyboard commands which do not specifically deal with the keyboard
 *    (enable, disable, reset) have identical counterparts.
 *  - The code refers to 'auxiliary device' and 'mouse'; these terms are not
 *    quite interchangeable. 'Auxiliary device' is used when referring to the
 *    generic PS/2 auxiliary device interface and 'mouse' when referring to
 *    a mouse attached to the auxiliary port.
 *  - The basic modes of operation are reset, stream, and remote. Those are
 *    mutually exclusive. Stream and remote modes can additionally have wrap
 *    mode enabled.
 *  - The auxiliary device sends unsolicited data to the host only when it is
 *    both in stream mode and enabled. Otherwise it only responds to commands.
 *
 *
 * There are three report packet formats supported by the emulated device. The
 * standard three-byte PS/2 format (with middle button support), IntelliMouse
 * four-byte format with added scroll wheel, and IntelliMouse Explorer four-byte
 * format with reduced scroll wheel range but two additional buttons. Note that
 * the first three bytes of the report are always the same.
 *
 * Upon reset, the mouse is always in the standard PS/2 mode. A special 'knock'
 * sequence can be used to switch to ImPS/2 or ImEx mode. Three consecutive
 * Set Sampling Rate (0F3h) commands with arguments 200, 100, 80 switch to ImPS/2
 * mode. While in ImPS/2 or PS/2 mode, three consecutive Set Sampling Rate
 * commands with arguments 200, 200, 80 switch to ImEx mode. The Read ID (0F2h)
 * command will report the currently selected protocol.
 *
 * There is an extended ImEx mode with support for horizontal scrolling. It is
 * entered from ImEx mode with a 200, 80, 40 sequence of Set Sampling Rate
 * commands. It does not change the reported protocol (it remains 4, or ImEx)
 * but changes the meaning of the 4th byte.
 *
 *
 * Standard PS/2 pointing device three-byte report packet format:
 *
 * +--------+--------+--------+--------+--------+--------+--------+--------+--------+
 * |Bit/byte|  bit 7 |  bit 6 |  bit 5 |  bit 4 |  bit 3 |  bit 2 |  bit 1 |  bit 0 |
 * +--------+--------+--------+--------+--------+--------+--------+--------+--------+
 * | Byte 1 | Y ovfl | X ovfl | Y sign | X sign |  Sync  | M btn  | R btn  | L btn  |
 * +--------+--------+--------+--------+--------+--------+--------+--------+--------+
 * | Byte 2 |              X movement delta (two's complement)                      |
 * +--------+--------+--------+--------+--------+--------+--------+--------+--------+
 * | Byte 3 |              Y movement delta (two's complement)                      |
 * +--------+--------+--------+--------+--------+--------+--------+--------+--------+
 *
 *   - The sync bit is always set. It allows software to synchronize data packets
 *     as the X/Y position data typically does not have bit 4 set.
 *   - The overflow bits are set if motion exceeds accumulator range. We use the
 *     maximum range (effectively 9 bits) and do not set the overflow bits.
 *   - Movement in the up/right direction is defined as having positive sign.
 *
 *
 * IntelliMouse PS/2 (ImPS/2) fourth report packet byte:
 *
 * +--------+--------+--------+--------+--------+--------+--------+--------+--------+
 * |Bit/byte|  bit 7 |  bit 6 |  bit 5 |  bit 4 |  bit 3 |  bit 2 |  bit 1 |  bit 0 |
 * +--------+--------+--------+--------+--------+--------+--------+--------+--------+
 * | Byte 4 |              Z movement delta (two's complement)                      |
 * +--------+--------+--------+--------+--------+--------+--------+--------+--------+
 *
 *   - The valid range for Z delta values is only -8/+7, i.e. 4 bits.
 *
 * IntelliMouse Explorer (ImEx) fourth report packet byte:
 *
 * +--------+--------+--------+--------+--------+--------+--------+--------+--------+
 * |Bit/byte|  bit 7 |  bit 6 |  bit 5 |  bit 4 |  bit 3 |  bit 2 |  bit 1 |  bit 0 |
 * +--------+--------+--------+--------+--------+--------+--------+--------+--------+
 * | Byte 4 |   0    |   0    | Btn 5  | Btn 4  |  Z mov't delta (two's complement) |
 * +--------+--------+--------+--------+--------+--------+--------+--------+--------+
 *
 *   - The Z delta values are in practice only -1/+1; some mice (A4tech?) report
 *     horizontal scrolling as -2/+2.
 *
 * IntelliMouse Explorer (ImEx) fourth report packet byte when scrolling:
 *
 * +--------+--------+--------+--------+--------+--------+--------+--------+--------+
 * |Bit/byte|  bit 7 |  bit 6 |  bit 5 |  bit 4 |  bit 3 |  bit 2 |  bit 1 |  bit 0 |
 * +--------+--------+--------+--------+--------+--------+--------+--------+--------+
 * | Byte 4 |   V    |   H    |      Z or W movement delta (two's complement)       |
 * +--------+--------+--------+--------+--------+--------+--------+--------+--------+
 *
 *   - Buttons 4 and 5 are reported as with the regular ImEx protocol, but not when
 *     scrolling. This is a departure from the usual logic because when the mouse
 *     sends scroll events, the state of buttons 4/5 is not reported and the last
 *     reported state should be assumed.
 *
 *   - When the V bit (bit 7) is set, vertical scroll (Z axis) is being reported.
 *     When the H bit (bit 6) is set, horizontal scroll (W axis) is being reported.
 *     The H and V bits are never set at the same time (also see below). When
 *     the H and V bits are both clear, button 4/5 state is being reported.
 *
 *   - The Z/W delta is extended to 6 bits. Z (vertical) values are not restricted
 *     to -1/+1, although W (horizontal) values are. Z values of at least -20/+20
 *     can be seen in practice.
 *
 *   - Horizontal and vertical scroll is mutually exclusive. When the button is
 *     tilted, no vertical scrolling is reported, i.e. horizontal scrolling
 *     has priority over vertical.
 *
 *   - Positive values indicate down/right direction, negative values up/left.
 *
 *   - When the scroll button is tilted to engage horizontal scrolling, the mouse
 *     keeps sending events at a rate of 4 or 5 per second as long as the button
 *     is tilted.
 *
 * All report formats were verified with a real Microsoft IntelliMouse Explorer 4.0
 * mouse attached through a PS/2 port.
 *
 * The button "accumulator" is necessary to avoid missing brief button presses.
 * Without it, a very fast mouse button press + release might be lost if it
 * happened between sending reports. The accumulator latches button presses to
 * prevent that.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP   LOG_GROUP_DEV_KBD
#include <VBox/vmm/pdmdev.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include "VBoxDD.h"
#define IN_PS2M
#include "DevPS2.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @name Auxiliary device commands sent by the system.
 * @{ */
#define ACMD_SET_SCALE_11   0xE6    /* Set 1:1 scaling. */
#define ACMD_SET_SCALE_21   0xE7    /* Set 2:1 scaling. */
#define ACMD_SET_RES        0xE8    /* Set resolution. */
#define ACMD_REQ_STATUS     0xE9    /* Get device status. */
#define ACMD_SET_STREAM     0xEA    /* Set stream mode. */
#define ACMD_READ_REMOTE    0xEB    /* Read remote data. */
#define ACMD_RESET_WRAP     0xEC    /* Exit wrap mode. */
#define ACMD_INVALID_1      0xED
#define ACMD_SET_WRAP       0xEE    /* Set wrap (echo) mode. */
#define ACMD_INVALID_2      0xEF
#define ACMD_SET_REMOTE     0xF0    /* Set remote mode. */
#define ACMD_INVALID_3      0xF1
#define ACMD_READ_ID        0xF2    /* Read device ID. */
#define ACMD_SET_SAMP_RATE  0xF3    /* Set sampling rate. */
#define ACMD_ENABLE         0xF4    /* Enable (streaming mode). */
#define ACMD_DISABLE        0xF5    /* Disable (streaming mode). */
#define ACMD_SET_DEFAULT    0xF6    /* Set defaults. */
#define ACMD_INVALID_4      0xF7
#define ACMD_INVALID_5      0xF8
#define ACMD_INVALID_6      0xF9
#define ACMD_INVALID_7      0xFA
#define ACMD_INVALID_8      0xFB
#define ACMD_INVALID_9      0xFC
#define ACMD_INVALID_10     0xFD
#define ACMD_RESEND         0xFE    /* Resend response. */
#define ACMD_RESET          0xFF    /* Reset device. */
/** @} */

/** @name Auxiliary device responses sent to the system.
 * @{ */
#define ARSP_ID             0x00
#define ARSP_BAT_OK         0xAA    /* Self-test passed. */
#define ARSP_ACK            0xFA    /* Command acknowledged. */
#define ARSP_ERROR          0xFC    /* Bad command. */
#define ARSP_RESEND         0xFE    /* Requesting resend. */
/** @} */


/*********************************************************************************************************************************
*   Test code function declarations                                                                                              *
*********************************************************************************************************************************/
#if defined(RT_STRICT) && defined(IN_RING3)
static void ps2mR3TestAccumulation(void);
#endif


#ifdef IN_RING3

/* Report a change in status down (or is it up?) the driver chain. */
static void ps2mR3SetDriverState(PPS2MR3 pThisCC, bool fEnabled)
{
    PPDMIMOUSECONNECTOR pDrv = pThisCC->Mouse.pDrv;
    if (pDrv)
        pDrv->pfnReportModes(pDrv, fEnabled, false, false, false);
}

/* Reset the pointing device. */
static void ps2mR3Reset(PPS2M pThis, PPS2MR3 pThisCC)
{
    LogFlowFunc(("Reset"));

    PS2Q_INSERT(&pThis->cmdQ, ARSP_BAT_OK);
    PS2Q_INSERT(&pThis->cmdQ, 0);
    pThis->enmMode   = AUX_MODE_STD;
    pThis->u8CurrCmd = 0;

    /// @todo move to its proper home!
    ps2mR3SetDriverState(pThisCC, true);
}

#endif /* IN_RING3 */

static void ps2mSetRate(PPS2M pThis, uint8_t rate)
{
    Assert(rate);
    pThis->uThrottleDelay = rate ? 1000 / rate : 0;
    pThis->u8SampleRate = rate;
    LogFlowFunc(("Sampling rate %u, throttle delay %u ms\n", pThis->u8SampleRate, pThis->uThrottleDelay));
}

static void ps2mSetDefaults(PPS2M pThis)
{
    LogFlowFunc(("Set mouse defaults\n"));
    /* Standard protocol, reporting disabled, resolution 2, 1:1 scaling. */
    pThis->enmProtocol  = PS2M_PROTO_PS2STD;
    pThis->u8State      = 0;
    pThis->u8Resolution = 2;

    /* Sample rate 100 reports per second. */
    ps2mSetRate(pThis, 100);

    /* Event queue, eccumulators, and button status bits are cleared. */
    PS2Q_CLEAR(&pThis->evtQ);
    pThis->iAccumX = pThis->iAccumY = pThis->iAccumZ = pThis->iAccumW = pThis->fAccumB = 0;
}

/* Handle the sampling rate 'knock' sequence which selects protocol. */
static void ps2mRateProtocolKnock(PPS2M pThis, uint8_t rate)
{
    PS2M_PROTO          enmOldProtocol = pThis->enmProtocol;
    LogFlowFunc(("rate=%u\n", rate));

    switch (pThis->enmKnockState)
    {
    case PS2M_KNOCK_INITIAL:
        if (rate == 200)
            pThis->enmKnockState = PS2M_KNOCK_1ST;
        break;
    case PS2M_KNOCK_1ST:
        if (rate == 100)
            pThis->enmKnockState = PS2M_KNOCK_IMPS2_2ND;
        else if (rate == 200)
            pThis->enmKnockState = PS2M_KNOCK_IMEX_2ND;
        else if (rate == 80)
            pThis->enmKnockState = PS2M_KNOCK_IMEX_HORZ_2ND;
        else
            pThis->enmKnockState = PS2M_KNOCK_INITIAL;
        break;
    case PS2M_KNOCK_IMPS2_2ND:
        if (rate == 80)
        {
            pThis->enmProtocol = PS2M_PROTO_IMPS2;
            LogRelFlow(("PS2M: Switching mouse to ImPS/2 protocol.\n"));
        }
        pThis->enmKnockState = PS2M_KNOCK_INITIAL;
        break;
    case PS2M_KNOCK_IMEX_2ND:
        if (rate == 80)
        {
            pThis->enmProtocol = PS2M_PROTO_IMEX;
            LogRelFlow(("PS2M: Switching mouse to ImEx protocol.\n"));
        }
        pThis->enmKnockState = PS2M_KNOCK_INITIAL;
        break;
    case PS2M_KNOCK_IMEX_HORZ_2ND:
        if (rate == 40)
        {
            pThis->enmProtocol = PS2M_PROTO_IMEX_HORZ;
            LogRelFlow(("PS2M: Switching mouse ImEx with horizontal scrolling.\n"));
        }
        RT_FALL_THRU();
    default:
        pThis->enmKnockState = PS2M_KNOCK_INITIAL;
    }

    /* If the protocol changed, throw away any queued input because it now
     * has the wrong format, which could severely confuse the guest.
     */
    if (enmOldProtocol != pThis->enmProtocol)
        PS2Q_CLEAR(&pThis->evtQ);
}

/* Three-button event mask. */
#define PS2M_STD_BTN_MASK   (RT_BIT(0) | RT_BIT(1) | RT_BIT(2))
/* ImEx button 4/5 event mask. */
#define PS2M_IMEX_BTN_MASK  (RT_BIT(3) | RT_BIT(4))

/** Report accumulated movement and button presses, then clear the accumulators. */
static void ps2mReportAccumulatedEvents(PPS2M pThis, PPS2QHDR pQHdr, size_t cQElements, uint8_t *pbQElements, bool fAccumBtns)
{
    uint32_t    fBtnState = fAccumBtns ? pThis->fAccumB : pThis->fCurrB;
    uint8_t     val;
    int         dX, dY, dZ, dW;

    LogFlowFunc(("cQElements=%zu, fAccumBtns=%RTbool\n", cQElements, fAccumBtns));

    /* Clamp the accumulated delta values to the allowed range. */
    dX = RT_MIN(RT_MAX(pThis->iAccumX, -255), 255);
    dY = RT_MIN(RT_MAX(pThis->iAccumY, -255), 255);

    /* Start with the sync bit and buttons 1-3. */
    val = RT_BIT(3) | (fBtnState & PS2M_STD_BTN_MASK);
    /* Set the X/Y sign bits. */
    if (dX < 0)
        val |= RT_BIT(4);
    if (dY < 0)
        val |= RT_BIT(5);

    /* Send the standard 3-byte packet (always the same). */
    LogFlowFunc(("Queuing standard 3-byte packet\n"));
    PS2CmnInsertQueue(pQHdr, cQElements, pbQElements, val);
    PS2CmnInsertQueue(pQHdr, cQElements, pbQElements, dX);
    PS2CmnInsertQueue(pQHdr, cQElements, pbQElements, dY);

    /* Add fourth byte if an extended protocol is in use. */
    if (pThis->enmProtocol > PS2M_PROTO_PS2STD)
    {
        /* Start out with 4-bit dZ range. */
        dZ = RT_MIN(RT_MAX(pThis->iAccumZ, -8), 7);

        if (pThis->enmProtocol == PS2M_PROTO_IMPS2)
        {
            /* NB: Only uses 4-bit dZ range, despite using a full byte. */
            LogFlowFunc(("Queuing ImPS/2 last byte\n"));
            PS2CmnInsertQueue(pQHdr, cQElements, pbQElements, dZ);
            pThis->iAccumZ -= dZ;
        }
        else if (pThis->enmProtocol == PS2M_PROTO_IMEX)
        {
           /* Z value uses 4 bits; buttons 4/5 in bits 4 and 5. */
           val  = (fBtnState & PS2M_IMEX_BTN_MASK) << 1;
           val |= dZ & 0x0f;
           pThis->iAccumZ -= dZ;
           LogFlowFunc(("Queuing ImEx last byte\n"));
           PS2CmnInsertQueue(pQHdr, cQElements, pbQElements, val);
        }
        else
        {
            Assert((pThis->enmProtocol == PS2M_PROTO_IMEX_HORZ));
            /* With ImEx + horizontal reporting, prioritize buttons 4/5. */
            if (pThis->iAccumZ || pThis->iAccumW)
            {
               /* ImEx + horizontal reporting Horizontal scroll has
                * precedence over vertical. Buttons cannot be reported
                * this way.
                */
               if (pThis->iAccumW)
               {
                  dW = RT_MIN(RT_MAX(pThis->iAccumW, -32), 31);
                  val = (dW & 0x3F) | 0x40;
                  pThis->iAccumW -= dW;
               }
               else
               {
                  Assert(pThis->iAccumZ);
                  /* We can use 6-bit dZ range. Wow! */
                  dZ = RT_MIN(RT_MAX(pThis->iAccumZ, -32), 31);
                  val = (dZ & 0x3F) | 0x80;
                  pThis->iAccumZ -= dZ;
               }
            }
            else
            {
               /* Just Buttons 4/5 in bits 4 and 5. No scrolling. */
               val = (fBtnState & PS2M_IMEX_BTN_MASK) << 1;
            }
            LogFlowFunc(("Queuing ImEx+horz last byte\n"));
            PS2CmnInsertQueue(pQHdr, cQElements, pbQElements, val);
        }
    }

    /* Clear the movement accumulators, but not necessarily button state. */
    pThis->iAccumX = pThis->iAccumY = 0;
    /* Clear accumulated button state only when it's being used. */
    if (fAccumBtns)
    {
        pThis->fReportedB = pThis->fCurrB | pThis->fAccumB;
        pThis->fAccumB    = 0;
    }
}


/* Determine whether a reporting rate is one of the valid ones. */
static bool ps2mIsRateSupported(uint8_t rate)
{
    static uint8_t  aValidRates[] = { 10, 20, 40, 60, 80, 100, 200 };
    size_t          i;
    bool            fValid = false;

    for (i = 0; i < RT_ELEMENTS(aValidRates); ++i)
        if (aValidRates[i] == rate)
        {
            fValid = true;
            break;
        }

   return fValid;
}


/**
 * The keyboard controller disabled the auxiliary serial line.
 *
 * @param   pThis   The PS/2 auxiliary device shared instance data.
 */
void PS2MLineDisable(PPS2M pThis)
{
    LogFlowFunc(("Disabling mouse serial line\n"));

    pThis->fLineDisabled = true;
}

/**
 * The keyboard controller enabled the auxiliary serial line.
 *
 * @param   pThis   The PS/2 auxiliary device shared instance data.
 */
void PS2MLineEnable(PPS2M pThis)
{
    LogFlowFunc(("Enabling mouse serial line\n"));

    pThis->fLineDisabled = false;

    /* If there was anything in the input queue,
     * consider it lost and throw it away.
     */
    PS2Q_CLEAR(&pThis->evtQ);
}


/**
 * Receive and process a byte sent by the keyboard controller.
 *
 * @param   pDevIns The device instance.
 * @param   pThis   The PS/2 auxiliary device shared instance data.
 * @param   cmd     The command (or data) byte.
 */
int PS2MByteToAux(PPDMDEVINS pDevIns, PPS2M pThis, uint8_t cmd)
{
    uint8_t u8Val;
    bool    fHandled = true;

    LogFlowFunc(("cmd=0x%02X, active cmd=0x%02X\n", cmd, pThis->u8CurrCmd));

    if (pThis->enmMode == AUX_MODE_RESET)
        /* In reset mode, do not respond at all. */
        return VINF_SUCCESS;

    /* If there's anything left in the command response queue, trash it. */
    PS2Q_CLEAR(&pThis->cmdQ);

    if (pThis->enmMode == AUX_MODE_WRAP)
    {
        /* In wrap mode, bounce most data right back.*/
        if (cmd == ACMD_RESET || cmd == ACMD_RESET_WRAP)
            ;   /* Handle as regular commands. */
        else
        {
            PS2Q_INSERT(&pThis->cmdQ, cmd);
            return VINF_SUCCESS;
        }
    }

#ifndef IN_RING3
    /* Reset, Enable, and Set Default commands must be run in R3. */
    if (cmd == ACMD_RESET || cmd == ACMD_ENABLE || cmd == ACMD_SET_DEFAULT)
        return VINF_IOM_R3_IOPORT_WRITE;
#endif

    switch (cmd)
    {
        case ACMD_SET_SCALE_11:
            pThis->u8State &= ~AUX_STATE_SCALING;
            PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
            pThis->u8CurrCmd = 0;
            break;
        case ACMD_SET_SCALE_21:
            pThis->u8State |= AUX_STATE_SCALING;
            PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
            pThis->u8CurrCmd = 0;
            break;
        case ACMD_REQ_STATUS:
            /* Report current status, sample rate, and resolution. */
            u8Val  = (pThis->u8State & AUX_STATE_EXTERNAL) | (pThis->fCurrB & PS2M_STD_BTN_MASK);
            PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
            PS2Q_INSERT(&pThis->cmdQ, u8Val);
            PS2Q_INSERT(&pThis->cmdQ, pThis->u8Resolution);
            PS2Q_INSERT(&pThis->cmdQ, pThis->u8SampleRate);
            pThis->u8CurrCmd = 0;
            break;
        case ACMD_SET_STREAM:
            pThis->u8State &= ~AUX_STATE_REMOTE;
            PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
            pThis->u8CurrCmd = 0;
            break;
        case ACMD_READ_REMOTE:
            PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
            ps2mReportAccumulatedEvents(pThis, &pThis->cmdQ.Hdr, RT_ELEMENTS(pThis->cmdQ.abQueue), pThis->cmdQ.abQueue, false);
            pThis->u8CurrCmd = 0;
            break;
        case ACMD_RESET_WRAP:
            pThis->enmMode = AUX_MODE_STD;
            /* NB: Stream mode reporting remains disabled! */
            PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
            pThis->u8CurrCmd = 0;
            break;
        case ACMD_SET_WRAP:
            pThis->enmMode = AUX_MODE_WRAP;
            pThis->u8State &= ~AUX_STATE_ENABLED;
            PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
            pThis->u8CurrCmd = 0;
            break;
        case ACMD_SET_REMOTE:
            pThis->u8State |= AUX_STATE_REMOTE;
            PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
            pThis->u8CurrCmd = 0;
            break;
        case ACMD_READ_ID:
            PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
            /* ImEx + horizontal is protocol 4, just like plain ImEx. */
            u8Val = pThis->enmProtocol == PS2M_PROTO_IMEX_HORZ ? PS2M_PROTO_IMEX : pThis->enmProtocol;
            PS2Q_INSERT(&pThis->cmdQ, u8Val);
            pThis->u8CurrCmd = 0;
            break;
        case ACMD_ENABLE:
            pThis->u8State |= AUX_STATE_ENABLED;
#ifdef IN_RING3
            ps2mR3SetDriverState(&PDMDEVINS_2_DATA_CC(pDevIns, PKBDSTATER3)->Aux, true);
#else
            AssertLogRelMsgFailed(("Invalid ACMD_ENABLE outside R3!\n"));
#endif
            PS2Q_CLEAR(&pThis->evtQ);
            PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
            pThis->u8CurrCmd = 0;
            break;
        case ACMD_DISABLE:
            pThis->u8State &= ~AUX_STATE_ENABLED;
            PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
            pThis->u8CurrCmd = 0;
            break;
        case ACMD_SET_DEFAULT:
            ps2mSetDefaults(pThis);
            PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
            pThis->u8CurrCmd = 0;
            break;
        case ACMD_RESEND:
            pThis->u8CurrCmd = 0;
            break;
        case ACMD_RESET:
            ps2mSetDefaults(pThis);
            /// @todo reset more?
            pThis->u8CurrCmd = cmd;
            pThis->enmMode   = AUX_MODE_RESET;
            PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
            if (pThis->fDelayReset)
                /* Slightly delay reset completion; it might take hundreds of ms. */
                PDMDevHlpTimerSetMillies(pDevIns, pThis->hDelayTimer, 1);
            else
#ifdef IN_RING3
                ps2mR3Reset(pThis, &PDMDEVINS_2_DATA_CC(pDevIns, PKBDSTATER3)->Aux);
#else
                AssertLogRelMsgFailed(("Invalid ACMD_RESET outside R3!\n"));
#endif
            break;
        /* The following commands need a parameter. */
        case ACMD_SET_RES:
        case ACMD_SET_SAMP_RATE:
            PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
            pThis->u8CurrCmd = cmd;
            break;
        default:
            /* Sending a command instead of a parameter starts the new command. */
            switch (pThis->u8CurrCmd)
            {
                case ACMD_SET_RES:
                    if (cmd < 4)    /* Valid resolutions are 0-3. */
                    {
                        pThis->u8Resolution = cmd;
                        pThis->u8State &= ~AUX_STATE_RES_ERR;
                        PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
                        pThis->u8CurrCmd = 0;
                    }
                    else
                    {
                        /* Bad resolution. Reply with Resend or Error. */
                        if (pThis->u8State & AUX_STATE_RES_ERR)
                        {
                            pThis->u8State &= ~AUX_STATE_RES_ERR;
                            PS2Q_INSERT(&pThis->cmdQ, ARSP_ERROR);
                            pThis->u8CurrCmd = 0;
                        }
                        else
                        {
                            pThis->u8State |= AUX_STATE_RES_ERR;
                            PS2Q_INSERT(&pThis->cmdQ, ARSP_RESEND);
                            /* NB: Current command remains unchanged. */
                        }
                    }
                    break;
                case ACMD_SET_SAMP_RATE:
                    if (ps2mIsRateSupported(cmd))
                    {
                        pThis->u8State &= ~AUX_STATE_RATE_ERR;
                        ps2mSetRate(pThis, cmd);
                        ps2mRateProtocolKnock(pThis, cmd);
                        PS2Q_INSERT(&pThis->cmdQ, ARSP_ACK);
                        pThis->u8CurrCmd = 0;
                    }
                    else
                    {
                        /* Bad rate. Reply with Resend or Error. */
                        if (pThis->u8State & AUX_STATE_RATE_ERR)
                        {
                            pThis->u8State &= ~AUX_STATE_RATE_ERR;
                            PS2Q_INSERT(&pThis->cmdQ, ARSP_ERROR);
                            pThis->u8CurrCmd = 0;
                        }
                        else
                        {
                            pThis->u8State |= AUX_STATE_RATE_ERR;
                            PS2Q_INSERT(&pThis->cmdQ, ARSP_RESEND);
                            /* NB: Current command remains unchanged. */
                        }
                    }
                    break;
                default:
                    fHandled = false;
            }
            /* Fall through only to handle unrecognized commands. */
            if (fHandled)
                break;
            RT_FALL_THRU();

        case ACMD_INVALID_1:
        case ACMD_INVALID_2:
        case ACMD_INVALID_3:
        case ACMD_INVALID_4:
        case ACMD_INVALID_5:
        case ACMD_INVALID_6:
        case ACMD_INVALID_7:
        case ACMD_INVALID_8:
        case ACMD_INVALID_9:
        case ACMD_INVALID_10:
            Log(("Unsupported command 0x%02X!\n", cmd));
            PS2Q_INSERT(&pThis->cmdQ, ARSP_RESEND);
            pThis->u8CurrCmd = 0;
            break;
    }
    LogFlowFunc(("Active cmd now 0x%02X; updating interrupts\n", pThis->u8CurrCmd));
    return VINF_SUCCESS;
}

/**
 * Send a byte (packet data or command response) to the keyboard controller.
 *
 * @returns VINF_SUCCESS or VINF_TRY_AGAIN.
 * @param   pThis   The PS/2 auxiliary device shared instance data.
 * @param   pb      Where to return the byte we've read.
 * @remarks Caller must have entered the device critical section.
 */
int PS2MByteFromAux(PPS2M pThis, uint8_t *pb)
{
    int         rc;

    AssertPtr(pb);

    /* Anything in the command queue has priority over data
     * in the event queue. Additionally, packet data are
     * blocked if a command is currently in progress, even if
     * the command queue is empty.
     */
    /// @todo Probably should flush/not fill queue if stream mode reporting disabled?!
    rc = PS2Q_REMOVE(&pThis->cmdQ, pb);
    if (rc != VINF_SUCCESS && !pThis->u8CurrCmd && (pThis->u8State & AUX_STATE_ENABLED))
        rc = PS2Q_REMOVE(&pThis->evtQ, pb);

    LogFlowFunc(("mouse sends 0x%02x (%svalid data)\n", *pb, rc == VINF_SUCCESS ? "" : "not "));

    return rc;
}

#ifdef IN_RING3

/** Is there any state change to send as events to the guest? */
static uint32_t ps2mR3HaveEvents(PPS2M pThis)
{
/** @todo r=bird: Why is this returning uint32_t when you're calculating a
 *        boolean value here?  Also, it's a predicate function... */
    return   pThis->iAccumX || pThis->iAccumY || pThis->iAccumZ || pThis->iAccumW
           || ((pThis->fCurrB | pThis->fAccumB) != pThis->fReportedB);
}

/**
 * @callback_method_impl{FNTMTIMERDEV,
 * Event rate throttling timer to emulate the auxiliary device sampling rate.}
 */
static DECLCALLBACK(void) ps2mR3ThrottleTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PPS2M       pThis = (PS2M *)pvUser;
    uint32_t    uHaveEvents;
    Assert(hTimer == pThis->hThrottleTimer);
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, pDevIns->pCritSectRoR3));

    /* If more movement is accumulated, report it and restart the timer. */
    uHaveEvents = ps2mR3HaveEvents(pThis);
    LogFlowFunc(("Have%s events\n", uHaveEvents ? "" : " no"));

    if (uHaveEvents)
    {
        /* Report accumulated data, poke the KBC, and start the timer. */
        ps2mReportAccumulatedEvents(pThis, &pThis->evtQ.Hdr, RT_ELEMENTS(pThis->evtQ.abQueue), pThis->evtQ.abQueue, true);
        KBCUpdateInterrupts(pDevIns);
        PDMDevHlpTimerSetMillies(pDevIns, hTimer, pThis->uThrottleDelay);
    }
    else
        pThis->fThrottleActive = false;
}

/**
 * @callback_method_impl{FNTMTIMERDEV}
 *
 * The auxiliary device reset is specified to take up to about 500 milliseconds.
 * We need to delay sending the result to the host for at least a tiny little
 * while.
 */
static DECLCALLBACK(void) ps2mR3DelayTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PPS2M   pThis   = &PDMDEVINS_2_DATA(pDevIns, PKBDSTATE)->Aux;
    PPS2MR3 pThisCC = &PDMDEVINS_2_DATA_CC(pDevIns, PKBDSTATER3)->Aux;
    RT_NOREF(pvUser, hTimer);

    LogFlowFunc(("Delay timer: cmd %02X\n", pThis->u8CurrCmd));

    Assert(pThis->u8CurrCmd == ACMD_RESET);
    ps2mR3Reset(pThis, pThisCC);

    /// @todo Might want a PS2MCompleteCommand() to push last response, clear command, and kick the KBC...
    /* Give the KBC a kick. */
    KBCUpdateInterrupts(pDevIns);
}


/**
 * Debug device info handler. Prints basic auxiliary device state.
 *
 * @param   pDevIns     Device instance which registered the info.
 * @param   pHlp        Callback functions for doing output.
 * @param   pszArgs     Argument string. Optional and specific to the handler.
 */
static DECLCALLBACK(void) ps2mR3InfoState(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    static const char * const s_apcszModes[] = { "normal", "reset", "wrap" };
    static const char * const s_apcszProtocols[] = { "PS/2", NULL, NULL, "ImPS/2", "ImEx", "ImEx+horizontal" };
    PKBDSTATE   pParent = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);
    PPS2M       pThis   = &pParent->Aux;
    NOREF(pszArgs);

    Assert(pThis->enmMode < RT_ELEMENTS(s_apcszModes));
    pHlp->pfnPrintf(pHlp, "PS/2 mouse state: %s, %s mode, reporting %s, serial line %s\n",
                    s_apcszModes[pThis->enmMode],
                    pThis->u8State & AUX_STATE_REMOTE  ? "remote"  : "stream",
                    pThis->u8State & AUX_STATE_ENABLED ? "enabled" : "disabled",
                    pThis->fLineDisabled ? "disabled" : "enabled");
    Assert(pThis->enmProtocol < RT_ELEMENTS(s_apcszProtocols));
    pHlp->pfnPrintf(pHlp, "Protocol: %s, scaling %u:1\n",
                    s_apcszProtocols[pThis->enmProtocol],
                    pThis->u8State & AUX_STATE_SCALING ? 2 : 1);
    pHlp->pfnPrintf(pHlp, "Active command %02X\n", pThis->u8CurrCmd);
    pHlp->pfnPrintf(pHlp, "Sampling rate %u reports/sec, resolution %u counts/mm\n",
                    pThis->u8SampleRate, 1 << pThis->u8Resolution);
    pHlp->pfnPrintf(pHlp, "Command queue: %d items (%d max)\n",
                    PS2Q_COUNT(&pThis->cmdQ), PS2Q_SIZE(&pThis->cmdQ));
    pHlp->pfnPrintf(pHlp, "Event queue  : %d items (%d max)\n",
                    PS2Q_COUNT(&pThis->evtQ), PS2Q_SIZE(&pThis->evtQ));
}


/* -=-=-=-=-=- Mouse: IMousePort  -=-=-=-=-=- */

/**
 * Mouse event handler.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The PS/2 auxiliary device shared instance data.
 * @param   dx          X direction movement delta.
 * @param   dy          Y direction movement delta.
 * @param   dz          Z (vertical scroll) movement delta.
 * @param   dw          W (horizontal scroll) movement delta.
 * @param   fButtons    Depressed button mask.
 */
static int ps2mR3PutEventWorker(PPDMDEVINS pDevIns, PPS2M pThis, int32_t dx, int32_t dy, int32_t dz, int32_t dw, uint32_t fButtons)
{
    LogFlowFunc(("dx=%d, dy=%d, dz=%d, dw=%d, fButtons=%X\n", dx, dy, dz, dw, fButtons));

    /* Update internal accumulators and button state. Ignore any buttons beyond 5. */
    pThis->iAccumX += dx;
    pThis->iAccumY += dy;
    pThis->iAccumZ += dz;
    pThis->iAccumW += dw;
    pThis->fCurrB   = fButtons & (PS2M_STD_BTN_MASK | PS2M_IMEX_BTN_MASK);
    pThis->fAccumB |= pThis->fCurrB;

    /* Ditch accumulated data that can't be reported by the current protocol.
     * This avoids sending phantom empty reports when un-reportable events
     * are received.
     */
    if (pThis->enmProtocol < PS2M_PROTO_IMEX_HORZ)
        pThis->iAccumW = 0; /* No horizontal scroll. */

    if (pThis->enmProtocol < PS2M_PROTO_IMEX)
    {
        pThis->fAccumB &= PS2M_STD_BTN_MASK;   /* Only buttons 1-3. */
        pThis->fCurrB  &= PS2M_STD_BTN_MASK;
    }

    if (pThis->enmProtocol < PS2M_PROTO_IMPS2)
        pThis->iAccumZ = 0; /* No vertical scroll. */

    /* Report the event (if any) and start the throttle timer unless it's already running. */
    if (!pThis->fThrottleActive && ps2mR3HaveEvents(pThis))
    {
        ps2mReportAccumulatedEvents(pThis, &pThis->evtQ.Hdr, RT_ELEMENTS(pThis->evtQ.abQueue), pThis->evtQ.abQueue, true);
        KBCUpdateInterrupts(pDevIns);
        pThis->fThrottleActive = true;
        PDMDevHlpTimerSetMillies(pDevIns, pThis->hThrottleTimer, pThis->uThrottleDelay);
    }

    return VINF_SUCCESS;
}


/* -=-=-=-=-=- Mouse: IMousePort  -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIMOUSEPORT,pfnPutEvent}
 */
static DECLCALLBACK(int) ps2mR3MousePort_PutEvent(PPDMIMOUSEPORT pInterface, int32_t dx, int32_t dy,
                                                  int32_t dz, int32_t dw, uint32_t fButtons)
{
    PPS2MR3     pThisCC = RT_FROM_MEMBER(pInterface, PS2MR3, Mouse.IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PPS2M       pThis   = &PDMDEVINS_2_DATA(pDevIns, PKBDSTATE)->Aux;
    int const   rcLock  = PDMDevHlpCritSectEnter(pDevIns, pDevIns->pCritSectRoR3, VERR_SEM_BUSY);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->pCritSectRoR3, rcLock);

    LogRelFlowFunc(("dX=%d dY=%d dZ=%d dW=%d buttons=%02X\n", dx, dy, dz, dw, fButtons));
    /* NB: The PS/2 Y axis direction is inverted relative to ours. */
    ps2mR3PutEventWorker(pDevIns, pThis, dx, -dy, dz, dw, fButtons);

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->pCritSectRoR3);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMOUSEPORT,pfnPutEventAbs}
 */
static DECLCALLBACK(int) ps2mR3MousePort_PutEventAbs(PPDMIMOUSEPORT pInterface, uint32_t x, uint32_t y,
                                                     int32_t dz, int32_t dw, uint32_t fButtons)
{
    AssertFailedReturn(VERR_NOT_SUPPORTED);
    NOREF(pInterface); NOREF(x); NOREF(y); NOREF(dz); NOREF(dw); NOREF(fButtons);
}

/**
 * @interface_method_impl{PDMIMOUSEPORT,pfnPutEventTouchScreen}
 */
static DECLCALLBACK(int) ps2mR3MousePort_PutEventMTAbs(PPDMIMOUSEPORT pInterface, uint8_t cContacts,
                                                       const uint64_t *pau64Contacts, uint32_t u32ScanTime)
{
    AssertFailedReturn(VERR_NOT_SUPPORTED);
    NOREF(pInterface); NOREF(cContacts); NOREF(pau64Contacts); NOREF(u32ScanTime);
}

/**
 * @interface_method_impl{PDMIMOUSEPORT,pfnPutEventTouchPad}
 */
static DECLCALLBACK(int) ps2mR3MousePort_PutEventMTRel(PPDMIMOUSEPORT pInterface, uint8_t cContacts,
                                                       const uint64_t *pau64Contacts, uint32_t u32ScanTime)
{
    AssertFailedReturn(VERR_NOT_SUPPORTED);
    NOREF(pInterface); NOREF(cContacts); NOREF(pau64Contacts); NOREF(u32ScanTime);
}


/* -=-=-=-=-=- Mouse: IBase  -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) ps2mR3QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPS2MR3 pThisCC = RT_FROM_MEMBER(pInterface, PS2MR3, Mouse.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->Mouse.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMOUSEPORT, &pThisCC->Mouse.IPort);
    return NULL;
}


/* -=-=-=-=-=- Device management -=-=-=-=-=- */

/**
 * Attach command.
 *
 * This is called to let the device attach to a driver for a
 * specified LUN.
 *
 * This is like plugging in the mouse after turning on the
 * system.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThisCC     The PS/2 auxiliary device instance data for ring-3.
 * @param   iLUN        The logical unit which is being detached.
 * @param   fFlags      Flags, combination of the PDMDEVATT_FLAGS_* \#defines.
 */
int PS2MR3Attach(PPDMDEVINS pDevIns, PPS2MR3 pThisCC, unsigned iLUN, uint32_t fFlags)
{
    int         rc;

    /* The LUN must be 1, i.e. mouse. */
    Assert(iLUN == 1);
    AssertMsgReturn(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
                    ("PS/2 mouse does not support hotplugging\n"),
                    VERR_INVALID_PARAMETER);

    LogFlowFunc(("iLUN=%d\n", iLUN));

    rc = PDMDevHlpDriverAttach(pDevIns, iLUN, &pThisCC->Mouse.IBase, &pThisCC->Mouse.pDrvBase, "Mouse Port");
    if (RT_SUCCESS(rc))
    {
        pThisCC->Mouse.pDrv = PDMIBASE_QUERY_INTERFACE(pThisCC->Mouse.pDrvBase, PDMIMOUSECONNECTOR);
        if (!pThisCC->Mouse.pDrv)
        {
            AssertLogRelMsgFailed(("LUN #1 doesn't have a mouse interface! rc=%Rrc\n", rc));
            rc = VERR_PDM_MISSING_INTERFACE;
        }
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
    {
        Log(("%s/%d: warning: no driver attached to LUN #1!\n", pDevIns->pReg->szName, pDevIns->iInstance));
        rc = VINF_SUCCESS;
    }
    else
        AssertLogRelMsgFailed(("Failed to attach LUN #1! rc=%Rrc\n", rc));

    return rc;
}

void PS2MR3SaveState(PPDMDEVINS pDevIns, PPS2M pThis, PSSMHANDLE pSSM)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;
    LogFlowFunc(("Saving PS2M state\n"));

    /* Save the core auxiliary device state. */
    pHlp->pfnSSMPutU8(pSSM, pThis->u8State);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8SampleRate);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8Resolution);
    pHlp->pfnSSMPutU8(pSSM, pThis->u8CurrCmd);
    pHlp->pfnSSMPutU8(pSSM, pThis->enmMode);
    pHlp->pfnSSMPutU8(pSSM, pThis->enmProtocol);
    pHlp->pfnSSMPutU8(pSSM, pThis->enmKnockState);

    /* Save the command and event queues. */
    PS2Q_SAVE(pHlp, pSSM, &pThis->cmdQ);
    PS2Q_SAVE(pHlp, pSSM, &pThis->evtQ);

    /* Save the command delay timer. Note that the rate throttling
     * timer is *not* saved.
     */
    PDMDevHlpTimerSave(pDevIns, pThis->hDelayTimer, pSSM);
}

int PS2MR3LoadState(PPDMDEVINS pDevIns, PPS2M pThis, PPS2MR3 pThisCC, PSSMHANDLE pSSM, uint32_t uVersion)
{
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;
    uint8_t         u8;
    int             rc;

    NOREF(uVersion);
    LogFlowFunc(("Loading PS2M state version %u\n", uVersion));

    /* Load the basic auxiliary device state. */
    pHlp->pfnSSMGetU8(pSSM, &pThis->u8State);
    pHlp->pfnSSMGetU8(pSSM, &pThis->u8SampleRate);
    pHlp->pfnSSMGetU8(pSSM, &pThis->u8Resolution);
    pHlp->pfnSSMGetU8(pSSM, &pThis->u8CurrCmd);
    pHlp->pfnSSMGetU8(pSSM, &u8);
    pThis->enmMode       = (PS2M_MODE)u8;
    pHlp->pfnSSMGetU8(pSSM, &u8);
    pThis->enmProtocol   = (PS2M_PROTO)u8;
    pHlp->pfnSSMGetU8(pSSM, &u8);
    pThis->enmKnockState = (PS2M_KNOCK_STATE)u8;

    /* Load the command and event queues. */
    rc = PS2Q_LOAD(pHlp, pSSM, &pThis->cmdQ);
    AssertRCReturn(rc, rc);
    rc = PS2Q_LOAD(pHlp, pSSM, &pThis->evtQ);
    AssertRCReturn(rc, rc);

    /* Load the command delay timer, just in case. */
    rc = PDMDevHlpTimerLoad(pDevIns, pThis->hDelayTimer, pSSM);
    AssertRCReturn(rc, rc);

    /* Recalculate the throttling delay. */
    ps2mSetRate(pThis, pThis->u8SampleRate);

    ps2mR3SetDriverState(pThisCC, !!(pThis->u8State & AUX_STATE_ENABLED));

    return VINF_SUCCESS;
}

void PS2MR3FixupState(PPS2M pThis, PPS2MR3 pThisCC, uint8_t u8State, uint8_t u8Rate, uint8_t u8Proto)
{
    LogFlowFunc(("Fixing up old PS2M state version\n"));

    /* Load the basic auxiliary device state. */
    pThis->u8State      = u8State;
    pThis->u8SampleRate = u8Rate ? u8Rate : 40; /* In case it wasn't saved right. */
    pThis->enmProtocol  = (PS2M_PROTO)u8Proto;

    /* Recalculate the throttling delay. */
    ps2mSetRate(pThis, pThis->u8SampleRate);

    ps2mR3SetDriverState(pThisCC, !!(pThis->u8State & AUX_STATE_ENABLED));
}

void PS2MR3Reset(PPS2M pThis)
{
    LogFlowFunc(("Resetting PS2M\n"));

    pThis->u8CurrCmd         = 0;

    /* Clear the queues. */
    PS2Q_CLEAR(&pThis->cmdQ);
    ps2mSetDefaults(pThis);     /* Also clears event queue. */
}

int PS2MR3Construct(PPDMDEVINS pDevIns, PPS2M pThis, PPS2MR3 pThisCC)
{
    LogFlowFunc(("\n"));

    pThis->cmdQ.Hdr.pszDescR3 = "Aux Cmd";
    pThis->evtQ.Hdr.pszDescR3 = "Aux Evt";

#ifdef RT_STRICT
    ps2mR3TestAccumulation();
#endif

    /*
     * Initialize the state.
     */
    pThisCC->pDevIns                            = pDevIns;
    pThisCC->Mouse.IBase.pfnQueryInterface      = ps2mR3QueryInterface;
    pThisCC->Mouse.IPort.pfnPutEvent            = ps2mR3MousePort_PutEvent;
    pThisCC->Mouse.IPort.pfnPutEventAbs         = ps2mR3MousePort_PutEventAbs;
    pThisCC->Mouse.IPort.pfnPutEventTouchScreen = ps2mR3MousePort_PutEventMTAbs;
    pThisCC->Mouse.IPort.pfnPutEventTouchPad    = ps2mR3MousePort_PutEventMTRel;

    /*
     * Create the input rate throttling timer. Does not use virtual time!
     */
    int rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_REAL, ps2mR3ThrottleTimer, pThis,
                                  TMTIMER_FLAGS_DEFAULT_CRIT_SECT | TMTIMER_FLAGS_NO_RING0,
                                  "PS2M Throttle", &pThis->hThrottleTimer);
    AssertRCReturn(rc, rc);

    /*
     * Create the command delay timer.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, ps2mR3DelayTimer, pThis,
                              TMTIMER_FLAGS_DEFAULT_CRIT_SECT | TMTIMER_FLAGS_RING0, "PS2M Delay", &pThis->hDelayTimer);
    AssertRCReturn(rc, rc);

    /*
     * Register debugger info callbacks.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "ps2m", "Display PS/2 mouse state.", ps2mR3InfoState);

    /// @todo Where should we do this?
    ps2mR3SetDriverState(pThisCC, true);
    pThis->u8State = 0;
    pThis->enmMode = AUX_MODE_STD;

    return rc;
}

#endif

#if defined(RT_STRICT) && defined(IN_RING3)
/* -=-=-=-=-=- Test code  -=-=-=-=-=- */

/** Test the event accumulation mechanism which we use to delay events going
 * to the guest to one per 10ms (the default PS/2 mouse event rate).  This
 * test depends on ps2mR3PutEventWorker() not touching the timer if
 * This.fThrottleActive is true. */
/** @todo if we add any more tests it might be worth using a table of test
 * operations and checks. */
static void ps2mR3TestAccumulation(void)
{
    PS2M This;
    unsigned i;
    int rc;
    uint8_t b;

    RT_ZERO(This);
    This.u8State = AUX_STATE_ENABLED;
    This.fThrottleActive = true;
    This.cmdQ.Hdr.pszDescR3 = "Test Aux Cmd";
    This.evtQ.Hdr.pszDescR3 = "Test Aux Evt";
    /* Certain Windows touch pad drivers report a double tap as a press, then
     * a release-press-release all within a single 10ms interval.  Simulate
     * this to check that it is handled right. */
    ps2mR3PutEventWorker(NULL, &This, 0, 0, 0, 0, 1);
    if (ps2mR3HaveEvents(&This))
        ps2mReportAccumulatedEvents(&This, &This.evtQ.Hdr, RT_ELEMENTS(This.evtQ.abQueue), This.evtQ.abQueue, true);
    ps2mR3PutEventWorker(NULL, &This, 0, 0, 0, 0, 0);
    if (ps2mR3HaveEvents(&This))
        ps2mReportAccumulatedEvents(&This, &This.evtQ.Hdr, RT_ELEMENTS(This.evtQ.abQueue), This.evtQ.abQueue, true);
    ps2mR3PutEventWorker(NULL, &This, 0, 0, 0, 0, 1);
    ps2mR3PutEventWorker(NULL, &This, 0, 0, 0, 0, 0);
    if (ps2mR3HaveEvents(&This))
        ps2mReportAccumulatedEvents(&This, &This.evtQ.Hdr, RT_ELEMENTS(This.evtQ.abQueue), This.evtQ.abQueue, true);
    if (ps2mR3HaveEvents(&This))
        ps2mReportAccumulatedEvents(&This, &This.evtQ.Hdr, RT_ELEMENTS(This.evtQ.abQueue), This.evtQ.abQueue, true);
    for (i = 0; i < 12; ++i)
    {
        const uint8_t abExpected[] = { 9, 0, 0, 8, 0, 0, 9, 0, 0, 8, 0, 0};

        rc = PS2MByteFromAux(&This, &b);
        AssertRCSuccess(rc);
        Assert(b == abExpected[i]);
    }
    rc = PS2MByteFromAux(&This, &b);
    Assert(rc != VINF_SUCCESS);
    /* Button hold down during mouse drags was broken at some point during
     * testing fixes for the previous issue.  Test that that works. */
    ps2mR3PutEventWorker(NULL, &This, 0, 0, 0, 0, 1);
    if (ps2mR3HaveEvents(&This))
        ps2mReportAccumulatedEvents(&This, &This.evtQ.Hdr, RT_ELEMENTS(This.evtQ.abQueue), This.evtQ.abQueue, true);
    if (ps2mR3HaveEvents(&This))
        ps2mReportAccumulatedEvents(&This, &This.evtQ.Hdr, RT_ELEMENTS(This.evtQ.abQueue), This.evtQ.abQueue, true);
    for (i = 0; i < 3; ++i)
    {
        const uint8_t abExpected[] = { 9, 0, 0 };

        rc = PS2MByteFromAux(&This, &b);
        AssertRCSuccess(rc);
        Assert(b == abExpected[i]);
    }
    rc = PS2MByteFromAux(&This, &b);
    Assert(rc != VINF_SUCCESS);
}
#endif /* RT_STRICT && IN_RING3 */

