/* $Id: vkatInternal.h 108045 2025-01-23 17:32:37Z andreas.loeffler@oracle.com $ */
/** @file
 * VKAT - Internal header file for common definitions + structs.
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

#ifndef VBOX_INCLUDED_SRC_audio_vkatInternal_h
#define VBOX_INCLUDED_SRC_audio_vkatInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/getopt.h>

#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmaudioinline.h>
#include <VBox/vmm/pdmaudiohostenuminline.h>

#include "Audio/AudioMixBuffer.h"
#include "Audio/AudioTest.h"
#include "Audio/AudioTestService.h"
#include "Audio/AudioTestServiceClient.h"

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Common defines                                                                                                               *
*********************************************************************************************************************************/
/** Default (failsafe) timeout (in ms). */
#define AUDIOTEST_TIMEOUT_DEFAULT_MS   RT_MS_5MIN


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Audio driver stack.
 *
 * This can be just be backend driver alone or DrvAudio with a backend.
 * @todo add automatic resampling via mixer so we can test more of the audio
 *       stack used by the device emulations.
 */
typedef struct AUDIOTESTDRVSTACK
{
    /** The device registration record for the backend. */
    PCPDMDRVREG             pDrvReg;
    /** The backend driver instance. */
    PPDMDRVINS              pDrvBackendIns;
    /** The backend's audio interface. */
    PPDMIHOSTAUDIO          pIHostAudio;

    /** The DrvAudio instance. */
    PPDMDRVINS              pDrvAudioIns;
    /** This is NULL if we don't use DrvAudio. */
    PPDMIAUDIOCONNECTOR     pIAudioConnector;

    /** The current (last) audio device enumeration to use. */
    PDMAUDIOHOSTENUM        DevEnum;
} AUDIOTESTDRVSTACK;
/** Pointer to an audio driver stack. */
typedef AUDIOTESTDRVSTACK *PAUDIOTESTDRVSTACK;

/**
 * Backend-only stream structure.
 */
typedef struct AUDIOTESTDRVSTACKSTREAM
{
    /** The public stream data. */
    PDMAUDIOSTREAM          Core;
    /** The backend data (variable size). */
    PDMAUDIOBACKENDSTREAM   Backend;
} AUDIOTESTDRVSTACKSTREAM;
/** Pointer to a backend-only stream structure. */
typedef AUDIOTESTDRVSTACKSTREAM *PAUDIOTESTDRVSTACKSTREAM;

/**
 * Mixer setup for a stream.
 */
typedef struct AUDIOTESTDRVMIXSTREAM
{
    /** Pointer to the driver stack. */
    PAUDIOTESTDRVSTACK      pDrvStack;
    /** Pointer to the stream. */
    PPDMAUDIOSTREAM         pStream;
    /** Properties to use. */
    PCPDMAUDIOPCMPROPS      pProps;
    /** Set if we're mixing or just passing thru to the driver stack. */
    bool                    fDoMixing;
    /** Mixer buffer. */
    AUDIOMIXBUF             MixBuf;
    /** Write state. */
    AUDIOMIXBUFWRITESTATE   WriteState;
    /** Peek state. */
    AUDIOMIXBUFPEEKSTATE    PeekState;
} AUDIOTESTDRVMIXSTREAM;
/** Pointer to mixer setup for a stream. */
typedef AUDIOTESTDRVMIXSTREAM *PAUDIOTESTDRVMIXSTREAM;

/**
 * Enumeration specifying the current audio test mode.
 */
typedef enum AUDIOTESTMODE
{
    /** Unknown mode. */
    AUDIOTESTMODE_UNKNOWN = 0,
    /** VKAT is running on the guest side. */
    AUDIOTESTMODE_GUEST,
    /** VKAT is running on the host side. */
    AUDIOTESTMODE_HOST
} AUDIOTESTMODE;

struct AUDIOTESTENV;
/** Pointer a audio test environment. */
typedef AUDIOTESTENV *PAUDIOTESTENV;

struct AUDIOTESTDESC;
/** Pointer a audio test descriptor. */
typedef AUDIOTESTDESC *PAUDIOTESTDESC;

/**
 * Callback to set up the test parameters for a specific test.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS    if setting the parameters up succeeded. Any other error code
 *                          otherwise indicating the kind of error.
 * @param   pszTest         Test name.
 * @param   pTstParmsAcq    The audio test parameters to set up.
 */
typedef DECLCALLBACKTYPE(int, FNAUDIOTESTSETUP,(PAUDIOTESTENV pTstEnv, PAUDIOTESTDESC pTstDesc, PAUDIOTESTPARMS pTstParmsAcq, void **ppvCtx));
/** Pointer to an audio test setup callback. */
typedef FNAUDIOTESTSETUP *PFNAUDIOTESTSETUP;

