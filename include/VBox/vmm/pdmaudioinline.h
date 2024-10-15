/* $Id: pdmaudioinline.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM - Audio Helpers, Inlined Code. (DEV,++)
 *
 * This is all inlined because it's too tedious to create a couple libraries to
 * contain it all (same bad excuse as for intnetinline.h & pdmnetinline.h).
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_vmm_pdmaudioinline_h
#define VBOX_INCLUDED_vmm_pdmaudioinline_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/vmm/pdmaudioifs.h>

#include <iprt/asm-mem.h>
#include <iprt/asm.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>


/** @defgroup grp_pdm_audio_inline      The PDM Audio Helper APIs
 * @ingroup grp_pdm
 * @{
 */


/**
 * Gets the name of an audio direction enum value.
 *
 * @returns Pointer to read-only name string on success, "bad" if passed an
 *          invalid enum value.
 * @param   enmDir  The audio direction value to name.
 */
DECLINLINE(const char *) PDMAudioDirGetName(PDMAUDIODIR enmDir)
{
    switch (enmDir)
    {
        case PDMAUDIODIR_INVALID: return "invalid";
        case PDMAUDIODIR_UNKNOWN: return "unknown";
        case PDMAUDIODIR_IN:      return "input";
        case PDMAUDIODIR_OUT:     return "output";
        case PDMAUDIODIR_DUPLEX:  return "duplex";

        /* no default */
        case PDMAUDIODIR_END:
        case PDMAUDIODIR_32BIT_HACK:
            break;
    }
    AssertMsgFailedReturn(("Invalid audio direction %d\n", enmDir), "bad");
}

/**
 * Gets the name of an audio mixer control enum value.
 *
 * @returns Pointer to read-only name, "bad" if invalid input.
 * @param   enmMixerCtl     The audio mixer control value.
 */
DECLINLINE(const char *) PDMAudioMixerCtlGetName(PDMAUDIOMIXERCTL enmMixerCtl)
{
    switch (enmMixerCtl)
    {
        case PDMAUDIOMIXERCTL_INVALID:      return "Invalid";
        case PDMAUDIOMIXERCTL_UNKNOWN:      return "Unknown";
        case PDMAUDIOMIXERCTL_VOLUME_MASTER: return "Master Volume";
        case PDMAUDIOMIXERCTL_FRONT:        return "Front";
        case PDMAUDIOMIXERCTL_CENTER_LFE:   return "Center / LFE";
        case PDMAUDIOMIXERCTL_REAR:         return "Rear";
        case PDMAUDIOMIXERCTL_LINE_IN:      return "Line-In";
        case PDMAUDIOMIXERCTL_MIC_IN:       return "Microphone-In";

        /* no default */
        case PDMAUDIOMIXERCTL_END:
        case PDMAUDIOMIXERCTL_32BIT_HACK:
            break;
    }
    AssertMsgFailedReturn(("Invalid mixer control %ld\n", enmMixerCtl), "bad");
}

/**
 * Gets the name of a path enum value.
 *
 * @returns Pointer to read-only name, "bad" if invalid input.
 * @param   enmPath     The path value to name.
 */
DECLINLINE(const char *) PDMAudioPathGetName(PDMAUDIOPATH enmPath)
{
    switch (enmPath)
    {
        case PDMAUDIOPATH_INVALID:          return "invalid";
        case PDMAUDIOPATH_UNKNOWN:          return "unknown";

        case PDMAUDIOPATH_OUT_FRONT:        return "front";
        case PDMAUDIOPATH_OUT_CENTER_LFE:   return "center-lfe";
        case PDMAUDIOPATH_OUT_REAR:         return "rear";

        case PDMAUDIOPATH_IN_MIC:           return "mic";
        case PDMAUDIOPATH_IN_CD:            return "cd";
        case PDMAUDIOPATH_IN_VIDEO:         return "video-in";
        case PDMAUDIOPATH_IN_AUX:           return "aux-in";
        case PDMAUDIOPATH_IN_LINE:          return "line-in";
        case PDMAUDIOPATH_IN_PHONE:         return "phone";

        /* no default */
        case PDMAUDIOPATH_END:
        case PDMAUDIOPATH_32BIT_HACK:
            break;
    }
    AssertMsgFailedReturn(("Unknown enmPath=%d\n", enmPath), "bad");
}

/**
 * Gets the name of a channel.
 *
 * @returns Pointer to read-only name, "bad" if invalid input.
 * @param   enmChannelId    The channel ID to name.
 */
DECLINLINE(const char *) PDMAudioChannelIdGetName(PDMAUDIOCHANNELID enmChannelId)
{
    switch (enmChannelId)
    {
        case PDMAUDIOCHANNELID_INVALID:                 return "invalid";
        case PDMAUDIOCHANNELID_UNUSED_ZERO:             return "unused-zero";
        case PDMAUDIOCHANNELID_UNUSED_SILENCE:          return "unused-silence";
        case PDMAUDIOCHANNELID_UNKNOWN:                 return "unknown";

        case PDMAUDIOCHANNELID_FRONT_LEFT:              return "FL";
        case PDMAUDIOCHANNELID_FRONT_RIGHT:             return "FR";
        case PDMAUDIOCHANNELID_FRONT_CENTER:            return "FC";
        case PDMAUDIOCHANNELID_LFE:                     return "LFE";
        case PDMAUDIOCHANNELID_REAR_LEFT:               return "BL";
        case PDMAUDIOCHANNELID_REAR_RIGHT:              return "BR";
        case PDMAUDIOCHANNELID_FRONT_LEFT_OF_CENTER:    return "FLC";
        case PDMAUDIOCHANNELID_FRONT_RIGHT_OF_CENTER:   return "FRC";
        case PDMAUDIOCHANNELID_REAR_CENTER:             return "BC";
        case PDMAUDIOCHANNELID_SIDE_LEFT:               return "SL";
        case PDMAUDIOCHANNELID_SIDE_RIGHT:              return "SR";
        case PDMAUDIOCHANNELID_TOP_CENTER:              return "TC";
        case PDMAUDIOCHANNELID_FRONT_LEFT_HEIGHT:       return "TFL";
        case PDMAUDIOCHANNELID_FRONT_CENTER_HEIGHT:     return "TFC";
        case PDMAUDIOCHANNELID_FRONT_RIGHT_HEIGHT:      return "TFR";
        case PDMAUDIOCHANNELID_REAR_LEFT_HEIGHT:        return "TBL";
        case PDMAUDIOCHANNELID_REAR_CENTER_HEIGHT:      return "TBC";
        case PDMAUDIOCHANNELID_REAR_RIGHT_HEIGHT:       return "TBR";

        /* no default */
        case PDMAUDIOCHANNELID_END:
        case PDMAUDIOCHANNELID_32BIT_HACK:
            break;
    }
    AssertMsgFailedReturn(("Unknown enmChannelId=%d\n", enmChannelId), "bad");
}


/*********************************************************************************************************************************
*  Volume Helpers                                                                                                                *
*********************************************************************************************************************************/

/**
 * Initializes a PDMAUDIOVOLUME structure to max.
 *
 * @param   pVol    The structure to initialize.
 */
DECLINLINE(void) PDMAudioVolumeInitMax(PPDMAUDIOVOLUME pVol)
{
    pVol->fMuted = false;
    for (uintptr_t i = 0; i < RT_ELEMENTS(pVol->auChannels); i++)
        pVol->auChannels[i] = PDMAUDIO_VOLUME_MAX;
}


/**
 * Initializes a PDMAUDIOVOLUME structure from a simple stereo setting.
 *
 * The additional channels will simply be assigned the higer of the two.
 *
 * @param   pVol    The structure to initialize.
 * @param   fMuted  Muted.
 * @param   bLeft   The left channel volume.
 * @param   bRight  The right channel volume.
 */
