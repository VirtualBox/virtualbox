/* $Id: NetPerf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NetPerf - Network Performance Benchmark.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/param.h>
#include <iprt/process.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/tcp.h>
#include <iprt/thread.h>
#include <iprt/test.h>
#include <iprt/time.h>
#include <iprt/timer.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Default TCP port (update help text if you change this) */
#define NETPERF_DEFAULT_PORT                    5002

/** Default TCP packet size (bytes) */
#define NETPERF_DEFAULT_PKT_SIZE_THROUGHPUT     8192
/** Default TCP packet size (bytes) */
#define NETPERF_DEFAULT_PKT_SIZE_LATENCY        1024
/** Maximum packet size possible (bytes). */
#define NETPERF_MAX_PKT_SIZE                    _1M
/** Minimum packet size possible (bytes). */
#define NETPERF_MIN_PKT_SIZE                    sizeof(NETPERFHDR)

/** Default timeout in (seconds) */
#define NETPERF_DEFAULT_TIMEOUT                 10
/** Maximum timeout possible (seconds). */
#define NETPERF_MAX_TIMEOUT                     3600 /* 1h */
/** Minimum timeout possible (seconds). */
#define NETPERF_MIN_TIMEOUT                     1

/** The default warmup time (ms). */
#define NETPERF_DEFAULT_WARMUP                  1000  /*  1s */
/** The maxium warmup time (ms). */
#define NETPERF_MAX_WARMUP                      60000 /* 60s */
/** The minimum warmup time (ms). */
#define NETPERF_MIN_WARMUP                      1000  /*  1s */

/** The default cool down time (ms). */
#define NETPERF_DEFAULT_COOL_DOWN               1000  /*  1s */
/** The maxium cool down time (ms). */
#define NETPERF_MAX_COOL_DOWN                   60000 /* 60s */
/** The minimum cool down time (ms). */
#define NETPERF_MIN_COOL_DOWN                   1000  /*  1s */

/** Maximum socket buffer size possible (bytes). */
#define NETPERF_MAX_BUF_SIZE                    _128M
/** Minimum socket buffer size possible (bytes). */
#define NETPERF_MIN_BUF_SIZE                    256

/** The length of the length prefix used when submitting parameters and
 * results. */
#define NETPERF_LEN_PREFIX                      4


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef enum NETPERFPROTO
{
    NETPERFPROTO_INVALID = 0,
    NETPERFPROTO_TCP
    //NETPERFPROTO_UDP
} NETPERFPROTO;

/**
 * What kind of test we're performing.
 */
typedef enum NETPERFMODE
{
    NETPERFMODE_INVALID = 0,
    /** Latency of a symmetric packet exchange. */
    NETPERFMODE_LATENCY,
    /** Separate throughput measurements for each direction. */
    NETPERFMODE_THROUGHPUT,
    /** Transmit throughput. */
    NETPERFMODE_THROUGHPUT_XMIT,
    /** Transmit throughput. */
    NETPERFMODE_THROUGHPUT_RECV
} NETPERFMODE;

/**
 * Statistics.
 */
typedef struct NETPERFSTATS
{
    uint64_t        cTx;
    uint64_t        cRx;
    uint64_t        cEchos;
    uint64_t        cErrors;
    uint64_t        cNsElapsed;
} NETPERFSTATS;

/**
 * Settings & a little bit of state.
 */
typedef struct NETPERFPARAMS
{
    /** @name Static settings
     * @{ */
    /** The TCP port number. */
    uint32_t        uPort;
    /** Client: Use server statistcs. */
    bool            fServerStats;
    /** Server: Quit after the first client. */
    bool            fSingleClient;
    /** Send and receive buffer sizes for TCP sockets, zero if to use defaults. */
    uint32_t        cbBufferSize;
    /** @} */

    /** @name Dynamic settings
     * @{ */
    /** Disable send packet coalescing. */
    bool            fNoDelay;
    /** Detect broken payloads. */
    bool            fCheckData;
    /** The test mode. */
    NETPERFMODE     enmMode;
    /** The number of seconds to run each of the test steps. */
    uint32_t        cSecTimeout;
    /** Number of millisecond to spend warning up before testing. */
    uint32_t        cMsWarmup;
    /** Number of millisecond to spend cooling down after the testing. */
    uint32_t        cMsCoolDown;
    /** The packet size. */
    uint32_t        cbPacket;
    /** @} */

    /** @name State
     * @{ */
    RTSOCKET        hSocket;
    /** @} */
} NETPERFPARAMS;

/**
 * Packet header used in tests.
 *
 * Need to indicate when we've timed out and it's time to reverse the roles or
 * stop testing.
 */
typedef struct NETPERFHDR
{
    /** Magic value (little endian). */
    uint32_t        u32Magic;
    /** State value. */
    uint32_t        u32State;
    /** Sequence number (little endian). */
    uint32_t        u32Seq;
    /** Reserved, must be zero. */
    uint32_t        u32Reserved;
} NETPERFHDR;

/** Magic value for NETPERFHDR::u32Magic. */
#define NETPERFHDR_MAGIC     UINT32_C(0xfeedf00d)

/** @name Packet State (NETPERF::u32Magic)
 * @{ */
/** Warm up. */
#define NETPERFHDR_WARMUP    UINT32_C(0x0c0ffe01)
/** The clock is running. */
#define NETPERFHDR_TESTING   UINT32_C(0x0c0ffe02)
/** Stop the clock but continue the package flow. */
#define NETPERFHDR_COOL_DOWN UINT32_C(0x0c0ffe03)
/** Done, stop the clock if not done already and reply with results. */
#define NETPERFHDR_DONE      UINT32_C(0x0c0ffe04)
/** @} */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Connection start/identifier to make sure other end is NetPerf. */
static const char g_ConnectStart[]  = "yo! waaazzzzzaaaaup dude?";
/** Start of parameters proposal made by the client. */
static const char g_szStartParams[] = "deal?";
/** All okay to start test */
static const char g_szAck[]         = "okay!";
/** Negative. */
static const char g_szNegative[]    = "nope!";
AssertCompile(sizeof(g_szAck) == sizeof(g_szNegative));
/** Start of statistics. */
static const char g_szStartStats[]  = "dude, stats";

/** Command line parameters */
static const RTGETOPTDEF g_aCmdOptions[] =
{
    { "--server",           's', RTGETOPT_REQ_NOTHING },
    { "--client",           'c', RTGETOPT_REQ_STRING  },
    { "--interval",         'i', RTGETOPT_REQ_UINT32  },
    { "--port",             'p', RTGETOPT_REQ_UINT32  },
    { "--len",              'l', RTGETOPT_REQ_UINT32  },
    { "--nodelay",          'N', RTGETOPT_REQ_NOTHING },
    { "--mode",             'm', RTGETOPT_REQ_STRING  },
    { "--warmup",           'w', RTGETOPT_REQ_UINT32  },
    { "--cool-down",        'W', RTGETOPT_REQ_UINT32  },
    { "--server-stats",     'S', RTGETOPT_REQ_NOTHING },
    { "--single-client",    '1', RTGETOPT_REQ_NOTHING },
    { "--daemonize",        'd', RTGETOPT_REQ_NOTHING },
    { "--daemonized",       'D', RTGETOPT_REQ_NOTHING },
    { "--check-data",       'C', RTGETOPT_REQ_NOTHING },
    { "--verbose",          'v', RTGETOPT_REQ_NOTHING },
    { "--buffer-size",      'b', RTGETOPT_REQ_UINT32  },
    { "--help",             'h', RTGETOPT_REQ_NOTHING } /* for Usage() */
};

/** The test handle. */
static RTTEST g_hTest;
/** Verbosity level. */
static uint32_t g_uVerbosity = 0;



static void Usage(PRTSTREAM pStrm)
{
    char szExec[RTPATH_MAX];
    RTStrmPrintf(pStrm, "usage: %s <-s|-c <host>> [options]\n",
                 RTPathFilename(RTProcGetExecutablePath(szExec, sizeof(szExec))));
    RTStrmPrintf(pStrm, "\n");
    RTStrmPrintf(pStrm, "options: \n");


    for (unsigned i = 0; i < RT_ELEMENTS(g_aCmdOptions); i++)
    {
        const char *pszHelp;
        switch (g_aCmdOptions[i].iShort)
        {
            case 'h':
                pszHelp = "Displays this help and exit";
                break;
            case 's':
                pszHelp = "Run in server mode, waiting for clients (default)";
                break;
            case 'c':
                pszHelp = "Run in client mode, connecting to <host>";
                break;
            case 'i':
                pszHelp = "Interval in seconds to run the test (default " RT_XSTR(NETPERF_DEFAULT_TIMEOUT) " s)";
                break;
            case 'p':
                pszHelp = "Server port to listen/connect to (default " RT_XSTR(NETPERF_DEFAULT_PORT) ")";
                break;
            case 'l':
                pszHelp = "Packet size in bytes (defaults to " RT_XSTR(NETPERF_DEFAULT_PKT_SIZE_LATENCY)
                    " for latency and " RT_XSTR(NETPERF_DEFAULT_PKT_SIZE_THROUGHPUT) " for throughput)";
                break;
            case 'm':
                pszHelp = "Test mode: latency (default), throughput, throughput-xmit or throughput-recv";
                break;
            case 'N':
                pszHelp = "Set TCP no delay, disabling Nagle's algorithm";
                break;
            case 'S':
                pszHelp = "Report server stats, ignored if server";
                break;
            case '1':
                pszHelp = "Stop the server after the first client";
                break;
            case 'd':
                pszHelp = "Daemonize if server, ignored if client";
                break;
            case 'D':
                continue; /* internal */
            case 'w':
                pszHelp = "Warmup time, in milliseconds (default " RT_XSTR(NETPERF_DEFAULT_WARMUP) " ms)";
                break;
            case 'W':
                pszHelp = "Cool down time, in milliseconds (default " RT_XSTR(NETPERF_DEFAULT_COOL_DOWN) " ms)";
                break;
            case 'C':
                pszHelp = "Check payload data at the receiving end";
                break;
            case 'b':
                pszHelp = "Send and receive buffer sizes for TCP";
                break;
            case 'v':
                pszHelp = "Verbose execution.";
                break;
            default:
                pszHelp = "Option undocumented";
                break;
        }
        char szOpt[256];
        RTStrPrintf(szOpt, sizeof(szOpt), "%s, -%c", g_aCmdOptions[i].pszLong, g_aCmdOptions[i].iShort);
        RTStrmPrintf(pStrm, "  %-20s%s\n", szOpt, pszHelp);
    }
}

