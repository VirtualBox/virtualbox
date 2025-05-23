/* $Id: DrvAudio.cpp 168976 2025-05-23 15:08:01Z andreas.loeffler@oracle.com $ */
/** @file
 * Intermediate audio driver - Connects the audio device emulation with the host backend.
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
#define LOG_GROUP LOG_GROUP_DRV_AUDIO
#include <VBox/log.h>
#include <VBox/vmm/pdm.h>
#include <VBox/err.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmaudioinline.h>
#include <VBox/vmm/pdmaudiohostenuminline.h>

#include <iprt/alloc.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/circbuf.h>
#include <iprt/req.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>

#include "VBoxDD.h"

#include <ctype.h>
#include <stdlib.h>

#include "AudioHlp.h"
#include "AudioMixBuffer.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @name PDMAUDIOSTREAM_STS_XXX - Used internally by DRVAUDIOSTREAM::fStatus.
 * @{ */
/** No flags being set. */
#define PDMAUDIOSTREAM_STS_NONE                 UINT32_C(0)
/** Set if the stream is enabled, clear if disabled. */
#define PDMAUDIOSTREAM_STS_ENABLED              RT_BIT_32(0)
/** Set if the stream is paused.
 * Requires the ENABLED status to be set when used. */
#define PDMAUDIOSTREAM_STS_PAUSED               RT_BIT_32(1)
/** Output only: Set when the stream is draining.
 * Requires the ENABLED status to be set when used. */
#define PDMAUDIOSTREAM_STS_PENDING_DISABLE      RT_BIT_32(2)

/** Set if the backend for the stream has been created.
 *
 * This is generally always set after stream creation, but
 * can be cleared if the re-initialization of the stream fails later on.
 * Asynchronous init may still be incomplete, see
 * PDMAUDIOSTREAM_STS_BACKEND_READY. */
#define PDMAUDIOSTREAM_STS_BACKEND_CREATED      RT_BIT_32(3)
/** The backend is ready (PDMIHOSTAUDIO::pfnStreamInitAsync is done).
 * Requires the BACKEND_CREATED status to be set.  */
#define PDMAUDIOSTREAM_STS_BACKEND_READY        RT_BIT_32(4)
/** Set if the stream needs to be re-initialized by the device (i.e. call
 * PDMIAUDIOCONNECTOR::pfnStreamReInit). (The other status bits are preserved
 * and are worked as normal while in this state, so that the stream can
 * resume operation where it left off.)  */
#define PDMAUDIOSTREAM_STS_NEED_REINIT          RT_BIT_32(5)
/** Validation mask for PDMIAUDIOCONNECTOR. */
#define PDMAUDIOSTREAM_STS_VALID_MASK           UINT32_C(0x0000003f)
/** Asserts the validity of the given stream status mask for PDMIAUDIOCONNECTOR. */
#define PDMAUDIOSTREAM_STS_ASSERT_VALID(a_fStreamStatus) do { \
        AssertMsg(!((a_fStreamStatus) & ~PDMAUDIOSTREAM_STS_VALID_MASK), ("%#x\n", (a_fStreamStatus))); \
        Assert(!((a_fStreamStatus) & PDMAUDIOSTREAM_STS_PAUSED)          || ((a_fStreamStatus) & PDMAUDIOSTREAM_STS_ENABLED)); \
        Assert(!((a_fStreamStatus) & PDMAUDIOSTREAM_STS_PENDING_DISABLE) || ((a_fStreamStatus) & PDMAUDIOSTREAM_STS_ENABLED)); \
        Assert(!((a_fStreamStatus) & PDMAUDIOSTREAM_STS_BACKEND_READY)   || ((a_fStreamStatus) & PDMAUDIOSTREAM_STS_BACKEND_CREATED)); \
    } while (0)

/** @} */

/**
 * Experimental code for destroying all streams in a disabled direction rather
 * than just disabling them.
 *
 * Cannot be enabled yet because the code isn't complete and DrvAudio will
 * behave differently (incorrectly), see @bugref{9558#c5} for details.
 */
#if defined(DOXYGEN_RUNNING) || 0
# define DRVAUDIO_WITH_STREAM_DESTRUCTION_IN_DISABLED_DIRECTION
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Audio stream context.
 *
 * Needed for separating data from the guest and host side (per stream).
 */
typedef struct DRVAUDIOSTREAMCTX
{
    /** The stream's audio configuration. */
    PDMAUDIOSTREAMCFG   Cfg;
} DRVAUDIOSTREAMCTX;

/**
 * Capture state of a stream wrt backend.
 */
typedef enum DRVAUDIOCAPTURESTATE
{
    /** Invalid zero value.   */
    DRVAUDIOCAPTURESTATE_INVALID = 0,
    /** No capturing or pre-buffering.   */
    DRVAUDIOCAPTURESTATE_NO_CAPTURE,
    /** Regular capturing. */
    DRVAUDIOCAPTURESTATE_CAPTURING,
    /** Returning silence till the backend buffer has reched the configured
     *  pre-buffering level. */
    DRVAUDIOCAPTURESTATE_PREBUF,
    /** End of valid values. */
    DRVAUDIOCAPTURESTATE_END
} DRVAUDIOCAPTURESTATE;

/**
 * Play state of a stream wrt backend.
 */
typedef enum DRVAUDIOPLAYSTATE
{
    /** Invalid zero value.   */
    DRVAUDIOPLAYSTATE_INVALID = 0,
    /** No playback or pre-buffering.   */
    DRVAUDIOPLAYSTATE_NOPLAY,
    /** Playing w/o any prebuffering. */
    DRVAUDIOPLAYSTATE_PLAY,
    /** Parallel pre-buffering prior to a device switch (i.e. we're outputting to
     * the old device and pre-buffering the same data in parallel). */
    DRVAUDIOPLAYSTATE_PLAY_PREBUF,
    /** Initial pre-buffering or the pre-buffering for a device switch (if it
     * the device setup took less time than filling up the pre-buffer). */
    DRVAUDIOPLAYSTATE_PREBUF,
    /** The device initialization is taking too long, pre-buffering wraps around
     * and drops samples. */
    DRVAUDIOPLAYSTATE_PREBUF_OVERDUE,
    /** Same as play-prebuf, but we don't have a working output device any more. */
    DRVAUDIOPLAYSTATE_PREBUF_SWITCHING,
    /** Working on committing the pre-buffered data.
     * We'll typically leave this state immediately and go to PLAY, however if
     * the backend cannot handle all the pre-buffered data at once, we'll stay
     * here till it does. */
    DRVAUDIOPLAYSTATE_PREBUF_COMMITTING,
    /** End of valid values. */
    DRVAUDIOPLAYSTATE_END
} DRVAUDIOPLAYSTATE;


/**
 * Extended stream structure.
 */
typedef struct DRVAUDIOSTREAM
{
    /** The publicly visible bit. */
    PDMAUDIOSTREAM          Core;

    /** Just an extra magic to verify that we allocated the stream rather than some
     * faked up stuff from the device (DRVAUDIOSTREAM_MAGIC). */
    uintptr_t               uMagic;

    /** List entry in DRVAUDIO::LstStreams. */
    RTLISTNODE              ListEntry;

    /** Number of references to this stream.
     *  Only can be destroyed when the reference count reaches 0. */
    uint32_t volatile       cRefs;
    /** Stream status - PDMAUDIOSTREAM_STS_XXX. */
    uint32_t                fStatus;

    /** Data to backend-specific stream data.
     *  This data block will be casted by the backend to access its backend-dependent data.
     *
     *  That way the backends do not have access to the audio connector's data. */
    PPDMAUDIOBACKENDSTREAM  pBackend;

    /** Set if pfnStreamCreate returned VINF_AUDIO_STREAM_ASYNC_INIT_NEEDED. */
    bool                    fNeedAsyncInit;
    /** The fImmediate parameter value for pfnStreamDestroy. */
    bool                    fDestroyImmediate;
    bool                    afPadding[2];

    /** Number of (re-)tries while re-initializing the stream. */
    uint32_t                cTriesReInit;

    /** The last backend state we saw.
     * This is used to detect state changes (for what that is worth).  */
    PDMHOSTAUDIOSTREAMSTATE enmLastBackendState;

    /** The pre-buffering threshold expressed in bytes. */
    uint32_t                cbPreBufThreshold;

    /** The pfnStreamInitAsync request handle. */
    PRTREQ                  hReqInitAsync;

    /** The nanosecond timestamp when the stream was started. */
    uint64_t                nsStarted;
    /** Internal stream position (as per pfnStreamPlay/pfnStreamCapture). */
    uint64_t                offInternal;

    /** Timestamp (in ns) since last trying to re-initialize.
     *  Might be 0 if has not been tried yet. */
    uint64_t                nsLastReInit;
    /** Timestamp (in ns) since last iteration. */
    uint64_t                nsLastIterated;
    /** Timestamp (in ns) since last playback / capture. */
    uint64_t                nsLastPlayedCaptured;
    /** Timestamp (in ns) since last read (input streams) or
     *  write (output streams). */
    uint64_t                nsLastReadWritten;


    /** Union for input/output specifics depending on enmDir. */
    union
    {
        /**
         * The specifics for an audio input stream.
         */
        struct
        {
            /** The capture state. */
            DRVAUDIOCAPTURESTATE enmCaptureState;

            struct
            {
                /** File for writing non-interleaved captures. */
                PAUDIOHLPFILE   pFileCapture;
            } Dbg;
            struct
            {
                uint32_t        cbBackendReadableBefore;
                uint32_t        cbBackendReadableAfter;
#ifdef VBOX_WITH_STATISTICS
                STAMPROFILE     ProfCapture;
                STAMPROFILE     ProfGetReadable;
                STAMPROFILE     ProfGetReadableBytes;
#endif
            } Stats;
        } In;

        /**
         * The specifics for an audio output stream.
         */
        struct
        {
            /** Space for pre-buffering. */
            uint8_t            *pbPreBuf;
            /** The size of the pre-buffer allocation (in bytes). */
            uint32_t            cbPreBufAlloc;
            /** The current pre-buffering read offset. */
            uint32_t            offPreBuf;
            /** Number of bytes we've pre-buffered. */
            uint32_t            cbPreBuffered;
            /** The play state. */
            DRVAUDIOPLAYSTATE   enmPlayState;

            struct
            {
                /** File for writing stream playback. */
                PAUDIOHLPFILE   pFilePlay;
            } Dbg;
            struct
            {
                uint32_t        cbBackendWritableBefore;
                uint32_t        cbBackendWritableAfter;
#ifdef VBOX_WITH_STATISTICS
                STAMPROFILE     ProfPlay;
                STAMPROFILE     ProfGetWritable;
                STAMPROFILE     ProfGetWritableBytes;
#endif
            } Stats;
        } Out;
    } RT_UNION_NM(u);
#ifdef VBOX_WITH_STATISTICS
    STAMPROFILE     StatProfGetState;
    STAMPROFILE     StatXfer;
#endif
} DRVAUDIOSTREAM;
/** Pointer to an extended stream structure. */
typedef DRVAUDIOSTREAM *PDRVAUDIOSTREAM;

/** Value for DRVAUDIOSTREAM::uMagic (Johann Sebastian Bach). */
#define DRVAUDIOSTREAM_MAGIC        UINT32_C(0x16850331)
/** Value for DRVAUDIOSTREAM::uMagic after destruction */
#define DRVAUDIOSTREAM_MAGIC_DEAD   UINT32_C(0x17500728)


/**
 * Audio driver configuration data, tweakable via CFGM.
 */
typedef struct DRVAUDIOCFG
{
    /** PCM properties to use. */
    PDMAUDIOPCMPROPS     Props;
    /** Whether using signed sample data or not.
     *  Needed in order to know whether there is a custom value set in CFGM or not.
     *  By default set to UINT8_MAX if not set to a custom value. */
    uint8_t              uSigned;
    /** Whether swapping endianess of sample data or not.
     *  Needed in order to know whether there is a custom value set in CFGM or not.
     *  By default set to UINT8_MAX if not set to a custom value. */
    uint8_t              uSwapEndian;
    /** Configures the period size (in ms).
     *  This value reflects the time in between each hardware interrupt on the
     *  backend (host) side. */
    uint32_t             uPeriodSizeMs;
    /** Configures the (ring) buffer size (in ms). Often is a multiple of uPeriodMs. */
    uint32_t             uBufferSizeMs;
    /** Configures the pre-buffering size (in ms).
     *  Time needed in buffer before the stream becomes active (pre buffering).
     *  The bigger this value is, the more latency for the stream will occur.
     *  Set to 0 to disable pre-buffering completely.
     *  By default set to UINT32_MAX if not set to a custom value. */
    uint32_t             uPreBufSizeMs;
    /** The driver's debugging configuration. */
    struct
    {
        /** Whether audio debugging is enabled or not. */
        bool             fEnabled;
        /** Where to store the debugging files. */
        char             szPathOut[RTPATH_MAX];
    } Dbg;
} DRVAUDIOCFG;
/** Pointer to tweakable audio configuration. */
typedef DRVAUDIOCFG *PDRVAUDIOCFG;
/** Pointer to const tweakable audio configuration. */
typedef DRVAUDIOCFG const *PCDRVAUDIOCFG;


/**
 * Audio driver instance data.
 *
 * @implements PDMIAUDIOCONNECTOR
 */
typedef struct DRVAUDIO
{
    /** Read/Write critical section for guarding changes to pHostDrvAudio and
     *  BackendCfg during deteach/attach.  Mostly taken in shared mode.
     * @note Locking order: Must be entered after CritSectGlobals.
     * @note Locking order: Must be entered after PDMAUDIOSTREAM::CritSect. */
    RTCRITSECTRW            CritSectHotPlug;
    /** Critical section for protecting:
     *      - LstStreams
     *      - cStreams
     *      - In.fEnabled
     *      - In.cStreamsFree
     *      - Out.fEnabled
     *      - Out.cStreamsFree
     * @note Locking order: Must be entered before PDMAUDIOSTREAM::CritSect.
     * @note Locking order: Must be entered before CritSectHotPlug. */
    RTCRITSECTRW            CritSectGlobals;
    /** List of audio streams (DRVAUDIOSTREAM). */
    RTLISTANCHOR            LstStreams;
    /** Number of streams in the list. */
    size_t                  cStreams;
    struct
    {
        /** Whether this driver's input streams are enabled or not.
         *  This flag overrides all the attached stream statuses. */
        bool                fEnabled;
        /** Max. number of free input streams.
         *  UINT32_MAX for unlimited streams. */
        uint32_t            cStreamsFree;
    } In;
    struct
    {
        /** Whether this driver's output streams are enabled or not.
         *  This flag overrides all the attached stream statuses. */
        bool                fEnabled;
        /** Max. number of free output streams.
         *  UINT32_MAX for unlimited streams. */
        uint32_t            cStreamsFree;
    } Out;

    /** Audio configuration settings retrieved from the backend.
     * The szName field is used for the DriverName config value till we get the
     * authoritative name from the backend (only for logging). */
    PDMAUDIOBACKENDCFG      BackendCfg;
    /** Our audio connector interface. */
    PDMIAUDIOCONNECTOR      IAudioConnector;
    /** Interface used by the host backend. */
    PDMIHOSTAUDIOPORT       IHostAudioPort;
    /** Pointer to the driver instance. */
    PPDMDRVINS              pDrvIns;
    /** Pointer to audio driver below us. */
    PPDMIHOSTAUDIO          pHostDrvAudio;

    /** Request pool if the backend needs it for async stream creation. */
    RTREQPOOL               hReqPool;

#ifdef VBOX_WITH_AUDIO_ENUM
    /** Handle to the timer for delayed re-enumeration of backend devices. */
    TMTIMERHANDLE           hEnumTimer;
    /** Unique name for the the disable-iteration timer.  */
    char                    szEnumTimerName[24];
#endif

    /** Input audio configuration values (static). */
    DRVAUDIOCFG             CfgIn;
    /** Output audio configuration values (static). */
    DRVAUDIOCFG             CfgOut;

    STAMCOUNTER             StatTotalStreamsCreated;
} DRVAUDIO;
/** Pointer to the instance data of an audio driver. */
typedef DRVAUDIO *PDRVAUDIO;
/** Pointer to const instance data of an audio driver. */
typedef DRVAUDIO const *PCDRVAUDIO;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int drvAudioStreamControlInternalBackend(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx, PDMAUDIOSTREAMCMD enmStreamCmd);
static int drvAudioStreamControlInternal(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx, PDMAUDIOSTREAMCMD enmStreamCmd);
static int drvAudioStreamUninitInternal(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx);
#ifdef DRVAUDIO_WITH_STREAM_DESTRUCTION_IN_DISABLED_DIRECTION
static int drvAudioStreamDestroyInternalBackend(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx);
static int drvAudioStreamReInitInternal(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx);
#endif
static uint32_t drvAudioStreamRetainInternal(PDRVAUDIOSTREAM pStreamEx);
static uint32_t drvAudioStreamReleaseInternal(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx, bool fMayDestroy);
static void drvAudioStreamResetInternal(PDRVAUDIOSTREAM pStreamEx);


/** Buffer size for drvAudioStreamStatusToStr.  */
# define DRVAUDIO_STATUS_STR_MAX sizeof("BACKEND_CREATED BACKEND_READY ENABLED PAUSED PENDING_DISABLED NEED_REINIT 0x12345678")

/**
 * Converts an audio stream status to a string.
 *
 * @returns pszDst
 * @param   pszDst      Buffer to convert into, at least minimum size is
 *                      DRVAUDIO_STATUS_STR_MAX.
 * @param   fStatus     Stream status flags to convert.
 */
static const char *drvAudioStreamStatusToStr(char pszDst[DRVAUDIO_STATUS_STR_MAX], uint32_t fStatus)
{
    static const struct
    {
        const char *pszMnemonic;
        uint32_t    cchMnemnonic;
        uint32_t    fFlag;
    } s_aFlags[] =
    {
        { RT_STR_TUPLE("BACKEND_CREATED "),  PDMAUDIOSTREAM_STS_BACKEND_CREATED  },
        { RT_STR_TUPLE("BACKEND_READY "),    PDMAUDIOSTREAM_STS_BACKEND_READY    },
        { RT_STR_TUPLE("ENABLED "),          PDMAUDIOSTREAM_STS_ENABLED          },
        { RT_STR_TUPLE("PAUSED "),           PDMAUDIOSTREAM_STS_PAUSED           },
        { RT_STR_TUPLE("PENDING_DISABLE "),  PDMAUDIOSTREAM_STS_PENDING_DISABLE  },
        { RT_STR_TUPLE("NEED_REINIT "),      PDMAUDIOSTREAM_STS_NEED_REINIT      },
    };
    if (!fStatus)
        strcpy(pszDst, "NONE");
    else
    {
        char *psz = pszDst;
        for (size_t i = 0; i < RT_ELEMENTS(s_aFlags); i++)
            if (fStatus & s_aFlags[i].fFlag)
            {
                memcpy(psz, s_aFlags[i].pszMnemonic, s_aFlags[i].cchMnemnonic);
                psz += s_aFlags[i].cchMnemnonic;
                fStatus &= ~s_aFlags[i].fFlag;
                if (!fStatus)
                    break;
            }
        if (fStatus == 0)
            psz[-1] = '\0';
        else
            psz += RTStrPrintf(psz, DRVAUDIO_STATUS_STR_MAX - (psz - pszDst), "%#x", fStatus);
        Assert((uintptr_t)(psz - pszDst) <= DRVAUDIO_STATUS_STR_MAX);
    }
    return pszDst;
}


/**
 * Get play state name string.
 */
static const char *drvAudioPlayStateName(DRVAUDIOPLAYSTATE enmState)
{
    switch (enmState)
    {
        case DRVAUDIOPLAYSTATE_INVALID:             return "INVALID";
        case DRVAUDIOPLAYSTATE_NOPLAY:              return "NOPLAY";
        case DRVAUDIOPLAYSTATE_PLAY:                return "PLAY";
        case DRVAUDIOPLAYSTATE_PLAY_PREBUF:         return "PLAY_PREBUF";
        case DRVAUDIOPLAYSTATE_PREBUF:              return "PREBUF";
        case DRVAUDIOPLAYSTATE_PREBUF_OVERDUE:      return "PREBUF_OVERDUE";
        case DRVAUDIOPLAYSTATE_PREBUF_SWITCHING:    return "PREBUF_SWITCHING";
        case DRVAUDIOPLAYSTATE_PREBUF_COMMITTING:   return "PREBUF_COMMITTING";
        case DRVAUDIOPLAYSTATE_END:
            break;
    }
    return "BAD";
}

#ifdef LOG_ENABLED
/**
 * Get capture state name string.
 */
static const char *drvAudioCaptureStateName(DRVAUDIOCAPTURESTATE enmState)
{
    switch (enmState)
    {
        case DRVAUDIOCAPTURESTATE_INVALID:          return "INVALID";
        case DRVAUDIOCAPTURESTATE_NO_CAPTURE:       return "NO_CAPTURE";
        case DRVAUDIOCAPTURESTATE_CAPTURING:        return "CAPTURING";
        case DRVAUDIOCAPTURESTATE_PREBUF:           return "PREBUF";
        case DRVAUDIOCAPTURESTATE_END:
            break;
    }
    return "BAD";
}
#endif

/**
 * Checks if the stream status is one that can be read from.
 *
 * @returns @c true if ready to be read from, @c false if not.
 * @param   fStatus     Stream status to evaluate, PDMAUDIOSTREAM_STS_XXX.
 * @note    Not for backend statuses (use PDMAudioStrmStatusBackendCanRead)!
 */
DECLINLINE(bool) PDMAudioStrmStatusCanRead(uint32_t fStatus)
{
    PDMAUDIOSTREAM_STS_ASSERT_VALID(fStatus);
    AssertReturn(!(fStatus & ~PDMAUDIOSTREAM_STS_VALID_MASK), false);
    return (fStatus & (  PDMAUDIOSTREAM_STS_BACKEND_CREATED
                       | PDMAUDIOSTREAM_STS_ENABLED
                       | PDMAUDIOSTREAM_STS_PAUSED
                       | PDMAUDIOSTREAM_STS_NEED_REINIT))
        == (  PDMAUDIOSTREAM_STS_BACKEND_CREATED
            | PDMAUDIOSTREAM_STS_ENABLED);
}

/**
 * Checks if the stream status is one that can be written to.
 *
 * @returns @c true if ready to be written to, @c false if not.
 * @param   fStatus     Stream status to evaluate, PDMAUDIOSTREAM_STS_XXX.
 * @note    Not for backend statuses (use PDMAudioStrmStatusBackendCanWrite)!
 */
DECLINLINE(bool) PDMAudioStrmStatusCanWrite(uint32_t fStatus)
{
    PDMAUDIOSTREAM_STS_ASSERT_VALID(fStatus);
    AssertReturn(!(fStatus & ~PDMAUDIOSTREAM_STS_VALID_MASK), false);
    return (fStatus & (  PDMAUDIOSTREAM_STS_BACKEND_CREATED
                       | PDMAUDIOSTREAM_STS_ENABLED
                       | PDMAUDIOSTREAM_STS_PAUSED
                       | PDMAUDIOSTREAM_STS_PENDING_DISABLE
                       | PDMAUDIOSTREAM_STS_NEED_REINIT))
        == (  PDMAUDIOSTREAM_STS_BACKEND_CREATED
            | PDMAUDIOSTREAM_STS_ENABLED);
}

/**
 * Checks if the stream status is a ready-to-operate one.
 *
 * @returns @c true if ready to operate, @c false if not.
 * @param   fStatus     Stream status to evaluate, PDMAUDIOSTREAM_STS_XXX.
 * @note    Not for backend statuses!
 */
DECLINLINE(bool) PDMAudioStrmStatusIsReady(uint32_t fStatus)
{
    PDMAUDIOSTREAM_STS_ASSERT_VALID(fStatus);
    AssertReturn(!(fStatus & ~PDMAUDIOSTREAM_STS_VALID_MASK), false);
    return (fStatus & (  PDMAUDIOSTREAM_STS_BACKEND_CREATED
                       | PDMAUDIOSTREAM_STS_ENABLED
                       | PDMAUDIOSTREAM_STS_NEED_REINIT))
        == (  PDMAUDIOSTREAM_STS_BACKEND_CREATED
            | PDMAUDIOSTREAM_STS_ENABLED);
}


/**
 * Wrapper around PDMIHOSTAUDIO::pfnStreamGetStatus and checks the result.
 *
 * @returns A PDMHOSTAUDIOSTREAMSTATE value.
 * @param   pThis       Pointer to the DrvAudio instance data.
 * @param   pStreamEx   The stream to get the backend status for.
 */
DECLINLINE(PDMHOSTAUDIOSTREAMSTATE) drvAudioStreamGetBackendState(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx)
{
    if (pThis->pHostDrvAudio)
    {
        AssertPtrReturn(pThis->pHostDrvAudio->pfnStreamGetState, PDMHOSTAUDIOSTREAMSTATE_NOT_WORKING);
        PDMHOSTAUDIOSTREAMSTATE enmState = pThis->pHostDrvAudio->pfnStreamGetState(pThis->pHostDrvAudio, pStreamEx->pBackend);
        Log9Func(("%s: %s\n", pStreamEx->Core.Cfg.szName, PDMHostAudioStreamStateGetName(enmState) ));
        Assert(   enmState > PDMHOSTAUDIOSTREAMSTATE_INVALID
               && enmState < PDMHOSTAUDIOSTREAMSTATE_END
               && (enmState != PDMHOSTAUDIOSTREAMSTATE_DRAINING || pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_OUT));
        return enmState;
    }
    Log9Func(("%s: not-working\n", pStreamEx->Core.Cfg.szName));
    return PDMHOSTAUDIOSTREAMSTATE_NOT_WORKING;
}


/**
 * Worker for drvAudioStreamProcessBackendStateChange that completes draining.
 */
DECLINLINE(void) drvAudioStreamProcessBackendStateChangeWasDraining(PDRVAUDIOSTREAM pStreamEx)
{
    Log(("drvAudioStreamProcessBackendStateChange: Stream '%s': Done draining - disabling stream.\n", pStreamEx->Core.Cfg.szName));
    pStreamEx->fStatus &= ~(PDMAUDIOSTREAM_STS_ENABLED | PDMAUDIOSTREAM_STS_PENDING_DISABLE);
    drvAudioStreamResetInternal(pStreamEx);
}


/**
 * Processes backend state change.
 *
 * @returns the new state value.
 */
static PDMHOSTAUDIOSTREAMSTATE drvAudioStreamProcessBackendStateChange(PDRVAUDIOSTREAM pStreamEx,
                                                                       PDMHOSTAUDIOSTREAMSTATE enmNewState,
                                                                       PDMHOSTAUDIOSTREAMSTATE enmOldState)
{
    PDMAUDIODIR const           enmDir          = pStreamEx->Core.Cfg.enmDir;
#ifdef LOG_ENABLED
    DRVAUDIOPLAYSTATE const     enmPlayState    = enmDir == PDMAUDIODIR_OUT
                                                ? pStreamEx->Out.enmPlayState   : DRVAUDIOPLAYSTATE_INVALID;
    DRVAUDIOCAPTURESTATE const  enmCaptureState = enmDir == PDMAUDIODIR_IN
                                                ? pStreamEx->In.enmCaptureState : DRVAUDIOCAPTURESTATE_INVALID;
#endif
    Assert(enmNewState != enmOldState);
    Assert(enmOldState > PDMHOSTAUDIOSTREAMSTATE_INVALID && enmOldState < PDMHOSTAUDIOSTREAMSTATE_END);
    AssertReturn(enmNewState > PDMHOSTAUDIOSTREAMSTATE_INVALID && enmNewState < PDMHOSTAUDIOSTREAMSTATE_END, enmOldState);

    /*
     * Figure out what happend and how that reflects on the playback state and stuff.
     */
    switch (enmNewState)
    {
        case PDMHOSTAUDIOSTREAMSTATE_INITIALIZING:
            /* Guess we're switching device. Nothing to do because the backend will tell us, right? */
            break;

        case PDMHOSTAUDIOSTREAMSTATE_NOT_WORKING:
        case PDMHOSTAUDIOSTREAMSTATE_INACTIVE:
            /* The stream has stopped working or is inactive.  Switch stop any draining & to noplay mode. */
            if (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_PENDING_DISABLE)
                drvAudioStreamProcessBackendStateChangeWasDraining(pStreamEx);
            if (enmDir == PDMAUDIODIR_OUT)
                pStreamEx->Out.enmPlayState   = DRVAUDIOPLAYSTATE_NOPLAY;
            else
                pStreamEx->In.enmCaptureState = DRVAUDIOCAPTURESTATE_NO_CAPTURE;
            break;

        case PDMHOSTAUDIOSTREAMSTATE_OKAY:
            switch (enmOldState)
            {
                case PDMHOSTAUDIOSTREAMSTATE_INITIALIZING:
                    /* Should be taken care of elsewhere, so do nothing. */
                    break;

                case PDMHOSTAUDIOSTREAMSTATE_NOT_WORKING:
                case PDMHOSTAUDIOSTREAMSTATE_INACTIVE:
                    /* Go back to pre-buffering/playing depending on whether it is enabled
                       or not, resetting the stream state. */
                    drvAudioStreamResetInternal(pStreamEx);
                    break;

                case PDMHOSTAUDIOSTREAMSTATE_DRAINING:
                    /* Complete the draining. May race the iterate code. */
                    if (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_PENDING_DISABLE)
                        drvAudioStreamProcessBackendStateChangeWasDraining(pStreamEx);
                    break;

                /* no default: */
                case PDMHOSTAUDIOSTREAMSTATE_OKAY: /* impossible */
                case PDMHOSTAUDIOSTREAMSTATE_INVALID:
                case PDMHOSTAUDIOSTREAMSTATE_END:
                case PDMHOSTAUDIOSTREAMSTATE_32BIT_HACK:
                    break;
            }
            break;

        case PDMHOSTAUDIOSTREAMSTATE_DRAINING:
            /* We do all we need to do when issuing the DRAIN command. */
            Assert(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_PENDING_DISABLE);
            break;

        /* no default: */
        case PDMHOSTAUDIOSTREAMSTATE_INVALID:
        case PDMHOSTAUDIOSTREAMSTATE_END:
        case PDMHOSTAUDIOSTREAMSTATE_32BIT_HACK:
            break;
    }

    if (enmDir == PDMAUDIODIR_OUT)
        LogFunc(("Output stream '%s': %s/%s -> %s/%s\n", pStreamEx->Core.Cfg.szName,
                 PDMHostAudioStreamStateGetName(enmOldState), drvAudioPlayStateName(enmPlayState),
                 PDMHostAudioStreamStateGetName(enmNewState), drvAudioPlayStateName(pStreamEx->Out.enmPlayState) ));
    else
        LogFunc(("Input stream '%s': %s/%s -> %s/%s\n", pStreamEx->Core.Cfg.szName,
                 PDMHostAudioStreamStateGetName(enmOldState), drvAudioCaptureStateName(enmCaptureState),
                 PDMHostAudioStreamStateGetName(enmNewState), drvAudioCaptureStateName(pStreamEx->In.enmCaptureState) ));

    pStreamEx->enmLastBackendState = enmNewState;
    return enmNewState;
}


