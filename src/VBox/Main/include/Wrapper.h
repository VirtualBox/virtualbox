/* $Id: Wrapper.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM - API wrapper helpers.
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

#ifndef MAIN_INCLUDED_Wrapper_h
#define MAIN_INCLUDED_Wrapper_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <vector>
#include <VBox/com/ptr.h>
#include <VBox/com/array.h>

#include "AutoCaller.h"


/**
 * Checks that the given pointer to an output argument is valid and throws
 * E_POINTER + extended error info otherwise.
 * @param arg   Pointer argument.
 */
#define CheckComArgOutPointerValidThrow(arg) \
    do { \
        if (RT_LIKELY(RT_VALID_PTR(arg))) \
        { /* likely */ }\
        /* Have to use use VirtualBoxBase::tr here or lupdate won't be able to figure out the context, \
           as it is picking it up right here rather than in the places where the macro is actually used. */ \
        else throw setError(E_POINTER, VirtualBoxBase::tr("Output argument %s points to invalid memory location (%p)"), \
                            #arg, (void *)(arg)); \
    } while (0)


class BSTROutConverter
{
public:
    BSTROutConverter() : mDst(NULL)
    {
    }

    BSTROutConverter(BSTR *aDst) : mDst(aDst)
    {
    }

    ~BSTROutConverter()
    {
        if (mDst)
            Bstr(mStr).detachTo(mDst);
    }

    com::Utf8Str &str()
    {
        return mStr;
    }

private:
    com::Utf8Str mStr;
    BSTR *mDst;
};

class BSTRInConverter
{
public:
    BSTRInConverter() : mSrc()
    {
    }

    BSTRInConverter(CBSTR aSrc) : mSrc(aSrc)
    {
    }

    ~BSTRInConverter()
    {
    }

    const com::Utf8Str &str()
    {
        return mSrc;
    }

private:
    const com::Utf8Str mSrc;
};

class ArrayBSTROutConverter
{
public:
    ArrayBSTROutConverter() :
#ifdef VBOX_WITH_XPCOM
        mDstSize(NULL),
        mDst(NULL)
#else // !VBOX_WITH_XPCOM
        mDst(NULL)
#endif // !VBOX_WITH_XPCOM
    {
    }

    ArrayBSTROutConverter(ComSafeArrayOut(BSTR, aDst)) :
#ifdef VBOX_WITH_XPCOM
        mDstSize(aDstSize),
        mDst(aDst)
#else // !VBOX_WITH_XPCOM
        mDst(aDst)
#endif // !VBOX_WITH_XPCOM
    {
    }

    ~ArrayBSTROutConverter()
    {
        if (mDst)
        {
            com::SafeArray<BSTR> outArray(mArray.size());
            for (size_t i = 0; i < mArray.size(); i++)
                Bstr(mArray[i]).detachTo(&outArray[i]);
            outArray.detachTo(ComSafeArrayOutArg(mDst));
        }
    }

    std::vector<com::Utf8Str> &array()
    {
        return mArray;
    }

private:
    std::vector<com::Utf8Str> mArray;
#ifdef VBOX_WITH_XPCOM
    PRUint32 *mDstSize;
    BSTR **mDst;
#else // !VBOX_WITH_XPCOM
    SAFEARRAY **mDst;
#endif // !VBOX_WITH_XPCOM
};

class ArrayBSTRInConverter
{
public:
    ArrayBSTRInConverter()
    {
    }

    ArrayBSTRInConverter(ComSafeArrayIn(IN_BSTR, aSrc))
    {
        if (!ComSafeArrayInIsNull(aSrc))
        {
            com::SafeArray<IN_BSTR> inArray(ComSafeArrayInArg(aSrc));
            mArray.resize(inArray.size());
            for (size_t i = 0; i < inArray.size(); i++)
                mArray[i] = inArray[i];
        }
    }

    ~ArrayBSTRInConverter()
    {
    }

    const std::vector<com::Utf8Str> &array()
    {
        return mArray;
    }

private:
    std::vector<com::Utf8Str> mArray;
};

class UuidOutConverter
{
public:
    UuidOutConverter() : mDst(NULL)
    {
    }

    UuidOutConverter(BSTR *aDst) : mDst(aDst)
    {
    }

    ~UuidOutConverter()
    {
        if (mDst)
            mUuid.toUtf16().detachTo(mDst);
    }

    com::Guid &uuid()
    {
        return mUuid;
    }

private:
    com::Guid mUuid;
    BSTR *mDst;
};

class UuidInConverter
{
public:
    UuidInConverter() : mSrc()
    {
    }

    UuidInConverter(CBSTR aSrc) : mSrc(aSrc)
    {
    }

    ~UuidInConverter()
    {
    }

    const com::Guid &uuid()
    {
        return mSrc;
    }

private:
    const com::Guid mSrc;
};

class ArrayUuidOutConverter
{
public:
    ArrayUuidOutConverter() :
#ifdef VBOX_WITH_XPCOM
        mDstSize(NULL),
        mDst(NULL)
#else // !VBOX_WITH_XPCOM
        mDst(NULL)
#endif // !VBOX_WITH_XPCOM
    {
    }

    ArrayUuidOutConverter(ComSafeArrayOut(BSTR, aDst)) :
#ifdef VBOX_WITH_XPCOM
        mDstSize(aDstSize),
        mDst(aDst)
#else // !VBOX_WITH_XPCOM
        mDst(aDst)
#endif // !VBOX_WITH_XPCOM
    {
    }

    ~ArrayUuidOutConverter()
    {
        if (mDst)
        {
            com::SafeArray<BSTR> outArray(mArray.size());
            for (size_t i = 0; i < mArray.size(); i++)
                mArray[i].toUtf16().detachTo(&outArray[i]);
            outArray.detachTo(ComSafeArrayOutArg(mDst));
        }
    }

    std::vector<com::Guid> &array()
    {
        return mArray;
    }

private:
    std::vector<com::Guid> mArray;
#ifdef VBOX_WITH_XPCOM
    PRUint32 *mDstSize;
    BSTR **mDst;
#else // !VBOX_WITH_XPCOM
    SAFEARRAY **mDst;
#endif // !VBOX_WITH_XPCOM
};

template <class A>
class ComTypeOutConverter
{
public:
    ComTypeOutConverter() : mDst(NULL)
    {
    }

    ComTypeOutConverter(A **aDst) : mDst(aDst)
    {
    }

    ~ComTypeOutConverter()
    {
        if (mDst)
            mPtr.queryInterfaceTo(mDst);
    }

    ComPtr<A> &ptr()
    {
        return mPtr;
    }

private:
    ComPtr<A> mPtr;
    A **mDst;
};

template <class A>
class ComTypeInConverter
{
public:
    ComTypeInConverter() : mSrc(NULL)
    {
    }

    ComTypeInConverter(A *aSrc) : mSrc(aSrc)
    {
    }

    ~ComTypeInConverter()
    {
    }

    const ComPtr<A> &ptr()
    {
        return mSrc;
    }

private:
    const ComPtr<A> mSrc;
};

template <class A>
class ArrayComTypeOutConverter
{
public:
    ArrayComTypeOutConverter() :
#ifdef VBOX_WITH_XPCOM
        mDstSize(NULL),
        mDst(NULL)
#else // !VBOX_WITH_XPCOM
        mDst(NULL)
#endif // !VBOX_WITH_XPCOM
    {
    }

    ArrayComTypeOutConverter(ComSafeArrayOut(A *, aDst)) :
#ifdef VBOX_WITH_XPCOM
        mDstSize(aDstSize),
        mDst(aDst)
#else // !VBOX_WITH_XPCOM
        mDst(aDst)
#endif // !VBOX_WITH_XPCOM
    {
    }

    ~ArrayComTypeOutConverter()
    {
        if (mDst)
        {
            com::SafeIfaceArray<A> outArray(mArray);
            outArray.detachTo(ComSafeArrayOutArg(mDst));
        }
    }

    std::vector<ComPtr<A> > &array()
    {
        return mArray;
    }

private:
    std::vector<ComPtr<A> > mArray;
#ifdef VBOX_WITH_XPCOM
    PRUint32 *mDstSize;
    A ***mDst;
#else // !VBOX_WITH_XPCOM
    SAFEARRAY **mDst;
#endif // !VBOX_WITH_XPCOM
};

template <class A>
class ArrayComTypeInConverter
{
public:
    ArrayComTypeInConverter()
    {
    }

    ArrayComTypeInConverter(ComSafeArrayIn(A *, aSrc))
    {
        if (!ComSafeArrayInIsNull(aSrc))
        {
            com::SafeIfaceArray<A> inArray(ComSafeArrayInArg(aSrc));
            mArray.resize(inArray.size());
            for (size_t i = 0; i < inArray.size(); i++)
                mArray[i] = inArray[i];
        }
    }

    ~ArrayComTypeInConverter()
    {
    }

    const std::vector<ComPtr<A> > &array()
    {
        return mArray;
    }

private:
    std::vector<ComPtr<A> > mArray;
};

template <typename A>
class ArrayOutConverter
{
public:
    ArrayOutConverter() :
#ifdef VBOX_WITH_XPCOM
        mDstSize(NULL),
        mDst(NULL)
#else // !VBOX_WITH_XPCOM
        mDst(NULL)
#endif // !VBOX_WITH_XPCOM
    {
    }

    ArrayOutConverter(ComSafeArrayOut(A, aDst)) :
#ifdef VBOX_WITH_XPCOM
        mDstSize(aDstSize),
        mDst(aDst)
#else // !VBOX_WITH_XPCOM
        mDst(aDst)
#endif // !VBOX_WITH_XPCOM
    {
    }

    ~ArrayOutConverter()
    {
        if (mDst)
        {
            com::SafeArray<A> outArray(mArray.size());
            for (size_t i = 0; i < mArray.size(); i++)
                outArray[i] = mArray[i];
            outArray.detachTo(ComSafeArrayOutArg(mDst));
        }
    }

    std::vector<A> &array()
    {
        return mArray;
    }

private:
    std::vector<A> mArray;
#ifdef VBOX_WITH_XPCOM
    PRUint32 *mDstSize;
    A **mDst;
#else // !VBOX_WITH_XPCOM
    SAFEARRAY **mDst;
#endif // !VBOX_WITH_XPCOM
};

template <typename A>
class ArrayInConverter
{
public:
    ArrayInConverter()
    {
    }

    ArrayInConverter(ComSafeArrayIn(A, aSrc))
    {
        if (!ComSafeArrayInIsNull(aSrc))
        {
            com::SafeArray<A> inArray(ComSafeArrayInArg(aSrc));
            mArray.resize(inArray.size());
            for (size_t i = 0; i < inArray.size(); i++)
                mArray[i] = inArray[i];
        }
    }

    ~ArrayInConverter()
    {
    }

    const std::vector<A> &array()
    {
        return mArray;
    }

private:
    std::vector<A> mArray;
};

#endif /* !MAIN_INCLUDED_Wrapper_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
