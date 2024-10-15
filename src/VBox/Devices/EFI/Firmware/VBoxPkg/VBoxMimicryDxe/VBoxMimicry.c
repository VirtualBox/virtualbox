/* $Id: VBoxMimicry.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxMimicry.c - Mimic table entry.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#include "VBoxMimicry.h"
#define VBOX_MIMICRY_VAR L"VBOX_MIMICRY"

/*610467a0-d8a7-11de-a911-87667af93b7d*/
static EFI_GUID gVBoxMimicryVarGuid = { 0x610467a0, 0xd8a7, 0x11de, {0xa9, 0x11, 0x87, 0x66, 0x7a, 0xf9, 0x3b, 0x7d}};

#define MIM_TBL_ENTRY(name, guid)   DO_9_FAKE_DECL(name)
#include "mimic_tbl.h"
#undef MIM_TBL_ENTRY

#define MIM_TBL_ENTRY(name, guid)           \
static EFI_GUID gFake ## name = guid;       \
static void *gFuncArray_ ## name [] =       \
{                                           \
    name ## _fake_impl0,                    \
    name ## _fake_impl1,                    \
    name ## _fake_impl2,                    \
    name ## _fake_impl3,                    \
    name ## _fake_impl4,                    \
    name ## _fake_impl5,                    \
    name ## _fake_impl6,                    \
    name ## _fake_impl7,                    \
    name ## _fake_impl8,                    \
    name ## _fake_impl9                     \
};
#include "mimic_tbl.h"
#undef MIM_TBL_ENTRY

#define MIM_TBL_ENTRY(name, guid)       \
FAKE_IMPL(name ## _fake_impl0, gFake ## name)    \
FAKE_IMPL(name ## _fake_impl1, gFake ## name)    \
FAKE_IMPL(name ## _fake_impl2, gFake ## name)    \
FAKE_IMPL(name ## _fake_impl3, gFake ## name)    \
FAKE_IMPL(name ## _fake_impl4, gFake ## name)    \
FAKE_IMPL(name ## _fake_impl5, gFake ## name)    \
FAKE_IMPL(name ## _fake_impl6, gFake ## name)    \
FAKE_IMPL(name ## _fake_impl7, gFake ## name)    \
FAKE_IMPL(name ## _fake_impl8, gFake ## name)    \
FAKE_IMPL(name ## _fake_impl9, gFake ## name)
#include "mimic_tbl.h"
#undef MIM_TBL_ENTRY



EFI_STATUS
EFIAPI
VBoxMimicryInit(EFI_HANDLE hImage, EFI_SYSTEM_TABLE *pSysTable)
{
        /* Set'n'check intercept variable */
        EFI_STATUS r;
        UINT32 val;
        UINTN size = sizeof(UINT32);
        r = gRT->GetVariable(VBOX_MIMICRY_VAR, &gVBoxMimicryVarGuid, NULL, &size, &val);
        if (   EFI_ERROR(r)
            && r == EFI_NOT_FOUND)
        {
            size = sizeof(UINT32);
            val = 1;
            r = gRT->SetVariable(VBOX_MIMICRY_VAR, &gVBoxMimicryVarGuid, EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS, size, &val);
            if (EFI_ERROR(r))
            {
                DEBUG((DEBUG_INFO, "%a:%d - %r\n", __FILE__,  __LINE__, r));
                return r;
            }
            gThis = AllocateZeroPool(sizeof(VBOXMIMICRY));
            r = install_mimic_interfaces();
            if(EFI_ERROR(r))
            {
                DEBUG((DEBUG_INFO, "%a:%d - %r\n", __FILE__,  __LINE__, r));
                return r;
            }
            gThis->hImage = hImage;
            return r;
        }
        if (!EFI_ERROR(r))
        {
            return EFI_ALREADY_STARTED;
        }
        return r;
}

EFI_STATUS
EFIAPI
VBoxMimicryFini(EFI_HANDLE hImage)
{
    EFI_STATUS r;
    uninstall_mimic_interfaces();
    FreePool(gThis);
    r = gRT->SetVariable(VBOX_MIMICRY_VAR, &gVBoxMimicryVarGuid, EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS, 0, NULL);
    if (EFI_ERROR(r))
    {
        DEBUG((DEBUG_INFO, "%a:%d - %r\n", __FILE__,  __LINE__, r));
        return r;
    }
    return EFI_SUCCESS;
}

EFI_STATUS install_mimic_interfaces()
{
    EFI_STATUS Status;
    Status = gBS->InstallMultipleProtocolInterfaces (
    &gThis->hImage,
#define MIM_TBL_ENTRY(name, guid) gFake##name, gFuncArray_##name,
#include "mimic_tbl.h"
#undef MIM_TBL_ENTRY
    NULL);
    return Status;
}
EFI_STATUS uninstall_mimic_interfaces()
{
    EFI_STATUS Status;
    Status = gBS->InstallMultipleProtocolInterfaces (
    &gThis->hImage,
#define MIM_TBL_ENTRY(name, guid) gFake##name,
#include "mimic_tbl.h"
#undef MIM_TBL_ENTRY
    NULL);
    return Status;
}
