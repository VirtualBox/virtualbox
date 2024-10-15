/* $Id: GaDrvEnvKMT.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver interface to the WDDM miniport driver.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_3D_win_VBoxGL_GaDrvEnvKMT_h
#define GA_INCLUDED_SRC_3D_win_VBoxGL_GaDrvEnvKMT_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBoxGaDriver.h>
#include <VBoxWddmUmHlp.h>

RT_C_DECLS_BEGIN

const WDDMGalliumDriverEnv *GaDrvEnvKmtCreate(void);
void GaDrvEnvKmtDelete(const WDDMGalliumDriverEnv *pEnv);

D3DKMT_HANDLE GaDrvEnvKmtContextHandle(const WDDMGalliumDriverEnv *pEnv,
                                       uint32_t u32Cid);
D3DKMT_HANDLE GaDrvEnvKmtSurfaceHandle(const WDDMGalliumDriverEnv *pEnv,
                                       uint32_t u32Sid);
void GaDrvEnvKmtAdapterLUID(const WDDMGalliumDriverEnv *pEnv,
                            LUID *pAdapterLuid);
D3DKMT_HANDLE GaDrvEnvKmtAdapterHandle(const WDDMGalliumDriverEnv *pEnv);
D3DKMT_HANDLE GaDrvEnvKmtDeviceHandle(const WDDMGalliumDriverEnv *pEnv);
void GaDrvEnvKmtRenderCompose(const WDDMGalliumDriverEnv *pEnv,
                              uint32_t u32Cid,
                              void *pvCommands,
                              uint32_t cbCommands,
                              ULONGLONG PresentHistoryToken);

RT_C_DECLS_END

#endif /* !GA_INCLUDED_SRC_3D_win_VBoxGL_GaDrvEnvKMT_h */
