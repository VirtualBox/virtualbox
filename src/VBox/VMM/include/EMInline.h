/* $Id: EMInline.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * EM - Internal header file, inline functions.
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

#ifndef VMM_INCLUDED_SRC_include_EMInline_h
#define VMM_INCLUDED_SRC_include_EMInline_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/** @addtogroup grp_em_int       Internal
 * @internal
 * @{
 */


/**
 * Check if the preset execution time cap restricts guest execution scheduling.
 *
 * @returns true if allowed, false otherwise
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
DECLINLINE(bool) emR3IsExecutionAllowed(PVM pVM, PVMCPU pVCpu)
{
    return RT_LIKELY(pVM->uCpuExecutionCap == 100)  /* no cap, likely */
        || emR3IsExecutionAllowedSlow(pVM, pVCpu);
}


/** @} */

#endif /* !VMM_INCLUDED_SRC_include_EMInline_h */

