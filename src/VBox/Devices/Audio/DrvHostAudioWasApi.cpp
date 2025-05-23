/* $Id: DrvHostAudioWasApi.cpp 168980 2025-05-23 22:14:36Z knut.osmundsen@oracle.com $ */
/** @file
 * Host audio driver - Windows Audio Session API.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
/*#define INITGUID - defined in VBoxhostAudioDSound.cpp already */
#include <VBox/log.h>
#include <iprt/win/windows.h>
#include <Mmdeviceapi.h>
#include <iprt/win/audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <AudioSessionTypes.h>
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
# define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY UINT32_C(0x08000000)
#endif
#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
# define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM UINT32_C(0x80000000)
#endif

#include <VBox/vmm/pdmaudioinline.h>
#include <VBox/vmm/pdmaudiohostenuminline.h>

#include <iprt/rand.h>
#include <iprt/semaphore.h>
#include <iprt/utf16.h>
#include <iprt/uuid.h>

#include <new> /* std::bad_alloc */

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Max GetCurrentPadding value we accept (to make sure it's safe to convert to bytes). */
#define VBOX_WASAPI_MAX_PADDING                 UINT32_C(0x007fffff)

/** Maximum number of cached device configs in each direction.
 * The number 4 was picked at random.  */
#define VBOX_WASAPI_MAX_TOTAL_CONFIG_ENTRIES    4

#if 0
/** @name WM_DRVHOSTAUDIOWAS_XXX - Worker thread messages.
 * @{ */
#define WM_DRVHOSTAUDIOWAS_PURGE_CACHE          (WM_APP + 3)
/** @} */
#endif


/** @name DRVHOSTAUDIOWAS_DO_XXX - Worker thread operations.
 * @{ */
#define DRVHOSTAUDIOWAS_DO_PURGE_CACHE          ((uintptr_t)0x49f37300 + 1)
#define DRVHOSTAUDIOWAS_DO_PRUNE_CACHE          ((uintptr_t)0x49f37300 + 2)
#define DRVHOSTAUDIOWAS_DO_STREAM_DEV_SWITCH    ((uintptr_t)0x49f37300 + 3)
/** @} */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
class DrvHostAudioWasMmNotifyClient;

/** Pointer to the cache entry for a host audio device (+dir). */
typedef struct DRVHOSTAUDIOWASCACHEDEV *PDRVHOSTAUDIOWASCACHEDEV;

/**
 * Cached pre-initialized audio client for a device.
 *
 * The activation and initialization of an IAudioClient has been observed to be
 * very very slow (> 100 ms) and not suitable to be done on an EMT.  So, we'll
 * pre-initialize the device clients at construction time and when the default
 * device changes to try avoid this problem.
 *
 * A client is returned to the cache after we're done with it, provided it still
 * works fine.
 */
typedef struct DRVHOSTAUDIOWASCACHEDEVCFG
{
    /** Entry in DRVHOSTAUDIOWASCACHEDEV::ConfigList. */
    RTLISTNODE                  ListEntry;
    /** The device.   */
    PDRVHOSTAUDIOWASCACHEDEV    pDevEntry;
    /** The cached audio client. */
    IAudioClient               *pIAudioClient;
    /** Output streams: The render client interface. */
    IAudioRenderClient         *pIAudioRenderClient;
    /** Input streams: The capture client interface. */
    IAudioCaptureClient        *pIAudioCaptureClient;
    /** The configuration. */
    PDMAUDIOPCMPROPS            Props;
    /** The buffer size in frames. */
    uint32_t                    cFramesBufferSize;
    /** The device/whatever period in frames. */
    uint32_t                    cFramesPeriod;
    /** The setup status code.
     * This is set to VERR_AUDIO_STREAM_INIT_IN_PROGRESS while the asynchronous
     * initialization is still running. */
    int volatile                rcSetup;
    /** Creation timestamp (just for reference). */
    uint64_t                    nsCreated;
    /** Init complete timestamp (just for reference). */
    uint64_t                    nsInited;
    /** When it was last used. */
    uint64_t                    nsLastUsed;
    /** The stringified properties. */
    char                        szProps[32];
} DRVHOSTAUDIOWASCACHEDEVCFG;
/** Pointer to a pre-initialized audio client. */
typedef DRVHOSTAUDIOWASCACHEDEVCFG *PDRVHOSTAUDIOWASCACHEDEVCFG;

/**
 * Per audio device (+ direction) cache entry.
 */
typedef struct DRVHOSTAUDIOWASCACHEDEV
{
    /** Entry in DRVHOSTAUDIOWAS::CacheHead. */
    RTLISTNODE                  ListEntry;
    /** The MM device associated with the stream. */
    IMMDevice                  *pIDevice;
    /** The direction of the device. */
    PDMAUDIODIR                 enmDir;
#if 0 /* According to https://social.msdn.microsoft.com/Forums/en-US/1d974d90-6636-4121-bba3-a8861d9ab92a,
         these were always support just missing from the SDK. */
    /** Support for AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM: -1=unknown, 0=no, 1=yes. */
    int8_t                      fSupportsAutoConvertPcm;
    /** Support for AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY: -1=unknown, 0=no, 1=yes. */
    int8_t                      fSupportsSrcDefaultQuality;
#endif
    /** Flag indicating whether this device is considered as being 'stale'
     *  (non-working / outdated interface / whatever) and can be removed
     *  from the cache (if enabled). */
    bool                        fStale;
    /** List of cached configurations (DRVHOSTAUDIOWASCACHEDEVCFG). */
    RTLISTANCHOR                ConfigList;
    /** The device ID length in RTUTF16 units. */
    size_t                      cwcDevId;
    /** The device ID. */
    RTUTF16                     wszDevId[RT_FLEXIBLE_ARRAY];
} DRVHOSTAUDIOWASCACHEDEV;


/**
 * Data for a WASABI stream.
 */
typedef struct DRVHOSTAUDIOWASSTREAM
{
    /** Common part. */
    PDMAUDIOBACKENDSTREAM       Core;

    /** Entry in DRVHOSTAUDIOWAS::StreamHead. */
    RTLISTNODE                  ListEntry;
    /** The stream's acquired configuration. */
    PDMAUDIOSTREAMCFG           Cfg;
    /** Cache entry to be relased when destroying the stream. */
    PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfg;

    /** Set if the stream is enabled. */
    bool                        fEnabled;
    /** Set if the stream is started (playing/capturing). */
    bool                        fStarted;
    /** Set if the stream is draining (output only). */
    bool                        fDraining;
    /** Set if we should restart the stream on resume (saved pause state). */
    bool                        fRestartOnResume;
    /** Set if we're switching to a new output/input device. */
    bool                        fSwitchingDevice;

    /** The RTTimeMilliTS() deadline for the draining of this stream (output). */
    uint64_t                    msDrainDeadline;
    /** Internal stream offset (bytes). */
    uint64_t                    offInternal;
    /** The RTTimeMilliTS() at the end of the last transfer. */
    uint64_t                    msLastTransfer;

    /** Input: Current capture buffer (advanced as we read). */
    uint8_t                    *pbCapture;
    /** Input: The number of bytes left in the current capture buffer. */
    uint32_t                    cbCapture;
    /** Input: The full size of what pbCapture is part of (for ReleaseBuffer). */
    uint32_t                    cFramesCaptureToRelease;

    /** Critical section protecting: . */
    RTCRITSECT                  CritSect;
    /** Buffer that drvHostWasStreamStatusString uses. */
    char                        szStatus[128];
} DRVHOSTAUDIOWASSTREAM;
/** Pointer to a WASABI stream. */
typedef DRVHOSTAUDIOWASSTREAM *PDRVHOSTAUDIOWASSTREAM;


/**
 * WASAPI-specific device entry.
 */
typedef struct DRVHOSTAUDIOWASDEV
{
    /** The core structure. */
    PDMAUDIOHOSTDEV         Core;
    /** The device ID (flexible length). */
    RTUTF16                 wszDevId[RT_FLEXIBLE_ARRAY];
} DRVHOSTAUDIOWASDEV;
/** Pointer to a DirectSound device entry. */
typedef DRVHOSTAUDIOWASDEV *PDRVHOSTAUDIOWASDEV;


/**
 * Data for a WASAPI host audio instance.
 */
typedef struct DRVHOSTAUDIOWAS
{
    /** The audio host audio interface we export. */
    PDMIHOSTAUDIO                   IHostAudio;
    /** Pointer to the PDM driver instance. */
    PPDMDRVINS                      pDrvIns;
    /** Audio device enumerator instance that we use for getting the default
     * devices (or specific ones if overriden by config).  Also used for
     * implementing enumeration. */
    IMMDeviceEnumerator            *pIEnumerator;
    /** The upwards interface. */
    PPDMIHOSTAUDIOPORT              pIHostAudioPort;
    /** The output device ID, NULL for default.
     * Protected by DrvHostAudioWasMmNotifyClient::m_CritSect. */
    PRTUTF16                        pwszOutputDevId;
    /** The input device ID, NULL for default.
     * Protected by DrvHostAudioWasMmNotifyClient::m_CritSect.  */
    PRTUTF16                        pwszInputDevId;

    /** Pointer to the MM notification client instance. */
    DrvHostAudioWasMmNotifyClient  *pNotifyClient;
    /** The input device to use.  This can be NULL if there wasn't a suitable one
     * around when we last looked or if it got removed/disabled/whatever.
    * All access must be done inside the pNotifyClient critsect. */
    IMMDevice                      *pIDeviceInput;
    /** The output device to use.  This can be NULL if there wasn't a suitable one
     * around when we last looked or if it got removed/disabled/whatever.
     * All access must be done inside the pNotifyClient critsect. */
    IMMDevice                      *pIDeviceOutput;

    /** List of streams (DRVHOSTAUDIOWASSTREAM).
     * Requires CritSect ownership.  */
    RTLISTANCHOR                    StreamHead;
    /** Serializing access to StreamHead. */
    RTCRITSECTRW                    CritSectStreamList;

    /** Whether the device cache (\a CacheHead and friends) is being used or not.
     *  Enabled by default, can be disabled via CFGM. */
    bool                            fCacheEnabled;
    /** List of cached devices (DRVHOSTAUDIOWASCACHEDEV).
     * Protected by CritSectCache  */
    RTLISTANCHOR                    CacheHead;
    /** Serializing access to CacheHead. */
    RTCRITSECT                      CritSectCache;
    /** Semaphore for signalling that cache purge is done and that the destructor
     *  can do cleanups. */
    RTSEMEVENTMULTI                 hEvtCachePurge;
    /** Total number of device config entries for capturing.
     * This includes in-use ones. */
    uint32_t volatile               cCacheEntriesIn;
    /** Total number of device config entries for playback.
     * This includes in-use ones. */
    uint32_t volatile               cCacheEntriesOut;

#if 0
    /** The worker thread. */
    RTTHREAD                        hWorkerThread;
    /** The TID of the worker thread (for posting messages to it). */
    DWORD                           idWorkerThread;
    /** The fixed wParam value for the worker thread. */
    WPARAM                          uWorkerThreadFixedParam;
#endif
} DRVHOSTAUDIOWAS;
/** Pointer to the data for a WASAPI host audio driver instance. */
typedef DRVHOSTAUDIOWAS *PDRVHOSTAUDIOWAS;




/**
 * Gets the stream status.
 *
 * @returns Pointer to stream status string.
 * @param   pStreamWas          The stream to get the status for.
 */
static const char *drvHostWasStreamStatusString(PDRVHOSTAUDIOWASSTREAM pStreamWas)
{
    static RTSTRTUPLE const s_aEnable[2] =
    {
        { RT_STR_TUPLE("DISABLED") },
        { RT_STR_TUPLE("ENABLED ") },
    };
    PCRTSTRTUPLE pTuple = &s_aEnable[pStreamWas->fEnabled];
    memcpy(pStreamWas->szStatus, pTuple->psz, pTuple->cch);
    size_t off = pTuple->cch;

    static RTSTRTUPLE const s_aStarted[2] =
    {
        { RT_STR_TUPLE(" STOPPED") },
        { RT_STR_TUPLE(" STARTED") },
    };
    pTuple = &s_aStarted[pStreamWas->fStarted];
    memcpy(&pStreamWas->szStatus[off], pTuple->psz, pTuple->cch);
    off += pTuple->cch;

    static RTSTRTUPLE const s_aDraining[2] =
    {
        { RT_STR_TUPLE("")          },
        { RT_STR_TUPLE(" DRAINING") },
    };
    pTuple = &s_aDraining[pStreamWas->fDraining];
    memcpy(&pStreamWas->szStatus[off], pTuple->psz, pTuple->cch);
    off += pTuple->cch;

    Assert(off < sizeof(pStreamWas->szStatus));
    pStreamWas->szStatus[off] = '\0';
    return pStreamWas->szStatus;
}


/**
 * Returns the MMDeviceState as a string.
 *
 * @returns Pointer to MMDeviceState string.
 * @param   dwState             MMDeviceState to return as a string.
 */
DECLINLINE(const char *) drvHostAudioWasMMDeviceStateToString(DWORD dwState)
{
    switch (dwState)
    {
        case DEVICE_STATE_ACTIVE:     return "ACTIVE";
        case DEVICE_STATE_DISABLED:   return "DISABLED";
        case DEVICE_STATE_NOTPRESENT: return "NOTPRESENT";
        case DEVICE_STATE_UNPLUGGED:  return "UNPLUGGED";
        default:                      break;
    }

    AssertFailed();
    return "???";
}


/*********************************************************************************************************************************
*   IMMNotificationClient implementation
*********************************************************************************************************************************/
/**
 * Multimedia notification client.
 *
 * We want to know when the default device changes so we can switch running
 * streams to use the new one and so we can pre-activate it in preparation
 * for new streams.
 */
class DrvHostAudioWasMmNotifyClient : public IMMNotificationClient
{
private:
    /** Reference counter. */
    uint32_t volatile           m_cRefs;
    /** The WASAPI host audio driver instance data.
     * @note    This can be NULL.  Only access after entering critical section. */
    PDRVHOSTAUDIOWAS            m_pDrvWas;
    /** Critical section serializing access to m_pDrvWas.  */
    RTCRITSECT                  m_CritSect;

public:
    /**
     * @throws int on critical section init failure.
     */
    DrvHostAudioWasMmNotifyClient(PDRVHOSTAUDIOWAS a_pDrvWas)
        : m_cRefs(1)
        , m_pDrvWas(a_pDrvWas)
    {
        RT_ZERO(m_CritSect);
    }

    virtual ~DrvHostAudioWasMmNotifyClient() RT_NOEXCEPT
    {
        if (RTCritSectIsInitialized(&m_CritSect))
            RTCritSectDelete(&m_CritSect);
    }

    /**
     * Initializes the critical section.
     * @note  Must be buildable w/o exceptions enabled, so cannot do this from the
     *        constructor. */
    int init(void) RT_NOEXCEPT
    {
        return RTCritSectInit(&m_CritSect);
    }

    /**
     * Called by drvHostAudioWasDestruct to set m_pDrvWas to NULL.
     */
    void notifyDriverDestroyed() RT_NOEXCEPT
    {
        RTCritSectEnter(&m_CritSect);
        m_pDrvWas = NULL;
        RTCritSectLeave(&m_CritSect);
    }

    /**
     * Enters the notification critsect for getting at the IMMDevice members in
     * PDMHOSTAUDIOWAS.
     */
    void lockEnter() RT_NOEXCEPT
    {
        RTCritSectEnter(&m_CritSect);
    }

    /**
     * Leaves the notification critsect.
     */
    void lockLeave() RT_NOEXCEPT
    {
        RTCritSectLeave(&m_CritSect);
    }

    /** @name IUnknown interface
     * @{ */
    IFACEMETHODIMP_(ULONG)  AddRef()
    {
        uint32_t cRefs = ASMAtomicIncU32(&m_cRefs);
        AssertMsg(cRefs < 64, ("%#x\n", cRefs));
        Log6Func(("returns %u\n", cRefs));
        return cRefs;
    }

    IFACEMETHODIMP_(ULONG)  Release()
    {
        uint32_t cRefs = ASMAtomicDecU32(&m_cRefs);
        AssertMsg(cRefs < 64, ("%#x\n", cRefs));
        if (cRefs == 0)
            delete this;
        Log6Func(("returns %u\n", cRefs));
        return cRefs;
    }

    IFACEMETHODIMP          QueryInterface(const IID &rIID, void **ppvInterface)
    {
        if (IsEqualIID(rIID, IID_IUnknown))
            *ppvInterface = static_cast<IUnknown *>(this);
        else if (IsEqualIID(rIID, __uuidof(IMMNotificationClient)))
            *ppvInterface = static_cast<IMMNotificationClient *>(this);
        else
        {
            LogFunc(("Unknown rIID={%RTuuid}\n", &rIID));
            *ppvInterface = NULL;
            return E_NOINTERFACE;
        }
        Log6Func(("returns S_OK + %p\n", *ppvInterface));
        return S_OK;
    }
    /** @} */

    /** @name IMMNotificationClient interface
     * @{ */
    IFACEMETHODIMP OnDeviceStateChanged(LPCWSTR pwszDeviceId, DWORD dwNewState)
    {
        LogRelMax(64, ("WasAPI: Device state for '%ls' changed to %s (%#x)\n",
                       pwszDeviceId, drvHostAudioWasMMDeviceStateToString(dwNewState), dwNewState));
        /*
         * Just trigger device re-enumeration.
         */
        notifyDeviceChanges();

        /** @todo do we need to check for our devices here too?  Not when using a
         *        default device.  But when using a specific device, we could perhaps
         *        re-init the stream when dwNewState indicates precense.  We might
         *        also take action when a devices ceases to be operating, but again
         *        only for non-default devices, probably... */

        return S_OK;
    }

    IFACEMETHODIMP OnDeviceAdded(LPCWSTR pwszDeviceId)
    {
        LogRelMax(64, ("WasAPI: Device '%ls' added\n", pwszDeviceId));

        /*
         * Is this a device we're interested in?  Grab the enumerator if it is.
         */
        bool                 fOutput      = false;
        IMMDeviceEnumerator *pIEnumerator = NULL;
        RTCritSectEnter(&m_CritSect);
        if (   m_pDrvWas != NULL
            && (   (fOutput = RTUtf16ICmp(m_pDrvWas->pwszOutputDevId, pwszDeviceId) == 0)
                || RTUtf16ICmp(m_pDrvWas->pwszInputDevId, pwszDeviceId) == 0))
        {
            pIEnumerator = m_pDrvWas->pIEnumerator;
            if (pIEnumerator /* paranoia */)
                pIEnumerator->AddRef();
        }
        RTCritSectLeave(&m_CritSect);
        if (pIEnumerator)
        {
            /*
             * Get the device and update it.
             */
            IMMDevice *pIDevice = NULL;
            HRESULT hrc = pIEnumerator->GetDevice(pwszDeviceId, &pIDevice);
            if (SUCCEEDED(hrc))
                setDevice(fOutput, pIDevice, pwszDeviceId, __PRETTY_FUNCTION__);
            else
                LogRelMax(64, ("WasAPI: Failed to get %s device '%ls' (OnDeviceAdded): %Rhrc\n",
                               fOutput ? "output" : "input", pwszDeviceId, hrc));
            pIEnumerator->Release();

            /*
             * Trigger device re-enumeration.
             */
            notifyDeviceChanges();
        }
        return S_OK;
    }

    IFACEMETHODIMP OnDeviceRemoved(LPCWSTR pwszDeviceId)
    {
        LogRelMax(64, ("WasAPI: Device '%ls' removed\n", pwszDeviceId));

        /*
         * Is this a device we're interested in?  Then set it to NULL.
         */
        bool fOutput = false;
        RTCritSectEnter(&m_CritSect);
        if (   m_pDrvWas != NULL
            && (   (fOutput = RTUtf16ICmp(m_pDrvWas->pwszOutputDevId, pwszDeviceId) == 0)
                || RTUtf16ICmp(m_pDrvWas->pwszInputDevId, pwszDeviceId) == 0))
        {
            RTCritSectLeave(&m_CritSect);
            setDevice(fOutput, NULL, pwszDeviceId, __PRETTY_FUNCTION__);
        }
        else
            RTCritSectLeave(&m_CritSect);

        /*
         * Trigger device re-enumeration.
         */
        notifyDeviceChanges();
        return S_OK;
    }

    IFACEMETHODIMP OnDefaultDeviceChanged(EDataFlow enmFlow, ERole enmRole, LPCWSTR pwszDefaultDeviceId)
    {
        /*
         * Are we interested in this device?  If so grab the enumerator.
         */
        IMMDeviceEnumerator *pIEnumerator = NULL;
        RTCritSectEnter(&m_CritSect);
        if (    m_pDrvWas != NULL
            && (   (enmFlow == eRender  && !m_pDrvWas->pwszOutputDevId)
                || (enmFlow == eCapture && !m_pDrvWas->pwszInputDevId))
            /* We only care for the multimedia role of a device here.
             * Devices can have multiple roles they expose interfaces for.
             *
             * Not having this check will result in several OnDefaultDeviceChanged calls
             * with the same device ID but with a different role. However, in our audio stack there is no concept
             * of a device role, so we try avoiding that by only caring about eMultimedia.
             *
             * See @bugref{10844} */
            && enmRole == eMultimedia
           )
        {
            pIEnumerator = m_pDrvWas->pIEnumerator;
            if (pIEnumerator /* paranoia */)
                pIEnumerator->AddRef();
        }

        LogRelMax2(64, ("WasAPI: Default %s device changed (role=%#x, id='%ls')\n",
                        enmFlow == eRender ? "output" : "input", enmRole, pwszDefaultDeviceId ? pwszDefaultDeviceId : L"<None>"));

        RTCritSectLeave(&m_CritSect);
        if (pIEnumerator)
        {
            /*
             * Get the device and update it.
             */
            IMMDevice *pIDevice = NULL;
            HRESULT hrc = pIEnumerator->GetDefaultAudioEndpoint(enmFlow, enmRole, &pIDevice);
            if (SUCCEEDED(hrc))
                setDevice(enmFlow == eRender, pIDevice, pwszDefaultDeviceId, __PRETTY_FUNCTION__);
            else
                LogRelMax(64, ("WasAPI: Failed to get default %s device (OnDefaultDeviceChange): %Rhrc\n",
                               enmFlow == eRender ? "output" : "input", hrc));
            pIEnumerator->Release();

            /*
             * Trigger device re-enumeration.
             */
            notifyDeviceChanges();
        }

        Log7Func(("enmFlow=%d enmRole=%d pwszDefaultDeviceId=%ls\n", enmFlow, enmRole, pwszDefaultDeviceId));
        return S_OK;
    }

    IFACEMETHODIMP OnPropertyValueChanged(LPCWSTR pwszDeviceId, const PROPERTYKEY Key)
    {
        RT_NOREF(pwszDeviceId, Key);
        Log7Func(("pwszDeviceId=%ls Key={%RTuuid, %u (%#x)}\n", pwszDeviceId, &Key.fmtid, Key.pid, Key.pid));
        return S_OK;
    }
    /** @} */

private:
    /**
     * Sets DRVHOSTAUDIOWAS::pIDeviceOutput or DRVHOSTAUDIOWAS::pIDeviceInput to @a pIDevice.
     */
    void setDevice(bool fOutput, IMMDevice *pIDevice, LPCWSTR pwszDeviceId, const char *pszCaller)
    {
        RT_NOREF(pszCaller, pwszDeviceId);

        RTCritSectEnter(&m_CritSect);

        /*
         * Update our internal device reference.
         */
        if (m_pDrvWas)
        {
            if (fOutput)
            {
                Log7((LOG_FN_FMT ": Changing output device from %p to %p (%ls)\n",
                      pszCaller, m_pDrvWas->pIDeviceOutput, pIDevice, pwszDeviceId));
                if (m_pDrvWas->pIDeviceOutput)
                    m_pDrvWas->pIDeviceOutput->Release();
                m_pDrvWas->pIDeviceOutput = pIDevice;
            }
            else
            {
                Log7((LOG_FN_FMT ": Changing input device from %p to %p (%ls)\n",
                      pszCaller, m_pDrvWas->pIDeviceInput, pIDevice, pwszDeviceId));
                if (m_pDrvWas->pIDeviceInput)
                    m_pDrvWas->pIDeviceInput->Release();
                m_pDrvWas->pIDeviceInput = pIDevice;
            }
        }
        else if (pIDevice)
            pIDevice->Release();

        /*
         * Tell DrvAudio that the device has changed for one of the directions.
         *
         * We have to exit the critsect when doing so, or we'll create a locking
         * order violation.  So, try make sure the VM won't be destroyed while
         * till DrvAudio have entered its critical section...
         */
        if (m_pDrvWas)
        {
            PPDMIHOSTAUDIOPORT const pIHostAudioPort = m_pDrvWas->pIHostAudioPort;
            if (pIHostAudioPort)
            {
                VMSTATE const enmVmState = PDMDrvHlpVMState(m_pDrvWas->pDrvIns);
                if (enmVmState < VMSTATE_POWERING_OFF)
                {
                    RTCritSectLeave(&m_CritSect);
                    pIHostAudioPort->pfnNotifyDeviceChanged(pIHostAudioPort, fOutput ? PDMAUDIODIR_OUT : PDMAUDIODIR_IN, NULL);
                    return;
                }
                LogFlowFunc(("Ignoring change: enmVmState=%d\n", enmVmState));
            }
        }

        RTCritSectLeave(&m_CritSect);
    }

    /**
     * Tell DrvAudio to re-enumerate devices when it get a chance.
     *
     * We exit the critsect here too before calling DrvAudio just to be on the safe
     * side (see setDevice()), even though the current DrvAudio code doesn't take
     * any critsects.
     */
    void notifyDeviceChanges(void)
    {
        RTCritSectEnter(&m_CritSect);
        if (m_pDrvWas)
        {
            PPDMIHOSTAUDIOPORT const pIHostAudioPort = m_pDrvWas->pIHostAudioPort;
            if (pIHostAudioPort)
            {
                VMSTATE const enmVmState = PDMDrvHlpVMState(m_pDrvWas->pDrvIns);
                if (enmVmState < VMSTATE_POWERING_OFF)
                {
                    RTCritSectLeave(&m_CritSect);
                    pIHostAudioPort->pfnNotifyDevicesChanged(pIHostAudioPort);
                    return;
                }
                LogFlowFunc(("Ignoring change: enmVmState=%d\n", enmVmState));
            }
        }
        RTCritSectLeave(&m_CritSect);
    }
};


/*********************************************************************************************************************************
*   Pre-configured audio client cache.                                                                                           *
*********************************************************************************************************************************/
#define WAS_CACHE_MAX_ENTRIES_SAME_DEVICE   2

/**
 * Converts from PDM stream config to windows WAVEFORMATEXTENSIBLE struct.
 *
 * @param   pProps  The PDM audio PCM properties to convert from.
 * @param   pFmt    The windows structure to initialize.
 */
static void drvHostAudioWasWaveFmtExtFromProps(PCPDMAUDIOPCMPROPS pProps, PWAVEFORMATEXTENSIBLE pFmt)
{
    RT_ZERO(*pFmt);
    pFmt->Format.wFormatTag      = WAVE_FORMAT_PCM;
    pFmt->Format.nChannels       = PDMAudioPropsChannels(pProps);
    pFmt->Format.wBitsPerSample  = PDMAudioPropsSampleBits(pProps);
    pFmt->Format.nSamplesPerSec  = PDMAudioPropsHz(pProps);
    pFmt->Format.nBlockAlign     = PDMAudioPropsFrameSize(pProps);
    pFmt->Format.nAvgBytesPerSec = PDMAudioPropsFramesToBytes(pProps, PDMAudioPropsHz(pProps));
    pFmt->Format.cbSize          = 0; /* No extra data specified. */

    /*
     * We need to use the extensible structure if there are more than two channels
     * or if the channels have non-standard assignments.
     */
    if (   pFmt->Format.nChannels > 2
        || (  pFmt->Format.nChannels == 1
            ?    pProps->aidChannels[0] != PDMAUDIOCHANNELID_MONO
            :    pProps->aidChannels[0] != PDMAUDIOCHANNELID_FRONT_LEFT
              || pProps->aidChannels[1] != PDMAUDIOCHANNELID_FRONT_RIGHT))
    {
        pFmt->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        pFmt->Format.cbSize     = sizeof(*pFmt) - sizeof(pFmt->Format);
        pFmt->Samples.wValidBitsPerSample = PDMAudioPropsSampleBits(pProps);
        pFmt->SubFormat         = KSDATAFORMAT_SUBTYPE_PCM;
        pFmt->dwChannelMask     = 0;
        unsigned const cSrcChannels = pFmt->Format.nChannels;
        for (unsigned i = 0; i < cSrcChannels; i++)
            if (   pProps->aidChannels[i] >= PDMAUDIOCHANNELID_FIRST_STANDARD
                && pProps->aidChannels[i] <  PDMAUDIOCHANNELID_END_STANDARD)
                pFmt->dwChannelMask |= RT_BIT_32(pProps->aidChannels[i] - PDMAUDIOCHANNELID_FIRST_STANDARD);
            else
                pFmt->Format.nChannels -= 1;
    }
}


#if 0 /* unused */
/**
 * Converts from windows WAVEFORMATEX and stream props to PDM audio properties.
 *
 * @returns VINF_SUCCESS on success, VERR_AUDIO_STREAM_COULD_NOT_CREATE if not
 *          supported.
 * @param   pProps      The output properties structure.
 * @param   pFmt        The windows wave format structure.
 * @param   pszStream   The stream name for error logging.
 * @param   pwszDevId   The device ID for error logging.
 */
static int drvHostAudioWasCacheWaveFmtExToProps(PPDMAUDIOPCMPROPS pProps, WAVEFORMATEX const *pFmt,
                                                const char *pszStream, PCRTUTF16 pwszDevId)
{
    if (pFmt->wFormatTag == WAVE_FORMAT_PCM)
    {
        if (   pFmt->wBitsPerSample == 8
            || pFmt->wBitsPerSample == 16
            || pFmt->wBitsPerSample == 32)
        {
            if (pFmt->nChannels > 0 && pFmt->nChannels < 16)
            {
                if (pFmt->nSamplesPerSec >= 4096 && pFmt->nSamplesPerSec <= 768000)
                {
                    PDMAudioPropsInit(pProps, pFmt->wBitsPerSample / 8, true /*fSigned*/, pFmt->nChannels, pFmt->nSamplesPerSec);
                    if (PDMAudioPropsFrameSize(pProps) == pFmt->nBlockAlign)
                        return VINF_SUCCESS;
                }
            }
        }
    }
    LogRelMax(64, ("WasAPI: Error! Unsupported stream format for '%s' suggested by '%ls':\n"
                   "WasAPI:   wFormatTag      = %RU16 (expected %d)\n"
                   "WasAPI:   nChannels       = %RU16 (expected 1..15)\n"
                   "WasAPI:   nSamplesPerSec  = %RU32 (expected 4096..768000)\n"
                   "WasAPI:   nAvgBytesPerSec = %RU32\n"
                   "WasAPI:   nBlockAlign     = %RU16\n"
                   "WasAPI:   wBitsPerSample  = %RU16 (expected 8, 16, or 32)\n"
                   "WasAPI:   cbSize          = %RU16\n",
                   pszStream, pwszDevId, pFmt->wFormatTag, WAVE_FORMAT_PCM, pFmt->nChannels, pFmt->nSamplesPerSec, pFmt->nAvgBytesPerSec,
                   pFmt->nBlockAlign, pFmt->wBitsPerSample, pFmt->cbSize));
    return VERR_AUDIO_STREAM_COULD_NOT_CREATE;
}
#endif


/**
 * Destroys a devie config cache entry.
 *
 * @param   pThis       The WASAPI host audio driver instance data.
 * @param   pDevCfg     Device config entry.  Must not be in the list.
 */
static void drvHostAudioWasCacheDestroyDevConfig(PDRVHOSTAUDIOWAS pThis, PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfg)
{
    if (pDevCfg->pDevEntry->enmDir == PDMAUDIODIR_IN)
        ASMAtomicDecU32(&pThis->cCacheEntriesIn);
    else
        ASMAtomicDecU32(&pThis->cCacheEntriesOut);

    uint32_t cTypeClientRefs = 0;
    if (pDevCfg->pIAudioCaptureClient)
    {
        cTypeClientRefs = pDevCfg->pIAudioCaptureClient->Release();
        pDevCfg->pIAudioCaptureClient = NULL;
    }

    if (pDevCfg->pIAudioRenderClient)
    {
        cTypeClientRefs = pDevCfg->pIAudioRenderClient->Release();
        pDevCfg->pIAudioRenderClient = NULL;
    }

    uint32_t cClientRefs = 0;
    if (pDevCfg->pIAudioClient /* paranoia */)
    {
        cClientRefs = pDevCfg->pIAudioClient->Release();
        pDevCfg->pIAudioClient = NULL;
    }

    Log8Func(("Destroying cache config entry: '%ls: %s' - cClientRefs=%u cTypeClientRefs=%u\n",
              pDevCfg->pDevEntry->wszDevId, pDevCfg->szProps, cClientRefs, cTypeClientRefs));
    RT_NOREF(cClientRefs, cTypeClientRefs);

    pDevCfg->pDevEntry = NULL;
    RTMemFree(pDevCfg);
}


/**
 * Destroys a device cache entry.
 *
 * @param   pThis       The WASAPI host audio driver instance data.
 * @param   pDevEntry   The device entry. Must not be in the cache!
 */
static void drvHostAudioWasCacheDestroyDevEntry(PDRVHOSTAUDIOWAS pThis, PDRVHOSTAUDIOWASCACHEDEV pDevEntry)
{
    Log8Func(("Destroying cache entry: %p - '%ls'\n", pDevEntry, pDevEntry->wszDevId));

    PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfg, pDevCfgNext;
    RTListForEachSafe(&pDevEntry->ConfigList, pDevCfg, pDevCfgNext, DRVHOSTAUDIOWASCACHEDEVCFG, ListEntry)
        drvHostAudioWasCacheDestroyDevConfig(pThis, pDevCfg);

    uint32_t cDevRefs = 0;
    if (pDevEntry->pIDevice /* paranoia */)
    {
        cDevRefs = pDevEntry->pIDevice->Release();
        pDevEntry->pIDevice = NULL;
    }

    pDevEntry->cwcDevId = 0;
    pDevEntry->wszDevId[0] = '\0';
    RTMemFree(pDevEntry);
    Log8Func(("Destroyed cache entry: %p cDevRefs=%u\n", pDevEntry, cDevRefs));
}


/**
 * Prunes the cache.
 */
static void drvHostAudioWasCachePrune(PDRVHOSTAUDIOWAS pThis)
{
    AssertReturnVoid(pThis->fCacheEnabled);

    /*
     * Prune each direction separately.
     */
    struct
    {
        PDMAUDIODIR        enmDir;
        uint32_t volatile *pcEntries;
    } aWork[] = { { PDMAUDIODIR_IN, &pThis->cCacheEntriesIn }, { PDMAUDIODIR_OUT, &pThis->cCacheEntriesOut }, };
    for (uint32_t iWork = 0; iWork < RT_ELEMENTS(aWork); iWork++)
    {
        /*
         * Remove the least recently used entry till we're below the threshold
         * or there are no more inactive entries.
         */
        LogFlowFunc(("iWork=%u cEntries=%u\n", iWork, *aWork[iWork].pcEntries));
        while (*aWork[iWork].pcEntries > VBOX_WASAPI_MAX_TOTAL_CONFIG_ENTRIES)
        {
            RTCritSectEnter(&pThis->CritSectCache);
            PDRVHOSTAUDIOWASCACHEDEVCFG pLeastRecentlyUsed = NULL;
            PDRVHOSTAUDIOWASCACHEDEV    pDevEntry;
            RTListForEach(&pThis->CacheHead, pDevEntry, DRVHOSTAUDIOWASCACHEDEV, ListEntry)
            {
                if (pDevEntry->enmDir == aWork[iWork].enmDir)
                {
                    PDRVHOSTAUDIOWASCACHEDEVCFG pHeadCfg = RTListGetFirst(&pDevEntry->ConfigList,
                                                                          DRVHOSTAUDIOWASCACHEDEVCFG, ListEntry);
                    if (   pHeadCfg
                        && (!pLeastRecentlyUsed || pHeadCfg->nsLastUsed < pLeastRecentlyUsed->nsLastUsed))
                        pLeastRecentlyUsed = pHeadCfg;
                }
            }
            if (pLeastRecentlyUsed)
                RTListNodeRemove(&pLeastRecentlyUsed->ListEntry);
            RTCritSectLeave(&pThis->CritSectCache);

            if (!pLeastRecentlyUsed)
                break;
            drvHostAudioWasCacheDestroyDevConfig(pThis, pLeastRecentlyUsed);
        }
    }
}


/**
 * Purges all the entries in the cache.
 */
static void drvHostAudioWasCachePurge(PDRVHOSTAUDIOWAS pThis, bool fOnWorker)
{
    AssertReturnVoid(pThis->fCacheEnabled);

    for (;;)
    {
        RTCritSectEnter(&pThis->CritSectCache);
        PDRVHOSTAUDIOWASCACHEDEV pDevEntry = RTListRemoveFirst(&pThis->CacheHead, DRVHOSTAUDIOWASCACHEDEV, ListEntry);
        RTCritSectLeave(&pThis->CritSectCache);
        if (!pDevEntry)
            break;
        drvHostAudioWasCacheDestroyDevEntry(pThis, pDevEntry);
    }

    if (fOnWorker)
    {
        int rc = RTSemEventMultiSignal(pThis->hEvtCachePurge);
        AssertRC(rc);
    }
}


/**
 * Looks up a specific configuration.
 *
 * @returns Pointer to the device config (removed from cache) on success.  NULL
 *          if no matching config found.
 * @param   pDevEntry       Where to perform the lookup.
 * @param   pProps          The config properties to match.
 */
static PDRVHOSTAUDIOWASCACHEDEVCFG
drvHostAudioWasCacheLookupLocked(PDRVHOSTAUDIOWASCACHEDEV pDevEntry, PCPDMAUDIOPCMPROPS pProps)
{
    PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfg;
    RTListForEach(&pDevEntry->ConfigList, pDevCfg, DRVHOSTAUDIOWASCACHEDEVCFG, ListEntry)
    {
        if (PDMAudioPropsAreEqual(&pDevCfg->Props, pProps))
        {
            RTListNodeRemove(&pDevCfg->ListEntry);
            pDevCfg->nsLastUsed = RTTimeNanoTS();
            return pDevCfg;
        }
    }
    return NULL;
}


/**
 * Initializes a device config entry.
 *
 * This is usually done on the worker thread.
 *
 * @returns VBox status code.
 * @param   pDevCfg         The device configuration entry to initialize.
 */
static int drvHostAudioWasCacheInitConfig(PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfg)
{
    /*
     * Assert some sanity given that we migth be called on the worker thread
     * and pDevCfg being a message parameter.
     */
    AssertPtrReturn(pDevCfg, VERR_INTERNAL_ERROR_2);
    AssertReturn(pDevCfg->rcSetup == VERR_AUDIO_STREAM_INIT_IN_PROGRESS, VERR_INTERNAL_ERROR_2);
    AssertReturn(pDevCfg->pIAudioClient == NULL, VERR_INTERNAL_ERROR_2);
    AssertReturn(pDevCfg->pIAudioCaptureClient == NULL, VERR_INTERNAL_ERROR_2);
    AssertReturn(pDevCfg->pIAudioRenderClient == NULL, VERR_INTERNAL_ERROR_2);
    AssertReturn(PDMAudioPropsAreValid(&pDevCfg->Props), VERR_INTERNAL_ERROR_2);

    PDRVHOSTAUDIOWASCACHEDEV pDevEntry = pDevCfg->pDevEntry;
    AssertPtrReturn(pDevEntry, VERR_INTERNAL_ERROR_2);
    AssertPtrReturn(pDevEntry->pIDevice, VERR_INTERNAL_ERROR_2);
    AssertReturn(pDevEntry->enmDir == PDMAUDIODIR_IN || pDevEntry->enmDir == PDMAUDIODIR_OUT, VERR_INTERNAL_ERROR_2);

    /*
     * First we need an IAudioClient interface for calling IsFormatSupported
     * on so we can get guidance as to what to do next.
     *
     * Initially, I thought the AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM was not
     * supported all the way back to Vista and that we'd had to try different
     * things here to get the most optimal format.  However, according to
     * https://social.msdn.microsoft.com/Forums/en-US/1d974d90-6636-4121-bba3-a8861d9ab92a
     * it is supported, just maybe missing from the SDK or something...
     *
     * I'll leave the IsFormatSupported call here as it gives us a clue as to
     * what exactly the WAS needs to convert our audio stream into/from.
     */
    Log8Func(("Activating an IAudioClient for '%ls' ...\n", pDevEntry->wszDevId));
    IAudioClient *pIAudioClient = NULL;
    HRESULT hrc = pDevEntry->pIDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                                                NULL /*pActivationParams*/, (void **)&pIAudioClient);
    Log8Func(("Activate('%ls', IAudioClient) -> %Rhrc\n", pDevEntry->wszDevId, hrc));
    if (FAILED(hrc))
    {
        LogRelMax(64, ("WasAPI: Activate(%ls, IAudioClient) failed: %Rhrc\n", pDevEntry->wszDevId, hrc));
        pDevCfg->nsInited   = RTTimeNanoTS();
        pDevCfg->nsLastUsed = pDevCfg->nsInited;
        return pDevCfg->rcSetup = VERR_AUDIO_STREAM_COULD_NOT_CREATE;
    }

    WAVEFORMATEXTENSIBLE WaveFmtExt;
    drvHostAudioWasWaveFmtExtFromProps(&pDevCfg->Props, &WaveFmtExt);

    PWAVEFORMATEX pClosestMatch = NULL;
    hrc = pIAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &WaveFmtExt.Format, &pClosestMatch);

    /*
     * If the format is supported, go ahead and initialize the client instance.
     *
     * The docs talks about AUDCLNT_E_UNSUPPORTED_FORMAT being success too, but
     * that doesn't seem to be the case (at least not for mixing up the
     * WAVEFORMATEX::wFormatTag values).  Seems that is the standard return code
     * if there is anything it doesn't grok.
     */
    if (SUCCEEDED(hrc))
    {
        if (hrc == S_OK)
            Log8Func(("IsFormatSupported(,%s,) -> S_OK + %p: requested format is supported\n", pDevCfg->szProps, pClosestMatch));
        else
            Log8Func(("IsFormatSupported(,%s,) -> %Rhrc + %p: %uch S%u %uHz\n", pDevCfg->szProps, hrc, pClosestMatch,
                      pClosestMatch ? pClosestMatch->nChannels : 0, pClosestMatch ? pClosestMatch->wBitsPerSample : 0,
                      pClosestMatch ? pClosestMatch->nSamplesPerSec : 0));

        REFERENCE_TIME const cBufferSizeInNtTicks = PDMAudioPropsFramesToNtTicks(&pDevCfg->Props, pDevCfg->cFramesBufferSize);
        uint32_t             fInitFlags           = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                                                  | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        hrc = pIAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, fInitFlags, cBufferSizeInNtTicks,
                                        0 /*cPeriodicityInNtTicks*/, &WaveFmtExt.Format, NULL /*pAudioSessionGuid*/);
        Log8Func(("Initialize(,%x, %RI64, %s,) -> %Rhrc\n", fInitFlags, cBufferSizeInNtTicks, pDevCfg->szProps, hrc));
        if (SUCCEEDED(hrc))
        {
            /*
             * The direction specific client interface.
             */
            if (pDevEntry->enmDir == PDMAUDIODIR_IN)
                hrc = pIAudioClient->GetService(__uuidof(IAudioCaptureClient), (void **)&pDevCfg->pIAudioCaptureClient);
            else
                hrc = pIAudioClient->GetService(__uuidof(IAudioRenderClient), (void **)&pDevCfg->pIAudioRenderClient);
            Log8Func(("GetService -> %Rhrc + %p\n", hrc, pDevEntry->enmDir == PDMAUDIODIR_IN
                      ? (void *)pDevCfg->pIAudioCaptureClient : (void *)pDevCfg->pIAudioRenderClient));
            if (SUCCEEDED(hrc))
            {
                /*
                 * Obtain the actual stream format and buffer config.
                 */
                UINT32          cFramesBufferSize       = 0;
                REFERENCE_TIME  cDefaultPeriodInNtTicks = 0;
                REFERENCE_TIME  cMinimumPeriodInNtTicks = 0;
                REFERENCE_TIME  cLatencyinNtTicks       = 0;
                hrc = pIAudioClient->GetBufferSize(&cFramesBufferSize);
                if (SUCCEEDED(hrc))
                {
                    hrc = pIAudioClient->GetDevicePeriod(&cDefaultPeriodInNtTicks, &cMinimumPeriodInNtTicks);
                    if (SUCCEEDED(hrc))
                    {
                        hrc = pIAudioClient->GetStreamLatency(&cLatencyinNtTicks);
                        if (SUCCEEDED(hrc))
                        {
                            LogRel2(("WasAPI: Aquired buffer parameters for %s:\n"
                                     "WasAPI:   cFramesBufferSize       = %RU32\n"
                                     "WasAPI:   cDefaultPeriodInNtTicks = %RI64\n"
                                     "WasAPI:   cMinimumPeriodInNtTicks = %RI64\n"
                                     "WasAPI:   cLatencyinNtTicks       = %RI64\n",
                                     pDevCfg->szProps, cFramesBufferSize, cDefaultPeriodInNtTicks,
                                     cMinimumPeriodInNtTicks, cLatencyinNtTicks));

                            pDevCfg->pIAudioClient      = pIAudioClient;
                            pDevCfg->cFramesBufferSize  = cFramesBufferSize;
                            pDevCfg->cFramesPeriod      = PDMAudioPropsNanoToFrames(&pDevCfg->Props,
                                                                                    cDefaultPeriodInNtTicks * 100);
                            pDevCfg->nsInited           = RTTimeNanoTS();
                            pDevCfg->nsLastUsed         = pDevCfg->nsInited;
                            pDevCfg->rcSetup            = VINF_SUCCESS;

                            if (pClosestMatch)
                                CoTaskMemFree(pClosestMatch);
                            Log8Func(("returns VINF_SUCCESS (%p (%s) inited in %'RU64 ns)\n",
                                      pDevCfg, pDevCfg->szProps, pDevCfg->nsInited - pDevCfg->nsCreated));
                            return VINF_SUCCESS;
                        }
                        LogRelMax(64, ("WasAPI: GetStreamLatency failed: %Rhrc\n", hrc));
                    }
                    else
                        LogRelMax(64, ("WasAPI: GetDevicePeriod failed: %Rhrc\n", hrc));
                }
                else
                    LogRelMax(64, ("WasAPI: GetBufferSize failed: %Rhrc\n", hrc));

                if (pDevCfg->pIAudioCaptureClient)
                {
                    pDevCfg->pIAudioCaptureClient->Release();
                    pDevCfg->pIAudioCaptureClient = NULL;
                }

                if (pDevCfg->pIAudioRenderClient)
                {
                    pDevCfg->pIAudioRenderClient->Release();
                    pDevCfg->pIAudioRenderClient = NULL;
                }
            }
            else
                LogRelMax(64, ("WasAPI: IAudioClient::GetService(%s) failed: %Rhrc\n", pDevCfg->szProps, hrc));
        }
        else
            LogRelMax(64, ("WasAPI: IAudioClient::Initialize(%s) failed: %Rhrc\n", pDevCfg->szProps, hrc));
    }
    else
        LogRelMax(64,("WasAPI: IAudioClient::IsFormatSupported(,%s,) failed: %Rhrc\n", pDevCfg->szProps, hrc));

    pIAudioClient->Release();
    if (pClosestMatch)
        CoTaskMemFree(pClosestMatch);
    pDevCfg->nsInited   = RTTimeNanoTS();
    pDevCfg->nsLastUsed = 0;
    Log8Func(("returns VERR_AUDIO_STREAM_COULD_NOT_CREATE (inited in %'RU64 ns)\n", pDevCfg->nsInited - pDevCfg->nsCreated));
    return pDevCfg->rcSetup = VERR_AUDIO_STREAM_COULD_NOT_CREATE;
}


