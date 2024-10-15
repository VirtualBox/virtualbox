/* $Id: VUSBSniffer.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual USB - Sniffer facility.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_USB_VUSBSniffer_h
#define VBOX_INCLUDED_SRC_USB_VUSBSniffer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/vusb.h>

RT_C_DECLS_BEGIN

/** Opaque VUSB sniffer handle. */
typedef struct VUSBSNIFFERINT *VUSBSNIFFER;
/** Pointer to a VUSB sniffer handle. */
typedef VUSBSNIFFER *PVUSBSNIFFER;

/** NIL sniffer instance handle. */
#define VUSBSNIFFER_NIL ((VUSBSNIFFER)0)

/**
 * VUSB Sniffer event types.
 */
typedef enum VUSBSNIFFEREVENT
{
    /** Invalid event. */
    VUSBSNIFFEREVENT_INVALID = 0,
    /** URB submit event. */
    VUSBSNIFFEREVENT_SUBMIT,
    /** URB complete event. */
    VUSBSNIFFEREVENT_COMPLETE,
    /** URB submit failed event. */
    VUSBSNIFFEREVENT_ERROR_SUBMIT,
    /** URB completed with error event. */
    VUSBSNIFFEREVENT_ERROR_COMPLETE,
    /** 32bit hack. */
    VUSBSNIFFEREVENT_32BIT_HACK = 0x7fffffff
} VUSBSNIFFEREVENT;

/** VUSB Sniffer creation flags.
 * @{ */
/** Default flags. */
#define VUSBSNIFFER_F_DEFAULT    0
/** Don't overwrite any existing capture file. */
#define VUSBSNIFFER_F_NO_REPLACE RT_BIT_32(0)
/** @} */

/**
 * Create a new VUSB sniffer instance dumping to the given capture file.
 *
 * @returns VBox status code.
 * @param   phSniffer             Where to store the handle to the sniffer instance on success.
 * @param   fFlags                Flags, combination of VUSBSNIFFER_F_*
 * @param   pszCaptureFilename    The filename to use for capturing the sniffed data.
 * @param   pszFmt                The format of the dump, NULL to select one based on the filename
 *                                extension.
 * @param   pszDesc               Optional description for the dump.
 */
DECLHIDDEN(int) VUSBSnifferCreate(PVUSBSNIFFER phSniffer, uint32_t fFlags,
                                  const char *pszCaptureFilename, const char *pszFmt,
                                  const char *pszDesc);

/**
 * Destroys the given VUSB sniffer instance.
 *
 * @param   hSniffer              The sniffer instance to destroy.
 */
DECLHIDDEN(void) VUSBSnifferDestroy(VUSBSNIFFER hSniffer);

/**
 * Records an VUSB event.
 *
 * @returns VBox status code.
 * @param   hSniffer              The sniffer instance.
 * @param   pUrb                  The URB triggering the event.
 * @param   enmEvent              The type of event to record.
 */
DECLHIDDEN(int) VUSBSnifferRecordEvent(VUSBSNIFFER hSniffer, PVUSBURB pUrb, VUSBSNIFFEREVENT enmEvent);


RT_C_DECLS_END
#endif /* !VBOX_INCLUDED_SRC_USB_VUSBSniffer_h */

