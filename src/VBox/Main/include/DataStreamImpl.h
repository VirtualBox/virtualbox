/* $Id: DataStreamImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_DataStreamImpl_h
#define MAIN_INCLUDED_DataStreamImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "DataStreamWrap.h"

#include <iprt/circbuf.h>
#include <iprt/semaphore.h>

class ATL_NO_VTABLE DataStream
    : public DataStreamWrap
{
public:
    DECLARE_COMMON_CLASS_METHODS(DataStream)

    HRESULT FinalConstruct();
    void FinalRelease();

    HRESULT init(unsigned long aBufferSize);
    void uninit();

    /// Feed data into the stream, used by the stream source.
    /// Blocks if the internal buffer cannot take anything, otherwise
    /// as much as the internal buffer can hold is taken (if smaller
    /// than @a cbWrite). Modeled after RTStrmWriteEx.
    int i_write(const void *pvBuf, size_t cbWrite, size_t *pcbWritten);

    /// Marks the end of the stream.
    int i_close();

private:
    // wrapped IDataStream attributes and methods
    HRESULT getReadSize(ULONG *aReadSize);
    HRESULT read(ULONG aSize, ULONG aTimeoutMS, std::vector<BYTE> &aData);

private:
    /** The temporary buffer the conversion process writes into and the user reads from. */
    PRTCIRCBUF        m_pBuffer;
    /** Event semaphore for waiting until data is available. */
    RTSEMEVENT        m_hSemEvtDataAvail;
    /** Event semaphore for waiting until there is room in the buffer for writing. */
    RTSEMEVENT        m_hSemEvtBufSpcAvail;
    /** Flag whether the end of stream flag is set. */
    bool              m_fEos;
};

#endif /* !MAIN_INCLUDED_DataStreamImpl_h */

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