/**
 * This gets the backend state and handles changes compared to
 * DRVAUDIOSTREAM::enmLastBackendState (updated).
 *
 * @returns A PDMHOSTAUDIOSTREAMSTATE value.
 * @param   pThis       Pointer to the DrvAudio instance data.
 * @param   pStreamEx   The stream to get the backend status for.
 */
DECLINLINE(PDMHOSTAUDIOSTREAMSTATE) drvAudioStreamGetBackendStateAndProcessChanges(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx)
{
    PDMHOSTAUDIOSTREAMSTATE const enmBackendState = drvAudioStreamGetBackendState(pThis, pStreamEx);
    if (pStreamEx->enmLastBackendState == enmBackendState)
        return enmBackendState;
    return drvAudioStreamProcessBackendStateChange(pStreamEx, enmBackendState, pStreamEx->enmLastBackendState);
}


#ifdef VBOX_WITH_AUDIO_ENUM
/**
 * Enumerates all host audio devices.
 *
 * This functionality might not be implemented by all backends and will return
 * VERR_NOT_SUPPORTED if not being supported.
 *
 * @note Must not hold the driver's critical section!
 *
 * @returns VBox status code.
 * @param   pThis               Driver instance to be called.
 * @param   fLog                Whether to print the enumerated device to the release log or not.
 * @param   pDevEnum            Where to store the device enumeration.
 *
 * @remarks This is currently ONLY used for release logging.
 */
static DECLCALLBACK(int) drvAudioDevicesEnumerateInternal(PDRVAUDIO pThis, bool fLog, PPDMAUDIOHOSTENUM pDevEnum)
{
    RTCritSectRwEnterShared(&pThis->CritSectHotPlug);

    int rc;

    /*
     * If the backend supports it, do a device enumeration.
     */
    if (pThis->pHostDrvAudio->pfnGetDevices)
    {
        PDMAUDIOHOSTENUM DevEnum;
        rc = pThis->pHostDrvAudio->pfnGetDevices(pThis->pHostDrvAudio, &DevEnum);
        if (RT_SUCCESS(rc))
        {
            if (fLog)
            {
                LogRel(("Audio: Found %RU16 devices for driver '%s'\n", DevEnum.cDevices, pThis->BackendCfg.szName));

                PPDMAUDIOHOSTDEV pDev;
                RTListForEach(&DevEnum.LstDevices, pDev, PDMAUDIOHOSTDEV, ListEntry)
                {
                    char szFlags[PDMAUDIOHOSTDEV_MAX_FLAGS_STRING_LEN];
                    LogRel(("Audio: Device '%s':\n"
                            "Audio:   ID              = %s\n"
                            "Audio:   Usage           = %s\n"
                            "Audio:   Flags           = %s\n"
                            "Audio:   Input channels  = %RU8\n"
                            "Audio:   Output channels = %RU8\n",
                            pDev->pszName, pDev->pszId ? pDev->pszId : "",
                            PDMAudioDirGetName(pDev->enmUsage), PDMAudioHostDevFlagsToString(szFlags, pDev->fFlags),
                            pDev->cMaxInputChannels, pDev->cMaxOutputChannels));
                }
            }

            if (pDevEnum)
                rc = PDMAudioHostEnumCopy(pDevEnum, &DevEnum, PDMAUDIODIR_INVALID /*all*/, true /*fOnlyCoreData*/);

            PDMAudioHostEnumDelete(&DevEnum);
        }
        else
        {
            if (fLog)
                LogRel(("Audio: Device enumeration for driver '%s' failed with %Rrc\n", pThis->BackendCfg.szName, rc));
            /* Not fatal. */
        }
    }
    else
    {
        rc = VERR_NOT_SUPPORTED;

        if (fLog)
            LogRel2(("Audio: Host driver '%s' does not support audio device enumeration, skipping\n", pThis->BackendCfg.szName));
    }

    RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    LogFunc(("Returning %Rrc\n", rc));
    return rc;
}
#endif /* VBOX_WITH_AUDIO_ENUM */


/*********************************************************************************************************************************
*   PDMIAUDIOCONNECTOR                                                                                                           *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnEnable}
 */
static DECLCALLBACK(int) drvAudioEnable(PPDMIAUDIOCONNECTOR pInterface, PDMAUDIODIR enmDir, bool fEnable)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    AssertPtr(pThis);
    LogFlowFunc(("enmDir=%s fEnable=%d\n", PDMAudioDirGetName(enmDir), fEnable));

    /*
     * Figure which status flag variable is being updated.
     */
    bool *pfEnabled;
    if (enmDir == PDMAUDIODIR_IN)
        pfEnabled = &pThis->In.fEnabled;
    else if (enmDir == PDMAUDIODIR_OUT)
        pfEnabled = &pThis->Out.fEnabled;
    else
        AssertFailedReturn(VERR_INVALID_PARAMETER);

    /*
     * Grab the driver wide lock and check it.  Ignore call if no change.
     */
    int rc = RTCritSectRwEnterExcl(&pThis->CritSectGlobals);
    AssertRCReturn(rc, rc);

    if (fEnable != *pfEnabled)
    {
        LogRel(("Audio: %s %s for driver '%s'\n",
                fEnable ? "Enabling" : "Disabling", PDMAudioDirGetName(enmDir), pThis->BackendCfg.szName));

        /*
         * When enabling, we must update flag before calling drvAudioStreamControlInternalBackend.
         */
        if (fEnable)
            *pfEnabled = true;

        /*
         * Update the backend status for the streams in the given direction.
         *
         * The pThis->Out.fEnable / pThis->In.fEnable status flags only reflect in the
         * direction of the backend, drivers and devices above us in the chain does not
         * know about this.  When disabled playback goes to /dev/null and we capture
         * only silence.  This means pStreamEx->fStatus holds the nominal status
         * and we'll use it to restore the operation.  (See also @bugref{9882}.)
         *
         * The DRVAUDIO_WITH_STREAM_DESTRUCTION_IN_DISABLED_DIRECTION build time define
         * controls how this is implemented.
         */
        PDRVAUDIOSTREAM pStreamEx;
        RTListForEach(&pThis->LstStreams, pStreamEx, DRVAUDIOSTREAM, ListEntry)
        {
            /** @todo duplex streams   */
            if (pStreamEx->Core.Cfg.enmDir == enmDir)
            {
                RTCritSectEnter(&pStreamEx->Core.CritSect);

                /*
                 * When (re-)enabling a stream, clear the disabled warning bit again.
                 */
                if (fEnable)
                    pStreamEx->Core.fWarningsShown &= ~PDMAUDIOSTREAM_WARN_FLAGS_DISABLED;

#ifdef DRVAUDIO_WITH_STREAM_DESTRUCTION_IN_DISABLED_DIRECTION
                /*
                 * When enabling, we must make sure the stream has been created with the
                 * backend before enabling and maybe pausing it. When disabling we must
                 * destroy the stream. Paused includes enabled, as does draining, but we
                 * only want the former.
                 */
#else
                /*
                 * We don't need to do anything unless the stream is enabled.
                 * Paused includes enabled, as does draining, but we only want the former.
                 */
#endif
                uint32_t const fStatus = pStreamEx->fStatus;

#ifndef DRVAUDIO_WITH_STREAM_DESTRUCTION_IN_DISABLED_DIRECTION
                if (fStatus & PDMAUDIOSTREAM_STS_ENABLED)
#endif
                {
                    const char *pszOperation;
                    int         rc2;
                    if (fEnable)
                    {
                        if (!(fStatus & PDMAUDIOSTREAM_STS_PENDING_DISABLE))
                        {
#ifdef DRVAUDIO_WITH_STREAM_DESTRUCTION_IN_DISABLED_DIRECTION
                            /* The backend shouldn't have been created, so do that before enabling
                               and possibly pausing the stream. */
                            if (!(fStatus & PDMAUDIOSTREAM_STS_BACKEND_CREATED))
                                rc2 = drvAudioStreamReInitInternal(pThis, pStreamEx);
                            else
                                rc2 = VINF_SUCCESS;
                            pszOperation = "re-init";
                            if (RT_SUCCESS(rc2) && (fStatus & PDMAUDIOSTREAM_STS_ENABLED))
#endif
                            {
                                /** @todo r=bird: We need to redo pre-buffering OR switch to
                                 *        DRVAUDIOPLAYSTATE_PREBUF_SWITCHING playback mode when disabling
                                 *        output streams.  The former is preferred if associated with
                                 *        reporting the stream as INACTIVE. */
                                rc2 = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_ENABLE);
                                pszOperation = "enable";
                                if (RT_SUCCESS(rc2) && (fStatus & PDMAUDIOSTREAM_STS_PAUSED))
                                {
                                    rc2 = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_PAUSE);
                                    pszOperation = "pause";
                                }
                            }
                        }
                        else
                        {
                            rc2 = VINF_SUCCESS;
                            pszOperation = NULL;
                        }
                    }
                    else
                    {
#ifdef DRVAUDIO_WITH_STREAM_DESTRUCTION_IN_DISABLED_DIRECTION
                        if (fStatus & PDMAUDIOSTREAM_STS_BACKEND_CREATED)
                            rc2 = drvAudioStreamDestroyInternalBackend(pThis, pStreamEx);
                        else
                            rc2 = VINF_SUCCESS;
                        pszOperation = "destroy";
#else
                        rc2 = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_DISABLE);
                        pszOperation = "disable";
#endif
                    }
                    if (RT_FAILURE(rc2))
                    {
                        LogRel(("Audio: Failed to %s %s stream '%s': %Rrc\n",
                                pszOperation, PDMAudioDirGetName(enmDir), pStreamEx->Core.Cfg.szName, rc2));
                        if (RT_SUCCESS(rc))
                            rc = rc2;  /** @todo r=bird: This isn't entirely helpful to the caller since we'll update the status
                                        * regardless of the status code we return.  And anyway, there is nothing that can be done
                                        * about individual stream by the caller... */
                    }
                }

                RTCritSectLeave(&pStreamEx->Core.CritSect);
            }
        }

        /*
         * When disabling, we must update the status flag after the
         * drvAudioStreamControlInternalBackend(DISABLE) calls.
         */
        *pfEnabled = fEnable;
    }

    RTCritSectRwLeaveExcl(&pThis->CritSectGlobals);
    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnIsEnabled}
 */
static DECLCALLBACK(bool) drvAudioIsEnabled(PPDMIAUDIOCONNECTOR pInterface, PDMAUDIODIR enmDir)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    AssertPtr(pThis);
    int rc = RTCritSectRwEnterShared(&pThis->CritSectGlobals);
    AssertRCReturn(rc, false);

    bool fEnabled;
    if (enmDir == PDMAUDIODIR_IN)
        fEnabled = pThis->In.fEnabled;
    else if (enmDir == PDMAUDIODIR_OUT)
        fEnabled = pThis->Out.fEnabled;
    else
        AssertFailedStmt(fEnabled = false);

    RTCritSectRwLeaveShared(&pThis->CritSectGlobals);
    return fEnabled;
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnGetConfig}
 */
static DECLCALLBACK(int) drvAudioGetConfig(PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOBACKENDCFG pCfg)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    AssertPtr(pThis);
    AssertPtrReturn(pCfg, VERR_INVALID_POINTER);
    int rc = RTCritSectRwEnterShared(&pThis->CritSectHotPlug);
    AssertRCReturn(rc, rc);

    if (pThis->pHostDrvAudio)
        rc = pThis->pHostDrvAudio->pfnGetConfig(pThis->pHostDrvAudio, pCfg);
    else
        rc = VERR_PDM_NO_ATTACHED_DRIVER;

    RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnGetStatus}
 */
static DECLCALLBACK(PDMAUDIOBACKENDSTS) drvAudioGetStatus(PPDMIAUDIOCONNECTOR pInterface, PDMAUDIODIR enmDir)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    AssertPtr(pThis);
    int rc = RTCritSectRwEnterShared(&pThis->CritSectHotPlug);
    AssertRCReturn(rc, PDMAUDIOBACKENDSTS_UNKNOWN);

    PDMAUDIOBACKENDSTS fBackendStatus;
    if (pThis->pHostDrvAudio)
    {
        if (pThis->pHostDrvAudio->pfnGetStatus)
            fBackendStatus = pThis->pHostDrvAudio->pfnGetStatus(pThis->pHostDrvAudio, enmDir);
        else
            fBackendStatus = PDMAUDIOBACKENDSTS_UNKNOWN;
    }
    else
        fBackendStatus = PDMAUDIOBACKENDSTS_NOT_ATTACHED;

    RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    LogFlowFunc(("LEAVE - %#x\n", fBackendStatus));
    return fBackendStatus;
}


/**
 * Frees an audio stream and its allocated resources.
 *
 * @param   pStreamEx   Audio stream to free. After this call the pointer will
 *                      not be valid anymore.
 */
static void drvAudioStreamFree(PDRVAUDIOSTREAM pStreamEx)
{
    if (pStreamEx)
    {
        LogFunc(("[%s]\n", pStreamEx->Core.Cfg.szName));
        Assert(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC);
        Assert(pStreamEx->uMagic      == DRVAUDIOSTREAM_MAGIC);

        pStreamEx->Core.uMagic    = ~PDMAUDIOSTREAM_MAGIC;
        pStreamEx->pBackend       = NULL;
        pStreamEx->uMagic         = DRVAUDIOSTREAM_MAGIC_DEAD;

        RTCritSectDelete(&pStreamEx->Core.CritSect);

        RTMemFree(pStreamEx);
    }
}


/**
 * Adjusts the request stream configuration, applying our settings.
 *
 * This also does some basic validations.
 *
 * Used by both the stream creation and stream configuration hinting code.
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to the DrvAudio instance data.
 * @param   pCfg        The configuration that should be adjusted.
 * @param   pszName     Stream name to use when logging warnings and errors.
 */
static int drvAudioStreamAdjustConfig(PCDRVAUDIO pThis, PPDMAUDIOSTREAMCFG pCfg, const char *pszName)
{
    /* Get the right configuration for the stream to be created. */
    PCDRVAUDIOCFG pDrvCfg = pCfg->enmDir == PDMAUDIODIR_IN ? &pThis->CfgIn: &pThis->CfgOut;

    /* Fill in the tweakable parameters into the requested host configuration.
     * All parameters in principle can be changed and returned by the backend via the acquired configuration. */

    /*
     * PCM
     */
    if (PDMAudioPropsSampleSize(&pDrvCfg->Props) != 0) /* Anything set via custom extra-data? */
    {
        PDMAudioPropsSetSampleSize(&pCfg->Props, PDMAudioPropsSampleSize(&pDrvCfg->Props));
        LogRel2(("Audio: Using custom sample size of %RU8 bytes for stream '%s'\n",
                 PDMAudioPropsSampleSize(&pCfg->Props), pszName));
    }

    if (pDrvCfg->Props.uHz) /* Anything set via custom extra-data? */
    {
        pCfg->Props.uHz = pDrvCfg->Props.uHz;
        LogRel2(("Audio: Using custom Hz rate %RU32 for stream '%s'\n", pCfg->Props.uHz, pszName));
    }

    if (pDrvCfg->uSigned != UINT8_MAX) /* Anything set via custom extra-data? */
    {
        pCfg->Props.fSigned = RT_BOOL(pDrvCfg->uSigned);
        LogRel2(("Audio: Using custom %s sample format for stream '%s'\n",
                 pCfg->Props.fSigned ? "signed" : "unsigned", pszName));
    }

    if (pDrvCfg->uSwapEndian != UINT8_MAX) /* Anything set via custom extra-data? */
    {
        pCfg->Props.fSwapEndian = RT_BOOL(pDrvCfg->uSwapEndian);
        LogRel2(("Audio: Using custom %s endianess for samples of stream '%s'\n",
                 pCfg->Props.fSwapEndian ? "swapped" : "original", pszName));
    }

    if (PDMAudioPropsChannels(&pDrvCfg->Props) != 0) /* Anything set via custom extra-data? */
    {
        PDMAudioPropsSetChannels(&pCfg->Props, PDMAudioPropsChannels(&pDrvCfg->Props));
        LogRel2(("Audio: Using custom %RU8 channel(s) for stream '%s'\n", PDMAudioPropsChannels(&pDrvCfg->Props), pszName));
    }

    /* Validate PCM properties. */
    if (!AudioHlpPcmPropsAreValidAndSupported(&pCfg->Props))
    {
        LogRel(("Audio: Invalid custom PCM properties set for stream '%s', cannot create stream\n", pszName));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Buffer size
     */
    const char *pszWhat = "device-specific";
    if (pDrvCfg->uBufferSizeMs)
    {
        pCfg->Backend.cFramesBufferSize = PDMAudioPropsMilliToFrames(&pCfg->Props, pDrvCfg->uBufferSizeMs);
        pszWhat = "custom";
    }

    if (!pCfg->Backend.cFramesBufferSize) /* Set default buffer size if nothing explicitly is set. */
    {
        pCfg->Backend.cFramesBufferSize = PDMAudioPropsMilliToFrames(&pCfg->Props, 300 /*ms*/);
        pszWhat = "default";
    }

    LogRel2(("Audio: Using %s buffer size %RU64 ms / %RU32 frames for stream '%s'\n",
             pszWhat, PDMAudioPropsFramesToMilli(&pCfg->Props, pCfg->Backend.cFramesBufferSize),
             pCfg->Backend.cFramesBufferSize, pszName));

    /*
     * Period size
     */
    pszWhat = "device-specific";
    if (pDrvCfg->uPeriodSizeMs)
    {
        pCfg->Backend.cFramesPeriod = PDMAudioPropsMilliToFrames(&pCfg->Props, pDrvCfg->uPeriodSizeMs);
        pszWhat = "custom";
    }

    if (!pCfg->Backend.cFramesPeriod) /* Set default period size if nothing explicitly is set. */
    {
        pCfg->Backend.cFramesPeriod = pCfg->Backend.cFramesBufferSize / 4;
        pszWhat = "default";
    }

    if (pCfg->Backend.cFramesPeriod >= pCfg->Backend.cFramesBufferSize / 2)
    {
        LogRel(("Audio: Warning! Stream '%s': The stream period size (%RU64ms, %s) cannot be more than half the buffer size (%RU64ms)!\n",
                pszName, PDMAudioPropsFramesToMilli(&pCfg->Props, pCfg->Backend.cFramesPeriod), pszWhat,
                PDMAudioPropsFramesToMilli(&pCfg->Props, pCfg->Backend.cFramesBufferSize)));
        pCfg->Backend.cFramesPeriod = pCfg->Backend.cFramesBufferSize / 2;
    }

    LogRel2(("Audio: Using %s period size %RU64 ms / %RU32 frames for stream '%s'\n",
             pszWhat, PDMAudioPropsFramesToMilli(&pCfg->Props, pCfg->Backend.cFramesPeriod),
             pCfg->Backend.cFramesPeriod, pszName));

    /*
     * Pre-buffering size
     */
    pszWhat = "device-specific";
    if (pDrvCfg->uPreBufSizeMs != UINT32_MAX) /* Anything set via global / per-VM extra-data? */
    {
        pCfg->Backend.cFramesPreBuffering = PDMAudioPropsMilliToFrames(&pCfg->Props, pDrvCfg->uPreBufSizeMs);
        pszWhat = "custom";
    }
    else /* No, then either use the default or device-specific settings (if any). */
    {
        if (pCfg->Backend.cFramesPreBuffering == UINT32_MAX) /* Set default pre-buffering size if nothing explicitly is set. */
        {
            /* Pre-buffer 50% for both output & input. Capping both at 200ms.
               The 50% reasoning being that we need to have sufficient slack space
               in both directions as the guest DMA timer might be delayed by host
               scheduling as well as sped up afterwards because of TM catch-up. */
            uint32_t const cFramesMax = PDMAudioPropsMilliToFrames(&pCfg->Props, 200);
            pCfg->Backend.cFramesPreBuffering = pCfg->Backend.cFramesBufferSize / 2;
            pCfg->Backend.cFramesPreBuffering = RT_MIN(pCfg->Backend.cFramesPreBuffering, cFramesMax);
            pszWhat = "default";
        }
    }

    if (pCfg->Backend.cFramesPreBuffering >= pCfg->Backend.cFramesBufferSize)
    {
        LogRel(("Audio: Warning! Stream '%s': Pre-buffering (%RU64ms, %s) cannot equal or exceed the buffer size (%RU64ms)!\n",
                pszName,  PDMAudioPropsFramesToMilli(&pCfg->Props, pCfg->Backend.cFramesBufferSize), pszWhat,
                PDMAudioPropsFramesToMilli(&pCfg->Props, pCfg->Backend.cFramesPreBuffering) ));
        pCfg->Backend.cFramesPreBuffering = pCfg->Backend.cFramesBufferSize - 1;
    }

    LogRel2(("Audio: Using %s pre-buffering size %RU64 ms / %RU32 frames for stream '%s'\n",
             pszWhat, PDMAudioPropsFramesToMilli(&pCfg->Props, pCfg->Backend.cFramesPreBuffering),
             pCfg->Backend.cFramesPreBuffering, pszName));

    return VINF_SUCCESS;
}


/**
 * Worker thread function for drvAudioStreamConfigHint that's used when
 * PDMAUDIOBACKEND_F_ASYNC_HINT is in effect.
 */
static DECLCALLBACK(void) drvAudioStreamConfigHintWorker(PDRVAUDIO pThis, PPDMAUDIOSTREAMCFG pCfg)
{
    LogFlowFunc(("pThis=%p pCfg=%p\n", pThis, pCfg));
    AssertPtrReturnVoid(pCfg);
    int rc = RTCritSectRwEnterShared(&pThis->CritSectHotPlug);
    AssertRCReturnVoid(rc);

    PPDMIHOSTAUDIO const pHostDrvAudio = pThis->pHostDrvAudio;
    if (pHostDrvAudio)
    {
        AssertPtr(pHostDrvAudio->pfnStreamConfigHint);
        if (pHostDrvAudio->pfnStreamConfigHint)
            pHostDrvAudio->pfnStreamConfigHint(pHostDrvAudio, pCfg);
    }
    PDMAudioStrmCfgFree(pCfg);

    RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    LogFlowFunc(("returns\n"));
}


/**
 * Checks whether a given stream direction is enabled (permitted) or not.
 *
 * Currently there are only per-direction enabling/disabling of audio streams.
 * This lets a user disabling input so it an untrusted VM cannot listen in
 * without the user explicitly allowing it, or disable output so it won't
 * disturb your and cannot communicate with other VMs or machines
 *
 * See @bugref{9882}.
 *
 * @retval  true if the stream configuration is enabled/allowed.
 * @retval  false if not permitted.
 * @param   pThis   Pointer to the DrvAudio instance data.
 * @param   enmDir  The stream direction to check.
 */
DECLINLINE(bool) drvAudioStreamIsDirectionEnabled(PDRVAUDIO pThis, PDMAUDIODIR enmDir)
{
    switch (enmDir)
    {
        case PDMAUDIODIR_IN:
            return pThis->In.fEnabled;
        case PDMAUDIODIR_OUT:
            return pThis->Out.fEnabled;
        case PDMAUDIODIR_DUPLEX:
            return pThis->Out.fEnabled && pThis->In.fEnabled;
        default:
            AssertFailedReturn(false);
    }
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnStreamConfigHint}
 */
static DECLCALLBACK(void) drvAudioStreamConfigHint(PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAMCFG pCfg)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    AssertReturnVoid(pCfg->enmDir == PDMAUDIODIR_IN || pCfg->enmDir == PDMAUDIODIR_OUT);

    int rc = RTCritSectRwEnterShared(&pThis->CritSectHotPlug);
    AssertRCReturnVoid(rc);

    /*
     * Don't do anything unless the backend has a pfnStreamConfigHint method
     * and the direction is currently enabled.
     */
    if (   pThis->pHostDrvAudio
        && pThis->pHostDrvAudio->pfnStreamConfigHint)
    {
        if (drvAudioStreamIsDirectionEnabled(pThis, pCfg->enmDir))
        {
            /*
             * Adjust the configuration (applying out settings) then call the backend driver.
             */
            rc = drvAudioStreamAdjustConfig(pThis, pCfg, pCfg->szName);
            AssertLogRelRC(rc);
            if (RT_SUCCESS(rc))
            {
                rc = VERR_CALLBACK_RETURN;
                if (pThis->BackendCfg.fFlags & PDMAUDIOBACKEND_F_ASYNC_HINT)
                {
                    PPDMAUDIOSTREAMCFG pDupCfg = PDMAudioStrmCfgDup(pCfg);
                    if (pDupCfg)
                    {
                        rc = RTReqPoolCallVoidNoWait(pThis->hReqPool, (PFNRT)drvAudioStreamConfigHintWorker, 2, pThis, pDupCfg);
                        if (RT_SUCCESS(rc))
                            LogFlowFunc(("Asynchronous call running on worker thread.\n"));
                        else
                            PDMAudioStrmCfgFree(pDupCfg);
                    }
                }
                if (RT_FAILURE_NP(rc))
                {
                    LogFlowFunc(("Doing synchronous call...\n"));
                    pThis->pHostDrvAudio->pfnStreamConfigHint(pThis->pHostDrvAudio, pCfg);
                }
            }
        }
        else
            LogFunc(("Ignoring hint because direction is not currently enabled\n"));
    }
    else
        LogFlowFunc(("Ignoring hint because backend has no pfnStreamConfigHint method.\n"));

    RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
}


/**
 * Common worker for synchronizing the ENABLED and PAUSED status bits with the
 * backend after it becomes ready.
 *
 * Used by async init and re-init.
 *
 * @note Is sometimes called w/o having entered DRVAUDIO::CritSectHotPlug.
 *       Caller must however own the stream critsect.
 */
static int drvAudioStreamUpdateBackendOnStatus(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx, const char *pszWhen)
{
    int rc = VINF_SUCCESS;
    if (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_ENABLED)
    {
        rc = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_ENABLE);
        if (RT_SUCCESS(rc))
        {
            if (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_PAUSED)
            {
                rc = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_PAUSE);
                if (RT_FAILURE(rc))
                    LogRelMax(64, ("Audio: Failed to pause stream '%s' after %s: %Rrc\n", pStreamEx->Core.Cfg.szName, pszWhen, rc));
            }
        }
        else
            LogRelMax(64, ("Audio: Failed to enable stream '%s' after %s: %Rrc\n", pStreamEx->Core.Cfg.szName, pszWhen, rc));
    }
    return rc;
}


/**
 * For performing PDMIHOSTAUDIO::pfnStreamInitAsync on a worker thread.
 *
 * @param   pThis       Pointer to the DrvAudio instance data.
 * @param   pStreamEx   The stream.  One reference for us to release.
 */
