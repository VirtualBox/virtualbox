/* $Id: VBoxCheckImports.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Checks that a windows image only imports from a given set of DLLs.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <iprt/formats/mz.h>
#include <iprt/formats/pecoff.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct
{
    const char *pszImage;
    FILE       *pFile;
    bool        f32Bit;
    union
    {
        IMAGE_NT_HEADERS32 Nt32;
        IMAGE_NT_HEADERS64 Nt64;
    } Hdrs;

    uint32_t                cSections;
    PIMAGE_SECTION_HEADER   paSections;
} MYIMAGE;


static bool Failed(MYIMAGE *pThis, const char *pszFmt, ...)
{
    va_list va;
    fprintf(stderr, "error '%s': ", pThis->pszImage);
    va_start(va, pszFmt);
    vfprintf(stderr, pszFmt, va);
    va_end(va);
    fprintf(stderr, "\n");
    return false;
}


static bool ReadPeHeaders(MYIMAGE *pThis)
{
    /*
     * MZ header.
     */
    IMAGE_DOS_HEADER MzHdr;
    if (fread(&MzHdr, sizeof(MzHdr), 1, pThis->pFile) != 1)
        return Failed(pThis, "Reading DOS header");

    if (MzHdr.e_magic != IMAGE_DOS_SIGNATURE)
        return Failed(pThis, "No MZ magic (found %#x)", MzHdr.e_magic);

    if (fseek(pThis->pFile, MzHdr.e_lfanew, SEEK_SET) != 0)
        return Failed(pThis, "Seeking to %#lx", (unsigned long)MzHdr.e_lfanew);

    /*
     * NT signature + file header.
     */
    if (fread(&pThis->Hdrs.Nt32, offsetof(IMAGE_NT_HEADERS32, OptionalHeader), 1, pThis->pFile) != 1)
        return Failed(pThis, "Reading NT file header");

    if (pThis->Hdrs.Nt32.Signature != IMAGE_NT_SIGNATURE)
        return Failed(pThis, "No PE magic (found %#x)", pThis->Hdrs.Nt32.Signature);

    if (pThis->Hdrs.Nt32.FileHeader.SizeOfOptionalHeader == sizeof(pThis->Hdrs.Nt32.OptionalHeader))
        pThis->f32Bit = true;
    else if (pThis->Hdrs.Nt32.FileHeader.SizeOfOptionalHeader == sizeof(pThis->Hdrs.Nt64.OptionalHeader))
        pThis->f32Bit = false;
    else
        return Failed(pThis, "Unsupported SizeOfOptionalHeaders value: %#x",
                      pThis->Hdrs.Nt32.FileHeader.SizeOfOptionalHeader);

    /*
     * NT optional header.
     */
    if (fread(&pThis->Hdrs.Nt32.OptionalHeader, pThis->Hdrs.Nt32.FileHeader.SizeOfOptionalHeader, 1, pThis->pFile) != 1)
        return Failed(pThis, "Reading NT optional header");

    if (   pThis->Hdrs.Nt32.OptionalHeader.Magic
        != (pThis->f32Bit ? IMAGE_NT_OPTIONAL_HDR32_MAGIC : IMAGE_NT_OPTIONAL_HDR64_MAGIC) )
        return Failed(pThis, "Bad optional header magic: %#x", pThis->Hdrs.Nt32.OptionalHeader.Magic);

    uint32_t NumberOfRvaAndSizes = pThis->f32Bit
                                 ? pThis->Hdrs.Nt32.OptionalHeader.NumberOfRvaAndSizes
                                 : pThis->Hdrs.Nt64.OptionalHeader.NumberOfRvaAndSizes;
    if (NumberOfRvaAndSizes != IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
        return Failed(pThis, "Unsupported NumberOfRvaAndSizes value: %#x", NumberOfRvaAndSizes);

    /*
     * Read the section table.
     */
    pThis->cSections  = pThis->Hdrs.Nt32.FileHeader.NumberOfSections;
    if (!pThis->cSections)
        return Failed(pThis, "No sections in image!");
    pThis->paSections = (PIMAGE_SECTION_HEADER)calloc(sizeof(pThis->paSections[0]), pThis->cSections);
    if (!pThis->paSections)
        return Failed(pThis, "Out of memory!");
    if (fread(pThis->paSections, sizeof(pThis->paSections[0]), pThis->cSections, pThis->pFile) != pThis->cSections)
        return Failed(pThis, "Reading NT section headers");


    return true;
}


