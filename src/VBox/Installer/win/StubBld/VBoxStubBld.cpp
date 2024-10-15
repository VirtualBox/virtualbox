/* $Id: VBoxStubBld.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxStubBld - VirtualBox's Windows installer stub builder.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <iprt/win/windows.h>
#include <shellapi.h>
#include <strsafe.h>

#include <VBox/version.h>
#include <iprt/types.h>

#include "VBoxStubBld.h"


static HRESULT GetFile(const char *pszFilePath, HANDLE *phFile, DWORD *pcbFile)
{
    HANDLE hFile = CreateFileA(pszFilePath, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        SetLastError(NO_ERROR);
        LARGE_INTEGER cbFile;
        if (GetFileSizeEx(hFile, &cbFile))
        {
            if (cbFile.HighPart == 0)
            {
                *pcbFile = cbFile.LowPart;
                *phFile  = hFile;
                return S_OK;
            }
            fprintf(stderr, "error: File '%s' is too large: %llu bytes\n", pszFilePath, cbFile.QuadPart);
        }
        else
            fprintf(stderr, "error: GetFileSizeEx failed on '%s': %lu\n", pszFilePath, GetLastError());
        CloseHandle(hFile);
    }
    else
        fprintf(stderr, "error: CreateFileA failed on '%s': %lu\n", pszFilePath, GetLastError());
    *phFile = INVALID_HANDLE_VALUE;
    return E_FAIL;
}

static HRESULT MyUpdateResource(HANDLE hFile, DWORD cbFile, HANDLE hResourceUpdate,
                                const char *pszResourceType, const char *pszResourceId)
{
    HRESULT hr   = E_FAIL;
    HANDLE  hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (hMap != NULL)
    {
        PVOID pvFile = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, cbFile);
        if (pvFile)
        {
            if (UpdateResourceA(hResourceUpdate, pszResourceType, pszResourceId,
                                MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), pvFile, cbFile))
                hr = S_OK;
            else
                fprintf(stderr, "error: UpdateResourceA failed: %lu\n", GetLastError());

            UnmapViewOfFile(pvFile);
        }
        else
            fprintf(stderr, "error: MapViewOfFile failed: %lu\n", GetLastError());
        CloseHandle(hMap);
    }
    else
        fprintf(stderr, "error: CreateFileMappingW failed: %lu\n", GetLastError());
    return hr;
}

static HRESULT IntegrateFile(HANDLE hResourceUpdate, const char *pszResourceType,
                             const char *pszResourceId, const char *pszFilePath)
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    DWORD cbFile = 0;
    HRESULT hr = GetFile(pszFilePath, &hFile, &cbFile);
    if (SUCCEEDED(hr))
    {
        hr = MyUpdateResource(hFile, cbFile, hResourceUpdate, pszResourceType, pszResourceId);
        if (FAILED(hr))
            printf("ERROR: Error updating resource for file %s!", pszFilePath);
        CloseHandle(hFile);
    }
    else
        hr = HRESULT_FROM_WIN32(GetLastError());
    return hr;
}

static char *MyPathFilename(const char *pszPath)
{
    const char *pszName = pszPath;
    for (const char *psz = pszPath;; psz++)
    {
        switch (*psz)
        {
            /* handle separators. */
            case ':':
                pszName = psz + 1;
                break;

            case '\\':
            case '/':
                pszName = psz + 1;
                break;

            /* the end */
            case '\0':
                if (*pszName)
                    return (char *)(void *)pszName;
                return NULL;
        }
    }

    /* will never get here */
}