static DECLCALLBACK(void) drvAudioStreamInitAsync(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx)
{
    LogFlow(("pThis=%p pStreamEx=%p (%s)\n", pThis, pStreamEx, pStreamEx->Core.Cfg.szName));

    int rc = RTCritSectRwEnterShared(&pThis->CritSectHotPlug);
    AssertRCReturnVoid(rc);

    /*
     * Do the init job.
     */
    bool           fDestroyed;
    PPDMIHOSTAUDIO pIHostDrvAudio = pThis->pHostDrvAudio;
    AssertPtr(pIHostDrvAudio);
    if (pIHostDrvAudio && pIHostDrvAudio->pfnStreamInitAsync)
    {
        fDestroyed = pStreamEx->cRefs <= 1;
        rc = pIHostDrvAudio->pfnStreamInitAsync(pIHostDrvAudio, pStreamEx->pBackend, fDestroyed);
        LogFlow(("pfnStreamInitAsync returns %Rrc (on %p, fDestroyed=%d)\n", rc, pStreamEx, fDestroyed));
    }
    else
    {
        fDestroyed = true;
        rc = VERR_AUDIO_STREAM_COULD_NOT_CREATE;
    }

    RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    RTCritSectEnter(&pStreamEx->Core.CritSect);

    /*
     * On success, update the backend on the stream status and mark it ready for business.
     */
    if (RT_SUCCESS(rc) && !fDestroyed)
    {
        RTCritSectRwEnterShared(&pThis->CritSectHotPlug);

        /*
         * Update the backend state.
         */
        pStreamEx->fStatus |= PDMAUDIOSTREAM_STS_BACKEND_READY; /* before the backend control call! */

        drvAudioStreamUpdateBackendOnStatus(pThis, pStreamEx, "asynchronous initialization completed");

        /*
         * Modify the play state if output stream.
         */
        if (pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_OUT)
        {
            DRVAUDIOPLAYSTATE const enmPlayState = pStreamEx->Out.enmPlayState;
            switch (enmPlayState)
            {
                case DRVAUDIOPLAYSTATE_PREBUF:
                case DRVAUDIOPLAYSTATE_PREBUF_SWITCHING:
                    break;
                case DRVAUDIOPLAYSTATE_PREBUF_OVERDUE:
                    pStreamEx->Out.enmPlayState = DRVAUDIOPLAYSTATE_PREBUF_COMMITTING;
                    break;
                case DRVAUDIOPLAYSTATE_NOPLAY:
                    pStreamEx->Out.enmPlayState = DRVAUDIOPLAYSTATE_PREBUF;
                    break;
                case DRVAUDIOPLAYSTATE_PLAY:
                case DRVAUDIOPLAYSTATE_PREBUF_COMMITTING:
                    break; /* possible race here, so don't assert. */
                case DRVAUDIOPLAYSTATE_PLAY_PREBUF:
                    AssertFailedBreak();
                /* no default */
                case DRVAUDIOPLAYSTATE_END:
                case DRVAUDIOPLAYSTATE_INVALID:
                    break;
            }
            LogFunc(("enmPlayState: %s -> %s\n", drvAudioPlayStateName(enmPlayState),
                     drvAudioPlayStateName(pStreamEx->Out.enmPlayState) ));
        }

        /*
         * Update the last backend state.
         */
        pStreamEx->enmLastBackendState = drvAudioStreamGetBackendState(pThis, pStreamEx);

        RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    }
    /*
     * Don't quite know what to do on failure...
     */
    else if (!fDestroyed)
    {
        LogRelMax(64, ("Audio: Failed to initialize stream '%s': %Rrc\n", pStreamEx->Core.Cfg.szName, rc));
    }

    /*
     * Release the request handle, must be done while inside the critical section.
     */
    if (pStreamEx->hReqInitAsync != NIL_RTREQ)
    {
        LogFlowFunc(("Releasing hReqInitAsync=%p\n", pStreamEx->hReqInitAsync));
        RTReqRelease(pStreamEx->hReqInitAsync);
        pStreamEx->hReqInitAsync = NIL_RTREQ;
    }

    RTCritSectLeave(&pStreamEx->Core.CritSect);

    /*
     * Release our stream reference.
     */
    uint32_t const cRefs = drvAudioStreamReleaseInternal(pThis, pStreamEx, true /*fMayDestroy*/);
    LogFlowFunc(("returns (fDestroyed=%d, cRefs=%u)\n", fDestroyed, cRefs)); RT_NOREF(cRefs);
}


/**
 * Worker for drvAudioStreamInitInternal and drvAudioStreamReInitInternal that
 * creates the backend (host driver) side of an audio stream.
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to driver instance.
 * @param   pStreamEx   Stream to create backend for.  The Core.Cfg field
 *                      contains the requested configuration when we're called
 *                      and the actual configuration when successfully
 *                      returning.
 *
 * @note    Configuration precedence for requested audio stream configuration (first has highest priority, if set):
 *          - per global extra-data
 *          - per-VM extra-data
 *          - requested configuration (by pCfgReq)
 *          - default value
 */
static int drvAudioStreamCreateInternalBackend(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx)
{
    AssertMsg((pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_CREATED) == 0,
              ("Stream '%s' already initialized in backend\n", pStreamEx->Core.Cfg.szName));

#ifdef DRVAUDIO_WITH_STREAM_DESTRUCTION_IN_DISABLED_DIRECTION
    /*
     * Check if the stream direction is enabled (permitted).
     */
    if (!drvAudioStreamIsDirectionEnabled(pThis, pStreamEx->Core.Cfg.enmDir))
    {
        LogFunc(("Stream direction is disbled, returning w/o doing anything\n"));
        return VINF_SUCCESS;
    }
#endif

    /*
     * Adjust the stream config, applying defaults and any overriding settings.
     */
    int rc = drvAudioStreamAdjustConfig(pThis, &pStreamEx->Core.Cfg, pStreamEx->Core.Cfg.szName);
    if (RT_FAILURE(rc))
        return rc;
    PDMAUDIOSTREAMCFG const CfgReq = pStreamEx->Core.Cfg;

    /*
     * Call the host driver to create the stream.
     */
    RTCritSectRwEnterShared(&pThis->CritSectHotPlug);

    AssertLogRelMsgStmt(RT_VALID_PTR(pThis->pHostDrvAudio),
                        ("Audio: %p\n", pThis->pHostDrvAudio), rc = VERR_PDM_NO_ATTACHED_DRIVER);
    if (RT_SUCCESS(rc))
        AssertLogRelMsgStmt(pStreamEx->Core.cbBackend == pThis->BackendCfg.cbStream,
                            ("Audio: Backend changed? cbBackend changed from %#x to %#x\n",
                             pStreamEx->Core.cbBackend, pThis->BackendCfg.cbStream),
                            rc = VERR_STATE_CHANGED);
    if (RT_SUCCESS(rc))
        rc = pThis->pHostDrvAudio->pfnStreamCreate(pThis->pHostDrvAudio, pStreamEx->pBackend, &CfgReq, &pStreamEx->Core.Cfg);
    if (RT_SUCCESS(rc))
    {
        pStreamEx->enmLastBackendState = drvAudioStreamGetBackendState(pThis, pStreamEx);

        RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);

        AssertLogRelReturn(pStreamEx->pBackend->uMagic  == PDMAUDIOBACKENDSTREAM_MAGIC, VERR_INTERNAL_ERROR_3);
        AssertLogRelReturn(pStreamEx->pBackend->pStream == &pStreamEx->Core, VERR_INTERNAL_ERROR_3);

        /* Must set the backend-initialized flag now or the backend won't be
           destroyed (this used to be done at the end of this function, with
           several possible early return paths before it). */
        pStreamEx->fStatus |= PDMAUDIOSTREAM_STS_BACKEND_CREATED;
    }
    else
    {
        RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
        if (rc == VERR_NOT_SUPPORTED)
            LogRel2(("Audio: Creating stream '%s' in backend not supported\n", pStreamEx->Core.Cfg.szName));
        else if (rc == VERR_AUDIO_STREAM_COULD_NOT_CREATE)
            LogRel2(("Audio: Stream '%s' could not be created in backend because of missing hardware / drivers\n",
                     pStreamEx->Core.Cfg.szName));
        else
            LogRel(("Audio: Creating stream '%s' in backend failed with %Rrc\n", pStreamEx->Core.Cfg.szName, rc));
        return rc;
    }

    /* Remember if we need to call pfnStreamInitAsync. */
    pStreamEx->fNeedAsyncInit = rc == VINF_AUDIO_STREAM_ASYNC_INIT_NEEDED;
    AssertStmt(rc != VINF_AUDIO_STREAM_ASYNC_INIT_NEEDED || pThis->pHostDrvAudio->pfnStreamInitAsync != NULL,
               pStreamEx->fNeedAsyncInit = false);
    AssertMsg(   rc != VINF_AUDIO_STREAM_ASYNC_INIT_NEEDED
              || pStreamEx->enmLastBackendState == PDMHOSTAUDIOSTREAMSTATE_INITIALIZING,
             ("rc=%Rrc %s\n", rc, PDMHostAudioStreamStateGetName(pStreamEx->enmLastBackendState)));

    PPDMAUDIOSTREAMCFG const pCfgAcq = &pStreamEx->Core.Cfg;

    /*
     * Validate acquired configuration.
     */
    char szTmp[PDMAUDIOPROPSTOSTRING_MAX];
    LogFunc(("Backend returned: %s\n", PDMAudioStrmCfgToString(pCfgAcq, szTmp, sizeof(szTmp)) ));
    AssertLogRelMsgReturn(AudioHlpStreamCfgIsValid(pCfgAcq),
                          ("Audio: Creating stream '%s' returned an invalid backend configuration (%s), skipping\n",
                           pCfgAcq->szName, PDMAudioPropsToString(&pCfgAcq->Props, szTmp, sizeof(szTmp))),
                          VERR_INVALID_PARAMETER);

    /* Let the user know that the backend changed one of the values requested above. */
    if (pCfgAcq->Backend.cFramesBufferSize != CfgReq.Backend.cFramesBufferSize)
        LogRel2(("Audio: Backend changed buffer size from %RU64ms (%RU32 frames) to %RU64ms (%RU32 frames)\n",
                 PDMAudioPropsFramesToMilli(&CfgReq.Props, CfgReq.Backend.cFramesBufferSize), CfgReq.Backend.cFramesBufferSize,
                 PDMAudioPropsFramesToMilli(&pCfgAcq->Props, pCfgAcq->Backend.cFramesBufferSize), pCfgAcq->Backend.cFramesBufferSize));

    if (pCfgAcq->Backend.cFramesPeriod != CfgReq.Backend.cFramesPeriod)
        LogRel2(("Audio: Backend changed period size from %RU64ms (%RU32 frames) to %RU64ms (%RU32 frames)\n",
                 PDMAudioPropsFramesToMilli(&CfgReq.Props, CfgReq.Backend.cFramesPeriod), CfgReq.Backend.cFramesPeriod,
                 PDMAudioPropsFramesToMilli(&pCfgAcq->Props, pCfgAcq->Backend.cFramesPeriod), pCfgAcq->Backend.cFramesPeriod));

    /* Was pre-buffering requested, but the acquired configuration from the backend told us something else? */
    if (CfgReq.Backend.cFramesPreBuffering)
    {
        if (pCfgAcq->Backend.cFramesPreBuffering != CfgReq.Backend.cFramesPreBuffering)
            LogRel2(("Audio: Backend changed pre-buffering size from %RU64ms (%RU32 frames) to %RU64ms (%RU32 frames)\n",
                     PDMAudioPropsFramesToMilli(&CfgReq.Props, CfgReq.Backend.cFramesPreBuffering), CfgReq.Backend.cFramesPreBuffering,
                     PDMAudioPropsFramesToMilli(&pCfgAcq->Props, pCfgAcq->Backend.cFramesPreBuffering), pCfgAcq->Backend.cFramesPreBuffering));

        if (pCfgAcq->Backend.cFramesPreBuffering > pCfgAcq->Backend.cFramesBufferSize)
        {
            pCfgAcq->Backend.cFramesPreBuffering = pCfgAcq->Backend.cFramesBufferSize;
            LogRel2(("Audio: Pre-buffering size bigger than buffer size for stream '%s', adjusting to %RU64ms (%RU32 frames)\n", pCfgAcq->szName,
                     PDMAudioPropsFramesToMilli(&pCfgAcq->Props, pCfgAcq->Backend.cFramesPreBuffering), pCfgAcq->Backend.cFramesPreBuffering));
        }
    }
    else /* Was the pre-buffering requested as being disabeld? Tell the users. */
    {
        LogRel2(("Audio: Pre-buffering is disabled for stream '%s'\n", pCfgAcq->szName));
        pCfgAcq->Backend.cFramesPreBuffering = 0;
    }

    /*
     * Check if the backend did return sane values and correct if necessary.
     */
    uint32_t const cFramesPreBufferingMax = pCfgAcq->Backend.cFramesBufferSize - RT_MIN(16, pCfgAcq->Backend.cFramesBufferSize);
    if (pCfgAcq->Backend.cFramesPreBuffering > cFramesPreBufferingMax)
    {
        LogRel2(("Audio: Warning! Pre-buffering size of %RU32 frames for stream '%s' is too close to or larger than the %RU32 frames buffer size, reducing it to %RU32 frames!\n",
                 pCfgAcq->Backend.cFramesPreBuffering, pCfgAcq->szName, pCfgAcq->Backend.cFramesBufferSize, cFramesPreBufferingMax));
        AssertMsgFailed(("cFramesPreBuffering=%#x vs cFramesPreBufferingMax=%#x\n", pCfgAcq->Backend.cFramesPreBuffering, cFramesPreBufferingMax));
        pCfgAcq->Backend.cFramesPreBuffering = cFramesPreBufferingMax;
    }

    if (pCfgAcq->Backend.cFramesPeriod > pCfgAcq->Backend.cFramesBufferSize)
    {
        LogRel2(("Audio: Warning! Period size of %RU32 frames for stream '%s' is larger than the %RU32 frames buffer size, reducing it to %RU32 frames!\n",
                 pCfgAcq->Backend.cFramesPeriod, pCfgAcq->szName, pCfgAcq->Backend.cFramesBufferSize, pCfgAcq->Backend.cFramesBufferSize / 2));
        AssertMsgFailed(("cFramesPeriod=%#x vs cFramesBufferSize=%#x\n", pCfgAcq->Backend.cFramesPeriod, pCfgAcq->Backend.cFramesBufferSize));
        pCfgAcq->Backend.cFramesPeriod = pCfgAcq->Backend.cFramesBufferSize / 2;
    }

    LogRel2(("Audio: Buffer size for stream '%s' is %RU64 ms / %RU32 frames\n", pCfgAcq->szName,
             PDMAudioPropsFramesToMilli(&pCfgAcq->Props, pCfgAcq->Backend.cFramesBufferSize), pCfgAcq->Backend.cFramesBufferSize));
    LogRel2(("Audio: Pre-buffering size for stream '%s' is %RU64 ms / %RU32 frames\n", pCfgAcq->szName,
             PDMAudioPropsFramesToMilli(&pCfgAcq->Props, pCfgAcq->Backend.cFramesPreBuffering), pCfgAcq->Backend.cFramesPreBuffering));
    LogRel2(("Audio: Scheduling hint for stream '%s' is %RU32ms / %RU32 frames\n", pCfgAcq->szName,
             pCfgAcq->Device.cMsSchedulingHint, PDMAudioPropsMilliToFrames(&pCfgAcq->Props, pCfgAcq->Device.cMsSchedulingHint)));

    /* Make sure the configured buffer size by the backend at least can hold the configured latency. */
    uint32_t const cMsPeriod = PDMAudioPropsFramesToMilli(&pCfgAcq->Props, pCfgAcq->Backend.cFramesPeriod);
    LogRel2(("Audio: Period size of stream '%s' is %RU64 ms / %RU32 frames\n",
             pCfgAcq->szName, cMsPeriod, pCfgAcq->Backend.cFramesPeriod));
    /** @todo r=bird: This is probably a misleading/harmless warning as we'd just
     *        have to transfer more each time we move data.  The period is generally
     *        pure irrelevant fiction anyway.  A more relevant comparison would
     *        be to half the buffer size, i.e. making sure we get scheduled often
     *        enough to keep the buffer at least half full (probably more
     *        sensible if the buffer size was more than 2x scheduling periods). */
    if (   CfgReq.Device.cMsSchedulingHint              /* Any scheduling hint set? */
        && CfgReq.Device.cMsSchedulingHint > cMsPeriod) /* This might lead to buffer underflows. */
        LogRel(("Audio: Warning: Scheduling hint of stream '%s' is bigger (%RU64ms) than used period size (%RU64ms)\n",
                pCfgAcq->szName, CfgReq.Device.cMsSchedulingHint, cMsPeriod));

    /*
     * Done, just log the result:
     */
    LogFunc(("Acquired stream config: %s\n", PDMAudioStrmCfgToString(&pStreamEx->Core.Cfg, szTmp, sizeof(szTmp)) ));
    LogRel2(("Audio: Acquired stream config: %s\n", PDMAudioStrmCfgToString(&pStreamEx->Core.Cfg, szTmp, sizeof(szTmp)) ));

    return VINF_SUCCESS;
}


/**
 * Worker for drvAudioStreamCreate that initializes the audio stream.
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to driver instance.
 * @param   pStreamEx   Stream to initialize.  Caller already set a few fields.
 *                      The Core.Cfg field contains the requested configuration
 *                      when we're called and the actual configuration when
 *                      successfully returning.
 */
static int drvAudioStreamInitInternal(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx)
{
    /*
     * Init host stream.
     */
    pStreamEx->Core.uMagic = PDMAUDIOSTREAM_MAGIC;

    char szTmp[PDMAUDIOSTRMCFGTOSTRING_MAX];
    LogFunc(("Requested stream config: %s\n", PDMAudioStrmCfgToString(&pStreamEx->Core.Cfg, szTmp, sizeof(szTmp)) ));
    LogRel2(("Audio: Creating stream: %s\n", PDMAudioStrmCfgToString(&pStreamEx->Core.Cfg, szTmp, sizeof(szTmp)) ));

    int rc = drvAudioStreamCreateInternalBackend(pThis, pStreamEx);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Configure host buffers.
     */
    Assert(pStreamEx->cbPreBufThreshold == 0);
    if (pStreamEx->Core.Cfg.Backend.cFramesPreBuffering != 0)
        pStreamEx->cbPreBufThreshold = PDMAudioPropsFramesToBytes(&pStreamEx->Core.Cfg.Props,
                                                                  pStreamEx->Core.Cfg.Backend.cFramesPreBuffering);

    /* Allocate space for pre-buffering of output stream w/o mixing buffers. */
    if (pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_OUT)
    {
        Assert(pStreamEx->Out.cbPreBufAlloc == 0);
        Assert(pStreamEx->Out.cbPreBuffered == 0);
        Assert(pStreamEx->Out.offPreBuf == 0);
        if (pStreamEx->Core.Cfg.Backend.cFramesPreBuffering != 0)
        {
            uint32_t cbPreBufAlloc = PDMAudioPropsFramesToBytes(&pStreamEx->Core.Cfg.Props,
                                                                pStreamEx->Core.Cfg.Backend.cFramesBufferSize);
            cbPreBufAlloc = RT_MIN(RT_ALIGN_32(pStreamEx->cbPreBufThreshold + _8K, _4K), cbPreBufAlloc);
            cbPreBufAlloc = PDMAudioPropsFloorBytesToFrame(&pStreamEx->Core.Cfg.Props, cbPreBufAlloc);
            pStreamEx->Out.cbPreBufAlloc     = cbPreBufAlloc;
            pStreamEx->Out.pbPreBuf          = (uint8_t *)RTMemAllocZ(cbPreBufAlloc);
            AssertReturn(pStreamEx->Out.pbPreBuf, VERR_NO_MEMORY);
        }
        pStreamEx->Out.enmPlayState          = DRVAUDIOPLAYSTATE_NOPLAY; /* Changed upon enable. */
    }

    /*
     * Register statistics.
     */
    PPDMDRVINS const pDrvIns = pThis->pDrvIns;
    /** @todo expose config and more. */
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->Core.Cfg.Backend.cFramesBufferSize, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_NONE,
                           "The size of the backend buffer (in frames)",     "%s/0-HostBackendBufSize", pStreamEx->Core.Cfg.szName);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->Core.Cfg.Backend.cFramesPeriod, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_NONE,
                           "The size of the backend period (in frames)",     "%s/0-HostBackendPeriodSize", pStreamEx->Core.Cfg.szName);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->Core.Cfg.Backend.cFramesPreBuffering, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_NONE,
                           "Pre-buffer size (in frames)",                    "%s/0-HostBackendPreBufferSize", pStreamEx->Core.Cfg.szName);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->Core.Cfg.Device.cMsSchedulingHint, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_NONE,
                           "Device DMA scheduling hint (in milliseconds)",   "%s/0-DeviceSchedulingHint", pStreamEx->Core.Cfg.szName);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->Core.Cfg.Props.uHz, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_HZ,
                           "Backend stream frequency",                       "%s/Hz", pStreamEx->Core.Cfg.szName);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->Core.Cfg.Props.cbFrame, STAMTYPE_U8, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                           "Backend frame size",                             "%s/Framesize", pStreamEx->Core.Cfg.szName);
    if (pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_IN)
    {
        PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->In.Stats.cbBackendReadableBefore, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_NONE,
                               "Free space in backend buffer before play",   "%s/0-HostBackendBufReadableBefore", pStreamEx->Core.Cfg.szName);
        PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->In.Stats.cbBackendReadableAfter, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_NONE,
                               "Free space in backend buffer after play",    "%s/0-HostBackendBufReadableAfter", pStreamEx->Core.Cfg.szName);
#ifdef VBOX_WITH_STATISTICS
        PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->In.Stats.ProfCapture, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "Profiling time spent in StreamCapture",      "%s/ProfStreamCapture", pStreamEx->Core.Cfg.szName);
        PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->In.Stats.ProfGetReadable, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "Profiling time spent in StreamGetReadable",  "%s/ProfStreamGetReadable", pStreamEx->Core.Cfg.szName);
        PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->In.Stats.ProfGetReadableBytes, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Readable byte stats",                        "%s/ProfStreamGetReadableBytes", pStreamEx->Core.Cfg.szName);
#endif
    }
    else
    {
        PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->Out.Stats.cbBackendWritableBefore, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_NONE,
                               "Free space in backend buffer before play",   "%s/0-HostBackendBufWritableBefore", pStreamEx->Core.Cfg.szName);
        PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->Out.Stats.cbBackendWritableAfter, STAMTYPE_U32, STAMVISIBILITY_USED, STAMUNIT_NONE,
                               "Free space in backend buffer after play",    "%s/0-HostBackendBufWritableAfter", pStreamEx->Core.Cfg.szName);
#ifdef VBOX_WITH_STATISTICS
        PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->Out.Stats.ProfPlay, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "Profiling time spent in StreamPlay",         "%s/ProfStreamPlay", pStreamEx->Core.Cfg.szName);
        PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->Out.Stats.ProfGetWritable, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                               "Profiling time spent in StreamGetWritable",  "%s/ProfStreamGetWritable", pStreamEx->Core.Cfg.szName);
        PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->Out.Stats.ProfGetWritableBytes, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                               "Writeable byte stats",                       "%s/ProfStreamGetWritableBytes", pStreamEx->Core.Cfg.szName);
#endif
    }
#ifdef VBOX_WITH_STATISTICS
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->StatProfGetState, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                           "Profiling time spent in StreamGetState",         "%s/ProfStreamGetState", pStreamEx->Core.Cfg.szName);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->StatXfer, STAMTYPE_PROFILE, STAMVISIBILITY_USED, STAMUNIT_BYTES,
                           "Byte transfer stats (excluding pre-buffering)",  "%s/Transfers", pStreamEx->Core.Cfg.szName);
