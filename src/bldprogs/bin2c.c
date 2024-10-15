/* $Id: bin2c.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * bin2c - Binary 2 C Structure Converter.
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
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>


/**
 * File size.
 *
 * @returns file size in bytes.
 * @returns 0 on failure.
 * @param   pFile   File to size.
 */
static size_t fsize(FILE *pFile)
{
    long    cbFile;
    off_t   Pos = ftell(pFile);
    if (    Pos >= 0
        &&  !fseek(pFile, 0, SEEK_END))
    {
        cbFile = ftell(pFile);
        if (    cbFile >= 0
            &&  !fseek(pFile, 0, SEEK_SET))
            return cbFile;
    }
    return 0;
}

static int usage(const char *argv0)
{
    fprintf(stderr,
            "Syntax: %s [options] <arrayname> <binaryfile> <outname>\n"
            "  --min <n>    check if <binaryfile> is not smaller than <n>KB\n"
            "  --max <n>    check if <binaryfile> is not bigger than <n>KB\n"
            "  --mask <n>   check if size of binaryfile is <n>-aligned\n"
            "  --width <n>  number of bytes per line (default: 16)\n"
            "  --break <n>  break every <n> lines    (default: -1)\n"
            , argv0);
    fprintf(stderr,
            "  --ascii      show ASCII representation of binary as comment\n"
            "  --export     emit DECLEXPORT\n"
            "  --append     append to the output file (default: truncate)\n"
            "  --no-size    Skip the size.\n"
            "  --static     Static data scope.\n");

    return 1;
}

