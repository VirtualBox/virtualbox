/* $Id: VBoxNSISTool.cpp 107329 2024-12-11 08:46:09Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxNSISTool - Utility program for NSIS-based tasks.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#include <iprt/assert.h>
#include <iprt/crc.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/stream.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/

/** Taken from NSIS 3.10. */
#pragma pack(1)
typedef struct
{
  uint32_t flags; // FH_FLAGS_*
  uint32_t siginfo;  // FH_SIG

  uint32_t nsinst[3]; // FH_INT1,FH_INT2,FH_INT3

  // these point to the header+sections+entries+stringtable in the datablock
  uint32_t length_of_header;

  // this specifies the length of all the data (including the firstheader and CRC)
  uint32_t length_of_all_following_data;
} firstheader;
#pragma pack()

/** Taken from NSIS 3.10. */
#define FH_SIG  0xDEADBEEF
#define FH_INT1 0x6C6C754E
#define FH_INT2 0x74666F73
#define FH_INT3 0x74736E49

/** Size of a NSIS block (in bytes). */
#define VBOX_NSIS_BLOCK_SIZE_BYTES 512

/**
 * Structure for keeping NSIS installer file data.
 */
typedef struct VBOXNSISFILE
{
    /** File handle of NSIS installer. */
    RTFILE      hFile;
    /** File size (in bytes).
     *  Set to 0 if not set (yet). */
    uint64_t    cbFile;
    /** Header structure, taken from NSIS. */
    firstheader FirstHdr;
    /** Absolute file offset (in bytes) of \a FirstHdr.
     *  Set to UINT64_MAX if not set (yet). */
    uint64_t    offHdr;
    /** Absolute file offset (in bytes) of CRC32 to read/write.
     *  Set to UINT64_MAX if not set (yet). */
    uint64_t    offCRC32;
    /** Currente CRC32 checksum being used.
     *  Set to 0 if not initialized. */
    uint32_t    uCRC32;
    /** Whether the installer file is considered being valid or not. */
    bool        fVerified;
} VBOXNSISFILE;
/** Pointer to a structure for keeping NSIS installer file data. */
typedef VBOXNSISFILE *PVBOXNSISFILE;
AssertCompileMemberSize(VBOXNSISFILE, uCRC32, 4);

/**
 * Calculates the CRC32 of a given NSIS installer file.
 *
 * @returns VBox status code.
 * @param   pNSISFile           NSIS installer file to calculate CRC32 for.
 * @param   puCRC32             Where to return the calculated CRC32 on success.
 */
static int vboxNSISCRC32Calc(PVBOXNSISFILE pNSISFile, uint32_t *puCRC32)
{
    AssertPtrReturn(puCRC32, VERR_INVALID_POINTER);

    AssertReturn(pNSISFile->FirstHdr.length_of_all_following_data, VERR_INVALID_PARAMETER);
    AssertReturn(pNSISFile->offHdr % VBOX_NSIS_BLOCK_SIZE_BYTES == 0, VERR_INVALID_PARAMETER);

    size_t const  offRead = VBOX_NSIS_BLOCK_SIZE_BYTES;

    int rc = RTFileSeek(pNSISFile->hFile, offRead, RTFILE_SEEK_BEGIN, NULL);
    if (RT_FAILURE(rc))
        return rc;

    size_t cbToRead =  pNSISFile->offHdr
                      + pNSISFile->FirstHdr.length_of_all_following_data
                      - sizeof(pNSISFile->uCRC32)
                      - VBOX_NSIS_BLOCK_SIZE_BYTES;
    Assert(offRead + cbToRead <= pNSISFile->cbFile);

    uint32_t uCRC32 = RTCrc32Start();

    uint8_t abBuf[_64K];
    while (cbToRead)
    {
        size_t cbRead = 0;
        rc = RTFileRead(pNSISFile->hFile, abBuf, RT_MIN(cbToRead, sizeof(abBuf)), &cbRead);
        if (RT_FAILURE(rc))
            break;

        uCRC32 = RTCrc32Process(uCRC32, abBuf, cbRead);

        AssertBreakStmt(cbToRead >= cbRead, rc = VERR_INVALID_PARAMETER);
        cbToRead -= cbRead;
    }

    if (RT_SUCCESS(rc))
    {
        Assert(cbToRead == 0);
        *puCRC32 = RTCrc32Finish(uCRC32);
    }

    return rc;
}

