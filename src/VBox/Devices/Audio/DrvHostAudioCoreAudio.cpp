/* $Id: DrvHostAudioCoreAudio.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Host audio driver - Mac OS X CoreAudio.
 *
 * For relevant Apple documentation, here are some starters:
 *     - Core Audio Essentials
 *       https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/CoreAudioOverview/CoreAudioEssentials/CoreAudioEssentials.html
 *     - TN2097: Playing a sound file using the Default Output Audio Unit
 *       https://developer.apple.com/library/archive/technotes/tn2097/
 *     - TN2091: Device input using the HAL Output Audio Unit
 *       https://developer.apple.com/library/archive/technotes/tn2091/
 *     - Audio Component Services
 *       https://developer.apple.com/documentation/audiounit/audio_component_services?language=objc
 *     - QA1533: How to handle kAudioUnitProperty_MaximumFramesPerSlice
 *       https://developer.apple.com/library/archive/qa/qa1533/
 *     - QA1317: Signaling the end of data when using AudioConverterFillComplexBuffer
 *       https://developer.apple.com/library/archive/qa/qa1317/
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#include <VBox/log.h>
#include <VBox/vmm/pdmaudioinline.h>
#include <VBox/vmm/pdmaudiohostenuminline.h>

#include "VBoxDD.h"

#include <iprt/asm.h>
#include <iprt/cdefs.h>
#include <iprt/circbuf.h>
#include <iprt/mem.h>
#include <iprt/uuid.h>
#include <iprt/timer.h>

#include <CoreAudio/CoreAudio.h>
#include <CoreServices/CoreServices.h>
#include <AudioToolbox/AudioQueue.h>
#include <AudioUnit/AudioUnit.h>


RT_GCC_NO_WARN_DEPRECATED_BEGIN /* Much here is deprecated since 12.0 */

#if MAC_OS_X_VERSION_MIN_REQUIRED < 1090 /* possibly 1080 */
# define kAudioHardwarePropertyTranslateUIDToDevice (AudioObjectPropertySelector)'uidd'
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The max number of queue buffers we'll use. */
#define COREAUDIO_MAX_BUFFERS       1024
/** The minimum number of queue buffers. */
#define COREAUDIO_MIN_BUFFERS       4

/** Enables the worker thread.
 * This saves CoreAudio from creating an additional thread upon queue
 * creation.  (It does not help with the slow AudioQueueDispose fun.)  */
#define CORE_AUDIO_WITH_WORKER_THREAD
#if 0
/** Enables the AudioQueueDispose breakpoint timer (debugging help). */
# define CORE_AUDIO_WITH_BREAKPOINT_TIMER
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to the instance data for a Core Audio driver instance.  */
typedef struct DRVHOSTCOREAUDIO *PDRVHOSTCOREAUDIO;
/** Pointer to the Core Audio specific backend data for an audio stream. */
typedef struct COREAUDIOSTREAM *PCOREAUDIOSTREAM;

/**
 * Core Audio device entry (enumeration).
 *
 * @note This is definitely not safe to just copy!
 */
typedef struct COREAUDIODEVICEDATA
{
    /** The core PDM structure. */
    PDMAUDIOHOSTDEV     Core;

    /** The audio device ID of the currently used device (UInt32 typedef). */
    AudioDeviceID       idDevice;
} COREAUDIODEVICEDATA;
/** Pointer to a Core Audio device entry (enumeration). */
typedef COREAUDIODEVICEDATA *PCOREAUDIODEVICEDATA;


/**
 * Audio device information.
 *
 * We do not use COREAUDIODEVICEDATA here as it contains lots more than what we
 * need and care to query.  We also don't want to depend on DrvAudio making
 * PDMIHOSTAUDIO::pfnGetDevices callbacks to keep this information up to date.
 */
typedef struct DRVHSTAUDCADEVICE
{
    /** The audio device ID. kAudioDeviceUnknown if not available. */
    AudioObjectID       idDevice;
    /** Indicates whether we've registered device change listener. */
    bool                fRegisteredListeners;
    /** The UID string (must release).  NULL if not available. */
    CFStringRef         hStrUid;
    /** The UID string for a specific device, NULL if we're using the default device. */
    char               *pszSpecific;
} DRVHSTAUDCADEVICE;
/** Pointer to info about a default device. */
typedef DRVHSTAUDCADEVICE *PDRVHSTAUDCADEVICE;


/**
 * Core Audio stream state.
 */
typedef enum COREAUDIOINITSTATE
{
    /** The device is uninitialized. */
    COREAUDIOINITSTATE_UNINIT = 0,
    /** The device is currently initializing. */
    COREAUDIOINITSTATE_IN_INIT,
    /** The device is initialized. */
    COREAUDIOINITSTATE_INIT,
    /** The device is currently uninitializing. */
    COREAUDIOINITSTATE_IN_UNINIT,
    /** The usual 32-bit hack. */
    COREAUDIOINITSTATE_32BIT_HACK = 0x7fffffff
} COREAUDIOINITSTATE;


/**
 * Core audio buffer tracker.
 *
 * For output buffer we'll be using AudioQueueBuffer::mAudioDataByteSize to
 * track how much we've written.  When a buffer is full, or if we run low on
 * queued bufferes, it will be queued.
 *
 * For input buffer we'll be using offRead to track how much we've read.
 *
 * The queued/not-queued state is stored in the first bit of
 * AudioQueueBuffer::mUserData.  While bits 8 and up holds the index into
 * COREAUDIOSTREAM::paBuffers.
 */
typedef struct COREAUDIOBUF
{
    /** The buffer. */
    AudioQueueBufferRef     pBuf;
    /** The buffer read offset (input only). */
    uint32_t                offRead;
} COREAUDIOBUF;
/** Pointer to a core audio buffer tracker. */
typedef COREAUDIOBUF *PCOREAUDIOBUF;


/**
 * Core Audio specific data for an audio stream.
 */
typedef struct COREAUDIOSTREAM
{
    /** Common part. */
    PDMAUDIOBACKENDSTREAM       Core;

    /** The stream's acquired configuration. */
    PDMAUDIOSTREAMCFG           Cfg;
    /** List node for the device's stream list. */
    RTLISTNODE                  Node;
    /** The acquired (final) audio format for this stream.
     * @note This what the device requests, we don't alter anything. */
    AudioStreamBasicDescription BasicStreamDesc;
    /** The actual audio queue being used. */
    AudioQueueRef               hAudioQueue;

    /** Number of buffers. */
    uint32_t                    cBuffers;
    /** The array of buffer. */
    PCOREAUDIOBUF               paBuffers;

    /** Initialization status tracker, actually COREAUDIOINITSTATE.
     * Used when some of the device parameters or the device itself is changed
     * during the runtime. */
    volatile uint32_t           enmInitState;
    /** The current buffer being written to / read from. */
    uint32_t                    idxBuffer;
    /** Set if the stream is enabled. */
    bool                        fEnabled;
    /** Set if the stream is started (playing/capturing). */
    bool                        fStarted;
    /** Set if the stream is draining (output only). */
    bool                        fDraining;
    /** Set if we should restart the stream on resume (saved pause state). */
    bool                        fRestartOnResume;
//    /** Set if we're switching to a new output/input device. */
//    bool                        fSwitchingDevice;
    /** Internal stream offset (bytes). */
    uint64_t                    offInternal;
    /** The RTTimeMilliTS() at the end of the last transfer. */
    uint64_t                    msLastTransfer;

    /** Critical section for serializing access between thread + callbacks. */
    RTCRITSECT                  CritSect;
    /** Buffer that drvHstAudCaStreamStatusString uses. */
    char                        szStatus[64];
} COREAUDIOSTREAM;


/**
 * Instance data for a Core Audio host audio driver.
 *
 * @implements PDMIAUDIOCONNECTOR
 */
typedef struct DRVHOSTCOREAUDIO
{
    /** Pointer to the driver instance structure. */
    PPDMDRVINS              pDrvIns;
    /** Pointer to host audio interface. */
    PDMIHOSTAUDIO           IHostAudio;
    /** The input device. */
    DRVHSTAUDCADEVICE       InputDevice;
    /** The output device. */
    DRVHSTAUDCADEVICE       OutputDevice;
    /** Upwards notification interface. */
    PPDMIHOSTAUDIOPORT      pIHostAudioPort;
    /** Indicates whether we've registered default input device change listener. */
    bool                    fRegisteredDefaultInputListener;
    /** Indicates whether we've registered default output device change listener. */
    bool                    fRegisteredDefaultOutputListener;

#ifdef CORE_AUDIO_WITH_WORKER_THREAD
    /** @name Worker Thread For Queue callbacks and stuff.
     * @{ */
    /** The worker thread. */
    RTTHREAD                hThread;
    /** The runloop of the worker thread. */
    CFRunLoopRef            hThreadRunLoop;
    /** The message port we use to talk to the thread.
     * @note While we don't currently use the port, it is necessary to prevent
     *       the thread from spinning or stopping prematurely because of
     *       CFRunLoopRunInMode returning kCFRunLoopRunFinished. */
    CFMachPortRef           hThreadPort;
    /** Runloop source for hThreadPort. */
    CFRunLoopSourceRef      hThreadPortSrc;
    /** @} */
#endif

    /** Critical section to serialize access. */
    RTCRITSECT              CritSect;
#ifdef CORE_AUDIO_WITH_BREAKPOINT_TIMER
    /** Timder for debugging AudioQueueDispose slowness. */
    RTTIMERLR               hBreakpointTimer;
#endif
} DRVHOSTCOREAUDIO;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void drvHstAudCaUpdateOneDefaultDevice(PDRVHOSTCOREAUDIO pThis, PDRVHSTAUDCADEVICE pDevice, bool fInput, bool fNotify);

/* DrvHostAudioCoreAudioAuth.mm: */
DECLHIDDEN(int) coreAudioInputPermissionCheck(void);


#ifdef LOG_ENABLED
/**
 * Gets the stream status.
 *
 * @returns Pointer to stream status string.
 * @param   pStreamCA   The stream to get the status for.
 */
static const char *drvHstAudCaStreamStatusString(PCOREAUDIOSTREAM pStreamCA)
{
    static RTSTRTUPLE const s_aInitState[5] =
    {
        { RT_STR_TUPLE("UNINIT")    },
        { RT_STR_TUPLE("IN_INIT")   },
        { RT_STR_TUPLE("INIT")      },
        { RT_STR_TUPLE("IN_UNINIT") },
        { RT_STR_TUPLE("BAD")       },
    };
    uint32_t enmInitState = pStreamCA->enmInitState;
    PCRTSTRTUPLE pTuple = &s_aInitState[RT_MIN(enmInitState, RT_ELEMENTS(s_aInitState) - 1)];
    memcpy(pStreamCA->szStatus, pTuple->psz, pTuple->cch);
    size_t off = pTuple->cch;

    static RTSTRTUPLE const s_aEnable[2] =
    {
        { RT_STR_TUPLE("DISABLED") },
        { RT_STR_TUPLE("ENABLED ") },
    };
    pTuple = &s_aEnable[pStreamCA->fEnabled];
    memcpy(pStreamCA->szStatus, pTuple->psz, pTuple->cch);
    off += pTuple->cch;

    static RTSTRTUPLE const s_aStarted[2] =
    {
        { RT_STR_TUPLE(" STOPPED") },
        { RT_STR_TUPLE(" STARTED") },
    };
    pTuple = &s_aStarted[pStreamCA->fStarted];
    memcpy(&pStreamCA->szStatus[off], pTuple->psz, pTuple->cch);
    off += pTuple->cch;

    static RTSTRTUPLE const s_aDraining[2] =
    {
        { RT_STR_TUPLE("")          },
        { RT_STR_TUPLE(" DRAINING") },
    };
    pTuple = &s_aDraining[pStreamCA->fDraining];
    memcpy(&pStreamCA->szStatus[off], pTuple->psz, pTuple->cch);
    off += pTuple->cch;

    Assert(off < sizeof(pStreamCA->szStatus));
    pStreamCA->szStatus[off] = '\0';
    return pStreamCA->szStatus;
}
#endif /*LOG_ENABLED*/




#if 0 /* unused */
static int drvHstAudCaCFStringToCString(const CFStringRef pCFString, char **ppszString)
{
    CFIndex cLen = CFStringGetLength(pCFString) + 1;
    char *pszResult = (char *)RTMemAllocZ(cLen * sizeof(char));
    if (!CFStringGetCString(pCFString, pszResult, cLen, kCFStringEncodingUTF8))
    {
        RTMemFree(pszResult);
        return VERR_NOT_FOUND;
    }

    *ppszString = pszResult;
    return VINF_SUCCESS;
}

static AudioDeviceID drvHstAudCaDeviceUIDtoID(const char* pszUID)
{
    /* Create a CFString out of our CString. */
    CFStringRef strUID = CFStringCreateWithCString(NULL, pszUID, kCFStringEncodingMacRoman);

    /* Fill the translation structure. */
    AudioDeviceID deviceID;

    AudioValueTranslation translation;
    translation.mInputData      = &strUID;
    translation.mInputDataSize  = sizeof(CFStringRef);
    translation.mOutputData     = &deviceID;
    translation.mOutputDataSize = sizeof(AudioDeviceID);

    /* Fetch the translation from the UID to the device ID. */
    AudioObjectPropertyAddress PropAddr =
    {
        kAudioHardwarePropertyDeviceForUID,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster
    };

    UInt32 uSize = sizeof(AudioValueTranslation);
    OSStatus err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &PropAddr, 0, NULL, &uSize, &translation);

    /* Release the temporary CFString */
    CFRelease(strUID);

    if (RT_LIKELY(err == noErr))
        return deviceID;

    /* Return the unknown device on error. */
    return kAudioDeviceUnknown;
}
#endif /* unused */


/**
 * Wrapper around AudioObjectGetPropertyData and AudioObjectGetPropertyDataSize.
 *
 * @returns Pointer to temp heap allocation with the data on success, free using
 *          RTMemTmpFree.  NULL on failure, fully logged.
 */
static void *drvHstAudCaGetPropertyDataEx(AudioObjectID idObject, AudioObjectPropertySelector enmSelector,
                                          AudioObjectPropertyScope enmScope, AudioObjectPropertyElement enmElement,
                                          const char *pszWhat, UInt32 *pcb)
{
    AudioObjectPropertyAddress const PropAddr =
    {
        /*.mSelector = */   enmSelector,
        /*.mScope = */      enmScope,
        /*.mElement = */    enmElement
    };

    /*
     * Have to retry here in case the size isn't stable (like if a new device/whatever is added).
    */
    for (uint32_t iTry = 0; ; iTry++)
    {
        UInt32 cb = 0;
        OSStatus orc = AudioObjectGetPropertyDataSize(idObject, &PropAddr, 0, NULL, &cb);
        if (orc == noErr)
        {
            cb = RT_MAX(cb, 1); /* we're allergic to zero allocations. */
            void *pv = RTMemTmpAllocZ(cb);
            if (pv)
            {
                orc = AudioObjectGetPropertyData(idObject, &PropAddr, 0, NULL, &cb, pv);
                if (orc == noErr)
                {
                    Log9Func(("%u/%#x/%#x/%x/%s: returning %p LB %#x\n",
                              idObject, enmSelector, enmScope, enmElement, pszWhat, pv, cb));
                    if (pcb)
                        *pcb = cb;
                    return pv;
                }

                RTMemTmpFree(pv);
                LogFunc(("AudioObjectGetPropertyData(%u/%#x/%#x/%x/%s, cb=%#x) -> %#x, iTry=%d\n",
                         idObject, enmSelector, enmScope, enmElement, pszWhat, cb, orc, iTry));
                if (iTry < 3)
                    continue;
                LogRelMax(32, ("CoreAudio: AudioObjectGetPropertyData(%u/%#x/%#x/%x/%s, cb=%#x) failed: %#x\n",
                               idObject, enmSelector, enmScope, enmElement, pszWhat, cb, orc));
            }
            else
                LogRelMax(32, ("CoreAudio: Failed to allocate %#x bytes (to get %s for %s).\n", cb, pszWhat, idObject));
        }
        else
            LogRelMax(32, ("CoreAudio: Failed to get %s for %u: %#x\n", pszWhat, idObject, orc));
        if (pcb)
            *pcb = 0;
        return NULL;
    }
}