static bool ReadAtRva(MYIMAGE *pThis, uint32_t uRva, void *pvBuf, size_t cbToRead)
{
    unsigned const uRvaOrg     = uRva;
    size_t   const cbToReadOrg = cbToRead;

    /*
     * Header section.
     */
    int      iSh        = -1;
    uint32_t uSectRva   = 0;
    uint32_t offSectRaw = 0;
    uint32_t cbSectRaw  = pThis->f32Bit
                        ? pThis->Hdrs.Nt32.OptionalHeader.SizeOfHeaders
                        : pThis->Hdrs.Nt64.OptionalHeader.SizeOfHeaders;
    uint32_t cbSectMax  = pThis->paSections[0].VirtualAddress;

    for (;;)
    {
        /* Read if we've got a match. */
        uint32_t off = uRva - uSectRva;
        if (off < cbSectMax)
        {
            uint32_t cbThis = cbSectMax - off;
            if (cbThis > cbToRead)
                cbThis = (uint32_t)cbToRead;

            memset(pvBuf, 0, cbThis);

            if (off < cbSectRaw)
            {
                if (fseek(pThis->pFile, offSectRaw + off, SEEK_SET) != 0)
                    return Failed(pThis, "Seeking to %#x", offSectRaw + off);
                if (fread(pvBuf, RT_MIN(cbThis, cbSectRaw - off), 1, pThis->pFile) != 1)
                    return Failed(pThis, "Reading %u bytes at %#x", RT_MIN(cbThis, cbSectRaw - off), offSectRaw + off);
            }

            cbToRead -= cbThis;
            if (!cbToRead)
                return true;
            uRva += cbThis;
            pvBuf = (uint8_t *)pvBuf + cbThis;
        }

        /* next section */
        iSh++;
        if ((unsigned)iSh >= pThis->cSections)
            return Failed(pThis, "RVA %#x LB %u is outside the image", uRvaOrg, cbToReadOrg);
        uSectRva   = pThis->paSections[iSh].VirtualAddress;
        offSectRaw = pThis->paSections[iSh].PointerToRawData;
        cbSectRaw  = pThis->paSections[iSh].SizeOfRawData;
        if ((unsigned)iSh + 1 < pThis->cSections)
            cbSectMax = pThis->paSections[iSh + 1].VirtualAddress - uSectRva;
        else
            cbSectMax = pThis->paSections[iSh].Misc.VirtualSize;
    }
}


static bool ReadStringAtRva(MYIMAGE *pThis, uint32_t uRva, char *pszBuf, size_t cbMax)
{
    uint32_t const uRvaOrg = uRva;

    /*
     * Try read the whole string at once.
     */
    uint32_t cbImage = pThis->f32Bit
                     ? pThis->Hdrs.Nt32.OptionalHeader.SizeOfImage
                     : pThis->Hdrs.Nt64.OptionalHeader.SizeOfImage;
    uint32_t cbThis = uRva < cbImage ? cbImage - uRva : 1;
    if (cbThis > cbMax)
        cbThis = (uint32_t)cbMax;
    if (!ReadAtRva(pThis, uRva, pszBuf, cbThis))
        return false;
    if (memchr(pszBuf, 0, cbThis) != NULL)
        return true;

    /*
     * Read more, byte-by-byte.
     */
    for (;;)
    {
        cbMax -= cbThis;
        if (!cbMax)
            return Failed(pThis, "String to long at %#x", uRvaOrg);
        pszBuf += cbThis;
        uRva   += cbThis;

        cbThis = 1;
        if (!ReadAtRva(pThis, uRva, pszBuf, cbThis))
            return false;
        if (!*pszBuf)
            return true;
    }
}


static void *ReadAtRvaAlloc(MYIMAGE *pThis, uint32_t uRva, size_t cbToRead)
{
    void *pvBuf = malloc(cbToRead);
    if (pvBuf)
    {
        if (ReadAtRva(pThis, uRva, pvBuf, cbToRead))
            return pvBuf;
        free(pvBuf);
    }
    else
        Failed(pThis, "Out of memory!");
    return NULL;
}