/**
 * Worker for drvHostAudioWasCacheLookupOrCreate.
 *
 * If lookup fails, a new entry will be created.
 *
 * @note    Called holding the cache's lock (if \a fUseCache is \c true), returning without holding it!
 */
static int drvHostAudioWasCacheLookupOrCreateConfig(PDRVHOSTAUDIOWAS pThis, PDRVHOSTAUDIOWASCACHEDEV pDevEntry,
                                                    PCPDMAUDIOSTREAMCFG pCfgReq, bool fUseCache, bool fOnWorker,
                                                    PDRVHOSTAUDIOWASCACHEDEVCFG *ppDevCfg)
{
    Assert(!pDevEntry->fStale);
    PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfg;

    /*
     * Check if we've got a matching config.
     */
    if (fUseCache)
    {
        pDevCfg = drvHostAudioWasCacheLookupLocked(pDevEntry, &pCfgReq->Props);
        if (pDevCfg)
        {
            *ppDevCfg = pDevCfg;
            RTCritSectLeave(&pThis->CritSectCache);
            Log8Func(("Config cache hit '%s' on '%ls': %p\n", pDevCfg->szProps, pDevEntry->wszDevId, pDevCfg));
            return VINF_SUCCESS;
        }

        RTCritSectLeave(&pThis->CritSectCache);
    }

    /*
     * Allocate an device config entry and hand the creation task over to the
     * worker thread, unless we're already on it.
     */
    pDevCfg = (PDRVHOSTAUDIOWASCACHEDEVCFG)RTMemAllocZ(sizeof(*pDevCfg));
    AssertPtrReturn(pDevCfg, VERR_NO_MEMORY);
    RTListInit(&pDevCfg->ListEntry);
    pDevCfg->pDevEntry         = pDevEntry;
    pDevCfg->rcSetup           = VERR_AUDIO_STREAM_INIT_IN_PROGRESS;
    pDevCfg->Props             = pCfgReq->Props;
    pDevCfg->cFramesBufferSize = pCfgReq->Backend.cFramesBufferSize;
    PDMAudioPropsToString(&pDevCfg->Props, pDevCfg->szProps, sizeof(pDevCfg->szProps));
    pDevCfg->nsCreated         = RTTimeNanoTS();
    pDevCfg->nsLastUsed        = pDevCfg->nsCreated;

    if (fUseCache)
    {
        uint32_t cCacheEntries;
        if (pDevCfg->pDevEntry->enmDir == PDMAUDIODIR_IN)
            cCacheEntries = ASMAtomicIncU32(&pThis->cCacheEntriesIn);
        else
            cCacheEntries = ASMAtomicIncU32(&pThis->cCacheEntriesOut);
        if (cCacheEntries > VBOX_WASAPI_MAX_TOTAL_CONFIG_ENTRIES)
        {
            LogFlowFunc(("Trigger cache pruning.\n"));
            int rc2 = pThis->pIHostAudioPort->pfnDoOnWorkerThread(pThis->pIHostAudioPort, NULL /*pStream*/,
                                                                  DRVHOSTAUDIOWAS_DO_PRUNE_CACHE, NULL /*pvUser*/);
            AssertRCStmt(rc2, drvHostAudioWasCachePrune(pThis));
        }
    }

    if (!fOnWorker)
    {
        *ppDevCfg = pDevCfg;
        LogFlowFunc(("Doing the rest of the work on %p via pfnStreamInitAsync...\n", pDevCfg));
        return VINF_AUDIO_STREAM_ASYNC_INIT_NEEDED;
    }

    /*
     * Initialize the entry on the calling thread.
     */
    int rc = drvHostAudioWasCacheInitConfig(pDevCfg);
    AssertRC(pDevCfg->rcSetup == rc);
    if (RT_SUCCESS(rc))
        rc = pDevCfg->rcSetup; /* paranoia */
    if (RT_SUCCESS(rc))
    {
        *ppDevCfg = pDevCfg;
        LogFlowFunc(("Returning %p\n", pDevCfg));
        return VINF_SUCCESS;
    }
    RTMemFree(pDevCfg);
    *ppDevCfg = NULL;
    return rc;
}


