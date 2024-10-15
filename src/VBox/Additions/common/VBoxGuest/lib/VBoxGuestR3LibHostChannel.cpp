/* $Id: VBoxGuestR3LibHostChannel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, Host Channel.
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


#include <iprt/mem.h>

#include <VBox/HostServices/VBoxHostChannel.h>

#include "VBoxGuestR3LibInternal.h"


VBGLR3DECL(int) VbglR3HostChannelInit(uint32_t *pidClient)
{
    return VbglR3HGCMConnect("VBoxHostChannel", pidClient);
}

VBGLR3DECL(void) VbglR3HostChannelTerm(uint32_t idClient)
{
    VbglR3HGCMDisconnect(idClient);
}

VBGLR3DECL(int) VbglR3HostChannelAttach(uint32_t *pu32ChannelHandle,
                                        uint32_t u32HGCMClientId,
                                        const char *pszName,
                                        uint32_t u32Flags)
{
    /* Make a heap copy of the name, because HGCM can not use some of other memory types. */
    size_t cbName = strlen(pszName) + 1;
    char *pszCopy = (char *)RTMemAlloc(cbName);
    if (pszCopy == NULL)
    {
        return VERR_NO_MEMORY;
    }

    memcpy(pszCopy, pszName, cbName);

    VBoxHostChannelAttach parms;
    VBGL_HGCM_HDR_INIT(&parms.hdr, u32HGCMClientId, VBOX_HOST_CHANNEL_FN_ATTACH, 3);
    VbglHGCMParmPtrSet(&parms.name, pszCopy, (uint32_t)cbName);
    VbglHGCMParmUInt32Set(&parms.flags, u32Flags);
    VbglHGCMParmUInt32Set(&parms.handle, 0);

    int rc = VbglR3HGCMCall(&parms.hdr, sizeof(parms));

    if (RT_SUCCESS(rc))
        *pu32ChannelHandle = parms.handle.u.value32;

    RTMemFree(pszCopy);

    return rc;
}

VBGLR3DECL(void) VbglR3HostChannelDetach(uint32_t u32ChannelHandle,
                                         uint32_t u32HGCMClientId)
{
    VBoxHostChannelDetach parms;
    VBGL_HGCM_HDR_INIT(&parms.hdr, u32HGCMClientId, VBOX_HOST_CHANNEL_FN_DETACH, 1);
    VbglHGCMParmUInt32Set(&parms.handle, u32ChannelHandle);

    VbglR3HGCMCall(&parms.hdr, sizeof(parms));
}

VBGLR3DECL(int) VbglR3HostChannelSend(uint32_t u32ChannelHandle,
                                      uint32_t u32HGCMClientId,
                                      void *pvData,
                                      uint32_t cbData)
{
    VBoxHostChannelSend parms;
    VBGL_HGCM_HDR_INIT(&parms.hdr, u32HGCMClientId, VBOX_HOST_CHANNEL_FN_SEND, 2);
    VbglHGCMParmUInt32Set(&parms.handle, u32ChannelHandle);
    VbglHGCMParmPtrSet(&parms.data, pvData, cbData);

    return VbglR3HGCMCall(&parms.hdr, sizeof(parms));
}

VBGLR3DECL(int) VbglR3HostChannelRecv(uint32_t u32ChannelHandle,
                                      uint32_t u32HGCMClientId,
                                      void *pvData,
                                      uint32_t cbData,
                                      uint32_t *pu32SizeReceived,
                                      uint32_t *pu32SizeRemaining)
{
    VBoxHostChannelRecv parms;
    VBGL_HGCM_HDR_INIT(&parms.hdr, u32HGCMClientId, VBOX_HOST_CHANNEL_FN_RECV, 4);
    VbglHGCMParmUInt32Set(&parms.handle, u32ChannelHandle);
    VbglHGCMParmPtrSet(&parms.data, pvData, cbData);
    VbglHGCMParmUInt32Set(&parms.sizeReceived, 0);
    VbglHGCMParmUInt32Set(&parms.sizeRemaining, 0);

    int rc = VbglR3HGCMCall(&parms.hdr, sizeof(parms));

    if (RT_SUCCESS(rc))
    {
        *pu32SizeReceived = parms.sizeReceived.u.value32;
        *pu32SizeRemaining = parms.sizeRemaining.u.value32;
    }

    return rc;
}

