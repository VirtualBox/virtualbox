/* $Id: DrvHostAudioDSound.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Host audio driver - DirectSound (Windows).
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
#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#define INITGUID
#include <VBox/log.h>
#include <iprt/win/windows.h>
#include <dsound.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <iprt/win/mmreg.h> /* WAVEFORMATEXTENSIBLE */

#include <iprt/alloc.h>
#include <iprt/system.h>
#include <iprt/uuid.h>
#include <iprt/utf16.h>

#include <VBox/vmm/pdmaudioinline.h>
#include <VBox/vmm/pdmaudiohostenuminline.h>

#include "VBoxDD.h"

#ifdef VBOX_WITH_AUDIO_MMNOTIFICATION_CLIENT
# include <new> /* For bad_alloc. */
# include "DrvHostAudioDSoundMMNotifClient.h"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/*
 * Optional release logging, which a user can turn on with the
 * 'VBoxManage debugvm' command.
 * Debug logging still uses the common Log* macros from VBox.
 * Messages which always should go to the release log use LogRel.
 *
 * @deprecated Use LogRelMax, LogRel2 and LogRel3 directly.
 */
/** General code behavior. */
#define DSLOG(a)    do { LogRel2(a); } while(0)
/** Something which produce a lot of logging during playback/recording. */
#define DSLOGF(a)   do { LogRel3(a); } while(0)
/** Important messages like errors. Limited in the default release log to avoid log flood. */
#define DSLOGREL(a) \
    do {  \
        static int8_t s_cLogged = 0; \
        if (s_cLogged < 8) { \
            ++s_cLogged; \
            LogRel(a); \
        } else DSLOG(a); \
    } while (0)

/** Maximum number of attempts to restore the sound buffer before giving up. */
#define DRV_DSOUND_RESTORE_ATTEMPTS_MAX         3
#if 0 /** @todo r=bird: What are these for? Nobody is using them... */
/** Default input latency (in ms). */
#define DRV_DSOUND_DEFAULT_LATENCY_MS_IN        50
/** Default output latency (in ms). */
#define DRV_DSOUND_DEFAULT_LATENCY_MS_OUT       50
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/* Dynamically load dsound.dll. */
typedef HRESULT WINAPI FNDIRECTSOUNDENUMERATEW(LPDSENUMCALLBACKW pDSEnumCallback, PVOID pContext);
typedef FNDIRECTSOUNDENUMERATEW *PFNDIRECTSOUNDENUMERATEW;
typedef HRESULT WINAPI FNDIRECTSOUNDCAPTUREENUMERATEW(LPDSENUMCALLBACKW pDSEnumCallback, PVOID pContext);
typedef FNDIRECTSOUNDCAPTUREENUMERATEW *PFNDIRECTSOUNDCAPTUREENUMERATEW;
typedef HRESULT WINAPI FNDIRECTSOUNDCAPTURECREATE8(LPCGUID lpcGUID, LPDIRECTSOUNDCAPTURE8 *lplpDSC, LPUNKNOWN pUnkOuter);
typedef FNDIRECTSOUNDCAPTURECREATE8 *PFNDIRECTSOUNDCAPTURECREATE8;

#define VBOX_DSOUND_MAX_EVENTS 3

typedef enum DSOUNDEVENT
{
    DSOUNDEVENT_NOTIFY = 0,
    DSOUNDEVENT_INPUT,
    DSOUNDEVENT_OUTPUT,
} DSOUNDEVENT;

typedef struct DSOUNDHOSTCFG
{
    RTUUID          uuidPlay;
    LPCGUID         pGuidPlay;
    RTUUID          uuidCapture;
    LPCGUID         pGuidCapture;
} DSOUNDHOSTCFG, *PDSOUNDHOSTCFG;

typedef struct DSOUNDSTREAM
{
    /** Common part. */
    PDMAUDIOBACKENDSTREAM   Core;
    /** Entry in DRVHOSTDSOUND::HeadStreams. */
    RTLISTNODE              ListEntry;
    /** The stream's acquired configuration. */
    PDMAUDIOSTREAMCFG       Cfg;
    /** Buffer alignment. */
    uint8_t                 uAlign;
    /** Whether this stream is in an enable state on the DirectSound side. */
    bool                    fEnabled;
    bool                    afPadding[2];
    /** Size (in bytes) of the DirectSound buffer. */
    DWORD                   cbBufSize;
    union
    {
        struct
        {
            /** The actual DirectSound Buffer (DSB) used for the capturing.
             *  This is a secondary buffer and is used as a streaming buffer. */
            LPDIRECTSOUNDCAPTUREBUFFER8 pDSCB;
            /** Current read offset (in bytes) within the DSB. */
            DWORD                       offReadPos;
            /** Number of buffer overruns happened. Used for logging. */
            uint8_t                     cOverruns;
        } In;
        struct
        {
            /** The actual DirectSound Buffer (DSB) used for playback.
             *  This is a secondary buffer and is used as a streaming buffer. */
            LPDIRECTSOUNDBUFFER8        pDSB;
            /** Current write offset (in bytes) within the DSB.
             * @note This is needed as the current write position as kept by direct sound
             *       will move ahead if we're too late. */
            DWORD                       offWritePos;
            /** Offset of last play cursor within the DSB when checked for pending. */
            DWORD                       offPlayCursorLastPending;
            /** Offset of last play cursor within the DSB when last played. */
            DWORD                       offPlayCursorLastPlayed;
            /** Total amount (in bytes) written to our internal ring buffer. */
            uint64_t                    cbWritten;
            /** Total amount (in bytes) played (to the DirectSound buffer). */
            uint64_t                    cbTransferred;
            /** Flag indicating whether playback was just (re)started. */
            bool                        fFirstTransfer;
            /** Flag indicating whether this stream is in draining mode, e.g. no new
             *  data is being written to it but DirectSound still needs to be able to
             *  play its remaining (buffered) data. */
            bool                        fDrain;
            /** How much (in bytes) the last transfer from the internal buffer
             *  to the DirectSound buffer was. */
            uint32_t                    cbLastTransferred;
            /** The RTTimeMilliTS() deadline for the draining of this stream. */
            uint64_t                    msDrainDeadline;
        } Out;
    };
    /** Timestamp (in ms) of the last transfer from the internal buffer to/from the
     *  DirectSound buffer. */
    uint64_t                msLastTransfer;
    /** The stream's critical section for synchronizing access. */
    RTCRITSECT              CritSect;
    /** Used for formatting the current DSound status. */
    char                    szStatus[127];
    /** Fixed zero terminator. */
    char const              chStateZero;
} DSOUNDSTREAM, *PDSOUNDSTREAM;

/**
 * DirectSound-specific device entry.
 */
typedef struct DSOUNDDEV
{
    PDMAUDIOHOSTDEV  Core;
    /** The GUID if handy. */
    GUID            Guid;
    /** The GUID as a string (empty if default). */
    char            szGuid[RTUUID_STR_LENGTH];
} DSOUNDDEV;
/** Pointer to a DirectSound device entry. */
typedef DSOUNDDEV *PDSOUNDDEV;

/**
 * Structure for holding a device enumeration context.
 */
typedef struct DSOUNDENUMCBCTX
{
    /** Enumeration flags. */
    uint32_t            fFlags;
    /** Pointer to device list to populate. */
    PPDMAUDIOHOSTENUM pDevEnm;
} DSOUNDENUMCBCTX, *PDSOUNDENUMCBCTX;

typedef struct DRVHOSTDSOUND
{
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Our audio host audio interface. */
    PDMIHOSTAUDIO               IHostAudio;
    /** Critical section to serialize access. */
    RTCRITSECT                  CritSect;
    /** DirectSound configuration options. */
    DSOUNDHOSTCFG               Cfg;
    /** List of devices of last enumeration. */
    PDMAUDIOHOSTENUM            DeviceEnum;
    /** Whether this backend supports any audio input.
     * @todo r=bird: This is not actually used for anything. */
    bool                        fEnabledIn;
    /** Whether this backend supports any audio output.
     * @todo r=bird: This is not actually used for anything. */
    bool                        fEnabledOut;
    /** The Direct Sound playback interface. */
    LPDIRECTSOUND8              pDS;
    /** The Direct Sound capturing interface. */
    LPDIRECTSOUNDCAPTURE8       pDSC;
    /** List of streams (DSOUNDSTREAM).
     * Requires CritSect ownership.  */
    RTLISTANCHOR                HeadStreams;

#ifdef VBOX_WITH_AUDIO_MMNOTIFICATION_CLIENT
    DrvHostAudioDSoundMMNotifClient *m_pNotificationClient;
#endif
} DRVHOSTDSOUND, *PDRVHOSTDSOUND;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static HRESULT  directSoundPlayRestore(PDRVHOSTDSOUND pThis, LPDIRECTSOUNDBUFFER8 pDSB);
static int      drvHostDSoundStreamStopPlayback(PDRVHOSTDSOUND pThis, PDSOUNDSTREAM pStreamDS, bool fReset);


#if defined(LOG_ENABLED) || defined(RTLOG_REL_ENABLED)
/**
 * Gets the stream status as a string for logging purposes.
 *
 * @returns Status string (pStreamDS->szStatus).
 * @param   pStreamDS   The stream to get the status for.
 */
static const char *drvHostDSoundStreamStatusString(PDSOUNDSTREAM pStreamDS)
{
    /*
     * Out internal stream status first.
     */
    size_t off;
    if (pStreamDS->fEnabled)
    {
        memcpy(pStreamDS->szStatus, RT_STR_TUPLE("ENABLED "));
        off = sizeof("ENABLED ") - 1;
    }
    else
    {
        memcpy(pStreamDS->szStatus, RT_STR_TUPLE("DISABLED"));
        off = sizeof("DISABLED") - 1;
    }

    /*
     * Direction specific stuff, returning with a status DWORD and string mappings for it.
     */
    typedef struct DRVHOSTDSOUNDSFLAGS2STR
    {
        const char *pszMnemonic;
        uint32_t    cchMnemonic;
        uint32_t    fFlag;
    } DRVHOSTDSOUNDSFLAGS2STR;
    static const DRVHOSTDSOUNDSFLAGS2STR s_aCaptureFlags[] =
    {
        { RT_STR_TUPLE(" CAPTURING"),    DSCBSTATUS_CAPTURING  },
        { RT_STR_TUPLE(" LOOPING"),      DSCBSTATUS_LOOPING    },
    };
    static const DRVHOSTDSOUNDSFLAGS2STR s_aPlaybackFlags[] =
    {
        { RT_STR_TUPLE(" PLAYING"),      DSBSTATUS_PLAYING     },
        { RT_STR_TUPLE(" BUFFERLOST"),   DSBSTATUS_BUFFERLOST  },
        { RT_STR_TUPLE(" LOOPING"),      DSBSTATUS_LOOPING     },
        { RT_STR_TUPLE(" LOCHARDWARE"),  DSBSTATUS_LOCHARDWARE },
        { RT_STR_TUPLE(" LOCSOFTWARE"),  DSBSTATUS_LOCSOFTWARE },
        { RT_STR_TUPLE(" TERMINATED"),   DSBSTATUS_TERMINATED  },
    };
    DRVHOSTDSOUNDSFLAGS2STR const  *paMappings = NULL;
    size_t                          cMappings  = 0;
    DWORD                           fStatus    = 0;
    if (pStreamDS->Cfg.enmDir == PDMAUDIODIR_IN)
    {
        if (pStreamDS->In.pDSCB)
        {
            HRESULT hrc = pStreamDS->In.pDSCB->GetStatus(&fStatus);
            if (SUCCEEDED(hrc))
            {
                paMappings = s_aCaptureFlags;
                cMappings  = RT_ELEMENTS(s_aCaptureFlags);
            }
            else
                RTStrPrintf(&pStreamDS->szStatus[off], sizeof(pStreamDS->szStatus) - off, "GetStatus->%Rhrc", hrc);
        }
        else
            RTStrCopy(&pStreamDS->szStatus[off], sizeof(pStreamDS->szStatus) - off, "NO-DSCB");
    }
    else if (pStreamDS->Cfg.enmDir == PDMAUDIODIR_OUT)
    {
        if (pStreamDS->Out.fDrain)
        {
            memcpy(&pStreamDS->szStatus[off], RT_STR_TUPLE(" DRAINING"));
            off += sizeof(" DRAINING") - 1;
        }

        if (pStreamDS->Out.fFirstTransfer)
        {
            memcpy(&pStreamDS->szStatus[off], RT_STR_TUPLE(" NOXFER"));
            off += sizeof(" NOXFER") - 1;
        }

        if (pStreamDS->Out.pDSB)
        {
            HRESULT hrc = pStreamDS->Out.pDSB->GetStatus(&fStatus);
            if (SUCCEEDED(hrc))
            {
                paMappings = s_aPlaybackFlags;
                cMappings  = RT_ELEMENTS(s_aPlaybackFlags);
            }
            else
                RTStrPrintf(&pStreamDS->szStatus[off], sizeof(pStreamDS->szStatus) - off, "GetStatus->%Rhrc", hrc);
        }
        else
            RTStrCopy(&pStreamDS->szStatus[off], sizeof(pStreamDS->szStatus) - off, "NO-DSB");
    }
    else
        RTStrCopy(&pStreamDS->szStatus[off], sizeof(pStreamDS->szStatus) - off, "BAD-DIR");

    /* Format flags. */
    if (paMappings)
    {
        if (fStatus == 0)
            RTStrCopy(&pStreamDS->szStatus[off], sizeof(pStreamDS->szStatus) - off, " 0");
        else
        {
            for (size_t i = 0; i < cMappings; i++)
                if (fStatus & paMappings[i].fFlag)
                {
                    memcpy(&pStreamDS->szStatus[off], paMappings[i].pszMnemonic, paMappings[i].cchMnemonic);
                    off += paMappings[i].cchMnemonic;

                    fStatus &= ~paMappings[i].fFlag;
                    if (!fStatus)
                        break;
                }
            if (fStatus != 0)
                off += RTStrPrintf(&pStreamDS->szStatus[off], sizeof(pStreamDS->szStatus) - off, " %#x", fStatus);
        }
    }

    /*
     * Finally, terminate the string.  By postponing it this long, it won't be
     * a big deal if two threads go thru here at the same time as long as the
     * status is the same.
     */
    Assert(off < sizeof(pStreamDS->szStatus));
    pStreamDS->szStatus[off] = '\0';

    return pStreamDS->szStatus;
}
#endif /* LOG_ENABLED || RTLOG_REL_ENABLED */


static DWORD dsoundRingDistance(DWORD offEnd, DWORD offBegin, DWORD cSize)
{
    AssertReturn(offEnd <= cSize,   0);
    AssertReturn(offBegin <= cSize, 0);

    return offEnd >= offBegin ? offEnd - offBegin : cSize - offBegin + offEnd;
}


static char *dsoundGUIDToUtf8StrA(LPCGUID pGUID)
{
    if (pGUID)
    {
        LPOLESTR lpOLEStr;
        HRESULT hr = StringFromCLSID(*pGUID, &lpOLEStr);
        if (SUCCEEDED(hr))
        {
            char *pszGUID;
            int rc = RTUtf16ToUtf8(lpOLEStr, &pszGUID);
            CoTaskMemFree(lpOLEStr);

            return RT_SUCCESS(rc) ? pszGUID : NULL;
        }
    }

    return RTStrDup("{Default device}");
}


