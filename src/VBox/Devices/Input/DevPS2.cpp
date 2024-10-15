/* $Id: DevPS2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevPS2 - PS/2 keyboard & mouse controller device.
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
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 * QEMU PC keyboard emulation (revision 1.12)
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_KBD
#include <VBox/vmm/pdmdev.h>
#include <VBox/AssertGuest.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>

#include "VBoxDD.h"
#include "DevPS2.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/* Do not remove this (unless eliminating the corresponding ifdefs), it will
 * cause instant triple faults when booting Windows VMs. */
#define TARGET_I386

#define PCKBD_SAVED_STATE_VERSION 8

/* debug PC keyboard */
#define DEBUG_KBD

/* debug PC keyboard : only mouse */
#define DEBUG_MOUSE

/*      Keyboard Controller Commands */
#define KBD_CCMD_READ_MODE      0x20    /* Read mode bits */
#define KBD_CCMD_WRITE_MODE     0x60    /* Write mode bits */
#define KBD_CCMD_GET_VERSION    0xA1    /* Get controller version */
#define KBD_CCMD_MOUSE_DISABLE  0xA7    /* Disable mouse interface */
#define KBD_CCMD_MOUSE_ENABLE   0xA8    /* Enable mouse interface */
#define KBD_CCMD_TEST_MOUSE     0xA9    /* Mouse interface test */
#define KBD_CCMD_SELF_TEST      0xAA    /* Controller self test */
#define KBD_CCMD_KBD_TEST       0xAB    /* Keyboard interface test */
#define KBD_CCMD_KBD_DISABLE    0xAD    /* Keyboard interface disable */
#define KBD_CCMD_KBD_ENABLE     0xAE    /* Keyboard interface enable */
#define KBD_CCMD_READ_INPORT    0xC0    /* read input port */
#define KBD_CCMD_READ_OUTPORT   0xD0    /* read output port */
#define KBD_CCMD_WRITE_OUTPORT  0xD1    /* write output port */
#define KBD_CCMD_WRITE_OBUF     0xD2
#define KBD_CCMD_WRITE_AUX_OBUF 0xD3    /* Write to output buffer as if
                                           initiated by the auxiliary device */
#define KBD_CCMD_WRITE_MOUSE    0xD4    /* Write the following byte to the mouse */
#define KBD_CCMD_DISABLE_A20    0xDD    /* HP vectra only ? */
#define KBD_CCMD_ENABLE_A20     0xDF    /* HP vectra only ? */
#define KBD_CCMD_READ_TSTINP    0xE0    /* Read test inputs T0, T1 */
#define KBD_CCMD_RESET_ALT      0xF0
#define KBD_CCMD_RESET          0xFE

/* Status Register Bits */
#define KBD_STAT_OBF            0x01    /* Keyboard output buffer full */
#define KBD_STAT_IBF            0x02    /* Keyboard input buffer full */
#define KBD_STAT_SELFTEST       0x04    /* Self test successful */
#define KBD_STAT_CMD            0x08    /* Last write was a command write (0=data) */
#define KBD_STAT_UNLOCKED       0x10    /* Zero if keyboard locked */
#define KBD_STAT_MOUSE_OBF      0x20    /* Mouse output buffer full */
#define KBD_STAT_GTO            0x40    /* General receive/xmit timeout */
#define KBD_STAT_PERR           0x80    /* Parity error */

/* Controller Mode Register Bits */
#define KBD_MODE_KBD_INT        0x01    /* Keyboard data generate IRQ1 */
#define KBD_MODE_MOUSE_INT      0x02    /* Mouse data generate IRQ12 */
#define KBD_MODE_SYS            0x04    /* The system flag (?) */
#define KBD_MODE_NO_KEYLOCK     0x08    /* The keylock doesn't affect the keyboard if set */
#define KBD_MODE_DISABLE_KBD    0x10    /* Disable keyboard interface */
#define KBD_MODE_DISABLE_MOUSE  0x20    /* Disable mouse interface */
#define KBD_MODE_KCC            0x40    /* Scan code conversion to PC format */
#define KBD_MODE_RFU            0x80


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** AT to PC scancode translator state.  */
typedef enum
{
    XS_IDLE,    /**< Starting state. */
    XS_BREAK,   /**< F0 break byte was received. */
    XS_HIBIT    /**< Break code still active. */
} xlat_state_t;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/* Table used by the keyboard controller to optionally translate the incoming
 * keyboard data. Note that the translation is designed for essentially taking
 * Scan Set 2 input and producing Scan Set 1 output, but can be turned on and
 * off regardless of what the keyboard is sending.
 */
static uint8_t const g_aAT2PC[128] =
{
    0xff,0x43,0x41,0x3f,0x3d,0x3b,0x3c,0x58,0x64,0x44,0x42,0x40,0x3e,0x0f,0x29,0x59,
    0x65,0x38,0x2a,0x70,0x1d,0x10,0x02,0x5a,0x66,0x71,0x2c,0x1f,0x1e,0x11,0x03,0x5b,
    0x67,0x2e,0x2d,0x20,0x12,0x05,0x04,0x5c,0x68,0x39,0x2f,0x21,0x14,0x13,0x06,0x5d,
    0x69,0x31,0x30,0x23,0x22,0x15,0x07,0x5e,0x6a,0x72,0x32,0x24,0x16,0x08,0x09,0x5f,
    0x6b,0x33,0x25,0x17,0x18,0x0b,0x0a,0x60,0x6c,0x34,0x35,0x26,0x27,0x19,0x0c,0x61,
    0x6d,0x73,0x28,0x74,0x1a,0x0d,0x62,0x6e,0x3a,0x36,0x1c,0x1b,0x75,0x2b,0x63,0x76,
    0x55,0x56,0x77,0x78,0x79,0x7a,0x0e,0x7b,0x7c,0x4f,0x7d,0x4b,0x47,0x7e,0x7f,0x6f,
    0x52,0x53,0x50,0x4c,0x4d,0x48,0x01,0x45,0x57,0x4e,0x51,0x4a,0x37,0x49,0x46,0x54
};



