/* $Id: tstRecording.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Recording testcases.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#include <iprt/test.h>
#include <VBox/err.h>

#include "RecordingUtils.h"


/** Tests centering / cropped centering of coordinates. */
static void testCenteredCrop(RTTEST hTest)
{
    RTTestSub(hTest, "testCenteredCrop");

    RECORDINGCODECPARMS Parms;

    #define INIT_CROP(/* Framebuffer width / height */   a_fw, a_fh, \
                      /* Video (codec) width / height */ a_vw, a_vh) \
        Parms.u.Video.uWidth  = a_vw; \
        Parms.u.Video.uHeight = a_vh; \
        Parms.u.Video.Scaling.u.Crop.m_iOriginX = int32_t(Parms.u.Video.uWidth  - a_fw) / 2; \
        Parms.u.Video.Scaling.u.Crop.m_iOriginY = int32_t(Parms.u.Video.uHeight - a_fh) / 2;

    #define TEST_CROP(/* Source In  */ a_in_sx, a_in_sy, a_in_sw, a_in_sh, \
                      /* Dest In    */ a_in_dx, a_in_dy, \
                      /* Source Out */ a_out_sx, a_out_sy, a_out_sw, a_out_sh, \
                      /* Dest Out   */ a_out_dx, a_out_dy, out_rc) \
        { \
            int32_t sx = a_in_sx; int32_t sy = a_in_sy; int32_t sw = a_in_sw; int32_t sh = a_in_sh; \
            int32_t dx = a_in_dx; int32_t dy = a_in_dy; \
            RTTEST_CHECK_RC (hTest, RecordingUtilsCoordsCropCenter(&Parms, &sx, &sy, &sw, &sh, &dx, &dy), out_rc); \
            RTTEST_CHECK_MSG(hTest, sx  == a_out_sx, (hTest, "Expected a_out_sx == %RI16, but got %RI16\n", a_out_sx, sx)); \
            RTTEST_CHECK_MSG(hTest, sy  == a_out_sy, (hTest, "Expected a_out_sy == %RI16, but got %RI16\n", a_out_sy, sy)); \
            RTTEST_CHECK_MSG(hTest, sw  == a_out_sw, (hTest, "Expected a_out_sw == %RI16, but got %RI16\n", a_out_sw, sw)); \
            RTTEST_CHECK_MSG(hTest, sh  == a_out_sh, (hTest, "Expected a_out_sh == %RI16, but got %RI16\n", a_out_sh, sh)); \
            RTTEST_CHECK_MSG(hTest, dx  == a_out_dx, (hTest, "Expected a_out_dx == %RI16, but got %RI16\n", a_out_dx, dx)); \
            RTTEST_CHECK_MSG(hTest, dy  == a_out_dy, (hTest, "Expected a_out_dy == %RI16, but got %RI16\n", a_out_dy, dy)); \
        }

    /*
     * No center / cropping performed (framebuffer and video codec are of the same size).
     */
    INIT_CROP(1024, 768, 1024, 768);
    TEST_CROP(0, 0, 1024, 768,
              0, 0,
              0, 0, 1024, 768,
              0, 0, VINF_SUCCESS);
    /* Source is bigger than allowed. */
    TEST_CROP(0, 0, 2048, 1536,
              0, 0,
              0, 0, 1024, 768,
              0, 0, VINF_SUCCESS);
    /* Source is bigger than allowed. */
    TEST_CROP(1024, 768, 2048, 1536,
              0, 0,
              1024, 768, 1024, 768,
              0, 0, VINF_SUCCESS);
    /* Check limits with custom destination. */
    TEST_CROP(0, 0, 1024, 768,
              512, 512,
              0, 0, 512, 256,
              512, 512, VINF_SUCCESS);
    TEST_CROP(512, 512, 1024, 768,
              512, 512,
              512, 512, 512, 256,
              512, 512, VINF_SUCCESS);
    TEST_CROP(512, 512, 1024, 768,
              1024, 768,
              512, 512, 0, 0,
              1024, 768, VWRN_RECORDING_ENCODING_SKIPPED);
    TEST_CROP(1024, 768, 1024, 768,
              1024, 768,
              1024, 768, 0, 0,
              1024, 768, VWRN_RECORDING_ENCODING_SKIPPED);

    /*
     * Framebuffer is twice the size of the video codec -- center crop the framebuffer.
     */
    INIT_CROP(2048, 1536, 1024, 768);
    TEST_CROP(0, 0, 2048, 1536,
              0, 0,
              512, 384, 1024, 768,
              0, 0, VINF_SUCCESS);

    TEST_CROP(1024, 768, 1024, 768,
              0, 0,
              1536, 1152, 512, 384,
              0, 0, VINF_SUCCESS);
    /* Check limits with custom destination. */
    TEST_CROP(1024, 768, 1024, 768,
              512, 384,
              1024, 768, 1024, 768,
              0, 0, VINF_SUCCESS);
    TEST_CROP(1024, 768, 1024, 768,
              512 + 42, 384 + 42,
              1024, 768, 1024 - 42, 768 - 42,
              42, 42, VINF_SUCCESS);
    TEST_CROP(1024, 768, 1024 * 2, 768 * 2,
              512, 384,
              1024, 768, 1024, 768,
              0, 0, VINF_SUCCESS);

    /*
     * Framebuffer is half the size of the video codec -- center (but not crop) the framebuffer within the video output.
     */
    INIT_CROP(1024, 768, 2048, 1536);
    TEST_CROP(0, 0, 1024, 768,
              0, 0,
              0, 0, 1024, 768,
              512, 384, VINF_SUCCESS);

#undef INIT_CROP
#undef TEST_CROP
}

int main()
{
    RTTEST     hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRecording", &hTest);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        RTTestBanner(hTest);

        testCenteredCrop(hTest);

        rcExit = RTTestSummaryAndDestroy(hTest);
    }
    return rcExit;
}