static HRESULT directSoundPlayRestore(PDRVHOSTDSOUND pThis, LPDIRECTSOUNDBUFFER8 pDSB)
{
    RT_NOREF(pThis);
    HRESULT hr = IDirectSoundBuffer8_Restore(pDSB);
    if (FAILED(hr))
        DSLOG(("DSound: Restoring playback buffer\n"));
    else
        DSLOGREL(("DSound: Restoring playback buffer failed with %Rhrc\n", hr));

    return hr;
}


static HRESULT directSoundPlayUnlock(PDRVHOSTDSOUND pThis, LPDIRECTSOUNDBUFFER8 pDSB,
                                     PVOID pv1, PVOID pv2,
                                     DWORD cb1, DWORD cb2)
{
    RT_NOREF(pThis);
    HRESULT hr = IDirectSoundBuffer8_Unlock(pDSB, pv1, cb1, pv2, cb2);
    if (FAILED(hr))
        DSLOGREL(("DSound: Unlocking playback buffer failed with %Rhrc\n", hr));
    return hr;
}


static HRESULT directSoundPlayLock(PDRVHOSTDSOUND pThis, PDSOUNDSTREAM pStreamDS,
                                   DWORD dwOffset, DWORD dwBytes,
                                   PVOID *ppv1, PVOID *ppv2,
                                   DWORD *pcb1, DWORD *pcb2,
                                   DWORD dwFlags)
{
    AssertReturn(dwBytes, VERR_INVALID_PARAMETER);

    HRESULT hr = E_FAIL;
    AssertCompile(DRV_DSOUND_RESTORE_ATTEMPTS_MAX > 0);
    for (unsigned i = 0; i < DRV_DSOUND_RESTORE_ATTEMPTS_MAX; i++)
    {
        PVOID pv1, pv2;
        DWORD cb1, cb2;
        hr = IDirectSoundBuffer8_Lock(pStreamDS->Out.pDSB, dwOffset, dwBytes, &pv1, &cb1, &pv2, &cb2, dwFlags);
        if (SUCCEEDED(hr))
        {
            if (   (!pv1 || !(cb1 & pStreamDS->uAlign))
                && (!pv2 || !(cb2 & pStreamDS->uAlign)))
            {
                if (ppv1)
                    *ppv1 = pv1;
                if (ppv2)
                    *ppv2 = pv2;
                if (pcb1)
                    *pcb1 = cb1;
                if (pcb2)
                    *pcb2 = cb2;
                return S_OK;
            }
            DSLOGREL(("DSound: Locking playback buffer returned misaligned buffer: cb1=%#RX32, cb2=%#RX32 (alignment: %#RX32)\n",
                      *pcb1, *pcb2, pStreamDS->uAlign));
            directSoundPlayUnlock(pThis, pStreamDS->Out.pDSB, pv1, pv2, cb1, cb2);
            return E_FAIL;
        }

        if (hr != DSERR_BUFFERLOST)
            break;

        LogFlowFunc(("Locking failed due to lost buffer, restoring ...\n"));
        directSoundPlayRestore(pThis, pStreamDS->Out.pDSB);
    }

    DSLOGREL(("DSound: Locking playback buffer failed with %Rhrc (dwOff=%ld, dwBytes=%ld)\n", hr, dwOffset, dwBytes));
    return hr;
}


static HRESULT directSoundCaptureUnlock(LPDIRECTSOUNDCAPTUREBUFFER8 pDSCB,
                                        PVOID pv1, PVOID pv2,
                                        DWORD cb1, DWORD cb2)
{
    HRESULT hr = IDirectSoundCaptureBuffer8_Unlock(pDSCB, pv1, cb1, pv2, cb2);
    if (FAILED(hr))
        DSLOGREL(("DSound: Unlocking capture buffer failed with %Rhrc\n", hr));
    return hr;
}


static HRESULT directSoundCaptureLock(PDSOUNDSTREAM pStreamDS,
                                      DWORD dwOffset, DWORD dwBytes,
                                      PVOID *ppv1, PVOID *ppv2,
                                      DWORD *pcb1, DWORD *pcb2,
                                      DWORD dwFlags)
{
    PVOID pv1 = NULL;
    PVOID pv2 = NULL;
    DWORD cb1 = 0;
    DWORD cb2 = 0;

    HRESULT hr = IDirectSoundCaptureBuffer8_Lock(pStreamDS->In.pDSCB, dwOffset, dwBytes,
                                                 &pv1, &cb1, &pv2, &cb2, dwFlags);
    if (FAILED(hr))
    {
        DSLOGREL(("DSound: Locking capture buffer failed with %Rhrc\n", hr));
        return hr;
    }

    if (   (pv1 && (cb1 & pStreamDS->uAlign))
        || (pv2 && (cb2 & pStreamDS->uAlign)))
    {
        DSLOGREL(("DSound: Locking capture buffer returned misaligned buffer: cb1=%RI32, cb2=%RI32 (alignment: %RU32)\n",
                  cb1, cb2, pStreamDS->uAlign));
        directSoundCaptureUnlock(pStreamDS->In.pDSCB, pv1, pv2, cb1, cb2);
        return E_FAIL;
    }

    *ppv1 = pv1;
    *ppv2 = pv2;
    *pcb1 = cb1;
    *pcb2 = cb2;

    return S_OK;
}


/*
 * DirectSound playback
 */

/**
 * Creates a DirectSound playback instance.
 *
 * @return  HRESULT
 * @param   pGUID   GUID of device to create the playback interface for. NULL
 *                  for the default device.
 * @param   ppDS    Where to return the interface to the created instance.
 */
static HRESULT drvHostDSoundCreateDSPlaybackInstance(LPCGUID pGUID, LPDIRECTSOUND8 *ppDS)
{
    LogFlowFuncEnter();

    LPDIRECTSOUND8 pDS = NULL;
    HRESULT        hrc = CoCreateInstance(CLSID_DirectSound8, NULL, CLSCTX_ALL, IID_IDirectSound8, (void **)&pDS);
    if (SUCCEEDED(hrc))
    {
        hrc = IDirectSound8_Initialize(pDS, pGUID);
        if (SUCCEEDED(hrc))
        {
            HWND hWnd = GetDesktopWindow();
            hrc = IDirectSound8_SetCooperativeLevel(pDS, hWnd, DSSCL_PRIORITY);
            if (SUCCEEDED(hrc))
            {
                *ppDS = pDS;
                LogFlowFunc(("LEAVE S_OK\n"));
                return S_OK;
            }
            LogRelMax(64, ("DSound: Setting cooperative level for (hWnd=%p) failed: %Rhrc\n", hWnd, hrc));
        }
        else if (hrc == DSERR_NODRIVER) /* Usually means that no playback devices are attached. */
            LogRelMax(64, ("DSound: DirectSound playback is currently unavailable\n"));
        else
            LogRelMax(64, ("DSound: DirectSound playback initialization failed: %Rhrc\n", hrc));

        IDirectSound8_Release(pDS);
    }
    else
        LogRelMax(64, ("DSound: Creating playback instance failed: %Rhrc\n", hrc));

    LogFlowFunc(("LEAVE %Rhrc\n", hrc));
    return hrc;
}


#if 0 /* not used */
static HRESULT directSoundPlayGetStatus(PDRVHOSTDSOUND pThis, LPDIRECTSOUNDBUFFER8 pDSB, DWORD *pdwStatus)
{
    AssertPtrReturn(pThis, E_POINTER);
    AssertPtrReturn(pDSB,  E_POINTER);

    AssertPtrNull(pdwStatus);

    DWORD dwStatus = 0;
    HRESULT hr = E_FAIL;
    for (unsigned i = 0; i < DRV_DSOUND_RESTORE_ATTEMPTS_MAX; i++)
    {
        hr = IDirectSoundBuffer8_GetStatus(pDSB, &dwStatus);
        if (   hr == DSERR_BUFFERLOST
            || (   SUCCEEDED(hr)
                && (dwStatus & DSBSTATUS_BUFFERLOST)))
        {
            LogFlowFunc(("Getting status failed due to lost buffer, restoring ...\n"));
            directSoundPlayRestore(pThis, pDSB);
        }
        else
            break;
    }

    if (SUCCEEDED(hr))
    {
        if (pdwStatus)
            *pdwStatus = dwStatus;
    }
    else
        DSLOGREL(("DSound: Retrieving playback status failed with %Rhrc\n", hr));

    return hr;
}
#endif


/*
 * DirectSoundCapture
 */

#if 0 /* unused */
static LPCGUID dsoundCaptureSelectDevice(PDRVHOSTDSOUND pThis, PPDMAUDIOSTREAMCFG pCfg)
{
    AssertPtrReturn(pThis, NULL);
    AssertPtrReturn(pCfg,  NULL);

    int rc = VINF_SUCCESS;

    LPCGUID pGUID = pThis->Cfg.pGuidCapture;
    if (!pGUID)
    {
        PDSOUNDDEV pDev = NULL;
        switch (pCfg->enmPath)
        {
            case PDMAUDIOPATH_IN_LINE:
                /*
                 * At the moment we're only supporting line-in in the HDA emulation,
                 * and line-in + mic-in in the AC'97 emulation both are expected
                 * to use the host's mic-in as well.
                 *
                 * So the fall through here is intentional for now.
                 */
            case PDMAUDIOPATH_IN_MIC:
                pDev = (PDSOUNDDEV)DrvAudioHlpDeviceEnumGetDefaultDevice(&pThis->DeviceEnum, PDMAUDIODIR_IN);
                break;

            default:
                AssertFailedStmt(rc = VERR_NOT_SUPPORTED);
                break;
        }

        if (   RT_SUCCESS(rc)
            && pDev)
        {
            DSLOG(("DSound: Guest source '%s' is using host recording device '%s'\n",
                   PDMAudioPathGetName(pCfg->enmPath), pDev->Core.szName));
            pGUID = &pDev->Guid;
        }
        if (RT_FAILURE(rc))
        {
            LogRel(("DSound: Selecting recording device failed with %Rrc\n", rc));
            return NULL;
        }
    }

    /* This always has to be in the release log. */
    char *pszGUID = dsoundGUIDToUtf8StrA(pGUID);
    LogRel(("DSound: Guest source '%s' is using host recording device with GUID '%s'\n",
            PDMAudioPathGetName(pCfg->enmPath), pszGUID ? pszGUID: "{?}"));
    RTStrFree(pszGUID);

    return pGUID;
}
#endif


/**
 * Creates a DirectSound capture instance.
 *
 * @returns HRESULT
 * @param   pGUID   GUID of device to create the capture interface for.  NULL
 *                  for default.
 * @param   ppDSC   Where to return the interface to the created instance.
 */
static HRESULT drvHostDSoundCreateDSCaptureInstance(LPCGUID pGUID, LPDIRECTSOUNDCAPTURE8 *ppDSC)
{
    LogFlowFuncEnter();

    LPDIRECTSOUNDCAPTURE8 pDSC = NULL;
    HRESULT hrc = CoCreateInstance(CLSID_DirectSoundCapture8, NULL, CLSCTX_ALL, IID_IDirectSoundCapture8, (void **)&pDSC);
    if (SUCCEEDED(hrc))
    {
        hrc = IDirectSoundCapture_Initialize(pDSC, pGUID);
        if (SUCCEEDED(hrc))
        {
            *ppDSC = pDSC;
            LogFlowFunc(("LEAVE S_OK\n"));
            return S_OK;
        }
        if (hrc == DSERR_NODRIVER) /* Usually means that no capture devices are attached. */
            LogRelMax(64, ("DSound: Capture device currently is unavailable\n"));
        else
            LogRelMax(64, ("DSound: Initializing capturing device failed: %Rhrc\n", hrc));
        IDirectSoundCapture_Release(pDSC);
    }
    else
        LogRelMax(64, ("DSound: Creating capture instance failed: %Rhrc\n", hrc));

    LogFlowFunc(("LEAVE %Rhrc\n", hrc));
    return hrc;
}


/*********************************************************************************************************************************
*   PDMIHOSTAUDIO                                                                                                                *
*********************************************************************************************************************************/

/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetConfig}
 */
static DECLCALLBACK(int) drvHostDSoundHA_GetConfig(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDCFG pBackendCfg)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pInterface,  VERR_INVALID_POINTER);
    AssertPtrReturn(pBackendCfg, VERR_INVALID_POINTER);


    /*
     * Fill in the config structure.
     */
    RTStrCopy(pBackendCfg->szName, sizeof(pBackendCfg->szName), "DirectSound");
    pBackendCfg->cbStream       = sizeof(DSOUNDSTREAM);
    pBackendCfg->fFlags         = 0;
    pBackendCfg->cMaxStreamsIn  = UINT32_MAX;
    pBackendCfg->cMaxStreamsOut = UINT32_MAX;

    return VINF_SUCCESS;
}


/**
 * Callback for the playback device enumeration.
 *
 * @return  TRUE if continuing enumeration, FALSE if not.
 * @param   pGUID               Pointer to GUID of enumerated device. Can be NULL.
 * @param   pwszDescription     Pointer to (friendly) description of enumerated device.
 * @param   pwszModule          Pointer to module name of enumerated device.
 * @param   lpContext           Pointer to PDSOUNDENUMCBCTX context for storing the enumerated information.
 *
 * @note    Carbon copy of drvHostDSoundEnumOldStyleCaptureCallback with OUT direction.
 */
static BOOL CALLBACK drvHostDSoundEnumOldStylePlaybackCallback(LPGUID pGUID, LPCWSTR pwszDescription,
                                                               LPCWSTR pwszModule, PVOID lpContext)
{
    PDSOUNDENUMCBCTX pEnumCtx = (PDSOUNDENUMCBCTX)lpContext;
    AssertPtrReturn(pEnumCtx, FALSE);

    PPDMAUDIOHOSTENUM pDevEnm = pEnumCtx->pDevEnm;
    AssertPtrReturn(pDevEnm, FALSE);

    AssertPtrNullReturn(pGUID, FALSE); /* pGUID can be NULL for default device(s). */
    AssertPtrReturn(pwszDescription, FALSE);
    RT_NOREF(pwszModule); /* Do not care about pwszModule. */

    int rc;
    size_t const cbName = RTUtf16CalcUtf8Len(pwszDescription) + 1;
    PDSOUNDDEV pDev = (PDSOUNDDEV)PDMAudioHostDevAlloc(sizeof(DSOUNDDEV), cbName, 0);
    if (pDev)
    {
        pDev->Core.enmUsage = PDMAUDIODIR_OUT;
        pDev->Core.enmType  = PDMAUDIODEVICETYPE_BUILTIN;

        if (pGUID == NULL)
            pDev->Core.fFlags = PDMAUDIOHOSTDEV_F_DEFAULT_OUT;

        rc = RTUtf16ToUtf8Ex(pwszDescription, RTSTR_MAX, &pDev->Core.pszName, cbName, NULL);
        if (RT_SUCCESS(rc))
        {
            if (!pGUID)
                pDev->Core.fFlags |= PDMAUDIOHOSTDEV_F_DEFAULT_OUT;
            else
            {
                memcpy(&pDev->Guid, pGUID, sizeof(pDev->Guid));
                rc = RTUuidToStr((PCRTUUID)pGUID, pDev->szGuid, sizeof(pDev->szGuid));
                AssertRC(rc);
            }
            pDev->Core.pszId = &pDev->szGuid[0];

            PDMAudioHostEnumAppend(pDevEnm, &pDev->Core);

            /* Note: Querying the actual device information will be done at some
             *       later point in time outside this enumeration callback to prevent
             *       DSound hangs. */
            return TRUE;
        }
        PDMAudioHostDevFree(&pDev->Core);
    }
    else
        rc = VERR_NO_MEMORY;

    LogRel(("DSound: Error enumeration playback device '%ls': rc=%Rrc\n", pwszDescription, rc));
    return FALSE; /* Abort enumeration. */
}