/**
 * Convert an AT (Scan Set 2) scancode to PC (Scan Set 1).
 *
 * @param state         Current state of the translator
 *                      (xlat_state_t).
 * @param scanIn        Incoming scan code.
 * @param pScanOut      Pointer to outgoing scan code. The
 *                      contents are only valid if returned
 *                      state is not XS_BREAK.
 *
 * @return xlat_state_t New state of the translator.
 */
static int32_t kbcXlateAT2PC(int32_t state, uint8_t scanIn, uint8_t *pScanOut)
{
    uint8_t     scan_in;
    uint8_t     scan_out;

    Assert(pScanOut);
    Assert(state == XS_IDLE || state == XS_BREAK || state == XS_HIBIT);

    /* Preprocess the scan code for a 128-entry translation table. */
    if (scanIn == 0x83)         /* Check for F7 key. */
        scan_in = 0x02;
    else if (scanIn == 0x84)    /* Check for SysRq key. */
        scan_in = 0x7f;
    else
        scan_in = scanIn;

    /* Values 0x80 and above are passed through, except for 0xF0
     * which indicates a key release.
     */
    if (scan_in < 0x80)
    {
        scan_out = g_aAT2PC[scan_in];
        /* Turn into break code if required. */
        if (state == XS_BREAK || state == XS_HIBIT)
            scan_out |= 0x80;

        state = XS_IDLE;
    }
    else
    {
        /* NB: F0 E0 10 will be translated to E0 E5 (high bit set on last byte)! */
        if (scan_in == 0xF0)        /* Check for break code. */
            state = XS_BREAK;
        else if (state == XS_BREAK)
            state = XS_HIBIT;       /* Remember the break bit. */
        scan_out = scan_in;
    }
    LogFlowFunc(("scan code %02X translated to %02X; new state is %d\n",
                 scanIn, scan_out, state));

    *pScanOut = scan_out;
    return state;
}


/** update irq and KBD_STAT_[MOUSE_]OBF */
static void kbd_update_irq(PPDMDEVINS pDevIns, PKBDSTATE s)
{
    int irq12_level, irq1_level;
    uint8_t val;

    irq1_level = 0;
    irq12_level = 0;

    /* Determine new OBF state, but only if OBF is clear. If OBF was already
     * set, we cannot risk changing the event type after an ISR potentially
     * started executing! Only kbd_read_data() clears the OBF bits.
     */
    if (!(s->status & KBD_STAT_OBF)) {
        s->status &= ~KBD_STAT_MOUSE_OBF;
        /* Keyboard data has priority if both kbd and aux data is available. */
        if (!(s->mode & KBD_MODE_DISABLE_KBD) && PS2KByteFromKbd(pDevIns, &s->Kbd, &val) == VINF_SUCCESS)
        {
            bool    fHaveData = true;

            /* If scancode translation is on (it usually is), there's more work to do. */
            if (s->translate)
            {
                uint8_t     xlated_val;

                s->xlat_state = kbcXlateAT2PC(s->xlat_state, val, &xlated_val);
                val = xlated_val;

                /* If the translation state is XS_BREAK, there's nothing to report
                 * and we keep going until the state changes or there's no more data.
                 */
                while (s->xlat_state == XS_BREAK && PS2KByteFromKbd(pDevIns, &s->Kbd, &val) == VINF_SUCCESS)
                {
                    s->xlat_state = kbcXlateAT2PC(s->xlat_state, val, &xlated_val);
                    val = xlated_val;
                }
                /* This can happen if the last byte in the queue is F0... */
                if (s->xlat_state == XS_BREAK)
                    fHaveData = false;
            }
            if (fHaveData)
            {
                s->dbbout = val;
                s->status |= KBD_STAT_OBF;
            }
        }
        else if (!(s->mode & KBD_MODE_DISABLE_MOUSE) && PS2MByteFromAux(&s->Aux, &val) == VINF_SUCCESS)
        {
            s->dbbout = val;
            s->status |= KBD_STAT_OBF | KBD_STAT_MOUSE_OBF;
        }
    }
    /* Determine new IRQ state. */
    if (s->status & KBD_STAT_OBF) {
        if (s->status & KBD_STAT_MOUSE_OBF)
        {
            if (s->mode & KBD_MODE_MOUSE_INT)
                irq12_level = 1;
        }
        else
        {   /* KBD_STAT_OBF set but KBD_STAT_MOUSE_OBF isn't. */
            if (s->mode & KBD_MODE_KBD_INT)
                irq1_level = 1;
        }
    }
    PDMDevHlpISASetIrq(pDevIns, 1, irq1_level);
    PDMDevHlpISASetIrq(pDevIns, 12, irq12_level);
}

void KBCUpdateInterrupts(PPDMDEVINS pDevIns)
{
    PKBDSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);
    kbd_update_irq(pDevIns, pThis);
}

static void kbc_dbb_out(PPDMDEVINS pDevIns, PKBDSTATE s, uint8_t val)
{
    s->dbbout = val;
    /* Set the OBF and raise IRQ. */
    s->status |= KBD_STAT_OBF;
    if (s->mode & KBD_MODE_KBD_INT)
        PDMDevHlpISASetIrq(pDevIns, 1, 1);
}

