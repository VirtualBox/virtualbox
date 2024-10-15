/* $Id: ResourceAssignmentManager.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox bus slots assignment manager
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN
#include "LoggingNew.h"

#include "ResourceAssignmentManager.h"

#include <iprt/asm.h>
#include <iprt/string.h>

#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/com/array.h>

#include <map>
#include <vector>
#include <algorithm>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/


/**
 * Resource assignment manage state data.
 * @internal
 */
struct ResourceAssignmentManager::State
{
    struct MemoryRange
    {
        char          szDevName[32];
        RTGCPHYS      mGCPhysStart;
        RTGCPHYS      mGCPhysEnd;

        MemoryRange(const char *pszName, RTGCPHYS GCPhysStart, RTGCPHYS GCPhysEnd)
        {
            RTStrCopy(this->szDevName, sizeof(szDevName), pszName);
            this->mGCPhysStart = GCPhysStart;
            this->mGCPhysEnd   = GCPhysEnd;
        }

        bool operator==(const MemoryRange &a) const
        {
            return RTStrNCmp(szDevName, a.szDevName, sizeof(szDevName)) == 0;
        }
    };

    typedef std::vector<MemoryRange> AddrRangeList;

    ChipsetType_T    mChipsetType;
    IommuType_T      mIommuType;
    PCVMMR3VTABLE    mpVMM;
    AddrRangeList    mAddrRanges;

    RTGCPHYS         mGCPhysMmioStartOrig;
    RTGCPHYS         mGCPhysMmioStart;
    RTGCPHYS         mGCPhysMmio32StartOrig;
    RTGCPHYS         mGCPhysMmio32Start;
    RTGCPHYS         mcbMmio32;
    RTGCPHYS         mGCPhysRamStart;
    uint32_t         mcInterrupts;
    uint32_t         miInterrupt;

    State()
        : mChipsetType(ChipsetType_Null), mpVMM(NULL)
    {}
    ~State()
    {}

    HRESULT init(PCVMMR3VTABLE pVMM, ChipsetType_T chipsetType, IommuType_T iommuType,
                 RTGCPHYS GCPhysMmioTop, RTGCPHYS GCPhysRamStart,
                 RTGCPHYS GCPhysMmio32Start, RTGCPHYS cbMmio32, uint32_t cInterrupts);

    HRESULT addAddrRange(const char *pszName, RTGCPHYS GCPhysStart, RTGCPHYS GCPhysEnd);
};


HRESULT ResourceAssignmentManager::State::init(PCVMMR3VTABLE pVMM, ChipsetType_T chipsetType, IommuType_T iommuType,
                                               RTGCPHYS GCPhysMmioStart, RTGCPHYS GCPhysRamStart,
                                               RTGCPHYS GCPhysMmio32Start, RTGCPHYS cbMmio32, uint32_t cInterrupts)
{
    mpVMM = pVMM;

    Assert(chipsetType == ChipsetType_ARMv8Virtual);
    Assert(iommuType == IommuType_None); /* For now no IOMMU support on ARMv8. */

    mChipsetType           = chipsetType;
    mIommuType             = iommuType;
    mGCPhysMmioStart       = GCPhysMmioStart;
    mGCPhysMmioStartOrig   = GCPhysMmioStart;
    mGCPhysRamStart        = GCPhysRamStart;
    mGCPhysMmio32Start     = GCPhysMmio32Start;
    mGCPhysMmio32StartOrig = GCPhysMmio32Start;
    mcbMmio32              = cbMmio32;
    mcInterrupts           = cInterrupts;
    miInterrupt            = 0;
    return S_OK;
}

HRESULT ResourceAssignmentManager::State::addAddrRange(const char *pszName, RTGCPHYS GCPhysStart, RTGCPHYS cbRegion)
{
    MemoryRange memRange(pszName, GCPhysStart, GCPhysStart + cbRegion - 1);
    mAddrRanges.push_back(memRange);
    return S_OK;
}

ResourceAssignmentManager::ResourceAssignmentManager()
    : pState(NULL)
{
    pState = new State();
    Assert(pState);
}

ResourceAssignmentManager::~ResourceAssignmentManager()
{
    if (pState)
    {
        delete pState;
        pState = NULL;
    }
}

ResourceAssignmentManager *ResourceAssignmentManager::createInstance(PCVMMR3VTABLE pVMM, ChipsetType_T chipsetType, IommuType_T iommuType,
                                                                     RTGCPHYS GCPhysMmioTop, RTGCPHYS GCPhysRamStart,
                                                                     RTGCPHYS GCPhysMmio32Start, RTGCPHYS cbMmio32,
                                                                     uint32_t cInterrupts)
{
    ResourceAssignmentManager *pInstance = new ResourceAssignmentManager();
    pInstance->pState->init(pVMM, chipsetType, iommuType, GCPhysMmioTop, GCPhysRamStart, GCPhysMmio32Start, cbMmio32, cInterrupts);
    Assert(pInstance);
    return pInstance;
}

