/* $Id: VBoxNetSend.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * A place to share code and definitions between VBoxNetAdp and VBoxNetFlt host drivers.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

/** @todo move this to src/VBox/HostDrivers/darwin as a .cpp file. */

#ifndef VBOX_INCLUDED_SRC_darwin_VBoxNetSend_h
#define VBOX_INCLUDED_SRC_darwin_VBoxNetSend_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#if defined(RT_OS_DARWIN)

# include <iprt/err.h>
# include <iprt/assert.h>
# include <iprt/string.h>

# include <sys/socket.h>
# if MAC_OS_X_VERSION_MIN_REQUIRED >= 101500 /* The 10.15 SDK has a slightly butchered API deprecation attempt. */
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wmacro-redefined"      /* Each header redefines __NKE_API_DEPRECATED. */
#  pragma clang diagnostic ignored "-Wmissing-declarations" /* Misplaced __NKE_API_DEPRECATED; in kpi_mbuf.h. */
#  include <net/kpi_interface.h>
#  include <sys/kpi_mbuf.h>
#  include <net/if.h>
#  pragma clang diagnostic pop
# else /* < 10.15 */
#  include <net/kpi_interface.h>
RT_C_DECLS_BEGIN /* Buggy 10.4 headers, fixed in 10.5. */
#  include <sys/kpi_mbuf.h>
RT_C_DECLS_END
#  include <net/if.h>
# endif /* < 10.15 */


RT_C_DECLS_BEGIN

# if defined(IN_RING0)

/**
 * Constructs and submits a dummy packet to ifnet_input().
 *
 * This is a workaround for "stuck dock icon" issue. When the first packet goes
 * through the interface DLIL grabs a reference to the thread that submits the
 * packet and holds it until the interface is destroyed. By submitting this
 * dummy we make DLIL grab the thread of a non-GUI process.
 *
 * Most of this function was copied from vboxNetFltDarwinMBufFromSG().
 *
 * @returns VBox status code.
 * @param   pIfNet      The interface that will hold the reference to the calling
 *                      thread. We submit dummy as if it was coming from this interface.
 */
DECLINLINE(int) VBoxNetSendDummy(ifnet_t pIfNet)
{
    int rc = VINF_SUCCESS;

    size_t      cbTotal = 50; /* No Ethernet header */
    mbuf_how_t  How     = MBUF_WAITOK;
    mbuf_t      pPkt    = NULL;
    errno_t err = mbuf_allocpacket(How, cbTotal, NULL, &pPkt);
    if (!err)
    {
        /* Skip zero sized memory buffers (paranoia). */
        mbuf_t pCur = pPkt;
        while (pCur && !mbuf_maxlen(pCur))
            pCur = mbuf_next(pCur);
        Assert(pCur);

        /* Set the required packet header attributes. */
        mbuf_pkthdr_setlen(pPkt, cbTotal);
        mbuf_pkthdr_setheader(pPkt, mbuf_data(pCur));

        mbuf_setlen(pCur, cbTotal);
        memset(mbuf_data(pCur), 0, cbTotal);

        mbuf_pkthdr_setrcvif(pPkt, pIfNet); /* will crash without this. */

        err = ifnet_input(pIfNet, pPkt, NULL);
        if (err)
        {
            rc = RTErrConvertFromErrno(err);
            mbuf_freem(pPkt);
        }
    }
    else
        rc = RTErrConvertFromErrno(err);

    return rc;
}

# endif /* IN_RING0 */

RT_C_DECLS_END

#endif /* RT_OS_DARWIN */

#endif /* !VBOX_INCLUDED_SRC_darwin_VBoxNetSend_h */