static void kbc_dbb_out_aux(PPDMDEVINS pDevIns, PKBDSTATE s, uint8_t val)
{
    s->dbbout = val;
    /* Set the aux OBF and raise IRQ. */
    s->status |= KBD_STAT_OBF | KBD_STAT_MOUSE_OBF;
    if (s->mode & KBD_MODE_MOUSE_INT)
        PDMDevHlpISASetIrq(pDevIns, 12, PDM_IRQ_LEVEL_HIGH);
}

static VBOXSTRICTRC kbd_write_command(PPDMDEVINS pDevIns, PKBDSTATE s, uint32_t val)
{
#ifdef DEBUG_KBD
    Log(("kbd: write cmd=0x%02x\n", val));
#endif
    switch(val) {
    case KBD_CCMD_READ_MODE:
        kbc_dbb_out(pDevIns, s, s->mode);
        break;
    case KBD_CCMD_WRITE_MODE:
    case KBD_CCMD_WRITE_OBUF:
    case KBD_CCMD_WRITE_AUX_OBUF:
    case KBD_CCMD_WRITE_MOUSE:
    case KBD_CCMD_WRITE_OUTPORT:
        s->write_cmd = val;
        break;
    case KBD_CCMD_MOUSE_DISABLE:
        s->mode |= KBD_MODE_DISABLE_MOUSE;
        PS2MLineDisable(&s->Aux);
        break;
    case KBD_CCMD_MOUSE_ENABLE:
        PS2MLineEnable(&s->Aux);
        s->mode &= ~KBD_MODE_DISABLE_MOUSE;
        /* Check for queued input. */
        /// @todo Can there actually be any?
        kbd_update_irq(pDevIns, s);
        break;
    case KBD_CCMD_TEST_MOUSE:
        kbc_dbb_out(pDevIns, s, 0x00);
        break;
    case KBD_CCMD_SELF_TEST:
        /* Enable the A20 line - that is the power-on state(!). */
# ifndef IN_RING3
        if (!PDMDevHlpA20IsEnabled(pDevIns))
            return VINF_IOM_R3_IOPORT_WRITE;
# else  /* IN_RING3 */
        PDMDevHlpA20Set(pDevIns, true);
# endif /* IN_RING3 */
        s->status |= KBD_STAT_SELFTEST;
        s->mode |= KBD_MODE_DISABLE_KBD;
        kbc_dbb_out(pDevIns, s, 0x55);
        break;
    case KBD_CCMD_KBD_TEST:
        kbc_dbb_out(pDevIns, s, 0x00);
        break;
    case KBD_CCMD_KBD_DISABLE:
        s->mode |= KBD_MODE_DISABLE_KBD;
        break;
    case KBD_CCMD_KBD_ENABLE:
        s->mode &= ~KBD_MODE_DISABLE_KBD;
        /* Check for queued input. */
        kbd_update_irq(pDevIns, s);
        break;
    case KBD_CCMD_READ_INPORT:
        kbc_dbb_out(pDevIns, s, 0xBF);
        break;
    case KBD_CCMD_READ_OUTPORT:
        /* XXX: check that */
#ifdef TARGET_I386
        val = 0x01 | (PDMDevHlpA20IsEnabled(pDevIns) << 1);
#else
        val = 0x01;
#endif
        if (s->status & KBD_STAT_OBF)
            val |= 0x10;
        if (s->status & KBD_STAT_MOUSE_OBF)
            val |= 0x20;
        kbc_dbb_out(pDevIns, s, val);
        break;
#ifdef TARGET_I386
    case KBD_CCMD_ENABLE_A20:
# ifndef IN_RING3
        if (!PDMDevHlpA20IsEnabled(pDevIns))
            return VINF_IOM_R3_IOPORT_WRITE;
# else  /* IN_RING3 */
        PDMDevHlpA20Set(pDevIns, true);
# endif /* IN_RING3 */
        break;
    case KBD_CCMD_DISABLE_A20:
# ifndef IN_RING3
        if (PDMDevHlpA20IsEnabled(pDevIns))
            return VINF_IOM_R3_IOPORT_WRITE;
# else  /* IN_RING3 */
        PDMDevHlpA20Set(pDevIns, false);
# endif /* IN_RING3 */
        break;
#endif
    case KBD_CCMD_READ_TSTINP:
        /* Keyboard clock line is zero IFF keyboard is disabled */
        val = (s->mode & KBD_MODE_DISABLE_KBD) ? 0 : 1;
        kbc_dbb_out(pDevIns, s, val);
        break;
    case KBD_CCMD_RESET:
    case KBD_CCMD_RESET_ALT:
#ifndef IN_RING3
        return VINF_IOM_R3_IOPORT_WRITE;
#else  /* IN_RING3 */
        LogRel(("Reset initiated by keyboard controller\n"));
        return PDMDevHlpVMReset(pDevIns, PDMVMRESET_F_KBD);
#endif /* IN_RING3 */
    case 0xff:
        /* ignore that - I don't know what is its use */
        break;
    /* Make OS/2 happy. */
    /* The 8042 RAM is readable using commands 0x20 thru 0x3f, and writable
       by 0x60 thru 0x7f. Now days only the first byte, the mode, is used.
       We'll ignore the writes (0x61..7f) and return 0 for all the reads
       just to make some OS/2 debug stuff a bit happier. */
    case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
    case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
        kbc_dbb_out(pDevIns, s, 0);
        Log(("kbd: reading non-standard RAM addr %#x\n", val & 0x1f));
        break;
    default:
        Log(("kbd: unsupported keyboard cmd=0x%02x\n", val));
        break;
    }
    return VINF_SUCCESS;
}

