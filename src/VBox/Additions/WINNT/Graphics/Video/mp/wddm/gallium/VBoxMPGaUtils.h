/* $Id: VBoxMPGaUtils.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver miscellaneous helpers and common includes.
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_gallium_VBoxMPGaUtils_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_gallium_VBoxMPGaUtils_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/nt/ntddk.h>
#include <iprt/nt/dispmprt.h>

#define LOG_GROUP LOG_GROUP_DRV_MINIPORT
#include <VBox/log.h>

#define GALOG_GROUP_RELEASE     0x00000001
#define GALOG_GROUP_TEST        0x00000002
#define GALOG_GROUP_DXGK        0x00000004
#define GALOG_GROUP_SVGA        0x00000008
#define GALOG_GROUP_SVGA_FIFO   0x00000010
#define GALOG_GROUP_FENCE       0x00000020
#define GALOG_GROUP_PRESENT     0x00000040
#define GALOG_GROUP_HOSTOBJECTS 0x00000080

#ifndef GALOG_GROUP
#define GALOG_GROUP GALOG_GROUP_TEST
#endif

extern volatile uint32_t g_fu32GaLogControl;

#define GALOG_ENABLED(a_Group) RT_BOOL(g_fu32GaLogControl & (a_Group))

#define GALOG_EXACT_(a_Group, a_Msg, a_Logger) do { \
    if (GALOG_ENABLED(a_Group)) \
    { \
        a_Logger(a_Msg); \
    } \
} while (0)

#define GALOG_(a_Group, a_Msg, a_Logger) do { \
    if (GALOG_ENABLED(a_Group)) \
    { \
        a_Logger(("%s: ", __FUNCTION__)); a_Logger(a_Msg); \
    } \
} while (0)

#define GALOGG_EXACT(a_Group, a_Msg) GALOG_EXACT_(a_Group, a_Msg, LogRel)
#define GALOGG(a_Group, a_Msg) GALOG_(a_Group, a_Msg, LogRel)

#define GALOG_EXACT(a_Msg) GALOGG_EXACT(GALOG_GROUP, a_Msg)
#define GALOG(a_Msg) GALOGG(GALOG_GROUP, a_Msg)

#define GALOGREL_EXACT(a_Msg) GALOGG_EXACT(GALOG_GROUP_RELEASE, a_Msg)
#define GALOGREL(a_cMax, a_Msg)  do { \
        static uint32_t s_cLogged = 0; \
        if (s_cLogged < (a_cMax)) \
        { \
            ++s_cLogged; \
            GALOGG(GALOG_GROUP_RELEASE, a_Msg); \
        } \
    } while (0)

#define GALOGTEST_EXACT(a_Msg) GALOGG_EXACT(GALOG_GROUP_TEST, a_Msg)
#define GALOGTEST(a_Msg) GALOGG(GALOG_GROUP_TEST, a_Msg)

void *GaMemAlloc(uint32_t cbSize);
void *GaMemAllocZero(uint32_t cbSize);
void GaMemFree(void *pvMem);

NTSTATUS GaIdAlloc(uint32_t *pu32Bits,
                   uint32_t cbBits,
                   uint32_t u32Limit,
                   uint32_t *pu32Id);
NTSTATUS GaIdFree(uint32_t *pu32Bits,
                  uint32_t cbBits,
                  uint32_t u32Limit,
                  uint32_t u32Id);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_gallium_VBoxMPGaUtils_h */
