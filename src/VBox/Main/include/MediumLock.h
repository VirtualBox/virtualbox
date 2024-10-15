/* $Id: MediumLock.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 *
 * VirtualBox medium object lock collections
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MAIN_INCLUDED_MediumLock_h
#define MAIN_INCLUDED_MediumLock_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* interface definitions */
#include "VBox/com/VirtualBox.h"
#include "VirtualBoxBase.h"
#include "AutoCaller.h"

#include <iprt/types.h>

#include <list>
#include <map>

class Medium;
class MediumAttachment;

/**
 * Single entry for medium lock lists. Has a medium object reference,
 * information about what kind of lock should be taken, and if it is
 * locked right now.
 */
class MediumLock
{
public:
    /**
     * Default medium lock constructor.
     */
    MediumLock();

    /**
     * Default medium lock destructor.
     */
    ~MediumLock();

    /**
     * Create a new medium lock description
     *
     * @param aMedium       Reference to medium object
     * @param aLockWrite    @c true means a write lock should be taken
     */
    MediumLock(const ComObjPtr<Medium> &aMedium, bool aLockWrite);

    /**
     * Copy constructor. Needed because we contain an AutoCaller
     * instance which is deliberately not copyable. The copy is not
     * marked as locked, so be careful.
     *
     * @param aMediumLock   Reference to source object.
     */
    MediumLock(const MediumLock &aMediumLock);

    /**
     * Update a medium lock description.
     *
     * @note May be used in locked state.
     *
     * @return COM status code
     * @param aLockWrite    @c true means a write lock should be taken
     */
    HRESULT UpdateLock(bool aLockWrite);

    /**
     * Get medium object reference.
     */
    const ComObjPtr<Medium> &GetMedium() const;

    /**
     * Get medium object lock request type.
     */
    bool GetLockRequest() const;

    /**
     * Check if this medium object has been locked by this MediumLock.
     */
    bool IsLocked() const;

    /**
     * Acquire a medium lock.
     *
     * @return COM status code
     * @param aIgnoreLockedMedia    If set ignore all media which is already
     *                              locked in an incompatible way.
     */
    HRESULT Lock(bool aIgnoreLockedMedia = false);

    /**
     * Release a medium lock.
     *
     * @return COM status code
     */
    HRESULT Unlock();

private:
    ComObjPtr<Medium> mMedium;
    ComPtr<IToken> mToken;
    AutoCaller mMediumCaller;
    bool mLockWrite;
    bool mIsLocked;
    /** Flag whether the medium was skipped when taking the locks.
     * Only existing and accessible media objects need to be locked. */
    bool mLockSkipped;
};


/**
 * Medium lock list. Meant for storing the ordered locking information
 * for a single medium chain.
 */
class MediumLockList
{
public:

    /* Base list data type. */
    typedef std::list<MediumLock> Base;

    /**
     * Default medium lock list constructor.
     */
    MediumLockList();

    /**
     * Default medium lock list destructor.
     */
    ~MediumLockList();

    /**
     * Checks if medium lock declaration list is empty.
     *
     * @return true if list is empty.
     */
    bool IsEmpty();

    /**
     * Add a new medium lock declaration to the end of the list.
     *
     * @note May be only used in unlocked state.
     *
     * @return COM status code
     * @param aMedium       Reference to medium object
     * @param aLockWrite    @c true means a write lock should be taken
     */
    HRESULT Append(const ComObjPtr<Medium> &aMedium, bool aLockWrite);

    /**
     * Add a new medium lock declaration to the beginning of the list.
     *
     * @note May be only used in unlocked state.
     *
     * @return COM status code
     * @param aMedium       Reference to medium object
     * @param aLockWrite    @c true means a write lock should be taken
     */
    HRESULT Prepend(const ComObjPtr<Medium> &aMedium, bool aLockWrite);

    /**
     * Update a medium lock declaration.
     *
     * @note May be used in locked state.
     *
     * @return COM status code
     * @param aMedium       Reference to medium object
     * @param aLockWrite    @c true means a write lock should be taken
     */
    HRESULT Update(const ComObjPtr<Medium> &aMedium, bool aLockWrite);

