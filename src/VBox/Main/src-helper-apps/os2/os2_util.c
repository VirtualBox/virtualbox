/* $Id: os2_util.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Os2Util - Unattended Installation Helper Utility for OS/2.
 *
 * Helps TEE'ing the installation script output to VBox.log and guest side log
 * files.  Also helps with displaying program exit codes, something CMD.exe can't.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
#include <iprt/asm-amd64-x86.h>
#include <VBox/log.h>

#include "VBox/version.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define IS_BLANK(ch)  ((ch) == ' ' || (ch) == '\t' || (ch) == '\r' || (ch) == '\n')

/** NIL HQUEUE value. */
#define NIL_HQUEUE  (~(HQUEUE)0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to buffered output. */
typedef struct MYBUFFER __far *PMYBUFFER;

/** Buffered output. */
typedef struct MYBUFFER
{
    PMYBUFFER pNext;
    USHORT    cb;
    USHORT    off;
    CHAR      sz[65536 - sizeof(USHORT) * 2 - sizeof(PMYBUFFER) - 2];
} MYBUFFER;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
void __far VBoxBackdoorPrint(PSZ psz, unsigned cch);
static PSZ MyGetOptValue(PSZ psz, PSZ pszOption, PSZ *ppszValue);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static HFILE        g_hStdOut = 1;
static HFILE        g_hStdErr = 2;
static BOOL         g_fOutputToBackdoor = FALSE;
static USHORT       g_cBuffers = 0;
static PMYBUFFER    g_pBufferHead = NULL;
static PMYBUFFER    g_pBufferTail = NULL;



/** strlen-like function. */
static unsigned MyStrLen(PSZ psz)
{
    unsigned cch = 0;
    while (psz[cch] != '\0')
        cch++;
    return cch;
}


/** strchr-like function. */
static char __far *MyStrChr(const char __far *psz, char chNeedle)
{
    char ch;
    while ((ch = *psz) != '\0')
    {
        if (ch == chNeedle)
            return (char __far *)psz;
        psz++;
    }
    return NULL;
}


/** memcpy-like function.   */
static void *MyMemCopy(void __far *pvDst, void const __far *pvSrc, USHORT cb)
{
    BYTE __far       *pbDst = (BYTE __far *)pvDst;
    BYTE const __far *pbSrc = (BYTE const __far *)pvSrc;
    while (cb-- > 0)
        *pbDst++ = *pbSrc++;
    return pvDst;
}


static void MyOutStr(PSZ psz)
{
    unsigned const cch = MyStrLen(psz);
    USHORT         usIgnored;
    DosWrite(g_hStdErr, psz, cch, &usIgnored);
    if (g_fOutputToBackdoor)
        VBoxBackdoorPrint(psz, cch);
}


static PSZ MyNumToString(PSZ pszBuf, unsigned uNum)
{
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
    return pszBuf;
}


static void MyOutNum(unsigned uNum)
{
    char szTmp[32];
    MyNumToString(szTmp, uNum);
    MyOutStr(szTmp);
}


static DECL_NO_RETURN(void) MyApiErrorAndQuit(PSZ pszOperation, USHORT rc)
{
    MyOutStr("Os2Util: error: ");
    MyOutStr(pszOperation);
    MyOutStr(" failed: ");
    MyOutNum(rc);
    MyOutStr("\r\n");
    DosExit(EXIT_PROCESS, 1);
}


static DECL_NO_RETURN(void) MyApiError3AndQuit(PSZ pszOperation, PSZ psz2, PSZ psz3, USHORT rc)
{
    MyOutStr("Os2Util: error: ");
    MyOutStr(pszOperation);
    MyOutStr(psz2);
    MyOutStr(psz3);
    MyOutStr(" failed: ");
    MyOutNum(rc);
    MyOutStr("\r\n");
    DosExit(EXIT_PROCESS, 1);
}


static DECL_NO_RETURN(void) MySyntaxErrorAndQuit(PSZ pszMsg)
{
    MyOutStr("Os2Util: syntax error: ");
    MyOutStr(pszMsg);
    MyOutStr("\r\n");
    DosExit(EXIT_PROCESS, 1);
}