/**
 * Invalidates device cache entry configurations.
 *
 * @param   pThis       The WASAPI host audio driver instance data.
 * @param   pDevEntry   The device entry to invalidate.
 */
static void drvHostAudioWasCacheInvalidateDevEntryConfig(PDRVHOSTAUDIOWAS pThis, PDRVHOSTAUDIOWASCACHEDEV pDevEntry)
{
    RT_NOREF(pThis);

    LogRelFunc(("Invalidating cache entry configurations: %p - '%ls'\n", pDevEntry, pDevEntry->wszDevId));

    PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfg, pDevCfgNext;
    RTListForEachSafe(&pDevEntry->ConfigList, pDevCfg, pDevCfgNext, DRVHOSTAUDIOWASCACHEDEVCFG, ListEntry)
    {
        if (pDevCfg->pIAudioClient)
        {
            pDevCfg->pIAudioClient->Reset();
            pDevCfg->pIAudioClient->Release();
            pDevCfg->pIAudioClient = NULL;

        }

        if (pDevCfg->pIAudioRenderClient)
        {
            pDevCfg->pIAudioRenderClient->Release();
            pDevCfg->pIAudioRenderClient = NULL;
        }

        pDevCfg->rcSetup = VERR_AUDIO_STREAM_INIT_IN_PROGRESS;
    }
}


/**
 * Looks up the given device + config combo in the cache, creating a new entry
 * if missing.
 *
 * @returns VBox status code.
 * @retval  VINF_AUDIO_STREAM_ASYNC_INIT_NEEDED if @a fOnWorker is @c false and
 *          we created a new entry that needs initalization by calling
 *          drvHostAudioWasCacheInitConfig() on it.
 * @param   pThis       The WASAPI host audio driver instance data.
 * @param   pIDevice    The device to look up.
 * @param   pCfgReq     The configuration to look up.
 * @param   fOnWorker   Set if we're on a worker thread, otherwise false.  When
 *                      set to @c true, VINF_AUDIO_STREAM_ASYNC_INIT_NEEDED will
 *                      not be returned and a new entry will be fully
 *                      initialized before returning.
 * @param   ppDevCfg    Where to return the requested device config.
 */
