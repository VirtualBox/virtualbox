/* $Id: tstGuestCtrlParseBuffer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Tests for VBoxService toolbox output streams.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MAIN
#include <VBox/err.h>
#include <VBox/log.h>

#include "../include/GuestCtrlImplPrivate.h"

using namespace com;

#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/test.h>
#include <iprt/rand.h>
#include <iprt/stream.h>

#ifndef BYTE
# define BYTE uint8_t
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Defines a test entry string size (in bytes). */
#define TST_STR_BYTES(a_sz)          (sizeof(a_sz) - 1)
/** Defines a test entry string, followed by its size (in bytes). */
#define TST_STR_AND_BYTES(a_sz)      a_sz, (sizeof(a_sz) - 1)
/** Defines the termination sequence for a single key/value pair. */
#define TST_STR_VAL_TRM              GUESTTOOLBOX_STRM_TERM_PAIR_STR
/** Defines the termination sequence for a single stream block. */
#define TST_STR_BLK_TRM              GUESTTOOLBOX_STRM_TERM_BLOCK_STR
/** Defines the termination sequence for the stream. */
#define TST_STR_STM_TRM              GUESTTOOLBOX_STRM_TERM_STREAM_STR


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct VBOXGUESTCTRL_BUFFER_VALUE
{
    char *pszValue;
} VBOXGUESTCTRL_BUFFER_VALUE, *PVBOXGUESTCTRL_BUFFER_VALUE;
typedef std::map< RTCString, VBOXGUESTCTRL_BUFFER_VALUE > GuestBufferMap;
typedef std::map< RTCString, VBOXGUESTCTRL_BUFFER_VALUE >::iterator GuestBufferMapIter;
typedef std::map< RTCString, VBOXGUESTCTRL_BUFFER_VALUE >::const_iterator GuestBufferMapIterConst;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
char szUnterm1[] = { 'a', 's', 'd', 'f' };
char szUnterm2[] = { 'f', 'o', 'o', '3', '=', 'b', 'a', 'r', '3' };

PRTLOGGER g_pLog = NULL;

/**
 * Tests single block parsing.
 */
