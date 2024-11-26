/* $Id: VBoxWinDrvInstInternal.h 107177 2024-11-26 12:34:25Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxWinDrvInstInternal.h - Internal header for VBoxWinDrvInst.cpp.
 *
 * Required for exposing internal stuff to the testcase(s).
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_installation_VBoxWinDrvInstInternal_h
#define VBOX_INCLUDED_SRC_installation_VBoxWinDrvInstInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/**
 * Enumeration specifying the driver (un)installation mode.
 */
typedef enum VBOXWINDRVINSTMODE
{
    /** Invalid mode; do not use. */
    VBOXWINDRVINSTMODE_INVALID = 0,
    /** Install a driver. */
    VBOXWINDRVINSTMODE_INSTALL,
    /** Install by executing an INF section. */
    VBOXWINDRVINSTMODE_INSTALL_INFSECTION,
    /** Uninstall a driver. */
    VBOXWINDRVINSTMODE_UNINSTALL,
    /** Uninstall by executing an INF section. */
    VBOXWINDRVINSTMODE_UNINSTALL_INFSECTION
} VBOXWINDRVINSTMODE;

/**
 * Structure for keeping driver (un)installation parameters.
 */
typedef struct VBOXWINDRVINSTPARMS
{
    /** Installation mode. */
    VBOXWINDRVINSTMODE enmMode;
    /** Installation flags of type VBOX_WIN_DRIVERINSTALL_F_XXX. */
    uint32_t           fFlags;
    /** INF file to use for (un)installation. */
    PRTUTF16           pwszInfFile;
    /** Union keeping specific parameters, depending on \a enmMode. */
    union
    {
        struct
        {
            /** Model including decoration (e.g. "VBoxUSB.NTAMD64"); optional and might be NULL. */
            PRTUTF16   pwszModel;
            /** Hardware (Pnp) ID; optional and might be NULL. */
            PRTUTF16   pwszPnpId;
            /** Name of section to install. */
            PRTUTF16   pwszSection;
        } UnInstall;
        struct
        {
            /** Section within in the INF file to execute. */
            PRTUTF16   pwszSection;
        } ExecuteInf;
    } u;
} VBOXWINDRVINSTPARMS;
/** Pointer to driver installation parameters. */
typedef VBOXWINDRVINSTPARMS *PVBOXWINDRVINSTPARMS;

#ifdef TESTCASE
PVBOXWINDRVINSTPARMS VBoxWinDrvInstTestGetParms(VBOXWINDRVINST hDrvInst);
void VBoxWinDrvInstTestParmsDestroy(PVBOXWINDRVINSTPARMS);
#endif

#endif /* !VBOX_INCLUDED_SRC_installation_VBoxWinDrvInstInternal_h */
