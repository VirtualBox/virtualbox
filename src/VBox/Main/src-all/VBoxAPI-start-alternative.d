/* $Id: VBoxAPI-start-alternative.d 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxAPI - Static dtrace probes.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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

/*#pragma D attributes Evolving/Evolving/Common provider vboxapi provider
#pragma D attributes Private/Private/Unknown  provider vboxapi module
#pragma D attributes Private/Private/Unknown  provider vboxapi function
#pragma D attributes Evolving/Evolving/Common provider vboxapi name
#pragma D attributes Evolving/Evolving/Common provider vboxapi args*/

provider vboxapi
{
    /* Manually defined probes: */
    probe machine__state__changed(void *a_pMachine, int a_enmNewState, int a_enmOldState, const char *pszMachineUuid);

    /* The following probes are automatically generated and changes with the API: */