DECLINLINE(void) PDMAudioVolumeInitFromStereo(PPDMAUDIOVOLUME pVol, bool fMuted, uint8_t bLeft, uint8_t bRight)
{
    pVol->fMuted        = fMuted;
    pVol->auChannels[0] = bLeft;
    pVol->auChannels[1] = bRight;

    uint8_t const bOther = RT_MAX(bLeft, bRight);
    for (uintptr_t i = 2; i < RT_ELEMENTS(pVol->auChannels); i++)
        pVol->auChannels[i] = bOther;
}


/**
 * Combines two volume settings (typically master and sink).
 *
 * @param   pVol    Where to return the combined volume
 * @param   pVol1   The first volume settings to combine.
 * @param   pVol2   The second volume settings.
 */
DECLINLINE(void) PDMAudioVolumeCombine(PPDMAUDIOVOLUME pVol, PCPDMAUDIOVOLUME pVol1, PCPDMAUDIOVOLUME pVol2)
{
    if (pVol1->fMuted || pVol2->fMuted)
    {
        pVol->fMuted = true;
        for (uintptr_t i = 0; i < RT_ELEMENTS(pVol->auChannels); i++)
            pVol->auChannels[i] = 0;
    }
    else
    {
        pVol->fMuted = false;
        /** @todo Very crude implementation for now -- needs more work! (At least
         *        when used in audioMixerSinkUpdateVolume it was considered as such.) */
        for (uintptr_t i = 0; i < RT_ELEMENTS(pVol->auChannels); i++)
        {
#if 0 /* bird: I think the shift variant should produce the exact same result, w/o two conditionals per iteration. */
            /* 255 * 255 / 255 = 0xFF (255) */
            /*  17 * 127 / 255 = 8 */
            /*  39 *  39 / 255 = 5 */
            pVol->auChannels[i] = (uint8_t)(  (RT_MAX(pVol1->auChannels[i], 1U) * RT_MAX(pVol2->auChannels[i], 1U))
                                            / PDMAUDIO_VOLUME_MAX);
#else
            /* (((255 + 1) * (255 + 1)) >> 8) - 1 = 0xFF (255) */
            /* ((( 17 + 1) * (127 + 1)) >> 8) - 1 = 0x8 (8) */
            /* ((( 39 + 1) * ( 39 + 1)) >> 8) - 1 = 0x5 (5) */
            pVol->auChannels[i] = (uint8_t)((((1U + pVol1->auChannels[i]) * (1U + pVol2->auChannels[i])) >> 8) - 1U);
#endif
        }
    }
}


/*********************************************************************************************************************************
*   PCM Property Helpers                                                                                                         *
*********************************************************************************************************************************/

/**
 * Assigns default channel IDs according to the channel count.
 *
 * The assignments are taken from the standard speaker channel layouts table
 * in the wikipedia article on surround sound:
 *      https://en.wikipedia.org/wiki/Surround_sound#Standard_speaker_channels
 */
DECLINLINE(void) PDMAudioPropsSetDefaultChannelIds(PPDMAUDIOPCMPROPS pProps)
{
    unsigned cChannels = pProps->cChannelsX;
    switch (cChannels)
    {
        case 1:
            pProps->aidChannels[0] = PDMAUDIOCHANNELID_MONO;
            break;
        case 2:
            pProps->aidChannels[0] = PDMAUDIOCHANNELID_FRONT_LEFT;
            pProps->aidChannels[1] = PDMAUDIOCHANNELID_FRONT_RIGHT;
            break;
        case 3: /* 2.1 */
            pProps->aidChannels[0] = PDMAUDIOCHANNELID_FRONT_LEFT;
            pProps->aidChannels[1] = PDMAUDIOCHANNELID_FRONT_RIGHT;
            pProps->aidChannels[2] = PDMAUDIOCHANNELID_LFE;
            break;
        case 4: /* 4.0 */
            pProps->aidChannels[0] = PDMAUDIOCHANNELID_FRONT_LEFT;
            pProps->aidChannels[1] = PDMAUDIOCHANNELID_FRONT_RIGHT;
            pProps->aidChannels[2] = PDMAUDIOCHANNELID_REAR_LEFT;
            pProps->aidChannels[3] = PDMAUDIOCHANNELID_REAR_RIGHT;
            break;
        case 5: /* 4.1 */
            pProps->aidChannels[0] = PDMAUDIOCHANNELID_FRONT_LEFT;
            pProps->aidChannels[1] = PDMAUDIOCHANNELID_FRONT_RIGHT;
            pProps->aidChannels[2] = PDMAUDIOCHANNELID_FRONT_CENTER;
            pProps->aidChannels[3] = PDMAUDIOCHANNELID_LFE;
            pProps->aidChannels[4] = PDMAUDIOCHANNELID_REAR_CENTER;
            break;
        case 6: /* 5.1 */
            pProps->aidChannels[0] = PDMAUDIOCHANNELID_FRONT_LEFT;
            pProps->aidChannels[1] = PDMAUDIOCHANNELID_FRONT_RIGHT;
            pProps->aidChannels[2] = PDMAUDIOCHANNELID_FRONT_CENTER;
            pProps->aidChannels[3] = PDMAUDIOCHANNELID_LFE;
            pProps->aidChannels[4] = PDMAUDIOCHANNELID_REAR_LEFT;
            pProps->aidChannels[5] = PDMAUDIOCHANNELID_REAR_RIGHT;
            break;
        case 7: /* 6.1 */
            pProps->aidChannels[0] = PDMAUDIOCHANNELID_FRONT_LEFT;
            pProps->aidChannels[1] = PDMAUDIOCHANNELID_FRONT_RIGHT;
            pProps->aidChannels[2] = PDMAUDIOCHANNELID_FRONT_CENTER;
            pProps->aidChannels[3] = PDMAUDIOCHANNELID_LFE;
            pProps->aidChannels[4] = PDMAUDIOCHANNELID_REAR_LEFT;
            pProps->aidChannels[5] = PDMAUDIOCHANNELID_REAR_RIGHT;
            pProps->aidChannels[6] = PDMAUDIOCHANNELID_REAR_CENTER;
            break;
        case 8: /* 7.1 */
            pProps->aidChannels[0] = PDMAUDIOCHANNELID_FRONT_LEFT;
            pProps->aidChannels[1] = PDMAUDIOCHANNELID_FRONT_RIGHT;
            pProps->aidChannels[2] = PDMAUDIOCHANNELID_FRONT_CENTER;
            pProps->aidChannels[3] = PDMAUDIOCHANNELID_LFE;
            pProps->aidChannels[4] = PDMAUDIOCHANNELID_REAR_LEFT;
            pProps->aidChannels[5] = PDMAUDIOCHANNELID_REAR_RIGHT;
            pProps->aidChannels[6] = PDMAUDIOCHANNELID_FRONT_LEFT_OF_CENTER;
            pProps->aidChannels[7] = PDMAUDIOCHANNELID_FRONT_RIGHT_OF_CENTER;
            break;
        case 9: /* 9.0 */
            pProps->aidChannels[0] = PDMAUDIOCHANNELID_FRONT_LEFT;
            pProps->aidChannels[1] = PDMAUDIOCHANNELID_FRONT_RIGHT;
            pProps->aidChannels[2] = PDMAUDIOCHANNELID_FRONT_CENTER;
            pProps->aidChannels[3] = PDMAUDIOCHANNELID_REAR_LEFT;
            pProps->aidChannels[4] = PDMAUDIOCHANNELID_REAR_RIGHT;
            pProps->aidChannels[5] = PDMAUDIOCHANNELID_SIDE_LEFT;
            pProps->aidChannels[6] = PDMAUDIOCHANNELID_SIDE_RIGHT;
            pProps->aidChannels[7] = PDMAUDIOCHANNELID_FRONT_LEFT_HEIGHT;
            pProps->aidChannels[8] = PDMAUDIOCHANNELID_FRONT_RIGHT_HEIGHT;
            break;
        case 10: /* 9.1 */
            pProps->aidChannels[0] = PDMAUDIOCHANNELID_FRONT_LEFT;
            pProps->aidChannels[1] = PDMAUDIOCHANNELID_FRONT_RIGHT;
            pProps->aidChannels[2] = PDMAUDIOCHANNELID_FRONT_CENTER;
            pProps->aidChannels[3] = PDMAUDIOCHANNELID_LFE;
            pProps->aidChannels[4] = PDMAUDIOCHANNELID_REAR_LEFT;
            pProps->aidChannels[5] = PDMAUDIOCHANNELID_REAR_RIGHT;
            pProps->aidChannels[6] = PDMAUDIOCHANNELID_SIDE_LEFT;
            pProps->aidChannels[7] = PDMAUDIOCHANNELID_SIDE_RIGHT;
            pProps->aidChannels[8] = PDMAUDIOCHANNELID_FRONT_LEFT_HEIGHT;
            pProps->aidChannels[9] = PDMAUDIOCHANNELID_FRONT_RIGHT_HEIGHT;
            break;
        case 11: /* 11.0 */
            pProps->aidChannels[0] = PDMAUDIOCHANNELID_FRONT_LEFT;
            pProps->aidChannels[1] = PDMAUDIOCHANNELID_FRONT_RIGHT;
            pProps->aidChannels[2] = PDMAUDIOCHANNELID_FRONT_CENTER;
            pProps->aidChannels[3] = PDMAUDIOCHANNELID_REAR_LEFT;
            pProps->aidChannels[4] = PDMAUDIOCHANNELID_REAR_RIGHT;
            pProps->aidChannels[5] = PDMAUDIOCHANNELID_FRONT_LEFT_OF_CENTER;
            pProps->aidChannels[6] = PDMAUDIOCHANNELID_FRONT_RIGHT_OF_CENTER;
            pProps->aidChannels[7] = PDMAUDIOCHANNELID_SIDE_LEFT;
            pProps->aidChannels[8] = PDMAUDIOCHANNELID_SIDE_RIGHT;
            pProps->aidChannels[9] = PDMAUDIOCHANNELID_FRONT_LEFT_HEIGHT;
            pProps->aidChannels[10]= PDMAUDIOCHANNELID_FRONT_RIGHT_HEIGHT;
            break;
        default:
            AssertFailed();
            cChannels = 12;
            RT_FALL_THROUGH();
        case 12: /* 11.1 */
            pProps->aidChannels[0] = PDMAUDIOCHANNELID_FRONT_LEFT;
            pProps->aidChannels[1] = PDMAUDIOCHANNELID_FRONT_RIGHT;
            pProps->aidChannels[2] = PDMAUDIOCHANNELID_FRONT_CENTER;
            pProps->aidChannels[3] = PDMAUDIOCHANNELID_LFE;
            pProps->aidChannels[4] = PDMAUDIOCHANNELID_REAR_LEFT;
            pProps->aidChannels[5] = PDMAUDIOCHANNELID_REAR_RIGHT;
            pProps->aidChannels[6] = PDMAUDIOCHANNELID_FRONT_LEFT_OF_CENTER;
            pProps->aidChannels[7] = PDMAUDIOCHANNELID_FRONT_RIGHT_OF_CENTER;
            pProps->aidChannels[8] = PDMAUDIOCHANNELID_SIDE_LEFT;
            pProps->aidChannels[9] = PDMAUDIOCHANNELID_SIDE_RIGHT;
            pProps->aidChannels[10]= PDMAUDIOCHANNELID_FRONT_LEFT_HEIGHT;
            pProps->aidChannels[11]= PDMAUDIOCHANNELID_FRONT_RIGHT_HEIGHT;
            break;
        case 0:
            break;
    }
    AssertCompile(RT_ELEMENTS(pProps->aidChannels) >= 12);

    while (cChannels < RT_ELEMENTS(pProps->aidChannels))
        pProps->aidChannels[cChannels++] = PDMAUDIOCHANNELID_INVALID;
}