/**
 * Callback for the capture device enumeration.
 *
 * @return  TRUE if continuing enumeration, FALSE if not.
 * @param   pGUID               Pointer to GUID of enumerated device. Can be NULL.
 * @param   pwszDescription     Pointer to (friendly) description of enumerated device.
 * @param   pwszModule          Pointer to module name of enumerated device.
 * @param   lpContext           Pointer to PDSOUNDENUMCBCTX context for storing the enumerated information.
 *
 * @note    Carbon copy of drvHostDSoundEnumOldStylePlaybackCallback with IN direction.
 */
static BOOL CALLBACK drvHostDSoundEnumOldStyleCaptureCallback(LPGUID pGUID, LPCWSTR pwszDescription,
                                                              LPCWSTR pwszModule, PVOID lpContext)
{
    PDSOUNDENUMCBCTX pEnumCtx = (PDSOUNDENUMCBCTX )lpContext;
    AssertPtrReturn(pEnumCtx, FALSE);

    PPDMAUDIOHOSTENUM pDevEnm = pEnumCtx->pDevEnm;
    AssertPtrReturn(pDevEnm, FALSE);

    AssertPtrNullReturn(pGUID, FALSE); /* pGUID can be NULL for default device(s). */
    AssertPtrReturn(pwszDescription, FALSE);
    RT_NOREF(pwszModule); /* Do not care about pwszModule. */

    int rc;
    size_t const cbName = RTUtf16CalcUtf8Len(pwszDescription) + 1;
    PDSOUNDDEV pDev = (PDSOUNDDEV)PDMAudioHostDevAlloc(sizeof(DSOUNDDEV), cbName, 0);
    if (pDev)
    {
        pDev->Core.enmUsage = PDMAUDIODIR_IN;
        pDev->Core.enmType  = PDMAUDIODEVICETYPE_BUILTIN;

        rc = RTUtf16ToUtf8Ex(pwszDescription, RTSTR_MAX, &pDev->Core.pszName, cbName, NULL);
        if (RT_SUCCESS(rc))
        {
            if (!pGUID)
                pDev->Core.fFlags |= PDMAUDIOHOSTDEV_F_DEFAULT_IN;
            else
            {
                memcpy(&pDev->Guid, pGUID, sizeof(pDev->Guid));
                rc = RTUuidToStr((PCRTUUID)pGUID, pDev->szGuid, sizeof(pDev->szGuid));
                AssertRC(rc);
            }
            pDev->Core.pszId = &pDev->szGuid[0];

            PDMAudioHostEnumAppend(pDevEnm, &pDev->Core);

            /* Note: Querying the actual device information will be done at some
             *       later point in time outside this enumeration callback to prevent
             *       DSound hangs. */
            return TRUE;
        }
        PDMAudioHostDevFree(&pDev->Core);
    }
    else
        rc = VERR_NO_MEMORY;

    LogRel(("DSound: Error enumeration capture device '%ls', rc=%Rrc\n", pwszDescription, rc));
    return FALSE; /* Abort enumeration. */
}


/**
 * Queries information for a given (DirectSound) device.
 *
 * @returns VBox status code.
 * @param   pDev                Audio device to query information for.
 */
static int drvHostDSoundEnumOldStyleQueryDeviceInfo(PDSOUNDDEV pDev)
{
    AssertPtr(pDev);
    int rc;

    if (pDev->Core.enmUsage == PDMAUDIODIR_OUT)
    {
        LPDIRECTSOUND8 pDS;
        HRESULT hr = drvHostDSoundCreateDSPlaybackInstance(&pDev->Guid, &pDS);
        if (SUCCEEDED(hr))
        {
            DSCAPS DSCaps;
            RT_ZERO(DSCaps);
            DSCaps.dwSize = sizeof(DSCAPS);
            hr = IDirectSound_GetCaps(pDS, &DSCaps);
            if (SUCCEEDED(hr))
            {
                pDev->Core.cMaxOutputChannels = DSCaps.dwFlags & DSCAPS_PRIMARYSTEREO ? 2 : 1;

                DWORD dwSpeakerCfg;
                hr = IDirectSound_GetSpeakerConfig(pDS, &dwSpeakerCfg);
                if (SUCCEEDED(hr))
                {
                    unsigned uSpeakerCount = 0;
                    switch (DSSPEAKER_CONFIG(dwSpeakerCfg))
                    {
                        case DSSPEAKER_MONO:             uSpeakerCount = 1; break;
                        case DSSPEAKER_HEADPHONE:        uSpeakerCount = 2; break;
                        case DSSPEAKER_STEREO:           uSpeakerCount = 2; break;
                        case DSSPEAKER_QUAD:             uSpeakerCount = 4; break;
                        case DSSPEAKER_SURROUND:         uSpeakerCount = 4; break;
                        case DSSPEAKER_5POINT1:          uSpeakerCount = 6; break;
                        case DSSPEAKER_5POINT1_SURROUND: uSpeakerCount = 6; break;
                        case DSSPEAKER_7POINT1:          uSpeakerCount = 8; break;
                        case DSSPEAKER_7POINT1_SURROUND: uSpeakerCount = 8; break;
                        default:                                            break;
                    }

                    if (uSpeakerCount) /* Do we need to update the channel count? */
                        pDev->Core.cMaxOutputChannels = uSpeakerCount;

                    rc = VINF_SUCCESS;
                }
                else
                {
                    LogRel(("DSound: Error retrieving playback device speaker config, hr=%Rhrc\n", hr));
                    rc = VERR_ACCESS_DENIED; /** @todo Fudge! */
                }
            }
            else
            {
                LogRel(("DSound: Error retrieving playback device capabilities, hr=%Rhrc\n", hr));
                rc = VERR_ACCESS_DENIED; /** @todo Fudge! */
            }

            IDirectSound8_Release(pDS);
        }
        else
            rc = VERR_GENERAL_FAILURE;
    }
    else if (pDev->Core.enmUsage == PDMAUDIODIR_IN)
    {
        LPDIRECTSOUNDCAPTURE8 pDSC;
        HRESULT hr = drvHostDSoundCreateDSCaptureInstance(&pDev->Guid, &pDSC);
        if (SUCCEEDED(hr))
        {
            DSCCAPS DSCCaps;
            RT_ZERO(DSCCaps);
            DSCCaps.dwSize = sizeof(DSCCAPS);
            hr = IDirectSoundCapture_GetCaps(pDSC, &DSCCaps);
            if (SUCCEEDED(hr))
            {
                pDev->Core.cMaxInputChannels = DSCCaps.dwChannels;
                rc = VINF_SUCCESS;
            }
            else
            {
                LogRel(("DSound: Error retrieving capture device capabilities, hr=%Rhrc\n", hr));
                rc = VERR_ACCESS_DENIED; /** @todo Fudge! */
            }

            IDirectSoundCapture_Release(pDSC);
        }
        else
            rc = VERR_GENERAL_FAILURE;
    }
    else
        AssertFailedStmt(rc = VERR_NOT_SUPPORTED);

    return rc;
}


/**
 * Queries information for @a pDevice and adds an entry to the enumeration.
 *
 * @returns VBox status code.
 * @param   pDevEnm     The enumeration to add the device to.
 * @param   pDevice     The device.
 * @param   enmType     The type of device.
 * @param   fDefault    Whether it's the default device.
 */
