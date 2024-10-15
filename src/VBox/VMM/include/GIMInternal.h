/* $Id: GIMInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GIM - Internal header file.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_GIMInternal_h
#define VMM_INCLUDED_SRC_include_GIMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/gim.h>
#include <VBox/vmm/pgm.h>
#include "GIMHvInternal.h"
#include "GIMKvmInternal.h"
#include "GIMMinimalInternal.h"

RT_C_DECLS_BEGIN

/** @defgroup grp_gim_int       Internal
 * @ingroup grp_gim
 * @internal
 * @{
 */

/** The saved state version. */
#define GIM_SAVED_STATE_VERSION         1

/**
 * GIM VM Instance data.
 */
typedef struct GIM
{
    /** The provider that is active for this VM. */
    GIMPROVIDERID                    enmProviderId;
    /** The interface implementation version. */
    uint32_t                         u32Version;

    /** Physical access handler type for semi-read-only MMIO2 memory. Lazy creation. */
    PGMPHYSHANDLERTYPE              hSemiReadOnlyMmio2Handler;

    /** Pointer to the GIM device - R3 ptr. */
    R3PTRTYPE(PPDMDEVINS)            pDevInsR3;
    /** The debug struct - R3 ptr. */
    R3PTRTYPE(PGIMDEBUG)             pDbgR3;

    /** The provider specific data. */
    union
    {
        GIMHV  Hv;
        GIMKVM Kvm;
    } u;

    /** Number of hypercalls initiated. */
    STAMCOUNTER                      StatHypercalls;
    /** Debug packets sent. */
    STAMCOUNTER                      StatDbgXmit;
    /** Debug bytes sent. */
    STAMCOUNTER                      StatDbgXmitBytes;
    /** Debug packets received. */
    STAMCOUNTER                      StatDbgRecv;
    /** Debug bytes received. */
    STAMCOUNTER                      StatDbgRecvBytes;
} GIM;
/** Pointer to GIM VM instance data. */
typedef GIM *PGIM;

/**
 * GIM VMCPU Instance data.
 */
typedef struct GIMCPU
{
    union
    {
        GIMKVMCPU KvmCpu;
        GIMHVCPU  HvCpu;
    } u;
} GIMCPU;
/** Pointer to GIM VMCPU instance data. */
typedef GIMCPU *PGIMCPU;

/**
 * Callback when a debug buffer read has completed and before signalling the next
 * read.
 *
 * @param   pVM             The cross context VM structure.
 */
typedef DECLCALLBACKTYPE(void, FNGIMDEBUGBUFREADCOMPLETED,(PVM pVM));
/** Pointer to GIM debug buffer read completion callback. */
typedef FNGIMDEBUGBUFREADCOMPLETED *PFNGIMDEBUGBUFREADCOMPLETED;

#ifdef IN_RING3
#if 0
VMMR3_INT_DECL(int)           gimR3Mmio2Unmap(PVM pVM, PGIMMMIO2REGION pRegion);
VMMR3_INT_DECL(int)           gimR3Mmio2Map(PVM pVM, PGIMMMIO2REGION pRegion, RTGCPHYS GCPhysRegion);
VMMR3_INT_DECL(int)           gimR3Mmio2HandlerPhysicalRegister(PVM pVM, PGIMMMIO2REGION pRegion);
VMMR3_INT_DECL(int)           gimR3Mmio2HandlerPhysicalDeregister(PVM pVM, PGIMMMIO2REGION pRegion);
#endif

VMMR3_INT_DECL(int)           gimR3DebugRead(PVM pVM, void *pvRead, size_t *pcbRead, PFNGIMDEBUGBUFREADCOMPLETED pfnReadComplete);
VMMR3_INT_DECL(int)           gimR3DebugWrite(PVM pVM, void *pvWrite, size_t *pcbWrite);
#endif /* IN_RING3 */

/** @} */

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_GIMInternal_h */