/**
 * Timer callback employed to set the stop indicator.
 *
 * This is used both by the client and server side.
 *
 * @param   hTimer              The timer, ignored.
 * @param   pvUser              Pointer to the stop variable.
 * @param   iTick               The tick, ignored.
 */
static DECLCALLBACK(void) netperfStopTimerCallback(RTTIMERLR hTimer, void *pvUser, uint64_t iTick)
{
    bool volatile *pfStop = (bool volatile *)pvUser;
    if (g_uVerbosity > 0)
        RTPrintf("Time's Up!\n");
    ASMAtomicWriteBool(pfStop, true);
    NOREF(hTimer); NOREF(iTick);
}

/**
 * Sends a statistics packet to our peer.
 *
 * @returns IPRT status code.
 * @param   pStats              The stats to send.
 * @param   hSocket             The TCP socket to send them to.
 */
static int netperfSendStats(NETPERFSTATS const *pStats, RTSOCKET hSocket)
{
    char szBuf[256 + NETPERF_LEN_PREFIX];
    size_t cch = RTStrPrintf(&szBuf[NETPERF_LEN_PREFIX], sizeof(szBuf) - NETPERF_LEN_PREFIX,
                             "%s:%llu:%llu:%llu:%llu:%llu",
                             g_szStartStats,
                             pStats->cTx,
                             pStats->cRx,
                             pStats->cEchos,
                             pStats->cErrors,
                             pStats->cNsElapsed);

    RTStrPrintf(szBuf, NETPERF_LEN_PREFIX + 1, "%0*u", NETPERF_LEN_PREFIX, cch);
    szBuf[NETPERF_LEN_PREFIX] = g_szStartStats[0];
    Assert(strlen(szBuf) == cch + NETPERF_LEN_PREFIX);

    int rc = RTTcpWrite(hSocket, szBuf, cch + NETPERF_LEN_PREFIX);
    if (RT_FAILURE(rc))
        return RTTestIFailedRc(rc, "stats: Failed to send stats: %Rrc\n", rc);

    /*
     * Wait for ACK.
     */
    rc = RTTcpRead(hSocket, szBuf, sizeof(g_szAck) - 1, NULL);
    if (RT_FAILURE(rc))
        return RTTestIFailedRc(rc, "stats: failed to write stats: %Rrc\n", rc);
    szBuf[sizeof(g_szAck) - 1] = '\0';
    if (!strcmp(szBuf, g_szNegative))
        return RTTestIFailedRc(rc, "stats: client failed to parse them\n");
    if (strcmp(szBuf, g_szAck))
        return RTTestIFailedRc(rc, "stats: got '%s' in instead of ack/nack\n", szBuf);

    return VINF_SUCCESS;
}

/**
 * Receives a statistics packet from our peer.
 *
 * @returns IPRT status code. Error signalled.
 * @param   pStats              Where to receive the stats.
 * @param   hSocket             The TCP socket to recevie them from.
 */
static int netperfRecvStats(NETPERFSTATS *pStats, RTSOCKET hSocket)
{
    /*
     * Read the stats message.
     */
    /* the length prefix */
    char szBuf[256 + NETPERF_LEN_PREFIX];
    int rc = RTTcpRead(hSocket, szBuf, NETPERF_LEN_PREFIX, NULL);
    if (RT_FAILURE(rc))
        return RTTestIFailedRc(rc, "stats: failed to read stats prefix: %Rrc\n", rc);

    szBuf[NETPERF_LEN_PREFIX] = '\0';
    uint32_t cch;
    rc = RTStrToUInt32Full(szBuf, 10, &cch);
    if (rc != VINF_SUCCESS)
        return RTTestIFailedRc(RT_SUCCESS(rc) ? -rc : rc, "stats: bad stat length prefix: '%s' - %Rrc\n", szBuf, rc);
    if (cch >= sizeof(szBuf))
        return RTTestIFailedRc(VERR_BUFFER_OVERFLOW, "stats: too large: %u bytes\n", cch);

    /* the actual message */
    rc = RTTcpRead(hSocket, szBuf, cch, NULL);
    if (RT_FAILURE(rc))
        return RTTestIFailedRc(rc, "failed to read stats: %Rrc\n", rc);
    szBuf[cch] = '\0';

    /*
     * Validate the message header.
     */
    if (   strncmp(szBuf, g_szStartStats, sizeof(g_szStartStats) - 1)
        || szBuf[sizeof(g_szStartStats) - 1] != ':')
        return RTTestIFailedRc(VERR_NET_PROTOCOL_ERROR, "stats: invalid packet start: '%s'\n", szBuf);
    char *pszCur = &szBuf[sizeof(g_szStartStats)];

    /*
     * Parse it.
     */
    static const char * const s_apszNames[] =
    {
        "cTx", "cRx", "cEchos", "cErrors", "cNsElapsed"
    };
    uint64_t *apu64[RT_ELEMENTS(s_apszNames)] =
    {
        &pStats->cTx,
        &pStats->cRx,
        &pStats->cEchos,
        &pStats->cErrors,
        &pStats->cNsElapsed
    };

    for (unsigned i = 0; i < RT_ELEMENTS(apu64); i++)
    {
        if (!pszCur)
            return RTTestIFailedRc(VERR_PARSE_ERROR, "stats: missing %s\n", s_apszNames[i]);

        char *pszNext = strchr(pszCur, ':');
        if (pszNext)
            *pszNext++ = '\0';
        rc = RTStrToUInt64Full(pszCur, 10, apu64[i]);
        if (rc != VINF_SUCCESS)
            return RTTestIFailedRc(RT_SUCCESS(rc) ? -rc : rc, "stats: bad value for %s: '%s' - %Rrc\n",
                                   s_apszNames[i], pszCur, rc);

        pszCur = pszNext;
    }

    if (pszCur)
        return RTTestIFailedRc(VERR_PARSE_ERROR, "stats: Unparsed data: '%s'\n", pszCur);

    /*
     * Send ACK.
     */
    rc = RTTcpWrite(hSocket, g_szAck, sizeof(g_szAck) - 1);
    if (RT_FAILURE(rc))
        return RTTestIFailedRc(rc, "stats: failed to write ack: %Rrc\n", rc);

    return VINF_SUCCESS;
}

/**
 * TCP Throughput: Print the statistics.
 *
 * @param   pSendStats          Send stats.
 * @param   pRecvStats          Receive stats.
 * @param   cbPacket            Packet size.
 */
static void netperfPrintThroughputStats(NETPERFSTATS const *pSendStats, NETPERFSTATS const *pRecvStats, uint32_t cbPacket)
{
    RTTestIValue("Packet size",         cbPacket,          RTTESTUNIT_BYTES);

    if (pSendStats)
    {
        double rdSecElapsed = (double)pSendStats->cNsElapsed / 1000000000.0;
        RTTestIValue("Sends",               pSendStats->cTx,              RTTESTUNIT_PACKETS);
        RTTestIValue("Send Interval",       pSendStats->cNsElapsed,       RTTESTUNIT_NS);
        RTTestIValue("Send Throughput",     (uint64_t)((double)(cbPacket * pSendStats->cTx) / rdSecElapsed), RTTESTUNIT_BYTES_PER_SEC);
        RTTestIValue("Send Rate",           (uint64_t)((double)pSendStats->cTx / rdSecElapsed),  RTTESTUNIT_PACKETS_PER_SEC);
        RTTestIValue("Send Latency",        (uint64_t)(rdSecElapsed / (double)pSendStats->cTx * 1000000000.0), RTTESTUNIT_NS_PER_PACKET);
    }

    if (pRecvStats)
    {
        double rdSecElapsed = (double)pRecvStats->cNsElapsed / 1000000000.0;
        RTTestIValue("Receives",            pRecvStats->cRx,              RTTESTUNIT_PACKETS);
        RTTestIValue("Receive Interval",    pRecvStats->cNsElapsed,       RTTESTUNIT_NS);
        RTTestIValue("Receive Throughput",  (uint64_t)((double)(cbPacket * pRecvStats->cRx) / rdSecElapsed), RTTESTUNIT_BYTES_PER_SEC);
        RTTestIValue("Receive Rate",        (uint64_t)((double)pRecvStats->cRx / rdSecElapsed),  RTTESTUNIT_PACKETS_PER_SEC);
        RTTestIValue("Receive Latency",     (uint64_t)(rdSecElapsed / (double)pRecvStats->cRx * 1000000000.0), RTTESTUNIT_NS_PER_PACKET);
    }
}

