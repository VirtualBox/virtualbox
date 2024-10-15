/* $Id: VBoxNetAdp-win.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetAdp-win.h - Host-only Miniport Driver, Windows-specific code.
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

#ifndef VBOX_INCLUDED_SRC_VBoxNetAdp_win_VBoxNetAdp_win_h
#define VBOX_INCLUDED_SRC_VBoxNetAdp_win_VBoxNetAdp_win_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define VBOXNETADP_VERSION_NDIS_MAJOR        6
#define VBOXNETADP_VERSION_NDIS_MINOR        0

#define VBOXNETADP_VERSION_MAJOR             1
#define VBOXNETADP_VERSION_MINOR             0

#define VBOXNETADP_VENDOR_NAME               "Oracle"
#define VBOXNETADP_VENDOR_ID                 0xFFFFFF
#define VBOXNETADP_MCAST_LIST_SIZE           32
#define VBOXNETADP_MAX_FRAME_SIZE            1518 // TODO: 14+4+1500

//#define VBOXNETADP_NAME_UNIQUE               L"{7af6b074-048d-4444-bfce-1ecc8bc5cb76}"
#define VBOXNETADP_NAME_SERVICE              L"VBoxNetAdp"

#define VBOXNETADP_NAME_LINK                 L"\\DosDevices\\Global\\VBoxNetAdp"
#define VBOXNETADP_NAME_DEVICE               L"\\Device\\VBoxNetAdp"

#define VBOXNETADPWIN_TAG                    'ANBV'

#define VBOXNETADPWIN_ATTR_FLAGS             NDIS_MINIPORT_ATTRIBUTES_NDIS_WDM | NDIS_MINIPORT_ATTRIBUTES_NO_HALT_ON_SUSPEND
#define VBOXNETADP_MAC_OPTIONS               NDIS_MAC_OPTION_NO_LOOPBACK
#define VBOXNETADP_SUPPORTED_FILTERS         (NDIS_PACKET_TYPE_DIRECTED | \
                                              NDIS_PACKET_TYPE_MULTICAST | \
                                              NDIS_PACKET_TYPE_BROADCAST | \
                                              NDIS_PACKET_TYPE_PROMISCUOUS | \
                                              NDIS_PACKET_TYPE_ALL_MULTICAST)
#define VBOXNETADPWIN_SUPPORTED_STATISTICS   0 //TODO!
#define VBOXNETADPWIN_HANG_CHECK_TIME        4

#endif /* !VBOX_INCLUDED_SRC_VBoxNetAdp_win_VBoxNetAdp_win_h */
