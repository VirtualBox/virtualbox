/* $Id: RTSystemQueryDmiString-solaris.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTSystemQueryDmiString, solaris ring-3.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/system.h>
#include "internal/iprt.h"

#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/string.h>

#include <smbios.h>
#include <errno.h>


RTDECL(int) RTSystemQueryDmiString(RTSYSDMISTR enmString, char *pszBuf, size_t cbBuf)
{
    AssertPtrReturn(pszBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf > 0, VERR_INVALID_PARAMETER);
    *pszBuf = '\0';
    AssertReturn(enmString > RTSYSDMISTR_INVALID && enmString < RTSYSDMISTR_END, VERR_INVALID_PARAMETER);

    int rc = VERR_NOT_SUPPORTED;
    int err = 0;
    smbios_hdl_t *pSMB = smbios_open(NULL /* default fd */, SMB_VERSION, 0 /* flags */, &err);
    if (pSMB)
    {
        smbios_system_t hSMBSys;
        id_t hSMBId = smbios_info_system(pSMB, &hSMBSys);
        if (hSMBId != SMB_ERR)
        {
            /* Don't need the common bits for the product UUID. */
            if (enmString == RTSYSDMISTR_PRODUCT_UUID)
            {
                static char const s_szHex[17] = "0123456789ABCDEF";
                char     szData[64];
                char    *pszData = szData;
                unsigned cchUuid = RT_MIN(hSMBSys.smbs_uuidlen, sizeof(szData) - 1);
                for (unsigned i = 0; i < cchUuid; i++)
                {
                    *pszData++ = s_szHex[hSMBSys.smbs_uuid[i] >> 4];
                    *pszData++ = s_szHex[hSMBSys.smbs_uuid[i] & 0xf];
                    if (i == 3 || i == 5 || i == 7 || i == 9)
                        *pszData++ = '-';
                }
                *pszData = '\0';
                rc = RTStrCopy(pszBuf, cbBuf, szData);
                smbios_close(pSMB);
                return rc;
            }

            smbios_info_t hSMBInfo;
            id_t hSMBInfoId = smbios_info_common(pSMB, hSMBId, &hSMBInfo);
            if (hSMBInfoId != SMB_ERR)
            {
                switch (enmString)
                {
                    case RTSYSDMISTR_PRODUCT_NAME:      rc = RTStrCopy(pszBuf, cbBuf, hSMBInfo.smbi_product); break;
                    case RTSYSDMISTR_PRODUCT_VERSION:   rc = RTStrCopy(pszBuf, cbBuf, hSMBInfo.smbi_version); break;
                    case RTSYSDMISTR_PRODUCT_SERIAL:    rc = RTStrCopy(pszBuf, cbBuf, hSMBInfo.smbi_serial);  break;
                    case RTSYSDMISTR_MANUFACTURER:      rc = RTStrCopy(pszBuf, cbBuf, hSMBInfo.smbi_manufacturer);  break;

                    default:  /* make gcc happy */
                        rc = VERR_NOT_SUPPORTED;
                }
                smbios_close(pSMB);
                return rc;
            }
        }

        /* smbios_* error path. */
        err = smbios_errno(pSMB);
        smbios_close(pSMB);
    }

    /* Do some error conversion.  */
    if (err == EPERM || err == EACCES)
        rc = VERR_ACCESS_DENIED;
    return rc;
}

