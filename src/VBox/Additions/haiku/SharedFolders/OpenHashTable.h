/* $Id: OpenHashTable.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * OpenHashTable, Haiku Guest Additions.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

/*
 * This code is based on:
 *
 * VirtualBox Guest Additions for Haiku.
 *
 * Copyright 2007, Hugo Santos. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef GA_INCLUDED_SRC_haiku_SharedFolders_OpenHashTable_h
#define GA_INCLUDED_SRC_haiku_SharedFolders_OpenHashTable_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#include <OS.h>
#include <stdlib.h>
#include <string.h>

#ifdef _KERNEL_MODE
#    include <KernelExport.h>
#    include "kernel_cpp.h"
#endif

/*!
    The Definition template must have four methods: `HashKey', `Hash',
    `Compare' and `GetLink;. It must also define several types as shown in the
    following example:

    struct Foo {
        int bar;

        Foo* fNext;
    };

    struct HashTableDefinition {
        typedef int        KeyType;
        typedef    Foo        ValueType;

        size_t HashKey(int key) const
        {
            return key >> 1;
        }

        size_t Hash(Foo* value) const
        {
            return HashKey(value->bar);
        }

        bool Compare(int key, Foo* value) const
        {
            return value->bar == key;
        }

        Foo*& GetLink(Foo* value) const
        {
            return value->fNext;
        }
    };
*/


struct MallocAllocator
{
    void* Allocate(size_t size) const
    {
        return malloc(size);
    }

    void Free(void* memory) const
    {
        free(memory);
    }
};


template<typename Definition, bool AutoExpand = true,
    bool CheckDuplicates = false, typename Allocator = MallocAllocator>
class BOpenHashTable
{
public:
    typedef BOpenHashTable<Definition, AutoExpand, CheckDuplicates> HashTable;
    typedef typename Definition::KeyType      KeyType;
    typedef typename Definition::ValueType    ValueType;

    static const size_t kMinimumSize = 8;

    // All allocations are of power of 2 lengths.

    // regrowth factor: 200 / 256 = 78.125%
    //                   50 / 256 = 19.53125%

    BOpenHashTable()
        :
        fTableSize(0),
        fItemCount(0),
        fTable(NULL)
    {
    }

    BOpenHashTable(const Definition& definition)
        :
        fDefinition(definition),
        fTableSize(0),
        fItemCount(0),
        fTable(NULL)
    {
    }

    BOpenHashTable(const Definition& definition, const Allocator& allocator)
        :
        fDefinition(definition),
        fAllocator(allocator),
        fTableSize(0),
        fItemCount(0),
        fTable(NULL)
    {
    }

    ~BOpenHashTable()
    {
        fAllocator.Free(fTable);
    }

    status_t Init(size_t initialSize = kMinimumSize)
    {
        if (initialSize > 0 && !_Resize(initialSize))
            return B_NO_MEMORY;
        return B_OK;
    }

    size_t TableSize() const
    {
        return fTableSize;
    }

    size_t CountElements() const
    {
        return fItemCount;
    }

    ValueType* Lookup(const KeyType& key) const
    {
        if (fTableSize == 0)
            return NULL;

        size_t index = fDefinition.HashKey(key) & (fTableSize - 1);
        ValueType* slot = fTable[index];

        while (slot)
        {
            if (fDefinition.Compare(key, slot))
                break;
            slot = _Link(slot);
        }

        return slot;
    }

    status_t Insert(ValueType* value)
    {
        if (fTableSize == 0)
        {
            if (!_Resize(kMinimumSize))
                return B_NO_MEMORY;
        }
        else if (AutoExpand && fItemCount >= (fTableSize * 200 / 256))
            _Resize(fTableSize * 2);

        InsertUnchecked(value);
        return B_OK;
    }

    void InsertUnchecked(ValueType* value)
    {
        if (CheckDuplicates && _ExhaustiveSearch(value))
        {
#ifdef _KERNEL_MODE
            panic("Hash Table: value already in table.");
#else
            debugger("Hash Table: value already in table.");
#endif
        }

        _Insert(fTable, fTableSize, value);
        fItemCount++;
    }

    // TODO: a ValueType* Remove(const KeyType& key) method is missing

    bool Remove(ValueType* value)
    {
        if (!RemoveUnchecked(value))
            return false;

        if (AutoExpand && fTableSize > kMinimumSize
            && fItemCount < (fTableSize * 50 / 256))
            _Resize(fTableSize / 2);

        return true;
    }

    bool RemoveUnchecked(ValueType* value)
    {
        size_t index = fDefinition.Hash(value) & (fTableSize - 1);
        ValueType* previous = NULL;
        ValueType* slot = fTable[index];

        while (slot)
        {
            ValueType* next = _Link(slot);

            if (value == slot)
            {
                if (previous)
                    _Link(previous) = next;
                else
                    fTable[index] = next;
                break;
            }

            previous = slot;
            slot = next;
        }

        if (slot == NULL)
            return false;

        if (CheckDuplicates && _ExhaustiveSearch(value))
        {
#ifdef _KERNEL_MODE
            panic("Hash Table: duplicate detected.");
#else
            debugger("Hash Table: duplicate detected.");
#endif
        }

        fItemCount--;
        return true;
    }

