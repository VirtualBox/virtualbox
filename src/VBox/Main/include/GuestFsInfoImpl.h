/* $Id: GuestFsInfoImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Guest file system information implementation.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_GuestFsInfoImpl_h
#define MAIN_INCLUDED_GuestFsInfoImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "GuestFsInfoWrap.h"
#include "GuestCtrlImplPrivate.h"

class ATL_NO_VTABLE GuestFsInfo
    : public GuestFsInfoWrap
{
public:
    /** @name COM and internal init/term/mapping cruft.
     * @{ */
    DECLARE_COMMON_CLASS_METHODS(GuestFsInfo)

    int     init(PCGSTCTLFSINFO pFsInfo);
    void    uninit(void);

    HRESULT FinalConstruct(void);
    void    FinalRelease(void);
    /** @}  */

    /** @name Internal access helpers.
     * @{ */
    const GSTCTLFSINFO &i_getData() const { return mData; }
    /** @}  */

private:

    /** Wrapped @name IGuestFsInfo properties.
     * @{ */
    HRESULT getFreeSize(LONG64 *aFreeSize);
    HRESULT getTotalSize(LONG64 *aTotalSize);
    HRESULT getBlockSize(ULONG *aBlockSize);
    HRESULT getSectorSize(ULONG *aSectorSize);
    HRESULT getSerialNumber(ULONG *aSerialNumber);
    HRESULT getIsRemote(BOOL *aIsRemote);
    HRESULT getIsCaseSensitive(BOOL *aIsCaseSensitive);
    HRESULT getIsReadOnly(BOOL *aIsReadOnly);
    HRESULT getIsCompressed(BOOL *aIsCompressed);
    HRESULT getSupportsFileCompression(BOOL *aSupportsFileCompression);
    HRESULT getMaxComponent(ULONG *aMaxComponent);
    HRESULT getType(com::Utf8Str &aType);
    HRESULT getLabel(com::Utf8Str &aLabel);
    HRESULT getMountPoint(com::Utf8Str &aMountPoint);
    /** @}  */

    GSTCTLFSINFO mData;
};

#endif /* !MAIN_INCLUDED_GuestFsInfoImpl_h */

