/* $Id: VBoxNetLwf-win.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetLwf-win.h - Bridged Networking Driver, Windows-specific code.
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

#ifndef VBOX_INCLUDED_SRC_VBoxNetFlt_win_drv_VBoxNetLwf_win_h
#define VBOX_INCLUDED_SRC_VBoxNetFlt_win_drv_VBoxNetLwf_win_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define VBOXNETLWF_VERSION_NDIS_MAJOR        6
#define VBOXNETLWF_VERSION_NDIS_MINOR        0

#define VBOXNETLWF_NAME_FRIENDLY             L"VirtualBox NDIS Light-Weight Filter"
#define VBOXNETLWF_NAME_UNIQUE               L"{7af6b074-048d-4444-bfce-1ecc8bc5cb76}"
#define VBOXNETLWF_NAME_SERVICE              L"VBoxNetLwf"

#define VBOXNETLWF_NAME_LINK                 L"\\DosDevices\\Global\\VBoxNetLwf"
#define VBOXNETLWF_NAME_DEVICE               L"\\Device\\VBoxNetLwf"

#define VBOXNETLWF_MEM_TAG                   'FLBV'
#define VBOXNETLWF_REQ_ID                    'fLBV'

#endif /* !VBOX_INCLUDED_SRC_VBoxNetFlt_win_drv_VBoxNetLwf_win_h */