static HFILE OpenTeeFile(PSZ pszTeeToFile, BOOL fAppend, PSZ pszToWrite, USHORT cchToWrite)
{
    PMYBUFFER pBuf, pNext;
    USHORT    usIgnored;
    USHORT    usAction = 0;
    HFILE     hFile    = -1;
    USHORT    rc;
    rc = DosOpen(pszTeeToFile, &hFile, &usAction, 0 /*cbInitial*/, 0 /*fFileAttribs*/,
                 OPEN_ACTION_CREATE_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS,
                 OPEN_ACCESS_WRITEONLY | OPEN_SHARE_DENYNONE | OPEN_FLAGS_NOINHERIT | OPEN_FLAGS_SEQUENTIAL, 0 /*Reserved*/);
    if (rc == NO_ERROR)
    {

        if (fAppend)
        {
            ULONG offNew = 0;
            DosChgFilePtr(hFile, 0, FILE_END, &offNew);
        }

        /*
         * Write out buffered data
         */
        /** @todo this does not seem to work. */
        pBuf = g_pBufferHead;
        while (pBuf)
        {
            do
                rc = DosWrite(hFile, pBuf->sz, pBuf->off, &usIgnored);
            while (rc == ERROR_INTERRUPT);
            pNext = pBuf->pNext;
            DosFreeSeg((__segment)pBuf);
            pBuf = pNext;
        }
        g_pBufferTail = g_pBufferHead = NULL;

        /*
         * Write the current output.
         */
        do
            rc = DosWrite(hFile, pszToWrite, cchToWrite, &usIgnored);
        while (rc == ERROR_INTERRUPT);
    }
    else
    {
        /*
         * Failed to open the file.  Buffer a bit in case the file can be
         * opened later (like when we've formatted the disk).
         */
        pBuf = g_pBufferTail;
        if (pBuf && pBuf->off < pBuf->cb)
        {
            USHORT cbToCopy = pBuf->cb - pBuf->off;
            if (cbToCopy > cchToWrite)
                cbToCopy = cchToWrite;
            MyMemCopy(&pBuf->sz[pBuf->off], pszToWrite, cbToCopy);
            pszToWrite += cbToCopy;
            cchToWrite -= cbToCopy;
        }
        if (cchToWrite > 0)
        {
            USHORT uSel = 0xffff;
            if (   g_cBuffers < 10
                && (rc = DosAllocSeg(0 /*64KiB*/, &uSel, 0 /*fFlags*/)) == NO_ERROR)
            {
                pBuf = ((__segment)uSel) :> ((MYBUFFER __near *)0);
                pBuf->pNext = NULL;
                pBuf->cb    = sizeof(pBuf->sz);
                pBuf->off   = cchToWrite;
                MyMemCopy(&pBuf->sz[0], pszToWrite, cchToWrite);

                if (g_pBufferTail)
                    g_pBufferTail->pNext = pBuf;
                else
                    g_pBufferHead = pBuf;
                g_pBufferTail = pBuf;
            }
            else if (g_cBuffers > 0)
            {
                pBuf = g_pBufferHead;
                pBuf->off = cchToWrite;
                MyMemCopy(&pBuf->sz[0], pszToWrite, cchToWrite);

                if (g_pBufferTail != pBuf)
                {
                    g_pBufferHead = pBuf->pNext;
                    pBuf->pNext = NULL;
                    g_pBufferTail->pNext = pBuf;
                    g_pBufferTail = pBuf;
                }
            }
        }
        hFile = -1;
    }
    return hFile;
}


/**
 * Waits for the child progress to complete, returning it's status code.
 */