typedef DECLCALLBACKTYPE(int, FNAUDIOTESTEXEC,(PAUDIOTESTENV pTstEnv, void *pvCtx, PAUDIOTESTPARMS pTstParms));
/** Pointer to an audio test exec callback. */
typedef FNAUDIOTESTEXEC *PFNAUDIOTESTEXEC;

typedef DECLCALLBACKTYPE(int, FNAUDIOTESTDESTROY,(PAUDIOTESTENV pTstEnv, void *pvCtx));
/** Pointer to an audio test destroy callback. */
typedef FNAUDIOTESTDESTROY *PFNAUDIOTESTDESTROY;

/**
 * Structure for keeping an audio test audio stream.
 */
typedef struct AUDIOTESTSTREAM
{
    /** The PDM stream. */
    PPDMAUDIOSTREAM         pStream;
    /** The backend stream. */
    PPDMAUDIOBACKENDSTREAM  pBackend;
    /** The stream config. */
    PDMAUDIOSTREAMCFG       Cfg;
    /** Associated mixing stream. Optional. */
    AUDIOTESTDRVMIXSTREAM   Mix;
} AUDIOTESTSTREAM;
/** Pointer to audio test stream. */
typedef AUDIOTESTSTREAM *PAUDIOTESTSTREAM;

/** Maximum audio streams a test environment can handle. */
#define AUDIOTESTENV_MAX_STREAMS 8

/**
 * Structure for keeping TCP/IP-specific options.
 */
typedef struct AUDIOTESTENVTCPOPTS
{
    /** Connection mode(s) to use. */
    ATSCONNMODE     enmConnMode;
    /** Bind address (server mode). When empty, "0.0.0.0" (any host) will be used. */
    char            szBindAddr[128];
    /** Bind port (server mode). */
    uint16_t        uBindPort;
    /** Connection address (client mode). */
    char            szConnectAddr[128];
    /** Connection port (client mode). */
    uint16_t        uConnectPort;
} AUDIOTESTENVTCPOPTS;
/** Pointer to audio test TCP options. */
typedef AUDIOTESTENVTCPOPTS *PAUDIOTESTENVTCPOPTS;

/**
 * Structure holding additional I/O options.
 */
typedef struct AUDIOTESTIOOPTS
{
    /** Whether to use the audio connector or not. */
    bool             fWithDrvAudio;
    /** Whether to use a mixing buffer or not. */
    bool             fWithMixer;
    /** Buffer size (in ms). */
    uint32_t         cMsBufferSize;
    /** Pre-buffering size (in ms). */
    uint32_t         cMsPreBuffer;
    /** Scheduling (in ms). */
    uint32_t         cMsSchedulingHint;
    /** Audio vlume to use (in percent). */
    uint8_t          uVolumePercent;
    /** PCM audio properties to use. */
    PDMAUDIOPCMPROPS Props;
} AUDIOTESTIOOPTS;
/** Pointer to additional playback options. */
typedef AUDIOTESTIOOPTS *PAUDIOTESTIOOPTS;

/**
 * Structure for keeping a user context for the test service callbacks.
 */
typedef struct ATSCALLBACKCTX
{
    /** The test environment bound to this context. */
    PAUDIOTESTENV pTstEnv;
    /** Absolute path to the packed up test set archive.
     *  Keep it simple for now and only support one (open) archive at a time. */
    char          szTestSetArchive[RTPATH_MAX];
    /** File handle to the (opened) test set archive for reading. */
    RTFILE        hTestSetArchive;
    /** Number of currently connected clients. */
    uint8_t       cClients;
} ATSCALLBACKCTX;
typedef ATSCALLBACKCTX *PATSCALLBACKCTX;

/**
 * Audio test environment parameters.
 *
 * This is global to all tests defined.
 */