#endif
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pStreamEx->offInternal, STAMTYPE_U64, STAMVISIBILITY_USED, STAMUNIT_NONE,
                           "Internal stream offset",                         "%s/offInternal", pStreamEx->Core.Cfg.szName);

    LogFlowFunc(("[%s] Returning %Rrc\n", pStreamEx->Core.Cfg.szName, rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnStreamCreate}
 */
static DECLCALLBACK(int) drvAudioStreamCreate(PPDMIAUDIOCONNECTOR pInterface, uint32_t fFlags, PCPDMAUDIOSTREAMCFG pCfgReq,
                                              PPDMAUDIOSTREAM *ppStream)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    AssertPtr(pThis);

    /*
     * Assert sanity.
     */
    AssertReturn(!(fFlags & ~PDMAUDIOSTREAM_CREATE_F_NO_MIXBUF), VERR_INVALID_FLAGS);
    AssertPtrReturn(pCfgReq, VERR_INVALID_POINTER);
    AssertPtrReturn(ppStream, VERR_INVALID_POINTER);
    *ppStream = NULL;
    LogFlowFunc(("pCfgReq=%s\n", pCfgReq->szName));
#ifdef LOG_ENABLED
    PDMAudioStrmCfgLog(pCfgReq);
#endif
    AssertReturn(AudioHlpStreamCfgIsValid(pCfgReq), VERR_INVALID_PARAMETER);
    AssertReturn(pCfgReq->enmDir == PDMAUDIODIR_IN || pCfgReq->enmDir == PDMAUDIODIR_OUT, VERR_NOT_SUPPORTED);

    /*
     * Grab a free stream count now.
     */
    int rc = RTCritSectRwEnterExcl(&pThis->CritSectGlobals);
    AssertRCReturn(rc, rc);

    uint32_t * const pcFreeStreams = pCfgReq->enmDir == PDMAUDIODIR_IN ? &pThis->In.cStreamsFree : &pThis->Out.cStreamsFree;
    if (*pcFreeStreams > 0)
        *pcFreeStreams -= 1;
    else
    {
        RTCritSectRwLeaveExcl(&pThis->CritSectGlobals);
        LogFlowFunc(("Maximum number of host %s streams reached\n", PDMAudioDirGetName(pCfgReq->enmDir) ));
        return pCfgReq->enmDir == PDMAUDIODIR_IN ? VERR_AUDIO_NO_FREE_INPUT_STREAMS : VERR_AUDIO_NO_FREE_OUTPUT_STREAMS;
    }

    RTCritSectRwLeaveExcl(&pThis->CritSectGlobals);

    /*
     * Get and check the backend size.
     *
     * Since we'll have to leave the hot-plug lock before we call the backend,
     * we'll have revalidate the size at that time.
     */
    RTCritSectRwEnterShared(&pThis->CritSectHotPlug);

    size_t const cbHstStrm = pThis->BackendCfg.cbStream;
    AssertStmt(cbHstStrm >= sizeof(PDMAUDIOBACKENDSTREAM), rc = VERR_OUT_OF_RANGE);
    AssertStmt(cbHstStrm < _16M, rc = VERR_OUT_OF_RANGE);

    RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate and initialize common state.
         */
        PDRVAUDIOSTREAM pStreamEx = (PDRVAUDIOSTREAM)RTMemAllocZ(sizeof(DRVAUDIOSTREAM) + RT_ALIGN_Z(cbHstStrm, 64));
        if (pStreamEx)
        {
            rc = RTCritSectInit(&pStreamEx->Core.CritSect); /* (drvAudioStreamFree assumes it's initailized) */
            if (RT_SUCCESS(rc))
            {
                PPDMAUDIOBACKENDSTREAM pBackend = (PPDMAUDIOBACKENDSTREAM)(pStreamEx + 1);
                pBackend->uMagic                = PDMAUDIOBACKENDSTREAM_MAGIC;
                pBackend->pStream               = &pStreamEx->Core;

                pStreamEx->pBackend             = pBackend;
                pStreamEx->Core.Cfg             = *pCfgReq;
                pStreamEx->Core.cbBackend       = (uint32_t)cbHstStrm;
                pStreamEx->fDestroyImmediate    = true;
                pStreamEx->hReqInitAsync        = NIL_RTREQ;
                pStreamEx->uMagic               = DRVAUDIOSTREAM_MAGIC;

                /* Make a unqiue stream name including the host (backend) driver name. */
                AssertPtr(pThis->pHostDrvAudio);
                size_t cchName = RTStrPrintf(pStreamEx->Core.Cfg.szName, RT_ELEMENTS(pStreamEx->Core.Cfg.szName), "[%s] %s:0",
                                             pThis->BackendCfg.szName, pCfgReq->szName[0] != '\0' ? pCfgReq->szName : "<NoName>");
                if (cchName < sizeof(pStreamEx->Core.Cfg.szName))
                {
                    RTCritSectRwEnterShared(&pThis->CritSectGlobals);
                    for (uint32_t i = 0; i < 256; i++)
                    {
                        bool fDone = true;
                        PDRVAUDIOSTREAM pIt;
                        RTListForEach(&pThis->LstStreams, pIt, DRVAUDIOSTREAM, ListEntry)
                        {
                            if (strcmp(pIt->Core.Cfg.szName, pStreamEx->Core.Cfg.szName) == 0)
                            {
                                RTStrPrintf(pStreamEx->Core.Cfg.szName, RT_ELEMENTS(pStreamEx->Core.Cfg.szName), "[%s] %s:%u",
                                            pThis->BackendCfg.szName, pCfgReq->szName[0] != '\0' ? pCfgReq->szName : "<NoName>",
                                            i);
                                fDone = false;
                                break;
                            }
                        }
                        if (fDone)
                            break;
                    }
                    RTCritSectRwLeaveShared(&pThis->CritSectGlobals);
                }

                /*
                 * Try to init the rest.
                 */
                rc = drvAudioStreamInitInternal(pThis, pStreamEx);
                if (RT_SUCCESS(rc))
                {
                    /* Set initial reference counts. */
                    pStreamEx->cRefs = pStreamEx->fNeedAsyncInit ? 2 : 1;

                    /* Add it to the list. */
                    RTCritSectRwEnterExcl(&pThis->CritSectGlobals);

                    RTListAppend(&pThis->LstStreams, &pStreamEx->ListEntry);
                    pThis->cStreams++;
                    STAM_REL_COUNTER_INC(&pThis->StatTotalStreamsCreated);

                    RTCritSectRwLeaveExcl(&pThis->CritSectGlobals);

                    /*
                     * Init debug stuff if enabled (ignore failures).
                     */
                    if (pCfgReq->enmDir == PDMAUDIODIR_IN)
                    {
                        if (pThis->CfgIn.Dbg.fEnabled)
                            AudioHlpFileCreateAndOpen(&pStreamEx->In.Dbg.pFileCapture, pThis->CfgIn.Dbg.szPathOut,
                                                      "DrvAudioCapture", pThis->pDrvIns->iInstance, &pStreamEx->Core.Cfg.Props);
                    }
                    else /* Out */
                    {
                        if (pThis->CfgOut.Dbg.fEnabled)
                            AudioHlpFileCreateAndOpen(&pStreamEx->Out.Dbg.pFilePlay, pThis->CfgOut.Dbg.szPathOut,
                                                      "DrvAudioPlay", pThis->pDrvIns->iInstance, &pStreamEx->Core.Cfg.Props);
                    }

                    /*
                     * Kick off the asynchronous init.
                     */
                    if (!pStreamEx->fNeedAsyncInit)
                    {
#ifdef DRVAUDIO_WITH_STREAM_DESTRUCTION_IN_DISABLED_DIRECTION
                        /* drvAudioStreamInitInternal returns success for disable stream directions w/o actually
                           creating a backend, so we need to check that before marking the backend ready.. */
                        if (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_CREATED)
#endif
                            pStreamEx->fStatus |= PDMAUDIOSTREAM_STS_BACKEND_READY;
                        PDMAUDIOSTREAM_STS_ASSERT_VALID(pStreamEx->fStatus);
                    }
                    else
                    {
                        int rc2 = RTReqPoolCallEx(pThis->hReqPool, 0 /*cMillies*/, &pStreamEx->hReqInitAsync,
                                                  RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                                                  (PFNRT)drvAudioStreamInitAsync, 2, pThis, pStreamEx);
                        LogFlowFunc(("hReqInitAsync=%p rc2=%Rrc\n", pStreamEx->hReqInitAsync, rc2));
                        AssertRCStmt(rc2, drvAudioStreamInitAsync(pThis, pStreamEx));
                    }

#ifdef VBOX_STRICT
                    /*
                     * Assert lock order to make sure the lock validator picks up on it.
                     */
                    RTCritSectRwEnterShared(&pThis->CritSectGlobals);
                    RTCritSectEnter(&pStreamEx->Core.CritSect);
                    RTCritSectRwEnterShared(&pThis->CritSectHotPlug);
                    RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
                    RTCritSectLeave(&pStreamEx->Core.CritSect);
                    RTCritSectRwLeaveShared(&pThis->CritSectGlobals);
#endif

                    *ppStream = &pStreamEx->Core;
                    LogFlowFunc(("returns VINF_SUCCESS (pStreamEx=%p)\n", pStreamEx));
                    return VINF_SUCCESS;
                }

                LogFunc(("drvAudioStreamInitInternal failed: %Rrc\n", rc));
                int rc2 = drvAudioStreamUninitInternal(pThis, pStreamEx);
                AssertRC(rc2);
                drvAudioStreamFree(pStreamEx);
            }
            else
                RTMemFree(pStreamEx);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    /*
     * Give back the stream count, we couldn't use it after all.
     */
    RTCritSectRwEnterExcl(&pThis->CritSectGlobals);
    *pcFreeStreams += 1;
    RTCritSectRwLeaveExcl(&pThis->CritSectGlobals);

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Calls the backend to give it the chance to destroy its part of the audio stream.
 *
 * Called from drvAudioPowerOff, drvAudioStreamUninitInternal and
 * drvAudioStreamReInitInternal.
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to driver instance.
 * @param   pStreamEx   Audio stream destruct backend for.
 */
static int drvAudioStreamDestroyInternalBackend(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx)
{
    AssertPtr(pThis);
    AssertPtr(pStreamEx);

    int rc = VINF_SUCCESS;

#ifdef LOG_ENABLED
    char szStreamSts[DRVAUDIO_STATUS_STR_MAX];
#endif
    LogFunc(("[%s] fStatus=%s\n", pStreamEx->Core.Cfg.szName, drvAudioStreamStatusToStr(szStreamSts, pStreamEx->fStatus)));

    if (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_CREATED)
    {
        AssertPtr(pStreamEx->pBackend);

        /* Check if the pointer to  the host audio driver is still valid.
         * It can be NULL if we were called in drvAudioDestruct, for example. */
        RTCritSectRwEnterShared(&pThis->CritSectHotPlug); /** @todo needed? */
        if (pThis->pHostDrvAudio)
            rc = pThis->pHostDrvAudio->pfnStreamDestroy(pThis->pHostDrvAudio, pStreamEx->pBackend, pStreamEx->fDestroyImmediate);
        RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);

        pStreamEx->fStatus &= ~(PDMAUDIOSTREAM_STS_BACKEND_CREATED | PDMAUDIOSTREAM_STS_BACKEND_READY);
        PDMAUDIOSTREAM_STS_ASSERT_VALID(pStreamEx->fStatus);
    }

    LogFlowFunc(("[%s] Returning %Rrc\n", pStreamEx->Core.Cfg.szName, rc));
    return rc;
}


/**
 * Uninitializes an audio stream - worker for drvAudioStreamDestroy,
 * drvAudioDestruct and drvAudioStreamCreate.
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to driver instance.
 * @param   pStreamEx   Pointer to audio stream to uninitialize.
 */
static int drvAudioStreamUninitInternal(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx)
{
    AssertPtrReturn(pThis,   VERR_INVALID_POINTER);
    AssertMsgReturn(pStreamEx->cRefs <= 1,
                    ("Stream '%s' still has %RU32 references held when uninitializing\n", pStreamEx->Core.Cfg.szName, pStreamEx->cRefs),
                    VERR_WRONG_ORDER);
    LogFlowFunc(("[%s] cRefs=%RU32\n", pStreamEx->Core.Cfg.szName, pStreamEx->cRefs));

    RTCritSectEnter(&pStreamEx->Core.CritSect);

    /*
     * ...
     */
    if (pStreamEx->fDestroyImmediate)
        drvAudioStreamControlInternal(pThis, pStreamEx, PDMAUDIOSTREAMCMD_DISABLE);
    int rc = drvAudioStreamDestroyInternalBackend(pThis, pStreamEx);

    /* Free pre-buffer space. */
    if (   pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_OUT
        && pStreamEx->Out.pbPreBuf)
    {
        RTMemFree(pStreamEx->Out.pbPreBuf);
        pStreamEx->Out.pbPreBuf      = NULL;
        pStreamEx->Out.cbPreBufAlloc = 0;
        pStreamEx->Out.cbPreBuffered = 0;
        pStreamEx->Out.offPreBuf     = 0;
    }

    if (RT_SUCCESS(rc))
    {
#ifdef LOG_ENABLED
        if (pStreamEx->fStatus != PDMAUDIOSTREAM_STS_NONE)
        {
            char szStreamSts[DRVAUDIO_STATUS_STR_MAX];
            LogFunc(("[%s] Warning: Still has %s set when uninitializing\n",
                     pStreamEx->Core.Cfg.szName, drvAudioStreamStatusToStr(szStreamSts, pStreamEx->fStatus)));
        }
#endif
        pStreamEx->fStatus = PDMAUDIOSTREAM_STS_NONE;
    }

    PPDMDRVINS const pDrvIns = pThis->pDrvIns;
    PDMDrvHlpSTAMDeregisterByPrefix(pDrvIns, pStreamEx->Core.Cfg.szName);

    if (pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_IN)
    {
        if (pThis->CfgIn.Dbg.fEnabled)
        {
            AudioHlpFileDestroy(pStreamEx->In.Dbg.pFileCapture);
            pStreamEx->In.Dbg.pFileCapture = NULL;
        }
    }
    else
    {
        Assert(pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_OUT);
        if (pThis->CfgOut.Dbg.fEnabled)
        {
            AudioHlpFileDestroy(pStreamEx->Out.Dbg.pFilePlay);
            pStreamEx->Out.Dbg.pFilePlay = NULL;
        }
    }

    RTCritSectLeave(&pStreamEx->Core.CritSect);
    LogFlowFunc(("Returning %Rrc\n", rc));
    return rc;
}


/**
 * Internal release function.
 *
 * @returns New reference count, UINT32_MAX if bad stream.
 * @param   pThis           Pointer to the DrvAudio instance data.
 * @param   pStreamEx       The stream to reference.
 * @param   fMayDestroy     Whether the caller is allowed to implicitly destroy
 *                          the stream or not.  The reference count must _not_
 *                          reach zero if this is false!
 */
static uint32_t drvAudioStreamReleaseInternal(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx, bool fMayDestroy)
{
    AssertPtrReturn(pStreamEx, UINT32_MAX);
    AssertReturn(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC, UINT32_MAX);
    AssertReturn(pStreamEx->uMagic == DRVAUDIOSTREAM_MAGIC, UINT32_MAX);
    Assert(!RTCritSectIsOwner(&pStreamEx->Core.CritSect));

    uint32_t cRefs = ASMAtomicDecU32(&pStreamEx->cRefs);
    if (cRefs != 0)
        Assert(cRefs < _1K);
    else if (fMayDestroy)
    {
/** @todo r=bird: Caching one stream in each direction for some time,
 * depending on the time it took to create it.  drvAudioStreamCreate can use it
 * if the configuration matches, otherwise it'll throw it away.  This will
 * provide a general speedup independ of device (HDA used to do this, but
 * doesn't) and backend implementation.  Ofc, the backend probably needs an
 * opt-out here. */
        int rc = drvAudioStreamUninitInternal(pThis, pStreamEx);
        if (RT_SUCCESS(rc))
        {
            RTCritSectRwEnterExcl(&pThis->CritSectGlobals);

            if (pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_IN)
                pThis->In.cStreamsFree++;
            else /* Out */
                pThis->Out.cStreamsFree++;
            pThis->cStreams--;

            RTListNodeRemove(&pStreamEx->ListEntry);

            RTCritSectRwLeaveExcl(&pThis->CritSectGlobals);

            drvAudioStreamFree(pStreamEx);
            pStreamEx = NULL;
        }
        else
        {
            LogRel(("Audio: Uninitializing stream '%s' failed with %Rrc\n", pStreamEx->Core.Cfg.szName, rc));
            /** @todo r=bird: What's the plan now? */
        }
    }
    else
    {
        cRefs = ASMAtomicIncU32(&pStreamEx->cRefs);
        AssertFailed();
    }

    Log12Func(("returns %u (%s)\n", cRefs, cRefs > 0 ? pStreamEx->Core.Cfg.szName : "destroyed")); /* Not 101% safe, but it's logging. */
    return cRefs;
}


/**
 * Asynchronous worker for drvAudioStreamDestroy.
 *
 * Does DISABLE and releases reference, possibly destroying the stream.
 *
 * @param   pThis       Pointer to the DrvAudio instance data.
 * @param   pStreamEx   The stream.  One reference for us to release.
 * @param   fImmediate  How to treat draining streams.
 */
static DECLCALLBACK(void) drvAudioStreamDestroyAsync(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx, bool fImmediate)
{
    LogFlowFunc(("pThis=%p pStreamEx=%p (%s) fImmediate=%RTbool\n", pThis, pStreamEx, pStreamEx->Core.Cfg.szName, fImmediate));
#ifdef LOG_ENABLED
    uint64_t const nsStart = RTTimeNanoTS();
#endif
    RTCritSectEnter(&pStreamEx->Core.CritSect);

    pStreamEx->fDestroyImmediate = fImmediate; /* Do NOT adjust for draining status, just pass it as-is. CoreAudio needs this. */

    if (!fImmediate && (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_PENDING_DISABLE))
        LogFlowFunc(("No DISABLE\n"));
    else
    {
        int rc2 = drvAudioStreamControlInternal(pThis, pStreamEx, PDMAUDIOSTREAMCMD_DISABLE);
        LogFlowFunc(("DISABLE done: %Rrc\n", rc2));
        AssertRC(rc2);
    }

    RTCritSectLeave(&pStreamEx->Core.CritSect);

    drvAudioStreamReleaseInternal(pThis, pStreamEx, true /*fMayDestroy*/);

    LogFlowFunc(("returning (after %'RU64 ns)\n", RTTimeNanoTS() - nsStart));
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnStreamDestroy}
 */
static DECLCALLBACK(int) drvAudioStreamDestroy(PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream, bool fImmediate)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    AssertPtr(pThis);

    /* Ignore NULL streams. */
    if (!pStream)
        return VINF_SUCCESS;

    PDRVAUDIOSTREAM pStreamEx = (PDRVAUDIOSTREAM)pStream;   /* Note! Do not touch pStream after this! */
    AssertPtrReturn(pStreamEx, VERR_INVALID_POINTER);
    LogFlowFunc(("ENTER - %p (%s) fImmediate=%RTbool\n", pStreamEx, pStreamEx->Core.Cfg.szName, fImmediate));
    AssertReturn(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pStreamEx->uMagic == DRVAUDIOSTREAM_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pStreamEx->pBackend && pStreamEx->pBackend->uMagic == PDMAUDIOBACKENDSTREAM_MAGIC, VERR_INVALID_MAGIC);

    /*
     * The main difference from a regular release is that this will disable
     * (or drain if we could) the stream and we can cancel any pending
     * pfnStreamInitAsync call.
     */
    int rc = RTCritSectEnter(&pStreamEx->Core.CritSect);
    AssertRCReturn(rc, rc);

    if (pStreamEx->uMagic == DRVAUDIOSTREAM_MAGIC)
    {
        if (pStreamEx->cRefs > 0 && pStreamEx->cRefs < UINT32_MAX / 4)
        {
            char szStatus[DRVAUDIO_STATUS_STR_MAX];
            LogRel2(("Audio: Destroying stream '%s': cRefs=%u; status: %s; backend: %s; hReqInitAsync=%p\n",
                     pStreamEx->Core.Cfg.szName, pStreamEx->cRefs, drvAudioStreamStatusToStr(szStatus, pStreamEx->fStatus),
                     PDMHostAudioStreamStateGetName(drvAudioStreamGetBackendState(pThis, pStreamEx)),
                     pStreamEx->hReqInitAsync));

            /* Try cancel pending async init request and release the it. */
            if (pStreamEx->hReqInitAsync != NIL_RTREQ)
            {
                Assert(pStreamEx->cRefs >= 2);
                int rc2 = RTReqCancel(pStreamEx->hReqInitAsync);

                RTReqRelease(pStreamEx->hReqInitAsync);
                pStreamEx->hReqInitAsync = NIL_RTREQ;

                RTCritSectLeave(&pStreamEx->Core.CritSect); /* (exit before releasing the stream to avoid assertion) */

                if (RT_SUCCESS(rc2))
                {
                    LogFlowFunc(("Successfully cancelled pending pfnStreamInitAsync call (hReqInitAsync=%p).\n",
                                 pStreamEx->hReqInitAsync));
                    uint32_t const cRefs = drvAudioStreamReleaseInternal(pThis, pStreamEx, true /*fMayDestroy*/);
                    AssertStmt(cRefs > 0, pStreamEx = NULL);
                }
                else
                {
                    LogFlowFunc(("Failed to cancel pending pfnStreamInitAsync call (hReqInitAsync=%p): %Rrc\n",
                                 pStreamEx->hReqInitAsync, rc2));
                    Assert(rc2 == VERR_RT_REQUEST_STATE);
                }
            }
            else
                RTCritSectLeave(&pStreamEx->Core.CritSect);

            if (RT_LIKELY(pStreamEx != NULL)) /* paranoia^2 */
            {
                /*
                 * Now, if the backend requests asynchronous disabling and destruction
                 * push the disabling and destroying over to a worker thread.
                 *
                 * This is a general offloading feature that all backends should make use of,
                 * however it's rather precarious on macs where stopping an already draining
                 * stream may take 8-10ms which naturally isn't something we should be doing
                 * on an EMT.
                 */
                if (!(pThis->BackendCfg.fFlags & PDMAUDIOBACKEND_F_ASYNC_STREAM_DESTROY))
                    drvAudioStreamDestroyAsync(pThis, pStreamEx, fImmediate);
                else
                {
                    int rc2 = RTReqPoolCallEx(pThis->hReqPool, 0 /*cMillies*/, NULL /*phReq*/,
                                              RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                                              (PFNRT)drvAudioStreamDestroyAsync, 3, pThis, pStreamEx, fImmediate);
                    LogFlowFunc(("hReqInitAsync=%p rc2=%Rrc\n", pStreamEx->hReqInitAsync, rc2));
                    AssertRCStmt(rc2, drvAudioStreamDestroyAsync(pThis, pStreamEx, fImmediate));
                }
            }
        }
        else
        {
            AssertLogRelMsgFailedStmt(("%p cRefs=%#x\n", pStreamEx, pStreamEx->cRefs), rc = VERR_CALLER_NO_REFERENCE);
            RTCritSectLeave(&pStreamEx->Core.CritSect); /*??*/
        }
    }
    else
    {
        AssertLogRelMsgFailedStmt(("%p uMagic=%#x\n", pStreamEx, pStreamEx->uMagic), rc = VERR_INVALID_MAGIC);
        RTCritSectLeave(&pStreamEx->Core.CritSect); /*??*/
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Drops all audio data (and associated state) of a stream.
 *
 * Used by drvAudioStreamIterateInternal(), drvAudioStreamResetOnDisable(), and
 * drvAudioStreamReInitInternal().
 *
 * @param   pStreamEx   Stream to drop data for.
 */
static void drvAudioStreamResetInternal(PDRVAUDIOSTREAM pStreamEx)
{
    LogFunc(("[%s]\n", pStreamEx->Core.Cfg.szName));
    Assert(RTCritSectIsOwner(&pStreamEx->Core.CritSect));

    pStreamEx->nsLastIterated       = 0;
    pStreamEx->nsLastPlayedCaptured = 0;
    pStreamEx->nsLastReadWritten    = 0;
    if (pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_OUT)
    {
        pStreamEx->Out.cbPreBuffered = 0;
        pStreamEx->Out.offPreBuf     = 0;
        pStreamEx->Out.enmPlayState  = pStreamEx->cbPreBufThreshold > 0
                                     ? DRVAUDIOPLAYSTATE_PREBUF : DRVAUDIOPLAYSTATE_PLAY;
    }
    else
        pStreamEx->In.enmCaptureState = pStreamEx->cbPreBufThreshold > 0
                                      ? DRVAUDIOCAPTURESTATE_PREBUF : DRVAUDIOCAPTURESTATE_CAPTURING;
}


/**
 * Re-initializes an audio stream with its existing host and guest stream
 * configuration.
 *
 * This might be the case if the backend told us we need to re-initialize
 * because something on the host side has changed.
 *
 * @note    Does not touch the stream's status flags.
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to driver instance.
 * @param   pStreamEx   Stream to re-initialize.
 */
static int drvAudioStreamReInitInternal(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx)
{
    char szTmp[RT_MAX(PDMAUDIOSTRMCFGTOSTRING_MAX, DRVAUDIO_STATUS_STR_MAX)];
    LogFlowFunc(("[%s] status: %s\n", pStreamEx->Core.Cfg.szName, drvAudioStreamStatusToStr(szTmp, pStreamEx->fStatus) ));
    Assert(RTCritSectIsOwner(&pStreamEx->Core.CritSect));
    RTCritSectRwEnterShared(&pThis->CritSectHotPlug);

    /*
     * Destroy and re-create stream on backend side.
     */
    if (   (pStreamEx->fStatus & (PDMAUDIOSTREAM_STS_ENABLED | PDMAUDIOSTREAM_STS_BACKEND_CREATED | PDMAUDIOSTREAM_STS_BACKEND_READY))
        ==                       (PDMAUDIOSTREAM_STS_ENABLED | PDMAUDIOSTREAM_STS_BACKEND_CREATED | PDMAUDIOSTREAM_STS_BACKEND_READY))
        drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_DISABLE);

    if (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_CREATED)
        drvAudioStreamDestroyInternalBackend(pThis, pStreamEx);

    int rc = VERR_AUDIO_STREAM_NOT_READY;
    if (!(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_CREATED))
    {
        drvAudioStreamResetInternal(pStreamEx);

        RT_BZERO(pStreamEx->pBackend + 1, pStreamEx->Core.cbBackend - sizeof(*pStreamEx->pBackend));

        rc = drvAudioStreamCreateInternalBackend(pThis, pStreamEx);
        if (RT_SUCCESS(rc))
        {
            LogFunc(("Acquired host config: %s\n", PDMAudioStrmCfgToString(&pStreamEx->Core.Cfg, szTmp, sizeof(szTmp)) ));
            /** @todo Validate (re-)acquired configuration with pStreamEx->Core.Core.Cfg?
             * drvAudioStreamInitInternal() does some setup and a bunch of
             * validations + adjustments of the stream config, so this surely is quite
             * optimistic. */
            if (true)
            {
                /*
                 * Kick off the asynchronous init.
                 */
                if (!pStreamEx->fNeedAsyncInit)
                {
                    pStreamEx->fStatus |= PDMAUDIOSTREAM_STS_BACKEND_READY;
                    PDMAUDIOSTREAM_STS_ASSERT_VALID(pStreamEx->fStatus);
                }
                else
                {
                    drvAudioStreamRetainInternal(pStreamEx);
                    int rc2 = RTReqPoolCallEx(pThis->hReqPool, 0 /*cMillies*/, &pStreamEx->hReqInitAsync,
                                              RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                                              (PFNRT)drvAudioStreamInitAsync, 2, pThis, pStreamEx);
                    LogFlowFunc(("hReqInitAsync=%p rc2=%Rrc\n", pStreamEx->hReqInitAsync, rc2));
                    AssertRCStmt(rc2, drvAudioStreamInitAsync(pThis, pStreamEx));
                }

                /*
                 * Update the backend on the stream state if it's ready, otherwise
                 * let the worker thread do it after the async init has completed.
                 */
                if (   (pStreamEx->fStatus & (PDMAUDIOSTREAM_STS_BACKEND_READY | PDMAUDIOSTREAM_STS_BACKEND_CREATED))
                    ==                       (PDMAUDIOSTREAM_STS_BACKEND_READY | PDMAUDIOSTREAM_STS_BACKEND_CREATED))
                {
                    rc = drvAudioStreamUpdateBackendOnStatus(pThis, pStreamEx, "re-initializing");
                    /** @todo not sure if we really need to care about this status code...   */
                }
                else if (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_CREATED)
                {
                    Assert(pStreamEx->hReqInitAsync != NIL_RTREQ);
                    LogFunc(("Asynchronous stream init (%p) ...\n", pStreamEx->hReqInitAsync));
                }
                else
                {
                    LogRel(("Audio: Re-initializing stream '%s' somehow failed, status: %s\n", pStreamEx->Core.Cfg.szName,
                            drvAudioStreamStatusToStr(szTmp, pStreamEx->fStatus) ));
                    AssertFailed();
                    rc = VERR_AUDIO_STREAM_COULD_NOT_CREATE;
                }
            }
        }
        else
            LogRel(("Audio: Re-initializing stream '%s' failed with %Rrc\n", pStreamEx->Core.Cfg.szName, rc));
    }
    else
    {
        LogRel(("Audio: Re-initializing stream '%s' failed to destroy previous backend.\n", pStreamEx->Core.Cfg.szName));
        AssertFailed();
    }

    RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    LogFunc(("[%s] Returning %Rrc\n", pStreamEx->Core.Cfg.szName, rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnStreamReInit}
 */
static DECLCALLBACK(int) drvAudioStreamReInit(PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    PDRVAUDIOSTREAM pStreamEx = (PDRVAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamEx, VERR_INVALID_POINTER);
    AssertReturn(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pStreamEx->uMagic == DRVAUDIOSTREAM_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_NEED_REINIT, VERR_INVALID_STATE);
    LogFlowFunc(("\n"));

    int rc = RTCritSectEnter(&pStreamEx->Core.CritSect);
    AssertRCReturn(rc, rc);

    if (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_NEED_REINIT)
    {
        const unsigned cMaxTries = 5;
        const uint64_t nsNow   = RTTimeNanoTS();

        /* Throttle re-initializing streams on failure. */
        if (   pStreamEx->cTriesReInit < cMaxTries
            && pStreamEx->hReqInitAsync == NIL_RTREQ
            && (   pStreamEx->nsLastReInit == 0
                || nsNow - pStreamEx->nsLastReInit >= RT_NS_1SEC * pStreamEx->cTriesReInit))
        {
            rc = drvAudioStreamReInitInternal(pThis, pStreamEx);
            if (RT_SUCCESS(rc))
            {
                /* Remove the pending re-init flag on success. */
                pStreamEx->fStatus &= ~PDMAUDIOSTREAM_STS_NEED_REINIT;
                PDMAUDIOSTREAM_STS_ASSERT_VALID(pStreamEx->fStatus);
            }
            else
            {
                pStreamEx->nsLastReInit = nsNow;
                pStreamEx->cTriesReInit++;

                /* Did we exceed our tries re-initializing the stream?
                 * Then this one is dead-in-the-water, so disable it for further use. */
                if (pStreamEx->cTriesReInit >= cMaxTries)
                {
                    LogRel(("Audio: Re-initializing stream '%s' exceeded maximum retries (%u), leaving as disabled\n",
                            pStreamEx->Core.Cfg.szName, cMaxTries));

                    /* Don't try to re-initialize anymore and mark as disabled. */
                    /** @todo should mark it as not-initialized too, shouldn't we?   */
                    pStreamEx->fStatus &= ~(PDMAUDIOSTREAM_STS_NEED_REINIT | PDMAUDIOSTREAM_STS_ENABLED);
                    PDMAUDIOSTREAM_STS_ASSERT_VALID(pStreamEx->fStatus);

                    /* Note: Further writes to this stream go to / will be read from the bit bucket (/dev/null) from now on. */
                }
            }
        }
        else
            Log8Func(("cTriesReInit=%d hReqInitAsync=%p nsLast=%RU64 nsNow=%RU64 nsDelta=%RU64\n", pStreamEx->cTriesReInit,
                      pStreamEx->hReqInitAsync, pStreamEx->nsLastReInit, nsNow, nsNow - pStreamEx->nsLastReInit));

#ifdef LOG_ENABLED
        char szStreamSts[DRVAUDIO_STATUS_STR_MAX];
#endif
        Log3Func(("[%s] fStatus=%s\n", pStreamEx->Core.Cfg.szName, drvAudioStreamStatusToStr(szStreamSts, pStreamEx->fStatus)));
    }
    else
    {
        AssertFailed();
        rc = VERR_INVALID_STATE;
    }

    RTCritSectLeave(&pStreamEx->Core.CritSect);

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Internal retain function.
 *
 * @returns New reference count, UINT32_MAX if bad stream.
 * @param   pStreamEx           The stream to reference.
 */
static uint32_t drvAudioStreamRetainInternal(PDRVAUDIOSTREAM pStreamEx)
{
    AssertPtrReturn(pStreamEx, UINT32_MAX);
    AssertReturn(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC, UINT32_MAX);
    AssertReturn(pStreamEx->uMagic == DRVAUDIOSTREAM_MAGIC, UINT32_MAX);

    uint32_t const cRefs = ASMAtomicIncU32(&pStreamEx->cRefs);
    Assert(cRefs > 1);
    Assert(cRefs < _1K);

    Log12Func(("returns %u (%s)\n", cRefs, pStreamEx->Core.Cfg.szName));
    return cRefs;
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnStreamRetain}
 */
static DECLCALLBACK(uint32_t) drvAudioStreamRetain(PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream)
{
    RT_NOREF(pInterface);
    return drvAudioStreamRetainInternal((PDRVAUDIOSTREAM)pStream);
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnStreamRelease}
 */
static DECLCALLBACK(uint32_t) drvAudioStreamRelease(PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream)
{
    return drvAudioStreamReleaseInternal(RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector),
                                         (PDRVAUDIOSTREAM)pStream,
                                         false /*fMayDestroy*/);
}


/**
 * Controls a stream's backend.
 *
 * @returns VBox status code.
 * @param   pThis           Pointer to driver instance.
 * @param   pStreamEx       Stream to control.
 * @param   enmStreamCmd    Control command.
 *
 * @note    Caller has entered the critical section of the stream.
 * @note    Can be called w/o having entered DRVAUDIO::CritSectHotPlug.
 */
static int drvAudioStreamControlInternalBackend(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx, PDMAUDIOSTREAMCMD enmStreamCmd)
{
    AssertPtr(pThis);
    AssertPtr(pStreamEx);
    Assert(RTCritSectIsOwner(&pStreamEx->Core.CritSect));

    int rc = RTCritSectRwEnterShared(&pThis->CritSectHotPlug);
    AssertRCReturn(rc, rc);

    /*
     * Whether to propagate commands down to the backend.
     *
     *      1. If the stream direction is disabled on the driver level, we should
     *         obviously not call the backend.  Our stream status will reflect the
     *         actual state so drvAudioEnable() can tell the backend if the user
     *         re-enables the stream direction.
     *
     *      2. If the backend hasn't finished initializing yet, don't try call
     *         it to start/stop/pause/whatever the stream.  (Better to do it here
     *         than to replicate this in the relevant backends.)  When the backend
     *         finish initializing the stream, we'll update it about the stream state.
     */
    bool const                     fDirEnabled     = drvAudioStreamIsDirectionEnabled(pThis, pStreamEx->Core.Cfg.enmDir);
    PDMHOSTAUDIOSTREAMSTATE const  enmBackendState = drvAudioStreamGetBackendState(pThis, pStreamEx);
                                                     /* ^^^ (checks pThis->pHostDrvAudio != NULL too) */

    char szStreamSts[DRVAUDIO_STATUS_STR_MAX];
    LogRel2(("Audio: %s stream '%s' backend (%s is %s; status: %s; backend-status: %s)\n",
             PDMAudioStrmCmdGetName(enmStreamCmd), pStreamEx->Core.Cfg.szName, PDMAudioDirGetName(pStreamEx->Core.Cfg.enmDir),
             fDirEnabled ? "enabled" : "disabled",  drvAudioStreamStatusToStr(szStreamSts, pStreamEx->fStatus),
             PDMHostAudioStreamStateGetName(enmBackendState) ));

    if (fDirEnabled)
    {
        if (   (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_READY /* don't really need this check, do we? */)
            && (   enmBackendState == PDMHOSTAUDIOSTREAMSTATE_OKAY
                || enmBackendState == PDMHOSTAUDIOSTREAMSTATE_DRAINING) )
        {
            switch (enmStreamCmd)
            {
                case PDMAUDIOSTREAMCMD_ENABLE:
                    rc = pThis->pHostDrvAudio->pfnStreamEnable(pThis->pHostDrvAudio, pStreamEx->pBackend);
                    break;

                case PDMAUDIOSTREAMCMD_DISABLE:
                    rc = pThis->pHostDrvAudio->pfnStreamDisable(pThis->pHostDrvAudio, pStreamEx->pBackend);
                    break;

                case PDMAUDIOSTREAMCMD_PAUSE:
                    rc = pThis->pHostDrvAudio->pfnStreamPause(pThis->pHostDrvAudio, pStreamEx->pBackend);
                    break;

                case PDMAUDIOSTREAMCMD_RESUME:
                    rc = pThis->pHostDrvAudio->pfnStreamResume(pThis->pHostDrvAudio, pStreamEx->pBackend);
                    break;

                case PDMAUDIOSTREAMCMD_DRAIN:
                    if (pThis->pHostDrvAudio->pfnStreamDrain)
                        rc = pThis->pHostDrvAudio->pfnStreamDrain(pThis->pHostDrvAudio, pStreamEx->pBackend);
                    else
                        rc = VERR_NOT_SUPPORTED;
                    break;

                default:
                    AssertMsgFailedBreakStmt(("Command %RU32 not implemented\n", enmStreamCmd), rc = VERR_INTERNAL_ERROR_2);
            }
            if (RT_SUCCESS(rc))
                Log2Func(("[%s] %s succeeded (%Rrc)\n", pStreamEx->Core.Cfg.szName, PDMAudioStrmCmdGetName(enmStreamCmd), rc));
            else
            {
                LogFunc(("[%s] %s failed with %Rrc\n", pStreamEx->Core.Cfg.szName, PDMAudioStrmCmdGetName(enmStreamCmd), rc));
                if (   rc != VERR_NOT_IMPLEMENTED
                    && rc != VERR_NOT_SUPPORTED
                    && rc != VERR_AUDIO_STREAM_NOT_READY)
                    LogRel(("Audio: %s stream '%s' failed with %Rrc\n", PDMAudioStrmCmdGetName(enmStreamCmd), pStreamEx->Core.Cfg.szName, rc));
            }
        }
        else
            LogFlowFunc(("enmBackendStat(=%s) != OKAY || !(fStatus(=%#x) & BACKEND_READY)\n",
                         PDMHostAudioStreamStateGetName(enmBackendState), pStreamEx->fStatus));
    }
    else
        LogFlowFunc(("fDirEnabled=false\n"));

    RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    return rc;
}


/**
 * Resets the given audio stream.
 *
 * @param   pStreamEx   Stream to reset.
 */
static void drvAudioStreamResetOnDisable(PDRVAUDIOSTREAM pStreamEx)
{
    drvAudioStreamResetInternal(pStreamEx);

    LogFunc(("[%s]\n", pStreamEx->Core.Cfg.szName));

    pStreamEx->fStatus            &= PDMAUDIOSTREAM_STS_BACKEND_CREATED | PDMAUDIOSTREAM_STS_BACKEND_READY;
    pStreamEx->Core.fWarningsShown = PDMAUDIOSTREAM_WARN_FLAGS_NONE;

#ifdef VBOX_WITH_STATISTICS
    /*
     * Reset statistics.
     */
    if (pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_IN)
    {
    }
    else if (pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_OUT)
    {
    }
    else
        AssertFailed();
#endif
}


/**
 * Controls an audio stream.
 *
 * @returns VBox status code.
 * @param   pThis           Pointer to driver instance.
 * @param   pStreamEx       Stream to control.
 * @param   enmStreamCmd    Control command.
 */
static int drvAudioStreamControlInternal(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx, PDMAUDIOSTREAMCMD enmStreamCmd)
{
    AssertPtr(pThis);
    AssertPtr(pStreamEx);
    Assert(RTCritSectIsOwner(&pStreamEx->Core.CritSect));

#ifdef LOG_ENABLED
    char szStreamSts[DRVAUDIO_STATUS_STR_MAX];
#endif
    LogFunc(("[%s] enmStreamCmd=%s fStatus=%s\n", pStreamEx->Core.Cfg.szName, PDMAudioStrmCmdGetName(enmStreamCmd),
             drvAudioStreamStatusToStr(szStreamSts, pStreamEx->fStatus)));

    int rc = VINF_SUCCESS;

    switch (enmStreamCmd)
    {
        case PDMAUDIOSTREAMCMD_ENABLE:
#ifdef DRVAUDIO_WITH_STREAM_DESTRUCTION_IN_DISABLED_DIRECTION
            if (!(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_CREATED))
            {
                rc = drvAudioStreamReInitInternal(pThis, pStreamEx);
                if (RT_FAILURE(rc))
                    break;
            }
#endif /* DRVAUDIO_WITH_STREAM_DESTRUCTION_IN_DISABLED_DIRECTION */
            if (!(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_ENABLED))
            {
                /* Are we still draining this stream? Then we must disable it first. */
                if (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_PENDING_DISABLE)
                {
                    LogFunc(("Stream '%s' is still draining - disabling...\n", pStreamEx->Core.Cfg.szName));
                    rc = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_DISABLE);
                    AssertRC(rc);
                    if (drvAudioStreamGetBackendState(pThis, pStreamEx) != PDMHOSTAUDIOSTREAMSTATE_DRAINING)
                    {
                        pStreamEx->fStatus &= ~(PDMAUDIOSTREAM_STS_ENABLED | PDMAUDIOSTREAM_STS_PENDING_DISABLE);
                        drvAudioStreamResetInternal(pStreamEx);
                        rc = VINF_SUCCESS;
                    }
                }

                if (RT_SUCCESS(rc))
                {
                    /* Reset the state before we try to start. */
                    PDMHOSTAUDIOSTREAMSTATE const enmBackendState = drvAudioStreamGetBackendState(pThis, pStreamEx);
                    pStreamEx->enmLastBackendState = enmBackendState;
                    pStreamEx->offInternal         = 0;

                    if (pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_OUT)
                    {
                        pStreamEx->Out.cbPreBuffered = 0;
                        pStreamEx->Out.offPreBuf     = 0;
                        pStreamEx->Out.enmPlayState  = DRVAUDIOPLAYSTATE_NOPLAY;
                        switch (enmBackendState)
                        {
                            case PDMHOSTAUDIOSTREAMSTATE_INITIALIZING:
                                if (pStreamEx->cbPreBufThreshold > 0)
                                    pStreamEx->Out.enmPlayState = DRVAUDIOPLAYSTATE_PREBUF;
                                break;
                            case PDMHOSTAUDIOSTREAMSTATE_DRAINING:
                                AssertFailed();
                                RT_FALL_THROUGH();
                            case PDMHOSTAUDIOSTREAMSTATE_OKAY:
                                pStreamEx->Out.enmPlayState = pStreamEx->cbPreBufThreshold > 0
                                                            ? DRVAUDIOPLAYSTATE_PREBUF : DRVAUDIOPLAYSTATE_PLAY;
                                break;
                            case PDMHOSTAUDIOSTREAMSTATE_NOT_WORKING:
                            case PDMHOSTAUDIOSTREAMSTATE_INACTIVE:
                                break;
                            /* no default */
                            case PDMHOSTAUDIOSTREAMSTATE_INVALID:
                            case PDMHOSTAUDIOSTREAMSTATE_END:
                            case PDMHOSTAUDIOSTREAMSTATE_32BIT_HACK:
                                break;
                        }
                        LogFunc(("ENABLE: enmBackendState=%s enmPlayState=%s\n", PDMHostAudioStreamStateGetName(enmBackendState),
                                 drvAudioPlayStateName(pStreamEx->Out.enmPlayState)));
                    }
                    else
                    {
                        pStreamEx->In.enmCaptureState = DRVAUDIOCAPTURESTATE_NO_CAPTURE;
                        switch (enmBackendState)
                        {
                            case PDMHOSTAUDIOSTREAMSTATE_INITIALIZING:
                                pStreamEx->In.enmCaptureState = DRVAUDIOCAPTURESTATE_PREBUF;
                                break;
                            case PDMHOSTAUDIOSTREAMSTATE_DRAINING:
                                AssertFailed();
                                RT_FALL_THROUGH();
                            case PDMHOSTAUDIOSTREAMSTATE_OKAY:
                                pStreamEx->In.enmCaptureState = pStreamEx->cbPreBufThreshold > 0
                                                              ? DRVAUDIOCAPTURESTATE_PREBUF : DRVAUDIOCAPTURESTATE_CAPTURING;
                                break;
                            case PDMHOSTAUDIOSTREAMSTATE_NOT_WORKING:
                            case PDMHOSTAUDIOSTREAMSTATE_INACTIVE:
                                break;
                            /* no default */
                            case PDMHOSTAUDIOSTREAMSTATE_INVALID:
                            case PDMHOSTAUDIOSTREAMSTATE_END:
                            case PDMHOSTAUDIOSTREAMSTATE_32BIT_HACK:
                                break;
                        }
                        LogFunc(("ENABLE: enmBackendState=%s enmCaptureState=%s\n", PDMHostAudioStreamStateGetName(enmBackendState),
                                 drvAudioCaptureStateName(pStreamEx->In.enmCaptureState)));
                    }

                    rc = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_ENABLE);
                    if (RT_SUCCESS(rc))
                    {
                        pStreamEx->nsStarted = RTTimeNanoTS();
                        pStreamEx->fStatus |= PDMAUDIOSTREAM_STS_ENABLED;
                        PDMAUDIOSTREAM_STS_ASSERT_VALID(pStreamEx->fStatus);
                    }
                }
            }
            break;

        case PDMAUDIOSTREAMCMD_DISABLE:
#ifndef DRVAUDIO_WITH_STREAM_DESTRUCTION_IN_DISABLED_DIRECTION
            if (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_ENABLED)
            {
                rc = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_DISABLE);
                LogFunc(("DISABLE '%s': Backend DISABLE -> %Rrc\n", pStreamEx->Core.Cfg.szName, rc));
                if (RT_SUCCESS(rc)) /** @todo ignore this and reset it anyway? */
                    drvAudioStreamResetOnDisable(pStreamEx);
            }
#else
            if (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_CREATED)
                rc = drvAudioStreamDestroyInternalBackend(pThis, pStreamEx);
#endif /* DRVAUDIO_WITH_STREAM_DESTRUCTION_IN_DISABLED_DIRECTION */
            break;

        case PDMAUDIOSTREAMCMD_PAUSE:
            if ((pStreamEx->fStatus & (PDMAUDIOSTREAM_STS_ENABLED | PDMAUDIOSTREAM_STS_PAUSED)) == PDMAUDIOSTREAM_STS_ENABLED)
            {
                rc = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_PAUSE);
                if (RT_SUCCESS(rc))
                {
                    pStreamEx->fStatus |= PDMAUDIOSTREAM_STS_PAUSED;
                    PDMAUDIOSTREAM_STS_ASSERT_VALID(pStreamEx->fStatus);
                }
            }
            break;

        case PDMAUDIOSTREAMCMD_RESUME:
            if (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_PAUSED)
            {
                Assert(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_ENABLED);
                rc = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_RESUME);
                if (RT_SUCCESS(rc))
                {
                    pStreamEx->fStatus &= ~PDMAUDIOSTREAM_STS_PAUSED;
                    PDMAUDIOSTREAM_STS_ASSERT_VALID(pStreamEx->fStatus);
                }
            }
            break;

        case PDMAUDIOSTREAMCMD_DRAIN:
            /*
             * Only for output streams and we don't want this command more than once.
             */
            AssertReturn(pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_OUT, VERR_INVALID_FUNCTION);
            AssertBreak(!(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_PENDING_DISABLE));
            if (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_ENABLED)
            {
                rc = VERR_INTERNAL_ERROR_2;
                switch (pStreamEx->Out.enmPlayState)
                {
                    case DRVAUDIOPLAYSTATE_PREBUF:
                        if (pStreamEx->Out.cbPreBuffered > 0)
                        {
                            LogFunc(("DRAIN '%s': Initiating draining of pre-buffered data...\n", pStreamEx->Core.Cfg.szName));
                            pStreamEx->Out.enmPlayState = DRVAUDIOPLAYSTATE_PREBUF_COMMITTING;
                            pStreamEx->fStatus |= PDMAUDIOSTREAM_STS_PENDING_DISABLE;
                            PDMAUDIOSTREAM_STS_ASSERT_VALID(pStreamEx->fStatus);
                            rc = VINF_SUCCESS;
                            break;
                        }
                        RT_FALL_THROUGH();
                    case DRVAUDIOPLAYSTATE_NOPLAY:
                    case DRVAUDIOPLAYSTATE_PREBUF_SWITCHING:
                    case DRVAUDIOPLAYSTATE_PREBUF_OVERDUE:
                        LogFunc(("DRAIN '%s': Nothing to drain (enmPlayState=%s)\n",
                                 pStreamEx->Core.Cfg.szName, drvAudioPlayStateName(pStreamEx->Out.enmPlayState)));
                        rc = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_DISABLE);
                        AssertRC(rc);
                        drvAudioStreamResetOnDisable(pStreamEx);
                        break;

                    case DRVAUDIOPLAYSTATE_PLAY:
                    case DRVAUDIOPLAYSTATE_PLAY_PREBUF:
                        LogFunc(("DRAIN '%s': Initiating backend draining (enmPlayState=%s -> NOPLAY) ...\n",
                                 pStreamEx->Core.Cfg.szName, drvAudioPlayStateName(pStreamEx->Out.enmPlayState)));
                        pStreamEx->Out.enmPlayState = DRVAUDIOPLAYSTATE_NOPLAY;
                        rc = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_DRAIN);
                        if (RT_SUCCESS(rc))
                        {
                            pStreamEx->fStatus |= PDMAUDIOSTREAM_STS_PENDING_DISABLE;
                            PDMAUDIOSTREAM_STS_ASSERT_VALID(pStreamEx->fStatus);
                        }
                        else
                        {
                            LogFunc(("DRAIN '%s': Backend DRAIN failed with %Rrc, disabling the stream instead...\n",
                                     pStreamEx->Core.Cfg.szName, rc));
                            rc = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_DISABLE);
                            AssertRC(rc);
                            drvAudioStreamResetOnDisable(pStreamEx);
                        }
                        break;

                    case DRVAUDIOPLAYSTATE_PREBUF_COMMITTING:
                        LogFunc(("DRAIN '%s': Initiating draining of pre-buffered data (already committing)...\n",
                                 pStreamEx->Core.Cfg.szName));
                        pStreamEx->fStatus |= PDMAUDIOSTREAM_STS_PENDING_DISABLE;
                        PDMAUDIOSTREAM_STS_ASSERT_VALID(pStreamEx->fStatus);
                        rc = VINF_SUCCESS;
                        break;

                    /* no default */
                    case DRVAUDIOPLAYSTATE_INVALID:
                    case DRVAUDIOPLAYSTATE_END:
                        AssertFailedBreak();
                }
            }
            break;

        default:
            rc = VERR_NOT_IMPLEMENTED;
            break;
    }

    if (RT_FAILURE(rc))
        LogFunc(("[%s] Failed with %Rrc\n", pStreamEx->Core.Cfg.szName, rc));

    return rc;
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnStreamControl}
 */