static void DoWait(PID pidChild, USHORT idSession, HQUEUE hQueue, PRESULTCODES pResultCodes)
{
    /*
     * Can we use DosCwait?
     */
    if (hQueue == NIL_HQUEUE)
    {
        for (;;)
        {
            PID pidIgnored;
            USHORT rc = DosCwait(DCWA_PROCESS, DCWW_WAIT, pResultCodes, &pidIgnored, pidChild);
            if (rc == NO_ERROR)
                break;
            if (rc != ERROR_INTERRUPT)
            {
                MyOutStr("Os2Util: error: DosCwait(DCWA_PROCESS,DCWW_WAIT,,,");
                MyOutNum(pidChild);
                MyOutStr(") failed: ");
                MyOutNum(rc);
                MyOutStr("\r\n");
            }
        }
    }
    else
    {
        /*
         * No we have to use the queue interface to the session manager.
         */
        for (;;)
        {
            ULONG   ulAdderPidAndEvent = 0;
            PUSHORT pausData           = NULL;
            USHORT  cbData             = 0;
            BYTE    bPriority          = 0;
            HSEM    hSem               = NULL;
            USHORT rc = DosReadQueue(hQueue, &ulAdderPidAndEvent, &cbData, (PULONG)&pausData,
                                     0 /*uElementCode*/, 0 /* fNoWait */, &bPriority, &hSem);
            if (rc == NO_ERROR)
            {
                if (cbData >= sizeof(USHORT) * 2)
                {
                    USHORT idTermSession = pausData[0];
                    USHORT uExitCode     = pausData[1];
                    if (idTermSession == idSession)
                    {
                        pResultCodes->codeTerminate = 0;
                        pResultCodes->codeResult    = uExitCode;
                        break;
                    }
                    if (1)
                    {
                        MyOutStr("OutUtil: info: idTermSession=");
                        MyOutNum(idTermSession);
                        MyOutStr(" uExitCode=");
                        MyOutNum(uExitCode);
                        MyOutStr("\r\n");
                    }
                }
                else
                {
                    MyOutStr("OutUtil: warning: bogus queue element size: cbData=");
                    MyOutNum(cbData);
                    MyOutStr("\r\n");
                }
                DosFreeSeg((__segment)pausData);
            }
            else if (rc != ERROR_INTERRUPT)
            {
                DosCloseQueue(hQueue);
                MyApiErrorAndQuit("DosReadQueue", rc);
            }
        }
    }
}


/**
 * Handles --file-to-backdoor / -c.
 */
static void CopyFileToBackdoorAndQuit(PSZ psz, BOOL fLongOpt, PSZ pszBuf, USHORT cbBuf)
{
    HFILE  hFile    = 0;
    USHORT usAction = 0;
    USHORT rc;

    /*
     * Get the filename and check that it is the last thing on the commandline.
     */
    PSZ  pszFilename = NULL;
    CHAR ch;
    psz = MyGetOptValue(psz, fLongOpt ? "--file-to-backdoor" : "-c", &pszFilename);
    while ((ch = *psz) != '\0' && IS_BLANK(ch))
        psz++;
    if (ch != '\0')
        MySyntaxErrorAndQuit("No options allowed after -c/--file-to-backdoor");

    /*
     * Open the file
     */
    rc = DosOpen(pszFilename, &hFile, &usAction, 0 /*cbInitial*/, 0 /*fFileAttribs*/,
                 OPEN_ACTION_FAIL_IF_NEW | OPEN_ACTION_OPEN_IF_EXISTS,
                 OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE | OPEN_FLAGS_NOINHERIT | OPEN_FLAGS_SEQUENTIAL, 0 /*Reserved*/);
    if (rc != NO_ERROR)
        MyApiError3AndQuit("Failed to open \"", pszFilename, "\" for reading", rc);

    VBoxBackdoorPrint(RT_STR_TUPLE("--- BEGIN OF \""));
    VBoxBackdoorPrint(pszFilename, MyStrLen(pszFilename));
    VBoxBackdoorPrint(RT_STR_TUPLE("\" ---\n"));

    for (;;)
    {
        USHORT cbRead = 0;
        rc = DosRead(hFile, pszBuf, cbBuf, &cbRead);
        if (rc == NO_ERROR)
        {
            if (cbRead == 0)
                break;
            VBoxBackdoorPrint(pszBuf, cbRead);
        }
        else if (rc != ERROR_INTERRUPT)
            MyApiError3AndQuit("Reading \"", pszFilename, "\"", rc);
    }

    VBoxBackdoorPrint(RT_STR_TUPLE("--- END OF \""));
    VBoxBackdoorPrint(pszFilename, MyStrLen(pszFilename));
    VBoxBackdoorPrint(RT_STR_TUPLE("\" ---\n"));

    DosClose(hFile);
    DosExit(EXIT_PROCESS, 1);
}


/** Displays version string and quits.   */
static DECL_NO_RETURN(void) ShowVersionAndQuit(void)
{
    CHAR szVer[] = "$Rev: 106320 $\r\n";
    USHORT usIgnored;
    DosWrite(g_hStdOut, szVer, sizeof(szVer) - 1, &usIgnored);
    DosExit(EXIT_PROCESS, 0);
}


