/* $Id: DynLoadLibSolaris.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Dynamically load libraries for Solaris hosts.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#include "DynLoadLibSolaris.h"

#include <iprt/errcore.h>
#include <iprt/ldr.h>


/** -=-=-=-=-= LIB DLPI -=-=-=-=-=-=- **/

/**
 * Global pointer to the libdlpi module. This should only be set once all needed libraries
 * and symbols have been successfully loaded.
 */
static RTLDRMOD g_hLibDlpi = NIL_RTLDRMOD;

/**
 * Whether we have tried to load libdlpi yet.  This flag should only be set
 * to "true" after we have either loaded both libraries and all symbols which we need,
 * or failed to load something and unloaded.
 */
static bool g_fCheckedForLibDlpi = false;

/** All the symbols we need from libdlpi.
 * @{
 */
int (*g_pfnLibDlpiWalk)(dlpi_walkfunc_t *, void *, uint_t);
int (*g_pfnLibDlpiOpen)(const char *, dlpi_handle_t *, uint_t);
void (*g_pfnLibDlpiClose)(dlpi_handle_t);
/** @} */

bool VBoxSolarisLibDlpiFound(void)
{
    RTLDRMOD hLibDlpi;

    if (g_fCheckedForLibDlpi)
        return g_hLibDlpi != NIL_RTLDRMOD;
    g_fCheckedForLibDlpi = true;
    int vrc = RTLdrLoad(LIB_DLPI, &hLibDlpi);
    if (RT_SUCCESS(vrc))
    {
        /*
         * Unfortunately; we cannot make use of dlpi_get_physaddr because it requires us to
         * open the VNIC/link which requires root permissions :/
         */
        vrc  = RTLdrGetSymbol(hLibDlpi, "dlpi_walk", (void **)&g_pfnLibDlpiWalk);
        vrc |= RTLdrGetSymbol(hLibDlpi, "dlpi_close", (void **)&g_pfnLibDlpiClose);
        vrc |= RTLdrGetSymbol(hLibDlpi, "dlpi_open", (void **)&g_pfnLibDlpiOpen);
        if (RT_SUCCESS(vrc))
        {
            g_hLibDlpi = hLibDlpi;
            return true;
        }

        RTLdrClose(hLibDlpi);
    }
    hLibDlpi = NIL_RTLDRMOD;
    return false;
}