typedef struct AUDIOTESTENV
{
    /** Audio testing mode. */
    AUDIOTESTMODE           enmMode;
    /** Failsafe timeout (in ms). Defaults to AUDIOTEST_TIMEOUT_DEFAULT_MS. */
    RTMSINTERVAL            msTimeout;
    /** Whether self test mode is active or not. */
    bool                    fSelftest;
    /** Whether skip the actual verification or not. */
    bool                    fSkipVerify;
    /** Name of the audio device to use.
     *  If empty the default audio device will be used. */
    char                    szDev[128];
    /** Zero-based index of current test (will be increased for every run test). */
    uint32_t                idxTest;
    /** Number of iterations for *all* tests specified.
     *  When set to 0 (default), a random value (see specific test) will be chosen. */
    uint32_t                cIterations;
    /** I/O options to use. */
    AUDIOTESTIOOPTS         IoOpts;
    /** Test tone parameters to use. */
    AUDIOTESTTONEPARMS      ToneParms;
    /** Output path for storing the test environment's final test files. */
    char                    szTag[AUDIOTEST_TAG_MAX];
    /** Output path for storing the test environment's final test files. */
    char                    szPathOut[RTPATH_MAX];
    /** Temporary path for this test environment. */
    char                    szPathTemp[RTPATH_MAX];
    /** Pointer to audio test driver stack to use. */
    PAUDIOTESTDRVSTACK      pDrvStack;
    /** Audio stream. */
    AUDIOTESTSTREAM         aStreams[AUDIOTESTENV_MAX_STREAMS];
    /** The audio test set to use. */
    AUDIOTESTSET            Set;
    /** TCP options to use for ATS. */
    AUDIOTESTENVTCPOPTS     TcpOpts;
    /** ATS server instance to use.
     *  NULL if not in use. */
    PATSSERVER              pSrv;
    /** ATS callback context to use. */
    ATSCALLBACKCTX          CallbackCtx;
    union
    {
        struct
        {
            /** Client connected to the ATS on the guest side. */
            ATSCLIENT       AtsClGuest;
            /** Path to the guest's test set downloaded to the host. */
            char            szPathTestSetGuest[RTPATH_MAX];
            /** Client connected to the Validation Kit audio driver ATS. */
            ATSCLIENT       AtsClValKit;
            /** Path to the Validation Kit audio driver's test set downloaded to the host. */
            char            szPathTestSetValKit[RTPATH_MAX];
        } Host;
    } u;
} AUDIOTESTENV;

/**
 * Audio test descriptor.
 */
typedef struct AUDIOTESTDESC
{
    /** (Sort of) Descriptive test name. */
    const char             *pszName;
    /** Flag whether the test is excluded. */
    bool                    fExcluded;
    /** The setup callback. */
    PFNAUDIOTESTSETUP       pfnSetup;
    /** The exec callback. */
    PFNAUDIOTESTEXEC        pfnExec;
    /** The destruction callback. */
    PFNAUDIOTESTDESTROY     pfnDestroy;
} AUDIOTESTDESC;

/**
 * Backend description.
 */
typedef struct AUDIOTESTBACKENDDESC
{
    /** The driver registration structure. */
    PCPDMDRVREG pDrvReg;
    /** The backend name.
     * Aliases are implemented by having multiple entries for the same backend.  */
    const char *pszName;
} AUDIOTESTBACKENDDESC;

/**
 * VKAT command table entry.
 */
typedef struct VKATCMD
{
    /** The command name. */
    const char     *pszCommand;
    /** The command handler.   */
    DECLCALLBACKMEMBER(RTEXITCODE, pfnHandler,(PRTGETOPTSTATE pGetState));

    /** Command description.   */
    const char     *pszDesc;
    /** Options array.  */
    PCRTGETOPTDEF   paOptions;
    /** Number of options in the option array. */
    size_t          cOptions;
    /** Gets help for an option. */
    DECLCALLBACKMEMBER(const char *, pfnOptionHelp,(PCRTGETOPTDEF pOpt));
    /** Flag indicating if the command needs the ATS transport layer.
     *  Needed for command line parsing. */
    bool            fNeedsTransport;
} VKATCMD;
/** Pointer to a const VKAT command entry. */
typedef VKATCMD const *PCVKATCMD;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Terminate ASAP if set.  Set on Ctrl-C. */
extern bool volatile    g_fTerminate;
/** The release logger. */
extern PRTLOGGER        g_pRelLogger;

/** The test handle. */
extern RTTEST           g_hTest;
/** The current verbosity level. */
extern unsigned         g_uVerbosity;
/** DrvAudio: Enable debug (or not). */
extern bool             g_fDrvAudioDebug;
/** DrvAudio: The debug output path. */
extern const char      *g_pszDrvAudioDebug;
/** Whether the host audio driver backend's caching is enabled or not. Defaults to enabled.
 *  Only implemented for DrvHostAudioWasApi backend so far, ignored otherwise. */
extern bool             g_fDrvHostAudioCacheEnabled;

extern const VKATCMD    g_CmdTest;
extern const VKATCMD    g_CmdVerify;
extern const VKATCMD    g_CmdBackends;
extern const VKATCMD    g_CmdEnum;
extern const VKATCMD    g_CmdPlay;
extern const VKATCMD    g_CmdRec;
extern const VKATCMD    g_CmdSelfTest;


