/* $Id$ */
/** @file
 * tstVMSVGACursor - Saved cursor format selection and bounds checks.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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

#include <iprt/test.h>
#include <iprt/string.h>

#include "../Graphics/DevVGA-SVGA.h"

/** Tests saved-format precedence and legacy bitmap classification. */
static void tstCursorFormat(void)
{
    RTTestSub(NIL_RTTEST, "Saved flag and legacy recovery");
    uint8_t abData[76]; /* 9x2: four mask bytes, followed by 18 BGRA pixels. */
    RT_ZERO(abData);
    memset(abData, 0xff, 4);

    for (uint32_t uCursorOn = 0; uCursorOn < 4; uCursorOn++)
    {
        /* Transparent alpha and all-ones-mask AND/XOR cursors are ambiguous. */
        RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(false, false, uCursorOn, abData, sizeof(abData), 9, 2)
                      == (uCursorOn == 0));
        /* Both saved flag values override pixels and the legacy register. */
        RTTESTI_CHECK(!vmsvgaR3CursorUsesAlpha(true, false, uCursorOn, abData, sizeof(abData), 9, 2));
        RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(true, true, uCursorOn, abData, sizeof(abData), 9, 2));

        /* RGB is not evidence of alpha: an all-ones mask can describe inversion. */
        abData[4] = 0xff;
        RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(false, false, uCursorOn, abData, sizeof(abData), 9, 2)
                      == (uCursorOn == 0));
        abData[4] = 0;

        /* A clear actual mask bit plus zero alpha identifies a masked cursor. */
        abData[3] = 0x7f; /* Last pixel of the second row. */
        RTTESTI_CHECK(!vmsvgaR3CursorUsesAlpha(false, true, uCursorOn, abData, sizeof(abData), 9, 2));
        RTTESTI_CHECK(!vmsvgaR3CursorUsesAlpha(true, false, uCursorOn, abData, sizeof(abData), 9, 2));
        RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(true, true, uCursorOn, abData, sizeof(abData), 9, 2));

        /* Nonzero alpha, even in the last pixel, prevents bitmap recovery. */
        abData[75] = 1;
        RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(false, false, uCursorOn, abData, sizeof(abData), 9, 2)
                      == (uCursorOn == 0));
        abData[75] = 0;
        abData[3] = 0xff;
    }

    /* Zero padding bits at the end of each mask row are not actual pixels. */
    abData[1] = abData[3] = 0x80;
    RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(false, false, 0, abData, sizeof(abData), 9, 2));

    /* One mask byte plus three zero alignment bytes, then one BGRA pixel. */
    uint8_t abSmall[9] = { 0x80, 0, 0, 0, 1, 2, 3, 0, 0xff };
    RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(false, false, 0, abSmall, sizeof(abSmall), 1, 1));
    abSmall[0] = 0; /* Padding and trailing bytes must not affect recovery. */
    RTTESTI_CHECK(!vmsvgaR3CursorUsesAlpha(false, true, 0, abSmall, sizeof(abSmall), 1, 1));
}

/** Tests incomplete and overflowing bitmap layouts without dereferencing them. */
static void tstCursorBounds(void)
{
    RTTestSub(NIL_RTTEST, "Invalid and incomplete layouts");
    uint8_t abData[8] = { 0 };
    RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(false, false, 0, NULL, sizeof(abData), 1, 1));
    RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(false, false, 0, abData, sizeof(abData), 0, 1));
    RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(false, false, 0, abData, sizeof(abData), 1, 0));
    RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(false, false, 0, abData, sizeof(abData), UINT32_MAX, UINT32_MAX));
    RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(false, false, 0, abData, UINT32_MAX, UINT32_MAX, 1));
    RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(false, false, 0, abData, UINT32_MAX, 1, UINT32_MAX));
    for (uint32_t cbData = 0; cbData < sizeof(abData); cbData++)
    {
        RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(false, false, 0, abData, cbData, 1, 1));
        RTTESTI_CHECK(!vmsvgaR3CursorUsesAlpha(false, true, 1, abData, cbData, 1, 1));
    }
    RTTESTI_CHECK(!vmsvgaR3CursorUsesAlpha(false, true, 0, abData, sizeof(abData), 1, 1));

    /* A serialized format must not consult the bitmap at all. */
    void const *pvInvalid = (void const *)(uintptr_t)1;
    RTTESTI_CHECK(!vmsvgaR3CursorUsesAlpha(true, false, 0, pvInvalid, UINT32_MAX, UINT32_MAX, UINT32_MAX));
    RTTESTI_CHECK(vmsvgaR3CursorUsesAlpha(true, true, 1, pvInvalid, UINT32_MAX, UINT32_MAX, UINT32_MAX));
}

int main(int argc, char **argv)
{
    RT_NOREF(argc, argv);
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstVMSVGACursor", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);
    tstCursorFormat();
    tstCursorBounds();
    return RTTestSummaryAndDestroy(hTest);
}
