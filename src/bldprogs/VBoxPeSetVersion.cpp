/* $Id: VBoxPeSetVersion.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Change the OS and SubSystem version to value suitable for NT v3.1.
 *
 * Also make sure the IAT is writable, since NT v3.1 expects this.  These are
 * tricks necessary to make binaries created by newer Visual C++ linkers work
 * on ancient NT version like W2K, NT4 and NT 3.x.
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
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define MK_VER(a_uHi, a_uLo)  ( ((a_uHi) << 8) | (a_uLo))


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char *g_pszFilename;
static unsigned    g_cVerbosity = 0;
static enum { kUpdateCheckSum_Never, kUpdateCheckSum_WhenNeeded, kUpdateCheckSum_Always, kUpdateCheckSum_Zero }
                   g_enmUpdateChecksum = kUpdateCheckSum_WhenNeeded;


static int Error(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    char szTmp[1024];
    _vsnprintf(szTmp, sizeof(szTmp), pszFormat, va);
    va_end(va);
    fprintf(stderr, "VBoxPeSetVersion: %s: error: %s\n", g_pszFilename, szTmp);
    return RTEXITCODE_FAILURE;
}


static void Info(unsigned iLevel, const char *pszFormat, ...)
{
    if (iLevel <= g_cVerbosity)
    {
        va_list va;
        va_start(va, pszFormat);
        char szTmp[1024];
        _vsnprintf(szTmp, sizeof(szTmp), pszFormat, va);
        va_end(va);
        fprintf(stderr, "VBoxPeSetVersion: %s: info: %s\n", g_pszFilename, szTmp);
    }
}


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


/**
 * Calculates a raw PE-style checksum on a plain buffer.
 *
 * ASSUMES pvBuf is dword aligned.
 */
static uint16_t CalcRawPeChecksum(void const *pvBuf, size_t cb, uint32_t uChksum)
{
    /*
     * Work thru the memory in 64 bits at a time.
     * ASSUMES well aligned input.
     */
    uint64_t        uBigSum = uChksum;
    uint64_t const *pu64    = (uint64_t const *)pvBuf;
    size_t          cQWords = cb / 8;
    while (cQWords-- > 0)
    {
        /* We emulate add with carry here. */
        uint64_t uTmp = uBigSum + *pu64++;
        uBigSum = uTmp >= uBigSum ? uTmp : uTmp + 1;
    }

    /*
     * Zeropadd any remaining bytes before adding them.
     */
    if (cb & 7)
    {
        uint8_t const *pb     = (uint8_t const *)pu64;
        uint64_t       uQWord = 0;
        switch (cb & 7)
        {
            case 7: uQWord |= (uint64_t)pb[6] << 48; RT_FALL_THRU();
            case 6: uQWord |= (uint64_t)pb[5] << 40; RT_FALL_THRU();
            case 5: uQWord |= (uint64_t)pb[4] << 32; RT_FALL_THRU();
            case 4: uQWord |= (uint64_t)pb[3] << 24; RT_FALL_THRU();
            case 3: uQWord |= (uint64_t)pb[2] << 16; RT_FALL_THRU();
            case 2: uQWord |= (uint64_t)pb[1] <<  8; RT_FALL_THRU();
            case 1: uQWord |= (uint64_t)pb[0]; break;
        }

        uint64_t uTmp = uBigSum + uQWord;
        uBigSum = uTmp >= uBigSum ? uTmp : uTmp + 1;
    }

    /*
     * Convert the 64-bit checksum to a 16-bit one.
     */
    uChksum =  (uBigSum        & 0xffffU)
            + ((uBigSum >> 16) & 0xffffU)
            + ((uBigSum >> 32) & 0xffffU)
            + ((uBigSum >> 48) & 0xffffU);
    uChksum = (uChksum         & 0xffffU)
            + (uChksum >> 16);
    if (uChksum > 0xffffU) Error("Checksum IPE#1");
    return uChksum & 0xffffU;
}


