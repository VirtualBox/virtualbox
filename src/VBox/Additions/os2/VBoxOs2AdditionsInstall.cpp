/** $Id: VBoxOs2AdditionsInstall.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxOs2AdditionsInstall - Barebone OS/2 Guest Additions Installer.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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
#define INCL_BASE
#include <os2.h>
#include <VBox/version.h>

#include <string.h>
#include <iprt/ctype.h>
#include <iprt/path.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define SKIP_CONFIG_SYS     0x01
#define SKIP_STARTUP_CMD    0x02
#define SKIP_SERVICE        0x04
#define SKIP_SHARED_FOLDERS 0x08
#define SKIP_GRAPHICS       0x10
#define SKIP_MOUSE          0x20
#define SKIP_LIBC_DLLS      0x40

/** NIL HFILE value. */
#define MY_NIL_HFILE        (~(HFILE)0)

#if defined(DOXYGEN_RUNNING)
/** Enabled extra debug output in the matching functions. */
# define DEBUG_MATCHING
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct FILEEDITOR
{
    size_t          cbOrg;
    char           *pszOrg;
    size_t          cbNew;
    size_t          cbNewAlloc;
    char           *pszNew;
    bool            fAppendEof;
    bool            fOverflowed;
    size_t          cBogusChars;
} FILEEDITOR;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Where the files to install (default: same dir as this program).  */
static CHAR         g_szSrcPath[CCHMAXPATH];
/** The length of g_szSrcPath, including a trailing slash. */
static size_t       g_cchSrcPath                    = 0;
/** The boot drive path, i.e. where Config.kmk & Startup.cmd lives. */
static CHAR         g_szBootDrivePath[CCHMAXPATH]   = "C:\\";
/** The size of the bootdrive path, including a trailing slash. */
static size_t       g_cchBootDrivePath              = sizeof("C:\\") - 1;
/** Where to install the guest additions files.  */
static CHAR         g_szDstPath[CCHMAXPATH]         = "C:\\VBoxAdd\\";
/** The length of g_szDstPath, including a trailing slash. */
static size_t       g_cchDstPath                    = sizeof("C:\\VBoxAdd\\") - 1;
/** Mask of SKIP_XXX flags of components/tasks to skip. */
static uint8_t      g_fSkipMask                     = 0;
/** Verbose or quiet. */
static bool         g_fVerbose                      = true;
/** Whether this is a real run (true) or just a trial. */
static bool         g_fRealRun                      = false;

/** The standard output handle. */
static HFILE const  g_hStdOut = (HFILE)1;
/** The standard error handle. */
static HFILE const  g_hStdErr = (HFILE)2;


/** File editor for Config.sys. */
static FILEEDITOR   g_ConfigSys;
/** File editor for Startup.cmd. */
static FILEEDITOR   g_StartupCmd;


/*********************************************************************************************************************************
*   Messaging.                                                                                                                   *
*********************************************************************************************************************************/

static void DoWriteNStr(HFILE hFile, const char *psz, size_t cch)
{
    ULONG cbIgnore;
    while (DosWrite(hFile, (void *)psz, cch, &cbIgnore) == ERROR_INTERRUPT)
        ;
}


static void DoWriteStr(HFILE hFile, const char *psz)
{
    DoWriteNStr(hFile, psz, strlen(psz));
}


/** Writes a variable number of strings to @a hFile, stopping at NULL. */
static void WriteStrings(HFILE hFile, ...)
{
    va_list va;
    va_start(va, hFile);
    for (;;)
    {
        const char *psz = va_arg(va, const char *);
        if (psz)
            DoWriteStr(hFile, psz);
        else
            break;
    }
    va_end(va);
}


/** Writes a variable number of length/strings pairs to @a hFile, stopping at
 *  0/NULL. */
static void WriteNStrings(HFILE hFile, ...)
{
    va_list va;
    va_start(va, hFile);
    for (;;)
    {
        const char *psz = va_arg(va, const char *);
        int         cch = va_arg(va, int);
        if (psz)
        {
            if (cch < 0)
                DoWriteStr(hFile, psz);
            else
                DoWriteNStr(hFile, psz, cch);
        }
        else
            break;
    }
    va_end(va);
}


static RTEXITCODE ErrorNStrings(const char *pszMsg, ssize_t cchMsg, ...)
{
    DoWriteNStr(g_hStdErr, RT_STR_TUPLE("VBoxOs2AdditionsInstall: error: "));
    va_list va;
    va_start(va, cchMsg);
    do
    {
        if (cchMsg < 0)
            DoWriteStr(g_hStdErr, pszMsg);
        else
            DoWriteNStr(g_hStdErr, pszMsg, cchMsg);
        pszMsg = va_arg(va, const char *);
        cchMsg = va_arg(va, int);
    } while (pszMsg != NULL);
    va_end(va);
    DoWriteNStr(g_hStdErr, RT_STR_TUPLE("\r\n"));
    return RTEXITCODE_FAILURE;
}


static char *MyNumToString(char *pszBuf, unsigned uNum)
{
    char *pszRet = pszBuf;

    /* Convert to decimal and inverted digit order:  */
    char     szTmp[32];
    unsigned off = 0;
    do
    {
        szTmp[off++] = uNum % 10 + '0';
        uNum /= 10;
    } while (uNum);

    /* Copy it out to the destination buffer in the right order and add a terminator: */
    while (off-- > 0)
        *pszBuf++ = szTmp[off];
    *pszBuf = '\0';

    return pszRet;
}


static void DoWriteNumber(HFILE hFile, unsigned uNum)
{
    char szTmp[32];
    MyNumToString(szTmp, uNum);
    DoWriteStr(hFile, szTmp);
}


static RTEXITCODE ApiErrorN(APIRET rc, unsigned cMsgs, ...)
{
    DoWriteNStr(g_hStdErr, RT_STR_TUPLE("VBoxOs2AdditionsInstall: error: "));
    va_list va;
    va_start(va, cMsgs);
    while (cMsgs-- > 0)
    {
        const char *pszMsg = va_arg(va, const char *);
        DoWriteStr(g_hStdErr, pszMsg);
    }
    va_end(va);
    DoWriteNStr(g_hStdErr, RT_STR_TUPLE(": "));
    DoWriteNumber(g_hStdErr, rc);
    DoWriteNStr(g_hStdErr, RT_STR_TUPLE("\r\n"));
    return RTEXITCODE_FAILURE;
}


DECLINLINE(RTEXITCODE) ApiError(const char *pszMsg, APIRET rc)
{
    return ApiErrorN(rc, 1, pszMsg);
}


static RTEXITCODE SyntaxError(const char *pszMsg, const char *pszArg)
{
    DoWriteNStr(g_hStdErr, RT_STR_TUPLE("VBoxOs2AdditionsInstall: syntax error: "));
    DoWriteNStr(g_hStdErr, pszMsg, strlen(pszMsg));
    DoWriteNStr(g_hStdErr, RT_STR_TUPLE("\r\n"));
    return RTEXITCODE_SYNTAX;
}


/*********************************************************************************************************************************
*   Editor.                                                                                                                      *
*********************************************************************************************************************************/

/**
 * Reads a file into the editor.
 */
static RTEXITCODE EditorReadInFile(FILEEDITOR *pEditor, const char *pszFilename, size_t cbExtraEdit, bool fMustExist)
{
    FILESTATUS3 FileSts;

    if (g_fVerbose)
        WriteStrings(g_hStdOut, "info: Preparing \"", pszFilename, "\" modifications...\r\n", NULL);

    /*
     * Open the file.
     */
    HFILE  hFile   = MY_NIL_HFILE;
    ULONG  uAction = ~0U;
    APIRET rc = DosOpen(pszFilename, &hFile, &uAction, 0, FILE_NORMAL,
                        OPEN_ACTION_OPEN_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW,
                        OPEN_ACCESS_READONLY | OPEN_SHARE_DENYWRITE | OPEN_FLAGS_SEQUENTIAL | OPEN_FLAGS_NOINHERIT,
                        NULL /*pEaOp2*/);
    if (rc == ERROR_OPEN_FAILED)
        rc = DosQueryPathInfo(pszFilename, FIL_STANDARD, &FileSts, sizeof(FileSts));
    if (rc == ERROR_FILE_NOT_FOUND)
        hFile = MY_NIL_HFILE;
    else if (rc != NO_ERROR)
        return ApiErrorN(rc, 3, "DosOpen(\"", pszFilename, "\",READONLY)");

    /*
     * Get it's size and check that it's sane.
     */
    if (hFile != MY_NIL_HFILE)
    {
        rc = DosQueryFileInfo(hFile, FIL_STANDARD, &FileSts, sizeof(FileSts));
        if (rc != NO_ERROR)
            return ApiErrorN(rc, 3, "DosQueryFileInfo(\"", pszFilename, "\",FIL_STANDARD,,)");

        if (FileSts.cbFile > _2M)
            return ApiErrorN(rc, FileSts.cbFile, "File \"", pszFilename, "\" is too large");
    }
    else
        FileSts.cbFile = 0;

    /*
     * Allocate buffers.
     */
    PVOID  pvAlloc = NULL;
    size_t cbAlloc = FileSts.cbFile * 2 + cbExtraEdit + 16;
    rc = DosAllocMem(&pvAlloc, cbAlloc, PAG_COMMIT | PAG_WRITE | PAG_READ);
    if (rc != NO_ERROR)
        return ApiError("DosAllocMem", rc);

    memset(pvAlloc, 0, cbAlloc);
    pEditor->cbOrg          = FileSts.cbFile;
    pEditor->pszOrg         = (char *)pvAlloc;
    pEditor->pszNew         = (char *)pvAlloc + FileSts.cbFile + 1;
    pEditor->cbNew          = 0;
    pEditor->cbNewAlloc     = cbAlloc - FileSts.cbFile - 2;
    pEditor->fAppendEof     = false;
    pEditor->fOverflowed    = false;
    pEditor->cBogusChars    = 0;

    /*
     * Read in the file content.
     */
    if (hFile != MY_NIL_HFILE)
    {
        ULONG cbRead = 0;
        rc = DosRead(hFile, pEditor->pszOrg, FileSts.cbFile, &cbRead);
        if (rc != NO_ERROR)
            return ApiErrorN(rc, 3, "DosRead(\"", pszFilename, "\")");
        if (cbRead != FileSts.cbFile)
            return ApiErrorN(cbRead < FileSts.cbFile ? ERROR_MORE_DATA : ERROR_BUFFER_OVERFLOW,
                             3, "DosRead(\"", pszFilename, "\")");
        DosClose(hFile);

        /*
         * Check for EOF/SUB character.
         */
        char *pchEof = (char *)memchr(pEditor->pszOrg, 0x1a, FileSts.cbFile);
        if (pchEof)
        {
            size_t offEof = pchEof - pEditor->pszOrg;
            for (size_t off = offEof + 1; off < FileSts.cbFile; off++)
                if (!RT_C_IS_SPACE(pEditor->pszOrg[off]))
                    return ErrorNStrings(RT_STR_TUPLE("Refusing to modify \""), pszFilename, -1,
                                         RT_STR_TUPLE("\" because of EOF character followed by text!"));
            pEditor->cbOrg      = offEof;
            pEditor->fAppendEof = true;
        }
    }

    return RTEXITCODE_SUCCESS;
}