static DECLCALLBACK(int) drvAudioStreamControl(PPDMIAUDIOCONNECTOR pInterface,
                                               PPDMAUDIOSTREAM pStream, PDMAUDIOSTREAMCMD enmStreamCmd)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    AssertPtr(pThis);

    /** @todo r=bird: why?  It's not documented to ignore NULL streams.   */
    if (!pStream)
        return VINF_SUCCESS;
    PDRVAUDIOSTREAM pStreamEx = (PDRVAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamEx, VERR_INVALID_POINTER);
    AssertReturn(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pStreamEx->uMagic      == DRVAUDIOSTREAM_MAGIC, VERR_INVALID_MAGIC);

    int rc = RTCritSectEnter(&pStreamEx->Core.CritSect);
    AssertRCReturn(rc, rc);

    LogFlowFunc(("[%s] enmStreamCmd=%s\n", pStreamEx->Core.Cfg.szName, PDMAudioStrmCmdGetName(enmStreamCmd)));

    rc = drvAudioStreamControlInternal(pThis, pStreamEx, enmStreamCmd);

    RTCritSectLeave(&pStreamEx->Core.CritSect);
    return rc;
}


/**
 * Copy data to the pre-buffer, ring-buffer style.
 *
 * The @a cbMax parameter is almost always set to the threshold size, the
 * exception is when commiting the buffer and we want to top it off to reduce
 * the number of transfers to the backend (the first transfer may start
 * playback, so more data is better).
 */
static int drvAudioStreamPreBuffer(PDRVAUDIOSTREAM pStreamEx, const uint8_t *pbBuf, uint32_t cbBuf, uint32_t cbMax)
{
    uint32_t const cbAlloc = pStreamEx->Out.cbPreBufAlloc;
    AssertReturn(cbAlloc >= cbMax, VERR_INTERNAL_ERROR_3);
    AssertReturn(cbAlloc >= 8, VERR_INTERNAL_ERROR_4);
    AssertReturn(cbMax >= 8, VERR_INTERNAL_ERROR_5);

    uint32_t offRead = pStreamEx->Out.offPreBuf;
    uint32_t cbCur   = pStreamEx->Out.cbPreBuffered;
    AssertStmt(offRead < cbAlloc, offRead %= cbAlloc);
    AssertStmt(cbCur <= cbMax, offRead = (offRead + cbCur - cbMax) % cbAlloc; cbCur = cbMax);

    /*
     * First chunk.
     */
    uint32_t offWrite = (offRead + cbCur) % cbAlloc;
    uint32_t cbToCopy = RT_MIN(cbAlloc - offWrite, cbBuf);
    memcpy(&pStreamEx->Out.pbPreBuf[offWrite], pbBuf, cbToCopy);

    /* Advance. */
    offWrite = (offWrite + cbToCopy) % cbAlloc;
    for (;;)
    {
        pbBuf    += cbToCopy;
        cbCur    += cbToCopy;
        if (cbCur > cbMax)
            offRead = (offRead + cbCur - cbMax) % cbAlloc;
        cbBuf    -= cbToCopy;
        if (!cbBuf)
            break;

        /*
         * Second+ chunk, from the start of the buffer.
         *
         * Note! It is assumed very unlikely that we will ever see a cbBuf larger than
         *       cbMax, so we don't waste space on clipping cbBuf here (can happen with
         *       custom pre-buffer sizes).
         */
        Assert(offWrite == 0);
        cbToCopy = RT_MIN(cbAlloc, cbBuf);
        memcpy(pStreamEx->Out.pbPreBuf, pbBuf, cbToCopy);
    }

    /*
     * Update the pre-buffering size and position.
     */
    pStreamEx->Out.cbPreBuffered = RT_MIN(cbCur, cbMax);
    pStreamEx->Out.offPreBuf     = offRead;
    return VINF_SUCCESS;
}


/**
 * Worker for drvAudioStreamPlay() and drvAudioStreamPreBufComitting().
 *
 * Caller owns the lock.
 */
static int drvAudioStreamPlayLocked(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx,
                                    const uint8_t *pbBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    Log3Func(("%s: @%#RX64: cbBuf=%#x\n", pStreamEx->Core.Cfg.szName, pStreamEx->offInternal, cbBuf));

    uint32_t      cbWritable = pThis->pHostDrvAudio->pfnStreamGetWritable(pThis->pHostDrvAudio, pStreamEx->pBackend);
    pStreamEx->Out.Stats.cbBackendWritableBefore = cbWritable;

    uint32_t      cbWritten  = 0;
    int           rc         = VINF_SUCCESS;
    uint8_t const cbFrame    = PDMAudioPropsFrameSize(&pStreamEx->Core.Cfg.Props);
    while (cbBuf >= cbFrame && cbWritable >= cbFrame)
    {
        uint32_t const cbToWrite    = PDMAudioPropsFloorBytesToFrame(&pStreamEx->Core.Cfg.Props, RT_MIN(cbBuf, cbWritable));
        uint32_t       cbWrittenNow = 0;
        rc = pThis->pHostDrvAudio->pfnStreamPlay(pThis->pHostDrvAudio, pStreamEx->pBackend, pbBuf, cbToWrite, &cbWrittenNow);
        if (RT_SUCCESS(rc))
        {
            if (cbWrittenNow != cbToWrite)
                Log3Func(("%s: @%#RX64: Wrote fewer bytes than requested: %#x, requested %#x\n",
                          pStreamEx->Core.Cfg.szName, pStreamEx->offInternal, cbWrittenNow, cbToWrite));
#ifdef DEBUG_bird
            Assert(cbWrittenNow == cbToWrite);
#endif
            AssertStmt(cbWrittenNow <= cbToWrite, cbWrittenNow = cbToWrite);
            cbWritten              += cbWrittenNow;
            cbBuf                  -= cbWrittenNow;
            pbBuf                  += cbWrittenNow;
            pStreamEx->offInternal += cbWrittenNow;
        }
        else
        {
            *pcbWritten = cbWritten;
            LogFunc(("%s: @%#RX64: pfnStreamPlay failed writing %#x bytes (%#x previous written, %#x writable): %Rrc\n",
                     pStreamEx->Core.Cfg.szName, pStreamEx->offInternal, cbToWrite, cbWritten, cbWritable, rc));
            return cbWritten ? VINF_SUCCESS : rc;
        }

        cbWritable = pThis->pHostDrvAudio->pfnStreamGetWritable(pThis->pHostDrvAudio, pStreamEx->pBackend);
    }

    STAM_PROFILE_ADD_PERIOD(&pStreamEx->StatXfer, cbWritten);
    *pcbWritten = cbWritten;
    pStreamEx->Out.Stats.cbBackendWritableAfter = cbWritable;
    if (cbWritten)
        pStreamEx->nsLastPlayedCaptured = RTTimeNanoTS();

    Log3Func(("%s: @%#RX64: Wrote %#x bytes (%#x bytes left)\n", pStreamEx->Core.Cfg.szName, pStreamEx->offInternal, cbWritten, cbBuf));
    return rc;
}


/**
 * Worker for drvAudioStreamPlay() and drvAudioStreamPreBufComitting().
 */
static int drvAudioStreamPlayToPreBuffer(PDRVAUDIOSTREAM pStreamEx, const void *pvBuf, uint32_t cbBuf, uint32_t cbMax,
                                         uint32_t *pcbWritten)
{
    int rc = drvAudioStreamPreBuffer(pStreamEx, (uint8_t const *)pvBuf, cbBuf, cbMax);
    if (RT_SUCCESS(rc))
    {
        *pcbWritten = cbBuf;
        pStreamEx->offInternal += cbBuf;
        Log3Func(("[%s] Pre-buffering (%s): wrote %#x bytes => %#x bytes / %u%%\n",
                  pStreamEx->Core.Cfg.szName, drvAudioPlayStateName(pStreamEx->Out.enmPlayState), cbBuf, pStreamEx->Out.cbPreBuffered,
                  pStreamEx->Out.cbPreBuffered * 100 / RT_MAX(pStreamEx->cbPreBufThreshold, 1)));

    }
    else
        *pcbWritten = 0;
    return rc;
}


/**
 * Used when we're committing (transfering) the pre-buffered bytes to the
 * device.
 *
 * This is called both from drvAudioStreamPlay() and
 * drvAudioStreamIterateInternal().
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to the DrvAudio instance data.
 * @param   pStreamEx   The stream to commit the pre-buffering for.
 * @param   pbBuf       Buffer with new bytes to write.  Can be NULL when called
 *                      in the PENDING_DISABLE state from
 *                      drvAudioStreamIterateInternal().
 * @param   cbBuf       Number of new bytes.  Can be zero.
 * @param   pcbWritten  Where to return the number of bytes written.
 *
 * @note    Locking: Stream critsect and hot-plug in shared mode.
 */
static int drvAudioStreamPreBufComitting(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx,
                                         const uint8_t *pbBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    /*
     * First, top up the buffer with new data from pbBuf.
     */
    *pcbWritten = 0;
    if (cbBuf > 0)
    {
        uint32_t const cbToCopy = RT_MIN(pStreamEx->Out.cbPreBufAlloc - pStreamEx->Out.cbPreBuffered, cbBuf);
        if (cbToCopy > 0)
        {
            int rc = drvAudioStreamPlayToPreBuffer(pStreamEx, pbBuf, cbBuf, pStreamEx->Out.cbPreBufAlloc, pcbWritten);
            AssertRCReturn(rc, rc);
            pbBuf += cbToCopy;
            cbBuf -= cbToCopy;
        }
    }

    AssertReturn(pThis->pHostDrvAudio, VERR_AUDIO_BACKEND_NOT_ATTACHED);

    /*
     * Write the pre-buffered chunk.
     */
    int             rc      = VINF_SUCCESS;
    uint32_t const  cbAlloc = pStreamEx->Out.cbPreBufAlloc;
    AssertReturn(cbAlloc > 0, VERR_INTERNAL_ERROR_2);
    uint32_t        off     = pStreamEx->Out.offPreBuf;
    AssertStmt(off < pStreamEx->Out.cbPreBufAlloc, off %= cbAlloc);
    uint32_t        cbLeft  = pStreamEx->Out.cbPreBuffered;
    while (cbLeft > 0)
    {
        uint32_t const cbToWrite = RT_MIN(cbAlloc - off, cbLeft);
        Assert(cbToWrite > 0);

        uint32_t cbPreBufWritten = 0;
        rc = pThis->pHostDrvAudio->pfnStreamPlay(pThis->pHostDrvAudio, pStreamEx->pBackend, &pStreamEx->Out.pbPreBuf[off],
                                                 cbToWrite, &cbPreBufWritten);
        if (   RT_FAILURE(rc)
            || !cbPreBufWritten)
            break;
        AssertStmt(cbPreBufWritten <= cbToWrite, cbPreBufWritten = cbToWrite);
        off     = (off + cbPreBufWritten) % cbAlloc;
        cbLeft -= cbPreBufWritten;
    }

    if (cbLeft == 0)
    {
        LogFunc(("@%#RX64: Wrote all %#x bytes of pre-buffered audio data. %s -> PLAY\n", pStreamEx->offInternal,
                 pStreamEx->Out.cbPreBuffered, drvAudioPlayStateName(pStreamEx->Out.enmPlayState) ));
        pStreamEx->Out.cbPreBuffered = 0;
        pStreamEx->Out.offPreBuf     = 0;
        pStreamEx->Out.enmPlayState  = DRVAUDIOPLAYSTATE_PLAY;

        if (cbBuf > 0)
        {
            uint32_t cbWritten2 = 0;
            rc = drvAudioStreamPlayLocked(pThis, pStreamEx, pbBuf, cbBuf, &cbWritten2);
            if (RT_SUCCESS(rc))
                *pcbWritten += cbWritten2;
        }
        else
            pStreamEx->nsLastPlayedCaptured = RTTimeNanoTS();
    }
    else
    {
        if (cbLeft != pStreamEx->Out.cbPreBuffered)
            pStreamEx->nsLastPlayedCaptured = RTTimeNanoTS();

        LogRel2(("Audio: @%#RX64: Stream '%s' pre-buffering commit problem: wrote %#x out of %#x + %#x - rc=%Rrc *pcbWritten=%#x %s -> PREBUF_COMMITTING\n",
                 pStreamEx->offInternal, pStreamEx->Core.Cfg.szName, pStreamEx->Out.cbPreBuffered - cbLeft,
                 pStreamEx->Out.cbPreBuffered, cbBuf, rc, *pcbWritten, drvAudioPlayStateName(pStreamEx->Out.enmPlayState) ));
        AssertMsg(   pStreamEx->Out.enmPlayState == DRVAUDIOPLAYSTATE_PREBUF_COMMITTING
                  || pStreamEx->Out.enmPlayState == DRVAUDIOPLAYSTATE_PREBUF
                  || RT_FAILURE(rc),
                  ("Buggy host driver buffer reporting? cbLeft=%#x cbPreBuffered=%#x enmPlayState=%s\n",
                   cbLeft, pStreamEx->Out.cbPreBuffered, drvAudioPlayStateName(pStreamEx->Out.enmPlayState) ));

        pStreamEx->Out.cbPreBuffered = cbLeft;
        pStreamEx->Out.offPreBuf     = off;
        pStreamEx->Out.enmPlayState  = DRVAUDIOPLAYSTATE_PREBUF_COMMITTING;
    }

    return *pcbWritten ? VINF_SUCCESS : rc;
}