static int UpdateChecksum(FILE *pFile)
{
    /*
     * Read the whole file into memory.
     */
    size_t const    cbFile = fsize(pFile);
    if (!cbFile)
        return Error("Failed to determine file size: %s", strerror(errno));
    uint8_t * const pbFile = (uint8_t *)malloc(cbFile + 4);
    if (!pbFile)
        return Error("Failed to allocate %#lx bytes for checksum calculations", (unsigned long)(cbFile + 4U));
    memset(pbFile, 0, cbFile + 4);

    int        rcExit;
    size_t     cItemsRead = fread(pbFile, cbFile, 1, pFile);
    if (cItemsRead == 1)
    {
        /*
         * Locate the NT headers as we need the CheckSum field in order to update it.
         * It has the same location in 32-bit and 64-bit images.
         */
        IMAGE_DOS_HEADER const   * const pMzHdr  = (IMAGE_DOS_HEADER const *)pbFile;
        AssertCompileMembersAtSameOffset(IMAGE_NT_HEADERS32, OptionalHeader.CheckSum, IMAGE_NT_HEADERS64, OptionalHeader.CheckSum);
        IMAGE_NT_HEADERS32 * const       pNtHdrs
            = (IMAGE_NT_HEADERS32 *)&pbFile[pMzHdr->e_magic == IMAGE_DOS_SIGNATURE ? pMzHdr->e_lfanew : 0];
        if ((uintptr_t)&pNtHdrs->OptionalHeader.DataDirectory[0] - (uintptr_t)pbFile < cbFile)
        {
            if (   pNtHdrs->Signature == IMAGE_NT_SIGNATURE
                && (   pNtHdrs->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC
                    || pNtHdrs->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC))
            {
                /*
                 * Set the checksum field to zero to avoid having to do tedious
                 * adjustments of the raw checksum. Then calculate the raw check sum.
                 */
                pNtHdrs->OptionalHeader.CheckSum = 0;
                uint32_t uChksum = CalcRawPeChecksum(pbFile, RT_ALIGN_Z(cbFile, 2), 0);

                /* Finalize the checksum by adding the image size to it. */
                uChksum += (uint32_t)cbFile;
                pNtHdrs->OptionalHeader.CheckSum = uChksum;

                /*
                 * Write back the checksum to the file.
                 */
                size_t const offChecksumField = (uintptr_t)&pNtHdrs->OptionalHeader.CheckSum - (uintptr_t)pbFile;

                if (fseek(pFile, (long)offChecksumField, SEEK_SET) == 0)
                {
                    if (fwrite(&pNtHdrs->OptionalHeader.CheckSum, sizeof(pNtHdrs->OptionalHeader.CheckSum), 1, pFile) == 1)
                    {
                        Info(1, "Checksum: %#x", uChksum);
                        rcExit = RTEXITCODE_SUCCESS;
                    }
                    else
                        rcExit = Error("Checksum write failed");
                }
                else
                    rcExit = Error("Failed seeking to %#lx for checksum write", (long)offChecksumField);
            }
            else
                rcExit = Error("PE header not found");
        }
        else
            rcExit = Error("NT headers not within file when checksumming");
    }
    else
        rcExit = Error("Failed read in file (%#lx bytes) for checksum calculations", (unsigned long)(cbFile + 4U));

    free(pbFile);
    return rcExit;
}


