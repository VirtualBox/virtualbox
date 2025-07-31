/** @file
 * PDM - Pluggable Device Manager, Audio interfaces.
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

/** @page pg_pdm_audio  PDM Audio
 *
 * PDM provides audio device emulations and their driver chains with the
 * interfaces they need to communicate with each other.
 *
 *
 * @section sec_pdm_audio_overview  Overview
 *
@startuml
skinparam componentStyle rectangle

node VM {
    [Music Player App] --> [Guest Audio Driver]
    [Recording App]    <-- [Guest Audio Driver]
}

component "DevAudio (DevHda / DevIchAc97 / DevSB16)" as DevAudio {
    [Output DMA Engine]
    [Input DMA Engine]
    () LUN0
    () LUN1

    component "AudioMixer" {
        component "Output Sink" {
            () "Output Stream #0" as DrvStreamOut0
            () "Output Stream #1" as DrvStreamOut1
            [Output Mixer Buffer] --> DrvStreamOut0
            [Output Mixer Buffer] --> DrvStreamOut1
            [Output DMA Engine] --> [Output Mixer Buffer]
            DrvStreamOut0 --> LUN0
            DrvStreamOut1 --> LUN1
        }
        component "Input Sink" {
            () "Input Stream #2" as DrvStreamIn0
            () "Input Stream #3" as DrvStreamIn1
            [Input Mixer Buffer] <-- DrvStreamIn0
            [Input Mixer Buffer] <-- DrvStreamIn1
            [Input DMA Engine] --> [Input Mixer Buffer]
            DrvStreamIn0 <-- LUN0
            DrvStreamIn1 <-- LUN1
        }
    }
}
[Guest Audio Driver] <..> DevAudio : " MMIO or Port I/O, DMA"

node "Driver Chain #0" {
    component "DrvAudio#0" {
        () PDMIHOSTAUDIOPORT0
        () PDMIAUDIOCONNECTOR0
    }
    component "DrvHostAudioWasApi" {
        () PDMIHOSTAUDIO0
    }
}
PDMIHOSTAUDIOPORT0 <--> PDMIHOSTAUDIO0

node "Driver Chain #1" {
    component "DrvAudio#1" {
        () PDMIAUDIOCONNECTOR1
        () PDMIHOSTAUDIOPORT1
    }
    component "DrvAudioVRDE" {
        () PDMIHOSTAUDIO1
    }
}
note bottom of DrvAudioVRDE
    The backend driver is sometimes not configured if the component it represents
    is not configured for the VM.  However, Main will still set up the LUN but
    with just DrvAudio attached to simplify runtime activation of the component.
    In the meanwhile, the DrvAudio instance works as if DrvHostAudioNull were attached.
end note

LUN1 <--> PDMIAUDIOCONNECTOR1
LUN0 <--> PDMIAUDIOCONNECTOR0

PDMIHOSTAUDIOPORT1 <--> PDMIHOSTAUDIO1

@enduml
 *
 * Actors:
 *      - An audio device implementation: "DevAudio"
 *          - Mixer instance (AudioMixer.cpp) with one or more mixer
 *            sinks: "Output Sink",  "Input Sink"
 *          - One DMA engine teamed up with each mixer sink: "Output DMA
 *            Engine", "Input DMA Engine"
 *      - The audio driver "DrvAudio" instances attached to LUN0 and LUN1
 *        respectively: "DrvAudio#0", "DrvAudio#1"
 *      - The Windows host audio driver attached to "DrvAudio0": "DrvHostAudioWas"
 *      - The VRDE/VRDP host audio driver attached to "DrvAudio1": "DrvAudioVRDE"
 *
 * Both "Output Sink" and "Input Sink" talks to all the attached driver chains
 * ("DrvAudio #0" and "DrvAudio #1"), but using different PDMAUDIOSTREAM
 * instances.  There can be an arbritrary number of driver chains attached to an
 * audio device, the mixer sinks will multiplex output to each of them and blend
 * input from all of them, taking care of format and rate conversions.  The
 * mixer and mixer sinks does not fit into the PDM device/driver model, because
 * a driver can only have exactly one or zero other drivers attached, so it is
 * implemented as a separate component that all the audio devices share (see
 * AudioMixer.h, AudioMixer.cpp, AudioMixBuffer.h and AudioMixBuffer.cpp).
 *
 * The driver chains attached to LUN0, LUN1, ... LUNn typically have two
 * drivers attached, first DrvAudio and then a backend driver like
 * DrvHostAudioWasApi, DrvHostAudioPulseAudio, or DrvAudioVRDE.  DrvAudio
 * exposes PDMIAUDIOCONNECTOR upwards towards the device and mixer component,
 * and PDMIHOSTAUDIOPORT downwards towards DrvHostAudioWasApi and the other
 * backends.
 *
 * The backend exposes the PDMIHOSTAUDIO upwards towards DrvAudio. It is
 * possible, though, to only have the DrvAudio instance and not backend, in
 * which case DrvAudio works as if the NULL backend was attached.  Main does
 * such setups when the main component we're interfacing with isn't currently
 * active, as this simplifies runtime activation.
 *
 * The purpose of DrvAudio is to make the work of the backend as simple as
 * possible and try avoid needing to write the same code over and over again for
 * each backend.  It takes care of:
 *      - Stream creation, operation, re-initialization and destruction.
 *      - Pre-buffering.
 *      - Thread pool.
 *
 * The purpose of a host audio driver (aka backend) is to interface with the
 * host audio system (or other audio systems like VRDP and video recording).
 * The backend will optionally provide a list of host audio devices, switch
 * between them, and monitor changes to them.  By default our host backends use
 * the default host device and will trigger stream re-initialization if this
 * changes while we're using it.
 *
 *
 * @section sec_pdm_audio_device    Virtual Audio Device
 *
 * The virtual device translates the settings of the emulated device into mixing
 * sinks with sample format, sample rate, volume control, and whatnot.
 *
 * It also implements a DMA engine for transfering samples to (input) or from
 * (output) the guest memory. The starting and stopping of the DMA engines are
 * communicated to the associated mixing sinks and by then onto the
 * PDMAUDIOSTREAM instance for each driver chain.  A RTCIRCBUF is used as an
 * intermediary between the DMA engine and the asynchronous worker thread of the
 * mixing sink.
 *
 *
 * @section sec_pdm_audio_mixing    Audio Mixing
 *
 * The audio mixer is a mandatory component in an audio device.  It consists of
 * a mixer and one or more sinks with mixer buffers.  The sinks are typically
 * one per virtual output/input connector, so for instance you could have a
 * device with a "PCM Output" sink and a "PCM Input" sink.
 *
 * The audio mixer takes care of:
 *      - Much of the driver chain (LUN) management work.
 *      - Multiplexing output to each active driver chain.
 *      - Blending input from each active driver chain into a single audio
 *        stream.
 *      - Do format conversion (it uses signed 32-bit PCM internally) between
 *        the audio device and all of the LUNs (no common format needed).
 *      - Do sample rate conversions between the device rate and that of the
 *        individual driver chains.
 *      - Apply the volume settings of the device to the audio stream.
 *      - Provide the asynchronous thread that pushes data from the device's
 *        internal DMA buffer and all the way to the backend for output sinks,
 *        and vice versa for input.
 *
 * The term active LUNs above means that not all LUNs will actually produce
 * (input) or consume (output) audio.  The mixer checks the return of
 * PDMIHOSTAUDIO::pfnStreamGetState each time it's processing samples to see
 * which streams are currently active and which aren't.  Inactive streams are
 * ignored.
 *
 * For more info: @ref pg_audio_mixer, @ref pg_audio_mixing_buffers
 *
 * The AudioMixer API reference can be found here:
 *      - @ref grp_pdm_ifs_audio_mixing
 *      - @ref grp_pdm_ifs_audio_mixing_buffers
 *
 *
 * @section sec_pdm_audio_timing    Timing
 *
 * Handling audio data in a virtual environment is hard, as the human perception
 * is very sensitive to the slightest cracks and stutters in the audible data,
 * and the task of playing back and recording audio is in the real-time domain.
 *
 * The virtual machine is not executed with any real-time guarentees, only best
 * effort, mainly because it is subject to preemptive scheduling on the host
 * side.  The audio processing done on the guest side is typically also subject
 * to preemptive scheduling on the guest side and available CPU processing power
 * there.
 *
 * Thus, the guest may be lagging behind because the host prioritizes other
 * processes/threads over the virtual machine.  This will, if it's too servere,
 * cause the virtual machine to speed up it's time sense while it's trying to
 * catch up.  So, we can easily have a bit of a seesaw execution going on here,
 * where in the playback case, the guest produces data too slowly for while and
 * then switches to producing it too quickly for a while to catch up.
 *
 * Our working principle is that the backends and the guest are producing and
 * consuming samples at the same rate, but we have to deal with the uneven
 * execution.
 *
 * To deal with this we employ (by default) 300ms of backend buffer and
 * pre-buffer 150ms of that for both input and output audio streams.  This means
 * we have about 150ms worth of samples to feed to the host audio device should
 * the virtual machine be starving and lagging behind.  Likewise, we have about
 * 150ms of buffer space will can fill when the VM is in a catch-up mode.  Now,
 * 300ms and 150 ms isn't much for the purpose of glossing over
 * scheduling/timing differences here, but we can't do too much more or the lag
 * will grow rather annoying.  The pre-buffering is implemented by DrvAudio.
 *
 * In addition to the backend buffer that defaults to 300ms, we have the
 * internal DMA buffer of the device and the mixing buffer of the mixing sink.
 * The latter two are typically rather small, sized to fit the anticipated DMA
 * period currently in use by the guest.
 */