/**
 * Wrapper around AudioObjectGetPropertyData.
 *
 * @returns Success indicator.  Failures (@c false) are fully logged.
 */
static bool drvHstAudCaGetPropertyData(AudioObjectID idObject, AudioObjectPropertySelector enmSelector,
                                       AudioObjectPropertyScope enmScope, AudioObjectPropertyElement enmElement,
                                       const char *pszWhat, void *pv, UInt32 cb)
{
    AudioObjectPropertyAddress const PropAddr =
    {
        /*.mSelector = */   enmSelector,
        /*.mScope = */      enmScope,
        /*.mElement = */    enmElement
    };

    OSStatus orc = AudioObjectGetPropertyData(idObject, &PropAddr, 0, NULL, &cb, pv);
    if (orc == noErr)
    {
        Log9Func(("%u/%#x/%#x/%x/%s: returning %p LB %#x\n", idObject, enmSelector, enmScope, enmElement, pszWhat, pv, cb));
        return true;
    }
    LogRelMax(64, ("CoreAudio: Failed to query %s (%u/%#x/%#x/%x, cb=%#x): %#x\n",
                   pszWhat, idObject, enmSelector, enmScope, enmElement, cb, orc));
    return false;
}


/**
 * Count the number of channels in one direction.
 *
 * @returns Channel count.
 */
static uint32_t drvHstAudCaEnumCountChannels(AudioObjectID idObject, AudioObjectPropertyScope enmScope)
{
    uint32_t cChannels = 0;

    AudioBufferList *pBufs
        = (AudioBufferList *)drvHstAudCaGetPropertyDataEx(idObject, kAudioDevicePropertyStreamConfiguration,
                                                          enmScope, kAudioObjectPropertyElementMaster, "stream config", NULL);
    if (pBufs)
    {
        UInt32 idxBuf = pBufs->mNumberBuffers;
        while (idxBuf-- > 0)
        {
            Log9Func(("%u/%#x[%u]: %u\n", idObject, enmScope, idxBuf, pBufs->mBuffers[idxBuf].mNumberChannels));
            cChannels += pBufs->mBuffers[idxBuf].mNumberChannels;
        }

        RTMemTmpFree(pBufs);
    }

    return cChannels;
}


/**
 * Translates a UID to an audio device ID.
 *
 * @returns Audio device ID on success, kAudioDeviceUnknown on failure.
 * @param   hStrUid     The UID string to convert.
 * @param   pszUid      The C-string vresion of @a hStrUid.
 * @param   pszWhat     What we're converting (for logging).
 */
static AudioObjectID drvHstAudCaDeviceUidToId(CFStringRef hStrUid, const char *pszUid, const char *pszWhat)
{
    AudioObjectPropertyAddress const PropAddr =
    {
        /*.mSelector = */   kAudioHardwarePropertyTranslateUIDToDevice,
        /*.mScope = */      kAudioObjectPropertyScopeGlobal,
        /*.mElement = */    kAudioObjectPropertyElementMaster
    };
    AudioObjectID idDevice = 0;
    UInt32        cb       = sizeof(idDevice);
    OSStatus      orc      = AudioObjectGetPropertyData(kAudioObjectSystemObject, &PropAddr,
                                                        sizeof(hStrUid), &hStrUid, &cb, &idDevice);
    if (orc == noErr)
    {
        Log9Func(("%s device UID '%s' -> %RU32\n", pszWhat, pszUid, idDevice));
        return idDevice;
    }
    /** @todo test on < 10.9, see which status code and do a fallback using the
     *        enumeration code.  */
    LogRelMax(64, ("CoreAudio: Failed to translate %s device UID '%s' to audio device ID: %#x\n", pszWhat, pszUid, orc));
    return kAudioDeviceUnknown;
}


/**
 * Copies a CFString to a buffer (UTF-8).
 *
 * @returns VBox status code.  In the case of a buffer overflow, the buffer will
 *          contain data and be correctly terminated (provided @a cbDst is not
 *          zero.)
 */
static int drvHstAudCaCFStringToBuf(CFStringRef hStr, char *pszDst, size_t cbDst)
{
    AssertReturn(cbDst > 0, VERR_BUFFER_OVERFLOW);

    if (CFStringGetCString(hStr, pszDst, cbDst, kCFStringEncodingUTF8))
        return VINF_SUCCESS;

    /* First fallback: */
    const char *pszSrc = CFStringGetCStringPtr(hStr, kCFStringEncodingUTF8);
    if (pszSrc)
        return RTStrCopy(pszDst, cbDst, pszSrc);

    /* Second fallback: */
    CFIndex cbMax = CFStringGetMaximumSizeForEncoding(CFStringGetLength(hStr), kCFStringEncodingUTF8) + 1;
    AssertReturn(cbMax > 0, VERR_INVALID_UTF8_ENCODING);
    AssertReturn(cbMax < (CFIndex)_16M, VERR_OUT_OF_RANGE);

    char *pszTmp = (char *)RTMemTmpAlloc(cbMax);
    AssertReturn(pszTmp, VERR_NO_TMP_MEMORY);

    int rc;
    if (CFStringGetCString(hStr, pszTmp, cbMax, kCFStringEncodingUTF8))
        rc = RTStrCopy(pszDst, cbDst, pszTmp);
    else
    {
        *pszDst = '\0';
        rc = VERR_INVALID_UTF8_ENCODING;
    }

    RTMemTmpFree(pszTmp);
    return rc;
}


/**
 * Copies a CFString to a heap buffer (UTF-8).
 *
 * @returns Pointer to the heap buffer on success, NULL if out of heap or some
 *          conversion/extraction problem
 */
static char *drvHstAudCaCFStringToHeap(CFStringRef hStr)
{
    const char *pszSrc = CFStringGetCStringPtr(hStr, kCFStringEncodingUTF8);
    if (pszSrc)
        return RTStrDup(pszSrc);

    /* Fallback: */
    CFIndex cbMax = CFStringGetMaximumSizeForEncoding(CFStringGetLength(hStr), kCFStringEncodingUTF8) + 1;
    AssertReturn(cbMax > 0, NULL);
    AssertReturn(cbMax < (CFIndex)_16M, NULL);

    char *pszDst = RTStrAlloc(cbMax);
    if (pszDst)
    {
        AssertReturnStmt(CFStringGetCString(hStr, pszDst, cbMax, kCFStringEncodingUTF8), RTStrFree(pszDst), NULL);
        size_t const cchDst = strlen(pszDst);
        if (cbMax - cchDst > 32)
            RTStrRealloc(&pszDst, cchDst + 1);
    }
    return pszDst;
}


/*********************************************************************************************************************************
*   Device Change Notification Callbacks                                                                                         *
*********************************************************************************************************************************/

#ifdef LOG_ENABLED
/**
 * Called when the kAudioDevicePropertyNominalSampleRate or
 * kAudioDeviceProcessorOverload properties changes on a default device.
 *
 * Registered on default devices after device enumeration.
 * Not sure on which thread/runloop this runs.
 *
 * (See AudioObjectPropertyListenerProc in the SDK headers.)
 */
static OSStatus drvHstAudCaDevicePropertyChangedCallback(AudioObjectID idObject, UInt32 cAddresses,
                                                         const AudioObjectPropertyAddress paAddresses[], void *pvUser)
{
    LogFlowFunc(("idObject=%#x (%u) cAddresses=%u pvUser=%p\n", idObject, idObject, cAddresses, pvUser));
    for (UInt32 idx = 0; idx < cAddresses; idx++)
        LogFlowFunc(("  #%u: sel=%#x scope=%#x element=%#x\n",
                     idx, paAddresses[idx].mSelector, paAddresses[idx].mScope, paAddresses[idx].mElement));

/** @todo r=bird: What's the plan here exactly?  I've changed it to
 *        LOG_ENABLED only for now, as this has no other purpose. */
    switch (idObject)
    {
        case kAudioDeviceProcessorOverload:
            LogFunc(("Processor overload detected!\n"));
            break;
        case kAudioDevicePropertyNominalSampleRate:
            LogFunc(("kAudioDevicePropertyNominalSampleRate!\n"));
            break;
        default:
            /* Just skip. */
            break;
    }

    return noErr;
}
#endif /* LOG_ENABLED */


/**
 * Called when the kAudioDevicePropertyDeviceIsAlive property changes on a
 * default device.
 *
 * The purpose is mainly to log the event.  There isn't much we can do about
 * active streams or future one, other than waiting for a default device change
 * notification callback.  In the mean time, active streams should start failing
 * to work and new ones fail on creation.  This is the same for when we're
 * configure to use specific devices, only we don't get any device change
 * callback like for default ones.
 *
 * Not sure on which thread/runloop this runs.
 *
 * (See AudioObjectPropertyListenerProc in the SDK headers.)
 */
static OSStatus drvHstAudCaDeviceIsAliveChangedCallback(AudioObjectID idObject, UInt32 cAddresses,
                                                        const AudioObjectPropertyAddress paAddresses[], void *pvUser)
{
    PDRVHOSTCOREAUDIO pThis = (PDRVHOSTCOREAUDIO)pvUser;
    AssertPtr(pThis);
    RT_NOREF(cAddresses, paAddresses);

    /*
     * Log everything.
     */
    LogFlowFunc(("idObject=%#x (%u) cAddresses=%u\n", idObject, idObject, cAddresses));
    for (UInt32 idx = 0; idx < cAddresses; idx++)
        LogFlowFunc(("  #%u: sel=%#x scope=%#x element=%#x\n",
                     idx, paAddresses[idx].mSelector, paAddresses[idx].mScope, paAddresses[idx].mElement));

    /*
     * Check which devices are affected.
     */
    int rc = RTCritSectEnter(&pThis->CritSect);
    AssertRCReturn(rc, noErr); /* could be a destruction race */

    for (unsigned i = 0; i < 2; i++)
    {
        if (idObject == (i == 0 ? pThis->InputDevice.idDevice : pThis->OutputDevice.idDevice))
        {
            AudioObjectPropertyAddress const PropAddr =
            {
                kAudioDevicePropertyDeviceIsAlive,
                i == 0 ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput,
                kAudioObjectPropertyElementMaster
            };
            UInt32   fAlive = 0;
            UInt32   cb     = sizeof(fAlive);
            OSStatus orc    = AudioObjectGetPropertyData(idObject, &PropAddr, 0, NULL, &cb, &fAlive);
            if (   orc == kAudioHardwareBadDeviceError
                || (orc == noErr && !fAlive))
            {
                LogRel(("CoreAudio: The default %s device (%u) stopped functioning.\n", idObject, i == 0 ? "input" : "output"));
#if 0 /* This will only cause an extra re-init (in addition to the default device change) and likely do no good even if that
         default device change callback doesn't arrive.  So, don't do it! (bird) */
                PPDMIHOSTAUDIOPORT pIHostAudioPort = pThis->pIHostAudioPort;
                if (pIHostAudioPort)
                {
                    RTCritSectLeave(&pThis->CritSect);

                    pIHostAudioPort->pfnNotifyDeviceChanged(pIHostAudioPort, i == 0 ? PDMAUDIODIR_IN : PDMAUDIODIR_OUT, NULL);

                    rc = RTCritSectEnter(&pThis->CritSect);
                    AssertRCReturn(rc, noErr); /* could be a destruction race */
                }
#endif
            }
        }
    }

    RTCritSectLeave(&pThis->CritSect);
    return noErr;
}


/**
 * Called when the default recording or playback device has changed.
 *
 * Registered by the constructor.  Not sure on which thread/runloop this runs.
 *
 * (See AudioObjectPropertyListenerProc in the SDK headers.)
 */
static OSStatus drvHstAudCaDefaultDeviceChangedCallback(AudioObjectID idObject, UInt32 cAddresses,
                                                        const AudioObjectPropertyAddress *paAddresses, void *pvUser)

{
    PDRVHOSTCOREAUDIO pThis = (PDRVHOSTCOREAUDIO)pvUser;
    AssertPtr(pThis);
    RT_NOREF(idObject, cAddresses, paAddresses);

    /*
     * Log everything.
     */
    LogFlowFunc(("idObject=%#x (%u) cAddresses=%u\n", idObject, idObject, cAddresses));
    for (UInt32 idx = 0; idx < cAddresses; idx++)
        LogFlowFunc(("  #%u: sel=%#x scope=%#x element=%#x\n",
                     idx, paAddresses[idx].mSelector, paAddresses[idx].mScope, paAddresses[idx].mElement));

    /*
     * Update the default devices and notify parent driver if anything actually changed.
     */
    drvHstAudCaUpdateOneDefaultDevice(pThis, &pThis->OutputDevice, false /*fInput*/, true /*fNotify*/);
    drvHstAudCaUpdateOneDefaultDevice(pThis, &pThis->InputDevice,   true /*fInput*/, true /*fNotify*/);

    return noErr;
}


/**
 * Registers callbacks for a specific Core Audio device.
 *
 * @returns true if idDevice isn't kAudioDeviceUnknown and callbacks were
 *          registered, otherwise false.
 * @param   pThis       The core audio driver instance data.
 * @param   idDevice    The device ID to deregister callbacks for.
 */
static bool drvHstAudCaDeviceRegisterCallbacks(PDRVHOSTCOREAUDIO pThis, AudioObjectID idDevice)
{
    if (idDevice != kAudioDeviceUnknown)
    {
        LogFunc(("idDevice=%RU32\n", idDevice));
        AudioObjectPropertyAddress PropAddr =
        {
            kAudioDevicePropertyDeviceIsAlive,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMaster
        };
        OSStatus orc;
        orc = AudioObjectAddPropertyListener(idDevice, &PropAddr, drvHstAudCaDeviceIsAliveChangedCallback, pThis);
        unsigned cRegistrations = orc == noErr;
        if (   orc != noErr
            && orc != kAudioHardwareIllegalOperationError)
            LogRel(("CoreAudio: Failed to add the recording device state changed listener (%#x)\n", orc));

#ifdef LOG_ENABLED
        PropAddr.mSelector = kAudioDeviceProcessorOverload;
        PropAddr.mScope    = kAudioUnitScope_Global;
        orc = AudioObjectAddPropertyListener(idDevice, &PropAddr, drvHstAudCaDevicePropertyChangedCallback, pThis);
        cRegistrations += orc == noErr;
        if (orc != noErr)
            LogRel(("CoreAudio: Failed to register processor overload listener (%#x)\n", orc));

        PropAddr.mSelector = kAudioDevicePropertyNominalSampleRate;
        PropAddr.mScope    = kAudioUnitScope_Global;
        orc = AudioObjectAddPropertyListener(idDevice, &PropAddr, drvHstAudCaDevicePropertyChangedCallback, pThis);
        cRegistrations += orc == noErr;
        if (orc != noErr)
            LogRel(("CoreAudio: Failed to register sample rate changed listener (%#x)\n", orc));
#endif
        return cRegistrations > 0;
    }
    return false;
}


/**
 * Undoes what drvHstAudCaDeviceRegisterCallbacks() did.
 *
 * @param   pThis       The core audio driver instance data.
 * @param   idDevice    The device ID to deregister callbacks for.
 */
