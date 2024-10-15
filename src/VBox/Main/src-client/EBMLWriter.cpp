/* $Id: EBMLWriter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * EBMLWriter.cpp - EBML writer implementation.
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

/**
 * For more information, see:
 * - https://w3c.github.io/media-source/webm-byte-stream-format.html
 * - https://www.webmproject.org/docs/container/#muxer-guidelines
 */

#ifdef LOG_GROUP
# undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_MAIN_DISPLAY
#include "LoggingNew.h"

#include <list>
#include <map>
#include <queue>
#include <stack>

#include <math.h> /* For lround.h. */

#include <iprt/asm.h>
#include <iprt/buildconfig.h>
#include <iprt/cdefs.h>
#include <iprt/critsect.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/rand.h>
#include <iprt/string.h>

#include <VBox/log.h>
#include <VBox/version.h>

#include "EBMLWriter.h"
#include "EBML_MKV.h"

/** No flags set. */
#define VBOX_EBMLWRITER_FLAG_NONE               0
/** The file handle was inherited. */
#define VBOX_EBMLWRITER_FLAG_HANDLE_INHERITED   RT_BIT(0)

/** Creates an EBML output file using an existing, open file handle. */
int EBMLWriter::createEx(const char *a_pszFile, PRTFILE phFile)
{
    AssertPtrReturn(phFile, VERR_INVALID_POINTER);

    m_hFile   = *phFile;
    m_fFlags |= VBOX_EBMLWRITER_FLAG_HANDLE_INHERITED;
    m_strFile = a_pszFile;

    return VINF_SUCCESS;
}

/** Creates an EBML output file using a file name. */
int EBMLWriter::create(const char *a_pszFile, uint64_t fOpen)
{
    int vrc = RTFileOpen(&m_hFile, a_pszFile, fOpen);
    if (RT_SUCCESS(vrc))
        m_strFile = a_pszFile;

    return vrc;
}

/** Returns available space on storage. */
uint64_t EBMLWriter::getAvailableSpace(void)
{
    RTFOFF pcbFree;
    int vrc = RTFileQueryFsSizes(m_hFile, NULL, &pcbFree, 0, 0);
    return (RT_SUCCESS(vrc)? (uint64_t)pcbFree : UINT64_MAX);
}

/** Closes the file. */
void EBMLWriter::close(void)
{
    if (!isOpen())
        return;

    AssertMsg(m_Elements.size() == 0,
              ("%zu elements are not closed yet (next element to close is 0x%x)\n",
               m_Elements.size(), m_Elements.top().classId));

    if (!(m_fFlags & VBOX_EBMLWRITER_FLAG_HANDLE_INHERITED))
    {
        RTFileClose(m_hFile);
        m_hFile = NIL_RTFILE;
    }

    m_fFlags  = VBOX_EBMLWRITER_FLAG_NONE;
    m_strFile = "";
}

/** Starts an EBML sub-element. */
EBMLWriter& EBMLWriter::subStart(EbmlClassId classId)
{
    writeClassId(classId);
    /* store the current file offset. */
    m_Elements.push(EbmlSubElement(RTFileTell(m_hFile), classId));
    /* Indicates that size of the element
     * is unkown (as according to EBML specs).
     */
    writeUnsignedInteger(UINT64_C(0x01FFFFFFFFFFFFFF));
    return *this;
}

/** Ends an EBML sub-element. */
EBMLWriter& EBMLWriter::subEnd(EbmlClassId classId)
{
#ifdef VBOX_STRICT
    /* Class ID on the top of the stack should match the class ID passed
     * to the function. Otherwise it may mean that we have a bug in the code.
     */
    AssertMsg(!m_Elements.empty(), ("No elements to close anymore\n"));
    AssertMsg(m_Elements.top().classId == classId,
              ("Ending sub element 0x%x is in wrong order (next to close is 0x%x)\n", classId, m_Elements.top().classId));
#else
    RT_NOREF(classId);
#endif

    uint64_t uPos = RTFileTell(m_hFile);
    uint64_t uSize = uPos - m_Elements.top().offset - 8;
    RTFileSeek(m_hFile, m_Elements.top().offset, RTFILE_SEEK_BEGIN, NULL);

    /* Make sure that size will be serialized as uint64_t. */
    writeUnsignedInteger(uSize | UINT64_C(0x0100000000000000));
    RTFileSeek(m_hFile, uPos, RTFILE_SEEK_BEGIN, NULL);
    m_Elements.pop();
    return *this;
}

/** Serializes a null-terminated string. */
EBMLWriter& EBMLWriter::serializeString(EbmlClassId classId, const char *str)
{
    writeClassId(classId);
    uint64_t size = strlen(str);
    writeSize(size);
    write(str, size);
    return *this;
}

/** Serializes an UNSIGNED integer.
 *  If size is zero then it will be detected automatically. */