/**
 * Writes out a modified file, backing up the original.
 */
static RTEXITCODE EditorWriteOutFile(FILEEDITOR *pEditor, const char *pszFilename)
{
    if (g_fVerbose)
        WriteStrings(g_hStdOut, "info: Writing out \"", pszFilename, "\" modifications...\r\n", NULL);

    /*
     * Skip if no change was made.
     */
    if (   pEditor->cbNew == 0
        || (   pEditor->cbNew == pEditor->cbOrg
            && memcmp(pEditor->pszNew, pEditor->pszOrg, pEditor->cbNew) == 0))
    {
        WriteStrings(g_hStdOut, "info: No changes to \"", pszFilename, "\".\r\n", NULL);
        return RTEXITCODE_SUCCESS;
    }

    /*
     * Back up the original.
     * ASSUMES that the input is CCHMAXPATH or less.
     */
    if (pEditor->cbOrg != 0)
    {
        CHAR         szBackup[CCHMAXPATH + 16];
        size_t const cchFilename = strlen(pszFilename);
        memcpy(szBackup, pszFilename, cchFilename + 1);
        char *pszExt = (char *)memrchr(szBackup, '.', cchFilename);
        if (!pszExt || strchr(pszExt, '\\') || strchr(pszExt, '/'))
            pszExt = &szBackup[cchFilename];
        strcpy(pszExt, ".BAK");
        for (unsigned short i = 0; ; i++)
        {
            HFILE  hFile   = MY_NIL_HFILE;
            ULONG  uAction = ~0U;
            APIRET rc = DosOpen(szBackup, &hFile, &uAction, 0, FILE_NORMAL,
                                OPEN_ACTION_FAIL_IF_EXISTS | OPEN_ACTION_CREATE_IF_NEW,
                                OPEN_ACCESS_WRITEONLY | OPEN_SHARE_DENYWRITE | OPEN_FLAGS_SEQUENTIAL | OPEN_FLAGS_NOINHERIT,
                                NULL /*pEaOp2*/);
            if (rc == NO_ERROR)
            {
                ULONG cbWritten = 0;
                do
                    rc = DosWrite(hFile, pEditor->pszOrg, pEditor->cbOrg + (pEditor->fAppendEof ? 1 : 0), &cbWritten);
                while (rc == ERROR_INTERRUPT);
                DosClose(hFile);
                if (rc != NO_ERROR)
                    return ApiErrorN(rc, 5, "Failed backing up \"", pszFilename, "\" as \"", szBackup, "\"");
                break;
            }

            /* try next extension variation */
            if (i >= 1000)
                return ApiErrorN(rc, 5, "Failed backing up \"", pszFilename, "\" as \"", szBackup, "\"");
            if (i >= 100)
                pszExt[1] = '0' + (i / 100);
            if (i >= 10)
                pszExt[2] = '0' + (i / 100 % 10);
            pszExt[3] = '0' + (i % 10);
        }
    }

    /*
     * Write out the new copy.
     */
    HFILE  hFile   = MY_NIL_HFILE;
    ULONG  uAction = ~0U;
    APIRET rc = DosOpen(pszFilename, &hFile, &uAction, 0, FILE_NORMAL,
                        OPEN_ACTION_REPLACE_IF_EXISTS | OPEN_ACTION_CREATE_IF_NEW,
                        OPEN_ACCESS_WRITEONLY | OPEN_SHARE_DENYWRITE | OPEN_FLAGS_SEQUENTIAL | OPEN_FLAGS_NOINHERIT,
                        NULL /*pEaOp2*/);
    if (rc != NO_ERROR)
        return ApiErrorN(rc, 3, "Opening \"", pszFilename, "\" for writing");

    ULONG cbToWrite = pEditor->cbNew;
    if (pEditor->fAppendEof)
    {
        pEditor->pszNew[cbToWrite] = 0x1a; /* replacing terminator */
        cbToWrite++;
    }

    ULONG cbWritten = 0;
    do
        rc = DosWrite(hFile, pEditor->pszNew, cbToWrite, &cbWritten);
    while (rc == ERROR_INTERRUPT);
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    if (rc != NO_ERROR)
        rcExit = ApiErrorN(rc, 3, "Failed writing \"", pszFilename, "\"");
    else if (cbWritten != cbToWrite)
    {
        char szNum1[32], szNum2[32];
        rcExit = ErrorNStrings(RT_STR_TUPLE("Failed writing \""), pszFilename, -1, RT_STR_TUPLE("\" - incomplete write: "),
                               MyNumToString(szNum1, cbWritten), -1, RT_STR_TUPLE(" written, requested "),
                               MyNumToString(szNum2, cbToWrite), NULL, 0);
    }

    rc = DosClose(hFile);
    if (rc != NO_ERROR)
        rcExit = ApiErrorN(rc, 3, "Failed closing \"", pszFilename, "\"");

    pEditor->pszNew[pEditor->cbNew - 1] = '\0'; /* replacing EOF */

    return rcExit;
}


/**
 * Gets the next line.
 *
 * @returns The offset to pass to the next EditorGetLine call.
 * @retval  0 if no further lines in the input file.
 *
 * @param   pEditor     Pointer to the editor.
 * @param   offSrc      The current source offset. Initialize to zero before
 *                      first calls and pass return value for subsequent calls
 * @param   ppchLine    Where to return the pointer to the start of the line.
 * @param   pcchLine    Where to return the length of the line (sans EOL).
 */
static size_t EditorGetLine(FILEEDITOR *pEditor, size_t offSrc, const char **ppchLine, size_t *pcchLine)
{
    if (offSrc < pEditor->cbOrg)
    {
        const char *pchLine = &pEditor->pszOrg[offSrc];
        *ppchLine = pchLine;

        size_t      cchMax  = pEditor->cbOrg - offSrc;
        const char *pchCr   = (const char *)memchr(pchLine, '\r', cchMax);
        const char *pchNl   = (const char *)memchr(pchLine, '\n', pchCr ? pchCr - pchLine : cchMax);
        size_t      cchLine;
        size_t      cchEol;
        if (pchCr && !pchNl)
        {
            cchLine = pchCr - pchLine;
            cchEol  = 1 + (pchCr[1] == '\n');
        }
        else if (pchNl)
        {
            cchLine = pchNl - pchLine;
            cchEol  = 1;
        }
        else
        {
            cchLine = cchMax;
            cchEol  = 0;
        }
        *pcchLine = cchLine;
        return offSrc + cchLine + cchEol;
    }

    *ppchLine = "";
    *pcchLine = 0;
    return 0;
}


/**
 * Checks that a string doesn't contain any funny control characters.
 *
 * These bogus characters are counted and EditorCheckState should be called to
 * check these after editing has completed.
 */
static void EditorCheckString(FILEEDITOR *pEditor, const char *pchString, size_t cchString, const char *pszCaller)
{
    for (size_t off = 0; off < cchString; off++)
    {
        if (   RT_C_IS_CNTRL(pchString[off])
            && pchString[off] != '\t')
        {
            static char s_szHex[] = "0123456789abcdef";
            char szDigits[3] = { s_szHex[pchString[off] >> 4],  s_szHex[pchString[off] & 0xf], '\0' };
            ErrorNStrings(pszCaller, -1, RT_STR_TUPLE(": Bogus control character in "),
                          pEditor == &g_ConfigSys ? "Config.sys: " : "Startup.cmd: ", -1, szDigits, 2, NULL, 0);
            pEditor->cBogusChars++;
        }
    }
}