static void drvHstAudCaDeviceUnregisterCallbacks(PDRVHOSTCOREAUDIO pThis, AudioObjectID idDevice)
{
    if (idDevice != kAudioDeviceUnknown)
    {
        LogFunc(("idDevice=%RU32\n", idDevice));
        AudioObjectPropertyAddress PropAddr =
        {
            kAudioDevicePropertyDeviceIsAlive,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMaster
        };
        OSStatus orc;
        orc = AudioObjectRemovePropertyListener(idDevice, &PropAddr, drvHstAudCaDeviceIsAliveChangedCallback, pThis);
        if (   orc != noErr
            && orc != kAudioHardwareBadObjectError)
            LogRel(("CoreAudio: Failed to remove the device alive listener (%#x)\n", orc));

#ifdef LOG_ENABLED
        PropAddr.mSelector = kAudioDeviceProcessorOverload;
        orc = AudioObjectRemovePropertyListener(idDevice, &PropAddr, drvHstAudCaDevicePropertyChangedCallback, pThis);
        if (   orc != noErr
            && orc != kAudioHardwareBadObjectError)
            LogRel(("CoreAudio: Failed to remove the recording processor overload listener (%#x)\n", orc));

        PropAddr.mSelector = kAudioDevicePropertyNominalSampleRate;
        orc = AudioObjectRemovePropertyListener(idDevice, &PropAddr, drvHstAudCaDevicePropertyChangedCallback, pThis);
        if (   orc != noErr
            && orc != kAudioHardwareBadObjectError)
            LogRel(("CoreAudio: Failed to remove the sample rate changed listener (%#x)\n", orc));
#endif
    }
}


/**
 * Updates the default device for one direction.
 *
 * @param   pThis       The core audio driver instance data.
 * @param   pDevice     The device information to update.
 * @param   fInput      Set if input device, clear if output.
 * @param   fNotify     Whether to notify the parent driver if something
 *                      changed.
 */
static void drvHstAudCaUpdateOneDefaultDevice(PDRVHOSTCOREAUDIO pThis, PDRVHSTAUDCADEVICE pDevice, bool fInput, bool fNotify)
{
    /*
     * Skip if there is a specific device we should use for this direction.
     */
    if (pDevice->pszSpecific)
        return;

    /*
     * Get the information before we enter the critical section.
     *
     * (Yeah, this may make us get things wrong if the defaults changes really
     * fast and we get notifications in parallel on multiple threads.  However,
     * the first is a don't-do-that situation and the latter is unlikely.)
     */
    AudioDeviceID idDefaultDev = kAudioDeviceUnknown;
    if (!drvHstAudCaGetPropertyData(kAudioObjectSystemObject,
                                    fInput ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice,
                                    kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster,
                                    fInput ? "default input device" : "default output device",
                                    &idDefaultDev, sizeof(idDefaultDev)))
        idDefaultDev = kAudioDeviceUnknown;

    CFStringRef hStrUid = NULL;
    if (idDefaultDev != kAudioDeviceUnknown)
    {
        if (!drvHstAudCaGetPropertyData(idDefaultDev, kAudioDevicePropertyDeviceUID,
                                        fInput ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput,
                                        kAudioObjectPropertyElementMaster,
                                        fInput ? "default input device UID" : "default output device UID",
                                        &hStrUid, sizeof(hStrUid)))
            hStrUid = NULL;
    }
    char szUid[128];
    if (hStrUid)
        drvHstAudCaCFStringToBuf(hStrUid, szUid, sizeof(szUid));
    else
        szUid[0] = '\0';

    /*
     * Grab the lock and do the updating.
     *
     * We're a little paranoid wrt the locking in case there turn out to be some kind
     * of race around destruction (there really can't be, but better play safe).
     */
    PPDMIHOSTAUDIOPORT pIHostAudioPort = NULL;

    int rc = RTCritSectEnter(&pThis->CritSect);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        if (idDefaultDev != pDevice->idDevice)
        {
            if (idDefaultDev != kAudioDeviceUnknown)
            {
                LogRel(("CoreAudio: Default %s device: %u (was %u), ID '%s'\n",
                        fInput ? "input" : "output", idDefaultDev, pDevice->idDevice, szUid));
                pIHostAudioPort = fNotify ? pThis->pIHostAudioPort : NULL; /* (only if there is a new device) */
            }
            else
                LogRel(("CoreAudio: Default %s device is gone (was %u)\n", fInput ? "input" : "output", pDevice->idDevice));

            if (pDevice->hStrUid)
                CFRelease(pDevice->hStrUid);
            if (pDevice->fRegisteredListeners)
                drvHstAudCaDeviceUnregisterCallbacks(pThis, pDevice->idDevice);
            pDevice->hStrUid              = hStrUid;
            pDevice->idDevice             = idDefaultDev;
            pDevice->fRegisteredListeners = drvHstAudCaDeviceRegisterCallbacks(pThis, pDevice->idDevice);
            hStrUid = NULL;
        }
        RTCritSectLeave(&pThis->CritSect);
    }

    if (hStrUid != NULL)
        CFRelease(hStrUid);

    /*
     * Notify parent driver to trigger a re-init of any associated streams.
     */
    if (pIHostAudioPort)
    {
        LogFlowFunc(("Notifying parent driver about %s default device change...\n", fInput ? "input" : "output"));
        pIHostAudioPort->pfnNotifyDeviceChanged(pIHostAudioPort, fInput ? PDMAUDIODIR_IN : PDMAUDIODIR_OUT, NULL /*pvUser*/);
    }
}


/**
 * Sets the device to use in one or the other direction (@a fInput).
 *
 * @returns VBox status code.
 * @param   pThis       The core audio driver instance data.
 * @param   pDevice     The device info structure to update.
 * @param   fInput      Set if input, clear if output.
 * @param   fNotify     Whether to notify the parent driver if something
 *                      changed.
 * @param   pszUid      The UID string for the device to use.  NULL or empty
 *                      string if default should be used.
 */
static int drvHstAudCaSetDevice(PDRVHOSTCOREAUDIO pThis, PDRVHSTAUDCADEVICE pDevice, bool fInput, bool fNotify,
                                const char *pszUid)
{
    if (!pszUid || !*pszUid)
    {
        /*
         * Use default.  Always refresh the given default device.
         */
        int rc = RTCritSectEnter(&pThis->CritSect);
        AssertRCReturn(rc, rc);

        if (pDevice->pszSpecific)
        {
            LogRel(("CoreAudio: Changing %s device from '%s' to default.\n", fInput ? "input" : "output", pDevice->pszSpecific));
            RTStrFree(pDevice->pszSpecific);
            pDevice->pszSpecific = NULL;
        }

        RTCritSectLeave(&pThis->CritSect);

        drvHstAudCaUpdateOneDefaultDevice(pThis, pDevice, fInput, fNotify);
    }
    else
    {
        /*
         * Use device specified by pszUid.  If not change, search for the device
         * again if idDevice is unknown.
         */
        int rc = RTCritSectEnter(&pThis->CritSect);
        AssertRCReturn(rc, rc);

        bool fSkip = false;
        bool fSame = false;
        if (pDevice->pszSpecific)
        {
            if (strcmp(pszUid, pDevice->pszSpecific) != 0)
            {
                LogRel(("CoreAudio: Changing %s device from '%s' to '%s'.\n",
                        fInput ? "input" : "output", pDevice->pszSpecific, pszUid));
                RTStrFree(pDevice->pszSpecific);
                pDevice->pszSpecific = NULL;
            }
            else
            {
                fSkip = pDevice->idDevice != kAudioDeviceUnknown;
                fSame = true;
            }
        }
        else
            LogRel(("CoreAudio: Changing %s device from default to '%s'.\n", fInput ? "input" : "output", pszUid));

        /*
         * Allocate and swap the strings. This is the bit that might fail.
         */
        if (!fSame)
        {
            CFStringRef hStrUid     = CFStringCreateWithBytes(NULL /*allocator*/, (UInt8 const *)pszUid, (CFIndex)strlen(pszUid),
                                                              kCFStringEncodingUTF8, false /*isExternalRepresentation*/);
            char       *pszSpecific = RTStrDup(pszUid);
            if (hStrUid && pszSpecific)
            {
                if (pDevice->hStrUid)
                    CFRelease(pDevice->hStrUid);
                pDevice->hStrUid = hStrUid;
                RTStrFree(pDevice->pszSpecific);
                pDevice->pszSpecific = pszSpecific;
            }
            else
            {
                RTCritSectLeave(&pThis->CritSect);

                LogFunc(("returns VERR_NO_STR_MEMORY!\n"));
                if (hStrUid)
                    CFRelease(hStrUid);
                RTStrFree(pszSpecific);
                return VERR_NO_STR_MEMORY;
            }

            if (pDevice->fRegisteredListeners)
            {
                drvHstAudCaDeviceUnregisterCallbacks(pThis, pDevice->idDevice);
                pDevice->fRegisteredListeners = false;
            }
        }

        /*
         * Locate the device ID corresponding to the UID string.
         */
        if (!fSkip)
        {
            pDevice->idDevice             = drvHstAudCaDeviceUidToId(pDevice->hStrUid, pszUid, fInput ? "input" : "output");
            pDevice->fRegisteredListeners = drvHstAudCaDeviceRegisterCallbacks(pThis, pDevice->idDevice);
        }

        PPDMIHOSTAUDIOPORT pIHostAudioPort = fNotify && !fSame ? pThis->pIHostAudioPort : NULL;
        RTCritSectLeave(&pThis->CritSect);

        /*
         * Notify parent driver to trigger a re-init of any associated streams.
         */
        if (pIHostAudioPort)
        {
            LogFlowFunc(("Notifying parent driver about %s device change...\n", fInput ? "input" : "output"));
            pIHostAudioPort->pfnNotifyDeviceChanged(pIHostAudioPort, fInput ? PDMAUDIODIR_IN : PDMAUDIODIR_OUT, NULL /*pvUser*/);
        }
    }
    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Worker Thread                                                                                                                *
*********************************************************************************************************************************/
#ifdef CORE_AUDIO_WITH_WORKER_THREAD

/**
 * Message handling callback for CFMachPort.
 */
static void drvHstAudCaThreadPortCallback(CFMachPortRef hPort, void *pvMsg, CFIndex cbMsg, void *pvUser)
{
    RT_NOREF(hPort, pvMsg, cbMsg, pvUser);
    LogFunc(("hPort=%p pvMsg=%p cbMsg=%#x pvUser=%p\n", hPort, pvMsg, cbMsg, pvUser));
}


/**
 * @callback_method_impl{FNRTTHREAD, Worker thread for buffer callbacks.}
 */
static DECLCALLBACK(int) drvHstAudCaThread(RTTHREAD hThreadSelf, void *pvUser)
{
    PDRVHOSTCOREAUDIO pThis = (PDRVHOSTCOREAUDIO)pvUser;

    /*
     * Get the runloop, add the mach port to it and signal the constructor thread that we're ready.
     */
    pThis->hThreadRunLoop = CFRunLoopGetCurrent();
    CFRetain(pThis->hThreadRunLoop);

    CFRunLoopAddSource(pThis->hThreadRunLoop, pThis->hThreadPortSrc, kCFRunLoopDefaultMode);

    int rc = RTThreadUserSignal(hThreadSelf);
    AssertRCReturn(rc, rc);

    /*
     * Do work.
     */
    for (;;)
    {
        SInt32 rcRunLoop = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 30.0, TRUE);
        Log8Func(("CFRunLoopRunInMode -> %d\n", rcRunLoop));
        Assert(rcRunLoop != kCFRunLoopRunFinished);
        if (rcRunLoop != kCFRunLoopRunStopped && rcRunLoop != kCFRunLoopRunFinished)
        { /* likely */ }
        else
            break;
    }

    /*
     * Clean up.
     */
    CFRunLoopRemoveSource(pThis->hThreadRunLoop, pThis->hThreadPortSrc, kCFRunLoopDefaultMode);
    LogFunc(("The thread quits!\n"));
    return VINF_SUCCESS;
}

#endif /* CORE_AUDIO_WITH_WORKER_THREAD */



/*********************************************************************************************************************************
*   PDMIHOSTAUDIO                                                                                                                *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetConfig}
 */
static DECLCALLBACK(int) drvHstAudCaHA_GetConfig(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDCFG pBackendCfg)
{
    PDRVHOSTCOREAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVHOSTCOREAUDIO, IHostAudio);
    AssertPtrReturn(pBackendCfg, VERR_INVALID_POINTER);

    /*
     * Fill in the config structure.
     */
    RTStrCopy(pBackendCfg->szName, sizeof(pBackendCfg->szName), "Core Audio");
    pBackendCfg->cbStream       = sizeof(COREAUDIOSTREAM);
    pBackendCfg->fFlags         = PDMAUDIOBACKEND_F_ASYNC_STREAM_DESTROY;

    RTCritSectEnter(&pThis->CritSect);
#if 0 /** @todo r=bird: This looks like complete utter non-sense to me. */
    /* For Core Audio we provide one stream per device for now. */
    pBackendCfg->cMaxStreamsIn  = PDMAudioHostEnumCountMatching(&pThis->Devices, PDMAUDIODIR_IN);
    pBackendCfg->cMaxStreamsOut = PDMAudioHostEnumCountMatching(&pThis->Devices, PDMAUDIODIR_OUT);
#else
    pBackendCfg->cMaxStreamsIn  = UINT32_MAX;
    pBackendCfg->cMaxStreamsOut = UINT32_MAX;
#endif
    RTCritSectLeave(&pThis->CritSect);

    LogFlowFunc(("Returning %Rrc\n", VINF_SUCCESS));
    return VINF_SUCCESS;
}


/**
 * Creates an enumeration of the host's playback and capture devices.
 *
 * @returns VBox status code.
 * @param   pDevEnm     Where to store the enumerated devices.  Caller is
 *                      expected to clean this up on failure, if so desired.
 *
 * @note    Handling of out-of-memory conditions isn't perhaps as good as it
 *          could be, but it was done so to make the drvHstAudCaGetPropertyData*
 *          functions as uncomplicated as possible.
 */