static int drvHostDSoundEnumNewStyleAdd(PPDMAUDIOHOSTENUM pDevEnm, IMMDevice *pDevice, EDataFlow enmType, bool fDefault)
{
    int rc = VINF_SUCCESS; /* ignore most errors */

    /*
     * Gather the necessary properties.
     */
    IPropertyStore *pProperties = NULL;
    HRESULT hrc = pDevice->OpenPropertyStore(STGM_READ, &pProperties);
    if (SUCCEEDED(hrc))
    {
        /* Get the friendly name. */
        PROPVARIANT VarName;
        PropVariantInit(&VarName);
        hrc = pProperties->GetValue(PKEY_Device_FriendlyName, &VarName);
        if (SUCCEEDED(hrc))
        {
            /* Get the DirectSound GUID. */
            PROPVARIANT VarGUID;
            PropVariantInit(&VarGUID);
            hrc = pProperties->GetValue(PKEY_AudioEndpoint_GUID, &VarGUID);
            if (SUCCEEDED(hrc))
            {
                /* Get the device format. */
                PROPVARIANT VarFormat;
                PropVariantInit(&VarFormat);
                hrc = pProperties->GetValue(PKEY_AudioEngine_DeviceFormat, &VarFormat);
                if (SUCCEEDED(hrc))
                {
                    WAVEFORMATEX const * const pFormat = (WAVEFORMATEX const *)VarFormat.blob.pBlobData;
                    AssertPtr(pFormat);

                    /*
                     * Create a enumeration entry for it.
                     */
                    size_t const cbName = RTUtf16CalcUtf8Len(VarName.pwszVal) + 1;
                    PDSOUNDDEV pDev = (PDSOUNDDEV)PDMAudioHostDevAlloc(sizeof(DSOUNDDEV), cbName, 0);
                    if (pDev)
                    {
                        pDev->Core.enmUsage = enmType == eRender ? PDMAUDIODIR_OUT : PDMAUDIODIR_IN;
                        pDev->Core.enmType  = PDMAUDIODEVICETYPE_BUILTIN;
                        if (fDefault)
                            pDev->Core.fFlags |= enmType == eRender
                                                 ? PDMAUDIOHOSTDEV_F_DEFAULT_OUT : PDMAUDIOHOSTDEV_F_DEFAULT_IN;
                        if (enmType == eRender)
                            pDev->Core.cMaxOutputChannels = pFormat->nChannels;
                        else
                            pDev->Core.cMaxInputChannels  = pFormat->nChannels;

                        //if (fDefault)
                            rc = RTUuidFromUtf16((PRTUUID)&pDev->Guid, VarGUID.pwszVal);
                        if (RT_SUCCESS(rc))
                        {
                            rc = RTUuidToStr((PCRTUUID)&pDev->Guid, pDev->szGuid, sizeof(pDev->szGuid));
                            AssertRC(rc);
                            pDev->Core.pszId = &pDev->szGuid[0];

                            rc = RTUtf16ToUtf8Ex(VarName.pwszVal, RTSTR_MAX, &pDev->Core.pszName, cbName, NULL);
                            if (RT_SUCCESS(rc))
                                PDMAudioHostEnumAppend(pDevEnm, &pDev->Core);
                            else
                                PDMAudioHostDevFree(&pDev->Core);
                        }
                        else
                        {
                            LogFunc(("RTUuidFromUtf16(%ls): %Rrc\n", VarGUID.pwszVal, rc));
                            PDMAudioHostDevFree(&pDev->Core);
                        }
                    }
                    else
                        rc = VERR_NO_MEMORY;
                    PropVariantClear(&VarFormat);
                }
                else
                    LogFunc(("Failed to get PKEY_AudioEngine_DeviceFormat: %Rhrc\n", hrc));
                PropVariantClear(&VarGUID);
            }
            else
                LogFunc(("Failed to get PKEY_AudioEndpoint_GUID: %Rhrc\n", hrc));
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
 * @param   pDevEnm             Where to store the enumerated devices.
 */
static int drvHostDSoundEnumerateDevices(PPDMAUDIOHOSTENUM pDevEnm)
{
    DSLOG(("DSound: Enumerating devices ...\n"));

    /*
     * Use the Vista+ API.
     */
    IMMDeviceEnumerator *pEnumerator;
    HRESULT hrc = CoCreateInstance(__uuidof(MMDeviceEnumerator), 0, CLSCTX_ALL,
                                   __uuidof(IMMDeviceEnumerator), (void **)&pEnumerator);
    if (SUCCEEDED(hrc))
    {
        int rc = VINF_SUCCESS;
        for (unsigned idxPass = 0; idxPass < 2 && RT_SUCCESS(rc); idxPass++)
        {
            EDataFlow const enmType = idxPass == 0 ? EDataFlow::eRender : EDataFlow::eCapture;

            /* Get the default device first. */
            IMMDevice *pDefaultDevice = NULL;
            hrc = pEnumerator->GetDefaultAudioEndpoint(enmType, eMultimedia, &pDefaultDevice);
            if (SUCCEEDED(hrc))
                rc = drvHostDSoundEnumNewStyleAdd(pDevEnm, pDefaultDevice, enmType, true);
            else
                pDefaultDevice = NULL;

            /* Enumerate the devices. */
            IMMDeviceCollection *pCollection = NULL;
            hrc = pEnumerator->EnumAudioEndpoints(enmType, DEVICE_STATE_ACTIVE /*| DEVICE_STATE_UNPLUGGED?*/, &pCollection);
            if (SUCCEEDED(hrc) && pCollection != NULL)
            {
                UINT cDevices = 0;
                hrc = pCollection->GetCount(&cDevices);
                if (SUCCEEDED(hrc))
                {
                    for (UINT idxDevice = 0; idxDevice < cDevices && RT_SUCCESS(rc); idxDevice++)
                    {
                        IMMDevice *pDevice = NULL;
                        hrc = pCollection->Item(idxDevice, &pDevice);
                        if (SUCCEEDED(hrc) && pDevice)
                        {
                            if (pDevice != pDefaultDevice)
                                rc = drvHostDSoundEnumNewStyleAdd(pDevEnm, pDevice, enmType, false);
                            pDevice->Release();
                        }
                    }
                }
                pCollection->Release();
            }
            else
                LogRelMax(10, ("EnumAudioEndpoints(%s) failed: %Rhrc\n", idxPass == 0 ? "output" : "input", hrc));

            if (pDefaultDevice)
                pDefaultDevice->Release();
        }
        pEnumerator->Release();
        if (pDevEnm->cDevices > 0 || RT_FAILURE(rc))
        {
            DSLOG(("DSound: Enumerating devices done - %u device (%Rrc)\n", pDevEnm->cDevices, rc));
            return rc;
        }
    }

    /*
     * Fall back to dsound.
     */
    /* Resolve symbols once. */
    static PFNDIRECTSOUNDENUMERATEW        volatile s_pfnDirectSoundEnumerateW        = NULL;
    static PFNDIRECTSOUNDCAPTUREENUMERATEW volatile s_pfnDirectSoundCaptureEnumerateW = NULL;

    PFNDIRECTSOUNDENUMERATEW        pfnDirectSoundEnumerateW          = s_pfnDirectSoundEnumerateW;
    PFNDIRECTSOUNDCAPTUREENUMERATEW pfnDirectSoundCaptureEnumerateW   = s_pfnDirectSoundCaptureEnumerateW;
    if (!pfnDirectSoundEnumerateW || !pfnDirectSoundCaptureEnumerateW)
    {
        RTLDRMOD hModDSound = NIL_RTLDRMOD;
        int rc = RTLdrLoadSystem("dsound.dll", true /*fNoUnload*/, &hModDSound);
        if (RT_SUCCESS(rc))
        {
            rc = RTLdrGetSymbol(hModDSound, "DirectSoundEnumerateW", (void **)&pfnDirectSoundEnumerateW);
            if (RT_SUCCESS(rc))
                s_pfnDirectSoundEnumerateW = pfnDirectSoundEnumerateW;
            else
                LogRel(("DSound: Failed to get dsound.dll export DirectSoundEnumerateW: %Rrc\n", rc));

            rc = RTLdrGetSymbol(hModDSound, "DirectSoundCaptureEnumerateW", (void **)&pfnDirectSoundCaptureEnumerateW);
            if (RT_SUCCESS(rc))
                s_pfnDirectSoundCaptureEnumerateW = pfnDirectSoundCaptureEnumerateW;
            else
                LogRel(("DSound: Failed to get dsound.dll export DirectSoundCaptureEnumerateW: %Rrc\n", rc));
            RTLdrClose(hModDSound);
        }
        else
            LogRel(("DSound: Unable to load dsound.dll for enumerating devices: %Rrc\n", rc));
        if (!pfnDirectSoundEnumerateW && !pfnDirectSoundCaptureEnumerateW)
            return rc;
    }

    /* Common callback context for both playback and capture enumerations: */
    DSOUNDENUMCBCTX EnumCtx;
    EnumCtx.fFlags  = 0;
    EnumCtx.pDevEnm = pDevEnm;

    /* Enumerate playback devices. */
    if (pfnDirectSoundEnumerateW)
    {
        DSLOG(("DSound: Enumerating playback devices ...\n"));
        HRESULT hr = pfnDirectSoundEnumerateW(&drvHostDSoundEnumOldStylePlaybackCallback, &EnumCtx);
        if (FAILED(hr))
            LogRel(("DSound: Error enumerating host playback devices: %Rhrc\n", hr));
    }

    /* Enumerate capture devices. */
    if (pfnDirectSoundCaptureEnumerateW)
    {
        DSLOG(("DSound: Enumerating capture devices ...\n"));
        HRESULT hr = pfnDirectSoundCaptureEnumerateW(&drvHostDSoundEnumOldStyleCaptureCallback, &EnumCtx);
        if (FAILED(hr))
            LogRel(("DSound: Error enumerating host capture devices: %Rhrc\n", hr));
    }

    /*
     * Query Information for all enumerated devices.
     * Note! This is problematic to do from the enumeration callbacks.
     */
    PDSOUNDDEV pDev;
    RTListForEach(&pDevEnm->LstDevices, pDev, DSOUNDDEV, Core.ListEntry)
    {
        drvHostDSoundEnumOldStyleQueryDeviceInfo(pDev); /* ignore rc */
    }

    DSLOG(("DSound: Enumerating devices done\n"));

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetDevices}
 */
static DECLCALLBACK(int) drvHostDSoundHA_GetDevices(PPDMIHOSTAUDIO pInterface, PPDMAUDIOHOSTENUM pDeviceEnum)
{
    RT_NOREF(pInterface);
    AssertPtrReturn(pDeviceEnum, VERR_INVALID_POINTER);

    PDMAudioHostEnumInit(pDeviceEnum);
    int rc = drvHostDSoundEnumerateDevices(pDeviceEnum);
    if (RT_FAILURE(rc))
        PDMAudioHostEnumDelete(pDeviceEnum);

    LogFlowFunc(("Returning %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnGetStatus}
 */
static DECLCALLBACK(PDMAUDIOBACKENDSTS) drvHostDSoundHA_GetStatus(PPDMIHOSTAUDIO pInterface, PDMAUDIODIR enmDir)
{
    RT_NOREF(pInterface, enmDir);
    return PDMAUDIOBACKENDSTS_RUNNING;
}


/**
 * Converts from PDM stream config to windows WAVEFORMATEXTENSIBLE struct.
 *
 * @param   pCfg    The PDM audio stream config to convert from.
 * @param   pFmt    The windows structure to initialize.
 */
static void dsoundWaveFmtFromCfg(PCPDMAUDIOSTREAMCFG pCfg, PWAVEFORMATEXTENSIBLE pFmt)
{
    RT_ZERO(*pFmt);
    pFmt->Format.wFormatTag      = WAVE_FORMAT_PCM;
    pFmt->Format.nChannels       = PDMAudioPropsChannels(&pCfg->Props);
    pFmt->Format.wBitsPerSample  = PDMAudioPropsSampleBits(&pCfg->Props);
    pFmt->Format.nSamplesPerSec  = PDMAudioPropsHz(&pCfg->Props);
    pFmt->Format.nBlockAlign     = PDMAudioPropsFrameSize(&pCfg->Props);
    pFmt->Format.nAvgBytesPerSec = PDMAudioPropsFramesToBytes(&pCfg->Props, PDMAudioPropsHz(&pCfg->Props));
    pFmt->Format.cbSize          = 0; /* No extra data specified. */

    /*
     * We need to use the extensible structure if there are more than two channels
     * or if the channels have non-standard assignments.
     */
    if (   pFmt->Format.nChannels > 2
        || (  pFmt->Format.nChannels == 1
            ?    pCfg->Props.aidChannels[0] != PDMAUDIOCHANNELID_MONO
            :    pCfg->Props.aidChannels[0] != PDMAUDIOCHANNELID_FRONT_LEFT
              || pCfg->Props.aidChannels[1] != PDMAUDIOCHANNELID_FRONT_RIGHT))
    {
        pFmt->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        pFmt->Format.cbSize     = sizeof(*pFmt) - sizeof(pFmt->Format);
        pFmt->Samples.wValidBitsPerSample = PDMAudioPropsSampleBits(&pCfg->Props);
        pFmt->SubFormat         = KSDATAFORMAT_SUBTYPE_PCM;
        pFmt->dwChannelMask     = 0;
        unsigned const cSrcChannels = pFmt->Format.nChannels;
        for (unsigned i = 0; i < cSrcChannels; i++)
            if (   pCfg->Props.aidChannels[i] >= PDMAUDIOCHANNELID_FIRST_STANDARD
                && pCfg->Props.aidChannels[i] <  PDMAUDIOCHANNELID_END_STANDARD)
                pFmt->dwChannelMask |= RT_BIT_32(pCfg->Props.aidChannels[i] - PDMAUDIOCHANNELID_FIRST_STANDARD);
            else
                pFmt->Format.nChannels -= 1;
    }
}


/**
 * Resets the state of a DirectSound stream, clearing the buffer content.
 *
 * @param   pThis               Host audio driver instance.
 * @param   pStreamDS           Stream to reset state for.
 */
static void drvHostDSoundStreamReset(PDRVHOSTDSOUND pThis, PDSOUNDSTREAM pStreamDS)
{
    RT_NOREF(pThis);
    LogFunc(("Resetting %s\n", pStreamDS->Cfg.enmDir == PDMAUDIODIR_IN ? "capture" : "playback"));

    if (pStreamDS->Cfg.enmDir == PDMAUDIODIR_IN)
    {
        /*
         * Input streams.
         */
        LogFunc(("Resetting capture stream '%s'\n", pStreamDS->Cfg.szName));

        /* Reset the state: */
        pStreamDS->msLastTransfer   = 0;
/** @todo r=bird: We set the read position to zero here, but shouldn't we query it
 * from the buffer instead given that there isn't any interface for repositioning
 * to the start of the buffer as with playback buffers? */
        pStreamDS->In.offReadPos    = 0;
        pStreamDS->In.cOverruns     = 0;

        /* Clear the buffer content: */
        AssertPtr(pStreamDS->In.pDSCB);
        if (pStreamDS->In.pDSCB)
        {
            PVOID   pv1 = NULL;
            DWORD   cb1 = 0;
            PVOID   pv2 = NULL;
            DWORD   cb2 = 0;
            HRESULT hrc = IDirectSoundCaptureBuffer8_Lock(pStreamDS->In.pDSCB, 0, pStreamDS->cbBufSize,
                                                          &pv1, &cb1, &pv2, &cb2, 0 /*fFlags*/);
            if (SUCCEEDED(hrc))
            {
                PDMAudioPropsClearBuffer(&pStreamDS->Cfg.Props, pv1, cb1, PDMAUDIOPCMPROPS_B2F(&pStreamDS->Cfg.Props, cb1));
                if (pv2 && cb2)
                    PDMAudioPropsClearBuffer(&pStreamDS->Cfg.Props, pv2, cb2, PDMAUDIOPCMPROPS_B2F(&pStreamDS->Cfg.Props, cb2));
                hrc = IDirectSoundCaptureBuffer8_Unlock(pStreamDS->In.pDSCB, pv1, cb1, pv2, cb2);
                if (FAILED(hrc))
                    LogRelMaxFunc(64, ("DSound: Unlocking capture buffer '%s' after reset failed: %Rhrc\n",
                                       pStreamDS->Cfg.szName, hrc));
            }
            else
                LogRelMaxFunc(64, ("DSound: Locking capture buffer '%s' for reset failed: %Rhrc\n",
                                   pStreamDS->Cfg.szName, hrc));
        }
    }
    else
    {
        /*
         * Output streams.
         */
        Assert(pStreamDS->Cfg.enmDir == PDMAUDIODIR_OUT);
        LogFunc(("Resetting playback stream '%s'\n", pStreamDS->Cfg.szName));

        /* If draining was enagaged, make sure dsound has stopped playing: */
        if (pStreamDS->Out.fDrain && pStreamDS->Out.pDSB)
            pStreamDS->Out.pDSB->Stop();

        /* Reset the internal state: */
        pStreamDS->msLastTransfer               = 0;
        pStreamDS->Out.fFirstTransfer           = true;
        pStreamDS->Out.fDrain                   = false;
        pStreamDS->Out.cbLastTransferred        = 0;
        pStreamDS->Out.cbTransferred            = 0;
        pStreamDS->Out.cbWritten                = 0;
        pStreamDS->Out.offWritePos              = 0;
        pStreamDS->Out.offPlayCursorLastPending = 0;
        pStreamDS->Out.offPlayCursorLastPlayed  = 0;

        /* Reset the buffer content and repositioning the buffer to the start of the buffer. */
        AssertPtr(pStreamDS->Out.pDSB);
        if (pStreamDS->Out.pDSB)
        {
            HRESULT hrc = IDirectSoundBuffer8_SetCurrentPosition(pStreamDS->Out.pDSB, 0);
            if (FAILED(hrc))
                LogRelMaxFunc(64, ("DSound: Failed to set buffer position for '%s': %Rhrc\n", pStreamDS->Cfg.szName, hrc));

            PVOID   pv1 = NULL;
            DWORD   cb1 = 0;
            PVOID   pv2 = NULL;
            DWORD   cb2 = 0;
            hrc = IDirectSoundBuffer8_Lock(pStreamDS->Out.pDSB, 0, pStreamDS->cbBufSize, &pv1, &cb1, &pv2, &cb2, 0 /*fFlags*/);
            if (hrc == DSERR_BUFFERLOST)
            {
                directSoundPlayRestore(pThis, pStreamDS->Out.pDSB);
                hrc = IDirectSoundBuffer8_Lock(pStreamDS->Out.pDSB, 0, pStreamDS->cbBufSize, &pv1, &cb1, &pv2, &cb2, 0 /*fFlags*/);
            }
            if (SUCCEEDED(hrc))
            {
                PDMAudioPropsClearBuffer(&pStreamDS->Cfg.Props, pv1, cb1, PDMAUDIOPCMPROPS_B2F(&pStreamDS->Cfg.Props, cb1));
                if (pv2 && cb2)
                    PDMAudioPropsClearBuffer(&pStreamDS->Cfg.Props, pv2, cb2, PDMAUDIOPCMPROPS_B2F(&pStreamDS->Cfg.Props, cb2));

                hrc = IDirectSoundBuffer8_Unlock(pStreamDS->Out.pDSB, pv1, cb1, pv2, cb2);
                if (FAILED(hrc))
                    LogRelMaxFunc(64, ("DSound: Unlocking playback buffer '%s' after reset failed: %Rhrc\n",
                                       pStreamDS->Cfg.szName, hrc));
            }
            else
                LogRelMaxFunc(64, ("DSound: Locking playback buffer '%s' for reset failed: %Rhrc\n", pStreamDS->Cfg.szName, hrc));
        }
    }
}


/**
 * Worker for drvHostDSoundHA_StreamCreate that creates caputre stream.
 *
 * @returns Windows COM status code.
 * @param   pThis       The DSound instance data.
 * @param   pStreamDS   The stream instance data.
 * @param   pCfgReq     The requested stream config (input).
 * @param   pCfgAcq     Where to return the actual stream config.  This is a
 *                      copy of @a *pCfgReq when called.
 * @param   pWaveFmtExt On input the requested stream format. Updated to the
 *                      actual stream format on successful return.
 */
static HRESULT drvHostDSoundStreamCreateCapture(PDRVHOSTDSOUND pThis, PDSOUNDSTREAM pStreamDS, PCPDMAUDIOSTREAMCFG pCfgReq,
                                                PPDMAUDIOSTREAMCFG pCfgAcq, WAVEFORMATEXTENSIBLE *pWaveFmtExt)
{
    Assert(pStreamDS->In.pDSCB == NULL);
    HRESULT hrc;

    /*
     * Create, initialize and set up a IDirectSoundCapture instance the first time
     * we go thru here.
     */
    /** @todo bird: Or should we rather just throw this away after we've gotten the
     *        capture buffer?  Old code would just leak it... */
    if (pThis->pDSC == NULL)
    {
        hrc = drvHostDSoundCreateDSCaptureInstance(pThis->Cfg.pGuidCapture, &pThis->pDSC);
        if (FAILED(hrc))
            return hrc; /* The worker has complained to the release log already. */
    }

    /*
     * Create the capture buffer.
     */
    DSCBUFFERDESC BufferDesc =
    {
        /*.dwSize = */          sizeof(BufferDesc),
        /*.dwFlags = */         0,
        /*.dwBufferBytes =*/    PDMAudioPropsFramesToBytes(&pCfgReq->Props, pCfgReq->Backend.cFramesBufferSize),
        /*.dwReserved = */      0,
        /*.lpwfxFormat = */     &pWaveFmtExt->Format,
        /*.dwFXCount = */       0,
        /*.lpDSCFXDesc = */     NULL
    };

    LogRel2(("DSound: Requested capture buffer is %#x B / %u B / %RU64 ms\n", BufferDesc.dwBufferBytes, BufferDesc.dwBufferBytes,
             PDMAudioPropsBytesToMilli(&pCfgReq->Props, BufferDesc.dwBufferBytes)));

    LPDIRECTSOUNDCAPTUREBUFFER pLegacyDSCB = NULL;
    hrc = IDirectSoundCapture_CreateCaptureBuffer(pThis->pDSC, &BufferDesc, &pLegacyDSCB, NULL);
    if (FAILED(hrc))
    {
        LogRelMax(64, ("DSound: Creating capture buffer for '%s' failed: %Rhrc\n", pCfgReq->szName, hrc));
        return hrc;
    }

    /* Get the IDirectSoundCaptureBuffer8 version of the interface. */
    hrc = IDirectSoundCaptureBuffer_QueryInterface(pLegacyDSCB, IID_IDirectSoundCaptureBuffer8, (void **)&pStreamDS->In.pDSCB);
    IDirectSoundCaptureBuffer_Release(pLegacyDSCB);
    if (FAILED(hrc))
    {
        LogRelMax(64, ("DSound: Querying IID_IDirectSoundCaptureBuffer8 for '%s' failed: %Rhrc\n", pCfgReq->szName, hrc));
        return hrc;
    }

    /*
     * Query the actual stream configuration.
     */
#if 0 /** @todo r=bird: WTF was this for? */
    DWORD offByteReadPos = 0;
    hrc = IDirectSoundCaptureBuffer8_GetCurrentPosition(pStreamDS->In.pDSCB, NULL, &offByteReadPos);
    if (FAILED(hrc))
    {
        offByteReadPos = 0;
        DSLOGREL(("DSound: Getting capture position failed with %Rhrc\n", hr));
    }
#endif
    RT_ZERO(*pWaveFmtExt);
    hrc = IDirectSoundCaptureBuffer8_GetFormat(pStreamDS->In.pDSCB, &pWaveFmtExt->Format, sizeof(*pWaveFmtExt), NULL);
    if (SUCCEEDED(hrc))
    {
        /** @todo r=bird: We aren't converting/checking the pWaveFmtX content...   */

        DSCBCAPS BufferCaps = { /*.dwSize = */ sizeof(BufferCaps), 0, 0, 0 };
        hrc = IDirectSoundCaptureBuffer8_GetCaps(pStreamDS->In.pDSCB, &BufferCaps);
        if (SUCCEEDED(hrc))
        {
            LogRel2(("DSound: Acquired capture buffer capabilities for '%s':\n"
                     "DSound:   dwFlags       = %#RX32\n"
                     "DSound:   dwBufferBytes = %#RX32 B / %RU32 B / %RU64 ms\n"
                     "DSound:   dwReserved    = %#RX32\n",
                     pCfgReq->szName, BufferCaps.dwFlags, BufferCaps.dwBufferBytes, BufferCaps.dwBufferBytes,
                     PDMAudioPropsBytesToMilli(&pCfgReq->Props, BufferCaps.dwBufferBytes), BufferCaps.dwReserved ));

            /* Update buffer related stuff: */
            pStreamDS->In.offReadPos = 0; /** @todo shouldn't we use offBytReadPos here to "read at the initial capture position"? */
            pStreamDS->cbBufSize     = BufferCaps.dwBufferBytes;
            pCfgAcq->Backend.cFramesBufferSize = PDMAudioPropsBytesToFrames(&pCfgAcq->Props, BufferCaps.dwBufferBytes);

#if 0 /** @todo r=bird: uAlign isn't set anywhere, so this hasn't been checking anything for a while... */
            if (bc.dwBufferBytes & pStreamDS->uAlign)
                DSLOGREL(("DSound: Capture GetCaps returned misaligned buffer: size %RU32, alignment %RU32\n",
                          bc.dwBufferBytes, pStreamDS->uAlign + 1));
#endif
            LogFlow(("returns S_OK\n"));
            return S_OK;
        }
        LogRelMax(64, ("DSound: Getting capture buffer capabilities for '%s' failed: %Rhrc\n", pCfgReq->szName, hrc));
    }
    else
        LogRelMax(64, ("DSound: Getting capture format for '%s' failed: %Rhrc\n", pCfgReq->szName, hrc));

    IDirectSoundCaptureBuffer8_Release(pStreamDS->In.pDSCB);
    pStreamDS->In.pDSCB = NULL;
    LogFlowFunc(("returns %Rhrc\n", hrc));
    return hrc;
}


/**
 * Worker for drvHostDSoundHA_StreamCreate that creates playback stream.
 *
 * @returns Windows COM status code.
 * @param   pThis       The DSound instance data.
 * @param   pStreamDS   The stream instance data.
 * @param   pCfgReq     The requested stream config (input).
 * @param   pCfgAcq     Where to return the actual stream config.  This is a
 *                      copy of @a *pCfgReq when called.
 * @param   pWaveFmtExt On input the requested stream format.
 *                      Updated to the actual stream format on successful
 *                      return.
 */
static HRESULT drvHostDSoundStreamCreatePlayback(PDRVHOSTDSOUND pThis, PDSOUNDSTREAM pStreamDS, PCPDMAUDIOSTREAMCFG pCfgReq,
                                                 PPDMAUDIOSTREAMCFG pCfgAcq, WAVEFORMATEXTENSIBLE *pWaveFmtExt)
{
    Assert(pStreamDS->Out.pDSB == NULL);
    HRESULT hrc;

    /*
     * Create, initialize and set up a DirectSound8 instance the first time
     * we go thru here.
     */
    /** @todo bird: Or should we rather just throw this away after we've gotten the
     *        sound buffer?  Old code would just leak it... */
    if (pThis->pDS == NULL)
    {
        hrc = drvHostDSoundCreateDSPlaybackInstance(pThis->Cfg.pGuidPlay, &pThis->pDS);
        if (FAILED(hrc))
            return hrc; /* The worker has complained to the release log already. */
    }

    /*
     * As we reuse our (secondary) buffer for playing out data as it comes in,
     * we're using this buffer as a so-called streaming buffer.
     *
     * See https://msdn.microsoft.com/en-us/library/windows/desktop/ee419014(v=vs.85).aspx
     *
     * However, as we do not want to use memory on the sound device directly
     * (as most modern audio hardware on the host doesn't have this anyway),
     * we're *not* going to use DSBCAPS_STATIC for that.
     *
     * Instead we're specifying DSBCAPS_LOCSOFTWARE, as this fits the bill
     * of copying own buffer data to our secondary's Direct Sound buffer.
     */
    DSBUFFERDESC BufferDesc =
    {
        /*.dwSize = */          sizeof(BufferDesc),
        /*.dwFlags = */         DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_LOCSOFTWARE,
        /*.dwBufferBytes = */   PDMAudioPropsFramesToBytes(&pCfgReq->Props, pCfgReq->Backend.cFramesBufferSize),
        /*.dwReserved = */      0,
        /*.lpwfxFormat = */     &pWaveFmtExt->Format,
        /*.guid3DAlgorithm =    {0, 0, 0, {0,0,0,0, 0,0,0,0}} */
    };
    LogRel2(("DSound: Requested playback buffer is %#x B / %u B / %RU64 ms\n", BufferDesc.dwBufferBytes, BufferDesc.dwBufferBytes,
             PDMAudioPropsBytesToMilli(&pCfgReq->Props, BufferDesc.dwBufferBytes)));

    LPDIRECTSOUNDBUFFER pLegacyDSB = NULL;
    hrc = IDirectSound8_CreateSoundBuffer(pThis->pDS, &BufferDesc, &pLegacyDSB, NULL);
    if (FAILED(hrc))
    {
        LogRelMax(64, ("DSound: Creating playback sound buffer for '%s' failed: %Rhrc\n", pCfgReq->szName, hrc));
        return hrc;
    }

    /* Get the IDirectSoundBuffer8 version of the interface. */
    hrc = IDirectSoundBuffer_QueryInterface(pLegacyDSB, IID_IDirectSoundBuffer8, (PVOID *)&pStreamDS->Out.pDSB);
    IDirectSoundBuffer_Release(pLegacyDSB);
    if (FAILED(hrc))
    {
        LogRelMax(64, ("DSound: Querying IID_IDirectSoundBuffer8 for '%s' failed: %Rhrc\n", pCfgReq->szName, hrc));
        return hrc;
    }

    /*
     * Query the actual stream parameters, they may differ from what we requested.
     */
    RT_ZERO(*pWaveFmtExt);
    hrc = IDirectSoundBuffer8_GetFormat(pStreamDS->Out.pDSB, &pWaveFmtExt->Format, sizeof(*pWaveFmtExt), NULL);
    if (SUCCEEDED(hrc))
    {
        /** @todo r=bird: We aren't converting/checking the pWaveFmtX content...   */

        DSBCAPS BufferCaps = { /*.dwSize = */ sizeof(BufferCaps), 0, 0, 0, 0 };
        hrc = IDirectSoundBuffer8_GetCaps(pStreamDS->Out.pDSB, &BufferCaps);
        if (SUCCEEDED(hrc))
        {
            LogRel2(("DSound: Acquired playback buffer capabilities for '%s':\n"
                     "DSound:   dwFlags              = %#RX32\n"
                     "DSound:   dwBufferBytes        = %#RX32 B / %RU32 B / %RU64 ms\n"
                     "DSound:   dwUnlockTransferRate = %RU32 KB/s\n"
                     "DSound:   dwPlayCpuOverhead    = %RU32%%\n",
                     pCfgReq->szName, BufferCaps.dwFlags, BufferCaps.dwBufferBytes, BufferCaps.dwBufferBytes,
                     PDMAudioPropsBytesToMilli(&pCfgReq->Props, BufferCaps.dwBufferBytes),
                     BufferCaps.dwUnlockTransferRate, BufferCaps.dwPlayCpuOverhead));

            /* Update buffer related stuff: */
            pStreamDS->cbBufSize = BufferCaps.dwBufferBytes;
            pCfgAcq->Backend.cFramesBufferSize    = PDMAudioPropsBytesToFrames(&pCfgAcq->Props, BufferCaps.dwBufferBytes);
            pCfgAcq->Backend.cFramesPeriod        = pCfgAcq->Backend.cFramesBufferSize / 4; /* total fiction */
            pCfgAcq->Backend.cFramesPreBuffering  = pCfgReq->Backend.cFramesPreBuffering * pCfgAcq->Backend.cFramesBufferSize
                                                  / RT_MAX(pCfgReq->Backend.cFramesBufferSize, 1);

#if 0 /** @todo r=bird: uAlign isn't set anywhere, so this hasn't been checking anything for a while... */
            if (bc.dwBufferBytes & pStreamDS->uAlign)
                DSLOGREL(("DSound: Playback capabilities returned misaligned buffer: size %RU32, alignment %RU32\n",
                          bc.dwBufferBytes, pStreamDS->uAlign + 1));
#endif
            LogFlow(("returns S_OK\n"));
            return S_OK;
        }
        LogRelMax(64, ("DSound: Getting playback buffer capabilities for '%s' failed: %Rhrc\n", pCfgReq->szName, hrc));
    }
    else
        LogRelMax(64, ("DSound: Getting playback format for '%s' failed: %Rhrc\n", pCfgReq->szName, hrc));

    IDirectSoundBuffer8_Release(pStreamDS->Out.pDSB);
    pStreamDS->Out.pDSB = NULL;
    LogFlowFunc(("returns %Rhrc\n", hrc));
    return hrc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCreate}
 */
static DECLCALLBACK(int) drvHostDSoundHA_StreamCreate(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                      PCPDMAUDIOSTREAMCFG pCfgReq, PPDMAUDIOSTREAMCFG pCfgAcq)
{
    PDRVHOSTDSOUND pThis     = RT_FROM_MEMBER(pInterface, DRVHOSTDSOUND, IHostAudio);
    PDSOUNDSTREAM  pStreamDS = (PDSOUNDSTREAM)pStream;
    AssertPtrReturn(pStreamDS, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgReq, VERR_INVALID_POINTER);
    AssertPtrReturn(pCfgAcq, VERR_INVALID_POINTER);
    AssertReturn(pCfgReq->enmDir == PDMAUDIODIR_IN || pCfgReq->enmDir == PDMAUDIODIR_OUT, VERR_INVALID_PARAMETER);
    Assert(PDMAudioStrmCfgEquals(pCfgReq, pCfgAcq));

    const char * const pszStreamType = pCfgReq->enmDir == PDMAUDIODIR_IN ? "capture" : "playback"; RT_NOREF(pszStreamType);
    LogFlowFunc(("enmPath=%s '%s'\n", PDMAudioPathGetName(pCfgReq->enmPath), pCfgReq->szName));
    RTListInit(&pStreamDS->ListEntry); /* paranoia */

    /*
     * DSound has different COM interfaces for working with input and output
     * streams, so we'll quickly part ways here after some common format
     * specification setup and logging.
     */
#if defined(RTLOG_REL_ENABLED) || defined(LOG_ENABLED)
    char szTmp[64];
#endif
    LogRel2(("DSound: Opening %s stream '%s' (%s)\n", pCfgReq->szName, pszStreamType,
             PDMAudioPropsToString(&pCfgReq->Props, szTmp, sizeof(szTmp))));

    WAVEFORMATEXTENSIBLE WaveFmtExt;
    dsoundWaveFmtFromCfg(pCfgReq, &WaveFmtExt);
    LogRel2(("DSound: Requested %s format for '%s':\n"
             "DSound:   wFormatTag      = %RU16\n"
             "DSound:   nChannels       = %RU16\n"
             "DSound:   nSamplesPerSec  = %RU32\n"
             "DSound:   nAvgBytesPerSec = %RU32\n"
             "DSound:   nBlockAlign     = %RU16\n"
             "DSound:   wBitsPerSample  = %RU16\n"
             "DSound:   cbSize          = %RU16\n",
             pszStreamType, pCfgReq->szName, WaveFmtExt.Format.wFormatTag, WaveFmtExt.Format.nChannels,
             WaveFmtExt.Format.nSamplesPerSec, WaveFmtExt.Format.nAvgBytesPerSec, WaveFmtExt.Format.nBlockAlign,
             WaveFmtExt.Format.wBitsPerSample, WaveFmtExt.Format.cbSize));
    if (WaveFmtExt.Format.cbSize != 0)
        LogRel2(("DSound:   dwChannelMask   = %#RX32\n"
                 "DSound:   wValidBitsPerSample = %RU16\n",
                 WaveFmtExt.dwChannelMask, WaveFmtExt.Samples.wValidBitsPerSample));

    HRESULT hrc;
    if (pCfgReq->enmDir == PDMAUDIODIR_IN)
        hrc = drvHostDSoundStreamCreateCapture(pThis, pStreamDS, pCfgReq, pCfgAcq, &WaveFmtExt);
    else
        hrc = drvHostDSoundStreamCreatePlayback(pThis, pStreamDS, pCfgReq, pCfgAcq, &WaveFmtExt);
    int rc;
    if (SUCCEEDED(hrc))
    {
        LogRel2(("DSound: Acquired %s format for '%s':\n"
                 "DSound:   wFormatTag      = %RU16\n"
                 "DSound:   nChannels       = %RU16\n"
                 "DSound:   nSamplesPerSec  = %RU32\n"
                 "DSound:   nAvgBytesPerSec = %RU32\n"
                 "DSound:   nBlockAlign     = %RU16\n"
                 "DSound:   wBitsPerSample  = %RU16\n"
                 "DSound:   cbSize          = %RU16\n",
                 pszStreamType, pCfgReq->szName, WaveFmtExt.Format.wFormatTag, WaveFmtExt.Format.nChannels,
                 WaveFmtExt.Format.nSamplesPerSec, WaveFmtExt.Format.nAvgBytesPerSec, WaveFmtExt.Format.nBlockAlign,
                 WaveFmtExt.Format.wBitsPerSample, WaveFmtExt.Format.cbSize));
        if (WaveFmtExt.Format.cbSize != 0)
        {
            LogRel2(("DSound:   dwChannelMask   = %#RX32\n"
                     "DSound:   wValidBitsPerSample = %RU16\n",
                     WaveFmtExt.dwChannelMask, WaveFmtExt.Samples.wValidBitsPerSample));

            /* Update the channel count and map here. */
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
         * Copy the acquired config and reset the stream (clears the buffer).
         */
        PDMAudioStrmCfgCopy(&pStreamDS->Cfg, pCfgAcq);
        drvHostDSoundStreamReset(pThis, pStreamDS);

        RTCritSectEnter(&pThis->CritSect);
        RTListAppend(&pThis->HeadStreams, &pStreamDS->ListEntry);
        RTCritSectLeave(&pThis->CritSect);

        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_AUDIO_STREAM_COULD_NOT_CREATE;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDestroy}
 */
static DECLCALLBACK(int) drvHostDSoundHA_StreamDestroy(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                       bool fImmediate)
{
    PDRVHOSTDSOUND pThis     = RT_FROM_MEMBER(pInterface, DRVHOSTDSOUND, IHostAudio);
    PDSOUNDSTREAM  pStreamDS = (PDSOUNDSTREAM)pStream;
    AssertPtrReturn(pStreamDS, VERR_INVALID_POINTER);
    LogFlowFunc(("Stream '%s' {%s}\n", pStreamDS->Cfg.szName, drvHostDSoundStreamStatusString(pStreamDS)));
    RT_NOREF(fImmediate);

    RTCritSectEnter(&pThis->CritSect);
    RTListNodeRemove(&pStreamDS->ListEntry);
    RTCritSectLeave(&pThis->CritSect);

    if (pStreamDS->Cfg.enmDir == PDMAUDIODIR_IN)
    {
        /*
         * Input.
         */
        if (pStreamDS->In.pDSCB)
        {
            HRESULT hrc = IDirectSoundCaptureBuffer_Stop(pStreamDS->In.pDSCB);
            if (FAILED(hrc))
                LogFunc(("IDirectSoundCaptureBuffer_Stop failed: %Rhrc\n", hrc));

            drvHostDSoundStreamReset(pThis, pStreamDS);

            IDirectSoundCaptureBuffer8_Release(pStreamDS->In.pDSCB);
            pStreamDS->In.pDSCB = NULL;
        }
    }
    else
    {
        /*
         * Output.
         */
        if (pStreamDS->Out.pDSB)
        {
            drvHostDSoundStreamStopPlayback(pThis, pStreamDS, true /*fReset*/);

            IDirectSoundBuffer8_Release(pStreamDS->Out.pDSB);
            pStreamDS->Out.pDSB = NULL;
        }
    }

    if (RTCritSectIsInitialized(&pStreamDS->CritSect))
        RTCritSectDelete(&pStreamDS->CritSect);

    return VINF_SUCCESS;
}


/**
 * Worker for drvHostDSoundHA_StreamEnable and drvHostDSoundHA_StreamResume.
 *
 * This will try re-open the capture device if we're having trouble starting it.
 *
 * @returns VBox status code.
 * @param   pThis       The DSound host audio driver instance data.
 * @param   pStreamDS   The stream instance data.
 */
static int drvHostDSoundStreamCaptureStart(PDRVHOSTDSOUND pThis, PDSOUNDSTREAM pStreamDS)
{
    /*
     * Check the stream status first.
     */
    int rc = VERR_AUDIO_STREAM_NOT_READY;
    if (pStreamDS->In.pDSCB)
    {
        DWORD   fStatus = 0;
        HRESULT hrc = IDirectSoundCaptureBuffer8_GetStatus(pStreamDS->In.pDSCB, &fStatus);
        if (SUCCEEDED(hrc))
        {
            /*
             * Try start capturing if it's not already doing so.
             */
            if (!(fStatus & DSCBSTATUS_CAPTURING))
            {
                LogRel2(("DSound: Starting capture on '%s' ... \n", pStreamDS->Cfg.szName));
                hrc = IDirectSoundCaptureBuffer8_Start(pStreamDS->In.pDSCB, DSCBSTART_LOOPING);
                if (SUCCEEDED(hrc))
                    rc = VINF_SUCCESS;
                else
                {
                    /*
                     * Failed to start, try re-create the capture buffer.
                     */
                    LogRelMax(64, ("DSound: Starting to capture on '%s' failed: %Rhrc - will try re-open it ...\n",
                                   pStreamDS->Cfg.szName, hrc));

                    IDirectSoundCaptureBuffer8_Release(pStreamDS->In.pDSCB);
                    pStreamDS->In.pDSCB = NULL;

                    PDMAUDIOSTREAMCFG       CfgReq = pStreamDS->Cfg;
                    PDMAUDIOSTREAMCFG       CfgAcq = pStreamDS->Cfg;
                    WAVEFORMATEXTENSIBLE    WaveFmtExt;
                    dsoundWaveFmtFromCfg(&pStreamDS->Cfg, &WaveFmtExt);
                    hrc = drvHostDSoundStreamCreateCapture(pThis, pStreamDS, &CfgReq, &CfgAcq, &WaveFmtExt);
                    if (SUCCEEDED(hrc))
                    {
                        PDMAudioStrmCfgCopy(&pStreamDS->Cfg, &CfgAcq);

                        /*
                         * Try starting capture again.
                         */
                        LogRel2(("DSound: Starting capture on re-opened '%s' ... \n", pStreamDS->Cfg.szName));
                        hrc = IDirectSoundCaptureBuffer8_Start(pStreamDS->In.pDSCB, DSCBSTART_LOOPING);
                        if (SUCCEEDED(hrc))
                            rc = VINF_SUCCESS;
                        else
                            LogRelMax(64, ("DSound: Starting to capture on re-opened '%s' failed: %Rhrc\n",
                                           pStreamDS->Cfg.szName, hrc));
                    }
                    else
                        LogRelMax(64, ("DSound: Re-opening '%s' failed: %Rhrc\n", pStreamDS->Cfg.szName, hrc));
                }
            }
            else
            {
                LogRel2(("DSound: Already capturing (%#x)\n", fStatus));
                AssertFailed();
            }
        }
        else
            LogRelMax(64, ("DSound: Retrieving capture status for '%s' failed: %Rhrc\n", pStreamDS->Cfg.szName, hrc));
    }
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamEnable}
 */
static DECLCALLBACK(int) drvHostDSoundHA_StreamEnable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVHOSTDSOUND  pThis     = RT_FROM_MEMBER(pInterface, DRVHOSTDSOUND, IHostAudio);
    PDSOUNDSTREAM   pStreamDS = (PDSOUNDSTREAM)pStream;
    LogFlowFunc(("Stream '%s' {%s}\n", pStreamDS->Cfg.szName, drvHostDSoundStreamStatusString(pStreamDS)));

    /*
     * We always reset the buffer before enabling the stream (normally never necessary).
     */
    drvHostDSoundStreamReset(pThis, pStreamDS);
    pStreamDS->fEnabled = true;

    /*
     * Input streams will start capturing, while output streams will only start
     * playing once we get some audio data to play.
     */
    int rc = VINF_SUCCESS;
    if (pStreamDS->Cfg.enmDir == PDMAUDIODIR_IN)
        rc = drvHostDSoundStreamCaptureStart(pThis, pStreamDS);
    else
        Assert(pStreamDS->Cfg.enmDir == PDMAUDIODIR_OUT);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/**
 * Worker for drvHostDSoundHA_StreamDestroy, drvHostDSoundHA_StreamDisable and
 * drvHostDSoundHA_StreamPause.
 *
 * @returns VBox status code.
 * @param   pThis       The DSound host audio driver instance data.
 * @param   pStreamDS   The stream instance data.
 * @param   fReset      Whether to reset the buffer and state.
 */
static int drvHostDSoundStreamStopPlayback(PDRVHOSTDSOUND pThis, PDSOUNDSTREAM pStreamDS, bool fReset)
{
    if (!pStreamDS->Out.pDSB)
        return VINF_SUCCESS;

    LogRel2(("DSound: Stopping playback of '%s'...\n", pStreamDS->Cfg.szName));
    HRESULT hrc = IDirectSoundBuffer8_Stop(pStreamDS->Out.pDSB);
    if (FAILED(hrc))
    {
        LogFunc(("IDirectSoundBuffer8_Stop -> %Rhrc; will attempt restoring the stream...\n", hrc));
        directSoundPlayRestore(pThis, pStreamDS->Out.pDSB);
        hrc = IDirectSoundBuffer8_Stop(pStreamDS->Out.pDSB);
        if (FAILED(hrc))
            LogRelMax(64, ("DSound: %s playback of '%s' failed: %Rhrc\n", fReset ? "Stopping" : "Pausing",
                           pStreamDS->Cfg.szName, hrc));
    }
    LogRel2(("DSound: Stopped playback of '%s': %Rhrc\n", pStreamDS->Cfg.szName, hrc));

    if (fReset)
        drvHostDSoundStreamReset(pThis, pStreamDS);
    return SUCCEEDED(hrc) ? VINF_SUCCESS : VERR_AUDIO_STREAM_NOT_READY;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDisable}
 */
static DECLCALLBACK(int) drvHostDSoundHA_StreamDisable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVHOSTDSOUND  pThis     = RT_FROM_MEMBER(pInterface, DRVHOSTDSOUND, IHostAudio);
    PDSOUNDSTREAM   pStreamDS = (PDSOUNDSTREAM)pStream;
    LogFlowFunc(("cMsLastTransfer=%RI64 ms, stream '%s' {%s} \n",
                 pStreamDS->msLastTransfer ? RTTimeMilliTS() - pStreamDS->msLastTransfer : -1,
                 pStreamDS->Cfg.szName, drvHostDSoundStreamStatusString(pStreamDS) ));

    /*
     * Change the state.
     */
    pStreamDS->fEnabled = false;

    /*
     * Stop the stream and maybe reset the buffer.
     */
    int rc = VINF_SUCCESS;
    if (pStreamDS->Cfg.enmDir == PDMAUDIODIR_IN)
    {
        if (pStreamDS->In.pDSCB)
        {
            HRESULT hrc = IDirectSoundCaptureBuffer_Stop(pStreamDS->In.pDSCB);
            if (SUCCEEDED(hrc))
                LogRel3(("DSound: Stopped capture on '%s'.\n", pStreamDS->Cfg.szName));
            else
            {
                LogRelMax(64, ("DSound: Stopping capture on '%s' failed: %Rhrc\n", pStreamDS->Cfg.szName, hrc));
                /* Don't report errors up to the caller, as it might just be a capture device change. */
            }

            /* This isn't strictly speaking necessary since StreamEnable does it too... */
            drvHostDSoundStreamReset(pThis, pStreamDS);
        }
    }
    else
    {
        Assert(pStreamDS->Cfg.enmDir == PDMAUDIODIR_OUT);
        if (pStreamDS->Out.pDSB)
        {
            rc = drvHostDSoundStreamStopPlayback(pThis, pStreamDS, true /*fReset*/);
            if (RT_SUCCESS(rc))
                LogRel3(("DSound: Stopped playback on '%s'.\n", pStreamDS->Cfg.szName));
        }
    }

    LogFlowFunc(("returns %Rrc {%s}\n", rc, drvHostDSoundStreamStatusString(pStreamDS)));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPause}
 *
 * @note    Basically the same as drvHostDSoundHA_StreamDisable, just w/o the
 *          buffer resetting and fEnabled change.
 */
static DECLCALLBACK(int) drvHostDSoundHA_StreamPause(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVHOSTDSOUND  pThis     = RT_FROM_MEMBER(pInterface, DRVHOSTDSOUND, IHostAudio);
    PDSOUNDSTREAM   pStreamDS = (PDSOUNDSTREAM)pStream;
    LogFlowFunc(("cMsLastTransfer=%RI64 ms, stream '%s' {%s} \n",
                 pStreamDS->msLastTransfer ? RTTimeMilliTS() - pStreamDS->msLastTransfer : -1,
                 pStreamDS->Cfg.szName, drvHostDSoundStreamStatusString(pStreamDS) ));

    /*
     * Stop the stream and maybe reset the buffer.
     */
    int rc = VINF_SUCCESS;
    if (pStreamDS->Cfg.enmDir == PDMAUDIODIR_IN)
    {
        if (pStreamDS->In.pDSCB)
        {
            HRESULT hrc = IDirectSoundCaptureBuffer_Stop(pStreamDS->In.pDSCB);
            if (SUCCEEDED(hrc))
                LogRel3(("DSound: Stopped capture on '%s'.\n", pStreamDS->Cfg.szName));
            else
            {
                LogRelMax(64, ("DSound: Stopping capture on '%s' failed: %Rhrc\n", pStreamDS->Cfg.szName, hrc));
                /* Don't report errors up to the caller, as it might just be a capture device change. */
            }
        }
    }
    else
    {
        Assert(pStreamDS->Cfg.enmDir == PDMAUDIODIR_OUT);
        if (pStreamDS->Out.pDSB)
        {
            /* Don't stop draining buffers, we won't be resuming them right.
               They'll stop by themselves anyway. */
            if (pStreamDS->Out.fDrain)
                LogFunc(("Stream '%s' is draining\n", pStreamDS->Cfg.szName));
            else
            {
                rc = drvHostDSoundStreamStopPlayback(pThis, pStreamDS, false /*fReset*/);
                if (RT_SUCCESS(rc))
                    LogRel3(("DSound: Stopped playback on '%s'.\n", pStreamDS->Cfg.szName));
            }
        }
    }

    LogFlowFunc(("returns %Rrc {%s}\n", rc, drvHostDSoundStreamStatusString(pStreamDS)));
    return rc;
}


/**
 * Worker for drvHostDSoundHA_StreamResume and drvHostDSoundHA_StreamPlay that
 * starts playing the DirectSound Buffer.
 *
 * @returns VBox status code.
 * @param   pThis               Host audio driver instance.
 * @param   pStreamDS           Stream to start playing.
 */
static int directSoundPlayStart(PDRVHOSTDSOUND pThis, PDSOUNDSTREAM pStreamDS)
{
    if (!pStreamDS->Out.pDSB)
        return VERR_AUDIO_STREAM_NOT_READY;

    LogRel2(("DSound: Starting playback of '%s' ...\n", pStreamDS->Cfg.szName));
    HRESULT hrc = IDirectSoundBuffer8_Play(pStreamDS->Out.pDSB, 0, 0, DSCBSTART_LOOPING);
    if (SUCCEEDED(hrc))
        return VINF_SUCCESS;

    for (unsigned i = 0; hrc == DSERR_BUFFERLOST && i < DRV_DSOUND_RESTORE_ATTEMPTS_MAX; i++)
    {
        LogFunc(("Restarting playback failed due to lost buffer, restoring ...\n"));
        directSoundPlayRestore(pThis, pStreamDS->Out.pDSB);

        hrc = IDirectSoundBuffer8_Play(pStreamDS->Out.pDSB, 0, 0, DSCBSTART_LOOPING);
        if (SUCCEEDED(hrc))
            return VINF_SUCCESS;
    }

    LogRelMax(64, ("DSound: Failed to start playback of '%s': %Rhrc\n", pStreamDS->Cfg.szName, hrc));
    return VERR_AUDIO_STREAM_NOT_READY;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamResume}
 */
static DECLCALLBACK(int) drvHostDSoundHA_StreamResume(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVHOSTDSOUND  pThis     = RT_FROM_MEMBER(pInterface, DRVHOSTDSOUND, IHostAudio);
    PDSOUNDSTREAM   pStreamDS = (PDSOUNDSTREAM)pStream;
    LogFlowFunc(("Stream '%s' {%s}\n", pStreamDS->Cfg.szName, drvHostDSoundStreamStatusString(pStreamDS)));

    /*
     * Input streams will start capturing, while output streams will only start
     * playing if we're past the pre-buffering state.
     */
    int rc = VINF_SUCCESS;
    if (pStreamDS->Cfg.enmDir == PDMAUDIODIR_IN)
        rc = drvHostDSoundStreamCaptureStart(pThis, pStreamDS);
    else
    {
        Assert(pStreamDS->Cfg.enmDir == PDMAUDIODIR_OUT);
        if (!pStreamDS->Out.fFirstTransfer)
            rc = directSoundPlayStart(pThis, pStreamDS);
    }

    LogFlowFunc(("returns %Rrc {%s}\n", rc, drvHostDSoundStreamStatusString(pStreamDS)));
    return rc;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamDrain}
 */
static DECLCALLBACK(int) drvHostDSoundHA_StreamDrain(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVHOSTDSOUND  pThis     = RT_FROM_MEMBER(pInterface, DRVHOSTDSOUND, IHostAudio);
    PDSOUNDSTREAM   pStreamDS = (PDSOUNDSTREAM)pStream;
    AssertReturn(pStreamDS->Cfg.enmDir == PDMAUDIODIR_OUT, VERR_INVALID_PARAMETER);
    LogFlowFunc(("cMsLastTransfer=%RI64 ms, stream '%s' {%s} \n",
                 pStreamDS->msLastTransfer ? RTTimeMilliTS() - pStreamDS->msLastTransfer : -1,
                 pStreamDS->Cfg.szName, drvHostDSoundStreamStatusString(pStreamDS) ));

    /*
     * We've started the buffer in looping mode, try switch to non-looping...
     */
    int rc = VINF_SUCCESS;
    if (pStreamDS->Out.pDSB && !pStreamDS->Out.fDrain)
    {
        LogRel2(("DSound: Switching playback stream '%s' to drain mode...\n", pStreamDS->Cfg.szName));
        HRESULT hrc = IDirectSoundBuffer8_Stop(pStreamDS->Out.pDSB);
        if (SUCCEEDED(hrc))
        {
            hrc = IDirectSoundBuffer8_Play(pStreamDS->Out.pDSB, 0, 0, 0);
            if (SUCCEEDED(hrc))
            {
                uint64_t const msNow = RTTimeMilliTS();
                pStreamDS->Out.msDrainDeadline = PDMAudioPropsBytesToMilli(&pStreamDS->Cfg.Props,  pStreamDS->cbBufSize) + msNow;
                pStreamDS->Out.fDrain          = true;
            }
            else
                LogRelMax(64, ("DSound: Failed to restart '%s' in drain mode: %Rhrc\n", pStreamDS->Cfg.szName, hrc));
        }
        else
        {
            Log2Func(("drain: IDirectSoundBuffer8_Stop failed: %Rhrc\n", hrc));
            directSoundPlayRestore(pThis, pStreamDS->Out.pDSB);

            HRESULT hrc2 = IDirectSoundBuffer8_Stop(pStreamDS->Out.pDSB);
            if (SUCCEEDED(hrc2))
                LogFunc(("Successfully stopped the stream after restoring it. (hrc=%Rhrc)\n", hrc));
            else
            {
                LogRelMax(64, ("DSound: Failed to stop playback stream '%s' for putting into drain mode: %Rhrc (initial), %Rhrc (after restore)\n",
                               pStreamDS->Cfg.szName, hrc, hrc2));
                rc = VERR_AUDIO_STREAM_NOT_READY;
            }
        }
    }
    LogFlowFunc(("returns %Rrc {%s}\n", rc, drvHostDSoundStreamStatusString(pStreamDS)));
    return rc;
}


/**
 * Retrieves the number of free bytes available for writing to a DirectSound output stream.
 *
 * @return  VBox status code. VERR_NOT_AVAILABLE if unable to determine or the
 *          buffer was not recoverable.
 * @param   pThis               Host audio driver instance.
 * @param   pStreamDS           DirectSound output stream to retrieve number for.
 * @param   pdwFree             Where to return the free amount on success.
 * @param   poffPlayCursor      Where to return the play cursor offset.
 */
static int dsoundGetFreeOut(PDRVHOSTDSOUND pThis, PDSOUNDSTREAM pStreamDS, DWORD *pdwFree, DWORD *poffPlayCursor)
{
    AssertPtrReturn(pThis,     VERR_INVALID_POINTER);
    AssertPtrReturn(pStreamDS, VERR_INVALID_POINTER);
    AssertPtrReturn(pdwFree,   VERR_INVALID_POINTER);
    AssertPtr(poffPlayCursor);

    Assert(pStreamDS->Cfg.enmDir == PDMAUDIODIR_OUT); /* Paranoia. */

    LPDIRECTSOUNDBUFFER8 pDSB = pStreamDS->Out.pDSB;
    AssertPtrReturn(pDSB, VERR_INVALID_POINTER);

    HRESULT hr = S_OK;

    /* Get the current play position which is used for calculating the free space in the buffer. */
    for (unsigned i = 0; i < DRV_DSOUND_RESTORE_ATTEMPTS_MAX; i++)
    {
        DWORD offPlayCursor  = 0;
        DWORD offWriteCursor = 0;
        hr = IDirectSoundBuffer8_GetCurrentPosition(pDSB, &offPlayCursor, &offWriteCursor);
        if (SUCCEEDED(hr))
        {
            int32_t cbDiff = offWriteCursor - offPlayCursor;
            if (cbDiff < 0)
                cbDiff += pStreamDS->cbBufSize;

            int32_t cbFree = offPlayCursor - pStreamDS->Out.offWritePos;
            if (cbFree < 0)
                cbFree += pStreamDS->cbBufSize;

            if (cbFree > (int32_t)pStreamDS->cbBufSize - cbDiff)
            {
                /** @todo count/log these. */
                pStreamDS->Out.offWritePos = offWriteCursor;
                cbFree = pStreamDS->cbBufSize - cbDiff;
            }

            /* When starting to use a DirectSound buffer, offPlayCursor and offWriteCursor
             * both point at position 0, so we won't be able to detect how many bytes
             * are writable that way.
             *
             * So use our per-stream written indicator to see if we just started a stream. */
            if (pStreamDS->Out.cbWritten == 0)
                cbFree = pStreamDS->cbBufSize;

            LogRel4(("DSound: offPlayCursor=%RU32, offWriteCursor=%RU32, offWritePos=%RU32 -> cbFree=%RI32\n",
                     offPlayCursor, offWriteCursor, pStreamDS->Out.offWritePos, cbFree));

            *pdwFree = cbFree;
            *poffPlayCursor = offPlayCursor;
            return VINF_SUCCESS;
        }

        if (hr != DSERR_BUFFERLOST) /** @todo MSDN doesn't state this error for GetCurrentPosition(). */
            break;

        LogFunc(("Getting playing position failed due to lost buffer, restoring ...\n"));

        directSoundPlayRestore(pThis, pDSB);
    }

    if (hr != DSERR_BUFFERLOST) /* Avoid log flooding if the error is still there. */
        DSLOGREL(("DSound: Getting current playback position failed with %Rhrc\n", hr));

    LogFunc(("Failed with %Rhrc\n", hr));

    *poffPlayCursor = pStreamDS->cbBufSize;
    return VERR_NOT_AVAILABLE;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetState}
 */
static DECLCALLBACK(PDMHOSTAUDIOSTREAMSTATE) drvHostDSoundHA_StreamGetState(PPDMIHOSTAUDIO pInterface,
                                                                            PPDMAUDIOBACKENDSTREAM pStream)
{
    RT_NOREF(pInterface);
    PDSOUNDSTREAM pStreamDS = (PDSOUNDSTREAM)pStream;
    AssertPtrReturn(pStreamDS, PDMHOSTAUDIOSTREAMSTATE_INVALID);

    if (   pStreamDS->Cfg.enmDir != PDMAUDIODIR_OUT
        || !pStreamDS->Out.fDrain)
    {
        LogFlowFunc(("returns OKAY for '%s' {%s}\n", pStreamDS->Cfg.szName, drvHostDSoundStreamStatusString(pStreamDS)));
        return PDMHOSTAUDIOSTREAMSTATE_OKAY;
    }
    LogFlowFunc(("returns DRAINING for '%s' {%s}\n", pStreamDS->Cfg.szName, drvHostDSoundStreamStatusString(pStreamDS)));
    return PDMHOSTAUDIOSTREAMSTATE_DRAINING;
}

#if 0 /* This isn't working as the write cursor is more a function of time than what we do.
         Previously we only reported the pre-buffering status anyway, so no harm. */
/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetPending}
 */
static DECLCALLBACK(uint32_t) drvHostDSoundHA_StreamGetPending(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    /*PDRVHOSTDSOUND  pThis     = RT_FROM_MEMBER(pInterface, DRVHOSTDSOUND, IHostAudio); */ RT_NOREF(pInterface);
    PDSOUNDSTREAM   pStreamDS = (PDSOUNDSTREAM)pStream;
    AssertPtrReturn(pStreamDS, 0);
    LogFlowFunc(("Stream '%s' {%s}\n", pStreamDS->Cfg.szName, drvHostDSoundStreamStatusString(pStreamDS)));

    if (pStreamDS->Cfg.enmDir == PDMAUDIODIR_OUT)
    {
        /* This is a similar calculation as for StreamGetReadable, only for an output buffer. */
        AssertPtr(pStreamDS->In.pDSCB);
        DWORD   offPlayCursor  = 0;
        DWORD   offWriteCursor = 0;
        HRESULT hrc = IDirectSoundBuffer8_GetCurrentPosition(pStreamDS->Out.pDSB, &offPlayCursor, &offWriteCursor);
        if (SUCCEEDED(hrc))
        {
            uint32_t cbPending = dsoundRingDistance(offWriteCursor, offPlayCursor, pStreamDS->cbBufSize);
            Log3Func(("cbPending=%RU32\n", cbPending));
            return cbPending;
        }
        AssertMsgFailed(("hrc=%Rhrc\n", hrc));
    }
    /* else: For input streams we never have any pending data. */

    return 0;
}
#endif

/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetWritable}
 */
static DECLCALLBACK(uint32_t) drvHostDSoundHA_StreamGetWritable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    PDRVHOSTDSOUND  pThis     = RT_FROM_MEMBER(pInterface, DRVHOSTDSOUND, IHostAudio);
    PDSOUNDSTREAM   pStreamDS = (PDSOUNDSTREAM)pStream;
    AssertPtrReturn(pStreamDS, 0);
    LogFlowFunc(("Stream '%s' {%s}\n", pStreamDS->Cfg.szName, drvHostDSoundStreamStatusString(pStreamDS)));

    DWORD           cbFree    = 0;
    DWORD           offIgn    = 0;
    int rc = dsoundGetFreeOut(pThis, pStreamDS, &cbFree, &offIgn);
    AssertRCReturn(rc, 0);

    return cbFree;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamPlay}
 */
static DECLCALLBACK(int) drvHostDSoundHA_StreamPlay(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                    const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    PDRVHOSTDSOUND  pThis     = RT_FROM_MEMBER(pInterface, DRVHOSTDSOUND, IHostAudio);
    PDSOUNDSTREAM   pStreamDS = (PDSOUNDSTREAM)pStream;
    AssertPtrReturn(pStreamDS, 0);
    if (cbBuf)
        AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbWritten, VERR_INVALID_POINTER);

    if (pStreamDS->fEnabled)
        AssertReturn(pStreamDS->cbBufSize, VERR_INTERNAL_ERROR_2);
    else
    {
        Log2Func(("Skipping disabled stream {%s}\n", drvHostDSoundStreamStatusString(pStreamDS)));
        return VINF_SUCCESS;
    }
    Log4Func(("cbBuf=%#x stream '%s' {%s}\n", cbBuf, pStreamDS->Cfg.szName, drvHostDSoundStreamStatusString(pStreamDS) ));

    /*
     * Transfer loop.
     */
    uint32_t cbWritten = 0;
    while (cbBuf > 0)
    {
        /*
         * Figure out how much we can possibly write.
         */
        DWORD offPlayCursor = 0;
        DWORD cbWritable    = 0;
        int rc = dsoundGetFreeOut(pThis, pStreamDS, &cbWritable, &offPlayCursor);
        AssertRCReturn(rc, rc);
        if (cbWritable < pStreamDS->Cfg.Props.cbFrame)
            break;

        uint32_t const cbToWrite = RT_MIN(cbWritable, cbBuf);
        Log3Func(("offPlay=%#x offWritePos=%#x -> cbWritable=%#x cbToWrite=%#x {%s}\n", offPlayCursor, pStreamDS->Out.offWritePos,
                  cbWritable, cbToWrite, drvHostDSoundStreamStatusString(pStreamDS) ));

        /*
         * Lock that amount of buffer.
         */
        PVOID pv1 = NULL;
        DWORD cb1 = 0;
        PVOID pv2 = NULL;
        DWORD cb2 = 0;
        HRESULT hrc = directSoundPlayLock(pThis, pStreamDS, pStreamDS->Out.offWritePos, cbToWrite,
                                          &pv1, &pv2, &cb1, &cb2, 0 /*dwFlags*/);
        AssertMsgReturn(SUCCEEDED(hrc), ("%Rhrc\n", hrc), VERR_ACCESS_DENIED); /** @todo translate these status codes already! */
        //AssertMsg(cb1 + cb2 == cbToWrite, ("%#x + %#x vs %#x\n", cb1, cb2, cbToWrite));

        /*
         * Copy over the data.
         */
        memcpy(pv1, pvBuf, cb1);
        pvBuf      = (uint8_t *)pvBuf + cb1;
        cbBuf     -= cb1;
        cbWritten += cb1;

        if (pv2)
        {
            memcpy(pv2, pvBuf, cb2);
            pvBuf      = (uint8_t *)pvBuf + cb2;
            cbBuf     -= cb2;
            cbWritten += cb2;
        }

        /*
         * Unlock and update the write position.
         */
        directSoundPlayUnlock(pThis, pStreamDS->Out.pDSB, pv1, pv2, cb1, cb2); /** @todo r=bird: pThis + pDSB parameters here for Unlock, but only pThis for Lock. Why? */
        pStreamDS->Out.offWritePos = (pStreamDS->Out.offWritePos + cb1 + cb2) % pStreamDS->cbBufSize;

        /*
         * If this was the first chunk, kick off playing.
         */
        if (!pStreamDS->Out.fFirstTransfer)
        { /* likely */ }
        else
        {
            *pcbWritten = cbWritten;
            rc = directSoundPlayStart(pThis, pStreamDS);
            AssertRCReturn(rc, rc);
            pStreamDS->Out.fFirstTransfer = false;
        }
    }

    /*
     * Done.
     */
    *pcbWritten = cbWritten;

    pStreamDS->Out.cbTransferred += cbWritten;
    if (cbWritten)
    {
        uint64_t const msPrev = pStreamDS->msLastTransfer; RT_NOREF(msPrev);
        pStreamDS->Out.cbLastTransferred = cbWritten;
        pStreamDS->msLastTransfer        = RTTimeMilliTS();
        LogFlowFunc(("cbLastTransferred=%RU32, msLastTransfer=%RU64 msNow=%RU64 cMsDelta=%RU64 {%s}\n",
                     cbWritten, msPrev, pStreamDS->msLastTransfer, msPrev ? pStreamDS->msLastTransfer - msPrev : 0,
                     drvHostDSoundStreamStatusString(pStreamDS) ));
    }
    else if (   pStreamDS->Out.fDrain
             && RTTimeMilliTS() >= pStreamDS->Out.msDrainDeadline)
    {
        LogRel2(("DSound: Stopping draining of '%s' {%s} ...\n", pStreamDS->Cfg.szName, drvHostDSoundStreamStatusString(pStreamDS)));
        if (pStreamDS->Out.pDSB)
        {
            HRESULT hrc = IDirectSoundBuffer8_Stop(pStreamDS->Out.pDSB);
            if (FAILED(hrc))
                LogRelMax(64, ("DSound: Failed to stop draining stream '%s': %Rhrc\n", pStreamDS->Cfg.szName, hrc));
        }
        pStreamDS->Out.fDrain = false;
        pStreamDS->fEnabled   = false;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamGetReadable}
 */
static DECLCALLBACK(uint32_t) drvHostDSoundHA_StreamGetReadable(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream)
{
    /*PDRVHOSTDSOUND  pThis     = RT_FROM_MEMBER(pInterface, DRVHOSTDSOUND, IHostAudio); */ RT_NOREF(pInterface);
    PDSOUNDSTREAM   pStreamDS = (PDSOUNDSTREAM)pStream;
    AssertPtrReturn(pStreamDS, 0);
    Assert(pStreamDS->Cfg.enmDir == PDMAUDIODIR_IN);

    if (pStreamDS->fEnabled)
    {
        /* This is the same calculation as for StreamGetPending. */
        AssertPtr(pStreamDS->In.pDSCB);
        DWORD   offCaptureCursor = 0;
        DWORD   offReadCursor    = 0;
        HRESULT hrc = IDirectSoundCaptureBuffer_GetCurrentPosition(pStreamDS->In.pDSCB, &offCaptureCursor, &offReadCursor);
        if (SUCCEEDED(hrc))
        {
            uint32_t cbPending = dsoundRingDistance(offCaptureCursor, offReadCursor, pStreamDS->cbBufSize);
            Log3Func(("cbPending=%RU32\n", cbPending));
            return cbPending;
        }
        AssertMsgFailed(("hrc=%Rhrc\n", hrc));
    }

    return 0;
}


/**
 * @interface_method_impl{PDMIHOSTAUDIO,pfnStreamCapture}
 */
static DECLCALLBACK(int) drvHostDSoundHA_StreamCapture(PPDMIHOSTAUDIO pInterface, PPDMAUDIOBACKENDSTREAM pStream,
                                                       void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead)
{
    /*PDRVHOSTDSOUND  pThis     = RT_FROM_MEMBER(pInterface, DRVHOSTDSOUND, IHostAudio);*/ RT_NOREF(pInterface);
    PDSOUNDSTREAM   pStreamDS = (PDSOUNDSTREAM)pStream;
    AssertPtrReturn(pStreamDS, 0);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbRead, VERR_INVALID_POINTER);

#if 0 /** @todo r=bird: shouldn't we do the same check as for output streams? */
    if (pStreamDS->fEnabled)
        AssertReturn(pStreamDS->cbBufSize, VERR_INTERNAL_ERROR_2);
    else
    {
        Log2Func(("Stream disabled, skipping\n"));
        return VINF_SUCCESS;
    }
#endif
    Log4Func(("cbBuf=%#x stream '%s' {%s}\n", cbBuf, pStreamDS->Cfg.szName, drvHostDSoundStreamStatusString(pStreamDS) ));

    /*
     * Read loop.
     */
    uint32_t cbRead = 0;
    while (cbBuf > 0)
    {
        /*
         * Figure out how much we can read.
         */
        DWORD   offCaptureCursor = 0;
        DWORD   offReadCursor    = 0;
        HRESULT hrc = IDirectSoundCaptureBuffer_GetCurrentPosition(pStreamDS->In.pDSCB, &offCaptureCursor, &offReadCursor);
        AssertMsgReturn(SUCCEEDED(hrc), ("%Rhrc\n", hrc), VERR_ACCESS_DENIED); /** @todo translate these status codes already! */
        //AssertMsg(offReadCursor == pStreamDS->In.offReadPos, ("%#x %#x\n", offReadCursor, pStreamDS->In.offReadPos));

        uint32_t const cbReadable = dsoundRingDistance(offCaptureCursor, pStreamDS->In.offReadPos, pStreamDS->cbBufSize);

        if (cbReadable >= pStreamDS->Cfg.Props.cbFrame)
        { /* likely */ }
        else
        {
            if (cbRead > 0)
            { /* likely */ }
            else if (pStreamDS->In.cOverruns < 32)
            {
                pStreamDS->In.cOverruns++;
                DSLOG(("DSound: Warning: Buffer full (size is %zu bytes), skipping to record data (overflow #%RU32)\n",
                       pStreamDS->cbBufSize, pStreamDS->In.cOverruns));
            }
            break;
        }

        uint32_t const cbToRead = RT_MIN(cbReadable, cbBuf);
        Log3Func(("offCapture=%#x offRead=%#x/%#x -> cbWritable=%#x cbToWrite=%#x {%s}\n", offCaptureCursor, offReadCursor,
                  pStreamDS->In.offReadPos, cbReadable, cbToRead, drvHostDSoundStreamStatusString(pStreamDS)));

        /*
         * Lock that amount of buffer.
         */
        PVOID pv1 = NULL;
        DWORD cb1 = 0;
        PVOID pv2 = NULL;
        DWORD cb2 = 0;
        hrc = directSoundCaptureLock(pStreamDS, pStreamDS->In.offReadPos, cbToRead, &pv1, &pv2, &cb1, &cb2, 0 /*dwFlags*/);
        AssertMsgReturn(SUCCEEDED(hrc), ("%Rhrc\n", hrc), VERR_ACCESS_DENIED); /** @todo translate these status codes already! */
        AssertMsg(cb1 + cb2 == cbToRead, ("%#x + %#x vs %#x\n", cb1, cb2, cbToRead));

        /*
         * Copy over the data.
         */
        memcpy(pvBuf, pv1, cb1);
        pvBuf   = (uint8_t *)pvBuf + cb1;
        cbBuf  -= cb1;
        cbRead += cb1;

        if (pv2)
        {
            memcpy(pvBuf, pv2, cb2);
            pvBuf   = (uint8_t *)pvBuf + cb2;
            cbBuf  -= cb2;
            cbRead += cb2;
        }

        /*
         * Unlock and update the write position.
         */
        directSoundCaptureUnlock(pStreamDS->In.pDSCB, pv1, pv2, cb1, cb2); /** @todo r=bird: pDSB parameter here for Unlock, but pStreamDS for Lock. Why? */
        pStreamDS->In.offReadPos = (pStreamDS->In.offReadPos + cb1 + cb2) % pStreamDS->cbBufSize;
    }

    /*
     * Done.
     */
    *pcbRead = cbRead;
    if (cbRead)
    {
        uint64_t const msPrev = pStreamDS->msLastTransfer; RT_NOREF(msPrev);
        pStreamDS->msLastTransfer = RTTimeMilliTS();
        LogFlowFunc(("cbRead=%RU32, msLastTransfer=%RU64 msNow=%RU64 cMsDelta=%RU64 {%s}\n",
                     cbRead, msPrev, pStreamDS->msLastTransfer, msPrev ? pStreamDS->msLastTransfer - msPrev : 0,
                     drvHostDSoundStreamStatusString(pStreamDS) ));
    }

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   PDMDRVINS::IBase Interface                                                                                                   *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvHostDSoundQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS     pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVHOSTDSOUND pThis   = PDMINS_2_DATA(pDrvIns, PDRVHOSTDSOUND);

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
static DECLCALLBACK(void) drvHostDSoundDestruct(PPDMDRVINS pDrvIns)
{
    PDRVHOSTDSOUND pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTDSOUND);
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    LogFlowFuncEnter();

#ifdef VBOX_WITH_AUDIO_MMNOTIFICATION_CLIENT
    if (pThis->m_pNotificationClient)
    {
        pThis->m_pNotificationClient->Unregister();
        pThis->m_pNotificationClient->Release();

        pThis->m_pNotificationClient = NULL;
    }
#endif

    PDMAudioHostEnumDelete(&pThis->DeviceEnum);

    int rc2 = RTCritSectDelete(&pThis->CritSect);
    AssertRC(rc2);

    LogFlowFuncLeave();
}


static LPCGUID dsoundConfigQueryGUID(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, const char *pszName, RTUUID *pUuid)
{
    PCPDMDRVHLPR3 pHlp = pDrvIns->pHlpR3;
    LPCGUID pGuid = NULL;

    char *pszGuid = NULL;
    int rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, pszName, &pszGuid);
    if (RT_SUCCESS(rc))
    {
        rc = RTUuidFromStr(pUuid, pszGuid);
        if (RT_SUCCESS(rc))
            pGuid = (LPCGUID)pUuid;
        else
            DSLOGREL(("DSound: Error parsing device GUID for device '%s': %Rrc\n", pszName, rc));

        RTStrFree(pszGuid);
    }

    return pGuid;
}


static void dsoundConfigInit(PDRVHOSTDSOUND pThis, PCFGMNODE pCfg)
{
    pThis->Cfg.pGuidPlay    = dsoundConfigQueryGUID(pThis->pDrvIns, pCfg, "DeviceGuidOut", &pThis->Cfg.uuidPlay);
    pThis->Cfg.pGuidCapture = dsoundConfigQueryGUID(pThis->pDrvIns, pCfg, "DeviceGuidIn",  &pThis->Cfg.uuidCapture);

    DSLOG(("DSound: Configuration: DeviceGuidOut {%RTuuid}, DeviceGuidIn {%RTuuid}\n",
           &pThis->Cfg.uuidPlay,
           &pThis->Cfg.uuidCapture));
}


/**
 * @callback_method_impl{FNPDMDRVCONSTRUCT,
 *      Construct a DirectSound Audio driver instance.}
 */
static DECLCALLBACK(int) drvHostDSoundConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVHOSTDSOUND pThis = PDMINS_2_DATA(pDrvIns, PDRVHOSTDSOUND);
    RT_NOREF(fFlags);
    LogRel(("Audio: Initializing DirectSound audio driver\n"));

    /*
     * Init basic data members and interfaces.
     */
    RTListInit(&pThis->HeadStreams);
    pThis->pDrvIns                   = pDrvIns;
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface = drvHostDSoundQueryInterface;
    /* IHostAudio */
    pThis->IHostAudio.pfnGetConfig                  = drvHostDSoundHA_GetConfig;
    pThis->IHostAudio.pfnGetDevices                 = drvHostDSoundHA_GetDevices;
    pThis->IHostAudio.pfnSetDevice                  = NULL;
    pThis->IHostAudio.pfnGetStatus                  = drvHostDSoundHA_GetStatus;
    pThis->IHostAudio.pfnDoOnWorkerThread           = NULL;
    pThis->IHostAudio.pfnStreamConfigHint           = NULL;
    pThis->IHostAudio.pfnStreamCreate               = drvHostDSoundHA_StreamCreate;
    pThis->IHostAudio.pfnStreamInitAsync            = NULL;
    pThis->IHostAudio.pfnStreamDestroy              = drvHostDSoundHA_StreamDestroy;
    pThis->IHostAudio.pfnStreamNotifyDeviceChanged  = NULL;
    pThis->IHostAudio.pfnStreamEnable               = drvHostDSoundHA_StreamEnable;
    pThis->IHostAudio.pfnStreamDisable              = drvHostDSoundHA_StreamDisable;
    pThis->IHostAudio.pfnStreamPause                = drvHostDSoundHA_StreamPause;
    pThis->IHostAudio.pfnStreamResume               = drvHostDSoundHA_StreamResume;
    pThis->IHostAudio.pfnStreamDrain                = drvHostDSoundHA_StreamDrain;
    pThis->IHostAudio.pfnStreamGetState             = drvHostDSoundHA_StreamGetState;
    pThis->IHostAudio.pfnStreamGetPending           = NULL;
    pThis->IHostAudio.pfnStreamGetWritable          = drvHostDSoundHA_StreamGetWritable;
    pThis->IHostAudio.pfnStreamPlay                 = drvHostDSoundHA_StreamPlay;
    pThis->IHostAudio.pfnStreamGetReadable          = drvHostDSoundHA_StreamGetReadable;
    pThis->IHostAudio.pfnStreamCapture              = drvHostDSoundHA_StreamCapture;

    /*
     * Init the static parts.
     */
    PDMAudioHostEnumInit(&pThis->DeviceEnum);

    pThis->fEnabledIn  = false;
    pThis->fEnabledOut = false;

    /*
     * Verify that IDirectSound is available.
     */
    LPDIRECTSOUND pDirectSound = NULL;
    HRESULT hrc = CoCreateInstance(CLSID_DirectSound, NULL, CLSCTX_ALL, IID_IDirectSound, (void **)&pDirectSound);
    if (SUCCEEDED(hrc))
        IDirectSound_Release(pDirectSound);
    else
    {
        LogRel(("DSound: DirectSound not available: %Rhrc\n", hrc));
        return VERR_AUDIO_BACKEND_INIT_FAILED;
    }

#ifdef VBOX_WITH_AUDIO_MMNOTIFICATION_CLIENT
    /*
     * Set up WASAPI device change notifications (Vista+).
     */
    if (RTSystemGetNtVersion() >= RTSYSTEM_MAKE_NT_VERSION(6, 0, 0))
    {
        /* Get the notification interface (from DrvAudio). */
# ifdef VBOX_WITH_AUDIO_CALLBACKS
        PPDMIHOSTAUDIOPORT pIHostAudioPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIHOSTAUDIOPORT);
        Assert(pIHostAudioPort);
# else
        PPDMIHOSTAUDIOPORT pIHostAudioPort = NULL;
# endif
#ifdef RT_EXCEPTIONS_ENABLED
        try
#endif
        {
            pThis->m_pNotificationClient = new DrvHostAudioDSoundMMNotifClient(pIHostAudioPort,
                                                                               pThis->Cfg.pGuidCapture == NULL,
                                                                               pThis->Cfg.pGuidPlay == NULL);
        }
#ifdef RT_EXCEPTIONS_ENABLED
        catch (std::bad_alloc &)
        {
            return VERR_NO_MEMORY;
        }
#else
        AssertReturn(pThis->m_pNotificationClient, VERR_NO_MEMORY);
#endif

        hrc = pThis->m_pNotificationClient->Initialize();
        if (SUCCEEDED(hrc))
        {
            hrc = pThis->m_pNotificationClient->Register();
            if (SUCCEEDED(hrc))
                LogRel2(("DSound: Notification client is enabled (ver %#RX64)\n", RTSystemGetNtVersion()));
            else
            {
                LogRel(("DSound: Notification client registration failed: %Rhrc\n", hrc));
                return VERR_AUDIO_BACKEND_INIT_FAILED;
            }
        }
        else
        {
            LogRel(("DSound: Notification client initialization failed: %Rhrc\n", hrc));
            return VERR_AUDIO_BACKEND_INIT_FAILED;
        }
    }
    else
        LogRel2(("DSound: Notification client is disabled (ver %#RX64)\n", RTSystemGetNtVersion()));
#endif

    /*
     * Initialize configuration values and critical section.
     */
    dsoundConfigInit(pThis, pCfg);
    return RTCritSectInit(&pThis->CritSect);
}


/**
 * PDM driver registration.
 */
const PDMDRVREG g_DrvHostDSound =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "DSoundAudio",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "DirectSound Audio host driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_AUDIO,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVHOSTDSOUND),
    /* pfnConstruct */
    drvHostDSoundConstruct,
    /* pfnDestruct */
    drvHostDSoundDestruct,
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
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};
