/* $Id: CPUProfileImpl.cpp 109259 2025-04-16 20:59:36Z knut.osmundsen@oracle.com $ */
/** @file
 * VirtualBox Main - interface for CPU profiles, VBoxSVC.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#include "CPUProfileImpl.h"

#include <VBox/vmm/cpum.h>
#include <iprt/x86.h>
#include "AutoCaller.h"


DEFINE_EMPTY_CTOR_DTOR(CPUProfile)

/**
 * Called by ComObjPtr::createObject when creating the object.
 *
 * Just initialize the basic object state, do the rest in initFromDbEntry().
 *
 * @returns S_OK.
 */
HRESULT CPUProfile::FinalConstruct()
{
    m_enmArchitecture = CPUArchitecture_Any;
    return BaseFinalConstruct();
}

/**
 * Initializes the CPU profile from the given CPUM CPU database entry.
 *
 * @returns COM status code.
 * @param   a_pDbEntry      The CPU database entry.
 */
HRESULT CPUProfile::initFromDbEntry(PCCPUMDBENTRY a_pDbEntry) RT_NOEXCEPT
{
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /*
     * Initialize our private data.
     */

    /* Determin x86 or AMD64 by scanning the CPUID leaves for the long mode feature bit: */
    if (a_pDbEntry->enmEntryType == CPUMDBENTRYTYPE_X86)
    {
        m_enmArchitecture = CPUArchitecture_x86;
        PCCPUMDBENTRYX86 const pDbEntryX86 = (PCCPUMDBENTRYX86)a_pDbEntry;
        uint32_t iLeaf = pDbEntryX86->cCpuIdLeaves;
        while (iLeaf-- > 0)
            if (pDbEntryX86->paCpuIdLeaves[iLeaf].uLeaf <= UINT32_C(0x80000001))
            {
                if (   pDbEntryX86->paCpuIdLeaves[iLeaf].uLeaf == UINT32_C(0x80000001)
                    && (pDbEntryX86->paCpuIdLeaves[iLeaf].uEdx & X86_CPUID_EXT_FEATURE_EDX_LONG_MODE))
                    m_enmArchitecture = CPUArchitecture_AMD64;
                break;
            }
    }
    else if (a_pDbEntry->enmEntryType == CPUMDBENTRYTYPE_ARM)
        m_enmArchitecture = CPUArchitecture_ARMv8_64;

    HRESULT hrc = m_strName.assignEx(a_pDbEntry->pszName);
    if (SUCCEEDED(hrc))
        hrc = m_strFullName.assignEx(a_pDbEntry->pszFullName);

    /*
     * Update the object state.
     */
    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();
    else
        autoInitSpan.setFailed(hrc);
    return hrc;
}

/**
 * COM cruft.
 */
void CPUProfile::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

/**
 * Do the actual cleanup.
 */
void CPUProfile::uninit()
{
    AutoUninitSpan autoUninitSpan(this);
}

/**
 * Used by SystemProperties::getCPUProfiles to do matching.
 */
bool CPUProfile::i_match(CPUArchitecture_T a_enmArchitecture, CPUArchitecture_T a_enmSecondaryArch,
                         const com::Utf8Str &a_strNamePattern) const RT_NOEXCEPT
{
    if (   m_enmArchitecture == a_enmArchitecture
        || m_enmArchitecture == a_enmSecondaryArch
        || a_enmArchitecture == CPUArchitecture_Any)
    {
        if (a_strNamePattern.isEmpty())
            return true;
        return RTStrSimplePatternMatch(a_strNamePattern.c_str(), m_strName.c_str());
    }
    return false;
}


HRESULT CPUProfile::getName(com::Utf8Str &aName)
{
    return aName.assignEx(m_strName);
}

HRESULT CPUProfile::getFullName(com::Utf8Str &aFullName)
{
    return aFullName.assignEx(m_strFullName);
}

HRESULT CPUProfile::getArchitecture(CPUArchitecture_T *aArchitecture)
{
    *aArchitecture = m_enmArchitecture;
    return S_OK;
}


/* vi: set tabstop=4 shiftwidth=4 expandtab: */
