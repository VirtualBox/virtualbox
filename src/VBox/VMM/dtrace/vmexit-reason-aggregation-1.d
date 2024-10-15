/* $Id: vmexit-reason-aggregation-1.d 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DTracing VBox - vmexit reason aggregation test \#1.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#pragma D option quiet


vboxvmm:::r0-hmsvm-vmexit
{
    @g_aSvmExits[args[2]] = count();
}

vboxvmm:::r0-hmvmx-vmexit-noctx
{
    @g_aVmxExits[args[2]] = count();
}

END
{
    printa(" svmexit=%#04llx   %@10u times\n", @g_aSvmExits);
    printa(" vmxexit=%#04llx   %@10u times\n", @g_aVmxExits);
}