static uint32_t kbd_read_data(PPDMDEVINS pDevIns, PKBDSTATE s)
{
    uint32_t val;

    /* Return the current DBB contents. */
    val = s->dbbout;

    /* Reading the DBB deasserts IRQs... */
    if (s->status & KBD_STAT_MOUSE_OBF)
        PDMDevHlpISASetIrq(pDevIns, 12, 0);
    else
        PDMDevHlpISASetIrq(pDevIns, 1, 0);
    /* ...and clears the OBF bits. */
    s->status &= ~(KBD_STAT_OBF | KBD_STAT_MOUSE_OBF);

    /* Check if more data is available. */
    kbd_update_irq(pDevIns, s);
#ifdef DEBUG_KBD
    Log(("kbd: read data=0x%02x\n", val));
#endif
    return val;
}

static VBOXSTRICTRC kbd_write_data(PPDMDEVINS pDevIns, PKBDSTATE s, uint32_t val)
{
    VBOXSTRICTRC rc = VINF_SUCCESS;

#ifdef DEBUG_KBD
    Log(("kbd: write data=0x%02x\n", val));
#endif

    switch(s->write_cmd) {
    case 0:
        /* Automatically enables keyboard interface. */
        s->mode &= ~KBD_MODE_DISABLE_KBD;
        rc = PS2KByteToKbd(pDevIns, &s->Kbd, val);
        if (rc == VINF_SUCCESS)
            kbd_update_irq(pDevIns, s);
        break;
    case KBD_CCMD_WRITE_MODE:
        s->mode = val;
        s->translate = (s->mode & KBD_MODE_KCC) == KBD_MODE_KCC;
        kbd_update_irq(pDevIns, s);
        break;
    case KBD_CCMD_WRITE_OBUF:
        kbc_dbb_out(pDevIns, s, val);
        break;
    case KBD_CCMD_WRITE_AUX_OBUF:
        kbc_dbb_out_aux(pDevIns, s, val);
        break;
    case KBD_CCMD_WRITE_OUTPORT:
#ifdef TARGET_I386
#  ifndef IN_RING3
        if (PDMDevHlpA20IsEnabled(pDevIns) != !!(val & 2))
            rc = VINF_IOM_R3_IOPORT_WRITE;
#  else /* IN_RING3 */
        PDMDevHlpA20Set(pDevIns, !!(val & 2));
#  endif /* IN_RING3 */
#endif
        if (!(val & 1)) {
# ifndef IN_RING3
            rc = VINF_IOM_R3_IOPORT_WRITE;
# else
            rc = PDMDevHlpVMReset(pDevIns, PDMVMRESET_F_KBD);
# endif
        }
        break;
    case KBD_CCMD_WRITE_MOUSE:
        /* Automatically enables aux interface. */
        if (s->mode & KBD_MODE_DISABLE_MOUSE)
        {
            PS2MLineEnable(&s->Aux);
            s->mode &= ~KBD_MODE_DISABLE_MOUSE;
        }
        rc = PS2MByteToAux(pDevIns, &s->Aux, val);
        if (rc == VINF_SUCCESS)
            kbd_update_irq(pDevIns, s);
        break;
    default:
        break;
    }
    if (rc != VINF_IOM_R3_IOPORT_WRITE)
        s->write_cmd = 0;
    return rc;
}

#ifdef IN_RING3