static int drvHostAudioWasCacheLookupOrCreate(PDRVHOSTAUDIOWAS pThis, IMMDevice *pIDevice, PCPDMAUDIOSTREAMCFG pCfgReq,
                                              bool fOnWorker, PDRVHOSTAUDIOWASCACHEDEVCFG *ppDevCfg)
{
    *ppDevCfg = NULL;

    LogRel(("WasAPI: Looking up or creating cache entry (caching is set to %s, iface %p, %s init)\n",
            pThis->fCacheEnabled ? "enabled" : "disabled", pIDevice, fOnWorker ? "on worker" : "async"));

    /*
     * Get the device ID so we can perform the lookup.
     */
    LPWSTR  pwszDevId = NULL;
    HRESULT hrc = pIDevice->GetId(&pwszDevId);
    if (SUCCEEDED(hrc))
    {
        bool fLookupOrCreateInCache = pThis->fCacheEnabled; /* Whether to lookup and/or create the device in our cache. */

        /* First, try retrieving the current device state of the passed device here as a yet another meassure for
         * AUDCLNT_E_DEVICE_INVALIDATED errors. We try not to cache malfunctioning devices here.  See @bugref{10844} */
        DWORD dwState;
        hrc = pIDevice->GetState(&dwState);
        if (SUCCEEDED(hrc))
        {
            if (dwState != DEVICE_STATE_ACTIVE)
            {
                LogRel2(("WasAPI: Device '%ls': Is in non-active state (state is %#x, iface %p)\n",
                         pwszDevId, drvHostAudioWasMMDeviceStateToString(dwState), pIDevice));
                fLookupOrCreateInCache = false;
            }
        }
        else
        {
            LogRel2(("WasAPI: Device '%ls': Unable to retrieve state (hr=%#x, iface %p)\n", pwszDevId, hrc, pIDevice));
            fLookupOrCreateInCache = false;
        }

        PDRVHOSTAUDIOWASCACHEDEV pDevEntry = NULL;
        size_t const             cwcDevId  = RTUtf16Len(pwszDevId);

        if (fLookupOrCreateInCache)
        {
            LogRel2(("WasAPI: Checking for cached device '%ls' ...\n", pwszDevId));

#define DEVICE_STALE_OR_INVALID(a_LogRel2What) \
    do { \
        LogRel2(a_LogRel2What); \
        LogRel(("WasAPI: Stale or invalid audio interface '%ls' detected!\n", pDevEntry->wszDevId)); \
        pDevEntry->fStale = true; \
    } while (0)
            /*
             * The cache has two levels, so first the device entry.
             */
            PDRVHOSTAUDIOWASCACHEDEV pDevEntryNext;
            RTCritSectEnter(&pThis->CritSectCache);
            RTListForEachSafe(&pThis->CacheHead, pDevEntry, pDevEntryNext, DRVHOSTAUDIOWASCACHEDEV, ListEntry)
            {
                if (   pDevEntry->cwcDevId == cwcDevId
                    && pDevEntry->enmDir   == pCfgReq->enmDir
                    && !pDevEntry->fStale
                    && memcmp(pDevEntry->wszDevId, pwszDevId, cwcDevId * sizeof(RTUTF16)) == 0)
                {
                    /*
                     * Cache hit.
                     *
                     * First we need to check if the device interface we want to look up
                     * actually matches the one we have in the cache entry.  If it doesn't,
                     * mark it as stale and continue looking. (We'll probably end up adding a
                     * new device entry to the cache with the new interface below.)
                     *
                     * This is needed when switching audio interfaces and the device interface
                     * becomes invalid via AUDCLNT_E_DEVICE_INVALIDATED.  See @bugref{10503}.
                     */
                    if (pDevEntry->pIDevice != pIDevice)
                        DEVICE_STALE_OR_INVALID(("WasAPI: Cache hit for device '%ls': Stale interface detected (new: %p, old: %p)\n",
                                                 pDevEntry->wszDevId, pIDevice, pDevEntry->pIDevice));
                    else
                    {
                        /*
                         * Second, we need to check if the cached device interface is in a
                         * working (active) shape.
                         */
#if 0 /* However we've already checked this above, outside the loop and the pDevEntry->pIDevice == pIDevice! */
                        hrc = pDevEntry->pIDevice->GetState(&dwState);
                        if (SUCCEEDED(hrc))
                        {
                            if (dwState != DEVICE_STATE_ACTIVE)
                                DEVICE_STALE_OR_INVALID(("WasAPI: Cache hit for device '%ls': Is in non-active state (state is %s)\n",
                                                         pDevEntry->wszDevId, drvHostAudioWasMMDeviceStateToString(dwState)));
                        }
                        else
                            DEVICE_STALE_OR_INVALID(("WasAPI: Cache hit for device '%ls': Unable to retrieve state (hr=%#x)\n",
                                                     pDevEntry->wszDevId, hrc));
#endif
                    }

                    LogRelFunc(("WasAPI: fOnWorker=%RTbool, fStaleDevice=%RTbool\n", fOnWorker, pDevEntry->fStale));

                    if (!pDevEntry->fStale)
                    {
                        LogRel2(("WasAPI: Cache hit for device '%ls' (iface %p)\n", pwszDevId, pIDevice));

                        CoTaskMemFree(pwszDevId);
                        pwszDevId = NULL;

                        return drvHostAudioWasCacheLookupOrCreateConfig(pThis, pDevEntry, pCfgReq,
                                                                        true /* fUseCache */, fOnWorker, ppDevCfg);
                    }
                }
            }
            RTCritSectLeave(&pThis->CritSectCache);

            if (!pDevEntry)
                LogRel2(("WasAPI: Cache miss for device '%ls' (iface %p)\n", pwszDevId, pIDevice));

#undef DEVICE_STALE_OR_INVALID
        }

        /* Note: An active device marked as being stale will be removed from the cache
                 after the device switch has been completed. */

        /*
         * Device not in the cache (anymore), (re-)add it.
         */
        pDevEntry = (PDRVHOSTAUDIOWASCACHEDEV)RTMemAllocZVar(RT_UOFFSETOF_DYN(DRVHOSTAUDIOWASCACHEDEV, wszDevId[cwcDevId + 1]));
        if (pDevEntry)
        {
            if (pThis->fCacheEnabled)
                pIDevice->AddRef();
            pDevEntry->pIDevice                   = pIDevice;
            pDevEntry->enmDir                     = pCfgReq->enmDir;
            pDevEntry->cwcDevId                   = cwcDevId;
#if 0
            pDevEntry->fSupportsAutoConvertPcm    = -1;
            pDevEntry->fSupportsSrcDefaultQuality = -1;
#endif
            RTListInit(&pDevEntry->ConfigList);
            memcpy(pDevEntry->wszDevId, pwszDevId, cwcDevId * sizeof(RTUTF16));
            pDevEntry->wszDevId[cwcDevId] = '\0';

            CoTaskMemFree(pwszDevId);
            pwszDevId = NULL;

            if (fLookupOrCreateInCache)
            {
                /* Make sure to enter the cache's critsect again for the following calls. */
                RTCritSectEnter(&pThis->CritSectCache);

                /*
                 * Before adding the device to the cache, check that someone didn't race us adding it.
                 */
                PDRVHOSTAUDIOWASCACHEDEV pDevEntry2;
                RTListForEach(&pThis->CacheHead, pDevEntry2, DRVHOSTAUDIOWASCACHEDEV, ListEntry)
                {
                    if (   pDevEntry2->cwcDevId == cwcDevId
                        && !pDevEntry->fStale
                        /* Note: We have to compare the device interface here as well, as a cached device entry might
                           have a stale audio interface for the same device. In such a case a new device entry will be created below.
                           Update: This predates the fStale flag and is just kept for safety. */
                        && pDevEntry2->pIDevice == pIDevice
                        && pDevEntry2->enmDir   == pCfgReq->enmDir
                        && memcmp(pDevEntry2->wszDevId, pDevEntry->wszDevId, cwcDevId * sizeof(RTUTF16)) == 0)
                    {
                        pIDevice->Release();
                        RTMemFree(pDevEntry);
                        pDevEntry = NULL;

                        LogRel2(("WasAPI: Lost race adding device '%ls' (node %p)\n", pDevEntry2->wszDevId, pDevEntry2));
                        return drvHostAudioWasCacheLookupOrCreateConfig(pThis, pDevEntry2, pCfgReq,
                                                                        true /* fUseCache */, fOnWorker, ppDevCfg);
                    }
                }
                RTListPrepend(&pThis->CacheHead, &pDevEntry->ListEntry);

                LogRel2(("WasAPI: Added device '%ls' to cache (node %p)\n", pDevEntry->wszDevId, pDevEntry));
            }
            else
                LogRel2(("WasAPI: Not caching device '%ls' (iface %p)\n", pDevEntry->wszDevId, pIDevice));

            return drvHostAudioWasCacheLookupOrCreateConfig(pThis, pDevEntry, pCfgReq,
                                                            fLookupOrCreateInCache /* fUseCache */, fOnWorker, ppDevCfg);
        }
        CoTaskMemFree(pwszDevId);
    }
    else
        LogRelMax(64, ("WasAPI: GetId failed (lookup): %Rhrc\n", hrc));
    return VERR_AUDIO_STREAM_COULD_NOT_CREATE;
}


/**
 * Return the given config to the cache.
 *
 * @param   pThis       The WASAPI host audio driver instance data.
 * @param   pDevCfg     The device config to put back.
 */
static void drvHostAudioWasCachePutBack(PDRVHOSTAUDIOWAS pThis, PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfg)
{
    /*
     * Reset the audio client to see that it works and to make sure it's in a sensible state.
     */
    HRESULT hrc = pDevCfg->pIAudioClient ? pDevCfg->pIAudioClient->Reset()
                : pDevCfg->rcSetup == VERR_AUDIO_STREAM_INIT_IN_PROGRESS ? S_OK : E_FAIL;
    if (SUCCEEDED(hrc))
    {
        if (pThis->fCacheEnabled)
        {
            Log8Func(("Putting %p/'%s' back\n", pDevCfg, pDevCfg->szProps));
            RTCritSectEnter(&pThis->CritSectCache);
            RTListAppend(&pDevCfg->pDevEntry->ConfigList, &pDevCfg->ListEntry);
            uint32_t const cEntries = pDevCfg->pDevEntry->enmDir == PDMAUDIODIR_IN ? pThis->cCacheEntriesIn : pThis->cCacheEntriesOut;
            RTCritSectLeave(&pThis->CritSectCache);

            /* Trigger pruning if we're over the threshold. */
            if (cEntries > VBOX_WASAPI_MAX_TOTAL_CONFIG_ENTRIES)
            {
                LogFlowFunc(("Trigger cache pruning.\n"));
                int rc2 = pThis->pIHostAudioPort->pfnDoOnWorkerThread(pThis->pIHostAudioPort, NULL /*pStream*/,
                                                                      DRVHOSTAUDIOWAS_DO_PRUNE_CACHE, NULL /*pvUser*/);
                AssertRCStmt(rc2, drvHostAudioWasCachePrune(pThis));
            }
        }
    }
    else
    {
        Log8Func(("IAudioClient::Reset failed (%Rhrc) on %p/'%s', destroying it.\n", hrc, pDevCfg, pDevCfg->szProps));
        drvHostAudioWasCacheDestroyDevConfig(pThis, pDevCfg);
    }
}


static void drvHostWasCacheConfigHinting(PDRVHOSTAUDIOWAS pThis, PPDMAUDIOSTREAMCFG pCfgReq, bool fOnWorker)
{
    /*
     * Get the device.
     */
    pThis->pNotifyClient->lockEnter();
    IMMDevice *pIDevice = pCfgReq->enmDir == PDMAUDIODIR_IN ? pThis->pIDeviceInput : pThis->pIDeviceOutput;
    if (pIDevice)
        pIDevice->AddRef();
    pThis->pNotifyClient->lockLeave();
    if (pIDevice)
    {
        /*
         * Look up the config and put it back.
         */
        PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfg = NULL;
        int rc = drvHostAudioWasCacheLookupOrCreate(pThis, pIDevice, pCfgReq, fOnWorker, &pDevCfg);
        LogFlowFunc(("pDevCfg=%p rc=%Rrc\n", pDevCfg, rc));
        if (pDevCfg && RT_SUCCESS(rc))
            drvHostAudioWasCachePutBack(pThis, pDevCfg);
        pIDevice->Release();
    }
}


/**
 * Prefills the cache.
 *
 * @param   pThis       The WASAPI host audio driver instance data.
 */
static void drvHostAudioWasCacheFill(PDRVHOSTAUDIOWAS pThis)
{
#if 0 /* we don't have the buffer config nor do we really know which frequences to expect */
    Log8Func(("enter\n"));
    struct
    {
        PCRTUTF16       pwszDevId;
        PDMAUDIODIR     enmDir;
    } aToCache[] =
    {
        { pThis->pwszInputDevId, PDMAUDIODIR_IN },
        { pThis->pwszOutputDevId, PDMAUDIODIR_OUT }
    };
    for (unsigned i = 0; i < RT_ELEMENTS(aToCache); i++)
    {
        PCRTUTF16       pwszDevId = aToCache[i].pwszDevId;
        IMMDevice      *pIDevice  = NULL;
        HRESULT         hrc;
        if (pwszDevId)
            hrc = pThis->pIEnumerator->GetDevice(pwszDevId, &pIDevice);
        else
        {
            hrc = pThis->pIEnumerator->GetDefaultAudioEndpoint(aToCache[i].enmDir == PDMAUDIODIR_IN ? eCapture : eRender,
                                                               eMultimedia, &pIDevice);
            pwszDevId = aToCache[i].enmDir == PDMAUDIODIR_IN ? L"{Default-In}" : L"{Default-Out}";
        }
        if (SUCCEEDED(hrc))
        {
            PDMAUDIOSTREAMCFG Cfg = { aToCache[i].enmDir, { PDMAUDIOPLAYBACKDST_INVALID },
                                      PDMAUDIOPCMPROPS_INITIALIZER(2, true, 2, 44100, false) };
            Cfg.Backend.cFramesBufferSize = PDMAudioPropsMilliToFrames(&Cfg.Props, 300);
            PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfg = drvHostAudioWasCacheLookupOrCreate(pThis, pIDevice, &Cfg);
            if (pDevCfg)
                drvHostAudioWasCachePutBack(pThis, pDevCfg);

            pIDevice->Release();
        }
        else
            LogRelMax(64, ("WasAPI: Failed to open audio device '%ls' (pre-caching): %Rhrc\n", pwszDevId, hrc));
    }
    Log8Func(("leave\n"));
#else
    RT_NOREF(pThis);
#endif
}


/*********************************************************************************************************************************
*   Worker thread                                                                                                                *
*********************************************************************************************************************************/
#if 0

/**
 * @callback_method_impl{FNRTTHREAD,
 * Asynchronous thread for setting up audio client configs.}
 */
static DECLCALLBACK(int) drvHostWasWorkerThread(RTTHREAD hThreadSelf, void *pvUser)
{
    PDRVHOSTAUDIOWAS pThis = (PDRVHOSTAUDIOWAS)pvUser;

    /*
     * We need to set the thread ID so others can post us thread messages.
     * And before we signal that we're ready, make sure we've got a message queue.
     */
    pThis->idWorkerThread = GetCurrentThreadId();
    LogFunc(("idWorkerThread=%#x (%u)\n", pThis->idWorkerThread, pThis->idWorkerThread));

    MSG Msg;
    PeekMessageW(&Msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

    int rc = RTThreadUserSignal(hThreadSelf);
    AssertRC(rc);

    /*
     * Message loop.
     */
    BOOL fRet;
    while ((fRet = GetMessageW(&Msg, NULL, 0, 0)) != FALSE)
    {
        if (fRet != -1)
        {
            TranslateMessage(&Msg);
            Log9Func(("Msg: time=%u: msg=%#x l=%p w=%p for hwnd=%p\n", Msg.time, Msg.message, Msg.lParam, Msg.wParam, Msg.hwnd));
            switch (Msg.message)
            {
                case WM_DRVHOSTAUDIOWAS_PURGE_CACHE:
                {
                    AssertMsgBreak(Msg.wParam == pThis->uWorkerThreadFixedParam, ("%p\n", Msg.wParam));
                    AssertBreak(Msg.hwnd == NULL);
                    AssertBreak(Msg.lParam == 0);

                    drvHostAudioWasCachePurge(pThis, false /*fOnWorker*/);
                    break;
                }

                default:
                    break;
            }
            DispatchMessageW(&Msg);
        }
        else
            AssertMsgFailed(("GetLastError()=%u\n", GetLastError()));
    }

    LogFlowFunc(("Pre-quit cache purge...\n"));
    drvHostAudioWasCachePurge(pThis, false /*fOnWorker*/);

    LogFunc(("Quits\n"));
    return VINF_SUCCESS;
}
#endif


/*********************************************************************************************************************************
*   PDMIHOSTAUDIO                                                                                                                *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetConfig}
 */
static DECLCALLBACK(int) drvHostAudioWasHA_GetConfig(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDCFG pBackendCfg)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pInterface,  VERR_INVALID_POINTER);
    AssertPtrReturn(pBackendCfg, VERR_INVALID_POINTER);


    /*
     * Fill in the config structure.
     */
    RTStrCopy(pBackendCfg->szName, sizeof(pBackendCfg->szName), "WasAPI");
    pBackendCfg->cbStream       = sizeof(DRVHOSTAUDIOWASSTREAM);
    pBackendCfg->fFlags         = PDMAUDIOBACKEND_F_ASYNC_HINT;
    pBackendCfg->cMaxStreamsIn  = UINT32_MAX;
    pBackendCfg->cMaxStreamsOut = UINT32_MAX;

    return VINF_SUCCESS;
}


/**
 * Queries information for @a pDevice and adds an entry to the enumeration.
 *
 * @returns VBox status code.
 * @param   pDevEnm     The enumeration to add the device to.
 * @param   pIDevice    The device.
 * @param   enmType     The type of device.
 * @param   fDefault    Whether it's the default device.
 */
