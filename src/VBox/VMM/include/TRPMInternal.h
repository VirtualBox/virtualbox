/* $Id: TRPMInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * TRPM - Internal header file.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_TRPMInternal_h
#define VMM_INCLUDED_SRC_include_TRPMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/pgm.h>

RT_C_DECLS_BEGIN


/** @defgroup grp_trpm_int   Internals
 * @ingroup grp_trpm
 * @internal
 * @{
 */

/**
 * TRPM Data (part of VM)
 *
 * @note This used to be a big deal when we had raw-mode, now it's a dud. :-)
 */
typedef struct TRPM
{
    /** Statistics for interrupt handlers. */
    STAMCOUNTER             aStatForwardedIRQ[256];
} TRPM;

/** Pointer to TRPM Data. */
typedef TRPM *PTRPM;


/**
 * Per CPU data for TRPM.
 */
typedef struct TRPMCPU
{
    /** Active Interrupt or trap vector number.
     * If not UINT32_MAX this indicates that we're currently processing a
     * interrupt, trap, fault, abort, whatever which have arrived at that
     * vector number.
     */
    uint32_t                uActiveVector;

    /** Active trap type. */
    TRPMEVENT               enmActiveType;

    /** Errorcode for the active interrupt/trap. */
    uint32_t                uActiveErrorCode;

    /** Instruction length for software interrupts and software exceptions
     * (\#BP, \#OF) */
    uint8_t                 cbInstr;

    /** Whether this \#DB trap is caused due to INT1/ICEBP. */
    bool                    fIcebp;

    /** CR2 at the time of the active exception. */
    RTGCUINTPTR             uActiveCR2;
} TRPMCPU;

/** Pointer to TRPMCPU Data. */
typedef TRPMCPU *PTRPMCPU;
/** Pointer to const TRPMCPU Data. */
typedef const TRPMCPU *PCTRPMCPU;

/** @} */

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_TRPMInternal_h */