static int kbd_load(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PKBDSTATE s, PKBDSTATER3 pThisCC, uint32_t version_id)
{
    uint32_t    u32, i;
    uint8_t     u8Dummy;
    uint32_t    u32Dummy;
    int         rc;

#if 0
    /** @todo enable this and remove the "if (version_id == 4)" code at some
     * later time */
    /* Version 4 was never created by any publicly released version of VBox */
    AssertReturn(version_id != 4, VERR_NOT_SUPPORTED);
#endif
    if (version_id < 2 || version_id > PCKBD_SAVED_STATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    pHlp->pfnSSMGetU8(pSSM, &s->write_cmd);
    pHlp->pfnSSMGetU8(pSSM, &s->status);
    pHlp->pfnSSMGetU8(pSSM, &s->mode);
    if (version_id <= 5)
    {
        pHlp->pfnSSMGetU32(pSSM, &u32Dummy);
        pHlp->pfnSSMGetU32(pSSM, &u32Dummy);
    }
    else
    {
        pHlp->pfnSSMGetU8(pSSM, &s->dbbout);
    }
    if (version_id <= 7)
    {
        int32_t     i32Dummy;
        uint8_t     u8State;
        uint8_t     u8Rate;
        uint8_t     u8Proto;

        pHlp->pfnSSMGetU32(pSSM, &u32Dummy);
        pHlp->pfnSSMGetU8(pSSM, &u8State);
        pHlp->pfnSSMGetU8(pSSM, &u8Dummy);
        pHlp->pfnSSMGetU8(pSSM, &u8Rate);
        pHlp->pfnSSMGetU8(pSSM, &u8Dummy);
        pHlp->pfnSSMGetU8(pSSM, &u8Proto);
        pHlp->pfnSSMGetU8(pSSM, &u8Dummy);
        pHlp->pfnSSMGetS32(pSSM, &i32Dummy);
        pHlp->pfnSSMGetS32(pSSM, &i32Dummy);
        pHlp->pfnSSMGetS32(pSSM, &i32Dummy);
        if (version_id > 2)
        {
            pHlp->pfnSSMGetS32(pSSM, &i32Dummy);
            pHlp->pfnSSMGetS32(pSSM, &i32Dummy);
        }
        rc = pHlp->pfnSSMGetU8(pSSM, &u8Dummy);
        if (version_id == 4)
        {
            pHlp->pfnSSMGetU32(pSSM, &u32Dummy);
            rc = pHlp->pfnSSMGetU32(pSSM, &u32Dummy);
        }
        if (version_id > 3)
            rc = pHlp->pfnSSMGetU8(pSSM, &u8Dummy);
        if (version_id == 4)
            rc = pHlp->pfnSSMGetU8(pSSM, &u8Dummy);
        AssertLogRelRCReturn(rc, rc);

        PS2MR3FixupState(&s->Aux, &pThisCC->Aux, u8State, u8Rate, u8Proto);
    }

    /* Determine the translation state. */
    s->translate = (s->mode & KBD_MODE_KCC) == KBD_MODE_KCC;

    /*
     * Load the queues
     */
    if (version_id <= 5)
    {
        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        if (RT_FAILURE(rc))
            return rc;
        for (i = 0; i < u32; i++)
        {
            rc = pHlp->pfnSSMGetU8(pSSM, &u8Dummy);
            if (RT_FAILURE(rc))
                return rc;
        }
        Log(("kbd_load: %d keyboard queue items discarded from old saved state\n", u32));
    }

    if (version_id <= 7)
    {
        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        if (RT_FAILURE(rc))
            return rc;
        for (i = 0; i < u32; i++)
        {
            rc = pHlp->pfnSSMGetU8(pSSM, &u8Dummy);
            if (RT_FAILURE(rc))
                return rc;
        }
        Log(("kbd_load: %d mouse event queue items discarded from old saved state\n", u32));

        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        if (RT_FAILURE(rc))
            return rc;
        for (i = 0; i < u32; i++)
        {
            rc = pHlp->pfnSSMGetU8(pSSM, &u8Dummy);
            if (RT_FAILURE(rc))
                return rc;
        }
        Log(("kbd_load: %d mouse command queue items discarded from old saved state\n", u32));
    }

    /* terminator */
    rc = pHlp->pfnSSMGetU32(pSSM, &u32);
    if (RT_FAILURE(rc))
        return rc;
    if (u32 != ~0U)
    {
        AssertMsgFailed(("u32=%#x\n", u32));
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;
    }
    return 0;
}

#endif /* IN_RING3 */


/* VirtualBox code start */

/* -=-=-=-=-=- wrappers -=-=-=-=-=- */

/** Fluff bits indexed by size (1,2,4). */
static uint32_t const g_afFluff[5] =
{
    /* [0] = */ 0,
    /* [1] = */ 0,
    /* [2] = */ UINT32_C(0xff00),
    /* [3] = */ 0,
    /* [4] = */ UINT32_C(0xffffff00) /* Crazy Apple (Darwin 6.0.2 and earlier). */
};

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,
 * Port I/O Handler for keyboard data IN operations.}
 */
static DECLCALLBACK(VBOXSTRICTRC) kbdIOPortDataRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PKBDSTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);
    RT_NOREF(pvUser, offPort);
    Assert(offPort == 0);
    Assert(cb == 1 || cb == 2 || cb == 4);

    *pu32 = kbd_read_data(pDevIns, pThis) | g_afFluff[cb];
    Log2(("kbdIOPortDataRead: cb=%u *pu32=%#x\n", cb, *pu32));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 * Port I/O Handler for keyboard data OUT operations.}
 */
static DECLCALLBACK(VBOXSTRICTRC) kbdIOPortDataWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    Assert(offPort == 0);

    if (cb == 1 || cb == 2)
    {
        PKBDSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);
        VBOXSTRICTRC rc = kbd_write_data(pDevIns, pThis, (uint8_t)u32);
        Log2(("kbdIOPortDataWrite: Port=0x60+%x cb=%d u32=%#x rc=%Rrc\n", offPort, cb, u32, VBOXSTRICTRC_VAL(rc)));
        return rc;
    }
    Assert(cb == 4);
    ASSERT_GUEST_MSG_FAILED(("Port=0x60+%x cb=%d\n", offPort, cb));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,
 * Port I/O Handler for keyboard status IN operations.}
 */
static DECLCALLBACK(VBOXSTRICTRC) kbdIOPortStatusRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PKBDSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);
    RT_NOREF(offPort, pvUser);
    Assert(offPort == 0);
    Assert(cb == 1 || cb == 2 || cb == 4);

    *pu32 = pThis->status | g_afFluff[cb];
    Log2(("kbdIOPortStatusRead: cb=%u -> *pu32=%#x\n", cb, *pu32));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,
 * Port I/O Handler for keyboard command OUT operations.}
 */
static DECLCALLBACK(VBOXSTRICTRC) kbdIOPortCommandWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(offPort, pvUser);
    Assert(offPort == 0);

    if (cb == 1 || cb == 2)
    {
        PKBDSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);
        VBOXSTRICTRC rc = kbd_write_command(pDevIns, pThis, (uint8_t)u32);
        Log2(("kbdIOPortCommandWrite: cb=%d u32=%#x rc=%Rrc\n", cb, u32, VBOXSTRICTRC_VAL(rc)));
        return rc;
    }
    Assert(cb == 4);
    ASSERT_GUEST_MSG_FAILED(("offPort=0x64+%x cb=%d\n", offPort, cb));
    return VINF_SUCCESS;
}

/**
 * Clear a queue.
 *
 * @param   pQHdr       The queue header.
 * @param   cElements   The queue size.
 */
void PS2CmnClearQueue(PPS2QHDR pQHdr, size_t cElements)
{
    Assert(cElements > 0);
    LogFlowFunc(("Clearing %s queue %p\n", R3STRING(pQHdr->pszDescR3), pQHdr));
    pQHdr->wpos  = pQHdr->rpos = pQHdr->rpos % cElements;
    pQHdr->cUsed = 0;
}