/**
 * TCP Throughput: Send data to the other party.
 *
 * @returns IPRT status code.
 * @param   pParams             The TCP parameters block.
 * @param   pBuf                The buffer we're using when sending.
 * @param   pSendStats          Where to return the statistics.
 */
static int netperfTCPThroughputSend(NETPERFPARAMS const *pParams, NETPERFHDR *pBuf, NETPERFSTATS *pSendStats)
{
    RT_ZERO(*pSendStats);

    /*
     * Create the timer
     */
    RTTIMERLR       hTimer;
    bool volatile   fStop = false;
    int rc = RTTimerLRCreateEx(&hTimer, 0 /* nsec */, RTTIMER_FLAGS_CPU_ANY, netperfStopTimerCallback, (void *)&fStop);
    if (RT_SUCCESS(rc))
    {
        uint32_t u32Seq = 0;

        RT_BZERO(pBuf, pParams->cbPacket);
        pBuf->u32Magic      = RT_H2LE_U32_C(NETPERFHDR_MAGIC);
        pBuf->u32State      = 0;
        pBuf->u32Seq        = 0;
        pBuf->u32Reserved   = 0;

        /*
         * Warm up.
         */
        if (g_uVerbosity > 0)
            RTPrintf("Warmup...\n");
        pBuf->u32State = RT_H2LE_U32_C(NETPERFHDR_WARMUP);
        rc = RTTimerLRStart(hTimer, pParams->cMsWarmup * UINT64_C(1000000) /* nsec */);
        if (RT_SUCCESS(rc))
        {
            while (!fStop)
            {
                u32Seq++;
                pBuf->u32Seq = RT_H2LE_U32(u32Seq);
                rc = RTTcpWrite(pParams->hSocket, pBuf, pParams->cbPacket);
                if (RT_FAILURE(rc))
                {
                    RTTestIFailed("RTTcpWrite/warmup: %Rrc\n", rc);
                    break;
                }
            }
        }
        else
            RTTestIFailed("RTTimerLRStart/warmup: %Rrc\n", rc);

        /*
         * The real thing.
         */
        if (RT_SUCCESS(rc))
        {
            if (g_uVerbosity > 0)
                RTPrintf("The real thing...\n");
            pBuf->u32State = RT_H2LE_U32_C(NETPERFHDR_TESTING);
            fStop          = false;
            rc = RTTimerLRStart(hTimer, pParams->cSecTimeout * UINT64_C(1000000000) /* nsec */);
            if (RT_SUCCESS(rc))
            {
                uint64_t u64StartTS = RTTimeNanoTS();
                while (!fStop)
                {
                    u32Seq++;
                    pBuf->u32Seq = RT_H2LE_U32(u32Seq);
                    rc = RTTcpWrite(pParams->hSocket, pBuf, pParams->cbPacket);
                    if (RT_FAILURE(rc))
                    {
                        RTTestIFailed("RTTcpWrite/testing: %Rrc\n", rc);
                        break;
                    }
                    pSendStats->cTx++;
                }
                pSendStats->cNsElapsed = RTTimeNanoTS() - u64StartTS;
            }
            else
                RTTestIFailed("RTTimerLRStart/testing: %Rrc\n", rc);
        }

        /*
         * Cool down.
         */
        if (RT_SUCCESS(rc))
        {
            if (g_uVerbosity > 0)
                RTPrintf("Cool down...\n");
            pBuf->u32State = RT_H2LE_U32_C(NETPERFHDR_COOL_DOWN);
            fStop          = false;
            rc = RTTimerLRStart(hTimer, pParams->cMsCoolDown * UINT64_C(1000000) /* nsec */);
            if (RT_SUCCESS(rc))
            {
                while (!fStop)
                {
                    u32Seq++;
                    pBuf->u32Seq = RT_H2LE_U32(u32Seq);
                    rc = RTTcpWrite(pParams->hSocket, pBuf, pParams->cbPacket);
                    if (RT_FAILURE(rc))
                    {
                        RTTestIFailed("RTTcpWrite/cool down: %Rrc\n", rc);
                        break;
                    }
                }
            }
            else
                RTTestIFailed("RTTimerLRStart/testing: %Rrc\n", rc);
        }

        /*
         * Send DONE packet.
         */
        if (g_uVerbosity > 0)
            RTPrintf("Done\n");
        if (RT_SUCCESS(rc))
        {
            u32Seq++;
            pBuf->u32Seq   = RT_H2LE_U32(u32Seq);
            pBuf->u32State = RT_H2LE_U32_C(NETPERFHDR_DONE);
            rc = RTTcpWrite(pParams->hSocket, pBuf, pParams->cbPacket);
            if (RT_FAILURE(rc))
                RTTestIFailed("RTTcpWrite/done: %Rrc\n", rc);
        }

        RTTimerLRDestroy(hTimer);
    }
    else
        RTTestIFailed("Failed to create timer object: %Rrc\n", rc);
    return rc;
}


/**
 * TCP Throughput: Receive data from the other party.
 *
 * @returns IPRT status code.
 * @param   pParams             The TCP parameters block.
 * @param   pBuf                The buffer we're using when sending.
 * @param   pStats              Where to return the statistics.
 */
static int netperfTCPThroughputRecv(NETPERFPARAMS const *pParams, NETPERFHDR *pBuf, NETPERFSTATS *pStats)
{
    RT_ZERO(*pStats);

    int         rc;
    uint32_t    u32Seq      = 0;
    uint64_t    cRx         = 0;
    uint64_t    u64StartTS  = 0;
    uint32_t    uState      = RT_H2LE_U32_C(NETPERFHDR_WARMUP);

    for (;;)
    {
        rc = RTTcpRead(pParams->hSocket, pBuf, pParams->cbPacket, NULL);
        if (RT_FAILURE(rc))
        {
            pStats->cErrors++;
            RTTestIFailed("RTTcpRead failed: %Rrc\n", rc);
            break;
        }
        if (RT_UNLIKELY(   pBuf->u32Magic    != RT_H2LE_U32_C(NETPERFHDR_MAGIC)
                        || pBuf->u32Reserved != 0))
        {
            pStats->cErrors++;
            RTTestIFailed("Invalid magic or reserved field value: %#x %#x\n", RT_H2LE_U32(pBuf->u32Magic), RT_H2LE_U32(pBuf->u32Reserved));
            rc = VERR_INVALID_MAGIC;
            break;
        }

        u32Seq += 1;
        if (RT_UNLIKELY(pBuf->u32Seq != RT_H2LE_U32(u32Seq)))
        {
            pStats->cErrors++;
            RTTestIFailed("Out of sequence: got %#x, expected %#x\n", RT_H2LE_U32(pBuf->u32Seq), u32Seq);
            rc = VERR_WRONG_ORDER;
            break;
        }

        if (pParams->fCheckData && uState == RT_H2LE_U32_C(NETPERFHDR_TESTING))
        {
            unsigned i = sizeof(NETPERFHDR);
            for (;i < pParams->cbPacket; ++i)
                if (((unsigned char *)pBuf)[i])
                    break;
            if (i != pParams->cbPacket)
            {
                pStats->cErrors++;
                RTTestIFailed("Broken payload: at %#x got %#x, expected %#x\n", i, ((unsigned char *)pBuf)[i], 0);
                rc = VERR_NOT_EQUAL;
                break;
            }
        }
        if (RT_LIKELY(pBuf->u32State == uState))
            cRx++;
        /*
         * Validate and act on switch state.
         */
        else if (   uState         == RT_H2LE_U32_C(NETPERFHDR_WARMUP)
                 && pBuf->u32State == RT_H2LE_U32_C(NETPERFHDR_TESTING))
        {
            cRx = 0;
            u64StartTS = RTTimeNanoTS();
            uState = pBuf->u32State;
        }
        else if (   uState             == RT_H2LE_U32_C(NETPERFHDR_TESTING)
                 && (   pBuf->u32State == RT_H2LE_U32_C(NETPERFHDR_COOL_DOWN)
                     || pBuf->u32State == RT_H2LE_U32_C(NETPERFHDR_DONE)) )
        {
            pStats->cNsElapsed = RTTimeNanoTS() - u64StartTS;
            pStats->cRx        = cRx + 1;
            uState = pBuf->u32State;
            if (uState == RT_H2LE_U32_C(NETPERFHDR_DONE))
                break;
        }
        else if (   uState         == RT_H2LE_U32_C(NETPERFHDR_COOL_DOWN)
                 && pBuf->u32State == RT_H2LE_U32_C(NETPERFHDR_DONE))
        {
            uState = pBuf->u32State;
            break;
        }
        else
        {
            pStats->cErrors++;
            RTTestIFailed("Protocol error: invalid state transition %#x -> %#x\n",
                          RT_LE2H_U32(uState), RT_LE2H_U32(pBuf->u32State));
            rc = VERR_INVALID_MAGIC;
            break;
        }
    }

    AssertReturn(uState == RT_H2LE_U32_C(NETPERFHDR_DONE) || RT_FAILURE(rc), VERR_INVALID_STATE);
    return rc;
}


/**
 * Prints the statistics for the latency test.
 *
 * @param   pStats              The statistics.
 * @param   cbPacket            The packet size in bytes.
 */