/**
 * Initialize PCM audio properties.
 */
DECLINLINE(void) PDMAudioPropsInit(PPDMAUDIOPCMPROPS pProps, uint8_t cbSample, bool fSigned, uint8_t cChannels, uint32_t uHz)
{
    Assert(cChannels <= PDMAUDIO_MAX_CHANNELS);

    pProps->cbFrame     = cbSample * cChannels;
    pProps->cbSampleX   = cbSample;
    pProps->cChannelsX  = cChannels;
    pProps->cShiftX     = PDMAUDIOPCMPROPS_MAKE_SHIFT_PARMS(cbSample, cChannels);
    pProps->fSigned     = fSigned;
    pProps->fSwapEndian = false;
    pProps->fRaw        = false;
    pProps->uHz         = uHz;

    Assert(pProps->cbFrame    == (uint32_t)cbSample * cChannels);
    Assert(pProps->cbSampleX  == cbSample);
    Assert(pProps->cChannelsX == cChannels);

    PDMAudioPropsSetDefaultChannelIds(pProps);
}

/**
 * Initialize PCM audio properties, extended version.
 */
DECLINLINE(void) PDMAudioPropsInitEx(PPDMAUDIOPCMPROPS pProps, uint8_t cbSample, bool fSigned, uint8_t cChannels, uint32_t uHz,
                                     bool fLittleEndian, bool fRaw)
{
    Assert(cChannels <= PDMAUDIO_MAX_CHANNELS);
    Assert(!fRaw || cbSample == sizeof(int64_t));
    pProps->cbFrame     = cbSample * cChannels;
    pProps->cbSampleX   = cbSample;
    pProps->cChannelsX  = cChannels;
    pProps->cShiftX     = PDMAUDIOPCMPROPS_MAKE_SHIFT_PARMS(cbSample, cChannels);
    pProps->fSigned     = fSigned;
#ifdef RT_LITTLE_ENDIAN
    pProps->fSwapEndian = !fLittleEndian;
#else
    pProps->fSwapEndian = fLittleEndian;
#endif
    pProps->fRaw        = fRaw;
    pProps->uHz         = uHz;

    Assert(pProps->cbFrame    == (uint32_t)cbSample * cChannels);
    Assert(pProps->cbSampleX  == cbSample);
    Assert(pProps->cChannelsX == cChannels);

    PDMAudioPropsSetDefaultChannelIds(pProps);
}

/**
 * Modifies the channel count.
 *
 * @note    This will reset the channel IDs to defaults.
 *
 * @param   pProps              The PCM properties to update.
 * @param   cChannels           The new channel count.
 */
DECLINLINE(void) PDMAudioPropsSetChannels(PPDMAUDIOPCMPROPS pProps, uint8_t cChannels)
{
    Assert(cChannels > 0); Assert(cChannels < 16);
    pProps->cChannelsX  = cChannels;
    pProps->cbFrame     = pProps->cbSampleX * cChannels;
    pProps->cShiftX     = PDMAUDIOPCMPROPS_MAKE_SHIFT_PARMS(pProps->cbSampleX, cChannels);

    PDMAudioPropsSetDefaultChannelIds(pProps);
}

/**
 * Modifies the sample size.
 *
 * @param   pProps              The PCM properties to update.
 * @param   cbSample            The new sample size (in bytes).
 */
DECLINLINE(void) PDMAudioPropsSetSampleSize(PPDMAUDIOPCMPROPS pProps, uint8_t cbSample)
{
    Assert(cbSample == 1 || cbSample == 2 || cbSample == 4 || cbSample == 8);
    pProps->cbSampleX   = cbSample;
    pProps->cbFrame     = cbSample * pProps->cChannelsX;
    pProps->cShiftX     = PDMAUDIOPCMPROPS_MAKE_SHIFT_PARMS(cbSample, pProps->cChannelsX);
}

/**
 * Gets the bitrate.
 *
 * Divide the result by 8 to get the byte rate.
 *
 * @returns Bit rate.
 * @param   pProps              PCM properties to calculate bitrate for.
 */
DECLINLINE(uint32_t) PDMAudioPropsGetBitrate(PCPDMAUDIOPCMPROPS pProps)
{
    Assert(pProps->cbFrame == pProps->cbSampleX * pProps->cChannelsX);
    return pProps->cbFrame * pProps->uHz * 8;
}

/**
 * Gets the number of channels.
 * @returns The channel count.
 * @param   pProps      The PCM properties.
 */