static int drvHostWasEnumAddDev(PPDMAUDIOHOSTENUM pDevEnm, IMMDevice *pIDevice, EDataFlow enmType, bool fDefault)
{
    int rc = VINF_SUCCESS; /* ignore most errors */
    RT_NOREF(fDefault); /** @todo default device marking/skipping. */

    /*
     * Gather the necessary properties.
     */
    IPropertyStore *pProperties = NULL;
    HRESULT hrc = pIDevice->OpenPropertyStore(STGM_READ, &pProperties);
    if (SUCCEEDED(hrc))
    {
        /* Get the friendly name (string). */
        PROPVARIANT VarName;
        PropVariantInit(&VarName);
        hrc = pProperties->GetValue(PKEY_Device_FriendlyName, &VarName);
        if (SUCCEEDED(hrc))
        {
            /* Get the device ID (string). */
            LPWSTR pwszDevId = NULL;
            hrc = pIDevice->GetId(&pwszDevId);
            if (SUCCEEDED(hrc))
            {
                size_t const cwcDevId = RTUtf16Len(pwszDevId);

                /* Get the device format (blob). */
                PROPVARIANT VarFormat;
                PropVariantInit(&VarFormat);
                hrc = pProperties->GetValue(PKEY_AudioEngine_DeviceFormat, &VarFormat);
                if (SUCCEEDED(hrc))
                {
                    WAVEFORMATEX const * const pFormat = (WAVEFORMATEX const *)VarFormat.blob.pBlobData;
                    AssertPtr(pFormat); /* Observed sometimes being NULL on windows 7 sp1. */

                    /*
                     * Create a enumeration entry for it.
                     */
                    size_t const cbId   = RTUtf16CalcUtf8Len(pwszDevId) + 1;
                    size_t const cbName = RTUtf16CalcUtf8Len(VarName.pwszVal) + 1;
                    size_t const cbDev  = RT_ALIGN_Z(  RT_OFFSETOF(DRVHOSTAUDIOWASDEV, wszDevId)
                                                     + (cwcDevId + 1) * sizeof(RTUTF16),
                                                     64);
                    PDRVHOSTAUDIOWASDEV pDev = (PDRVHOSTAUDIOWASDEV)PDMAudioHostDevAlloc(cbDev, cbName, cbId);
                    if (pDev)
                    {
                        pDev->Core.enmType    = PDMAUDIODEVICETYPE_BUILTIN;
                        pDev->Core.enmUsage   = enmType == eRender ? PDMAUDIODIR_OUT : PDMAUDIODIR_IN;
                        if (fDefault)
                            pDev->Core.fFlags = enmType == eRender ? PDMAUDIOHOSTDEV_F_DEFAULT_OUT : PDMAUDIOHOSTDEV_F_DEFAULT_IN;
                        if (enmType == eRender)
                            pDev->Core.cMaxOutputChannels = RT_VALID_PTR(pFormat) ? pFormat->nChannels : 2;
                        else
                            pDev->Core.cMaxInputChannels  = RT_VALID_PTR(pFormat) ? pFormat->nChannels : 1;

                        memcpy(pDev->wszDevId, pwszDevId, cwcDevId * sizeof(RTUTF16));
                        pDev->wszDevId[cwcDevId] = '\0';

                        Assert(pDev->Core.pszName);
                        rc = RTUtf16ToUtf8Ex(VarName.pwszVal, RTSTR_MAX, &pDev->Core.pszName, cbName, NULL);
                        if (RT_SUCCESS(rc))
                        {
                            Assert(pDev->Core.pszId);
                            rc = RTUtf16ToUtf8Ex(pDev->wszDevId, RTSTR_MAX, &pDev->Core.pszId, cbId, NULL);
                            if (RT_SUCCESS(rc))
                            {
                                PDMAudioHostEnumAppend(pDevEnm, &pDev->Core);
                                LogRel2(("WasAPI: Device '%ls': %p\n", pDev->wszDevId, pIDevice));
                           }
                            else
                                PDMAudioHostDevFree(&pDev->Core);
                        }
                        else
                            PDMAudioHostDevFree(&pDev->Core);
                    }
                    else
                        rc = VERR_NO_MEMORY;
                    PropVariantClear(&VarFormat);
                }
                else
                    LogFunc(("Failed to get PKEY_AudioEngine_DeviceFormat: %Rhrc\n", hrc));
                CoTaskMemFree(pwszDevId);
            }
            else
                LogFunc(("Failed to get the device ID: %Rhrc\n", hrc));
            PropVariantClear(&VarName);
        }
        else
            LogFunc(("Failed to get PKEY_Device_FriendlyName: %Rhrc\n", hrc));
        pProperties->Release();
    }
    else
        LogFunc(("OpenPropertyStore failed: %Rhrc\n", hrc));

    if (hrc == E_OUTOFMEMORY && RT_SUCCESS_NP(rc))
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Does a (Re-)enumeration of the host's playback + capturing devices.
 *
 * @return  VBox status code.
 * @param   pThis       The WASAPI host audio driver instance data.
 * @param   pDevEnm     Where to store the enumerated devices.
 */
static int drvHostWasEnumerateDevices(PDRVHOSTAUDIOWAS pThis, PPDMAUDIOHOSTENUM pDevEnm)
{
    LogRel2(("WasAPI: Enumerating devices ...\n"));

    int rc = VINF_SUCCESS;
    for (unsigned idxPass = 0; idxPass < 2 && RT_SUCCESS(rc); idxPass++)
    {
        EDataFlow const enmType = idxPass == 0 ? EDataFlow::eRender : EDataFlow::eCapture;

        /* Get the default device first. */
        IMMDevice *pIDefaultDevice = NULL;
        HRESULT hrc = pThis->pIEnumerator->GetDefaultAudioEndpoint(enmType, eMultimedia, &pIDefaultDevice);
        if (SUCCEEDED(hrc))
            rc = drvHostWasEnumAddDev(pDevEnm, pIDefaultDevice, enmType, true);
        else
            pIDefaultDevice = NULL;

        /* Enumerate the devices. */
        IMMDeviceCollection *pCollection = NULL;
        hrc = pThis->pIEnumerator->EnumAudioEndpoints(enmType, DEVICE_STATE_ACTIVE /*| DEVICE_STATE_UNPLUGGED?*/, &pCollection);
        if (SUCCEEDED(hrc) && pCollection != NULL)
        {
            UINT cDevices = 0;
            hrc = pCollection->GetCount(&cDevices);
            if (SUCCEEDED(hrc))
            {
                for (UINT idxDevice = 0; idxDevice < cDevices && RT_SUCCESS(rc); idxDevice++)
                {
                    IMMDevice *pIDevice = NULL;
                    hrc = pCollection->Item(idxDevice, &pIDevice);
                    if (SUCCEEDED(hrc) && pIDevice)
                    {
                        if (pIDevice != pIDefaultDevice)
                            rc = drvHostWasEnumAddDev(pDevEnm, pIDevice, enmType, false);
                        pIDevice->Release();
                    }
                }
            }
            pCollection->Release();
        }
        else
            LogRelMax(10, ("EnumAudioEndpoints(%s) failed: %Rhrc\n", idxPass == 0 ? "output" : "input", hrc));

        if (pIDefaultDevice)
            pIDefaultDevice->Release();
    }

    LogRel2(("WasAPI: Enumerating devices done - %u devices (%Rrc)\n", pDevEnm->cDevices, rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetDevices}
 */
static DECLCALLBACK(int) drvHostAudioWasHA_GetDevices(PPDMIHOSTAUDIO pInterface, PPDMAUDIOHOSTENUM pDeviceEnum)
{
    PDRVHOSTAUDIOWAS pThis = RT_FROM_MEMBER(pInterface, DRVHOSTAUDIOWAS, IHostAudio);
    AssertPtrReturn(pDeviceEnum, VERR_INVALID_POINTER);

    PDMAudioHostEnumInit(pDeviceEnum);
    int rc = drvHostWasEnumerateDevices(pThis, pDeviceEnum);
    if (RT_FAILURE(rc))
        PDMAudioHostEnumDelete(pDeviceEnum);

    LogFlowFunc(("Returning %Rrc\n", rc));
    return rc;
}


/**
 * Worker for drvHostAudioWasHA_SetDevice.
 */
static int drvHostAudioWasSetDeviceWorker(PDRVHOSTAUDIOWAS pThis, const char *pszId, PRTUTF16 *ppwszDevId, IMMDevice **ppIDevice,
                                          EDataFlow enmFlow, PDMAUDIODIR enmDir, const char *pszWhat)
{
    pThis->pNotifyClient->lockEnter();

    /*
     * Did anything actually change?
     */
    if (    (pszId == NULL) != (*ppwszDevId == NULL)
        || (   pszId
            && RTUtf16ICmpUtf8(*ppwszDevId, pszId) != 0))
    {
        /*
         * Duplicate the ID.
         */
        PRTUTF16 pwszDevId = NULL;
        if (pszId)
        {
            int rc = RTStrToUtf16(pszId, &pwszDevId);
            AssertRCReturnStmt(rc, pThis->pNotifyClient->lockLeave(), rc);
        }

        /*
         * Try get the device.
         */
        IMMDevice *pIDevice = NULL;
        HRESULT    hrc;
        if (pwszDevId)
            hrc = pThis->pIEnumerator->GetDevice(pwszDevId, &pIDevice);
        else
            hrc = pThis->pIEnumerator->GetDefaultAudioEndpoint(enmFlow, eMultimedia, &pIDevice);
        LogFlowFunc(("Got device %p (%Rhrc)\n", pIDevice, hrc));
        if (FAILED(hrc))
        {
            LogRel(("WasAPI: Failed to get IMMDevice for %s audio device '%s' (SetDevice): %Rhrc\n",
                    pszWhat, pszId ? pszId : "{default}", hrc));
            pIDevice = NULL;
        }

        /*
         * Make the switch.
         */
        LogRel(("PulseAudio: Changing %s device: '%ls' -> '%s'\n",
                pszWhat, *ppwszDevId ? *ppwszDevId : L"{Default}", pszId ? pszId : "{Default}"));

        if (*ppIDevice)
            (*ppIDevice)->Release();
        *ppIDevice = pIDevice;

        RTUtf16Free(*ppwszDevId);
        *ppwszDevId = pwszDevId;

        /*
         * Only notify the driver above us.
         */
        PPDMIHOSTAUDIOPORT const pIHostAudioPort = pThis->pIHostAudioPort;
        pThis->pNotifyClient->lockLeave();

        if (pIHostAudioPort)
        {
            LogFlowFunc(("Notifying parent driver about %s device change...\n", pszWhat));
            pIHostAudioPort->pfnNotifyDeviceChanged(pIHostAudioPort, enmDir, NULL);
        }
    }
    else
    {
        pThis->pNotifyClient->lockLeave();
        LogFunc(("No %s device change\n", pszWhat));
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnSetDevice}
 */
static DECLCALLBACK(int) drvHostAudioWasHA_SetDevice(PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir, const char *pszId)
{
    PDRVHOSTAUDIOWAS pThis = RT_FROM_MEMBER(pInterface, DRVHOSTAUDIOWAS, IHostAudio);

    /*
     * Validate and normalize input.
     */
    AssertReturn(enmDir == PDMAUDIODIR_IN || enmDir == PDMAUDIODIR_OUT || enmDir == PDMAUDIODIR_DUPLEX, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pszId, VERR_INVALID_POINTER);
    if (!pszId || !*pszId)
        pszId = NULL;
    else
        AssertReturn(strlen(pszId) < 1024, VERR_INVALID_NAME);
    LogFunc(("enmDir=%d pszId=%s\n", enmDir, pszId));

    /*
     * Do the updating.
     */
    if (enmDir == PDMAUDIODIR_IN || enmDir == PDMAUDIODIR_DUPLEX)
    {
        int rc = drvHostAudioWasSetDeviceWorker(pThis, pszId, &pThis->pwszInputDevId, &pThis->pIDeviceInput,
                                                eCapture, PDMAUDIODIR_IN, "input");
        AssertRCReturn(rc, rc);
    }

    if (enmDir == PDMAUDIODIR_OUT || enmDir == PDMAUDIODIR_DUPLEX)
    {
        int rc = drvHostAudioWasSetDeviceWorker(pThis, pszId, &pThis->pwszOutputDevId, &pThis->pIDeviceOutput,
                                                eRender, PDMAUDIODIR_OUT, "output");
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetStatus}
 */
static DECLCALLBACK(PDMAUDIOBACKENDSTS) drvHostAudioWasHA_GetStatus(PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir)
{
    RT_NOREF(pInterface, enmDir);
    return PDMAUDIOBACKENDSTS_RUNNING;
}


/**
 * Performs the actual switching of device config.
 *
 * Worker for drvHostAudioWasDoStreamDevSwitch() and
 * drvHostAudioWasHA_StreamNotifyDeviceChanged().
 */
static void drvHostAudioWasCompleteStreamDevSwitch(PDRVHOSTAUDIOWAS pThis, PDRVHOSTAUDIOWASSTREAM pStreamWas,
                                                   PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfg)
{
    RTCritSectEnter(&pStreamWas->CritSect);

    /* Do the switch. */
    PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfgOld = pStreamWas->pDevCfg;
    pStreamWas->pDevCfg = pDevCfg;

    /* The new stream is neither started nor draining. */
    pStreamWas->fStarted         = false;
    pStreamWas->fDraining        = false;

    /* Device switching is done now. */
    pStreamWas->fSwitchingDevice = false;

    /* Stop the old stream or Reset() will fail when putting it back into the cache. */
    if (pStreamWas->fEnabled && pDevCfgOld->pIAudioClient)
        pDevCfgOld->pIAudioClient->Stop();

    RTCritSectLeave(&pStreamWas->CritSect);

    /* Notify DrvAudio. */
    pThis->pIHostAudioPort->pfnStreamNotifyDeviceChanged(pThis->pIHostAudioPort, &pStreamWas->Core, false /*fReInit*/);

    /* Put the old config back into the cache. */
    drvHostAudioWasCachePutBack(pThis, pDevCfgOld);

    AssertPtr(pDevCfgOld->pDevEntry);
    if (pDevCfgOld->pDevEntry->fStale)
    {
        LogRelFunc(("Removing stale device from cache\n"));
        RTListNodeRemove(&pDevCfgOld->pDevEntry->ListEntry);

        drvHostAudioWasCacheInvalidateDevEntryConfig(pThis, pDevCfgOld->pDevEntry);
        drvHostAudioWasCacheDestroyDevEntry(pThis, pDevCfgOld->pDevEntry);
        pDevCfgOld = NULL; /* Got invalid due to drvHostAudioWasCacheDestroyDevEntry() above. */
    }

    LogFlowFunc(("returns with '%s' state: %s\n", pStreamWas->Cfg.szName, drvHostWasStreamStatusString(pStreamWas) ));
}


/**
 * Called on a worker thread to initialize a new device config and switch the
 * given stream to using it.
 *
 * @sa  drvHostAudioWasHA_StreamNotifyDeviceChanged
 */
static void drvHostAudioWasDoStreamDevSwitch(PDRVHOSTAUDIOWAS pThis, PDRVHOSTAUDIOWASSTREAM pStreamWas,
                                             PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfg)
{
    /*
     * Do the initializing.
     */
    int rc = drvHostAudioWasCacheInitConfig(pDevCfg);
    if (RT_SUCCESS(rc))
        drvHostAudioWasCompleteStreamDevSwitch(pThis, pStreamWas, pDevCfg);
    else
    {
        LogRelMax(64, ("WasAPI: Failed to set up new device config '%ls:%s' for stream '%s': %Rrc\n",
                       pDevCfg->pDevEntry->wszDevId, pDevCfg->szProps, pStreamWas->Cfg.szName, rc));
        drvHostAudioWasCacheDestroyDevConfig(pThis, pDevCfg);
        pThis->pIHostAudioPort->pfnStreamNotifyDeviceChanged(pThis->pIHostAudioPort, &pStreamWas->Core, true /*fReInit*/);
    }
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnDoOnWorkerThread}
 */
static DECLCALLBACK(void) drvHostAudioWasHA_DoOnWorkerThread(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                             uintptr_t uUser, void *pvUser)
{
    PDRVHOSTAUDIOWAS pThis = RT_FROM_MEMBER(pInterface, DRVHOSTAUDIOWAS, IHostAudio);
    LogFlowFunc(("uUser=%#zx pStream=%p pvUser=%p\n", uUser, pStream, pvUser));

    switch (uUser)
    {
        case DRVHOSTAUDIOWAS_DO_PURGE_CACHE:
            LogFlowFunc(("DRVHOSTAUDIOWAS_DO_PURGE_CACHE\n"));
            Assert(pStream == NULL);
            Assert(pvUser == NULL);
            Assert(pThis->fCacheEnabled);
            drvHostAudioWasCachePurge(pThis, true /*fOnWorker*/);
            break;

        case DRVHOSTAUDIOWAS_DO_PRUNE_CACHE:
            LogFlowFunc(("DRVHOSTAUDIOWAS_DO_PRUNE_CACHE\n"));
            Assert(pStream == NULL);
            Assert(pvUser == NULL);
            Assert(pThis->fCacheEnabled);
            drvHostAudioWasCachePrune(pThis);
            break;

        case DRVHOSTAUDIOWAS_DO_STREAM_DEV_SWITCH:
            LogFlowFunc(("DRVHOSTAUDIOWAS_DO_STREAM_DEV_SWITCH\n"));
            AssertPtr(pStream);
            AssertPtr(pvUser);
            drvHostAudioWasDoStreamDevSwitch(pThis, (PDRVHOSTAUDIOWASSTREAM)pStream, (PDRVHOSTAUDIOWASCACHEDEVCFG)pvUser);
            break;

        default:
            AssertMsgFailedBreak(("%#zx\n", uUser));
    }
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamConfigHint}
 *
 * @note This is called on a DrvAudio worker thread.
 */
static DECLCALLBACK(void) drvHostAudioWasHA_StreamConfigHint(PPDMIHOSTAUDIO pInterface, PPDMAUDIOSTREAMCFG pCfg)
{
#if 0 /* disable to test async stream creation. */
    PDRVHOSTAUDIOWAS pThis = RT_FROM_MEMBER(pInterface, DRVHOSTAUDIOWAS, IHostAudio);
    LogFlowFunc(("pCfg=%p\n", pCfg));

    drvHostWasCacheConfigHinting(pThis, pCfg);
#else
    RT_NOREF(pInterface, pCfg);
#endif
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCreate}
 */
static DECLCALLBACK(int) drvHostAudioWasHA_StreamCreate(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                        PCPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    PDRVHOSTAUDIOWAS        pThis      = RT_FROM_MEMBER(pInterface, DRVHOSTAUDIOWAS, IHostAudio);
    PDRVHOSTAUDIOWASSTREAM  pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    AssertPtrReturn(pStreamWas, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgReq, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgAcq, VERR_INVALID_POINTER);
    AssertReturn(pCfgReq->enmDir == PDMAUDIODIR_IN || pCfgReq->enmDir == PDMAUDIODIR_OUT, VERR_INVALID_PARAMETER);
    Assert(PDMAudioStrmCfgEquals(pCfgReq, pCfgAcq));

    const char * const pszStreamType = pCfgReq->enmDir == PDMAUDIODIR_IN ? "capture" : "playback"; RT_NOREF(pszStreamType);
    LogFlowFunc(("enmPath=%s '%s'\n", PDMAudioPathGetName(pCfgReq->enmPath), pCfgReq->szName));
#if defined(RTLOG_REL_ENABLED) || defined(LOG_ENABLED)
    char szTmp[64];
#endif
    LogRel2(("WasAPI: Opening %s stream '%s' (%s)\n", pCfgReq->szName, pszStreamType,
             PDMAudioPropsToString(&pCfgReq->Props, szTmp, sizeof(szTmp))));

    RTListInit(&pStreamWas->ListEntry);

    /*
     * Do configuration conversion.
     */
    WAVEFORMATEXTENSIBLE WaveFmtExt;
    drvHostAudioWasWaveFmtExtFromProps(&pCfgReq->Props, &WaveFmtExt);
    LogRel2(("WasAPI: Requested %s format for '%s':\n"
             "WasAPI:   wFormatTag      = %#RX16\n"
             "WasAPI:   nChannels       = %RU16\n"
             "WasAPI:   nSamplesPerSec  = %RU32\n"
             "WasAPI:   nAvgBytesPerSec = %RU32\n"
             "WasAPI:   nBlockAlign     = %RU16\n"
             "WasAPI:   wBitsPerSample  = %RU16\n"
             "WasAPI:   cbSize          = %RU16\n"
             "WasAPI:   cBufferSizeInNtTicks = %RU64\n",
             pszStreamType, pCfgReq->szName, WaveFmtExt.Format.wFormatTag, WaveFmtExt.Format.nChannels,
             WaveFmtExt.Format.nSamplesPerSec, WaveFmtExt.Format.nAvgBytesPerSec, WaveFmtExt.Format.nBlockAlign,
             WaveFmtExt.Format.wBitsPerSample, WaveFmtExt.Format.cbSize,
             PDMAudioPropsFramesToNtTicks(&pCfgReq->Props, pCfgReq->Backend.cFramesBufferSize) ));
    if (WaveFmtExt.Format.cbSize != 0)
        LogRel2(("WasAPI:   dwChannelMask   = %#RX32\n"
                 "WasAPI:   wValidBitsPerSample = %RU16\n",
                 WaveFmtExt.dwChannelMask, WaveFmtExt.Samples.wValidBitsPerSample));

    /* Set up the acquired format here as channel count + layout may have
       changed and need to be communicated to caller and used in cache lookup. */
    *pCfgAcq = *pCfgReq;
    if (WaveFmtExt.Format.cbSize != 0)
    {
        PDMAudioPropsSetChannels(&pCfgAcq->Props, WaveFmtExt.Format.nChannels);
        uint8_t idCh = 0;
        for (unsigned iBit = 0; iBit < 32 && idCh < WaveFmtExt.Format.nChannels; iBit++)
            if (WaveFmtExt.dwChannelMask & RT_BIT_32(iBit))
            {
                pCfgAcq->Props.aidChannels[idCh] = (unsigned)PDMAUDIOCHANNELID_FIRST_STANDARD + iBit;
                idCh++;
            }
        Assert(idCh == WaveFmtExt.Format.nChannels);
    }

    /*
     * Get the device we're supposed to use.
     * (We cache this as it takes ~2ms to get the default device on a random W10 19042 system.)
     */
    pThis->pNotifyClient->lockEnter();
    IMMDevice *pIDevice = pCfgReq->enmDir == PDMAUDIODIR_IN ? pThis->pIDeviceInput : pThis->pIDeviceOutput;
    if (pIDevice)
        pIDevice->AddRef();
    pThis->pNotifyClient->lockLeave();

    PCRTUTF16       pwszDevId     = pCfgReq->enmDir == PDMAUDIODIR_IN ? pThis->pwszInputDevId : pThis->pwszOutputDevId;
    PCRTUTF16 const pwszDevIdDesc = pwszDevId ? pwszDevId : pCfgReq->enmDir == PDMAUDIODIR_IN ? L"{Default-In}" : L"{Default-Out}";
    if (!pIDevice)
    {
        /* This might not strictly be necessary anymore, however it shouldn't
           hurt and may be useful when using specific devices. */
        HRESULT hrc;
        if (pwszDevId)
            hrc = pThis->pIEnumerator->GetDevice(pwszDevId, &pIDevice);
        else
            hrc = pThis->pIEnumerator->GetDefaultAudioEndpoint(pCfgReq->enmDir == PDMAUDIODIR_IN ? eCapture : eRender,
                                                               eMultimedia, &pIDevice);
        LogFlowFunc(("Got device %p (%Rhrc)\n", pIDevice, hrc));
        if (FAILED(hrc))
        {
            LogRelMax(64, ("WasAPI: Failed to open audio %s device '%ls': %Rhrc\n", pszStreamType, pwszDevIdDesc, hrc));
            return VERR_AUDIO_STREAM_COULD_NOT_CREATE;
        }
    }

    /*
     * Ask the cache to retrieve or instantiate the requested configuration.
     */
    /** @todo make it return a status code too and retry if the default device
     *        was invalidated/changed while we where working on it here. */
    PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfg = NULL;
    int rc = drvHostAudioWasCacheLookupOrCreate(pThis, pIDevice, pCfgAcq, false /*fOnWorker*/, &pDevCfg);

    pIDevice->Release();
    pIDevice = NULL;

    if (pDevCfg && RT_SUCCESS(rc))
    {
        pStreamWas->pDevCfg = pDevCfg;

        pCfgAcq->Props                       = pDevCfg->Props;
        pCfgAcq->Backend.cFramesBufferSize   = pDevCfg->cFramesBufferSize;
        pCfgAcq->Backend.cFramesPeriod       = pDevCfg->cFramesPeriod;
        pCfgAcq->Backend.cFramesPreBuffering = pCfgReq->Backend.cFramesPreBuffering * pDevCfg->cFramesBufferSize
                                             / RT_MAX(pCfgReq->Backend.cFramesBufferSize, 1);

        PDMAudioStrmCfgCopy(&pStreamWas->Cfg, pCfgAcq);

        /* Finally, the critical section. */
        int rc2 = RTCritSectInit(&pStreamWas->CritSect);
        if (RT_SUCCESS(rc2))
        {
            RTCritSectRwEnterExcl(&pThis->CritSectStreamList);
            RTListAppend(&pThis->StreamHead, &pStreamWas->ListEntry);
            RTCritSectRwLeaveExcl(&pThis->CritSectStreamList);

            if (pStreamWas->pDevCfg->pIAudioClient != NULL)
            {
                LogFlowFunc(("returns VINF_SUCCESS\n", rc));
                return VINF_SUCCESS;
            }
            LogFlowFunc(("returns VINF_AUDIO_STREAM_ASYNC_INIT_NEEDED\n", rc));
            return VINF_AUDIO_STREAM_ASYNC_INIT_NEEDED;
        }

        LogRelMax(64, ("WasAPI: Failed to create critical section for stream.\n"));
        drvHostAudioWasCachePutBack(pThis, pDevCfg);
        pStreamWas->pDevCfg = NULL;
    }
    else
        LogRelMax(64, ("WasAPI: Failed to setup %s on audio device '%ls' (%Rrc).\n", pszStreamType, pwszDevIdDesc, rc));

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamInitAsync}
 */
static DECLCALLBACK(int) drvHostAudioWasHA_StreamInitAsync(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                           bool fDestroyed)
{
    RT_NOREF(pInterface);
    PDRVHOSTAUDIOWASSTREAM pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    AssertPtrReturn(pStreamWas, VERR_INVALID_POINTER);
    LogFlowFunc(("Stream '%s'%s\n", pStreamWas->Cfg.szName, fDestroyed ? " - destroyed!" : ""));

    /*
     * Assert sane preconditions for this call.
     */
    AssertPtrReturn(pStreamWas->Core.pStream, VERR_INTERNAL_ERROR);
    AssertPtrReturn(pStreamWas->pDevCfg, VERR_INTERNAL_ERROR_2);
    AssertPtrReturn(pStreamWas->pDevCfg->pDevEntry, VERR_INTERNAL_ERROR_3);
    AssertPtrReturn(pStreamWas->pDevCfg->pDevEntry->pIDevice, VERR_INTERNAL_ERROR_4);
    AssertReturn(pStreamWas->pDevCfg->pDevEntry->enmDir == pStreamWas->Core.pStream->Cfg.enmDir, VERR_INTERNAL_ERROR_4);
    AssertReturn(pStreamWas->pDevCfg->pIAudioClient == NULL, VERR_INTERNAL_ERROR_5);
    AssertReturn(pStreamWas->pDevCfg->pIAudioRenderClient == NULL, VERR_INTERNAL_ERROR_5);
    AssertReturn(pStreamWas->pDevCfg->pIAudioCaptureClient == NULL, VERR_INTERNAL_ERROR_5);

    /*
     * Do the job.
     */
    int rc;
    if (!fDestroyed)
        rc = drvHostAudioWasCacheInitConfig(pStreamWas->pDevCfg);
    else
    {
        AssertReturn(pStreamWas->pDevCfg->rcSetup == VERR_AUDIO_STREAM_INIT_IN_PROGRESS, VERR_INTERNAL_ERROR_2);
        pStreamWas->pDevCfg->rcSetup = VERR_WRONG_ORDER;
        rc = VINF_SUCCESS;
    }

    LogFlowFunc(("returns %Rrc (%s)\n", rc, pStreamWas->Cfg.szName));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDestroy}
 */
static DECLCALLBACK(int) drvHostAudioWasHA_StreamDestroy(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                         bool fImmediate)
{
    PDRVHOSTAUDIOWAS        pThis      = RT_FROM_MEMBER(pInterface, DRVHOSTAUDIOWAS, IHostAudio);
    PDRVHOSTAUDIOWASSTREAM  pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    AssertPtrReturn(pStreamWas, VERR_INVALID_POINTER);
    LogFlowFunc(("Stream '%s'\n", pStreamWas->Cfg.szName));
    RT_NOREF(fImmediate);
    HRESULT hrc;

    if (RTCritSectIsInitialized(&pStreamWas->CritSect))
    {
        RTCritSectRwEnterExcl(&pThis->CritSectStreamList);
        RTListNodeRemove(&pStreamWas->ListEntry);
        RTCritSectRwLeaveExcl(&pThis->CritSectStreamList);

        RTCritSectDelete(&pStreamWas->CritSect);
    }

    if (pStreamWas->fStarted && pStreamWas->pDevCfg && pStreamWas->pDevCfg->pIAudioClient)
    {
        hrc = pStreamWas->pDevCfg->pIAudioClient->Stop();
        LogFunc(("Stop('%s') -> %Rhrc\n", pStreamWas->Cfg.szName, hrc));
        pStreamWas->fStarted = false;
    }

    if (pStreamWas->cFramesCaptureToRelease)
    {
        hrc = pStreamWas->pDevCfg->pIAudioCaptureClient->ReleaseBuffer(0);
        Log4Func(("Releasing capture buffer (%#x frames): %Rhrc\n", pStreamWas->cFramesCaptureToRelease, hrc));
        pStreamWas->cFramesCaptureToRelease = 0;
        pStreamWas->pbCapture               = NULL;
        pStreamWas->cbCapture               = 0;
    }

    if (pStreamWas->pDevCfg)
    {
        drvHostAudioWasCachePutBack(pThis, pStreamWas->pDevCfg);
        pStreamWas->pDevCfg = NULL;
    }

    LogFlowFunc(("returns\n"));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamNotifyDeviceChanged}
 */
static DECLCALLBACK(void) drvHostAudioWasHA_StreamNotifyDeviceChanged(PPDMIHOSTAUDIO pInterface,
                                                                      PPDMAUDIOBACKENDSTREAM pStream, void *pvUser)
{
    PDRVHOSTAUDIOWAS        pThis      = RT_FROM_MEMBER(pInterface, DRVHOSTAUDIOWAS, IHostAudio);
    PDRVHOSTAUDIOWASSTREAM  pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    LogFlowFunc(("pStreamWas=%p (%s)\n", pStreamWas, pStreamWas->Cfg.szName));
    RT_NOREF(pvUser);

    /*
     * See if we've got a cached config for the new device around.
     * We ignore this entirely, for now at least, if the device was
     * disconnected and there is no replacement.
     */
    pThis->pNotifyClient->lockEnter();
    IMMDevice *pIDevice = pStreamWas->Cfg.enmDir == PDMAUDIODIR_IN ? pThis->pIDeviceInput : pThis->pIDeviceOutput;
    if (pIDevice)
        pIDevice->AddRef();
    pThis->pNotifyClient->lockLeave();
    if (pIDevice)
    {
        PDRVHOSTAUDIOWASCACHEDEVCFG pDevCfg = NULL;
        int rc = drvHostAudioWasCacheLookupOrCreate(pThis, pIDevice, &pStreamWas->Cfg, false /*fOnWorker*/, &pDevCfg);

        pIDevice->Release();
        pIDevice = NULL;

        /*
         * If we have a working audio client, just do the switch.
         */
        if (RT_SUCCESS(rc) && pDevCfg->pIAudioClient)
        {
            LogFlowFunc(("New device config is ready already!\n"));
            Assert(rc == VINF_SUCCESS);
            drvHostAudioWasCompleteStreamDevSwitch(pThis, pStreamWas, pDevCfg);
        }
        /*
         * Otherwise create one asynchronously on a worker thread.
         */
        else if (RT_SUCCESS(rc))
        {
            LogFlowFunc(("New device config needs async init ...\n"));
            Assert(rc == VINF_AUDIO_STREAM_ASYNC_INIT_NEEDED);

            RTCritSectEnter(&pStreamWas->CritSect);
            pStreamWas->fSwitchingDevice = true;
            RTCritSectLeave(&pStreamWas->CritSect);

            pThis->pIHostAudioPort->pfnStreamNotifyPreparingDeviceSwitch(pThis->pIHostAudioPort, &pStreamWas->Core);

            rc = pThis->pIHostAudioPort->pfnDoOnWorkerThread(pThis->pIHostAudioPort, &pStreamWas->Core,
                                                             DRVHOSTAUDIOWAS_DO_STREAM_DEV_SWITCH, pDevCfg);
            AssertRCStmt(rc, drvHostAudioWasDoStreamDevSwitch(pThis, pStreamWas, pDevCfg));
        }
        else
        {
            LogRelMax(64, ("WasAPI: Failed to create new device config '%ls:%s' for stream '%s': %Rrc\n",
                           pDevCfg->pDevEntry->wszDevId, pDevCfg->szProps, pStreamWas->Cfg.szName, rc));

            pThis->pIHostAudioPort->pfnStreamNotifyDeviceChanged(pThis->pIHostAudioPort, &pStreamWas->Core, true /*fReInit*/);
        }
    }
    else
        LogFlowFunc(("no new device, leaving it as-is\n"));
}


/**
 * Wrapper for starting a stream.
 *
 * @returns VBox status code.
 * @param   pThis           The WASAPI host audio driver instance data.
 * @param   pStreamWas      The stream.
 * @param   pszOperation    The operation we're doing.
 */
static int drvHostAudioWasStreamStartWorker(PDRVHOSTAUDIOWAS pThis, PDRVHOSTAUDIOWASSTREAM pStreamWas, const char *pszOperation)
{
    HRESULT hrc = pStreamWas->pDevCfg->pIAudioClient->Start();
    LogFlow(("%s: Start(%s) returns %Rhrc\n", pszOperation, pStreamWas->Cfg.szName, hrc));
    AssertStmt(hrc != AUDCLNT_E_NOT_STOPPED, hrc = S_OK);
    if (SUCCEEDED(hrc))
    {
        pStreamWas->fStarted = true;
        return VINF_SUCCESS;
    }

    /** @todo try re-setup the stuff on AUDCLNT_E_DEVICEINVALIDATED.
     * Need some way of telling the caller (e.g. playback, capture) so they can
     * retry what they're doing */
    RT_NOREF(pThis);

    pStreamWas->fStarted = false;
    LogRelMax(64, ("WasAPI: Starting '%s' failed (%s): %Rhrc\n", pStreamWas->Cfg.szName, pszOperation, hrc));
    return VERR_AUDIO_STREAM_NOT_READY;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamEnable}
 */
static DECLCALLBACK(int) drvHostAudioWasHA_StreamEnable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVHOSTAUDIOWAS        pThis      = RT_FROM_MEMBER(pInterface, DRVHOSTAUDIOWAS, IHostAudio);
    PDRVHOSTAUDIOWASSTREAM  pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    LogFlowFunc(("Stream '%s' {%s}\n", pStreamWas->Cfg.szName, drvHostWasStreamStatusString(pStreamWas)));
    HRESULT hrc;
    RTCritSectEnter(&pStreamWas->CritSect);

    Assert(!pStreamWas->fEnabled);
    Assert(!pStreamWas->fStarted);

    /*
     * We always reset the buffer before enabling the stream (normally never necessary).
     */
    if (pStreamWas->cFramesCaptureToRelease)
    {
        hrc = pStreamWas->pDevCfg->pIAudioCaptureClient->ReleaseBuffer(pStreamWas->cFramesCaptureToRelease);
        Log4Func(("Releasing capture buffer (%#x frames): %Rhrc\n", pStreamWas->cFramesCaptureToRelease, hrc));
        pStreamWas->cFramesCaptureToRelease = 0;
        pStreamWas->pbCapture               = NULL;
        pStreamWas->cbCapture               = 0;
    }

    hrc = pStreamWas->pDevCfg->pIAudioClient->Reset();
    if (FAILED(hrc))
        LogRelMax(64, ("WasAPI: Stream reset failed when enabling '%s': %Rhrc\n", pStreamWas->Cfg.szName, hrc));
    pStreamWas->offInternal      = 0;
    pStreamWas->fDraining        = false;
    pStreamWas->fEnabled         = true;
    pStreamWas->fRestartOnResume = false;

    /*
     * Input streams will start capturing, while output streams will only start
     * playing once we get some audio data to play.
     */
    int rc = VINF_SUCCESS;
    if (pStreamWas->Cfg.enmDir == PDMAUDIODIR_IN)
        rc = drvHostAudioWasStreamStartWorker(pThis, pStreamWas, "enable");
    else
        Assert(pStreamWas->Cfg.enmDir == PDMAUDIODIR_OUT);

    RTCritSectLeave(&pStreamWas->CritSect);
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDisable}
 */
static DECLCALLBACK(int) drvHostAudioWasHA_StreamDisable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHOSTAUDIOWASSTREAM pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    LogFlowFunc(("cMsLastTransfer=%RI64 ms, stream '%s' {%s} \n",
                 pStreamWas->msLastTransfer ? RTTimeMilliTS() - pStreamWas->msLastTransfer : -1,
                 pStreamWas->Cfg.szName, drvHostWasStreamStatusString(pStreamWas) ));
    RTCritSectEnter(&pStreamWas->CritSect);

    /*
     * Always try stop it (draining or no).
     */
    pStreamWas->fEnabled         = false;
    pStreamWas->fRestartOnResume = false;
    Assert(!pStreamWas->fDraining || pStreamWas->Cfg.enmDir == PDMAUDIODIR_OUT);

    int rc = VINF_SUCCESS;
    if (pStreamWas->fStarted)
    {
        HRESULT hrc = pStreamWas->pDevCfg->pIAudioClient->Stop();
        LogFlowFunc(("Stop(%s) returns %Rhrc\n", pStreamWas->Cfg.szName, hrc));
        if (FAILED(hrc))
        {
            LogRelMax(64, ("WasAPI: Stopping '%s' failed (disable): %Rhrc\n", pStreamWas->Cfg.szName, hrc));
            rc = VERR_GENERAL_FAILURE;
        }
        pStreamWas->fStarted  = false;
        pStreamWas->fDraining = false;
    }

    RTCritSectLeave(&pStreamWas->CritSect);
    LogFlowFunc(("returns %Rrc {%s}\n", rc, drvHostWasStreamStatusString(pStreamWas)));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPause}
 *
 * @note    Basically the same as drvHostAudioWasHA_StreamDisable, just w/o the
 *          buffer resetting and fEnabled change.
 */
static DECLCALLBACK(int) drvHostAudioWasHA_StreamPause(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHOSTAUDIOWASSTREAM pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    LogFlowFunc(("cMsLastTransfer=%RI64 ms, stream '%s' {%s} \n",
                 pStreamWas->msLastTransfer ? RTTimeMilliTS() - pStreamWas->msLastTransfer : -1,
                 pStreamWas->Cfg.szName, drvHostWasStreamStatusString(pStreamWas) ));
    RTCritSectEnter(&pStreamWas->CritSect);

    /*
     * Unless we're draining the stream, stop it if it's started.
     */
    int rc = VINF_SUCCESS;
    if (pStreamWas->fStarted && !pStreamWas->fDraining)
    {
        pStreamWas->fRestartOnResume = true;

        HRESULT hrc = pStreamWas->pDevCfg->pIAudioClient->Stop();
        LogFlowFunc(("Stop(%s) returns %Rhrc\n", pStreamWas->Cfg.szName, hrc));
        if (FAILED(hrc))
        {
            LogRelMax(64, ("WasAPI: Stopping '%s' failed (pause): %Rhrc\n", pStreamWas->Cfg.szName, hrc));
            rc = VERR_GENERAL_FAILURE;
        }
        pStreamWas->fStarted = false;
    }
    else
    {
        pStreamWas->fRestartOnResume = false;
        if (pStreamWas->fDraining)
        {
            LogFunc(("Stream '%s' is draining\n", pStreamWas->Cfg.szName));
            Assert(pStreamWas->fStarted);
        }
    }

    RTCritSectLeave(&pStreamWas->CritSect);
    LogFlowFunc(("returns %Rrc {%s}\n", rc, drvHostWasStreamStatusString(pStreamWas)));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamResume}
 */
static DECLCALLBACK(int) drvHostAudioWasHA_StreamResume(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVHOSTAUDIOWAS        pThis      = RT_FROM_MEMBER(pInterface, DRVHOSTAUDIOWAS, IHostAudio);
    PDRVHOSTAUDIOWASSTREAM  pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    LogFlowFunc(("Stream '%s' {%s}\n", pStreamWas->Cfg.szName, drvHostWasStreamStatusString(pStreamWas)));
    RTCritSectEnter(&pStreamWas->CritSect);

    /*
     * Resume according to state saved by drvHostAudioWasHA_StreamPause.
     */
    int rc;
    if (pStreamWas->fRestartOnResume)
        rc = drvHostAudioWasStreamStartWorker(pThis, pStreamWas, "resume");
    else
        rc = VINF_SUCCESS;
    pStreamWas->fRestartOnResume = false;

    RTCritSectLeave(&pStreamWas->CritSect);
    LogFlowFunc(("returns %Rrc {%s}\n", rc, drvHostWasStreamStatusString(pStreamWas)));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDrain}
 */
static DECLCALLBACK(int) drvHostAudioWasHA_StreamDrain(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHOSTAUDIOWASSTREAM  pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    AssertReturn(pStreamWas->Cfg.enmDir == PDMAUDIODIR_OUT, VERR_INVALID_PARAMETER);
    LogFlowFunc(("cMsLastTransfer=%RI64 ms, stream '%s' {%s} \n",
                 pStreamWas->msLastTransfer ? RTTimeMilliTS() - pStreamWas->msLastTransfer : -1,
                 pStreamWas->Cfg.szName, drvHostWasStreamStatusString(pStreamWas) ));

    /*
     * If the stram was started, calculate when the buffered data has finished
     * playing and switch to drain mode.  DrvAudio will keep on calling
     * pfnStreamPlay with an empty buffer while we're draining, so we'll use
     * that for checking the deadline and finally stopping the stream.
     */
    RTCritSectEnter(&pStreamWas->CritSect);
    int rc = VINF_SUCCESS;
    if (pStreamWas->fStarted)
    {
        if (!pStreamWas->fDraining)
        {
            uint64_t const msNow           = RTTimeMilliTS();
            uint64_t       msDrainDeadline = 0;
            UINT32         cFramesPending  = 0;
            HRESULT hrc = pStreamWas->pDevCfg->pIAudioClient->GetCurrentPadding(&cFramesPending);
            if (SUCCEEDED(hrc))
                msDrainDeadline = msNow
                                + PDMAudioPropsFramesToMilli(&pStreamWas->Cfg.Props,
                                                             RT_MIN(cFramesPending,
                                                                    pStreamWas->Cfg.Backend.cFramesBufferSize * 2))
                                + 1 /*fudge*/;
            else
            {
                msDrainDeadline = msNow;
                LogRelMax(64, ("WasAPI: GetCurrentPadding fail on '%s' when starting draining: %Rhrc\n",
                               pStreamWas->Cfg.szName, hrc));
            }
            pStreamWas->msDrainDeadline = msDrainDeadline;
            pStreamWas->fDraining       = true;
        }
        else
            LogFlowFunc(("Already draining '%s' ...\n", pStreamWas->Cfg.szName));
    }
    else
    {
        LogFlowFunc(("Drain requested for '%s', but not started playback...\n", pStreamWas->Cfg.szName));
        AssertStmt(!pStreamWas->fDraining, pStreamWas->fDraining = false);
    }
    RTCritSectLeave(&pStreamWas->CritSect);

    LogFlowFunc(("returns %Rrc {%s}\n", rc, drvHostWasStreamStatusString(pStreamWas)));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetState}
 */
static DECLCALLBACK(PDMHOSTAUDIOSTREAMSTATE) drvHostAudioWasHA_StreamGetState(PPDMIHOSTAUDIO pInterface,
                                                                              PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHOSTAUDIOWASSTREAM pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    AssertPtrReturn(pStreamWas, PDMHOSTAUDIOSTREAMSTATE_INVALID);

    PDMHOSTAUDIOSTREAMSTATE enmState;
    AssertPtr(pStreamWas->pDevCfg);
    if (pStreamWas->pDevCfg /*paranoia*/)
    {
        if (RT_SUCCESS(pStreamWas->pDevCfg->rcSetup))
        {
            if (!pStreamWas->fDraining)
                enmState = PDMHOSTAUDIOSTREAMSTATE_OKAY;
            else
            {
                Assert(pStreamWas->Cfg.enmDir == PDMAUDIODIR_OUT);
                enmState = PDMHOSTAUDIOSTREAMSTATE_DRAINING;
            }
        }
        else if (   pStreamWas->pDevCfg->rcSetup == VERR_AUDIO_STREAM_INIT_IN_PROGRESS
                 || pStreamWas->fSwitchingDevice )
            enmState = PDMHOSTAUDIOSTREAMSTATE_INITIALIZING;
        else
            enmState = PDMHOSTAUDIOSTREAMSTATE_NOT_WORKING;
    }
    else if (pStreamWas->fSwitchingDevice)
        enmState = PDMHOSTAUDIOSTREAMSTATE_INITIALIZING;
    else
        enmState = PDMHOSTAUDIOSTREAMSTATE_NOT_WORKING;

    LogFlowFunc(("returns %d for '%s' {%s}\n", enmState, pStreamWas->Cfg.szName, drvHostWasStreamStatusString(pStreamWas)));
    return enmState;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetPending}
 */
static DECLCALLBACK(uint32_t) drvHostAudioWasHA_StreamGetPending(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHOSTAUDIOWASSTREAM pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    AssertPtrReturn(pStreamWas, 0);
    LogFlowFunc(("Stream '%s' {%s}\n", pStreamWas->Cfg.szName, drvHostWasStreamStatusString(pStreamWas)));
    AssertReturn(pStreamWas->Cfg.enmDir == PDMAUDIODIR_OUT, 0);

    uint32_t cbPending = 0;
    RTCritSectEnter(&pStreamWas->CritSect);

    if (   pStreamWas->Cfg.enmDir == PDMAUDIODIR_OUT
        && pStreamWas->pDevCfg->pIAudioClient /* paranoia */)
    {
        if (pStreamWas->fStarted)
        {
            UINT32  cFramesPending = 0;
            HRESULT hrc = pStreamWas->pDevCfg->pIAudioClient->GetCurrentPadding(&cFramesPending);
            if (SUCCEEDED(hrc))
            {
                AssertMsg(cFramesPending <= pStreamWas->Cfg.Backend.cFramesBufferSize,
                          ("cFramesPending=%#x cFramesBufferSize=%#x\n",
                           cFramesPending, pStreamWas->Cfg.Backend.cFramesBufferSize));
                cbPending = PDMAudioPropsFramesToBytes(&pStreamWas->Cfg.Props, RT_MIN(cFramesPending, VBOX_WASAPI_MAX_PADDING));
            }
            else /* Don't use LogRelMax here to get a more accurate tracking about when this works again wrt recovery. */
                LogRel2(("WasAPI: GetCurrentPadding[R] failed on '%s': %Rhrc\n", pStreamWas->Cfg.szName, hrc));
        }
    }

    RTCritSectLeave(&pStreamWas->CritSect);

    LogFlowFunc(("returns %#x (%u) {%s}\n", cbPending, cbPending, drvHostWasStreamStatusString(pStreamWas)));
    return cbPending;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetWritable}
 */
static DECLCALLBACK(uint32_t) drvHostAudioWasHA_StreamGetWritable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHOSTAUDIOWASSTREAM pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    AssertPtrReturn(pStreamWas, 0);
    LogFlowFunc(("Stream '%s' {%s}\n", pStreamWas->Cfg.szName, drvHostWasStreamStatusString(pStreamWas)));
    Assert(pStreamWas->Cfg.enmDir == PDMAUDIODIR_OUT);

    uint32_t cbWritable = 0;
    RTCritSectEnter(&pStreamWas->CritSect);

    if (   pStreamWas->Cfg.enmDir == PDMAUDIODIR_OUT
        && pStreamWas->pDevCfg->pIAudioClient /* paranoia */)
    {
        UINT32  cFramesPending = 0;
        HRESULT hrc = pStreamWas->pDevCfg->pIAudioClient->GetCurrentPadding(&cFramesPending);
        if (SUCCEEDED(hrc))
        {
            if (cFramesPending < pStreamWas->Cfg.Backend.cFramesBufferSize)
                cbWritable = PDMAudioPropsFramesToBytes(&pStreamWas->Cfg.Props,
                                                        pStreamWas->Cfg.Backend.cFramesBufferSize - cFramesPending);
            else if (cFramesPending > pStreamWas->Cfg.Backend.cFramesBufferSize)
            {
                LogRelMax(64, ("WasAPI: Warning! GetCurrentPadding('%s') return too high: cFramesPending=%#x > cFramesBufferSize=%#x\n",
                               pStreamWas->Cfg.szName, cFramesPending, pStreamWas->Cfg.Backend.cFramesBufferSize));
                AssertMsgFailed(("cFramesPending=%#x > cFramesBufferSize=%#x\n",
                                 cFramesPending, pStreamWas->Cfg.Backend.cFramesBufferSize));
            }
        }
        else /* Don't use LogRelMax here to get a more accurate tracking about when this works again wrt recovery. */
            LogRel2(("WasAPI: GetCurrentPadding[W] failed on '%s': %Rhrc\n", pStreamWas->Cfg.szName, hrc));
    }

    RTCritSectLeave(&pStreamWas->CritSect);

    LogFlowFunc(("returns %#x (%u) {%s}\n", cbWritable, cbWritable, drvHostWasStreamStatusString(pStreamWas)));
    return cbWritable;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPlay}
 */
static DECLCALLBACK(int) drvHostAudioWasHA_StreamPlay(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                      const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    PDRVHOSTAUDIOWAS        pThis      = RT_FROM_MEMBER(pInterface, DRVHOSTAUDIOWAS, IHostAudio);
    PDRVHOSTAUDIOWASSTREAM  pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    AssertPtrReturn(pStreamWas, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbWritten, VERR_INVALID_POINTER);
    if (cbBuf)
        AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    Assert(PDMAudioPropsIsSizeAligned(&pStreamWas->Cfg.Props, cbBuf));

    RTCritSectEnter(&pStreamWas->CritSect);
    if (pStreamWas->fEnabled)
    { /* likely */ }
    else
    {
        RTCritSectLeave(&pStreamWas->CritSect);
        *pcbWritten = 0;
        LogFunc(("Skipping %#x byte write to disabled stream {%s}\n", cbBuf, drvHostWasStreamStatusString(pStreamWas)));
        return VINF_SUCCESS;
    }
    Log4Func(("cbBuf=%#x stream '%s' {%s}\n", cbBuf, pStreamWas->Cfg.szName, drvHostWasStreamStatusString(pStreamWas) ));

    /*
     * Transfer loop.
     */
    int      rc        = VINF_SUCCESS;
    uint32_t cReInits  = 0;
    uint32_t cbWritten = 0;
    while (cbBuf > 0)
    {
        AssertBreakStmt(pStreamWas->pDevCfg && pStreamWas->pDevCfg->pIAudioRenderClient && pStreamWas->pDevCfg->pIAudioClient,
                        rc = VERR_AUDIO_STREAM_NOT_READY);

        /*
         * Figure out how much we can possibly write.
         */
        UINT32   cFramesPending = 0;
        uint32_t cbWritable = 0;
        HRESULT hrc = pStreamWas->pDevCfg->pIAudioClient->GetCurrentPadding(&cFramesPending);
        if (SUCCEEDED(hrc))
            cbWritable = PDMAudioPropsFramesToBytes(&pStreamWas->Cfg.Props,
                                                      pStreamWas->Cfg.Backend.cFramesBufferSize
                                                    - RT_MIN(cFramesPending, pStreamWas->Cfg.Backend.cFramesBufferSize));
        else
        {
            LogRelMax(64, ("WasAPI: GetCurrentPadding(%s) failed during playback: %Rhrc (@%#RX64)\n",
                           pStreamWas->Cfg.szName, hrc, pStreamWas->offInternal));
            /** @todo reinit on AUDCLNT_E_DEVICEINVALIDATED? */
            rc = VERR_AUDIO_STREAM_NOT_READY;
            break;
        }
        if (cbWritable <= PDMAudioPropsFrameSize(&pStreamWas->Cfg.Props))
            break;

        uint32_t const cbToWrite      = PDMAudioPropsFloorBytesToFrame(&pStreamWas->Cfg.Props, RT_MIN(cbWritable, cbBuf));
        uint32_t const cFramesToWrite = PDMAudioPropsBytesToFrames(&pStreamWas->Cfg.Props, cbToWrite);
        Assert(PDMAudioPropsFramesToBytes(&pStreamWas->Cfg.Props, cFramesToWrite) == cbToWrite);
        Log3Func(("@%#RX64: cFramesPending=%#x -> cbWritable=%#x cbToWrite=%#x cFramesToWrite=%#x {%s}\n",
                  pStreamWas->offInternal, cFramesPending, cbWritable, cbToWrite, cFramesToWrite,
                  drvHostWasStreamStatusString(pStreamWas) ));

        /*
         * Get the buffer, copy the data into it, and relase it back to the WAS machinery.
         */
        BYTE *pbData = NULL;
        hrc = pStreamWas->pDevCfg->pIAudioRenderClient->GetBuffer(cFramesToWrite, &pbData);
        if (SUCCEEDED(hrc))
        {
            memcpy(pbData, pvBuf, cbToWrite);
            hrc = pStreamWas->pDevCfg->pIAudioRenderClient->ReleaseBuffer(cFramesToWrite, 0 /*fFlags*/);
            if (SUCCEEDED(hrc))
            {
                /*
                 * Before we advance the buffer position (so we can resubmit it
                 * after re-init), make sure we've successfully started stream.
                 */
                if (pStreamWas->fStarted)
                { }
                else
                {
                    rc = drvHostAudioWasStreamStartWorker(pThis, pStreamWas, "play");
                    if (rc == VINF_SUCCESS)
                    { /* likely */ }
                    else if (RT_SUCCESS(rc) && ++cReInits < 5)
                        continue; /* re-submit buffer after re-init */
                    else
                        break;
                }

                /* advance. */
                pvBuf                    = (uint8_t *)pvBuf + cbToWrite;
                cbBuf                   -= cbToWrite;
                cbWritten               += cbToWrite;
                pStreamWas->offInternal += cbToWrite;
            }
            else
            {
                LogRelMax(64, ("WasAPI: ReleaseBuffer(%#x) failed on '%s' during playback: %Rhrc (@%#RX64)\n",
                               cFramesToWrite, pStreamWas->Cfg.szName, hrc, pStreamWas->offInternal));
                /** @todo reinit on AUDCLNT_E_DEVICEINVALIDATED? */
                rc = VERR_AUDIO_STREAM_NOT_READY;
                break;
            }
        }
        else
        {
            LogRelMax(64, ("WasAPI: GetBuffer(%#x) failed on '%s' during playback: %Rhrc (@%#RX64)\n",
                           cFramesToWrite, pStreamWas->Cfg.szName, hrc, pStreamWas->offInternal));
            /** @todo reinit on AUDCLNT_E_DEVICEINVALIDATED? */
            rc = VERR_AUDIO_STREAM_NOT_READY;
            break;
        }
    }

    /*
     * Do draining deadline processing.
     */
    uint64_t const msNow = RTTimeMilliTS();
    if (   !pStreamWas->fDraining
        || msNow < pStreamWas->msDrainDeadline)
    { /* likely */ }
    else
    {
        LogRel2(("WasAPI: Stopping draining of '%s' {%s} ...\n", pStreamWas->Cfg.szName, drvHostWasStreamStatusString(pStreamWas)));
        HRESULT hrc = pStreamWas->pDevCfg->pIAudioClient->Stop();
        if (FAILED(hrc))
            LogRelMax(64, ("WasAPI: Failed to stop draining stream '%s': %Rhrc\n", pStreamWas->Cfg.szName, hrc));
        pStreamWas->fDraining = false;
        pStreamWas->fStarted  = false;
        pStreamWas->fEnabled  = false;
    }

    /*
     * Done.
     */
    uint64_t const msPrev = pStreamWas->msLastTransfer; RT_NOREF(msPrev);
    if (cbWritten)
        pStreamWas->msLastTransfer = msNow;

    RTCritSectLeave(&pStreamWas->CritSect);

    *pcbWritten = cbWritten;
    if (RT_SUCCESS(rc) || !cbWritten)
    { }
    else
    {
        LogFlowFunc(("Suppressing %Rrc to report %#x bytes written\n", rc, cbWritten));
        rc = VINF_SUCCESS;
    }
    LogFlowFunc(("@%#RX64: rc=%Rrc cbWritten=%RU32 cMsDelta=%RU64 (%RU64 -> %RU64) {%s}\n", pStreamWas->offInternal, rc, cbWritten,
                 msPrev ? msNow - msPrev : 0, msPrev, pStreamWas->msLastTransfer, drvHostWasStreamStatusString(pStreamWas) ));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetReadable}
 */
static DECLCALLBACK(uint32_t) drvHostAudioWasHA_StreamGetReadable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDRVHOSTAUDIOWASSTREAM pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    AssertPtrReturn(pStreamWas, 0);
    Assert(pStreamWas->Cfg.enmDir == PDMAUDIODIR_IN);

    uint32_t cbReadable = 0;
    RTCritSectEnter(&pStreamWas->CritSect);

    if (pStreamWas->pDevCfg->pIAudioCaptureClient /* paranoia */)
    {
        UINT32  cFramesPending = 0;
        HRESULT hrc = pStreamWas->pDevCfg->pIAudioClient->GetCurrentPadding(&cFramesPending);
        if (SUCCEEDED(hrc))
        {
            /* An unreleased buffer is included in the pending frame count, so subtract
               whatever we've got hanging around since the previous pfnStreamCapture call. */
            AssertMsgStmt(cFramesPending >= pStreamWas->cFramesCaptureToRelease,
                          ("%#x vs %#x\n", cFramesPending, pStreamWas->cFramesCaptureToRelease),
                          cFramesPending = pStreamWas->cFramesCaptureToRelease);
            cFramesPending -= pStreamWas->cFramesCaptureToRelease;

            /* Add what we've got left in said buffer. */
            uint32_t cFramesCurPacket = PDMAudioPropsBytesToFrames(&pStreamWas->Cfg.Props, pStreamWas->cbCapture);
            cFramesPending += cFramesCurPacket;

            /* Paranoia: Make sure we don't exceed the buffer size. */
            AssertMsgStmt(cFramesPending <= pStreamWas->Cfg.Backend.cFramesBufferSize,
                          ("cFramesPending=%#x cFramesCaptureToRelease=%#x cFramesCurPacket=%#x cFramesBufferSize=%#x\n",
                           cFramesPending, pStreamWas->cFramesCaptureToRelease, cFramesCurPacket,
                           pStreamWas->Cfg.Backend.cFramesBufferSize),
                          cFramesPending = pStreamWas->Cfg.Backend.cFramesBufferSize);

            cbReadable = PDMAudioPropsFramesToBytes(&pStreamWas->Cfg.Props, cFramesPending);
        }
        else
            LogRelMax(64, ("WasAPI: GetCurrentPadding failed on '%s': %Rhrc\n", pStreamWas->Cfg.szName, hrc));
    }

    RTCritSectLeave(&pStreamWas->CritSect);

    LogFlowFunc(("returns %#x (%u) {%s}\n", cbReadable, cbReadable, drvHostWasStreamStatusString(pStreamWas)));
    return cbReadable;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCapture}
 */
static DECLCALLBACK(int) drvHostAudioWasHA_StreamCapture(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                         void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead)
{
    RT_NOREF(pInterface); //PDRVHOSTAUDIOWAS        pThis      = RT_FROM_MEMBER(pInterface, DRVHOSTAUDIOWAS, IHostAudio);
    PDRVHOSTAUDIOWASSTREAM  pStreamWas = (PDRVHOSTAUDIOWASSTREAM)pStream;
    AssertPtrReturn(pStreamWas, 0);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbRead, VERR_INVALID_POINTER);
    Assert(PDMAudioPropsIsSizeAligned(&pStreamWas->Cfg.Props, cbBuf));

    RTCritSectEnter(&pStreamWas->CritSect);
    if (pStreamWas->fEnabled)
    { /* likely */ }
    else
    {
        RTCritSectLeave(&pStreamWas->CritSect);
        *pcbRead = 0;
        LogFunc(("Skipping %#x byte read from disabled stream {%s}\n", cbBuf, drvHostWasStreamStatusString(pStreamWas)));
        return VINF_SUCCESS;
    }
    Log4Func(("cbBuf=%#x stream '%s' {%s}\n", cbBuf, pStreamWas->Cfg.szName, drvHostWasStreamStatusString(pStreamWas) ));


    /*
     * Transfer loop.
     */
    int            rc          = VINF_SUCCESS;
    uint32_t       cbRead      = 0;
    uint32_t const cbFrame     = PDMAudioPropsFrameSize(&pStreamWas->Cfg.Props);
    while (cbBuf >= cbFrame)
    {
        AssertBreakStmt(pStreamWas->pDevCfg->pIAudioCaptureClient && pStreamWas->pDevCfg->pIAudioClient, rc = VERR_AUDIO_STREAM_NOT_READY);

        /*
         * Anything pending from last call?
         * (This is rather similar to the Pulse interface.)
         */
        if (pStreamWas->cFramesCaptureToRelease)
        {
            uint32_t const cbToCopy = RT_MIN(pStreamWas->cbCapture, cbBuf);
            memcpy(pvBuf, pStreamWas->pbCapture, cbToCopy);
            pvBuf                    = (uint8_t *)pvBuf + cbToCopy;
            cbBuf                   -= cbToCopy;
            cbRead                  += cbToCopy;
            pStreamWas->offInternal += cbToCopy;
            pStreamWas->pbCapture   += cbToCopy;
            pStreamWas->cbCapture   -= cbToCopy;
            if (!pStreamWas->cbCapture)
            {
                HRESULT hrc = pStreamWas->pDevCfg->pIAudioCaptureClient->ReleaseBuffer(pStreamWas->cFramesCaptureToRelease);
                Log4Func(("@%#RX64: Releasing capture buffer (%#x frames): %Rhrc\n",
                          pStreamWas->offInternal, pStreamWas->cFramesCaptureToRelease, hrc));
                if (SUCCEEDED(hrc))
                {
                    pStreamWas->cFramesCaptureToRelease = 0;
                    pStreamWas->pbCapture               = NULL;
                }
                else
                {
                    LogRelMax(64, ("WasAPI: ReleaseBuffer(%s) failed during capture: %Rhrc (@%#RX64)\n",
                                   pStreamWas->Cfg.szName, hrc, pStreamWas->offInternal));
                    /** @todo reinit on AUDCLNT_E_DEVICEINVALIDATED? */
                    rc = VERR_AUDIO_STREAM_NOT_READY;
                    break;
                }
            }
            if (cbBuf < cbFrame)
                break;
        }

        /*
         * Figure out if there is any data available to be read now. (Docs hint that we can not
         * skip this and go straight for GetBuffer or we risk getting unwritten buffer space back).
         */
        UINT32 cFramesCaptured = 0;
        HRESULT hrc = pStreamWas->pDevCfg->pIAudioCaptureClient->GetNextPacketSize(&cFramesCaptured);
        if (SUCCEEDED(hrc))
        {
            if (!cFramesCaptured)
                break;
        }
        else
        {
            LogRelMax(64, ("WasAPI: GetNextPacketSize(%s) failed during capture: %Rhrc (@%#RX64)\n",
                           pStreamWas->Cfg.szName, hrc, pStreamWas->offInternal));
            /** @todo reinit on AUDCLNT_E_DEVICEINVALIDATED? */
            rc = VERR_AUDIO_STREAM_NOT_READY;
            break;
        }

        /*
         * Get the buffer.
         */
        cFramesCaptured     = 0;
        UINT64  uQpsNtTicks = 0;
        UINT64  offDevice   = 0;
        DWORD   fBufFlags   = 0;
        BYTE   *pbData      = NULL;
        hrc = pStreamWas->pDevCfg->pIAudioCaptureClient->GetBuffer(&pbData, &cFramesCaptured, &fBufFlags, &offDevice, &uQpsNtTicks);
        Log4Func(("@%#RX64: GetBuffer -> %Rhrc pbData=%p cFramesCaptured=%#x fBufFlags=%#x offDevice=%#RX64 uQpcNtTicks=%#RX64\n",
                  pStreamWas->offInternal, hrc, pbData, cFramesCaptured, fBufFlags, offDevice, uQpsNtTicks));
        if (SUCCEEDED(hrc))
        {
            Assert(cFramesCaptured < VBOX_WASAPI_MAX_PADDING);
            pStreamWas->pbCapture               = pbData;
            pStreamWas->cFramesCaptureToRelease = cFramesCaptured;
            pStreamWas->cbCapture               = PDMAudioPropsFramesToBytes(&pStreamWas->Cfg.Props, cFramesCaptured);
            /* Just loop and re-use the copying code above. Can optimize later. */
        }
        else
        {
            LogRelMax(64, ("WasAPI: GetBuffer() failed on '%s' during capture: %Rhrc (@%#RX64)\n",
                           pStreamWas->Cfg.szName, hrc, pStreamWas->offInternal));
            /** @todo reinit on AUDCLNT_E_DEVICEINVALIDATED? */
            rc = VERR_AUDIO_STREAM_NOT_READY;
            break;
        }
    }

    /*
     * Done.
     */
    uint64_t const msPrev = pStreamWas->msLastTransfer; RT_NOREF(msPrev);
    uint64_t const msNow  = RTTimeMilliTS();
    if (cbRead)
        pStreamWas->msLastTransfer = msNow;

    RTCritSectLeave(&pStreamWas->CritSect);

    *pcbRead = cbRead;
    if (RT_SUCCESS(rc) || !cbRead)
    { }
    else
    {
        LogFlowFunc(("Suppressing %Rrc to report %#x bytes read\n", rc, cbRead));
        rc = VINF_SUCCESS;
    }
    LogFlowFunc(("@%#RX64: rc=%Rrc cbRead=%#RX32 cMsDelta=%RU64 (%RU64 -> %RU64) {%s}\n", pStreamWas->offInternal, rc, cbRead,
                 msPrev ? msNow - msPrev : 0, msPrev, pStreamWas->msLastTransfer, drvHostWasStreamStatusString(pStreamWas) ));
    return rc;
}


