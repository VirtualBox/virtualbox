/* $Id: scriptbase.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTScript, Script language support in IPRT.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_scriptbase_h
#define IPRT_INCLUDED_scriptbase_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>

RT_C_DECLS_BEGIN


/**
 * Position information.
 */
typedef struct RTSCRIPTPOS
{
    /** Line in the source. */
    unsigned       iLine;
    /** Character index.*/
    unsigned       iCh;
} RTSCRIPTPOS;
/** Pointer to a source position. */
typedef struct RTSCRIPTPOS *PRTSCRIPTPOS;
/** Pointer to a constant source position. */
typedef const RTSCRIPTPOS *PCRTSCRIPTPOS;


#if 0 /* Later, maybe */
/**
 * Native data types.
 */
typedef enum RTSCRIPTNATIVETYPE
{
    /** Invalid data type. */
    RTSCRIPTNATIVETYPE_INVALID = 0,
    /** Void type. */
    RTSCRIPTNATIVETYPE_VOID    = RTSCRIPTNATIVETYPE_ASSEMBLE(RTSCRIPTNATIVETYPE_UNSIGNED, RTSCRIPTNATIVETYPE_CLASS_INTEGER, 0),
    /** Signed 8bit integer. */
    RTSCRIPTNATIVETYPE_INT8    = RTSCRIPTNATIVETYPE_ASSEMBLE(RTSCRIPTNATIVETYPE_SIGNED, RTSCRIPTNATIVETYPE_CLASS_INTEGER, 8),
    /** Unsigned 8bit integer. */
    RTSCRIPTNATIVETYPE_UINT8   = RTSCRIPTNATIVETYPE_ASSEMBLE(RTSCRIPTNATIVETYPE_UNSIGNED, RTSCRIPTNATIVETYPE_CLASS_INTEGER, 8),
    /** Signed 16bit integer. */
    RTSCRIPTNATIVETYPE_INT16   = RTSCRIPTNATIVETYPE_ASSEMBLE(RTSCRIPTNATIVETYPE_SIGNED, RTSCRIPTNATIVETYPE_CLASS_INTEGER, 16),
    /** Unsigned 16bit integer. */
    RTSCRIPTNATIVETYPE_UINT16  = RTSCRIPTNATIVETYPE_ASSEMBLE(RTSCRIPTNATIVETYPE_UNSIGNED, RTSCRIPTNATIVETYPE_CLASS_INTEGER, 16),
    /** Signed 32bit integer. */
    RTSCRIPTNATIVETYPE_INT32   = RTSCRIPTNATIVETYPE_ASSEMBLE(RTSCRIPTNATIVETYPE_SIGNED, RTSCRIPTNATIVETYPE_CLASS_INTEGER, 32),
    /** Unsigned 32bit integer. */
    RTSCRIPTNATIVETYPE_UINT32  = RTSCRIPTNATIVETYPE_ASSEMBLE(RTSCRIPTNATIVETYPE_UNSIGNED, RTSCRIPTNATIVETYPE_CLASS_INTEGER, 32),
    /** Signed 64bit integer. */
    RTSCRIPTNATIVETYPE_INT64   = RTSCRIPTNATIVETYPE_ASSEMBLE(RTSCRIPTNATIVETYPE_SIGNED, RTSCRIPTNATIVETYPE_CLASS_INTEGER, 64),
    /** Unsigned 64bit integer. */
    RTSCRIPTNATIVETYPE_UINT64  = RTSCRIPTNATIVETYPE_ASSEMBLE(RTSCRIPTNATIVETYPE_UNSIGNED, RTSCRIPTNATIVETYPE_CLASS_INTEGER, 64),
    /** 32bit floating point. */
    RTSCRIPTNATIVETYPE_FLOAT32 = RTSCRIPTNATIVETYPE_ASSEMBLE(RTSCRIPTNATIVETYPE_SIGNED, RTSCRIPTNATIVETYPE_CLASS_FLOAT, 32),
    /** 64bit floating point. */
    RTSCRIPTNATIVETYPE_FLOAT64 = RTSCRIPTNATIVETYPE_ASSEMBLE(RTSCRIPTNATIVETYPE_SIGNED, RTSCRIPTNATIVETYPE_CLASS_FLOAT, 64),
    /** Pointer type. */
    RTSCRIPTNATIVETYPE_PTR     = RTSCRIPTNATIVETYPE_ASSEMBLE(RTSCRIPTNATIVETYPE_UNSIGNED, RTSCRIPTNATIVETYPE_CLASS_POINTER, 64),
    /** 32bit hack. */
    RTSCRIPTNATIVETYPE_32BIT_HACK = 0x7fffffff
} RTSCRIPTNATIVETYPE;

#define RTSCRIPTNATIVETYPE_ENTRIES

/**
 * Data value.
 */
typedef struct RTSCRIPTVAL
{
    /** The value type. */
    RTSCRIPTNATIVETYPE    enmType;
    /** Value, dependent on type. */
    union
    {
        int8_t           i8;
        uint8_t          u8;
        int16_t          i16;
        uint16_t         u16;
        int32_t          i32;
        uint32_t         u32;
        int64_t          i64;
        uint64_t         u64;
        RTFLOAT64U       r64;
    } u;
} RTSCRIPTVAL;
/** Pointer to a value. */
typedef RTSCRIPTVAL *PRTSCRIPTVAL;
/** Pointer to a const value. */
typedef const RTSCRIPTVAL *PCRTSCRIPTVAL;
#endif


RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_scriptbase_h */