extern AUDIOTESTDESC    g_aTests[];
extern unsigned         g_cTests;

extern AUDIOTESTBACKENDDESC const g_aBackends[];
extern unsigned                   g_cBackends;


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/

/** @name Command line handlers
 * @{ */
RTEXITCODE audioTestUsage(PRTSTREAM pStrm, PCVKATCMD pOnlyCmd);
RTEXITCODE audioTestVersion(void);
void       audioTestShowLogo(PRTSTREAM pStream);
/** @}  */

/** @name Driver stack
 * @{ */
int         AudioTestDriverStackPerformSelftest(void);

void        audioTestDriverStackDelete(PAUDIOTESTDRVSTACK pDrvStack);
int         audioTestDriverStackInitEx(PAUDIOTESTDRVSTACK pDrvStack, PCPDMDRVREG pDrvReg, bool fEnabledIn, bool fEnabledOut, bool fWithDrvAudio);
int         audioTestDriverStackInit(PAUDIOTESTDRVSTACK pDrvStack, PCPDMDRVREG pDrvReg, bool fWithDrvAudio);
int         audioTestDriverStackProbe(PAUDIOTESTDRVSTACK pDrvStack, bool fEnabledIn, bool fEnabledOut, bool fWithDrvAudio);
int         audioTestDriverStackSetDevice(PAUDIOTESTDRVSTACK pDrvStack, PDMAUDIODIR enmDir, const char *pszDevId);
/** @}  */

/** @name Driver
 * @{ */
int         audioTestDrvConstruct(PAUDIOTESTDRVSTACK pDrvStack, PCPDMDRVREG pDrvReg, PPDMDRVINS pParentDrvIns, PPPDMDRVINS ppDrvIns);
/** @}  */

/** @name Driver stack stream
 * @{ */
int         audioTestDriverStackStreamCreateInput(PAUDIOTESTDRVSTACK pDrvStack, PCPDMAUDIOPCMPROPS pProps,
                                                  uint32_t cMsBufferSize, uint32_t cMsPreBuffer, uint32_t cMsSchedulingHint,
                                                  PPDMAUDIOSTREAM *ppStream, PPDMAUDIOSTREAMCFG pCfgAcq);
int         audioTestDriverStackStreamCreateOutput(PAUDIOTESTDRVSTACK pDrvStack, PCPDMAUDIOPCMPROPS pProps,
                                                   uint32_t cMsBufferSize, uint32_t cMsPreBuffer, uint32_t cMsSchedulingHint,
                                                   PPDMAUDIOSTREAM *ppStream, PPDMAUDIOSTREAMCFG pCfgAcq);
void        audioTestDriverStackStreamDestroy(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream);
int         audioTestDriverStackStreamDrain(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream, bool fSync);
int         audioTestDriverStackStreamEnable(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream);
int         AudioTestDriverStackStreamDisable(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream);
bool        audioTestDriverStackStreamIsOkay(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream);
uint32_t    audioTestDriverStackStreamGetWritable(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream);
int         audioTestDriverStackStreamPlay(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream, void const *pvBuf,
                                           uint32_t cbBuf, uint32_t *pcbPlayed);
uint32_t    audioTestDriverStackStreamGetReadable(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream);
int         audioTestDriverStackStreamCapture(PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream,
                                              void *pvBuf, uint32_t cbBuf, uint32_t *pcbCaptured);
/** @}  */

/** @name Backend handling
 * @{ */
PCPDMDRVREG AudioTestGetDefaultBackend(void);
PCPDMDRVREG AudioTestFindBackendOpt(const char *pszBackend);
/** @}  */

/** @name Mixing stream
 * @{ */
int         AudioTestMixStreamInit(PAUDIOTESTDRVMIXSTREAM pMix, PAUDIOTESTDRVSTACK pDrvStack, PPDMAUDIOSTREAM pStream,
                                   PCPDMAUDIOPCMPROPS pProps, uint32_t cMsBuffer);
