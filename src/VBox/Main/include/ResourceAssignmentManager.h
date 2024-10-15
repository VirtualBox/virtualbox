/* $Id: ResourceAssignmentManager.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox resource assignment (Address ranges, interrupts) manager.
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

#ifndef MAIN_INCLUDED_ResourceAssignmentManager_h
#define MAIN_INCLUDED_ResourceAssignmentManager_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBox/types.h"
#include "VirtualBoxBase.h"
#include <vector>

class ResourceAssignmentManager
{
private:
    struct State;
    State *pState;

    ResourceAssignmentManager();

public:
    static ResourceAssignmentManager *createInstance(PCVMMR3VTABLE pVMM, ChipsetType_T chipsetType, IommuType_T iommuType,
                                                     RTGCPHYS GCPhysMmioTop, RTGCPHYS GCPhysRamStart,
                                                     RTGCPHYS GCPhysMmio32Start, RTGCPHYS cbMmio32,
                                                     uint32_t cInterrupts);

    ~ResourceAssignmentManager();

    HRESULT assignMmioRegion(const char *pszName, RTGCPHYS cbRegion, PRTGCPHYS pGCPhysStart, PRTGCPHYS pcbRegion);
    HRESULT assignMmio32Region(const char *pszName, RTGCPHYS cbRegion, PRTGCPHYS pGCPhysStart, PRTGCPHYS pcbRegion);
    HRESULT assignMmioRegionAligned(const char *pszName, RTGCPHYS cbRegion, RTGCPHYS cbAlignment, PRTGCPHYS pGCPhysStart, PRTGCPHYS pcbRegion);
    HRESULT assignFixedAddress(const char *pszName, RTGCPHYS GCPhysStart, RTGCPHYS cbRegion);
    HRESULT assignRamRegion(const char *pszName, RTGCPHYS cbRam, PRTGCPHYS pGCPhysStart);
    HRESULT assignInterrupts(const char *pszName, uint32_t cInterrupts, uint32_t *piInterruptFirst);
    HRESULT assignSingleInterrupt(const char *pszName, uint32_t *piInterrupt);
    HRESULT queryMmioRegion(PRTGCPHYS pGCPhysMmioStart, PRTGCPHYS pcbMmio);
    HRESULT queryMmio32Region(PRTGCPHYS pGCPhysMmioStart, PRTGCPHYS pcbMmio);
};

#endif /* !MAIN_INCLUDED_ResourceAssignmentManager_h */