HRESULT ResourceAssignmentManager::assignMmioRegion(const char *pszName, RTGCPHYS cbRegion, PRTGCPHYS pGCPhysStart, PRTGCPHYS pcbRegion)
{
    RTGCPHYS cbRegionAligned = RT_ALIGN_T(cbRegion, _4K, RTGCPHYS);
    RTGCPHYS GCPhysMmioStart = pState->mGCPhysMmioStart;

    *pGCPhysStart = GCPhysMmioStart;
    *pcbRegion    = cbRegionAligned;
    pState->mGCPhysMmioStart += cbRegionAligned;
    pState->addAddrRange(pszName, GCPhysMmioStart, cbRegionAligned);
    return S_OK;
}

HRESULT ResourceAssignmentManager::assignMmio32Region(const char *pszName, RTGCPHYS cbRegion, PRTGCPHYS pGCPhysStart, PRTGCPHYS pcbRegion)
{
    RTGCPHYS cbRegionAligned = RT_ALIGN_T(cbRegion, _4K, RTGCPHYS);
    RTGCPHYS GCPhysMmioStart = pState->mGCPhysMmio32Start;

    if (GCPhysMmioStart > pState->mGCPhysRamStart)
    {
        AssertLogRelMsgFailed(("ResourceAssignmentManager: MMIO32 range for %s would overlap RAM region\n", pszName));
        return E_INVALIDARG;
    }

    *pGCPhysStart = GCPhysMmioStart;
    *pcbRegion    = cbRegionAligned;
    pState->mGCPhysMmio32Start += cbRegionAligned;
    pState->addAddrRange(pszName, GCPhysMmioStart, cbRegionAligned);
    return S_OK;
}

HRESULT ResourceAssignmentManager::assignMmioRegionAligned(const char *pszName, RTGCPHYS cbRegion, RTGCPHYS cbAlignment, PRTGCPHYS pGCPhysStart, PRTGCPHYS pcbRegion)
{
    RTGCPHYS cbRegionAligned = RT_ALIGN_T(cbRegion, cbAlignment, RTGCPHYS);
    RTGCPHYS GCPhysMmioStart = pState->mGCPhysMmioStart;

    GCPhysMmioStart = RT_ALIGN_T(GCPhysMmioStart, cbAlignment, RTGCPHYS);

    *pGCPhysStart = GCPhysMmioStart;
    *pcbRegion    = cbRegionAligned;
    pState->mGCPhysMmioStart = GCPhysMmioStart + cbRegionAligned;
    pState->addAddrRange(pszName, GCPhysMmioStart, cbRegionAligned);
    return S_OK;
}

HRESULT ResourceAssignmentManager::assignFixedAddress(const char *pszName, RTGCPHYS GCPhysStart, RTGCPHYS cbRegion)
{
    RT_NOREF(pszName, GCPhysStart, cbRegion);
    AssertReleaseFailed();
    return S_OK;
}

HRESULT ResourceAssignmentManager::assignRamRegion(const char *pszName, RTGCPHYS cbRam, PRTGCPHYS pGCPhysStart)
{
    *pGCPhysStart = pState->mGCPhysRamStart;
    pState->mGCPhysRamStart += cbRam;
    pState->addAddrRange(pszName, *pGCPhysStart, cbRam);
    return S_OK;
}

HRESULT ResourceAssignmentManager::assignInterrupts(const char *pszName, uint32_t cInterrupts, uint32_t *piInterruptFirst)
{
    if (pState->miInterrupt + cInterrupts > pState->mcInterrupts)
    {
        AssertLogRelMsgFailed(("ResourceAssignmentManager: Interrupt count for %s exceeds number of available interrupts\n", pszName));
        return E_INVALIDARG;
    }

    *piInterruptFirst = pState->miInterrupt;
    pState->miInterrupt += cInterrupts;
    return S_OK;
}

HRESULT ResourceAssignmentManager::assignSingleInterrupt(const char *pszName, uint32_t *piInterrupt)
{
    return assignInterrupts(pszName, 1, piInterrupt);
}

HRESULT ResourceAssignmentManager::queryMmioRegion(PRTGCPHYS pGCPhysMmioStart, PRTGCPHYS pcbMmio)
{
    *pGCPhysMmioStart = pState->mGCPhysMmioStartOrig;
    *pcbMmio          = pState->mGCPhysMmioStart - pState->mGCPhysMmioStartOrig;
    return S_OK;
}

HRESULT ResourceAssignmentManager::queryMmio32Region(PRTGCPHYS pGCPhysMmioStart, PRTGCPHYS pcbMmio)
{
    *pGCPhysMmioStart = pState->mGCPhysMmio32StartOrig;
    *pcbMmio          = pState->mcbMmio32;
    return S_OK;
}