static int drvHstAudCaDevicesEnumerateAll(PPDMAUDIOHOSTENUM pDevEnm)
{
    AssertPtr(pDevEnm);

    /*
     * First get the UIDs for the default devices.
     */
    AudioDeviceID idDefaultDevIn = kAudioDeviceUnknown;
    if (!drvHstAudCaGetPropertyData(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultInputDevice,
                                    kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster,
                                    "default input device", &idDefaultDevIn, sizeof(idDefaultDevIn)))
        idDefaultDevIn = kAudioDeviceUnknown;
    if (idDefaultDevIn == kAudioDeviceUnknown)
        LogFunc(("No default input device\n"));

    AudioDeviceID idDefaultDevOut = kAudioDeviceUnknown;
    if (!drvHstAudCaGetPropertyData(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice,
                                    kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster,
                                    "default output device", &idDefaultDevOut, sizeof(idDefaultDevOut)))
        idDefaultDevOut = kAudioDeviceUnknown;
    if (idDefaultDevOut == kAudioDeviceUnknown)
        LogFunc(("No default output device\n"));

    /*
     * Get a list of all audio devices.
     * (We have to retry as the we may race new devices being inserted.)
     */
    UInt32         cDevices = 0;
    AudioDeviceID *paidDevices
        = (AudioDeviceID *)drvHstAudCaGetPropertyDataEx(kAudioObjectSystemObject, kAudioHardwarePropertyDevices,
                                                        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster,
                                                        "devices", &cDevices);
    cDevices /= sizeof(paidDevices[0]);

    /*
     * Try get details on each device and try add them to the enumeration result.
     */
    for (uint32_t i = 0; i < cDevices; i++)
    {
        AudioDeviceID const idDevice = paidDevices[i];

        /*
         * Allocate a new device entry and populate it.
         *
         * The only relevant information here is channel counts and the UID(s),
         * everything else is just extras we can live without.
         */
        PCOREAUDIODEVICEDATA pDevEntry = (PCOREAUDIODEVICEDATA)PDMAudioHostDevAlloc(sizeof(*pDevEntry), 0, 0);
        AssertReturnStmt(pDevEntry, RTMemTmpFree(paidDevices), VERR_NO_MEMORY);

        pDevEntry->idDevice = idDevice;
        if (idDevice != kAudioDeviceUnknown)
        {
            if (idDevice == idDefaultDevIn)
                pDevEntry->Core.fFlags |= PDMAUDIOHOSTDEV_F_DEFAULT_IN;
            if (idDevice == idDefaultDevOut)
                pDevEntry->Core.fFlags |= PDMAUDIOHOSTDEV_F_DEFAULT_OUT;
        }

        /* Count channels and determin the usage. */
        pDevEntry->Core.cMaxInputChannels  = drvHstAudCaEnumCountChannels(idDevice, kAudioDevicePropertyScopeInput);
        pDevEntry->Core.cMaxOutputChannels = drvHstAudCaEnumCountChannels(idDevice, kAudioDevicePropertyScopeOutput);
        if (pDevEntry->Core.cMaxInputChannels > 0 && pDevEntry->Core.cMaxOutputChannels > 0)
            pDevEntry->Core.enmUsage = PDMAUDIODIR_DUPLEX;
        else if (pDevEntry->Core.cMaxInputChannels > 0)
            pDevEntry->Core.enmUsage = PDMAUDIODIR_IN;
        else if (pDevEntry->Core.cMaxOutputChannels > 0)
            pDevEntry->Core.enmUsage = PDMAUDIODIR_OUT;
        else
        {
            pDevEntry->Core.enmUsage = PDMAUDIODIR_UNKNOWN;
            pDevEntry->Core.fFlags  |= PDMAUDIOHOSTDEV_F_IGNORE;
            /** @todo drop & skip? */
        }

        /* Get the device UID.  (We ASSUME this is the same for both input and
           output sides of the device.) */
        CFStringRef hStrUid;
        if (!drvHstAudCaGetPropertyData(idDevice, kAudioDevicePropertyDeviceUID, kAudioDevicePropertyDeviceUID,
                                        kAudioObjectPropertyElementMaster,
                                        "device UID", &hStrUid, sizeof(hStrUid)))
            hStrUid = NULL;

        if (hStrUid)
        {
            pDevEntry->Core.pszId   = drvHstAudCaCFStringToHeap(hStrUid);
            pDevEntry->Core.fFlags |= PDMAUDIOHOSTDEV_F_ID_ALLOC;
        }
        else
            pDevEntry->Core.fFlags |= PDMAUDIOHOSTDEV_F_IGNORE;

        /* Get the device name (ignore failures). */
        CFStringRef hStrName = NULL;
        if (drvHstAudCaGetPropertyData(idDevice, kAudioObjectPropertyName,
                                       pDevEntry->Core.enmUsage == PDMAUDIODIR_IN
                                       ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput,
                                       kAudioObjectPropertyElementMaster, "device name", &hStrName, sizeof(hStrName)))
        {
            pDevEntry->Core.pszName = drvHstAudCaCFStringToHeap(hStrName);
            pDevEntry->Core.fFlags |= PDMAUDIOHOSTDEV_F_NAME_ALLOC;
            CFRelease(hStrName);
        }

        /* Check if the device is alive for the intended usage.  For duplex
           devices we'll flag it as dead if either of the directions are dead,
           as there is no convenient way of saying otherwise.  It's acadmic as
           nobody currently 2021-05-22) uses the flag for anything.  */
        UInt32 fAlive = 0;
        if (drvHstAudCaGetPropertyData(idDevice, kAudioDevicePropertyDeviceIsAlive,
                                       pDevEntry->Core.enmUsage == PDMAUDIODIR_IN
                                       ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput,
                                       kAudioObjectPropertyElementMaster, "is-alive", &fAlive, sizeof(fAlive)))
            if (!fAlive)
                pDevEntry->Core.fFlags |= PDMAUDIOHOSTDEV_F_DEAD;
        fAlive = 0;
        if (   pDevEntry->Core.enmUsage == PDMAUDIODIR_DUPLEX
            && !(pDevEntry->Core.fFlags == PDMAUDIOHOSTDEV_F_DEAD)
            && drvHstAudCaGetPropertyData(idDevice, kAudioDevicePropertyDeviceIsAlive, kAudioDevicePropertyScopeInput,
                                          kAudioObjectPropertyElementMaster, "is-alive", &fAlive, sizeof(fAlive)))
            if (!fAlive)
                pDevEntry->Core.fFlags |= PDMAUDIOHOSTDEV_F_DEAD;

        /* Check if the device is being hogged by someone else. */
        pid_t pidHogger = -2;
        if (drvHstAudCaGetPropertyData(idDevice, kAudioDevicePropertyHogMode, kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMaster, "hog-mode", &pidHogger, sizeof(pidHogger)))
            if (pidHogger >= 0)
                pDevEntry->Core.fFlags |= PDMAUDIOHOSTDEV_F_LOCKED;

        /*
         * Try make sure we've got a name...  Only add it to the enumeration if we have one.
         */
        if (!pDevEntry->Core.pszName)
        {
            pDevEntry->Core.pszName = pDevEntry->Core.pszId;
            pDevEntry->Core.fFlags &= ~PDMAUDIOHOSTDEV_F_NAME_ALLOC;
        }

        if (pDevEntry->Core.pszName)
            PDMAudioHostEnumAppend(pDevEnm, &pDevEntry->Core);
        else
            PDMAudioHostDevFree(&pDevEntry->Core);
    }

    RTMemTmpFree(paidDevices);

    LogFunc(("Returning %u devices\n", pDevEnm->cDevices));
    PDMAudioHostEnumLog(pDevEnm, "Core Audio");
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetDevices}
 */
static DECLCALLBACK(int) drvHstAudCaHA_GetDevices(PPDMIHOSTAUDIO pInterface, PPDMAUDIOHOSTENUM pDeviceEnum)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pDeviceEnum, VERR_INVALID_POINTER);

    PDMAudioHostEnumInit(pDeviceEnum);
    int rc = drvHstAudCaDevicesEnumerateAll(pDeviceEnum);
    if (RT_FAILURE(rc))
        PDMAudioHostEnumDelete(pDeviceEnum);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnSetDevice}
 */
static DECLCALLBACK(int) drvHstAudCaHA_SetDevice(PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir, const char *pszId)
{
    PDRVHOSTCOREAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVHOSTCOREAUDIO, IHostAudio);
    AssertPtrNullReturn(pszId, VERR_INVALID_POINTER);
    if (pszId && !*pszId)
        pszId = NULL;
    AssertMsgReturn(enmDir == PDMAUDIODIR_IN || enmDir == PDMAUDIODIR_OUT || enmDir == PDMAUDIODIR_DUPLEX,
                    ("enmDir=%d\n", enmDir, pszId), VERR_INVALID_PARAMETER);

    /*
     * Make the change.
     */
    int rc = VINF_SUCCESS;
    if (enmDir == PDMAUDIODIR_IN || enmDir == PDMAUDIODIR_DUPLEX)
        rc = drvHstAudCaSetDevice(pThis, &pThis->InputDevice, true /*fInput*/, true /*fNotify*/, pszId);
    if (enmDir == PDMAUDIODIR_OUT || (enmDir == PDMAUDIODIR_DUPLEX && RT_SUCCESS(rc)))
        rc = drvHstAudCaSetDevice(pThis, &pThis->OutputDevice, false /*fInput*/, true /*fNotify*/, pszId);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetStatus}
 */
static DECLCALLBACK(PDMAUDIOBACKENDSTS) drvHstAudCaHA_GetStatus(PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir)
{
    RT_NOREF(pInterface, enmDir);
    return PDMAUDIOBACKENDSTS_RUNNING;
}


/**
 * Marks the given buffer as queued or not-queued.
 *
 * @returns Old queued value.
 * @param   pAudioBuffer    The buffer.
 * @param   fQueued         The new queued state.
 */
DECLINLINE(bool) drvHstAudCaSetBufferQueued(AudioQueueBufferRef pAudioBuffer, bool fQueued)
{
    if (fQueued)
        return ASMAtomicBitTestAndSet(&pAudioBuffer->mUserData, 0);
    return ASMAtomicBitTestAndClear(&pAudioBuffer->mUserData, 0);
}


/**
 * Gets the queued state of the buffer.
 * @returns true if queued, false if not.
 * @param   pAudioBuffer    The buffer.
 */
DECLINLINE(bool) drvHstAudCaIsBufferQueued(AudioQueueBufferRef pAudioBuffer)
{
    return ((uintptr_t)pAudioBuffer->mUserData & 1) == 1;
}


/**
 * Output audio queue buffer callback.
 *
 * Called whenever an audio queue is done processing a buffer.  This routine
 * will set the data fill size to zero and mark it as unqueued so that
 * drvHstAudCaHA_StreamPlay knowns it can use it.
 *
 * @param   pvUser          User argument.
 * @param   hAudioQueue     Audio queue to process output data for.
 * @param   pAudioBuffer    Audio buffer to store output data in.
 *
 * @thread  queue thread.
 */
static void drvHstAudCaOutputQueueBufferCallback(void *pvUser, AudioQueueRef hAudioQueue, AudioQueueBufferRef pAudioBuffer)
{
#if defined(VBOX_STRICT) || defined(LOG_ENABLED)
    PCOREAUDIOSTREAM pStreamCA = (PCOREAUDIOSTREAM)pvUser;
    AssertPtr(pStreamCA);
    Assert(pStreamCA->hAudioQueue == hAudioQueue);

    uintptr_t idxBuf = (uintptr_t)pAudioBuffer->mUserData >> 8;
    Log4Func(("Got back buffer #%zu (%p)\n", idxBuf, pAudioBuffer));
    AssertReturnVoid(   idxBuf < pStreamCA->cBuffers
                     && pStreamCA->paBuffers[idxBuf].pBuf == pAudioBuffer);
#endif

    pAudioBuffer->mAudioDataByteSize = 0;
    bool fWasQueued = drvHstAudCaSetBufferQueued(pAudioBuffer, false /*fQueued*/);
    Assert(!drvHstAudCaIsBufferQueued(pAudioBuffer));
    Assert(fWasQueued); RT_NOREF(fWasQueued);

    RT_NOREF(pvUser, hAudioQueue);
}


/**
 * Input audio queue buffer callback.
 *
 * Called whenever input data from the audio queue becomes available.  This
 * routine will mark the buffer unqueued so that drvHstAudCaHA_StreamCapture can
 * read the data from it.
 *
 * @param   pvUser          User argument.
 * @param   hAudioQueue     Audio queue to process input data from.
 * @param   pAudioBuffer    Audio buffer to process input data from.
 * @param   pAudioTS        Audio timestamp.
 * @param   cPacketDesc     Number of packet descriptors.
 * @param   paPacketDesc    Array of packet descriptors.
 */
static void drvHstAudCaInputQueueBufferCallback(void *pvUser, AudioQueueRef hAudioQueue,
                                                AudioQueueBufferRef pAudioBuffer, const AudioTimeStamp *pAudioTS,
                                                UInt32 cPacketDesc, const AudioStreamPacketDescription *paPacketDesc)
{
#if defined(VBOX_STRICT) || defined(LOG_ENABLED)
    PCOREAUDIOSTREAM pStreamCA = (PCOREAUDIOSTREAM)pvUser;
    AssertPtr(pStreamCA);
    Assert(pStreamCA->hAudioQueue == hAudioQueue);

    uintptr_t idxBuf = (uintptr_t)pAudioBuffer->mUserData >> 8;
    Log4Func(("Got back buffer #%zu (%p) with %#x bytes\n", idxBuf, pAudioBuffer, pAudioBuffer->mAudioDataByteSize));
    AssertReturnVoid(   idxBuf < pStreamCA->cBuffers
                     && pStreamCA->paBuffers[idxBuf].pBuf == pAudioBuffer);
#endif

    bool fWasQueued = drvHstAudCaSetBufferQueued(pAudioBuffer, false /*fQueued*/);
    Assert(!drvHstAudCaIsBufferQueued(pAudioBuffer));
    Assert(fWasQueued); RT_NOREF(fWasQueued);

    RT_NOREF(pvUser, hAudioQueue, pAudioTS, cPacketDesc, paPacketDesc);
}


static void drvHstAudCaLogAsbd(const char *pszDesc, const AudioStreamBasicDescription *pASBD)
{
    LogRel2(("CoreAudio: %s description:\n", pszDesc));
    LogRel2(("CoreAudio:  Format ID: %#RX32 (%c%c%c%c)\n", pASBD->mFormatID,
             RT_BYTE4(pASBD->mFormatID), RT_BYTE3(pASBD->mFormatID),
             RT_BYTE2(pASBD->mFormatID), RT_BYTE1(pASBD->mFormatID)));
    LogRel2(("CoreAudio:  Flags: %#RX32", pASBD->mFormatFlags));
    if (pASBD->mFormatFlags & kAudioFormatFlagIsFloat)
        LogRel2((" Float"));
    if (pASBD->mFormatFlags & kAudioFormatFlagIsBigEndian)
        LogRel2((" BigEndian"));
    if (pASBD->mFormatFlags & kAudioFormatFlagIsSignedInteger)
        LogRel2((" SignedInteger"));
    if (pASBD->mFormatFlags & kAudioFormatFlagIsPacked)
        LogRel2((" Packed"));
    if (pASBD->mFormatFlags & kAudioFormatFlagIsAlignedHigh)
        LogRel2((" AlignedHigh"));
    if (pASBD->mFormatFlags & kAudioFormatFlagIsNonInterleaved)
        LogRel2((" NonInterleaved"));
    if (pASBD->mFormatFlags & kAudioFormatFlagIsNonMixable)
        LogRel2((" NonMixable"));
    if (pASBD->mFormatFlags & kAudioFormatFlagsAreAllClear)
        LogRel2((" AllClear"));
    LogRel2(("\n"));
    LogRel2(("CoreAudio:  SampleRate      : %RU64.%02u Hz\n",
             (uint64_t)pASBD->mSampleRate, (unsigned)(pASBD->mSampleRate * 100) % 100));
    LogRel2(("CoreAudio:  ChannelsPerFrame: %RU32\n", pASBD->mChannelsPerFrame));
    LogRel2(("CoreAudio:  FramesPerPacket : %RU32\n", pASBD->mFramesPerPacket));
    LogRel2(("CoreAudio:  BitsPerChannel  : %RU32\n", pASBD->mBitsPerChannel));
    LogRel2(("CoreAudio:  BytesPerFrame   : %RU32\n", pASBD->mBytesPerFrame));
    LogRel2(("CoreAudio:  BytesPerPacket  : %RU32\n", pASBD->mBytesPerPacket));
}


