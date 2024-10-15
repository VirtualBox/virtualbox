/* $Id: D3DFeatureLevel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * ????
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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

/*
 * This debugging tool asks D3D API regarding to maximum supported
 * D3D10 Feature Level and dumps it to stdout.
 */

#include <iprt/win/d3d11.h>
#include <iprt/stream.h>

int main(int argc, char **argv)
{
    RT_NOREF(argc, argv);

    D3D_FEATURE_LEVEL iFeatureLevelMax = static_cast<D3D_FEATURE_LEVEL>(0);

    /* The list of feature levels we're selecting from. */
    const D3D_FEATURE_LEVEL aiFeatureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1
    };

    HRESULT rc = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, aiFeatureLevels,
                                   ARRAYSIZE(aiFeatureLevels), D3D11_SDK_VERSION, NULL, &iFeatureLevelMax, NULL);

    RTPrintf("Maximum supported feature level: 0x%X, hr=0x%X.\n", iFeatureLevelMax, (unsigned int)rc);

    return rc;
}