/**
 * Adds a line to the output buffer.
 *
 * A CRLF is appended automatically.
 *
 * @returns true on success, false on overflow (error displayed and sets
 *          fOverflowed on the editor).
 *
 * @param   pEditor     Pointer to the editor.
 * @param   pchLine     Pointer to the line string.
 * @param   cchLine     The length of the line (sans newline).
 */
static bool EditorPutLine(FILEEDITOR *pEditor, const char *pchLine, size_t cchLine)
{
    EditorCheckString(pEditor, pchLine, cchLine, "EditorPutLine");

    size_t offNew = pEditor->cbNew;
    if (offNew + cchLine + 2 < pEditor->cbNewAlloc)
    {
        char *pszNew = pEditor->pszNew;
        memcpy(&pszNew[offNew], pchLine, cchLine);
        offNew += cchLine;
        pszNew[offNew++] = '\r';
        pszNew[offNew++] = '\n';
        pszNew[offNew]   = '\0';
        pEditor->cbNew   = offNew;
        return true;
    }
    pEditor->fOverflowed = true;
    return false;
}


/**
 * Writes a string to the output buffer.
 *
 * @returns true on success, false on overflow (error displayed and sets
 *          fOverflowed on the editor).
 *
 * @param   pEditor     Pointer to the editor.
 * @param   pchString   Pointer to the string.
 * @param   cchString   The length of the string.
 */
static bool EditorPutStringN(FILEEDITOR *pEditor, const char *pchString, size_t cchString)
{
    EditorCheckString(pEditor, pchString, cchString, "EditorPutStringN");

    size_t offNew = pEditor->cbNew;
    if (offNew + cchString < pEditor->cbNewAlloc)
    {
        char *pszNew = pEditor->pszNew;
        memcpy(&pszNew[offNew], pchString, cchString);
        offNew += cchString;
        pszNew[offNew]   = '\0';
        pEditor->cbNew   = offNew;
        return true;
    }
    pEditor->fOverflowed = true;
    return false;
}


/**
 * Checks the editor state and makes the editing was successful.
 */
static RTEXITCODE EditorCheckState(FILEEDITOR *pEditor, const char *pszFilename)
{
    if (pEditor->fOverflowed)
        return ErrorNStrings(RT_STR_TUPLE("Editor overflowed while modifying \""), pszFilename, -1, RT_STR_TUPLE("\""));
    if (pEditor->cBogusChars > 0)
        return ErrorNStrings(RT_STR_TUPLE("Editing failed because \""), pszFilename, -1,
                             RT_STR_TUPLE("\" contains bogus control characters (see above)"));
    return RTEXITCODE_SUCCESS;
}


/**
 * Simplistic case-insensitive memory compare function.
 */
static int MyMemICmp(void const *pv1, void const *pv2, size_t cb)
{
    char const *pch1 = (const char *)pv1;
    char const *pch2 = (const char *)pv2;
    while (cb-- > 0)
    {
        char ch1 = *pch1++;
        char ch2 = *pch2++;
        if (   ch1 != ch2
            && RT_C_TO_UPPER(ch1) != RT_C_TO_UPPER(ch2))
            return (int)ch1 - (int)ch2;
    }
    return 0;
}


/**
 * Matches a word deliminated by space of @a chAltSep.
 *
 * @returns true if matched, false if not.
 * @param   pchLine     The line we're working on.
 * @param   poff        The current line offset.  Updated on match.
 * @param   cchLine     The current line length.
 * @param   pszWord     The word to match with.
 * @param   cchWord     The length of the word to match.
 * @param   chAltSep    Alternative word separator, optional.
 */
static bool MatchWord(const char *pchLine, size_t *poff, size_t cchLine, const char *pszWord, size_t cchWord, char chAltSep = ' ')
{
    size_t off = *poff;
    pchLine += off;
    cchLine -= off;
    if (cchWord <= cchLine)
        if (MyMemICmp(pchLine, pszWord, cchWord) == 0)
            if (   cchWord == cchLine
                || RT_C_IS_BLANK(pchLine[cchWord])
                || pchLine[cchWord] == chAltSep)
            {
                *poff += cchWord;
                return true;
            }
    return false;
}


/**
 * Checks if the path @a pchLine[@a off] ends with @a pszFilename, ignoring case.
 *
 * @returns true if filename found, false if not.
 * @param   pchLine             The line we're working on.
 * @param   off                 The current line offset where the image path of
 *                              a DEVICE or IFS statement starts.
 * @param   cchLine             The current line length.
 * @param   pszFilename         The filename (no path) to match with, all upper
 *                              cased.
 * @param   cchFilename         The length of the filename to match with.
 */
static bool MatchOnlyFilename(const char *pchLine, size_t off, size_t cchLine, const char *pszFilename, size_t cchFilename)
{
    pchLine += off;
    cchLine -= off;

    /*
     * Skip ahead in pchString till we get to the filename.
     */
    size_t offFilename = 0;
    size_t offCur      = 0;
    if (   cchLine > 2
        && pchLine[1] == ':'
        && RT_C_IS_ALPHA(pchLine[0]))
        offCur += 2;
    while (offCur < cchLine)
    {
        char ch = pchLine[offCur];
        if (RTPATH_IS_SLASH(pchLine[offCur]))
            offFilename = offCur + 1;
        else if (RT_C_IS_BLANK(ch))
            break;
        offCur++;
    }
    size_t const cchLeftFilename = offCur - offFilename;
#ifdef DEBUG_MATCHING
    WriteNStrings(g_hStdOut, RT_STR_TUPLE("debug: MatchOnlyFilename: '"), &pchLine[offFilename], cchLeftFilename,
                  RT_STR_TUPLE("' vs '"), pszFilename, cchFilename, RT_STR_TUPLE("'\r\n"), NULL, 0);
#endif

    /*
     * Check if the length matches.
     */
    if (cchLeftFilename != cchFilename)
        return false;

    /*
     * Check if the filenames matches (ASSUMES right side is uppercased).
     */
    pchLine += offFilename;
    while (cchFilename-- > 0)
    {
        if (RT_C_TO_UPPER(*pchLine) != *pszFilename)
            return false;
        pchLine++;
        pszFilename++;
    }
#ifdef DEBUG_MATCHING
    WriteStrings(g_hStdOut, "debug: MatchOnlyFilename: -> true\r\n", NULL);
#endif
    return true;
}


static bool MatchPath(const char *pchPath1, size_t cchPath1, const char *pchPath2, size_t cchPath2)
{
#ifdef DEBUG_MATCHING
    WriteNStrings(g_hStdOut, RT_STR_TUPLE("debug: MatchPath: '"), pchPath1, cchPath1,
                  RT_STR_TUPLE("' vs '"), pchPath2, cchPath2, RT_STR_TUPLE("'\r\n"), NULL, 0);
#endif

    while (cchPath1 > 0 && cchPath2 > 0)
    {
        char const ch1 = *pchPath1++;
        cchPath1--;
        char const ch2 = *pchPath2++;
        cchPath2--;

        /* Slashes are special as it generally doesn't matter how many are in
           a row, at least no on decent systems. */
        if (RTPATH_IS_SLASH(ch1))
        {
            if (!RTPATH_IS_SLASH(ch2))
                return false;
            while (cchPath1 > 0 && RTPATH_IS_SLASH(*pchPath1))
                pchPath1++, cchPath1--;
            while (cchPath2 > 0 && RTPATH_IS_SLASH(*pchPath2))
                pchPath2++, cchPath2--;
        }
        /* Just uppercase before comparing to save space. */
        else if (RT_C_TO_UPPER(ch1) != RT_C_TO_UPPER(ch2))
            return false;
    }

    /* Ignore trailing slashes before reaching a conclusion. */
    while (cchPath1 > 0 && RTPATH_IS_SLASH(*pchPath1))
        pchPath1++, cchPath1--;
    while (cchPath2 > 0 && RTPATH_IS_SLASH(*pchPath2))
        pchPath2++, cchPath2--;

#ifdef DEBUG_MATCHING
    if (cchPath1 == 0 && cchPath2 == 0)
        WriteStrings(g_hStdOut, "debug: MatchPath: -> true\r\n", NULL);
#endif
    return cchPath1 == 0 && cchPath2 == 0;
}


/*********************************************************************************************************************************
*   Installation Steps.                                                                                                          *
*********************************************************************************************************************************/

/**
 * Checks that the necessary GRADD components are present.
 */
static RTEXITCODE CheckForGradd(void)
{
    strcpy(&g_szBootDrivePath[g_cchBootDrivePath], "OS2\\DLL\\GENGRADD.DLL");
    FILESTATUS3 FileSts;
    APIRET rc = DosQueryPathInfo(g_szBootDrivePath, FIL_STANDARD, &FileSts, sizeof(FileSts));
    if (rc != NO_ERROR)
        return ApiErrorN(rc, 3, "DosQueryPathInfo(\"", g_szBootDrivePath, "\",,,) [installed gengradd?] ");

    /* Note! GRADD precense in Config.sys is checked below while modifying it. */
    return RTEXITCODE_SUCCESS;
}