/*********************************************************************************************************************************
*   PDMDRVINS::IBase Interface                                                                                                   *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvHostAudioWasQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS          pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVHOSTAUDIOWAS    pThis   = PDMINS_2_DATA(pDrvIns, PDRVHOSTAUDIOWAS);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHOSTAUDIO, &pThis->IHostAudio);
    return NULL;
}


/*********************************************************************************************************************************
*   PDMDRVREG Interface                                                                                                          *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNPDMDRVDESTRUCT, pfnDestruct}
 */
static DECLCALLBACK(void) drvHostAudioWasPowerOff(PPDMDRVINS pDrvIns)
{
    PDRVHOSTAUDIOWAS pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTAUDIOWAS);

    /*
     * Start purging the cache asynchronously before we get to destruct.
     * This might speed up VM shutdown a tiny fraction and also stress
     * the shutting down of the thread pool a little.
     */
#if 0
    if (pThis->hWorkerThread != NIL_RTTHREAD)
    {
        BOOL fRc = PostThreadMessageW(pThis->idWorkerThread, WM_DRVHOSTAUDIOWAS_PURGE_CACHE, pThis->uWorkerThreadFixedParam, 0);
        LogFlowFunc(("Posted WM_DRVHOSTAUDIOWAS_PURGE_CACHE: %d\n", fRc));
        Assert(fRc); RT_NOREF(fRc);
    }
