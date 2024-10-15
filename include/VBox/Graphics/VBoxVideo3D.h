/* $Id: VBoxVideo3D.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox 3D common tooling
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_Graphics_VBoxVideo3D_h
#define VBOX_INCLUDED_Graphics_VBoxVideo3D_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/asm.h>
#ifndef VBoxTlsRefGetImpl
# ifdef VBoxTlsRefSetImpl
#  error "VBoxTlsRefSetImpl is defined, unexpected!"
# endif
# include <iprt/thread.h>
# define VBoxTlsRefGetImpl(_tls) (RTTlsGet((RTTLS)(_tls)))
# define VBoxTlsRefSetImpl(_tls, _val) (RTTlsSet((RTTLS)(_tls), (_val)))
#else
# ifndef VBoxTlsRefSetImpl
#  error "VBoxTlsRefSetImpl is NOT defined, unexpected!"
# endif
#endif

#ifndef VBoxTlsRefAssertImpl
# define VBoxTlsRefAssertImpl(_a) do {} while (0)
#endif

typedef DECLCALLBACKTYPE(void, FNVBOXTLSREFDTOR,(void *));
typedef FNVBOXTLSREFDTOR *PFNVBOXTLSREFDTOR;

typedef enum {
    VBOXTLSREFDATA_STATE_UNDEFINED = 0,
    VBOXTLSREFDATA_STATE_INITIALIZED,
    VBOXTLSREFDATA_STATE_TOBE_DESTROYED,
    VBOXTLSREFDATA_STATE_DESTROYING,
    VBOXTLSREFDATA_STATE_32BIT_HACK = 0x7fffffff
} VBOXTLSREFDATA_STATE;

#define VBOXTLSREFDATA \
    volatile int32_t cTlsRefs; \
    VBOXTLSREFDATA_STATE enmTlsRefState; \
    PFNVBOXTLSREFDTOR pfnTlsRefDtor; \

struct VBOXTLSREFDATA_DUMMY
{
    VBOXTLSREFDATA
};

#define VBOXTLSREFDATA_OFFSET(_t) RT_OFFSETOF(_t, cTlsRefs)
#define VBOXTLSREFDATA_ASSERT_OFFSET(_t) RTASSERT_OFFSET_OF(_t, cTlsRefs)
#define VBOXTLSREFDATA_SIZE() (sizeof (struct VBOXTLSREFDATA_DUMMY))
#define VBOXTLSREFDATA_COPY(_pDst, _pSrc) do { \
        (_pDst)->cTlsRefs = (_pSrc)->cTlsRefs; \
        (_pDst)->enmTlsRefState = (_pSrc)->enmTlsRefState; \
        (_pDst)->pfnTlsRefDtor = (_pSrc)->pfnTlsRefDtor; \
    } while (0)

#define VBOXTLSREFDATA_EQUAL(_pDst, _pSrc) ( \
           (_pDst)->cTlsRefs == (_pSrc)->cTlsRefs \
        && (_pDst)->enmTlsRefState == (_pSrc)->enmTlsRefState \
        && (_pDst)->pfnTlsRefDtor == (_pSrc)->pfnTlsRefDtor \
    )


#define VBoxTlsRefInit(_p, _pfnDtor) do { \
        (_p)->cTlsRefs = 1; \
        (_p)->enmTlsRefState = VBOXTLSREFDATA_STATE_INITIALIZED; \
        (_p)->pfnTlsRefDtor = (_pfnDtor); \
    } while (0)

#define VBoxTlsRefIsFunctional(_p) (!!((_p)->enmTlsRefState == VBOXTLSREFDATA_STATE_INITIALIZED))

#define VBoxTlsRefAddRef(_p) do { \
        int cRefs = ASMAtomicIncS32(&(_p)->cTlsRefs); \
        VBoxTlsRefAssertImpl(cRefs > 1 || (_p)->enmTlsRefState == VBOXTLSREFDATA_STATE_DESTROYING); \
        RT_NOREF(cRefs); \
    } while (0)

#define VBoxTlsRefCountGet(_p) (ASMAtomicReadS32(&(_p)->cTlsRefs))

#define VBoxTlsRefRelease(_p) do { \
        int cRefs = ASMAtomicDecS32(&(_p)->cTlsRefs); \
        VBoxTlsRefAssertImpl(cRefs >= 0); \
        if (!cRefs && (_p)->enmTlsRefState != VBOXTLSREFDATA_STATE_DESTROYING /* <- avoid recursion if VBoxTlsRefAddRef/Release is called from dtor */) { \
            (_p)->enmTlsRefState = VBOXTLSREFDATA_STATE_DESTROYING; \
            (_p)->pfnTlsRefDtor((_p)); \
        } \
    } while (0)