#ifndef VBOX_INCLUDED_vmm_pdmaudioifs_h
#define VBOX_INCLUDED_vmm_pdmaudioifs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assertcompile.h>
#include <iprt/critsect.h>
#include <iprt/circbuf.h>
#include <iprt/list.h>
#include <iprt/path.h>

#include <VBox/types.h>
#include <VBox/vmm/pdmcommon.h>
#include <VBox/vmm/stam.h>

RT_C_DECLS_BEGIN


/** @defgroup grp_pdm_ifs_audio     PDM Audio Interfaces
 * @ingroup grp_pdm_interfaces
 * @{
 */

/** The maximum number of channels PDM supports. */
#define PDMAUDIO_MAX_CHANNELS   12

/**
 * Audio direction.
 */
typedef enum PDMAUDIODIR
{
    /** Invalid zero value as per usual (guards against using unintialized values). */
    PDMAUDIODIR_INVALID = 0,
    /** Unknown direction. */
    PDMAUDIODIR_UNKNOWN,
    /** Input. */
    PDMAUDIODIR_IN,
    /** Output. */
    PDMAUDIODIR_OUT,
    /** Duplex handling. */
    PDMAUDIODIR_DUPLEX,
    /** End of valid values. */
    PDMAUDIODIR_END,
    /** Hack to blow the type up to 32-bit. */
    PDMAUDIODIR_32BIT_HACK = 0x7fffffff
} PDMAUDIODIR;


/** @name PDMAUDIOHOSTDEV_F_XXX
 * @{  */
/** No flags set. */
#define PDMAUDIOHOSTDEV_F_NONE              UINT32_C(0)
/** The default input (capture/recording) device (for the user). */
#define PDMAUDIOHOSTDEV_F_DEFAULT_IN        RT_BIT_32(0)
/** The default output (playback) device (for the user). */
#define PDMAUDIOHOSTDEV_F_DEFAULT_OUT       RT_BIT_32(1)
/** The device can be removed at any time and we have to deal with it. */
#define PDMAUDIOHOSTDEV_F_HOTPLUG           RT_BIT_32(2)
/** The device is known to be buggy and needs special treatment. */
#define PDMAUDIOHOSTDEV_F_BUGGY             RT_BIT_32(3)
/** Ignore the device, no matter what. */
#define PDMAUDIOHOSTDEV_F_IGNORE            RT_BIT_32(4)
/** The device is present but marked as locked by some other application. */
#define PDMAUDIOHOSTDEV_F_LOCKED            RT_BIT_32(5)
/** The device is present but not in an alive state (dead). */
#define PDMAUDIOHOSTDEV_F_DEAD              RT_BIT_32(6)
/** Set if the PDMAUDIOHOSTDEV::pszName is allocated. */
#define PDMAUDIOHOSTDEV_F_NAME_ALLOC        RT_BIT_32(29)
/** Set if the PDMAUDIOHOSTDEV::pszId is allocated. */
#define PDMAUDIOHOSTDEV_F_ID_ALLOC          RT_BIT_32(30)
/** Set if the extra backend specific data cannot be duplicated. */
#define PDMAUDIOHOSTDEV_F_NO_DUP            RT_BIT_32(31)
/** @} */

/**
 * Audio device type.
 */
typedef enum PDMAUDIODEVICETYPE
{
    /** Invalid zero value as per usual (guards against using unintialized values). */
    PDMAUDIODEVICETYPE_INVALID = 0,
    /** Unknown device type. This is the default. */
    PDMAUDIODEVICETYPE_UNKNOWN,
    /** Dummy device; for backends which are not able to report
     *  actual device information (yet). */
    PDMAUDIODEVICETYPE_DUMMY,
    /** The device is built into the host (non-removable). */
    PDMAUDIODEVICETYPE_BUILTIN,
    /** The device is an (external) USB device. */
    PDMAUDIODEVICETYPE_USB,
    /** End of valid values. */
    PDMAUDIODEVICETYPE_END,
    /** Hack to blow the type up to 32-bit. */
    PDMAUDIODEVICETYPE_32BIT_HACK = 0x7fffffff
} PDMAUDIODEVICETYPE;

/**
 * Host audio device info, part of enumeration result.
 *
 * @sa PDMAUDIOHOSTENUM, PDMIHOSTAUDIO::pfnGetDevices
 */
typedef struct PDMAUDIOHOSTDEV
{
    /** List entry (like PDMAUDIOHOSTENUM::LstDevices). */
    RTLISTNODE          ListEntry;
    /** Magic value (PDMAUDIOHOSTDEV_MAGIC). */
    uint32_t            uMagic;
    /** Size of this structure and whatever backend specific data that follows it. */
    uint32_t            cbSelf;
    /** The device type. */
    PDMAUDIODEVICETYPE  enmType;
    /** Usage of the device. */
    PDMAUDIODIR         enmUsage;
    /** Device flags, PDMAUDIOHOSTDEV_F_XXX. */
    uint32_t            fFlags;
    /** Maximum number of input audio channels the device supports. */
    uint8_t             cMaxInputChannels;
    /** Maximum number of output audio channels the device supports. */
    uint8_t             cMaxOutputChannels;
    uint8_t             abAlignment[ARCH_BITS == 32 ? 2 + 8 : 2 + 8];
    /** Backend specific device identifier, can be NULL, used to select device.
     * This can either point into some non-public part of this structure or to a
     * RTStrAlloc allocation.  PDMAUDIOHOSTDEV_F_ID_ALLOC is set in the latter
     * case.
     * @sa PDMIHOSTAUDIO::pfnSetDevice */
    char               *pszId;
    /** The friendly device name. */
    char               *pszName;
} PDMAUDIOHOSTDEV;
AssertCompileSizeAlignment(PDMAUDIOHOSTDEV, 16);
/** Pointer to audio device info (enumeration result). */
typedef PDMAUDIOHOSTDEV *PPDMAUDIOHOSTDEV;
/** Pointer to a const audio device info (enumeration result). */
typedef PDMAUDIOHOSTDEV const *PCPDMAUDIOHOSTDEV;

/** Magic value for PDMAUDIOHOSTDEV.  */
#define PDMAUDIOHOSTDEV_MAGIC       PDM_VERSION_MAKE(0xa0d0, 3, 0)


/**
 * A host audio device enumeration result.
 *
 * @sa PDMIHOSTAUDIO::pfnGetDevices
 */
typedef struct PDMAUDIOHOSTENUM
{
    /** Magic value (PDMAUDIOHOSTENUM_MAGIC). */
    uint32_t        uMagic;
    /** Number of audio devices in the list. */
    uint32_t        cDevices;
    /** List of audio devices (PDMAUDIOHOSTDEV). */
    RTLISTANCHOR    LstDevices;
} PDMAUDIOHOSTENUM;
/** Pointer to an audio device enumeration result. */
typedef PDMAUDIOHOSTENUM *PPDMAUDIOHOSTENUM;
/** Pointer to a const audio device enumeration result. */
typedef PDMAUDIOHOSTENUM const *PCPDMAUDIOHOSTENUM;

/** Magic for the host audio device enumeration. */
#define PDMAUDIOHOSTENUM_MAGIC      PDM_VERSION_MAKE(0xa0d1, 1, 0)


/**
 * Audio configuration (static) of an audio host backend.
 */
