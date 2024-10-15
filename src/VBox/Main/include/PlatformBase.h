/* $Id: PlatformBase.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - Abstract base platform, used by the specific platform implementations (x86, ARM).
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_PlatformBase_h
#define MAIN_INCLUDED_PlatformBase_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/com/ptr.h>
#include <iprt/cpp/utils.h>

using namespace com;

struct Data;
class  Platform;

class PlatformBase
{
public:

    PlatformBase()
        : mParent(NULL)
        , mMachine(NULL)
    {

    }

protected:

    Platform * const mParent;
    Machine * const  mMachine;

    Data *m;
};
#endif /* !MAIN_INCLUDED_PlatformBase_h */
