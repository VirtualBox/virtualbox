/* $Id: vmexit-rip-aggregation-1.d 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DTracing VBox - vmexit rip aggregation test \#1.
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


vboxvmm:::r0-hmsvm-vmexit,vboxvmm:::r0-hmvmx-vmexit
{
    /*printf("cs:rip=%02x:%08llx", args[1]->cs.Sel, args[1]->rip.rip);*/
    @g_aRips[args[1]->rip.rip] = count();
    /*@g_aRips[args[0]->cpum.s.Guest.rip.rip] = count(); - alternative access route */
}

END
{
    printa(" rip=%#018llx   %@4u times\n", @g_aRips);
}