typedef struct PDMAUDIOBACKENDCFG
{
    /** The backend's friendly name. */
    char            szName[32];
    /** The size of the backend specific stream data (in bytes). */
    uint32_t        cbStream;
    /** PDMAUDIOBACKEND_F_XXX. */
    uint32_t        fFlags;
    /** Number of concurrent output (playback) streams supported on the host.
     *  UINT32_MAX for unlimited concurrent streams, 0 if no concurrent input streams are supported. */
    uint32_t        cMaxStreamsOut;
    /** Number of concurrent input (recording) streams supported on the host.
     *  UINT32_MAX for unlimited concurrent streams, 0 if no concurrent input streams are supported. */
    uint32_t        cMaxStreamsIn;
} PDMAUDIOBACKENDCFG;
/** Pointer to a static host audio audio configuration. */
typedef PDMAUDIOBACKENDCFG *PPDMAUDIOBACKENDCFG;

/** @name PDMAUDIOBACKEND_F_XXX - PDMAUDIOBACKENDCFG::fFlags
 * @{ */
/** PDMIHOSTAUDIO::pfnStreamConfigHint should preferably be called on a
 *  worker thread rather than EMT as it may take a good while. */
#define PDMAUDIOBACKEND_F_ASYNC_HINT            RT_BIT_32(0)
/** PDMIHOSTAUDIO::pfnStreamDestroy and any preceding
 *  PDMIHOSTAUDIO::pfnStreamControl/DISABLE should be preferably be called on a
 *  worker thread rather than EMT as it may take a good while. */
#define PDMAUDIOBACKEND_F_ASYNC_STREAM_DESTROY  RT_BIT_32(1)
/** @} */


/**
 * Audio path: input sources and playback destinations.
 *
 * Think of this as the name of the socket you plug the virtual audio stream
 * jack into.
 *
 * @note Not quite sure what the purpose of this type is.  It used to be two
 * separate enums (PDMAUDIOPLAYBACKDST & PDMAUDIORECSRC) without overlapping
 * values and most commonly used in a union (PDMAUDIODSTSRCUNION).  The output
 * values were designated "channel" (e.g. "Front channel"), whereas this was not
 * done to the input ones.  So, I'm (bird) a little confused what the actual
 * meaning was.
 */
typedef enum PDMAUDIOPATH
{
    /** Customary invalid zero value. */
    PDMAUDIOPATH_INVALID = 0,

    /** Unknown path / Doesn't care. */
    PDMAUDIOPATH_UNKNOWN,

    /** First output value. */
    PDMAUDIOPATH_OUT_FIRST,
    /** Output: Front. */
    PDMAUDIOPATH_OUT_FRONT = PDMAUDIOPATH_OUT_FIRST,
    /** Output: Center / LFE (Subwoofer). */
    PDMAUDIOPATH_OUT_CENTER_LFE,
    /** Output: Rear. */
    PDMAUDIOPATH_OUT_REAR,
    /** Last output value (inclusive)   */
    PDMAUDIOPATH_OUT_END = PDMAUDIOPATH_OUT_REAR,

    /** First input value. */
    PDMAUDIOPATH_IN_FIRST,
    /** Input: Microphone. */
    PDMAUDIOPATH_IN_MIC = PDMAUDIOPATH_IN_FIRST,
    /** Input: CD. */
    PDMAUDIOPATH_IN_CD,
    /** Input: Video-In. */
    PDMAUDIOPATH_IN_VIDEO,
    /** Input: AUX. */
    PDMAUDIOPATH_IN_AUX,
    /** Input: Line-In. */
    PDMAUDIOPATH_IN_LINE,
    /** Input: Phone-In. */
    PDMAUDIOPATH_IN_PHONE,
    /** Last intput value (inclusive). */
    PDMAUDIOPATH_IN_LAST = PDMAUDIOPATH_IN_PHONE,

    /** End of valid values. */
    PDMAUDIOPATH_END,
    /** Hack to blow the typ up to 32 bits. */
    PDMAUDIOPATH_32BIT_HACK = 0x7fffffff
} PDMAUDIOPATH;


/**
 * Standard speaker channel IDs.
 */
typedef enum PDMAUDIOCHANNELID
{
    /** Invalid zero value as per usual (guards against using unintialized values). */
    PDMAUDIOCHANNELID_INVALID = 0,

    /** Unused channel - fill with zero when encoding, ignore when decoding. */
    PDMAUDIOCHANNELID_UNUSED_ZERO,
    /** Unused channel - fill with silence when encoding, ignore when decoding. */
    PDMAUDIOCHANNELID_UNUSED_SILENCE,

    /** Unknown channel ID (unable to map to PDM terms). */
    PDMAUDIOCHANNELID_UNKNOWN,

    /** The first ID in the standard WAV-file assignment block. */
    PDMAUDIOCHANNELID_FIRST_STANDARD,
    /** Front left channel (FR). */
    PDMAUDIOCHANNELID_FRONT_LEFT = PDMAUDIOCHANNELID_FIRST_STANDARD,
    /** Front right channel (FR). */
    PDMAUDIOCHANNELID_FRONT_RIGHT,
    /** Front center channel (FC). */
    PDMAUDIOCHANNELID_FRONT_CENTER,
    /** Mono channel (alias for front center). */
    PDMAUDIOCHANNELID_MONO = PDMAUDIOCHANNELID_FRONT_CENTER,
    /** Low frequency effects (subwoofer) channel. */
    PDMAUDIOCHANNELID_LFE,
    /** Rear left channel (BL). */
    PDMAUDIOCHANNELID_REAR_LEFT,
    /** Rear right channel (BR). */
    PDMAUDIOCHANNELID_REAR_RIGHT,
    /** Front left of center channel (FLC). */
    PDMAUDIOCHANNELID_FRONT_LEFT_OF_CENTER,
    /** Front right of center channel (FLR). */
    PDMAUDIOCHANNELID_FRONT_RIGHT_OF_CENTER,
    /** Rear center channel (BC). */
    PDMAUDIOCHANNELID_REAR_CENTER,
    /** Side left channel (SL). */
    PDMAUDIOCHANNELID_SIDE_LEFT,
    /** Side right channel (SR). */
    PDMAUDIOCHANNELID_SIDE_RIGHT,
    /** Top center (TC). */
    PDMAUDIOCHANNELID_TOP_CENTER,
    /** Front left height channel (TFL). */
    PDMAUDIOCHANNELID_FRONT_LEFT_HEIGHT,
    /** Front center height channel (TFC). */
    PDMAUDIOCHANNELID_FRONT_CENTER_HEIGHT,
    /** Front right height channel (TFR). */
    PDMAUDIOCHANNELID_FRONT_RIGHT_HEIGHT,
    /** Rear left height channel (TBL). */
    PDMAUDIOCHANNELID_REAR_LEFT_HEIGHT,
    /** Rear center height channel (TBC). */
    PDMAUDIOCHANNELID_REAR_CENTER_HEIGHT,
    /** Rear right height channel (TBR). */
    PDMAUDIOCHANNELID_REAR_RIGHT_HEIGHT,
    /** The end of the standard WAV-file assignment block. */
    PDMAUDIOCHANNELID_END_STANDARD,

    /** End of valid values. */
    PDMAUDIOCHANNELID_END = PDMAUDIOCHANNELID_END_STANDARD,
    /** Hack to blow the type up to 32-bit. */
    PDMAUDIOCHANNELID_32BIT_HACK = 0x7fffffff
} PDMAUDIOCHANNELID;
AssertCompile(PDMAUDIOCHANNELID_FRONT_LEFT - PDMAUDIOCHANNELID_FIRST_STANDARD == 0);
AssertCompile(PDMAUDIOCHANNELID_LFE - PDMAUDIOCHANNELID_FIRST_STANDARD == 3);
AssertCompile(PDMAUDIOCHANNELID_REAR_CENTER - PDMAUDIOCHANNELID_FIRST_STANDARD == 8);
AssertCompile(PDMAUDIOCHANNELID_REAR_RIGHT_HEIGHT - PDMAUDIOCHANNELID_FIRST_STANDARD == 17);


/**
 * Properties of audio streams for host/guest for in or out directions.
 */
