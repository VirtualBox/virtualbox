/* $Id: parseshell.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Parser, Bourne-like Shell.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_parseshell_h
#define IPRT_INCLUDED_parseshell_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/list.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_parse  RTParse - Generic Parser
 * @{ */


/**
 * IPRT parse node types.
 */
typedef enum RTPARSENODETYPE
{
    RTPARSENODETYPE_INVALID = 0,

    /** Single quoted (sub-)string. */
    RTPARSENODETYPE_STRING_SQ,
    /** Double quoted (sub-)string. */
    RTPARSENODETYPE_STRING_DQ,
    /** Unquoted string. */
    RTPARSENODETYPE_STRING_UQ,

    RTPARSENODETYPE_END
} RTPARSENODETYPE;


/**
 * Type info for an IPRT parser node.
 *
 * This is for simplifying generic processing, dumping and such things.  It may
 * be extended with method pointers if deemed useful/sensible.
 */
typedef struct RTPARSENODETYPEINFO
{
    /** The node type. */
    RTPARSENODETYPE     enmType;
    /** Number of child pointers.   */
    uint8_t             cChildPtrs;
    /** The type name. */
    const char         *pszName;
    /** Pointer to a list of child member names. */
    const char * const *papszChild;
} RTPARSENODETYPEINFO;
/** Pointer (const) to type info for a parser node.  */
typedef RTPARSENODETYPEINFO const *PCRTPARSENODETYPEINFO;


/**
 * Source location for an IPRT parser instance.
 */
typedef struct RTPARSESRCLOC
{
    /** The line number (0-based). */
    RT_GCC_EXTENSION uint64_t   iLine   : 24;
    /** The line offset (0-based). */
    RT_GCC_EXTENSION uint64_t   offLine : 24;
    /** The file index (zero is invalid). */
    RT_GCC_EXTENSION uint64_t   idxFile : 16;
} RTPARSESRCLOC;
/** Pointer to a source location for an IPRT parser instance. */
typedef RTPARSESRCLOC *PRTPARSESRCLOC;
/** Pointer to a const source location for an IPRT parser instance. */
typedef RTPARSESRCLOC const *PCRTPARSESRCLOC;


/** Pointer to a parser base node. */
typedef struct RTPARSENODE RT_FAR *PRTPARSENODE;
/** Pointer to a const parser base node. */
typedef struct RTPARSENODE const RT_FAR *PCRTPARSENODE;
/**
 * IPRT parser base node.
 *
 * All parse three nodes derive from this one.
 *
 * @note If there are children, their list anchors must all follow immediately
 *       after this structure.
 */
typedef struct RTPARSENODE
{
    /** The node type info. */
    PCRTPARSENODETYPEINFO   pType;
    /** Node type specific flags. */
    unsigned                fFlags;
    /** User flags/whatever. */
    unsigned                fUser;
    /** Pointer to user specified data. */
    void                   *pvUser;
    /** Source location. */
    RTPARSESRCLOC           SrcLoc;
    /** The parent node. */
    PRTPARSENODE            pParent;
    /** The sibling list entry (PRTPARSENODE). */
    RTLISTNODE              ListEntry;
} RTPARSENODE;


typedef struct RTPARSENODEBODY
{
    /** The common part. */
    RTPARSENODE     Core;
    /** The children list (PRTPARSENODE). */
    RTLISTANCHOR    Body;
} RTPARSENODEBODY;


typedef struct RTPARSENODEBINARY
{
    /** The common part. */
    RTPARSENODE     Core;
    /** The first child (left side) (PRTPARSENODE). */
    RTLISTANCHOR    Child1;
    /** The second child (right side) (PRTPARSENODE). */
    RTLISTANCHOR    Child2;
} RTPARSENODEBINARY;


/**
 * A string node.
 *
 * This is used wtih
 */
typedef struct RTPARSENODESTRING
{
    /** The common part. */
    RTPARSENODE     Core;
    /** Pointer to the string. */
    const char     *psz;
    /** String length.   */
    size_t          cch;
} RTPARSENODESTRING;

/** @} */

/** @defgroup grp_rt_parse_shell RTParseShell - Bourne-like Shell Parser
 * @{ */

struct RTPARSESHELLINT;
/** Handle to a bourne-like shell parser. */
typedef struct RTPARSESHELLINT *RTPARSESHELL;
/** Pointer to a bourne-like shell parser. */
typedef RTPARSESHELL           *PRTPARSESHELL;
/** NIL handle to a bourne-like shell parser. */
#define NIL_RTPARSESHELL        ((RTPARSESHELL)~(uintptr_t)0)


/** @} */

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_parseshell_h */