/**
 * Add a byte to a queue.
 *
 * @param   pQHdr       The queue header.
 * @param   cElements   The queue size.
 * @param   pbElements  The queue element array.
 * @param   bValue      The byte to store.
 */
void PS2CmnInsertQueue(PPS2QHDR pQHdr, size_t cElements, uint8_t *pbElements, uint8_t bValue)
{
    Assert(cElements > 0);

    /* Check that the queue is not full. */
    uint32_t cUsed = pQHdr->cUsed;
    if (cUsed < cElements)
    {
        /* Insert data and update circular buffer write position. */
        uint32_t wpos = pQHdr->wpos % cElements;
        pbElements[wpos] = bValue;

        wpos += 1;
        if (wpos < cElements)
            pQHdr->wpos = wpos;
        else
            pQHdr->wpos = 0; /* Roll over. */
        pQHdr->cUsed = cUsed + 1;

        LogRelFlowFunc(("inserted %#04x into %s queue %p\n", bValue, R3STRING(pQHdr->pszDescR3), pQHdr));
    }
    else
    {
        Assert(cUsed == cElements);
        LogRelFlowFunc(("%s queue %p full (%zu entries)\n", R3STRING(pQHdr->pszDescR3), pQHdr, cElements));
    }
}

/**
 * Retrieve a byte from a queue.
 *
 * @param   pQHdr       The queue header.
 * @param   cElements   The queue size.
 * @param   pbElements  The queue element array.
 * @param   pbValue     Where to return the byte on success.
 *
 * @retval  VINF_TRY_AGAIN if queue is empty,
 * @retval  VINF_SUCCESS if a byte was read.
 */
int PS2CmnRemoveQueue(PPS2QHDR pQHdr, size_t cElements, uint8_t const *pbElements, uint8_t *pbValue)
{
    int rc;

    Assert(cElements > 0);
    Assert(pbValue);

    uint32_t cUsed = (uint32_t)RT_MIN(pQHdr->cUsed, cElements);
    if (cUsed > 0)
    {
        uint32_t rpos = pQHdr->rpos % cElements;
        *pbValue = pbElements[rpos];

        rpos += 1;
        if (rpos < cElements)
            pQHdr->rpos = rpos;
        else
            pQHdr->rpos = 0;   /* Roll over. */
        pQHdr->cUsed = cUsed - 1;

        LogFlowFunc(("removed 0x%02X from %s queue %p\n", *pbValue, R3STRING(pQHdr->pszDescR3), pQHdr));
        rc = VINF_SUCCESS;
    }
    else
    {
        LogFlowFunc(("%s queue %p empty\n", R3STRING(pQHdr->pszDescR3), pQHdr));
        rc = VINF_TRY_AGAIN;
    }
    return rc;
}

#ifdef IN_RING3

/**
 * Save a queue state.
 *
 * @param   pHlp        The device helpers.
 * @param   pSSM        SSM handle to write the state to.
 * @param   pQHdr       The queue header.
 * @param   cElements   The queue size.
 * @param   pbElements  The queue element array.
 */
void PS2CmnR3SaveQueue(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PPS2QHDR pQHdr, size_t cElements, uint8_t const *pbElements)
{
    uint32_t cItems = (uint32_t)RT_MIN(pQHdr->cUsed, cElements);

    /* Only save the number of items. Note that the read/write
     * positions aren't saved as they will be rebuilt on load.
     */
    pHlp->pfnSSMPutU32(pSSM, cItems);

    LogFlow(("Storing %u items from %s queue %p\n", cItems, pQHdr->pszDescR3, pQHdr));

    /* Save queue data - only the bytes actually used (typically zero). */
    for (uint32_t i = pQHdr->rpos % cElements; cItems-- > 0; i = (i + 1) % cElements)
        pHlp->pfnSSMPutU8(pSSM, pbElements[i]);
}

/**
 * Load a queue state.
 *
 * @param   pHlp        The device helpers.
 * @param   pSSM        SSM handle to read the state from.
 * @param   pQHdr       The queue header.
 * @param   cElements   The queue size.
 * @param   pbElements  The queue element array.
 *
 * @returns VBox status/error code.
 */
int PS2CmnR3LoadQueue(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PPS2QHDR pQHdr, size_t cElements, uint8_t *pbElements)
{
    /* On load, always put the read pointer at zero. */
    uint32_t cUsed;
    int rc = pHlp->pfnSSMGetU32(pSSM, &cUsed);
    AssertRCReturn(rc, rc);

    LogFlow(("Loading %u items to %s queue %p\n", cUsed, pQHdr->pszDescR3, pQHdr));

    AssertMsgReturn(cUsed <= cElements, ("Saved size=%u, actual=%zu\n", cUsed, cElements),
                    VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

    /* Recalculate queue positions and load data in one go. */
    pQHdr->rpos  = 0;
    pQHdr->wpos  = cUsed;
    pQHdr->cUsed = cUsed;
    return pHlp->pfnSSMGetMem(pSSM, pbElements, cUsed);
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC, Saves a state of the keyboard device.}
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pSSM  The handle to save the state to.
 */
static DECLCALLBACK(int) kbdR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PKBDSTATE       pThis = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);
    PCPDMDEVHLPR3   pHlp = pDevIns->pHlpR3;

    pHlp->pfnSSMPutU8(pSSM, pThis->write_cmd);
    pHlp->pfnSSMPutU8(pSSM, pThis->status);
    pHlp->pfnSSMPutU8(pSSM, pThis->mode);
    pHlp->pfnSSMPutU8(pSSM, pThis->dbbout);
    /* terminator */
    pHlp->pfnSSMPutU32(pSSM, UINT32_MAX);

    PS2KR3SaveState(pDevIns, &pThis->Kbd, pSSM);
    PS2MR3SaveState(pDevIns, &pThis->Aux, pSSM);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC, Loads a saved keyboard device state.}
 */