typedef struct PDMAUDIOPCMPROPS
{
    /** The frame size. */
    uint8_t     cbFrame;
    /** Shift count used with PDMAUDIOPCMPROPS_F2B and PDMAUDIOPCMPROPS_B2F.
     * Depends on number of stream channels and the stream format being used, calc
     * value using PDMAUDIOPCMPROPS_MAKE_SHIFT.
     * @sa   PDMAUDIOSTREAMCFG_B2F, PDMAUDIOSTREAMCFG_F2B */
    uint8_t     cShiftX;
    /** Sample width (in bytes). */
    RT_GCC_EXTENSION
    uint8_t     cbSampleX : 4;
    /** Number of audio channels. */
    RT_GCC_EXTENSION
    uint8_t     cChannelsX : 4;
    /** Signed or unsigned sample. */
    bool        fSigned : 1;
    /** Whether the endianness is swapped or not. */
    bool        fSwapEndian : 1;
    /** Raw mixer frames, only applicable for signed 64-bit samples.
     * The raw mixer samples are really just signed 32-bit samples stored as 64-bit
     * integers without any change in the value.
     *
     * @todo Get rid of this, only VRDE needs it an it should use the common
     *       mixer code rather than cooking its own stuff. */
    bool        fRaw : 1;
    /** Sample frequency in Hertz (Hz). */
    uint32_t    uHz;
    /** PDMAUDIOCHANNELID mappings for each channel.
     * This ASSUMES all channels uses the same sample size. */
    uint8_t     aidChannels[PDMAUDIO_MAX_CHANNELS];
    /** Padding the structure up to 32 bytes. */
    uint32_t    auPadding[3];
} PDMAUDIOPCMPROPS;
AssertCompileSize(PDMAUDIOPCMPROPS, 32);
AssertCompileSizeAlignment(PDMAUDIOPCMPROPS, 8);
/** Pointer to audio stream properties. */
typedef PDMAUDIOPCMPROPS *PPDMAUDIOPCMPROPS;
/** Pointer to const audio stream properties. */
typedef PDMAUDIOPCMPROPS const *PCPDMAUDIOPCMPROPS;

/** @name Macros for use with PDMAUDIOPCMPROPS
 * @{ */
/** Initializer for PDMAUDIOPCMPROPS.
 * @note The default channel mapping here is very simple and doesn't always
 *       match that of PDMAudioPropsInit and PDMAudioPropsInitEx. */
#define PDMAUDIOPCMPROPS_INITIALIZER(a_cbSample, a_fSigned, a_cChannels, a_uHz, a_fSwapEndian) \
    { \
        (uint8_t)((a_cbSample) * (a_cChannels)), PDMAUDIOPCMPROPS_MAKE_SHIFT_PARMS(a_cbSample, a_cChannels), \
        (uint8_t)(a_cbSample), (uint8_t)(a_cChannels), a_fSigned, a_fSwapEndian, false /*fRaw*/, a_uHz, \
        /*aidChannels =*/ { \
            (a_cChannels) >   1 ? PDMAUDIOCHANNELID_FRONT_LEFT              : PDMAUDIOCHANNELID_MONO, \
            (a_cChannels) >=  2 ? PDMAUDIOCHANNELID_FRONT_RIGHT             : PDMAUDIOCHANNELID_INVALID, \
            (a_cChannels) >=  3 ? PDMAUDIOCHANNELID_FRONT_CENTER            : PDMAUDIOCHANNELID_INVALID, \
            (a_cChannels) >=  4 ? PDMAUDIOCHANNELID_LFE                     : PDMAUDIOCHANNELID_INVALID, \
            (a_cChannels) >=  5 ? PDMAUDIOCHANNELID_REAR_LEFT               : PDMAUDIOCHANNELID_INVALID, \
            (a_cChannels) >=  6 ? PDMAUDIOCHANNELID_REAR_RIGHT              : PDMAUDIOCHANNELID_INVALID, \
            (a_cChannels) >=  7 ? PDMAUDIOCHANNELID_FRONT_LEFT_OF_CENTER    : PDMAUDIOCHANNELID_INVALID, \
            (a_cChannels) >=  8 ? PDMAUDIOCHANNELID_FRONT_RIGHT_OF_CENTER   : PDMAUDIOCHANNELID_INVALID, \
            (a_cChannels) >=  9 ? PDMAUDIOCHANNELID_REAR_CENTER             : PDMAUDIOCHANNELID_INVALID, \
            (a_cChannels) >= 10 ? PDMAUDIOCHANNELID_SIDE_LEFT               : PDMAUDIOCHANNELID_INVALID, \
            (a_cChannels) >= 11 ? PDMAUDIOCHANNELID_SIDE_RIGHT              : PDMAUDIOCHANNELID_INVALID, \
            (a_cChannels) >= 12 ? PDMAUDIOCHANNELID_UNKNOWN                 : PDMAUDIOCHANNELID_INVALID, \
         }, \
         /* auPadding = */ { 0, 0, 0 } \
    }

/** Calculates the cShift value of given sample bits and audio channels.
 * @note Does only support mono/stereo channels for now, for non-stereo/mono we
 *       returns a special value which the two conversion functions detect
 *       and make them fall back on cbSample * cChannels. */
#define PDMAUDIOPCMPROPS_MAKE_SHIFT_PARMS(cbSample, cChannels) \
    ( RT_IS_POWER_OF_TWO((unsigned)((cChannels) * (cbSample))) \
      ? (uint8_t)(ASMBitFirstSetU32((unsigned)((cChannels) * (cbSample))) - 1) : (uint8_t)UINT8_MAX )
/** Calculates the cShift value of a PDMAUDIOPCMPROPS structure. */
#define PDMAUDIOPCMPROPS_MAKE_SHIFT(pProps) \
    PDMAUDIOPCMPROPS_MAKE_SHIFT_PARMS((pProps)->cbSampleX, (pProps)->cChannelsX)
/** Converts (audio) frames to bytes.
 * @note Requires properly initialized properties, i.e. cbFrames correctly calculated
 *       and cShift set using PDMAUDIOPCMPROPS_MAKE_SHIFT. */
#define PDMAUDIOPCMPROPS_F2B(pProps, cFrames) \
    ( (pProps)->cShiftX != UINT8_MAX ? (cFrames) << (pProps)->cShiftX : (cFrames) * (pProps)->cbFrame )
/** Converts bytes to (audio) frames.
 * @note Requires properly initialized properties, i.e. cbFrames correctly calculated
 *       and cShift set using PDMAUDIOPCMPROPS_MAKE_SHIFT. */
#define PDMAUDIOPCMPROPS_B2F(pProps, cb) \
    ( (pProps)->cShiftX != UINT8_MAX ?      (cb) >> (pProps)->cShiftX :      (cb) / (pProps)->cbFrame )
/** @}   */

/**
 * An audio stream configuration.
 */
typedef struct PDMAUDIOSTREAMCFG
{
    /** The stream's PCM properties. */
    PDMAUDIOPCMPROPS        Props;
    /** Direction of the stream. */
    PDMAUDIODIR             enmDir;
    /** Destination / source path. */
    PDMAUDIOPATH            enmPath;
    /** Device emulation-specific data needed for the audio connector. */
    struct
    {
        /** Scheduling hint set by the device emulation about when this stream is being served on average (in ms).
         *  Can be 0 if not hint given or some other mechanism (e.g. callbacks) is being used. */
        uint32_t            cMsSchedulingHint;
    } Device;
    /**
     * Backend-specific data for the stream.
     * On input (requested configuration) those values are set by the audio connector to let the backend know what we expect.
     * On output (acquired configuration) those values reflect the values set and used by the backend.
     * Set by the backend on return. Not all backends support all values / features.
     */
    struct
    {
        /** Period size of the stream (in audio frames).
         *  This value reflects the number of audio frames in between each hardware interrupt on the
         *  backend (host) side. 0 if not set / available by the backend. */
        uint32_t            cFramesPeriod;
        /** (Ring) buffer size (in audio frames). Often is a multiple of cFramesPeriod.
         *  0 if not set / available by the backend. */
        uint32_t            cFramesBufferSize;
        /** Pre-buffering size (in audio frames). Frames needed in buffer before the stream becomes active (pre buffering).
         *  The bigger this value is, the more latency for the stream will occur.
         *  0 if not set / available by the backend. UINT32_MAX if not defined (yet). */
        uint32_t            cFramesPreBuffering;
    } Backend;
    /** Friendly name of the stream. */
    char                    szName[64];
} PDMAUDIOSTREAMCFG;
AssertCompileSizeAlignment(PDMAUDIOSTREAMCFG, 8);
/** Pointer to audio stream configuration keeper. */
typedef PDMAUDIOSTREAMCFG *PPDMAUDIOSTREAMCFG;
/** Pointer to a const audio stream configuration keeper. */
typedef PDMAUDIOSTREAMCFG const *PCPDMAUDIOSTREAMCFG;

