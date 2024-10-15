/* $Id: MediumIOImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - MediumIO.
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

#ifndef MAIN_INCLUDED_MediumIOImpl_h
#define MAIN_INCLUDED_MediumIOImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "MediumIOWrap.h"
#include "VirtualBoxBase.h"
#include "AutoCaller.h"

class ATL_NO_VTABLE MediumIO :
    public MediumIOWrap
{
public:
    /** @name Dummy/standard constructors and destructors.
     * @{ */
    DECLARE_COMMON_CLASS_METHODS(MediumIO)
    HRESULT FinalConstruct();
    void    FinalRelease();
    /** @} */

    /** @name Initializer & uninitializer.
     * @{ */
    HRESULT initForMedium(Medium *pMedium, VirtualBox *pVirtualBox, bool fWritable,
                          com::Utf8Str const &rStrKeyId, com::Utf8Str const &rStrPassword);
    void    uninit();
    /** @} */

private:
    /** @name Wrapped IMediumIO properties
     * @{ */
    HRESULT getMedium(ComPtr<IMedium> &a_rPtrMedium);
    HRESULT getWritable(BOOL *a_fWritable);
    HRESULT getExplorer(ComPtr<IVFSExplorer> &a_rPtrExplorer);
    /** @} */

    /** @name Wrapped IMediumIO methods
     * @{ */
    HRESULT read(LONG64 a_off, ULONG a_cbRead, std::vector<BYTE> &a_rData);
    HRESULT write(LONG64 a_off, const std::vector<BYTE> &a_rData, ULONG *a_pcbWritten);
    HRESULT formatFAT(BOOL a_fQuick);
    HRESULT initializePartitionTable(PartitionTableType_T a_enmFormat, BOOL a_fWholeDiskInOneEntry);
    HRESULT convertToStream(const com::Utf8Str &aFormat,
                            const std::vector<MediumVariant_T> &aVariant,
                            ULONG aBufferSize,
                            ComPtr<IDataStream> &aStream,
                            ComPtr<IProgress> &aProgress);
    HRESULT close();
    /** @} */

    /** @name Internal workers.
     *  @{ */
    void    i_close();
    /** @} */

    struct Data;
    Data *m;

    class StreamTask;
    friend class StreamTask;
};

#endif /* !MAIN_INCLUDED_MediumIOImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */

