/* $Id: time-1.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Query the time and check that it always goes forward, POSIX only.
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
#include <stdio.h>
#include <time.h>
#include <sys/time.h>



int main()
{
    unsigned            cErrors = 0;
#ifdef USE_CLOCK_MONOTONIC
    struct timespec     aTs[2];
    struct timespec    *pCur    = &aTs[0];
    struct timespec    *pPrev   = &aTs[1];
    struct timespec    *pTmp;
#else
    struct timeval      aTv[2];
    struct timeval     *pCur    = &aTv[0];
    struct timeval     *pPrev   = &aTv[1];
    struct timeval     *pTmp;
#endif

#ifdef USE_CLOCK_MONOTONIC
    clock_gettime(CLOCK_MONOTONIC, pPrev);
#else
    gettimeofday(pPrev, NULL);
#endif
    for (;;)
    {
#ifdef USE_CLOCK_MONOTONIC
        clock_gettime(CLOCK_MONOTONIC, pCur);
#else
        gettimeofday(pCur, NULL);
#endif

        if (   pCur->tv_sec == pPrev->tv_sec
#ifdef USE_CLOCK_MONOTONIC
            && pCur->tv_nsec < pPrev->tv_nsec
#else
            && pCur->tv_usec < pPrev->tv_usec
#endif
           )
        {
#ifdef USE_CLOCK_MONOTONIC
            printf("tv_nsec in the past: %ld.%09u < %ld.%09u - %u nsec\n",
                   (long)pCur->tv_sec,  (unsigned)pCur->tv_nsec,
                   (long)pPrev->tv_sec, (unsigned)pPrev->tv_nsec,
                   (unsigned)pPrev->tv_nsec - (unsigned)pCur->tv_nsec);
#else
            printf("tv_usec in the past: %ld.%06u < %ld.%06u - %u usec\n",
                   (long)pCur->tv_sec,  (unsigned)pCur->tv_usec,
                   (long)pPrev->tv_sec, (unsigned)pPrev->tv_usec,
                   (unsigned)pPrev->tv_usec - (unsigned)pCur->tv_usec);
#endif
            cErrors++;
            if (cErrors > 1000)
                break;
        }
        else if (pCur->tv_sec < pPrev->tv_sec)
        {
#ifdef USE_CLOCK_MONOTONIC
            printf("tv_sec  in the past: %ld.%09u < %ld.%09u\n",
                   (long)pCur->tv_sec,  (unsigned)pCur->tv_nsec,
                   (long)pPrev->tv_sec, (unsigned)pPrev->tv_nsec);
#else
            printf("tv_sec  in the past: %ld.%06u < %ld.%06u\n",
                   (long)pCur->tv_sec,  (unsigned)pCur->tv_usec,
                   (long)pPrev->tv_sec, (unsigned)pPrev->tv_usec);
#endif
            cErrors++;
            if (cErrors > 1000)
                break;
        }
        else
        {
            /* swap */
            pTmp  = pPrev;
            pPrev = pCur;
            pCur  = pTmp;
        }
    }

    return 1;
}
