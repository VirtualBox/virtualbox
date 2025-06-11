/* $Id: RecordingUtils.h 109818 2025-06-11 08:31:50Z andreas.loeffler@oracle.com $ */
/** @file
 * Recording utility header.
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

#ifndef MAIN_INCLUDED_RecordingUtils_h
#define MAIN_INCLUDED_RecordingUtils_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "RecordingInternals.h"


void RecordingUtilsConvBGRA32ToYUVI420(uint8_t *paDst, uint32_t uDstWidth, uint32_t uDstHeight,
                                       uint8_t *paSrc, uint32_t uSrcWidth, uint32_t uSrcHeight);
void RecordingUtilsConvBGRA32ToYUVI420Ex(uint8_t *paDst, uint32_t dx, uint32_t dy, uint32_t uDstWidth, uint32_t uDstHeight,
                                         uint8_t *paSrc, uint32_t sx, uint32_t sy, uint32_t uSrcWidth, uint32_t uSrcHeight, uint32_t uSrcStride, uint8_t uBPP);
int RecordingUtilsCoordsCropCenter(PRECORDINGCODECPARMS pCodecParms, int32_t *sx, int32_t *sy, int32_t *sw, int32_t *sh, int32_t *dx, int32_t *dy);
const char *RecordingUtilsRecordingFrameTypeToStr(RECORDINGFRAME_TYPE enmType);

#ifdef DEBUG
int RecordingUtilsDbgDumpImageData(const uint8_t *pu8RGBBuf, size_t cbRGBBuf, const char *pszPath, const char *pszWhat, uint32_t uX, uint32_t uY, uint32_t uWidth, uint32_t uHeight, uint32_t uBytesPerLine, uint8_t uBPP);
int RecordingUtilsDbgDumpVideoFrameEx(const PRECORDINGVIDEOFRAME pFrame, const char *pszPath, const char *pszWhat);
int RecordingUtilsDbgDumpVideoFrame(const PRECORDINGVIDEOFRAME pFrame, const char *pszWhat);
void RecordingUtilsDbgLogFrame(PRECORDINGFRAME pFrame);
#endif

#endif /* !MAIN_INCLUDED_RecordingUtils_h */

