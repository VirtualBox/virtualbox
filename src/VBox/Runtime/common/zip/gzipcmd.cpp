/* $Id: gzipcmd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - GZIP Utility.
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
#include <iprt/zip.h>

#include <iprt/buildconfig.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/vfs.h>
#include <iprt/zip.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Gzip command options.
 */
typedef struct RTGZIPCMDOPTS
{
    bool            fAscii;
    bool            fStdOut;
    bool            fDecompress;
    bool            fForce;
    bool            fKeep;
    bool            fList;
    bool            fName;
    bool            fQuiet;
    bool            fRecursive;
    const char     *pszSuff;
    bool            fTest;
    unsigned        uLevel;
    /** The current output filename (for deletion). */
    char            szOutput[RTPATH_MAX];
    /** The current input filename (for deletion and messages). */
    const char     *pszInput;
} RTGZIPCMDOPTS;
/** Pointer to GZIP options. */
typedef RTGZIPCMDOPTS *PRTGZIPCMDOPTS;
/** Pointer to const GZIP options. */
typedef RTGZIPCMDOPTS const *PCRTGZIPCMDOPTS;



/**
 * Checks if the given standard handle is a TTY.
 *
 * @returns true / false
 * @param   enmStdHandle    The standard handle.
 */
static bool gzipIsStdHandleATty(RTHANDLESTD enmStdHandle)
{
    /** @todo Add isatty() to IPRT. */
    RT_NOREF1(enmStdHandle);
    return false;
}


/**
 * Pushes data from the input to the output I/O streams.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE.
 * @param   hVfsSrc         The source I/O stream.
 * @param   hVfsDst         The destination I/O stream.
 */
static RTEXITCODE gzipPush(RTVFSIOSTREAM hVfsSrc, RTVFSIOSTREAM hVfsDst)
{
    for (;;)
    {
        uint8_t abBuf[_64K];
        size_t  cbRead;
        int rc = RTVfsIoStrmRead(hVfsSrc, abBuf, sizeof(abBuf), true /*fBlocking*/, &cbRead);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTVfsIoStrmRead failed: %Rrc", rc);
        if (rc == VINF_EOF && cbRead == 0)
            return RTEXITCODE_SUCCESS;

        rc = RTVfsIoStrmWrite(hVfsDst, abBuf, cbRead, true /*fBlocking*/, NULL /*cbWritten*/);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTVfsIoStrmWrite failed: %Rrc", rc);
    }
}


/**
 * Pushes the bytes from the input to the output stream, flushes the output
 * stream and closes both of them.
 *
 * On failure, we will delete the output file, if it's a file.  The input file
 * may be deleted, if we're not told to keep it (--keep, --to-stdout).
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE.
 * @param   phVfsSrc        The input stream. Set to NIL if closed.
 * @param   pOpts           The options.
 * @param   phVfsDst        The output stream. Set to NIL if closed.
 */
static RTEXITCODE gzipPushFlushAndClose(PRTVFSIOSTREAM phVfsSrc, PCRTGZIPCMDOPTS pOpts, PRTVFSIOSTREAM phVfsDst)
{
    /*
     * Push bytes, flush and close the streams.
     */
    RTEXITCODE rcExit = gzipPush(*phVfsSrc, *phVfsDst);

    RTVfsIoStrmRelease(*phVfsSrc);
    *phVfsSrc = NIL_RTVFSIOSTREAM;

    int rc = RTVfsIoStrmFlush(*phVfsDst);
    if (RT_FAILURE(rc) && rc != VERR_INVALID_PARAMETER)
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to flush the output file: %Rrc", rc);
    RTVfsIoStrmRelease(*phVfsDst);
    *phVfsDst = NIL_RTVFSIOSTREAM;

    /*
     * Do the cleaning up, if needed.  Remove the input file, if that's the
     * desire of the user, or remove the output file on failure.
     */
    if (!pOpts->fStdOut)
    {
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            if (!pOpts->fKeep)
            {
                rc = RTFileDelete(pOpts->pszInput);
                if (RT_FAILURE(rc))
                    rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to delete '%s': %Rrc", pOpts->pszInput, rc);
            }
        }
        else
        {
            rc = RTFileDelete(pOpts->szOutput);
            if (RT_FAILURE(rc))
                RTMsgError("Failed to delete '%s': %Rrc", pOpts->szOutput, rc);
        }
    }

    return rcExit;
}


