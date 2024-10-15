/* $Id: EmulatedUSBImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Emulated USB devices manager.
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

#ifndef MAIN_INCLUDED_EmulatedUSBImpl_h
#define MAIN_INCLUDED_EmulatedUSBImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "EmulatedUSBWrap.h"

#include <VBox/vrdpusb.h>

class Console;
class EUSBWEBCAM;

typedef std::map<Utf8Str, EUSBWEBCAM *> WebcamsMap;

class ATL_NO_VTABLE EmulatedUSB :
    public EmulatedUSBWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(EmulatedUSB)

    HRESULT FinalConstruct();
    void FinalRelease();

    /* Public initializer/uninitializer for internal purposes only. */
    HRESULT init(ComObjPtr<Console> pConsole);
    void uninit();

    /* Public method for internal use. */
    static DECLCALLBACK(int) i_eusbCallback(void *pv, const char *pszId, uint32_t iEvent,
                                            const void *pvData, uint32_t cbData);

    PEMULATEDUSBIF i_getEmulatedUsbIf();

    HRESULT i_webcamAttachInternal(const com::Utf8Str &aPath,
                                   const com::Utf8Str &aSettings,
                                   const char *pszDriver,
                                   void *pvObject);
    HRESULT i_webcamDetachInternal(const com::Utf8Str &aPath);

private:

    static DECLCALLBACK(int) eusbCallbackEMT(EmulatedUSB *pThis, char *pszId, uint32_t iEvent,
                                             void *pvData, uint32_t cbData);

    static DECLCALLBACK(int) i_QueryEmulatedUsbDataById(void *pvUser, const char *pszId, void **ppvEmUsbCb, void **ppvEmUsbCbData, void **ppvObject);

    HRESULT webcamPathFromId(com::Utf8Str *pPath, const char *pszId);

    // wrapped IEmulatedUSB properties
    virtual HRESULT getWebcams(std::vector<com::Utf8Str> &aWebcams);

    // wrapped IEmulatedUSB methods
    virtual HRESULT webcamAttach(const com::Utf8Str &aPath,
                                 const com::Utf8Str &aSettings);
    virtual HRESULT webcamDetach(const com::Utf8Str &aPath);

    /* Data. */
    struct Data
    {
        Data()
        {
        }

        ComObjPtr<Console> pConsole;
        WebcamsMap webcams;
    };

    Data m;
    EMULATEDUSBIF mEmUsbIf;
};

#endif /* !MAIN_INCLUDED_EmulatedUSBImpl_h */

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
