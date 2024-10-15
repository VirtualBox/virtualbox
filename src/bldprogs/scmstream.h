/* $Id: scmstream.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase / Tool - Source Code Massager Stream Code.
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

#ifndef VBOX_INCLUDED_SRC_bldprogs_scmstream_h
#define VBOX_INCLUDED_SRC_bldprogs_scmstream_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

RT_C_DECLS_BEGIN

/** End of line marker type. */
typedef enum SCMEOL
{
    SCMEOL_NONE = 0,
    SCMEOL_LF = 1,
    SCMEOL_CRLF = 2
} SCMEOL;
/** Pointer to an end of line marker type. */
typedef SCMEOL *PSCMEOL;

/**
 * Line record.
 */
typedef struct SCMSTREAMLINE
{
    /** The offset of the line. */
    size_t          off;
    /** The line length, excluding the LF character.
     * @todo This could be derived from the offset of the next line if that wasn't
     *       so tedious. */
    size_t          cch;
    /** The end of line marker type. */
    SCMEOL          enmEol;
} SCMSTREAMLINE;
/** Pointer to a line record. */
typedef SCMSTREAMLINE *PSCMSTREAMLINE;

/**
 * Source code massager stream.
 */
typedef struct SCMSTREAM
{
    /** Pointer to the file memory. */
    char           *pch;
    /** The current stream position. */
    size_t          off;
    /** The current stream size. */
    size_t          cb;
    /** The size of the memory pb points to. */
    size_t          cbAllocated;

    /** Line records. */
    PSCMSTREAMLINE  paLines;
    /** The current line. */
    size_t          iLine;
    /** The current stream size given in lines.   */
    size_t          cLines;
    /** The sizeof the memory backing paLines.   */
    size_t          cLinesAllocated;

    /** Set if write-only, clear if read-only. */
    bool            fWriteOrRead;
    /** Set if the memory pb points to is from RTFileReadAll. */
    bool            fFileMemory;
    /** Set if fully broken into lines. */
    bool            fFullyLineated;

    /** Stream status code (IPRT). */
    int             rc;
} SCMSTREAM;
/** Pointer to a SCM stream. */
typedef SCMSTREAM *PSCMSTREAM;
/** Pointer to a const SCM stream. */
typedef SCMSTREAM const *PCSCMSTREAM;


int         ScmStreamInitForReading(PSCMSTREAM pStream, const char *pszFilename);
int         ScmStreamInitForReadingFromStdInput(PSCMSTREAM pStream);
int         ScmStreamInitForWriting(PSCMSTREAM pStream, PCSCMSTREAM pRelatedStream);
void        ScmStreamDelete(PSCMSTREAM pStream);
int         ScmStreamGetStatus(PCSCMSTREAM pStream);
void        ScmStreamRewindForReading(PSCMSTREAM pStream);
void        ScmStreamRewindForWriting(PSCMSTREAM pStream);
bool        ScmStreamIsText(PSCMSTREAM pStream);
int         ScmStreamCheckItegrity(PSCMSTREAM pStream);
int         ScmStreamWriteToFile(PSCMSTREAM pStream, const char *pszFilenameFmt, ...);
int         ScmStreamWriteToStdOut(PSCMSTREAM pStream);

size_t      ScmStreamTell(PSCMSTREAM pStream);
size_t      ScmStreamTellLine(PSCMSTREAM pStream);
size_t      ScmStreamTellOffsetOfLine(PSCMSTREAM pStream, size_t iLine);
size_t      ScmStreamSize(PSCMSTREAM pStream);
size_t      ScmStreamCountLines(PSCMSTREAM pStream);
int         ScmStreamSeekAbsolute(PSCMSTREAM pStream, size_t offAbsolute);
int         ScmStreamSeekRelative(PSCMSTREAM pStream, ssize_t offRelative);
int         ScmStreamSeekByLine(PSCMSTREAM pStream, size_t iLine);
bool        ScmStreamIsAtStartOfLine(PSCMSTREAM pStream);
bool        ScmStreamAreIdentical(PCSCMSTREAM pStream1, PCSCMSTREAM pStream2);

const char *ScmStreamGetLineByNo(PSCMSTREAM pStream, size_t iLine, size_t *pcchLine, PSCMEOL penmEol);
const char *ScmStreamGetLine(PSCMSTREAM pStream, size_t *pcchLine, PSCMEOL penmEol);
unsigned    ScmStreamGetCh(PSCMSTREAM pStream);
const char *ScmStreamGetCur(PSCMSTREAM pStream);
unsigned    ScmStreamPeekCh(PSCMSTREAM pStream);
int         ScmStreamRead(PSCMSTREAM pStream, void *pvBuf, size_t cbToRead);
bool        ScmStreamIsEndOfStream(PSCMSTREAM pStream);
bool        ScmStreamIsWhiteLine(PSCMSTREAM pStream, size_t iLine);
SCMEOL      ScmStreamGetEol(PSCMSTREAM pStream);
SCMEOL      ScmStreamGetEolByLine(PSCMSTREAM pStream, size_t iLine);

int         ScmStreamPutLine(PSCMSTREAM pStream, const char *pchLine, size_t cchLine, SCMEOL enmEol);
int         ScmStreamWrite(PSCMSTREAM pStream, const char *pchBuf, size_t cchBuf);
int         ScmStreamPutCh(PSCMSTREAM pStream, char ch);
int         ScmStreamPutEol(PSCMSTREAM pStream, SCMEOL enmEol);
ssize_t     ScmStreamPrintf(PSCMSTREAM pStream, const char *pszFormat, ...);
ssize_t     ScmStreamPrintfV(PSCMSTREAM pStream, const char *pszFormat, va_list va);
int         ScmStreamCopyLines(PSCMSTREAM pDst, PSCMSTREAM pSrc, size_t cLines);

bool        ScmStreamCMatchingWordM1(PSCMSTREAM pStream, const char *pszWord, size_t cchWord);
const char *ScmStreamCGetWord(PSCMSTREAM pStream, size_t *pcchWord);
const char *ScmStreamCGetWordM1(PSCMSTREAM pStream, size_t *pcchWord);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_bldprogs_scmstream_h */