DECL_FORCE_INLINE(uint8_t) PDMAudioPropsChannels(PCPDMAUDIOPCMPROPS pProps)
{
    AssertReturn(pProps->cChannelsX <= PDMAUDIO_MAX_CHANNELS, PDMAUDIO_MAX_CHANNELS);
    return pProps->cChannelsX;
}

/**
 * Gets the sample size in bytes.
 * @returns Number of bytes per sample.
 * @param   pProps      The PCM properties.
 */
DECL_FORCE_INLINE(uint8_t) PDMAudioPropsSampleSize(PCPDMAUDIOPCMPROPS pProps)
{
    return pProps->cbSampleX;
}

/**
 * Gets the sample size in bits.
 * @returns Number of bits per sample.
 * @param   pProps      The PCM properties.
 */
DECLINLINE(uint8_t) PDMAudioPropsSampleBits(PCPDMAUDIOPCMPROPS pProps)
{
    return pProps->cbSampleX * 8;
}

/**
 * Gets the frame size in bytes.
 * @returns Number of bytes per frame.
 * @param   pProps      The PCM properties.
 */
DECL_FORCE_INLINE(uint8_t) PDMAudioPropsFrameSize(PCPDMAUDIOPCMPROPS pProps)
{
    return pProps->cbFrame;
}

/**
 * Gets the frequency.
 * @returns Frequency.
 * @param   pProps      The PCM properties.
 */
DECL_FORCE_INLINE(uint32_t) PDMAudioPropsHz(PCPDMAUDIOPCMPROPS pProps)
{
    return pProps->uHz;
}

/**
 * Checks if the format is signed or unsigned.
 * @returns true if signed, false if unsigned.
 * @param   pProps      The PCM properties.
 */
DECL_FORCE_INLINE(bool) PDMAudioPropsIsSigned(PCPDMAUDIOPCMPROPS pProps)
{
    return pProps->fSigned;
}

/**
 * Checks if the format is little-endian or not.
 * @returns true if little-endian (or if 8-bit), false if big-endian.
 * @param   pProps      The PCM properties.
 */
DECL_FORCE_INLINE(bool) PDMAudioPropsIsLittleEndian(PCPDMAUDIOPCMPROPS pProps)
{
#ifdef RT_LITTLE_ENDIAN
    return !pProps->fSwapEndian || pProps->cbSampleX < 2;
#else
    return pProps->fSwapEndian  || pProps->cbSampleX < 2;
#endif
}

/**
 * Checks if the format is big-endian or not.
 * @returns true if big-endian (or if 8-bit), false if little-endian.
 * @param   pProps      The PCM properties.
 */
DECL_FORCE_INLINE(bool) PDMAudioPropsIsBigEndian(PCPDMAUDIOPCMPROPS pProps)
{
#ifdef RT_LITTLE_ENDIAN
    return pProps->fSwapEndian || pProps->cbSampleX < 2;
#else
    return !pProps->fSwapEndian  || pProps->cbSampleX < 2;
#endif
}

/**
 * Rounds down the given byte amount to the nearest frame boundrary.
 *
 * @returns Rounded byte amount.
 * @param   pProps      PCM properties to use.
 * @param   cb          The size (in bytes) to round.
 */
DECLINLINE(uint32_t) PDMAudioPropsFloorBytesToFrame(PCPDMAUDIOPCMPROPS pProps, uint32_t cb)
{
    AssertPtrReturn(pProps, 0);
    return PDMAUDIOPCMPROPS_F2B(pProps, PDMAUDIOPCMPROPS_B2F(pProps, cb));
}

/**
 * Rounds up the given byte amount to the nearest frame boundrary.
 *
 * @returns Rounded byte amount.
 * @param   pProps      PCM properties to use.
 * @param   cb          The size (in bytes) to round.
 */
DECLINLINE(uint32_t) PDMAudioPropsRoundUpBytesToFrame(PCPDMAUDIOPCMPROPS pProps, uint32_t cb)
{
    AssertPtrReturn(pProps, 0);
    uint32_t const cbFrame = PDMAudioPropsFrameSize(pProps);
    AssertReturn(cbFrame, 0);
    return PDMAUDIOPCMPROPS_F2B(pProps, PDMAUDIOPCMPROPS_B2F(pProps, cb + cbFrame - 1));
}

/**
 * Checks if the given size is aligned on a frame boundrary.
 *
 * @returns @c true if properly aligned, @c false if not.
 * @param   pProps      PCM properties to use.
 * @param   cb          The size (in bytes) to check.
 */
DECLINLINE(bool) PDMAudioPropsIsSizeAligned(PCPDMAUDIOPCMPROPS pProps, uint32_t cb)
{
    AssertPtrReturn(pProps, false);
    uint32_t const cbFrame = PDMAudioPropsFrameSize(pProps);
    AssertReturn(cbFrame, false);
    return cb % cbFrame == 0;
}

/**
 * Converts bytes to frames (rounding down of course).
 *
 * @returns Number of frames.
 * @param   pProps      PCM properties to use.
 * @param   cb          The number of bytes to convert.
 */
DECLINLINE(uint32_t) PDMAudioPropsBytesToFrames(PCPDMAUDIOPCMPROPS pProps, uint32_t cb)
{
    AssertPtrReturn(pProps, 0);
    return PDMAUDIOPCMPROPS_B2F(pProps, cb);
}

/**
 * Converts bytes to milliseconds.
 *
 * @return  Number milliseconds @a cb takes to play or record.
 * @param   pProps      PCM properties to use.
 * @param   cb          The number of bytes to convert.
 *
 * @note    Rounds up the result.
 */
DECLINLINE(uint64_t) PDMAudioPropsBytesToMilli(PCPDMAUDIOPCMPROPS pProps, uint32_t cb)
{
    AssertPtrReturn(pProps, 0);

    /* Check parameters to prevent division by chainsaw: */
    uint32_t const uHz = pProps->uHz;
    if (uHz)
    {
        const unsigned cbFrame = PDMAudioPropsFrameSize(pProps);
        if (cbFrame)
        {
            /* Round cb up to closest frame size: */
            cb = (cb + cbFrame - 1) / cbFrame;

            /* Convert to milliseconds. */
            return (cb * (uint64_t)RT_MS_1SEC + uHz - 1) / uHz;
        }
    }
    return 0;
}

/**
 * Converts bytes to microseconds.
 *
 * @return  Number microseconds @a cb takes to play or record.
 * @param   pProps      PCM properties to use.
 * @param   cb          The number of bytes to convert.
 *
 * @note    Rounds up the result.
 */
DECLINLINE(uint64_t) PDMAudioPropsBytesToMicro(PCPDMAUDIOPCMPROPS pProps, uint32_t cb)
{
    AssertPtrReturn(pProps, 0);

    /* Check parameters to prevent division by chainsaw: */
    uint32_t const uHz = pProps->uHz;
    if (uHz)
    {
        const unsigned cbFrame = PDMAudioPropsFrameSize(pProps);
        if (cbFrame)
        {
            /* Round cb up to closest frame size: */
            cb = (cb + cbFrame - 1) / cbFrame;

            /* Convert to microseconds. */
            return (cb * (uint64_t)RT_US_1SEC + uHz - 1) / uHz;
        }
    }
    return 0;
}

/**
 * Converts bytes to nanoseconds.
 *
 * @return  Number nanoseconds @a cb takes to play or record.
 * @param   pProps      PCM properties to use.
 * @param   cb          The number of bytes to convert.
 *
 * @note    Rounds up the result.
 */
DECLINLINE(uint64_t) PDMAudioPropsBytesToNano(PCPDMAUDIOPCMPROPS pProps, uint32_t cb)
{
    AssertPtrReturn(pProps, 0);

    /* Check parameters to prevent division by chainsaw: */
    uint32_t const uHz = pProps->uHz;
    if (uHz)
    {
        const unsigned cbFrame = PDMAudioPropsFrameSize(pProps);
        if (cbFrame)
        {
            /* Round cb up to closest frame size: */
            cb = (cb + cbFrame - 1) / cbFrame;

            /* Convert to nanoseconds. */
            return (cb * (uint64_t)RT_NS_1SEC + uHz - 1) / uHz;
        }
    }
    return 0;
}

