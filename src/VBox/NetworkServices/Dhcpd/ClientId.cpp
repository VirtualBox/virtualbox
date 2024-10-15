/* $Id: ClientId.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DHCP server - client identifier
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <algorithm>
#include "ClientId.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Indiciates wherther ClientId::rtStrFormat was already registered. */
bool ClientId::g_fFormatRegistered = false;


/**
 * Registers the ClientId format type callback ("%R[id]").
 */
void ClientId::registerFormat() RT_NOEXCEPT
{
    if (!g_fFormatRegistered)
    {
        int rc = RTStrFormatTypeRegister("id", rtStrFormat, NULL);
        AssertRC(rc);
        g_fFormatRegistered = RT_SUCCESS(rc);
    }
}


/**
 * @callback_method_impl{FNRTSTRFORMATTYPE, Formats ClientId via "%R[id]". }
 */
DECLCALLBACK(size_t)
ClientId::rtStrFormat(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                      const char *pszType, void const *pvValue,
                      int cchWidth, int cchPrecision, unsigned fFlags,
                      void *pvUser)
{
    RT_NOREF(pszType, cchWidth, cchPrecision, fFlags, pvUser);
    Assert(strcmp(pszType, "id") == 0);

    const ClientId *pThis = static_cast<const ClientId *>(pvValue);
    if (pThis == NULL)
        return pfnOutput(pvArgOutput, RT_STR_TUPLE("<NULL>"));

    size_t cb = 0;
    if (pThis->m_id.present())
    {
        cb += pfnOutput(pvArgOutput, RT_STR_TUPLE("["));

        const OptClientId::value_t &idopt = pThis->m_id.value();
        for (size_t i = 0; i < idopt.size(); ++i)
            cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "%s%02x", (i == 0 ? "" : ":"), idopt[i]);

        cb += pfnOutput(pvArgOutput, RT_STR_TUPLE("] ("));
    }

    cb += RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, "%RTmac", &pThis->m_mac);

    if (pThis->m_id.present())
        cb += pfnOutput(pvArgOutput, RT_STR_TUPLE(")"));

    return cb;
}


bool operator==(const ClientId &l, const ClientId &r) RT_NOEXCEPT
{
    if (l.m_id.present())
    {
        if (r.m_id.present())
            return l.m_id.value() == r.m_id.value();
    }
    else
    {
        if (!r.m_id.present())
            return l.m_mac == r.m_mac;
    }

    return false;
}


bool operator<(const ClientId &l, const ClientId &r) RT_NOEXCEPT
{
    if (l.m_id.present())
    {
        if (r.m_id.present())
            return l.m_id.value() < r.m_id.value();
        return false;           /* the one with id comes last */
    }
    else
    {
        if (r.m_id.present())
            return true;        /* the one with id comes last */
        return l.m_mac < r.m_mac;
    }
}