/**
 * Writes the CRC32 to the NSIS installer file.
 *
 * @returns VBox status code.
 * @param   pNSISFile           NSIS installer file to write CRC32 for.
 * @param   uCRC32              CRC32 to write.
 */
static int vboxNSISToolCRCWrite(PVBOXNSISFILE pNSISFile, uint32_t uCRC32)
{
    AssertReturn(pNSISFile->offCRC32 != UINT64_MAX, VERR_WRONG_ORDER);

    int rc = RTFileSeek(pNSISFile->hFile, pNSISFile->offCRC32, RTFILE_SEEK_BEGIN, NULL);
    if (RT_FAILURE(rc))
        return rc;

    return RTFileWrite(pNSISFile->hFile, &uCRC32, sizeof(uCRC32), NULL);
}

/**
 * Verifies an NSIS installer file.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if verification successful, error if not.
 * @param   pNSISFile           NSIS installer file to verify.
 */
static int vboxNSISToolVerify(PVBOXNSISFILE pNSISFile)
{
    AssertReturn(pNSISFile->cbFile, VERR_WRONG_ORDER);

    if (pNSISFile->fVerified) /* Already verified? Bail out. */
        return VINF_SUCCESS;

    int rc = VERR_INVALID_EXE_SIGNATURE;

    /*
     * Do a little bit of verification first.
     */
    size_t i = 0;
    for (; i < pNSISFile->cbFile / VBOX_NSIS_BLOCK_SIZE_BYTES; i++)
    {
        rc = RTFileSeek(pNSISFile->hFile, i * VBOX_NSIS_BLOCK_SIZE_BYTES, RTFILE_SEEK_BEGIN, NULL);
        if (RT_FAILURE(rc))
            break;

        RT_ZERO(pNSISFile->FirstHdr);

        size_t cbRead;
        rc = RTFileRead(pNSISFile->hFile, &pNSISFile->FirstHdr, sizeof(pNSISFile->FirstHdr), &cbRead);
        if (cbRead != sizeof(pNSISFile->FirstHdr))
        {
            rc = VERR_INVALID_EXE_SIGNATURE;
            break;
        }

        if (   cbRead == sizeof(pNSISFile->FirstHdr)
            && pNSISFile->FirstHdr.siginfo   == FH_SIG
            && pNSISFile->FirstHdr.nsinst[0] == FH_INT1
            && pNSISFile->FirstHdr.nsinst[1] == FH_INT2
            && pNSISFile->FirstHdr.nsinst[2] == FH_INT3
            && pNSISFile->FirstHdr.length_of_header
            && pNSISFile->FirstHdr.length_of_all_following_data)
        {
            rc = VINF_SUCCESS;
            break;
        }
    }

    if (RT_FAILURE(rc))
    {
        RTMsgError("NSIS Header invalid / corrupt");
        return rc;
    }

    pNSISFile->offHdr   = i * VBOX_NSIS_BLOCK_SIZE_BYTES;
    AssertReturn(pNSISFile->offHdr >= sizeof(pNSISFile->uCRC32), VERR_INVALID_EXE_SIGNATURE);
    AssertReturn(pNSISFile->offHdr % VBOX_NSIS_BLOCK_SIZE_BYTES == 0, VERR_INVALID_EXE_SIGNATURE);
    pNSISFile->offCRC32 = pNSISFile->offHdr + pNSISFile->FirstHdr.length_of_all_following_data - sizeof(pNSISFile->uCRC32);

    /*
     * Get the stored CRC checksum.
     */
    uint32_t u32CRCFile = 0;
    rc = RTFileSeek(pNSISFile->hFile, pNSISFile->offCRC32, RTFILE_SEEK_BEGIN, NULL);
    if (RT_FAILURE(rc))
        return rc;
    rc = RTFileRead(pNSISFile->hFile, &u32CRCFile, sizeof(u32CRCFile), NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Calculate the checksum ourselves.
     *
     * Note! We store the calculated checksum in our file context so that we can skip calculating it
     *       again when writing (patching) the checksum.
     */
    rc = vboxNSISCRC32Calc(pNSISFile, &pNSISFile->uCRC32);
    if (RT_SUCCESS(rc))
    {
        if (pNSISFile->uCRC32 == u32CRCFile)
        {
            pNSISFile->fVerified = true;
        }
        else
        {
            RTMsgInfo("Warning: Checksums do not match (expected %#x, got %#x)\n", pNSISFile->uCRC32, u32CRCFile);
            rc = VERR_INVALID_PARAMETER;
        }
    }
    else
        RTMsgError("Error calculating checksum, rc=%Rrc\n", rc);

    return rc;
}

/**
 * Perform a repair of a given NSIS installer file.
 *
 * @returns VBox status code.
 * @param   pNSISFile           NSIS installer file to perform repair for.
 */
static int vboxNSISToolRepair(PVBOXNSISFILE pNSISFile)
{
    if (pNSISFile->fVerified) /* Successfully verified? Skip repair. */
        return VINF_SUCCESS;

    /* Write the CRC32 we already calculated when verifying the file in a former step. */
    return vboxNSISToolCRCWrite(pNSISFile, pNSISFile->uCRC32);
}

/**
 * Prints the syntax help.
 *
 * @returns RTEXITCODE_SYNTAX.
 * @param   argc                Number of arguments in \a argv.
 * @param   argv                Argument vector.
 */
static RTEXITCODE printHelp(int argc, char **argv)
{
    RT_NOREF(argc);

    RTPrintf("Syntax:\n\n");
    RTPrintf("    %s repair <NSIS installer file>\n", argv[0]);
    RTPrintf("    %s verify <NSIS installer file>\n", argv[0]);

    return RTEXITCODE_SYNTAX;
}

int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Initializing IPRT failed with %Rrc", rc);

    /* Keep it as simple as possible here currently. */
    RTEXITCODE  rcExit = RTEXITCODE_SUCCESS;
    char const *pszCmd = NULL;

    if (argc < 2)
    {
        rcExit = RTMsgErrorExit(RTEXITCODE_SYNTAX, "No command specified!\n");
    }
    else
    {
        pszCmd = argv[1];

        if (   RTStrICmp(pszCmd, "repair")
            && RTStrICmp(pszCmd, "verify"))
        {
            rcExit = RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid command specified!\n");
        }
        if (argc < 3)
            rcExit = RTMsgErrorExit(RTEXITCODE_SYNTAX, "No NSIS installer file specified!");
    }

    if (rcExit == RTEXITCODE_SYNTAX)
        return printHelp(argc, argv);

    char const *pszFile = argv[2];

    VBOXNSISFILE File;
    RT_ZERO(File);
    File.offHdr   = UINT64_MAX;
    File.offCRC32 = UINT64_MAX;

    rc = RTFileOpen(&File.hFile, pszFile, RTFILE_O_OPEN | RTFILE_O_READWRITE | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Could not open file '%s' (%Rrc)", pszFile, rc);

    rc = RTFileQuerySize(File.hFile, &File.cbFile);
    if (RT_SUCCESS(rc) && File.cbFile)
    {
        RTMsgInfo("Verifying file '%s' (%zu bytes) ...\n", pszFile, File.cbFile);

        rc = vboxNSISToolVerify(&File);
        if (RT_SUCCESS(rc))
        {
            if (!RTStrICmp(pszCmd, "verify"))
                RTMsgInfo("Verification successful\n");
            else
                RTMsgInfo("File is valid, no repair necessary\n");
        }
        else
        {
            if (rc == VERR_EOF)
                RTMsgError("Installer contains no data (empty)?!\n");
            else
            {
                if (!RTStrICmp(pszCmd, "repair"))
                {
                    RTMsgInfo("Verification failed, repairing ...\n");
                    rc = vboxNSISToolRepair(&File);
                    if (RT_SUCCESS(rc))
                        RTMsgInfo("Repair successful\n");
                }
                else
                    RTMsgError("Verification failed\n");
            }
        }
    }
    else if (RT_FAILURE(rc))
        RTMsgError("Could not query file size (%Rrc)", rc);
    else
    {
        RTMsgError("File size invalid (%zu bytes)", File.cbFile);
        rc = VERR_INVALID_EXE_SIGNATURE;
    }

    if (RT_FAILURE(rc))
        RTMsgError("Failed with %Rrc", rc);

    RTFileClose(File.hFile);
    return RT_SUCCESS(rc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