static void drvHstAudCaPropsToAsbd(PCPDMAUDIOPCMPROPS pProps, AudioStreamBasicDescription *pASBD)
{
    AssertPtrReturnVoid(pProps);
    AssertPtrReturnVoid(pASBD);

    RT_BZERO(pASBD, sizeof(AudioStreamBasicDescription));

    pASBD->mFormatID         = kAudioFormatLinearPCM;
    pASBD->mFormatFlags      = kAudioFormatFlagIsPacked;
    if (pProps->fSigned)
        pASBD->mFormatFlags |= kAudioFormatFlagIsSignedInteger;
    if (PDMAudioPropsIsBigEndian(pProps))
        pASBD->mFormatFlags |= kAudioFormatFlagIsBigEndian;
    pASBD->mSampleRate       = PDMAudioPropsHz(pProps);
    pASBD->mChannelsPerFrame = PDMAudioPropsChannels(pProps);
    pASBD->mBitsPerChannel   = PDMAudioPropsSampleBits(pProps);
    pASBD->mBytesPerFrame    = PDMAudioPropsFrameSize(pProps);
    pASBD->mFramesPerPacket  = 1; /* For uncompressed audio, set this to 1. */
    pASBD->mBytesPerPacket   = PDMAudioPropsFrameSize(pProps) * pASBD->mFramesPerPacket;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCreate}
 */
static DECLCALLBACK(int) drvHstAudCaHA_StreamCreate(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                    PCPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    PDRVHOSTCOREAUDIO pThis     = RT_FROM_MEMBER(pInterface, DRVHOSTCOREAUDIO, IHostAudio);
    PCOREAUDIOSTREAM  pStreamCA = (PCOREAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamCA, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgReq, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgAcq, VERR_INVALID_POINTER);
    AssertReturn(pCfgReq->enmDir == PDMAUDIODIR_IN || pCfgReq->enmDir == PDMAUDIODIR_OUT, VERR_INVALID_PARAMETER);
    int rc;

    /** @todo This takes too long.  Stats indicates it may take up to 200 ms.
     *        Knoppix guest resets the stream and we hear nada because the
     *        draining is aborted when the stream is destroyed.  Should try use
     *        async init for parts (much) of this. */

    /*
     * Permission check for input devices before we start.
     */
    if (pCfgReq->enmDir == PDMAUDIODIR_IN)
    {
        rc = coreAudioInputPermissionCheck();
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Do we have a device for the requested stream direction?
     */
    RTCritSectEnter(&pThis->CritSect);
    CFStringRef hDevUidStr = pCfgReq->enmDir == PDMAUDIODIR_IN ? pThis->InputDevice.hStrUid : pThis->OutputDevice.hStrUid;
    if (hDevUidStr)
        CFRetain(hDevUidStr);
    RTCritSectLeave(&pThis->CritSect);

#ifdef LOG_ENABLED
    char szTmp[PDMAUDIOSTRMCFGTOSTRING_MAX];
#endif
    LogFunc(("hDevUidStr=%p *pCfgReq: %s\n", hDevUidStr, PDMAudioStrmCfgToString(pCfgReq, szTmp, sizeof(szTmp)) ));
    if (hDevUidStr)
    {
        /*
         * Basic structure init.
         */
        pStreamCA->fEnabled         = false;
        pStreamCA->fStarted         = false;
        pStreamCA->fDraining        = false;
        pStreamCA->fRestartOnResume = false;
        pStreamCA->offInternal      = 0;
        pStreamCA->idxBuffer        = 0;
        pStreamCA->enmInitState     = COREAUDIOINITSTATE_IN_INIT;

        rc = RTCritSectInit(&pStreamCA->CritSect);
        if (RT_SUCCESS(rc))
        {
            /*
             * Do format conversion and create the circular buffer we use to shuffle
             * data to/from the queue thread.
             */
            PDMAudioStrmCfgCopy(&pStreamCA->Cfg, pCfgReq);
            drvHstAudCaPropsToAsbd(&pCfgReq->Props, &pStreamCA->BasicStreamDesc);
            /** @todo Do some validation? */
            drvHstAudCaLogAsbd(pCfgReq->enmDir == PDMAUDIODIR_IN ? "Capturing queue format" : "Playback queue format",
                               &pStreamCA->BasicStreamDesc);
            /*
             * Create audio queue.
             *
             * Documentation says the callbacks will be run on some core audio
             * related thread if we don't specify a runloop here.  That's simpler.
             */
#ifdef CORE_AUDIO_WITH_WORKER_THREAD
            CFRunLoopRef const hRunLoop     = pThis->hThreadRunLoop;
            CFStringRef  const hRunLoopMode = kCFRunLoopDefaultMode;
#else
            CFRunLoopRef const hRunLoop     = NULL;
            CFStringRef  const hRunLoopMode = NULL;
#endif
            OSStatus orc;
            if (pCfgReq->enmDir == PDMAUDIODIR_OUT)
                orc = AudioQueueNewOutput(&pStreamCA->BasicStreamDesc, drvHstAudCaOutputQueueBufferCallback, pStreamCA,
                                          hRunLoop, hRunLoopMode, 0 /*fFlags - MBZ*/, &pStreamCA->hAudioQueue);
            else
                orc = AudioQueueNewInput(&pStreamCA->BasicStreamDesc, drvHstAudCaInputQueueBufferCallback, pStreamCA,
                                         hRunLoop, hRunLoopMode, 0 /*fFlags - MBZ*/, &pStreamCA->hAudioQueue);
            LogFlowFunc(("AudioQueueNew%s -> %#x\n", pCfgReq->enmDir == PDMAUDIODIR_OUT ? "Output" : "Input", orc));
            if (orc == noErr)
            {
                /*
                 * Assign device to the queue.
                 */
                UInt32 uSize = sizeof(hDevUidStr);
                orc = AudioQueueSetProperty(pStreamCA->hAudioQueue, kAudioQueueProperty_CurrentDevice, &hDevUidStr, uSize);
                LogFlowFunc(("AudioQueueSetProperty -> %#x\n", orc));
                if (orc == noErr)
                {
                    /*
                     * Sanity-adjust the requested buffer size.
                     */
                    uint32_t cFramesBufferSizeMax = PDMAudioPropsMilliToFrames(&pStreamCA->Cfg.Props, 2 * RT_MS_1SEC);
                    uint32_t cFramesBufferSize    = PDMAudioPropsMilliToFrames(&pStreamCA->Cfg.Props, 32 /*ms*/);
                    cFramesBufferSize = RT_MAX(cFramesBufferSize, pCfgReq->Backend.cFramesBufferSize);
                    cFramesBufferSize = RT_MIN(cFramesBufferSize, cFramesBufferSizeMax);

                    /*
                     * The queue buffers size is based on cMsSchedulingHint so that we're likely to
                     * have a new one ready/done after each guest DMA transfer.  We must however
                     * make sure we don't end up with too may or too few.
                     */
                    Assert(pCfgReq->Device.cMsSchedulingHint > 0);
                    uint32_t cFramesQueueBuffer = PDMAudioPropsMilliToFrames(&pStreamCA->Cfg.Props,
                                                                             pCfgReq->Device.cMsSchedulingHint > 0
                                                                             ? pCfgReq->Device.cMsSchedulingHint : 10);
                    uint32_t cQueueBuffers;
                    if (cFramesQueueBuffer * COREAUDIO_MIN_BUFFERS <= cFramesBufferSize)
                    {
                        cQueueBuffers      = cFramesBufferSize / cFramesQueueBuffer;
                        if (cQueueBuffers > COREAUDIO_MAX_BUFFERS)
                        {
                            cQueueBuffers = COREAUDIO_MAX_BUFFERS;
                            cFramesQueueBuffer = cFramesBufferSize / COREAUDIO_MAX_BUFFERS;
                        }
                    }
                    else
                    {
                        cQueueBuffers      = COREAUDIO_MIN_BUFFERS;
                        cFramesQueueBuffer = cFramesBufferSize / COREAUDIO_MIN_BUFFERS;
                    }

                    cFramesBufferSize = cQueueBuffers * cFramesBufferSize;

                    /*
                     * Allocate the audio queue buffers.
                     */
                    pStreamCA->paBuffers = (PCOREAUDIOBUF)RTMemAllocZ(sizeof(pStreamCA->paBuffers[0]) * cQueueBuffers);
                    if (pStreamCA->paBuffers != NULL)
                    {
                        pStreamCA->cBuffers = cQueueBuffers;

                        const size_t cbQueueBuffer = PDMAudioPropsFramesToBytes(&pStreamCA->Cfg.Props, cFramesQueueBuffer);
                        LogFlowFunc(("Allocating %u, each %#x bytes / %u frames\n", cQueueBuffers, cbQueueBuffer, cFramesQueueBuffer));
                        cFramesBufferSize = 0;
                        for (uint32_t iBuf = 0; iBuf < cQueueBuffers; iBuf++)
                        {
                            AudioQueueBufferRef pBuf = NULL;
                            orc = AudioQueueAllocateBuffer(pStreamCA->hAudioQueue, cbQueueBuffer, &pBuf);
                            if (RT_LIKELY(orc == noErr))
                            {
                                pBuf->mUserData = (void *)(uintptr_t)(iBuf << 8); /* bit zero is the queued-indicator. */
                                pStreamCA->paBuffers[iBuf].pBuf = pBuf;
                                cFramesBufferSize += PDMAudioPropsBytesToFrames(&pStreamCA->Cfg.Props,
                                                                                pBuf->mAudioDataBytesCapacity);
                                Assert(PDMAudioPropsIsSizeAligned(&pStreamCA->Cfg.Props, pBuf->mAudioDataBytesCapacity));
                            }
                            else
                            {
                                LogRel(("CoreAudio: Out of memory (buffer %#x out of %#x, %#x bytes)\n",
                                        iBuf, cQueueBuffers, cbQueueBuffer));
                                while (iBuf-- > 0)
                                {
                                    AudioQueueFreeBuffer(pStreamCA->hAudioQueue, pStreamCA->paBuffers[iBuf].pBuf);
                                    pStreamCA->paBuffers[iBuf].pBuf = NULL;
                                }
                                break;
                            }
                        }
                        if (orc == noErr)
                        {
                            /*
                             * Update the stream config.
                             */
                            pStreamCA->Cfg.Backend.cFramesBufferSize   = cFramesBufferSize;
                            pStreamCA->Cfg.Backend.cFramesPeriod       = cFramesQueueBuffer; /* whatever */
                            pStreamCA->Cfg.Backend.cFramesPreBuffering =   pStreamCA->Cfg.Backend.cFramesPreBuffering
                                                                         * pStreamCA->Cfg.Backend.cFramesBufferSize
                                                                       / RT_MAX(pCfgReq->Backend.cFramesBufferSize, 1);

                            PDMAudioStrmCfgCopy(pCfgAcq, &pStreamCA->Cfg);

                            ASMAtomicWriteU32(&pStreamCA->enmInitState, COREAUDIOINITSTATE_INIT);

                            LogFunc(("returns VINF_SUCCESS\n"));
                            CFRelease(hDevUidStr);
                            return VINF_SUCCESS;
                        }

                        RTMemFree(pStreamCA->paBuffers);
                    }
                    else
                        rc = VERR_NO_MEMORY;
                }
                else
                    LogRelMax(64, ("CoreAudio: Failed to associate device with queue: %#x (%d)\n", orc, orc));
                AudioQueueDispose(pStreamCA->hAudioQueue, TRUE /*inImmediate*/);
            }
            else
                LogRelMax(64, ("CoreAudio: Failed to create audio queue: %#x (%d)\n", orc, orc));
            RTCritSectDelete(&pStreamCA->CritSect);
        }
        else
            LogRel(("CoreAudio: Failed to initialize critical section for stream: %Rrc\n", rc));
        CFRelease(hDevUidStr);
    }
    else
    {
        LogRelMax(64, ("CoreAudio: No device for %s stream.\n", PDMAudioDirGetName(pCfgReq->enmDir)));
        rc = VERR_AUDIO_STREAM_COULD_NOT_CREATE;
    }

    LogFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDestroy}
 */
static DECLCALLBACK(int) drvHstAudCaHA_StreamDestroy(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream, bool fImmediate)
{
    RT_NOREF(pInterface);
    PCOREAUDIOSTREAM pStreamCA = (PCOREAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamCA, VERR_INVALID_POINTER);
    LogFunc(("%p: %s fImmediate=%RTbool\n", pStreamCA, pStreamCA->Cfg.szName, fImmediate));
#ifdef LOG_ENABLED
    uint64_t const nsStart = RTTimeNanoTS();
#endif

    /*
     * Never mind if the status isn't INIT (it should always be, though).
     */
    COREAUDIOINITSTATE const enmInitState = (COREAUDIOINITSTATE)ASMAtomicReadU32(&pStreamCA->enmInitState);
    AssertMsg(enmInitState == COREAUDIOINITSTATE_INIT, ("%d\n", enmInitState));
    if (enmInitState == COREAUDIOINITSTATE_INIT)
    {
        Assert(RTCritSectIsInitialized(&pStreamCA->CritSect));

        /*
         * Change the stream state and stop the stream (just to be sure).
         */
        OSStatus orc;
        ASMAtomicWriteU32(&pStreamCA->enmInitState, COREAUDIOINITSTATE_IN_UNINIT);
        if (pStreamCA->hAudioQueue)
        {
            orc = AudioQueueStop(pStreamCA->hAudioQueue, fImmediate ? TRUE : FALSE /*inImmediate/synchronously*/);
            LogFlowFunc(("AudioQueueStop -> %#x\n", orc));
        }

        /*
         * Enter and leave the critsect afterwards for paranoid reasons.
         */
        RTCritSectEnter(&pStreamCA->CritSect);
        RTCritSectLeave(&pStreamCA->CritSect);

        /*
         * Free the queue buffers and the queue.
         *
         * This may take a while.  The AudioQueueReset call seems to helps
         * reducing time stuck in AudioQueueDispose.
         */
#ifdef CORE_AUDIO_WITH_BREAKPOINT_TIMER
        LogRel(("Queue-destruction timer starting...\n"));
        PDRVHOSTCOREAUDIO pThis = RT_FROM_MEMBER(pInterface, DRVHOSTCOREAUDIO, IHostAudio);
        RTTimerLRStart(pThis->hBreakpointTimer, RT_NS_100MS);
        uint64_t nsStart = RTTimeNanoTS();
#endif

#if 0 /* This seems to work even when doing a non-immediate stop&dispose. However, it doesn't make sense conceptually. */
        if (pStreamCA->hAudioQueue /*&& fImmediate*/)
        {
            LogFlowFunc(("Calling AudioQueueReset ...\n"));
            orc = AudioQueueReset(pStreamCA->hAudioQueue);
            LogFlowFunc(("AudioQueueReset -> %#x\n", orc));
        }
#endif

        if (pStreamCA->paBuffers && fImmediate)
        {
            LogFlowFunc(("Freeing %u buffers ...\n", pStreamCA->cBuffers));
            for (uint32_t iBuf = 0; iBuf < pStreamCA->cBuffers; iBuf++)
            {
                orc = AudioQueueFreeBuffer(pStreamCA->hAudioQueue, pStreamCA->paBuffers[iBuf].pBuf);
                AssertMsg(orc == noErr, ("AudioQueueFreeBuffer(#%u) -> orc=%#x\n", iBuf, orc));
                pStreamCA->paBuffers[iBuf].pBuf = NULL;
            }
        }

        if (pStreamCA->hAudioQueue)
        {
            LogFlowFunc(("Disposing of the queue ...\n"));
            orc = AudioQueueDispose(pStreamCA->hAudioQueue, fImmediate ? TRUE : FALSE /*inImmediate/synchronously*/); /* may take some time */
            LogFlowFunc(("AudioQueueDispose -> %#x (%d)\n", orc, orc));
            AssertMsg(orc == noErr, ("AudioQueueDispose -> orc=%#x\n", orc));
            pStreamCA->hAudioQueue = NULL;
        }

        /* We should get no further buffer callbacks at this point according to the docs. */
        if (pStreamCA->paBuffers)
        {
            RTMemFree(pStreamCA->paBuffers);
            pStreamCA->paBuffers = NULL;
        }
        pStreamCA->cBuffers = 0;

#ifdef CORE_AUDIO_WITH_BREAKPOINT_TIMER
        RTTimerLRStop(pThis->hBreakpointTimer);
        LogRel(("Queue-destruction: %'RU64\n", RTTimeNanoTS() - nsStart));
#endif

        /*
         * Delete the critsect and we're done.
         */
        RTCritSectDelete(&pStreamCA->CritSect);

        ASMAtomicWriteU32(&pStreamCA->enmInitState, COREAUDIOINITSTATE_UNINIT);
    }
    else
        LogFunc(("Wrong stream init state for %p: %d - leaking it\n", pStream, enmInitState));

    LogFunc(("returns (took %'RU64 ns)\n", RTTimeNanoTS() - nsStart));
    return VINF_SUCCESS;
}


#ifdef CORE_AUDIO_WITH_BREAKPOINT_TIMER
/** @callback_method_impl{FNRTTIMERLR, For debugging things that takes too long.} */
static DECLCALLBACK(void) drvHstAudCaBreakpointTimer(RTTIMERLR hTimer, void *pvUser, uint64_t iTick)
{
    LogFlowFunc(("Queue-destruction timeout! iTick=%RU64\n", iTick));
    RT_NOREF(hTimer, pvUser, iTick);
    RTLogFlush(NULL);
    RT_BREAKPOINT();
}
#endif


/**
 * @ interface_method_impl{PDMIHOSTAUDIO,pfnStreamEnable}
 */
static DECLCALLBACK(int) drvHstAudCaHA_StreamEnable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PCOREAUDIOSTREAM pStreamCA = (PCOREAUDIOSTREAM)pStream;
    LogFlowFunc(("Stream '%s' {%s}\n", pStreamCA->Cfg.szName, drvHstAudCaStreamStatusString(pStreamCA)));
    AssertReturn(pStreamCA->enmInitState == COREAUDIOINITSTATE_INIT, VERR_AUDIO_STREAM_NOT_READY);
    RTCritSectEnter(&pStreamCA->CritSect);

    Assert(!pStreamCA->fEnabled);
    Assert(!pStreamCA->fStarted);

    /*
     * We always reset the buffer before enabling the stream (normally never necessary).
     */
    OSStatus orc = AudioQueueReset(pStreamCA->hAudioQueue);
    if (orc != noErr)
        LogRelMax(64, ("CoreAudio: Stream reset failed when enabling '%s': %#x (%d)\n", pStreamCA->Cfg.szName, orc, orc));
    Assert(orc == noErr);
    for (uint32_t iBuf = 0; iBuf < pStreamCA->cBuffers; iBuf++)
        Assert(!drvHstAudCaIsBufferQueued(pStreamCA->paBuffers[iBuf].pBuf));

    pStreamCA->offInternal      = 0;
    pStreamCA->fDraining        = false;
    pStreamCA->fEnabled         = true;
    pStreamCA->fRestartOnResume = false;
    pStreamCA->idxBuffer        = 0;

    /*
     * Input streams will start capturing, while output streams will only start
     * playing once we get some audio data to play (see drvHstAudCaHA_StreamPlay).
     */
    int rc = VINF_SUCCESS;
    if (pStreamCA->Cfg.enmDir == PDMAUDIODIR_IN)
    {
        /* Zero (probably not needed) and submit all the buffers first. */
        for (uint32_t iBuf = 0; iBuf < pStreamCA->cBuffers; iBuf++)
        {
            AudioQueueBufferRef pBuf = pStreamCA->paBuffers[iBuf].pBuf;

            RT_BZERO(pBuf->mAudioData, pBuf->mAudioDataBytesCapacity);
            pBuf->mAudioDataByteSize = 0;
            drvHstAudCaSetBufferQueued(pBuf, true /*fQueued*/);

            orc = AudioQueueEnqueueBuffer(pStreamCA->hAudioQueue, pBuf, 0 /*inNumPacketDescs*/, NULL /*inPacketDescs*/);
            AssertLogRelMsgBreakStmt(orc == noErr, ("CoreAudio: AudioQueueEnqueueBuffer(#%u) -> %#x (%d) - stream '%s'\n",
                                                    iBuf, orc, orc, pStreamCA->Cfg.szName),
                                     drvHstAudCaSetBufferQueued(pBuf, false /*fQueued*/));
        }

        /* Start the stream. */
        if (orc == noErr)
        {
            LogFlowFunc(("Start input stream '%s'...\n", pStreamCA->Cfg.szName));
            orc = AudioQueueStart(pStreamCA->hAudioQueue, NULL /*inStartTime*/);
            AssertLogRelMsgStmt(orc == noErr, ("CoreAudio: AudioQueueStart(%s) -> %#x (%d) \n", pStreamCA->Cfg.szName, orc, orc),
                                rc = VERR_AUDIO_STREAM_NOT_READY);
            pStreamCA->fStarted = orc == noErr;
        }
        else
            rc = VERR_AUDIO_STREAM_NOT_READY;
    }
    else
        Assert(pStreamCA->Cfg.enmDir == PDMAUDIODIR_OUT);

    RTCritSectLeave(&pStreamCA->CritSect);
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * @ interface_method_impl{PDMIHOSTAUDIO,pfnStreamDisable}
 */
static DECLCALLBACK(int) drvHstAudCaHA_StreamDisable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PCOREAUDIOSTREAM pStreamCA = (PCOREAUDIOSTREAM)pStream;
    LogFlowFunc(("cMsLastTransfer=%RI64 ms, stream '%s' {%s} \n",
                 pStreamCA->msLastTransfer ? RTTimeMilliTS() - pStreamCA->msLastTransfer : -1,
                 pStreamCA->Cfg.szName, drvHstAudCaStreamStatusString(pStreamCA) ));
    AssertReturn(pStreamCA->enmInitState == COREAUDIOINITSTATE_INIT, VERR_AUDIO_STREAM_NOT_READY);
    RTCritSectEnter(&pStreamCA->CritSect);

    /*
     * Always stop it (draining or no).
     */
    pStreamCA->fEnabled         = false;
    pStreamCA->fRestartOnResume = false;
    Assert(!pStreamCA->fDraining || pStreamCA->Cfg.enmDir == PDMAUDIODIR_OUT);

    int rc = VINF_SUCCESS;
    if (pStreamCA->fStarted)
    {
#if 0
        OSStatus orc2 = AudioQueueReset(pStreamCA->hAudioQueue);
        LogFlowFunc(("AudioQueueReset(%s) returns %#x (%d)\n", pStreamCA->Cfg.szName, orc2, orc2)); RT_NOREF(orc2);
        orc2 = AudioQueueFlush(pStreamCA->hAudioQueue);
        LogFlowFunc(("AudioQueueFlush(%s) returns %#x (%d)\n", pStreamCA->Cfg.szName, orc2, orc2)); RT_NOREF(orc2);
#endif

        OSStatus orc = AudioQueueStop(pStreamCA->hAudioQueue, TRUE /*inImmediate*/);
        LogFlowFunc(("AudioQueueStop(%s,TRUE) returns %#x (%d)\n", pStreamCA->Cfg.szName, orc, orc));
        if (orc != noErr)
        {
            LogRelMax(64, ("CoreAudio: Stopping '%s' failed (disable): %#x (%d)\n", pStreamCA->Cfg.szName, orc, orc));
            rc = VERR_GENERAL_FAILURE;
        }
        pStreamCA->fStarted  = false;
        pStreamCA->fDraining = false;
    }

    RTCritSectLeave(&pStreamCA->CritSect);
    LogFlowFunc(("returns %Rrc {%s}\n", rc, drvHstAudCaStreamStatusString(pStreamCA)));
    return rc;
}


