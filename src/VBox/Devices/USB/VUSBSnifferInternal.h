/* $Id: VUSBSnifferInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual USB Sniffer facility - Internal header.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_USB_VUSBSnifferInternal_h
#define VBOX_INCLUDED_SRC_USB_VUSBSnifferInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>

#include "VUSBSniffer.h"

RT_C_DECLS_BEGIN

/** Pointer to a stream operations structure. */
typedef struct VUSBSNIFFERSTRM *PVUSBSNIFFERSTRM;
/** Pointer to the internal format specific state. */
typedef struct VUSBSNIFFERFMTINT *PVUSBSNIFFERFMTINT;

/**
 * Stream operations structure.
 */
typedef struct VUSBSNIFFERSTRM
{
    /**
     * Write the given buffer to the underlying stream.
     *
     * @returns VBox status code.
     * @param   pStrm    The pointer to the interface containing this method.
     * @param   pvBuf    The buffer to write.
     * @param   cbBuf    How many bytes to write.
     */
    DECLR3CALLBACKMEMBER(int, pfnWrite, (PVUSBSNIFFERSTRM pStrm, const void *pvBuf, size_t cbBuf));

} VUSBSNIFFERSTRM;


/**
 * VUSB Sniffer format backend.
 */
typedef struct VUSBSNIFFERFMT
{
    /** Name of the format. */
    char            szName[16];
    /** Description of the format. */
    const char     *pszDesc;
    /** Supported file extensions - terminated with a NULL entry. */
    const char     **papszFileExts;
    /** Size of the format specific state structure in bytes. */
    size_t          cbFmt;

    /**
     * Initializes the format.
     *
     * @returns VBox status code.
     * @param   pThis    Pointer to the unitialized format specific state.
     * @param   pStrm    The stream to use for writing.
     */
    DECLR3CALLBACKMEMBER(int, pfnInit, (PVUSBSNIFFERFMTINT pThis, PVUSBSNIFFERSTRM pStrm));

    /**
     * Destroys the format instance.
     *
     * @param   pThis    Pointer to the format specific state.
     */
    DECLR3CALLBACKMEMBER(void, pfnDestroy, (PVUSBSNIFFERFMTINT pThis));

    /**
     * Records the given VUSB event.
     *
     * @returns VBox status code.
     * @param   pThis    Pointer to the format specific state.
     * @param   pUrb     The URB triggering the event.
     * @param   enmEvent The type of event to record.
     */
    DECLR3CALLBACKMEMBER(int, pfnRecordEvent, (PVUSBSNIFFERFMTINT pThis, PVUSBURB pUrb, VUSBSNIFFEREVENT enmEvent));

} VUSBSNIFFERFMT;
/** Pointer to a VUSB Sniffer format backend. */
typedef VUSBSNIFFERFMT *PVUSBSNIFFERFMT;
/** Pointer to a const VUSB Sniffer format backend. */
typedef const VUSBSNIFFERFMT *PCVUSBSNIFFERFMT;

/** PCAP-Ng format writer. */
extern const VUSBSNIFFERFMT g_VUsbSnifferFmtPcapNg;
/** VMware VMX log format writer. */
extern const VUSBSNIFFERFMT g_VUsbSnifferFmtVmx;
/** Linux UsbMon log format writer. */
extern const VUSBSNIFFERFMT g_VUsbSnifferFmtUsbMon;

RT_C_DECLS_END
#endif /* !VBOX_INCLUDED_SRC_USB_VUSBSnifferInternal_h */