static void netperfPrintLatencyStats(NETPERFSTATS const *pStats, uint32_t cbPacket)
{
    double rdSecElapsed = (double)pStats->cNsElapsed / 1000000000.0;
    RTTestIValue("Transmitted",         pStats->cTx,             RTTESTUNIT_PACKETS);
    RTTestIValue("Successful echos",    pStats->cEchos,          RTTESTUNIT_PACKETS);
    RTTestIValue("Errors",              pStats->cErrors,         RTTESTUNIT_PACKETS);
    RTTestIValue("Interval",            pStats->cNsElapsed,      RTTESTUNIT_NS);
    RTTestIValue("Packet size",         cbPacket,                RTTESTUNIT_BYTES);
    RTTestIValue("Average rate",        (uint64_t)((double)pStats->cEchos / rdSecElapsed),  RTTESTUNIT_PACKETS_PER_SEC);
    RTTestIValue("Average throughput",  (uint64_t)((double)(cbPacket * pStats->cEchos) / rdSecElapsed), RTTESTUNIT_BYTES_PER_SEC);
    RTTestIValue("Average latency",     (uint64_t)(rdSecElapsed / (double)pStats->cEchos * 1000000000.0), RTTESTUNIT_NS_PER_ROUND_TRIP);
    RTTestISubDone();
}


/**
 * NETPERFMODE -> string.
 *
 * @returns readonly string.
 * @param   enmMode             The mode.
 */
static const char *netperfModeToString(NETPERFMODE enmMode)
{
    switch (enmMode)
    {
        case NETPERFMODE_LATENCY:           return "latency";
        case NETPERFMODE_THROUGHPUT:        return "throughput";
        case NETPERFMODE_THROUGHPUT_XMIT:   return "throughput-xmit";
        case NETPERFMODE_THROUGHPUT_RECV:   return "throughput-recv";
        default:                        AssertFailed(); return "internal-error";
    }
}

/**
 * String -> NETPERFMODE.
 *
 * @returns The corresponding NETPERFMODE, NETPERFMODE_INVALID on failure.
 * @param   pszMode             The mode string.
 */
static NETPERFMODE netperfModeFromString(const char *pszMode)
{
    if (!strcmp(pszMode, "latency"))
        return NETPERFMODE_LATENCY;
    if (   !strcmp(pszMode, "throughput")
        || !strcmp(pszMode, "thruput") )
        return NETPERFMODE_THROUGHPUT;
    if (   !strcmp(pszMode, "throughput-xmit")
        || !strcmp(pszMode, "thruput-xmit")
        || !strcmp(pszMode, "xmit") )
        return NETPERFMODE_THROUGHPUT_XMIT;
    if (   !strcmp(pszMode, "throughput-recv")
        || !strcmp(pszMode, "thruput-recv")
        || !strcmp(pszMode, "recv") )
        return NETPERFMODE_THROUGHPUT_RECV;
    return NETPERFMODE_INVALID;
}





/**
 * TCP Server: Throughput test.
 *
 * @returns IPRT status code.
 * @param   pParams             The parameters to use for this test.
 */
static int netperfTCPServerDoThroughput(NETPERFPARAMS const *pParams)
{
    /*
     * Allocate the buffer.
     */
    NETPERFHDR *pBuf = (NETPERFHDR *)RTMemAllocZ(pParams->cbPacket);
    if (!pBuf)
        return RTTestIFailedRc(VERR_NO_MEMORY, "Out of memory");

    /*
     * Receive first, then Send.  The reverse of the client.
     */
    NETPERFSTATS RecvStats;
    int rc = netperfTCPThroughputRecv(pParams, pBuf, &RecvStats);
    if (RT_SUCCESS(rc))
    {
        rc = netperfSendStats(&RecvStats, pParams->hSocket);
        if (RT_SUCCESS(rc))
        {
            NETPERFSTATS SendStats;
            rc = netperfTCPThroughputSend(pParams, pBuf, &SendStats);
            if (RT_SUCCESS(rc))
            {
                rc = netperfSendStats(&SendStats, pParams->hSocket);
                netperfPrintThroughputStats(&SendStats, &RecvStats, pParams->cbPacket);
            }
        }
    }

    return rc;
}

/**
 * TCP Server: Throughput xmit test (receive from client).
 *
 * @returns IPRT status code.
 * @param   pParams             The parameters to use for this test.
 */
static int netperfTCPServerDoThroughputXmit(NETPERFPARAMS const *pParams)
{
    /*
     * Allocate the buffer.
     */
    NETPERFHDR *pBuf = (NETPERFHDR *)RTMemAllocZ(pParams->cbPacket);
    if (!pBuf)
        return RTTestIFailedRc(VERR_NO_MEMORY, "Out of memory");

    /*
     * Receive the transmitted data (reverse of client).
     */
    NETPERFSTATS RecvStats;
    int rc = netperfTCPThroughputRecv(pParams, pBuf, &RecvStats);
    if (RT_SUCCESS(rc))
    {
        rc = netperfSendStats(&RecvStats, pParams->hSocket);
        if (RT_SUCCESS(rc))
            netperfPrintThroughputStats(NULL, &RecvStats, pParams->cbPacket);
    }

    return rc;
}

/**
 * TCP Server: Throughput recv test (transmit to client).
 *
 * @returns IPRT status code.
 * @param   pParams             The parameters to use for this test.
 */
static int netperfTCPServerDoThroughputRecv(NETPERFPARAMS const *pParams)
{
    /*
     * Allocate the buffer.
     */
    NETPERFHDR *pBuf = (NETPERFHDR *)RTMemAllocZ(pParams->cbPacket);
    if (!pBuf)
        return RTTestIFailedRc(VERR_NO_MEMORY, "Out of memory");

    /*
     * Send data to the client (reverse of client).
     */
    NETPERFSTATS SendStats;
    int rc = netperfTCPThroughputSend(pParams, pBuf, &SendStats);
    if (RT_SUCCESS(rc))
    {
        rc = netperfSendStats(&SendStats, pParams->hSocket);
        if (RT_SUCCESS(rc))
            netperfPrintThroughputStats(&SendStats, NULL, pParams->cbPacket);
    }

    return rc;
}

/**
 * TCP Server: Latency test.
 *
 * @returns IPRT status code.
 * @param   pParams             The parameters to use for this test.
 */
static int netperfTCPServerDoLatency(NETPERFPARAMS const *pParams)
{
    NETPERFHDR *pBuf = (NETPERFHDR *)RTMemAllocZ(pParams->cbPacket);
    if (!pBuf)
        return RTTestIFailedRc(VERR_NO_MEMORY, "Failed to allocated packet buffer of %u bytes.\n", pParams->cbPacket);

    /*
     * Ping pong with client.
     */
    int             rc;
    uint32_t        uState    = RT_H2LE_U32_C(NETPERFHDR_WARMUP);
    uint32_t        u32Seq    = 0;
    uint64_t        cTx       = 0;
    uint64_t        cRx       = 0;
    uint64_t        u64StartTS  = 0;
    NETPERFSTATS    Stats;
    RT_ZERO(Stats);
    for (;;)
    {
        rc = RTTcpRead(pParams->hSocket, pBuf, pParams->cbPacket, NULL);
        if (RT_FAILURE(rc))
        {
            RTTestIFailed("Failed to read data from client: %Rrc\n", rc);
            break;
        }

        /*
         * Validate the packet
         */
        if (RT_UNLIKELY(   pBuf->u32Magic    != RT_H2LE_U32_C(NETPERFHDR_MAGIC)
                        || pBuf->u32Reserved != 0))
        {
            RTTestIFailed("Invalid magic or reserved field value: %#x %#x\n", RT_H2LE_U32(pBuf->u32Magic), RT_H2LE_U32(pBuf->u32Reserved));
            rc = VERR_INVALID_MAGIC;
            break;
        }

        u32Seq += 1;
        if (RT_UNLIKELY(pBuf->u32Seq != RT_H2LE_U32(u32Seq)))
        {
            RTTestIFailed("Out of sequence: got %#x, expected %#x\n", RT_H2LE_U32(pBuf->u32Seq), u32Seq);
            rc = VERR_WRONG_ORDER;
            break;
        }

        /*
         * Count the packet if the state remains unchanged.
         */
        if (RT_LIKELY(pBuf->u32State == uState))
            cRx++;
        /*
         * Validate and act on the state transition.
         */
        else if (   uState         == RT_H2LE_U32_C(NETPERFHDR_WARMUP)
                 && pBuf->u32State == RT_H2LE_U32_C(NETPERFHDR_TESTING))
        {
            cRx = cTx = 0;
            u64StartTS = RTTimeNanoTS();
            uState = pBuf->u32State;
        }
        else if (   uState             == RT_H2LE_U32_C(NETPERFHDR_TESTING)
                 && (   pBuf->u32State == RT_H2LE_U32_C(NETPERFHDR_COOL_DOWN)
                     || pBuf->u32State == RT_H2LE_U32_C(NETPERFHDR_DONE)) )
        {
            Stats.cNsElapsed = RTTimeNanoTS() - u64StartTS;
            Stats.cEchos     = cTx;
            Stats.cTx        = cTx;
            Stats.cRx        = cRx;
            uState = pBuf->u32State;
            if (uState == RT_H2LE_U32_C(NETPERFHDR_DONE))
                break;
        }
        else if (   uState         == RT_H2LE_U32_C(NETPERFHDR_COOL_DOWN)
                 && pBuf->u32State == RT_H2LE_U32_C(NETPERFHDR_DONE))
        {
            uState = pBuf->u32State;
            break;
        }
        else
        {
            RTTestIFailed("Protocol error: invalid state transition %#x -> %#x\n",
                          RT_LE2H_U32(uState), RT_LE2H_U32(pBuf->u32State));
            break;
        }

        /*
         * Write same data back to client.
         */
        rc = RTTcpWrite(pParams->hSocket, pBuf, pParams->cbPacket);
        if (RT_FAILURE(rc))
        {
            RTTestIFailed("Failed to write data to client: %Rrc\n", rc);
            break;
        }

        cTx++;
    }

    /*
     * Send stats to client and print them.
     */
    if (uState == RT_H2LE_U32_C(NETPERFHDR_DONE))
        netperfSendStats(&Stats, pParams->hSocket);

    if (   uState == RT_H2LE_U32_C(NETPERFHDR_DONE)
        || uState == RT_H2LE_U32_C(NETPERFHDR_COOL_DOWN))
        netperfPrintLatencyStats(&Stats, pParams->cbPacket);

    RTMemFree(pBuf);
    return rc;
}

