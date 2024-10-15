/* $Id: DevFwCommon.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * FwCommon - Shared firmware code, header.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_PC_DevFwCommon_h
#define VBOX_INCLUDED_SRC_PC_DevFwCommon_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "DevPcBios.h"

/* Plant DMI table */
int FwCommonPlantDMITable(PPDMDEVINS pDevIns, uint8_t *pTable, unsigned cbMax, PCRTUUID pUuid, PCFGMNODE pCfg, uint16_t cCpus,
                          uint16_t *pcbDmiTables, uint16_t *pcDmiTables, bool fUefi);
void FwCommonPlantSmbiosAndDmiHdrs(PPDMDEVINS pDevIns, uint8_t *pHdr, uint16_t cbDmiTables, uint16_t cNumDmiTables);

/* Plant MPS table */
void FwCommonPlantMpsTable(PPDMDEVINS pDevIns, uint8_t *pTable, unsigned cbMax, uint16_t cCpus);
void FwCommonPlantMpsFloatPtr(PPDMDEVINS pDevIns, uint32_t u32MpTableAddr);

#endif /* !VBOX_INCLUDED_SRC_PC_DevFwCommon_h */
