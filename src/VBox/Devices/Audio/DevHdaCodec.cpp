/* $Id: DevHdaCodec.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Intel HD Audio Controller Emulation - Codec, Sigmatel/IDT STAC9220.
 *
 * Implemented based on the Intel HD Audio specification and the
 * Sigmatel/IDT STAC9220 datasheet.
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
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_HDA_CODEC
#include <VBox/log.h>

#include <VBox/AssertGuest.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>

#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/string.h>
#include <iprt/mem.h>
#include <iprt/asm.h>
#include <iprt/cpp/utils.h>

#include "VBoxDD.h"
#include "AudioMixer.h"
#include "DevHda.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define AMPLIFIER_IN    0
#define AMPLIFIER_OUT   1
#define AMPLIFIER_LEFT  1
#define AMPLIFIER_RIGHT 0
#define AMPLIFIER_REGISTER(amp, inout, side, index) ((amp)[30*(inout) + 15*(side) + (index)])


/** @name STAC9220 - Nodes IDs / Names.
 * @{  */
#define STAC9220_NID_ROOT                                  0x0  /* Root node */
#define STAC9220_NID_AFG                                   0x1  /* Audio Configuration Group */
#define STAC9220_NID_DAC0                                  0x2  /* Out */
#define STAC9220_NID_DAC1                                  0x3  /* Out */
#define STAC9220_NID_DAC2                                  0x4  /* Out */
#define STAC9220_NID_DAC3                                  0x5  /* Out */
#define STAC9220_NID_ADC0                                  0x6  /* In */
#define STAC9220_NID_ADC1                                  0x7  /* In */
#define STAC9220_NID_SPDIF_OUT                             0x8  /* Out */
#define STAC9220_NID_SPDIF_IN                              0x9  /* In */
/** Also known as PIN_A. */
#define STAC9220_NID_PIN_HEADPHONE0                        0xA  /* In, Out */
#define STAC9220_NID_PIN_B                                 0xB  /* In, Out */
#define STAC9220_NID_PIN_C                                 0xC  /* In, Out */
/** Also known as PIN D. */
#define STAC9220_NID_PIN_HEADPHONE1                        0xD  /* In, Out */
#define STAC9220_NID_PIN_E                                 0xE  /* In */
#define STAC9220_NID_PIN_F                                 0xF  /* In, Out */
/** Also known as DIGOUT0. */
#define STAC9220_NID_PIN_SPDIF_OUT                         0x10 /* Out */
/** Also known as DIGIN. */
#define STAC9220_NID_PIN_SPDIF_IN                          0x11 /* In */
#define STAC9220_NID_ADC0_MUX                              0x12 /* In */
#define STAC9220_NID_ADC1_MUX                              0x13 /* In */
#define STAC9220_NID_PCBEEP                                0x14 /* Out */
#define STAC9220_NID_PIN_CD                                0x15 /* In */
#define STAC9220_NID_VOL_KNOB                              0x16
#define STAC9220_NID_AMP_ADC0                              0x17 /* In */
#define STAC9220_NID_AMP_ADC1                              0x18 /* In */
/* Only for STAC9221. */
#define STAC9221_NID_ADAT_OUT                              0x19 /* Out */
#define STAC9221_NID_I2S_OUT                               0x1A /* Out */
#define STAC9221_NID_PIN_I2S_OUT                           0x1B /* Out */

/** Number of total nodes emulated. */
#define STAC9221_NUM_NODES                                 0x1C
/** @} */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/**
 * A codec verb descriptor.
 */
