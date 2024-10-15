/* $Id: shared_ptr.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Simplified shared pointer.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_NetLib_shared_ptr_h
#define VBOX_INCLUDED_SRC_NetLib_shared_ptr_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef __cplusplus
template<typename T>
class SharedPtr
{
    struct imp
    {
        imp(T *pTrg = NULL, int cnt = 1): ptr(pTrg),refcnt(cnt){}
        ~imp() { if (ptr) delete ptr;}

        T *ptr;
        int refcnt;
    };


    public:
    SharedPtr(T *t = NULL):p(NULL)
    {
        p = new imp(t);
    }

    ~SharedPtr()
    {
        p->refcnt--;

        if (p->refcnt == 0)
            delete p;
    }


    SharedPtr(const SharedPtr& rhs)
    {
        p = rhs.p;
        p->refcnt++;
    }

    const SharedPtr& operator= (const SharedPtr& rhs)
    {
        if (p == rhs.p) return *this;

        p->refcnt--;
        if (p->refcnt == 0)
            delete p;

        p = rhs.p;
        p->refcnt++;

        return *this;
    }


    T *get() const
    {
        return p->ptr;
    }


    T *operator->()
    {
        return p->ptr;
    }


    const T*operator->() const
    {
        return p->ptr;
    }


    int use_count()
    {
        return p->refcnt;
    }

    private:
    imp *p;
};
#endif

#endif /* !VBOX_INCLUDED_SRC_NetLib_shared_ptr_h */