static struct
{
    const char *pbData;
    size_t      cbData;
    uint32_t    offStart;
    uint32_t    offAfter;
    uint32_t    cMapElements;
    int         iResult;
} g_aTestBlocks[] =
{
    /* Invalid stuff. */
    { NULL,                             0,   0,  0,                             0, VERR_INVALID_POINTER },
    { NULL,                             512, 0,  0,                             0, VERR_INVALID_POINTER },
    { "",                               0,   0,  0,                             0, VERR_INVALID_PARAMETER },
    { "",                               0,   0,  0,                             0, VERR_INVALID_PARAMETER },
    { "foo=bar1",                       0,   0,  0,                             0, VERR_INVALID_PARAMETER },
    { "foo=bar2",                       0,   50, 50,                            0, VERR_INVALID_PARAMETER },
    /* Has a empty key (not allowed). */
    { TST_STR_AND_BYTES("=test2" TST_STR_VAL_TRM), 0, TST_STR_BYTES(""),        0, VERR_INVALID_PARAMETER },
    /* Empty buffers, i.e. nothing to process. */
    /* Index 6*/
    { "",                               1, 0,  0,                               0, VINF_SUCCESS },
    { TST_STR_VAL_TRM,                  1, 0,  0,                               0, VINF_SUCCESS },
    /* Stream termination sequence. */
    { TST_STR_AND_BYTES(TST_STR_STM_TRM),                                       0,
      TST_STR_BYTES    (TST_STR_STM_TRM),                                       0, VINF_EOF },
    /* Trash after stream termination sequence (skipped / ignored). */
    { TST_STR_AND_BYTES(TST_STR_STM_TRM "trash"),                               0,
      TST_STR_BYTES    (TST_STR_STM_TRM "trash"),                               0, VINF_EOF },
    { TST_STR_AND_BYTES("a=b" TST_STR_STM_TRM),                                 0,
      TST_STR_BYTES    ("a=b" TST_STR_STM_TRM),                                 1, VINF_EOF },
    { TST_STR_AND_BYTES("a=b" TST_STR_VAL_TRM "c=d" TST_STR_STM_TRM),           0,
      TST_STR_BYTES    ("a=b" TST_STR_VAL_TRM "c=d" TST_STR_STM_TRM),           2, VINF_EOF },
    /* Unterminated values (missing separator, i.e. no valid pair). */
    { TST_STR_AND_BYTES("test1"), 0,  0,                                        0, VINF_SUCCESS },
    /* Has a NULL value (allowed). */
    { TST_STR_AND_BYTES("test2=" TST_STR_VAL_TRM),                              0,
      TST_STR_BYTES    ("test2="),                                              1, VINF_SUCCESS },
    /* One completed pair only. */
    { TST_STR_AND_BYTES("test3=test3" TST_STR_VAL_TRM),                         0,
      TST_STR_BYTES    ("test3=test3"),                                         1, VINF_SUCCESS },
    /* One completed pair, plus an unfinished pair (separator + terminator missing). */
    { TST_STR_AND_BYTES("test4=test4" TST_STR_VAL_TRM "t41"),                   0,
      TST_STR_BYTES    ("test4=test4" TST_STR_VAL_TRM),                         1, VINF_SUCCESS },
    /* Two completed pairs. */
    { TST_STR_AND_BYTES("test5=test5" TST_STR_VAL_TRM "t51=t51" TST_STR_VAL_TRM), 0,
      TST_STR_BYTES    ("test5=test5" TST_STR_VAL_TRM "t51=t51"),               2, VINF_SUCCESS },
    /* One complete block, next block unterminated. */
    { TST_STR_AND_BYTES("a51=b51" TST_STR_VAL_TRM "c52=d52" TST_STR_BLK_TRM "e53=f53"), 0,
      TST_STR_BYTES    ("a51=b51" TST_STR_VAL_TRM "c52=d52" TST_STR_BLK_TRM),           2, VINF_SUCCESS },
    /* Ditto. */
    { TST_STR_AND_BYTES("test6=test6" TST_STR_BLK_TRM "t61=t61"),               0,
      TST_STR_BYTES    ("test6=test6" TST_STR_BLK_TRM),                         1, VINF_SUCCESS },
    /* Two complete pairs with a complete stream. */
    { TST_STR_AND_BYTES("test61=" TST_STR_VAL_TRM "test611=test612" TST_STR_STM_TRM), 0,
      TST_STR_BYTES    ("test61=" TST_STR_VAL_TRM "test611=test612" TST_STR_STM_TRM), 2, VINF_EOF },
    /* One complete block. */
    { TST_STR_AND_BYTES("test7=test7" TST_STR_BLK_TRM),                         0,
      TST_STR_BYTES     ("test7=test7"),                                        1, VINF_SUCCESS },
    /* Ditto. */
    { TST_STR_AND_BYTES("test81=test82" TST_STR_VAL_TRM "t81=t82" TST_STR_BLK_TRM), 0,
      TST_STR_BYTES    ("test81=test82" TST_STR_VAL_TRM "t81=t82"),             2, VINF_SUCCESS },
    /* Good stuff, but with a second block -- should be *not* taken into account since
     * we're only interested in parsing/handling the first object. */
    { TST_STR_AND_BYTES("t91=t92" TST_STR_VAL_TRM "t93=t94" TST_STR_BLK_TRM "t95=t96" TST_STR_BLK_TRM), 0,
      TST_STR_BYTES    ("t91=t92" TST_STR_VAL_TRM "t93=t94" TST_STR_BLK_TRM),   2, VINF_SUCCESS },
    /* Nasty stuff. */
        /* iso 8859-1 encoding (?) of 'aou' all with diaeresis '=f' and 'ao' with diaeresis. */
    { TST_STR_AND_BYTES("1\xe4\xf6\xfc=\x66\xe4\xf6" TST_STR_BLK_TRM),          0,
      TST_STR_BYTES    ("1\xe4\xf6\xfc=\x66\xe4\xf6"),                          1, VINF_SUCCESS },
        /* Like above, but after the first '\0' it adds 'ooo=aaa' all letters with diaeresis. */
    { TST_STR_AND_BYTES("2\xe4\xf6\xfc=\x66\xe4\xf6" TST_STR_VAL_TRM "\xf6\xf6\xf6=\xe4\xe4\xe4"), 0,
      TST_STR_BYTES    ("2\xe4\xf6\xfc=\x66\xe4\xf6" TST_STR_VAL_TRM),                             1, VINF_SUCCESS },
    /* Some "real world" examples from VBoxService toolbox. */
    { TST_STR_AND_BYTES("hdr_id=vbt_stat" TST_STR_VAL_TRM "hdr_ver=1" TST_STR_VAL_TRM "name=foo.txt" TST_STR_BLK_TRM), 0,
      TST_STR_BYTES    ("hdr_id=vbt_stat" TST_STR_VAL_TRM "hdr_ver=1" TST_STR_VAL_TRM "name=foo.txt"),                 3, VINF_SUCCESS }
};