typedef struct CODECVERB
{
    /** Verb. */
    uint32_t                   uVerb;
    /** Verb mask. */
    uint32_t                   fMask;
    /**
     * Function pointer for implementation callback.
     *
     * This is always a valid pointer in ring-3, while elsewhere a NULL indicates
     * that we must return to ring-3 to process it.
     *
     * @returns VBox status code (99.9% is VINF_SUCCESS, caller doesn't care much
     *          what you return at present).
     *
     * @param   pThis   The shared codec intance data.
     * @param   uCmd    The command.
     * @param   puResp  Where to return the response value.
     *
     * @thread  EMT or task worker thread (see HDASTATE::hCorbDmaTask).
     */
    DECLCALLBACKMEMBER(int,    pfn, (PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp));
    /** Friendly name, for debugging. */
    const char                *pszName;
} CODECVERB;
/** Pointer to a const codec verb descriptor. */
typedef CODECVERB const *PCCODECVERB;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** @name STAC9220 Node Classifications.
 * @note Referenced through STAC9220WIDGET in the constructor below.
 * @{ */
static uint8_t const g_abStac9220Ports[]      = { STAC9220_NID_PIN_HEADPHONE0, STAC9220_NID_PIN_B, STAC9220_NID_PIN_C, STAC9220_NID_PIN_HEADPHONE1, STAC9220_NID_PIN_E, STAC9220_NID_PIN_F, 0 };
static uint8_t const g_abStac9220Dacs[]       = { STAC9220_NID_DAC0, STAC9220_NID_DAC1, STAC9220_NID_DAC2, STAC9220_NID_DAC3, 0 };
static uint8_t const g_abStac9220Adcs[]       = { STAC9220_NID_ADC0, STAC9220_NID_ADC1, 0 };
static uint8_t const g_abStac9220SpdifOuts[]  = { STAC9220_NID_SPDIF_OUT, 0 };
static uint8_t const g_abStac9220SpdifIns[]   = { STAC9220_NID_SPDIF_IN, 0 };
static uint8_t const g_abStac9220DigOutPins[] = { STAC9220_NID_PIN_SPDIF_OUT, 0 };
static uint8_t const g_abStac9220DigInPins[]  = { STAC9220_NID_PIN_SPDIF_IN, 0 };
static uint8_t const g_abStac9220AdcVols[]    = { STAC9220_NID_AMP_ADC0, STAC9220_NID_AMP_ADC1, 0 };
static uint8_t const g_abStac9220AdcMuxs[]    = { STAC9220_NID_ADC0_MUX, STAC9220_NID_ADC1_MUX, 0 };
static uint8_t const g_abStac9220Pcbeeps[]    = { STAC9220_NID_PCBEEP, 0 };
static uint8_t const g_abStac9220Cds[]        = { STAC9220_NID_PIN_CD, 0 };
static uint8_t const g_abStac9220VolKnobs[]   = { STAC9220_NID_VOL_KNOB, 0 };
/** @} */

/** @name STAC 9221 Values.
 * @note Referenced through STAC9220WIDGET in the constructor below
 * @{ */
/** @todo Is STAC9220_NID_SPDIF_IN really correct for reserved nodes? */
static uint8_t const g_abStac9220Reserveds[]  = { STAC9220_NID_SPDIF_IN, STAC9221_NID_ADAT_OUT, STAC9221_NID_I2S_OUT, STAC9221_NID_PIN_I2S_OUT, 0 };
/** @} */


/** SSM description of CODECCOMMONNODE. */
static SSMFIELD const g_aCodecNodeFields[] =
{
    SSMFIELD_ENTRY(     CODECSAVEDSTATENODE, Core.uID),
    SSMFIELD_ENTRY_PAD_HC_AUTO(3, 3),
    SSMFIELD_ENTRY(     CODECSAVEDSTATENODE, Core.au32F00_param),
    SSMFIELD_ENTRY(     CODECSAVEDSTATENODE, Core.au32F02_param),
    SSMFIELD_ENTRY(     CODECSAVEDSTATENODE, au32Params),
    SSMFIELD_ENTRY_TERM()
};

/** Backward compatibility with v1 of CODECCOMMONNODE. */
static SSMFIELD const g_aCodecNodeFieldsV1[] =
{
    SSMFIELD_ENTRY(     CODECSAVEDSTATENODE, Core.uID),
    SSMFIELD_ENTRY_PAD_HC_AUTO(3, 7),
    SSMFIELD_ENTRY_OLD_HCPTR(Core.name),
    SSMFIELD_ENTRY(     CODECSAVEDSTATENODE, Core.au32F00_param),
    SSMFIELD_ENTRY(     CODECSAVEDSTATENODE, Core.au32F02_param),
    SSMFIELD_ENTRY(     CODECSAVEDSTATENODE, au32Params),
    SSMFIELD_ENTRY_TERM()
};



/*********************************************************************************************************************************
*   STAC9220 Constructor / Reset                                                                                                 *
*********************************************************************************************************************************/

/**
 * Resets a single node of the codec.
 *
 * @param   pThis       HDA codec of node to reset.
 * @param   uNID        Node ID to set node to.
 * @param   pNode       Node to reset.
 * @param   fInReset    Set if we're called from hdaCodecReset via
 *                      stac9220Reset, clear if called from stac9220Construct.
 */
static void stac9220NodeReset(PHDACODECR3 pThis, uint8_t uNID, PCODECNODE pNode, bool const fInReset)
{
    LogFlowFunc(("NID=0x%x (%RU8)\n", uNID, uNID));

    if (   !fInReset
        && (   uNID != STAC9220_NID_ROOT
            && uNID != STAC9220_NID_AFG)
       )
    {
        RT_ZERO(pNode->node);
    }

    /* Set common parameters across all nodes. */
    pNode->node.uID = uNID;
    pNode->node.uSD = 0;

    switch (uNID)
    {
        /* Root node. */
        case STAC9220_NID_ROOT:
        {
            /* Set the revision ID. */
            pNode->root.node.au32F00_param[0x02] = CODEC_MAKE_F00_02(0x1, 0x0, 0x3, 0x4, 0x0, 0x1);
            break;
        }

        /*
         * AFG (Audio Function Group).
         */
        case STAC9220_NID_AFG:
        {
            pNode->afg.node.au32F00_param[0x08] = CODEC_MAKE_F00_08(1, 0xd, 0xd);
            /* We set the AFG's PCM capabitilies fixed to 16kHz, 22.5kHz + 44.1kHz, 16-bit signed. */
            pNode->afg.node.au32F00_param[0x0A] = CODEC_F00_0A_44_1KHZ          /* 44.1 kHz */
                                                | CODEC_F00_0A_44_1KHZ_1_2X     /* Messed up way of saying 22.05 kHz */
                                                | CODEC_F00_0A_48KHZ_1_3X       /* Messed up way of saying 16 kHz. */
                                                | CODEC_F00_0A_16_BIT;          /* 16-bit signed */
            /* Note! We do not set CODEC_F00_0A_48KHZ here because we end up with
                     S/PDIF output showing up in windows and it trying to configure
                     streams other than 0 and 4 and stuff going sideways in the
                     stream setup/removal area. */
            pNode->afg.node.au32F00_param[0x0B] = CODEC_F00_0B_PCM;
            pNode->afg.node.au32F00_param[0x0C] = CODEC_MAKE_F00_0C(0x17)
                                                | CODEC_F00_0C_CAP_BALANCED_IO
                                                | CODEC_F00_0C_CAP_INPUT
                                                | CODEC_F00_0C_CAP_OUTPUT
                                                | CODEC_F00_0C_CAP_PRESENCE_DETECT
                                                | CODEC_F00_0C_CAP_TRIGGER_REQUIRED
                                                | CODEC_F00_0C_CAP_IMPENDANCE_SENSE;

            /* Default input amplifier capabilities. */
            pNode->node.au32F00_param[0x0D] = CODEC_MAKE_F00_0D(CODEC_AMP_CAP_MUTE,
                                                                CODEC_AMP_STEP_SIZE,
                                                                CODEC_AMP_NUM_STEPS,
                                                                CODEC_AMP_OFF_INITIAL);
            /* Default output amplifier capabilities. */
            pNode->node.au32F00_param[0x12] = CODEC_MAKE_F00_12(CODEC_AMP_CAP_MUTE,
                                                                CODEC_AMP_STEP_SIZE,
                                                                CODEC_AMP_NUM_STEPS,
                                                                CODEC_AMP_OFF_INITIAL);

            pNode->afg.node.au32F00_param[0x11] = CODEC_MAKE_F00_11(1, 1, 0, 0, 4);
            pNode->afg.node.au32F00_param[0x0F] = CODEC_F00_0F_D3
                                                | CODEC_F00_0F_D2
                                                | CODEC_F00_0F_D1
                                                | CODEC_F00_0F_D0;

            pNode->afg.u32F05_param = CODEC_MAKE_F05(0, 0, 0, CODEC_F05_D2, CODEC_F05_D2); /* PS-Act: D2, PS->Set D2. */
            pNode->afg.u32F08_param = 0;
            pNode->afg.u32F17_param = 0;
            break;
        }

        /*
         * DACs.
         */
        case STAC9220_NID_DAC0: /* DAC0: Headphones 0 + 1 */
        case STAC9220_NID_DAC1: /* DAC1: PIN C */
        case STAC9220_NID_DAC2: /* DAC2: PIN B */
        case STAC9220_NID_DAC3: /* DAC3: PIN F */
        {
            pNode->dac.u32A_param = CODEC_MAKE_A(HDA_SDFMT_TYPE_PCM, HDA_SDFMT_BASE_44KHZ,
                                                 HDA_SDFMT_MULT_1X, HDA_SDFMT_DIV_2X, HDA_SDFMT_16_BIT,
                                                 HDA_SDFMT_CHAN_STEREO);

            /* 7.3.4.6: Audio widget capabilities. */
            pNode->dac.node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_AUDIO_OUTPUT, 13, 0)
                                               | CODEC_F00_09_CAP_L_R_SWAP
                                               | CODEC_F00_09_CAP_POWER_CTRL
                                               | CODEC_F00_09_CAP_OUT_AMP_PRESENT
                                               | CODEC_F00_09_CAP_STEREO;

            /* Connection list; must be 0 if the only connection for the widget is
             * to the High Definition Audio Link. */
            pNode->dac.node.au32F00_param[0xE] = CODEC_MAKE_F00_0E(CODEC_F00_0E_LIST_NID_SHORT, 0 /* Entries */);

            pNode->dac.u32F05_param = CODEC_MAKE_F05(0, 0, 0, CODEC_F05_D3, CODEC_F05_D3);

            RT_ZERO(pNode->dac.B_params);
            AMPLIFIER_REGISTER(pNode->dac.B_params, AMPLIFIER_OUT, AMPLIFIER_LEFT,  0) = 0x7F | RT_BIT(7);
            AMPLIFIER_REGISTER(pNode->dac.B_params, AMPLIFIER_OUT, AMPLIFIER_RIGHT, 0) = 0x7F | RT_BIT(7);
            break;
        }

        /*
         * ADCs.
         */
        case STAC9220_NID_ADC0: /* Analog input. */
        {
            pNode->node.au32F02_param[0] = STAC9220_NID_AMP_ADC0;
            goto adc_init;
        }

        case STAC9220_NID_ADC1: /* Analog input (CD). */
        {
            pNode->node.au32F02_param[0] = STAC9220_NID_AMP_ADC1;

            /* Fall through is intentional. */
        adc_init:

            pNode->adc.u32A_param   = CODEC_MAKE_A(HDA_SDFMT_TYPE_PCM, HDA_SDFMT_BASE_44KHZ,
                                                   HDA_SDFMT_MULT_1X, HDA_SDFMT_DIV_2X, HDA_SDFMT_16_BIT,
                                                   HDA_SDFMT_CHAN_STEREO);

            pNode->adc.u32F03_param = RT_BIT(0);
            pNode->adc.u32F05_param = CODEC_MAKE_F05(0, 0, 0, CODEC_F05_D3, CODEC_F05_D3); /* PS-Act: D3 Set: D3 */

            pNode->adc.node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_AUDIO_INPUT, 0xD, 0)
                                               | CODEC_F00_09_CAP_POWER_CTRL
                                               | CODEC_F00_09_CAP_CONNECTION_LIST
                                               | CODEC_F00_09_CAP_PROC_WIDGET
                                               | CODEC_F00_09_CAP_STEREO;
            /* Connection list entries. */
            pNode->adc.node.au32F00_param[0xE] = CODEC_MAKE_F00_0E(CODEC_F00_0E_LIST_NID_SHORT, 1 /* Entries */);
            break;
        }

        /*
         * SP/DIF In/Out.
         */
        case STAC9220_NID_SPDIF_OUT:
        {
            pNode->spdifout.u32A_param   = CODEC_MAKE_A(HDA_SDFMT_TYPE_PCM, HDA_SDFMT_BASE_44KHZ,
                                                        HDA_SDFMT_MULT_1X, HDA_SDFMT_DIV_2X, HDA_SDFMT_16_BIT,
                                                        HDA_SDFMT_CHAN_STEREO);
            pNode->spdifout.u32F06_param = 0;
            pNode->spdifout.u32F0d_param = 0;

            pNode->spdifout.node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_AUDIO_OUTPUT, 4, 0)
                                                    | CODEC_F00_09_CAP_DIGITAL
                                                    | CODEC_F00_09_CAP_FMT_OVERRIDE
                                                    | CODEC_F00_09_CAP_STEREO;

            /* Use a fixed format from AFG. */
            pNode->spdifout.node.au32F00_param[0xA] = pThis->aNodes[STAC9220_NID_AFG].node.au32F00_param[0xA];
            pNode->spdifout.node.au32F00_param[0xB] = CODEC_F00_0B_PCM;
            break;
        }

        case STAC9220_NID_SPDIF_IN:
        {
            pNode->spdifin.u32A_param = CODEC_MAKE_A(HDA_SDFMT_TYPE_PCM, HDA_SDFMT_BASE_44KHZ,
                                                     HDA_SDFMT_MULT_1X, HDA_SDFMT_DIV_2X, HDA_SDFMT_16_BIT,
                                                     HDA_SDFMT_CHAN_STEREO);

            pNode->spdifin.node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_AUDIO_INPUT, 4, 0)
                                                   | CODEC_F00_09_CAP_DIGITAL
                                                   | CODEC_F00_09_CAP_CONNECTION_LIST
                                                   | CODEC_F00_09_CAP_FMT_OVERRIDE
                                                   | CODEC_F00_09_CAP_STEREO;

            /* Use a fixed format from AFG. */
            pNode->spdifin.node.au32F00_param[0xA] = pThis->aNodes[STAC9220_NID_AFG].node.au32F00_param[0xA];
            pNode->spdifin.node.au32F00_param[0xB] = CODEC_F00_0B_PCM;

            /* Connection list entries. */
            pNode->spdifin.node.au32F00_param[0xE] = CODEC_MAKE_F00_0E(CODEC_F00_0E_LIST_NID_SHORT, 1 /* Entries */);
            pNode->spdifin.node.au32F02_param[0]   = 0x11;
            break;
        }

        /*
         * PINs / Ports.
         */
        case STAC9220_NID_PIN_HEADPHONE0: /* Port A: Headphone in/out (front). */
        {
            pNode->port.u32F09_param = CODEC_MAKE_F09_ANALOG(0 /*fPresent*/, CODEC_F09_ANALOG_NA);

            pNode->port.node.au32F00_param[0xC] = CODEC_MAKE_F00_0C(0x17)
                                                | CODEC_F00_0C_CAP_INPUT
                                                | CODEC_F00_0C_CAP_OUTPUT
                                                | CODEC_F00_0C_CAP_HEADPHONE_AMP
                                                | CODEC_F00_0C_CAP_PRESENCE_DETECT
                                                | CODEC_F00_0C_CAP_TRIGGER_REQUIRED;

            /* Connection list entry 0: Goes to DAC0. */
            pNode->port.node.au32F02_param[0]   = STAC9220_NID_DAC0;

            if (!fInReset)
                pNode->port.u32F1c_param = CODEC_MAKE_F1C(CODEC_F1C_PORT_COMPLEX,
                                                          CODEC_F1C_LOCATION_FRONT,
                                                          CODEC_F1C_DEVICE_HP,
                                                          CODEC_F1C_CONNECTION_TYPE_1_8INCHES,
                                                          CODEC_F1C_COLOR_GREEN,
                                                          CODEC_F1C_MISC_NONE,
                                                          CODEC_F1C_ASSOCIATION_GROUP_1, 0x0 /* Seq */);
            goto port_init;
        }

        case STAC9220_NID_PIN_B: /* Port B: Rear CLFE (Center / Subwoofer). */
        {
            pNode->port.u32F09_param = CODEC_MAKE_F09_ANALOG(1 /*fPresent*/, CODEC_F09_ANALOG_NA);

            pNode->port.node.au32F00_param[0xC] = CODEC_MAKE_F00_0C(0x17)
                                                | CODEC_F00_0C_CAP_INPUT
                                                | CODEC_F00_0C_CAP_OUTPUT
                                                | CODEC_F00_0C_CAP_PRESENCE_DETECT
                                                | CODEC_F00_0C_CAP_TRIGGER_REQUIRED;

            /* Connection list entry 0: Goes to DAC2. */
            pNode->port.node.au32F02_param[0]   = STAC9220_NID_DAC2;

            if (!fInReset)
                pNode->port.u32F1c_param = CODEC_MAKE_F1C(CODEC_F1C_PORT_COMPLEX,
                                                          CODEC_F1C_LOCATION_REAR,
                                                          CODEC_F1C_DEVICE_SPEAKER,
                                                          CODEC_F1C_CONNECTION_TYPE_1_8INCHES,
                                                          CODEC_F1C_COLOR_BLACK,
                                                          CODEC_F1C_MISC_NONE,
                                                          CODEC_F1C_ASSOCIATION_GROUP_0, 0x1 /* Seq */);
            goto port_init;
        }

        case STAC9220_NID_PIN_C: /* Rear Speaker. */
        {
            pNode->port.u32F09_param = CODEC_MAKE_F09_ANALOG(1 /*fPresent*/, CODEC_F09_ANALOG_NA);

            pNode->port.node.au32F00_param[0xC] = CODEC_MAKE_F00_0C(0x17)
                                                | CODEC_F00_0C_CAP_INPUT
                                                | CODEC_F00_0C_CAP_OUTPUT
                                                | CODEC_F00_0C_CAP_PRESENCE_DETECT
                                                | CODEC_F00_0C_CAP_TRIGGER_REQUIRED;

            /* Connection list entry 0: Goes to DAC1. */
            pNode->port.node.au32F02_param[0x0] = STAC9220_NID_DAC1;

            if (!fInReset)
                pNode->port.u32F1c_param = CODEC_MAKE_F1C(CODEC_F1C_PORT_COMPLEX,
                                                          CODEC_F1C_LOCATION_REAR,
                                                          CODEC_F1C_DEVICE_SPEAKER,
                                                          CODEC_F1C_CONNECTION_TYPE_1_8INCHES,
                                                          CODEC_F1C_COLOR_GREEN,
                                                          CODEC_F1C_MISC_NONE,
                                                          CODEC_F1C_ASSOCIATION_GROUP_0, 0x0 /* Seq */);
            goto port_init;
        }

        case STAC9220_NID_PIN_HEADPHONE1: /* Also known as PIN_D. */
        {
            pNode->port.u32F09_param = CODEC_MAKE_F09_ANALOG(1 /*fPresent*/, CODEC_F09_ANALOG_NA);

            pNode->port.node.au32F00_param[0xC] = CODEC_MAKE_F00_0C(0x17)
                                                | CODEC_F00_0C_CAP_INPUT
                                                | CODEC_F00_0C_CAP_OUTPUT
                                                | CODEC_F00_0C_CAP_HEADPHONE_AMP
                                                | CODEC_F00_0C_CAP_PRESENCE_DETECT
                                                | CODEC_F00_0C_CAP_TRIGGER_REQUIRED;

            /* Connection list entry 0: Goes to DAC1. */
            pNode->port.node.au32F02_param[0x0] = STAC9220_NID_DAC0;

            if (!fInReset)
                pNode->port.u32F1c_param = CODEC_MAKE_F1C(CODEC_F1C_PORT_COMPLEX,
                                                          CODEC_F1C_LOCATION_FRONT,
                                                          CODEC_F1C_DEVICE_MIC,
                                                          CODEC_F1C_CONNECTION_TYPE_1_8INCHES,
                                                          CODEC_F1C_COLOR_PINK,
                                                          CODEC_F1C_MISC_NONE,
                                                          CODEC_F1C_ASSOCIATION_GROUP_15, 0x0 /* Ignored */);
            /* Fall through is intentional. */

        port_init:

            pNode->port.u32F07_param = CODEC_F07_IN_ENABLE
                                     | CODEC_F07_OUT_ENABLE;
            pNode->port.u32F08_param = 0;

            pNode->port.node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_PIN_COMPLEX, 0, 0)
                                                | CODEC_F00_09_CAP_CONNECTION_LIST
                                                | CODEC_F00_09_CAP_UNSOL
                                                | CODEC_F00_09_CAP_STEREO;
            /* Connection list entries. */
            pNode->port.node.au32F00_param[0xE] = CODEC_MAKE_F00_0E(CODEC_F00_0E_LIST_NID_SHORT, 1 /* Entries */);
            break;
        }

        case STAC9220_NID_PIN_E:
        {
            pNode->port.u32F07_param = CODEC_F07_IN_ENABLE;
            pNode->port.u32F08_param = 0;
            /* If Line in is reported as enabled, OS X sees no speakers! Windows does
             * not care either way, although Linux does.
             */
            pNode->port.u32F09_param = CODEC_MAKE_F09_ANALOG(0 /* fPresent */, 0);

            pNode->port.node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_PIN_COMPLEX, 0, 0)
                                                | CODEC_F00_09_CAP_UNSOL
                                                | CODEC_F00_09_CAP_STEREO;

            pNode->port.node.au32F00_param[0xC] = CODEC_F00_0C_CAP_INPUT
                                                | CODEC_F00_0C_CAP_PRESENCE_DETECT;

            if (!fInReset)
                pNode->port.u32F1c_param = CODEC_MAKE_F1C(CODEC_F1C_PORT_COMPLEX,
                                                          CODEC_F1C_LOCATION_REAR,
                                                          CODEC_F1C_DEVICE_LINE_IN,
                                                          CODEC_F1C_CONNECTION_TYPE_1_8INCHES,
                                                          CODEC_F1C_COLOR_BLUE,
                                                          CODEC_F1C_MISC_NONE,
                                                          CODEC_F1C_ASSOCIATION_GROUP_4, 0x1 /* Seq */);
            break;
        }

        case STAC9220_NID_PIN_F:
        {
            pNode->port.u32F07_param = CODEC_F07_IN_ENABLE | CODEC_F07_OUT_ENABLE;
            pNode->port.u32F08_param = 0;
            pNode->port.u32F09_param = CODEC_MAKE_F09_ANALOG(1 /* fPresent */, CODEC_F09_ANALOG_NA);

            pNode->port.node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_PIN_COMPLEX, 0, 0)
                                                | CODEC_F00_09_CAP_CONNECTION_LIST
                                                | CODEC_F00_09_CAP_UNSOL
                                                | CODEC_F00_09_CAP_OUT_AMP_PRESENT
                                                | CODEC_F00_09_CAP_STEREO;

            pNode->port.node.au32F00_param[0xC] = CODEC_F00_0C_CAP_INPUT
                                                | CODEC_F00_0C_CAP_OUTPUT;

            /* Connection list entry 0: Goes to DAC3. */
            pNode->port.node.au32F00_param[0xE] = CODEC_MAKE_F00_0E(CODEC_F00_0E_LIST_NID_SHORT, 1 /* Entries */);
            pNode->port.node.au32F02_param[0x0] = STAC9220_NID_DAC3;

            if (!fInReset)
                pNode->port.u32F1c_param = CODEC_MAKE_F1C(CODEC_F1C_PORT_COMPLEX,
                                                          CODEC_F1C_LOCATION_INTERNAL,
                                                          CODEC_F1C_DEVICE_SPEAKER,
                                                          CODEC_F1C_CONNECTION_TYPE_1_8INCHES,
                                                          CODEC_F1C_COLOR_ORANGE,
                                                          CODEC_F1C_MISC_NONE,
                                                          CODEC_F1C_ASSOCIATION_GROUP_0, 0x2 /* Seq */);
            break;
        }

        case STAC9220_NID_PIN_SPDIF_OUT: /* Rear SPDIF Out. */
        {
            pNode->digout.u32F07_param = CODEC_F07_OUT_ENABLE;
            pNode->digout.u32F09_param = 0;

            pNode->digout.node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_PIN_COMPLEX, 0, 0)
                                                  | CODEC_F00_09_CAP_DIGITAL
                                                  | CODEC_F00_09_CAP_CONNECTION_LIST
                                                  | CODEC_F00_09_CAP_STEREO;
            pNode->digout.node.au32F00_param[0xC] = CODEC_F00_0C_CAP_OUTPUT;

            /* Connection list entries. */
            pNode->digout.node.au32F00_param[0xE] = CODEC_MAKE_F00_0E(CODEC_F00_0E_LIST_NID_SHORT, 3 /* Entries */);
            pNode->digout.node.au32F02_param[0x0] = RT_MAKE_U32_FROM_U8(STAC9220_NID_SPDIF_OUT,
                                                                        STAC9220_NID_AMP_ADC0, STAC9221_NID_ADAT_OUT, 0);
            if (!fInReset)
                pNode->digout.u32F1c_param = CODEC_MAKE_F1C(CODEC_F1C_PORT_COMPLEX,
                                                            CODEC_F1C_LOCATION_REAR,
                                                            CODEC_F1C_DEVICE_SPDIF_OUT,
                                                            CODEC_F1C_CONNECTION_TYPE_DIN,
                                                            CODEC_F1C_COLOR_BLACK,
                                                            CODEC_F1C_MISC_NONE,
                                                            CODEC_F1C_ASSOCIATION_GROUP_2, 0x0 /* Seq */);
            break;
        }

        case STAC9220_NID_PIN_SPDIF_IN:
        {
            pNode->digin.u32F05_param = CODEC_MAKE_F05(0, 0, 0, CODEC_F05_D3, CODEC_F05_D3); /* PS-Act: D3 -> D3 */
            pNode->digin.u32F07_param = CODEC_F07_IN_ENABLE;
            pNode->digin.u32F08_param = 0;
            pNode->digin.u32F09_param = CODEC_MAKE_F09_DIGITAL(0, 0);
            pNode->digin.u32F0c_param = 0;

            pNode->digin.node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_PIN_COMPLEX, 3, 0)
                                                 | CODEC_F00_09_CAP_POWER_CTRL
                                                 | CODEC_F00_09_CAP_DIGITAL
                                                 | CODEC_F00_09_CAP_UNSOL
                                                 | CODEC_F00_09_CAP_STEREO;

            pNode->digin.node.au32F00_param[0xC] = CODEC_F00_0C_CAP_EAPD
                                                 | CODEC_F00_0C_CAP_INPUT
                                                 | CODEC_F00_0C_CAP_PRESENCE_DETECT;
            if (!fInReset)
                pNode->digin.u32F1c_param = CODEC_MAKE_F1C(CODEC_F1C_PORT_COMPLEX,
                                                           CODEC_F1C_LOCATION_REAR,
                                                           CODEC_F1C_DEVICE_SPDIF_IN,
                                                           CODEC_F1C_CONNECTION_TYPE_OTHER_DIGITAL,
                                                           CODEC_F1C_COLOR_BLACK,
                                                           CODEC_F1C_MISC_NONE,
                                                           CODEC_F1C_ASSOCIATION_GROUP_5, 0x0 /* Seq */);
            break;
        }

        case STAC9220_NID_ADC0_MUX:
        {
            pNode->adcmux.u32F01_param = 0; /* Connection select control index (STAC9220_NID_PIN_E). */
            goto adcmux_init;
        }

        case STAC9220_NID_ADC1_MUX:
        {
            pNode->adcmux.u32F01_param = 1; /* Connection select control index (STAC9220_NID_PIN_CD). */
            /* Fall through is intentional. */

        adcmux_init:

            pNode->adcmux.node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_AUDIO_SELECTOR, 0, 0)
                                                  | CODEC_F00_09_CAP_CONNECTION_LIST
                                                  | CODEC_F00_09_CAP_AMP_FMT_OVERRIDE
                                                  | CODEC_F00_09_CAP_OUT_AMP_PRESENT
                                                  | CODEC_F00_09_CAP_STEREO;

            pNode->adcmux.node.au32F00_param[0xD] = CODEC_MAKE_F00_0D(0, 27, 4, 0);

            /* Connection list entries. */
            pNode->adcmux.node.au32F00_param[0xE] = CODEC_MAKE_F00_0E(CODEC_F00_0E_LIST_NID_SHORT, 7 /* Entries */);
            pNode->adcmux.node.au32F02_param[0x0] = RT_MAKE_U32_FROM_U8(STAC9220_NID_PIN_E,
                                                                        STAC9220_NID_PIN_CD,
                                                                        STAC9220_NID_PIN_F,
                                                                        STAC9220_NID_PIN_B);
            pNode->adcmux.node.au32F02_param[0x4] = RT_MAKE_U32_FROM_U8(STAC9220_NID_PIN_C,
                                                                        STAC9220_NID_PIN_HEADPHONE1,
                                                                        STAC9220_NID_PIN_HEADPHONE0,
                                                                        0x0 /* Unused */);

            /* STAC 9220 v10 6.21-22.{4,5} both(left and right) out amplifiers initialized with 0. */
            RT_ZERO(pNode->adcmux.B_params);
            break;
        }

        case STAC9220_NID_PCBEEP:
        {
            pNode->pcbeep.u32F0a_param = 0;

            pNode->pcbeep.node.au32F00_param[0x9]  = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_BEEP_GEN, 0, 0)
                                                   | CODEC_F00_09_CAP_AMP_FMT_OVERRIDE
                                                   | CODEC_F00_09_CAP_OUT_AMP_PRESENT;
            pNode->pcbeep.node.au32F00_param[0xD]  = CODEC_MAKE_F00_0D(0, 17, 3, 3);

            RT_ZERO(pNode->pcbeep.B_params);
            break;
        }

        case STAC9220_NID_PIN_CD:
        {
            pNode->cdnode.node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_PIN_COMPLEX, 0, 0)
                                                  | CODEC_F00_09_CAP_STEREO;
            pNode->cdnode.node.au32F00_param[0xC] = CODEC_F00_0C_CAP_INPUT;

            if (!fInReset)
                pNode->cdnode.u32F1c_param = CODEC_MAKE_F1C(CODEC_F1C_PORT_FIXED,
                                                            CODEC_F1C_LOCATION_INTERNAL,
                                                            CODEC_F1C_DEVICE_CD,
                                                            CODEC_F1C_CONNECTION_TYPE_ATAPI,
                                                            CODEC_F1C_COLOR_UNKNOWN,
                                                            CODEC_F1C_MISC_NONE,
                                                            CODEC_F1C_ASSOCIATION_GROUP_4, 0x2 /* Seq */);
            break;
        }

        case STAC9220_NID_VOL_KNOB:
        {
            pNode->volumeKnob.u32F08_param = 0;
            pNode->volumeKnob.u32F0f_param = 0x7f;

            pNode->volumeKnob.node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_VOLUME_KNOB, 0, 0);
            pNode->volumeKnob.node.au32F00_param[0xD] = RT_BIT(7) | 0x7F;

            /* Connection list entries. */
            pNode->volumeKnob.node.au32F00_param[0xE] = CODEC_MAKE_F00_0E(CODEC_F00_0E_LIST_NID_SHORT, 4 /* Entries */);
            pNode->volumeKnob.node.au32F02_param[0x0] = RT_MAKE_U32_FROM_U8(STAC9220_NID_DAC0,
                                                                            STAC9220_NID_DAC1,
                                                                            STAC9220_NID_DAC2,
                                                                            STAC9220_NID_DAC3);
            break;
        }

        case STAC9220_NID_AMP_ADC0: /* ADC0Vol */
        {
            pNode->adcvol.node.au32F02_param[0] = STAC9220_NID_ADC0_MUX;
            goto adcvol_init;
        }

        case STAC9220_NID_AMP_ADC1: /* ADC1Vol */
        {
            pNode->adcvol.node.au32F02_param[0] = STAC9220_NID_ADC1_MUX;
            /* Fall through is intentional. */

        adcvol_init:

            pNode->adcvol.node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_AUDIO_SELECTOR, 0, 0)
                                                  | CODEC_F00_09_CAP_L_R_SWAP
                                                  | CODEC_F00_09_CAP_CONNECTION_LIST
                                                  | CODEC_F00_09_CAP_IN_AMP_PRESENT
                                                  | CODEC_F00_09_CAP_STEREO;


            pNode->adcvol.node.au32F00_param[0xE] = CODEC_MAKE_F00_0E(CODEC_F00_0E_LIST_NID_SHORT, 1 /* Entries */);

            RT_ZERO(pNode->adcvol.B_params);
            AMPLIFIER_REGISTER(pNode->adcvol.B_params, AMPLIFIER_IN, AMPLIFIER_LEFT,  0) = RT_BIT(7);
            AMPLIFIER_REGISTER(pNode->adcvol.B_params, AMPLIFIER_IN, AMPLIFIER_RIGHT, 0) = RT_BIT(7);
            break;
        }

        /*
         * STAC9221 nodes.
         */

        case STAC9221_NID_ADAT_OUT:
        {
            pNode->node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_VENDOR_DEFINED, 3, 0)
                                           | CODEC_F00_09_CAP_DIGITAL
                                           | CODEC_F00_09_CAP_STEREO;
            break;
        }

        case STAC9221_NID_I2S_OUT:
        {
            pNode->node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_AUDIO_OUTPUT, 3, 0)
                                           | CODEC_F00_09_CAP_DIGITAL
                                           | CODEC_F00_09_CAP_STEREO;
            break;
        }

        case STAC9221_NID_PIN_I2S_OUT:
        {
            pNode->node.au32F00_param[0x9] = CODEC_MAKE_F00_09(CODEC_F00_09_TYPE_PIN_COMPLEX, 0, 0)
                                           | CODEC_F00_09_CAP_DIGITAL
                                           | CODEC_F00_09_CAP_CONNECTION_LIST
                                           | CODEC_F00_09_CAP_STEREO;

            pNode->node.au32F00_param[0xC] = CODEC_F00_0C_CAP_OUTPUT;

            /* Connection list entries. */
            pNode->node.au32F00_param[0xE] = CODEC_MAKE_F00_0E(CODEC_F00_0E_LIST_NID_SHORT, 1 /* Entries */);
            pNode->node.au32F02_param[0]   = STAC9221_NID_I2S_OUT;

            if (!fInReset)
                pNode->reserved.u32F1c_param = CODEC_MAKE_F1C(CODEC_F1C_PORT_NO_PHYS,
                                                              CODEC_F1C_LOCATION_NA,
                                                              CODEC_F1C_DEVICE_LINE_OUT,
                                                              CODEC_F1C_CONNECTION_TYPE_UNKNOWN,
                                                              CODEC_F1C_COLOR_UNKNOWN,
                                                              CODEC_F1C_MISC_NONE,
                                                              CODEC_F1C_ASSOCIATION_GROUP_15, 0x0 /* Ignored */);
            break;
        }

        default:
            AssertMsgFailed(("Node %RU8 not implemented\n", uNID));
            break;
    }
}


