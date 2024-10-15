/* $Id: VDBackends.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VD - builtin backends.
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/

#ifndef VBOX_INCLUDED_SRC_Storage_VDBackends_h
#define VBOX_INCLUDED_SRC_Storage_VDBackends_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vd-plugin.h>

#include <iprt/cdefs.h>

RT_C_DECLS_BEGIN

extern const VDIMAGEBACKEND g_RawBackend;
extern const VDIMAGEBACKEND g_VmdkBackend;
extern const VDIMAGEBACKEND g_VDIBackend;
extern const VDIMAGEBACKEND g_VhdBackend;
extern const VDIMAGEBACKEND g_ParallelsBackend;
extern const VDIMAGEBACKEND g_DmgBackend;
extern const VDIMAGEBACKEND g_ISCSIBackend;
extern const VDIMAGEBACKEND g_QedBackend;
extern const VDIMAGEBACKEND g_QCowBackend;
extern const VDIMAGEBACKEND g_VhdxBackend;
extern const VDIMAGEBACKEND g_CueBackend;
extern const VDIMAGEBACKEND g_VBoxIsoMakerBackend;

extern const VDCACHEBACKEND g_VciCacheBackend;

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Storage_VDBackends_h */

