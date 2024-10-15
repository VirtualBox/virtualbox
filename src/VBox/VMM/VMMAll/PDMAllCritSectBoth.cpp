/* $Id: PDMAllCritSectBoth.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PDM - Code Common to Both Critical Section Types, All Contexts.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_PDM_CRITSECT
#include "PDMInternal.h"
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/vmm/pdmcritsectrw.h>
#include <VBox/vmm/vmcc.h>
#include <iprt/errcore.h>

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>


#if defined(IN_RING3) /*|| defined(IN_RING0) - not called from ring-0 */
/**
 * Process the critical sections (both types) queued for ring-3 'leave'.
 *
 * @param   pVM           The cross context VM structure.
 * @param   pVCpu         The cross context virtual CPU structure.
 */
VMM_INT_DECL(void) PDMCritSectBothFF(PVMCC pVM, PVMCPUCC pVCpu)
{
    uint32_t i;
    Assert(   pVCpu->pdm.s.cQueuedCritSectLeaves       > 0
           || pVCpu->pdm.s.cQueuedCritSectRwShrdLeaves > 0
           || pVCpu->pdm.s.cQueuedCritSectRwExclLeaves > 0);

    /* Shared leaves. */
    i = pVCpu->pdm.s.cQueuedCritSectRwShrdLeaves;
    pVCpu->pdm.s.cQueuedCritSectRwShrdLeaves = 0;
    while (i-- > 0)
    {
# ifdef IN_RING3
        PPDMCRITSECTRW pCritSectRw = pVCpu->pdm.s.apQueuedCritSectRwShrdLeaves[i];
# else
        PPDMCRITSECTRW pCritSectRw = (PPDMCRITSECTRW)MMHyperR3ToCC(pVCpu->CTX_SUFF(pVM),
                                                                   pVCpu->pdm.s.apQueuedCritSectRwShrdLeaves[i]);
# endif

        pdmCritSectRwLeaveSharedQueued(pVM, pCritSectRw);
        LogIt(RTLOGGRPFLAGS_FLOW, LOG_GROUP_PDM_CRITSECTRW, ("PDMR3CritSectFF: %p (shared)\n", pCritSectRw));
    }

    /* Last, exclusive leaves. */
    i = pVCpu->pdm.s.cQueuedCritSectRwExclLeaves;
    pVCpu->pdm.s.cQueuedCritSectRwExclLeaves = 0;
    while (i-- > 0)
    {
# ifdef IN_RING3
        PPDMCRITSECTRW pCritSectRw = pVCpu->pdm.s.apQueuedCritSectRwExclLeaves[i];
# else
        PPDMCRITSECTRW pCritSectRw = (PPDMCRITSECTRW)MMHyperR3ToCC(pVCpu->CTX_SUFF(pVM),
                                                                   pVCpu->pdm.s.apQueuedCritSectRwExclLeaves[i]);
# endif

        pdmCritSectRwLeaveExclQueued(pVM, pCritSectRw);
        LogIt(RTLOGGRPFLAGS_FLOW, LOG_GROUP_PDM_CRITSECTRW, ("PDMR3CritSectFF: %p (exclusive)\n", pCritSectRw));
    }

    /* Normal leaves. */
    i = pVCpu->pdm.s.cQueuedCritSectLeaves;
    pVCpu->pdm.s.cQueuedCritSectLeaves = 0;
    while (i-- > 0)
    {
# ifdef IN_RING3
        PPDMCRITSECT pCritSect = pVCpu->pdm.s.apQueuedCritSectLeaves[i];
# else
        PPDMCRITSECT pCritSect = (PPDMCRITSECT)MMHyperR3ToCC(pVCpu->CTX_SUFF(pVM), pVCpu->pdm.s.apQueuedCritSectLeaves[i]);
# endif
        Assert(pCritSect->s.Core.NativeThreadOwner == pVCpu->hNativeThread);

        /* Note! We *must* clear the pending-unlock flag here and not depend on
                 PDMCritSectLeave to do it, as the EMT might be sitting on
                 further nestings since it queued the section to be left, and
                 leaving it set would throw subsequent PDMCritSectIsOwner calls.

                 This will happen with the PGM lock if we nip back to ring-3 for
                 more handy pages or similar where the lock is supposed to be
                 held while in ring-3. */
        ASMAtomicAndU32(&pCritSect->s.Core.fFlags, ~PDMCRITSECT_FLAGS_PENDING_UNLOCK);
        PDMCritSectLeave(pVM, pCritSect);
        LogFlow(("PDMR3CritSectFF: %p\n", pCritSect));
    }

    VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_PDM_CRITSECT);
}
#endif /* IN_RING3 || IN_RING0 */

