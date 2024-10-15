/* $Id: PDMInline.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM - Internal header file containing the inlined functions.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_PDMInline_h
#define VMM_INCLUDED_SRC_include_PDMInline_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/**
 * Calculates the next IRQ tag.
 *
 * @returns IRQ tag.
 * @param   pVM                 The cross context VM structure.
 * @param   idTracer            The ID of the source device.
 */
DECLINLINE(uint32_t) pdmCalcIrqTag(PVM pVM, uint32_t idTracer)
{
    uint32_t uTag = (pVM->pdm.s.uIrqTag + 1) & 0x3ff; /* {0..1023} */
    if (!uTag)
        uTag++;
    pVM->pdm.s.uIrqTag = uTag |= (idTracer << 16);
    return uTag;
}

#endif /* !VMM_INCLUDED_SRC_include_PDMInline_h */