/**
 * Does one iteration of an audio stream.
 *
 * This function gives the backend the chance of iterating / altering data and
 * does the actual mixing between the guest <-> host mixing buffers.
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to driver instance.
 * @param   pStreamEx   Stream to iterate.
 */
static int drvAudioStreamIterateInternal(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx)
{
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

#ifdef LOG_ENABLED
    char szStreamSts[DRVAUDIO_STATUS_STR_MAX];
#endif
    Log3Func(("[%s] fStatus=%s\n", pStreamEx->Core.Cfg.szName, drvAudioStreamStatusToStr(szStreamSts, pStreamEx->fStatus)));

    /* Not enabled or paused? Skip iteration. */
    if ((pStreamEx->fStatus & (PDMAUDIOSTREAM_STS_ENABLED | PDMAUDIOSTREAM_STS_PAUSED)) != PDMAUDIOSTREAM_STS_ENABLED)
        return VINF_SUCCESS;

    /*
     * Pending disable is really what we're here for.
     *
     * This only happens to output streams.  We ASSUME the caller (MixerBuffer)
     * implements a timeout on the draining, so we skip that here.
     */
    if (!(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_PENDING_DISABLE))
    { /* likely until we get to the end of the stream at least. */ }
    else
    {
        AssertReturn(pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_OUT, VINF_SUCCESS);
        RTCritSectRwEnterShared(&pThis->CritSectHotPlug);

        /*
         * Move pre-buffered samples to the backend.
         */
        if (pStreamEx->Out.enmPlayState == DRVAUDIOPLAYSTATE_PREBUF_COMMITTING)
        {
            if (pStreamEx->Out.cbPreBuffered > 0)
            {
                uint32_t cbIgnored = 0;
                drvAudioStreamPreBufComitting(pThis, pStreamEx, NULL, 0, &cbIgnored);
                Log3Func(("Stream '%s': Transferred %#x bytes\n", pStreamEx->Core.Cfg.szName, cbIgnored));
            }
            if (pStreamEx->Out.cbPreBuffered == 0)
            {
                Log3Func(("Stream '%s': No more pre-buffered data -> NOPLAY + backend DRAIN\n", pStreamEx->Core.Cfg.szName));
                pStreamEx->Out.enmPlayState = DRVAUDIOPLAYSTATE_NOPLAY;

                int rc = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_DRAIN);
                if (RT_FAILURE(rc))
                {
                    LogFunc(("Stream '%s': Backend DRAIN failed with %Rrc, disabling the stream instead...\n",
                             pStreamEx->Core.Cfg.szName, rc));
                    rc = drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_DISABLE);
                    AssertRC(rc);
                    drvAudioStreamResetOnDisable(pStreamEx);
                }
            }
        }
        else
            Assert(pStreamEx->Out.enmPlayState == DRVAUDIOPLAYSTATE_NOPLAY);

        /*
         * Check the backend status to see if it's still draining and to
         * update our status when it stops doing so.
         */
        PDMHOSTAUDIOSTREAMSTATE const enmBackendState = drvAudioStreamGetBackendState(pThis, pStreamEx);
        if (enmBackendState == PDMHOSTAUDIOSTREAMSTATE_DRAINING)
        {
            uint32_t cbIgnored = 0;
            pThis->pHostDrvAudio->pfnStreamPlay(pThis->pHostDrvAudio, pStreamEx->pBackend, NULL, 0, &cbIgnored);
        }
        else
        {
            LogFunc(("Stream '%s': Backend finished draining.\n", pStreamEx->Core.Cfg.szName));
            drvAudioStreamResetOnDisable(pStreamEx);
        }

        RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    }

    /* Update timestamps. */
    pStreamEx->nsLastIterated = RTTimeNanoTS();

    return VINF_SUCCESS; /** @todo r=bird: What can the caller do with an error status here? */
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnStreamIterate}
 */
static DECLCALLBACK(int) drvAudioStreamIterate(PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    AssertPtr(pThis);
    PDRVAUDIOSTREAM pStreamEx = (PDRVAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamEx, VERR_INVALID_POINTER);
    AssertReturn(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pStreamEx->uMagic      == DRVAUDIOSTREAM_MAGIC, VERR_INVALID_MAGIC);

    int rc = RTCritSectEnter(&pStreamEx->Core.CritSect);
    AssertRCReturn(rc, rc);

    rc = drvAudioStreamIterateInternal(pThis, pStreamEx);

    RTCritSectLeave(&pStreamEx->Core.CritSect);

    if (RT_FAILURE(rc))
        LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnStreamGetState}
 */
static DECLCALLBACK(PDMAUDIOSTREAMSTATE) drvAudioStreamGetState(PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream)
{
    PDRVAUDIO       pThis     = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    PDRVAUDIOSTREAM pStreamEx = (PDRVAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamEx, PDMAUDIOSTREAMSTATE_INVALID);
    AssertReturn(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC, PDMAUDIOSTREAMSTATE_INVALID);
    AssertReturn(pStreamEx->uMagic      == DRVAUDIOSTREAM_MAGIC, PDMAUDIOSTREAMSTATE_INVALID);
    STAM_PROFILE_START(&pStreamEx->StatProfGetState, a);

    /*
     * Get the status mask.
     */
    int rc = RTCritSectEnter(&pStreamEx->Core.CritSect);
    AssertRCReturn(rc, PDMAUDIOSTREAMSTATE_INVALID);
    RTCritSectRwEnterShared(&pThis->CritSectHotPlug);

    PDMHOSTAUDIOSTREAMSTATE const enmBackendState = drvAudioStreamGetBackendStateAndProcessChanges(pThis, pStreamEx);
    uint32_t const                fStrmStatus     = pStreamEx->fStatus;
    PDMAUDIODIR const             enmDir          = pStreamEx->Core.Cfg.enmDir;
    Assert(enmDir == PDMAUDIODIR_IN || enmDir == PDMAUDIODIR_OUT);

    RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    RTCritSectLeave(&pStreamEx->Core.CritSect);

    /*
     * Translate it to state enum value.
     */
    PDMAUDIOSTREAMSTATE enmState;
    if (!(fStrmStatus & PDMAUDIOSTREAM_STS_NEED_REINIT))
    {
        if (fStrmStatus & PDMAUDIOSTREAM_STS_BACKEND_CREATED)
        {
            if (   (fStrmStatus & PDMAUDIOSTREAM_STS_ENABLED)
                && drvAudioStreamIsDirectionEnabled(pThis, pStreamEx->Core.Cfg.enmDir)
                && (   enmBackendState == PDMHOSTAUDIOSTREAMSTATE_OKAY
                    || enmBackendState == PDMHOSTAUDIOSTREAMSTATE_DRAINING
                    || enmBackendState == PDMHOSTAUDIOSTREAMSTATE_INITIALIZING ))
                enmState = enmDir == PDMAUDIODIR_IN ? PDMAUDIOSTREAMSTATE_ENABLED_READABLE : PDMAUDIOSTREAMSTATE_ENABLED_WRITABLE;
            else
                enmState = PDMAUDIOSTREAMSTATE_INACTIVE;
        }
        else
            enmState = PDMAUDIOSTREAMSTATE_NOT_WORKING;
    }
    else
        enmState = PDMAUDIOSTREAMSTATE_NEED_REINIT;

    STAM_PROFILE_STOP(&pStreamEx->StatProfGetState, a);
#ifdef LOG_ENABLED
    char szStreamSts[DRVAUDIO_STATUS_STR_MAX];
#endif
    Log3Func(("[%s] returns %s (status: %s)\n", pStreamEx->Core.Cfg.szName, PDMAudioStreamStateGetName(enmState),
              drvAudioStreamStatusToStr(szStreamSts, fStrmStatus)));
    return enmState;
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnStreamGetWritable}
 */
static DECLCALLBACK(uint32_t) drvAudioStreamGetWritable(PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    AssertPtr(pThis);
    PDRVAUDIOSTREAM pStreamEx = (PDRVAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamEx, 0);
    AssertReturn(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC, 0);
    AssertReturn(pStreamEx->uMagic      == DRVAUDIOSTREAM_MAGIC, 0);
    AssertMsgReturn(pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_OUT, ("Can't write to a non-output stream\n"), 0);
    STAM_PROFILE_START(&pStreamEx->Out.Stats.ProfGetWritable, a);

    int rc = RTCritSectEnter(&pStreamEx->Core.CritSect);
    AssertRCReturn(rc, 0);
    RTCritSectRwEnterShared(&pThis->CritSectHotPlug);

    /*
     * Use the playback and backend states to determin how much can be written, if anything.
     */
    uint32_t cbWritable = 0;
    DRVAUDIOPLAYSTATE const       enmPlayMode     = pStreamEx->Out.enmPlayState;
    PDMHOSTAUDIOSTREAMSTATE const enmBackendState = drvAudioStreamGetBackendState(pThis, pStreamEx);
    if (   PDMAudioStrmStatusCanWrite(pStreamEx->fStatus)
        && pThis->pHostDrvAudio != NULL
        && enmBackendState != PDMHOSTAUDIOSTREAMSTATE_DRAINING)
    {
        switch (enmPlayMode)
        {
            /*
             * Whatever the backend can hold.
             */
            case DRVAUDIOPLAYSTATE_PLAY:
            case DRVAUDIOPLAYSTATE_PLAY_PREBUF:
                Assert(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_READY);
                Assert(enmBackendState == PDMHOSTAUDIOSTREAMSTATE_OKAY /* potential unplug race */);
                cbWritable = pThis->pHostDrvAudio->pfnStreamGetWritable(pThis->pHostDrvAudio, pStreamEx->pBackend);
                break;

            /*
             * Whatever we've got of available space in the pre-buffer.
             * Note! For the last round when we pass the pre-buffering threshold, we may
             *       report fewer bytes than what a DMA timer period for the guest device
             *       typically produces, however that should be transfered in the following
             *       round that goes directly to the backend buffer.
             */
            case DRVAUDIOPLAYSTATE_PREBUF:
                cbWritable = pStreamEx->Out.cbPreBufAlloc - pStreamEx->Out.cbPreBuffered;
                if (!cbWritable)
                    cbWritable = PDMAudioPropsFramesToBytes(&pStreamEx->Core.Cfg.Props, 2);
                break;

            /*
             * These are slightly more problematic and can go wrong if the pre-buffer is
             * manually configured to be smaller than the output of a typeical DMA timer
             * period for the guest device.  So, to overcompensate, we just report back
             * the backend buffer size (the pre-buffer is circular, so no overflow issue).
             */
            case DRVAUDIOPLAYSTATE_PREBUF_OVERDUE:
            case DRVAUDIOPLAYSTATE_PREBUF_SWITCHING:
                cbWritable = PDMAudioPropsFramesToBytes(&pStreamEx->Core.Cfg.Props,
                                                        RT_MAX(pStreamEx->Core.Cfg.Backend.cFramesBufferSize,
                                                               pStreamEx->Core.Cfg.Backend.cFramesPreBuffering));
                break;

            case DRVAUDIOPLAYSTATE_PREBUF_COMMITTING:
            {
                /* Buggy backend: We weren't able to copy all the pre-buffered data to it
                   when reaching the threshold.  Try escape this situation, or at least
                   keep the extra buffering to a minimum.  We must try write something
                   as long as there is space for it, as we need the pfnStreamWrite call
                   to move the data. */
                Assert(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_READY);
                Assert(enmBackendState == PDMHOSTAUDIOSTREAMSTATE_OKAY /* potential unplug race */);
                uint32_t const cbMin = PDMAudioPropsFramesToBytes(&pStreamEx->Core.Cfg.Props, 8);
                cbWritable = pThis->pHostDrvAudio->pfnStreamGetWritable(pThis->pHostDrvAudio, pStreamEx->pBackend);
                if (cbWritable >= pStreamEx->Out.cbPreBuffered + cbMin)
                    cbWritable -= pStreamEx->Out.cbPreBuffered + cbMin / 2;
                else
                    cbWritable = RT_MIN(cbMin, pStreamEx->Out.cbPreBufAlloc - pStreamEx->Out.cbPreBuffered);
                AssertLogRel(cbWritable);
                break;
            }

            case DRVAUDIOPLAYSTATE_NOPLAY:
                break;
            case DRVAUDIOPLAYSTATE_INVALID:
            case DRVAUDIOPLAYSTATE_END:
                AssertFailed();
                break;
        }

        /* Make sure to align the writable size to the host's frame size. */
        cbWritable = PDMAudioPropsFloorBytesToFrame(&pStreamEx->Core.Cfg.Props, cbWritable);
    }

    RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    STAM_PROFILE_ADD_PERIOD(&pStreamEx->Out.Stats.ProfGetWritableBytes, cbWritable);
    STAM_PROFILE_STOP(&pStreamEx->Out.Stats.ProfGetWritable, a);
    RTCritSectLeave(&pStreamEx->Core.CritSect);
    Log3Func(("[%s] cbWritable=%#RX32 (%RU64ms) enmPlayMode=%s enmBackendState=%s\n",
              pStreamEx->Core.Cfg.szName, cbWritable, PDMAudioPropsBytesToMilli(&pStreamEx->Core.Cfg.Props, cbWritable),
              drvAudioPlayStateName(enmPlayMode), PDMHostAudioStreamStateGetName(enmBackendState) ));
    return cbWritable;
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnStreamPlay}
 */
static DECLCALLBACK(int) drvAudioStreamPlay(PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream,
                                            const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    AssertPtr(pThis);

    /*
     * Check input and sanity.
     */
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    PDRVAUDIOSTREAM pStreamEx = (PDRVAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamEx, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);
    uint32_t uTmp;
    if (pcbWritten)
        AssertPtrReturn(pcbWritten, VERR_INVALID_PARAMETER);
    else
        pcbWritten = &uTmp;

    AssertReturn(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pStreamEx->uMagic      == DRVAUDIOSTREAM_MAGIC, VERR_INVALID_MAGIC);
    AssertMsgReturn(pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_OUT,
                    ("Stream '%s' is not an output stream and therefore cannot be written to (direction is '%s')\n",
                     pStreamEx->Core.Cfg.szName, PDMAudioDirGetName(pStreamEx->Core.Cfg.enmDir)), VERR_ACCESS_DENIED);

    AssertMsg(PDMAudioPropsIsSizeAligned(&pStreamEx->Core.Cfg.Props, cbBuf),
              ("Stream '%s' got a non-frame-aligned write (%#RX32 bytes)\n", pStreamEx->Core.Cfg.szName, cbBuf));
    STAM_PROFILE_START(&pStreamEx->Out.Stats.ProfPlay, a);

    int rc = RTCritSectEnter(&pStreamEx->Core.CritSect);
    AssertRCReturn(rc, rc);

    /*
     * First check that we can write to the stream, and if not,
     * whether to just drop the input into the bit bucket.
     */
    if (PDMAudioStrmStatusIsReady(pStreamEx->fStatus))
    {
        RTCritSectRwEnterShared(&pThis->CritSectHotPlug);
        if (   pThis->Out.fEnabled /* (see @bugref{9882}) */
            && pThis->pHostDrvAudio != NULL)
        {
            /*
             * Get the backend state and process changes to it since last time we checked.
             */
            PDMHOSTAUDIOSTREAMSTATE const enmBackendState = drvAudioStreamGetBackendStateAndProcessChanges(pThis, pStreamEx);

            /*
             * Do the transfering.
             */
            switch (pStreamEx->Out.enmPlayState)
            {
                case DRVAUDIOPLAYSTATE_PLAY:
                    Assert(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_READY);
                    Assert(enmBackendState == PDMHOSTAUDIOSTREAMSTATE_OKAY);
                    rc = drvAudioStreamPlayLocked(pThis, pStreamEx, (uint8_t const *)pvBuf, cbBuf, pcbWritten);
                    break;

                case DRVAUDIOPLAYSTATE_PLAY_PREBUF:
                    Assert(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_READY);
                    Assert(enmBackendState == PDMHOSTAUDIOSTREAMSTATE_OKAY);
                    rc = drvAudioStreamPlayLocked(pThis, pStreamEx, (uint8_t const *)pvBuf, cbBuf, pcbWritten);
                    drvAudioStreamPreBuffer(pStreamEx, (uint8_t const *)pvBuf, *pcbWritten, pStreamEx->cbPreBufThreshold);
                    break;

                case DRVAUDIOPLAYSTATE_PREBUF:
                    if (cbBuf + pStreamEx->Out.cbPreBuffered < pStreamEx->cbPreBufThreshold)
                        rc = drvAudioStreamPlayToPreBuffer(pStreamEx, pvBuf, cbBuf, pStreamEx->cbPreBufThreshold, pcbWritten);
                    else if (   enmBackendState == PDMHOSTAUDIOSTREAMSTATE_OKAY
                             && (pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_READY))
                    {
                        Log3Func(("[%s] Pre-buffering completing: cbBuf=%#x cbPreBuffered=%#x => %#x vs cbPreBufThreshold=%#x\n",
                                  pStreamEx->Core.Cfg.szName, cbBuf, pStreamEx->Out.cbPreBuffered,
                                  cbBuf + pStreamEx->Out.cbPreBuffered, pStreamEx->cbPreBufThreshold));
                        rc = drvAudioStreamPreBufComitting(pThis, pStreamEx, (uint8_t const *)pvBuf, cbBuf, pcbWritten);
                    }
                    else
                    {
                        Log3Func(("[%s] Pre-buffering completing but device not ready: cbBuf=%#x cbPreBuffered=%#x => %#x vs cbPreBufThreshold=%#x; PREBUF -> PREBUF_OVERDUE\n",
                                  pStreamEx->Core.Cfg.szName, cbBuf, pStreamEx->Out.cbPreBuffered,
                                  cbBuf + pStreamEx->Out.cbPreBuffered, pStreamEx->cbPreBufThreshold));
                        pStreamEx->Out.enmPlayState = DRVAUDIOPLAYSTATE_PREBUF_OVERDUE;
                        rc = drvAudioStreamPlayToPreBuffer(pStreamEx, pvBuf, cbBuf, pStreamEx->cbPreBufThreshold, pcbWritten);
                    }
                    break;

                case DRVAUDIOPLAYSTATE_PREBUF_OVERDUE:
                    Assert(   !(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_READY)
                           || enmBackendState != PDMHOSTAUDIOSTREAMSTATE_OKAY);
                    RT_FALL_THRU();
                case DRVAUDIOPLAYSTATE_PREBUF_SWITCHING:
                    rc = drvAudioStreamPlayToPreBuffer(pStreamEx, pvBuf, cbBuf, pStreamEx->cbPreBufThreshold, pcbWritten);
                    break;

                case DRVAUDIOPLAYSTATE_PREBUF_COMMITTING:
                    Assert(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_READY);
                    Assert(enmBackendState == PDMHOSTAUDIOSTREAMSTATE_OKAY);
                    rc = drvAudioStreamPreBufComitting(pThis, pStreamEx, (uint8_t const *)pvBuf, cbBuf, pcbWritten);
                    break;

                case DRVAUDIOPLAYSTATE_NOPLAY:
                    *pcbWritten = cbBuf;
                    pStreamEx->offInternal += cbBuf;
                    Log3Func(("[%s] Discarding the data, backend state: %s\n", pStreamEx->Core.Cfg.szName,
                              PDMHostAudioStreamStateGetName(enmBackendState) ));
                    break;

                default:
                    *pcbWritten = cbBuf;
                    AssertMsgFailedBreak(("%d; cbBuf=%#x\n", pStreamEx->Out.enmPlayState, cbBuf));
            }

            if (!pStreamEx->Out.Dbg.pFilePlay || RT_FAILURE(rc))
            { /* likely */ }
            else
                AudioHlpFileWrite(pStreamEx->Out.Dbg.pFilePlay, pvBuf, *pcbWritten);
        }
        else
        {
            *pcbWritten = cbBuf;
            pStreamEx->offInternal += cbBuf;
            Log3Func(("[%s] Backend stream %s, discarding the data\n", pStreamEx->Core.Cfg.szName,
                      !pThis->Out.fEnabled ? "disabled" : !pThis->pHostDrvAudio ? "not attached" : "not ready yet"));
        }
        RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    }
    else
        rc = VERR_AUDIO_STREAM_NOT_READY;

    STAM_PROFILE_STOP(&pStreamEx->Out.Stats.ProfPlay, a);
    RTCritSectLeave(&pStreamEx->Core.CritSect);
    return rc;
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnStreamGetReadable}
 */
static DECLCALLBACK(uint32_t) drvAudioStreamGetReadable(PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    AssertPtr(pThis);
    PDRVAUDIOSTREAM pStreamEx = (PDRVAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamEx, 0);
    AssertReturn(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC, 0);
    AssertReturn(pStreamEx->uMagic      == DRVAUDIOSTREAM_MAGIC, 0);
    AssertMsg(pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_IN, ("Can't read from a non-input stream\n"));
    STAM_PROFILE_START(&pStreamEx->In.Stats.ProfGetReadable, a);

    int rc = RTCritSectEnter(&pStreamEx->Core.CritSect);
    AssertRCReturn(rc, 0);
    RTCritSectRwEnterShared(&pThis->CritSectHotPlug);

    /*
     * Use the capture state to determin how much can be written, if anything.
     */
    uint32_t cbReadable = 0;
    DRVAUDIOCAPTURESTATE const    enmCaptureState = pStreamEx->In.enmCaptureState;
    PDMHOSTAUDIOSTREAMSTATE const enmBackendState = drvAudioStreamGetBackendState(pThis, pStreamEx); RT_NOREF(enmBackendState);
    if (   PDMAudioStrmStatusCanRead(pStreamEx->fStatus)
        && pThis->pHostDrvAudio != NULL)
    {
        switch (enmCaptureState)
        {
            /*
             * Whatever the backend has to offer when in capture mode.
             */
            case DRVAUDIOCAPTURESTATE_CAPTURING:
                Assert(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_READY);
                Assert(enmBackendState == PDMHOSTAUDIOSTREAMSTATE_OKAY /* potential unplug race */);
                cbReadable = pThis->pHostDrvAudio->pfnStreamGetReadable(pThis->pHostDrvAudio, pStreamEx->pBackend);
                break;

            /*
             * Same calculation as in drvAudioStreamCaptureSilence, only we cap it
             * at the pre-buffering threshold so we don't get into trouble when we
             * switch to capture mode between now and pfnStreamCapture.
             */
            case DRVAUDIOCAPTURESTATE_PREBUF:
            {
                uint64_t const cNsStream = RTTimeNanoTS() - pStreamEx->nsStarted;
                uint64_t const offCur    = PDMAudioPropsNanoToBytes64(&pStreamEx->Core.Cfg.Props, cNsStream);
                if (offCur > pStreamEx->offInternal)
                {
                    uint64_t const cbUnread = offCur - pStreamEx->offInternal;
                    cbReadable = (uint32_t)RT_MIN(pStreamEx->cbPreBufThreshold, cbUnread);
                }
                break;
            }

            case DRVAUDIOCAPTURESTATE_NO_CAPTURE:
                break;

            case DRVAUDIOCAPTURESTATE_INVALID:
            case DRVAUDIOCAPTURESTATE_END:
                AssertFailed();
                break;
        }

        /* Make sure to align the readable size to the host's frame size. */
        cbReadable = PDMAudioPropsFloorBytesToFrame(&pStreamEx->Core.Cfg.Props, cbReadable);
    }

    RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    STAM_PROFILE_ADD_PERIOD(&pStreamEx->In.Stats.ProfGetReadableBytes, cbReadable);
    STAM_PROFILE_STOP(&pStreamEx->In.Stats.ProfGetReadable, a);
    RTCritSectLeave(&pStreamEx->Core.CritSect);
    Log3Func(("[%s] cbReadable=%#RX32 (%RU64ms) enmCaptureMode=%s enmBackendState=%s\n",
              pStreamEx->Core.Cfg.szName, cbReadable, PDMAudioPropsBytesToMilli(&pStreamEx->Core.Cfg.Props, cbReadable),
              drvAudioCaptureStateName(enmCaptureState), PDMHostAudioStreamStateGetName(enmBackendState) ));
    return cbReadable;
}


/**
 * Worker for drvAudioStreamCapture that returns silence.
 *
 * The amount of silence returned is a function of how long the stream has been
 * enabled.
 *
 * @returns VINF_SUCCESS
 * @param   pStreamEx   The stream to commit the pre-buffering for.
 * @param   pbBuf       The output buffer.
 * @param   cbBuf       The size of the output buffer.
 * @param   pcbRead     Where to return the number of bytes actually read.
 */
static int drvAudioStreamCaptureSilence(PDRVAUDIOSTREAM pStreamEx, uint8_t *pbBuf, uint32_t cbBuf, uint32_t *pcbRead)
{
    /** @todo  Does not take paused time into account...  */
    uint64_t const cNsStream = RTTimeNanoTS() - pStreamEx->nsStarted;
    uint64_t const offCur    = PDMAudioPropsNanoToBytes64(&pStreamEx->Core.Cfg.Props, cNsStream);
    if (offCur > pStreamEx->offInternal)
    {
        uint64_t const cbUnread  = offCur - pStreamEx->offInternal;
        uint32_t const cbToClear = (uint32_t)RT_MIN(cbBuf, cbUnread);
        *pcbRead                 = cbToClear;
        pStreamEx->offInternal  += cbToClear;
        cbBuf                   -= cbToClear;
        PDMAudioPropsClearBuffer(&pStreamEx->Core.Cfg.Props, pbBuf, cbToClear,
                                 PDMAudioPropsBytesToFrames(&pStreamEx->Core.Cfg.Props, cbToClear));
    }
    else
        *pcbRead = 0;
    Log4Func(("%s: @%#RX64: Read %#x bytes of silence (%#x bytes left)\n",
              pStreamEx->Core.Cfg.szName, pStreamEx->offInternal, *pcbRead, cbBuf));
    return VINF_SUCCESS;
}


/**
 * Worker for drvAudioStreamCapture.
 */
