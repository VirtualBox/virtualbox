/* $Id: nocrt.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal header for miscellaneous global defs and types.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef IPRT_INCLUDED_INTERNAL_nocrt_h
#define IPRT_INCLUDED_INTERNAL_nocrt_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "internal/iprt.h"
#include <iprt/list.h>


/**
 * No-CRT thread data.
 */
typedef struct RTNOCRTTHREADDATA
{
    /** Used by kAllocType_Heap for DLL unload cleanup. */
    RTLISTNODE      ListEntry;
    /** How this structure was allocated. */
    enum
    {
        /** Invalid zero entry. */
        kAllocType_Invalid = 0,
        /** Embedded in RTTHREADINT.   */
        kAllocType_Embedded,
        /** Preallocated static array. */
        kAllocType_Static,
        /** It's on the heap. */
        kAllocType_Heap,
        /** Cleanup dummy. */
        kAllocType_CleanupDummy,
        /** End of valid values. */
        kAllocType_End
    }               enmAllocType;

    /** errno variable.   */
    int             iErrno;
    /** strtok internal variable. */
    char           *pszStrToken;

} RTNOCRTTHREADDATA;
/** Pointer to no-CRT per-thread data. */
typedef RTNOCRTTHREADDATA *PRTNOCRTTHREADDATA;


extern RTTLS volatile       g_iTlsRtNoCrtPerThread;
extern RTNOCRTTHREADDATA    g_RtNoCrtPerThreadDummy;

PRTNOCRTTHREADDATA rtNoCrtThreadDataGet(void);

#ifdef IN_RING3
void rtNoCrtFatalWriteBegin(const char *pchMsg, size_t cchMsg);
void rtNoCrtFatalWrite(const char *pchMsg, size_t cchMsg);
void rtNoCrtFatalWriteEnd(const char *pchMsg, size_t cchMsg);
void rtNoCrtFatalWriteStr(const char *pszMsg);
void rtNoCrtFatalWritePtr(void const *pv);
void rtNoCrtFatalWriteX64(uint64_t uValue);
void rtNoCrtFatalWriteX32(uint32_t uValue);
void rtNoCrtFatalWriteRc(int rc);
void rtNoCrtFatalWriteWinRc(uint32_t rc);

void rtNoCrtFatalMsg(const char *pchMsg, size_t cchMsg);
void rtNoCrtFatalMsgWithRc(const char *pchMsg, size_t cchMsg, int rc);
#endif


#endif /* !IPRT_INCLUDED_INTERNAL_nocrt_h */

