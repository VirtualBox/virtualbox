/* $Id: UIDefs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Global definitions.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

/* GUI includes: */
#include "UIDefs.h"

/* Other VBox includes: */
#include <VBox/version.h> /* VBOX_PUEL_PRODUCT */


/* File name definitions: */
const char* UIDefs::GUI_GuestAdditionsName = "VBoxGuestAdditions";
const char* UIDefs::GUI_ExtPackName = VBOX_PUEL_PRODUCT;

/* File extensions definitions: */
QStringList UIDefs::VBoxFileExts = QStringList() << "xml" << "vbox";
QStringList UIDefs::VBoxExtPackFileExts = QStringList() << "vbox-extpack";
QStringList UIDefs::OVFFileExts = QStringList() << "ovf" << "ova";
