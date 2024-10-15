/* $Id: iprt.d 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Static dtrace probes.
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

provider iprt
{
    probe critsect__entered(void *a_pvCritSect, const char *a_pszLaterNm, int32_t a_cLockers, uint32_t a_cNestings);
    probe critsect__leaving(void *a_pvCritSect, const char *a_pszLaterNm, int32_t a_cLockers, uint32_t a_cNestings);
    probe critsect__waiting(void *a_pvCritSect, const char *a_pszLaterNm, int32_t a_cLockers, void *a_pvNativeThreadOwner);
    probe critsect__busy(   void *a_pvCritSect, const char *a_pszLaterNm, int32_t a_cLockers, void *a_pvNativeThreadOwner);

    probe critsectrw__excl_entered(void *a_pvCritSect, const char *a_pszLaterNm, uint32_t a_cNestings,
                                   uint32_t a_cWaitingReaders, uint32_t cWriters);
    probe critsectrw__excl_leaving(void *a_pvCritSect, const char *a_pszLaterNm, uint32_t a_cNestings,
                                   uint32_t a_cWaitingReaders, uint32_t cWriters);
    probe critsectrw__excl_waiting(void *a_pvCritSect, const char *a_pszLaterNm, uint8_t a_fWriteMode, uint32_t a_cWaitingReaders,
                                   uint32_t a_cReaders, uint32_t a_cWriters, void *a_pvNativeOwnerThread);
    probe critsectrw__excl_busy(   void *a_pvCritSect, const char *a_pszLaterNm, uint8_t a_fWriteMode, uint32_t a_cWaitingReaders,
                                   uint32_t a_cReaders, uint32_t a_cWriters, void *a_pvNativeOwnerThread);
    probe critsectrw__excl_entered_shared(void *a_pvCritSect, const char *a_pszLaterNm, uint32_t a_cNestings,
                                          uint32_t a_cWaitingReaders, uint32_t a_cWriters);
    probe critsectrw__excl_leaving_shared(void *a_pvCritSect, const char *a_pszLaterNm, uint32_t a_cNestings,
                                          uint32_t a_cWaitingReaders, uint32_t a_cWriters);
    probe critsectrw__shared_entered(void *a_pvCritSect, const char *a_pszLaterNm, uint32_t a_cReaders, uint32_t a_cNestings);
    probe critsectrw__shared_leaving(void *a_pvCritSect, const char *a_pszLaterNm, uint32_t a_cReaders, uint32_t a_cNestings);
    probe critsectrw__shared_waiting(void *a_pvCritSect, const char *a_pszLaterNm, void *a_pvNativeThreadOwner,
                                     uint32_t cWaitingReaders, uint32_t cWriters);
    probe critsectrw__shared_busy(   void *a_pvCritSect, const char *a_pszLaterNm, void *a_pvNativeThreadOwner,
                                     uint32_t a_cWaitingReaders, uint32_t a_cWriters);

};

#pragma D attributes Evolving/Evolving/Common provider iprt provider
#pragma D attributes Private/Private/Unknown  provider iprt module
#pragma D attributes Private/Private/Unknown  provider iprt function
#pragma D attributes Evolving/Evolving/Common provider iprt name
#pragma D attributes Evolving/Evolving/Common provider iprt args