#else
    if (   pThis->fCacheEnabled
        && !RTListIsEmpty(&pThis->CacheHead) && pThis->pIHostAudioPort)
    {
        int rc = RTSemEventMultiCreate(&pThis->hEvtCachePurge);
        if (RT_SUCCESS(rc))
        {
            rc = pThis->pIHostAudioPort->pfnDoOnWorkerThread(pThis->pIHostAudioPort, NULL/*pStream*/,
                                                             DRVHOSTAUDIOWAS_DO_PURGE_CACHE, NULL /*pvUser*/);
            if (RT_FAILURE(rc))
            {
                LogFunc(("pfnDoOnWorkerThread/DRVHOSTAUDIOWAS_DO_PURGE_CACHE failed: %Rrc\n", rc));
                RTSemEventMultiDestroy(pThis->hEvtCachePurge);
                pThis->hEvtCachePurge = NIL_RTSEMEVENTMULTI;
            }
        }
    }
#endif

    /*
     * Deregister the notification client to reduce the risk of notifications
     * comming in while we're being detatched or the VM is being destroyed.
     */
    if (pThis->pNotifyClient)
    {
        pThis->pNotifyClient->notifyDriverDestroyed();
        pThis->pIEnumerator->UnregisterEndpointNotificationCallback(pThis->pNotifyClient);
        pThis->pNotifyClient->Release();
        pThis->pNotifyClient = NULL;
    }
}


