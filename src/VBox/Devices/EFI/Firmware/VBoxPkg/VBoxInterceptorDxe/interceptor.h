/* $Id: interceptor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * interceptor.h - universal interceptor builder.
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
#define TBL_ENTRY(a,b, ign, return_type, params_num, params) static return_type EFIAPI CONCAT(VBOXINTERCEPTOR,b)(PARAMETER(params_num)params);
#include SERVICE_H
#undef TBL_ENTRY


#define TBL_ENTRY(a, b, voidness, return_type, nparams, params) \
    FUNCTION(voidness)(return_type, b, nparams, params)
#include SERVICE_H
#undef TBL_ENTRY




EFI_STATUS INSTALLER(SERVICE)()
{
#define TBL_ENTRY(a,b, ign0, ign1, ign2, ign3)\
do {                                    \
    gThis->CONCAT(SERVICE, Orig).b = ORIG_SERVICE->b;           \
    ORIG_SERVICE->b = CONCAT(VBOXINTERCEPTOR, b);      \
}while (0);
#include SERVICE_H
#undef TBL_ENTRY
    return EFI_SUCCESS;
}

EFI_STATUS UNINSTALLER(SERVICE)()
{
#define TBL_ENTRY(a,b, ign0, ign1, ign2, ign3)\
do {                                    \
    ORIG_SERVICE->b = gThis->CONCAT(SERVICE,Orig).b;           \
}while (0);
#include SERVICE_H
#undef TBL_ENTRY
    return EFI_SUCCESS;
}