static int drvAudioStreamCaptureLocked(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx,
                                       uint8_t *pbBuf, uint32_t cbBuf, uint32_t *pcbRead)
{
    Log4Func(("%s: @%#RX64: cbBuf=%#x\n", pStreamEx->Core.Cfg.szName, pStreamEx->offInternal, cbBuf));

    uint32_t      cbReadable = pThis->pHostDrvAudio->pfnStreamGetReadable(pThis->pHostDrvAudio, pStreamEx->pBackend);
    pStreamEx->In.Stats.cbBackendReadableBefore = cbReadable;

    uint32_t      cbRead     = 0;
    int           rc         = VINF_SUCCESS;
    uint8_t const cbFrame    = PDMAudioPropsFrameSize(&pStreamEx->Core.Cfg.Props);
    while (cbBuf >= cbFrame && cbReadable >= cbFrame)
    {
        uint32_t const cbToRead  = PDMAudioPropsFloorBytesToFrame(&pStreamEx->Core.Cfg.Props, RT_MIN(cbBuf, cbReadable));
        uint32_t       cbReadNow = 0;
        rc = pThis->pHostDrvAudio->pfnStreamCapture(pThis->pHostDrvAudio, pStreamEx->pBackend, pbBuf, cbToRead, &cbReadNow);
        if (RT_SUCCESS(rc))
        {
            if (cbReadNow != cbToRead)
                Log4Func(("%s: @%#RX64: Read fewer bytes than requested: %#x, requested %#x\n",
                          pStreamEx->Core.Cfg.szName, pStreamEx->offInternal, cbReadNow, cbToRead));
#ifdef DEBUG_bird
            Assert(cbReadNow == cbToRead);
#endif
            AssertStmt(cbReadNow <= cbToRead, cbReadNow = cbToRead);
            cbRead                 += cbReadNow;
            cbBuf                  -= cbReadNow;
            pbBuf                  += cbReadNow;
            pStreamEx->offInternal += cbReadNow;
        }
        else
        {
            *pcbRead = cbRead;
            LogFunc(("%s: @%#RX64: pfnStreamCapture failed read %#x bytes (%#x previous read, %#x readable): %Rrc\n",
                     pStreamEx->Core.Cfg.szName, pStreamEx->offInternal, cbToRead, cbRead, cbReadable, rc));
            return cbRead ? VINF_SUCCESS : rc;
        }

        cbReadable = pThis->pHostDrvAudio->pfnStreamGetReadable(pThis->pHostDrvAudio, pStreamEx->pBackend);
    }

    STAM_PROFILE_ADD_PERIOD(&pStreamEx->StatXfer, cbRead);
    *pcbRead = cbRead;
    pStreamEx->In.Stats.cbBackendReadableAfter = cbReadable;
    if (cbRead)
        pStreamEx->nsLastPlayedCaptured = RTTimeNanoTS();

    Log4Func(("%s: @%#RX64: Read %#x bytes (%#x bytes left)\n", pStreamEx->Core.Cfg.szName, pStreamEx->offInternal, cbRead, cbBuf));
    return rc;
}


/**
 * @interface_method_impl{PDMIAUDIOCONNECTOR,pfnStreamCapture}
 */
static DECLCALLBACK(int) drvAudioStreamCapture(PPDMIAUDIOCONNECTOR pInterface, PPDMAUDIOSTREAM pStream,
                                               void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IAudioConnector);
    AssertPtr(pThis);

    /*
     * Check input and sanity.
     */
    AssertPtrReturn(pInterface, VERR_INVALID_POINTER);
    PDRVAUDIOSTREAM pStreamEx = (PDRVAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamEx, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);
    uint32_t uTmp;
    if (pcbRead)
        AssertPtrReturn(pcbRead, VERR_INVALID_PARAMETER);
    else
        pcbRead = &uTmp;

    AssertReturn(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pStreamEx->uMagic      == DRVAUDIOSTREAM_MAGIC, VERR_INVALID_MAGIC);
    AssertMsgReturn(pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_IN,
                    ("Stream '%s' is not an input stream and therefore cannot be read from (direction is '%s')\n",
                     pStreamEx->Core.Cfg.szName, PDMAudioDirGetName(pStreamEx->Core.Cfg.enmDir)), VERR_ACCESS_DENIED);

    AssertMsg(PDMAudioPropsIsSizeAligned(&pStreamEx->Core.Cfg.Props, cbBuf),
              ("Stream '%s' got a non-frame-aligned write (%#RX32 bytes)\n", pStreamEx->Core.Cfg.szName, cbBuf));
    STAM_PROFILE_START(&pStreamEx->In.Stats.ProfCapture, a);

    int rc = RTCritSectEnter(&pStreamEx->Core.CritSect);
    AssertRCReturn(rc, rc);

    /*
     * First check that we can read from the stream, and if not,
     * whether to just drop the input into the bit bucket.
     */
    if (PDMAudioStrmStatusIsReady(pStreamEx->fStatus))
    {
        RTCritSectRwEnterShared(&pThis->CritSectHotPlug);
        if (   pThis->In.fEnabled /* (see @bugref{9882}) */
            && pThis->pHostDrvAudio != NULL)
        {
            /*
             * Get the backend state and process changes to it since last time we checked.
             */
            PDMHOSTAUDIOSTREAMSTATE const enmBackendState = drvAudioStreamGetBackendStateAndProcessChanges(pThis, pStreamEx);

            /*
             * Do the transfering.
             */
            switch (pStreamEx->In.enmCaptureState)
            {
                case DRVAUDIOCAPTURESTATE_CAPTURING:
                    Assert(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_BACKEND_READY);
                    Assert(enmBackendState == PDMHOSTAUDIOSTREAMSTATE_OKAY);
                    rc = drvAudioStreamCaptureLocked(pThis, pStreamEx, (uint8_t *)pvBuf, cbBuf, pcbRead);
                    break;

                case DRVAUDIOCAPTURESTATE_PREBUF:
                    if (enmBackendState == PDMHOSTAUDIOSTREAMSTATE_OKAY)
                    {
                        uint32_t const cbReadable = pThis->pHostDrvAudio->pfnStreamGetReadable(pThis->pHostDrvAudio,
                                                                                               pStreamEx->pBackend);
                        if (cbReadable >= pStreamEx->cbPreBufThreshold)
                        {
                            Log4Func(("[%s] Pre-buffering completed: cbReadable=%#x vs cbPreBufThreshold=%#x (cbBuf=%#x)\n",
                                      pStreamEx->Core.Cfg.szName, cbReadable, pStreamEx->cbPreBufThreshold, cbBuf));
                            pStreamEx->In.enmCaptureState = DRVAUDIOCAPTURESTATE_CAPTURING;
                            rc = drvAudioStreamCaptureLocked(pThis, pStreamEx, (uint8_t *)pvBuf, cbBuf, pcbRead);
                            break;
                        }
                        pStreamEx->In.Stats.cbBackendReadableBefore = cbReadable;
                        pStreamEx->In.Stats.cbBackendReadableAfter  = cbReadable;
                        Log4Func(("[%s] Pre-buffering: Got %#x out of %#x\n",
                                  pStreamEx->Core.Cfg.szName, cbReadable, pStreamEx->cbPreBufThreshold));
                    }
                    else
                        Log4Func(("[%s] Pre-buffering: Backend status %s\n",
                                  pStreamEx->Core.Cfg.szName, PDMHostAudioStreamStateGetName(enmBackendState) ));
                    drvAudioStreamCaptureSilence(pStreamEx, (uint8_t *)pvBuf, cbBuf, pcbRead);
                    break;

                case DRVAUDIOCAPTURESTATE_NO_CAPTURE:
                    *pcbRead = 0;
                    Log4Func(("[%s] Not capturing - backend state: %s\n",
                              pStreamEx->Core.Cfg.szName, PDMHostAudioStreamStateGetName(enmBackendState) ));
                    break;

                default:
                    *pcbRead = 0;
                    AssertMsgFailedBreak(("%d; cbBuf=%#x\n", pStreamEx->In.enmCaptureState, cbBuf));
            }

            if (!pStreamEx->In.Dbg.pFileCapture || RT_FAILURE(rc))
            { /* likely */ }
            else
                AudioHlpFileWrite(pStreamEx->In.Dbg.pFileCapture, pvBuf, *pcbRead);
        }
        else
        {
            *pcbRead = 0;
            Log4Func(("[%s] Backend stream %s, returning no data\n", pStreamEx->Core.Cfg.szName,
                      !pThis->Out.fEnabled ? "disabled" : !pThis->pHostDrvAudio ? "not attached" : "not ready yet"));
        }
        RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    }
    else
        rc = VERR_AUDIO_STREAM_NOT_READY;

    STAM_PROFILE_STOP(&pStreamEx->In.Stats.ProfCapture, a);
    RTCritSectLeave(&pStreamEx->Core.CritSect);
    return rc;
}


/*********************************************************************************************************************************
*   PDMIHOSTAUDIOPORT interface implementation.                                                                                  *
*********************************************************************************************************************************/

/**
 * Worker for drvAudioHostPort_DoOnWorkerThread with stream argument, called on
 * worker thread.
 */
static DECLCALLBACK(void) drvAudioHostPort_DoOnWorkerThreadStreamWorker(PDRVAUDIO pThis, PDRVAUDIOSTREAM pStreamEx,
                                                                        uintptr_t uUser, void *pvUser)
{
    LogFlowFunc(("pThis=%p uUser=%#zx pvUser=%p\n", pThis, uUser, pvUser));
    AssertPtrReturnVoid(pThis);
    AssertPtrReturnVoid(pStreamEx);
    AssertReturnVoid(pStreamEx->uMagic == DRVAUDIOSTREAM_MAGIC);

    /*
     * The CritSectHotPlug lock should not be needed here as detach will destroy
     * the thread pool.  So, we'll leave taking the stream lock to the worker we're
     * calling as there are no lock order concerns.
     */
    PPDMIHOSTAUDIO const pIHostDrvAudio = pThis->pHostDrvAudio;
    AssertPtrReturnVoid(pIHostDrvAudio);
    AssertPtrReturnVoid(pIHostDrvAudio->pfnDoOnWorkerThread);
    pIHostDrvAudio->pfnDoOnWorkerThread(pIHostDrvAudio, pStreamEx->pBackend, uUser, pvUser);

    uint32_t const cRefs = drvAudioStreamReleaseInternal(pThis, pStreamEx, true /*fMayDestroy*/);
    LogFlowFunc(("returns (cRefs=%u)\n", cRefs)); RT_NOREF(cRefs);
}


/**
 * Worker for drvAudioHostPort_DoOnWorkerThread without stream argument, called
 * on worker thread.
 *
 * This wrapper isn't technically required, but it helps with logging and a few
 * extra sanity checks.
 */
static DECLCALLBACK(void) drvAudioHostPort_DoOnWorkerThreadWorker(PDRVAUDIO pThis, uintptr_t uUser, void *pvUser)
{
    LogFlowFunc(("pThis=%p uUser=%#zx pvUser=%p\n", pThis, uUser, pvUser));
    AssertPtrReturnVoid(pThis);

    /*
     * The CritSectHotPlug lock should not be needed here as detach will destroy
     * the thread pool.
     */
    PPDMIHOSTAUDIO const pIHostDrvAudio = pThis->pHostDrvAudio;
    AssertPtrReturnVoid(pIHostDrvAudio);
    AssertPtrReturnVoid(pIHostDrvAudio->pfnDoOnWorkerThread);

    pIHostDrvAudio->pfnDoOnWorkerThread(pIHostDrvAudio, NULL, uUser, pvUser);

    LogFlowFunc(("returns\n"));
}


/**
 * @interface_method_impl{PDMIHOSTAUDIOPORT,pfnDoOnWorkerThread}
 */
static DECLCALLBACK(int) drvAudioHostPort_DoOnWorkerThread(PPDMIHOSTAUDIOPORT pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                           uintptr_t uUser, void *pvUser)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IHostAudioPort);
    LogFlowFunc(("pStream=%p uUser=%#zx pvUser=%p\n", pStream, uUser, pvUser));

    /*
     * Assert some sanity.
     */
    PDRVAUDIOSTREAM pStreamEx;
    if (!pStream)
        pStreamEx = NULL;
    else
    {
        AssertPtrReturn(pStream, VERR_INVALID_POINTER);
        AssertReturn(pStream->uMagic == PDMAUDIOBACKENDSTREAM_MAGIC, VERR_INVALID_MAGIC);
        pStreamEx = (PDRVAUDIOSTREAM)pStream->pStream;
        AssertPtrReturn(pStreamEx, VERR_INVALID_POINTER);
        AssertReturn(pStreamEx->uMagic == DRVAUDIOSTREAM_MAGIC, VERR_INVALID_MAGIC);
        AssertReturn(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC, VERR_INVALID_MAGIC);
    }

    int rc = RTCritSectRwEnterShared(&pThis->CritSectHotPlug);
    AssertRCReturn(rc, rc);

    Assert(pThis->hReqPool != NIL_RTREQPOOL);
    AssertPtr(pThis->pHostDrvAudio);
    if (   pThis->hReqPool != NIL_RTREQPOOL
        && pThis->pHostDrvAudio != NULL)
    {
        AssertPtr(pThis->pHostDrvAudio->pfnDoOnWorkerThread);
        if (pThis->pHostDrvAudio->pfnDoOnWorkerThread)
        {
            /*
             * Try do the work.
             */
            if (!pStreamEx)
            {
                rc = RTReqPoolCallEx(pThis->hReqPool, 0 /*cMillies*/, NULL /*phReq*/, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                                     (PFNRT)drvAudioHostPort_DoOnWorkerThreadWorker, 3, pThis, uUser, pvUser);
                AssertRC(rc);
            }
            else
            {
                uint32_t cRefs = drvAudioStreamRetainInternal(pStreamEx);
                if (cRefs != UINT32_MAX)
                {
                    rc = RTReqPoolCallEx(pThis->hReqPool, 0 /*cMillies*/, NULL, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                                         (PFNRT)drvAudioHostPort_DoOnWorkerThreadStreamWorker,
                                         4, pThis, pStreamEx, uUser, pvUser);
                    AssertRC(rc);
                    if (RT_FAILURE(rc))
                    {
                        RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
                        drvAudioStreamReleaseInternal(pThis, pStreamEx, true /*fMayDestroy*/);
                        RTCritSectRwEnterShared(&pThis->CritSectHotPlug);
                    }
                }
                else
                    rc = VERR_INVALID_PARAMETER;
            }
        }
        else
            rc = VERR_INVALID_FUNCTION;
    }
    else
        rc = VERR_INVALID_STATE;

    RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * Marks a stream for re-init.
 */
static void drvAudioStreamMarkNeedReInit(PDRVAUDIOSTREAM pStreamEx, const char *pszCaller)
{
    LogFlow((LOG_FN_FMT ": Flagging %s for re-init.\n", pszCaller, pStreamEx->Core.Cfg.szName)); RT_NOREF(pszCaller);
    Assert(RTCritSectIsOwner(&pStreamEx->Core.CritSect));

    pStreamEx->fStatus      |= PDMAUDIOSTREAM_STS_NEED_REINIT;
    PDMAUDIOSTREAM_STS_ASSERT_VALID(pStreamEx->fStatus);
    pStreamEx->cTriesReInit  = 0;
    pStreamEx->nsLastReInit  = 0;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIOPORT,pfnNotifyDeviceChanged}
 */
static DECLCALLBACK(void) drvAudioHostPort_NotifyDeviceChanged(PPDMIHOSTAUDIOPORT pInterface, PDMAUDIODIR enmDir, void *pvUser)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IHostAudioPort);
    AssertReturnVoid(enmDir == PDMAUDIODIR_IN || enmDir == PDMAUDIODIR_OUT);
    LogRel(("Audio: The %s device for %s is changing.\n", PDMAudioDirGetName(enmDir), pThis->BackendCfg.szName));

    /*
     * Grab the list lock in shared mode and do the work.
     */
    int rc = RTCritSectRwEnterShared(&pThis->CritSectGlobals);
    AssertRCReturnVoid(rc);

    PDRVAUDIOSTREAM pStreamEx;
    RTListForEach(&pThis->LstStreams, pStreamEx, DRVAUDIOSTREAM, ListEntry)
    {
        if (pStreamEx->Core.Cfg.enmDir == enmDir)
        {
            RTCritSectEnter(&pStreamEx->Core.CritSect);
            RTCritSectRwEnterShared(&pThis->CritSectHotPlug);

            if (pThis->pHostDrvAudio->pfnStreamNotifyDeviceChanged)
            {
                LogFlowFunc(("Calling pfnStreamNotifyDeviceChanged on %s, old backend state: %s...\n", pStreamEx->Core.Cfg.szName,
                             PDMHostAudioStreamStateGetName(drvAudioStreamGetBackendState(pThis, pStreamEx)) ));
                pThis->pHostDrvAudio->pfnStreamNotifyDeviceChanged(pThis->pHostDrvAudio, pStreamEx->pBackend, pvUser);
                LogFlowFunc(("New stream backend state: %s\n",
                             PDMHostAudioStreamStateGetName(drvAudioStreamGetBackendState(pThis, pStreamEx)) ));
            }
            else
                drvAudioStreamMarkNeedReInit(pStreamEx, __PRETTY_FUNCTION__);

            RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
            RTCritSectLeave(&pStreamEx->Core.CritSect);
        }
    }

    RTCritSectRwLeaveShared(&pThis->CritSectGlobals);
}


/**
 * @interface_method_impl{PDMIHOSTAUDIOPORT,pfnStreamNotifyPreparingDeviceSwitch}
 */
static DECLCALLBACK(void) drvAudioHostPort_StreamNotifyPreparingDeviceSwitch(PPDMIHOSTAUDIOPORT pInterface,
                                                                             PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);

    /*
     * Backend stream to validated DrvAudio stream:
     */
    AssertPtrReturnVoid(pStream);
    AssertReturnVoid(pStream->uMagic == PDMAUDIOBACKENDSTREAM_MAGIC);
    PDRVAUDIOSTREAM pStreamEx = (PDRVAUDIOSTREAM)pStream->pStream;
    AssertPtrReturnVoid(pStreamEx);
    AssertReturnVoid(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC);
    AssertReturnVoid(pStreamEx->uMagic == DRVAUDIOSTREAM_MAGIC);
    LogFlowFunc(("pStreamEx=%p '%s'\n", pStreamEx, pStreamEx->Core.Cfg.szName));

    /*
     * Grab the lock and do switch the state (only needed for output streams for now).
     */
    RTCritSectEnter(&pStreamEx->Core.CritSect);
    AssertReturnVoidStmt(pStreamEx->uMagic == DRVAUDIOSTREAM_MAGIC, RTCritSectLeave(&pStreamEx->Core.CritSect)); /* paranoia */

    if (pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_OUT)
    {
        if (pStreamEx->cbPreBufThreshold > 0)
        {
            DRVAUDIOPLAYSTATE const enmPlayState = pStreamEx->Out.enmPlayState;
            switch (enmPlayState)
            {
                case DRVAUDIOPLAYSTATE_PREBUF:
                case DRVAUDIOPLAYSTATE_PREBUF_OVERDUE:
                case DRVAUDIOPLAYSTATE_NOPLAY:
                case DRVAUDIOPLAYSTATE_PREBUF_COMMITTING: /* simpler */
                    pStreamEx->Out.enmPlayState = DRVAUDIOPLAYSTATE_PREBUF_SWITCHING;
                    break;
                case DRVAUDIOPLAYSTATE_PLAY:
                    pStreamEx->Out.enmPlayState = DRVAUDIOPLAYSTATE_PLAY_PREBUF;
                    break;
                case DRVAUDIOPLAYSTATE_PREBUF_SWITCHING:
                case DRVAUDIOPLAYSTATE_PLAY_PREBUF:
                    break;
                /* no default */
                case DRVAUDIOPLAYSTATE_END:
                case DRVAUDIOPLAYSTATE_INVALID:
                    break;
            }
            LogFunc(("%s -> %s\n", drvAudioPlayStateName(enmPlayState), drvAudioPlayStateName(pStreamEx->Out.enmPlayState) ));
        }
        else
            LogFunc(("No pre-buffering configured.\n"));
    }
    else
        LogFunc(("input stream, nothing to do.\n"));

    RTCritSectLeave(&pStreamEx->Core.CritSect);
}


/**
 * @interface_method_impl{PDMIHOSTAUDIOPORT,pfnStreamNotifyDeviceChanged}
 */
static DECLCALLBACK(void) drvAudioHostPort_StreamNotifyDeviceChanged(PPDMIHOSTAUDIOPORT pInterface,
                                                                     PPDMAUDIOBACKENDSTREAM pStream, bool fReInit)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IHostAudioPort);

    /*
     * Backend stream to validated DrvAudio stream:
     */
    AssertPtrReturnVoid(pStream);
    AssertReturnVoid(pStream->uMagic == PDMAUDIOBACKENDSTREAM_MAGIC);
    PDRVAUDIOSTREAM pStreamEx = (PDRVAUDIOSTREAM)pStream->pStream;
    AssertPtrReturnVoid(pStreamEx);
    AssertReturnVoid(pStreamEx->Core.uMagic == PDMAUDIOSTREAM_MAGIC);
    AssertReturnVoid(pStreamEx->uMagic == DRVAUDIOSTREAM_MAGIC);

    /*
     * Grab the lock and do the requested work.
     */
    RTCritSectEnter(&pStreamEx->Core.CritSect);
    AssertReturnVoidStmt(pStreamEx->uMagic == DRVAUDIOSTREAM_MAGIC, RTCritSectLeave(&pStreamEx->Core.CritSect)); /* paranoia */

    if (fReInit)
        drvAudioStreamMarkNeedReInit(pStreamEx, __PRETTY_FUNCTION__);
    else
    {
        /*
         * Adjust the stream state now that the device has (perhaps finally) been switched.
         *
         * For enabled output streams, we must update the play state.  We could try commit
         * pre-buffered data here, but it's really not worth the hazzle and risk (don't
         * know which thread we're on, do we now).
         */
        AssertStmt(!(pStreamEx->fStatus & PDMAUDIOSTREAM_STS_NEED_REINIT),
                   pStreamEx->fStatus &= ~PDMAUDIOSTREAM_STS_NEED_REINIT);


        if (pStreamEx->Core.Cfg.enmDir == PDMAUDIODIR_OUT)
        {
            DRVAUDIOPLAYSTATE const enmPlayState = pStreamEx->Out.enmPlayState;
            pStreamEx->Out.enmPlayState = DRVAUDIOPLAYSTATE_PREBUF;
            LogFunc(("%s: %s -> %s\n", pStreamEx->Core.Cfg.szName, drvAudioPlayStateName(enmPlayState),
                     drvAudioPlayStateName(pStreamEx->Out.enmPlayState) ));
            RT_NOREF(enmPlayState);
        }

        /* Disable and then fully resync. */
        /** @todo This doesn't work quite reliably if we're in draining mode
         * (PENDING_DISABLE, so the backend needs to take care of that prior to calling
         * us.  Sigh.  The idea was to avoid extra state mess in the backend... */
        drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_DISABLE);
        drvAudioStreamUpdateBackendOnStatus(pThis, pStreamEx, "device changed");
    }

    RTCritSectLeave(&pStreamEx->Core.CritSect);
}


#ifdef VBOX_WITH_AUDIO_ENUM
/**
 * @callback_method_impl{FNTMTIMERDRV, Re-enumerate backend devices.}
 *
 * Used to do/trigger re-enumeration of backend devices with a delay after we
 * got notification as there can be further notifications following shortly
 * after the first one.  Also good to get it of random COM/whatever threads.
 */
static DECLCALLBACK(void) drvAudioEnumerateTimer(PPDMDRVINS pDrvIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PDRVAUDIO pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIO);
    RT_NOREF(hTimer, pvUser);

    /* Try push the work over to the thread-pool if we've got one. */
    RTCritSectRwEnterShared(&pThis->CritSectHotPlug);
    if (pThis->hReqPool != NIL_RTREQPOOL)
    {
        int rc = RTReqPoolCallEx(pThis->hReqPool, 0 /*cMillies*/, NULL, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                                 (PFNRT)drvAudioDevicesEnumerateInternal,
                                 3, pThis, true /*fLog*/, (PPDMAUDIOHOSTENUM)NULL /*pDevEnum*/);
        LogFunc(("RTReqPoolCallEx: %Rrc\n", rc));
        RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);
        if (RT_SUCCESS(rc))
            return;
    }
    else
        RTCritSectRwLeaveShared(&pThis->CritSectHotPlug);

    LogFunc(("Calling drvAudioDevicesEnumerateInternal...\n"));
    drvAudioDevicesEnumerateInternal(pThis, true /* fLog */, NULL /* pDevEnum */);
}
#endif /* VBOX_WITH_AUDIO_ENUM */


/**
 * @interface_method_impl{PDMIHOSTAUDIOPORT,pfnNotifyDevicesChanged}
 */
static DECLCALLBACK(void) drvAudioHostPort_NotifyDevicesChanged(PPDMIHOSTAUDIOPORT pInterface)
{
    PDRVAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVAUDIO, IHostAudioPort);
    LogRel(("Audio: Device configuration of driver '%s' has changed\n", pThis->BackendCfg.szName));

#ifdef RT_OS_DARWIN /** @todo Remove legacy behaviour: */
    /* Mark all host streams to re-initialize. */
    int rc2 = RTCritSectRwEnterShared(&pThis->CritSectGlobals);
    AssertRCReturnVoid(rc2);
    PDRVAUDIOSTREAM pStreamEx;
    RTListForEach(&pThis->LstStreams, pStreamEx, DRVAUDIOSTREAM, ListEntry)
    {
        RTCritSectEnter(&pStreamEx->Core.CritSect);
        drvAudioStreamMarkNeedReInit(pStreamEx, __PRETTY_FUNCTION__);
        RTCritSectLeave(&pStreamEx->Core.CritSect);
    }
    RTCritSectRwLeaveShared(&pThis->CritSectGlobals);
#endif

#ifdef VBOX_WITH_AUDIO_ENUM
    /*
     * Re-enumerate all host devices with a tiny delay to avoid re-doing this
     * when a bunch of changes happens at once (they typically do on windows).
     * We'll keep postponing it till it quiesces for a fraction of a second.
     */
    int rc = PDMDrvHlpTimerSetMillies(pThis->pDrvIns, pThis->hEnumTimer, RT_MS_1SEC / 3);
    AssertRC(rc);
#endif
}


/*********************************************************************************************************************************
*   PDMIBASE interface implementation.                                                                                           *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvAudioQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    LogFlowFunc(("pInterface=%p, pszIID=%s\n", pInterface, pszIID));

    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVAUDIO  pThis   = PDMINS_2_DATA(pDrvIns, PDRVAUDIO);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIAUDIOCONNECTOR, &pThis->IAudioConnector);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHOSTAUDIOPORT, &pThis->IHostAudioPort);

    return NULL;
}


/*********************************************************************************************************************************
*   PDMDRVREG interface implementation.                                                                                          *
*********************************************************************************************************************************/

/**
 * Power Off notification.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvAudioPowerOff(PPDMDRVINS pDrvIns)
{
    PDRVAUDIO pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIO);

    LogFlowFuncEnter();

    /** @todo locking?   */
    if (pThis->pHostDrvAudio) /* If not lower driver is configured, bail out. */
    {
        /*
         * Just destroy the host stream on the backend side.
         * The rest will either be destructed by the device emulation or
         * in drvAudioDestruct().
         */
        int rc = RTCritSectRwEnterShared(&pThis->CritSectGlobals);
        AssertRCReturnVoid(rc);

        PDRVAUDIOSTREAM pStreamEx;
        RTListForEach(&pThis->LstStreams, pStreamEx, DRVAUDIOSTREAM, ListEntry)
        {
            RTCritSectEnter(&pStreamEx->Core.CritSect);
            drvAudioStreamControlInternalBackend(pThis, pStreamEx, PDMAUDIOSTREAMCMD_DISABLE);
            RTCritSectLeave(&pStreamEx->Core.CritSect);
        }

        RTCritSectRwLeaveShared(&pThis->CritSectGlobals);
    }

    LogFlowFuncLeave();
}


/**
 * Detach notification.
 *
 * @param   pDrvIns     The driver instance data.
 * @param   fFlags      Detach flags.
 */
static DECLCALLBACK(void) drvAudioDetach(PPDMDRVINS pDrvIns, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVAUDIO pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIO);
    RT_NOREF(fFlags);

    int rc = RTCritSectRwEnterExcl(&pThis->CritSectHotPlug);
    AssertLogRelRCReturnVoid(rc);

    LogFunc(("%s (detached %p, hReqPool=%p)\n", pThis->BackendCfg.szName, pThis->pHostDrvAudio, pThis->hReqPool));

    /*
     * Must first destroy the thread pool first so we are certain no threads
     * are still using the instance being detached.  Release lock while doing
     * this as the thread functions may need to take it to complete.
     */
    if (pThis->pHostDrvAudio && pThis->hReqPool != NIL_RTREQPOOL)
    {
        RTREQPOOL hReqPool = pThis->hReqPool;
        pThis->hReqPool = NIL_RTREQPOOL;

        RTCritSectRwLeaveExcl(&pThis->CritSectHotPlug);

        RTReqPoolRelease(hReqPool);

        RTCritSectRwEnterExcl(&pThis->CritSectHotPlug);
    }

    /*
     * Now we can safely set pHostDrvAudio to NULL.
     */
    pThis->pHostDrvAudio = NULL;

    RTCritSectRwLeaveExcl(&pThis->CritSectHotPlug);
}


/**
 * Initializes the host backend and queries its initial configuration.
 *
 * @returns VBox status code.
 * @param   pThis               Driver instance to be called.
 */
