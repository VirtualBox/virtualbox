/* $Id: SELMInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SELM - Internal header file.
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

#ifndef VMM_INCLUDED_SRC_include_SELMInternal_h
#define VMM_INCLUDED_SRC_include_SELMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/pgm.h>
#include <VBox/log.h>
#include <iprt/x86.h>



/** @defgroup grp_selm_int   Internals
 * @ingroup grp_selm
 * @internal
 * @{
 */

/** The number of GDTS allocated for our GDT. (full size) */
#define SELM_GDT_ELEMENTS                   8192


/**
 * SELM Data (part of VM)
 *
 * @note This is a very marginal component after kicking raw-mode.
 */
typedef struct SELM
{
#ifdef VBOX_WITH_STATISTICS
    STAMCOUNTER             StatLoadHidSelGst;
    STAMCOUNTER             StatLoadHidSelShw;
#endif
    STAMCOUNTER             StatLoadHidSelReadErrors;
    STAMCOUNTER             StatLoadHidSelGstNoGood;
} SELM, *PSELM;


/** @} */

#endif /* !VMM_INCLUDED_SRC_include_SELMInternal_h */