EBMLWriter& EBMLWriter::serializeUnsignedInteger(EbmlClassId classId, uint64_t parm, size_t size /* = 0 */)
{
    writeClassId(classId);
    if (!size) size = getSizeOfUInt(parm);
    writeSize(size);
    writeUnsignedInteger(parm, size);
    return *this;
}

/** Serializes a floating point value.
 *
 * Only 8-bytes double precision values are supported
 * by this function.
 */
EBMLWriter& EBMLWriter::serializeFloat(EbmlClassId classId, float value)
{
    writeClassId(classId);
    Assert(sizeof(uint32_t) == sizeof(float));
    writeSize(sizeof(float));

    union
    {
        float   f;
        uint8_t u8[4];
    } u;

    u.f = value;

    for (int i = 3; i >= 0; i--) /* Converts values to big endian. */
        write(&u.u8[i], 1);

    return *this;
}

/** Serializes binary data. */
EBMLWriter& EBMLWriter::serializeData(EbmlClassId classId, const void *pvData, size_t cbData)
{
    writeClassId(classId);
    writeSize(cbData);
    write(pvData, cbData);
    return *this;
}

/** Writes raw data to file. */
int EBMLWriter::write(const void *data, size_t size)
{
    return RTFileWrite(m_hFile, data, size, NULL);
}

/** Writes an unsigned integer of variable of fixed size. */
void EBMLWriter::writeUnsignedInteger(uint64_t value, size_t size /* = sizeof(uint64_t) */)
{
    /* convert to big-endian */
    value = RT_H2BE_U64(value);
    write(reinterpret_cast<uint8_t*>(&value) + sizeof(value) - size, size);
}

/** Writes EBML class ID to file.
 *
 * EBML ID already has a UTF8-like represenation
 * so getSizeOfUInt is used to determine
 * the number of its bytes.
 */
void EBMLWriter::writeClassId(EbmlClassId parm)
{
    writeUnsignedInteger(parm, getSizeOfUInt(parm));
}

/** Writes data size value. */
void EBMLWriter::writeSize(uint64_t parm)
{
    /* The following expression defines the size of the value that will be serialized
     * as an EBML UTF-8 like integer (with trailing bits represeting its size):
      1xxx xxxx                                                                              - value 0 to  2^7-2
      01xx xxxx  xxxx xxxx                                                                   - value 0 to 2^14-2
      001x xxxx  xxxx xxxx  xxxx xxxx                                                        - value 0 to 2^21-2
      0001 xxxx  xxxx xxxx  xxxx xxxx  xxxx xxxx                                             - value 0 to 2^28-2
      0000 1xxx  xxxx xxxx  xxxx xxxx  xxxx xxxx  xxxx xxxx                                  - value 0 to 2^35-2
      0000 01xx  xxxx xxxx  xxxx xxxx  xxxx xxxx  xxxx xxxx  xxxx xxxx                       - value 0 to 2^42-2
      0000 001x  xxxx xxxx  xxxx xxxx  xxxx xxxx  xxxx xxxx  xxxx xxxx  xxxx xxxx            - value 0 to 2^49-2
      0000 0001  xxxx xxxx  xxxx xxxx  xxxx xxxx  xxxx xxxx  xxxx xxxx  xxxx xxxx  xxxx xxxx - value 0 to 2^56-2
     */
    size_t size = 8 - ! (parm & (UINT64_MAX << 49)) - ! (parm & (UINT64_MAX << 42)) -
                      ! (parm & (UINT64_MAX << 35)) - ! (parm & (UINT64_MAX << 28)) -
                      ! (parm & (UINT64_MAX << 21)) - ! (parm & (UINT64_MAX << 14)) -
                      ! (parm & (UINT64_MAX << 7));
    /* One is subtracted in order to avoid loosing significant bit when size = 8. */
    uint64_t mask = RT_BIT_64(size * 8 - 1);
    writeUnsignedInteger((parm & (((mask << 1) - 1) >> size)) | (mask >> (size - 1)), size);
}

/** Size calculation for variable size UNSIGNED integer.
 *
 * The function defines the size of the number by trimming
 * consequent trailing zero bytes starting from the most significant.
 * The following statement is always true:
 * 1 <= getSizeOfUInt(arg) <= 8.
 *
 * Every !(arg & (UINT64_MAX << X)) expression gives one
 * if an only if all the bits from X to 63 are set to zero.
 */
size_t EBMLWriter::getSizeOfUInt(uint64_t arg)
{
    return 8 - ! (arg & (UINT64_MAX << 56)) - ! (arg & (UINT64_MAX << 48)) -
               ! (arg & (UINT64_MAX << 40)) - ! (arg & (UINT64_MAX << 32)) -
               ! (arg & (UINT64_MAX << 24)) - ! (arg & (UINT64_MAX << 16)) -
               ! (arg & (UINT64_MAX << 8));
}

