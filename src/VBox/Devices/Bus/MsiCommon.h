/* $Id: MsiCommon.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Header for MSI/MSI-X support routines.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Bus_MsiCommon_h
#define VBOX_INCLUDED_SRC_Bus_MsiCommon_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

typedef CTX_SUFF(PCPDMPCIHLP) PCPDMPCIHLP;

#ifdef IN_RING3
int      MsiR3Init(PPDMPCIDEV pDev, PPDMMSIREG pMsiReg);
void     MsiR3PciConfigWrite(PPDMDEVINS pDevIns, PCPDMPCIHLP pPciHlp, PPDMPCIDEV pDev, uint32_t u32Address, uint32_t val, unsigned len);
#endif
bool     MsiIsEnabled(PPDMPCIDEV pDev);
void     MsiNotify(PPDMDEVINS pDevIns, PCPDMPCIHLP pPciHlp, PPDMPCIDEV pDev, int iVector, int iLevel, uint32_t uTagSrc);

#ifdef IN_RING3
int      MsixR3Init(PCPDMPCIHLP pPciHlp, PPDMPCIDEV pDev, PPDMMSIREG pMsiReg);
void     MsixR3PciConfigWrite(PPDMDEVINS pDevIns, PCPDMPCIHLP pPciHlp, PPDMPCIDEV pDev, uint32_t u32Address, uint32_t val, unsigned len);
#endif
bool     MsixIsEnabled(PPDMPCIDEV pDev);
void     MsixNotify(PPDMDEVINS pDevIns, PCPDMPCIHLP pPciHlp, PPDMPCIDEV pDev, int iVector, int iLevel, uint32_t uTagSrc);

#endif /* !VBOX_INCLUDED_SRC_Bus_MsiCommon_h */

