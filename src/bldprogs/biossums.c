/* $Id: biossums.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Tool for modifying a BIOS image to write the BIOS checksum.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#ifndef RT_OS_WINDOWS
# include <unistd.h> /* unlink */
#endif

typedef unsigned char uint8_t;

static uint8_t abBios[64*1024];
static FILE *g_pIn = NULL;
static FILE *g_pOut = NULL;
static const char *g_pszOutFile = NULL;
static const char *g_argv0;

/**
 * Find where the filename starts in the given path.
 */
static const char *name(const char *pszPath)
{
    const char *psz = strrchr(pszPath, '/');
#if defined(_MSC_VER) || defined(__OS2__)
    const char *psz2 = strrchr(pszPath, '\\');
    if (!psz2)
        psz2 = strrchr(pszPath, ':');
    if (psz2 && (!psz || psz2 > psz))
        psz = psz2;
#endif
    return psz ? psz + 1 : pszPath;
}

/**
 * Report an error.
 */
static int fatal(const char *pszFormat, ...)
{
    va_list va;

    fprintf(stderr, "%s: ", name(g_argv0));

    va_start(va, pszFormat);
    vfprintf(stderr, pszFormat, va);
    va_end(va);

    /* clean up */
    if (g_pIn)
        fclose(g_pIn);
    if (g_pOut)
        fclose(g_pOut);
    if (g_pszOutFile)
        unlink(g_pszOutFile);

    return 1;
}

/**
 * Calculate the checksum.
 */
static uint8_t calculateChecksum(uint8_t *pb, size_t cb, size_t iChecksum)
{
    uint8_t u8Sum = 0;
    size_t  i;

    for (i = 0; i < cb; i++)
        if (i != iChecksum)
            u8Sum += pb[i];

    return -u8Sum;
}

/**
 * Find a header in the binary.
 *
 * @param   pb        Where to search for the signature
 * @param   cb        Size of the search area
 * @param   pbHeader  Pointer to the start of the signature
 * @returns           0 if signature was not found, 1 if found or
 *                    2 if more than one signature was found */
static int searchHeader(uint8_t *pb, size_t cb, const char *pszHeader, uint8_t **pbHeader)
{
    int          fFound = 0;
    unsigned int i;
    size_t       cbSignature = strlen(pszHeader);

    for (i = 0; i < cb; i += 16)
        if (!memcmp(pb + i, pszHeader, cbSignature))
        {
            if (fFound++)
                return 2;
            *pbHeader = pb + i;
        }

    return fFound;
}

