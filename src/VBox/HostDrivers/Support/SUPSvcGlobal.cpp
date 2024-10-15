/* $Id: SUPSvcGlobal.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Service - The Global Service.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include "SUPSvcInternal.h"

#include <iprt/errcore.h>
#include <iprt/assert.h>


/** @copydoc SUPSVCSERVICE::pfnCreate */
DECLCALLBACK(int)  supSvcGlobalCreate(void **ppvInstance)
{
    *ppvInstance = (void *)1;
    return VINF_SUCCESS;
}


/** @copydoc SUPSVCSERVICE::pfnStart */
DECLCALLBACK(void) supSvcGlobalStart(void *pvInstance)
{
    Assert(pvInstance == (void *)1);
    NOREF(pvInstance);
}


/** @copydoc SUPSVCSERVICE::pfnTryStop */
DECLCALLBACK(int)  supSvcGlobalTryStop(void *pvInstance)
{
    Assert(pvInstance == (void *)1);
    NOREF(pvInstance);
    return VINF_SUCCESS;
}


/** @copydoc SUPSVCSERVICE::pfnStopAndDestroy */
DECLCALLBACK(void) supSvcGlobalStopAndDestroy(void *pvInstance, bool fRunning)
{
    Assert(pvInstance == (void *)1);
    NOREF(pvInstance);
    NOREF(fRunning);
}

