/* $Id: MachineLaunchVMCommonWorker.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - VM process launcher helper for VBoxSVC & VBoxSDS.
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

#ifndef MAIN_INCLUDED_MachineLaunchVMCommonWorker_h
#define MAIN_INCLUDED_MachineLaunchVMCommonWorker_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <vector>
#include "VirtualBoxBase.h"

int MachineLaunchVMCommonWorker(const Utf8Str &aNameOrId,
                                const Utf8Str &aComment,
                                const Utf8Str &aFrontend,
                                const std::vector<com::Utf8Str> &aEnvironmentChanges,
                                const Utf8Str &aExtraArg,
                                const Utf8Str &aFilename,
                                uint32_t      aFlags,
                                void         *aExtraData,
                                RTPROCESS     &aPid);

#endif /* !MAIN_INCLUDED_MachineLaunchVMCommonWorker_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */

