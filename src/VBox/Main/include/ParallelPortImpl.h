/* $Id: ParallelPortImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 * VirtualBox COM class implementation.
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

#ifndef MAIN_INCLUDED_ParallelPortImpl_h
#define MAIN_INCLUDED_ParallelPortImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "ParallelPortWrap.h"

namespace settings
{
    struct ParallelPort;
}

class ATL_NO_VTABLE ParallelPort :
    public ParallelPortWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(ParallelPort)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *aParent, ULONG aSlot);
    HRESULT init(Machine *aParent, ParallelPort *aThat);
    HRESULT initCopy(Machine *parent, ParallelPort *aThat);
    void uninit();

    HRESULT i_loadSettings(const settings::ParallelPort &data);
    HRESULT i_saveSettings(settings::ParallelPort &data);

    // public methods only for internal purposes
    bool i_isModified();
    void i_rollback();
    void i_commit();
    void i_copyFrom(ParallelPort *aThat);
    void i_applyDefaults();
    bool i_hasDefaults();

private:

    // Wrapped IParallelPort properties
    HRESULT getEnabled(BOOL *aEnabled);
    HRESULT setEnabled(BOOL aEnabled);
    HRESULT getSlot(ULONG *aSlot);
    HRESULT getIRQ(ULONG *aIRQ);
    HRESULT setIRQ(ULONG aIRQ);
    HRESULT getIOBase(ULONG *aIOBase);
    HRESULT setIOBase(ULONG aIOBase);
    HRESULT getPath(com::Utf8Str &aPath);
    HRESULT setPath(const com::Utf8Str &aPath);

    struct Data;
    Data *m;
};

#endif /* !MAIN_INCLUDED_ParallelPortImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