static int drvAudioHostInit(PDRVAUDIO pThis)
{
    LogFlowFuncEnter();

    /*
     * Check the function pointers, make sure the ones we define as
     * mandatory are present.
     */
    PPDMIHOSTAUDIO pIHostDrvAudio = pThis->pHostDrvAudio;
    AssertPtrReturn(pIHostDrvAudio, VERR_INVALID_POINTER);
    AssertPtrReturn(pIHostDrvAudio->pfnGetConfig, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pIHostDrvAudio->pfnGetDevices, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pIHostDrvAudio->pfnSetDevice, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pIHostDrvAudio->pfnGetStatus, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pIHostDrvAudio->pfnDoOnWorkerThread, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pIHostDrvAudio->pfnStreamConfigHint, VERR_INVALID_POINTER);
    AssertPtrReturn(pIHostDrvAudio->pfnStreamCreate, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pIHostDrvAudio->pfnStreamInitAsync, VERR_INVALID_POINTER);
    AssertPtrReturn(pIHostDrvAudio->pfnStreamDestroy, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pIHostDrvAudio->pfnStreamNotifyDeviceChanged, VERR_INVALID_POINTER);
    AssertPtrReturn(pIHostDrvAudio->pfnStreamEnable, VERR_INVALID_POINTER);
    AssertPtrReturn(pIHostDrvAudio->pfnStreamDisable, VERR_INVALID_POINTER);
    AssertPtrReturn(pIHostDrvAudio->pfnStreamPause, VERR_INVALID_POINTER);
    AssertPtrReturn(pIHostDrvAudio->pfnStreamResume, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pIHostDrvAudio->pfnStreamDrain, VERR_INVALID_POINTER);
    AssertPtrReturn(pIHostDrvAudio->pfnStreamGetReadable, VERR_INVALID_POINTER);
    AssertPtrReturn(pIHostDrvAudio->pfnStreamGetWritable, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pIHostDrvAudio->pfnStreamGetPending, VERR_INVALID_POINTER);
    AssertPtrReturn(pIHostDrvAudio->pfnStreamGetState, VERR_INVALID_POINTER);
    AssertPtrReturn(pIHostDrvAudio->pfnStreamPlay, VERR_INVALID_POINTER);
    AssertPtrReturn(pIHostDrvAudio->pfnStreamCapture, VERR_INVALID_POINTER);

    /*
     * Get the backend configuration.
     *
     * Note! Limit the number of streams to max 128 in each direction to
     *       prevent wasting resources.
     * Note! Take care not to wipe the DriverName config value on failure.
     */
    PDMAUDIOBACKENDCFG BackendCfg;
    RT_ZERO(BackendCfg);
    int rc = pIHostDrvAudio->pfnGetConfig(pIHostDrvAudio, &BackendCfg);
    if (RT_SUCCESS(rc))
    {
        if (LogIsEnabled() && strcmp(BackendCfg.szName, pThis->BackendCfg.szName) != 0)
            LogFunc(("BackendCfg.szName: '%s' -> '%s'\n", pThis->BackendCfg.szName, BackendCfg.szName));
        pThis->BackendCfg       = BackendCfg;
        pThis->In.cStreamsFree  = RT_MIN(BackendCfg.cMaxStreamsIn,  128);
        pThis->Out.cStreamsFree = RT_MIN(BackendCfg.cMaxStreamsOut, 128);

        LogFlowFunc(("cStreamsFreeIn=%RU8, cStreamsFreeOut=%RU8\n", pThis->In.cStreamsFree, pThis->Out.cStreamsFree));
    }
    else
    {
        LogRel(("Audio: Getting configuration for driver '%s' failed with %Rrc\n", pThis->BackendCfg.szName, rc));
        return VERR_AUDIO_BACKEND_INIT_FAILED;
    }

    LogRel2(("Audio: Host driver '%s' supports %RU32 input streams and %RU32 output streams at once.\n",
             pThis->BackendCfg.szName, pThis->In.cStreamsFree, pThis->Out.cStreamsFree));

#ifdef VBOX_WITH_AUDIO_ENUM
    int rc2 = drvAudioDevicesEnumerateInternal(pThis, true /* fLog */, NULL /* pDevEnum */);
    if (rc2 != VERR_NOT_SUPPORTED) /* Some backends don't implement device enumeration. */
        AssertRC(rc2);
    /* Ignore rc2. */
#endif

    /*
     * Create a thread pool if stream creation can be asynchronous.
     *
     * The pool employs no pushback as the caller is typically EMT and
     * shouldn't be delayed.
     *
     * The number of threads limits and the device implementations use
     * of pfnStreamDestroy limits the number of streams pending async
     * init.  We use RTReqCancel in drvAudioStreamDestroy to allow us
     * to release extra reference held by the pfnStreamInitAsync call
     * if successful.  Cancellation will only be possible if the call
     * hasn't been picked up by a worker thread yet, so the max number
     * of threads in the pool defines how many destroyed streams that
     * can be lingering.  (We must keep this under control, otherwise
     * an evil guest could just rapidly trigger stream creation and
     * destruction to consume host heap and hog CPU resources for
     * configuring audio backends.)
     */
    if (   pThis->hReqPool == NIL_RTREQPOOL
        && (   pIHostDrvAudio->pfnStreamInitAsync
            || pIHostDrvAudio->pfnDoOnWorkerThread
            || (pThis->BackendCfg.fFlags & (PDMAUDIOBACKEND_F_ASYNC_HINT | PDMAUDIOBACKEND_F_ASYNC_STREAM_DESTROY)) ))
    {
        char szName[16];
        RTStrPrintf(szName, sizeof(szName), "Aud%uWr", pThis->pDrvIns->iInstance);
        RTREQPOOL hReqPool = NIL_RTREQPOOL;
        rc = RTReqPoolCreate(3 /*cMaxThreads*/, RT_MS_30SEC /*cMsMinIdle*/, UINT32_MAX /*cThreadsPushBackThreshold*/,
                             1 /*cMsMaxPushBack*/, szName, &hReqPool);
        LogFlowFunc(("Creating thread pool '%s': %Rrc, hReqPool=%p\n", szName, rc, hReqPool));
        AssertRCReturn(rc, rc);

        rc = RTReqPoolSetCfgVar(hReqPool, RTREQPOOLCFGVAR_THREAD_FLAGS, RTTHREADFLAGS_COM_MTA);
        AssertRCReturnStmt(rc, RTReqPoolRelease(hReqPool), rc);

        rc = RTReqPoolSetCfgVar(hReqPool, RTREQPOOLCFGVAR_MIN_THREADS, 1);
        AssertRC(rc); /* harmless */

        pThis->hReqPool = hReqPool;
    }
    else
        LogFlowFunc(("No thread pool.\n"));

    LogFlowFuncLeave();
    return VINF_SUCCESS;
}


/**
 * Does the actual backend driver attaching and queries the backend's interface.
 *
 * This is a worker for both drvAudioAttach and drvAudioConstruct.
 *
 * @returns VBox status code.
 * @param   pDrvIns     The driver instance.
 * @param   pThis       Pointer to driver instance.
 * @param   fFlags      Attach flags; see PDMDrvHlpAttach().
 */
static int drvAudioDoAttachInternal(PPDMDRVINS pDrvIns, PDRVAUDIO pThis, uint32_t fFlags)
{
    Assert(pThis->pHostDrvAudio == NULL); /* No nested attaching. */

    /*
     * Attach driver below and query its connector interface.
     */
    PPDMIBASE pDownBase;
    int rc = PDMDrvHlpAttach(pDrvIns, fFlags, &pDownBase);
    if (RT_SUCCESS(rc))
    {
        pThis->pHostDrvAudio = PDMIBASE_QUERY_INTERFACE(pDownBase, PDMIHOSTAUDIO);
        if (pThis->pHostDrvAudio)
        {
            /*
             * If everything went well, initialize the lower driver.
             */
            rc = drvAudioHostInit(pThis);
            if (RT_FAILURE(rc))
                pThis->pHostDrvAudio = NULL;
        }
        else
        {
            LogRel(("Audio: Failed to query interface for underlying host driver '%s'\n", pThis->BackendCfg.szName));
            rc = PDMDRV_SET_ERROR(pThis->pDrvIns, VERR_PDM_MISSING_INTERFACE_BELOW,
                                  N_("The host audio driver does not implement PDMIHOSTAUDIO!"));
        }
    }
    /*
     * If the host driver below us failed to construct for some beningn reason,
     * we'll report it as a runtime error and replace it with the Null driver.
     *
     * Note! We do NOT change anything in PDM (or CFGM), so pDrvIns->pDownBase
     *       will remain NULL in this case.
     */
    else if (   rc == VERR_AUDIO_BACKEND_INIT_FAILED
             || rc == VERR_MODULE_NOT_FOUND
             || rc == VERR_SYMBOL_NOT_FOUND
             || rc == VERR_FILE_NOT_FOUND
             || rc == VERR_PATH_NOT_FOUND)
    {
        /* Complain: */
        LogRel(("DrvAudio: Host audio driver '%s' init failed with %Rrc. Switching to the NULL driver for now.\n",
                pThis->BackendCfg.szName, rc));
        PDMDrvHlpVMSetRuntimeError(pDrvIns, 0 /*fFlags*/, "HostAudioNotResponding",
                                   N_("Host audio backend (%s) initialization has failed. Selecting the NULL audio backend with the consequence that no sound is audible"),
                                   pThis->BackendCfg.szName);

        /* Replace with null audio: */
        pThis->pHostDrvAudio = (PPDMIHOSTAUDIO)&g_DrvHostAudioNull;
        RTStrCopy(pThis->BackendCfg.szName, sizeof(pThis->BackendCfg.szName), "NULL");
        rc = drvAudioHostInit(pThis);
        AssertRC(rc);
    }

    LogFunc(("[%s] rc=%Rrc\n", pThis->BackendCfg.szName, rc));
    return rc;
}


/**
 * Attach notification.
 *
 * @param   pDrvIns     The driver instance data.
 * @param   fFlags      Attach flags.
 */
static DECLCALLBACK(int) drvAudioAttach(PPDMDRVINS pDrvIns, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVAUDIO pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIO);
    LogFunc(("%s\n", pThis->BackendCfg.szName));

    int rc = RTCritSectRwEnterExcl(&pThis->CritSectHotPlug);
    AssertRCReturn(rc, rc);

    rc = drvAudioDoAttachInternal(pDrvIns, pThis, fFlags);

    RTCritSectRwLeaveExcl(&pThis->CritSectHotPlug);
    return rc;
}


/**
 * Handles state changes for all audio streams.
 *
 * @param   pDrvIns             Pointer to driver instance.
 * @param   enmCmd              Stream command to set for all streams.
 */
static void drvAudioStateHandler(PPDMDRVINS pDrvIns, PDMAUDIOSTREAMCMD enmCmd)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVAUDIO pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIO);
    LogFlowFunc(("enmCmd=%s\n", PDMAudioStrmCmdGetName(enmCmd)));

    int rc2 = RTCritSectRwEnterShared(&pThis->CritSectGlobals);
    AssertRCReturnVoid(rc2);

    PDRVAUDIOSTREAM pStreamEx;
    RTListForEach(&pThis->LstStreams, pStreamEx, DRVAUDIOSTREAM, ListEntry)
    {
        RTCritSectEnter(&pStreamEx->Core.CritSect);
        drvAudioStreamControlInternal(pThis, pStreamEx, enmCmd);
        RTCritSectLeave(&pStreamEx->Core.CritSect);
    }

    RTCritSectRwLeaveShared(&pThis->CritSectGlobals);
}


/**
 * Resume notification.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvAudioResume(PPDMDRVINS pDrvIns)
{
    drvAudioStateHandler(pDrvIns, PDMAUDIOSTREAMCMD_RESUME);
}


/**
 * Suspend notification.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvAudioSuspend(PPDMDRVINS pDrvIns)
{
    drvAudioStateHandler(pDrvIns, PDMAUDIOSTREAMCMD_PAUSE);
}


/**
 * Destructs an audio driver instance.
 *
 * @copydoc FNPDMDRVDESTRUCT
 */
static DECLCALLBACK(void) drvAudioDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVAUDIO pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIO);

    LogFlowFuncEnter();

    /*
     * We must start by setting pHostDrvAudio to NULL here as the anything below
     * us has already been destroyed at this point.
     */
    if (RTCritSectRwIsInitialized(&pThis->CritSectHotPlug))
    {
        RTCritSectRwEnterExcl(&pThis->CritSectHotPlug);
        pThis->pHostDrvAudio = NULL;
        RTCritSectRwLeaveExcl(&pThis->CritSectHotPlug);
    }
    else
    {
        Assert(pThis->pHostDrvAudio == NULL);
        pThis->pHostDrvAudio = NULL;
    }

    /*
     * Make sure the thread pool is out of the picture before we terminate all the streams.
     */
    if (pThis->hReqPool != NIL_RTREQPOOL)
    {
        uint32_t cRefs = RTReqPoolRelease(pThis->hReqPool);
        Assert(cRefs == 0); RT_NOREF(cRefs);
        pThis->hReqPool = NIL_RTREQPOOL;
    }

    /*
     * Destroy all streams.
     */
    if (RTCritSectRwIsInitialized(&pThis->CritSectGlobals))
    {
        RTCritSectRwEnterExcl(&pThis->CritSectGlobals);

        PDRVAUDIOSTREAM pStreamEx, pStreamExNext;
        RTListForEachSafe(&pThis->LstStreams, pStreamEx, pStreamExNext, DRVAUDIOSTREAM, ListEntry)
        {
            int rc = drvAudioStreamUninitInternal(pThis, pStreamEx);
            if (RT_SUCCESS(rc))
            {
                RTListNodeRemove(&pStreamEx->ListEntry);
                drvAudioStreamFree(pStreamEx);
            }
        }

        RTCritSectRwLeaveExcl(&pThis->CritSectGlobals);
        RTCritSectRwDelete(&pThis->CritSectGlobals);
    }


    /* Sanity. */
    Assert(RTListIsEmpty(&pThis->LstStreams));

    if (RTCritSectRwIsInitialized(&pThis->CritSectHotPlug))
        RTCritSectRwDelete(&pThis->CritSectHotPlug);

    PDMDrvHlpSTAMDeregisterByPrefix(pDrvIns, "");

    LogFlowFuncLeave();
}


/**
 * Constructs an audio driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvAudioConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVAUDIO       pThis = PDMINS_2_DATA(pDrvIns, PDRVAUDIO);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;
    LogFlowFunc(("pDrvIns=%#p, pCfgHandle=%#p, fFlags=%x\n", pDrvIns, pCfg, fFlags));

    /*
     * Basic instance init.
     */
    RTListInit(&pThis->LstStreams);
    pThis->hReqPool = NIL_RTREQPOOL;

    /*
     * Read configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns,
                                  "DriverName|"
                                  "InputEnabled|"
                                  "OutputEnabled|"
                                  "CacheEnabled|"
                                  "DebugEnabled|"
                                  "DebugPathOut|"
                                  /* Deprecated: */
                                  "PCMSampleBitIn|"
                                  "PCMSampleBitOut|"
                                  "PCMSampleHzIn|"
                                  "PCMSampleHzOut|"
                                  "PCMSampleSignedIn|"
                                  "PCMSampleSignedOut|"
                                  "PCMSampleSwapEndianIn|"
                                  "PCMSampleSwapEndianOut|"
                                  "PCMSampleChannelsIn|"
                                  "PCMSampleChannelsOut|"
                                  "PeriodSizeMsIn|"
                                  "PeriodSizeMsOut|"
                                  "BufferSizeMsIn|"
                                  "BufferSizeMsOut|"
                                  "PreBufferSizeMsIn|"
                                  "PreBufferSizeMsOut",
                                  "In|Out");

    int rc = pHlp->pfnCFGMQueryStringDef(pCfg, "DriverName", pThis->BackendCfg.szName, sizeof(pThis->BackendCfg.szName), "Untitled");
    AssertLogRelRCReturn(rc, rc);

    /* Neither input nor output by default for security reasons. */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "InputEnabled",  &pThis->In.fEnabled, false);
    AssertLogRelRCReturn(rc, rc);

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "OutputEnabled", &pThis->Out.fEnabled, false);
    AssertLogRelRCReturn(rc, rc);

    /* Debug stuff (same for both directions). */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "DebugEnabled", &pThis->CfgIn.Dbg.fEnabled, false);
    AssertLogRelRCReturn(rc, rc);

    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "DebugPathOut", pThis->CfgIn.Dbg.szPathOut, sizeof(pThis->CfgIn.Dbg.szPathOut), "");
    AssertLogRelRCReturn(rc, rc);
    if (pThis->CfgIn.Dbg.szPathOut[0] == '\0')
    {
        rc = RTPathTemp(pThis->CfgIn.Dbg.szPathOut, sizeof(pThis->CfgIn.Dbg.szPathOut));
        if (RT_FAILURE(rc))
        {
            LogRel(("Audio: Warning! Failed to retrieve temporary directory: %Rrc - disabling debugging.\n", rc));
            pThis->CfgIn.Dbg.szPathOut[0] = '\0';
            pThis->CfgIn.Dbg.fEnabled = false;
        }
    }
    if (pThis->CfgIn.Dbg.fEnabled)
        LogRel(("Audio: Debugging for driver '%s' enabled (audio data written to '%s')\n",
                pThis->BackendCfg.szName, pThis->CfgIn.Dbg.szPathOut));

    /* Copy debug setup to the output direction. */
    pThis->CfgOut.Dbg = pThis->CfgIn.Dbg;

    LogRel2(("Audio: Verbose logging for driver '%s' is probably enabled too.\n", pThis->BackendCfg.szName));
    /* This ^^^^^^^ is the *WRONG* place for that kind of statement. Verbose logging might only be enabled for DrvAudio. */
    LogRel2(("Audio: Initial status for driver '%s' is: input is %s, output is %s\n",
             pThis->BackendCfg.szName, pThis->In.fEnabled ? "enabled" : "disabled", pThis->Out.fEnabled ? "enabled" : "disabled"));

    /*
     * Per direction configuration.  A bit complicated as
     * these wasn't originally in sub-nodes.
     */
    for (unsigned iDir = 0; iDir < 2; iDir++)
    {
        char         szNm[48];
        PDRVAUDIOCFG pAudioCfg = iDir == 0 ? &pThis->CfgIn : &pThis->CfgOut;
        const char  *pszDir    = iDir == 0 ? "In"           : "Out";
        size_t const cbDir     = iDir == 0 ? sizeof("In")   : sizeof("Out");

#define QUERY_VAL_RET(a_Width, a_szName, a_pValue, a_uDefault, a_ExprValid, a_szValidRange) \
            do { \
                AssertCompile(sizeof(szNm) >= sizeof(a_szName "Out")); \
                memcpy(szNm, a_szName, sizeof(a_szName)); \
                rc = RT_CONCAT(pHlp->pfnCFGMQueryU,a_Width)(pDirNode, szNm, a_pValue); \
                if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT) \
                { \
                    memcpy(&szNm[sizeof(a_szName) - 1], pszDir, cbDir); \
                    rc = RT_CONCAT(pHlp->pfnCFGMQueryU,a_Width)(pCfg, szNm, a_pValue); \
                    if (rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT) \
                    { \
                        *(a_pValue) = a_uDefault; \
                        rc = VINF_SUCCESS; \
                    } \
                    else \
                        LogRel(("DrvAudio: Warning! Please use '%s/" a_szName "' instead of '%s' for your VBoxInternal hacks\n", pszDir, szNm)); \
                } \
                AssertRCReturn(rc, PDMDrvHlpVMSetError(pDrvIns, rc, RT_SRC_POS, \
                                                       N_("Configuration error: Failed to read %s config value '%s'"), pszDir, szNm)); \
                if (!(a_ExprValid)) \
                    return PDMDrvHlpVMSetError(pDrvIns, VERR_OUT_OF_RANGE, RT_SRC_POS, \
                                               N_("Configuration error: Unsupported %s value %u. " a_szValidRange), szNm, *(a_pValue)); \
            } while (0)

        PCFGMNODE const pDirNode = pHlp->pfnCFGMGetChild(pCfg, pszDir);
        rc = pHlp->pfnCFGMValidateConfig(pDirNode, iDir == 0 ? "In/" : "Out/",
                                         "PCMSampleBit|"
                                         "PCMSampleHz|"
                                         "PCMSampleSigned|"
                                         "PCMSampleSwapEndian|"
                                         "PCMSampleChannels|"
                                         "PeriodSizeMs|"
                                         "BufferSizeMs|"
                                         "PreBufferSizeMs",
                                         "", pDrvIns->pReg->szName, pDrvIns->iInstance);
        AssertRCReturn(rc, rc);

        uint8_t cSampleBits = 0;
        QUERY_VAL_RET(8,  "PCMSampleBit",        &cSampleBits,                  0,
                         cSampleBits == 0
                      || cSampleBits == 8
                      || cSampleBits == 16
                      || cSampleBits == 32
                      || cSampleBits == 64,
                      "Must be either 0, 8, 16, 32 or 64");
        if (cSampleBits)
            PDMAudioPropsSetSampleSize(&pAudioCfg->Props, cSampleBits / 8);

        uint8_t cChannels;
        QUERY_VAL_RET(8,  "PCMSampleChannels",   &cChannels,                    0, cChannels <= 16, "Max 16");
        if (cChannels)
            PDMAudioPropsSetChannels(&pAudioCfg->Props, cChannels);

        QUERY_VAL_RET(32, "PCMSampleHz",         &pAudioCfg->Props.uHz,         0,
                      pAudioCfg->Props.uHz == 0 || (pAudioCfg->Props.uHz >= 6000 && pAudioCfg->Props.uHz <= 768000),
                      "In the range 6000 thru 768000, or 0");

        QUERY_VAL_RET(8,  "PCMSampleSigned",     &pAudioCfg->uSigned,           UINT8_MAX,
                      pAudioCfg->uSigned == 0 || pAudioCfg->uSigned == 1 || pAudioCfg->uSigned == UINT8_MAX,
                      "Must be either 0, 1, or 255");

        QUERY_VAL_RET(8,  "PCMSampleSwapEndian", &pAudioCfg->uSwapEndian,       UINT8_MAX,
                      pAudioCfg->uSwapEndian == 0 || pAudioCfg->uSwapEndian == 1 || pAudioCfg->uSwapEndian == UINT8_MAX,
                      "Must be either 0, 1, or 255");

        QUERY_VAL_RET(32, "PeriodSizeMs",        &pAudioCfg->uPeriodSizeMs,     0,
                      pAudioCfg->uPeriodSizeMs <= RT_MS_1SEC, "Max 1000");

        QUERY_VAL_RET(32, "BufferSizeMs",        &pAudioCfg->uBufferSizeMs,     0,
                      pAudioCfg->uBufferSizeMs <= RT_MS_5SEC, "Max 5000");

        QUERY_VAL_RET(32, "PreBufferSizeMs",     &pAudioCfg->uPreBufSizeMs,     UINT32_MAX,
                      pAudioCfg->uPreBufSizeMs <= RT_MS_1SEC || pAudioCfg->uPreBufSizeMs == UINT32_MAX,
                      "Max 1000, or 0xffffffff");
#undef QUERY_VAL_RET
    }

    /*
     * Init the rest of the driver instance data.
     */
    rc = RTCritSectRwInit(&pThis->CritSectHotPlug);
    AssertRCReturn(rc, rc);
    rc = RTCritSectRwInit(&pThis->CritSectGlobals);
    AssertRCReturn(rc, rc);
#ifdef VBOX_STRICT
    /* Define locking order: */
    RTCritSectRwEnterExcl(&pThis->CritSectGlobals);
    RTCritSectRwEnterExcl(&pThis->CritSectHotPlug);
    RTCritSectRwLeaveExcl(&pThis->CritSectHotPlug);
    RTCritSectRwLeaveExcl(&pThis->CritSectGlobals);
#endif

    pThis->pDrvIns                              = pDrvIns;
    /* IBase. */
    pDrvIns->IBase.pfnQueryInterface            = drvAudioQueryInterface;
    /* IAudioConnector. */
    pThis->IAudioConnector.pfnEnable            = drvAudioEnable;
    pThis->IAudioConnector.pfnIsEnabled         = drvAudioIsEnabled;
    pThis->IAudioConnector.pfnGetConfig         = drvAudioGetConfig;
    pThis->IAudioConnector.pfnGetStatus         = drvAudioGetStatus;
    pThis->IAudioConnector.pfnStreamConfigHint  = drvAudioStreamConfigHint;
    pThis->IAudioConnector.pfnStreamCreate      = drvAudioStreamCreate;
    pThis->IAudioConnector.pfnStreamDestroy     = drvAudioStreamDestroy;
    pThis->IAudioConnector.pfnStreamReInit      = drvAudioStreamReInit;
    pThis->IAudioConnector.pfnStreamRetain      = drvAudioStreamRetain;
    pThis->IAudioConnector.pfnStreamRelease     = drvAudioStreamRelease;
    pThis->IAudioConnector.pfnStreamControl     = drvAudioStreamControl;
    pThis->IAudioConnector.pfnStreamIterate     = drvAudioStreamIterate;
    pThis->IAudioConnector.pfnStreamGetState    = drvAudioStreamGetState;
    pThis->IAudioConnector.pfnStreamGetWritable = drvAudioStreamGetWritable;
    pThis->IAudioConnector.pfnStreamPlay        = drvAudioStreamPlay;
    pThis->IAudioConnector.pfnStreamGetReadable = drvAudioStreamGetReadable;
    pThis->IAudioConnector.pfnStreamCapture     = drvAudioStreamCapture;
    /* IHostAudioPort */
    pThis->IHostAudioPort.pfnDoOnWorkerThread                   = drvAudioHostPort_DoOnWorkerThread;
    pThis->IHostAudioPort.pfnNotifyDeviceChanged                = drvAudioHostPort_NotifyDeviceChanged;
    pThis->IHostAudioPort.pfnStreamNotifyPreparingDeviceSwitch  = drvAudioHostPort_StreamNotifyPreparingDeviceSwitch;
    pThis->IHostAudioPort.pfnStreamNotifyDeviceChanged          = drvAudioHostPort_StreamNotifyDeviceChanged;
    pThis->IHostAudioPort.pfnNotifyDevicesChanged               = drvAudioHostPort_NotifyDevicesChanged;

#ifdef VBOX_WITH_AUDIO_ENUM
    /*
     * Create a timer to trigger delayed device enumeration on device changes.
     */
    RTStrPrintf(pThis->szEnumTimerName, sizeof(pThis->szEnumTimerName), "AudioEnum-%u", pDrvIns->iInstance);
    rc = PDMDrvHlpTMTimerCreate(pDrvIns, TMCLOCK_REAL, drvAudioEnumerateTimer, NULL /*pvUser*/,
                                0 /*fFlags*/, pThis->szEnumTimerName, &pThis->hEnumTimer);
    AssertRCReturn(rc, rc);
#endif

    /*
     * Attach the host driver, if present.
     */
    rc = drvAudioDoAttachInternal(pDrvIns, pThis, fFlags);
    if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        rc = VINF_SUCCESS;

    /*
     * Statistics (afte driver attach for name).
     */
    PDMDrvHlpSTAMRegister(pDrvIns, &pThis->BackendCfg.fFlags,   STAMTYPE_U32,  "BackendFlags",     STAMUNIT_COUNT, pThis->BackendCfg.szName); /* Mainly for the name. */
    PDMDrvHlpSTAMRegister(pDrvIns, &pThis->cStreams,            STAMTYPE_U32,  "Streams",          STAMUNIT_COUNT, "Current streams count.");
    PDMDrvHlpSTAMRegCounter(pDrvIns, &pThis->StatTotalStreamsCreated,          "TotalStreamsCreated", "Number of stream ever created.");
    PDMDrvHlpSTAMRegister(pDrvIns, &pThis->In.fEnabled,         STAMTYPE_BOOL, "InputEnabled",     STAMUNIT_NONE, "Whether input is enabled or not.");
    PDMDrvHlpSTAMRegister(pDrvIns, &pThis->In.cStreamsFree,     STAMTYPE_U32,  "InputStreamFree",  STAMUNIT_COUNT, "Number of free input stream slots");
    PDMDrvHlpSTAMRegister(pDrvIns, &pThis->Out.fEnabled,        STAMTYPE_BOOL, "OutputEnabled",    STAMUNIT_NONE, "Whether output is enabled or not.");
    PDMDrvHlpSTAMRegister(pDrvIns, &pThis->Out.cStreamsFree,    STAMTYPE_U32,  "OutputStreamFree", STAMUNIT_COUNT, "Number of free output stream slots");

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Audio driver registration record.
 */
const PDMDRVREG g_DrvAUDIO =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "AUDIO",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Audio connector driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass */
    PDM_DRVREG_CLASS_AUDIO,
    /* cMaxInstances */
    UINT32_MAX,
    /* cbInstance */
    sizeof(DRVAUDIO),
    /* pfnConstruct */
    drvAudioConstruct,
    /* pfnDestruct */
    drvAudioDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    drvAudioSuspend,
    /* pfnResume */
    drvAudioResume,
    /* pfnAttach */
    drvAudioAttach,
    /* pfnDetach */
    drvAudioDetach,
    /* pfnPowerOff */
    drvAudioPowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