/** Converts (audio) frames to bytes. */
#define PDMAUDIOSTREAMCFG_F2B(pCfg, frames)     PDMAUDIOPCMPROPS_F2B(&(pCfg)->Props, (frames))
/** Converts bytes to (audio) frames. */
#define PDMAUDIOSTREAMCFG_B2F(pCfg, cb)         PDMAUDIOPCMPROPS_B2F(&(pCfg)->Props, (cb))

/**
 * Audio stream commands.
 *
 * Used in the audio connector as well as in the actual host backends.
 */
typedef enum PDMAUDIOSTREAMCMD
{
    /** Invalid zero value as per usual (guards against using unintialized values). */
    PDMAUDIOSTREAMCMD_INVALID = 0,
    /** Enables the stream. */
    PDMAUDIOSTREAMCMD_ENABLE,
    /** Pauses the stream.
     * This is currently only issued when the VM is suspended (paused).
     * @remarks This is issued by DrvAudio, never by the mixer or devices. */
    PDMAUDIOSTREAMCMD_PAUSE,
    /** Resumes the stream.
     * This is currently only issued when the VM is resumed.
     * @remarks This is issued by DrvAudio, never by the mixer or devices. */
    PDMAUDIOSTREAMCMD_RESUME,
    /** Drain the stream, that is, play what's in the buffers and then stop.
     *
     * There will be no more samples written after this command is issued.
     * PDMIAUDIOCONNECTOR::pfnStreamIterate will drive progress for DrvAudio and
     * calls to PDMIHOSTAUDIO::pfnStreamPlay with a zero sized buffer will provide
     * the backend with a way to drive it forwards.  These calls will come at a
     * frequency set by the device and be on an asynchronous I/O thread.
     *
     * A DISABLE command maybe submitted if the device/mixer wants to re-enable the
     * stream while it's still draining or if it gets impatient and thinks the
     * draining has been going on too long, in which case the stream should stop
     * immediately.
     *
     * @note    This should not wait for the stream to finish draining, just change
     *          the state.  (The caller could be an EMT and it must not block for
     *          hundreds of milliseconds of buffer to finish draining.)
     *
     * @note    Does not apply to input streams. Backends should refuse such requests. */
    PDMAUDIOSTREAMCMD_DRAIN,
    /** Stops the stream immediately w/o any draining. */
    PDMAUDIOSTREAMCMD_DISABLE,
    /** End of valid values. */
    PDMAUDIOSTREAMCMD_END,
    /** Hack to blow the type up to 32-bit. */
    PDMAUDIOSTREAMCMD_32BIT_HACK = 0x7fffffff
} PDMAUDIOSTREAMCMD;

/**
 * Backend status.
 */
typedef enum PDMAUDIOBACKENDSTS
{
    /** Unknown/invalid status. */
    PDMAUDIOBACKENDSTS_UNKNOWN = 0,
    /** No backend attached. */
    PDMAUDIOBACKENDSTS_NOT_ATTACHED,
    /** The backend is in its initialization phase.
     *  Not all backends support this status. */
    PDMAUDIOBACKENDSTS_INITIALIZING,
    /** The backend has stopped its operation. */
    PDMAUDIOBACKENDSTS_STOPPED,
    /** The backend is up and running. */
    PDMAUDIOBACKENDSTS_RUNNING,
    /** The backend ran into an error and is unable to recover.
     *  A manual re-initialization might help. */
    PDMAUDIOBACKENDSTS_ERROR,
    /** Hack to blow the type up to 32-bit. */
    PDMAUDIOBACKENDSTS_32BIT_HACK = 0x7fffffff
} PDMAUDIOBACKENDSTS;

/**
 * PDM audio stream state.
 *
 * This is all the mixer/device needs.  The PDMAUDIOSTREAM_STS_XXX stuff will
 * become DrvAudio internal state once the backend stuff is destilled out of it.
 *
 * @note    The value order is significant, don't change it willy-nilly.
 */
typedef enum PDMAUDIOSTREAMSTATE
{
    /** Invalid state value. */
    PDMAUDIOSTREAMSTATE_INVALID = 0,
    /** The stream is not operative and cannot be enabled. */
    PDMAUDIOSTREAMSTATE_NOT_WORKING,
    /** The stream needs to be re-initialized by the device/mixer
     * (i.e. call PDMIAUDIOCONNECTOR::pfnStreamReInit). */
    PDMAUDIOSTREAMSTATE_NEED_REINIT,
    /** The stream is inactive (not enabled). */
    PDMAUDIOSTREAMSTATE_INACTIVE,
    /** The stream is enabled but nothing to read/write.
     *  @todo not sure if we need this variant... */
    PDMAUDIOSTREAMSTATE_ENABLED,
    /** The stream is enabled and captured samples can be read. */
    PDMAUDIOSTREAMSTATE_ENABLED_READABLE,
    /** The stream is enabled and samples can be written for playback. */
    PDMAUDIOSTREAMSTATE_ENABLED_WRITABLE,
    /** End of valid states.   */
    PDMAUDIOSTREAMSTATE_END,
    /** Make sure the type is 32-bit wide. */
    PDMAUDIOSTREAMSTATE_32BIT_HACK = 0x7fffffff
} PDMAUDIOSTREAMSTATE;

/** @name PDMAUDIOSTREAM_CREATE_F_XXX
 * @{ */
/** Does not need any mixing buffers, the device takes care of all conversion.
 * @note this is now default and assumed always set. */
#define PDMAUDIOSTREAM_CREATE_F_NO_MIXBUF       RT_BIT_32(0)
/** @} */

/** @name PDMAUDIOSTREAM_WARN_FLAGS_XXX
 * @{ */
/** No stream warning flags set. */
#define PDMAUDIOSTREAM_WARN_FLAGS_NONE          0
/** Warned about a disabled stream. */
#define PDMAUDIOSTREAM_WARN_FLAGS_DISABLED      RT_BIT(0)
/** @} */

/**
 * An input or output audio stream.
 */
typedef struct PDMAUDIOSTREAM
{
    /** Critical section protecting the stream.
     *
     * When not otherwise stated, DrvAudio will enter this before calling the
     * backend.   The backend and device/mixer can normally safely enter it prior to
     * a DrvAudio call, however not to pfnStreamDestroy, pfnStreamRelease or
     * anything that may access the stream list.
     *
     * @note Lock ordering:
     *          - After DRVAUDIO::CritSectGlobals.
     *          - Before DRVAUDIO::CritSectHotPlug. */
    RTCRITSECT              CritSect;
    /** Stream configuration. */
    PDMAUDIOSTREAMCFG       Cfg;
    /** Magic value (PDMAUDIOSTREAM_MAGIC). */
    uint32_t                uMagic;
    /** Size (in bytes) of the backend-specific stream data. */
    uint32_t                cbBackend;
    /** Warnings shown already in the release log.
     *  See PDMAUDIOSTREAM_WARN_FLAGS_XXX. */
    uint32_t                fWarningsShown;
} PDMAUDIOSTREAM;
/** Pointer to an audio stream. */
typedef struct PDMAUDIOSTREAM *PPDMAUDIOSTREAM;
/** Pointer to a const audio stream. */
typedef struct PDMAUDIOSTREAM const *PCPDMAUDIOSTREAM;

/** Magic value for PDMAUDIOSTREAM. */
#define PDMAUDIOSTREAM_MAGIC    PDM_VERSION_MAKE(0xa0d3, 5, 0)



/** Pointer to a audio connector interface. */
typedef struct PDMIAUDIOCONNECTOR *PPDMIAUDIOCONNECTOR;

/**
 * Audio connector interface (up).
 */
