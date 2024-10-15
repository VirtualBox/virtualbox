/* $Id: string-base64.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MS COM / XPCOM Abstraction Layer - UTF-8 and UTF-16 string classes, BASE64 bits.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "VBox/com/string.h"

#include <iprt/base64.h>
#include <iprt/errcore.h>


namespace com
{

HRESULT Bstr::base64Encode(const void *pvData, size_t cbData, bool fLineBreaks /*= false*/)
{
    uint32_t const fFlags     = fLineBreaks ? RTBASE64_FLAGS_EOL_LF : RTBASE64_FLAGS_NO_LINE_BREAKS;
    size_t         cwcEncoded = RTBase64EncodedUtf16LengthEx(cbData, fFlags);
    HRESULT hrc = reserveNoThrow(cwcEncoded + 1);
    if (SUCCEEDED(hrc))
    {
        int vrc = RTBase64EncodeUtf16Ex(pvData, cbData, fFlags, mutableRaw(), cwcEncoded + 1, &cwcEncoded);
        AssertRCReturnStmt(vrc, setNull(), E_FAIL);
        hrc = joltNoThrow(cwcEncoded);
    }
    return hrc;
}

int Bstr::base64Decode(void *pvData, size_t cbData, size_t *pcbActual /*= NULL*/, PRTUTF16 *ppwszEnd /*= NULL*/)
{
    return RTBase64DecodeUtf16Ex(raw(), RTSTR_MAX, pvData, cbData, pcbActual, ppwszEnd);
}

ssize_t Bstr::base64DecodedSize(PRTUTF16 *ppwszEnd /*= NULL*/)
{
    return RTBase64DecodedUtf16SizeEx(raw(), RTSTR_MAX, ppwszEnd);
}

} /* namespace com */