VBGLR3DECL(int) VbglR3HostChannelControl(uint32_t u32ChannelHandle,
                                         uint32_t u32HGCMClientId,
                                         uint32_t u32Code,
                                         void *pvParm,
                                         uint32_t cbParm,
                                         void *pvData,
                                         uint32_t cbData,
                                         uint32_t *pu32SizeDataReturned)
{
    VBoxHostChannelControl parms;
    VBGL_HGCM_HDR_INIT(&parms.hdr, u32HGCMClientId, VBOX_HOST_CHANNEL_FN_CONTROL, 5);
    VbglHGCMParmUInt32Set(&parms.handle, u32ChannelHandle);
    VbglHGCMParmUInt32Set(&parms.code, u32Code);
    VbglHGCMParmPtrSet(&parms.parm, pvParm, cbParm);
    VbglHGCMParmPtrSet(&parms.data, pvData, cbData);
    VbglHGCMParmUInt32Set(&parms.sizeDataReturned, 0);

    int rc = VbglR3HGCMCall(&parms.hdr, sizeof(parms));

    if (RT_SUCCESS(rc))
    {
        *pu32SizeDataReturned = parms.sizeDataReturned.u.value32;
    }

    return rc;
}

VBGLR3DECL(int) VbglR3HostChannelEventWait(uint32_t *pu32ChannelHandle,
                                           uint32_t u32HGCMClientId,
                                           uint32_t *pu32EventId,
                                           void *pvParm,
                                           uint32_t cbParm,
                                           uint32_t *pu32SizeReturned)
{
    VBoxHostChannelEventWait parms;
    VBGL_HGCM_HDR_INIT(&parms.hdr, u32HGCMClientId, VBOX_HOST_CHANNEL_FN_EVENT_WAIT, 4);
    VbglHGCMParmUInt32Set(&parms.handle, 0);
    VbglHGCMParmUInt32Set(&parms.id, 0);
    VbglHGCMParmPtrSet(&parms.parm, pvParm, cbParm);
    VbglHGCMParmUInt32Set(&parms.sizeReturned, 0);

    int rc = VbglR3HGCMCall(&parms.hdr, sizeof(parms));

    if (RT_SUCCESS(rc))
    {
        *pu32ChannelHandle = parms.handle.u.value32;
        *pu32EventId = parms.id.u.value32;
        *pu32SizeReturned = parms.sizeReturned.u.value32;
    }

    return rc;
}

VBGLR3DECL(int) VbglR3HostChannelEventCancel(uint32_t u32ChannelHandle,
                                             uint32_t u32HGCMClientId)
{
    RT_NOREF1(u32ChannelHandle);

    VBoxHostChannelEventCancel parms;
    VBGL_HGCM_HDR_INIT(&parms.hdr, u32HGCMClientId, VBOX_HOST_CHANNEL_FN_EVENT_CANCEL, 0);

    return VbglR3HGCMCall(&parms.hdr, sizeof(parms));
}

VBGLR3DECL(int) VbglR3HostChannelQuery(const char *pszName,
                                       uint32_t u32HGCMClientId,
                                       uint32_t u32Code,
                                       void *pvParm,
                                       uint32_t cbParm,
                                       void *pvData,
                                       uint32_t cbData,
                                       uint32_t *pu32SizeDataReturned)
{
    /* Make a heap copy of the name, because HGCM can not use some of other memory types. */
    size_t cbName = strlen(pszName) + 1;
    char *pszCopy = (char *)RTMemAlloc(cbName);
    if (pszCopy == NULL)
    {
        return VERR_NO_MEMORY;
    }

    memcpy(pszCopy, pszName, cbName);

    VBoxHostChannelQuery parms;
    VBGL_HGCM_HDR_INIT(&parms.hdr, u32HGCMClientId, VBOX_HOST_CHANNEL_FN_QUERY, 5);
    VbglHGCMParmPtrSet(&parms.name, pszCopy, (uint32_t)cbName);
    VbglHGCMParmUInt32Set(&parms.code, u32Code);
    VbglHGCMParmPtrSet(&parms.parm, pvParm, cbParm);
    VbglHGCMParmPtrSet(&parms.data, pvData, cbData);
    VbglHGCMParmUInt32Set(&parms.sizeDataReturned, 0);

    int rc = VbglR3HGCMCall(&parms.hdr, sizeof(parms));

    if (RT_SUCCESS(rc))
    {
        *pu32SizeDataReturned = parms.sizeDataReturned.u.value32;
    }

    RTMemFree(pszCopy);

    return rc;
}
