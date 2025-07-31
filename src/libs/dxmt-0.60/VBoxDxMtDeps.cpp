/* $Id: VBoxDxMtDeps.cpp 110294 2025-07-18 10:38:25Z alexander.eichner@oracle.com $ */
/** @file
 * VBoxDxMt - For dragging in library objects.
 */

/*
 * Copyright (C) 2025 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/types.h>

#include <d3d11.h>

/** Just a dummy global structure containing a bunch of
 * function pointers to code which is wanted in the link.
 */
struct CLANG11WEIRDNESS { PFNRT pfn; } g_apfnVBoxDxMtDeps[] =
{
    { (PFNRT)D3D11CreateDevice },
    { NULL },
};