int main(int argc, char* argv[])
{
    /*
     * Parse arguments.
     */
    const char *pszSetupStub = "VBoxStub.exe";
    const char *pszOutput    = "VirtualBox-MultiArch.exe";

    printf(VBOX_PRODUCT " Stub Builder v%d.%d.%d.%d\n",
           VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR, VBOX_VERSION_BUILD, VBOX_SVN_REV);

    struct VBOXSTUBBUILDPKG
    {
        const char     *pszSrcPath;
        VBOXSTUBPKGARCH enmArch;
    }                   aBuildPkgs[VBOXSTUB_MAX_PACKAGES] = { { NULL } };
    VBOXSTUBPKGHEADER   StubHdr =
    {
        /*.szMagic = */   VBOXSTUBPKGHEADER_MAGIC_SZ,
        /*.cPackages = */ 0
    };

    for (int i = 1; i < argc; i++)
    {
        const char *pszArg = argv[i];
        if (   strcmp(pszArg, "--help") == 0
            || strcmp(pszArg, "-help") == 0
            || strcmp(pszArg, "-h") == 0
            || strcmp(pszArg, "-?") == 0)
        {
            printf("usage: %s -out <installer.exe> -stub <stub.exe> [-target-all <file>] [-target-<arch> <file>]\n", argv[0]);
            return RTEXITCODE_SUCCESS;
        }

        /* The remaining options all take a while. */
        if (   strcmp(pszArg, "-out")
            && strcmp(pszArg, "-stub")
            && strcmp(pszArg, "-target-all")
            && strcmp(pszArg, "-target-x86")
            && strcmp(pszArg, "-target-amd64"))
        {
            fprintf(stderr, "syntax error: Invalid parameter: %s\n", argv[i]);
            return RTEXITCODE_SYNTAX;
        }

        i++;
        if (i >= argc)
        {
            fprintf(stderr, "syntax error: Option '%s' takes a value argument!\n", pszArg);
            return RTEXITCODE_SYNTAX;
        }
        const char *pszValue = argv[i];

        /* Process the individual options. */
        if (strcmp(pszArg, "-out") == 0)
            pszOutput = pszValue;
        else if (strcmp(pszArg, "-stub") == 0)
            pszSetupStub = pszValue;
        else
        {
            if (StubHdr.cPackages >= RT_ELEMENTS(aBuildPkgs))
            {
                fprintf(stderr, "error: Too many packages specified!\n");
                return RTEXITCODE_FAILURE;
            }
            aBuildPkgs[StubHdr.cPackages].pszSrcPath = pszValue;
            if (strcmp(pszArg, "-target-all") == 0)
                aBuildPkgs[StubHdr.cPackages].enmArch = VBOXSTUBPKGARCH_ALL;
            else if (strcmp(pszArg, "-target-amd64") == 0)
                aBuildPkgs[StubHdr.cPackages].enmArch = VBOXSTUBPKGARCH_AMD64;
            else if (strcmp(pszArg, "-target-x86") == 0)
                aBuildPkgs[StubHdr.cPackages].enmArch = VBOXSTUBPKGARCH_X86;
            else
            {
                fprintf(stderr, "internal error: %u\n", __LINE__);
                return RTEXITCODE_FAILURE;
            }
            StubHdr.cPackages++;
        }
    }

    if (StubHdr.cPackages == 0)
    {
        fprintf(stderr, "syntax error: No packages specified! Exiting.\n");
        return RTEXITCODE_SYNTAX;
    }

    printf("Stub:       %s\n", pszSetupStub);
    printf("Output:     %s\n", pszOutput);
    printf("# Packages: %u\n", StubHdr.cPackages);

    /*
     * Copy the stub over the output file.
     */
    if (!CopyFile(pszSetupStub, pszOutput, FALSE))
    {
        fprintf(stderr, "ERROR: Could not copy the stub loader: %lu\n", GetLastError());
        return RTEXITCODE_SYNTAX;
    }

    /*
     * Start updating the resources of the output file.
     */
    HANDLE hUpdate = BeginUpdateResourceA(pszOutput, FALSE);
    if (hUpdate)
    {
        /*
         * Add the file one by one to the output file.
         */
        HRESULT hrc = S_OK;
        for (BYTE i = 0; i < StubHdr.cPackages; i++)
        {
            printf("Integrating (Platform %d): %s\n", aBuildPkgs[i].enmArch, aBuildPkgs[i].pszSrcPath);

            /*
             * Create the package header.
             */
            VBOXSTUBPKG Package = {0};
            Package.enmArch = aBuildPkgs[i].enmArch;

            /* The resource name */
            hrc = StringCchPrintf(Package.szResourceName, sizeof(Package.szResourceName), "BIN_%02d", i);
            if (FAILED(hrc))
            {
                fprintf(stderr, "Internal error: %u\n", __LINE__);
                break;
            }

            /* Construct final name used when extracting. */
            hrc = StringCchCopy(Package.szFilename, sizeof(Package.szFilename), MyPathFilename(aBuildPkgs[i].pszSrcPath));
            if (FAILED(hrc))
            {
                fprintf(stderr, "ERROR: Filename is too long: %s\n", aBuildPkgs[i].pszSrcPath);
                break;
            }

            /*
             * Add the package header to the binary.
             */
            char szHeaderName[32];
            hrc = StringCchPrintf(szHeaderName, sizeof(szHeaderName), "HDR_%02d", i);
            if (FAILED(hrc))
            {
                fprintf(stderr, "Internal error: %u\n", __LINE__);
                break;
            }

            if (!UpdateResourceA(hUpdate, RT_RCDATA, szHeaderName, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                                 &Package, sizeof(Package)))
            {
                fprintf(stderr, "ERROR: UpdateResourceA failed for the package header: %lu\n", GetLastError());
                hrc = E_FAIL;
                break;
            }

            /*
             * Add the file content under the BIN_xx resource name.
             */
            hrc = IntegrateFile(hUpdate, RT_RCDATA, Package.szResourceName, aBuildPkgs[i].pszSrcPath);
            if (FAILED(hrc))
                break;
        }
        if (SUCCEEDED(hrc))
        {
            /*
             * Now add the header/manifest and complete the operation.
             */
            if (UpdateResourceA(hUpdate, RT_RCDATA, "MANIFEST", MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                                &StubHdr, sizeof(StubHdr)))
            {
                if (EndUpdateResourceA(hUpdate, FALSE /*fDiscard*/))
                {
                    printf("Successfully created the installer\n");
                    return RTEXITCODE_SUCCESS;
                }
                fprintf(stderr, "ERROR: EndUpdateResourceA failed: %lu\n", GetLastError());
            }
            else
                fprintf(stderr, "ERROR: UpdateResourceA failed for the installer header/manifest: %lu\n", GetLastError());
        }

        EndUpdateResourceA(hUpdate, TRUE /*fDiscard*/);
        hUpdate = NULL;
    }
    else
        fprintf(stderr, "error: BeginUpdateResource failed: %lu\n", GetLastError());
    return RTEXITCODE_FAILURE;
}