typedef struct PDMIAUDIOCONNECTOR
{
    /**
     * Enables or disables the given audio direction for this driver.
     *
     * When disabled, assiociated output streams consume written audio without passing them further down to the backends.
     * Associated input streams then return silence when read from those.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   enmDir          Audio direction to enable or disable driver for.
     * @param   fEnable         Whether to enable or disable the specified audio direction.
     *
     * @note    Be very careful when using this function, as this could
     *          violate / run against the (global) VM settings.  See @bugref{9882}.
     */
    DECLR3CALLBACKMEMBER(int, pfnEnable, (PPDMIAUDIOCONNECTOR pInterface, PDMAUDIODIR enmDir, bool fEnable));

    /**
     * Returns whether the given audio direction for this driver is enabled or not.
     *
     * @returns True if audio is enabled for the given direction, false if not.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   enmDir          Audio direction to retrieve enabled status for.
     */
    DECLR3CALLBACKMEMBER(bool, pfnIsEnabled, (PPDMIAUDIOCONNECTOR pInterface, PDMAUDIODIR enmDir));

    /**
     * Retrieves the current configuration of the host audio backend.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pCfg            Where to store the host audio backend configuration data.
     */
    DECLR3CALLBACKMEMBER(int, pfnGetConfig, (PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOBACKENDCFG pCfg));

    /**
     * Retrieves the current status of the host audio backend.
     *
     * @returns Status of the host audio backend.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   enmDir          Audio direction to check host audio backend for. Specify PDMAUDIODIR_DUPLEX for the overall
     *                          backend status.
     */
    DECLR3CALLBACKMEMBER(PDMAUDIOBACKENDSTS, pfnGetStatus, (PPDMIAUDIOCONNECTOR pInterface, PDMAUDIODIR enmDir));

    /**
     * Gives the audio drivers a hint about a typical configuration.
     *
     * This is a little hack for windows (and maybe other hosts) where stream
     * creation can take a relatively long time, making it very unsuitable for EMT.
     * The audio backend can use this hint to cache pre-configured stream setups,
     * so that when the guest actually wants to play something EMT won't be blocked
     * configuring host audio.
     *
     * @param   pInterface  Pointer to this interface.
     * @param   pCfg        The typical configuration.  Can be modified by the
     *                      drivers in unspecified ways.
     */
    DECLR3CALLBACKMEMBER(void, pfnStreamConfigHint, (PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAMCFG pCfg));

    /**
     * Creates an audio stream.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this interface.
     * @param   fFlags      PDMAUDIOSTREAM_CREATE_F_XXX.
     * @param   pCfgReq     The requested stream configuration.  The actual stream
     *                      configuration can be found in pStream->Cfg on success.
     * @param   ppStream    Pointer where to return the created audio stream on
     *                      success.
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamCreate, (PPDMIAUDIOCONNECTOR pInterface, uint32_t fFlags, PCPDMAUDIOSTREAMCFG pCfgReq,
                                                PPDMAUDIOSTREAM *ppStream));


    /**
     * Destroys an audio stream.
     *
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pStream         Pointer to audio stream.
     * @param   fImmediate      Whether to immdiately stop and destroy a draining
     *                          stream (@c true), or to allow it to complete
     *                          draining first (@c false) if that's feasable.
     *                          The latter depends on the draining stage and what
     *                          the backend is capable of.
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamDestroy, (PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream, bool fImmediate));

    /**
     * Re-initializes the stream in response to PDMAUDIOSTREAM_STS_NEED_REINIT.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to this interface.
     * @param   pStream         The audio stream needing re-initialization.
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamReInit, (PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream));

    /**
     * Adds a reference to the specified audio stream.
     *
     * @returns New reference count. UINT32_MAX on error.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pStream         Pointer to audio stream adding the reference to.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnStreamRetain, (PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream));

    /**
     * Releases a reference from the specified stream.
     *
     * @returns New reference count. UINT32_MAX on error.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pStream         Pointer to audio stream releasing a reference from.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnStreamRelease, (PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream));

    /**
     * Controls a specific audio stream.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pStream         Pointer to audio stream.
     * @param   enmStreamCmd    The stream command to issue.
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamControl, (PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream,
                                                 PDMAUDIOSTREAMCMD enmStreamCmd));

    /**
     * Processes stream data.
     *
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pStream         Pointer to audio stream.
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamIterate, (PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream));

    /**
     * Returns the state of a specific audio stream (destilled status).
     *
     * @returns PDMAUDIOSTREAMSTATE value.
     * @retval  PDMAUDIOSTREAMSTATE_INVALID if the input isn't valid (w/ assertion).
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pStream         Pointer to audio stream.
     */
    DECLR3CALLBACKMEMBER(PDMAUDIOSTREAMSTATE, pfnStreamGetState, (PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream));

    /**
     * Returns the number of bytes that can be written to an audio output stream.
     *
     * @returns Number of bytes writable data.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pStream         Pointer to audio stream.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnStreamGetWritable, (PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream));

    /**
     * Plays (writes to) an audio output stream.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pStream         Pointer to audio stream to read from.
     * @param   pvBuf           Audio data to be written.
     * @param   cbBuf           Number of bytes to be written.
     * @param   pcbWritten      Bytes of audio data written. Optional.
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamPlay, (PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream,
                                              const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten));

    /**
     * Returns the number of bytes that can be read from an input stream.
     *
     * @returns Number of bytes of readable data.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pStream         Pointer to audio stream.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnStreamGetReadable, (PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream));

    /**
     * Captures (reads) samples from an audio input stream.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to the interface structure containing the called function pointer.
     * @param   pStream         Pointer to audio stream to write to.
     * @param   pvBuf           Where to store the read data.
     * @param   cbBuf           Number of bytes to read.
     * @param   pcbRead         Bytes of audio data read. Optional.
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamCapture, (PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream,
                                                 void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead));
} PDMIAUDIOCONNECTOR;

/** PDMIAUDIOCONNECTOR interface ID. */
#define PDMIAUDIOCONNECTOR_IID                  "2900fe2a-6aeb-4953-ac12-f8965612f446"


/**
 * Host audio backend specific stream data.
 *
 * The backend will put this as the first member of it's own data structure.
 */
typedef struct PDMAUDIOBACKENDSTREAM
{
    /** Magic value (PDMAUDIOBACKENDSTREAM_MAGIC). */
    uint32_t            uMagic;
    /** Explicit zero padding - do not touch! */
    uint32_t            uReserved;
    /** Pointer to the stream this backend data is associated with. */
    PPDMAUDIOSTREAM     pStream;
    /** Reserved for future use (zeroed) - do not touch. */
    void               *apvReserved[2];
} PDMAUDIOBACKENDSTREAM;
/** Pointer to host audio specific stream data! */
typedef PDMAUDIOBACKENDSTREAM *PPDMAUDIOBACKENDSTREAM;

/** Magic value for PDMAUDIOBACKENDSTREAM. */
#define PDMAUDIOBACKENDSTREAM_MAGIC PDM_VERSION_MAKE(0xa0d4, 1, 0)

/**
 * Host audio (backend) stream state returned by PDMIHOSTAUDIO::pfnStreamGetState.
 */
typedef enum PDMHOSTAUDIOSTREAMSTATE
{
    /** Invalid zero value, as per usual.   */
    PDMHOSTAUDIOSTREAMSTATE_INVALID = 0,
    /** The stream is being initialized.
     * This should also be used when switching to a new device and the stream
     * stops to work with the old device while the new one being configured.  */
    PDMHOSTAUDIOSTREAMSTATE_INITIALIZING,
    /** The stream does not work (async init failed, audio subsystem gone
     *  fishing, or similar). */
    PDMHOSTAUDIOSTREAMSTATE_NOT_WORKING,
    /** Backend is working okay. */
    PDMHOSTAUDIOSTREAMSTATE_OKAY,
    /** Backend is working okay, but currently draining the stream. */
    PDMHOSTAUDIOSTREAMSTATE_DRAINING,
    /** Backend is working but doesn't want any commands or data reads/writes. */
    PDMHOSTAUDIOSTREAMSTATE_INACTIVE,
    /** End of valid values. */
    PDMHOSTAUDIOSTREAMSTATE_END,
    /** Blow the type up to 32 bits. */
    PDMHOSTAUDIOSTREAMSTATE_32BIT_HACK = 0x7fffffff
} PDMHOSTAUDIOSTREAMSTATE;


/** Pointer to a host audio interface. */
typedef struct PDMIHOSTAUDIO *PPDMIHOSTAUDIO;

/**
 * PDM host audio interface.
 */
typedef struct PDMIHOSTAUDIO
{
    /**
     * Returns the host backend's configuration (backend).
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to the interface structure containing the called function pointer.
     * @param   pBackendCfg         Where to store the backend audio configuration to.
     */
    DECLR3CALLBACKMEMBER(int, pfnGetConfig, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDCFG pBackendCfg));

    /**
     * Returns (enumerates) host audio device information (optional).
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to the interface structure containing the called function pointer.
     * @param   pDeviceEnum         Where to return the enumerated audio devices.
     */
    DECLR3CALLBACKMEMBER(int, pfnGetDevices, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOHOSTENUM pDeviceEnum));

    /**
     * Changes the output or input device.
     *
     * @returns VBox status code.
     * @param   pInterface      Pointer to this interface.
     * @param   enmDir          The direction to set the device for: PDMAUDIODIR_IN,
     *                          PDMAUDIODIR_OUT or PDMAUDIODIR_DUPLEX (both the
     *                          previous).
     * @param   pszId           The PDMAUDIOHOSTDEV::pszId value of the device to
     *                          use, or NULL / empty string for the default device.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetDevice, (PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir, const char *pszId));

    /**
     * Returns the current status from the audio backend (optional).
     *
     * @returns PDMAUDIOBACKENDSTS enum.
     * @param   pInterface          Pointer to the interface structure containing the called function pointer.
     * @param   enmDir              Audio direction to get status for. Pass PDMAUDIODIR_DUPLEX for overall status.
     */
    DECLR3CALLBACKMEMBER(PDMAUDIOBACKENDSTS, pfnGetStatus, (PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir));

    /**
     * Callback for genric on-worker-thread requests initiated by the backend itself.
     *
     * This is the counterpart to PDMIHOSTAUDIOPORT::pfnDoOnWorkerThread that will
     * be invoked on a worker thread when the backend requests it - optional.
     *
     * This does not return a value, so the backend must keep track of
     * failure/success on its own.
     *
     * This method is optional.  A non-NULL will, together with pfnStreamInitAsync
     * and PDMAUDIOBACKEND_F_ASYNC_HINT, force DrvAudio to create the thread pool.
     *
     * @param   pInterface  Pointer to this interface.
     * @param   pStream     Optionally a backend stream if specified in the
     *                      PDMIHOSTAUDIOPORT::pfnDoOnWorkerThread() call.
     * @param   uUser       User specific value as specified in the
     *                      PDMIHOSTAUDIOPORT::pfnDoOnWorkerThread() call.
     * @param   pvUser      User specific pointer as specified in the
     *                      PDMIHOSTAUDIOPORT::pfnDoOnWorkerThread() call.
     */
    DECLR3CALLBACKMEMBER(void, pfnDoOnWorkerThread,(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                    uintptr_t uUser, void *pvUser));

    /**
     * Gives the audio backend a hint about a typical configuration (optional).
     *
     * This is a little hack for windows (and maybe other hosts) where stream
     * creation can take a relatively long time, making it very unsuitable for EMT.
     * The audio backend can use this hint to cache pre-configured stream setups,
     * so that when the guest actually wants to play something EMT won't be blocked
     * configuring host audio.
     *
     * The backend can return PDMAUDIOBACKEND_F_ASYNC_HINT in
     * PDMIHOSTAUDIO::pfnGetConfig to avoid having EMT making this call and thereby
     * speeding up VM construction.
     *
     * @param   pInterface      Pointer to this interface.
     * @param   pCfg            The typical configuration.  (Feel free to change it
     *                          to the actual stream config that would be used,
     *                          however caller will probably ignore this.)
     */
    DECLR3CALLBACKMEMBER(void, pfnStreamConfigHint, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOSTREAMCFG pCfg));

    /**
     * Creates an audio stream using the requested stream configuration.
     *
     * If a backend is not able to create this configuration, it will return its
     * best match in the acquired configuration structure on success.
     *
     * @returns VBox status code.
     * @retval  VINF_AUDIO_STREAM_ASYNC_INIT_NEEDED if
     *          PDMIHOSTAUDIO::pfnStreamInitAsync should be called.
     * @param   pInterface      Pointer to this interface.
     * @param   pStream         Pointer to the audio stream.
     * @param   pCfgReq         The requested stream configuration.
     * @param   pCfgAcq         The acquired stream configuration - output.  This is
     *                          the same as @a *pCfgReq when called, the
     *                          implementation will adjust it to make the actual
     *                          stream configuration as needed.
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamCreate, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                PCPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq));

    /**
     * Asynchronous stream initialization step, optional.
     *
     * This is called on a worker thread iff the PDMIHOSTAUDIO::pfnStreamCreate
     * method returns VINF_AUDIO_STREAM_ASYNC_INIT_NEEDED.
     *
     * @returns VBox status code.
     * @param   pInterface          Pointer to this interface.
     * @param   pStream             Pointer to audio stream to continue
     *                              initialization of.
     * @param   fDestroyed          Set to @c true if the stream has been destroyed
     *                              before the worker thread got to making this
     *                              call.  The backend should just ready the stream
     *                              for destruction in that case.
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamInitAsync, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream, bool fDestroyed));

    /**
     * Destroys an audio stream.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to the interface containing the called function.
     * @param   pStream     Pointer to audio stream.
     * @param   fImmediate  Whether to immdiately stop and destroy a draining
     *                      stream (@c true), or to allow it to complete
     *                      draining first (@c false) if that's feasable.
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamDestroy, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream, bool fImmediate));

    /**
     * Called from PDMIHOSTAUDIOPORT::pfnNotifyDeviceChanged so the backend can start
     * the device change for a stream.
     *
     * This is mainly to avoid the need for a list of streams in the backend.
     *
     * @param   pInterface          Pointer to this interface.
     * @param   pStream             Pointer to audio stream (locked).
     * @param   pvUser              Backend specific parameter from the call to
     *                              PDMIHOSTAUDIOPORT::pfnNotifyDeviceChanged.
     */
    DECLR3CALLBACKMEMBER(void, pfnStreamNotifyDeviceChanged,(PPDMIHOSTAUDIO pInterface,
                                                             PPDMAUDIOBACKENDSTREAM pStream, void *pvUser));

    /**
     * Enables (starts) the stream.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this interface.
     * @param   pStream     Pointer to the audio stream to enable.
     * @sa      PDMAUDIOSTREAMCMD_ENABLE
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamEnable, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream));

    /**
     * Disables (stops) the stream immediately.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this interface.
     * @param   pStream     Pointer to the audio stream to disable.
     * @sa      PDMAUDIOSTREAMCMD_DISABLE
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamDisable, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream));

    /**
     * Pauses the stream - called when the VM is suspended.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this interface.
     * @param   pStream     Pointer to the audio stream to pause.
     * @sa      PDMAUDIOSTREAMCMD_PAUSE
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamPause, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream));

    /**
     * Resumes a paused stream - called when the VM is resumed.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this interface.
     * @param   pStream     Pointer to the audio stream to resume.
     * @sa      PDMAUDIOSTREAMCMD_RESUME
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamResume, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream));

    /**
     * Drain the stream, that is, play what's in the buffers and then stop.
     *
     * There will be no more samples written after this command is issued.
     * PDMIHOSTAUDIO::pfnStreamPlay with a zero sized buffer will provide the
     * backend with a way to drive it forwards.  These calls will come at a
     * frequency set by the device and be on an asynchronous I/O thread.
     *
     * The PDMIHOSTAUDIO::pfnStreamDisable method maybe called if the device/mixer
     * wants to re-enable the stream while it's still draining or if it gets
     * impatient and thinks the draining has been going on too long, in which case
     * the stream should stop immediately.
     *
     * @note    This should not wait for the stream to finish draining, just change
     *          the state.  (The caller could be an EMT and it must not block for
     *          hundreds of milliseconds of buffer to finish draining.)
     *
     * @note    Does not apply to input streams. Backends should refuse such
     *          requests.
     *
     * @returns VBox status code.
     * @retval  VERR_WRONG_ORDER if not output stream.
     * @param   pInterface  Pointer to this interface.
     * @param   pStream     Pointer to the audio stream to drain.
     * @sa      PDMAUDIOSTREAMCMD_DRAIN
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamDrain, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream));

    /**
     * Returns the current state of the given backend stream.
     *
     * @returns PDMHOSTAUDIOSTREAMSTATE value.
     * @retval  PDMHOSTAUDIOSTREAMSTATE_INVALID if invalid stream.
     * @param   pInterface          Pointer to the interface structure containing the called function pointer.
     * @param   pStream             Pointer to audio stream.
     */
    DECLR3CALLBACKMEMBER(PDMHOSTAUDIOSTREAMSTATE, pfnStreamGetState, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream));

    /**
     * Returns the number of buffered bytes that hasn't been played yet (optional).
     *
     * Is not valid on an input stream, implementions shall assert and return zero.
     *
     * @returns Number of pending bytes.
     * @param   pInterface          Pointer to this interface.
     * @param   pStream             Pointer to the audio stream.
     *
     * @todo This is no longer not used by DrvAudio and can probably be removed.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnStreamGetPending, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream));

    /**
     * Returns the amount which is writable to the audio (output) stream.
     *
     * @returns Number of writable bytes.
     * @param   pInterface          Pointer to the interface structure containing the called function pointer.
     * @param   pStream             Pointer to audio stream.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnStreamGetWritable, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream));

    /**
     * Plays (writes to) an audio (output) stream.
     *
     * This is always called with data in the buffer, except after
     * PDMAUDIOSTREAMCMD_DRAIN is issued when it's called every so often to assist
     * the backend with moving the draining operation forward (kind of like
     * PDMIAUDIOCONNECTOR::pfnStreamIterate).
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to the interface structure containing the called function pointer.
     * @param   pStream     Pointer to audio stream.
     * @param   pvBuf       Pointer to audio data buffer to play.  This will be NULL
     *                      when called to assist draining the stream.
     * @param   cbBuf       The number of bytes of audio data to play.  This will be
     *                      zero when called to assist draining the stream.
     * @param   pcbWritten  Where to return the actual number of bytes played.
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamPlay, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                              const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten));

    /**
     * Returns the amount which is readable from the audio (input) stream.
     *
     * @returns For non-raw layout streams: Number of readable bytes.
     *          for raw layout streams    : Number of readable audio frames.
     * @param   pInterface          Pointer to the interface structure containing the called function pointer.
     * @param   pStream             Pointer to audio stream.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnStreamGetReadable, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream));

    /**
     * Captures (reads from) an audio (input) stream.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to the interface structure containing the called function pointer.
     * @param   pStream     Pointer to audio stream.
     * @param   pvBuf       Buffer where to store read audio data.
     * @param   cbBuf       Size of the audio data buffer in bytes.
     * @param   pcbRead     Where to return the number of bytes actually captured.
     */
    DECLR3CALLBACKMEMBER(int, pfnStreamCapture, (PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                 void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead));
} PDMIHOSTAUDIO;

