/* $Id: SVMInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * SVM - Internal header file for the SVM code.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_SVMInternal_h
#define VMM_INCLUDED_SRC_include_SVMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** @name SVM transient.
 *
 * A state structure for holding miscellaneous information across AMD-V
 * VMRUN/\#VMEXIT operation, restored after the transition.
 *
 * @{ */
typedef struct SVMTRANSIENT
{
    /** The host's rflags/eflags. */
    RTCCUINTREG     fEFlags;
    /** The \#VMEXIT exit code (the EXITCODE field in the VMCB). */
    uint64_t        u64ExitCode;

    /** The guest's TPR value used for TPR shadowing. */
    uint8_t         u8GuestTpr;
    /** Alignment. */
    uint8_t         abAlignment0[7];

    /** Pointer to the currently executing VMCB. */
    PSVMVMCB        pVmcb;

    /** Whether we are currently executing a nested-guest. */
    bool            fIsNestedGuest;
    /** Whether the guest debug state was active at the time of \#VMEXIT. */
    bool            fWasGuestDebugStateActive;
    /** Whether the hyper debug state was active at the time of \#VMEXIT. */
    bool            fWasHyperDebugStateActive;
    /** Whether the TSC offset mode needs to be updated. */
    bool            fUpdateTscOffsetting;
    /** Whether the TSC_AUX MSR needs restoring on \#VMEXIT. */
    bool            fRestoreTscAuxMsr;
    /** Whether the \#VMEXIT was caused by a page-fault during delivery of a
     *  contributary exception or a page-fault. */
    bool            fVectoringDoublePF;
    /** Whether the \#VMEXIT was caused by a page-fault during delivery of an
     *  external interrupt or NMI. */
    bool            fVectoringPF;
    /** Padding. */
    bool            afPadding0;
} SVMTRANSIENT;
/** Pointer to SVM transient state. */
typedef SVMTRANSIENT *PSVMTRANSIENT;
/** Pointer to a const SVM transient state. */
typedef const SVMTRANSIENT *PCSVMTRANSIENT;

AssertCompileSizeAlignment(SVMTRANSIENT, sizeof(uint64_t));
AssertCompileMemberAlignment(SVMTRANSIENT, u64ExitCode, sizeof(uint64_t));
AssertCompileMemberAlignment(SVMTRANSIENT, pVmcb,       sizeof(uint64_t));
/** @}  */

RT_C_DECLS_BEGIN
/* Nothing for now. */
RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_SVMInternal_h */

