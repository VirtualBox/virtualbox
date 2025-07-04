/* $Id: resource.h 108598 2025-02-28 10:03:38Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxStub - resource header file.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Stub_resource_h
#define VBOX_INCLUDED_SRC_Stub_resource_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define IDI_VIRTUALBOX 101

#ifndef RT_MANIFEST
# define RT_MANIFEST 24
#endif
#define APP_MANIFEST 1

#ifdef VBOX_STUB_WITH_SPLASH
# define IDB_SPLASH    102
#endif

#endif /* !VBOX_INCLUDED_SRC_Stub_resource_h */
