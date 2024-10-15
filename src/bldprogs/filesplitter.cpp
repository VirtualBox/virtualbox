/* $Id: filesplitter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * File splitter - Splits a text file according to ###### markers in it.
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
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <iprt/string.h>
#include <iprt/stdarg.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifndef S_ISDIR
# define S_ISDIR(a_fMode)       ( (S_IFMT & (a_fMode)) == S_IFDIR )
#endif


/**
 * Calculates the line number for a file position.
 *
 * @returns Line number.
 * @param   pcszContent         The file content.
 * @param   pcszPos             The current position.
 */
static unsigned long lineNumber(const char *pcszContent, const char *pcszPos)
{
    unsigned long cLine = 0;
    while (   *pcszContent
           && (uintptr_t)pcszContent < (uintptr_t)pcszPos)
    {
        pcszContent = strchr(pcszContent, '\n');
        if (!pcszContent)
            break;
        ++cLine;
        ++pcszContent;
    }

    return cLine;
}


/**
 * Writes an error message.
 *
 * @returns RTEXITCODE_FAILURE.
 * @param   pcszFormat          Error message.
 * @param   ...                 Format argument referenced in the message.
 */
static int printErr(const char *pcszFormat, ...)
{
    va_list va;

    fprintf(stderr, "filesplitter: ");
    va_start(va, pcszFormat);
    vfprintf(stderr, pcszFormat, va);
    va_end(va);

    return RTEXITCODE_FAILURE;
}


/**
 * Opens the makefile list for writing.
 *
 * @returns Exit code.
 * @param   pcszPath            The path to the file.
 * @param   pcszVariableName    The make variable name.
 * @param   ppFile              Where to return the file stream.
 */
static int openMakefileList(const char *pcszPath, const char *pcszVariableName, FILE **ppFile)
{
    *ppFile = NULL;

    FILE *pFile= fopen(pcszPath, "w");
    if (!pFile)
#ifdef _MSC_VER
        return printErr("Failed to open \"%s\" for writing the file list: %s (win32: %d)\n",
                        pcszPath, strerror(errno), _doserrno);
#else
        return printErr("Failed to open \"%s\" for writing the file list: %s\n", pcszPath, strerror(errno));
#endif

    if (fprintf(pFile, "%s := \\\n", pcszVariableName) <= 0)
    {
        fclose(pFile);
        return printErr("Error writing to the makefile list.\n");
    }

    *ppFile = pFile;
    return 0;
}


/**
 * Adds the given file to the makefile list.
 *
 * @returns Exit code.
 * @param   pFile               The file stream of the makefile list.
 * @param   pszFilename         The file name to add.
 */
static int addFileToMakefileList(FILE *pFile, char *pszFilename)
{
    if (pFile)
    {
        char *pszSlash = pszFilename;
        while ((pszSlash = strchr(pszSlash, '\\')) != NULL)
            *pszSlash++ = '/';

        if (fprintf(pFile, "\t%s \\\n", pszFilename) <= 0)
            return printErr("Error adding file to makefile list.\n");
    }
    return 0;
}


/**
 * Closes the makefile list.
 *
 * @returns Exit code derived from @a rc.
 * @param   pFile               The file stream of the makefile list.
 * @param   rc                  The current exit code.
 */
static int closeMakefileList(FILE *pFile, int rc)
{
    fprintf(pFile, "\n\n");
    if (fclose(pFile))
        return printErr("Error closing the file list file: %s\n", strerror(errno));
    return rc;
}


/**
 * Reads in a file.
 *
 * @returns Exit code.
 * @param   pcszFile            The path to the file.
 * @param   ppszFile            Where to return the buffer.
 * @param   pcchFile            Where to return the file size.
 */
static int readFile(const char *pcszFile, char **ppszFile, size_t *pcchFile)
{
    FILE          *pFile;
    struct stat    FileStat;
    int            rc;

    if (stat(pcszFile, &FileStat))
        return printErr("Failed to stat \"%s\": %s\n", pcszFile, strerror(errno));

    pFile = fopen(pcszFile, "r");
    if (!pFile)
        return printErr("Failed to open \"%s\": %s\n", pcszFile, strerror(errno));

    *ppszFile = (char *)malloc(FileStat.st_size + 1);
    if (*ppszFile)
    {
        errno = 0;
        size_t cbRead = fread(*ppszFile, 1, FileStat.st_size, pFile);
        if (   cbRead <= (size_t)FileStat.st_size
            && (cbRead > 0 || !ferror(pFile)) )
        {
            if (ftell(pFile) == FileStat.st_size) /* (\r\n vs \n in the DOS world) */
            {
                (*ppszFile)[cbRead] = '\0';
                if (pcchFile)
                    *pcchFile = (size_t)cbRead;

                fclose(pFile);
                return 0;
            }
        }

        rc = printErr("Error reading \"%s\": %s\n", pcszFile, strerror(errno));
        free(*ppszFile);
        *ppszFile = NULL;
    }
    else
        rc = printErr("Failed to allocate %lu bytes\n", (unsigned long)(FileStat.st_size + 1));
    fclose(pFile);
    return rc;
}


/**
 * Checks whether the sub-file already exists and has the exact
 * same content.
 *
 * @returns @c true if the existing file matches exactly, otherwise @c false.
 * @param   pcszFilename    The path to the file.
 * @param   pcszSubContent  The content to write.
 * @param   cchSubContent   The length of the content.
 */
static bool compareSubFile(const char *pcszFilename, const char *pcszSubContent, size_t cchSubContent)
{
    struct stat     FileStat;
    if (stat(pcszFilename, &FileStat))
        return false;
    if ((size_t)FileStat.st_size < cchSubContent)
        return false;

    size_t cchExisting;
    char  *pszExisting;
    int rc = readFile(pcszFilename, &pszExisting, &cchExisting);
    if (rc)
        return false;

    bool fRc = cchExisting == cchSubContent
            && !memcmp(pcszSubContent, pszExisting, cchSubContent);
    free(pszExisting);

    return fRc;
}