static DECLCALLBACK(int) kbdR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PKBDSTATE    pThis   = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);
    PKBDSTATER3  pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PKBDSTATER3);
    int rc;

    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);
    rc = kbd_load(pDevIns->pHlpR3, pSSM, pThis, pThisCC, uVersion);
    AssertRCReturn(rc, rc);

    if (uVersion >= 6)
        rc = PS2KR3LoadState(pDevIns, &pThis->Kbd, pSSM, uVersion);
    AssertRCReturn(rc, rc);

    if (uVersion >= 8)
        rc = PS2MR3LoadState(pDevIns, &pThis->Aux, &pThisCC->Aux, pSSM, uVersion);
    AssertRCReturn(rc, rc);
    return rc;
}


/**
 * @callback_method_impl{FNSSMDEVLOADDONE, Key state fix-up after loading}
 */
static DECLCALLBACK(int) kbdR3LoadDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PKBDSTATE    pThis   = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);
    PKBDSTATER3  pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PKBDSTATER3);
    RT_NOREF(pSSM);
    if (pThis->mode & KBD_MODE_DISABLE_MOUSE)
        PS2MLineDisable(&pThis->Aux);
    if (pThis->mode & KBD_MODE_DISABLE_KBD)
        PS2KLineDisable(&pThis->Kbd);
    return PS2KR3LoadDone(pDevIns, &pThis->Kbd, &pThisCC->Kbd);
}


/**
 * Debug device info handler. Prints basic auxiliary device state.
 *
 * @param   pDevIns     Device instance which registered the info.
 * @param   pHlp        Callback functions for doing output.
 * @param   pszArgs     Argument string. Optional and specific to the handler.
 */
static DECLCALLBACK(void) kbdR3InfoState(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PKBDSTATE    pThis   = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);
    NOREF(pszArgs);

    pHlp->pfnPrintf(pHlp, "Keyboard controller: Active command %02X, DBB out %02X, translation %s\n",
                    pThis->write_cmd, pThis->dbbout, pThis->translate ? "on"  : "off");

    pHlp->pfnPrintf(pHlp, "Mode: %02X ( ", pThis->mode);
    if (pThis->mode & KBD_MODE_DISABLE_KBD)
        pHlp->pfnPrintf(pHlp, "DISABLE_KBD ");
    if (pThis->mode & KBD_MODE_KBD_INT)
        pHlp->pfnPrintf(pHlp, "KBD_INT ");
    if (pThis->mode & KBD_MODE_MOUSE_INT)
        pHlp->pfnPrintf(pHlp, "AUX_INT ");
    if (pThis->mode & KBD_MODE_SYS)
        pHlp->pfnPrintf(pHlp, "SYS ");
    if (pThis->mode & KBD_MODE_NO_KEYLOCK)
        pHlp->pfnPrintf(pHlp, "NO_KEYLOCK ");
    if (pThis->mode & KBD_MODE_DISABLE_KBD)
        pHlp->pfnPrintf(pHlp, "DISABLE_KBD ");
    if (pThis->mode & KBD_MODE_DISABLE_MOUSE)
        pHlp->pfnPrintf(pHlp, "DISABLE_AUX ");
    if (pThis->mode & KBD_MODE_KCC)
        pHlp->pfnPrintf(pHlp, "KCC ");
    if (pThis->mode & KBD_MODE_RFU)
        pHlp->pfnPrintf(pHlp, "RFU ");
    pHlp->pfnPrintf(pHlp, " )\n");

    pHlp->pfnPrintf(pHlp, "Status: %02X ( ", pThis->status);
    if (pThis->status & KBD_STAT_OBF)
        pHlp->pfnPrintf(pHlp, "OBF ");
    if (pThis->status & KBD_STAT_IBF)
        pHlp->pfnPrintf(pHlp, "IBF ");
    if (pThis->status & KBD_STAT_SELFTEST)
        pHlp->pfnPrintf(pHlp, "SELFTEST ");
    if (pThis->status & KBD_STAT_CMD)
        pHlp->pfnPrintf(pHlp, "CMD ");
    if (pThis->status & KBD_STAT_UNLOCKED)
        pHlp->pfnPrintf(pHlp, "UNLOCKED ");
    if (pThis->status & KBD_STAT_MOUSE_OBF)
        pHlp->pfnPrintf(pHlp, "AUX_OBF ");
    if (pThis->status & KBD_STAT_GTO)
        pHlp->pfnPrintf(pHlp, "GTO ");
    if (pThis->status & KBD_STAT_PERR)
        pHlp->pfnPrintf(pHlp, "PERR ");
    pHlp->pfnPrintf(pHlp, " )\n");
}


/* -=-=-=-=-=- real code -=-=-=-=-=- */

/**
 * Reset notification.
 *
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(void)  kbdR3Reset(PPDMDEVINS pDevIns)
{
    PKBDSTATE    pThis   = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);
    PKBDSTATER3  pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PKBDSTATER3);

    pThis->mode = KBD_MODE_KBD_INT | KBD_MODE_MOUSE_INT;
    pThis->status = KBD_STAT_CMD | KBD_STAT_UNLOCKED;
    /* Resetting everything, keyword was not working right on NT4 reboot. */
    pThis->write_cmd = 0;
    pThis->translate = 0;

    PS2KR3Reset(pDevIns, &pThis->Kbd, &pThisCC->Kbd);
    PS2MR3Reset(&pThis->Aux);
}