static int UpdateFile(FILE *pFile, unsigned uNtVersion, PIMAGE_SECTION_HEADER *ppaShdr)
{
    unsigned cFileModifications = 0;

    /*
     * Locate and read the PE header.
     *
     * Note! We'll be reading the 64-bit size even for 32-bit since the difference
     *       is 16 bytes, which is less than a section header, so it won't be a problem.
     */
    unsigned long offNtHdrs;
    {
        IMAGE_DOS_HEADER MzHdr;
        if (fread(&MzHdr, sizeof(MzHdr), 1, pFile) != 1)
            return Error("Failed to read MZ header: %s", strerror(errno));
        if (MzHdr.e_magic != IMAGE_DOS_SIGNATURE)
            return Error("Invalid MZ magic: %#x", MzHdr.e_magic);
        offNtHdrs = MzHdr.e_lfanew;
    }

    if (fseek(pFile, offNtHdrs, SEEK_SET) != 0)
        return Error("Failed to seek to PE header at %#lx: %s", offNtHdrs, strerror(errno));
    union
    {
        IMAGE_NT_HEADERS32 x32;
        IMAGE_NT_HEADERS64 x64;
    }   NtHdrs,
        NtHdrsNew;
    if (fread(&NtHdrs, sizeof(NtHdrs), 1, pFile) != 1)
        return Error("Failed to read PE header at %#lx: %s", offNtHdrs, strerror(errno));

    /*
     * Validate it a little bit.
     */
    if (NtHdrs.x32.Signature != IMAGE_NT_SIGNATURE)
        return Error("Invalid PE signature: %#x", NtHdrs.x32.Signature);
    uint32_t cbNewHdrs;
    if (NtHdrs.x32.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64)
    {
        if (NtHdrs.x64.FileHeader.SizeOfOptionalHeader != sizeof(NtHdrs.x64.OptionalHeader))
            return Error("Invalid optional header size: %#x", NtHdrs.x64.FileHeader.SizeOfOptionalHeader);
        if (NtHdrs.x64.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            return Error("Invalid optional header magic: %#x", NtHdrs.x64.OptionalHeader.Magic);
        if (!uNtVersion)
            uNtVersion = MK_VER(5, 2);
        else if (uNtVersion < MK_VER(5, 2))
            return Error("Selected version is too old for AMD64: %u.%u", uNtVersion >> 8, uNtVersion & 0xff);
        cbNewHdrs = sizeof(NtHdrsNew.x64);
    }
    else if (NtHdrs.x32.FileHeader.Machine != IMAGE_FILE_MACHINE_I386)
        return Error("Not I386 or AMD64 machine: %#x", NtHdrs.x32.FileHeader.Machine);
    else
    {
        if (NtHdrs.x32.FileHeader.SizeOfOptionalHeader != sizeof(NtHdrs.x32.OptionalHeader))
            return Error("Invalid optional header size: %#x", NtHdrs.x32.FileHeader.SizeOfOptionalHeader);
        if (NtHdrs.x32.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            return Error("Invalid optional header magic: %#x", NtHdrs.x32.OptionalHeader.Magic);
        if (!uNtVersion)
            uNtVersion = MK_VER(3, 10);
        cbNewHdrs = sizeof(NtHdrsNew.x32);
    }

    /*
     * Do the header modifications.
     */
    memcpy(&NtHdrsNew, &NtHdrs, sizeof(NtHdrsNew));
    NtHdrsNew.x32.OptionalHeader.MajorOperatingSystemVersion = uNtVersion >> 8;
    NtHdrsNew.x32.OptionalHeader.MinorOperatingSystemVersion = uNtVersion & 0xff;
    NtHdrsNew.x32.OptionalHeader.MajorSubsystemVersion       = uNtVersion >> 8;
    NtHdrsNew.x32.OptionalHeader.MinorSubsystemVersion       = uNtVersion & 0xff;
    AssertCompileMembersAtSameOffset(IMAGE_NT_HEADERS32, OptionalHeader.MajorOperatingSystemVersion,  IMAGE_NT_HEADERS64, OptionalHeader.MajorOperatingSystemVersion);
    AssertCompileMembersAtSameOffset(IMAGE_NT_HEADERS32, OptionalHeader.MinorOperatingSystemVersion,  IMAGE_NT_HEADERS64, OptionalHeader.MinorOperatingSystemVersion);
    AssertCompileMembersAtSameOffset(IMAGE_NT_HEADERS32, OptionalHeader.MajorSubsystemVersion,        IMAGE_NT_HEADERS64, OptionalHeader.MajorSubsystemVersion);
    AssertCompileMembersAtSameOffset(IMAGE_NT_HEADERS32, OptionalHeader.MinorSubsystemVersion,        IMAGE_NT_HEADERS64, OptionalHeader.MinorSubsystemVersion);

    if (uNtVersion <= MK_VER(3, 50))
    {
        NtHdrsNew.x32.OptionalHeader.MajorOperatingSystemVersion = 1;
        NtHdrsNew.x32.OptionalHeader.MinorOperatingSystemVersion = 0;
        AssertCompileMembersAtSameOffset(IMAGE_NT_HEADERS32, OptionalHeader.MajorOperatingSystemVersion, IMAGE_NT_HEADERS64, OptionalHeader.MajorOperatingSystemVersion);
        AssertCompileMembersAtSameOffset(IMAGE_NT_HEADERS32, OptionalHeader.MinorOperatingSystemVersion, IMAGE_NT_HEADERS64, OptionalHeader.MinorOperatingSystemVersion);
    }

    if (memcmp(&NtHdrsNew, &NtHdrs, sizeof(NtHdrs)))
    {
        /** @todo calc checksum. */
        NtHdrsNew.x32.OptionalHeader.CheckSum = 0;
        AssertCompileMembersAtSameOffset(IMAGE_NT_HEADERS32, OptionalHeader.MinorOperatingSystemVersion, IMAGE_NT_HEADERS64, OptionalHeader.MinorOperatingSystemVersion);

        if (   NtHdrsNew.x32.OptionalHeader.MajorOperatingSystemVersion != NtHdrs.x32.OptionalHeader.MajorOperatingSystemVersion
            || NtHdrsNew.x32.OptionalHeader.MinorOperatingSystemVersion != NtHdrs.x32.OptionalHeader.MinorOperatingSystemVersion)
            Info(1,"OperatingSystemVersion %u.%u -> %u.%u",
                 NtHdrs.x32.OptionalHeader.MajorOperatingSystemVersion, NtHdrs.x32.OptionalHeader.MinorOperatingSystemVersion,
                 NtHdrsNew.x32.OptionalHeader.MajorOperatingSystemVersion, NtHdrsNew.x32.OptionalHeader.MinorOperatingSystemVersion);
        if (   NtHdrsNew.x32.OptionalHeader.MajorSubsystemVersion != NtHdrs.x32.OptionalHeader.MajorSubsystemVersion
            || NtHdrsNew.x32.OptionalHeader.MinorSubsystemVersion != NtHdrs.x32.OptionalHeader.MinorSubsystemVersion)
            Info(1,"SubsystemVersion %u.%u -> %u.%u",
                 NtHdrs.x32.OptionalHeader.MajorSubsystemVersion, NtHdrs.x32.OptionalHeader.MinorSubsystemVersion,
                 NtHdrsNew.x32.OptionalHeader.MajorSubsystemVersion, NtHdrsNew.x32.OptionalHeader.MinorSubsystemVersion);

        if (fseek(pFile, offNtHdrs, SEEK_SET) != 0)
            return Error("Failed to seek to PE header at %#lx: %s", offNtHdrs, strerror(errno));
        if (fwrite(&NtHdrsNew, cbNewHdrs, 1, pFile) != 1)
            return Error("Failed to write PE header at %#lx: %s", offNtHdrs, strerror(errno));
        cFileModifications++;
    }
    else
        Info(3, "No header changes");

    /*
     * Make the IAT writable for NT 3.1 and drop the non-cachable flag from .bss.
     *
     * The latter is a trick we use to prevent the linker from merging .data and .bss,
     * because NT 3.1 does not honor Misc.VirtualSize and won't zero padd the .bss part
     * if it's not zero padded in the file.  This seemed simpler than adding zero padding.
     */
    if (   uNtVersion <= MK_VER(3, 10)
        && NtHdrsNew.x32.FileHeader.NumberOfSections > 0)
    {
        uint32_t              cbShdrs = sizeof(IMAGE_SECTION_HEADER) * NtHdrsNew.x32.FileHeader.NumberOfSections;
        PIMAGE_SECTION_HEADER paShdrs = (PIMAGE_SECTION_HEADER)calloc(1, cbShdrs);
        if (!paShdrs)
            return Error("Out of memory");
        *ppaShdr = paShdrs;

        unsigned long offShdrs = offNtHdrs
                               + RT_UOFFSETOF_DYN(IMAGE_NT_HEADERS32,
                                                  OptionalHeader.DataDirectory[NtHdrsNew.x32.OptionalHeader.NumberOfRvaAndSizes]);
        if (fseek(pFile, offShdrs, SEEK_SET) != 0)
            return Error("Failed to seek to section headers at %#lx: %s", offShdrs, strerror(errno));
        if (fread(paShdrs, cbShdrs, 1, pFile) != 1)
            return Error("Failed to read section headers at %#lx: %s", offShdrs, strerror(errno));

        bool     fFoundBss = false;
        uint32_t uRvaEnd   = NtHdrsNew.x32.OptionalHeader.SizeOfImage;
        uint32_t uRvaIat   = NtHdrsNew.x32.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size > 0
                           ? NtHdrsNew.x32.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress : UINT32_MAX;
        uint32_t i         = NtHdrsNew.x32.FileHeader.NumberOfSections;
        while (i-- > 0)
            if (!(paShdrs[i].Characteristics & IMAGE_SCN_TYPE_NOLOAD))
            {
                bool     fModified = false;
                if (uRvaIat >= paShdrs[i].VirtualAddress && uRvaIat < uRvaEnd)
                {
                    if (!(paShdrs[i].Characteristics & IMAGE_SCN_MEM_WRITE))
                    {
                        paShdrs[i].Characteristics |= IMAGE_SCN_MEM_WRITE;
                        fModified = true;
                    }
                    uRvaIat = UINT32_MAX;
                }

                if (   !fFoundBss
                    && strcmp((const char *)paShdrs[i].Name, ".bss") == 0)
                {
                    if (paShdrs[i].Characteristics & IMAGE_SCN_MEM_NOT_CACHED)
                    {
                        paShdrs[i].Characteristics &= ~IMAGE_SCN_MEM_NOT_CACHED;
                        fModified = true;
                    }
                    fFoundBss = true;
                }

                if (fModified)
                {
                    unsigned long offShdr = offShdrs + i * sizeof(IMAGE_SECTION_HEADER);
                    if (fseek(pFile, offShdr, SEEK_SET) != 0)
                        return Error("Failed to seek to section header #%u at %#lx: %s", i, offShdr, strerror(errno));
                    if (fwrite(&paShdrs[i], sizeof(IMAGE_SECTION_HEADER), 1, pFile) != 1)
                        return Error("Failed to write %8.8s section header header at %#lx: %s",
                                     paShdrs[i].Name, offShdr, strerror(errno));
                    cFileModifications++;
                    if (uRvaIat == UINT32_MAX && fFoundBss)
                        break;
                }

                /* Advance */
                uRvaEnd = paShdrs[i].VirtualAddress;
            }
    }

    /*
     * Recalculate the checksum if we changed anything or if it is zero.
     */
    if (   g_enmUpdateChecksum == kUpdateCheckSum_Always
        || (   g_enmUpdateChecksum == kUpdateCheckSum_WhenNeeded
            && (cFileModifications || NtHdrsNew.x32.OptionalHeader.CheckSum == 0)))
        return UpdateChecksum(pFile);

    /* Zero the checksum if explicitly requested. */
    if (   g_enmUpdateChecksum == kUpdateCheckSum_Zero
        && NtHdrsNew.x32.OptionalHeader.CheckSum != 0)
    {
        unsigned long const offCheckSumField = offNtHdrs + RT_UOFFSETOF(IMAGE_NT_HEADERS32, OptionalHeader.CheckSum);
        if (fseek(pFile, offCheckSumField, SEEK_SET) != 0)
            return Error("Failed to seek to the CheckSum field in the PE at %#lx: %s", offCheckSumField, strerror(errno));

        NtHdrsNew.x32.OptionalHeader.CheckSum = 0;
        if (fwrite(&NtHdrsNew.x32.OptionalHeader.CheckSum, sizeof(NtHdrsNew.x32.OptionalHeader.CheckSum), 1, pFile) != 1)
            return Error("Failed to write the CheckSum field in the PE header at %#lx: %s", offCheckSumField, strerror(errno));
    }

    return RTEXITCODE_SUCCESS;
}


static int Usage(FILE *pOutput)
{
    fprintf(pOutput,
            "Usage: VBoxPeSetVersion [options] <PE-image>\n"
            "Options:\n"
            "  -v, --verbose\n"
            "    Increases verbosity.\n"
            "  -q, --quiet\n"
            "    Quiet operation (default).\n"
            "  --nt31, --nt350, --nt351, --nt4, --w2k, --xp, --w2k3, --vista,\n"
            "  --w7, --w8, --w81, --w10\n"
            "    Which version to set.  Default: --nt31 (x86), --w2k3 (amd64)\n"
            "  --update-checksum-when-needed, --always-update-checksum,\n"
            "  --never-update-checksum, --zero-checksum:\n"
            "    Checksum updating. Default: --update-checksum-when-needed\n"
            );
    return RTEXITCODE_SYNTAX;
}


/** @todo Rewrite this so it can take options and print out error messages. */
int main(int argc, char **argv)
{
    /*
     * Parse arguments.
     * This stucks
     */
    unsigned    uNtVersion     = 0;
    const char *pszFilename    = NULL;
    bool        fAcceptOptions = true;
    for (int i = 1; i < argc; i++)
    {
        const char *psz = argv[i];
        if (fAcceptOptions && *psz == '-')
        {
            char ch = psz[1];
            psz += 2;
            if (ch == '-')
            {
                if (!*psz)
                {
                    fAcceptOptions = false;
                    continue;
                }

                if (strcmp(psz, "verbose") == 0)
                    ch = 'v';
                else if (strcmp(psz, "quiet") == 0)
                    ch = 'q';
                else if (strcmp(psz, "help") == 0)
                    ch = 'h';
                else if (strcmp(psz, "version") == 0)
                    ch = 'V';
                else
                {
                    if (strcmp(psz, "default") == 0)
                        uNtVersion = 0;
                    else if (strcmp(psz, "nt31") == 0)
                        uNtVersion = MK_VER(3,10);
                    else if (strcmp(psz, "nt350") == 0)
                        uNtVersion = MK_VER(3,50);
                    else if (strcmp(psz, "nt351") == 0)
                        uNtVersion = MK_VER(3,51);
                    else if (strcmp(psz, "nt4") == 0)
                        uNtVersion = MK_VER(4,0);
                    else if (strcmp(psz, "w2k") == 0)
                        uNtVersion = MK_VER(5,0);
                    else if (strcmp(psz, "xp") == 0)
                        uNtVersion = MK_VER(5,1);
                    else if (strcmp(psz, "w2k3") == 0 || strcmp(psz, "xp64") == 0)
                        uNtVersion = MK_VER(5,2);
                    else if (strcmp(psz, "vista") == 0)
                        uNtVersion = MK_VER(6,0);
                    else if (strcmp(psz, "w7") == 0)
                        uNtVersion = MK_VER(6,1);
                    else if (strcmp(psz, "w8") == 0)
                        uNtVersion = MK_VER(6,2);
                    else if (strcmp(psz, "w81") == 0)
                        uNtVersion = MK_VER(6,3);
                    else if (strcmp(psz, "w10") == 0)
                        uNtVersion = MK_VER(10,0);
                    else if (strcmp(psz, "always-update-checksum") == 0)
                        g_enmUpdateChecksum = kUpdateCheckSum_Always;
                    else if (strcmp(psz, "never-update-checksum") == 0)
                        g_enmUpdateChecksum = kUpdateCheckSum_Never;
                    else if (strcmp(psz, "update-checksum-when-needed") == 0)
                        g_enmUpdateChecksum = kUpdateCheckSum_WhenNeeded;
                    else if (strcmp(psz, "zero-checksum") == 0)
                        g_enmUpdateChecksum = kUpdateCheckSum_Zero;
                    else
                    {
                        fprintf(stderr, "VBoxPeSetVersion: syntax error: Unknown option: --%s\n", psz);
                        return RTEXITCODE_SYNTAX;
                    }
                    continue;
                }
                psz = " ";
            }
            do
            {
                switch (ch)
                {
                    case 'q':
                        g_cVerbosity = 0;
                        break;
                    case 'v':
                        g_cVerbosity++;
                        break;
                    case 'V':
                        printf("2.0\n");
                        return RTEXITCODE_SUCCESS;
                    case 'h':
                        Usage(stdout);
                        return RTEXITCODE_SUCCESS;
                    default:
                        fprintf(stderr, "VBoxPeSetVersion: syntax error: Unknown option: -%c\n", ch ? ch : ' ');
                        return RTEXITCODE_SYNTAX;
                }
            } while ((ch = *psz++) != '\0');

        }
        else if (!pszFilename)
            pszFilename = psz;
        else
        {
            fprintf(stderr, "VBoxPeSetVersion: syntax error: More than one PE-image specified!\n");
            return RTEXITCODE_SYNTAX;
        }
    }

    if (!pszFilename)
    {
        fprintf(stderr, "VBoxPeSetVersion: syntax error: No PE-image specified!\n");
        return RTEXITCODE_SYNTAX;
    }
    g_pszFilename = pszFilename;

    /*
     * Process the file.
     */
    int rcExit;
    FILE *pFile = fopen(pszFilename, "r+b");
    if (pFile)
    {
        PIMAGE_SECTION_HEADER paShdrs = NULL;
        rcExit = UpdateFile(pFile, uNtVersion, &paShdrs);
        if (paShdrs)
            free(paShdrs);
        if (fclose(pFile) != 0)
            rcExit = Error("fclose failed on '%s': %s", pszFilename, strerror(errno));
    }
    else
        rcExit = Error("Failed to open '%s' for updating: %s", pszFilename, strerror(errno));
    return rcExit;
}