/** Adds DEVICE=[path]\\VBoxGuest.sys to the modified Config.sys. */
static bool ConfigSysAddVBoxGuest(void)
{
    EditorPutStringN(&g_ConfigSys, RT_STR_TUPLE("DEVICE="));
    EditorPutStringN(&g_ConfigSys, g_szDstPath, g_cchDstPath);
    EditorPutLine(&g_ConfigSys, RT_STR_TUPLE("VBoxGuest.sys"));
    return true;
}


/** Adds IFS=[path]\\VBoxSF.IFS to the modified Config.sys. */
static bool ConfigSysAddVBoxSF(void)
{
    EditorPutStringN(&g_ConfigSys, RT_STR_TUPLE("IFS="));
    EditorPutStringN(&g_ConfigSys, g_szDstPath, g_cchDstPath);
    EditorPutLine(&g_ConfigSys, RT_STR_TUPLE("VBoxSF.ifs"));
    return true;
}


/** Adds DEVICE=[path]\\VBoxMouse.sys to the modified Config.sys. */
static bool ConfigSysAddVBoxMouse(void)
{
    EditorPutStringN(&g_ConfigSys, RT_STR_TUPLE("DEVICE="));
    EditorPutStringN(&g_ConfigSys, g_szDstPath, g_cchDstPath);
    EditorPutLine(&g_ConfigSys, RT_STR_TUPLE("VBoxMouse.sys"));
    return true;
}


/**
 * Strips leading and trailing spaces and commas from the given substring.
 *
 * This is for GRADD_CHAINS and friends.
 */
static size_t StripGraddList(const char **ppch, size_t cch)
{
    const char *pch = *ppch;
    while (   cch > 0
           && (   RT_C_IS_BLANK(pch[0])
               || pch[0] == ',') )
        cch--, pch++;
    *ppch = pch;

    while (   cch > 0
           && (   RT_C_IS_BLANK(pch[cch - 1])
               || pch[cch - 1] == ',') )
        cch--;
    return cch;
}


/**
 * Prepares the config.sys modifications.
 */
static RTEXITCODE PrepareConfigSys(void)
{
    if (g_fSkipMask & SKIP_CONFIG_SYS)
        return RTEXITCODE_SUCCESS;

    strcpy(&g_szBootDrivePath[g_cchBootDrivePath], "CONFIG.SYS");
    RTEXITCODE rcExit = EditorReadInFile(&g_ConfigSys, g_szBootDrivePath, 4096, true /*fMustExist*/);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Figure out which IFS we should place ourselves after by examining the
     * destination path's file system, assuming HPFS if we cannot figure it out.
     */
    const char *pszAfterIfs =        "HPFS.IFS";
    size_t      cchAfterIfs = sizeof("HPFS.IFS") - 1;

    union
    {
        FSQBUFFER2 FsQBuf;
        uint8_t abPadding[1024];
    } u;
    memset(&u, 0, sizeof(u));
    ULONG cbBuf = sizeof(u) - 8 /* for adding .IFS */;

    char szDrv[4];
    szDrv[0] = g_szDstPath[0];
    szDrv[1] = g_szDstPath[1];
    szDrv[2] = '\0';

    APIRET rc = DosQueryFSAttach(szDrv, 0 /*iOrdinal*/, FSAIL_QUERYNAME, &u.FsQBuf, &cbBuf);
    if (   rc == NO_ERROR
        || (rc == ERROR_BUFFER_OVERFLOW && u.FsQBuf.cbFSDName > 2 && u.FsQBuf.cbFSDName <= 7))
    {
        char *pszFsdName = (char *)&u.FsQBuf.szName[u.FsQBuf.cbName + 1];
        if (   RT_C_IS_ALNUM(pszFsdName[0])
            && RT_C_IS_ALNUM(pszFsdName[1])
            && pszFsdName[u.FsQBuf.cbFSDName] == '\0')
        {
            /* MatchOnlyFilename requires it to be all uppercase (should be the case already). */
            size_t off = u.FsQBuf.cbFSDName;
            while (off-- > 0)
                pszFsdName[off] = RT_C_TO_UPPER(pszFsdName[off]);

            /* Add the IFS suffix. */
            strcpy(&pszFsdName[u.FsQBuf.cbFSDName], ".IFS");
            pszAfterIfs = pszFsdName;
            cchAfterIfs = u.FsQBuf.cbFSDName + sizeof(".IFS") - 1;

            if (g_fVerbose)
                WriteStrings(g_hStdOut, "info: Found \"IFS=", pszFsdName, "\" for ", pszFsdName, "\r\n", NULL);
        }
        else
        {
            pszFsdName[10] = '\0';
            ApiErrorN(ERROR_INVALID_NAME, 5, "Bogus FSD name \"", pszFsdName, "\" for ", szDrv, " - assuming HPFS");
        }
    }
    else
        ApiErrorN(rc, 3, "DosQueryFSAttach(", szDrv, ") - assuming HPFS");

    /*
     * Do a scan to locate where to insert ourselves and such.
     */
    char        szLineNo[32];
    char        szNumBuf[32];
    bool        fInsertedGuest = false;
    bool        fInsertedMouse = RT_BOOL(g_fSkipMask & SKIP_MOUSE);
    bool        fPendingMouse  = false;
    bool        fInsertedIfs   = RT_BOOL(g_fSkipMask & SKIP_SHARED_FOLDERS);
    unsigned    cPathsFound    = 0;
    const char *pchGraddChains = "C1";
    size_t      cchGraddChains = sizeof("C1") - 1;
    const char *pchGraddChain1 = NULL;
    size_t      cchGraddChain1 = NULL;
    unsigned    iLine  = 0;
    size_t      offSrc = 0;
    const char *pchLine;
    size_t      cchLine;
    while ((offSrc = EditorGetLine(&g_ConfigSys, offSrc, &pchLine, &cchLine)) != 0)
    {
        iLine++;

        size_t off = 0;
#define SKIP_BLANKS() \
            while (off < cchLine && RT_C_IS_BLANK(pchLine[off])) \
                off++

        bool fDone = false;
        SKIP_BLANKS();

        /*
         * Add the destination directory to the PATH.
         * If there are multiple SET PATH statements, we add ourselves to all of them.
         */
        if (MatchWord(pchLine, &off, cchLine, RT_STR_TUPLE("SET")))
        {
            SKIP_BLANKS();
            if (MatchWord(pchLine, &off, cchLine, RT_STR_TUPLE("PATH"), '='))
            {
                SKIP_BLANKS();
                if (cchLine > off && pchLine[off] == '=')
                {
                    off++;
                    SKIP_BLANKS();

                    if (g_fVerbose)
                        WriteStrings(g_hStdOut, "info: Config.sys line ", MyNumToString(szLineNo, iLine), ": SET PATH\r\n", NULL);

                    /* Strip trailing spaces and semicolons. */
                    while (cchLine > off && (RT_C_IS_BLANK(pchLine[cchLine - 1]) || pchLine[cchLine - 1] == ';'))
                        cchLine--;

                    /* Remove any previous entries of the destination directory. */
                    unsigned iElement   = 0;
                    char     chLast     = 0;
                    uint32_t cchWritten = 0;
                    while (off < cchLine)
                    {
                        iElement++;
                        const char *pszElement   = &pchLine[off];
                        const char *pszSemiColon = (const char *)memchr(&pchLine[off], ';', cchLine - off);
                        size_t      cchElement   = (pszSemiColon ? pszSemiColon : &pchLine[cchLine]) - pszElement;
                        if (MatchPath(pszElement, cchElement, g_szDstPath, g_cchDstPath - (g_cchDstPath > 3 ? 1 : 0)))
                        {
                            if (g_fVerbose)
                                WriteNStrings(g_hStdOut, RT_STR_TUPLE("info: Config.sys line "),
                                              MyNumToString(szLineNo, iLine), -1, RT_STR_TUPLE(": Removing PATH element #"),
                                              MyNumToString(szNumBuf, iElement), -1, RT_STR_TUPLE(" \""), pszElement, cchElement,
                                              RT_STR_TUPLE("\"\r\n"), NULL, 0);
                            EditorPutStringN(&g_ConfigSys, &pchLine[cchWritten], off - cchWritten);
                            chLast = pchLine[off - 1];
                            cchWritten = off + cchElement + (chLast == ';');
                        }
                        off += cchElement + 1;
                    }

                    /* Write out the rest of the line and append the destination directory to it. */
                    if (cchLine > cchWritten)
                    {
                        EditorPutStringN(&g_ConfigSys, &pchLine[cchWritten], cchLine - cchWritten);
                        chLast = pchLine[cchLine - 1];
                    }
                    if (chLast != ';')
                        EditorPutStringN(&g_ConfigSys, RT_STR_TUPLE(";"));
                    EditorPutStringN(&g_ConfigSys, g_szDstPath, g_cchDstPath - (g_cchDstPath > 3 ? 1 : 0));
                    EditorPutLine(&g_ConfigSys, RT_STR_TUPLE(";"));
                    fDone = true;

                    cPathsFound += 1;
                }
            }
            /*
             * Look for the GRADD_CHAINS variable.
             *
             * It is a comma separated list of chains (other env.vars.), however
             * we can only deal with a single element.  This shouldn't be an issue
             * as GRADD_CHAINS is standardized by COMGRADD.DSP to the value C1, so
             * other values can only be done by users or special drivers.
             */
            else if (MatchWord(pchLine, &off, cchLine, RT_STR_TUPLE("GRADD_CHAINS"), '='))
            {
                SKIP_BLANKS();
                if (cchLine > off && pchLine[off] == '=')
                {
                    off++;

                    const char *pchNew   = &pchLine[off];
                    size_t      cchNew   = StripGraddList(&pchNew, cchLine - off);

                    const char *pszComma = (const char *)memchr(pchNew, ',', cchNew);
                    if (pszComma)
                    {
                        cchNew = StripGraddList(&pchNew, pchNew - pszComma);
                        WriteStrings(g_hStdOut, "warning: Config.sys line ", MyNumToString(szLineNo, iLine),
                                     "GRADD_CHAINS contains more than one element.  Ignoring all but the first.\r\n", NULL);
                    }

                    /* If it differs from the default "C1" / previous value, we must
                       restart the search for the primary chain environment variable.
                       This means that chains values other than "C1" must come after
                       the GRADD_CHAINS statement, since we're not doing an extra pass. */
                    if (   cchGraddChains != cchNew
                        || MyMemICmp(pchNew, pchGraddChains, cchNew) == 0)
                    {
                        pchGraddChains = pchNew;
                        cchGraddChains = cchNew;
                        pchGraddChain1 = NULL;
                        cchGraddChain1 = 0;
                    }

                    if (g_fVerbose)
                        WriteNStrings(g_hStdOut, RT_STR_TUPLE("info: Config.sys line "), MyNumToString(szLineNo, iLine), - 1,
                                      RT_STR_TUPLE(": SET GRADD_CHAINS="), &pchLine[off], cchLine - off,
                                      RT_STR_TUPLE("\r\n"), NULL, 0);
                }
            }
            /*
             * Look for the chains listed by GRADD_CHAINS.
             */
            else if (MatchWord(pchLine, &off, cchLine, pchGraddChains, cchGraddChains, '='))
            {
                SKIP_BLANKS();
                if (cchLine > off && pchLine[off] == '=')
                {
                    off++;
                    SKIP_BLANKS();

                    /* Just save it, we'll validate it after processing everything. */
                    pchGraddChain1 = &pchLine[off];
                    cchGraddChain1 = StripGraddList(&pchGraddChain1, cchLine - off);

                    if (g_fVerbose)
                        WriteNStrings(g_hStdOut, RT_STR_TUPLE("info: Config.sys line "), MyNumToString(szLineNo, iLine), - 1,
                                      RT_STR_TUPLE(": Found GRADD chain "), pchGraddChains, cchGraddChains,
                                      RT_STR_TUPLE(" with value: "), pchGraddChain1, cchGraddChain1,
                                      RT_STR_TUPLE("\r\n"), NULL, 0);
                }
            }
        }
        /*
         * Look for that IFS that should be loaded before we can load our drivers.
         */
        else if (MatchWord(pchLine, &off, cchLine, RT_STR_TUPLE("IFS"), '='))
        {
            SKIP_BLANKS();
            if (cchLine > off && pchLine[off] == '=')
            {
                off++;
                SKIP_BLANKS();
                if (MatchOnlyFilename(pchLine, off, cchLine, pszAfterIfs, cchAfterIfs))
                {
                    if (g_fVerbose)
                        WriteStrings(g_hStdOut, "info: Config.sys line ", MyNumToString(szLineNo, iLine),
                                     ": Found IFS=", pszAfterIfs, "\r\n", NULL);
                    EditorPutLine(&g_ConfigSys, pchLine, cchLine);
                    fDone = true;

                    if (!fInsertedGuest)
                        fInsertedGuest = ConfigSysAddVBoxGuest();
                    if (!fInsertedIfs)
                        fInsertedIfs   = ConfigSysAddVBoxSF();
                    if (fPendingMouse && !fInsertedMouse)
                        fInsertedMouse = ConfigSysAddVBoxMouse();
                }
                /* Remove old VBoxSF.IFS lines */
                else if (   !(g_fSkipMask & SKIP_SHARED_FOLDERS)
                         && (   MatchOnlyFilename(pchLine, off, cchLine, RT_STR_TUPLE("VBOXSF.IFS"))
                             || MatchOnlyFilename(pchLine, off, cchLine, RT_STR_TUPLE("VBOXFS.IFS")) ) )
                {
                    if (g_fVerbose)
                        WriteStrings(g_hStdOut, "info: Config.sys line ", MyNumToString(szLineNo, iLine),
                                     ": Removing old VBoxSF.ifs statement\r\n", NULL);
                    fDone = true;
                }
            }
        }
        /*
         * Look for the mouse driver we need to comment out / existing VBoxMouse.sys,
         * as well as older VBoxGuest.sys statements we should remove.
         */
        else if (MatchWord(pchLine, &off, cchLine, RT_STR_TUPLE("DEVICE"), '='))
        {
            SKIP_BLANKS();
            if (cchLine > off && pchLine[off] == '=')
            {
                off++;
                SKIP_BLANKS();
                if (   !(g_fSkipMask & SKIP_MOUSE)
                    && MatchOnlyFilename(pchLine, off, cchLine, RT_STR_TUPLE("MOUSE.SYS")))
                {
                    if (g_fVerbose)
                        WriteStrings(g_hStdOut, "info: Config.sys line ", MyNumToString(szLineNo, iLine),
                                     ": Found DEVICE=<path>\\MOUSE.SYS\r\n", NULL);
                    EditorPutStringN(&g_ConfigSys, RT_STR_TUPLE("REM "));
                    EditorPutLine(&g_ConfigSys, pchLine, cchLine);
                    fDone = true;

                    if (!fInsertedMouse)
                    {
                        if (fInsertedGuest) /* means we've found the IFS and can access the destination dir */
                            fInsertedMouse = ConfigSysAddVBoxMouse();
                        else
                            fPendingMouse = true;
                    }
                }
                /* Remove or replace old VBoxMouse.IFS lines */
                else if (   !(g_fSkipMask & SKIP_MOUSE)
                         && MatchOnlyFilename(pchLine, off, cchLine, RT_STR_TUPLE("VBOXMOUSE.SYS")))
                {
                    if (g_fVerbose)
                        WriteStrings(g_hStdOut, "info: Config.sys line ", MyNumToString(szLineNo, iLine), ": ",
                                     fInsertedMouse || !fInsertedGuest ? "Removing" : "Replacing",
                                     " old VBoxMouse.sys statement\r\n", NULL);
                    if (!fInsertedMouse)
                    {
                        if (fInsertedGuest) /* means we've found the IFS and can access the destination dir */
                            fInsertedMouse = ConfigSysAddVBoxMouse();
                        else
                            fPendingMouse = true;
                    }
                    fDone = true;
                }
                /* Remove old VBoxGuest.sys lines. */
                else if (MatchOnlyFilename(pchLine, off, cchLine, RT_STR_TUPLE("VBOXGUEST.SYS")))
                {
                    if (g_fVerbose)
                        WriteStrings(g_hStdOut, "info: Config.sys line ", MyNumToString(szLineNo, iLine),
                                     ": Removing old VBoxGuest.sys statement\r\n", NULL);
                    fDone = true;
                }
            }
        }
#undef SKIP_BLANKS

        /*
         * Output the current line if we didn't already do so above.
         */
        if (!fDone)
            EditorPutLine(&g_ConfigSys, pchLine, cchLine);
    }

    /*
     * If we've still got pending stuff, add it now at the end.
     */
    if (!fInsertedGuest)
        fInsertedGuest = ConfigSysAddVBoxGuest();
    if (!fInsertedIfs)
        fInsertedIfs   = ConfigSysAddVBoxSF();
    if (!fInsertedMouse)
        fInsertedMouse = ConfigSysAddVBoxMouse();

    if (!cPathsFound)
        WriteStrings(g_hStdErr, "warning: Found no SET PATH statement in Config.sys.\r\n", NULL);

    /*
     * If we're installing the graphics driver, check that GENGRADD is in the
     * primary GRADD chain.
     */
    if (!(g_fSkipMask & SKIP_GRAPHICS))
    {
        if (cchGraddChain1 > 0 && cchGraddChains > 0)
        {
            int idxGenGradd = -1;
            for (size_t off = 0, idx = 0; off < cchGraddChain1;)
            {
                const char *psz = &pchGraddChain1[off];
                size_t      cch = cchGraddChain1 - off;
                const char *pszComma = (const char *)memchr(psz, ',', cchGraddChain1 - off);
                if (!pszComma)
                    off += cch;
                else
                {
                    cch = pszComma - psz;
                    off += cch + 1;
                }
                while (cch > 0 && RT_C_IS_BLANK(*psz))
                    cch--, psz++;
                while (cch > 0 && RT_C_IS_BLANK(psz[cch - 1]))
                    cch--;
                if (   cch == sizeof("GENGRADD") - 1
                    && MyMemICmp(psz, RT_STR_TUPLE("GENGRADD")) == 0)
                {
                    idxGenGradd = idx;
                    break;
                }
                idx += cch != 0;
            }
            if (idxGenGradd < 0)
                return ErrorNStrings(RT_STR_TUPLE("Primary GRADD chain \""), pchGraddChains, cchGraddChains,
                                     RT_STR_TUPLE("="), pchGraddChain1, cchGraddChain1,
                                     RT_STR_TUPLE("\" does not contain a GENGRADD entry."), NULL, 0);
            if (idxGenGradd != 0)
                return ErrorNStrings(RT_STR_TUPLE("GENGRADD is not the first entry in the primary GRADD chain \""),
                                     pchGraddChains, cchGraddChains, RT_STR_TUPLE("="), pchGraddChain1, cchGraddChain1, NULL, 0);
        }
        else if (cchGraddChains > 0)
            return ErrorNStrings(RT_STR_TUPLE("Primary GRADD chain \""), pchGraddChains, cchGraddChains,
                                 RT_STR_TUPLE("\" not found (only searched after SET GRADD_CHAINS)."), NULL, 0);
        else
            return ErrorNStrings(RT_STR_TUPLE("No SET GRADD_CHAINS statement found in Config.sys"), NULL, 0);
    }

    return EditorCheckState(&g_ConfigSys, g_szBootDrivePath);
}