/** PDMIHOSTAUDIO interface ID. */
#define PDMIHOSTAUDIO_IID                           "c0875b91-a4f9-48be-8595-31d27048432d"


/** Pointer to a audio notify from host interface. */
typedef struct PDMIHOSTAUDIOPORT *PPDMIHOSTAUDIOPORT;

/**
 * PDM host audio port interface, upwards sibling of PDMIHOSTAUDIO.
 */
typedef struct PDMIHOSTAUDIOPORT
{
    /**
     * Ask DrvAudio to call PDMIHOSTAUDIO::pfnDoOnWorkerThread on a worker thread.
     *
     * Generic method for doing asynchronous work using the DrvAudio thread pool.
     *
     * This function will not wait for PDMIHOSTAUDIO::pfnDoOnWorkerThread to
     * complete, but returns immediately after submitting the request to the thread
     * pool.
     *
     * @returns VBox status code.
     * @param   pInterface  Pointer to this interface.
     * @param   pStream     Optional backend stream structure to pass along.  The
     *                      reference count will be increased till the call
     *                      completes to make sure the stream stays valid.
     * @param   uUser       User specific value.
     * @param   pvUser      User specific pointer.
     */
    DECLR3CALLBACKMEMBER(int, pfnDoOnWorkerThread,(PPDMIHOSTAUDIOPORT pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                   uintptr_t uUser, void *pvUser));

    /**
     * The device for the given direction changed.
     *
     * The driver above backend (DrvAudio) will call the backend back
     * (PDMIHOSTAUDIO::pfnStreamNotifyDeviceChanged) for all open streams in the
     * given direction. (This ASSUMES the backend uses one output device and one
     * input devices for all streams.)
     *
     * @param   pInterface  Pointer to this interface.
     * @param   enmDir      The audio direction.
     * @param   pvUser      Backend specific parameter for
     *                      PDMIHOSTAUDIO::pfnStreamNotifyDeviceChanged.
     */
    DECLR3CALLBACKMEMBER(void, pfnNotifyDeviceChanged,(PPDMIHOSTAUDIOPORT pInterface, PDMAUDIODIR enmDir, void *pvUser));

    /**
     * Notification that the stream is about to change device in a bit.
     *
     * This will assume PDMAUDIOSTREAM_STS_PREPARING_SWITCH will be set when
     * PDMIHOSTAUDIO::pfnStreamGetStatus is next called and change the stream state
     * accordingly.
     *
     * @param   pInterface  Pointer to this interface.
     * @param   pStream     The stream that changed device (backend variant).
     */
    DECLR3CALLBACKMEMBER(void, pfnStreamNotifyPreparingDeviceSwitch,(PPDMIHOSTAUDIOPORT pInterface,
                                                                     PPDMAUDIOBACKENDSTREAM pStream));

    /**
     * The stream has changed its device and left the
     * PDMAUDIOSTREAM_STS_PREPARING_SWITCH state (if it entered it at all).
     *
     * @param   pInterface  Pointer to this interface.
     * @param   pStream     The stream that changed device (backend variant).
     * @param   fReInit     Set if a re-init is required, clear if not.
     */
    DECLR3CALLBACKMEMBER(void, pfnStreamNotifyDeviceChanged,(PPDMIHOSTAUDIOPORT pInterface,
                                                             PPDMAUDIOBACKENDSTREAM pStream, bool fReInit));

    /**
     * One or more audio devices have changed in some way.
     *
     * The upstream driver/device should re-evaluate the devices they're using.
     *
     * @todo r=bird: The upstream driver/device does not know which host audio
     *       devices they are using.  This is mainly for triggering enumeration and
     *       logging of the audio devices.
     *
     * @param   pInterface  Pointer to this interface.
     */
    DECLR3CALLBACKMEMBER(void, pfnNotifyDevicesChanged,(PPDMIHOSTAUDIOPORT pInterface));
} PDMIHOSTAUDIOPORT;

