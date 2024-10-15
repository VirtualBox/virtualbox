/* $Id: fileaio.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Internal RTFileAio header.
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

#ifndef IPRT_INCLUDED_INTERNAL_fileaio_h
#define IPRT_INCLUDED_INTERNAL_fileaio_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/file.h>
#include "internal/magics.h"

/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * Defined request states.
 */
typedef enum RTFILEAIOREQSTATE
{
    /** Prepared. */
    RTFILEAIOREQSTATE_PREPARED = 0,
    /** Submitted. */
    RTFILEAIOREQSTATE_SUBMITTED,
    /** Completed. */
    RTFILEAIOREQSTATE_COMPLETED,
    /** Omni present 32bit hack. */
    RTFILEAIOREQSTATE_32BIT_HACK = 0x7fffffff
} RTFILEAIOREQSTATE;

/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/

/** Return true if the specified request is not valid, false otherwise. */
#define RTFILEAIOREQ_IS_NOT_VALID(pReq) \
    (RT_UNLIKELY(!RT_VALID_PTR(pReq) || (pReq->u32Magic != RTFILEAIOREQ_MAGIC)))

/** Validates a context handle and returns VERR_INVALID_HANDLE if not valid. */
#define RTFILEAIOREQ_VALID_RETURN_RC(pReq, rc) \
    do { \
        AssertPtrReturn((pReq), (rc)); \
        AssertReturn((pReq)->u32Magic == RTFILEAIOREQ_MAGIC, (rc)); \
    } while (0)

/** Validates a context handle and returns VERR_INVALID_HANDLE if not valid. */
#define RTFILEAIOREQ_VALID_RETURN(pReq) RTFILEAIOREQ_VALID_RETURN_RC((pReq), VERR_INVALID_HANDLE)

/** Validates a context handle and returns (void) if not valid. */
#define RTFILEAIOREQ_VALID_RETURN_VOID(pReq) \
    do { \
        AssertPtrReturnVoid(pReq); \
        AssertReturnVoid((pReq)->u32Magic == RTFILEAIOREQ_MAGIC); \
    } while (0)

/** Validates a context handle and returns the specified rc if not valid. */
#define RTFILEAIOCTX_VALID_RETURN_RC(pCtx, rc) \
    do { \
        AssertPtrReturn((pCtx), (rc)); \
        AssertReturn((pCtx)->u32Magic == RTFILEAIOCTX_MAGIC, (rc)); \
    } while (0)

/** Validates a context handle and returns VERR_INVALID_HANDLE if not valid. */
#define RTFILEAIOCTX_VALID_RETURN(pCtx) RTFILEAIOCTX_VALID_RETURN_RC((pCtx), VERR_INVALID_HANDLE)

/** Checks if a request is in the specified state and returns the specified rc if not. */
#define RTFILEAIOREQ_STATE_RETURN_RC(pReq, State, rc) \
    do { \
        if (RT_UNLIKELY(pReq->enmState != RTFILEAIOREQSTATE_##State)) \
            return rc; \
    } while (0)

/** Checks if a request is not in the specified state and returns the specified rc if it is. */
#define RTFILEAIOREQ_NOT_STATE_RETURN_RC(pReq, State, rc) \
    do { \
        if (RT_UNLIKELY(pReq->enmState == RTFILEAIOREQSTATE_##State)) \
            return rc; \
    } while (0)

/** Checks if a request in the given states and sserts if not. */
#define RTFIELAIOREQ_ASSERT_STATE(pReq, State) \
    do { \
        AssertPtr((pReq)); \
        Assert((pReq)->u32Magic == RTFILEAIOREQ_MAGIC); \
        Assert((pReq)->enmState == RTFILEAIOREQSTATE_##State); \
    } while (0)

/** Sets the request into a specific state. */
#define RTFILEAIOREQ_SET_STATE(pReq, State) \
    do { \
        pReq->enmState = RTFILEAIOREQSTATE_##State; \
    } while (0)


RT_C_DECLS_BEGIN

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_INTERNAL_fileaio_h */

