/* $Id: HGCMObjects.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * HGCMObjects - Host-Guest Communication Manager objects header.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_HGCMObjects_h
#define MAIN_INCLUDED_HGCMObjects_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assert.h>
#include <iprt/avl.h>
#include <iprt/critsect.h>
#include <iprt/asm.h>

class HGCMObject;

typedef struct ObjectAVLCore
{
    AVLU32NODECORE AvlCore;
    HGCMObject *pSelf;
} ObjectAVLCore;

typedef enum
{
    HGCMOBJ_CLIENT,
    HGCMOBJ_THREAD,
    HGCMOBJ_MSG,
    HGCMOBJ_SizeHack   = 0x7fffffff
} HGCMOBJ_TYPE;


/**
 * A referenced object.
 */
class HGCMReferencedObject
{
    private:
        int32_t volatile m_cRefs;
        HGCMOBJ_TYPE     m_enmObjType;

    protected:
        virtual ~HGCMReferencedObject()
        {}

    public:
        HGCMReferencedObject(HGCMOBJ_TYPE enmObjType)
            : m_cRefs(0)   /** @todo change to 1! */
            , m_enmObjType(enmObjType)
        {}

        void Reference()
        {
            int32_t cRefs = ASMAtomicIncS32(&m_cRefs);
            NOREF(cRefs);
            Log(("Reference(%p/%d): cRefs = %d\n", this, m_enmObjType, cRefs));
        }

        void Dereference()
        {
            int32_t cRefs = ASMAtomicDecS32(&m_cRefs);
            Log(("Dereference(%p/%d): cRefs = %d \n", this, m_enmObjType, cRefs));
            AssertRelease(cRefs >= 0);

            if (cRefs)
            { /* likely */ }
            else
                delete this;
        }

        HGCMOBJ_TYPE Type()
        {
            return m_enmObjType;
        }
};


class HGCMObject : public HGCMReferencedObject
{
    private:
        friend uint32_t hgcmObjMake(HGCMObject *pObject, uint32_t u32HandleIn);

        ObjectAVLCore   m_core;

    protected:
        virtual ~HGCMObject()
        {}

    public:
        HGCMObject(HGCMOBJ_TYPE enmObjType)
            : HGCMReferencedObject(enmObjType)
        {}

        uint32_t Handle()
        {
            return (uint32_t)m_core.AvlCore.Key;
        }
};

int         hgcmObjInit();
void        hgcmObjUninit();

uint32_t    hgcmObjGenerateHandle(HGCMObject *pObject);
uint32_t    hgcmObjAssignHandle(HGCMObject *pObject, uint32_t u32Handle);

void        hgcmObjDeleteHandle(uint32_t handle);

HGCMObject *hgcmObjReference(uint32_t handle, HGCMOBJ_TYPE enmObjType);
void        hgcmObjDereference(HGCMObject *pObject);

uint32_t    hgcmObjQueryHandleCount();
void        hgcmObjSetHandleCount(uint32_t u32HandleCount);


#endif /* !MAIN_INCLUDED_HGCMObjects_h */