/**
 * Parses the parameters the client has sent us.
 *
 * @returns IPRT status code. Message has been shown on failure.
 * @param   pParams             The parameter structure to store the parameters
 *                              in.
 * @param   pszParams           The parameter string sent by the client.
 */
static int netperfTCPServerParseParams(NETPERFPARAMS *pParams, char *pszParams)
{
    /*
     * Set defaults for the dynamic settings.
     */
    pParams->fNoDelay    = false;
    pParams->enmMode     = NETPERFMODE_LATENCY;
    pParams->cSecTimeout = NETPERF_DEFAULT_WARMUP;
    pParams->cMsCoolDown = NETPERF_DEFAULT_COOL_DOWN;
    pParams->cMsWarmup   = NETPERF_DEFAULT_WARMUP;
    pParams->cbPacket    = NETPERF_DEFAULT_PKT_SIZE_LATENCY;

    /*
     * Parse the client parameters.
     */
    /* first arg: transport type. [mandatory] */
    char *pszCur = strchr(pszParams, ':');
    if (!pszCur)
        return RTTestIFailedRc(VERR_PARSE_ERROR, "client params: No colon\n");
    char *pszNext = strchr(++pszCur, ':');
    if (pszNext)
        *pszNext++ = '\0';
    if (strcmp(pszCur, "TCP"))
        return RTTestIFailedRc(VERR_PARSE_ERROR, "client params: Invalid transport type: \"%s\"\n", pszCur);
    pszCur = pszNext;

    /* second arg: mode. [mandatory] */
    if (!pszCur)
        return RTTestIFailedRc(VERR_PARSE_ERROR, "client params: Missing test mode\n");
    pszNext = strchr(pszCur, ':');
    if (pszNext)
        *pszNext++ = '\0';
    pParams->enmMode = netperfModeFromString(pszCur);
    if (pParams->enmMode == NETPERFMODE_INVALID)
        return RTTestIFailedRc(VERR_PARSE_ERROR, "client params: Invalid test mode: \"%s\"\n", pszCur);
    pszCur = pszNext;

    /*
     * The remainder are uint32_t or bool.
     */
    struct
    {
        bool        fBool;
        bool        fMandatory;
        void       *pvValue;
        uint32_t    uMin;
        uint32_t    uMax;
        const char *pszName;
    } aElements[] =
    {
        { false, true, &pParams->cSecTimeout, NETPERF_MIN_TIMEOUT,  NETPERF_MAX_TIMEOUT,  "timeout" },
        { false, true, &pParams->cbPacket,    NETPERF_MIN_PKT_SIZE, NETPERF_MAX_PKT_SIZE, "packet size" },
        { false, true, &pParams->cMsWarmup,   NETPERF_MIN_WARMUP, NETPERF_MAX_WARMUP, "warmup period" },
        { false, true, &pParams->cMsCoolDown, NETPERF_MIN_COOL_DOWN, NETPERF_MAX_COOL_DOWN, "cool down period" },
        { true,  true, &pParams->fNoDelay,    false, true, "no delay" },
    };

    for (unsigned i = 0; i < RT_ELEMENTS(aElements); i++)
    {
        if (!pszCur)
            return aElements[i].fMandatory
                 ? RTTestIFailedRc(VERR_PARSE_ERROR, "client params: missing %s\n", aElements[i].pszName)
                 : VINF_SUCCESS;

        pszNext = strchr(pszCur, ':');
        if (pszNext)
            *pszNext++ = '\0';
        uint32_t u32;
        int rc = RTStrToUInt32Full(pszCur, 10, &u32);
        if (rc != VINF_SUCCESS)
            return RTTestIFailedRc(VERR_PARSE_ERROR, "client params: bad %s value \"%s\": %Rrc\n",
                                   aElements[i].pszName, pszCur, rc);

        if (   u32 < aElements[i].uMin
            || u32 > aElements[i].uMax)
            return RTTestIFailedRc(VERR_PARSE_ERROR, "client params: %s %u s is out of range (%u..%u)\n",
                                   aElements[i].pszName, u32, aElements[i].uMin, aElements[i].uMax);
        if (aElements[i].fBool)
            *(bool *)aElements[i].pvValue = u32 ? true : false;
        else
            *(uint32_t *)aElements[i].pvValue = u32;

        pszCur = pszNext;
    }

    /* Fail if too many elements. */
    if (pszCur)
        return RTTestIFailedRc(VERR_PARSE_ERROR, "client params: too many elements: \"%s\"\n",
                               pszCur);
    return VINF_SUCCESS;
}


/**
 * TCP server callback that handles one client connection.
 *
 * @returns IPRT status code. VERR_TCP_SERVER_STOP is special.
 * @param   hSocket             The client socket.
 * @param   pvUser              Our parameters.
 */
static DECLCALLBACK(int) netperfTCPServerWorker(RTSOCKET hSocket, void *pvUser)
{
    NETPERFPARAMS *pParams = (NETPERFPARAMS *)pvUser;
    AssertReturn(pParams, VERR_INVALID_POINTER);

    pParams->hSocket = hSocket;

    RTNETADDR Addr;
    int rc = RTTcpGetPeerAddress(hSocket, &Addr);
    if (RT_SUCCESS(rc))
        RTTestIPrintf(RTTESTLVL_ALWAYS, "Client connected from %RTnaddr\n", &Addr);
    else
    {
        RTTestIPrintf(RTTESTLVL_ALWAYS, "Failed to get client details: %Rrc\n", rc);
        Addr.enmType = RTNETADDRTYPE_INVALID;
    }

    /*
     * Adjust send and receive buffer sizes if necessary.
     */
    if (pParams->cbBufferSize)
    {
        rc = RTTcpSetBufferSize(hSocket, pParams->cbBufferSize);
        if (RT_FAILURE(rc))
            return RTTestIFailedRc(rc, "Failed to set socket buffer sizes to %#x: %Rrc\n", pParams->cbBufferSize, rc);
    }

    /*
     * Greet the other dude.
     */
    rc = RTTcpWrite(hSocket, g_ConnectStart, sizeof(g_ConnectStart) - 1);
    if (RT_FAILURE(rc))
        return RTTestIFailedRc(rc, "Failed to send connection start Id: %Rrc\n", rc);

    /*
     * Read connection parameters.
     */
    char szBuf[256];
    rc = RTTcpRead(hSocket, szBuf, NETPERF_LEN_PREFIX, NULL);
    if (RT_FAILURE(rc))
        return RTTestIFailedRc(rc, "Failed to read connection parameters: %Rrc\n", rc);
    szBuf[NETPERF_LEN_PREFIX] = '\0';
    uint32_t cchParams;
    rc = RTStrToUInt32Full(szBuf, 10, &cchParams);
    if (rc != VINF_SUCCESS)
        return RTTestIFailedRc(RT_SUCCESS(rc) ? VERR_INTERNAL_ERROR : rc,
                               "Failed to read connection parameters: %Rrc\n", rc);
    if (cchParams >= sizeof(szBuf))
        return RTTestIFailedRc(VERR_TOO_MUCH_DATA, "parameter packet is too big (%u bytes)\n", cchParams);
    rc = RTTcpRead(hSocket, szBuf, cchParams, NULL);
    if (RT_FAILURE(rc))
        return RTTestIFailedRc(rc, "Failed to read connection parameters: %Rrc\n", rc);
    szBuf[cchParams] = '\0';

    if (strncmp(szBuf, g_szStartParams, sizeof(g_szStartParams) - 1))
        return RTTestIFailedRc(VERR_NET_PROTOCOL_ERROR, "Invalid connection parameters '%s'\n", szBuf);

    /*
     * Parse the parameters and signal whether we've got a deal or not.
     */
    rc = netperfTCPServerParseParams(pParams, szBuf);
    if (RT_FAILURE(rc))
    {
        int rc2 = RTTcpWrite(hSocket, g_szNegative, sizeof(g_szNegative) - 1);
        if (RT_FAILURE(rc2))
            RTTestIFailed("Failed to send negative ack: %Rrc\n", rc2);
        return rc;
    }

    if (Addr.enmType != RTNETADDRTYPE_INVALID)
        RTTestISubF("%RTnaddr - %s, %u s, %u bytes", &Addr,
                    netperfModeToString(pParams->enmMode), pParams->cSecTimeout, pParams->cbPacket);
    else
        RTTestISubF("Unknown - %s, %u s, %u bytes",
                    netperfModeToString(pParams->enmMode), pParams->cSecTimeout, pParams->cbPacket);

    rc = RTTcpSetSendCoalescing(hSocket, !pParams->fNoDelay);
    if (RT_FAILURE(rc))
        return RTTestIFailedRc(rc, "Failed to apply no-delay option (%RTbool): %Rrc\n", pParams->fNoDelay, rc);

    rc = RTTcpWrite(hSocket, g_szAck, sizeof(g_szAck) - 1);
    if (RT_FAILURE(rc))
        return RTTestIFailedRc(rc, "Failed to send start test commend to client: %Rrc\n", rc);

    /*
     * Take action according to our mode.
     */
    switch (pParams->enmMode)
    {
        case NETPERFMODE_LATENCY:
            rc = netperfTCPServerDoLatency(pParams);
            break;

        case NETPERFMODE_THROUGHPUT:
            rc = netperfTCPServerDoThroughput(pParams);
            break;

        case NETPERFMODE_THROUGHPUT_XMIT:
            rc = netperfTCPServerDoThroughputXmit(pParams);
            break;

        case NETPERFMODE_THROUGHPUT_RECV:
            rc = netperfTCPServerDoThroughputRecv(pParams);
            break;

        case NETPERFMODE_INVALID:
            rc = VERR_INTERNAL_ERROR;
            break;

        /* no default! */
    }
    if (rc == VERR_NO_MEMORY)
        return VERR_TCP_SERVER_STOP;

    /*
     * Wait for other clients or quit.
     */
    if (pParams->fSingleClient)
        return VERR_TCP_SERVER_STOP;
    return VINF_SUCCESS;
}


