/* $Id: ATAPIPassthrough.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices: ATAPI passthrough helpers (common code for DevATA and DevAHCI).
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Storage_ATAPIPassthrough_h
#define VBOX_INCLUDED_SRC_Storage_ATAPIPassthrough_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/vmm/pdmifs.h>
#include <VBox/vmm/pdmstorageifs.h>

RT_C_DECLS_BEGIN

/**
 * Opaque media track list.
 */
typedef struct TRACKLIST *PTRACKLIST;

DECLHIDDEN(int) ATAPIPassthroughTrackListCreateEmpty(PTRACKLIST *ppTrackList);
DECLHIDDEN(void) ATAPIPassthroughTrackListDestroy(PTRACKLIST pTrackList);
DECLHIDDEN(void) ATAPIPassthroughTrackListClear(PTRACKLIST pTrackList);
DECLHIDDEN(int)  ATAPIPassthroughTrackListUpdate(PTRACKLIST pTrackList, const uint8_t *pbCDB, const void *pvBuf, size_t cbBuf);
DECLHIDDEN(uint32_t) ATAPIPassthroughTrackListGetSectorSizeFromLba(PTRACKLIST pTrackList, uint32_t iAtapiLba);
DECLHIDDEN(bool) ATAPIPassthroughParseCdb(const uint8_t *pbCdb, size_t cbCdb, size_t cbBuf,
                                          PTRACKLIST pTrackList, uint8_t *pbSense, size_t cbSense,
                                          PDMMEDIATXDIR *penmTxDir, size_t *pcbXfer,
                                          size_t *pcbSector, uint8_t *pu8ScsiSts);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Storage_ATAPIPassthrough_h */
