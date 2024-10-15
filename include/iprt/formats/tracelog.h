/* $Id: tracelog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, Binary trace log format.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_formats_tracelog_h
#define IPRT_INCLUDED_formats_tracelog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assert.h>
#include <iprt/cdefs.h>
#include <iprt/types.h>


/** @defgroup grp_rt_formats_tracelog    Binary trace log structures and definitions
 * @ingroup grp_rt_formats
 * @{
 */

/** Size of the record magic in bytes. */
#define TRACELOG_MAGIC_SZ      8

/**
 * Trace log identification and options header.
 */
typedef struct TRACELOGHDR
{
    /** Identifiaction magic. */
    uint8_t                     szMagic[8];
    /** Endianess indicator. */
    uint32_t                    u32Endianess;
    /** File version indicator. */
    uint32_t                    u32Version;
    /** File flags (MBZ for now). */
    uint32_t                    fFlags;
    /** Size of the trace log description in bytes following this header. */
    uint32_t                    cbStrDesc;
    /** Size of a pointer item in bytes. */
    uint8_t                     cbTypePtr;
    /** size of the size_t item in bytes. */
    uint8_t                     cbTypeSize;
    /** Padding to an 4 byte boundary. */
    uint16_t                    u16Reserved0;
    /** Padding to an 8 byte boundary. */
    uint32_t                    u32Reserved0;
    /** Starting timestamp when the log was initialised. */
    uint64_t                    u64TsStart;
    /** Padding to 64byte boundary, reserved for future use. */
    uint64_t                    au64Reserved[3];
} TRACELOGHDR;
AssertCompileSize(TRACELOGHDR, 64);
/** Pointer to a trace log header. */
typedef TRACELOGHDR *PTRACELOGHDR;
/** Pointer to a const trace log header. */
typedef const TRACELOGHDR *PCTRACELOGHDR;

/** Magic value for a trace log file (TRACELOG backwards). */
#define TRACELOG_HDR_MAGIC     "GOLECART"
/** Endianess indicator. */
#define TRACELOG_HDR_ENDIANESS 0xdeadc0de
/** The default version (Higher 16bits major, low 16bits minor version). */
#define TRACELOG_VERSION       RT_MAKE_U32(1, 0)


/**
 * Trace log event structure descriptor.
 */
typedef struct TRACELOGEVTDESC
{
    /** Event descriptor magic. */
    uint8_t                     szMagic[8];
    /** Event structure descriptor ID for identification in events later. */
    uint32_t                    u32Id;
    /** Severity class of the event .*/
    uint32_t                    u32Severity;
    /** Size of the identifier string in bytes without terminator. */
    uint32_t                    cbStrId;
    /** Size of the description string in bytes without terminator. */
    uint32_t                    cbStrDesc;
    /** Number of event items following. */
    uint32_t                    cEvtItems;
    /** Padding to end the descriptor on a 32 byte boundary. */
    uint32_t                    au32Padding0;
} TRACELOGEVTDESC;
AssertCompileSize(TRACELOGEVTDESC, 32);
/** Pointer to a trace log event structure descriptor. */
typedef TRACELOGEVTDESC *PTRACELOGEVTDESC;
/** Pointer to a const trace log event structure descriptor. */
typedef const TRACELOGEVTDESC *PCTRACELOGEVTDESC;

/** Event descriptor magic. */
#define TRACELOG_EVTDESC_MAGIC "\0CSEDTVE"

/** Severity: Informational event*/
#define TRACELOG_EVTDESC_SEVERITY_INFO    UINT32_C(0)
/** Severity: Warning event*/
#define TRACELOG_EVTDESC_SEVERITY_WARNING UINT32_C(1)
/** Severity: Error event*/
#define TRACELOG_EVTDESC_SEVERITY_ERROR   UINT32_C(2)
/** Severity: Fatal event*/
#define TRACELOG_EVTDESC_SEVERITY_FATAL   UINT32_C(3)
/** Severity: Debug event*/
#define TRACELOG_EVTDESC_SEVERITY_DEBUG   UINT32_C(4)


/**
 * Trace log event item descriptor.
 */
