/* $Id: tstThread-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Thread Testcase no.1.
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
#include <iprt/thread.h>
#include <iprt/stream.h>
#include <iprt/initterm.h>
#include <iprt/errcore.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static unsigned volatile g_cErrors = 0;


static DECLCALLBACK(int) tstThread1ReturnImmediately(RTTHREAD hSelf, void *pvUser)
{
    RT_NOREF_PV(hSelf); RT_NOREF_PV(pvUser);
    return VINF_SUCCESS;
}



int main(int argc, char **argv)
{
    RTR3InitExe(argc, &argv, 0);

    /*
     * A simple testcase for the termination race we used to have.
     */
    RTTHREAD ahThreads[128];
    RTPrintf("tstThread-1: TESTING - %u waitable immediate return threads\n", RT_ELEMENTS(ahThreads));
    for (unsigned j = 0; j < 10; j++)
    {
        RTPrintf("tstThread-1: Iteration %u...\n", j);
        for (unsigned i = 0; i < RT_ELEMENTS(ahThreads); i++)
        {
            int rc = RTThreadCreate(&ahThreads[i], tstThread1ReturnImmediately, &ahThreads[i], 0, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "TEST1");
            if (RT_FAILURE(rc))
            {
                RTPrintf("tstThread-1: FAILURE(%d) - %d/%d RTThreadCreate failed, rc=%Rrc\n", __LINE__, i, j, rc);
                g_cErrors++;
                ahThreads[i] = NIL_RTTHREAD;
            }
        }

        /*
         * Wait for the threads to complete.
         */
        for (unsigned i = 0; i < RT_ELEMENTS(ahThreads); i++)
            if (ahThreads[i] != NIL_RTTHREAD)
            {
                int rc2;
                int rc = RTThreadWait(ahThreads[i], RT_INDEFINITE_WAIT, &rc2);
                if (RT_FAILURE(rc))
                {
                    RTPrintf("tstThread-1: FAILURE(%d) - %d/%d RTThreadWait failed, rc=%Rrc\n", __LINE__, j, i, rc);
                    g_cErrors++;
                }
                else if (RT_FAILURE(rc2))
                {
                    RTPrintf("tstThread-1: FAILURE(%d) - %d/%d Thread failed, rc2=%Rrc\n", __LINE__, j, i, rc2);
                    g_cErrors++;
                }
            }
    }

    /*
     * Summary.
     */
    if (!g_cErrors)
        RTPrintf("tstThread-1: SUCCESS\n");
    else
        RTPrintf("tstThread-1: FAILURE - %d errors\n", g_cErrors);

    return !!g_cErrors;
}
