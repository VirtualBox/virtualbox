/* $Id: IEMAllN8veLiveness6-x86.cpp 108370 2025-02-13 16:26:48Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Native Recompiler, Liveness Analysis, x86 target, Part 6.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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


/* Common header with all the IEM_MC_XXX defines and whatnot. */
#include "IEMAllN8veLiveness-x86.h"

/* Include the generated headers: */
#include "IEMNativeLiveness.h"
#include "IEMNativeLiveness6.cpp.h"