/**
 * Converts bytes to nanoseconds, 64-bit version.
 *
 * @return  Number nanoseconds @a cb takes to play or record.
 * @param   pProps      PCM properties to use.
 * @param   cb          The number of bytes to convert (64-bit).
 *
 * @note    Rounds up the result.
 */
DECLINLINE(uint64_t) PDMAudioPropsBytesToNano64(PCPDMAUDIOPCMPROPS pProps, uint64_t cb)
{
    AssertPtrReturn(pProps, 0);

    /* Check parameters to prevent division by chainsaw: */
    uint32_t const uHz = pProps->uHz;
    if (uHz)
    {
        const unsigned cbFrame = PDMAudioPropsFrameSize(pProps);
        if (cbFrame)
        {
            /* Round cb up to closest frame size: */
            cb = (cb + cbFrame - 1) / cbFrame;

            /* Convert to nanoseconds. */
            return (cb * RT_NS_1SEC + uHz - 1) / uHz;
        }
    }
    return 0;
}

/**
 * Converts frames to bytes.
 *
 * @returns Number of bytes.
 * @param   pProps      The PCM properties to use.
 * @param   cFrames     Number of audio frames to convert.
 * @sa      PDMAUDIOPCMPROPS_F2B
 */
DECLINLINE(uint32_t) PDMAudioPropsFramesToBytes(PCPDMAUDIOPCMPROPS pProps, uint32_t cFrames)
{
    AssertPtrReturn(pProps, 0);
    return PDMAUDIOPCMPROPS_F2B(pProps, cFrames);
}

/**
 * Converts frames to milliseconds.
 *
 * @returns milliseconds.
 * @param   pProps      The PCM properties to use.
 * @param   cFrames     Number of audio frames to convert.
 * @note    No rounding here, result is floored.
 */
DECLINLINE(uint64_t) PDMAudioPropsFramesToMilli(PCPDMAUDIOPCMPROPS pProps, uint32_t cFrames)
{
    AssertPtrReturn(pProps, 0);

    /* Check input to prevent division by chainsaw: */
    uint32_t const uHz = pProps->uHz;
    if (uHz)
        return ASMMultU32ByU32DivByU32(cFrames, RT_MS_1SEC, uHz);
    return 0;
}

/**
 * Converts frames to milliseconds, but not returning more than @a cMsMax
 *
 * This is a convenience for logging and such.
 *
 * @returns milliseconds (32-bit).
 * @param   pProps      The PCM properties to use.
 * @param   cFrames     Number of audio frames to convert.
 * @param   cMsMax      Max return value (32-bit).
 * @note    No rounding here, result is floored.
 */
DECLINLINE(uint32_t) PDMAudioPropsFramesToMilliMax(PCPDMAUDIOPCMPROPS pProps, uint32_t cFrames, uint32_t cMsMax)
{
    AssertPtrReturn(pProps, 0);

    /* Check input to prevent division by chainsaw: */
    uint32_t const uHz = pProps->uHz;
    if (uHz)
    {
        uint32_t const cMsResult = ASMMultU32ByU32DivByU32(cFrames, RT_MS_1SEC, uHz);
        return RT_MIN(cMsResult, cMsMax);
    }
    return 0;
}

/**
 * Converts frames to microseconds.
 *
 * @returns microseconds.
 * @param   pProps      The PCM properties to use.
 * @param   cFrames     Number of audio frames to convert.
 * @note    No rounding here, result is floored.
 */
DECLINLINE(uint64_t) PDMAudioPropsFramesToMicro(PCPDMAUDIOPCMPROPS pProps, uint32_t cFrames)
{
    AssertPtrReturn(pProps, 0);

    /* Check input to prevent division by chainsaw: */
    uint32_t const uHz = pProps->uHz;
    if (uHz)
        return ASMMultU32ByU32DivByU32(cFrames, RT_US_1SEC, uHz);
    return 0;
}

/**
 * Converts frames to nanoseconds.
 *
 * @returns Nanoseconds.
 * @param   pProps      The PCM properties to use.
 * @param   cFrames     Number of audio frames to convert.
 * @note    No rounding here, result is floored.
 */
DECLINLINE(uint64_t) PDMAudioPropsFramesToNano(PCPDMAUDIOPCMPROPS pProps, uint32_t cFrames)
{
    AssertPtrReturn(pProps, 0);

    /* Check input to prevent division by chainsaw: */
    uint32_t const uHz = pProps->uHz;
    if (uHz)
        return ASMMultU32ByU32DivByU32(cFrames, RT_NS_1SEC, uHz);
    return 0;
}

/**
 * Converts frames to NT ticks (100 ns units).
 *
 * @returns NT ticks.
 * @param   pProps      The PCM properties to use.
 * @param   cFrames     Number of audio frames to convert.
 * @note    No rounding here, result is floored.
 */
DECLINLINE(uint64_t) PDMAudioPropsFramesToNtTicks(PCPDMAUDIOPCMPROPS pProps, uint32_t cFrames)
{
    AssertPtrReturn(pProps, 0);

    /* Check input to prevent division by chainsaw: */
    uint32_t const uHz = pProps->uHz;
    if (uHz)
        return ASMMultU32ByU32DivByU32(cFrames, RT_NS_1SEC / 100, uHz);
    return 0;
}

/**
 * Converts milliseconds to frames.
 *
 * @returns Number of frames
 * @param   pProps      The PCM properties to use.
 * @param   cMs         The number of milliseconds to convert.
 *
 * @note    The result is rounded rather than floored (hysterical raisins).
 */
DECLINLINE(uint32_t) PDMAudioPropsMilliToFrames(PCPDMAUDIOPCMPROPS pProps, uint64_t cMs)
{
    AssertPtrReturn(pProps, 0);

    uint32_t const uHz = pProps->uHz;
    uint32_t cFrames;
    if (cMs < RT_MS_1SEC)
        cFrames = 0;
    else
    {
        cFrames = cMs / RT_MS_1SEC * uHz;
        cMs %= RT_MS_1SEC;
    }
    cFrames += (ASMMult2xU32RetU64(uHz, (uint32_t)cMs) + RT_MS_1SEC - 1) / RT_MS_1SEC;
    return cFrames;
}

/**
 * Converts milliseconds to bytes.
 *
 * @returns Number of bytes (frame aligned).
 * @param   pProps      The PCM properties to use.
 * @param   cMs         The number of milliseconds to convert.
 *
 * @note    The result is rounded rather than floored (hysterical raisins).
 */
DECLINLINE(uint32_t) PDMAudioPropsMilliToBytes(PCPDMAUDIOPCMPROPS pProps, uint64_t cMs)
{
    return PDMAUDIOPCMPROPS_F2B(pProps, PDMAudioPropsMilliToFrames(pProps, cMs));
}

/**
 * Converts nanoseconds to frames.
 *
 * @returns Number of frames.
 * @param   pProps      The PCM properties to use.
 * @param   cNs         The number of nanoseconds to convert.
 *
 * @note    The result is rounded rather than floored (hysterical raisins).
 */
DECLINLINE(uint32_t) PDMAudioPropsNanoToFrames(PCPDMAUDIOPCMPROPS pProps, uint64_t cNs)
{
    AssertPtrReturn(pProps, 0);

    uint32_t const uHz = pProps->uHz;
    uint32_t cFrames;
    if (cNs < RT_NS_1SEC)
        cFrames = 0;
    else
    {
        cFrames = cNs / RT_NS_1SEC * uHz;
        cNs %= RT_NS_1SEC;
    }
    cFrames += (ASMMult2xU32RetU64(uHz, (uint32_t)cNs) + RT_NS_1SEC - 1) / RT_NS_1SEC;
    return cFrames;
}

