/* $Id: VMMR3VTable.cpp 109447 2025-05-06 21:12:42Z knut.osmundsen@oracle.com $ */
/** @file
 * VM - The Virtual Machine Monitor, Ring-3 API VTable Definitions.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define RT_RELAXED_CALLBACKS_TYPES
#define LOG_GROUP LOG_GROUP_VMM
#include <VBox/vmm/vmmr3vtable.h>

#include <iprt/asm.h>
#include <iprt/errcore.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) vmmR3ReservedVTableEntry(void);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const VMMR3VTABLE g_VMMR3VTable =
{
    /* .uMagicVersion = */      VMMR3VTABLE_MAGIC_VERSION,
    /* .fFlags = */             0,
    /* .pszDescription = */     "x86 & amd64",

#define VTABLE_ENTRY(a_Api)     a_Api,
#define VTABLE_RESERVED(a_Name) vmmR3ReservedVTableEntry,

#include <VBox/vmm/vmmr3vtable-def.h>

#undef VTABLE_ENTRY
#undef VTABLE_RESERVED

    /* .uMagicVersionEnd = */   VMMR3VTABLE_MAGIC_VERSION,
};


/**
 * Reserved VMM function table entry.
 */
static DECLCALLBACK(int) vmmR3ReservedVTableEntry(void)
{
    void * volatile pvCaller = ASMReturnAddress();
    AssertLogRelMsgFailed(("Reserved VMM function table entry called from %p!\n", pvCaller ));
    return VERR_INTERNAL_ERROR;
}


#ifndef VBOX_WITH_DEBUGGER
/** Stub */
DBGDECL(int) DBGCCreate(PUVM pUVM, PCDBGCIO pIo, unsigned fFlags)
{
    RT_NOREF(pUVM, pIo, fFlags);
    return VERR_NOT_IMPLEMENTED;
}
#endif


VMMR3DECL(PCVMMR3VTABLE) VMMR3GetVTable(void)
{
    return &g_VMMR3VTable;
}