#define VBoxTlsRefMarkDestroy(_p) do { \
        (_p)->enmTlsRefState = VBOXTLSREFDATA_STATE_TOBE_DESTROYED; \
    } while (0)

#define VBoxTlsRefGetCurrent(_t, _Tsd) ((_t*) VBoxTlsRefGetImpl((_Tsd)))

#define VBoxTlsRefGetCurrentFunctional(_val, _t, _Tsd) do { \
       _t * cur = VBoxTlsRefGetCurrent(_t, _Tsd); \
       if (!cur || VBoxTlsRefIsFunctional(cur)) { \
           (_val) = cur; \
       } else { \
           VBoxTlsRefSetCurrent(_t, _Tsd, NULL); \
           (_val) = NULL; \
       } \
   } while (0)

#define VBoxTlsRefSetCurrent(_t, _Tsd, _p) do { \
        _t * oldCur = VBoxTlsRefGetCurrent(_t, _Tsd); \
        if (oldCur != (_p)) { \
            VBoxTlsRefSetImpl((_Tsd), (_p)); \
            if (oldCur) { \
                VBoxTlsRefRelease(oldCur); \
            } \
            if ((_p)) { \
                VBoxTlsRefAddRef((_t*)(_p)); \
            } \
        } \
    } while (0)


/* host 3D->Fe[/Qt] notification mechanism defines */
typedef enum
{
    VBOX3D_NOTIFY_TYPE_TEST_FUNCTIONAL = 3,
    VBOX3D_NOTIFY_TYPE_3DDATA_VISIBLE  = 4,
    VBOX3D_NOTIFY_TYPE_3DDATA_HIDDEN   = 5,

    VBOX3D_NOTIFY_TYPE_HW_SCREEN_FIRST        = 100,
    VBOX3D_NOTIFY_TYPE_HW_SCREEN_IS_SUPPORTED = 100,
    VBOX3D_NOTIFY_TYPE_HW_SCREEN_CREATED      = 101,
    VBOX3D_NOTIFY_TYPE_HW_SCREEN_DESTROYED    = 102,
    VBOX3D_NOTIFY_TYPE_HW_SCREEN_UPDATE_BEGIN = 103,
    VBOX3D_NOTIFY_TYPE_HW_SCREEN_UPDATE_END   = 104,
    VBOX3D_NOTIFY_TYPE_HW_SCREEN_BIND_SURFACE = 105,
    VBOX3D_NOTIFY_TYPE_HW_SCREEN_LAST         = 105,

    VBOX3D_NOTIFY_TYPE_HW_OVERLAY_CREATED   = 200,
    VBOX3D_NOTIFY_TYPE_HW_OVERLAY_DESTROYED = 201,
    VBOX3D_NOTIFY_TYPE_HW_OVERLAY_GET_ID    = 202,

    VBOX3D_NOTIFY_TYPE_32BIT_HACK = 0x7fffffff
} VBOX3D_NOTIFY_TYPE;

typedef struct VBOX3DNOTIFY
{
    VBOX3D_NOTIFY_TYPE enmNotification;
    int32_t  iDisplay;
    uint32_t u32Reserved;
    uint32_t cbData;
    uint8_t  au8Data[sizeof(uint64_t)];
} VBOX3DNOTIFY;

#endif /* !VBOX_INCLUDED_Graphics_VBoxVideo3D_h */