void        AudioTestMixStreamTerm(PAUDIOTESTDRVMIXSTREAM pMix);
int         AudioTestMixStreamEnable(PAUDIOTESTDRVMIXSTREAM pMix);
int         AudioTestMixStreamDrain(PAUDIOTESTDRVMIXSTREAM pMix, bool fSync);
int         AudioTestMixStreamDisable(PAUDIOTESTDRVMIXSTREAM pMix);
bool        AudioTestMixStreamIsOkay(PAUDIOTESTDRVMIXSTREAM pMix);
uint32_t    AudioTestMixStreamGetWritable(PAUDIOTESTDRVMIXSTREAM pMix);
int         AudioTestMixStreamPlay(PAUDIOTESTDRVMIXSTREAM pMix, void const *pvBuf, uint32_t cbBuf, uint32_t *pcbPlayed);
uint32_t    AudioTestMixStreamGetReadable(PAUDIOTESTDRVMIXSTREAM pMix);
int         AudioTestMixStreamCapture(PAUDIOTESTDRVMIXSTREAM pMix, void *pvBuf, uint32_t cbBuf, uint32_t *pcbCaptured);
void        AudioTestMixStreamSetVolume(PAUDIOTESTDRVMIXSTREAM pMix, uint8_t uVolumePercent);
/** @}  */

/** @name Device handling
 * @{ */
int         audioTestDeviceOpen(PPDMAUDIOHOSTDEV pDev);
int         audioTestDeviceClose(PPDMAUDIOHOSTDEV pDev);

int         audioTestDevicesEnumerateAndCheck(PAUDIOTESTDRVSTACK pDrvStack, const char *pszDev, PPDMAUDIOHOSTDEV *ppDev);
/** @}  */

/** @name ATS routines
 * @{ */
int         audioTestEnvConnectToValKitAts(PAUDIOTESTENV pTstEnv,
                                         const char *pszHostTcpAddr, uint32_t uHostTcpPort);
/** @}  */

/** @name Test environment handling
 * @{ */
void        audioTestEnvInit(PAUDIOTESTENV pTstEnv);
int         audioTestEnvCreate(PAUDIOTESTENV pTstEnv, PAUDIOTESTDRVSTACK pDrvStack);
void        audioTestEnvDestroy(PAUDIOTESTENV pTstEnv);
int         audioTestEnvPrologue(PAUDIOTESTENV pTstEnv, bool fPack, char *pszPackFile, size_t cbPackFile);

void        audioTestParmsInit(PAUDIOTESTPARMS pTstParms);
void        audioTestParmsDestroy(PAUDIOTESTPARMS pTstParms);
/** @}  */

int         audioTestWorker(PAUDIOTESTENV pTstEnv);

/** @todo Test tone handling */
int         audioTestPlayTone(PAUDIOTESTIOOPTS pIoOpts, PAUDIOTESTENV pTstEnv, PAUDIOTESTSTREAM pStream, PAUDIOTESTTONEPARMS pParms);
void        audioTestToneParmsInit(PAUDIOTESTTONEPARMS pToneParms);
/** @}  */

void        audioTestIoOptsInitDefaults(PAUDIOTESTIOOPTS pIoOpts);


/*********************************************************************************************************************************
*   Common command line stuff                                                                                                    *
*********************************************************************************************************************************/

/**
 * Common long options values.
 */
enum
{
    AUDIO_TEST_OPT_CMN_DAEMONIZE = 256,
    AUDIO_TEST_OPT_CMN_DAEMONIZED,
    AUDIO_TEST_OPT_CMN_DEBUG_AUDIO_ENABLE,
    AUDIO_TEST_OPT_CMN_DEBUG_AUDIO_PATH,
    AUDIO_TEST_OPT_CMN_DRVHOST_CACHE_ENABLED
};

/** For use in the option switch to handle common options. */
#define AUDIO_TEST_COMMON_OPTION_CASES(a_ValueUnion, a_pCmd) \
            case 'q': \
                g_uVerbosity = 0; \
                if (g_pRelLogger) \
                    RTLogGroupSettings(g_pRelLogger, "all=0 all.e"); \
                break; \
            \
            case 'v': \
                /* No-op here, has been handled by main() already. */ /** @todo r-bird: -q works, so -v must too! */ \
                break; \
            \
            case 'V': \
                return audioTestVersion(); \
            \
            case 'h': \
                return audioTestUsage(g_pStdOut, a_pCmd); \
            \
            case AUDIO_TEST_OPT_CMN_DEBUG_AUDIO_ENABLE: \
                g_fDrvAudioDebug = true; \
                break; \
            \
            case AUDIO_TEST_OPT_CMN_DRVHOST_CACHE_ENABLED: \
                g_fDrvHostAudioCacheEnabled = a_ValueUnion.f; \
                break; \
            \
            case AUDIO_TEST_OPT_CMN_DEBUG_AUDIO_PATH: \
                g_pszDrvAudioDebug = a_ValueUnion.psz; \
                break; \
            case AUDIO_TEST_OPT_CMN_DAEMONIZE: \
                break; \
            case AUDIO_TEST_OPT_CMN_DAEMONIZED: \
                break;

#endif /* !VBOX_INCLUDED_SRC_audio_vkatInternal_h */

