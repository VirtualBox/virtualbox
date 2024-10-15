/* $Id: EBMLWriter.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * EBMLWriter.h - EBML writer.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_EBMLWriter_h
#define MAIN_INCLUDED_EBMLWriter_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <stack>

#include <iprt/critsect.h>
#include <iprt/file.h>

#include <VBox/com/string.h>


/** No flags set. */
#define VBOX_EBMLWRITER_FLAG_NONE               0
/** The file handle was inherited. */
#define VBOX_EBMLWRITER_FLAG_HANDLE_INHERITED   RT_BIT(0)

class EBMLWriter
{
public:
    typedef uint32_t EbmlClassId;

private:

    struct EbmlSubElement
    {
        uint64_t offset;
        EbmlClassId classId;
        EbmlSubElement(uint64_t offs, EbmlClassId cid) : offset(offs), classId(cid) {}
    };

    /** Stack of EBML sub elements. */
    std::stack<EbmlSubElement> m_Elements;
    /** The file's handle. */
    RTFILE                     m_hFile;
    /** The file's name (path). */
    com::Utf8Str               m_strFile;
    /** Flags. */
    uint32_t                   m_fFlags;

public:

    EBMLWriter(void)
        : m_hFile(NIL_RTFILE)
        , m_fFlags(VBOX_EBMLWRITER_FLAG_NONE) { }

    virtual ~EBMLWriter(void) { close(); }

public:

    int createEx(const char *a_pszFile, PRTFILE phFile);

    int create(const char *a_pszFile, uint64_t fOpen);

    void close(void);

    /** Returns the file name. */
    const com::Utf8Str& getFileName(void) { return m_strFile; }

    /** Returns file size. */
    uint64_t getFileSize(void) { return RTFileTell(m_hFile); }

    /** Get reference to file descriptor */
    inline const RTFILE &getFile(void) { return m_hFile; }

    /** Returns available space on storage. */
    uint64_t getAvailableSpace(void);

    /**
     * Returns whether the file is open or not.
     *
     * @returns True if open, false if not.
     */
    bool isOpen(void) { return RTFileIsValid(m_hFile); }

public:

    EBMLWriter &subStart(EbmlClassId classId);

    EBMLWriter &subEnd(EbmlClassId classId);

    EBMLWriter &serializeString(EbmlClassId classId, const char *str);

    EBMLWriter &serializeUnsignedInteger(EbmlClassId classId, uint64_t parm, size_t size = 0);

    EBMLWriter &serializeFloat(EbmlClassId classId, float value);

    EBMLWriter &serializeData(EbmlClassId classId, const void *pvData, size_t cbData);

    int write(const void *data, size_t size);

    void writeUnsignedInteger(uint64_t value, size_t size = sizeof(uint64_t));

    void writeClassId(EbmlClassId parm);

    void writeSize(uint64_t parm);

    static inline size_t getSizeOfUInt(uint64_t arg);

private:

    void operator=(const EBMLWriter &);
};

#endif /* !MAIN_INCLUDED_EBMLWriter_h */

