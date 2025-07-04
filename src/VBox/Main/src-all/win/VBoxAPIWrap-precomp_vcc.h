/* $Id: VBoxAPIWrap-precomp_vcc.h 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * VirtualBox COM - Visual C++ precompiled header for the API wrappers.
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


#include <iprt/cdefs.h>
#include <iprt/win/windows.h>
#include <VBox/cdefs.h>
#include <iprt/types.h>
#include <iprt/cpp/list.h>
#include <iprt/cpp/meta.h>
#include <iprt/cpp/ministring.h>
#include <VBox/com/microatl.h>
#include <VBox/com/com.h>
#include <VBox/com/array.h>
#include <VBox/com/Guid.h>
#include <VBox/com/string.h>

#include "VBox/com/VirtualBox.h"

#include "VirtualBoxBase.h"
#include "Wrapper.h"

#if defined(Log) || defined(LogIsEnabled)
# error "Log() from iprt/log.h cannot be defined in the precompiled header!"
#endif

