/* $Id: bs3-cmn-MemMove.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Bs3MemMove
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

#include "bs3kit-template-header.h"

#undef Bs3MemMove
BS3_CMN_DEF(void BS3_FAR *, Bs3MemMove,(void BS3_FAR *pvDst, const void BS3_FAR *pvSrc, size_t cbToCopy))
{
    size_t          cLargeRounds;
    BS3CVPTRUNION   uSrc;
    BS3PTRUNION     uDst;
    uSrc.pv = pvSrc;
    uDst.pv = pvDst;

    /* We don't care about segment wrapping here. */
    if ((uintptr_t)pvDst > (uintptr_t)pvSrc + cbToCopy)
    {
        /* Reverse copy. */
        uSrc.pb += cbToCopy;
        uDst.pb += cbToCopy;

        cLargeRounds = cbToCopy / sizeof(*uSrc.pcb);
        while (cLargeRounds-- > 0)
        {
            size_t uTmp = *--uSrc.pcb;
            *--uDst.pcb = uTmp;
        }

        cbToCopy %= sizeof(*uSrc.pcb);
        while (cbToCopy-- > 0)
        {
            uint8_t b = *--uSrc.pb;
            *--uDst.pb = b;
        }
    }
    else
    {
        /* Forward copy. */
        cLargeRounds = cbToCopy / sizeof(*uSrc.pcb);
        while (cLargeRounds-- > 0)
        {
            size_t uTmp = *uSrc.pcb++;
            *uDst.pcb++ = uTmp;
        }

        cbToCopy %= sizeof(*uSrc.pcb);
        while (cbToCopy-- > 0)
        {
            uint8_t b = *uSrc.pb++;
            *uDst.pb++ = b;
        }
    }
    return pvDst;
}

