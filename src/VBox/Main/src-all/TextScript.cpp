/* $Id: TextScript.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Classes for reading/parsing/saving text scripts (unattended installation, ++).
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
#define LOG_GROUP LOG_GROUP_MAIN_UNATTENDED
#include "LoggingNew.h"
#include "TextScript.h"

#include <VBox/err.h>

#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/vfs.h>
#include <iprt/path.h>

using namespace std;


/*********************************************************************************************************************************
*   BaseTextScript Implementation                                                                                                *
*********************************************************************************************************************************/

HRESULT BaseTextScript::read(const Utf8Str &rStrFilename)
{
    /*
     * Open the file for reading and figure it's size.  Capping the size
     * at 16MB so we don't exaust the heap on bad input.
     */
    HRESULT   hrc;
    RTVFSFILE hVfsFile;
    int vrc = RTVfsFileOpenNormal(rStrFilename.c_str(), RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        hrc = readFromHandle(hVfsFile, rStrFilename.c_str());
        RTVfsFileRelease(hVfsFile);
    }
    else
        hrc = mpSetError->setErrorVrc(vrc, tr("Failed to open '%s' (%Rrc)"), rStrFilename.c_str(), vrc);
    return hrc;
}

HRESULT BaseTextScript::readFromHandle(RTVFSFILE hVfsFile, const char *pszFilename)
{
    /*
     * Open the file for reading and figure it's size.  Capping the size
     * at 16MB so we don't exaust the heap on bad input.
     */
    HRESULT  hrc;
    uint64_t cbFile = 0;
    int vrc = RTVfsFileQuerySize(hVfsFile, &cbFile);
    if (   RT_SUCCESS(vrc)
        && cbFile < _16M)
    {
        /*
         * Exploint the jolt() feature of RTCString and read the content directly into
         * its storage buffer.
         */
        vrc = mStrScriptFullContent.reserveNoThrow((size_t)cbFile + 1);
        if (RT_SUCCESS(vrc))
        {
            char *pszDst = mStrScriptFullContent.mutableRaw();
            vrc = RTVfsFileReadAt(hVfsFile, 0 /*off*/, pszDst, (size_t)cbFile, NULL);
            pszDst[(size_t)cbFile] = '\0';
            if (RT_SUCCESS(vrc))
            {
                /*
                 * We must validate the encoding or we'll be subject to potential security trouble.
                 * If this turns out to be problematic, we will need to implement codeset
                 * conversion coping mechanisms.
                 */
                vrc = RTStrValidateEncodingEx(pszDst, (size_t)cbFile + 1,
                                              RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED | RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
                if (RT_SUCCESS(vrc))
                {
                    mStrScriptFullContent.jolt();
                    return S_OK;
                }

                hrc = mpSetError->setErrorVrc(vrc, tr("'%s' isn't valid UTF-8: %Rrc"), pszFilename, vrc);
            }
            else
                hrc = mpSetError->setErrorVrc(vrc, tr("Error reading '%s': %Rrc"), pszFilename, vrc);
            mStrScriptFullContent.setNull();
        }
        else
            hrc = mpSetError->setErrorVrc(vrc, tr("Failed to allocate memory (%'RU64 bytes) for '%s'", "", cbFile),
                                          cbFile, pszFilename);
    }
    else if (RT_SUCCESS(vrc))
        hrc = mpSetError->setErrorVrc(VERR_FILE_TOO_BIG, tr("'%s' is too big (max 16MB): %'RU64"), pszFilename, cbFile);
    else
        hrc = mpSetError->setErrorVrc(vrc, tr("RTVfsFileQuerySize failed (%Rrc)"), vrc);
    return hrc;
}

HRESULT BaseTextScript::save(const Utf8Str &rStrFilename, bool fOverwrite)
{
    /*
     * We may have to append the default filename to the
     */
    const char *pszFilename = rStrFilename.c_str();
    Utf8Str     strWithDefaultFilename;
    if (   getDefaultFilename() != NULL
        && *getDefaultFilename() != '\0'
        && RTDirExists(rStrFilename.c_str()) )
    {
        try
        {
            strWithDefaultFilename = rStrFilename;
            strWithDefaultFilename.append(RTPATH_SLASH);
            strWithDefaultFilename.append(getDefaultFilename());
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
        pszFilename = strWithDefaultFilename.c_str();
    }

    /*
     * Save the filename for later use.
     */
    try
    {
        mStrSavedPath = pszFilename;
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    /*
     * Use the saveToString method to produce the content.
     */
    Utf8Str strDst;
    HRESULT hrc = saveToString(strDst);
    if (SUCCEEDED(hrc))
    {
        /*
         * Write the content.
         */
        RTFILE   hFile;
        uint64_t fOpen = RTFILE_O_WRITE | RTFILE_O_DENY_ALL;
        if (fOverwrite)
            fOpen |= RTFILE_O_CREATE_REPLACE;
        else
            fOpen |= RTFILE_O_CREATE;
        int vrc = RTFileOpen(&hFile, pszFilename, fOpen);
        if (RT_SUCCESS(vrc))
        {
            vrc = RTFileWrite(hFile, strDst.c_str(), strDst.length(), NULL);
            if (RT_SUCCESS(vrc))
            {
                vrc = RTFileClose(hFile);
                if (RT_SUCCESS(vrc))
                {
                    LogRelFlow(("GeneralTextScript::save(): saved %zu bytes to '%s'\n", strDst.length(), pszFilename));
                    return S_OK;
                }
            }
            RTFileClose(hFile);
            RTFileDelete(pszFilename);
            hrc = mpSetError->setErrorVrc(vrc, tr("Error writing to '%s' (%Rrc)"), pszFilename, vrc);
        }
        else
            hrc = mpSetError->setErrorVrc(vrc, tr("Error creating/replacing '%s' (%Rrc)"), pszFilename, vrc);
    }
    return hrc;
}



/*********************************************************************************************************************************
*   GeneralTextScript Implementation                                                                                             *
*********************************************************************************************************************************/

HRESULT GeneralTextScript::parse()
{
    AssertReturn(!mfDataParsed, mpSetError->setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("parse called more than once")));

    /*
     * Split the raw context into an array of lines.
     */
    try
    {
        mScriptContentByLines = mStrScriptFullContent.split("\n");
    }
    catch (std::bad_alloc &)
    {
        mScriptContentByLines.clear();
        return E_OUTOFMEMORY;
    }

    mfDataParsed = true;
    return S_OK;
}

HRESULT GeneralTextScript::saveToString(Utf8Str &rStrDst)
{
    AssertReturn(mfDataParsed, mpSetError->setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("saveToString() called before parse()")));

    /*
     * Calc the required size first.
     */
    size_t const cLines = mScriptContentByLines.size();
    size_t       cbTotal = 1;
    for (size_t iLine = 0; iLine < cLines; iLine++)
        cbTotal = mScriptContentByLines[iLine].length() + 1;

    /*
     * Clear the output and try reserve sufficient space.
     */
    rStrDst.setNull();

    int vrc = rStrDst.reserveNoThrow(cbTotal);
    if (RT_FAILURE(vrc))
        return E_OUTOFMEMORY;

    /*
     * Assemble the output.
     */
    for (size_t iLine = 0; iLine < cLines; iLine++)
    {
        try
        {
            rStrDst.append(mScriptContentByLines[iLine]);
            rStrDst.append('\n');
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
    }

    return S_OK;
}

const RTCString &GeneralTextScript::getContentOfLine(size_t idxLine)
{
    if (idxLine < mScriptContentByLines.size())
        return mScriptContentByLines[idxLine];
    return Utf8Str::Empty;
}


HRESULT GeneralTextScript::setContentOfLine(size_t idxLine, const Utf8Str &rStrNewLine)
{
    AssertReturn(idxLine < mScriptContentByLines.size(),
                 mpSetError->setErrorBoth(E_FAIL, VERR_OUT_OF_RANGE,
                                          tr("attempting to set line %zu when there are only %zu lines", "",
                                             mScriptContentByLines.size()),
                                          idxLine, mScriptContentByLines.size()));
    try
    {
        mScriptContentByLines[idxLine] = rStrNewLine;
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

vector<size_t> GeneralTextScript::findTemplate(const Utf8Str &rStrNeedle,
                                               RTCString::CaseSensitivity enmCase /*= RTCString::CaseSensitive*/)
{
    vector<size_t> vecHitLineNumbers;
    size_t const   cLines = mScriptContentByLines.size();
    for (size_t iLine = 0; iLine < cLines; iLine++)
        if (mScriptContentByLines[iLine].contains(rStrNeedle, enmCase))
            vecHitLineNumbers.push_back(iLine);

    return vecHitLineNumbers;
}

HRESULT GeneralTextScript::findAndReplace(size_t idxLine, const Utf8Str &rStrNeedle, const Utf8Str &rStrReplacement)
{
    AssertReturn(idxLine < mScriptContentByLines.size(),
                 mpSetError->setErrorBoth(E_FAIL, VERR_OUT_OF_RANGE,
                                          tr("attempting search&replace in line %zu when there are only %zu lines", "",
                                             mScriptContentByLines.size()),
                                          idxLine, mScriptContentByLines.size()));

    RTCString &rDstString = mScriptContentByLines[idxLine];
    size_t const offNeedle = rDstString.find(&rStrNeedle);
    if (offNeedle != RTCString::npos)
    {
        try
        {
            RTCString strBefore(rDstString, 0, offNeedle);
            RTCString strAfter(rDstString, offNeedle + rStrNeedle.length());
            rDstString = strBefore;
            strBefore.setNull();
            rDstString.append(rStrReplacement);
            rDstString.append(strAfter);
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
    }
    return S_OK;
}

HRESULT GeneralTextScript::appendToLine(size_t idxLine, const Utf8Str &rStrToAppend)
{
    AssertReturn(idxLine < mScriptContentByLines.size(),
                 mpSetError->setErrorBoth(E_FAIL, VERR_OUT_OF_RANGE,
                                          tr("appending to line %zu when there are only %zu lines", "",
                                             mScriptContentByLines.size()),
                                          idxLine, mScriptContentByLines.size()));

    try
    {
        mScriptContentByLines[idxLine].append(rStrToAppend);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

HRESULT GeneralTextScript::prependToLine(size_t idxLine, const Utf8Str &rStrToPrepend)
{
    AssertReturn(idxLine < mScriptContentByLines.size(),
                 mpSetError->setErrorBoth(E_FAIL, VERR_OUT_OF_RANGE,
                                          tr("prepending to line %zu when there are only %zu lines", "",
                                             mScriptContentByLines.size()),
                                          idxLine, mScriptContentByLines.size()));

    RTCString &rDstString = mScriptContentByLines[idxLine];
    try
    {
        RTCString strCopy;
        rDstString.swap(strCopy);
        rDstString.reserve(strCopy.length() + rStrToPrepend.length() + 1);
        rDstString = rStrToPrepend;
        rDstString.append(strCopy);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

HRESULT GeneralTextScript::appendLine(const Utf8Str &rStrLineToAppend)
{
    AssertReturn(mfDataParsed, mpSetError->setErrorBoth(E_FAIL, VERR_WRONG_ORDER, tr("appendLine() called before parse()")));

    try
    {
        mScriptContentByLines.append(rStrLineToAppend);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}
