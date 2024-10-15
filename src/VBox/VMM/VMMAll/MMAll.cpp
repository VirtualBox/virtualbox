/* $Id: MMAll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MM - Memory Manager - Any Context.
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
#define LOG_GROUP LOG_GROUP_MM_HYPER
#include <VBox/vmm/mm.h>
#include <VBox/vmm/vmm.h>
#include "MMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/hm.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/string.h>


/**
 * Gets the string name of a memory tag.
 *
 * @returns name of enmTag.
 * @param   enmTag      The tag.
 */
const char *mmGetTagName(MMTAG enmTag)
{
    switch (enmTag)
    {
        #define TAG2STR(tag) case MM_TAG_##tag: return #tag

        TAG2STR(CFGM);
        TAG2STR(CFGM_BYTES);
        TAG2STR(CFGM_STRING);
        TAG2STR(CFGM_USER);

        TAG2STR(CPUM_CTX);
        TAG2STR(CPUM_CPUID);
        TAG2STR(CPUM_MSRS);

        TAG2STR(CSAM);
        TAG2STR(CSAM_PATCH);

        TAG2STR(DBGF);
        TAG2STR(DBGF_AS);
        TAG2STR(DBGF_FLOWTRACE);
        TAG2STR(DBGF_INFO);
        TAG2STR(DBGF_LINE);
        TAG2STR(DBGF_LINE_DUP);
        TAG2STR(DBGF_MODULE);
        TAG2STR(DBGF_OS);
        TAG2STR(DBGF_REG);
        TAG2STR(DBGF_STACK);
        TAG2STR(DBGF_SYMBOL);
        TAG2STR(DBGF_SYMBOL_DUP);
        TAG2STR(DBGF_TYPE);
        TAG2STR(DBGF_TRACER);

        TAG2STR(EM);

        TAG2STR(IEM);

        TAG2STR(IOM);
        TAG2STR(IOM_STATS);

        TAG2STR(MM);
        TAG2STR(MM_LOOKUP_GUEST);
        TAG2STR(MM_LOOKUP_PHYS);
        TAG2STR(MM_LOOKUP_VIRT);
        TAG2STR(MM_PAGE);

        TAG2STR(PARAV);

        TAG2STR(PATM);
        TAG2STR(PATM_PATCH);

        TAG2STR(PDM);
        TAG2STR(PDM_DEVICE);
        TAG2STR(PDM_DEVICE_DESC);
        TAG2STR(PDM_DEVICE_USER);
        TAG2STR(PDM_DRIVER);
        TAG2STR(PDM_DRIVER_DESC);
        TAG2STR(PDM_DRIVER_USER);
        TAG2STR(PDM_USB);
        TAG2STR(PDM_USB_DESC);
        TAG2STR(PDM_USB_USER);
        TAG2STR(PDM_LUN);
        TAG2STR(PDM_QUEUE);
        TAG2STR(PDM_THREAD);
        TAG2STR(PDM_ASYNC_COMPLETION);
#ifdef VBOX_WITH_NETSHAPER
        TAG2STR(PDM_NET_SHAPER);
#endif /* VBOX_WITH_NETSHAPER */

        TAG2STR(PGM);
        TAG2STR(PGM_CHUNK_MAPPING);
        TAG2STR(PGM_HANDLERS);
        TAG2STR(PGM_HANDLER_TYPES);
        TAG2STR(PGM_MAPPINGS);
        TAG2STR(PGM_PHYS);
        TAG2STR(PGM_POOL);

        TAG2STR(REM);

        TAG2STR(SELM);

        TAG2STR(SSM);

        TAG2STR(STAM);

        TAG2STR(TM);

        TAG2STR(TRPM);

        TAG2STR(VM);
        TAG2STR(VM_REQ);

        TAG2STR(VMM);

        TAG2STR(HM);

        #undef TAG2STR

        default:
        {
            AssertMsgFailed(("Unknown tag %d! forgot to add it to the switch?\n", enmTag));
#ifdef IN_RING3
            static char sz[48];
            RTStrPrintf(sz, sizeof(sz), "%d", enmTag);
            return sz;
#else
            return "unknown tag!";
#endif
        }
    }
}

