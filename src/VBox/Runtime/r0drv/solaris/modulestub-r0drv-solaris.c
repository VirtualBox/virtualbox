/* $Id: modulestub-r0drv-solaris.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Ring-0 Solaris stubs
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <sys/modctl.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static struct modlmisc g_rtModuleStubMisc =
{
    &mod_miscops,           /* extern from kernel */
    "platform agnostic module"
};


static struct modlinkage g_rtModuleStubModLinkage =
{
    MODREV_1,   /* loadable module system revision */
    {
        &g_rtModuleStubMisc,
        NULL    /* terminate array of linkage structures */
    }
};



int _init(void);
int _init(void)
{
    /* Disable auto unloading. */
    modctl_t *pModCtl = mod_getctl(&g_rtModuleStubModLinkage);
    if (pModCtl)
        pModCtl->mod_loadflags |= MOD_NOAUTOUNLOAD;

    return mod_install(&g_rtModuleStubModLinkage);
}


int _fini(void);
int _fini(void)
{
    return mod_remove(&g_rtModuleStubModLinkage);
}


int _info(struct modinfo *pModInfo);
int _info(struct modinfo *pModInfo)
{
    return mod_info(&g_rtModuleStubModLinkage, pModInfo);
}

