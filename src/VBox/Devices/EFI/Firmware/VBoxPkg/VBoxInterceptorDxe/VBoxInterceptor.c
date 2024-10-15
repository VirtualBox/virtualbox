/* $Id: VBoxInterceptor.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxIntercepter.c - Entry point.
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
#include "VBoxInterceptor.h"

#define VBOX_INTERCEPTOR_VAR L"VBOX_INTERCEPTOR"
/*8e7505ec-d103-11de-8dbb-678848bdcb46*/
static EFI_GUID gVBoxInterceptorVarGuid = { 0x817505ec, 0xd103, 0x11de, {0x8d, 0xbb, 0x67, 0x88, 0x48, 0xbd, 0xcb, 0x46}};

static int g_indent = 0;

static char* getIndent(int count, int enter)
{
    static char buf[64];
    int i;
    char ch = enter ? '>' : '<';

    for (i=0; i<count+1; i++)
        buf[i] = ch;

    buf[i++] = ' ';
    buf[i] = 0;

    return buf;
}

char* indentRight()
{
    return getIndent(g_indent++, 1);
}

char* indentLeft()
{
    return getIndent(--g_indent, 0);
}


EFI_STATUS
EFIAPI
VBoxInterceptorInit(EFI_HANDLE hImage, EFI_SYSTEM_TABLE *pSysTable)
{
        /* Set'n'check intercept variable */
        EFI_STATUS r;
        UINT32 val;
        UINTN size = sizeof(UINT32);
        r = gRT->GetVariable(VBOX_INTERCEPTOR_VAR, &gVBoxInterceptorVarGuid, NULL, &size, &val);
        if (   EFI_ERROR(r)
            && r == EFI_NOT_FOUND)
        {
            size = sizeof(UINT32);
            val = 1;
            r = gRT->SetVariable(VBOX_INTERCEPTOR_VAR, &gVBoxInterceptorVarGuid, EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS, size, &val);
            if (EFI_ERROR(r))
            {
                DEBUG((DEBUG_INFO, "%a:%d - %r\n", __FILE__,  __LINE__, r));
                return r;
            }
            /* intercept installation */
            gThis = AllocateZeroPool(sizeof(VBOXINTERCEPTOR));
            r = install_bs_interceptors();
            if(EFI_ERROR(r))
            {
                DEBUG((DEBUG_INFO, "%a:%d - %r\n", __FILE__,  __LINE__, r));
                return r;
            }

            r = install_rt_interceptors();
            if(EFI_ERROR(r))
            {
                DEBUG((DEBUG_INFO, "%a:%d - %r\n", __FILE__,  __LINE__, r));
                return r;
            }
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
VBoxInterceptorFini(EFI_HANDLE hImage)
{
    EFI_STATUS r;
    uninstall_rt_interceptors();
    uninstall_bs_interceptors();
    FreePool(gThis);
    r = gRT->SetVariable(VBOX_INTERCEPTOR_VAR, &gVBoxInterceptorVarGuid, EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS, 0, NULL);
    if (EFI_ERROR(r))
    {
        DEBUG((DEBUG_INFO, "%a:%d - %r\n", __FILE__,  __LINE__, r));
        return r;
    }
    return EFI_SUCCESS;
}