int main(int argc, char *argv[])
{
    FILE          *pFileIn;
    FILE          *pFileOut;
    int           iArg;
    size_t        cbMin = 0;
    size_t        cbMax = ~0U;
    size_t        uMask = 0;
    int           fAscii = 0;
    int           fAppend = 0;
    int           fExport = 0;
    int           fNoSize = 0;
    int           fStatic = 0;
    long          iBreakEvery = -1;
    unsigned char abLine[32];
    size_t        cbLine = 16;
    size_t        off;
    size_t        cbRead;
    size_t        cbBin;
    int           rc = 1;               /* assume the worst... */

    if (argc < 2)
        return usage(argv[0]);

    for (iArg = 1; iArg < argc; iArg++)
    {
        if (!strcmp(argv[iArg], "--min") || !strcmp(argv[iArg], "-min"))
        {
            if (++iArg >= argc)
                return usage(argv[0]);
            cbMin = 1024 * strtoul(argv[iArg], NULL, 0);
        }
        else if (!strcmp(argv[iArg], "--max") || !strcmp(argv[iArg], "-max"))
        {
            if (++iArg >= argc)
                return usage(argv[0]);
            cbMax = 1024 * strtoul(argv[iArg], NULL, 0);
        }
        else if (!strcmp(argv[iArg], "--mask") || !strcmp(argv[iArg], "-mask"))
        {
            if (++iArg >= argc)
                return usage(argv[0]);
            uMask = strtoul(argv[iArg], NULL, 0);
        }
        else if (!strcmp(argv[iArg], "--ascii") || !strcmp(argv[iArg], "-ascii"))
            fAscii = 1;
        else if (!strcmp(argv[iArg], "--append"))
            fAppend = 1;
        else if (!strcmp(argv[iArg], "--export") || !strcmp(argv[iArg], "-export"))
            fExport = 1;
        else if (!strcmp(argv[iArg], "--no-size"))
            fNoSize = 1;
        else if (!strcmp(argv[iArg], "--static"))
            fStatic = 1;
        else if (!strcmp(argv[iArg], "--width") || !strcmp(argv[iArg], "-width"))
        {
            if (++iArg >= argc)
                return usage(argv[0]);
            cbLine = strtoul(argv[iArg], NULL, 0);
            if (cbLine == 0 || cbLine > sizeof(abLine))
            {
                fprintf(stderr, "%s: '%s' is too wide, max %u\n",
                        argv[0], argv[iArg], (unsigned)sizeof(abLine));
                return 1;
            }
        }
        else if (!strcmp(argv[iArg], "--break") || !strcmp(argv[iArg], "-break"))
        {
            if (++iArg >= argc)
                return usage(argv[0]);
            iBreakEvery = strtol(argv[iArg], NULL, 0);
            if (iBreakEvery <= 0 && iBreakEvery != -1)
            {
                fprintf(stderr, "%s: -break value '%s' is not >= 1 or -1.\n",
                        argv[0], argv[iArg]);
                return 1;
            }
        }
        else if (iArg == argc - 3)
            break;
        else
        {
            fprintf(stderr, "%s: syntax error: Unknown argument '%s'\n",
                    argv[0], argv[iArg]);
            return usage(argv[0]);
        }
    }

    pFileIn = fopen(argv[iArg+1], "rb");
    if (!pFileIn)
    {
        fprintf(stderr, "Error: failed to open input file '%s'!\n", argv[iArg+1]);
        return 1;
    }

    pFileOut = fopen(argv[iArg+2], fAppend ? "a" : "w"); /* no b! */
    if (!pFileOut)
    {
        fprintf(stderr, "Error: failed to open output file '%s'!\n", argv[iArg+2]);
        fclose(pFileIn);
        return 1;
    }

    cbBin = fsize(pFileIn);

    fprintf(pFileOut,
           "/*\n"
           " * This file was automatically generated\n"
           " * from %s\n"
           " * by %s.\n"
           " */\n"
           "\n"
           "#include <iprt/cdefs.h>\n"
           "\n"
           "%sconst unsigned char%s g_ab%s[] =\n"
           "{\n",
           argv[iArg+1], argv[0], fStatic ? "static " : fExport ? "DECLEXPORT(" : "", !fStatic && fExport ? ")" : "", argv[iArg]);

    /* check size restrictions */
    if (uMask && (cbBin & uMask))
        fprintf(stderr, "%s: size=%ld - Not aligned!\n", argv[0], (long)cbBin);
    else if (cbBin < cbMin || cbBin > cbMax)
        fprintf(stderr, "%s: size=%ld - Not %ld-%ldb in size!\n",
                argv[0], (long)cbBin, (long)cbMin, (long)cbMax);
    else
    {
        /* the binary data */
        off = 0;
        while ((cbRead = fread(&abLine[0], 1, cbLine, pFileIn)) > 0)
        {
            size_t j;

            if (    iBreakEvery > 0
                &&  off
                && (off / cbLine) % iBreakEvery == 0)
                fprintf(pFileOut, "\n");

            fprintf(pFileOut, "   ");
            for (j = 0; j < cbRead; j++)
                fprintf(pFileOut, " 0x%02x,", abLine[j]);
            for (; j < cbLine; j++)
                fprintf(pFileOut, "      ");
            if (fAscii)
            {
                fprintf(pFileOut, " /* 0x%08lx: ", (long)off);
                for (j = 0; j < cbRead; j++)
                    /* be careful with '/' prefixed/followed by a '*'! */
                    fprintf(pFileOut, "%c",
                            isprint(abLine[j]) && abLine[j] != '/' ? abLine[j] : '.');
                for (; j < cbLine; j++)
                    fprintf(pFileOut, " ");
                fprintf(pFileOut, " */");
            }
            fprintf(pFileOut, "\n");

            off += cbRead;
        }

        /* check for errors */
        if (ferror(pFileIn) && !feof(pFileIn))
            fprintf(stderr, "%s: read error\n", argv[0]);
        else if (off != cbBin)
            fprintf(stderr, "%s: read error off=%ld cbBin=%ld\n", argv[0], (long)off, (long)cbBin);
        else
        {
            /* no errors, finish the structure. */
            fprintf(pFileOut,
                    "};\n");

            if (!fNoSize)
                fprintf(pFileOut,
                        "\n"
                        "%sconst unsigned%s g_cb%s = sizeof(g_ab%s);\n",
                        fExport ? "DECLEXPORT(" : "", fExport ? ")" : "", argv[iArg], argv[iArg]);

            fprintf(pFileOut, "/* end of file */\n");

            /* flush output and check for error. */
            fflush(pFileOut);
            if (ferror(pFileOut))
                fprintf(stderr, "%s: write error\n", argv[0]);
            else
                rc = 0; /* success! */
        }
    }

    /* cleanup, delete the output file on failure. */
    fclose(pFileOut);
    fclose(pFileIn);
    if (rc)
        remove(argv[iArg+2]);

    return rc;
}
