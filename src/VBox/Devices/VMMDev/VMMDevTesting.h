/* $Id: VMMDevTesting.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMMDev - Guest <-> VMM/Host communication device, internal header.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_VMMDev_VMMDevTesting_h
#define VBOX_INCLUDED_SRC_VMMDev_VMMDevTesting_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <VBox/VMMDevTesting.h>

RT_C_DECLS_BEGIN

int  vmmdevR3TestingInitialize(PPDMDEVINS pDevIns);
void vmmdevR3TestingTerminate(PPDMDEVINS pDevIns);
int  vmmdevRZTestingInitialize(PPDMDEVINS pDevIns);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_VMMDev_VMMDevTesting_h */