/** Displays usage info and quits.   */
static DECL_NO_RETURN(void) ShowUsageAndQuit(void)
{
    static char s_szHelp[] =
        VBOX_PRODUCT " OS/2 Unattended Helper Version " VBOX_VERSION_STRING "\r\n"
        "Copyright (C) 2005-" VBOX_C_YEAR " " VBOX_VENDOR "\r\n"
        "\r\n"
        "Os2Util.exe is tiny helper utility that implements TEE'ing to the VBox release\r\n"
        "log, files and shows the actual exit code of a program.  Standard error and\r\n"
        "output will be merged into one for simplicity reasons.\r\n"
        "\r\n"
        "Usage: Os2Util.exe [-a|--append] [-f<filename>|--tee-to-file <filename>] \\\r\n"
        "                   [-b|--tee-to-backdoor] [-z<exit>|--as-zero <exit> [..]] \\\r\n"
        "                   -- <prog> [args]\r\n"
        "   or  Os2Util.exe <-w<msg>|--write-backdoor <msg>>\r\n"
        "   or  Os2Util.exe <-c<file>|--file-to-backdoor <file>>\r\n"
        "\r\n"
        "Note! Does not supported any kind of quoting before the child arguments.\r\n"
        ;
    USHORT usIgnored;
    DosWrite(g_hStdOut, s_szHelp, sizeof(s_szHelp) - 1, &usIgnored);
    DosExit(EXIT_PROCESS, 0);
}


/**
 * Gets the an option value.
 *
 * The option value string will be terminated.
 */
static PSZ MyGetOptValue(PSZ psz, PSZ pszOption, PSZ *ppszValue)
{
    CHAR ch;
    while ((ch = *psz) != '\0' && IS_BLANK(ch))
        psz++;
    if (*psz == '\0')
    {
        MyOutStr("Os2Util: syntax error: Option '");
        MyOutStr(pszOption);
        MyOutStr("' takes a value\r\n");
        DosExit(EXIT_PROCESS, 2);
    }

    *ppszValue = psz;

    while ((ch = *psz) != '\0' && !IS_BLANK(ch))
        psz++;
    if (ch != '\0')
        *psz++ = '\0';
    return psz;
}


/**
 * Gets the an numeric option value.
 */
static PSZ MyGetOptNum(PSZ psz, PSZ pszOption, PUSHORT puValue)
{
    PSZ         pszError      = NULL;
    PSZ         pszValue      = NULL;
    PSZ const   pszRet        = MyGetOptValue(psz, pszOption, &pszValue);
    PSZ const   pszValueStart = pszValue;
    USHORT      uValue        = 0;
    CHAR        ch;
    if (pszValue[0] == '0' && ((ch = pszValue[1]) == 'x' || ch == 'X'))
    {
        pszValue += 2;
        while ((ch = *pszValue++) != '\0')
        {
            BYTE bDigit;
            if (ch <= '9' && ch >= '0')
                bDigit = ch - '0';
            else if (ch <= 'f' && ch >= 'a')
                bDigit = ch - 'a' + 10;
            else if (ch <= 'F' && ch >= 'A')
                bDigit = ch - 'A' + 10;
            else
            {
                pszError = "': invalid hex value\r\n";
                break;
            }
            if (uValue >> 12)
            {
                pszError = "': hex value out of range\r\n";
                break;
            }
            uValue <<= 4;
            uValue |= bDigit;
        }
    }
    else
    {
        while ((ch = *pszValue++) != '\0')
        {
            BYTE bDigit;
            if (ch <= '9' && ch >= '0')
                bDigit = ch - '0';
            else
            {
                pszError = "': invalid decimal value\r\n";
                break;
            }
            if (uValue * 10 / 10 != uValue)
            {
                pszError = "': decimal value out of range\r\n";
                break;
            }
            uValue *= 10;
            uValue += bDigit;
        }
    }

    if (pszError)
    {
        MyOutStr("Os2Util: syntax error: Option '");
        MyOutStr(pszOption);
        MyOutStr("' with value '");
        MyOutStr(pszValueStart);
        MyOutStr(pszError);
        DosExit(EXIT_PROCESS, 2);
    }

    *puValue = uValue;
    return pszRet;
}


/**
 * Checks if @a pszOption matches @a *ppsz, advance *ppsz if TRUE.
 */
static BOOL MyMatchLongOption(PSZ _far *ppsz, PSZ pszOption, unsigned cchOption)
{
    /* Match option and command line strings: */
    PSZ psz = *ppsz;
    while (cchOption-- > 0)
    {
        if (*psz != *pszOption)
            return FALSE;
        psz++;
        pszOption++;
    }

    /* Is this the end of a word on the command line? */
    if (*psz == '\0')
        *ppsz = psz;
    else if (IS_BLANK(*psz))
        *ppsz = psz + 1;
    else
        return FALSE;
    return TRUE;
}