/**
 * Compresses one stream to another.
 *
 * @returns Exit code.
 * @param   phVfsSrc        The input stream. Set to NIL if closed.
 * @param   pOpts           The options.
 * @param   phVfsDst        The output stream. Set to NIL if closed.
 */
static RTEXITCODE gzipCompressFile(PRTVFSIOSTREAM phVfsSrc, PCRTGZIPCMDOPTS pOpts, PRTVFSIOSTREAM phVfsDst)
{
    /*
     * Attach the compressor to the output stream.
     */
    RTVFSIOSTREAM hVfsGzip;
    int rc = RTZipGzipCompressIoStream(*phVfsDst, 0 /*fFlags*/, pOpts->uLevel, &hVfsGzip);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTZipGzipCompressIoStream failed: %Rrc", rc);

    uint32_t cRefs = RTVfsIoStrmRelease(*phVfsDst);
    Assert(cRefs > 0); RT_NOREF_PV(cRefs);
    *phVfsDst = hVfsGzip;

    return gzipPushFlushAndClose(phVfsSrc, pOpts, phVfsDst);
}


/**
 * Attach a decompressor to the given source stream, replacing and releasing the
 * input handle with the decompressor.
 *
 * @returns Exit code.
 * @param   phVfsSrc        The input stream. Replaced on success.
 */
static RTEXITCODE gzipSetupDecompressor(PRTVFSIOSTREAM phVfsSrc)
{
    /*
     * Attach the decompressor to the input stream.
     */
    uint32_t fFlags = 0;
    fFlags |= RTZIPGZIPDECOMP_F_ALLOW_ZLIB_HDR;
    RTVFSIOSTREAM hVfsGunzip;
    int rc = RTZipGzipDecompressIoStream(*phVfsSrc, fFlags, &hVfsGunzip);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTZipGzipDecompressIoStream failed: %Rrc", rc);

    uint32_t cRefs = RTVfsIoStrmRelease(*phVfsSrc);
    Assert(cRefs > 0); RT_NOREF_PV(cRefs);
    *phVfsSrc = hVfsGunzip;

#if 0
    /* This is a good place for testing stuff. */
    rc = RTVfsCreateReadAheadForIoStream(*phVfsSrc, 0, 16, _4K+1, &hVfsGunzip);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        uint32_t cRefs = RTVfsIoStrmRelease(*phVfsSrc);
        Assert(cRefs > 0);
        *phVfsSrc = hVfsGunzip;
    }
#endif

    return RTEXITCODE_SUCCESS;
}


/**
 * Decompresses one stream to another.
 *
 * @returns Exit code.
 * @param   phVfsSrc        The input stream. Set to NIL if closed.
 * @param   pOpts           The options.
 * @param   phVfsDst        The output stream. Set to NIL if closed.
 */
static RTEXITCODE gzipDecompressFile(PRTVFSIOSTREAM phVfsSrc, PCRTGZIPCMDOPTS pOpts, PRTVFSIOSTREAM phVfsDst)
{
    RTEXITCODE rcExit = gzipSetupDecompressor(phVfsSrc);
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = gzipPushFlushAndClose(phVfsSrc, pOpts, phVfsDst);
    return rcExit;
}


/**
 * For testing the archive (todo).
 *
 * @returns Exit code.
 * @param   phVfsSrc        The input stream. Set to NIL if closed.
 * @param   pOpts           The options.
 */