int main(int argc, char **argv)
{
    FILE    *pIn, *pOut;
    size_t  cbIn, cbOut;
    int     fAdapterBios    = 0;
    int     fPIRQRequired   = 0;
    int     fBIOS32Required = 0;
    int     fSMBIOSRequired = 0;

    g_argv0 = argv[0];

    while ((argc > 1) && (argv[1][0] == '-'))
    {
        if (!strcmp(argv[1], "-3"))
            fBIOS32Required = 1;
        else if (!strcmp(argv[1], "-p"))
            fPIRQRequired = 1;
        else if (!strcmp(argv[1], "-s"))
            fSMBIOSRequired = 1;
        else
            return fatal("Invalid parameter: %s\n", argv[1]);

        argc--;
        argv++;
    }

    if (argc != 3)
        return fatal("Input file name and output file name required.\n");

    pIn = g_pIn = fopen(argv[1], "rb");
    if (!pIn)
        return fatal("Error opening '%s' for reading (%s).\n", argv[1], strerror(errno));

    pOut = g_pOut = fopen(argv[2], "wb");
    if (!pOut)
        return fatal("Error opening '%s' for writing (%s).\n", argv[2], strerror(errno));
    g_pszOutFile = argv[2];

    /* safety precaution (aka. complete paranoia :-) */
    memset(abBios, 0, sizeof(abBios));

    cbIn = fread(abBios, 1, sizeof(abBios), pIn);
    if (ferror(pIn))
        return fatal("Error reading from '%s' (%s).\n", argv[1], strerror(errno));
    g_pIn = NULL;
    fclose(pIn);

    fAdapterBios = abBios[0] == 0x55 && abBios[1] == 0xaa;

    /* align size to page size */
    if ((cbIn % 4096) != 0)
        cbIn = (cbIn + 4095) & ~4095;

    if (!fAdapterBios && cbIn != 64*1024)
        return fatal("Size of system BIOS is not 64KB!\n");

    if (fAdapterBios)
    {
        /* adapter BIOS */

        /* set the length indicator */
        abBios[2] = (uint8_t)(cbIn / 512);
    }
    else
    {
        /* system BIOS */
        size_t  cbChecksum;
        uint8_t u8Checksum;
        uint8_t *pbHeader;

        /* Set the BIOS32 header checksum. */
        switch (searchHeader(abBios, cbIn, "_32_", &pbHeader))
        {
            case 0:
                if (fBIOS32Required)
                    return fatal("No BIOS32 header found!\n");
                else
                    break;
            case 2:
                return fatal("More than one BIOS32 header found!\n");
            case 1:
                cbChecksum = (size_t)pbHeader[9] * 16;
                u8Checksum = calculateChecksum(pbHeader, cbChecksum, 10);
                pbHeader[10] = u8Checksum;
                break;
        }

        /* Set the PIR header checksum according to PCI IRQ Routing table
         * specification version 1.0, Microsoft Corporation, 1996 */
        switch (searchHeader(abBios, cbIn, "$PIR", &pbHeader))
        {
            case 0:
                if (fPIRQRequired)
                    return fatal("No PCI IRQ routing table found!\n");
                else
                    break;
            case 2:
                return fatal("More than one PCI IRQ routing table found!\n");
            case 1:
                cbChecksum = (size_t)pbHeader[6] + (size_t)pbHeader[7] * 256;
                u8Checksum = calculateChecksum(pbHeader, cbChecksum, 31);
                pbHeader[31] = u8Checksum;
                break;
        }

        /* Set the SMBIOS header checksum according to System Management BIOS
         * Reference Specification Version 2.5, DSP0134. */
        switch (searchHeader(abBios, cbIn, "_SM_", &pbHeader))
        {
            case 0:
                if (fBIOS32Required)
                    return fatal("No SMBIOS header found!\n");
                else
                    break;
            case 2:
                return fatal("More than one SMBIOS header found!\n");
            case 1:
                /* at first fix the DMI header starting at SMBIOS header offset 16 */
                u8Checksum = calculateChecksum(pbHeader+16, 15, 5);
                pbHeader[21] = u8Checksum;

                /* now fix the checksum of the whole SMBIOS header */
                cbChecksum = (size_t)pbHeader[5];
                u8Checksum = calculateChecksum(pbHeader, cbChecksum, 4);
                pbHeader[4] = u8Checksum;
                break;
        }

        /* If there is a VPD table, adjust its checksum. */
        switch (searchHeader(abBios, cbIn, "\xAA\x55VPD", &pbHeader))
        {
            case 0:
                break;  /* VPD is optional */
            case 2:
                return fatal("More than one VPD header found!\n");
            case 1:
                cbChecksum = (size_t)pbHeader[5];
                if (cbChecksum < 0x30)
                    return fatal("VPD size too small!\n");
                u8Checksum = calculateChecksum(pbHeader, cbChecksum, cbChecksum - 1);
                pbHeader[cbChecksum - 1] = u8Checksum;
                break;
        }
    }

    /* set the BIOS checksum */
    abBios[cbIn-1] = calculateChecksum(abBios, cbIn, cbIn - 1);

    cbOut = fwrite(abBios, 1, cbIn, pOut);
    if (ferror(pOut))
        return fatal("Error writing to '%s' (%s).\n", g_pszOutFile, strerror(errno));
    g_pOut = NULL;
    if (fclose(pOut))
        return fatal("Error closing '%s' (%s).\n", g_pszOutFile, strerror(errno));

    return 0;
}