/**
 * The entrypoint (no crt).
 */
#pragma aux Os2UtilMain "_*" parm caller [ ax ] [ bx ];
void Os2UtilMain(USHORT uSelEnv, USHORT offCmdLine)
{
    PSZ         pszzEnv        = ((__segment)uSelEnv) :> ((char _near *)0);
    PSZ         pszzCmdLine    = ((__segment)uSelEnv) :> ((char _near *)offCmdLine);
    USHORT      uExitCode      = 1;
    BOOL        fTeeToBackdoor = FALSE;
    BOOL        fAppend        = FALSE;
    PSZ         pszTeeToFile   = NULL;
    HFILE       hTeeToFile     = -1;
    HFILE       hPipeRead      = -1;
    PSZ         pszzNewCmdLine;
    PSZ         psz;
    CHAR        ch;
    USHORT      usIgnored;
    USHORT      rc;
    RESULTCODES ResultCodes    = { 0xffff, 0xffff };
    CHAR        szBuf[512];
    CHAR        szExeFull[CCHMAXPATH];
    PSZ         pszExe;
    USHORT      uExeType;
    USHORT      idSession      = 0;
    PID         pidChild       = 0;
    HQUEUE      hQueue         = NIL_HQUEUE;
    CHAR        szQueueName[64];
    unsigned    cAsZero        = 0;
    USHORT      auAsZero[16];

    /*
     * Parse the command line.
     * Note! We do not accept any kind of quoting.
     */
    /* Skip the executable filename: */
    psz = pszzCmdLine;
    while (*psz != '\0')
        psz++;
    psz++;

    /* Now parse arguments. */
    while ((ch = *psz) != '\0')
    {
        if (IS_BLANK(ch))
            psz++;
        else if (ch != '-')
            break;
        else
        {
            PSZ const pszOptStart = psz;
            ch = *++psz;
            if (ch == '-')
            {
                ch = *++psz;
                if (IS_BLANK(ch))
                {
                    /* Found end-of-arguments marker "--" */
                    psz++;
                    break;
                }
                if (ch == 'a' && MyMatchLongOption(&psz, RT_STR_TUPLE("append")))
                    fAppend = TRUE;
                else if (ch == 'a' && MyMatchLongOption(&psz, RT_STR_TUPLE("as-zero")))
                {
                    if (cAsZero > RT_ELEMENTS(auAsZero))
                        MySyntaxErrorAndQuit("Too many --as-zero/-z options");
                    psz = MyGetOptNum(psz, "--as-zero", &auAsZero[cAsZero]);
                    cAsZero++;
                }
                else if (ch == 'f' && MyMatchLongOption(&psz, RT_STR_TUPLE("file-to-backdoor")))
                    CopyFileToBackdoorAndQuit(psz, TRUE /*fLongOpt*/, szBuf, sizeof(szBuf));
                else if (ch == 'h' && MyMatchLongOption(&psz, RT_STR_TUPLE("help")))
                    ShowUsageAndQuit();
                else if (ch == 't' && MyMatchLongOption(&psz, RT_STR_TUPLE("tee-to-backdoor")))
                    g_fOutputToBackdoor = fTeeToBackdoor = TRUE;
                else if (ch == 't' && MyMatchLongOption(&psz, RT_STR_TUPLE("tee-to-file")))
                    psz = MyGetOptValue(psz, "--tee-to-file", &pszTeeToFile);
                else if (ch == 'v' && MyMatchLongOption(&psz, RT_STR_TUPLE("version")))
                    ShowVersionAndQuit();
                else if (ch == 'w' && MyMatchLongOption(&psz, RT_STR_TUPLE("write-backdoor")))
                {
                    VBoxBackdoorPrint(psz, MyStrLen(psz));
                    VBoxBackdoorPrint("\n", 1);
                    DosExit(EXIT_PROCESS, 0);
                }
                else
                {
                    MyOutStr("Os2util: syntax error: ");
                    MyOutStr(pszOptStart);
                    MyOutStr("\r\n");
                    DosExit(EXIT_PROCESS, 2);
                }
            }
            else
            {
                do
                {
                    if (ch == 'a')
                        fAppend = TRUE;
                    else if (ch == 'b')
                        g_fOutputToBackdoor = fTeeToBackdoor = TRUE;
                    else if (ch == 'c')
                        CopyFileToBackdoorAndQuit(psz + 1, FALSE /*fLongOpt*/, szBuf, sizeof(szBuf));
                    else if (ch == 'f')
                    {
                        psz = MyGetOptValue(psz + 1, "-f", &pszTeeToFile);
                        break;
                    }
                    else if (ch == 'w')
                    {
                        psz++;
                        VBoxBackdoorPrint(psz, MyStrLen(psz));
                        VBoxBackdoorPrint("\n", 1);
                        DosExit(EXIT_PROCESS, 0);
                    }
                    else if (ch == 'z')
                    {
                        if (cAsZero > RT_ELEMENTS(auAsZero))
                            MySyntaxErrorAndQuit("Too many --as-zero/-z options");
                        psz = MyGetOptNum(psz + 1, "-z", &auAsZero[cAsZero]);
                        cAsZero++;
                    }
                    else if (ch == '?' || ch == 'h' || ch == 'H')
                        ShowUsageAndQuit();
                    else if (ch == 'V')
                        ShowVersionAndQuit();
                    else
                    {
                        MyOutStr("Os2util: syntax error: ");
                        if (ch)
                            DosWrite(g_hStdErr, &ch, 1, &usIgnored);
                        else
                            MyOutStr("lone dash");
                        MyOutStr(" (");
                        MyOutStr(pszOptStart);
                        MyOutStr(")\r\n");
                        DosExit(EXIT_PROCESS, 2);
                    }
                    ch = *++psz;
                } while (!IS_BLANK(ch) && ch != '\0');
            }
        }
    }

    /*
     * Zero terminate the executable name in the command line.
     */
    pszzNewCmdLine = psz;
    if (ch == '\0')
    {
        MyOutStr("Os2Util: syntax error: No program specified\r\n");
        DosExit(EXIT_PROCESS, 2);
    }
    psz++;
    while ((ch = *psz) != '\0' && !IS_BLANK(ch))
        psz++;
    *psz++ = '\0';

    /*
     * Find the executable and check its type.
     */
    if (   pszzNewCmdLine[1] == ':'
        || MyStrChr(pszzNewCmdLine, '\\')
        || MyStrChr(pszzNewCmdLine, '/'))
        pszExe = pszzNewCmdLine;
    else
    {
        rc = DosSearchPath(SEARCH_CUR_DIRECTORY | SEARCH_ENVIRONMENT | SEARCH_IGNORENETERRS, "PATH",
                           pszzNewCmdLine, szExeFull, sizeof(szExeFull));
        if (rc != NO_ERROR)
            MyApiError3AndQuit("DosSearchPath(7, \"PATH\", \"", pszzNewCmdLine, "\",,)", rc);
        pszExe = &szExeFull[0];
    }

    /* Perhapse we should use WinQueryProgramType here instead? */
    rc = DosQAppType(pszExe, &uExeType);
    if (rc != NO_ERROR)
        MyApiErrorAndQuit("DosQAppType(pszExe, &uExeType)", rc);
#ifdef DEBUG
    MyOutStr("Os2Util: debug: uExeType="); MyOutNum(uExeType); MyOutStr("\r\n");
#endif
    /** @todo deal with launching winos2 programs too...   */

    /*
     * Prepare redirection.
     */
    if (fTeeToBackdoor || pszTeeToFile != NULL)
    {
        HFILE   hPipeWrite = -1;
        HFILE   hDup;

        /* Make new copies of the standard handles. */
        hDup = 0xffff;
        rc = DosDupHandle(g_hStdErr, &hDup);
        if (rc != NO_ERROR)
            MyApiErrorAndQuit("DosDupHandle(g_hStdErr, &hDup)", rc);
        g_hStdErr = hDup;
        DosSetFHandState(hDup, OPEN_FLAGS_NOINHERIT); /* not strictly necessary, so ignore errors */

        hDup = 0xffff;
        rc = DosDupHandle(g_hStdOut, &hDup);
        if (rc != NO_ERROR)
            MyApiErrorAndQuit("DosDupHandle(g_hStdOut, &hDup)", rc);
        g_hStdOut = hDup;
        DosSetFHandState(hDup, OPEN_FLAGS_NOINHERIT); /* not strictly necessary, so ignore errors */

        /* Create the pipe and make the read-end non-inheritable (we'll hang otherwise). */
        rc = DosMakePipe(&hPipeRead, &hPipeWrite, 0 /*default size*/);
        if (rc != NO_ERROR)
            MyApiErrorAndQuit("DosMakePipe", rc);

        rc = DosSetFHandState(hPipeRead, OPEN_FLAGS_NOINHERIT);
        if (rc != NO_ERROR)
            MyApiErrorAndQuit("DosSetFHandState(hPipeRead, OPEN_FLAGS_NOINHERIT)", rc);

        /* Replace standard output and standard error with the write end of the pipe. */
        hDup = 1;
        rc = DosDupHandle(hPipeWrite, &hDup);
        if (rc != NO_ERROR)
            MyApiErrorAndQuit("DosDupHandle(hPipeWrite, &hDup[=1])", rc);

        hDup = 2;
        rc = DosDupHandle(hPipeWrite, &hDup);
        if (rc != NO_ERROR)
            MyApiErrorAndQuit("DosDupHandle(hPipeWrite, &hDup[=2])", rc);

        /* We can close the write end of the pipe as we don't need the original handle any more. */
        DosClose(hPipeWrite);
    }

    /*
     * Execute the program.
     */
    szBuf[0] = '\0';
#define FAPPTYP_TYPE_MASK 7
    if ((uExeType & FAPPTYP_TYPE_MASK) == PT_WINDOWABLEVIO) /** @todo what if we're in fullscreen ourselves? */
    {
        /* For same type programs we can use DosExecPgm: */
        rc = DosExecPgm(szBuf, sizeof(szBuf), hPipeRead == -1 ? EXEC_SYNC : EXEC_ASYNCRESULT,
                        pszzNewCmdLine, pszzEnv, &ResultCodes, pszExe);
        if (rc != NO_ERROR)
        {
            MyOutStr("Os2Util: error: DosExecPgm failed for \"");
            MyOutStr(pszzNewCmdLine);
            MyOutStr("\": ");
            MyOutNum(rc);
            if (szBuf[0])
            {
                MyOutStr(" ErrObj=");
                szBuf[sizeof(szBuf) - 1] = '\0';
                MyOutStr(szBuf);
            }
            MyOutStr("\r\n");
            DosExit(EXIT_PROCESS, 1);
        }
        if (hPipeRead != -1)
        {
            pidChild = ResultCodes.codeTerminate;
            MyOutStr("info: started pid ");
            MyOutNum(pidChild);
            MyOutStr("\r\n");
        }
    }
    else
    {
        /* For different typed programs we have to use DosStartSession, which
           is a lot more tedious to use. */
        static const char   s_szQueueBase[] = "\\QUEUES\\OS2_UTIL-";
        union
        {
            STARTDATA       StartData;
            BYTE            abPadding[sizeof(STARTDATA) + 64];
            struct
            {
                STARTDATA   Core;
                ULONG       ulReserved;
                PSZ         pszBuf;
                USHORT      cbBuf;
            } s;
        } u;
        PIDINFO             PidInfo         = {0, 0, 0};

        /* Create the wait queue first. */
        DosGetPID(&PidInfo);
        MyMemCopy(szQueueName, s_szQueueBase, sizeof(s_szQueueBase));
        MyNumToString(&szQueueName[sizeof(s_szQueueBase) - 1], PidInfo.pid);

        rc = DosCreateQueue(&hQueue, 0 /*FIFO*/, szQueueName);
        if (rc != NO_ERROR)
            MyApiError3AndQuit("DosCreateQueue(&hQueue, 0, \"", szQueueName, "\")", rc);

        u.StartData.Length        = sizeof(u.StartData);
        u.StartData.Related       = 1 /* SSF_RELATED_CHILD */;
        u.StartData.FgBg          = (uExeType & FAPPTYP_TYPE_MASK) == PT_PM
                                  ? 1 /* SSF_FGBG_BACK - try avoid ERROR_SMG_START_IN_BACKGROUND */
                                  : 0 /* SSF_FGBG_FORE */;
        u.StartData.TraceOpt      = 0 /* SSF_TRACEOPT_NONE */;
        u.StartData.PgmTitle      = NULL;
        u.StartData.PgmName       = pszExe;
        u.StartData.PgmInputs     = psz; /* just arguments, not exec apparently.*/
        u.StartData.TermQ         = szQueueName;
        u.StartData.Environment   = NULL; /* Inherit our env. Note! Using pszzEnv causes it to be freed
                                                                    and we'll crash reporting the error. */
        u.StartData.InheritOpt    = 1 /* SSF_INHERTOPT_PARENT */;
        u.StartData.SessionType   = uExeType & FAPPTYP_TYPE_MASK;
        if (uExeType & 0x20 /*FAPPTYP_DOS*/)
            u.StartData.SessionType = 4 /* SSF_TYPE_VDM */;
        u.StartData.IconFile      = NULL;
        u.StartData.PgmHandle     = 0;
        u.StartData.PgmControl    = 0 /* SSF_CONTROL_VISIBLE */;
        u.StartData.InitXPos      = 0;
        u.StartData.InitYPos      = 0;
        u.StartData.InitXSize     = 0;
        u.StartData.InitYSize     = 0;
        u.s.ulReserved            = 0;
        u.s.pszBuf                = NULL;
        u.s.cbBuf                 = 0;

        rc = DosStartSession(&u.StartData, &idSession, &pidChild);
        if (rc != NO_ERROR && rc != ERROR_SMG_START_IN_BACKGROUND)
        {
            DosCloseQueue(hQueue);
            MyApiError3AndQuit("DosStartSession for \"", pszExe, "\"", rc);
        }

        if (1)
        {
            MyOutStr("info: started session ");
            MyOutNum(idSession);
            MyOutStr(", pid ");
            MyOutNum(pidChild);
            MyOutStr("\r\n");
        }
    }

    /*
     * Wait for the child process to complete.
     */
    if (hPipeRead != -1)
    {

        /* Close the write handles or we'll hang in the read loop. */
        DosClose(1);
        DosClose(2);

        /* Disable hard error popups (file output to unformatted disks). */
        DosError(2 /* only exceptions */);

        /*
         * Read the pipe and tee it to the desired outputs
         */
        for (;;)
        {
            USHORT cbRead = 0;
            rc = DosRead(hPipeRead, szBuf, sizeof(szBuf), &cbRead);
            if (rc == NO_ERROR)
            {
                if (cbRead == 0)
                    break; /* No more writers. */

                /* Standard output: */
                do
                    rc = DosWrite(g_hStdOut, szBuf, cbRead, &usIgnored);
                while (rc == ERROR_INTERRUPT);

                /* Backdoor: */
                if (fTeeToBackdoor)
                    VBoxBackdoorPrint(szBuf, cbRead);

                /* File: */
                if (hTeeToFile != -1)
                    do
                        rc = DosWrite(hTeeToFile, szBuf, cbRead, &usIgnored);
                    while (rc == ERROR_INTERRUPT);
                else if (pszTeeToFile != NULL)
                    hTeeToFile = OpenTeeFile(pszTeeToFile, fAppend, szBuf, cbRead);
            }
            else if (rc == ERROR_BROKEN_PIPE)
                break;
            else
            {
                MyOutStr("Os2Util: error: Error reading pipe: ");
                MyOutNum(rc);
                MyOutStr("\r\n");
                break;
            }
        }

        DosClose(hPipeRead);

        /*
         * Wait for the process to complete.
         */
        DoWait(pidChild, idSession, hQueue, &ResultCodes);
    }
    /*
     * Must wait for the session completion too.
     */
    else if (idSession != 0)
        DoWait(pidChild, idSession, hQueue, &ResultCodes);

    /*
     * Report the status code and quit.
     */
    MyOutStr("Os2Util: Child: ");
    MyOutStr(pszzNewCmdLine);
    MyOutStr(" ");
    MyOutStr(psz);
    MyOutStr("\r\n"
             "Os2Util: codeTerminate=");
    MyOutNum(ResultCodes.codeTerminate);
    MyOutStr(" codeResult=");
    MyOutNum(ResultCodes.codeResult);
    MyOutStr("\r\n");

    /* Treat it as zero? */
    if (ResultCodes.codeTerminate == 0)
    {
        unsigned i = cAsZero;
        while (i-- > 0)
            if (auAsZero[i] == ResultCodes.codeResult)
            {
                MyOutStr("Os2Util: info: treating status as zero\r\n");
                ResultCodes.codeResult = 0;
                break;
            }
    }

    if (idSession != 0)
        DosCloseQueue(hQueue);
    for (;;)
        DosExit(EXIT_PROCESS, ResultCodes.codeTerminate == 0 ? ResultCodes.codeResult : 127);
}


/**
 * Backdoor print function living in an IOPL=2 segment.
 */
#pragma code_seg("IOPL", "CODE")
void __far VBoxBackdoorPrint(PSZ psz, unsigned cch)
{
    ASMOutStrU8(RTLOG_DEBUG_PORT, psz, cch);
}

