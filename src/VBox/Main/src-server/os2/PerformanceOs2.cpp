/* $Id: PerformanceOs2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 *
 * VBox OS/2-specific Performance Classes implementation.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#include "Performance.h"

namespace pm {

class CollectorOS2 : public CollectorHAL
{
public:
    virtual int getHostCpuLoad(ULONG *user, ULONG *kernel, ULONG *idle);
    virtual int getHostCpuMHz(ULONG *mhz);
    virtual int getHostMemoryUsage(ULONG *total, ULONG *used, ULONG *available);
    virtual int getProcessCpuLoad(RTPROCESS process, ULONG *user, ULONG *kernel);
    virtual int getProcessMemoryUsage(RTPROCESS process, ULONG *used);
};


CollectorHAL *createHAL()
{
    return new CollectorOS2();
}

int CollectorOS2::getHostCpuLoad(ULONG *user, ULONG *kernel, ULONG *idle)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorOS2::getHostCpuMHz(ULONG *mhz)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorOS2::getHostMemoryUsage(ULONG *total, ULONG *used, ULONG *available)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorOS2::getProcessCpuLoad(RTPROCESS process, ULONG *user, ULONG *kernel)
{
    return VERR_NOT_IMPLEMENTED;
}

int CollectorOS2::getProcessMemoryUsage(RTPROCESS process, ULONG *used)
{
    return VERR_NOT_IMPLEMENTED;
}

}