/**
 * Converts nanoseconds to frames, 64-bit return.
 *
 * @returns Number of frames (64-bit).
 * @param   pProps      The PCM properties to use.
 * @param   cNs         The number of nanoseconds to convert.
 *
 * @note    The result is floored!
 */
DECLINLINE(uint64_t) PDMAudioPropsNanoToFrames64(PCPDMAUDIOPCMPROPS pProps, uint64_t cNs)
{
    AssertPtrReturn(pProps, 0);

    uint32_t const uHz = pProps->uHz;
    uint64_t cFrames;
    if (cNs < RT_NS_1SEC)
        cFrames = 0;
    else
    {
        cFrames = cNs / RT_NS_1SEC * uHz;
        cNs %= RT_NS_1SEC;
    }
    cFrames += ASMMult2xU32RetU64(uHz, (uint32_t)cNs) / RT_NS_1SEC;
    return cFrames;
}

/**
 * Converts nanoseconds to bytes.
 *
 * @returns Number of bytes (frame aligned).
 * @param   pProps      The PCM properties to use.
 * @param   cNs         The number of nanoseconds to convert.
 *
 * @note    The result is rounded rather than floored (hysterical raisins).
 */
DECLINLINE(uint32_t) PDMAudioPropsNanoToBytes(PCPDMAUDIOPCMPROPS pProps, uint64_t cNs)
{
    return PDMAUDIOPCMPROPS_F2B(pProps, PDMAudioPropsNanoToFrames(pProps, cNs));
}

/**
 * Converts nanoseconds to bytes, 64-bit version.
 *
 * @returns Number of bytes (frame aligned), 64-bit.
 * @param   pProps      The PCM properties to use.
 * @param   cNs         The number of nanoseconds to convert.
 *
 * @note    The result is floored.
 */
DECLINLINE(uint64_t) PDMAudioPropsNanoToBytes64(PCPDMAUDIOPCMPROPS pProps, uint64_t cNs)
{
    return PDMAUDIOPCMPROPS_F2B(pProps, PDMAudioPropsNanoToFrames(pProps, cNs));
}

/**
 * Clears a sample buffer by the given amount of audio frames with silence (according to the format
 * given by the PCM properties).
 *
 * @param   pProps      The PCM properties to apply.
 * @param   pvBuf       The buffer to clear.
 * @param   cbBuf       The buffer size in bytes.
 * @param   cFrames     The number of audio frames to clear.  Capped at @a cbBuf
 *                      if exceeding the buffer.  If the size is an unaligned
 *                      number of frames, the extra bytes may be left
 *                      uninitialized in some configurations.
 */
DECLINLINE(void) PDMAudioPropsClearBuffer(PCPDMAUDIOPCMPROPS pProps, void *pvBuf, size_t cbBuf, uint32_t cFrames)
{
    /*
     * Validate input
     */
    AssertPtrReturnVoid(pProps);
    Assert(pProps->cbSampleX);
    if (!cbBuf || !cFrames)
        return;
    AssertPtrReturnVoid(pvBuf);

    /*
     * Decide how much needs clearing.
     */
    size_t cbToClear = PDMAudioPropsFramesToBytes(pProps, cFrames);
    AssertStmt(cbToClear <= cbBuf, cbToClear = cbBuf);

    Log2Func(("pProps=%p, pvBuf=%p, cFrames=%RU32, fSigned=%RTbool, cbSample=%RU8\n",
              pProps, pvBuf, cFrames, pProps->fSigned, pProps->cbSampleX));

    /*
     * Do the job.
     */
    if (pProps->fSigned)
        RT_BZERO(pvBuf, cbToClear);
    else /* Unsigned formats. */
    {
        switch (pProps->cbSampleX)
        {
            case 1: /* 8 bit */
                memset(pvBuf, 0x80, cbToClear);
                break;

            case 2: /* 16 bit */
            {
                uint16_t       *pu16Dst   = (uint16_t *)pvBuf;
                uint16_t const  u16Offset = !pProps->fSwapEndian ? UINT16_C(0x8000) : UINT16_C(0x80);
                cbBuf /= sizeof(*pu16Dst);
                while (cbBuf-- > 0)
                    *pu16Dst++ = u16Offset;
                break;
            }

            case 4: /* 32 bit */
                ASMMemFill32(pvBuf, cbToClear & ~(size_t)(sizeof(uint32_t) - 1),
                             !pProps->fSwapEndian ? UINT32_C(0x80000000) : UINT32_C(0x80));
                break;

            default:
                AssertMsgFailed(("Invalid bytes per sample: %RU8\n", pProps->cbSampleX));
        }
    }
}

/**
 * Checks if the given buffer is silence.
 *
 * @param   pProps      The PCM properties to use checking the buffer.
 * @param   pvBuf       The buffer to check.
 * @param   cbBuf       The number of bytes to check (must be frame aligned).
 */
DECLINLINE(bool) PDMAudioPropsIsBufferSilence(PCPDMAUDIOPCMPROPS pProps, void const *pvBuf, size_t cbBuf)
{
    /*
     * Validate input
     */
    AssertPtrReturn(pProps, false);
    if (!cbBuf)
        return false;
    AssertPtrReturn(pvBuf, false);

    /*
     * Do the job.
     */
    if (pProps->fSigned)
        return ASMMemIsZero(pvBuf, cbBuf);

    switch (pProps->cbSampleX)
    {
        case 1: /* 8 bit */
            return ASMMemIsAllU8(pvBuf, cbBuf, 0x80);

        case 2: /* 16 bit */
        {
            uint16_t const *pu16      = (uint16_t const *)pvBuf;
            uint16_t const  u16Offset = !pProps->fSwapEndian ? UINT16_C(0x8000) : UINT16_C(0x80);
            cbBuf /= sizeof(*pu16);
            while (cbBuf-- > 0)
                if (*pu16 != u16Offset)
                    return false;
            return true;
        }

        case 4: /* 32 bit */
        {
            uint32_t const *pu32      = (uint32_t const *)pvBuf;
            uint32_t const  u32Offset = !pProps->fSwapEndian ? UINT32_C(0x80000000) : UINT32_C(0x80);
            cbBuf /= sizeof(*pu32);
            while (cbBuf-- > 0)
                if (*pu32 != u32Offset)
                    return false;
            return true;
        }

        default:
            AssertMsgFailed(("Invalid bytes per sample: %RU8\n", pProps->cbSampleX));
            return false;
    }
}

/**
 * Compares two sets of PCM properties.
 *
 * @returns @c true if the same, @c false if not.
 * @param   pProps1     The first set of properties to compare.
 * @param   pProps2     The second set of properties to compare.
 */
DECLINLINE(bool) PDMAudioPropsAreEqual(PCPDMAUDIOPCMPROPS pProps1, PCPDMAUDIOPCMPROPS pProps2)
{
    uintptr_t idxCh;
    AssertPtrReturn(pProps1, false);
    AssertPtrReturn(pProps2, false);

    if (pProps1 == pProps2) /* If the pointers match, take a shortcut. */
        return true;

    if (pProps1->uHz != pProps2->uHz)
        return false;
    if (pProps1->cChannelsX  != pProps2->cChannelsX)
        return false;
    if (pProps1->cbSampleX   != pProps2->cbSampleX)
        return false;
    if (pProps1->fSigned     != pProps2->fSigned)
        return false;
    if (pProps1->fSwapEndian != pProps2->fSwapEndian)
        return false;
    if (pProps1->fRaw        != pProps2->fRaw)
        return false;

    idxCh = pProps1->cChannelsX;
    while (idxCh-- > 0)
        if (pProps1->aidChannels[idxCh] != pProps2->aidChannels[idxCh])
            return false;

    return true;
}

/**
 * Checks whether the given PCM properties are valid or not.
 *
 * @returns true/false accordingly.
 * @param   pProps  The PCM properties to check.
 *
 * @remarks This just performs a generic check of value ranges.
 *
 * @sa      PDMAudioStrmCfgIsValid
 */
