/* $Id: USBIdDatabaseStub.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USB device vendor and product ID database - stub.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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

#include "USBIdDatabase.h"

const RTBLDPROGSTRTAB   USBIdDatabase::s_StrTab          =  { "", 0, 0, NULL };

const size_t            USBIdDatabase::s_cVendors        = 0;
const USBIDDBVENDOR     USBIdDatabase::s_aVendors[]      = { {0,0,0} };
const RTBLDPROGSTRREF   USBIdDatabase::s_aVendorNames[]  = { {0,0} };

const size_t            USBIdDatabase::s_cProducts       = 0;
const USBIDDBPROD       USBIdDatabase::s_aProducts[]     = { {0} };
const RTBLDPROGSTRREF   USBIdDatabase::s_aProductNames[] = { {0,0} };