/** Puts the line starting VBoxService to Startup.cmd. */
static void StartupCmdPutLine(const char *pszLineNo)
{
    if (g_fVerbose)
        WriteStrings(g_hStdOut, "info: Starting VBoxService at line ", pszLineNo, " of Startup.cmd\r\n", NULL);
    EditorPutStringN(&g_StartupCmd, g_szDstPath, g_cchDstPath);
    EditorPutLine(&g_StartupCmd, RT_STR_TUPLE("VBoxService.exe"));
}


/**
 * Prepares the startup.cmd modifications.
 */
static RTEXITCODE PrepareStartupCmd(void)
{
    if (g_fSkipMask & SKIP_STARTUP_CMD)
        return RTEXITCODE_SUCCESS;

    strcpy(&g_szBootDrivePath[g_cchBootDrivePath], "STARTUP.CMD");
    RTEXITCODE rcExit = EditorReadInFile(&g_StartupCmd, g_szBootDrivePath, 1024, false /*fMustExist*/);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Scan startup.cmd and see if there is an [@]ECHO OFF without anything other
     * than REM statements preceding it.  If there is we'll insert ourselves after
     * that, otherwise we'll just jump in at the top.
     */
    unsigned    iInsertBeforeLine = 0;
    unsigned    iLine             = 0;
    size_t      offSrc            = 0;
    const char *pchLine;
    size_t      cchLine;
    while ((offSrc = EditorGetLine(&g_StartupCmd, offSrc, &pchLine, &cchLine)) != 0)
    {
        iLine++;

        size_t off = 0;
#define SKIP_BLANKS() \
        while (off < cchLine && RT_C_IS_BLANK(pchLine[off])) \
            off++
        SKIP_BLANKS();
        if (off < cchLine && pchLine[off] == '@')
        {
            off++;
            SKIP_BLANKS();
        }
        if (MatchWord(pchLine, &off, cchLine, RT_STR_TUPLE("ECHO")))
        {
            SKIP_BLANKS();

            if (MatchWord(pchLine, &off, cchLine, RT_STR_TUPLE("OFF")))
            {
                iInsertBeforeLine = iLine + 1;
                break;
            }
        }
        else if (MatchWord(pchLine, &off, cchLine, RT_STR_TUPLE("REM")))
        { /* skip */ }
        else
            break;
    }

    /*
     * Make the modifications.
     */
    if (iInsertBeforeLine == 0) /* Necessary to do this outside the loop in case startup.cmd is empty or non-existing. */
        StartupCmdPutLine("1");

    offSrc = iLine = 0;
    while ((offSrc = EditorGetLine(&g_StartupCmd, offSrc, &pchLine, &cchLine)) != 0)
    {
        char szLineNo[32];
        iLine++;
        if (iLine == iInsertBeforeLine)
            StartupCmdPutLine(MyNumToString(szLineNo, iLine));

        /*
         * Filter out old VBoxService lines.  To be on the safe side we skip
         * past DETACH, CALL, and START before checking for VBoxService.
         */
        size_t off = 0;
        SKIP_BLANKS();
        if (off < cchLine && pchLine[off] == '@')
        {
            off++;
            SKIP_BLANKS();
        }

        if (MatchWord(pchLine, &off, cchLine, RT_STR_TUPLE("DETACH")))
            SKIP_BLANKS();

        if (MatchWord(pchLine, &off, cchLine, RT_STR_TUPLE("CALL")))
            SKIP_BLANKS();

        if (MatchWord(pchLine, &off, cchLine, RT_STR_TUPLE("START")))
            SKIP_BLANKS();

        if (   MatchOnlyFilename(pchLine, off, cchLine, RT_STR_TUPLE("VBOXSERVICE.EXE"))
            || MatchOnlyFilename(pchLine, off, cchLine, RT_STR_TUPLE("VBOXSERVICE")) )
        {
            if (g_fVerbose)
                WriteStrings(g_hStdOut, "info: Removing old VBoxService statement on line ",
                             MyNumToString(szLineNo, iLine), "\r\n", NULL);
        }
        else
            EditorPutLine(&g_StartupCmd, pchLine, cchLine);

#undef SKIP_BLANKS
    }

    return EditorCheckState(&g_StartupCmd, g_szBootDrivePath);
}


/**
 * Tells the loader to cache all the pages in @a pszFile and close it, so that
 * we can modify or replace it.
 */
static void CacheLdrFile(const char *pszFile)
{
    if (g_fVerbose)
        DoWriteNStr(g_hStdOut, RT_STR_TUPLE("info: Sharing violation - applying DosReplaceModule...\r\n"));

    APIRET rc = DosReplaceModule(pszFile, NULL, NULL);
    if (rc != NO_ERROR)
        ApiErrorN(rc, 3, "DosReplaceModule(\"", pszFile, "\",,)");
}


/**
 * Worker for CopyFiles that handles one copying operation.
 */
static RTEXITCODE CopyOneFile(const char *pszSrc, const char *pszDst)
{
    FILESTATUS3 FileSts;
    if (g_fVerbose)
        WriteStrings(g_hStdOut, "info: Copying \"", pszSrc, "\" to \"", pszDst, "\"...\r\n", NULL);

    if (g_fRealRun)
    {
        /* Make sure the destination file isn't read-only before attempting to copying it. */
        APIRET rc = DosQueryPathInfo(pszDst, FIL_STANDARD, &FileSts, sizeof(FileSts));
        if (rc == NO_ERROR && (FileSts.attrFile & FILE_READONLY))
        {
            FileSts.attrFile &= ~FILE_READONLY;

            /* Don't update the timestamps: */
            *(USHORT *)&FileSts.fdateCreation   = 0;
            *(USHORT *)&FileSts.ftimeCreation   = 0;
            *(USHORT *)&FileSts.fdateLastAccess = 0;
            *(USHORT *)&FileSts.ftimeLastAccess = 0;
            *(USHORT *)&FileSts.fdateLastWrite  = 0;
            *(USHORT *)&FileSts.ftimeLastWrite  = 0;

            rc = DosSetPathInfo(pszDst, FIL_STANDARD, &FileSts, sizeof(FileSts), 0 /*fOptions*/);
            if (rc == ERROR_SHARING_VIOLATION)
            {
                CacheLdrFile(pszDst);
                rc = DosSetPathInfo(pszDst, FIL_STANDARD, &FileSts, sizeof(FileSts), 0 /*fOptions*/);
            }

            if (rc != NO_ERROR)
                ApiErrorN(rc, 3, "DosSetPathInfo(\"", pszDst, "\",~READONLY,)");
        }

        /* Do the copying. */
        rc = DosCopy(pszSrc, pszDst, DCPY_EXISTING);
        if (rc == NO_ERROR)
            return RTEXITCODE_SUCCESS;
        if (rc != ERROR_SHARING_VIOLATION)
            return ApiErrorN(rc, 3, "Failed copying to \"", pszDst, "\"");

        CacheLdrFile(pszDst);
        rc = DosCopy(pszSrc, pszDst, DCPY_EXISTING);
        if (rc == NO_ERROR)
            return RTEXITCODE_SUCCESS;
        return ApiErrorN(rc, 3, "Failed copying to \"", pszDst, "\"");
    }
    /*
     * Dry run: just check that the source file exists.
     */
    else
    {
        APIRET rc = DosQueryPathInfo(pszSrc, FIL_STANDARD, &FileSts, sizeof(FileSts));
        if (rc == NO_ERROR)
            return RTEXITCODE_SUCCESS;
        return ApiErrorN(rc, 3, "DosQueryPathInfo failed on \"", pszSrc, "\"");
    }
}


/**
 * Copies the GA files.
 */
