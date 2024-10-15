/* $Id: DisplayUtils.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Display helper declarations
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

#ifndef MAIN_INCLUDED_DisplayUtils_h
#define MAIN_INCLUDED_DisplayUtils_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBox/com/string.h"

#include "CryptoUtils.h"

using namespace com;

#define sSSMDisplayScreenshotVer 0x00010001
#define sSSMDisplayVer 0x00010001
#define sSSMDisplayVer2 0x00010002
#define sSSMDisplayVer3 0x00010003
#define sSSMDisplayVer4 0x00010004
#define sSSMDisplayVer5 0x00010005

int readSavedGuestScreenInfo(SsmStream &ssmStream, const Utf8Str &strStateFilePath,
                             uint32_t u32ScreenId, uint32_t *pu32OriginX, uint32_t *pu32OriginY,
                             uint32_t *pu32Width, uint32_t *pu32Height, uint16_t *pu16Flags);

int readSavedDisplayScreenshot(SsmStream &ssmStream, const Utf8Str &strStateFilePath,
                               uint32_t u32Type, uint8_t **ppu8Data, uint32_t *pcbData,
                               uint32_t *pu32Width, uint32_t *pu32Height);
void freeSavedDisplayScreenshot(uint8_t *pu8Data);

#endif /* !MAIN_INCLUDED_DisplayUtils_h */

