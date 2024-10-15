/* $Id: objectslist.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * List of COM objects
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MAIN_INCLUDED_objectslist_h
#define MAIN_INCLUDED_objectslist_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <list>
#include <VBox/com/ptr.h>

/**
 * Implements a "flat" objects list with a lock. Since each such list
 * has its own lock it is not a good idea to implement trees with this.
 *
 * ObjectList<T> is designed to behave as if it were a std::list of
 * COM pointers of class T; in other words,
 * ObjectList<Medium> behaves like std::list< ComObjPtr<Medium> > but
 * it's less typing. Iterators, front(), size(), begin() and end()
 * are implemented.
 *
 * In addition it automatically includes an RWLockHandle which can be
 * accessed with getLockHandle().
 *
 * If you need the raw std::list for some reason you can access it with
 * getList().
 *
 * The destructor automatically calls uninit() on every contained
 * COM object. If this is not desired, clear the member list before
 * deleting the list object.
 */
template<typename T>
class ObjectsList
{
public:
    typedef ComObjPtr<T> MyType;
    typedef std::list<MyType> MyList;

    typedef typename MyList::iterator iterator;
    typedef typename MyList::const_iterator const_iterator;
        // typename is necessary to disambiguate "::iterator" in templates; see
        // the "this might hurt your head" part in
        // http://www.parashift.com/c++-faq-lite/templates.html#faq-35.18

    ObjectsList(RWLockHandle &lockHandle)
        : m_lock(lockHandle)
    { }

    ~ObjectsList()
    {
        uninitAll();
    }

private:
    // prohibit copying and assignment
    ObjectsList(const ObjectsList &d);
    ObjectsList& operator=(const ObjectsList &d);

public:

    /**
     * Returns the lock handle which protects this list, for use with
     * AutoReadLock or AutoWriteLock.
     */
    RWLockHandle& getLockHandle()
    {
        return m_lock;
    }

    /**
     * Calls m_ll.push_back(p) with locking.
     * @param p
     */
    void addChild(MyType p)
    {
        AutoWriteLock al(m_lock COMMA_LOCKVAL_SRC_POS);
        m_ll.push_back(p);
    }

    /**
     * Calls m_ll.remove(p) with locking. Does NOT call uninit()
     * on the contained object.
     * @param p
     */
    void removeChild(MyType p)
    {
        AutoWriteLock al(m_lock COMMA_LOCKVAL_SRC_POS);
        m_ll.remove(p);
    }

    /**
     * Appends all objects from another list to the member list.
     * Locks the other list for reading but does not lock "this"
     * (because it might be on the caller's stack and needs no
     * locking).
     * @param ll
     */
    void appendOtherList(ObjectsList<T> &ll)
    {
        AutoReadLock alr(ll.getLockHandle() COMMA_LOCKVAL_SRC_POS);
        for (const_iterator it = ll.begin();
             it != ll.end();
             ++it)
        {
            m_ll.push_back(*it);
        }
    }

    /**
     * Calls uninit() on every COM object on the list and then
     * clears the list, with locking.
     */
    void uninitAll()
    {
        /* The implementation differs from the high level description, because
         * it isn't safe to hold any locks when invoking uninit() methods. It
         * leads to incorrect lock order (first lock, then the Caller related
         * event semaphore) and thus deadlocks. Dropping the lock is vital,
         * and means we can't rely on iterators while not holding the lock. */
        AutoWriteLock al(m_lock COMMA_LOCKVAL_SRC_POS);
        while (!m_ll.empty())
        {
            /* Need a copy of the element, have to delete the entry before
             * dropping the lock, otherwise someone else might mess with the
             * list in the mean time, leading to erratic behavior. */
            MyType q = m_ll.front();
            m_ll.pop_front();
            al.release();
            q->uninit();
            al.acquire();
        }
    }

    /**
     * Returns the no. of objects on the list (std::list compatibility)
     * with locking.
     */
    size_t size()
    {
        AutoReadLock al(m_lock COMMA_LOCKVAL_SRC_POS);
        return m_ll.size();
    }

    /**
     * Returns a raw pointer to the member list of objects.
     * Does not lock!
     * @return
     */
    MyList& getList()
    {
        return m_ll;
    }

    /**
     * Returns the first object on the list (std::list compatibility)
     * with locking.
     */
    MyType front()
    {
        AutoReadLock al(m_lock COMMA_LOCKVAL_SRC_POS);
        return m_ll.front();
    }

    /**
     * Returns the begin iterator from the list (std::list compatibility).
     * Does not lock!
     * @return
     */
    iterator begin()
    {
        return m_ll.begin();
    }

    /**
     * Returns the end iterator from the list (std::list compatibility).
     * Does not lock!
     */
    iterator end()
    {
        return m_ll.end();
    }

    void insert(iterator it, MyType &p)
    {
        m_ll.insert(it, p);
    }

    void erase(iterator it)
    {
        m_ll.erase(it);
    }

private:
    MyList          m_ll;
    RWLockHandle    &m_lock;
};

#endif /* !MAIN_INCLUDED_objectslist_h */
