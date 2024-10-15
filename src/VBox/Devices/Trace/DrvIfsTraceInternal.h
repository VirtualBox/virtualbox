/* $Id: DrvIfsTraceInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox interface callback tracing driver - internal header.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Trace_DrvIfsTraceInternal_h
#define VBOX_INCLUDED_SRC_Trace_DrvIfsTraceInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmserialifs.h>
#include <VBox/vmm/pdmtpmifs.h>

#include <iprt/types.h>


/**
 * Interface Tracing Driver Instance Data.
 */
typedef struct DRVIFTRACE
{
    /** @name Interfaces exposed by this driver.
     * @{ */
    PDMIBASE             IBase;
    PDMISERIALPORT       ISerialPort;
    PDMISERIALCONNECTOR  ISerialConnector;
    PDMITPMPORT          ITpmPort;
    PDMITPMCONNECTOR     ITpmConnector;
    /** @}  */

    /** @name Interfaces exposed by the driver below us.
     * @{ */
    PPDMISERIALCONNECTOR pISerialConBelow;
    PPDMITPMCONNECTOR    pITpmConBelow;
    /** @} */

    /** @name Interfaces exposed by the driver/device above us.
     * @{ */
    PPDMISERIALPORT      pISerialPortAbove;
    PPDMITPMPORT         pITpmPortAbove;
    /** @} */

    /** PDM device driver instance pointer. */
    PPDMDRVINS           pDrvIns;
    /** The trace log writer handle. */
    RTTRACELOGWR         hTraceLog;

} DRVIFTRACE;
/** Pointer to a interface tracing driver instance. */
typedef DRVIFTRACE *PDRVIFTRACE;


DECLHIDDEN(void) drvIfsTrace_SerialIfInit(PDRVIFTRACE pThis);
DECLHIDDEN(void) drvIfsTrace_TpmIfInit(PDRVIFTRACE pThis);

#endif /* !VBOX_INCLUDED_SRC_Trace_DrvIfsTraceInternal_h */