/**
 * TCP server.
 *
 * @returns IPRT status code.
 * @param   pParams             The TCP parameter block.
 */
static int netperfTCPServer(NETPERFPARAMS *pParams)
{
    /*
     * Spawn the TCP server thread & listen.
     */
    PRTTCPSERVER pServer;
    int rc = RTTcpServerCreateEx(NULL, pParams->uPort, &pServer);
    if (RT_SUCCESS(rc))
    {
        RTPrintf("Server listening on TCP port %d\n", pParams->uPort);
        rc = RTTcpServerListen(pServer, netperfTCPServerWorker, pParams);
        RTTcpServerDestroy(pServer);
    }
    else
        RTPrintf("Failed to create TCP server thread: %Rrc\n", rc);

    return rc;
}

/**
 * The server part.
 *
 * @returns Exit code.
 * @param   enmProto            The protocol.
 * @param   pParams             The parameter block.
 */
static RTEXITCODE netperfServer(NETPERFPROTO enmProto, NETPERFPARAMS *pParams)
{

    switch (enmProto)
    {
        case NETPERFPROTO_TCP:
        {
            int rc = netperfTCPServer(pParams);
            return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
        }

        default:
            RTTestIFailed("Protocol not supported.\n");
            return RTEXITCODE_FAILURE;
    }
}





/**
 * TCP client: Do the throughput test.
 *
 * @returns IPRT status code
 * @param   pParams             The parameters.
 */
static int netperfTCPClientDoThroughput(NETPERFPARAMS *pParams)
{
    /*
     * Allocate the buffer.
     */
    NETPERFHDR *pBuf = (NETPERFHDR *)RTMemAllocZ(pParams->cbPacket);
    if (!pBuf)
        return RTTestIFailedRc(VERR_NO_MEMORY, "Out of memory");

    /*
     * Send first, then Receive.
     */
    NETPERFSTATS SendStats;
    int rc = netperfTCPThroughputSend(pParams, pBuf, &SendStats);
    if (RT_SUCCESS(rc))
    {
        NETPERFSTATS SrvSendStats;
        rc = netperfRecvStats(&SrvSendStats, pParams->hSocket);
        if (RT_SUCCESS(rc))
        {
            NETPERFSTATS RecvStats;
            rc = netperfTCPThroughputRecv(pParams, pBuf, &RecvStats);
            if (RT_SUCCESS(rc))
            {
                NETPERFSTATS SrvRecvStats;
                rc = netperfRecvStats(&SrvRecvStats, pParams->hSocket);
                if (RT_SUCCESS(rc))
                {
                    if (pParams->fServerStats)
                        netperfPrintThroughputStats(&SrvSendStats, &SrvRecvStats, pParams->cbPacket);
                    else
                        netperfPrintThroughputStats(&SendStats, &RecvStats, pParams->cbPacket);
                }
            }
        }
    }

    RTTestISubDone();
    return rc;
}

/**
 * TCP client: Do the throughput xmit test.
 *
 * @returns IPRT status code
 * @param   pParams             The parameters.
 */
static int netperfTCPClientDoThroughputXmit(NETPERFPARAMS *pParams)
{
    /*
     * Allocate the buffer.
     */
    NETPERFHDR *pBuf = (NETPERFHDR *)RTMemAllocZ(pParams->cbPacket);
    if (!pBuf)
        return RTTestIFailedRc(VERR_NO_MEMORY, "Out of memory");

    /*
     * Do the job.
     */
    NETPERFSTATS SendStats;
    int rc = netperfTCPThroughputSend(pParams, pBuf, &SendStats);
    if (RT_SUCCESS(rc))
    {
        NETPERFSTATS SrvSendStats;
        rc = netperfRecvStats(&SrvSendStats, pParams->hSocket);
        if (RT_SUCCESS(rc))
        {
            if (pParams->fServerStats)
                netperfPrintThroughputStats(&SrvSendStats, NULL, pParams->cbPacket);
            else
                netperfPrintThroughputStats(&SendStats,    NULL, pParams->cbPacket);
        }
    }

    RTTestISubDone();
    return rc;
}

/**
 * TCP client: Do the throughput recv test.
 *
 * @returns IPRT status code
 * @param   pParams             The parameters.
 */
static int netperfTCPClientDoThroughputRecv(NETPERFPARAMS *pParams)
{
    /*
     * Allocate the buffer.
     */
    NETPERFHDR *pBuf = (NETPERFHDR *)RTMemAllocZ(pParams->cbPacket);
    if (!pBuf)
        return RTTestIFailedRc(VERR_NO_MEMORY, "Out of memory");

    /*
     * Do the job.
     */
    NETPERFSTATS RecvStats;
    int rc = netperfTCPThroughputRecv(pParams, pBuf, &RecvStats);
    if (RT_SUCCESS(rc))
    {
        NETPERFSTATS SrvRecvStats;
        rc = netperfRecvStats(&SrvRecvStats, pParams->hSocket);
        if (RT_SUCCESS(rc))
        {
            if (pParams->fServerStats)
                netperfPrintThroughputStats(NULL, &SrvRecvStats, pParams->cbPacket);
            else
                netperfPrintThroughputStats(NULL, &RecvStats,    pParams->cbPacket);
        }
    }

    RTTestISubDone();
    return rc;
}

/**
 * TCP client: Do the latency test.
 *
 * @returns IPRT status code
 * @param   pParams             The parameters.
 */