/**
 * @ interface_method_impl{PDMIHOSTAUDIO,pfnStreamPause}
 */
static DECLCALLBACK(int) drvHstAudCaHA_StreamPause(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PCOREAUDIOSTREAM pStreamCA = (PCOREAUDIOSTREAM)pStream;
    LogFlowFunc(("cMsLastTransfer=%RI64 ms, stream '%s' {%s} \n",
                 pStreamCA->msLastTransfer ? RTTimeMilliTS() - pStreamCA->msLastTransfer : -1,
                 pStreamCA->Cfg.szName, drvHstAudCaStreamStatusString(pStreamCA) ));
    AssertReturn(pStreamCA->enmInitState == COREAUDIOINITSTATE_INIT, VERR_AUDIO_STREAM_NOT_READY);
    RTCritSectEnter(&pStreamCA->CritSect);

    /*
     * Unless we're draining the stream, pause it if it has started.
     */
    int rc = VINF_SUCCESS;
    if (pStreamCA->fStarted && !pStreamCA->fDraining)
    {
        pStreamCA->fRestartOnResume = true;

        OSStatus orc = AudioQueuePause(pStreamCA->hAudioQueue);
        LogFlowFunc(("AudioQueuePause(%s) returns %#x (%d)\n", pStreamCA->Cfg.szName, orc, orc));
        if (orc != noErr)
        {
            LogRelMax(64, ("CoreAudio: Pausing '%s' failed: %#x (%d)\n", pStreamCA->Cfg.szName, orc, orc));
            rc = VERR_GENERAL_FAILURE;
        }
        pStreamCA->fStarted = false;
    }
    else
    {
        pStreamCA->fRestartOnResume = false;
        if (pStreamCA->fDraining)
        {
            LogFunc(("Stream '%s' is draining\n", pStreamCA->Cfg.szName));
            Assert(pStreamCA->fStarted);
        }
    }

    RTCritSectLeave(&pStreamCA->CritSect);
    LogFlowFunc(("returns %Rrc {%s}\n", rc, drvHstAudCaStreamStatusString(pStreamCA)));
    return rc;
}


/**
 * @ interface_method_impl{PDMIHOSTAUDIO,pfnStreamResume}
 */
static DECLCALLBACK(int) drvHstAudCaHA_StreamResume(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PCOREAUDIOSTREAM pStreamCA = (PCOREAUDIOSTREAM)pStream;
    LogFlowFunc(("Stream '%s' {%s}\n", pStreamCA->Cfg.szName, drvHstAudCaStreamStatusString(pStreamCA)));
    AssertReturn(pStreamCA->enmInitState == COREAUDIOINITSTATE_INIT, VERR_AUDIO_STREAM_NOT_READY);
    RTCritSectEnter(&pStreamCA->CritSect);

    /*
     * Resume according to state saved by drvHstAudCaHA_StreamPause.
     */
    int rc = VINF_SUCCESS;
    if (pStreamCA->fRestartOnResume)
    {
        OSStatus orc = AudioQueueStart(pStreamCA->hAudioQueue, NULL /*inStartTime*/);
        LogFlowFunc(("AudioQueueStart(%s, NULL) returns %#x (%d)\n", pStreamCA->Cfg.szName, orc, orc));
        if (orc != noErr)
        {
            LogRelMax(64, ("CoreAudio: Pausing '%s' failed: %#x (%d)\n", pStreamCA->Cfg.szName, orc, orc));
            rc = VERR_AUDIO_STREAM_NOT_READY;
        }
    }
    pStreamCA->fRestartOnResume = false;

    RTCritSectLeave(&pStreamCA->CritSect);
    LogFlowFunc(("returns %Rrc {%s}\n", rc, drvHstAudCaStreamStatusString(pStreamCA)));
    return rc;
}


/**
 * @ interface_method_impl{PDMIHOSTAUDIO,pfnStreamDrain}
 */
static DECLCALLBACK(int) drvHstAudCaHA_StreamDrain(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PCOREAUDIOSTREAM pStreamCA = (PCOREAUDIOSTREAM)pStream;
    AssertReturn(pStreamCA->Cfg.enmDir == PDMAUDIODIR_OUT, VERR_INVALID_PARAMETER);
    LogFlowFunc(("cMsLastTransfer=%RI64 ms, stream '%s' {%s} \n",
                 pStreamCA->msLastTransfer ? RTTimeMilliTS() - pStreamCA->msLastTransfer : -1,
                 pStreamCA->Cfg.szName, drvHstAudCaStreamStatusString(pStreamCA) ));
    AssertReturn(pStreamCA->enmInitState == COREAUDIOINITSTATE_INIT, VERR_AUDIO_STREAM_NOT_READY);
    RTCritSectEnter(&pStreamCA->CritSect);

    /*
     * The AudioQueueStop function has both an immediate and a drain mode,
     * so we'll obviously use the latter here.  For checking draining progress,
     * we will just check if all buffers have been returned or not.
     */
    int rc = VINF_SUCCESS;
    if (pStreamCA->fStarted)
    {
        if (!pStreamCA->fDraining)
        {
            OSStatus orc = AudioQueueStop(pStreamCA->hAudioQueue, FALSE /*inImmediate*/);
            LogFlowFunc(("AudioQueueStop(%s, FALSE) returns %#x (%d)\n", pStreamCA->Cfg.szName, orc, orc));
            if (orc == noErr)
                pStreamCA->fDraining = true;
            else
            {
                LogRelMax(64, ("CoreAudio: Stopping '%s' failed (drain): %#x (%d)\n", pStreamCA->Cfg.szName, orc, orc));
                rc = VERR_GENERAL_FAILURE;
            }
        }
        else
            LogFlowFunc(("Already draining '%s' ...\n", pStreamCA->Cfg.szName));
    }
    else
    {
        LogFlowFunc(("Drain requested for '%s', but not started playback...\n", pStreamCA->Cfg.szName));
        AssertStmt(!pStreamCA->fDraining, pStreamCA->fDraining = false);
    }

    RTCritSectLeave(&pStreamCA->CritSect);
    LogFlowFunc(("returns %Rrc {%s}\n", rc, drvHstAudCaStreamStatusString(pStreamCA)));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetReadable}
 */
static DECLCALLBACK(uint32_t) drvHstAudCaHA_StreamGetReadable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PCOREAUDIOSTREAM pStreamCA = (PCOREAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamCA, VERR_INVALID_POINTER);
    AssertReturn(pStreamCA->enmInitState == COREAUDIOINITSTATE_INIT, 0);

    uint32_t cbReadable = 0;
    if (pStreamCA->Cfg.enmDir == PDMAUDIODIR_IN)
    {
        RTCritSectEnter(&pStreamCA->CritSect);
        PCOREAUDIOBUF const paBuffers = pStreamCA->paBuffers;
        uint32_t const      cBuffers  = pStreamCA->cBuffers;
        uint32_t const      idxStart  = pStreamCA->idxBuffer;
        uint32_t            idxBuffer = idxStart;
        AudioQueueBufferRef pBuf;

        if (   cBuffers > 0
            && !drvHstAudCaIsBufferQueued(pBuf = paBuffers[idxBuffer].pBuf))
        {
            do
            {
                uint32_t const  cbTotal = pBuf->mAudioDataBytesCapacity;
                uint32_t        cbFill  = pBuf->mAudioDataByteSize;
                AssertStmt(cbFill <= cbTotal, cbFill = cbTotal);
                uint32_t        off     = paBuffers[idxBuffer].offRead;
                AssertStmt(off < cbFill, off = cbFill);

                cbReadable += cbFill - off;

                /* Advance. */
                idxBuffer++;
                if (idxBuffer < cBuffers)
                { /* likely */ }
                else
                    idxBuffer = 0;
            } while (idxBuffer != idxStart && !drvHstAudCaIsBufferQueued(pBuf = paBuffers[idxBuffer].pBuf));
        }

        RTCritSectLeave(&pStreamCA->CritSect);
    }
    Log2Func(("returns %#x for '%s'\n", cbReadable, pStreamCA->Cfg.szName));
    return cbReadable;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetWritable}
 */
