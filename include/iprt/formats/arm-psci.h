/* $Id: arm-psci.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, ARM PSCI (Power State Coordination Interface) common definitions (this is actually a protocol and not a format).
 *
 * from https://developer.arm.com/documentation/den0022/e/?lang=en (2023-06-05)
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

#ifndef IPRT_INCLUDED_formats_arm_psci_h
#define IPRT_INCLUDED_formats_arm_psci_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/assertcompile.h>
#include <iprt/formats/arm-smccc.h>

/** @name Return error codes.
 * @{ */
/** Success. */
#define ARM_PSCI_STS_SUCCESS                        INT32_C(0)
/** Operation is not supported. */
#define ARM_PSCI_STS_NOT_SUPPORTED                  INT32_C(-1)
/** Caller supplied invalid parameters. */
#define ARM_PSCI_STS_INVALID_PARAMETERS             INT32_C(-2)
/** Access to the operation was denied. */
#define ARM_PSCI_STS_DENIED                         INT32_C(-3)
/** CPU is already online. */
#define ARM_PSCI_STS_ALREADY_ON                     INT32_C(-4)
/** Operation is already pending. */
#define ARM_PSCI_STS_ON_PENDING                     INT32_C(-5)
/** PSCI implementation encountered an internal failure for the operation. */
#define ARM_PSCI_STS_INTERNAL_FAILURE               INT32_C(-6)
/** Target CPU is not present. */
#define ARM_PSCI_STS_NOT_PRESENT                    INT32_C(-7)
/** Target CPU is disabled. */
#define ARM_PSCI_STS_DISABLED                       INT32_C(-8)
/** An invalid address was supplied. */
#define ARM_PSCI_STS_INVALID_ADDRESS                INT32_C(-9)
/** @} */


/** @name 32-bit Function identifiers.
 * @{ */
/** Return the version of the PSCI implemented. */
#define ARM_PSCI_FUNC_ID_PSCI_VERSION               0
# define ARM_PSCI_FUNC_ID_PSCI_VERSION_SET(a_Major, a_Minor)    (((a_Major) << 16) | ((a_Minor)))
/** Suspends execution on a core or topology node. */
#define ARM_PSCI_FUNC_ID_CPU_SUSPEND                1
/** Powers down the calling core. */
#define ARM_PSCI_FUNC_ID_CPU_OFF                    2
/** Power up a core. */
#define ARM_PSCI_FUNC_ID_CPU_ON                     3
/** Enable the caller to request status of an affinity instance. */
#define ARM_PSCI_FUNC_ID_AFFINITY_INFO              4
/** Asks a uniprocessor trusted OS to migrate its context to a specific core. */
#define ARM_PSCI_FUNC_ID_MIGRATE                    5
/** Allows caller to identify the level of multicore support present in the trusted OS. */
#define ARM_PSCI_FUNC_ID_MIGRATE_INFO_TYPE          6
/** Returns the current resident core for a uniprocessor trusted OS. */
#define ARM_PSCI_FUNC_ID_MIGRATE_INFO_UP_CPU        7
/** Shut down the system. */
#define ARM_PSCI_FUNC_ID_SYSTEM_OFF                 8
/** Reset the system. */
#define ARM_PSCI_FUNC_ID_SYSTEM_RESET               9
/** Query API allowing to discover whether SMCCC_VERSION or a specific PSCI function is implemented. */
#define ARM_PSCI_FUNC_ID_PSCI_FEATURES             10
/** Places the core into an implementation defined low-power state. */
#define ARM_PSCI_FUNC_ID_CPU_FREEZE                11
/** Places the core into an implementation defined low-power state. */
#define ARM_PSCI_FUNC_ID_CPU_DEFAULT_SUSPEND       12
/** Returns HW state of a node in the power domain topology of the system. */
#define ARM_PSCI_FUNC_ID_NODE_HW_STATE             13
/** Used to implement suspend to RAM. */
#define ARM_PSCI_FUNC_ID_SYSTEM_SUSPEND            14
/** Sets the mode used by CPU_SUSPEND to coordinate power states. */
#define ARM_PSCI_FUNC_ID_SET_SUSPEND_MODE          15
/** Returns the amount of time the platform has spent in the given power state since cold boot. */
#define ARM_PSCI_FUNC_ID_STAT_RESIDENCY            16
/** Returns the number of times the platform has used the given power state since cold boot. */
#define ARM_PSCI_FUNC_ID_STAT_COUNT                17
/** Reset the system, extended version present in PSCI 1.1. */
#define ARM_PSCI_FUNC_ID_SYSTEM_RESET2             18
/** Provide protection against cold reboot attacks, present in PSCI 1.1. */
#define ARM_PSCI_FUNC_ID_MEM_PROTECT               19
/** Checks whether a given memory range is protected by MEM_PROTECT, present in PSCI 1.1. */
#define ARM_PSCI_FUNC_ID_MEM_PROTECT_CHECK_RANGE   20
/** @} */


#define ARM_PSCI_MIGRATE_INFO_TYPE_TOS_NOT_PRESENT  2

/** Helper to define a PSCI function identifier conforming to SMC32/HVC32. */
#define ARM_PSCI_FUNC_ID_CREATE_FAST_32(a_FunNum)  ARM_SMCCC_FUNC_ID_CREATE_FAST_32(ARM_SMCCC_FUNC_ID_ENTITY_STD_SEC_SERVICE, a_FunNum)
/** Helper to define a PSCI function identifier conforming to SMC64/HVC64. */
#define ARM_PSCI_FUNC_ID_CREATE_FAST_64(a_FunNum)  ARM_SMCCC_FUNC_ID_CREATE_FAST_64(ARM_SMCCC_FUNC_ID_ENTITY_STD_SEC_SERVICE, a_FunNum)

#endif /* !IPRT_INCLUDED_formats_arm_psci_h */

