/* $Id: d3d9render.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Gallium D3D testcase. Interface for D3D9 tests.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_test_d3d9render_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_test_d3d9render_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "d3dhlp.h"

class D3D9DeviceProvider
{
public:
    virtual ~D3D9DeviceProvider() {}
    virtual int DeviceCount() = 0;
    virtual IDirect3DDevice9 *Device(int index) = 0;
};

class D3D9Render
{
public:
    D3D9Render() {}
    virtual ~D3D9Render() {}
    virtual int RequiredDeviceCount() { return 1; }
    virtual HRESULT InitRender(D3D9DeviceProvider *pDP) = 0;
    virtual HRESULT DoRender(D3D9DeviceProvider *pDP) = 0;
    virtual void TimeAdvance(float dt) { (void)dt; return; }
};

D3D9Render *CreateRender(int iRenderId);
void DeleteRender(D3D9Render *pRender);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_wddm_gallium_test_d3d9render_h */