static DECLCALLBACK(uint32_t) drvHstAudCaHA_StreamGetWritable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PCOREAUDIOSTREAM pStreamCA = (PCOREAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamCA, VERR_INVALID_POINTER);
    AssertReturn(pStreamCA->enmInitState == COREAUDIOINITSTATE_INIT, 0);

    uint32_t cbWritable = 0;
    if (pStreamCA->Cfg.enmDir == PDMAUDIODIR_OUT)
    {
        RTCritSectEnter(&pStreamCA->CritSect);
        PCOREAUDIOBUF const paBuffers = pStreamCA->paBuffers;
        uint32_t const      cBuffers  = pStreamCA->cBuffers;
        uint32_t const      idxStart  = pStreamCA->idxBuffer;
        uint32_t            idxBuffer = idxStart;
        AudioQueueBufferRef pBuf;

        if (   cBuffers > 0
            && !drvHstAudCaIsBufferQueued(pBuf = paBuffers[idxBuffer].pBuf))
        {
            do
            {
                uint32_t const  cbTotal = pBuf->mAudioDataBytesCapacity;
                uint32_t        cbUsed  = pBuf->mAudioDataByteSize;
                AssertStmt(cbUsed <= cbTotal, paBuffers[idxBuffer].pBuf->mAudioDataByteSize = cbUsed = cbTotal);

                cbWritable += cbTotal - cbUsed;

                /* Advance. */
                idxBuffer++;
                if (idxBuffer < cBuffers)
                { /* likely */ }
                else
                    idxBuffer = 0;
            } while (idxBuffer != idxStart && !drvHstAudCaIsBufferQueued(pBuf = paBuffers[idxBuffer].pBuf));
        }

        RTCritSectLeave(&pStreamCA->CritSect);
    }
    Log2Func(("returns %#x for '%s'\n", cbWritable, pStreamCA->Cfg.szName));
    return cbWritable;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetState}
 */
static DECLCALLBACK(PDMHOSTAUDIOSTREAMSTATE) drvHstAudCaHA_StreamGetState(PPDMIHOSTAUDIO pInterface,
                                                                          PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PCOREAUDIOSTREAM pStreamCA = (PCOREAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamCA, PDMHOSTAUDIOSTREAMSTATE_INVALID);

    if (ASMAtomicReadU32(&pStreamCA->enmInitState) == COREAUDIOINITSTATE_INIT)
    {
        if (!pStreamCA->fDraining)
        { /* likely */ }
        else
        {
            /*
             * If we're draining, we're done when we've got all the buffers back.
             */
            RTCritSectEnter(&pStreamCA->CritSect);
            PCOREAUDIOBUF const paBuffers = pStreamCA->paBuffers;
            uintptr_t           idxBuffer = pStreamCA->cBuffers;
            while (idxBuffer-- > 0)
                if (!drvHstAudCaIsBufferQueued(paBuffers[idxBuffer].pBuf))
                { /* likely */ }
                else
                {
#ifdef LOG_ENABLED
                    uint32_t cQueued = 1;
                    while (idxBuffer-- > 0)
                        cQueued += drvHstAudCaIsBufferQueued(paBuffers[idxBuffer].pBuf);
                    LogFunc(("Still done draining '%s': %u queued buffers\n", pStreamCA->Cfg.szName, cQueued));
#endif
                    RTCritSectLeave(&pStreamCA->CritSect);
                    return PDMHOSTAUDIOSTREAMSTATE_DRAINING;
                }

            LogFunc(("Done draining '%s'\n", pStreamCA->Cfg.szName));
            pStreamCA->fDraining = false;
            pStreamCA->fEnabled  = false;
            pStreamCA->fStarted  = false;
            RTCritSectLeave(&pStreamCA->CritSect);
        }

        return PDMHOSTAUDIOSTREAMSTATE_OKAY;
    }
    return PDMHOSTAUDIOSTREAMSTATE_NOT_WORKING; /** @todo ?? */
}

/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPlay}
 */
static DECLCALLBACK(int) drvHstAudCaHA_StreamPlay(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                  const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    RT_NOREF(pInterface);
    PCOREAUDIOSTREAM pStreamCA = (PCOREAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamCA, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbWritten, VERR_INVALID_POINTER);
    if (cbBuf)
        AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    Assert(PDMAudioPropsIsSizeAligned(&pStreamCA->Cfg.Props, cbBuf));
    AssertReturnStmt(pStreamCA->enmInitState == COREAUDIOINITSTATE_INIT, *pcbWritten = 0, VERR_AUDIO_STREAM_NOT_READY);

    RTCritSectEnter(&pStreamCA->CritSect);
    if (pStreamCA->fEnabled)
    { /* likely */ }
    else
    {
        RTCritSectLeave(&pStreamCA->CritSect);
        *pcbWritten = 0;
        LogFunc(("Skipping %#x byte write to disabled stream {%s}\n", cbBuf, drvHstAudCaStreamStatusString(pStreamCA) ));
        return VINF_SUCCESS;
    }
    Log4Func(("cbBuf=%#x stream '%s' {%s}\n", cbBuf, pStreamCA->Cfg.szName, drvHstAudCaStreamStatusString(pStreamCA) ));

    /*
     * Transfer loop.
     */
    PCOREAUDIOBUF const paBuffers = pStreamCA->paBuffers;
    uint32_t const      cBuffers  = pStreamCA->cBuffers;
    AssertMsgReturnStmt(cBuffers >= COREAUDIO_MIN_BUFFERS && cBuffers < COREAUDIO_MAX_BUFFERS, ("%u\n", cBuffers),
                        RTCritSectLeave(&pStreamCA->CritSect), VERR_AUDIO_STREAM_NOT_READY);

    uint32_t            idxBuffer = pStreamCA->idxBuffer;
    AssertStmt(idxBuffer < cBuffers, idxBuffer %= cBuffers);

    int                 rc        = VINF_SUCCESS;
    uint32_t            cbWritten = 0;
    while (cbBuf > 0)
    {
        AssertBreakStmt(pStreamCA->hAudioQueue, rc = VERR_AUDIO_STREAM_NOT_READY);

        /*
         * Check out how much we can put into the current buffer.
         */
        AudioQueueBufferRef const pBuf = paBuffers[idxBuffer].pBuf;
        if (!drvHstAudCaIsBufferQueued(pBuf))
        { /* likely */ }
        else
        {
            LogFunc(("@%#RX64: Warning! Out of buffer space! (%#x bytes unwritten)\n", pStreamCA->offInternal, cbBuf));
            /** @todo stats   */
            break;
        }

        AssertPtrBreakStmt(pBuf, rc = VERR_INTERNAL_ERROR_2);
        uint32_t const  cbTotal = pBuf->mAudioDataBytesCapacity;
        uint32_t        cbUsed  = pBuf->mAudioDataByteSize;
        AssertStmt(cbUsed < cbTotal, cbUsed = cbTotal);
        uint32_t const  cbAvail = cbTotal - cbUsed;

        /*
         * Copy over the data.
         */
        if (cbBuf < cbAvail)
        {
            Log3Func(("@%#RX64: buffer #%u/%u: %#x bytes, have %#x only - leaving unqueued {%s}\n",
                      pStreamCA->offInternal, idxBuffer, cBuffers, cbAvail, cbBuf, drvHstAudCaStreamStatusString(pStreamCA) ));
            memcpy((uint8_t *)pBuf->mAudioData + cbUsed, pvBuf, cbBuf);
            pBuf->mAudioDataByteSize = cbUsed + cbBuf;
            cbWritten               += cbBuf;
            pStreamCA->offInternal  += cbBuf;
            /** @todo Maybe queue it anyway if it's almost full or we haven't got a lot of
             *        buffers queued. */
            break;
        }

        Log3Func(("@%#RX64: buffer #%u/%u: %#x bytes, have %#x - will queue {%s}\n",
                  pStreamCA->offInternal, idxBuffer, cBuffers, cbAvail, cbBuf, drvHstAudCaStreamStatusString(pStreamCA) ));
        memcpy((uint8_t *)pBuf->mAudioData + cbUsed, pvBuf, cbAvail);
        pBuf->mAudioDataByteSize = cbTotal;
        cbWritten               += cbAvail;
        pStreamCA->offInternal  += cbAvail;
        drvHstAudCaSetBufferQueued(pBuf, true /*fQueued*/);

        OSStatus orc = AudioQueueEnqueueBuffer(pStreamCA->hAudioQueue, pBuf, 0 /*inNumPacketDesc*/, NULL /*inPacketDescs*/);
        if (orc == noErr)
        { /* likely */ }
        else
        {
            LogRelMax(256, ("CoreAudio: AudioQueueEnqueueBuffer('%s', #%u) failed: %#x (%d)\n",
                            pStreamCA->Cfg.szName, idxBuffer, orc, orc));
            drvHstAudCaSetBufferQueued(pBuf, false /*fQueued*/);
            pBuf->mAudioDataByteSize -= PDMAudioPropsFramesToBytes(&pStreamCA->Cfg.Props, 1); /* avoid assertions above */
            rc = VERR_AUDIO_STREAM_NOT_READY;
            break;
        }

        /*
         * Advance.
         */
        idxBuffer += 1;
        if (idxBuffer < cBuffers)
        { /* likely */ }
        else
            idxBuffer = 0;
        pStreamCA->idxBuffer = idxBuffer;

        pvBuf  = (const uint8_t *)pvBuf + cbAvail;
        cbBuf -= cbAvail;
    }

    /*
     * Start the stream if we haven't do so yet.
     */
    if (   pStreamCA->fStarted
        || cbWritten == 0
        || RT_FAILURE_NP(rc))
    { /* likely */ }
    else
    {
        UInt32   cFramesPrepared = 0;
#if 0 /* taking too long? */
        OSStatus orc = AudioQueuePrime(pStreamCA->hAudioQueue, 0 /*inNumberOfFramesToPrepare*/, &cFramesPrepared);
        LogFlowFunc(("AudioQueuePrime(%s, 0,) returns %#x (%d) and cFramesPrepared=%u (offInternal=%#RX64)\n",
                     pStreamCA->Cfg.szName, orc, orc, cFramesPrepared, pStreamCA->offInternal));
        AssertMsg(orc == noErr, ("%#x (%d)\n", orc, orc));
#else
        OSStatus orc;
#endif
        orc = AudioQueueStart(pStreamCA->hAudioQueue, NULL /*inStartTime*/);
        LogFunc(("AudioQueueStart(%s, NULL) returns %#x (%d)\n", pStreamCA->Cfg.szName, orc, orc));
        if (orc == noErr)
            pStreamCA->fStarted = true;
        else
        {
            LogRelMax(128, ("CoreAudio: Starting '%s' failed: %#x (%d) - %u frames primed, %#x bytes queued\n",
                            pStreamCA->Cfg.szName, orc, orc, cFramesPrepared, pStreamCA->offInternal));
            rc = VERR_AUDIO_STREAM_NOT_READY;
        }
    }

    /*
     * Done.
     */
#ifdef LOG_ENABLED
    uint64_t const msPrev = pStreamCA->msLastTransfer;
#endif
    uint64_t const msNow  = RTTimeMilliTS();
    if (cbWritten)
        pStreamCA->msLastTransfer = msNow;

    RTCritSectLeave(&pStreamCA->CritSect);

    *pcbWritten = cbWritten;
    if (RT_SUCCESS(rc) || !cbWritten)
    { }
    else
    {
        LogFlowFunc(("Suppressing %Rrc to report %#x bytes written\n", rc, cbWritten));
        rc = VINF_SUCCESS;
    }
    LogFlowFunc(("@%#RX64: rc=%Rrc cbWritten=%RU32 cMsDelta=%RU64 (%RU64 -> %RU64) {%s}\n", pStreamCA->offInternal, rc, cbWritten,
                 msPrev ? msNow - msPrev : 0, msPrev, pStreamCA->msLastTransfer, drvHstAudCaStreamStatusString(pStreamCA) ));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCapture}
 */
static DECLCALLBACK(int) drvHstAudCaHA_StreamCapture(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                     void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead)
{
    RT_NOREF(pInterface);
    PCOREAUDIOSTREAM pStreamCA = (PCOREAUDIOSTREAM)pStream;
    AssertPtrReturn(pStreamCA, 0);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbRead, VERR_INVALID_POINTER);
    Assert(PDMAudioPropsIsSizeAligned(&pStreamCA->Cfg.Props, cbBuf));
    AssertReturnStmt(pStreamCA->enmInitState == COREAUDIOINITSTATE_INIT, *pcbRead = 0, VERR_AUDIO_STREAM_NOT_READY);

    RTCritSectEnter(&pStreamCA->CritSect);
    if (pStreamCA->fEnabled)
    { /* likely */ }
    else
    {
        RTCritSectLeave(&pStreamCA->CritSect);
        *pcbRead = 0;
        LogFunc(("Skipping %#x byte read from disabled stream {%s}\n", cbBuf, drvHstAudCaStreamStatusString(pStreamCA)));
        return VINF_SUCCESS;
    }
    Log4Func(("cbBuf=%#x stream '%s' {%s}\n", cbBuf, pStreamCA->Cfg.szName, drvHstAudCaStreamStatusString(pStreamCA) ));


    /*
     * Transfer loop.
     */
    uint32_t const      cbFrame   = PDMAudioPropsFrameSize(&pStreamCA->Cfg.Props);
    PCOREAUDIOBUF const paBuffers = pStreamCA->paBuffers;
    uint32_t const      cBuffers  = pStreamCA->cBuffers;
    AssertMsgReturnStmt(cBuffers >= COREAUDIO_MIN_BUFFERS && cBuffers < COREAUDIO_MAX_BUFFERS, ("%u\n", cBuffers),
                        RTCritSectLeave(&pStreamCA->CritSect), VERR_AUDIO_STREAM_NOT_READY);

    uint32_t            idxBuffer = pStreamCA->idxBuffer;
    AssertStmt(idxBuffer < cBuffers, idxBuffer %= cBuffers);

    int                 rc        = VINF_SUCCESS;
    uint32_t            cbRead    = 0;
    while (cbBuf > cbFrame)
    {
        AssertBreakStmt(pStreamCA->hAudioQueue, rc = VERR_AUDIO_STREAM_NOT_READY);

        /*
         * Check out how much we can read from the current buffer (if anything at all).
         */
        AudioQueueBufferRef const pBuf = paBuffers[idxBuffer].pBuf;
        if (!drvHstAudCaIsBufferQueued(pBuf))
        { /* likely */ }
        else
        {
            LogFunc(("@%#RX64: Warning! Underrun! (%#x bytes unread)\n", pStreamCA->offInternal, cbBuf));
            /** @todo stats   */
            break;
        }

        AssertPtrBreakStmt(pBuf, rc = VERR_INTERNAL_ERROR_2);
        uint32_t const  cbTotal = pBuf->mAudioDataBytesCapacity;
        uint32_t        cbValid = pBuf->mAudioDataByteSize;
        AssertStmt(cbValid < cbTotal, cbValid = cbTotal);
        uint32_t        offRead = paBuffers[idxBuffer].offRead;
        uint32_t const  cbLeft  = cbValid - offRead;

        /*
         * Copy over the data.
         */
        if (cbBuf < cbLeft)
        {
            Log3Func(("@%#RX64: buffer #%u/%u: %#x bytes, want %#x - leaving unqueued {%s}\n",
                      pStreamCA->offInternal, idxBuffer, cBuffers, cbLeft, cbBuf, drvHstAudCaStreamStatusString(pStreamCA) ));
            memcpy(pvBuf, (uint8_t const *)pBuf->mAudioData + offRead, cbBuf);
            paBuffers[idxBuffer].offRead = offRead + cbBuf;
            cbRead                      += cbBuf;
            pStreamCA->offInternal      += cbBuf;
            break;
        }

        Log3Func(("@%#RX64: buffer #%u/%u: %#x bytes, want all (%#x) - will queue {%s}\n",
                  pStreamCA->offInternal, idxBuffer, cBuffers, cbLeft, cbBuf, drvHstAudCaStreamStatusString(pStreamCA) ));
        memcpy(pvBuf, (uint8_t const *)pBuf->mAudioData + offRead, cbLeft);
        cbRead                  += cbLeft;
        pStreamCA->offInternal  += cbLeft;

        RT_BZERO(pBuf->mAudioData, cbTotal); /* paranoia */
        paBuffers[idxBuffer].offRead = 0;
        pBuf->mAudioDataByteSize     = 0;
        drvHstAudCaSetBufferQueued(pBuf, true /*fQueued*/);

        OSStatus orc = AudioQueueEnqueueBuffer(pStreamCA->hAudioQueue, pBuf, 0 /*inNumPacketDesc*/, NULL /*inPacketDescs*/);
        if (orc == noErr)
        { /* likely */ }
        else
        {
            LogRelMax(256, ("CoreAudio: AudioQueueEnqueueBuffer('%s', #%u) failed: %#x (%d)\n",
                            pStreamCA->Cfg.szName, idxBuffer, orc, orc));
            drvHstAudCaSetBufferQueued(pBuf, false /*fQueued*/);
            rc = VERR_AUDIO_STREAM_NOT_READY;
            break;
        }

        /*
         * Advance.
         */
        idxBuffer += 1;
        if (idxBuffer < cBuffers)
        { /* likely */ }
        else
            idxBuffer = 0;
        pStreamCA->idxBuffer = idxBuffer;

        pvBuf  = (uint8_t *)pvBuf + cbLeft;
        cbBuf -= cbLeft;
    }

    /*
     * Done.
     */
