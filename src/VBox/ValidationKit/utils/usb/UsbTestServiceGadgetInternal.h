/* $Id: UsbTestServiceGadgetInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbTestServ - Remote USB test configuration and execution server, Interal gadget interfaces.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_SRC_usb_UsbTestServiceGadgetInternal_h
#define VBOX_INCLUDED_SRC_usb_UsbTestServiceGadgetInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>

#include "UsbTestServiceGadget.h"

RT_C_DECLS_BEGIN

/** Pointer to an opaque type dependent gadget host instance data. */
typedef struct UTSGADGETCLASSINT *PUTSGADGETCLASSINT;
/** Pointer to a gadget host instance pointer. */
typedef PUTSGADGETCLASSINT *PPUTSGADGETCLASSINT;

/**
 * Gadget class interface.
 */
typedef struct UTSGADGETCLASSIF
{
    /** The gadget class type implemented. */
    UTSGADGETCLASS            enmClass;
    /** Description. */
    const char               *pszDesc;
    /** Size of the class specific instance data. */
    size_t                    cbClass;

    /**
     * Initializes the gadget class instance.
     *
     * @returns IPRT status code.
     * @param   pClass        The interface specific instance data.
     * @param   paCfg         The configuration of the interface.
     */
    DECLR3CALLBACKMEMBER(int, pfnInit, (PUTSGADGETCLASSINT pClass, PCUTSGADGETCFGITEM paCfg));

    /**
     * Terminates the gadget class instance.
     *
     * @param   pClass        The interface specific instance data.
     */
    DECLR3CALLBACKMEMBER(void, pfnTerm, (PUTSGADGETCLASSINT pClass));

    /**
     * Returns the bus ID of the class instance.
     *
     * @returns Bus ID.
     * @param   pClass        The interface specific instance data.
     */
    DECLR3CALLBACKMEMBER(uint32_t, pfnGetBusId, (PUTSGADGETCLASSINT pClass));

    /**
     * Connects the gadget.
     *
     * @returns IPRT status code.
     * @param   pClass        The interface specific instance data.
     */
    DECLR3CALLBACKMEMBER(int, pfnConnect, (PUTSGADGETCLASSINT pClass));

    /**
     * Disconnect the gadget.
     *
     * @returns IPRT status code.
     * @param   pClass        The interface specific instance data.
     */
    DECLR3CALLBACKMEMBER(int, pfnDisconnect, (PUTSGADGETCLASSINT pClass));

} UTSGADGETCLASSIF;
/** Pointer to a gadget class callback table. */
typedef UTSGADGETCLASSIF *PUTSGADGETCLASSIF;
/** Pointer to a const gadget host callback table. */
typedef const struct UTSGADGETCLASSIF *PCUTSGADGETCLASSIF;

extern UTSGADGETCLASSIF const g_UtsGadgetClassTest;

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_usb_UsbTestServiceGadgetInternal_h */

