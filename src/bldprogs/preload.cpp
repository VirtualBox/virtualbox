/* $Id: preload.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#ifdef RT_OS_WINDOWS
# include <Windows.h>
#else
# include <sys/mman.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>
# include <errno.h>
#endif
#include <stdio.h>
#include <string.h>


static int load(const char *pszImage)
{
#ifdef RT_OS_WINDOWS
    HANDLE hFile = CreateFile(pszImage,
                              GENERIC_READ,
                              FILE_SHARE_READ,
                              NULL /*pSecurityAttributes*/,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,
                              NULL /*hTemplateFile*/);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        printf("error: CreateFile('%s',): %d\n", pszImage, GetLastError());
        return 1;
    }

    DWORD cbHigh  = 0;
    DWORD cbLow   = GetFileSize(hFile, &cbHigh);
    size_t cbFile = cbLow != INVALID_FILE_SIZE
                  ? cbHigh == 0
                  ? cbLow
                  : ~(DWORD)0 / 4
                  : 64;

    HANDLE hMap = CreateFileMapping(hFile,
                                    NULL /*pAttributes*/,
                                    PAGE_READONLY | SEC_COMMIT,
                                    0 /*dwMaximumSizeHigh -> file size*/,
                                    0 /*dwMaximumSizeLow  -> file size*/,
                                    NULL /*pName*/);
    if (hMap == INVALID_HANDLE_VALUE)
        printf("error: CreateFile('%s',): %d\n", pszImage, GetLastError());
        CloseHandle(hFile);
    if (hMap == INVALID_HANDLE_VALUE)
        return 1;

    void *pvWhere = MapViewOfFile(hMap,
                                  FILE_MAP_READ,
                                  0 /*dwFileOffsetHigh*/,
                                  0 /*dwFileOffsetLow*/,
                                  0 /*dwNumberOfBytesToMap - file size */);
    if (!pvWhere)
    {
        printf("error: MapViewOfView('%s',): %d\n", pszImage, GetLastError());
        CloseHandle(hMap);
        return 1;
    }

#else
    int fd = open(pszImage, O_RDONLY, 0);
    if (fd < 0)
    {
        printf("error: open('%s',): %d\n", pszImage, errno);
        return 1;
    }

    struct stat st;
    memset(&st, 0, sizeof(st));
    if (fstat(fd, &st))
        st.st_size = 64;
    size_t cbFile = st.st_size < ~(size_t)0
                  ? (size_t)st.st_size
                  : ~(size_t)0 / 4;

    void *pvWhere = mmap(NULL /*addr*/, cbFile, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0 /*offset*/);
    if (pvWhere == MAP_FAILED)
        printf("error: mmap(,%lu,)/'%s': %d\n", (unsigned long)cbFile, pszImage, errno);
    close(fd);
    if (pvWhere == MAP_FAILED)
        return 1;

#endif

    /* Touch the whole image... do a dummy crc to keep the optimizer from begin
       smart with us. */
    unsigned char  *puchFile = (unsigned char *)pvWhere;
    size_t          off      = 0;
    unsigned int    uCrc     = 0;
    while (off < cbFile)
        uCrc += puchFile[off++];
    printf("info: %p/%#lx/%#x - %s\n", pvWhere, (unsigned long)cbFile, (unsigned char)uCrc, pszImage);

    return 0;
}

static int usage(const char *argv0)
{
    printf("Generic executable image preloader.\n"
           "Usage: %s [dll|exe|file []]\n", argv0);
    return 1;
}

int main(int argc, char **argv)
{
    /*
     * Check for options.
     */
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            if (   argv[i][1] == '-'
                && argv[i][2] == '\0')
                break;
            if (   !strcmp(argv[i], "--help")
                || !strcmp(argv[i], "-help")
                || !strcmp(argv[i], "-h")
                || !strcmp(argv[i], "-?"))
            {
                usage(argv[0]);
                return 1;
            }
            if (   !strcmp(argv[i], "--version")
                || !strcmp(argv[i], "-V"))
            {
                printf("$Revision: 106320 $\n");
                return 0;
            }
            fprintf(stderr, "syntax error: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }
    if (argc <= 1)
        return usage(argv[0]);

    /*
     * Do the loading.
     */
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--"))
            continue;
        if (argv[i][0] == '@')
        {
            FILE *pFile = fopen(&argv[i][1], "r");
            if (pFile)
            {
                char szLine[4096];
                while (fgets(szLine, sizeof(szLine), pFile))
                {
                    char *psz = szLine;
                    while (*psz == ' ' || *psz == '\t')
                        psz++;
                    size_t off = strlen(psz);
                    while (     off > 0
                           &&   (   psz[off - 1] == ' '
                                 || psz[off - 1] == '\t'
                                 || psz[off - 1] == '\n'
                                 || psz[off - 1] == '\r')
                          )
                        psz[--off] = '\0';

                    if (*psz && *psz != '#')
                        load(psz);
                }
                fclose(pFile);
            }
            else
                fprintf(stderr, "error: fopen('%s','r'): %d\n", &argv[i][1], errno);
        }
        else
            load(argv[i]);
    }

    /*
     * Sleep for ever.
     */
    for (;;)
    {
#ifdef RT_OS_WINDOWS
        Sleep(3600*1000);
#else
        sleep(3600);
#endif
    }

    return 0;
}