#ifdef LOG_ENABLED
    uint64_t const msPrev = pStreamCA->msLastTransfer;
#endif
    uint64_t const msNow  = RTTimeMilliTS();
    if (cbRead)
        pStreamCA->msLastTransfer = msNow;

    RTCritSectLeave(&pStreamCA->CritSect);

    *pcbRead = cbRead;
    if (RT_SUCCESS(rc) || !cbRead)
    { }
    else
    {
        LogFlowFunc(("Suppressing %Rrc to report %#x bytes read\n", rc, cbRead));
        rc = VINF_SUCCESS;
    }
    LogFlowFunc(("@%#RX64: rc=%Rrc cbRead=%RU32 cMsDelta=%RU64 (%RU64 -> %RU64) {%s}\n", pStreamCA->offInternal, rc, cbRead,
                 msPrev ? msNow - msPrev : 0, msPrev, pStreamCA->msLastTransfer, drvHstAudCaStreamStatusString(pStreamCA) ));
    return rc;
}


/*********************************************************************************************************************************
*   PDMIBASE                                                                                                                     *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvHstAudCaQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS        pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVHOSTCOREAUDIO pThis   = PDMINS_2_DATA(pDrvIns, PDRVHOSTCOREAUDIO);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE,      &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHOSTAUDIO, &pThis->IHostAudio);

    return NULL;
}


/*********************************************************************************************************************************
*   PDMDRVREG                                                                                                                    *
*********************************************************************************************************************************/

/**
 * Worker for the power off and destructor callbacks.
 */
static void drvHstAudCaRemoveDefaultDeviceListners(PDRVHOSTCOREAUDIO pThis)
{
    /*
     * Unregister system callbacks.
     */
    AudioObjectPropertyAddress PropAddr =
    {
        kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMaster
    };

    OSStatus orc;
    if (pThis->fRegisteredDefaultInputListener)
    {
        orc = AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &PropAddr,
                                                drvHstAudCaDefaultDeviceChangedCallback, pThis);
        if (   orc != noErr
            && orc != kAudioHardwareBadObjectError)
            LogRel(("CoreAudio: Failed to remove the default input device changed listener: %d (%#x))\n", orc, orc));
        pThis->fRegisteredDefaultInputListener = false;
    }

    if (pThis->fRegisteredDefaultOutputListener)
    {
        PropAddr.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
        orc = AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &PropAddr,
                                                drvHstAudCaDefaultDeviceChangedCallback, pThis);
        if (   orc != noErr
            && orc != kAudioHardwareBadObjectError)
            LogRel(("CoreAudio: Failed to remove the default output device changed listener: %d (%#x))\n", orc, orc));
        pThis->fRegisteredDefaultOutputListener = false;
    }

    /*
     * Unregister device callbacks.
     */
    RTCritSectEnter(&pThis->CritSect);

    drvHstAudCaDeviceUnregisterCallbacks(pThis, pThis->InputDevice.idDevice);
    pThis->InputDevice.idDevice  = kAudioDeviceUnknown;

    drvHstAudCaDeviceUnregisterCallbacks(pThis, pThis->OutputDevice.idDevice);
    pThis->OutputDevice.idDevice = kAudioDeviceUnknown;

    RTCritSectLeave(&pThis->CritSect);

    LogFlowFuncEnter();
}


/**
 * @interface_method_impl{PDMDRVREG,pfnPowerOff}
 */
static DECLCALLBACK(void) drvHstAudCaPowerOff(PPDMDRVINS pDrvIns)
{
    PDRVHOSTCOREAUDIO pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTCOREAUDIO);
    drvHstAudCaRemoveDefaultDeviceListners(pThis);
}


/**
 * @callback_method_impl{FNPDMDRVDESTRUCT}
 */
static DECLCALLBACK(void) drvHstAudCaDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVHOSTCOREAUDIO pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTCOREAUDIO);

    if (RTCritSectIsInitialized(&pThis->CritSect))
        drvHstAudCaRemoveDefaultDeviceListners(pThis);

#ifdef CORE_AUDIO_WITH_WORKER_THREAD
    if (pThis->hThread != NIL_RTTHREAD)
    {
        for (unsigned iLoop = 0; iLoop < 60; iLoop++)
        {
            if (pThis->hThreadRunLoop)
                CFRunLoopStop(pThis->hThreadRunLoop);
            if (iLoop > 10)
                RTThreadPoke(pThis->hThread);
            int rc = RTThreadWait(pThis->hThread, 500 /*ms*/, NULL /*prcThread*/);
            if (RT_SUCCESS(rc))
                break;
            AssertMsgBreak(rc == VERR_TIMEOUT, ("RTThreadWait -> %Rrc\n",rc));
        }
        pThis->hThread = NIL_RTTHREAD;
    }
    if (pThis->hThreadPortSrc)
    {
        CFRelease(pThis->hThreadPortSrc);
        pThis->hThreadPortSrc = NULL;
    }
    if (pThis->hThreadPort)
    {
        CFMachPortInvalidate(pThis->hThreadPort);
        CFRelease(pThis->hThreadPort);
        pThis->hThreadPort = NULL;
    }
    if (pThis->hThreadRunLoop)
    {
        CFRelease(pThis->hThreadRunLoop);
        pThis->hThreadRunLoop = NULL;
    }
#endif

#ifdef CORE_AUDIO_WITH_BREAKPOINT_TIMER
    if (pThis->hBreakpointTimer != NIL_RTTIMERLR)
    {
        RTTimerLRDestroy(pThis->hBreakpointTimer);
        pThis->hBreakpointTimer = NIL_RTTIMERLR;
    }
#endif

    if (RTCritSectIsInitialized(&pThis->CritSect))
    {
        int rc2 = RTCritSectDelete(&pThis->CritSect);
        AssertRC(rc2);
    }

    LogFlowFuncLeave();
}


/**
 * @callback_method_impl{FNPDMDRVCONSTRUCT,
 *      Construct a Core Audio driver instance.}
 */
static DECLCALLBACK(int) drvHstAudCaConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(pCfg, fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVHOSTCOREAUDIO   pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTCOREAUDIO);
    PCPDMDRVHLPR3       pHlp  = pDrvIns->pHlpR3;
    LogRel(("Audio: Initializing Core Audio driver\n"));

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                   = pDrvIns;
#ifdef CORE_AUDIO_WITH_WORKER_THREAD
    pThis->hThread                   = NIL_RTTHREAD;
#endif
#ifdef CORE_AUDIO_WITH_BREAKPOINT_TIMER
    pThis->hBreakpointTimer          = NIL_RTTIMERLR;
#endif
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface = drvHstAudCaQueryInterface;
    /* IHostAudio */
    pThis->IHostAudio.pfnGetConfig                  = drvHstAudCaHA_GetConfig;
    pThis->IHostAudio.pfnGetDevices                 = drvHstAudCaHA_GetDevices;
    pThis->IHostAudio.pfnSetDevice                  = drvHstAudCaHA_SetDevice;
    pThis->IHostAudio.pfnGetStatus                  = drvHstAudCaHA_GetStatus;
    pThis->IHostAudio.pfnDoOnWorkerThread           = NULL;
    pThis->IHostAudio.pfnStreamConfigHint           = NULL;
    pThis->IHostAudio.pfnStreamCreate               = drvHstAudCaHA_StreamCreate;
    pThis->IHostAudio.pfnStreamInitAsync            = NULL;
    pThis->IHostAudio.pfnStreamDestroy              = drvHstAudCaHA_StreamDestroy;
    pThis->IHostAudio.pfnStreamNotifyDeviceChanged  = NULL;
    pThis->IHostAudio.pfnStreamEnable               = drvHstAudCaHA_StreamEnable;
    pThis->IHostAudio.pfnStreamDisable              = drvHstAudCaHA_StreamDisable;
    pThis->IHostAudio.pfnStreamPause                = drvHstAudCaHA_StreamPause;
    pThis->IHostAudio.pfnStreamResume               = drvHstAudCaHA_StreamResume;
    pThis->IHostAudio.pfnStreamDrain                = drvHstAudCaHA_StreamDrain;
    pThis->IHostAudio.pfnStreamGetReadable          = drvHstAudCaHA_StreamGetReadable;
    pThis->IHostAudio.pfnStreamGetWritable          = drvHstAudCaHA_StreamGetWritable;
    pThis->IHostAudio.pfnStreamGetPending           = NULL;
    pThis->IHostAudio.pfnStreamGetState             = drvHstAudCaHA_StreamGetState;
    pThis->IHostAudio.pfnStreamPlay                 = drvHstAudCaHA_StreamPlay;
    pThis->IHostAudio.pfnStreamCapture              = drvHstAudCaHA_StreamCapture;

    int rc = RTCritSectInit(&pThis->CritSect);
    AssertRCReturn(rc, rc);

    /*
     * Validate and read configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "InputDeviceID|OutputDeviceID", "");

    char *pszTmp = NULL;
    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "InputDeviceID", &pszTmp);
    if (RT_SUCCESS(rc))
    {
        rc = drvHstAudCaSetDevice(pThis, &pThis->InputDevice, true /*fInput*/, false /*fNotify*/, pszTmp);
        PDMDrvHlpMMHeapFree(pDrvIns, pszTmp);
    }
    else if (rc != VERR_CFGM_VALUE_NOT_FOUND && rc != VERR_CFGM_NO_PARENT)
        return PDMDRV_SET_ERROR(pDrvIns, rc, "Failed to query 'InputDeviceID'");

    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "OutputDeviceID", &pszTmp);
    if (RT_SUCCESS(rc))
    {
        rc = drvHstAudCaSetDevice(pThis, &pThis->OutputDevice, false /*fInput*/, false /*fNotify*/, pszTmp);
        PDMDrvHlpMMHeapFree(pDrvIns, pszTmp);
    }
    else if (rc != VERR_CFGM_VALUE_NOT_FOUND && rc != VERR_CFGM_NO_PARENT)
        return PDMDRV_SET_ERROR(pDrvIns, rc, "Failed to query 'OutputDeviceID'");

    /*
     * Query the notification interface from the driver/device above us.
     */
    pThis->pIHostAudioPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIHOSTAUDIOPORT);
    AssertReturn(pThis->pIHostAudioPort, VERR_PDM_MISSING_INTERFACE_ABOVE);

#ifdef CORE_AUDIO_WITH_WORKER_THREAD
    /*
     * Create worker thread for running callbacks on.
     */
    CFMachPortContext PortCtx;
    PortCtx.version         = 0;
    PortCtx.info            = pThis;
    PortCtx.retain          = NULL;
    PortCtx.release         = NULL;
    PortCtx.copyDescription = NULL;
    pThis->hThreadPort = CFMachPortCreate(NULL /*allocator*/, drvHstAudCaThreadPortCallback, &PortCtx, NULL);
    AssertLogRelReturn(pThis->hThreadPort != NULL, VERR_NO_MEMORY);

    pThis->hThreadPortSrc = CFMachPortCreateRunLoopSource(NULL, pThis->hThreadPort, 0 /*order*/);
    AssertLogRelReturn(pThis->hThreadPortSrc != NULL, VERR_NO_MEMORY);

    rc = RTThreadCreateF(&pThis->hThread, drvHstAudCaThread, pThis, 0, RTTHREADTYPE_IO,
                         RTTHREADFLAGS_WAITABLE, "CaAud-%u", pDrvIns->iInstance);
    AssertLogRelMsgReturn(RT_SUCCESS(rc), ("RTThreadCreateF failed: %Rrc\n", rc), rc);

    RTThreadUserWait(pThis->hThread, RT_MS_10SEC);
    AssertLogRel(pThis->hThreadRunLoop);
#endif

#ifdef CORE_AUDIO_WITH_BREAKPOINT_TIMER
    /*
     * Create a IPRT timer.  The TM timers won't necessarily work as EMT is probably busy.
     */
    rc = RTTimerLRCreateEx(&pThis->hBreakpointTimer, 0 /*no interval*/, 0, drvHstAudCaBreakpointTimer, pThis);
    AssertRCReturn(rc, rc);
#endif

    /*
     * Determin the default devices.
     */
    drvHstAudCaUpdateOneDefaultDevice(pThis, &pThis->OutputDevice, false /*fInput*/, false /*fNotifty*/);
    drvHstAudCaUpdateOneDefaultDevice(pThis, &pThis->InputDevice,   true /*fInput*/, false /*fNotifty*/);

    /*
     * Register callbacks for default device input and output changes.
     * (We just ignore failures here as there isn't much we can do about it,
     * and it isn't 100% critical.)
     */
    AudioObjectPropertyAddress PropAddr =
    {
        /* .mSelector = */  kAudioHardwarePropertyDefaultInputDevice,
        /* .mScope = */     kAudioObjectPropertyScopeGlobal,
        /* .mElement = */   kAudioObjectPropertyElementMaster
    };

    OSStatus orc;
    orc = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &PropAddr, drvHstAudCaDefaultDeviceChangedCallback, pThis);
    pThis->fRegisteredDefaultInputListener = orc == noErr;
    if (   orc != noErr
        && orc != kAudioHardwareIllegalOperationError)
        LogRel(("CoreAudio: Failed to add the input default device changed listener: %d (%#x)\n", orc, orc));

    PropAddr.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    orc = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &PropAddr, drvHstAudCaDefaultDeviceChangedCallback, pThis);
    pThis->fRegisteredDefaultOutputListener = orc == noErr;
    if (   orc != noErr
        && orc != kAudioHardwareIllegalOperationError)
        LogRel(("CoreAudio: Failed to add the output default device changed listener: %d (%#x)\n", orc, orc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Char driver registration record.
 */
const PDMDRVREG g_DrvHostCoreAudio =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "CoreAudio",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Core Audio host driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_AUDIO,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVHOSTCOREAUDIO),
    /* pfnConstruct */
    drvHstAudCaConstruct,
    /* pfnDestruct */
    drvHstAudCaDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    drvHstAudCaPowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

RT_GCC_NO_WARN_DEPRECATED_END