    /**
     * Remove a medium lock declaration and return an updated iterator.
     *
     * @note May be used in locked state.
     *
     * @return COM status code
     * @param aIt           Iterator for the element to remove
     */
    HRESULT RemoveByIterator(Base::iterator &aIt);

    /**
     * Clear all medium lock declarations.
     *
     * @note Implicitly unlocks all locks.
     *
     * @return COM status code
     */
    HRESULT Clear();

    /**
     * Get iterator begin() for base list.
     */
    Base::iterator GetBegin();

    /**
     * Get iterator end() for base list.
     */
    Base::iterator GetEnd();

    /**
     * Acquire all medium locks "atomically", i.e. all or nothing.
     *
     * @return COM status code
     * @param aSkipOverLockedMedia  If set ignore all media which is already
     *                              locked for reading or writing. For callers
     *                              which need to know which medium objects
     *                              have been locked by this lock list you
     *                              can iterate over the list and check the
     *                              MediumLock state.
     */
    HRESULT Lock(bool aSkipOverLockedMedia = false);

    /**
     * Release all medium locks.
     *
     * @return COM status code
     */
    HRESULT Unlock();

private:
    Base mMediumLocks;
    bool mIsLocked;
};

/**
 * Medium lock list map. Meant for storing a collection of lock lists.
 * The usual use case is creating such a map when locking all medium chains
 * belonging to one VM, however that's not the limit. Be creative.
 */
class MediumLockListMap
{
public:

    /**
     * Default medium lock list map constructor.
     */
    MediumLockListMap();

    /**
     * Default medium lock list map destructor.
     */
    ~MediumLockListMap();

    /**
     * Checks if medium lock list map is empty.
     *
     * @return true if list is empty.
     */
    bool IsEmpty();

    /**
     * Insert a new medium lock list into the map.
     *
     * @note May be only used in unlocked state.
     *
     * @return COM status code
     * @param aMediumAttachment Reference to medium attachment object, the key.
     * @param aMediumLockList   Reference to medium lock list object
     */
    HRESULT Insert(const ComObjPtr<MediumAttachment> &aMediumAttachment, MediumLockList *aMediumLockList);

    /**
     * Replace the medium lock list key by a different one.
     *
     * @note May be used in locked state.
     *
     * @return COM status code
     * @param aMediumAttachmentOld  Reference to medium attachment object.
     * @param aMediumAttachmentNew  Reference to medium attachment object.
     */
    HRESULT ReplaceKey(const ComObjPtr<MediumAttachment> &aMediumAttachmentOld, const ComObjPtr<MediumAttachment> &aMediumAttachmentNew);

    /**
     * Remove a medium lock list from the map. The list will get deleted.
     *
     * @note May be only used in unlocked state.
     *
     * @return COM status code
     * @param aMediumAttachment Reference to medium attachment object, the key.
     */
    HRESULT Remove(const ComObjPtr<MediumAttachment> &aMediumAttachment);

    /**
     * Clear all medium lock declarations in this map.
     *
     * @note Implicitly unlocks all locks.
     *
     * @return COM status code
     */
    HRESULT Clear();

    /**
     * Get the medium lock list identified by the given key.
     *
     * @note May be used in locked state.
     *
     * @return COM status code
     * @param aMediumAttachment     Key for medium attachment object.
     * @param aMediumLockList       Out: medium attachment object reference.
     */
    HRESULT Get(const ComObjPtr<MediumAttachment> &aMediumAttachment, MediumLockList * &aMediumLockList);

    /**
     * Acquire all medium locks "atomically", i.e. all or nothing.
     *
     * @return COM status code
     */
    HRESULT Lock();

    /**
     * Release all medium locks.
     *
     * @return COM status code
     */
    HRESULT Unlock();

    /** Introspection. */
    bool IsLocked(void) const { return mIsLocked; }

private:
    typedef std::map<ComObjPtr<MediumAttachment>, MediumLockList *> Base;
    Base mMediumLocks;
    bool mIsLocked;
};

#endif /* !MAIN_INCLUDED_MediumLock_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