    /*!    Removes all elements from the hash table. No resizing happens. The
        elements are not deleted. If \a returnElements is \c true, the method
        returns all elements chained via their hash table link.
    */
    ValueType* Clear(bool returnElements = false)
    {
        if (this->fItemCount == 0)
            return NULL;

        ValueType* result = NULL;

        if (returnElements)
                {
            ValueType** nextPointer = &result;

            // iterate through all buckets
            for (size_t i = 0; i < fTableSize; i++)
            {
                ValueType* element = fTable[i];
                if (element != NULL)
                {
                    // add the bucket to the list
                    *nextPointer = element;

                    // update nextPointer to point to the fNext of the last
                    // element in the bucket
                    while (element != NULL)
                    {
                        nextPointer = &_Link(element);
                        element = *nextPointer;
                    }
                }
            }
        }

        memset(this->fTable, 0, sizeof(ValueType*) * this->fTableSize);

        return result;
    }

    /*!    If the table needs resizing, the number of bytes for the required
        allocation is returned. If no resizing is needed, 0 is returned.
    */
    size_t ResizeNeeded() const
    {
        size_t size = fTableSize;
        if (size == 0 || fItemCount >= (size * 200 / 256))
        {
            // grow table
            if (size == 0)
                size = kMinimumSize;
            while (fItemCount >= size * 200 / 256)
                size <<= 1;
        }
        else if (size > kMinimumSize && fItemCount < size * 50 / 256)
        {
            // shrink table
            while (fItemCount < size * 50 / 256)
                size >>= 1;
            if (size < kMinimumSize)
                size = kMinimumSize;
        }

        if (size == fTableSize)
            return 0;

        return size * sizeof(ValueType*);
    }

    /*!    Resizes the table using the given allocation. The allocation must not
        be \c NULL. It must be of size \a size, which must a value returned
        earlier by ResizeNeeded(). If the size requirements have changed in the
        meantime, the method free()s the given allocation and returns \c false,
        unless \a force is \c true, in which case the supplied allocation is
        used in any event.
        Otherwise \c true is returned.
        If \a oldTable is non-null and resizing is successful, the old table
        will not be freed, but will be returned via this parameter instead.
    */
    bool Resize(void* allocation, size_t size, bool force = false,
        void** oldTable = NULL)
    {
        if (!force && size != ResizeNeeded())
        {
            fAllocator.Free(allocation);
            return false;
        }

        _Resize((ValueType**)allocation, size / sizeof(ValueType*), oldTable);
        return true;
    }

    class Iterator
        {
    public:
        Iterator(const HashTable* table)
            : fTable(table)
        {
            Rewind();
        }

        Iterator(const HashTable* table, size_t index, ValueType* value)
            : fTable(table), fIndex(index), fNext(value) {}

        bool HasNext() const { return fNext != NULL; }

        ValueType* Next()
        {
            ValueType* current = fNext;
            _GetNext();
            return current;
        }

        void Rewind()
        {
            // get the first one
            fIndex = 0;
            fNext = NULL;
            _GetNext();
        }

    protected:
        Iterator() {}

        void _GetNext()
        {
            if (fNext)
                fNext = fTable->_Link(fNext);

            while (fNext == NULL && fIndex < fTable->fTableSize)
                fNext = fTable->fTable[fIndex++];
        }

        const HashTable* fTable;
        size_t fIndex;
        ValueType* fNext;
    };

    Iterator GetIterator() const
    {
        return Iterator(this);
    }

    Iterator GetIterator(const KeyType& key) const
    {
        if (fTableSize == 0)
            return Iterator(this, fTableSize, NULL);

        size_t index = fDefinition.HashKey(key) & (fTableSize - 1);
        ValueType* slot = fTable[index];

        while (slot)
        {
            if (fDefinition.Compare(key, slot))
                break;
            slot = _Link(slot);
        }

        if (slot == NULL)
            return Iterator(this, fTableSize, NULL);

        return Iterator(this, index + 1, slot);
    }

protected:
    // for g++ 2.95
    friend class Iterator;

    void _Insert(ValueType** table, size_t tableSize, ValueType* value)
    {
        size_t index = fDefinition.Hash(value) & (tableSize - 1);

        _Link(value) = table[index];
        table[index] = value;
    }

    bool _Resize(size_t newSize)
    {
        ValueType** newTable
            = (ValueType**)fAllocator.Allocate(sizeof(ValueType*) * newSize);
        if (newTable == NULL)
            return false;

        _Resize(newTable, newSize);
        return true;
    }

    void _Resize(ValueType** newTable, size_t newSize, void** _oldTable = NULL)
    {
        for (size_t i = 0; i < newSize; i++)
            newTable[i] = NULL;

        if (fTable)
        {
            for (size_t i = 0; i < fTableSize; i++)
            {
                ValueType* bucket = fTable[i];
                while (bucket)
                {
                    ValueType* next = _Link(bucket);
                    _Insert(newTable, newSize, bucket);
                    bucket = next;
                }
            }

            if (_oldTable != NULL)
                *_oldTable = fTable;
            else
                fAllocator.Free(fTable);
        }
        else if (_oldTable != NULL)
            *_oldTable = NULL;

        fTableSize = newSize;
        fTable = newTable;
    }

    ValueType*& _Link(ValueType* bucket) const
    {
        return fDefinition.GetLink(bucket);
    }

    bool _ExhaustiveSearch(ValueType* value) const
    {
        for (size_t i = 0; i < fTableSize; i++)
        {
            ValueType* bucket = fTable[i];
            while (bucket) {
                if (bucket == value)
                    return true;
                bucket = _Link(bucket);
            }
        }

        return false;
    }

    Definition        fDefinition;
    Allocator        fAllocator;
    size_t            fTableSize;
    size_t            fItemCount;
    ValueType**        fTable;
};

#endif /* !GA_INCLUDED_SRC_haiku_SharedFolders_OpenHashTable_h */