/** PDMIHOSTAUDIOPORT interface ID. */
#define PDMIHOSTAUDIOPORT_IID                    "92ea5169-8271-402d-99a7-9de26a52acaf"


/**
 * Audio mixer controls.
 *
 * @note This isn't part of any official PDM interface as such, it's more of a
 *       common thing that all the devices seem to need.
 */
typedef enum PDMAUDIOMIXERCTL
{
    /** Invalid zero value as per usual (guards against using unintialized values). */
    PDMAUDIOMIXERCTL_INVALID = 0,
    /** Unknown mixer control. */
    PDMAUDIOMIXERCTL_UNKNOWN,
    /** Master volume. */
    PDMAUDIOMIXERCTL_VOLUME_MASTER,
    /** Front. */
    PDMAUDIOMIXERCTL_FRONT,
    /** Center / LFE (Subwoofer). */
    PDMAUDIOMIXERCTL_CENTER_LFE,
    /** Rear. */
    PDMAUDIOMIXERCTL_REAR,
    /** Line-In. */
    PDMAUDIOMIXERCTL_LINE_IN,
    /** Microphone-In. */
    PDMAUDIOMIXERCTL_MIC_IN,
    /** End of valid values. */
    PDMAUDIOMIXERCTL_END,
    /** Hack to blow the type up to 32-bit. */
    PDMAUDIOMIXERCTL_32BIT_HACK = 0x7fffffff
} PDMAUDIOMIXERCTL;

/**
 * Audio volume parameters.
 *
 * @note This isn't part of any official PDM interface any more (it used to be
 *       used to PDMIAUDIOCONNECTOR). It's currently only used by the mixer API.
 */
typedef struct PDMAUDIOVOLUME
{
    /** Set to @c true if this stream is muted, @c false if not. */
    bool    fMuted;
    /** The volume for each channel.
     * The values zero is the most silent one (although not quite muted), and 255
     * the loudest. */
    uint8_t auChannels[PDMAUDIO_MAX_CHANNELS];
} PDMAUDIOVOLUME;
/** Pointer to audio volume settings. */
typedef PDMAUDIOVOLUME *PPDMAUDIOVOLUME;
/** Pointer to const audio volume settings. */
typedef PDMAUDIOVOLUME const *PCPDMAUDIOVOLUME;

/** Defines the minimum volume allowed. */
#define PDMAUDIO_VOLUME_MIN     (0)
/** Defines the maximum volume allowed. */
#define PDMAUDIO_VOLUME_MAX     (255)
/** Initializator for max volume on all channels. */
#define PDMAUDIOVOLUME_INITIALIZER_MAX \
    { /* .fMuted = */       false, \
      /* .auChannels = */   { 255, 255, 255, 255,  255, 255, 255, 255,  255, 255, 255, 255 } }

/** @} */

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_vmm_pdmaudioifs_h */

