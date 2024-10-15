/* $Id: TextScript.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef MAIN_INCLUDED_TextScript_h
#define MAIN_INCLUDED_TextScript_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VirtualBoxBase.h"
#include <iprt/cpp/utils.h>
#include <vector>


/**
 * Base class for all the script readers/editors.
 *
 * @todo get rid of this silly bugger.
 */
class AbstractScript
    : public RTCNonCopyable
{
protected:
    /** For setting errors.
     * Yeah, class isn't entirely abstract now.  */
    VirtualBoxBase *mpSetError;

private: /* no default constructors for children. */
    AbstractScript() {}

public:
    DECLARE_TRANSLATE_METHODS(AbstractScript)

    AbstractScript(VirtualBoxBase *pSetError) : mpSetError(pSetError) {}
    virtual ~AbstractScript() {}

    /**
     * Read a script from a file
     */
    virtual HRESULT read(const Utf8Str &rStrFilename) = 0;

    /**
     * Read a script from a VFS file handle.
     */
    virtual HRESULT readFromHandle(RTVFSFILE hVfsFile, const char *pszFilename) = 0;

    /**
     * Parse the script
     */
    virtual HRESULT parse() = 0;

    /**
     * Save a script to a string.
     *
     * This is used by save() and later others to deloy the script.
     */
    virtual HRESULT saveToString(Utf8Str &rStrDst) = 0;

    /**
     * Save a script to a file.
     * @param   rStrPath        Where to save the script.  This normally points to a
     *                          file, but in a number of child use cases it's
     *                          actually giving a directory to put the script in
     *                          using the default deloyment filename.  One day we
     *                          might make the caller do this path joining.
     * @param   fOverwrite      Whether to overwrite the file or not.
     */
    virtual HRESULT save(const Utf8Str &rStrPath, bool fOverwrite) = 0;

    /**
     * Path where an actual script with user's data is located
     */
    virtual const Utf8Str &getActualScriptPath() const = 0;
};

/**
 * Base class for text based script readers/editors.
 *
 * This deals with reading the file into a string data member, writing it back
 * out to a file, and remember the filenames.
 */
class BaseTextScript : public AbstractScript
{
protected:
    const char * const mpszDefaultTemplateFilename; /**< The default template filename.  Can be empty. */
    const char * const mpszDefaultFilename;         /**< Filename to use when someone calls save() with a directory path.  Can be NULL. */
    RTCString   mStrScriptFullContent;  /**< Raw text file content.  Produced by read() and typically only used by parse(). */
    Utf8Str     mStrOriginalPath;       /**< Path where an original script is located (set by read()). */
    Utf8Str     mStrSavedPath;          /**< Path where an saved script with user's data is located (set by save()). */

public:
    DECLARE_TRANSLATE_METHODS(BaseTextScript)

    BaseTextScript(VirtualBoxBase *pSetError, const char *pszDefaultTemplateFilename, const char *pszDefaultFilename)
        : AbstractScript(pSetError)
        , mpszDefaultTemplateFilename(pszDefaultTemplateFilename)
        , mpszDefaultFilename(pszDefaultFilename)
    { }
    virtual ~BaseTextScript() {}

    HRESULT read(const Utf8Str &rStrFilename);
    HRESULT readFromHandle(RTVFSFILE hVfsFile, const char *pszFilename);
    HRESULT save(const Utf8Str &rStrFilename, bool fOverwrite);

    /**
     * Gets the default filename for this class of scripts (empty if none).
     *
     * @note Just the filename, no path.
     */
    const char *getDefaultFilename() const
    {
        return mpszDefaultFilename;
    }

    /**
     * Gets the default template filename for this class of scripts (empty if none).
     *
     * @note Just the filename, no path.
     */
    const char *getDefaultTemplateFilename() const
    {
        return mpszDefaultTemplateFilename;
    }

    /**
     * Path to the file we last saved the script as.
     */
    const Utf8Str &getActualScriptPath() const
    {
        return mStrSavedPath;
    }

    /**
     * Path where an original script is located
     */
    const Utf8Str &getOriginalScriptPath() const
    {
        return mStrOriginalPath;
    }
};


/**
 * Generic line based text script editor.
 *
 * This is used for editing isolinux configuratin files among other things.
 */
class GeneralTextScript : public BaseTextScript
{
protected:
    RTCList<RTCString>  mScriptContentByLines; /**< Content index by line. This contains the edited version. */
    bool                mfDataParsed;          /**< Indicates whether the script has been parse() yet.  */

public:
    DECLARE_TRANSLATE_METHODS(GeneralTextScript)

    GeneralTextScript(VirtualBoxBase *pSetError, const char *pszDefaultTemplateFilename = NULL, const char *pszDefaultFilename = NULL)
        : BaseTextScript(pSetError, pszDefaultTemplateFilename, pszDefaultFilename), mfDataParsed(false)
    {}
    virtual ~GeneralTextScript() {}

    HRESULT parse();
    HRESULT saveToString(Utf8Str &rStrDst);

    //////////////////New functions//////////////////////////////

    bool isDataParsed() const
    {
        return mfDataParsed;
    }

    /**
     * Returns the actual size of script in lines
     */
    size_t getLineNumbersOfScript() const
    {
        return mScriptContentByLines.size();
    }

    /**
     * Gets a read-only reference to the given line, returning Utf8Str::Empty if
     * idxLine is out of range.
     *
     * @returns Line string reference or Utf8Str::Empty.
     * @param   idxLine     The line number.
     *
     * @todo    RTCList doesn't allow this method to be const.
     */
    RTCString const &getContentOfLine(size_t idxLine);

    /**
     * Set new content of line
     */
    HRESULT setContentOfLine(size_t idxLine, const Utf8Str &newContent);

    /**
     * Find a substring in the script
     * Returns a list with the found lines
     * @throws std::bad_alloc
     */
    std::vector<size_t> findTemplate(const Utf8Str &rStrNeedle, RTCString::CaseSensitivity enmCase = RTCString::CaseSensitive);

    /**
     * In line @a idxLine replace the first occurence of @a rStrNeedle with
     * @a rStrRelacement.
     */
    HRESULT findAndReplace(size_t idxLine, const Utf8Str &rStrNeedle, const Utf8Str &rStrReplacement);

    /**
     * Append a string into the end of the given line.
     */
    HRESULT appendToLine(size_t idxLine, const Utf8Str &rStrToAppend);

    /**
     * Prepend a string in the beginning of the given line.
     */
    HRESULT prependToLine(size_t idxLine, const Utf8Str &rStrToPrepend);

    /**
     * Append a new line at the end of the list of line.
     */
    HRESULT appendLine(const Utf8Str &rStrLineToAppend);
    //////////////////New functions//////////////////////////////
};


#endif /* !MAIN_INCLUDED_TextScript_h */