/**
 * @callback_method_impl{FNPDMDRVDESTRUCT, pfnDestruct}
 */
static DECLCALLBACK(void) drvHostAudioWasDestruct(PPDMDRVINS pDrvIns)
{
    PDRVHOSTAUDIOWAS pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTAUDIOWAS);
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    LogFlowFuncEnter();

    /*
     * Release the notification client first.
     */
    if (pThis->pNotifyClient)
    {
        pThis->pNotifyClient->notifyDriverDestroyed();
        pThis->pIEnumerator->UnregisterEndpointNotificationCallback(pThis->pNotifyClient);
        pThis->pNotifyClient->Release();
        pThis->pNotifyClient = NULL;
    }

#if 0
    if (pThis->hWorkerThread != NIL_RTTHREAD)
    {
        BOOL fRc = PostThreadMessageW(pThis->idWorkerThread, WM_QUIT, 0, 0);
        Assert(fRc); RT_NOREF(fRc);

        int rc = RTThreadWait(pThis->hWorkerThread, RT_MS_15SEC, NULL);
        AssertRC(rc);
    }
#endif

    if (pThis->fCacheEnabled)
    {
        if (RTCritSectIsInitialized(&pThis->CritSectCache))
        {
            drvHostAudioWasCachePurge(pThis, false /*fOnWorker*/);
            if (pThis->hEvtCachePurge != NIL_RTSEMEVENTMULTI)
                RTSemEventMultiWait(pThis->hEvtCachePurge, RT_MS_30SEC);
            RTCritSectDelete(&pThis->CritSectCache);
        }

        if (pThis->hEvtCachePurge != NIL_RTSEMEVENTMULTI)
        {
            RTSemEventMultiDestroy(pThis->hEvtCachePurge);
            pThis->hEvtCachePurge = NIL_RTSEMEVENTMULTI;
        }
    }
    else
    {
        Assert(pThis->cCacheEntriesIn == 0);
        Assert(pThis->cCacheEntriesOut == 0);
    }

    if (pThis->pIEnumerator)
    {
        uint32_t cRefs = pThis->pIEnumerator->Release(); RT_NOREF(cRefs);
        LogFlowFunc(("cRefs=%d\n", cRefs));
    }

    if (pThis->pIDeviceOutput)
    {
        pThis->pIDeviceOutput->Release();
        pThis->pIDeviceOutput = NULL;
    }

    if (pThis->pIDeviceInput)
    {
        pThis->pIDeviceInput->Release();
        pThis->pIDeviceInput = NULL;
    }


    if (RTCritSectRwIsInitialized(&pThis->CritSectStreamList))
        RTCritSectRwDelete(&pThis->CritSectStreamList);

    LogFlowFuncLeave();
}


/**
 * @callback_method_impl{FNPDMDRVCONSTRUCT, pfnConstruct}
 */
static DECLCALLBACK(int) drvHostAudioWasConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVHOSTAUDIOWAS    pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTAUDIOWAS);
    PCPDMDRVHLPR3       pHlp  = pDrvIns->pHlpR3;
    RT_NOREF(fFlags, pCfg);

    /*
     * Init basic data members and interfaces.
     */
    pThis->pDrvIns                          = pDrvIns;
    pThis->hEvtCachePurge                   = NIL_RTSEMEVENTMULTI;
#if 0
    pThis->hWorkerThread                    = NIL_RTTHREAD;
    pThis->idWorkerThread                   = 0;
#endif
    RTListInit(&pThis->StreamHead);
    RTListInit(&pThis->CacheHead);
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface        = drvHostAudioWasQueryInterface;
    /* IHostAudio */
    pThis->IHostAudio.pfnGetConfig                  = drvHostAudioWasHA_GetConfig;
    pThis->IHostAudio.pfnGetDevices                 = drvHostAudioWasHA_GetDevices;
    pThis->IHostAudio.pfnSetDevice                  = drvHostAudioWasHA_SetDevice;
    pThis->IHostAudio.pfnGetStatus                  = drvHostAudioWasHA_GetStatus;
    pThis->IHostAudio.pfnDoOnWorkerThread           = drvHostAudioWasHA_DoOnWorkerThread;
    pThis->IHostAudio.pfnStreamConfigHint           = drvHostAudioWasHA_StreamConfigHint;
    pThis->IHostAudio.pfnStreamCreate               = drvHostAudioWasHA_StreamCreate;
    pThis->IHostAudio.pfnStreamInitAsync            = drvHostAudioWasHA_StreamInitAsync;
    pThis->IHostAudio.pfnStreamDestroy              = drvHostAudioWasHA_StreamDestroy;
    pThis->IHostAudio.pfnStreamNotifyDeviceChanged  = drvHostAudioWasHA_StreamNotifyDeviceChanged;
    pThis->IHostAudio.pfnStreamEnable               = drvHostAudioWasHA_StreamEnable;
    pThis->IHostAudio.pfnStreamDisable              = drvHostAudioWasHA_StreamDisable;
    pThis->IHostAudio.pfnStreamPause                = drvHostAudioWasHA_StreamPause;
    pThis->IHostAudio.pfnStreamResume               = drvHostAudioWasHA_StreamResume;
    pThis->IHostAudio.pfnStreamDrain                = drvHostAudioWasHA_StreamDrain;
    pThis->IHostAudio.pfnStreamGetState             = drvHostAudioWasHA_StreamGetState;
    pThis->IHostAudio.pfnStreamGetPending           = drvHostAudioWasHA_StreamGetPending;
    pThis->IHostAudio.pfnStreamGetWritable          = drvHostAudioWasHA_StreamGetWritable;
    pThis->IHostAudio.pfnStreamPlay                 = drvHostAudioWasHA_StreamPlay;
    pThis->IHostAudio.pfnStreamGetReadable          = drvHostAudioWasHA_StreamGetReadable;
    pThis->IHostAudio.pfnStreamCapture              = drvHostAudioWasHA_StreamCapture;

    /*
     * Validate and read the configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "VmName|VmUuid|InputDeviceID|OutputDeviceID|CacheEnabled", "");

    char szTmp[1024];
    int rc = pHlp->pfnCFGMQueryStringDef(pCfg, "InputDeviceID", szTmp, sizeof(szTmp), "");
    AssertMsgRCReturn(rc, ("Confguration error: Failed to read \"InputDeviceID\" as string: rc=%Rrc\n", rc), rc);
    if (szTmp[0])
    {
        rc = RTStrToUtf16(szTmp, &pThis->pwszInputDevId);
        AssertRCReturn(rc, rc);
    }

    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "OutputDeviceID", szTmp, sizeof(szTmp), "");
    AssertMsgRCReturn(rc, ("Confguration error: Failed to read \"OutputDeviceID\" as string: rc=%Rrc\n", rc), rc);
    if (szTmp[0])
    {
        rc = RTStrToUtf16(szTmp, &pThis->pwszOutputDevId);
        AssertRCReturn(rc, rc);
    }

    /* Caching is enabled by default. */
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "CacheEnabled", &pThis->fCacheEnabled, true);
    AssertMsgRCReturn(rc, ("Confguration error: Failed to read \"CacheEnabled\" as boolean: rc=%Rrc\n", rc), rc);

    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    LogRel2(("WasAPI: Caching device configuration entries is %s\n", pThis->fCacheEnabled ? "enabled" : "disabled"));

    /*
     * Initialize the critical sections early.
     */
    rc = RTCritSectRwInit(&pThis->CritSectStreamList);
    AssertRCReturn(rc, rc);

    if (pThis->fCacheEnabled)
    {
        rc = RTCritSectInit(&pThis->CritSectCache);
        AssertRCReturn(rc, rc);
    }

    /*
     * Create an enumerator instance that we can get the default devices from
     * as well as do enumeration thru.
     */
    HRESULT hrc = CoCreateInstance(__uuidof(MMDeviceEnumerator), 0, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                   (void **)&pThis->pIEnumerator);
    if (FAILED(hrc))
    {
        pThis->pIEnumerator = NULL;
        LogRel(("WasAPI: Failed to create an MMDeviceEnumerator object: %Rhrc\n", hrc));
        return VERR_AUDIO_BACKEND_INIT_FAILED;
    }
    AssertPtr(pThis->pIEnumerator);

    /*
     * Resolve the interface to the driver above us.
     */
    pThis->pIHostAudioPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIHOSTAUDIOPORT);
    AssertPtrReturn(pThis->pIHostAudioPort, VERR_PDM_MISSING_INTERFACE_ABOVE);

    /*
     * Instantiate and register the notification client with the enumerator.
     *
     * Failure here isn't considered fatal at this time as we'll just miss
     * default device changes.
     */
#ifdef RT_EXCEPTIONS_ENABLED
    try { pThis->pNotifyClient = new DrvHostAudioWasMmNotifyClient(pThis); }
    catch (std::bad_alloc &) { return VERR_NO_MEMORY; }
#else
    pThis->pNotifyClient = new DrvHostAudioWasMmNotifyClient(pThis);
    AssertReturn(pThis->pNotifyClient, VERR_NO_MEMORY);
#endif
    rc = pThis->pNotifyClient->init();
    AssertRCReturn(rc, rc);

    hrc = pThis->pIEnumerator->RegisterEndpointNotificationCallback(pThis->pNotifyClient);
    AssertMsg(SUCCEEDED(hrc), ("%Rhrc\n", hrc));
    if (FAILED(hrc))
    {
        LogRel(("WasAPI: RegisterEndpointNotificationCallback failed: %Rhrc (ignored)\n"
                "WasAPI: Warning! Will not be able to detect default device changes!\n"));
        pThis->pNotifyClient->notifyDriverDestroyed();
        pThis->pNotifyClient->Release();
        pThis->pNotifyClient = NULL;
    }

    /*
     * Retrieve the input and output device.
     */
    IMMDevice *pIDeviceInput = NULL;
    if (pThis->pwszInputDevId)
        hrc = pThis->pIEnumerator->GetDevice(pThis->pwszInputDevId, &pIDeviceInput);
    else
        hrc = pThis->pIEnumerator->GetDefaultAudioEndpoint(eCapture, eMultimedia, &pIDeviceInput);
    if (SUCCEEDED(hrc))
        LogRel(("WasAPI: Input device is: %ls (iface %p)\n",
                pThis->pwszInputDevId ? pThis->pwszInputDevId : L"{Default}", pIDeviceInput));
    else
    {
        LogRel(("WasAPI: Failed to get audio input device '%ls': %Rhrc\n",
                pThis->pwszInputDevId ? pThis->pwszInputDevId : L"{Default}", hrc));
        pIDeviceInput = NULL;
    }

    IMMDevice *pIDeviceOutput = NULL;
    if (pThis->pwszOutputDevId)
        hrc = pThis->pIEnumerator->GetDevice(pThis->pwszOutputDevId, &pIDeviceOutput);
    else
        hrc = pThis->pIEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pIDeviceOutput);
    if (SUCCEEDED(hrc))
        LogRel(("WasAPI: Output device is: %ls (iface %p)\n",
                pThis->pwszOutputDevId ? pThis->pwszOutputDevId : L"{Default}", pIDeviceOutput));
    else
    {
        LogRel(("WasAPI: Failed to get audio output device '%ls': %Rhrc\n",
                pThis->pwszOutputDevId ? pThis->pwszOutputDevId : L"{Default}", hrc));
        pIDeviceOutput = NULL;
    }

    /* Carefully place them in the instance data:  */
    pThis->pNotifyClient->lockEnter();

    if (pThis->pIDeviceInput)
        pThis->pIDeviceInput->Release();
    pThis->pIDeviceInput = pIDeviceInput;

    if (pThis->pIDeviceOutput)
        pThis->pIDeviceOutput->Release();
    pThis->pIDeviceOutput = pIDeviceOutput;

    pThis->pNotifyClient->lockLeave();

#if 0
    /*
     * Create the worker thread.  This thread has a message loop and will be
     * signalled by DrvHostAudioWasMmNotifyClient while the VM is paused/whatever,
     * so better make it a regular thread rather than PDM thread.
     */
    pThis->uWorkerThreadFixedParam = (WPARAM)RTRandU64();
    rc = RTThreadCreateF(&pThis->hWorkerThread, drvHostWasWorkerThread, pThis, 0 /*cbStack*/, RTTHREADTYPE_DEFAULT,
                         RTTHREADFLAGS_WAITABLE | RTTHREADFLAGS_COM_MTA, "WasWork%u", pDrvIns->iInstance);
    AssertRCReturn(rc, rc);

    rc = RTThreadUserWait(pThis->hWorkerThread, RT_MS_10SEC);
    AssertRC(rc);
#endif

    /*
     * Prime the cache.
     */
    if (pThis->fCacheEnabled)
        drvHostAudioWasCacheFill(pThis);

    return VINF_SUCCESS;
}


/**
 * PDM driver registration for WasAPI.
 */
const PDMDRVREG g_DrvHostAudioWas =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "HostAudioWas",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Windows Audio Session API (WASAPI) host audio driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_AUDIO,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVHOSTAUDIOWAS),
    /* pfnConstruct */
    drvHostAudioWasConstruct,
    /* pfnDestruct */
    drvHostAudioWasDestruct,
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
    drvHostAudioWasPowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