DECLINLINE(bool) PDMAudioPropsAreValid(PCPDMAUDIOPCMPROPS pProps)
{
    AssertPtrReturn(pProps, false);

        /* Channels. */
    if (   pProps->cChannelsX != 0
        && pProps->cChannelsX <= PDMAUDIO_MAX_CHANNELS
        /* Sample size. */
        && (   pProps->cbSampleX == 1
            || pProps->cbSampleX == 2
            || pProps->cbSampleX == 4
            || (pProps->cbSampleX == 8 && pProps->fRaw))
        /* Hertz rate. */
        && pProps->uHz >= 1000
        && pProps->uHz < 1000000
        /* Raw format: Here we only support int64_t as sample size currently, if enabled. */
        && (   !pProps->fRaw
            || (pProps->fSigned && pProps->cbSampleX == sizeof(int64_t)))
       )
    {
        /* A few more sanity checks to see if the structure has been properly initialized (via PDMAudioPropsInit[Ex]). */
        AssertMsgReturn(pProps->cShiftX == PDMAUDIOPCMPROPS_MAKE_SHIFT(pProps),
                        ("cShift=%u cbSample=%u cChannels=%u\n", pProps->cShiftX, pProps->cbSampleX, pProps->cChannelsX),
                        false);
        AssertMsgReturn(pProps->cbFrame == pProps->cbSampleX * pProps->cChannelsX,
                        ("cbFrame=%u cbSample=%u cChannels=%u\n", pProps->cbFrame, pProps->cbSampleX, pProps->cChannelsX),
                        false);

        return true;
    }

    return false;
}

/**
 * Get number of bytes per frame.
 *
 * @returns Number of bytes per audio frame.
 * @param   pProps  PCM properties to use.
 * @sa      PDMAUDIOPCMPROPS_F2B
 */
DECLINLINE(uint32_t) PDMAudioPropsBytesPerFrame(PCPDMAUDIOPCMPROPS pProps)
{
    return PDMAUDIOPCMPROPS_F2B(pProps, 1 /*cFrames*/);
}

/**
 * Prints PCM properties to the debug log.
 *
 * @param   pProps  PCM properties to use.
 */
DECLINLINE(void) PDMAudioPropsLog(PCPDMAUDIOPCMPROPS pProps)
{
    AssertPtrReturnVoid(pProps);

    Log(("uHz=%RU32, cChannels=%RU8, cBits=%RU8%s",
         pProps->uHz, pProps->cChannelsX, pProps->cbSampleX * 8, pProps->fSigned ? "S" : "U"));
}

/** Max necessary buffer space for  PDMAudioPropsToString  */
#define PDMAUDIOPROPSTOSTRING_MAX   sizeof("16ch S64 4294967296Hz swap raw")

/**
 * Formats the PCM audio properties into a string buffer.
 *
 * @returns pszDst
 * @param   pProps  PCM properties to use.
 * @param   pszDst  The destination buffer.
 * @param   cchDst  The size of the destination buffer.  Recommended to be at
 *                  least PDMAUDIOPROPSTOSTRING_MAX bytes.
 */
DECLINLINE(char *) PDMAudioPropsToString(PCPDMAUDIOPCMPROPS pProps, char *pszDst, size_t cchDst)
{
    /* 2ch S64 44100Hz swap raw */
    RTStrPrintf(pszDst, cchDst, "%uch %c%u %RU32Hz%s%s",
                PDMAudioPropsChannels(pProps), PDMAudioPropsIsSigned(pProps) ? 'S' : 'U', PDMAudioPropsSampleBits(pProps),
                PDMAudioPropsHz(pProps), pProps->fSwapEndian ? " swap" : "", pProps->fRaw ? " raw" : "");
    return pszDst;
}


/*********************************************************************************************************************************
*   Stream Configuration Helpers                                                                                                 *
*********************************************************************************************************************************/

/**
 * Initializes a stream configuration from PCM properties.
 *
 * @returns VBox status code.
 * @param   pCfg        The stream configuration to initialize.
 * @param   pProps      The PCM properties to use.
 */
DECLINLINE(int) PDMAudioStrmCfgInitWithProps(PPDMAUDIOSTREAMCFG pCfg, PCPDMAUDIOPCMPROPS pProps)
{
    AssertPtrReturn(pProps, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfg,   VERR_INVALID_POINTER);

    RT_ZERO(*pCfg);
    pCfg->Backend.cFramesPreBuffering = UINT32_MAX; /* Explicitly set to "undefined". */

    memcpy(&pCfg->Props, pProps, sizeof(PDMAUDIOPCMPROPS));

    return VINF_SUCCESS;
}

/**
 * Checks whether stream configuration matches the given PCM properties.
 *
 * @returns @c true if equal, @c false if not.
 * @param   pCfg    The stream configuration.
 * @param   pProps  The PCM properties to match with.
 */
DECLINLINE(bool) PDMAudioStrmCfgMatchesProps(PCPDMAUDIOSTREAMCFG pCfg, PCPDMAUDIOPCMPROPS pProps)
{
    AssertPtrReturn(pCfg, false);
    return PDMAudioPropsAreEqual(pProps, &pCfg->Props);
}

/**
 * Checks whether two stream configuration matches.
 *
 * @returns @c true if equal, @c false if not.
 * @param   pCfg1   The first stream configuration.
 * @param   pCfg2   The second stream configuration.
 */
DECLINLINE(bool) PDMAudioStrmCfgEquals(PCPDMAUDIOSTREAMCFG pCfg1, PCPDMAUDIOSTREAMCFG pCfg2)
{
    if (!pCfg1 || !pCfg2)
        return false;
    if (pCfg1 == pCfg2)
        return pCfg1 != NULL;
    if (PDMAudioPropsAreEqual(&pCfg1->Props, &pCfg2->Props))
        return pCfg1->enmDir    == pCfg2->enmDir
            && pCfg1->enmPath   == pCfg2->enmPath
            && pCfg1->Device.cMsSchedulingHint == pCfg2->Device.cMsSchedulingHint
            && pCfg1->Backend.cFramesPeriod == pCfg2->Backend.cFramesPeriod
            && pCfg1->Backend.cFramesBufferSize == pCfg2->Backend.cFramesBufferSize
            && pCfg1->Backend.cFramesPreBuffering == pCfg2->Backend.cFramesPreBuffering
            && strcmp(pCfg1->szName, pCfg2->szName) == 0;
    return false;
}

/**
 * Frees an audio stream allocated by PDMAudioStrmCfgDup().
 *
 * @param   pCfg    The stream configuration to free.
 */
DECLINLINE(void) PDMAudioStrmCfgFree(PPDMAUDIOSTREAMCFG pCfg)
{
    if (pCfg)
        RTMemFree(pCfg);
}

/**
 * Checks whether the given stream configuration is valid or not.
 *
 * @returns true/false accordingly.
 * @param   pCfg    Stream configuration to check.
 *
 * @remarks This just performs a generic check of value ranges.  Further, it
 *          will assert if the input is invalid.
 *
 * @sa      PDMAudioPropsAreValid
 */
DECLINLINE(bool) PDMAudioStrmCfgIsValid(PCPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pCfg, false);
    AssertMsgReturn(pCfg->enmDir >= PDMAUDIODIR_UNKNOWN && pCfg->enmDir < PDMAUDIODIR_END, ("%d\n", pCfg->enmDir), false);
    return PDMAudioPropsAreValid(&pCfg->Props);
}

/**
 * Copies one stream configuration to another.
 *
 * @returns VBox status code.
 * @param   pDstCfg     The destination stream configuration.
 * @param   pSrcCfg     The source stream configuration.
 */
DECLINLINE(int) PDMAudioStrmCfgCopy(PPDMAUDIOSTREAMCFG pDstCfg, PCPDMAUDIOSTREAMCFG pSrcCfg)
{
    AssertPtrReturn(pDstCfg, VERR_INVALID_POINTER);
    AssertPtrReturn(pSrcCfg, VERR_INVALID_POINTER);

    /* This used to be VBOX_STRICT only and return VERR_INVALID_PARAMETER, but
       that's making release builds work differently from debug & strict builds,
       which is a terrible idea: */
    Assert(PDMAudioStrmCfgIsValid(pSrcCfg));

    memcpy(pDstCfg, pSrcCfg, sizeof(PDMAUDIOSTREAMCFG));

    return VINF_SUCCESS;
}