/**
 * @interface_method_impl{PDMDEVREGR3,pfnAttach}
 *
 * @remark  The keyboard controller doesn't support this action, this is just
 *          implemented to try out the driver<->device structure.
 */
static DECLCALLBACK(int)  kbdR3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PKBDSTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PKBDSTATER3);
    int         rc;

    AssertMsgReturn(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
                    ("PS/2 device does not support hotplugging\n"),
                    VERR_INVALID_PARAMETER);

    switch (iLUN)
    {
        /* LUN #0: keyboard */
        case 0:
            rc = PS2KR3Attach(pDevIns, &pThisCC->Kbd, iLUN, fFlags);
            break;

        /* LUN #1: aux/mouse */
        case 1:
            rc = PS2MR3Attach(pDevIns, &pThisCC->Aux, iLUN, fFlags);
            break;

        default:
            AssertMsgFailed(("Invalid LUN #%d\n", iLUN));
            return VERR_PDM_NO_SUCH_LUN;
    }

    return rc;
}


/**
 * @interface_method_impl{PDMDEVREGR3,pfnDetach}
 * @remark  The keyboard controller doesn't support this action, this is just
 *          implemented to try out the driver<->device structure.
 */
static DECLCALLBACK(void)  kbdR3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
#if 0
    /*
     * Reset the interfaces and update the controller state.
     */
    PKBDSTATE    pThis = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);
    switch (iLUN)
    {
        /* LUN #0: keyboard */
        case 0:
            pThisCC->Keyboard.pDrv = NULL;
            pThisCC->Keyboard.pDrvBase = NULL;
            break;

        /* LUN #1: aux/mouse */
        case 1:
            pThisCC->Mouse.pDrv = NULL;
            pThisCC->Mouse.pDrvBase = NULL;
            break;

        default:
            AssertMsgFailed(("Invalid LUN #%d\n", iLUN));
            break;
    }
#else
    NOREF(pDevIns); NOREF(iLUN); NOREF(fFlags);
#endif
}


/**
 * @interface_method_impl{PDMDEVREGR3,pfnConstruct}
 */
static DECLCALLBACK(int) kbdR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PKBDSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);
    PKBDSTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PKBDSTATER3);
    int         rc;
    RT_NOREF(iInstance);

    Assert(iInstance == 0);

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns,  "KbdThrottleEnabled", "");
    Log(("pckbd: fRCEnabled=%RTbool fR0Enabled=%RTbool\n", pDevIns->fRCEnabled, pDevIns->fR0Enabled));

    /*
     * Initialize the sub-components.
     */
    rc = PS2KR3Construct(pDevIns, &pThis->Kbd, &pThisCC->Kbd, pCfg);
    AssertRCReturn(rc, rc);

    rc = PS2MR3Construct(pDevIns, &pThis->Aux, &pThisCC->Aux);
    AssertRCReturn(rc, rc);

    /*
     * Register I/O ports.
     */
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, 0x60 /*uPort*/, 1 /*cPorts*/, kbdIOPortDataWrite, kbdIOPortDataRead,
                                     "PC Keyboard - Data", NULL /*pExtDescs*/, &pThis->hIoPortData);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, 0x64 /*uPort*/, 1 /*cPorts*/, kbdIOPortCommandWrite, kbdIOPortStatusRead,
                                     "PC Keyboard - Command / Status", NULL /*pExtDescs*/, &pThis->hIoPortCmdStatus);
    AssertRCReturn(rc, rc);

    /*
     * Saved state.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, PCKBD_SAVED_STATE_VERSION, sizeof(*pThis), NULL,
                                NULL, NULL, NULL,
                                NULL, kbdR3SaveExec, NULL,
                                NULL, kbdR3LoadExec, kbdR3LoadDone);
    AssertRCReturn(rc, rc);

    /*
     * Register debugger info callbacks.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "ps2c", "Display keyboard/mouse controller state.", kbdR3InfoState);

    /*
     * Attach to the keyboard and mouse drivers.
     */
    rc = kbdR3Attach(pDevIns, 0 /* keyboard LUN # */, PDM_TACH_FLAGS_NOT_HOT_PLUG);
    AssertRCReturn(rc, rc);
    rc = kbdR3Attach(pDevIns, 1 /* aux/mouse LUN # */, PDM_TACH_FLAGS_NOT_HOT_PLUG);
    AssertRCReturn(rc, rc);

    /*
     * Initialize the device state.
     */
    kbdR3Reset(pDevIns);

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) kbdRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PKBDSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PKBDSTATE);

    int rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortData, kbdIOPortDataWrite, kbdIOPortDataRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortCmdStatus, kbdIOPortCommandWrite, kbdIOPortStatusRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DevicePS2KeyboardMouse =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "pckbd",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_INPUT,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(KBDSTATE),
    /* .cbInstanceCC = */           CTX_EXPR(sizeof(KBDSTATER3), 0, 0),
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "PS/2 Keyboard and Mouse device. Emulates both the keyboard, mouse and the keyboard controller.\n"
                                    "LUN #0 is the keyboard connector.\n"
                                    "LUN #1 is the aux/mouse connector.",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           kbdR3Construct,
    /* .pfnDestruct = */            NULL,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               kbdR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              kbdR3Attach,
    /* .pfnDetach = */              kbdR3Detach,
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
    /* .pfnConstruct = */           kbdRZConstruct,
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
    /* .pfnConstruct = */           kbdRZConstruct,
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

