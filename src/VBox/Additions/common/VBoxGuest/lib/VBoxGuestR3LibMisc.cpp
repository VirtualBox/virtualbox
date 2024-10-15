/* $Id: VBoxGuestR3LibMisc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, Misc.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include <VBox/log.h>
#include "VBoxGuestR3LibInternal.h"


/**
 * Change the IRQ filter mask.
 *
 * @returns IPRT status code.
 * @param   fOr     The OR mask.
 * @param   fNot    The NOT mask.
 */
VBGLR3DECL(int) VbglR3CtlFilterMask(uint32_t fOr, uint32_t fNot)
{
    VBGLIOCCHANGEFILTERMASK Info;
    VBGLREQHDR_INIT(&Info.Hdr, CHANGE_FILTER_MASK);
    Info.u.In.fOrMask  = fOr;
    Info.u.In.fNotMask = fNot;
    return vbglR3DoIOCtl(VBGL_IOCTL_CHANGE_FILTER_MASK, &Info.Hdr, sizeof(Info));
}


/**
 * Report a change in the capabilities that we support to the host.
 *
 * @returns IPRT status code.
 * @param   fOr     Capabilities which have been added.
 * @param   fNot    Capabilities which have been removed.
 *
 * @todo    Move to a different file.
 */
VBGLR3DECL(int) VbglR3SetGuestCaps(uint32_t fOr, uint32_t fNot)
{
    VBGLIOCSETGUESTCAPS Info;
    VBGLREQHDR_INIT(&Info.Hdr, CHANGE_GUEST_CAPABILITIES);
    Info.u.In.fOrMask  = fOr;
    Info.u.In.fNotMask = fNot;
    return vbglR3DoIOCtl(VBGL_IOCTL_CHANGE_GUEST_CAPABILITIES, &Info.Hdr, sizeof(Info));
}


/**
 * Acquire capabilities to report to the host.
 *
 * The capabilities which can be acquired are the same as those reported by
 * VbglR3SetGuestCaps, and once a capability has been acquired once is is
 * switched to "acquire mode" and can no longer be set using VbglR3SetGuestCaps.
 * Capabilities can also be switched to acquire mode without actually being
 * acquired.  A client can not acquire a capability which has been acquired and
 * not released by another client.  Capabilities acquired are automatically
 * released on session termination.
 *
 * @returns IPRT status code
 * @returns VERR_RESOURCE_BUSY and acquires nothing if another client has
 *          acquired and not released at least one of the @a fOr capabilities
 * @param   fOr      Capabilities to acquire or to switch to acquire mode
 * @param   fNot     Capabilities to release
 * @param   fConfig  if set, capabilities in @a fOr are switched to acquire mode
 *                   but not acquired, and @a fNot is ignored.  See
 *                   VBGL_IOC_AGC_FLAGS_CONFIG_ACQUIRE_MODE for details.
 */
VBGLR3DECL(int) VbglR3AcquireGuestCaps(uint32_t fOr, uint32_t fNot, bool fConfig)
{
    VBGLIOCACQUIREGUESTCAPS Info;
    VBGLREQHDR_INIT(&Info.Hdr, ACQUIRE_GUEST_CAPABILITIES);
    Info.u.In.fFlags   = fConfig ? VBGL_IOC_AGC_FLAGS_CONFIG_ACQUIRE_MODE : VBGL_IOC_AGC_FLAGS_DEFAULT;
    Info.u.In.fOrMask  = fOr;
    Info.u.In.fNotMask = fNot;
    return vbglR3DoIOCtl(VBGL_IOCTL_ACQUIRE_GUEST_CAPABILITIES, &Info.Hdr, sizeof(Info));
}


/**
 * Query the session ID of this VM.
 *
 * The session id is an unique identifier that gets changed for each VM start,
 * reset or restore.  Useful for detection a VM restore.
 *
 * @returns IPRT status code.
 * @param   pu64IdSession       Session id (out).  This is NOT changed on
 *                              failure, so the caller can depend on this to
 *                              deal with backward compatibility (see
 *                              VBoxServiceVMInfoWorker() for an example.)
 */
VBGLR3DECL(int) VbglR3GetSessionId(uint64_t *pu64IdSession)
{
    VMMDevReqSessionId Req;

    vmmdevInitRequest(&Req.header, VMMDevReq_GetSessionId);
    Req.idSession = 0;
    int rc = vbglR3GRPerform(&Req.header);
    if (RT_SUCCESS(rc))
        *pu64IdSession = Req.idSession;

    return rc;
}