static RTEXITCODE gzipTestFile(PRTVFSIOSTREAM phVfsSrc, PCRTGZIPCMDOPTS pOpts)
{
    RT_NOREF_PV(pOpts);

    /*
     * Read the whole stream.
     */
    RTEXITCODE rcExit = gzipSetupDecompressor(phVfsSrc);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        for (;;)
        {
            uint8_t abBuf[_64K];
            size_t  cbRead;
            int rc = RTVfsIoStrmRead(*phVfsSrc, abBuf, sizeof(abBuf), true /*fBlocking*/, &cbRead);
            if (RT_FAILURE(rc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTVfsIoStrmRead failed: %Rrc", rc);
            if (rc == VINF_EOF && cbRead == 0)
                return RTEXITCODE_SUCCESS;
        }
    }
    return rcExit;
}


static RTEXITCODE gzipListFile(PRTVFSIOSTREAM phVfsSrc, PCRTGZIPCMDOPTS pOpts)
{
    RT_NOREF2(phVfsSrc, pOpts);
    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Listing has not been implemented");
}


/**
 * Opens the output file.
 *
 * @returns Command exit, error messages written using RTMsg*.
 *
 * @param   pszFile             The input filename.
 * @param   pOpts               The options, szOutput will be filled in by this
 *                              function on success.
 * @param   phVfsIos            Where to return the output stream handle.
 *
 * @remarks This is actually not quite the way we need to do things.
 *
 *          First of all, we need a GZIP file system stream for a real GZIP
 *          implementation, since there may be more than one file in the gzipped
 *          file.
 *
 *          Second, we need to open the output files as we encounter files in the input
 *          file system stream. The gzip format contains timestamp and usually a
 *          filename, the default is to use this name (see the --no-name
 *          option).
 */
static RTEXITCODE gzipOpenOutput(const char *pszFile, PRTGZIPCMDOPTS pOpts, PRTVFSIOSTREAM phVfsIos)
{
    int rc;
    if (!strcmp(pszFile, "-") || pOpts->fStdOut)
    {
        strcpy(pOpts->szOutput, "-");

        if (   !pOpts->fForce
            && !pOpts->fDecompress
            && gzipIsStdHandleATty(RTHANDLESTD_OUTPUT))
            return RTMsgErrorExit(RTEXITCODE_SYNTAX,
                                  "Yeah, right. I'm not writing any compressed data to the terminal without --force.\n");

        rc = RTVfsIoStrmFromStdHandle(RTHANDLESTD_OUTPUT,
                                      RTFILE_O_WRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE,
                                      true /*fLeaveOpen*/,
                                      phVfsIos);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error opening standard output: %Rrc", rc);
    }
    else
    {
        Assert(!RTVfsChainIsSpec(pszFile));

        /* Construct an output filename. */
        rc = RTStrCopy(pOpts->szOutput, sizeof(pOpts->szOutput), pszFile);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error constructing output filename: %Rrc", rc);
        if (pOpts->fDecompress)
        {
            /** @todo take filename from archive? */
            size_t cchSuff = strlen(pOpts->pszSuff); Assert(cchSuff > 0);
            size_t cch = strlen(pOpts->szOutput);
            if (   cch <= cchSuff
                || strcmp(&pOpts->szOutput[cch - cchSuff], pOpts->pszSuff))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "Input file does not end with: '%s'", pOpts->pszSuff);
            pOpts->szOutput[cch - cchSuff] = '\0';
            if (!RTPathFilename(pOpts->szOutput))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error constructing output filename: Input file name is all suffix.");
        }
        else
        {
            rc = RTStrCat(pOpts->szOutput, sizeof(pOpts->szOutput), pOpts->pszSuff);
            if (RT_FAILURE(rc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error constructing output filename: %Rrc", rc);
        }

        /* Open the output file. */
        uint32_t fOpen = RTFILE_O_WRITE | RTFILE_O_DENY_WRITE;
        if (pOpts->fForce)
            fOpen |= RTFILE_O_CREATE_REPLACE;
        else
            fOpen |= RTFILE_O_CREATE;
        rc = RTVfsIoStrmOpenNormal(pOpts->szOutput, fOpen, phVfsIos);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error opening output file '%s': %Rrc", pOpts->szOutput, rc);
    }

    return RTEXITCODE_SUCCESS;
}


/**
 * Opens the input file.
 *
 * @returns Command exit, error messages written using RTMsg*.
 *
 * @param   pszFile             The input filename.
 * @param   pOpts               The options, szOutput will be filled in by this
 *                              function on success.
 * @param   phVfsIos            Where to return the input stream handle.
 */
static RTEXITCODE gzipOpenInput(const char *pszFile, PRTGZIPCMDOPTS pOpts, PRTVFSIOSTREAM phVfsIos)
{
    int rc;

    pOpts->pszInput = pszFile;
    if (!strcmp(pszFile, "-"))
    {
        if (   !pOpts->fForce
            && pOpts->fDecompress
            && gzipIsStdHandleATty(RTHANDLESTD_OUTPUT))
            return RTMsgErrorExit(RTEXITCODE_SYNTAX,
                                  "Yeah, right. I'm not reading any compressed data from the terminal without --force.\n");

        rc = RTVfsIoStrmFromStdHandle(RTHANDLESTD_INPUT,
                                      RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE,
                                      true /*fLeaveOpen*/,
                                      phVfsIos);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error opening standard input: %Rrc", rc);
    }
    else
    {
        uint32_t        offError = 0;
        RTERRINFOSTATIC ErrInfo;
        rc = RTVfsChainOpenIoStream(pszFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE,
                                    phVfsIos, &offError, RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(rc))
            return RTVfsChainMsgErrorExitFailure("RTVfsChainOpenIoStream", pszFile, rc, offError, &ErrInfo.Core);
    }

    return RTEXITCODE_SUCCESS;

}


/**
 * A mini GZIP program.
 *
 * @returns Program exit code.
 *
 * @param   cArgs               The number of arguments.
 * @param   papszArgs           The argument vector.  (Note that this may be
 *                              reordered, so the memory must be writable.)
 */
RTDECL(RTEXITCODE) RTZipGzipCmd(unsigned cArgs, char **papszArgs)
{

    /*
     * Parse the command line.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--ascii",        'a', RTGETOPT_REQ_NOTHING },
        { "--stdout",       'c', RTGETOPT_REQ_NOTHING },
        { "--to-stdout",    'c', RTGETOPT_REQ_NOTHING },
        { "--decompress",   'd', RTGETOPT_REQ_NOTHING },
        { "--uncompress",   'd', RTGETOPT_REQ_NOTHING },
        { "--force",        'f', RTGETOPT_REQ_NOTHING },
        { "--keep",         'k', RTGETOPT_REQ_NOTHING },
        { "--list",         'l', RTGETOPT_REQ_NOTHING },
        { "--no-name",      'n', RTGETOPT_REQ_NOTHING },
        { "--name",         'N', RTGETOPT_REQ_NOTHING },
        { "--quiet",        'q', RTGETOPT_REQ_NOTHING },
        { "--recursive",    'r', RTGETOPT_REQ_NOTHING },
        { "--suffix",       'S', RTGETOPT_REQ_STRING  },
        { "--test",         't', RTGETOPT_REQ_NOTHING },
        { "--verbose",      'v', RTGETOPT_REQ_NOTHING },
        { "--fast",         '1', RTGETOPT_REQ_NOTHING },
        { "-1",             '1', RTGETOPT_REQ_NOTHING },
        { "-2",             '2', RTGETOPT_REQ_NOTHING },
        { "-3",             '3', RTGETOPT_REQ_NOTHING },
        { "-4",             '4', RTGETOPT_REQ_NOTHING },
        { "-5",             '5', RTGETOPT_REQ_NOTHING },
        { "-6",             '6', RTGETOPT_REQ_NOTHING },
        { "-7",             '7', RTGETOPT_REQ_NOTHING },
        { "-8",             '8', RTGETOPT_REQ_NOTHING },
        { "-9",             '9', RTGETOPT_REQ_NOTHING },
        { "--best",         '9', RTGETOPT_REQ_NOTHING }
    };

    RTGZIPCMDOPTS Opts;
    Opts.fAscii      = false;
    Opts.fStdOut     = false;
    Opts.fDecompress = false;
    Opts.fForce      = false;
    Opts.fKeep       = false;
    Opts.fList       = false;
    Opts.fName       = true;
    Opts.fQuiet      = false;
    Opts.fRecursive  = false;
    Opts.pszSuff     = ".gz";
    Opts.fTest       = false;
    Opts.uLevel      = 6;

    RTEXITCODE  rcExit      = RTEXITCODE_SUCCESS;
    unsigned    cProcessed  = 0;
    //RTVFSIOSTREAM hVfsStdOut= NIL_RTVFSIOSTREAM;

    RTGETOPTSTATE GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
                          RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "RTGetOptInit: %Rrc", rc);

    for (;;)
    {
        RTGETOPTUNION ValueUnion;
        int chOpt = RTGetOpt(&GetState, &ValueUnion);
        switch (chOpt)
        {
            case 0:
                /*
                 * If we've processed any files we're done.  Otherwise take
                 * input from stdin and write the output to stdout.
                 */
                if (cProcessed > 0)
                    return rcExit;
                ValueUnion.psz = "-";
                Opts.fStdOut = true;
                RT_FALL_THRU();
            case VINF_GETOPT_NOT_OPTION:
            {
                if (!*Opts.pszSuff && !Opts.fStdOut)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "The --suffix option specified an empty string");
                if (!Opts.fStdOut && RTVfsChainIsSpec(ValueUnion.psz))
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Must use standard out with VFS chain specifications");
                if (   Opts.fName
                    && !Opts.fList
                    && !Opts.fTest
                    && !Opts.fDecompress)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "The --name option has not yet been implemented. Use --no-name.");
                if (Opts.fAscii)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "The --ascii option has not yet been implemented.");
                if (Opts.fRecursive)
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "The --recursive option has not yet been implemented.");

                /* Open the input file. */
                RTVFSIOSTREAM hVfsSrc;
                RTEXITCODE rcExit2 = gzipOpenInput(ValueUnion.psz, &Opts, &hVfsSrc);
                if (rcExit2 == RTEXITCODE_SUCCESS)
                {
                    if (Opts.fList)
                        rcExit2 = gzipListFile(&hVfsSrc, &Opts);
                    else if (Opts.fTest)
                        rcExit2 = gzipTestFile(&hVfsSrc, &Opts);
                    else
                    {
                        RTVFSIOSTREAM hVfsDst;
                        rcExit2 = gzipOpenOutput(ValueUnion.psz, &Opts, &hVfsDst);
                        if (rcExit2 == RTEXITCODE_SUCCESS)
                        {
                            if (Opts.fDecompress)
                                rcExit2 = gzipDecompressFile(&hVfsSrc, &Opts, &hVfsDst);
                            else
                                rcExit2 = gzipCompressFile(&hVfsSrc, &Opts, &hVfsDst);
                            RTVfsIoStrmRelease(hVfsDst);
                        }
                    }
                    RTVfsIoStrmRelease(hVfsSrc);
                }
                if (rcExit2 != RTEXITCODE_SUCCESS)
                    rcExit = rcExit2;

                cProcessed++;
                break;
            }

            case 'a':   Opts.fAscii      = true;  break;
            case 'c':
                Opts.fStdOut = true;
                Opts.fKeep   = true;
                break;
            case 'd':   Opts.fDecompress = true;  break;
            case 'f':   Opts.fForce      = true;  break;
            case 'k':   Opts.fKeep       = true;  break;
            case 'l':   Opts.fList       = true;  break;
            case 'n':   Opts.fName       = false; break;
            case 'N':   Opts.fName       = true;  break;
            case 'q':   Opts.fQuiet      = true;  break;
            case 'r':   Opts.fRecursive  = true;  break;
            case 'S':   Opts.pszSuff     = ValueUnion.psz; break;
            case 't':   Opts.fTest       = true;  break;
            case 'v':   Opts.fQuiet      = false; break;
            case '1':   Opts.uLevel      = 1;     break;
            case '2':   Opts.uLevel      = 2;     break;
            case '3':   Opts.uLevel      = 3;     break;
            case '4':   Opts.uLevel      = 4;     break;
            case '5':   Opts.uLevel      = 5;     break;
            case '6':   Opts.uLevel      = 6;     break;
            case '7':   Opts.uLevel      = 7;     break;
            case '8':   Opts.uLevel      = 8;     break;
            case '9':   Opts.uLevel      = 9;     break;

            case 'h':
                RTPrintf("Usage: to be written\nOption dump:\n");
                for (unsigned i = 0; i < RT_ELEMENTS(s_aOptions); i++)
                    RTPrintf(" -%c,%s\n", s_aOptions[i].iShort, s_aOptions[i].pszLong);
                return RTEXITCODE_SUCCESS;

            case 'V':
                RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
                return RTEXITCODE_SUCCESS;

            default:
                return RTGetOptPrintError(chOpt, &ValueUnion);
        }
    }
}

