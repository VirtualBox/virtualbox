/* $Id: CPUProfileImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef MAIN_INCLUDED_CPUProfileImpl_h
#define MAIN_INCLUDED_CPUProfileImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "CPUProfileWrap.h"

struct CPUMDBENTRY;

/**
 * A CPU profile.
 */
class ATL_NO_VTABLE CPUProfile
    : public CPUProfileWrap
{
public:
    /** @name COM and internal init/term/mapping cruft
     * @{ */
    DECLARE_COMMON_CLASS_METHODS(CPUProfile)
    HRESULT FinalConstruct();
    void    FinalRelease();
    HRESULT initFromDbEntry(struct CPUMDBENTRY const *a_pDbEntry) RT_NOEXCEPT;
    void    uninit() RT_OVERRIDE;
    /** @} */

    bool    i_match(CPUArchitecture_T a_enmArchitecture, CPUArchitecture_T a_enmSecondaryArch,
                    const com::Utf8Str &a_strNamePattern) const RT_NOEXCEPT;

private:
    /** @name Wrapped ICPUProfile attributes
     * @{ */
    HRESULT getName(com::Utf8Str &aName) RT_OVERRIDE;
    HRESULT getFullName(com::Utf8Str &aFullName) RT_OVERRIDE;
    HRESULT getArchitecture(CPUArchitecture_T *aArchitecture) RT_OVERRIDE;
    /** @} */

    /** @name Data
     * @{ */
    com::Utf8Str        m_strName;
    com::Utf8Str        m_strFullName;
    CPUArchitecture_T   m_enmArchitecture;
    /** @} */
};

#endif /* !MAIN_INCLUDED_CPUProfileImpl_h */

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