/**
 * Duplicates an audio stream configuration.
 *
 * @returns Pointer to duplicate on success, NULL on failure.  Must be freed
 *          using PDMAudioStrmCfgFree().
 *
 * @param   pCfg        The audio stream configuration to duplicate.
 */
DECLINLINE(PPDMAUDIOSTREAMCFG) PDMAudioStrmCfgDup(PCPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pCfg, NULL);

    PPDMAUDIOSTREAMCFG pDst = (PPDMAUDIOSTREAMCFG)RTMemAllocZ(sizeof(PDMAUDIOSTREAMCFG));
    if (pDst)
    {
        int rc = PDMAudioStrmCfgCopy(pDst, pCfg);
        if (RT_SUCCESS(rc))
            return pDst;

        PDMAudioStrmCfgFree(pDst);
    }
    return NULL;
}

/**
 * Logs an audio stream configuration.
 *
 * @param   pCfg        The stream configuration to log.
 */
DECLINLINE(void) PDMAudioStrmCfgLog(PCPDMAUDIOSTREAMCFG pCfg)
{
    if (pCfg)
        LogFunc(("szName=%s enmDir=%RU32 uHz=%RU32 cBits=%RU8%s cChannels=%RU8\n", pCfg->szName, pCfg->enmDir,
                 pCfg->Props.uHz, pCfg->Props.cbSampleX * 8, pCfg->Props.fSigned ? "S" : "U", pCfg->Props.cChannelsX));
}

/**
 * Converts a stream command enum value to a string.
 *
 * @returns Pointer to read-only stream command name on success,
 *          "bad" if invalid command value.
 * @param   enmCmd      The stream command to name.
 */
DECLINLINE(const char *) PDMAudioStrmCmdGetName(PDMAUDIOSTREAMCMD enmCmd)
{
    switch (enmCmd)
    {
        case PDMAUDIOSTREAMCMD_INVALID: return "Invalid";
        case PDMAUDIOSTREAMCMD_ENABLE:  return "Enable";
        case PDMAUDIOSTREAMCMD_DISABLE: return "Disable";
        case PDMAUDIOSTREAMCMD_PAUSE:   return "Pause";
        case PDMAUDIOSTREAMCMD_RESUME:  return "Resume";
        case PDMAUDIOSTREAMCMD_DRAIN:   return "Drain";
        case PDMAUDIOSTREAMCMD_END:
        case PDMAUDIOSTREAMCMD_32BIT_HACK:
            break;
        /* no default! */
    }
    AssertMsgFailedReturn(("Invalid stream command %d\n", enmCmd), "bad");
}

/** Max necessary buffer space for PDMAudioStrmCfgToString  */
#define PDMAUDIOSTRMCFGTOSTRING_MAX \
    sizeof("'01234567890123456789012345678901234567890123456789012345678901234' unknown 16ch S64 4294967295Hz swap raw, 9999999ms buffer, 9999999ms period, 9999999ms pre-buffer, 4294967295ms sched, center-lfe")

/**
 * Formats an audio stream configuration.
 *
 * @param   pCfg        The stream configuration to stringify.
 * @param   pszDst      The destination buffer.
 * @param   cbDst       The size of the destination buffer.  Recommend this be
 *                      at least PDMAUDIOSTRMCFGTOSTRING_MAX bytes.
 */
DECLINLINE(const char *) PDMAudioStrmCfgToString(PCPDMAUDIOSTREAMCFG pCfg, char *pszDst, size_t cbDst)
{
    /* 'front' output 2ch 44100Hz raw, 300ms buffer, 75ms period, 150ms pre-buffer, 10ms sched */
    RTStrPrintf(pszDst, cbDst,
                "'%s' %s %uch %c%u %RU32Hz%s%s, %RU32ms buffer, %RU32ms period, %RU32ms pre-buffer, %RU32ms sched%s%s",
                pCfg->szName, PDMAudioDirGetName(pCfg->enmDir), PDMAudioPropsChannels(&pCfg->Props),
                PDMAudioPropsIsSigned(&pCfg->Props) ? 'S' : 'U', PDMAudioPropsSampleBits(&pCfg->Props),
                PDMAudioPropsHz(&pCfg->Props), pCfg->Props.fSwapEndian ? " swap" : "", pCfg->Props.fRaw ? " raw" : "",
                PDMAudioPropsFramesToMilliMax(&pCfg->Props, pCfg->Backend.cFramesBufferSize, 9999999),
                PDMAudioPropsFramesToMilliMax(&pCfg->Props, pCfg->Backend.cFramesPeriod, 9999999),
                PDMAudioPropsFramesToMilliMax(&pCfg->Props, pCfg->Backend.cFramesPreBuffering, 9999999),
                pCfg->Device.cMsSchedulingHint,
                pCfg->enmPath == PDMAUDIOPATH_UNKNOWN ? "" : ", ",
                pCfg->enmPath == PDMAUDIOPATH_UNKNOWN ? "" : PDMAudioPathGetName(pCfg->enmPath) );
    return pszDst;
}


/*********************************************************************************************************************************
*   Stream Status Helpers                                                                                                        *
*********************************************************************************************************************************/

/**
 * Converts a audio stream state enum value to a string.
 *
 * @returns Pointer to read-only audio stream state string on success,
 *          "illegal" if invalid command value.
 * @param   enmStreamState  The state to convert.
 */
DECLINLINE(const char *) PDMAudioStreamStateGetName(PDMAUDIOSTREAMSTATE enmStreamState)
{
    switch (enmStreamState)
    {
        case PDMAUDIOSTREAMSTATE_INVALID:           return "invalid";
        case PDMAUDIOSTREAMSTATE_NOT_WORKING:       return "not-working";
        case PDMAUDIOSTREAMSTATE_NEED_REINIT:       return "need-reinit";
        case PDMAUDIOSTREAMSTATE_INACTIVE:          return "inactive";
        case PDMAUDIOSTREAMSTATE_ENABLED:           return "enabled";
        case PDMAUDIOSTREAMSTATE_ENABLED_READABLE:  return "enabled-readable";
        case PDMAUDIOSTREAMSTATE_ENABLED_WRITABLE:  return "enabled-writable";
        /* no default: */
        case PDMAUDIOSTREAMSTATE_END:
        case PDMAUDIOSTREAMSTATE_32BIT_HACK:
            break;
    }
    AssertMsgFailedReturn(("Invalid audio stream state: %d\n", enmStreamState), "illegal");
}

/**
 * Converts a host audio (backend) stream state enum value to a string.
 *
 * @returns Pointer to read-only host audio stream state string on success,
 *          "illegal" if invalid command value.
 * @param   enmHostAudioStreamState The state to convert.
 */
DECLINLINE(const char *) PDMHostAudioStreamStateGetName(PDMHOSTAUDIOSTREAMSTATE enmHostAudioStreamState)
{
    switch (enmHostAudioStreamState)
    {
        case PDMHOSTAUDIOSTREAMSTATE_INVALID:       return "invalid";
        case PDMHOSTAUDIOSTREAMSTATE_INITIALIZING:  return "initializing";
        case PDMHOSTAUDIOSTREAMSTATE_NOT_WORKING:   return "not-working";
        case PDMHOSTAUDIOSTREAMSTATE_OKAY:          return "okay";
        case PDMHOSTAUDIOSTREAMSTATE_DRAINING:      return "draining";
        case PDMHOSTAUDIOSTREAMSTATE_INACTIVE:      return "inactive";
        /* no default: */
        case PDMHOSTAUDIOSTREAMSTATE_END:
        case PDMHOSTAUDIOSTREAMSTATE_32BIT_HACK:
            break;
    }
    AssertMsgFailedReturn(("Invalid host audio stream state: %d\n", enmHostAudioStreamState), "illegal");
}

/** @} */

#endif /* !VBOX_INCLUDED_vmm_pdmaudioinline_h */