typedef struct TRACELOGEVTITEMDESC
{
    /** Event item descriptor magic. */
    uint8_t                     szMagic[8];
    /** Size of the item name string in bytes without terminator. */
    uint32_t                    cbStrName;
    /** Size of the optional description string in bytes without terminator. */
    uint32_t                    cbStrDesc;
    /** Item type */
    uint32_t                    u32Type;
    /** Size of the raw data type if static throughout. */
    uint32_t                    cbRawData;
    /** Padding to end the descriptor on a 32 byte boundary. */
    uint32_t                    au32Padding0[2];
} TRACELOGEVTITEMDESC;
AssertCompileSize(TRACELOGEVTITEMDESC, 32);
/** Pointer to a trace log event item descriptor. */
typedef TRACELOGEVTITEMDESC *PTRACELOGEVTITEMDESC;
/** Pointer to a const trace log event item descriptor. */
typedef const TRACELOGEVTITEMDESC *PCTRACELOGEVTITEMDESC;

/** Event item descriptor magic. */
#define TRACELOG_EVTITEMDESC_MAGIC "CSEDMETI"
/** Boolean type. */
#define TRACELOG_EVTITEMDESC_TYPE_BOOL      UINT32_C(1)
/** Unsigned 8bit integer type. */
#define TRACELOG_EVTITEMDESC_TYPE_UINT8     UINT32_C(2)
/** Signed 8bit integer type. */
#define TRACELOG_EVTITEMDESC_TYPE_INT8      UINT32_C(3)
/** Unsigned 16bit integer type. */
#define TRACELOG_EVTITEMDESC_TYPE_UINT16    UINT32_C(4)
/** Signed 16bit integer type. */
#define TRACELOG_EVTITEMDESC_TYPE_INT16     UINT32_C(5)
/** Unsigned 32bit integer type. */
#define TRACELOG_EVTITEMDESC_TYPE_UINT32    UINT32_C(6)
/** Signed 32bit integer type. */
#define TRACELOG_EVTITEMDESC_TYPE_INT32     UINT32_C(7)
/** Unsigned 64bit integer type. */
#define TRACELOG_EVTITEMDESC_TYPE_UINT64    UINT32_C(8)
/** Signed 64bit integer type. */
#define TRACELOG_EVTITEMDESC_TYPE_INT64     UINT32_C(9)
/** 32bit floating point type. */
#define TRACELOG_EVTITEMDESC_TYPE_FLOAT32   UINT32_C(10)
/** 64bit floating point type. */
#define TRACELOG_EVTITEMDESC_TYPE_FLOAT64   UINT32_C(11)
/** Raw binary data type. */
#define TRACELOG_EVTITEMDESC_TYPE_RAWDATA   UINT32_C(12)
/** Pointer data type. */
#define TRACELOG_EVTITEMDESC_TYPE_POINTER   UINT32_C(13)
/** size_t data type. */
#define TRACELOG_EVTITEMDESC_TYPE_SIZE      UINT32_C(14)

/**
 * Trace log event marker.
 */
typedef struct TRACELOGEVT
{
    /** Event marker magic. */
    uint8_t                     szMagic[8];
    /** Trace log sequence number to identify the event uniquely. */
    uint64_t                    u64SeqNo;
    /** Timestamp for the marker (resolution is infered from the header). */
    uint64_t                    u64Ts;
    /** Event group ID for grouping different events together - for no grouped event. */
    uint64_t                    u64EvtGrpId;
    /** Parent group ID this event originated from. */
    uint64_t                    u64EvtParentGrpId;
    /** Overall number of bytes for the event data following including static and possibly variable data. */
    uint32_t                    cbEvtData;
    /** Number of size_t sized raw data size indicators before the raw event data follows. */
    uint32_t                    cRawEvtDataSz;
    /** Event flags. */
    uint32_t                    fFlags;
    /** Event structure descriptor ID to use for structuring the event data. */
    uint32_t                    u32EvtDescId;
    /** Reserved for future use. */
    uint64_t                    u64Reserved0;
} TRACELOGEVT;
AssertCompileSize(TRACELOGEVT, 64);
/** Pointer to a trace log event marker. */
typedef TRACELOGEVT *PTRACELOGEVT;
/** Pointer to a const trace log event marker. */
typedef const TRACELOGEVT *PCTRACELOGEVT;

/** Event marker descriptor magic. */
#define TRACELOG_EVT_MAGIC       "\0RKRMTVE"
/** Flag indicating this is the start of an event group and all subsequent events
 * with the same group ID belong to the same group. */
#define TRACELOG_EVT_F_GRP_START RT_BIT_32(0)
/** Flag indicating this is the end of an event group which was started earlier. */
#define TRACELOG_EVT_F_GRP_END   RT_BIT_32(1)
/** Combination of valid flags. */
#define TRACELOG_EVT_F_VALID     (TRACELOG_EVT_F_GRP_START | TRACELOG_EVT_F_GRP_END)

/** @} */

#endif /* !IPRT_INCLUDED_formats_tracelog_h */