/**
 * Tests parsing multiple stream blocks.
 *
 * Same parsing behavior as for the tests above apply.
 */
static struct
{
    /** Stream data. */
    const char *pbData;
    /** Size of stream data (in bytes). */
    size_t      cbData;
    /** Number of data blocks retrieved. These are separated by "\0\0". */
    uint32_t    cBlocks;
    /** Overall result when done parsing. */
    int         iResult;
} const g_aTestStream[] =
{
    /* No blocks. */
    { "", sizeof(""), 0, VINF_SUCCESS },
    /* Empty block (no key/value pairs), will not be accounted. */
    { TST_STR_STM_TRM,
      TST_STR_BYTES(TST_STR_STM_TRM),                                           0, VINF_EOF },
    /* Good stuff. */
    { TST_STR_AND_BYTES(TST_STR_VAL_TRM "b1=b2" TST_STR_STM_TRM),               1, VINF_EOF },
    { TST_STR_AND_BYTES("b3=b31" TST_STR_STM_TRM),                              1, VINF_EOF },
    { TST_STR_AND_BYTES("b4=b41" TST_STR_BLK_TRM "b51=b61" TST_STR_STM_TRM),    2, VINF_EOF },
    { TST_STR_AND_BYTES("b5=b51" TST_STR_VAL_TRM "b61=b71" TST_STR_STM_TRM),    1, VINF_EOF }
};

/**
 * Tests payload data to string vector parsing.
 */
static struct
{
    /** Payload data to test. */
    const char *pbData;
    /** Size (in bytes) of \b pbData. */
    size_t      cbData;
    /** Number of extracted strings. */
    size_t      cStrings;
    /** Expected result (IPRT-style). */
    int         iResult;
} g_aTestPayloadToStringVector[] =
{
    /** Empty payload. */
    { NULL, 0, 0, VINF_SUCCESS },
    { RT_STR_TUPLE("\0"), 0, VINF_SUCCESS },
    { RT_STR_TUPLE(""), 0, VINF_SUCCESS },
    /** Invalid data. */
    { RT_STR_TUPLE("two\0\0terminators"), 1, VERR_INVALID_PARAMETER },
    { RT_STR_TUPLE("no\0ending\0terminator"), 2, VERR_BUFFER_OVERFLOW },
    { RT_STR_TUPLE("foo"), 0, VERR_BUFFER_OVERFLOW },
    /** Valid data. */
    { RT_STR_TUPLE("foo\0"), 1, VINF_SUCCESS },
    { RT_STR_TUPLE("foo\0bar\0"), 2, VINF_SUCCESS },
    { RT_STR_TUPLE("twoendterminators\0\0"), 1, VINF_SUCCESS },
    { RT_STR_TUPLE("이것은 테스트입니다\0bar\0"), 2, VINF_SUCCESS }
};


/**
 * Reads and parses the stream from a given file.
 *
 * @returns RTEXITCODE
 * @param   pszFile             Absolute path to file to parse.
 */