/**
 * Writes out a sub-file.
 *
 * @returns exit code.
 * @param   pcszFilename    The path to the sub-file.
 * @param   pcszSubContent  The content of the file.
 * @param   cchSubContent   The size of the content.
 */
static int writeSubFile(const char *pcszFilename, const char *pcszSubContent, size_t cchSubContent)
{
    FILE   *pFile = fopen(pcszFilename, "w");
    if (!pFile)
#ifdef _MSC_VER
        return printErr("Failed to open \"%s\" for writing: %s (win32: %d)\n", pcszFilename, strerror(errno), _doserrno);
#else
        return printErr("Failed to open \"%s\" for writing: %s\n", pcszFilename, strerror(errno));
#endif

    errno = 0;
    int rc = 0;
    if (fwrite(pcszSubContent, cchSubContent, 1, pFile) != 1)
        rc = printErr("Error writing \"%s\": %s\n", pcszFilename, strerror(errno));

    errno = 0;
    int rc2 = fclose(pFile);
    if (rc2 == EOF)
        rc = printErr("Error closing \"%s\": %s\n", pcszFilename, strerror(errno));
    return rc;
}


/**
 * Does the actual file splitting.
 *
 * @returns exit code.
 * @param   pcszOutDir      Path to the output directory.
 * @param   pcszContent     The content to split up.
 * @param   pFileList       The file stream of the makefile list.  Can be NULL.
 */
static int splitFile(const char *pcszOutDir, const char *pcszContent, FILE *pFileList)
{
    static char const   s_szBeginMarker[] = "\n// ##### BEGINFILE \"";
    static char const   s_szEndMarker[]   = "\n// ##### ENDFILE";
    const size_t        cchBeginMarker    = sizeof(s_szBeginMarker) - 1;
    const char         *pcszSearch        = pcszContent;
    size_t const        cchOutDir         = strlen(pcszOutDir);
    unsigned long       cFilesWritten     = 0;
    unsigned long       cFilesUnchanged   = 0;
    int                 rc                = 0;

    do
    {
        /* find begin marker */
        const char *pcszBegin = strstr(pcszSearch, s_szBeginMarker);
        if (!pcszBegin)
            break;

        /* find line after begin marker */
        const char *pcszLineAfterBegin = strchr(pcszBegin + cchBeginMarker, '\n');
        if (!pcszLineAfterBegin)
            return printErr("No newline after begin-file marker found.\n");
        ++pcszLineAfterBegin;

        /* find filename end quote in begin marker line */
        const char *pcszStartFilename = pcszBegin + cchBeginMarker;
        const char *pcszEndQuote = (const char *)memchr(pcszStartFilename, '\"', pcszLineAfterBegin - pcszStartFilename);
        if (!pcszEndQuote)
            return printErr("Can't parse filename after begin-file marker (line %lu).\n",
                            lineNumber(pcszContent, s_szBeginMarker));

        /* find end marker */
        const char *pcszEnd = strstr(pcszLineAfterBegin, s_szEndMarker);
        if (!pcszEnd)
            return printErr("No matching end-line marker for begin-file marker found (line %lu).\n",
                            lineNumber(pcszContent, s_szBeginMarker));

        /* construct output filename */
        size_t cchFilename = pcszEndQuote - pcszStartFilename;
        char  *pszFilename = (char *)malloc(cchOutDir + 1 + cchFilename + 1);
        if (!pszFilename)
            return printErr("Can't allocate memory for filename.\n");

        memcpy(pszFilename, pcszOutDir, cchOutDir);
        pszFilename[cchOutDir] = '/';
        memcpy(pszFilename + cchOutDir + 1, pcszStartFilename, cchFilename);
        pszFilename[cchFilename + 1 + cchOutDir] = '\0';

        /* Write the file only if necessary. */
        if (compareSubFile(pszFilename, pcszLineAfterBegin, pcszEnd - pcszLineAfterBegin))
            cFilesUnchanged++;
        else
        {
            rc = writeSubFile(pszFilename, pcszLineAfterBegin, pcszEnd - pcszLineAfterBegin);
            cFilesWritten++;
        }

        if (!rc)
            rc = addFileToMakefileList(pFileList, pszFilename);

        free(pszFilename);

        pcszSearch = pcszEnd;
    } while (rc == 0);

    printf("filesplitter: Out of %lu files: %lu rewritten, %lu unchanged. (%s)\n",
           cFilesWritten + cFilesUnchanged, cFilesWritten, cFilesUnchanged, pcszOutDir);
    return rc;
}


int main(int argc, char *argv[])
{
    int rc = 0;

    if (argc == 3 || argc == 5)
    {
        struct stat DirStat;
        if (   stat(argv[2], &DirStat) == 0
            && S_ISDIR(DirStat.st_mode))
        {
            char   *pszContent;
            rc = readFile(argv[1], &pszContent, NULL);
            if (!rc)
            {
                FILE *pFileList = NULL;
                if (argc == 5)
                    rc = openMakefileList(argv[3], argv[4], &pFileList);

                if (argc < 4 || pFileList)
                    rc = splitFile(argv[2], pszContent, pFileList);

                if (pFileList)
                    rc = closeMakefileList(pFileList, rc);
                free(pszContent);
            }
        }
        else
            rc = printErr("Given argument \"%s\" is not a valid directory.\n", argv[2]);
    }
    else
        rc = printErr("Syntax error: usage: filesplitter <infile> <outdir> [<list.kmk> <kmkvar>]\n");
    return rc;
}
