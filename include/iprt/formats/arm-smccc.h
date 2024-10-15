/* $Id: arm-smccc.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, ARM SMC Calling Convention common definitions (this is actually a protocol and not a format).
 *
 * from https://developer.arm.com/documentation/den0028/latest (2023-06-05)
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

#ifndef IPRT_INCLUDED_formats_arm_smccc_h
#define IPRT_INCLUDED_formats_arm_smccc_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/assertcompile.h>

/** @name Function identifier definitions.
 * @{ */
/** Identifies a fast call. */
#define ARM_SMCCC_FUNC_ID_FAST_CALL                 RT_BIT_32(31)
/** Set when the SMC64/HVC64 calling convention is used, clear for SMC32/HVC32. */
#define ARM_SMCCC_FUNC_ID_64BIT                     RT_BIT_32(30)
/** The service call range mask. */
#define ARM_SMCCC_FUNC_ID_ENTITY_MASK               (  RT_BIT_32(24) | RT_BIT_32(25) | RT_BIT_32(26) \
                                                     | RT_BIT_32(27) | RT_BIT_32(28) | RT_BIT_32(29))
#define ARM_SMCCC_FUNC_ID_ENTITY_SHIFT              24
#define ARM_SMCCC_FUNC_ID_ENTITY_GET(a_FunId)       (((a_FunId) & ARM_SMCCC_FUNC_ID_ENTITY_MASK) >> ARM_SMCCC_FUNC_ID_ENTITY_SHIFT)
#define ARM_SMCCC_FUNC_ID_ENTITY_SET(a_EntityId)    (((a_EntityId) << ARM_SMCCC_FUNC_ID_ENTITY_SHIFT) & ARM_SMCCC_FUNC_ID_ENTITY_MASK)
/** Hint that there is no SVE specific live state which needs saving. */
#define ARM_SMCCC_FUNC_ID_SVE_STATE_ABSENT          RT_BIT_32(16)
/** Function number mask. */
#define ARM_SMCCC_FUNC_ID_NUM_MASK                  UINT32_C(0x0000ffff)
#define ARM_SMCCC_FUNC_ID_NUM_SHIFT                 0
#define ARM_SMCCC_FUNC_ID_NUM_GET(a_FunId)          (((a_FunId) & ARM_SMCCC_FUNC_ID_NUM_MASK) >> ARM_SMCCC_FUNC_ID_NUM_SHIFT)
#define ARM_SMCCC_FUNC_ID_NUM_SET(a_FunNum)         ((a_FunNum) & ARM_SMCCC_FUNC_ID_NUM_MASK)


/** @name Owning entity IDs.
 * @{ */
/** Arm Architecture Calls. */
# define ARM_SMCCC_FUNC_ID_ENTITY_ARM_ARCH          0
/** CPU Service Calls. */
# define ARM_SMCCC_FUNC_ID_ENTITY_CPU_SERVICE       1
/** SiP Service Calls. */
# define ARM_SMCCC_FUNC_ID_ENTITY_SIP_SERVICE       2
/** OEM Service Calls. */
# define ARM_SMCCC_FUNC_ID_ENTITY_OEM_SERVICE       3
/** Standard Secure Service Calls. */
# define ARM_SMCCC_FUNC_ID_ENTITY_STD_SEC_SERVICE   4
/** Standard Hypervisor Service Calls. */
# define ARM_SMCCC_FUNC_ID_ENTITY_STD_HYP_SERVICE   5
/** Vendor Specific Hypervisor Service Calls. */
# define ARM_SMCCC_FUNC_ID_ENTITY_VEN_HYP_SERVICE   6
/** Trusted Application Calls, start. */
# define ARM_SMCCC_FUNC_ID_ENTITY_TRUST_APP_FIRST   48
/** Trusted Application Calls, last (inclusive). */
# define ARM_SMCCC_FUNC_ID_ENTITY_TRUST_APP_LAST    49
/** Trusted OS Calls, start. */
# define ARM_SMCCC_FUNC_ID_ENTITY_TRUST_OS_FIRST    50
/** Trusted OS Calls, last (inclusive). */
# define ARM_SMCCC_FUNC_ID_ENTITY_TRUST_OS_LAST     63
/** @} */


/** Helper to define a SMCCC function identifier conforming to SMC32/HVC32. */
#define ARM_SMCCC_FUNC_ID_CREATE_FAST_32(a_Entity, a_FunNum)    (ARM_SMCCC_FUNC_ID_FAST_CALL | ARM_SMCCC_FUNC_ID_ENTITY_SET(a_Entity) | ARM_SMCCC_FUNC_ID_NUM_SET(a_FunNum))
/** Helper to define a SMCCC function identifier conforming to SMC64/HVC64. */
#define ARM_SMCCC_FUNC_ID_CREATE_FAST_64(a_Entity, a_FunNum)    (  ARM_SMCCC_FUNC_ID_FAST_CALL | ARM_SMCCC_FUNC_ID_64BIT \
                                                                 | ARM_SMCCC_FUNC_ID_ENTITY_SET(a_Entity) | ARM_SMCCC_FUNC_ID_NUM_SET(a_FunNum))

/** @} */

#endif /* !IPRT_INCLUDED_formats_arm_smccc_h */