static int tstReadFromFile(const char *pszFile)
{
    RTFILE fh;
    int rc = RTFileOpen(&fh, pszFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);

    uint64_t cbFileSize;
    rc = RTFileQuerySize(fh, &cbFileSize);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);

    GuestToolboxStream      stream;
    GuestToolboxStreamBlock block;

    size_t cPairs = 0;
    size_t cBlocks = 0;

    unsigned aToRead[] = { 256, 23, 13 }; /* Array of certain read amounts to check for in a sequence. */
    unsigned idxToRead = 0; /* Index within aToRead. */

    uint64_t cbToRead = cbFileSize;

    for (unsigned a = 0; a < 32; a++)
    {
        uint8_t buf[_64K];
        do
        {
            size_t       cbChunk           = cbToRead;
            size_t const cbToReadFromArray = idxToRead < RT_ELEMENTS(aToRead) ? aToRead[idxToRead++] : 0;
            if (cbToReadFromArray)
                cbChunk = RT_MIN(cbChunk, cbToReadFromArray);
            size_t const cbRand            = RTRandU64Ex(8, RT_MIN(sizeof(buf), 64));
            cbChunk = RT_MIN(cbChunk, cbRand);
            if (cbChunk)
            {
                RTTestIPrintf(RTTESTLVL_DEBUG, "Reading %zu bytes (of %zu left) ...\n", cbChunk, cbToRead);

                size_t cbRead;
                rc = RTFileRead(fh, &buf, cbChunk, &cbRead);
                AssertRCBreak(rc);

                if (!cbRead)
                    continue;

                cbToRead -= cbRead;

                rc = stream.AddData((BYTE *)buf, cbRead);
                AssertRCBreak(rc);
            }

            rc = stream.ParseBlock(block);
            Assert(rc != VERR_INVALID_PARAMETER);
            RTTestIPrintf(RTTESTLVL_DEBUG, "Parsing ended with %Rrc\n", rc);
            if (block.IsComplete())
            {
                /* Sanity checks; disable this if you parse anything else but fsinfo output from VBoxService toolbox. */
                //Assert(block.GetString("name") != NULL);

                cPairs += block.GetCount();
                cBlocks = stream.GetBlocks();
                block.Clear();
            }
        } while (VINF_SUCCESS == rc /* Might also be VINF_EOF when finished */);

        RTTestIPrintf(RTTESTLVL_ALWAYS, "Total %zu blocks + %zu pairs\n", cBlocks, cPairs);

        /* Reset. */
        RTFileSeek(fh, 0, RTFILE_SEEK_BEGIN, NULL);
        cbToRead = cbFileSize;
        cPairs = 0;
        cBlocks = 0;
        block.Clear();
        stream.Destroy();
    }

    int rc2 = RTFileClose(fh);
    if (RT_SUCCESS(rc))
        rc = rc2;

    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

int main(int argc, char **argv)
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstParseBuffer", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(hTest);

#ifdef DEBUG
    RTUINT fFlags = RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    fFlags |= RTLOGFLAGS_USECRLF;
#endif
    static const char * const s_apszLogGroups[] = VBOX_LOGGROUP_NAMES;
    int rc = RTLogCreate(&g_pLog, fFlags, "guest_control.e.l.l2.l3.f", NULL,
                         RT_ELEMENTS(s_apszLogGroups), s_apszLogGroups, RTLOGDEST_STDOUT, NULL /*"vkat-release.log"*/);
    AssertRCReturn(rc, rc);
    RTLogSetDefaultInstance(g_pLog);
