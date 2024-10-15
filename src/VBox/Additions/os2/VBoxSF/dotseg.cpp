/** $Id: dotseg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSF - OS/2 Shared Folders, NASM Object File Editor for DWARF segments.
 */

/*
 * Copyright (c) 2018 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */



/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/types.h>
#include <iprt/formats/omf.h>

#include <stdio.h>



int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "syntax error! Expected exactly one argument, found %d!\n", argc - 1);
        return 2;
    }
    const char *pszFilename = argv[1];

    /*
     * Open the file.
     */
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
    FILE *pFile = fopen(pszFilename, "r+b");
#else
    FILE *pFile = fopen(pszFilename, "r+b");
#endif
    if (!pFile)
    {
        fprintf(stderr,  "error opening '%s' for updating!\n", pszFilename);
        return 1;
    }

    /*
     * Parse the file.
     */
    uint32_t offRec = 0;
    while (!feof(pFile))
    {
        OMFRECHDR Hdr;
        if (fread(&Hdr, sizeof(Hdr), 1, pFile) != 1)
            break;
        /*fprintf(stderr, "dbg: %#07x: %02x %04x\n", offRec, Hdr.bType, Hdr.cbLen);*/
        uint8_t abData[OMF_MAX_RECORD_LENGTH];
        if (Hdr.cbLen > sizeof(abData))
        {
            fprintf(stderr, "%#07x: bad record: cbLen=%#x\n", offRec, Hdr.cbLen);
            return 1;
        }

        /* Is it interesting? */
        if (Hdr.bType == OMF_LNAMES)
        {
            /* Read the whole record. */
            if (fread(abData, Hdr.cbLen, 1, pFile) != 1)
                break;

            /* Scan it and make updates. */
            bool fUpdated = false;
            for (unsigned offData = 0; offData + 1 < Hdr.cbLen; )
            {
                uint8_t cchName = abData[offData++];
                if (offData + cchName + 1 > Hdr.cbLen)
                {
                    fprintf(stderr, "%#07x: bad LNAMES record (offData=3 + %#x)\n", offRec, offData);
                    return 1;
                }
                if (   cchName > 5
                    && abData[offData + 0] == '_'
                    && abData[offData + 1] == 'd'
                    && abData[offData + 2] == 'e'
                    && abData[offData + 3] == 'b'
                    && abData[offData + 4] == 'u'
                    && abData[offData + 5] == 'g')
                {
                    abData[offData] = '.';
                    fUpdated = true;
                }
                offData += cchName;
            }

            /* Write out updates. */
            if (fUpdated)
            {
                abData[Hdr.cbLen - 1] = 0; /* squash crc */
                if (   fseek(pFile, offRec + 3, SEEK_SET) != 0
                    || fwrite(abData, Hdr.cbLen, 1, pFile) != 1
                    || fseek(pFile, offRec + 3 + Hdr.cbLen, SEEK_SET) != 0)
                {
                    fprintf(stderr, "%#07x: error writing %#x bytes\n", offRec, Hdr.cbLen);
                    return 1;
                }
            }
        }
        /* Not interesting, so skip it and the CRC. */
        else if (fseek(pFile, Hdr.cbLen, SEEK_CUR) != 0)
        {
            fprintf(stderr, "%#07x: error skipping %#x bytes\n", offRec, Hdr.cbLen);
            return 1;
        }
        offRec += 3 + Hdr.cbLen;
    }

    if (ferror(pFile))
    {
        fprintf(stderr,  "read error\n");
        return 1;
    }
    if (fclose(pFile) != 0)
    {
        fprintf(stderr,  "error flush/closing file\n");
        return 1;
    }
    return 0;
}

