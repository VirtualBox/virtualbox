/* $Id: list.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MS COM / XPCOM Abstraction Layer - List classes declaration.
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

#ifndef VBOX_INCLUDED_com_list_h
#define VBOX_INCLUDED_com_list_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/com/ptr.h>
#include <VBox/com/string.h>
#include <VBox/com/array.h>
#include <iprt/cpp/list.h>


/** @defgroup grp_com_list  List Classes
 * @ingroup grp_com
 * @{
 */

/**
 * Specialized list class for using with com::ComPtr<C>
 *
 * @note    This is necessary cause ComPtr<IFACE> has a size of 8.
 */
template <typename C>
class RTCList< ComPtr<C> >: public RTCListBase< ComPtr<C>, ComPtr<C>*, false>
{
    /* Traits */
    typedef ComPtr<C>                 T;
    typedef T                        *ITYPE;
    static const bool                 MT = false;
    typedef RTCListBase<T, ITYPE, MT> BASE;

public:
    /**
     * Creates a new list.
     *
     * This preallocates @a cCapacity elements within the list.
     *
     * @param   cCapacity   The initial capacity the list has.
     * @throws  std::bad_alloc
     */
    RTCList(size_t cCapacity = BASE::kDefaultCapacity)
        : BASE(cCapacity) {}

    /* Define our own new and delete. */
    RTMEMEF_NEW_AND_DELETE_OPERATORS();
};

/**
 * Specialized list class for using with com::ComObjPtr<C>
 *
 * @note: This is necessary cause ComObjPtr<IFACE> has a size of 8.
 */
template <typename C>
class RTCList< ComObjPtr<C> >: public RTCListBase< ComObjPtr<C>, ComObjPtr<C>*, false>
{
    /* Traits */
    typedef ComObjPtr<C>              T;
    typedef T                        *ITYPE;
    static const bool                 MT = false;
    typedef RTCListBase<T, ITYPE, MT> BASE;

public:
    /**
     * Creates a new list.
     *
     * This preallocates @a cCapacity elements within the list.
     *
     * @param   cCapacity   The initial capacity the list has.
     * @throws  std::bad_alloc
     */
    RTCList(size_t cCapacity = BASE::kDefaultCapacity)
        : BASE(cCapacity) {}

    /* Define our own new and delete. */
    RTMEMEF_NEW_AND_DELETE_OPERATORS();
};

/**
 * Specialized list class for using with com::Utf8Str.
 *
 * The class offers methods for importing com::SafeArray's of com::Bstr's.
 */
template <>
class RTCList<com::Utf8Str>: public RTCListBase<com::Utf8Str, com::Utf8Str*, false>
{
    /* Traits */
    typedef com::Utf8Str              T;
    typedef T                        *ITYPE;
    static const bool                 MT = false;
    typedef RTCListBase<T, ITYPE, MT> BASE;

public:
    /**
     * Creates a new list.
     *
     * This preallocates @a cCapacity elements within the list.
     *
     * @param   cCapacity    The initial capacity the list has.
     * @throws  std::bad_alloc
     */
    RTCList(size_t cCapacity = BASE::kDefaultCapacity)
        : BASE(cCapacity) {}

    /**
     * Creates a copy of another list.
     *
     * The other list will be fully copied and the capacity will be the same as
     * the size of the other list. The com::Bstr's are silently converted to
     * com::Utf8Str's.
     *
     * @param   other          The list to copy.
     * @throws  std::bad_alloc
     */
    RTCList(ComSafeArrayIn(IN_BSTR, other))
    {
        com::SafeArray<IN_BSTR> sfaOther(ComSafeArrayInArg(other));
        size_t const cElementsOther = sfaOther.size();
        resizeArray(cElementsOther);
        m_cElements = cElementsOther;
        for (size_t i = 0; i < cElementsOther; ++i)
            RTCListHelper<T, ITYPE>::set(m_pArray, i, T(sfaOther[i]));
    }

    /**
     * Creates a copy of another list.
     *
     * The other list will be fully copied and the capacity will be the same as
     * the size of the other list. The com::Bstr's are silently converted to
     * com::Utf8Str's.
     *
     * @param   other          The list to copy.
     * @throws  std::bad_alloc
     */
    RTCList(const com::SafeArray<IN_BSTR> &other)
        : BASE(other.size())
    {
        for (size_t i = 0; i < m_cElements; ++i)
            RTCListHelper<T, ITYPE>::set(m_pArray, i, T(other[i]));
    }

    /**
     * Copy the items of the other list into this list. All previous items of
     * this list are deleted.
     *
     * @param   other   The list to copy.
     * @return  a reference to this list.
     * @throws  std::bad_alloc
     */
    RTCListBase<T, ITYPE, MT> &operator=(const com::SafeArray<IN_BSTR> &other)
    {
        m_guard.enterWrite();

        /* Values cleanup */
        RTCListHelper<T, ITYPE>::eraseRange(m_pArray, 0, m_cElements);

        /* Copy */
        size_t cElementsOther = other.size();
        if (cElementsOther != m_cCapacity)
            resizeArrayNoErase(cElementsOther);
        m_cElements = cElementsOther;
        for (size_t i = 0; i < cElementsOther; ++i)
            RTCListHelper<T, ITYPE>::set(m_pArray, i, T(other[i]));

        m_guard.leaveWrite();
        return *this;
    }

    /**
     * Implicit conversion to a RTCString list.
     *
     * This allows the usage of the RTCString::join method with this list type.
     *
     * @return  a converted const reference to this list.
     */
    operator const RTCList<RTCString, RTCString*>&()
    {
        return *reinterpret_cast<RTCList<RTCString, RTCString*> *>(this);
    }

    /* Define our own new and delete. */
    RTMEMEF_NEW_AND_DELETE_OPERATORS();
};

/** @} */

#endif /* !VBOX_INCLUDED_com_list_h */