/**
 * Resets the codec with all its connected nodes.
 *
 * @param   pThis               HDA codec to reset.
 */
static void stac9220Reset(PHDACODECR3 pThis)
{
    AssertPtrReturnVoid(pThis->aNodes);

    LogRel(("HDA: Codec reset\n"));

    uint8_t const cTotalNodes = (uint8_t)RT_MIN(pThis->Cfg.cTotalNodes, RT_ELEMENTS(pThis->aNodes));
    for (uint8_t i = 0; i < cTotalNodes; i++)
        stac9220NodeReset(pThis, i, &pThis->aNodes[i], true /*fInReset*/);
}


static int stac9220Construct(PHDACODECR3 pThis, HDACODECCFG *pCfg)
{
    /*
     * Note: The Linux kernel uses "patch_stac922x" for the fixups,
     *       which in turn uses "ref922x_pin_configs" for the configuration
     *       defaults tweaking in sound/pci/hda/patch_sigmatel.c.
     */
    pCfg->idVendor          = 0x8384; /* SigmaTel */
    pCfg->idDevice          = 0x7680; /* STAC9221 A1 */
    pCfg->bBSKU             = 0x76;
    pCfg->idAssembly        = 0x80;

    AssertCompile(STAC9221_NUM_NODES <= RT_ELEMENTS(pThis->aNodes));
    pCfg->cTotalNodes       = STAC9221_NUM_NODES;
    pCfg->idxAdcVolsLineIn  = STAC9220_NID_AMP_ADC0; /* We treat ADC0 as Line-In. */
    pCfg->idxAdcVolsMicIn   = STAC9220_NID_AMP_ADC1; /* We treat ADC1 as Mic-In. */
    pCfg->idxDacLineOut     = STAC9220_NID_DAC1;

    /* Copy over the node class lists and popuplate afNodeClassifications. */
#define STAC9220WIDGET(a_Type) do { \
            AssertCompile(RT_ELEMENTS(g_abStac9220##a_Type##s) <= RT_ELEMENTS(pCfg->ab##a_Type##s)); \
            uint8_t  *pbDst = (uint8_t *)&pCfg->ab##a_Type##s[0]; \
            uintptr_t i; \
            for (i = 0; i < RT_ELEMENTS(g_abStac9220##a_Type##s); i++) \
            { \
                uint8_t const idNode = g_abStac9220##a_Type##s[i]; \
                if (idNode == 0) \
                    break; \
                AssertReturn(idNode < RT_ELEMENTS(pThis->aNodes), VERR_INTERNAL_ERROR_3); \
                pCfg->afNodeClassifications[idNode] |= RT_CONCAT(CODEC_NODE_CLS_,a_Type); \
                pbDst[i] = idNode; \
            } \
            Assert(i + 1 == RT_ELEMENTS(g_abStac9220##a_Type##s)); \
            for (; i < RT_ELEMENTS(pCfg->ab##a_Type##s); i++) \
                pbDst[i] = 0; \
        } while (0)
    STAC9220WIDGET(Port);
    STAC9220WIDGET(Dac);
    STAC9220WIDGET(Adc);
    STAC9220WIDGET(AdcVol);
    STAC9220WIDGET(AdcMux);
    STAC9220WIDGET(Pcbeep);
    STAC9220WIDGET(SpdifIn);
    STAC9220WIDGET(SpdifOut);
    STAC9220WIDGET(DigInPin);
    STAC9220WIDGET(DigOutPin);
    STAC9220WIDGET(Cd);
    STAC9220WIDGET(VolKnob);
    STAC9220WIDGET(Reserved);
#undef STAC9220WIDGET

    /*
     * Initialize all codec nodes.
     * This is specific to the codec, so do this here.
     *
     * Note: Do *not* call stac9220Reset() here, as this would not
     *       initialize the node default configuration values then!
     */
    for (uint8_t i = 0; i < STAC9221_NUM_NODES; i++)
        stac9220NodeReset(pThis, i, &pThis->aNodes[i], false /*fInReset*/);

    /* Common root node initializers. */
    pThis->aNodes[STAC9220_NID_ROOT].root.node.au32F00_param[0] = CODEC_MAKE_F00_00(pCfg->idVendor, pCfg->idDevice);
    pThis->aNodes[STAC9220_NID_ROOT].root.node.au32F00_param[4] = CODEC_MAKE_F00_04(0x1, 0x1);

    /* Common AFG node initializers. */
    pThis->aNodes[STAC9220_NID_AFG].afg.node.au32F00_param[0x4] = CODEC_MAKE_F00_04(0x2, STAC9221_NUM_NODES - 2);
    pThis->aNodes[STAC9220_NID_AFG].afg.node.au32F00_param[0x5] = CODEC_MAKE_F00_05(1, CODEC_F00_05_AFG);
    pThis->aNodes[STAC9220_NID_AFG].afg.node.au32F00_param[0xA] = CODEC_F00_0A_44_1KHZ | CODEC_F00_0A_16_BIT;
    pThis->aNodes[STAC9220_NID_AFG].afg.u32F20_param = CODEC_MAKE_F20(pCfg->idVendor, pCfg->bBSKU, pCfg->idAssembly);

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Common Helpers                                                                                                               *
*********************************************************************************************************************************/

/*
 * Some generic predicate functions.
 */
#define HDA_CODEC_IS_NODE_OF_TYPE_FUNC(a_Type) \
    DECLINLINE(bool) hdaCodecIs##a_Type##Node(PHDACODECR3 pThis, uint8_t idNode) \
    { \
        Assert(idNode < RT_ELEMENTS(pThis->Cfg.afNodeClassifications)); \
        Assert(   (memchr(&pThis->Cfg.RT_CONCAT3(ab,a_Type,s)[0], idNode, sizeof(pThis->Cfg.RT_CONCAT3(ab,a_Type,s))) != NULL) \
               == RT_BOOL(pThis->Cfg.afNodeClassifications[idNode] & RT_CONCAT(CODEC_NODE_CLS_,a_Type))); \
        return RT_BOOL(pThis->Cfg.afNodeClassifications[idNode] & RT_CONCAT(CODEC_NODE_CLS_,a_Type)); \
    }
/* hdaCodecIsPortNode */
HDA_CODEC_IS_NODE_OF_TYPE_FUNC(Port)
/* hdaCodecIsDacNode */
HDA_CODEC_IS_NODE_OF_TYPE_FUNC(Dac)
/* hdaCodecIsAdcVolNode */
HDA_CODEC_IS_NODE_OF_TYPE_FUNC(AdcVol)
/* hdaCodecIsAdcNode */
HDA_CODEC_IS_NODE_OF_TYPE_FUNC(Adc)
/* hdaCodecIsAdcMuxNode */
HDA_CODEC_IS_NODE_OF_TYPE_FUNC(AdcMux)
/* hdaCodecIsPcbeepNode */
HDA_CODEC_IS_NODE_OF_TYPE_FUNC(Pcbeep)
/* hdaCodecIsSpdifOutNode */
HDA_CODEC_IS_NODE_OF_TYPE_FUNC(SpdifOut)
/* hdaCodecIsSpdifInNode */
HDA_CODEC_IS_NODE_OF_TYPE_FUNC(SpdifIn)
/* hdaCodecIsDigInPinNode */
HDA_CODEC_IS_NODE_OF_TYPE_FUNC(DigInPin)
/* hdaCodecIsDigOutPinNode */
HDA_CODEC_IS_NODE_OF_TYPE_FUNC(DigOutPin)
/* hdaCodecIsCdNode */
HDA_CODEC_IS_NODE_OF_TYPE_FUNC(Cd)
/* hdaCodecIsVolKnobNode */
HDA_CODEC_IS_NODE_OF_TYPE_FUNC(VolKnob)
/* hdaCodecIsReservedNode */
HDA_CODEC_IS_NODE_OF_TYPE_FUNC(Reserved)


/*
 * Misc helpers.
 */
static int hdaR3CodecToAudVolume(PHDACODECR3 pThis, PCODECNODE pNode, AMPLIFIER *pAmp, PDMAUDIOMIXERCTL enmMixerCtl)
{
    RT_NOREF(pNode);

    uint8_t iDir;
    switch (enmMixerCtl)
    {
        case PDMAUDIOMIXERCTL_VOLUME_MASTER:
        case PDMAUDIOMIXERCTL_FRONT:
            iDir = AMPLIFIER_OUT;
            break;
        case PDMAUDIOMIXERCTL_LINE_IN:
        case PDMAUDIOMIXERCTL_MIC_IN:
            iDir = AMPLIFIER_IN;
            break;
        default:
            AssertMsgFailedReturn(("Invalid mixer control %RU32\n", enmMixerCtl), VERR_INVALID_PARAMETER);
            break;
    }

    int iMute;
    iMute  = AMPLIFIER_REGISTER(*pAmp, iDir, AMPLIFIER_LEFT,  0) & RT_BIT(7);
    iMute |= AMPLIFIER_REGISTER(*pAmp, iDir, AMPLIFIER_RIGHT, 0) & RT_BIT(7);
    iMute >>=7;
    iMute &= 0x1;

    uint8_t bLeft = AMPLIFIER_REGISTER(*pAmp, iDir, AMPLIFIER_LEFT,  0) & 0x7f;
    uint8_t bRight = AMPLIFIER_REGISTER(*pAmp, iDir, AMPLIFIER_RIGHT, 0) & 0x7f;

    /*
     * The STAC9220 volume controls have 0 to -96dB attenuation range in 128 steps.
     * We have 0 to -96dB range in 256 steps. HDA volume setting of 127 must map
     * to 255 internally (0dB), while HDA volume setting of 0 (-96dB) should map
     * to 1 (rather than zero) internally.
     */
    bLeft = (bLeft + 1) * (2 * 255) / 256;
    bRight = (bRight + 1) * (2 * 255) / 256;

    PDMAUDIOVOLUME Vol;
    PDMAudioVolumeInitFromStereo(&Vol, RT_BOOL(iMute), bLeft, bRight);

    LogFunc(("[NID0x%02x] %RU8/%RU8%s\n", pNode->node.uID, bLeft, bRight, Vol.fMuted ? "- Muted!" : ""));
    LogRel2(("HDA: Setting volume for mixer control '%s' to %RU8/%RU8%s\n",
             PDMAudioMixerCtlGetName(enmMixerCtl), bLeft, bRight, Vol.fMuted ? "- Muted!" : ""));

    return hdaR3MixerSetVolume(pThis, enmMixerCtl, &Vol);
}


DECLINLINE(void) hdaCodecSetRegister(uint32_t *pu32Reg, uint32_t u32Cmd, uint8_t u8Offset, uint32_t mask)
{
    Assert((pu32Reg && u8Offset < 32));
    *pu32Reg &= ~(mask << u8Offset);
    *pu32Reg |= (u32Cmd & mask) << u8Offset;
}

DECLINLINE(void) hdaCodecSetRegisterU8(uint32_t *pu32Reg, uint32_t u32Cmd, uint8_t u8Offset)
{
    hdaCodecSetRegister(pu32Reg, u32Cmd, u8Offset, CODEC_VERB_8BIT_DATA);
}

DECLINLINE(void) hdaCodecSetRegisterU16(uint32_t *pu32Reg, uint32_t u32Cmd, uint8_t u8Offset)
{
    hdaCodecSetRegister(pu32Reg, u32Cmd, u8Offset, CODEC_VERB_16BIT_DATA);
}


/*********************************************************************************************************************************
*   Verb Processor Functions.                                                                                                    *
*********************************************************************************************************************************/
#if 0 /* unused */

/**
 * @interface_method_impl{CODECVERB,pfn, Unimplemented}
 */
static DECLCALLBACK(int) vrbProcUnimplemented(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    RT_NOREF(pThis, uCmd);
    LogFlowFunc(("uCmd(raw:%x: cad:%x, d:%c, nid:%x, verb:%x)\n", uCmd,
                 CODEC_CAD(uCmd), CODEC_DIRECT(uCmd) ? 'N' : 'Y', CODEC_NID(uCmd), CODEC_VERBDATA(uCmd)));
    *puResp = 0;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, ??? }
 */
static DECLCALLBACK(int) vrbProcBreak(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    int rc;
    rc = vrbProcUnimplemented(pThis, uCmd, puResp);
    *puResp |= CODEC_RESPONSE_UNSOLICITED;
    return rc;
}

#endif /* unused */

/**
 * @interface_method_impl{CODECVERB,pfn, b-- }
 */
static DECLCALLBACK(int) vrbProcGetAmplifier(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    /* HDA spec 7.3.3.7 Note A */
    /** @todo If index out of range response should be 0. */
    uint8_t u8Index = CODEC_GET_AMP_DIRECTION(uCmd) == AMPLIFIER_OUT ? 0 : CODEC_GET_AMP_INDEX(uCmd);

    PCODECNODE pNode = &pThis->aNodes[CODEC_NID(uCmd)];
    if (hdaCodecIsDacNode(pThis, CODEC_NID(uCmd)))
        *puResp = AMPLIFIER_REGISTER(pNode->dac.B_params,
                                     CODEC_GET_AMP_DIRECTION(uCmd),
                                     CODEC_GET_AMP_SIDE(uCmd),
                                     u8Index);
    else if (hdaCodecIsAdcVolNode(pThis, CODEC_NID(uCmd)))
        *puResp = AMPLIFIER_REGISTER(pNode->adcvol.B_params,
                                     CODEC_GET_AMP_DIRECTION(uCmd),
                                     CODEC_GET_AMP_SIDE(uCmd),
                                     u8Index);
    else if (hdaCodecIsAdcMuxNode(pThis, CODEC_NID(uCmd)))
        *puResp = AMPLIFIER_REGISTER(pNode->adcmux.B_params,
                                     CODEC_GET_AMP_DIRECTION(uCmd),
                                     CODEC_GET_AMP_SIDE(uCmd),
                                     u8Index);
    else if (hdaCodecIsPcbeepNode(pThis, CODEC_NID(uCmd)))
        *puResp = AMPLIFIER_REGISTER(pNode->pcbeep.B_params,
                                     CODEC_GET_AMP_DIRECTION(uCmd),
                                     CODEC_GET_AMP_SIDE(uCmd),
                                     u8Index);
    else if (hdaCodecIsPortNode(pThis, CODEC_NID(uCmd)))
        *puResp = AMPLIFIER_REGISTER(pNode->port.B_params,
                                     CODEC_GET_AMP_DIRECTION(uCmd),
                                     CODEC_GET_AMP_SIDE(uCmd),
                                     u8Index);
    else if (hdaCodecIsAdcNode(pThis, CODEC_NID(uCmd)))
        *puResp = AMPLIFIER_REGISTER(pNode->adc.B_params,
                                     CODEC_GET_AMP_DIRECTION(uCmd),
                                     CODEC_GET_AMP_SIDE(uCmd),
                                     u8Index);
    else
        LogRel2(("HDA: Warning: Unhandled get amplifier command: 0x%x (NID=0x%x [%RU8])\n", uCmd, CODEC_NID(uCmd), CODEC_NID(uCmd)));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, ??? }
 */
static DECLCALLBACK(int) vrbProcGetParameter(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    Assert((uCmd & CODEC_VERB_8BIT_DATA) < CODECNODE_F00_PARAM_LENGTH);
    if ((uCmd & CODEC_VERB_8BIT_DATA) >= CODECNODE_F00_PARAM_LENGTH)
    {
        *puResp = 0;

        LogFlowFunc(("invalid F00 parameter %d\n", (uCmd & CODEC_VERB_8BIT_DATA)));
        return VINF_SUCCESS;
    }

    *puResp = pThis->aNodes[CODEC_NID(uCmd)].node.au32F00_param[uCmd & CODEC_VERB_8BIT_DATA];
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, f01 }
 */
static DECLCALLBACK(int) vrbProcGetConSelectCtrl(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsAdcMuxNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].adcmux.u32F01_param;
    else if (hdaCodecIsDigOutPinNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].digout.u32F01_param;
    else if (hdaCodecIsPortNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].port.u32F01_param;
    else if (hdaCodecIsAdcNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].adc.u32F01_param;
    else if (hdaCodecIsAdcVolNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].adcvol.u32F01_param;
    else
        LogRel2(("HDA: Warning: Unhandled get connection select control command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 701 }
 */
static DECLCALLBACK(int) vrbProcSetConSelectCtrl(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    uint32_t *pu32Reg = NULL;
    if (hdaCodecIsAdcMuxNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].adcmux.u32F01_param;
    else if (hdaCodecIsDigOutPinNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].digout.u32F01_param;
    else if (hdaCodecIsPortNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].port.u32F01_param;
    else if (hdaCodecIsAdcNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].adc.u32F01_param;
    else if (hdaCodecIsAdcVolNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].adcvol.u32F01_param;
    else
        LogRel2(("HDA: Warning: Unhandled set connection select control command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    if (pu32Reg)
        hdaCodecSetRegisterU8(pu32Reg, uCmd, 0);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, f07 }
 */
static DECLCALLBACK(int) vrbProcGetPinCtrl(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsPortNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].port.u32F07_param;
    else if (hdaCodecIsDigOutPinNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].digout.u32F07_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].digin.u32F07_param;
    else if (hdaCodecIsCdNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].cdnode.u32F07_param;
    else if (hdaCodecIsPcbeepNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].pcbeep.u32F07_param;
    else if (hdaCodecIsReservedNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].reserved.u32F07_param;
    else
        LogRel2(("HDA: Warning: Unhandled get pin control command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 707 }
 */
static DECLCALLBACK(int) vrbProcSetPinCtrl(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    uint32_t *pu32Reg = NULL;
    if (hdaCodecIsPortNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].port.u32F07_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].digin.u32F07_param;
    else if (hdaCodecIsDigOutPinNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].digout.u32F07_param;
    else if (hdaCodecIsCdNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].cdnode.u32F07_param;
    else if (hdaCodecIsPcbeepNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].pcbeep.u32F07_param;
    else if (   hdaCodecIsReservedNode(pThis, CODEC_NID(uCmd))
             && CODEC_NID(uCmd) == 0x1b)
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].reserved.u32F07_param;
    else
        LogRel2(("HDA: Warning: Unhandled set pin control command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    if (pu32Reg)
        hdaCodecSetRegisterU8(pu32Reg, uCmd, 0);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, f08 }
 */
static DECLCALLBACK(int) vrbProcGetUnsolicitedEnabled(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsPortNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].port.u32F08_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].digin.u32F08_param;
    else if ((uCmd) == STAC9220_NID_AFG)
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].afg.u32F08_param;
    else if (hdaCodecIsVolKnobNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].volumeKnob.u32F08_param;
    else if (hdaCodecIsDigOutPinNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].digout.u32F08_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].digin.u32F08_param;
    else
        LogRel2(("HDA: Warning: Unhandled get unsolicited enabled command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 708 }
 */
static DECLCALLBACK(int) vrbProcSetUnsolicitedEnabled(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    uint32_t *pu32Reg = NULL;
    if (hdaCodecIsPortNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].port.u32F08_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].digin.u32F08_param;
    else if (CODEC_NID(uCmd) == STAC9220_NID_AFG)
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].afg.u32F08_param;
    else if (hdaCodecIsVolKnobNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].volumeKnob.u32F08_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].digin.u32F08_param;
    else if (hdaCodecIsDigOutPinNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].digout.u32F08_param;
    else
        LogRel2(("HDA: Warning: Unhandled set unsolicited enabled command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    if (pu32Reg)
        hdaCodecSetRegisterU8(pu32Reg, uCmd, 0);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, f09 }
 */
static DECLCALLBACK(int) vrbProcGetPinSense(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsPortNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].port.u32F09_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].digin.u32F09_param;
    else
    {
        AssertFailed();
        LogRel2(("HDA: Warning: Unhandled get pin sense command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 709 }
 */
static DECLCALLBACK(int) vrbProcSetPinSense(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    uint32_t *pu32Reg = NULL;
    if (hdaCodecIsPortNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].port.u32F09_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].digin.u32F09_param;
    else
        LogRel2(("HDA: Warning: Unhandled set pin sense command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    if (pu32Reg)
        hdaCodecSetRegisterU8(pu32Reg, uCmd, 0);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, ??? }
 */
static DECLCALLBACK(int) vrbProcGetConnectionListEntry(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    Assert((uCmd & CODEC_VERB_8BIT_DATA) < CODECNODE_F02_PARAM_LENGTH);
    if ((uCmd & CODEC_VERB_8BIT_DATA) >= CODECNODE_F02_PARAM_LENGTH)
    {
        LogFlowFunc(("access to invalid F02 index %d\n", (uCmd & CODEC_VERB_8BIT_DATA)));
        return VINF_SUCCESS;
    }
    *puResp = pThis->aNodes[CODEC_NID(uCmd)].node.au32F02_param[uCmd & CODEC_VERB_8BIT_DATA];
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, f03 }
 */
static DECLCALLBACK(int) vrbProcGetProcessingState(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsAdcNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].adc.u32F03_param;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 703 }
 */
static DECLCALLBACK(int) vrbProcSetProcessingState(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsAdcNode(pThis, CODEC_NID(uCmd)))
        hdaCodecSetRegisterU8(&pThis->aNodes[CODEC_NID(uCmd)].adc.u32F03_param, uCmd, 0);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, f0d }
 */
static DECLCALLBACK(int) vrbProcGetDigitalConverter(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsSpdifOutNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].spdifout.u32F0d_param;
    else if (hdaCodecIsSpdifInNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].spdifin.u32F0d_param;
    return VINF_SUCCESS;
}


static int codecSetDigitalConverter(PHDACODECR3 pThis, uint32_t uCmd, uint8_t u8Offset, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsSpdifOutNode(pThis, CODEC_NID(uCmd)))
        hdaCodecSetRegisterU8(&pThis->aNodes[CODEC_NID(uCmd)].spdifout.u32F0d_param, uCmd, u8Offset);
    else if (hdaCodecIsSpdifInNode(pThis, CODEC_NID(uCmd)))
        hdaCodecSetRegisterU8(&pThis->aNodes[CODEC_NID(uCmd)].spdifin.u32F0d_param, uCmd, u8Offset);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 70d }
 */
static DECLCALLBACK(int) vrbProcSetDigitalConverter1(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    return codecSetDigitalConverter(pThis, uCmd, 0, puResp);
}


/**
 * @interface_method_impl{CODECVERB,pfn, 70e }
 */
static DECLCALLBACK(int) vrbProcSetDigitalConverter2(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    return codecSetDigitalConverter(pThis, uCmd, 8, puResp);
}


/**
 * @interface_method_impl{CODECVERB,pfn, f20 }
 */
static DECLCALLBACK(int) vrbProcGetSubId(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    Assert(CODEC_CAD(uCmd) == pThis->Cfg.id);
    uint8_t const cTotalNodes = (uint8_t)RT_MIN(pThis->Cfg.cTotalNodes, RT_ELEMENTS(pThis->aNodes));
    Assert(CODEC_NID(uCmd) < cTotalNodes);
    if (CODEC_NID(uCmd) >= cTotalNodes)
    {
        LogFlowFunc(("invalid node address %d\n", CODEC_NID(uCmd)));
        *puResp = 0;
        return VINF_SUCCESS;
    }
    if (CODEC_NID(uCmd) == STAC9220_NID_AFG)
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].afg.u32F20_param;
    else
        *puResp = 0;
    return VINF_SUCCESS;
}


static int codecSetSubIdX(PHDACODECR3 pThis, uint32_t uCmd, uint8_t u8Offset)
{
    Assert(CODEC_CAD(uCmd) == pThis->Cfg.id);
    uint8_t const cTotalNodes = (uint8_t)RT_MIN(pThis->Cfg.cTotalNodes, RT_ELEMENTS(pThis->aNodes));
    Assert(CODEC_NID(uCmd) < cTotalNodes);
    if (CODEC_NID(uCmd) >= cTotalNodes)
    {
        LogFlowFunc(("invalid node address %d\n", CODEC_NID(uCmd)));
        return VINF_SUCCESS;
    }
    uint32_t *pu32Reg;
    if (CODEC_NID(uCmd) == STAC9220_NID_AFG)
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].afg.u32F20_param;
    else
        AssertFailedReturn(VINF_SUCCESS);
    hdaCodecSetRegisterU8(pu32Reg, uCmd, u8Offset);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 720 }
 */
static DECLCALLBACK(int) vrbProcSetSubId0(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;
    return codecSetSubIdX(pThis, uCmd, 0);
}


/**
 * @interface_method_impl{CODECVERB,pfn, 721 }
 */
static DECLCALLBACK(int) vrbProcSetSubId1(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;
    return codecSetSubIdX(pThis, uCmd, 8);
}


/**
 * @interface_method_impl{CODECVERB,pfn, 722 }
 */
static DECLCALLBACK(int) vrbProcSetSubId2(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;
    return codecSetSubIdX(pThis, uCmd, 16);
}


/**
 * @interface_method_impl{CODECVERB,pfn, 723 }
 */
static DECLCALLBACK(int) vrbProcSetSubId3(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;
    return codecSetSubIdX(pThis, uCmd, 24);
}


/**
 * @interface_method_impl{CODECVERB,pfn, ??? }
 */
static DECLCALLBACK(int) vrbProcReset(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    Assert(CODEC_CAD(uCmd) == pThis->Cfg.id);

    if (pThis->Cfg.enmType == CODECTYPE_STAC9220)
    {
        Assert(CODEC_NID(uCmd) == STAC9220_NID_AFG);

        if (CODEC_NID(uCmd) == STAC9220_NID_AFG)
            stac9220Reset(pThis);
    }
    else
        AssertFailedReturn(VERR_NOT_IMPLEMENTED);

    *puResp = 0;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, f05 }
 */
static DECLCALLBACK(int) vrbProcGetPowerState(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (CODEC_NID(uCmd) == STAC9220_NID_AFG)
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].afg.u32F05_param;
    else if (hdaCodecIsDacNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].dac.u32F05_param;
    else if (hdaCodecIsAdcNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].adc.u32F05_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].digin.u32F05_param;
    else if (hdaCodecIsDigOutPinNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].digout.u32F05_param;
    else if (hdaCodecIsSpdifOutNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].spdifout.u32F05_param;
    else if (hdaCodecIsSpdifInNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].spdifin.u32F05_param;
    else if (hdaCodecIsReservedNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].reserved.u32F05_param;
    else
        LogRel2(("HDA: Warning: Unhandled get power state command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    LogFunc(("[NID0x%02x]: fReset=%RTbool, fStopOk=%RTbool, Act=D%RU8, Set=D%RU8\n",
             CODEC_NID(uCmd), CODEC_F05_IS_RESET(*puResp), CODEC_F05_IS_STOPOK(*puResp), CODEC_F05_ACT(*puResp), CODEC_F05_SET(*puResp)));
    return VINF_SUCCESS;
}

#if 1

/**
 * @interface_method_impl{CODECVERB,pfn, 705 }
 */
static DECLCALLBACK(int) vrbProcSetPowerState(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    uint32_t *pu32Reg = NULL;
    if (CODEC_NID(uCmd) == STAC9220_NID_AFG)
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].afg.u32F05_param;
    else if (hdaCodecIsDacNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].dac.u32F05_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].digin.u32F05_param;
    else if (hdaCodecIsDigOutPinNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].digout.u32F05_param;
    else if (hdaCodecIsAdcNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].adc.u32F05_param;
    else if (hdaCodecIsSpdifOutNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].spdifout.u32F05_param;
    else if (hdaCodecIsSpdifInNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].spdifin.u32F05_param;
    else if (hdaCodecIsReservedNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].reserved.u32F05_param;
    else
    {
        LogRel2(("HDA: Warning: Unhandled set power state command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));
    }

    if (!pu32Reg)
        return VINF_SUCCESS;

    uint8_t uPwrCmd = CODEC_F05_SET      (uCmd);
    bool    fReset  = CODEC_F05_IS_RESET (*pu32Reg);
    bool    fStopOk = CODEC_F05_IS_STOPOK(*pu32Reg);
#ifdef LOG_ENABLED
    bool    fError  = CODEC_F05_IS_ERROR (*pu32Reg);
    uint8_t uPwrAct = CODEC_F05_ACT      (*pu32Reg);
    uint8_t uPwrSet = CODEC_F05_SET      (*pu32Reg);
    LogFunc(("[NID0x%02x] Cmd=D%RU8, fReset=%RTbool, fStopOk=%RTbool, fError=%RTbool, uPwrAct=D%RU8, uPwrSet=D%RU8\n",
             CODEC_NID(uCmd), uPwrCmd, fReset, fStopOk, fError, uPwrAct, uPwrSet));
    LogFunc(("AFG: Act=D%RU8, Set=D%RU8\n",
            CODEC_F05_ACT(pThis->aNodes[STAC9220_NID_AFG].afg.u32F05_param),
            CODEC_F05_SET(pThis->aNodes[STAC9220_NID_AFG].afg.u32F05_param)));
#endif

    if (CODEC_NID(uCmd) == STAC9220_NID_AFG)
        *pu32Reg = CODEC_MAKE_F05(fReset, fStopOk, 0, uPwrCmd /* PS-Act */, uPwrCmd /* PS-Set */);

    const uint8_t uAFGPwrAct = CODEC_F05_ACT(pThis->aNodes[STAC9220_NID_AFG].afg.u32F05_param);
    if (uAFGPwrAct == CODEC_F05_D0) /* Only propagate power state if AFG is on (D0). */
    {
        /* Propagate to all other nodes under this AFG. */
        LogFunc(("Propagating Act=D%RU8 (AFG), Set=D%RU8 to all AFG child nodes ...\n", uAFGPwrAct, uPwrCmd));

#define PROPAGATE_PWR_STATE(a_abList, a_Member) \
        do { \
            for (uintptr_t idxList = 0; idxList < RT_ELEMENTS(a_abList); idxList++) \
            { \
                uint8_t const idxNode = a_abList[idxList]; \
                if (idxNode) \
                { \
                    pThis->aNodes[idxNode].a_Member.u32F05_param = CODEC_MAKE_F05(fReset, fStopOk, 0, uAFGPwrAct, uPwrCmd); \
                    LogFunc(("\t[NID0x%02x]: Act=D%RU8, Set=D%RU8\n", idxNode, \
                             CODEC_F05_ACT(pThis->aNodes[idxNode].a_Member.u32F05_param), \
                             CODEC_F05_SET(pThis->aNodes[idxNode].a_Member.u32F05_param))); \
                } \
                else \
                    break; \
            } \
        } while (0)

        PROPAGATE_PWR_STATE(pThis->Cfg.abDacs,       dac);
        PROPAGATE_PWR_STATE(pThis->Cfg.abAdcs,       adc);
        PROPAGATE_PWR_STATE(pThis->Cfg.abDigInPins,  digin);
        PROPAGATE_PWR_STATE(pThis->Cfg.abDigOutPins, digout);
        PROPAGATE_PWR_STATE(pThis->Cfg.abSpdifIns,   spdifin);
        PROPAGATE_PWR_STATE(pThis->Cfg.abSpdifOuts,  spdifout);
        PROPAGATE_PWR_STATE(pThis->Cfg.abReserveds,  reserved);

#undef PROPAGATE_PWR_STATE
    }
    /*
     * If this node is a reqular node (not the AFG one), adopt PS-Set of the AFG node
     * as PS-Set of this node. PS-Act always is one level under PS-Set here.
     */
    else
    {
        *pu32Reg = CODEC_MAKE_F05(fReset, fStopOk, 0, uAFGPwrAct, uPwrCmd);
    }

    LogFunc(("[NID0x%02x] fReset=%RTbool, fStopOk=%RTbool, Act=D%RU8, Set=D%RU8\n",
             CODEC_NID(uCmd),
             CODEC_F05_IS_RESET(*pu32Reg), CODEC_F05_IS_STOPOK(*pu32Reg), CODEC_F05_ACT(*pu32Reg), CODEC_F05_SET(*pu32Reg)));

    return VINF_SUCCESS;
}

#else

DECLINLINE(void) codecPropogatePowerState(uint32_t *pu32F05_param)
{
    Assert(pu32F05_param);
    if (!pu32F05_param)
        return;
    bool fReset = CODEC_F05_IS_RESET(*pu32F05_param);
    bool fStopOk = CODEC_F05_IS_STOPOK(*pu32F05_param);
    uint8_t u8SetPowerState = CODEC_F05_SET(*pu32F05_param);
    *pu32F05_param = CODEC_MAKE_F05(fReset, fStopOk, 0, u8SetPowerState, u8SetPowerState);
}


/**
 * @interface_method_impl{CODECVERB,pfn, 705 }
 */
static DECLCALLBACK(int) vrbProcSetPowerState(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    Assert(CODEC_CAD(uCmd) == pThis->Cfg.id);
    uint8_t const cTotalNodes = (uint8_t)RT_MIN(pThis->Cfg.cTotalNodes, RT_ELEMENTS(pThis->aNodes));
    Assert(CODEC_NID(uCmd) < cTotalNodes);
    if (CODEC_NID(uCmd) >= cTotalNodes)
    {
        *puResp = 0;
        LogFlowFunc(("invalid node address %d\n", CODEC_NID(uCmd)));
        return VINF_SUCCESS;
    }
    *puResp = 0;
    uint32_t *pu32Reg;
    if (CODEC_NID(uCmd) == 1 /* AFG */)
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].afg.u32F05_param;
    else if (hdaCodecIsDacNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].dac.u32F05_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].digin.u32F05_param;
    else if (hdaCodecIsAdcNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].adc.u32F05_param;
    else if (hdaCodecIsSpdifOutNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].spdifout.u32F05_param;
    else if (hdaCodecIsSpdifInNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].spdifin.u32F05_param;
    else if (hdaCodecIsReservedNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].reserved.u32F05_param;
    else
        AssertFailedReturn(VINF_SUCCESS);

    bool fReset = CODEC_F05_IS_RESET(*pu32Reg);
    bool fStopOk = CODEC_F05_IS_STOPOK(*pu32Reg);

    if (CODEC_NID(uCmd) != 1 /* AFG */)
    {
        /*
         * We shouldn't propogate actual power state, which actual for AFG
         */
        *pu32Reg = CODEC_MAKE_F05(fReset, fStopOk, 0,
                                  CODEC_F05_ACT(pThis->aNodes[1].afg.u32F05_param),
                                  CODEC_F05_SET(uCmd));
    }

    /* Propagate next power state only if AFG is on or verb modifies AFG power state */
    if (   CODEC_NID(uCmd) == 1 /* AFG */
        || !CODEC_F05_ACT(pThis->aNodes[1].afg.u32F05_param))
    {
        *pu32Reg = CODEC_MAKE_F05(fReset, fStopOk, 0, CODEC_F05_SET(uCmd), CODEC_F05_SET(uCmd));
        if (   CODEC_NID(uCmd) == 1 /* AFG */
            && (CODEC_F05_SET(uCmd)) == CODEC_F05_D0)
        {
            /* now we're powered on AFG and may propogate power states on nodes */
            const uint8_t *pu8NodeIndex = &pThis->abDacs[0];
            while (*(++pu8NodeIndex))
                codecPropogatePowerState(&pThis->aNodes[*pu8NodeIndex].dac.u32F05_param);

            pu8NodeIndex = &pThis->abAdcs[0];
            while (*(++pu8NodeIndex))
                codecPropogatePowerState(&pThis->aNodes[*pu8NodeIndex].adc.u32F05_param);

            pu8NodeIndex = &pThis->abDigInPins[0];
            while (*(++pu8NodeIndex))
                codecPropogatePowerState(&pThis->aNodes[*pu8NodeIndex].digin.u32F05_param);
        }
    }
    return VINF_SUCCESS;
}

#endif

/**
 * @interface_method_impl{CODECVERB,pfn, f06 }
 */
static DECLCALLBACK(int) vrbProcGetStreamId(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsDacNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].dac.u32F06_param;
    else if (hdaCodecIsAdcNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].adc.u32F06_param;
    else if (hdaCodecIsSpdifInNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].spdifin.u32F06_param;
    else if (hdaCodecIsSpdifOutNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].spdifout.u32F06_param;
    else if (CODEC_NID(uCmd) == STAC9221_NID_I2S_OUT)
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].reserved.u32F06_param;
    else
        LogRel2(("HDA: Warning: Unhandled get stream ID command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    LogFlowFunc(("[NID0x%02x] Stream ID=%RU8, channel=%RU8\n",
                 CODEC_NID(uCmd), CODEC_F00_06_GET_STREAM_ID(uCmd), CODEC_F00_06_GET_CHANNEL_ID(uCmd)));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, a0 }
 */
static DECLCALLBACK(int) vrbProcGetConverterFormat(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsDacNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].dac.u32A_param;
    else if (hdaCodecIsAdcNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].adc.u32A_param;
    else if (hdaCodecIsSpdifOutNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].spdifout.u32A_param;
    else if (hdaCodecIsSpdifInNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].spdifin.u32A_param;
    else if (hdaCodecIsReservedNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].reserved.u32A_param;
    else
        LogRel2(("HDA: Warning: Unhandled get converter format command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, ??? - Also see section 3.7.1. }
 */
static DECLCALLBACK(int) vrbProcSetConverterFormat(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsDacNode(pThis, CODEC_NID(uCmd)))
        hdaCodecSetRegisterU16(&pThis->aNodes[CODEC_NID(uCmd)].dac.u32A_param, uCmd, 0);
    else if (hdaCodecIsAdcNode(pThis, CODEC_NID(uCmd)))
        hdaCodecSetRegisterU16(&pThis->aNodes[CODEC_NID(uCmd)].adc.u32A_param, uCmd, 0);
    else if (hdaCodecIsSpdifOutNode(pThis, CODEC_NID(uCmd)))
        hdaCodecSetRegisterU16(&pThis->aNodes[CODEC_NID(uCmd)].spdifout.u32A_param, uCmd, 0);
    else if (hdaCodecIsSpdifInNode(pThis, CODEC_NID(uCmd)))
        hdaCodecSetRegisterU16(&pThis->aNodes[CODEC_NID(uCmd)].spdifin.u32A_param, uCmd, 0);
    else
        LogRel2(("HDA: Warning: Unhandled set converter format command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, f0c }
 */
static DECLCALLBACK(int) vrbProcGetEAPD_BTLEnabled(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsAdcVolNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].adcvol.u32F0c_param;
    else if (hdaCodecIsDacNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].dac.u32F0c_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].digin.u32F0c_param;
    else
        LogRel2(("HDA: Warning: Unhandled get EAPD/BTL enabled command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 70c }
 */
static DECLCALLBACK(int) vrbProcSetEAPD_BTLEnabled(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    uint32_t *pu32Reg = NULL;
    if (hdaCodecIsAdcVolNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].adcvol.u32F0c_param;
    else if (hdaCodecIsDacNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].dac.u32F0c_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].digin.u32F0c_param;
    else
        LogRel2(("HDA: Warning: Unhandled set EAPD/BTL enabled command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    if (pu32Reg)
        hdaCodecSetRegisterU8(pu32Reg, uCmd, 0);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, f0f }
 */
static DECLCALLBACK(int) vrbProcGetVolumeKnobCtrl(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsVolKnobNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].volumeKnob.u32F0f_param;
    else
        LogRel2(("HDA: Warning: Unhandled get volume knob control command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 70f }
 */
static DECLCALLBACK(int) vrbProcSetVolumeKnobCtrl(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    uint32_t *pu32Reg = NULL;
    if (hdaCodecIsVolKnobNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].volumeKnob.u32F0f_param;
    else
        LogRel2(("HDA: Warning: Unhandled set volume knob control command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    if (pu32Reg)
        hdaCodecSetRegisterU8(pu32Reg, uCmd, 0);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, f15 }
 */
static DECLCALLBACK(int) vrbProcGetGPIOData(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    RT_NOREF(pThis, uCmd);
    *puResp = 0;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 715 }
 */
static DECLCALLBACK(int) vrbProcSetGPIOData(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    RT_NOREF(pThis, uCmd);
    *puResp = 0;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, f16 }
 */
static DECLCALLBACK(int) vrbProcGetGPIOEnableMask(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    RT_NOREF(pThis, uCmd);
    *puResp = 0;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 716 }
 */
static DECLCALLBACK(int) vrbProcSetGPIOEnableMask(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    RT_NOREF(pThis, uCmd);
    *puResp = 0;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, f17 }
 */
static DECLCALLBACK(int) vrbProcGetGPIODirection(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    /* Note: this is true for ALC885. */
    if (CODEC_NID(uCmd) == STAC9220_NID_AFG)
        *puResp = pThis->aNodes[1].afg.u32F17_param;
    else
        LogRel2(("HDA: Warning: Unhandled get GPIO direction command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 717 }
 */
static DECLCALLBACK(int) vrbProcSetGPIODirection(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    uint32_t *pu32Reg = NULL;
    if (CODEC_NID(uCmd) == STAC9220_NID_AFG)
        pu32Reg = &pThis->aNodes[1].afg.u32F17_param;
    else
        LogRel2(("HDA: Warning: Unhandled set GPIO direction command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    if (pu32Reg)
        hdaCodecSetRegisterU8(pu32Reg, uCmd, 0);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, f1c }
 */
static DECLCALLBACK(int) vrbProcGetConfig(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsPortNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].port.u32F1c_param;
    else if (hdaCodecIsDigOutPinNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].digout.u32F1c_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].digin.u32F1c_param;
    else if (hdaCodecIsPcbeepNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].pcbeep.u32F1c_param;
    else if (hdaCodecIsCdNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].cdnode.u32F1c_param;
    else if (hdaCodecIsReservedNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].reserved.u32F1c_param;
    else
        LogRel2(("HDA: Warning: Unhandled get config command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    return VINF_SUCCESS;
}

static int codecSetConfigX(PHDACODECR3 pThis, uint32_t uCmd, uint8_t u8Offset)
{
    uint32_t *pu32Reg = NULL;
    if (hdaCodecIsPortNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].port.u32F1c_param;
    else if (hdaCodecIsDigInPinNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].digin.u32F1c_param;
    else if (hdaCodecIsDigOutPinNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].digout.u32F1c_param;
    else if (hdaCodecIsCdNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].cdnode.u32F1c_param;
    else if (hdaCodecIsPcbeepNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].pcbeep.u32F1c_param;
    else if (hdaCodecIsReservedNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].reserved.u32F1c_param;
    else
        LogRel2(("HDA: Warning: Unhandled set config command (%RU8) for NID0x%02x: 0x%x\n", u8Offset, CODEC_NID(uCmd), uCmd));

    if (pu32Reg)
        hdaCodecSetRegisterU8(pu32Reg, uCmd, u8Offset);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 71c }
 */
static DECLCALLBACK(int) vrbProcSetConfig0(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;
    return codecSetConfigX(pThis, uCmd, 0);
}


/**
 * @interface_method_impl{CODECVERB,pfn, 71d }
 */
static DECLCALLBACK(int) vrbProcSetConfig1(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;
    return codecSetConfigX(pThis, uCmd, 8);
}


/**
 * @interface_method_impl{CODECVERB,pfn, 71e }
 */
static DECLCALLBACK(int) vrbProcSetConfig2(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;
    return codecSetConfigX(pThis, uCmd, 16);
}


/**
 * @interface_method_impl{CODECVERB,pfn, 71e }
 */
static DECLCALLBACK(int) vrbProcSetConfig3(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;
    return codecSetConfigX(pThis, uCmd, 24);
}


/**
 * @interface_method_impl{CODECVERB,pfn, f04 }
 */
static DECLCALLBACK(int) vrbProcGetSDISelect(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    if (hdaCodecIsDacNode(pThis, CODEC_NID(uCmd)))
        *puResp = pThis->aNodes[CODEC_NID(uCmd)].dac.u32F04_param;
    else
        LogRel2(("HDA: Warning: Unhandled get SDI select command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 704 }
 */
static DECLCALLBACK(int) vrbProcSetSDISelect(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    uint32_t *pu32Reg = NULL;
    if (hdaCodecIsDacNode(pThis, CODEC_NID(uCmd)))
        pu32Reg = &pThis->aNodes[CODEC_NID(uCmd)].dac.u32F04_param;
    else
        LogRel2(("HDA: Warning: Unhandled set SDI select command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));

    if (pu32Reg)
        hdaCodecSetRegisterU8(pu32Reg, uCmd, 0);

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 3-- }
 */
static DECLCALLBACK(int) vrbProcR3SetAmplifier(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    PCODECNODE pNode      = &pThis->aNodes[CODEC_NID(uCmd)];
    AMPLIFIER *pAmplifier = NULL;
    if (hdaCodecIsDacNode(pThis, CODEC_NID(uCmd)))
        pAmplifier = &pNode->dac.B_params;
    else if (hdaCodecIsAdcVolNode(pThis, CODEC_NID(uCmd)))
        pAmplifier = &pNode->adcvol.B_params;
    else if (hdaCodecIsAdcMuxNode(pThis, CODEC_NID(uCmd)))
        pAmplifier = &pNode->adcmux.B_params;
    else if (hdaCodecIsPcbeepNode(pThis, CODEC_NID(uCmd)))
        pAmplifier = &pNode->pcbeep.B_params;
    else if (hdaCodecIsPortNode(pThis, CODEC_NID(uCmd)))
        pAmplifier = &pNode->port.B_params;
    else if (hdaCodecIsAdcNode(pThis, CODEC_NID(uCmd)))
        pAmplifier = &pNode->adc.B_params;
    else
        LogRel2(("HDA: Warning: Unhandled set amplifier command: 0x%x (Payload=%RU16, NID=0x%x [%RU8])\n",
                 uCmd, CODEC_VERB_PAYLOAD16(uCmd), CODEC_NID(uCmd), CODEC_NID(uCmd)));

    if (!pAmplifier)
        return VINF_SUCCESS;

    bool fIsOut     = CODEC_SET_AMP_IS_OUT_DIRECTION(uCmd);
    bool fIsIn      = CODEC_SET_AMP_IS_IN_DIRECTION(uCmd);
    bool fIsLeft    = CODEC_SET_AMP_IS_LEFT_SIDE(uCmd);
    bool fIsRight   = CODEC_SET_AMP_IS_RIGHT_SIDE(uCmd);
    uint8_t u8Index = CODEC_SET_AMP_INDEX(uCmd);

    if (   (!fIsLeft && !fIsRight)
        || (!fIsOut && !fIsIn))
        return VINF_SUCCESS;

    LogFunc(("[NID0x%02x] fIsOut=%RTbool, fIsIn=%RTbool, fIsLeft=%RTbool, fIsRight=%RTbool, Idx=%RU8\n",
             CODEC_NID(uCmd), fIsOut, fIsIn, fIsLeft, fIsRight, u8Index));

    if (fIsIn)
    {
        if (fIsLeft)
            hdaCodecSetRegisterU8(&AMPLIFIER_REGISTER(*pAmplifier, AMPLIFIER_IN, AMPLIFIER_LEFT, u8Index), uCmd, 0);
        if (fIsRight)
            hdaCodecSetRegisterU8(&AMPLIFIER_REGISTER(*pAmplifier, AMPLIFIER_IN, AMPLIFIER_RIGHT, u8Index), uCmd, 0);

        switch (pThis->Cfg.enmType)
        {
            case CODECTYPE_STAC9220:
            {
                /*
                 * Check if the node ID is the one we use for controlling the recording (capturing) volume + mute status.
                 *
                 * The STAC9220 codec is connected to STAC9220_NID_AMP_ADC0 (ID 0x17) and
                 * STAC9220_NID_AMP_ADC0 (ID 0x18).
                 *
                 * If we don't do this check here, some guests like newer Ubuntus mute mic-in
                 * afterwards (connected to STAC9220_NID_AMP_ADC1 (ID 0x18)). This then would
                 * also mute line-in, which breaks audio recording.
                 *
                 * See STAC9220 V1.0 01/08, p. 30 + @oem2ticketref{53}.
                 */
                if (CODEC_NID(uCmd) == pThis->Cfg.idxAdcVolsLineIn)
                    hdaR3CodecToAudVolume(pThis, pNode, pAmplifier, PDMAUDIOMIXERCTL_LINE_IN);

#ifdef VBOX_AUDIO_HDA_MIC_IN_AS_LINE_IN
                /* Newer Windows 10 versions use STAC9220_NID_AMP_ADC1 (ID 0x18) for microphone
                 * volume control by default. */
# ifndef VBOX_WITH_AUDIO_HDA_MIC_IN
                /* If we don't implement dedicated microphone-in support (we then only offer recording via line-in),
                 * make sure to propagate volume control from mic-in to our line-in mixer control.  See @oem2ticketref{93}. */
                else if (CODEC_NID(uCmd) == pThis->Cfg.idxAdcVolsMicIn)
                    hdaR3CodecToAudVolume(pThis, pNode, pAmplifier, PDMAUDIOMIXERCTL_LINE_IN);
# endif
#endif
                break;
            }

            default:
                /* Nothing to do here. */
                break;
        }
    }
    if (fIsOut)
    {
        if (fIsLeft)
            hdaCodecSetRegisterU8(&AMPLIFIER_REGISTER(*pAmplifier, AMPLIFIER_OUT, AMPLIFIER_LEFT, u8Index), uCmd, 0);
        if (fIsRight)
            hdaCodecSetRegisterU8(&AMPLIFIER_REGISTER(*pAmplifier, AMPLIFIER_OUT, AMPLIFIER_RIGHT, u8Index), uCmd, 0);

        if (CODEC_NID(uCmd) == pThis->Cfg.idxDacLineOut)
            hdaR3CodecToAudVolume(pThis, pNode, pAmplifier, PDMAUDIOMIXERCTL_FRONT);
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{CODECVERB,pfn, 706 }
 */
static DECLCALLBACK(int) vrbProcR3SetStreamId(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    *puResp = 0;

    uint8_t uSD      = CODEC_F00_06_GET_STREAM_ID(uCmd);
    uint8_t uChannel = CODEC_F00_06_GET_CHANNEL_ID(uCmd);

    LogFlowFunc(("[NID0x%02x] Setting to stream ID=%RU8, channel=%RU8\n",
                 CODEC_NID(uCmd), uSD, uChannel));

    ASSERT_GUEST_LOGREL_MSG_RETURN(uSD < HDA_MAX_STREAMS,
                                   ("Setting stream ID #%RU8 is invalid\n", uSD), VERR_INVALID_PARAMETER);

    PDMAUDIODIR enmDir;
    uint32_t   *pu32Addr;
    if (hdaCodecIsDacNode(pThis, CODEC_NID(uCmd)))
    {
        pu32Addr = &pThis->aNodes[CODEC_NID(uCmd)].dac.u32F06_param;
        enmDir = PDMAUDIODIR_OUT;
    }
    else if (hdaCodecIsAdcNode(pThis, CODEC_NID(uCmd)))
    {
        pu32Addr = &pThis->aNodes[CODEC_NID(uCmd)].adc.u32F06_param;
        enmDir = PDMAUDIODIR_IN;
    }
    else if (hdaCodecIsSpdifOutNode(pThis, CODEC_NID(uCmd)))
    {
        pu32Addr = &pThis->aNodes[CODEC_NID(uCmd)].spdifout.u32F06_param;
        enmDir = PDMAUDIODIR_OUT;
    }
    else if (hdaCodecIsSpdifInNode(pThis, CODEC_NID(uCmd)))
    {
        pu32Addr = &pThis->aNodes[CODEC_NID(uCmd)].spdifin.u32F06_param;
        enmDir = PDMAUDIODIR_IN;
    }
    else
    {
        enmDir = PDMAUDIODIR_UNKNOWN;
        LogRel2(("HDA: Warning: Unhandled set stream ID command for NID0x%02x: 0x%x\n", CODEC_NID(uCmd), uCmd));
        return VINF_SUCCESS;
    }

    /* Do we (re-)assign our input/output SDn (SDI/SDO) IDs? */
    pThis->aNodes[CODEC_NID(uCmd)].node.uSD      = uSD;
    pThis->aNodes[CODEC_NID(uCmd)].node.uChannel = uChannel;

    if (enmDir == PDMAUDIODIR_OUT)
    {
        /** @todo Check if non-interleaved streams need a different channel / SDn? */

        /* Propagate to the controller. */
        hdaR3MixerControl(pThis, PDMAUDIOMIXERCTL_FRONT,      uSD, uChannel);
# ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
        hdaR3MixerControl(pThis, PDMAUDIOMIXERCTL_CENTER_LFE, uSD, uChannel);
        hdaR3MixerControl(pThis, PDMAUDIOMIXERCTL_REAR,       uSD, uChannel);
# endif
    }
    else if (enmDir == PDMAUDIODIR_IN)
    {
        hdaR3MixerControl(pThis, PDMAUDIOMIXERCTL_LINE_IN,    uSD, uChannel);
# ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
        hdaR3MixerControl(pThis, PDMAUDIOMIXERCTL_MIC_IN,     uSD, uChannel);
# endif
    }

    hdaCodecSetRegisterU8(pu32Addr, uCmd, 0);

    return VINF_SUCCESS;
}



/**
 * HDA codec verb descriptors.
 *
 * @note This must be ordered by uVerb so we can do a binary lookup.
 */
static const CODECVERB g_aCodecVerbs[] =
{
    /* Verb        Verb mask            Callback                                   Name
       ---------- --------------------- ------------------------------------------------------------------- */
    { 0x00020000, CODEC_VERB_16BIT_CMD, vrbProcSetConverterFormat                , "SetConverterFormat    " },
    { 0x00030000, CODEC_VERB_16BIT_CMD, vrbProcR3SetAmplifier                    , "SetAmplifier          " },
    { 0x00070100, CODEC_VERB_8BIT_CMD , vrbProcSetConSelectCtrl                  , "SetConSelectCtrl      " },
    { 0x00070300, CODEC_VERB_8BIT_CMD , vrbProcSetProcessingState                , "SetProcessingState    " },
    { 0x00070400, CODEC_VERB_8BIT_CMD , vrbProcSetSDISelect                      , "SetSDISelect          " },
    { 0x00070500, CODEC_VERB_8BIT_CMD , vrbProcSetPowerState                     , "SetPowerState         " },
    { 0x00070600, CODEC_VERB_8BIT_CMD , vrbProcR3SetStreamId                     , "SetStreamId           " },
    { 0x00070700, CODEC_VERB_8BIT_CMD , vrbProcSetPinCtrl                        , "SetPinCtrl            " },
    { 0x00070800, CODEC_VERB_8BIT_CMD , vrbProcSetUnsolicitedEnabled             , "SetUnsolicitedEnabled " },
    { 0x00070900, CODEC_VERB_8BIT_CMD , vrbProcSetPinSense                       , "SetPinSense           " },
    { 0x00070C00, CODEC_VERB_8BIT_CMD , vrbProcSetEAPD_BTLEnabled                , "SetEAPD_BTLEnabled    " },
    { 0x00070D00, CODEC_VERB_8BIT_CMD , vrbProcSetDigitalConverter1              , "SetDigitalConverter1  " },
    { 0x00070E00, CODEC_VERB_8BIT_CMD , vrbProcSetDigitalConverter2              , "SetDigitalConverter2  " },
    { 0x00070F00, CODEC_VERB_8BIT_CMD , vrbProcSetVolumeKnobCtrl                 , "SetVolumeKnobCtrl     " },
    { 0x00071500, CODEC_VERB_8BIT_CMD , vrbProcSetGPIOData                       , "SetGPIOData           " },
    { 0x00071600, CODEC_VERB_8BIT_CMD , vrbProcSetGPIOEnableMask                 , "SetGPIOEnableMask     " },
    { 0x00071700, CODEC_VERB_8BIT_CMD , vrbProcSetGPIODirection                  , "SetGPIODirection      " },
    { 0x00071C00, CODEC_VERB_8BIT_CMD , vrbProcSetConfig0                        , "SetConfig0            " },
    { 0x00071D00, CODEC_VERB_8BIT_CMD , vrbProcSetConfig1                        , "SetConfig1            " },
    { 0x00071E00, CODEC_VERB_8BIT_CMD , vrbProcSetConfig2                        , "SetConfig2            " },
    { 0x00071F00, CODEC_VERB_8BIT_CMD , vrbProcSetConfig3                        , "SetConfig3            " },
    { 0x00072000, CODEC_VERB_8BIT_CMD , vrbProcSetSubId0                         , "SetSubId0             " },
    { 0x00072100, CODEC_VERB_8BIT_CMD , vrbProcSetSubId1                         , "SetSubId1             " },
    { 0x00072200, CODEC_VERB_8BIT_CMD , vrbProcSetSubId2                         , "SetSubId2             " },
    { 0x00072300, CODEC_VERB_8BIT_CMD , vrbProcSetSubId3                         , "SetSubId3             " },
    { 0x0007FF00, CODEC_VERB_8BIT_CMD , vrbProcReset                             , "Reset                 " },
    { 0x000A0000, CODEC_VERB_16BIT_CMD, vrbProcGetConverterFormat                , "GetConverterFormat    " },
    { 0x000B0000, CODEC_VERB_16BIT_CMD, vrbProcGetAmplifier                      , "GetAmplifier          " },
    { 0x000F0000, CODEC_VERB_8BIT_CMD , vrbProcGetParameter                      , "GetParameter          " },
    { 0x000F0100, CODEC_VERB_8BIT_CMD , vrbProcGetConSelectCtrl                  , "GetConSelectCtrl      " },
    { 0x000F0200, CODEC_VERB_8BIT_CMD , vrbProcGetConnectionListEntry            , "GetConnectionListEntry" },
    { 0x000F0300, CODEC_VERB_8BIT_CMD , vrbProcGetProcessingState                , "GetProcessingState    " },
    { 0x000F0400, CODEC_VERB_8BIT_CMD , vrbProcGetSDISelect                      , "GetSDISelect          " },
    { 0x000F0500, CODEC_VERB_8BIT_CMD , vrbProcGetPowerState                     , "GetPowerState         " },
    { 0x000F0600, CODEC_VERB_8BIT_CMD , vrbProcGetStreamId                       , "GetStreamId           " },
    { 0x000F0700, CODEC_VERB_8BIT_CMD , vrbProcGetPinCtrl                        , "GetPinCtrl            " },
    { 0x000F0800, CODEC_VERB_8BIT_CMD , vrbProcGetUnsolicitedEnabled             , "GetUnsolicitedEnabled " },
    { 0x000F0900, CODEC_VERB_8BIT_CMD , vrbProcGetPinSense                       , "GetPinSense           " },
    { 0x000F0C00, CODEC_VERB_8BIT_CMD , vrbProcGetEAPD_BTLEnabled                , "GetEAPD_BTLEnabled    " },
    { 0x000F0D00, CODEC_VERB_8BIT_CMD , vrbProcGetDigitalConverter               , "GetDigitalConverter   " },
    { 0x000F0F00, CODEC_VERB_8BIT_CMD , vrbProcGetVolumeKnobCtrl                 , "GetVolumeKnobCtrl     " },
    { 0x000F1500, CODEC_VERB_8BIT_CMD , vrbProcGetGPIOData                       , "GetGPIOData           " },
    { 0x000F1600, CODEC_VERB_8BIT_CMD , vrbProcGetGPIOEnableMask                 , "GetGPIOEnableMask     " },
    { 0x000F1700, CODEC_VERB_8BIT_CMD , vrbProcGetGPIODirection                  , "GetGPIODirection      " },
    { 0x000F1C00, CODEC_VERB_8BIT_CMD , vrbProcGetConfig                         , "GetConfig             " },
    { 0x000F2000, CODEC_VERB_8BIT_CMD , vrbProcGetSubId                          , "GetSubId              " },
    /** @todo Implement 0x7e7: IDT Set GPIO (STAC922x only). */
};


/**
 * Implements codec lookup and will call the handler on the verb it finds,
 * returning the handler response.
 *
 * @returns VBox status code (not strict).
 */
DECLHIDDEN(int) hdaR3CodecLookup(PHDACODECR3 pThis, uint32_t uCmd, uint64_t *puResp)
{
    /*
     * Clear the return value and assert some sanity.
     */
    AssertPtr(puResp);
    *puResp = 0;
    AssertPtr(pThis);
    AssertMsgReturn(CODEC_CAD(uCmd) == pThis->Cfg.id,
                    ("Unknown codec address 0x%x\n", CODEC_CAD(uCmd)),
                    VERR_INVALID_PARAMETER);
    uint32_t const uCmdData = CODEC_VERBDATA(uCmd);
    AssertMsgReturn(   uCmdData != 0
                    && CODEC_NID(uCmd) < RT_MIN(pThis->Cfg.cTotalNodes, RT_ELEMENTS(pThis->aNodes)),
                    ("[NID0x%02x] Unknown / invalid node or data (0x%x)\n", CODEC_NID(uCmd), uCmdData),
                    VERR_INVALID_PARAMETER);
    STAM_COUNTER_INC(&pThis->CTX_SUFF(StatLookups));

    /*
     * Do a binary lookup of the verb.
     * Note! if we want other verb tables, add a table selector before the loop.
     */
    size_t iFirst = 0;
    size_t iEnd   = RT_ELEMENTS(g_aCodecVerbs);
    for (;;)
    {
        size_t const   iCur  = iFirst + (iEnd - iFirst) / 2;
        uint32_t const uVerb = g_aCodecVerbs[iCur].uVerb;
        if (uCmdData < uVerb)
        {
            if (iCur > iFirst)
                iEnd = iCur;
            else
                break;
        }
        else if ((uCmdData & g_aCodecVerbs[iCur].fMask) != uVerb)
        {
            if (iCur + 1 < iEnd)
                iFirst = iCur + 1;
            else
                break;
        }
        else
        {
            /*
             * Found it!  Run the callback and return.
             */
            AssertPtrReturn(g_aCodecVerbs[iCur].pfn, VERR_INTERNAL_ERROR_5); /* Paranoia^2. */

            int rc = g_aCodecVerbs[iCur].pfn(pThis, uCmd, puResp);
            AssertRC(rc);
            Log3Func(("[NID0x%02x] (0x%x) %s: 0x%x -> 0x%x\n",
                      CODEC_NID(uCmd), g_aCodecVerbs[iCur].uVerb, g_aCodecVerbs[iCur].pszName, CODEC_VERB_PAYLOAD8(uCmd), *puResp));
            return rc;
        }
    }

#ifdef VBOX_STRICT
    for (size_t i = 0; i < RT_ELEMENTS(g_aCodecVerbs); i++)
    {
        AssertMsg(i == 0 || g_aCodecVerbs[i - 1].uVerb < g_aCodecVerbs[i].uVerb,
                  ("i=%#x uVerb[-1]=%#x uVerb=%#x - buggy table!\n", i, g_aCodecVerbs[i - 1].uVerb, g_aCodecVerbs[i].uVerb));
        AssertMsg((uCmdData & g_aCodecVerbs[i].fMask) != g_aCodecVerbs[i].uVerb,
                  ("i=%#x uVerb=%#x uCmd=%#x - buggy binary search or table!\n", i, g_aCodecVerbs[i].uVerb, uCmd));
    }
#endif
    LogFunc(("[NID0x%02x] Callback for %x not found\n", CODEC_NID(uCmd), CODEC_VERBDATA(uCmd)));
    return VERR_NOT_FOUND;
}


/*********************************************************************************************************************************
*   Debug                                                                                                                        *
*********************************************************************************************************************************/
/**
 * CODEC debug info item printing state.
 */
typedef struct CODECDEBUG
{
    /** DBGF info helpers. */
    PCDBGFINFOHLP   pHlp;
    /** Current recursion level. */
    uint8_t         uLevel;
    /** Pointer to codec state. */
    PHDACODECR3     pThis;
} CODECDEBUG;
/** Pointer to the debug info item printing state for the codec. */
typedef CODECDEBUG *PCODECDEBUG;

#define CODECDBG_INDENT         pInfo->uLevel++;
#define CODECDBG_UNINDENT       if (pInfo->uLevel) pInfo->uLevel--;

#define CODECDBG_PRINT(...)     pInfo->pHlp->pfnPrintf(pInfo->pHlp, __VA_ARGS__)
#define CODECDBG_PRINTI(...)    codecDbgPrintf(pInfo, __VA_ARGS__)


/** Wrapper around DBGFINFOHLP::pfnPrintf that adds identation. */
static void codecDbgPrintf(PCODECDEBUG pInfo, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    pInfo->pHlp->pfnPrintf(pInfo->pHlp, "%*s%N", pInfo->uLevel * 4, "", pszFormat, &va);
    va_end(va);
}


/** Power state */
static void codecDbgPrintNodeRegF05(PCODECDEBUG pInfo, uint32_t u32Reg)
{
    codecDbgPrintf(pInfo, "Power (F05): fReset=%RTbool, fStopOk=%RTbool, Set=%RU8, Act=%RU8\n",
                   CODEC_F05_IS_RESET(u32Reg), CODEC_F05_IS_STOPOK(u32Reg), CODEC_F05_SET(u32Reg), CODEC_F05_ACT(u32Reg));
}


static void codecDbgPrintNodeRegA(PCODECDEBUG pInfo, uint32_t u32Reg)
{
    codecDbgPrintf(pInfo, "RegA: %x\n", u32Reg);
}


static void codecDbgPrintNodeRegF00(PCODECDEBUG pInfo, uint32_t *paReg00)
{
    codecDbgPrintf(pInfo, "Parameters (F00):\n");

    CODECDBG_INDENT
        codecDbgPrintf(pInfo, "Connections: %RU8\n", CODEC_F00_0E_COUNT(paReg00[0xE]));
        codecDbgPrintf(pInfo, "Amplifier Caps:\n");
        uint32_t uReg = paReg00[0xD];
        CODECDBG_INDENT
            codecDbgPrintf(pInfo, "Input Steps=%02RU8, StepSize=%02RU8, StepOff=%02RU8, fCanMute=%RTbool\n",
                           CODEC_F00_0D_NUM_STEPS(uReg),
                           CODEC_F00_0D_STEP_SIZE(uReg),
                           CODEC_F00_0D_OFFSET(uReg),
                           RT_BOOL(CODEC_F00_0D_IS_CAP_MUTE(uReg)));

            uReg = paReg00[0x12];
            codecDbgPrintf(pInfo, "Output Steps=%02RU8, StepSize=%02RU8, StepOff=%02RU8, fCanMute=%RTbool\n",
                           CODEC_F00_12_NUM_STEPS(uReg),
                           CODEC_F00_12_STEP_SIZE(uReg),
                           CODEC_F00_12_OFFSET(uReg),
                           RT_BOOL(CODEC_F00_12_IS_CAP_MUTE(uReg)));
        CODECDBG_UNINDENT
    CODECDBG_UNINDENT
}


static void codecDbgPrintNodeAmp(PCODECDEBUG pInfo, uint32_t *paReg, uint8_t uIdx, uint8_t uDir)
{
#define CODECDBG_AMP(reg, chan) \
        codecDbgPrintf(pInfo, "Amp %RU8 %s %s: In=%RTbool, Out=%RTbool, Left=%RTbool, Right=%RTbool, Idx=%RU8, fMute=%RTbool, uGain=%RU8\n", \
                       uIdx, chan, uDir == AMPLIFIER_IN ? "In" : "Out", \
                       RT_BOOL(CODEC_SET_AMP_IS_IN_DIRECTION(reg)), RT_BOOL(CODEC_SET_AMP_IS_OUT_DIRECTION(reg)), \
                       RT_BOOL(CODEC_SET_AMP_IS_LEFT_SIDE(reg)), RT_BOOL(CODEC_SET_AMP_IS_RIGHT_SIDE(reg)), \
                       CODEC_SET_AMP_INDEX(reg), RT_BOOL(CODEC_SET_AMP_MUTE(reg)), CODEC_SET_AMP_GAIN(reg))

    uint32_t regAmp = AMPLIFIER_REGISTER(paReg, uDir, AMPLIFIER_LEFT, uIdx);
    CODECDBG_AMP(regAmp, "Left");
    regAmp = AMPLIFIER_REGISTER(paReg, uDir, AMPLIFIER_RIGHT, uIdx);
    CODECDBG_AMP(regAmp, "Right");

#undef CODECDBG_AMP
}


#if 0 /* unused */
static void codecDbgPrintNodeConnections(PCODECDEBUG pInfo, PCODECNODE pNode)
{
    if (pNode->node.au32F00_param[0xE] == 0) /* Directly connected to HDA link. */
    {
         codecDbgPrintf(pInfo, "[HDA LINK]\n");
         return;
    }
}
#endif


static void codecDbgPrintNode(PCODECDEBUG pInfo, PCODECNODE pNode, bool fRecursive)
{
    codecDbgPrintf(pInfo, "Node 0x%02x (%02RU8): ", pNode->node.uID, pNode->node.uID);

    if (pNode->node.uID == STAC9220_NID_ROOT)
        CODECDBG_PRINT("ROOT\n");
    else if (pNode->node.uID == STAC9220_NID_AFG)
    {
        CODECDBG_PRINT("AFG\n");
        CODECDBG_INDENT
            codecDbgPrintNodeRegF00(pInfo, pNode->node.au32F00_param);
            codecDbgPrintNodeRegF05(pInfo, pNode->afg.u32F05_param);
        CODECDBG_UNINDENT
    }
    else if (hdaCodecIsPortNode(pInfo->pThis, pNode->node.uID))
        CODECDBG_PRINT("PORT\n");
    else if (hdaCodecIsDacNode(pInfo->pThis, pNode->node.uID))
    {
        CODECDBG_PRINT("DAC\n");
        CODECDBG_INDENT
            codecDbgPrintNodeRegF00(pInfo, pNode->node.au32F00_param);
            codecDbgPrintNodeRegF05(pInfo, pNode->dac.u32F05_param);
            codecDbgPrintNodeRegA  (pInfo, pNode->dac.u32A_param);
            codecDbgPrintNodeAmp   (pInfo, pNode->dac.B_params, 0, AMPLIFIER_OUT);
        CODECDBG_UNINDENT
    }
    else if (hdaCodecIsAdcVolNode(pInfo->pThis, pNode->node.uID))
    {
        CODECDBG_PRINT("ADC VOLUME\n");
        CODECDBG_INDENT
            codecDbgPrintNodeRegF00(pInfo, pNode->node.au32F00_param);
            codecDbgPrintNodeRegA  (pInfo, pNode->adcvol.u32A_params);
            codecDbgPrintNodeAmp   (pInfo, pNode->adcvol.B_params, 0, AMPLIFIER_IN);
        CODECDBG_UNINDENT
    }
    else if (hdaCodecIsAdcNode(pInfo->pThis, pNode->node.uID))
    {
        CODECDBG_PRINT("ADC\n");
        CODECDBG_INDENT
            codecDbgPrintNodeRegF00(pInfo, pNode->node.au32F00_param);
            codecDbgPrintNodeRegF05(pInfo, pNode->adc.u32F05_param);
            codecDbgPrintNodeRegA  (pInfo, pNode->adc.u32A_param);
            codecDbgPrintNodeAmp   (pInfo, pNode->adc.B_params, 0, AMPLIFIER_IN);
        CODECDBG_UNINDENT
    }
    else if (hdaCodecIsAdcMuxNode(pInfo->pThis, pNode->node.uID))
    {
        CODECDBG_PRINT("ADC MUX\n");
        CODECDBG_INDENT
            codecDbgPrintNodeRegF00(pInfo, pNode->node.au32F00_param);
            codecDbgPrintNodeRegA  (pInfo, pNode->adcmux.u32A_param);
            codecDbgPrintNodeAmp   (pInfo, pNode->adcmux.B_params, 0, AMPLIFIER_IN);
        CODECDBG_UNINDENT
    }
    else if (hdaCodecIsPcbeepNode(pInfo->pThis, pNode->node.uID))
        CODECDBG_PRINT("PC BEEP\n");
    else if (hdaCodecIsSpdifOutNode(pInfo->pThis, pNode->node.uID))
        CODECDBG_PRINT("SPDIF OUT\n");
    else if (hdaCodecIsSpdifInNode(pInfo->pThis, pNode->node.uID))
        CODECDBG_PRINT("SPDIF IN\n");
    else if (hdaCodecIsDigInPinNode(pInfo->pThis, pNode->node.uID))
        CODECDBG_PRINT("DIGITAL IN PIN\n");
    else if (hdaCodecIsDigOutPinNode(pInfo->pThis, pNode->node.uID))
        CODECDBG_PRINT("DIGITAL OUT PIN\n");
    else if (hdaCodecIsCdNode(pInfo->pThis, pNode->node.uID))
        CODECDBG_PRINT("CD\n");
    else if (hdaCodecIsVolKnobNode(pInfo->pThis, pNode->node.uID))
        CODECDBG_PRINT("VOLUME KNOB\n");
    else if (hdaCodecIsReservedNode(pInfo->pThis, pNode->node.uID))
        CODECDBG_PRINT("RESERVED\n");
    else
        CODECDBG_PRINT("UNKNOWN TYPE 0x%x\n", pNode->node.uID);

    if (fRecursive)
    {
#define CODECDBG_PRINT_CONLIST_ENTRY(_aNode, _aEntry) \
            if (cCnt >= _aEntry) \
            { \
                const uint8_t uID = RT_BYTE##_aEntry(_aNode->node.au32F02_param[0x0]); \
                if (pNode->node.uID == uID) \
                    codecDbgPrintNode(pInfo, _aNode, false /* fRecursive */); \
            }

        /* Slow recursion, but this is debug stuff anyway. */
        for (uint8_t i = 0; i < pInfo->pThis->Cfg.cTotalNodes; i++)
        {
            const PCODECNODE pSubNode = &pInfo->pThis->aNodes[i];
            if (pSubNode->node.uID == pNode->node.uID)
                continue;

            const uint8_t cCnt = CODEC_F00_0E_COUNT(pSubNode->node.au32F00_param[0xE]);
            if (cCnt == 0) /* No connections present? Skip. */
                continue;

            CODECDBG_INDENT
                CODECDBG_PRINT_CONLIST_ENTRY(pSubNode, 1)
                CODECDBG_PRINT_CONLIST_ENTRY(pSubNode, 2)
                CODECDBG_PRINT_CONLIST_ENTRY(pSubNode, 3)
                CODECDBG_PRINT_CONLIST_ENTRY(pSubNode, 4)
            CODECDBG_UNINDENT
        }

#undef CODECDBG_PRINT_CONLIST_ENTRY
   }
}


/**
 * Worker for hdaR3DbgInfoCodecNodes implementing the 'hdcnodes' info item.
 */
DECLHIDDEN(void) hdaR3CodecDbgListNodes(PHDACODECR3 pThis, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);

    pHlp->pfnPrintf(pHlp, "HDA LINK / INPUTS\n");

    CODECDEBUG DbgInfo;
    DbgInfo.pHlp    = pHlp;
    DbgInfo.pThis   = pThis;
    DbgInfo.uLevel  = 0;

    PCODECDEBUG pInfo = &DbgInfo;

    CODECDBG_INDENT
        for (uint8_t i = 0; i < pThis->Cfg.cTotalNodes; i++)
        {
            PCODECNODE pNode = &pThis->aNodes[i];

            /* Start with all nodes which have connection entries set. */
            if (CODEC_F00_0E_COUNT(pNode->node.au32F00_param[0xE]))
                codecDbgPrintNode(&DbgInfo, pNode, true /* fRecursive */);
        }
    CODECDBG_UNINDENT
}


/**
 * Worker for hdaR3DbgInfoCodecSelector implementing the 'hdcselector' info item.
 */
DECLHIDDEN(void) hdaR3CodecDbgSelector(PHDACODECR3 pThis, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pThis, pHlp, pszArgs);
}


#if 0 /* unused */
static DECLCALLBACK(void) stac9220DbgNodes(PHDACODECR3 pThis, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    uint8_t const cTotalNodes = RT_MIN(pThis->Cfg.cTotalNodes, RT_ELEMENTS(pThis->aNodes));
    for (uint8_t i = 1; i < cTotalNodes; i++)
    {
        PCODECNODE pNode = &pThis->aNodes[i];
        AMPLIFIER *pAmp = &pNode->dac.B_params;

        uint8_t lVol = AMPLIFIER_REGISTER(*pAmp, AMPLIFIER_OUT, AMPLIFIER_LEFT, 0) & 0x7f;
        uint8_t rVol = AMPLIFIER_REGISTER(*pAmp, AMPLIFIER_OUT, AMPLIFIER_RIGHT, 0) & 0x7f;

        pHlp->pfnPrintf(pHlp, "0x%x: lVol=%RU8, rVol=%RU8\n", i, lVol, rVol);
    }
}
#endif


/*********************************************************************************************************************************
*   Stream and State Management                                                                                                  *
*********************************************************************************************************************************/

int hdaR3CodecAddStream(PHDACODECR3 pThis, PDMAUDIOMIXERCTL enmMixerCtl, PPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfg,  VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    switch (enmMixerCtl)
    {
        case PDMAUDIOMIXERCTL_VOLUME_MASTER:
        case PDMAUDIOMIXERCTL_FRONT:
#ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
        case PDMAUDIOMIXERCTL_CENTER_LFE:
        case PDMAUDIOMIXERCTL_REAR:
#endif
            break;

        case PDMAUDIOMIXERCTL_LINE_IN:
#ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
        case PDMAUDIOMIXERCTL_MIC_IN:
#endif
            break;

        default:
            AssertMsgFailed(("Mixer control %#x not implemented\n", enmMixerCtl));
            rc = VERR_NOT_IMPLEMENTED;
            break;
    }

    if (RT_SUCCESS(rc))
        rc = hdaR3MixerAddStream(pThis, enmMixerCtl, pCfg);

    LogFlowFuncLeaveRC(rc);
    return rc;
}


int hdaR3CodecRemoveStream(PHDACODECR3 pThis, PDMAUDIOMIXERCTL enmMixerCtl, bool fImmediate)
{
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    int rc = hdaR3MixerRemoveStream(pThis, enmMixerCtl, fImmediate);

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Saved the codec state.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance of the HDA device.
 * @param   pThis               The codec instance data.
 * @param   pSSM                The saved state handle.
 */
int hdaCodecSaveState(PPDMDEVINS pDevIns, PHDACODECR3 pThis, PSSMHANDLE pSSM)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;
    AssertLogRelMsgReturn(pThis->Cfg.cTotalNodes == STAC9221_NUM_NODES, ("cTotalNodes=%#x, should be 0x1c", pThis->Cfg.cTotalNodes),
                          VERR_INTERNAL_ERROR);
    pHlp->pfnSSMPutU32(pSSM, pThis->Cfg.cTotalNodes);
    for (unsigned idxNode = 0; idxNode < pThis->Cfg.cTotalNodes; ++idxNode)
        pHlp->pfnSSMPutStructEx(pSSM, &pThis->aNodes[idxNode].SavedState, sizeof(pThis->aNodes[idxNode].SavedState),
                                0 /*fFlags*/, g_aCodecNodeFields, NULL /*pvUser*/);
    return VINF_SUCCESS;
}


/**
 * Loads the codec state.
 *
 * @returns VBox status code.
 * @param   pDevIns             The device instance of the HDA device.
 * @param   pThis               The codec instance data.
 * @param   pSSM                The saved state handle.
 * @param   uVersion            The state version.
 */
int hdaR3CodecLoadState(PPDMDEVINS pDevIns, PHDACODECR3 pThis, PSSMHANDLE pSSM, uint32_t uVersion)
{
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    PCSSMFIELD      pFields = NULL;
    uint32_t        fFlags  = 0;
    if (uVersion >= HDA_SAVED_STATE_VERSION_4)
    {
        /* Since version 4 a flexible node count is supported. */
        uint32_t cNodes;
        int rc2 = pHlp->pfnSSMGetU32(pSSM, &cNodes);
        AssertRCReturn(rc2, rc2);
        AssertReturn(cNodes == 0x1c, VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
        AssertReturn(pThis->Cfg.cTotalNodes == 0x1c, VERR_INTERNAL_ERROR);

        pFields = g_aCodecNodeFields;
        fFlags  = 0;
    }
    else if (uVersion >= HDA_SAVED_STATE_VERSION_2)
    {
        AssertReturn(pThis->Cfg.cTotalNodes == 0x1c, VERR_INTERNAL_ERROR);
        pFields = g_aCodecNodeFields;
        fFlags  = SSMSTRUCT_FLAGS_MEM_BAND_AID_RELAXED;
    }
    else if (uVersion >= HDA_SAVED_STATE_VERSION_1)
    {
        AssertReturn(pThis->Cfg.cTotalNodes == 0x1c, VERR_INTERNAL_ERROR);
        pFields = g_aCodecNodeFieldsV1;
        fFlags  = SSMSTRUCT_FLAGS_MEM_BAND_AID_RELAXED;
    }
    else
        AssertFailedReturn(VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);

    for (unsigned idxNode = 0; idxNode < pThis->Cfg.cTotalNodes; ++idxNode)
    {
        uint8_t idOld = pThis->aNodes[idxNode].SavedState.Core.uID;
        int rc = pHlp->pfnSSMGetStructEx(pSSM, &pThis->aNodes[idxNode].SavedState, sizeof(pThis->aNodes[idxNode].SavedState),
                                         fFlags, pFields, NULL);
        AssertRCReturn(rc, rc);
        AssertLogRelMsgReturn(idOld == pThis->aNodes[idxNode].SavedState.Core.uID,
                              ("loaded %#x, expected %#x\n", pThis->aNodes[idxNode].SavedState.Core.uID, idOld),
                              VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
    }

    /*
     * Update stuff after changing the state.
     */
    PCODECNODE pNode;
    if (hdaCodecIsDacNode(pThis, pThis->Cfg.idxDacLineOut))
    {
        pNode = &pThis->aNodes[pThis->Cfg.idxDacLineOut];
        hdaR3CodecToAudVolume(pThis, pNode, &pNode->dac.B_params, PDMAUDIOMIXERCTL_FRONT);
    }
    else if (hdaCodecIsSpdifOutNode(pThis, pThis->Cfg.idxDacLineOut))
    {
        pNode = &pThis->aNodes[pThis->Cfg.idxDacLineOut];
        hdaR3CodecToAudVolume(pThis, pNode, &pNode->spdifout.B_params, PDMAUDIOMIXERCTL_FRONT);
    }

    pNode = &pThis->aNodes[pThis->Cfg.idxAdcVolsLineIn];
    hdaR3CodecToAudVolume(pThis, pNode, &pNode->adcvol.B_params, PDMAUDIOMIXERCTL_LINE_IN);

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}


/**
 * Powers off the codec (ring-3).
 *
 * @param   pThis       The codec data.
 */
void hdaR3CodecPowerOff(PHDACODECR3 pThis)
{
    LogFlowFuncEnter();
    LogRel2(("HDA: Powering off codec ...\n"));

    int rc2 = hdaR3CodecRemoveStream(pThis, PDMAUDIOMIXERCTL_FRONT, true /*fImmediate*/);
    AssertRC(rc2);
#ifdef VBOX_WITH_AUDIO_HDA_51_SURROUND
    rc2 = hdaR3CodecRemoveStream(pThis, PDMAUDIOMIXERCTL_CENTER_LFE, true /*fImmediate*/);
    AssertRC(rc2);
    rc2 = hdaR3CodecRemoveStream(pThis, PDMAUDIOMIXERCTL_REAR, true /*fImmediate*/);
    AssertRC(rc2);
#endif

#ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
    rc2 = hdaR3CodecRemoveStream(pThis, PDMAUDIOMIXERCTL_MIC_IN, true /*fImmediate*/);
    AssertRC(rc2);
#endif
    rc2 = hdaR3CodecRemoveStream(pThis, PDMAUDIOMIXERCTL_LINE_IN, true /*fImmediate*/);
    AssertRC(rc2);
}


/**
 * Constructs a codec (ring-3).
 *
 * @returns VBox status code.
 * @param   pDevIns     The associated device instance.
 * @param   pThis       The codec data.
 * @param   uLUN        Device LUN to assign.
 * @param   pCfg        CFGM node to use for configuration.
 */
int hdaR3CodecConstruct(PPDMDEVINS pDevIns, PHDACODECR3 pThis, uint16_t uLUN, PCFGMNODE pCfg)
{
    AssertPtrReturn(pDevIns, VERR_INVALID_POINTER);
    AssertPtrReturn(pThis,   VERR_INVALID_POINTER);
    AssertPtrReturn(pCfg,    VERR_INVALID_POINTER);
    HDACODECCFG *pCodecCfg = (HDACODECCFG *)&pThis->Cfg;

    pCodecCfg->id      = uLUN;
    pCodecCfg->enmType = CODECTYPE_STAC9220; /** @todo Make this dynamic. */

    int rc;

    switch (pCodecCfg->enmType)
    {
        case CODECTYPE_STAC9220:
        {
            rc = stac9220Construct(pThis, pCodecCfg);
            AssertRCReturn(rc, rc);
            break;
        }

        default:
            AssertFailedReturn(VERR_NOT_IMPLEMENTED);
            break;
    }

    /*
     * Set initial volume.
     */
    PCODECNODE pNode = &pThis->aNodes[pCodecCfg->idxDacLineOut];
    rc = hdaR3CodecToAudVolume(pThis, pNode, &pNode->dac.B_params, PDMAUDIOMIXERCTL_FRONT);
    AssertRCReturn(rc, rc);

    pNode = &pThis->aNodes[pCodecCfg->idxAdcVolsLineIn];
    rc = hdaR3CodecToAudVolume(pThis, pNode, &pNode->adcvol.B_params, PDMAUDIOMIXERCTL_LINE_IN);
    AssertRCReturn(rc, rc);

#ifdef VBOX_WITH_AUDIO_HDA_MIC_IN
# error "Implement mic-in support!"
#endif

    /*
     * Statistics
     */
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatLookupsR3, STAMTYPE_COUNTER, "Codec/LookupsR0", STAMUNIT_OCCURENCES, "Number of R0 codecLookup calls");
#if 0 /* Codec is not yet kosher enough for ring-0.  @bugref{9890c64} */
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatLookupsR0, STAMTYPE_COUNTER, "Codec/LookupsR3", STAMUNIT_OCCURENCES, "Number of R3 codecLookup calls");
#endif

    return rc;
}


/**
 * Destructs a codec.
 *
 * @param   pThis       Codec to destruct.
 */
void hdaCodecDestruct(PHDACODECR3 pThis)
{
    LogFlowFuncEnter();

    /* Nothing to do here atm. */
    RT_NOREF(pThis);
}


/**
 * Resets a codec.
 *
 * @param   pThis       Codec to reset.
 */
void hdaCodecReset(PHDACODECR3 pThis)
{
    switch (pThis->Cfg.enmType)
    {
        case CODECTYPE_STAC9220:
            stac9220Reset(pThis);
            break;

        default:
            AssertFailed();
            break;
    }
}