static bool ParseAndCheckImports(MYIMAGE *pThis,  const char **papszAllowed, unsigned cAllowed)
{
    /*
     * Do we have an import directory? If so, read it.
     */
    IMAGE_DATA_DIRECTORY ImpDir = pThis->f32Bit
                                ? pThis->Hdrs.Nt32.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
                                : pThis->Hdrs.Nt64.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (ImpDir.Size == 0)
        return true;

    uint32_t cImps = ImpDir.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
    if (cImps * sizeof(IMAGE_IMPORT_DESCRIPTOR) != ImpDir.Size)
        return Failed(pThis, "Import directory size is not a multiple of IMAGE_IMPORT_DESCRIPTOR: %#x", ImpDir.Size);

    PIMAGE_IMPORT_DESCRIPTOR paImps = (PIMAGE_IMPORT_DESCRIPTOR)ReadAtRvaAlloc(pThis, ImpDir.VirtualAddress, ImpDir.Size);
    if (!paImps)
        return false;

    /* There is usually an empty entry at the end. */
    if (   paImps[cImps - 1].Name       == 0
        || paImps[cImps - 1].FirstThunk == 0)
           cImps--;

    /*
     * Do the processing.
     */
    bool fRc = true;
    for (uint32_t i = 0; i < cImps; i++)
    {
        /* Read the import name string. */
        char szName[128];
        if (!ReadStringAtRva(pThis, paImps[i].Name, szName, sizeof(szName)))
        {
            fRc = false;
            break;
        }

        /* Check it against the list of allowed DLLs. */
        bool fFound = false;
        unsigned j = cAllowed;
        while (j-- > 0)
            if (stricmp(papszAllowed[j], szName) == 0)
            {
                fFound = true;
                break;
            }
        if (!fFound)
            fRc = Failed(pThis, "Illegal import: '%s'", szName);
    }

    free(paImps);
    return fRc;
}


static int usage(const char *argv0)
{
    printf("usage: %s --image <image> [allowed-dll [..]]\n", argv0);
    return RTEXITCODE_SUCCESS;
}


int main(int argc, char **argv)
{
    /*
     * Parse arguments.
     */
    const char  *pszImage     = NULL;
    const char **papszAllowed = (const char **)calloc(argc, sizeof(const char *));
    unsigned     cAllowed     = 0;

    for (int i = 1; i < argc; i++)
    {
        const char *psz = argv[i];
        if (*psz == '-')
        {
            if (!strcmp(psz, "--image") || !strcmp(psz, "-i"))
            {
                if (++i >= argc)
                {
                    fprintf(stderr, "syntax error: File name expected after '%s'.\n", psz);
                    return RTEXITCODE_SYNTAX;
                }
                pszImage = argv[i];
            }
            else if (   !strcmp(psz, "--help")
                     || !strcmp(psz, "-help")
                     || !strcmp(psz, "-h")
                     || !strcmp(psz, "-?") )
                return usage(argv[0]);
            else if (   !strcmp(psz, "--version")
                     || !strcmp(psz, "-V"))
            {
                printf("$Revision: 106320 $\n");
                return RTEXITCODE_SUCCESS;
            }
            else
            {
                fprintf(stderr, "syntax error: Unknown option '%s'.\n", psz);
                return RTEXITCODE_SYNTAX;
            }
        }
        else
            papszAllowed[cAllowed++] = argv[i];
    }

    if (!pszImage)
    {
        fprintf(stderr, "syntax error: No input file specified.\n");
        return RTEXITCODE_SYNTAX;
    }

    /*
     * Open the image and process headers.
     */
    RTEXITCODE  rcExit = RTEXITCODE_FAILURE;
    MYIMAGE     MyImage;
    memset(&MyImage, 0, sizeof(MyImage));
    MyImage.pszImage = pszImage;
    MyImage.pFile     = fopen(pszImage, "rb");
    if (MyImage.pFile)
    {
        if (   ReadPeHeaders(&MyImage)
            && ParseAndCheckImports(&MyImage, papszAllowed, cAllowed))
               rcExit = RTEXITCODE_SUCCESS;

        fclose(MyImage.pFile);
        free(MyImage.paSections);
    }
    else
        Failed(&MyImage, "Failed to open image for binary reading.");

    return rcExit;
}