static RTEXITCODE CopyFiles(void)
{
    if (g_fRealRun)
    {
        /*
         * Create the install directory.  We do this from the root up as that is
         * a nice feature and saves us dealing with trailing slash troubles.
         */
        char *psz = g_szDstPath;
        if (psz[1] == ':' && RTPATH_IS_SLASH(psz[2]))
            psz += 3;
        else if (psz[1] == ':')
            psz += 2;
        else
            return ApiError("Unexpected condition", __LINE__);

        for (;;)
        {
            char ch;
            while ((ch = *psz) != '\0' && !RTPATH_IS_SLASH(ch))
                psz++;
            if (ch != '\0')
                *psz = '\0';
            APIRET rc = DosMkDir(g_szDstPath, 0);
            if (rc != NO_ERROR && rc != ERROR_ACCESS_DENIED /*HPFS*/ && rc != ERROR_ALREADY_EXISTS /*what one would expect*/)
                return ApiErrorN(rc, 3, "DosMkDir(\"", g_szDstPath, "\")");
            if (ch == '\0')
                break;
            *psz++ = ch;
            while ((ch = *psz) != '\0' && RTPATH_IS_SLASH(ch))
                psz++;
            if (ch == '\0')
                break;
        }
    }

    /*
     * Start copying files.  We copy all files into the directory regardless
     * of whether they will be referenced by config.sys, startup.cmd or whatever.
     */
    static struct
    {
        const char *pszFile;
        const char *pszAltDst;
        uint8_t     fSkipMask;
    } const s_aFiles[] =
    {
        { "VBoxService.exe",    NULL, 0 }, /* first as likely to be running */
        { "VBoxControl.exe",    NULL, 0 },
        { "VBoxReplaceDll.exe", NULL, 0 },
        { "gengradd.dll",       "OS2\\DLL\\gengradd.dll", SKIP_GRAPHICS },
        { "libc06.dll",         "OS2\\DLL\\libc06.dll",  SKIP_LIBC_DLLS },
        { "libc061.dll",        "OS2\\DLL\\libc061.dll", SKIP_LIBC_DLLS },
        { "libc062.dll",        "OS2\\DLL\\libc062.dll", SKIP_LIBC_DLLS },
        { "libc063.dll",        "OS2\\DLL\\libc063.dll", SKIP_LIBC_DLLS },
        { "libc064.dll",        "OS2\\DLL\\libc064.dll", SKIP_LIBC_DLLS },
        { "libc065.dll",        "OS2\\DLL\\libc065.dll", SKIP_LIBC_DLLS },
        { "libc066.dll",        "OS2\\DLL\\libc066.dll", SKIP_LIBC_DLLS },
        { "VBoxGuest.sys",      NULL, 0 },
        { "VBoxSF.ifs",         NULL, 0 },
        { "vboxmouse.sys",      NULL, 0 },
        { "readme.txt",         NULL, 0 },
    };

    RTEXITCODE rcExit;
    for (size_t i = 0; i < RT_ELEMENTS(s_aFiles); i++)
    {
        /* Always copy files to the destination folder. */
        strcpy(&g_szSrcPath[g_cchSrcPath], s_aFiles[i].pszFile);
        strcpy(&g_szDstPath[g_cchDstPath], s_aFiles[i].pszFile);
        RTEXITCODE rcExit2 = CopyOneFile(g_szSrcPath, g_szDstPath);
        if (rcExit2 != RTEXITCODE_SUCCESS)
            rcExit = rcExit2;

        /* Additional install location and this not being skipped? */
        if (   s_aFiles[i].pszAltDst
            && !(s_aFiles[i].fSkipMask & g_fSkipMask) /* ASSUMES one skip bit per file */)
        {
            strcpy(&g_szBootDrivePath[g_cchBootDrivePath], s_aFiles[i].pszAltDst);

            rcExit2 = CopyOneFile(g_szSrcPath, g_szBootDrivePath);
            if (rcExit2 != RTEXITCODE_SUCCESS)
                rcExit = rcExit2;
        }
    }

    return rcExit;
}


/**
 * Writes out the modified config.sys.
 */
static RTEXITCODE WriteConfigSys(void)
{
    if (g_fSkipMask & SKIP_CONFIG_SYS)
        return RTEXITCODE_SUCCESS;
    strcpy(&g_szBootDrivePath[g_cchBootDrivePath], "CONFIG.SYS");
    return EditorWriteOutFile(&g_ConfigSys, g_szBootDrivePath);
}


/**
 * Writes out the modified startup.cmd.
 */
static RTEXITCODE WriteStartupCmd(void)
{
    if (g_fSkipMask & SKIP_CONFIG_SYS)
        return RTEXITCODE_SUCCESS;
    strcpy(&g_szBootDrivePath[g_cchBootDrivePath], "STARTUP.CMD");
    return EditorWriteOutFile(&g_StartupCmd, g_szBootDrivePath);
}


/*********************************************************************************************************************************
*   Option parsing and such.                                                                                                     *
*********************************************************************************************************************************/

