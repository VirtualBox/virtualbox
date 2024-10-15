/* $Id: SerialPortImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 *
 * VirtualBox COM class implementation
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

#ifndef MAIN_INCLUDED_SerialPortImpl_h
#define MAIN_INCLUDED_SerialPortImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "SerialPortWrap.h"

class GuestOSType;

namespace settings
{
    struct SerialPort;
}

class ATL_NO_VTABLE SerialPort :
    public SerialPortWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(SerialPort)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Machine *aParent, ULONG aSlot);
    HRESULT init(Machine *aParent, SerialPort *aThat);
    HRESULT initCopy(Machine *parent, SerialPort *aThat);
    void uninit();

    // public methods only for internal purposes
    HRESULT i_loadSettings(const settings::SerialPort &data);
    HRESULT i_saveSettings(settings::SerialPort &data);

    bool i_isModified();
    void i_rollback();
    void i_commit();
    void i_copyFrom(SerialPort *aThat);

    void i_applyDefaults(GuestOSType *aOsType);
    bool i_hasDefaults();

    // public methods for internal purposes only
    // (ensure there is a caller and a read lock before calling them!)

private:

    HRESULT i_checkSetPath(const Utf8Str &str);

    // Wrapped ISerialPort properties
    HRESULT getEnabled(BOOL *aEnabled);
    HRESULT setEnabled(BOOL aEnabled);
    HRESULT getHostMode(PortMode_T *aHostMode);
    HRESULT setHostMode(PortMode_T aHostMode);
    HRESULT getSlot(ULONG *aSlot);
    HRESULT getIRQ(ULONG *aIRQ);
    HRESULT setIRQ(ULONG aIRQ);
    HRESULT getIOAddress(ULONG *aIOAddress);
    HRESULT setIOAddress(ULONG aIOAddress);
    HRESULT getServer(BOOL *aServer);
    HRESULT setServer(BOOL aServer);
    HRESULT getPath(com::Utf8Str &aPath);
    HRESULT setPath(const com::Utf8Str &aPath);
    HRESULT getUartType(UartType_T *aUartType);
    HRESULT setUartType(UartType_T aUartType);

    struct Data;
    Data *m;
};

#endif /* !MAIN_INCLUDED_SerialPortImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
