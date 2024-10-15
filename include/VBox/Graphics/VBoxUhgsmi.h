/* $Id: VBoxUhgsmi.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Document me, pretty please.
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

#ifndef VBOX_INCLUDED_Graphics_VBoxUhgsmi_h
#define VBOX_INCLUDED_Graphics_VBoxUhgsmi_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>

typedef struct VBOXUHGSMI *PVBOXUHGSMI;

typedef struct VBOXUHGSMI_BUFFER *PVBOXUHGSMI_BUFFER;

typedef union VBOXUHGSMI_BUFFER_TYPE_FLAGS
{
    uint32_t Value;
    struct
    {
        uint32_t fCommand       : 1;
        uint32_t Reserved       : 31;
    } s;
} VBOXUHGSMI_BUFFER_TYPE_FLAGS;

typedef union VBOXUHGSMI_BUFFER_LOCK_FLAGS
{
    uint32_t Value;
    struct
    {
        uint32_t fReadOnly      : 1;
        uint32_t fWriteOnly     : 1;
        uint32_t fDonotWait     : 1;
        uint32_t fDiscard       : 1;
        uint32_t fLockEntire    : 1;
        uint32_t Reserved       : 27;
    } s;
} VBOXUHGSMI_BUFFER_LOCK_FLAGS;

typedef union VBOXUHGSMI_BUFFER_SUBMIT_FLAGS
{
    uint32_t Value;
    struct
    {
        uint32_t fHostReadOnly  : 1;
        uint32_t fHostWriteOnly : 1;
        uint32_t fDoNotRetire   : 1; /**< the buffer will be used in a subsequent command */
        uint32_t fEntireBuffer  : 1;
        uint32_t Reserved       : 28;
    } s;
} VBOXUHGSMI_BUFFER_SUBMIT_FLAGS, *PVBOXUHGSMI_BUFFER_SUBMIT_FLAGS;

/* the caller can specify NULL as a hSynch and specify a valid enmSynchType to make UHGSMI create a proper object itself,
 *  */
typedef DECLCALLBACKTYPE(int, FNVBOXUHGSMI_BUFFER_CREATE,(PVBOXUHGSMI pHgsmi, uint32_t cbBuf, VBOXUHGSMI_BUFFER_TYPE_FLAGS fType,
                                                          PVBOXUHGSMI_BUFFER* ppBuf));
typedef FNVBOXUHGSMI_BUFFER_CREATE *PFNVBOXUHGSMI_BUFFER_CREATE;

typedef struct VBOXUHGSMI_BUFFER_SUBMIT
{
    PVBOXUHGSMI_BUFFER pBuf;
    uint32_t offData;
    uint32_t cbData;
    VBOXUHGSMI_BUFFER_SUBMIT_FLAGS fFlags;
} VBOXUHGSMI_BUFFER_SUBMIT, *PVBOXUHGSMI_BUFFER_SUBMIT;

typedef DECLCALLBACKTYPE(int, FNVBOXUHGSMI_BUFFER_SUBMIT,(PVBOXUHGSMI pHgsmi, PVBOXUHGSMI_BUFFER_SUBMIT aBuffers,
                                                          uint32_t cBuffers));
typedef FNVBOXUHGSMI_BUFFER_SUBMIT *PFNVBOXUHGSMI_BUFFER_SUBMIT;

typedef DECLCALLBACKTYPE(int, FNVBOXUHGSMI_BUFFER_DESTROY,(PVBOXUHGSMI_BUFFER pBuf));
typedef FNVBOXUHGSMI_BUFFER_DESTROY *PFNVBOXUHGSMI_BUFFER_DESTROY;

typedef DECLCALLBACKTYPE(int, FNVBOXUHGSMI_BUFFER_LOCK,(PVBOXUHGSMI_BUFFER pBuf, uint32_t offLock, uint32_t cbLock,
                                                        VBOXUHGSMI_BUFFER_LOCK_FLAGS fFlags, void**pvLock));
typedef FNVBOXUHGSMI_BUFFER_LOCK *PFNVBOXUHGSMI_BUFFER_LOCK;

typedef DECLCALLBACKTYPE(int, FNVBOXUHGSMI_BUFFER_UNLOCK,(PVBOXUHGSMI_BUFFER pBuf));
typedef FNVBOXUHGSMI_BUFFER_UNLOCK *PFNVBOXUHGSMI_BUFFER_UNLOCK;

typedef struct VBOXUHGSMI
{
    PFNVBOXUHGSMI_BUFFER_CREATE pfnBufferCreate;
    PFNVBOXUHGSMI_BUFFER_SUBMIT pfnBufferSubmit;
    /** User custom data. */
    void *pvUserData;
} VBOXUHGSMI;

typedef struct VBOXUHGSMI_BUFFER
{
    PFNVBOXUHGSMI_BUFFER_LOCK pfnLock;
    PFNVBOXUHGSMI_BUFFER_UNLOCK pfnUnlock;
    PFNVBOXUHGSMI_BUFFER_DESTROY pfnDestroy;

    /* r/o data added for ease of access and simplicity
     * modifying it leads to unpredictable behavior */
    VBOXUHGSMI_BUFFER_TYPE_FLAGS fType;
    uint32_t cbBuffer;
    /** User custom data. */
    void *pvUserData;
} VBOXUHGSMI_BUFFER;

#define VBoxUhgsmiBufferCreate(_pUhgsmi, _cbBuf, _fType, _ppBuf) ((_pUhgsmi)->pfnBufferCreate(_pUhgsmi, _cbBuf, _fType, _ppBuf))
#define VBoxUhgsmiBufferSubmit(_pUhgsmi, _aBuffers, _cBuffers) ((_pUhgsmi)->pfnBufferSubmit(_pUhgsmi, _aBuffers, _cBuffers))

#define VBoxUhgsmiBufferLock(_pBuf, _offLock, _cbLock, _fFlags, _pvLock) ((_pBuf)->pfnLock(_pBuf, _offLock, _cbLock, _fFlags, _pvLock))
#define VBoxUhgsmiBufferUnlock(_pBuf) ((_pBuf)->pfnUnlock(_pBuf))
#define VBoxUhgsmiBufferDestroy(_pBuf) ((_pBuf)->pfnDestroy(_pBuf))

#endif /* !VBOX_INCLUDED_Graphics_VBoxUhgsmi_h */