#endif

    if (argc > 1)
        return tstReadFromFile(argv[1]);

    RTTestIPrintf(RTTESTLVL_DEBUG, "Initializing COM...\n");
    HRESULT hrc = com::Initialize();
    if (FAILED(hrc))
    {
        RTTestFailed(hTest, "Failed to initialize COM (%Rhrc)!\n", hrc);
        return RTEXITCODE_FAILURE;
    }

    AssertCompile(TST_STR_BYTES("1")           == 1);
    AssertCompile(TST_STR_BYTES("sizecheck")   == 9);
    AssertCompile(TST_STR_BYTES("off=rab")     == 7);
    AssertCompile(TST_STR_BYTES("off=rab\0\0") == 9);

    RTTestSub(hTest, "Blocks");

    RTTestDisableAssertions(hTest);

    for (unsigned iTest = 0; iTest < RT_ELEMENTS(g_aTestBlocks); iTest++)
    {
        RTTestIPrintf(RTTESTLVL_DEBUG, "=> Block test #%u:\n'%.*Rhxd\n", iTest, g_aTestBlocks[iTest].cbData, g_aTestBlocks[iTest].pbData);

        GuestToolboxStream stream;
        int iResult = stream.AddData((BYTE *)g_aTestBlocks[iTest].pbData, g_aTestBlocks[iTest].cbData);
        if (RT_SUCCESS(iResult))
        {
            GuestToolboxStreamBlock curBlock;
            iResult = stream.ParseBlock(curBlock);
            if (iResult != g_aTestBlocks[iTest].iResult)
                RTTestFailed(hTest, "Block #%u: Returned %Rrc, expected %Rrc\n", iTest, iResult, g_aTestBlocks[iTest].iResult);
            else if (stream.GetOffset() != g_aTestBlocks[iTest].offAfter)
                RTTestFailed(hTest, "Block #%u: Offset %zu wrong ('%#x'), expected %u ('%#x')\n",
                             iTest, stream.GetOffset(), g_aTestBlocks[iTest].pbData[stream.GetOffset()],
                             g_aTestBlocks[iTest].offAfter, g_aTestBlocks[iTest].pbData[g_aTestBlocks[iTest].offAfter]);
            else if (iResult == VERR_MORE_DATA)
                RTTestIPrintf(RTTESTLVL_DEBUG, "\tMore data (Offset: %zu)\n", stream.GetOffset());

            if (RT_SUCCESS(iResult) || iResult == VERR_MORE_DATA)
                if (curBlock.GetCount() != g_aTestBlocks[iTest].cMapElements)
                    RTTestFailed(hTest, "Block #%u: Map has %u elements, expected %u\n",
                                 iTest, curBlock.GetCount(), g_aTestBlocks[iTest].cMapElements);

            /* There is remaining data left in the buffer (which needs to be merged
             * with a following buffer) -- print it. */
            size_t off = stream.GetOffset();
            size_t cbToWrite = g_aTestBlocks[iTest].cbData - off;
            if (cbToWrite)
            {
                RTTestIPrintf(RTTESTLVL_DEBUG, "\tRemaining (%u):\n", cbToWrite);

                /* How to properly get the current RTTESTLVL (aka IPRT_TEST_MAX_LEVEL) here?
                 * Hack alert: Using RTEnvGet for now. */
                if (!RTStrICmp(RTEnvGet("IPRT_TEST_MAX_LEVEL"), "debug"))
                    RTStrmWriteEx(g_pStdOut, &g_aTestBlocks[iTest].pbData[off], cbToWrite - 1, NULL);
            }

            if (RTTestIErrorCount())
                break;
        }
    }

    RTTestSub(hTest, "Streams");

    for (unsigned iTest = 0; iTest < RT_ELEMENTS(g_aTestStream); iTest++)
    {
        RTTestIPrintf(RTTESTLVL_DEBUG, "=> Stream test #%u\n%.*Rhxd\n",
                      iTest, g_aTestStream[iTest].cbData, g_aTestStream[iTest].pbData);

        GuestToolboxStream stream;
        int iResult = stream.AddData((BYTE*)g_aTestStream[iTest].pbData, g_aTestStream[iTest].cbData);
        if (RT_SUCCESS(iResult))
        {
            uint32_t cBlocksComplete = 0;
            uint8_t  cSafety = 0;
            do
            {
                GuestToolboxStreamBlock curBlock;
                iResult = stream.ParseBlock(curBlock);
                RTTestIPrintf(RTTESTLVL_DEBUG, "Stream #%u: Returned with %Rrc\n", iTest, iResult);
                if (cSafety++ > 8)
                    break;
                if (curBlock.IsComplete())
                    cBlocksComplete++;
            } while (iResult != VINF_EOF);

            if (iResult != g_aTestStream[iTest].iResult)
                RTTestFailed(hTest, "Stream #%u: Returned %Rrc, expected %Rrc\n", iTest, iResult, g_aTestStream[iTest].iResult);
            else if (cBlocksComplete != g_aTestStream[iTest].cBlocks)
                RTTestFailed(hTest, "Stream #%u: Returned %u blocks, expected %u\n", iTest, cBlocksComplete, g_aTestStream[iTest].cBlocks);
        }
        else
            RTTestFailed(hTest, "Stream #%u: Adding data failed with %Rrc\n", iTest, iResult);

        if (RTTestIErrorCount())
            break;
    }

    /*
     * Payload to string vector testing.
     */
    for (unsigned iTest = 0; iTest < RT_ELEMENTS(g_aTestPayloadToStringVector); iTest++)
    {
        GuestWaitEventPayload Payload(0 /* Type */,
                                      g_aTestPayloadToStringVector[iTest].pbData, (uint32_t)g_aTestPayloadToStringVector[iTest].cbData);
        std::vector<Utf8Str> vecStr;
        RTTEST_CHECK_RC(hTest, Payload.ToStringVector(vecStr), g_aTestPayloadToStringVector[iTest].iResult);
        RTTEST_CHECK(hTest, vecStr.size() == g_aTestPayloadToStringVector[iTest].cStrings);
    }

    RTTestRestoreAssertions(hTest);

    RTTestIPrintf(RTTESTLVL_DEBUG, "Shutting down COM...\n");
    com::Shutdown();

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