static RTEXITCODE ShowUsage(void)
{
    static const char g_szUsage[] =
        VBOX_PRODUCT " OS/2 Additions Installer " VBOX_VERSION_STRING "\r\n"
        "Copyright (C) 2005-" VBOX_C_YEAR " " VBOX_VENDOR "\r\n"
        "\r\n"
        "This is a very barebone OS/2 guest additions installer which main purpose is\r\n"
        "to help with unattended installation.  Do not expect it to handle complicated\r\n"
        "situations like upgrades and similar.  It also does not understand arguments\r\n"
        "that are placed in double quotes.\r\n"
        "\r\n"
        "Usage: VBoxIs2AdditionsInstall.exe [options]\r\n"
        "   or  VBoxIs2AdditionsInstall.exe <-h|-?|--help>\r\n"
        "   or  VBoxIs2AdditionsInstall.exe <-v|--version>\r\n"
        "\r\n"
        "Options:\r\n"
        "  -i, --do-install         / -z, --dry-run\r\n"
        "      Controls whether to do a real install or not.  Default: --dry-run\r\n"
        "  -s<path>, --source[=]<path>\r\n"
        "      Specifies where the files to install are.  Default: Same as installer\r\n"
        "  -d<path>, --destination[=]<path>\r\n"
        "      Specifies where to install all the VBox OS/2 additions files.\r\n"
        "      Default: C:\VBoxAdd  (C is replaced by actual boot drive)\r\n"
        "  -b<path>, --boot-drive[=]<path>\r\n"
        "      Specifies the boot drive.  Default: C: (C is replaced by the actual one)\r\n"
        "  -F, --no-shared-folders  /  -f, --shared-folders (default)\r\n"
        "      Controls whether to put the shared folders IFS in Config.sys.\r\n"
        "  -G, --no-graphics        /  -g, --graphics (default)\r\n"
        "      Controls whether to replace OS2\\DLL\\GENGRADD.DLL with the VBox version.\r\n"
        "  -M, --no-mouse           /  -m, --mouse (default)\r\n"
        "      Controls whether to add the VBox mouse driver to Config.sys and disable\r\n"
        "      the regular OS/2 one.\r\n"
        "  -S, --no-service         /  -s, --service (default)\r\n"
        "      Controls whether to add starting VBoxService from Startup.cmd.\r\n"
        "  -T, --no-startup-cmd     /  -t, --startup-cmd (default)\r\n"
        "      Controls whether to modify Startup.cmd.\r\n"
        "  -C, --no-config-sys      /  -c, --config-sys (default)\r\n"
        "      Controls whether to modify Config.sys.\r\n"
        "  -L, --no-libc-dlls       /  -l, --libc-dlls (default)\r\n"
        "      Controls whether copy the kLibC DLLs to OS2\\DLLS.\r\n"
        "  -q, --quiet              /  -V, --verbose (default)\r\n"
        "      Controls the installer noise level.\r\n"
        "\r\n"
        "Exit Codes:\r\n"
        "   0 - Success. Reboot required.\r\n"
        "   1 - Failure.\r\n"
        "   2 - Syntax error.\r\n"
        ;
    DoWriteNStr(g_hStdOut, RT_STR_TUPLE(g_szUsage));
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE ShowVersion(void)
{
    DoWriteNStr(g_hStdOut, RT_STR_TUPLE(VBOX_VERSION_STRING " r"));

    const char *pszRev = "$Rev: 106320 $";
    while (!RT_C_IS_DIGIT(*pszRev))
        pszRev++;
    size_t cchRev = 1;
    while (RT_C_IS_DIGIT(pszRev[cchRev]))
        cchRev++;
    DoWriteNStr(g_hStdOut, pszRev, cchRev);

    DoWriteNStr(g_hStdOut, RT_STR_TUPLE("\r\n"));
    return RTEXITCODE_SUCCESS;
}


static bool MatchOptWord(PSZ *ppsz, const char *pszWord, size_t cchWord, bool fTakeValue = false)
{
    PSZ psz = *ppsz;
    if (strncmp(psz, pszWord, cchWord) == 0)
    {
        psz += cchWord;
        CHAR ch = *psz;
        if (ch == '\0')
        {
            /* No extra complaining needed when fTakeValue is true, as values must be non-empty strings. */
            *ppsz = psz;
            return true;
        }
        if (RT_C_IS_SPACE(ch))
        {
            if (fTakeValue)
                do
                {
                    ch = *++psz;
                } while (RT_C_IS_SPACE(ch));
            *ppsz = psz;
            return true;
        }
        if (fTakeValue && (ch == ':' || ch == '='))
        {
            *ppsz = psz + 1;
            return true;
        }
    }
    return false;
}


static PSZ GetOptValue(PSZ psz, const char *pszOption, char *pszValue, size_t cbValue)
{
    PSZ const pszStart = psz;
    CHAR      ch       = *psz;
    if (ch != '\0' && RT_C_IS_SPACE(ch))
    {
        do
            ch = *++psz;
        while (ch != '\0' && !RT_C_IS_SPACE(ch));

        size_t const cchSrc = psz - pszStart;
        if (cchSrc < cbValue)
        {
            memcpy(pszValue, pszStart, cchSrc);
            pszValue[cchSrc] = '\0';
            return psz; /* Do not skip space or we won't get out of the inner option loop! */
        }
        SyntaxError("Argument value too long", pszOption);
    }
    else
        SyntaxError("Argument value cannot be empty", pszOption);
    return NULL;
}


static PSZ GetOptPath(PSZ psz, const char *pszOption, char *pszValue, size_t cbValue, size_t cchScratch, size_t *pcchValue)
{
    psz = GetOptValue(psz, pszOption, pszValue, cbValue - cchScratch);
    if (psz)
    {
        /* Only accept drive letters for now.  This could be a UNC path too for CID servers ;-) */
        if (   !RT_C_IS_ALPHA(pszValue[0])
            || pszValue[1] != ':'
            || (pszValue[2] != '\0' && pszValue[2] != '\\' && pszValue[2] != '/'))
            SyntaxError("The path must be absolute", pszOption);

        *pcchValue = RTPathEnsureTrailingSeparator(pszValue, cbValue);
        if (*pcchValue == 0)
            SyntaxError("RTPathEnsureTrailingSeparator overflowed", pszValue);
    }
    return psz;
}



/**
 * This is the main entrypoint of the executable (no CRT).
 *
 * @note Considered doing a main() wrapper by means of RTGetOptArgvFromString,
 *       but the dependencies are bad and we definitely need a half working heap
 *       for that.  Maybe later.
 */
extern "C" int __cdecl VBoxOs2AdditionsInstallMain(HMODULE hmodExe, ULONG ulReserved, PSZ pszzEnv, PSZ pszzCmdLine)
{
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;

    /*
     * Correct defaults.
     */
    ULONG ulBootDrv = 0x80;
    DosQuerySysInfo(QSV_BOOT_DRIVE, QSV_BOOT_DRIVE, &ulBootDrv, sizeof(ulBootDrv));
    g_szBootDrivePath[0] = g_szDstPath[0] = 'A' + ulBootDrv - 1;

    /*
     * Parse parameters, skipping the first argv[0] one.
     */
    PSZ  pszArgs = &pszzCmdLine[strlen(pszzCmdLine) + 1];
    CHAR ch;
    while ((ch = *pszArgs++) != '\0')
    {
        if (RT_C_IS_SPACE(ch))
            continue;
        if (ch != '-')
            return SyntaxError("Non-option argument", pszArgs - 1);
        ch = *pszArgs++;
        if (ch == '-')
        {
            if (!pszArgs[0])
                break;
            if (   MatchOptWord(&pszArgs, RT_STR_TUPLE("boot"), true)
                || MatchOptWord(&pszArgs, RT_STR_TUPLE("boot-drive"), true))
                ch = 'b';
            else if (   MatchOptWord(&pszArgs, RT_STR_TUPLE("dst"), true)
                     || MatchOptWord(&pszArgs, RT_STR_TUPLE("destination"), true) )
                ch = 'd';
            else if (   MatchOptWord(&pszArgs, RT_STR_TUPLE("src"), true)
                     || MatchOptWord(&pszArgs, RT_STR_TUPLE("source"), true))
                ch = 's';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("do-install")))
                ch = 'i';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("dry-run")))
                ch = 'z';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("shared-folders")))
                ch = 'f';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("no-shared-folders")))
                ch = 'F';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("graphics")))
                ch = 'g';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("no-graphics")))
                ch = 'G';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("mouse")))
                ch = 'm';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("no-mouse")))
                ch = 'M';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("service")))
                ch = 's';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("no-service")))
                ch = 'S';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("startup-cmd")))
                ch = 't';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("no-startup-cmd")))
                ch = 'T';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("config-sys")))
                ch = 'c';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("no-config-sys")))
                ch = 'C';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("libc-dlls")))
                ch = 'l';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("no-libc-dlls")))
                ch = 'L';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("quiet")))
                ch = 'q';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("verbose")))
                ch = 'V';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("help")))
                ch = 'h';
            else if (MatchOptWord(&pszArgs, RT_STR_TUPLE("version")))
                ch = 'v';
            else
                return SyntaxError("Unknown option", pszArgs - 2);
        }

        for (;;)
        {
            switch (ch)
            {
                case '-':
                    while ((ch = *pszArgs) != '\0' && RT_C_IS_SPACE(ch))
                        pszArgs++;
                    if (ch == '\0')
                        break;
                    return SyntaxError("Non-option argument", pszArgs);

                case 'b':
                    pszArgs = GetOptPath(pszArgs, "--boot-drive / -b",
                                         g_szBootDrivePath, sizeof(g_szBootDrivePath), 64, &g_cchBootDrivePath);
                    if (!pszArgs)
                        return RTEXITCODE_SYNTAX;
                    break;

                case 'd':
                    pszArgs = GetOptPath(pszArgs, "--destination / -d", g_szDstPath, sizeof(g_szDstPath), 32, &g_cchDstPath);
                    if (!pszArgs)
                        return RTEXITCODE_SYNTAX;
                    break;

                case 's':
                    pszArgs = GetOptPath(pszArgs, "--source / -s", g_szSrcPath, sizeof(g_szSrcPath), 32, &g_cchSrcPath);
                    if (!pszArgs)
                        return RTEXITCODE_SYNTAX;
                    break;

                case 'i':
                    g_fRealRun = true;
                    break;

                case 'z':
                    g_fRealRun = false;
                    break;

#define SKIP_OPT_CASES(a_chSkip, a_chDontSkip, a_fFlag) \
                case a_chDontSkip: g_fSkipMask &= ~(a_fFlag); break; \
                case a_chSkip:     g_fSkipMask |=  (a_fFlag); break
                SKIP_OPT_CASES('F', 'f', SKIP_SHARED_FOLDERS);
                SKIP_OPT_CASES('G', 'g', SKIP_GRAPHICS);
                SKIP_OPT_CASES('M', 'm', SKIP_MOUSE);
                SKIP_OPT_CASES('E', 'e', SKIP_SERVICE);
                SKIP_OPT_CASES('U', 'u', SKIP_STARTUP_CMD);
                SKIP_OPT_CASES('C', 'c', SKIP_CONFIG_SYS);
                SKIP_OPT_CASES('L', 'l', SKIP_LIBC_DLLS);
#undef SKIP_OPT_CASES

                case 'q':
                    g_fVerbose = false;
                    break;

                case 'V':
                    g_fVerbose = true;
                    break;

                case 'h':
                case '?':
                    return ShowUsage();
                case 'v':
                    return ShowVersion();

                default:
                    return SyntaxError("Unknown option", pszArgs - 2);
            }

            ch = *pszArgs;
            if (RT_C_IS_SPACE(ch) || ch == '\0')
                break;
            pszArgs++;
        }
    }

    if (g_szSrcPath[0] == '\0')
    {
        APIRET rc = DosQueryModuleName(hmodExe, sizeof(g_szSrcPath), g_szSrcPath);
        if (rc != NO_ERROR)
            return ApiError("DosQueryModuleName", rc);
        RTPathStripFilename(g_szSrcPath);
        g_cchSrcPath = RTPathEnsureTrailingSeparator(g_szSrcPath, sizeof(g_szSrcPath));
        if (g_cchSrcPath == 0)
            return ApiError("RTPathEnsureTrailingSeparator", ERROR_BUFFER_OVERFLOW);
    }

    /*
     * Do the installation.
     */
    rcExit = CheckForGradd();
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = PrepareConfigSys();
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = PrepareStartupCmd();
    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = CopyFiles();
    if (g_fRealRun)
    {
        if (rcExit == RTEXITCODE_SUCCESS)
            rcExit = WriteConfigSys();
        if (rcExit == RTEXITCODE_SUCCESS)
            rcExit = WriteStartupCmd();

        /*
         * Status summary.
         */
        if (rcExit == RTEXITCODE_SUCCESS)
            WriteStrings(g_hStdOut, "info: Installation successful\r\n", NULL);
        else
            WriteStrings(g_hStdErr, "info: Installation failed!\r\n", NULL);
    }
    else if (rcExit == RTEXITCODE_SUCCESS)
        WriteStrings(g_hStdOut, "info: Trial run successful\r\n", NULL);
    else
        WriteStrings(g_hStdErr, "info: Trial run failed!\r\n", NULL);
    return rcExit;
}


#if 0 /* Better off with the assembly file here. */
/*
 * Define the stack.
 *
 * This \#pragma data_seg(STACK,STACK) thing seems to work a little better for
 * 32-bit OS2 binaries than 16-bit.  Wlink still thinks it needs to allocate
 * zero bytes in the file for the abStack variable, but it doesn't looks like
 * both the starting ESP and stack size fields in the LX header are correct.
 *
 * The problem seems to be that wlink will write anything backed by
 * LEDATA32/LIDATA32 even it's all zeros.  The C compiler emits either of those
 * here, and boom the whole BSS is written to the file.
 *
 * For 16-bit (see os2_util.c) it would find the stack, but either put the
 * correct obj:SP or stack size fields in the NE header, never both and the
 * resulting EXE would either not start or crash immediately.
 */
#pragma data_seg("STACK", "STACK")
static uint64_t abStack[4096];
#endif

