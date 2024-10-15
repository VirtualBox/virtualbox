/* $Id: bs3-mode-TestDoModes.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Common header for the Bs3TestDoModes family.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef BS3KIT_INCLUDED_bs3_mode_TestDoModes_h
#define BS3KIT_INCLUDED_bs3_mode_TestDoModes_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "bs3kit.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def CONV_TO_FLAT
 * Get flat address.  In 16-bit the parameter is a real mode far address, while
 * in 32-bit and 64-bit modes it is already flat.
 */
/** @def CONV_TO_PROT_FAR16
 * Get a 32-bit value that makes a protected mode far 16:16 address.
 */
/** @def CONV_TO_RM_FAR16
 * Get a 32-bit value that makes a real mode far 16:16 address.  In 16-bit mode
 * this is already what we've got, except must be converted to uint32_t.
 */
#if ARCH_BITS == 16
# define CONV_TO_FLAT(a_fpfn)           (((uint32_t)BS3_FP_SEG(a_fpfn) << 4) + BS3_FP_OFF(a_fpfn))
# define CONV_TO_PROT_FAR16(a_fpfn)     RT_MAKE_U32(BS3_FP_OFF(a_fpfn), Bs3SelRealModeCodeToProtMode(BS3_FP_SEG(a_fpfn)))
# define CONV_TO_RM_FAR16(a_fpfn)       RT_MAKE_U32(BS3_FP_OFF(a_fpfn), BS3_FP_SEG(a_fpfn))
#else
# define CONV_TO_FLAT(a_fpfn)           ((uint32_t)(uintptr_t)(a_fpfn))
# define CONV_TO_PROT_FAR16(a_fpfn)     Bs3SelFlatCodeToProtFar16((uint32_t)(uintptr_t)(a_fpfn))
# define CONV_TO_RM_FAR16(a_fpfn)       Bs3SelFlatCodeToRealMode( (uint32_t)(uintptr_t)(a_fpfn))
#endif


/*********************************************************************************************************************************
*   Assembly Symbols                                                                                                             *
*********************************************************************************************************************************/
/* These are in the same code segment as the main API, so no FAR necessary. */
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInRM)(uint32_t uCallbackFarPtr);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPE16)(uint32_t uCallbackFarPtr);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPE16_32)(uint32_t uFlatAddrCallback, uint8_t bMode);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPE16_V86)(uint32_t uCallbackFarPtr);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPE32)(uint32_t uFlatAddrCallback, uint8_t bMode);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPE32_16)(uint32_t uCallbackFarPtr);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPEV86)(uint32_t uCallbackFarPtr);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPP16)(uint32_t uCallbackFarPtr);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPP16_32)(uint32_t uFlatAddrCallback, uint8_t bMode);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPP16_V86)(uint32_t uCallbackFarPtr);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPP32)(uint32_t uFlatAddrCallback, uint8_t bMode);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPP32_16)(uint32_t uCallbackFarPtr);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPPV86)(uint32_t uCallbackFarPtr);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPAE16)(uint32_t uCallbackFarPtr);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPAE16_32)(uint32_t uFlatAddrCallback, uint8_t bMode);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPAE16_V86)(uint32_t uCallbackFarPtr);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPAE32)(uint32_t uFlatAddrCallback, uint8_t bMode);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPAE32_16)(uint32_t uCallbackFarPtr);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInPAEV86)(uint32_t uCallbackFarPtr);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInLM16)(uint32_t uCallbackFarPtr);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInLM32)(uint32_t uFlatAddrCallback);
BS3_DECL_NEAR(uint8_t) TMPL_NM(Bs3TestCallDoerInLM64)(uint32_t uFlatAddrCallback, uint8_t bMode);

#endif /* !BS3KIT_INCLUDED_bs3_mode_TestDoModes_h */