static int netperfTCPClientDoLatency(NETPERFPARAMS *pParams)
{
    /*
     * Generate a selection of packages before we start, after all we're not
     * benchmarking the random number generator, are we. :-)
     */
    void *pvReadBuf = RTMemAllocZ(pParams->cbPacket);
    if (!pvReadBuf)
        return RTTestIFailedRc(VERR_NO_MEMORY, "Out of memory");

    size_t i;
    NETPERFHDR *apPackets[256];
    for (i = 0; i < RT_ELEMENTS(apPackets); i++)
    {
        apPackets[i] = (NETPERFHDR *)RTMemAllocZ(pParams->cbPacket);
        if (!apPackets[i])
        {
            while (i-- > 0)
                RTMemFree(apPackets[i]);
            RTMemFree(pvReadBuf);
            return RTTestIFailedRc(VERR_NO_MEMORY, "Out of memory");
        }
        RTRandBytes(apPackets[i], pParams->cbPacket);
        apPackets[i]->u32Magic      = RT_H2LE_U32_C(NETPERFHDR_MAGIC);
        apPackets[i]->u32State      = 0;
        apPackets[i]->u32Seq        = 0;
        apPackets[i]->u32Reserved   = 0;
    }

    /*
     * Create & start a timer to eventually disconnect.
     */
    bool volatile fStop = false;
    RTTIMERLR hTimer;
    int rc = RTTimerLRCreateEx(&hTimer, 0 /* nsec */, RTTIMER_FLAGS_CPU_ANY, netperfStopTimerCallback, (void *)&fStop);
    if (RT_SUCCESS(rc))
    {
        uint32_t        u32Seq = 0;
        NETPERFSTATS    Stats;
        RT_ZERO(Stats);

        /*
         * Warm up.
         */
        if (g_uVerbosity > 0)
            RTPrintf("Warmup...\n");
        rc = RTTimerLRStart(hTimer, pParams->cMsWarmup * UINT64_C(1000000) /* nsec */);
        if (RT_SUCCESS(rc))
        {
            while (!fStop)
            {
                NETPERFHDR *pPacket = apPackets[u32Seq % RT_ELEMENTS(apPackets)];
                u32Seq++;
                pPacket->u32Seq   = RT_H2LE_U32(u32Seq);
                pPacket->u32State = RT_H2LE_U32_C(NETPERFHDR_WARMUP);
                rc = RTTcpWrite(pParams->hSocket, pPacket, pParams->cbPacket);
                if (RT_FAILURE(rc))
                {
                    RTTestIFailed("RTTcpWrite/warmup: %Rrc\n", rc);
                    break;
                }
                rc = RTTcpRead(pParams->hSocket, pvReadBuf, pParams->cbPacket, NULL);
                if (RT_FAILURE(rc))
                {
                    RTTestIFailed("RTTcpRead/warmup: %Rrc\n", rc);
                    break;
                }
            }
        }
        else
            RTTestIFailed("RTTimerLRStart/warmup: %Rrc\n", rc);

        /*
         * The real thing.
         */
        if (RT_SUCCESS(rc))
        {
            if (g_uVerbosity > 0)
                RTPrintf("The real thing...\n");
            fStop = false;
            rc = RTTimerLRStart(hTimer, pParams->cSecTimeout * UINT64_C(1000000000) /* nsec */);
            if (RT_SUCCESS(rc))
            {
                uint64_t u64StartTS = RTTimeNanoTS();
                while (!fStop)
                {
                    NETPERFHDR *pPacket = apPackets[u32Seq % RT_ELEMENTS(apPackets)];
                    u32Seq++;
                    pPacket->u32Seq   = RT_H2LE_U32(u32Seq);
                    pPacket->u32State = RT_H2LE_U32_C(NETPERFHDR_TESTING);
                    rc = RTTcpWrite(pParams->hSocket, pPacket, pParams->cbPacket);
                    if (RT_FAILURE(rc))
                    {
                        RTTestIFailed("RTTcpWrite/testing: %Rrc\n", rc);
                        break;
                    }
                    Stats.cTx++;

                    rc = RTTcpRead(pParams->hSocket, pvReadBuf, pParams->cbPacket, NULL);
                    if (RT_FAILURE(rc))
                    {
                        RTTestIFailed("RTTcpRead/testing: %Rrc\n", rc);
                        break;
                    }
                    Stats.cRx++;

                    if (!memcmp(pvReadBuf, pPacket, pParams->cbPacket))
                        Stats.cEchos++;
                    else
                        Stats.cErrors++;
                }
                Stats.cNsElapsed = RTTimeNanoTS() - u64StartTS;
            }
            else
                RTTestIFailed("RTTimerLRStart/testing: %Rrc\n", rc);
        }

        /*
         * Cool down.
         */
        if (RT_SUCCESS(rc))
        {
            if (g_uVerbosity > 0)
                RTPrintf("Cool down...\n");
            fStop = false;
            rc = RTTimerLRStart(hTimer, pParams->cMsCoolDown * UINT64_C(1000000) /* nsec */);
            if (RT_SUCCESS(rc))
            {
                while (!fStop)
                {
                    NETPERFHDR *pPacket = apPackets[u32Seq % RT_ELEMENTS(apPackets)];
                    u32Seq++;
                    pPacket->u32Seq   = RT_H2LE_U32(u32Seq);
                    pPacket->u32State = RT_H2LE_U32_C(NETPERFHDR_COOL_DOWN);
                    rc = RTTcpWrite(pParams->hSocket, pPacket, pParams->cbPacket);
                    if (RT_FAILURE(rc))
                    {
                        RTTestIFailed("RTTcpWrite/warmup: %Rrc\n", rc);
                        break;
                    }
                    rc = RTTcpRead(pParams->hSocket, pvReadBuf, pParams->cbPacket, NULL);
                    if (RT_FAILURE(rc))
                    {
                        RTTestIFailed("RTTcpRead/warmup: %Rrc\n", rc);
                        break;
                    }
                }
            }
            else
                RTTestIFailed("RTTimerLRStart/testing: %Rrc\n", rc);
        }

        /*
         * Send DONE packet.
         */
        if (g_uVerbosity > 0)
            RTPrintf("Done\n");
        if (RT_SUCCESS(rc))
        {
            u32Seq++;
            NETPERFHDR *pPacket = apPackets[u32Seq % RT_ELEMENTS(apPackets)];
            pPacket->u32Seq   = RT_H2LE_U32(u32Seq);
            pPacket->u32State = RT_H2LE_U32_C(NETPERFHDR_DONE);
            rc = RTTcpWrite(pParams->hSocket, pPacket, pParams->cbPacket);
            if (RT_FAILURE(rc))
                RTTestIFailed("RTTcpWrite/done: %Rrc\n", rc);
        }


        /*
         * Get and print stats.
         */
        NETPERFSTATS SrvStats;
        if (RT_SUCCESS(rc))
        {
            rc = netperfRecvStats(&SrvStats, pParams->hSocket);
            if (RT_SUCCESS(rc) && pParams->fServerStats)
                netperfPrintLatencyStats(&SrvStats, pParams->cbPacket);
            else if (!pParams->fServerStats)
                netperfPrintLatencyStats(&Stats, pParams->cbPacket);
        }

        /* clean up*/
        RTTimerLRDestroy(hTimer);
    }
    else
        RTTestIFailed("Failed to create timer object: %Rrc\n", rc);
    for (i = 0; i < RT_ELEMENTS(apPackets); i++)
        RTMemFree(apPackets[i]);

    RTMemFree(pvReadBuf);

    return rc;
}

/**
 * TCP client test driver.
 *
 * @returns IPRT status code
 * @param   pszServer           The server name.
 * @param   pParams             The parameter structure.
 */
static int netperfTCPClient(const char *pszServer, NETPERFPARAMS *pParams)
{
    AssertReturn(pParams, VERR_INVALID_POINTER);
    RTTestISubF("TCP - %u s, %u bytes%s", pParams->cSecTimeout,
                pParams->cbPacket, pParams->fNoDelay ? ", no delay" : "");

    RTSOCKET hSocket = NIL_RTSOCKET;
    int rc = RTTcpClientConnect(pszServer, pParams->uPort, &hSocket);
    if (RT_FAILURE(rc))
        return RTTestIFailedRc(rc, "Failed to connect to %s on port %u: %Rrc\n", pszServer, pParams->uPort, rc);
    pParams->hSocket = hSocket;

    /*
     * Disable send coalescing (no-delay).
     */
    if (pParams->fNoDelay)
    {
        rc = RTTcpSetSendCoalescing(hSocket, false /*fEnable*/);
        if (RT_FAILURE(rc))
            return RTTestIFailedRc(rc, "Failed to set no-delay option: %Rrc\n", rc);
    }

    /*
     * Adjust send and receive buffer sizes if necessary.
     */
    if (pParams->cbBufferSize)
    {
        rc = RTTcpSetBufferSize(hSocket, pParams->cbBufferSize);
        if (RT_FAILURE(rc))
            return RTTestIFailedRc(rc, "Failed to set socket buffer sizes to %#x: %Rrc\n", pParams->cbBufferSize, rc);
    }

    /*
     * Verify the super secret Start Connect Id to start the connection.
     */
    char szBuf[256 + NETPERF_LEN_PREFIX];
    RT_ZERO(szBuf);
    rc = RTTcpRead(hSocket, szBuf, sizeof(g_ConnectStart) - 1, NULL);
    if (RT_FAILURE(rc))
        return RTTestIFailedRc(rc, "Failed to read connection initializer: %Rrc\n", rc);

    if (strcmp(szBuf, g_ConnectStart))
        return RTTestIFailedRc(VERR_INVALID_MAGIC, "Invalid connection initializer '%s'\n", szBuf);

    /*
     * Send all the dynamic parameters to the server.
     * (If the server is newer than the client, it will select default for any
     * missing parameters.)
     */
    size_t cchParams = RTStrPrintf(&szBuf[NETPERF_LEN_PREFIX], sizeof(szBuf) - NETPERF_LEN_PREFIX,
                                   "%s:%s:%s:%u:%u:%u:%u:%u",
                                   g_szStartParams,
                                   "TCP",
                                   netperfModeToString(pParams->enmMode),
                                   pParams->cSecTimeout,
                                   pParams->cbPacket,
                                   pParams->cMsWarmup,
                                   pParams->cMsCoolDown,
                                   pParams->fNoDelay);
    RTStrPrintf(szBuf, NETPERF_LEN_PREFIX + 1, "%0*u", NETPERF_LEN_PREFIX, cchParams);
    szBuf[NETPERF_LEN_PREFIX] = g_szStartParams[0];
    Assert(strlen(szBuf) == NETPERF_LEN_PREFIX + cchParams);
    rc = RTTcpWrite(hSocket, szBuf, NETPERF_LEN_PREFIX + cchParams);
    if (RT_FAILURE(rc))
        return RTTestIFailedRc(rc, "Failed to send connection parameters: %Rrc\n", rc);

    /*
     * Wait for acknowledgment.
     */
    rc = RTTcpRead(hSocket, szBuf, sizeof(g_szAck) - 1, NULL);
    if (RT_FAILURE(rc))
        return RTTestIFailedRc(rc, "Failed to send parameters: %Rrc\n", rc);
    szBuf[sizeof(g_szAck) - 1] = '\0';

    if (!strcmp(szBuf, g_szNegative))
        return RTTestIFailedRc(VERR_NET_PROTOCOL_ERROR, "Server failed to accept packet size of %u bytes.\n", pParams->cbPacket);
    if (strcmp(szBuf, g_szAck))
        return RTTestIFailedRc(VERR_NET_PROTOCOL_ERROR, "Invalid response from server '%s'\n", szBuf);

    /*
     * Take action according to our mode.
     */
    switch (pParams->enmMode)
    {
        case NETPERFMODE_LATENCY:
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Connected to %s port %u, running the latency test for %u seconds.\n",
                          pszServer, pParams->uPort, pParams->cSecTimeout);
            rc = netperfTCPClientDoLatency(pParams);
            break;

        case NETPERFMODE_THROUGHPUT:
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Connected to %s port %u, running the throughput test for %u seconds in each direction.\n",
                          pszServer, pParams->uPort, pParams->cSecTimeout);
            rc = netperfTCPClientDoThroughput(pParams);
            break;

        case NETPERFMODE_THROUGHPUT_XMIT:
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Connected to %s port %u, running the throughput-xmit test for %u seconds.\n",
                          pszServer, pParams->uPort, pParams->cSecTimeout);
            rc = netperfTCPClientDoThroughputXmit(pParams);
            break;

        case NETPERFMODE_THROUGHPUT_RECV:
            RTTestIPrintf(RTTESTLVL_ALWAYS, "Connected to %s port %u, running the throughput-recv test for %u seconds.\n",
                          pszServer, pParams->uPort, pParams->cSecTimeout);
            rc = netperfTCPClientDoThroughputRecv(pParams);
            break;

        case NETPERFMODE_INVALID:
            rc = VERR_INTERNAL_ERROR;
            break;

        /* no default! */
    }
    return rc;
}

/**
 * The client part.
 *
 * @returns Exit code.
 * @param   enmProto            The protocol.
 * @param   pszServer           The server name.
 * @param   pvUser              The parameter block as opaque user data.
 */
static RTEXITCODE netperfClient(NETPERFPROTO enmProto, const char *pszServer, void *pvUser)
{
    switch (enmProto)
    {
        case NETPERFPROTO_TCP:
        {
            NETPERFPARAMS *pParams = (NETPERFPARAMS *)pvUser;
            int rc = netperfTCPClient(pszServer, pParams);
            if (pParams->hSocket != NIL_RTSOCKET)
            {
                RTTcpClientClose(pParams->hSocket);
                pParams->hSocket = NIL_RTSOCKET;
            }
            return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
        }

        default:
            RTTestIFailed("Protocol not supported.\n");
            return RTEXITCODE_FAILURE;
    }
}


int main(int argc, char *argv[])
{
    /*
     * Init IPRT and globals.
     */
    int rc = RTTestInitAndCreate("NetPerf", &g_hTest);
    if (rc)
        return rc;

    /*
     * Special case.
     */
    if (argc < 2)
    {
        RTTestFailed(g_hTest, "No arguments given.");
        return RTTestSummaryAndDestroy(g_hTest);
    }

    /*
     * Default values.
     */
    NETPERFPROTO        enmProtocol     = NETPERFPROTO_TCP;
    bool                fServer         = true;
    bool                fDaemonize      = false;
    bool                fDaemonized     = false;
    bool                fPacketSizeSet  = false;
    const char         *pszServerAddress= NULL;

    NETPERFPARAMS       Params;
    Params.uPort            = NETPERF_DEFAULT_PORT;
    Params.fServerStats     = false;
    Params.fSingleClient    = false;

    Params.fNoDelay         = false;
    Params.fCheckData       = false;
    Params.enmMode          = NETPERFMODE_LATENCY;
    Params.cSecTimeout      = NETPERF_DEFAULT_TIMEOUT;
    Params.cMsWarmup        = NETPERF_DEFAULT_WARMUP;
    Params.cMsCoolDown      = NETPERF_DEFAULT_COOL_DOWN;
    Params.cbPacket         = NETPERF_DEFAULT_PKT_SIZE_LATENCY;
    Params.cbBufferSize     = 0;

    Params.hSocket          = NIL_RTSOCKET;

    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, g_aCmdOptions, RT_ELEMENTS(g_aCmdOptions), 1, 0 /* fFlags */);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (rc)
        {
            case 's':
                fServer = true;
                break;

            case 'c':
                fServer = false;
                pszServerAddress = ValueUnion.psz;
                break;

            case 'd':
                fDaemonize = true;
                break;

            case 'D':
                fDaemonized = true;
                break;

            case 'i':
                Params.cSecTimeout = ValueUnion.u32;
                if (   Params.cSecTimeout < NETPERF_MIN_TIMEOUT
                    || Params.cSecTimeout > NETPERF_MAX_TIMEOUT)
                {
                    RTTestFailed(g_hTest, "Invalid interval %u s, valid range: %u-%u\n",
                                 Params.cbPacket, NETPERF_MIN_TIMEOUT, NETPERF_MAX_TIMEOUT);
                    return RTTestSummaryAndDestroy(g_hTest);
                }
                break;

            case 'l':
                Params.cbPacket = ValueUnion.u32;
                if (    Params.cbPacket < NETPERF_MIN_PKT_SIZE
                     || Params.cbPacket > NETPERF_MAX_PKT_SIZE)
                {
                    RTTestFailed(g_hTest, "Invalid packet size %u bytes, valid range: %u-%u\n",
                                 Params.cbPacket, NETPERF_MIN_PKT_SIZE, NETPERF_MAX_PKT_SIZE);
                    return RTTestSummaryAndDestroy(g_hTest);
                }
                fPacketSizeSet = true;
                break;

            case 'm':
                Params.enmMode = netperfModeFromString(ValueUnion.psz);
                if (Params.enmMode == NETPERFMODE_INVALID)
                {
                    RTTestFailed(g_hTest, "Invalid test mode: \"%s\"\n", ValueUnion.psz);
                    return RTTestSummaryAndDestroy(g_hTest);
                }
                if (!fPacketSizeSet)
                    switch (Params.enmMode)
                    {
                        case NETPERFMODE_LATENCY:
                            Params.cbPacket = NETPERF_DEFAULT_PKT_SIZE_LATENCY;
                            break;
                        case NETPERFMODE_THROUGHPUT:
                        case NETPERFMODE_THROUGHPUT_XMIT:
                        case NETPERFMODE_THROUGHPUT_RECV:
                            Params.cbPacket = NETPERF_DEFAULT_PKT_SIZE_THROUGHPUT;
                            break;
                        case NETPERFMODE_INVALID:
                            break;
                        /* no default! */
                    }
                break;

            case 'p':
                Params.uPort = ValueUnion.u32;
                break;

            case 'N':
                Params.fNoDelay = true;
                break;

            case 'S':
                Params.fServerStats = true;
                break;

            case '1':
                Params.fSingleClient = true;
                break;

            case 'v':
                g_uVerbosity++;
                break;

            case 'h':
                Usage(g_pStdOut);
                return RTEXITCODE_SUCCESS;

            case 'V':
                RTPrintf("$Revision: 106320 $\n");
                return RTEXITCODE_SUCCESS;

            case 'w':
                Params.cMsWarmup = ValueUnion.u32;
                if (   Params.cMsWarmup < NETPERF_MIN_WARMUP
                    || Params.cMsWarmup > NETPERF_MAX_WARMUP)
                {
                    RTTestFailed(g_hTest, "invalid warmup time %u ms, valid range: %u-%u\n",
                                 Params.cMsWarmup, NETPERF_MIN_WARMUP, NETPERF_MAX_WARMUP);
                    return RTTestSummaryAndDestroy(g_hTest);
                }
                break;

            case 'W':
                Params.cMsCoolDown = ValueUnion.u32;
                if (   Params.cMsCoolDown < NETPERF_MIN_COOL_DOWN
                    || Params.cMsCoolDown > NETPERF_MAX_COOL_DOWN)
                {
                    RTTestFailed(g_hTest, "invalid cool down time %u ms, valid range: %u-%u\n",
                                 Params.cMsCoolDown, NETPERF_MIN_COOL_DOWN, NETPERF_MAX_COOL_DOWN);
                    return RTTestSummaryAndDestroy(g_hTest);
                }
                break;

            case 'C':
                Params.fCheckData = true;
                break;

            case 'b':
                Params.cbBufferSize = ValueUnion.u32;
                if (   (    Params.cbBufferSize < NETPERF_MIN_BUF_SIZE
                         || Params.cbBufferSize > NETPERF_MAX_BUF_SIZE)
                    && Params.cbBufferSize  != 0)
                {
                    RTTestFailed(g_hTest, "Invalid packet size %u bytes, valid range: %u-%u or 0\n",
                                 Params.cbBufferSize, NETPERF_MIN_BUF_SIZE, NETPERF_MAX_BUF_SIZE);
                    return RTTestSummaryAndDestroy(g_hTest);
                }
                break;

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    /*
     * Handle the server process daemoniziation.
     */
    if (fDaemonize && !fDaemonized && fServer)
    {
        rc = RTProcDaemonize(argv, "--daemonized");
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTProcDaemonize failed: %Rrc\n", rc);
        return RTEXITCODE_SUCCESS;
    }

    /*
     * Get down to business.
     */
    RTTestBanner(g_hTest);
    if (fServer)
        rc = netperfServer(enmProtocol, &Params);
    else if (pszServerAddress)
        rc = netperfClient(enmProtocol, pszServerAddress, &Params);
    else
        RTTestFailed(g_hTest, "missing server address to connect to\n");

    RTEXITCODE rc2 = RTTestSummaryAndDestroy(g_hTest);
    return rc2 != RTEXITCODE_FAILURE ? (RTEXITCODE)rc2 : rc;
}

