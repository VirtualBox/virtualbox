/** @file
 * MS COM / XPCOM Abstraction Layer - Safe array helper class declaration.
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

#ifndef VBOX_INCLUDED_com_array_h
#define VBOX_INCLUDED_com_array_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/** @defgroup   grp_com_arrays    COM/XPCOM Arrays
 * @ingroup grp_com
 * @{
 *
 * The COM/XPCOM array support layer provides a cross-platform way to pass
 * arrays to and from COM interface methods and consists of the com::SafeArray
 * template and a set of ComSafeArray* macros part of which is defined in
 * VBox/com/defs.h.
 *
 * This layer works with interface attributes and method parameters that have
 * the 'safearray="yes"' attribute in the XIDL definition:
 * @code

    <interface name="ISomething" ...>

      <method name="testArrays">
        <param name="inArr" type="long" dir="in" safearray="yes"/>
        <param name="outArr" type="long" dir="out" safearray="yes"/>
        <param name="retArr" type="long" dir="return" safearray="yes"/>
      </method>

    </interface>

 * @endcode
 *
 * Methods generated from this and similar definitions are implemented in
 * component classes using the following declarations:
 * @code

    STDMETHOD(TestArrays)(ComSafeArrayIn(LONG, aIn),
                          ComSafeArrayOut(LONG, aOut),
                          ComSafeArrayOut(LONG, aRet));

 * @endcode
 *
 * And the following function bodies:
 * @code

    STDMETHODIMP Component::TestArrays(ComSafeArrayIn(LONG, aIn),
                                       ComSafeArrayOut(LONG, aOut),
                                       ComSafeArrayOut(LONG, aRet))
    {
        if (ComSafeArrayInIsNull(aIn))
            return E_INVALIDARG;
        if (ComSafeArrayOutIsNull(aOut))
            return E_POINTER;
        if (ComSafeArrayOutIsNull(aRet))
            return E_POINTER;

        // Use SafeArray to access the input array parameter

        com::SafeArray<LONG> in(ComSafeArrayInArg(aIn));

        for (size_t i = 0; i < in.size(); ++ i)
            LogFlow(("*** in[%u]=%d\n", i, in[i]));

        // Use SafeArray to create the return array (the same technique is used
        // for output array parameters)

        SafeArray<LONG> ret(in.size() * 2);
        for (size_t i = 0; i < in.size(); ++ i)
        {
            ret[i] = in[i];
            ret[i + in.size()] = in[i] * 10;
        }

        ret.detachTo(ComSafeArrayOutArg(aRet));

        return S_OK;
    }

 * @endcode
 *
 * Such methods can be called from the client code using the following pattern:
 * @code

    ComPtr<ISomething> component;

    // ...

    com::SafeArray<LONG> in(3);
    in[0] = -1;
    in[1] = -2;
    in[2] = -3;

    com::SafeArray<LONG> out;
    com::SafeArray<LONG> ret;

    HRESULT rc = component->TestArrays(ComSafeArrayAsInParam(in),
                                       ComSafeArrayAsOutParam(out),
                                       ComSafeArrayAsOutParam(ret));

    if (SUCCEEDED(rc))
        for (size_t i = 0; i < ret.size(); ++ i)
            printf("*** ret[%u]=%d\n", i, ret[i]);

 * @endcode
 *
 * For interoperability with standard C++ containers, there is a template
 * constructor that takes such a container as argument and performs a deep copy
 * of its contents. This can be used in method implementations like this:
 * @code

    STDMETHODIMP Component::COMGETTER(Values)(ComSafeArrayOut(int, aValues))
    {
        // ... assume there is a |std::list<int> mValues| data member

        com::SafeArray<int> values(mValues);
        values.detachTo(ComSafeArrayOutArg(aValues));

        return S_OK;
    }

 * @endcode
 *
 * The current implementation of the SafeArray layer supports all types normally
 * allowed in XIDL as array element types (including 'wstring' and 'uuid').
 * However, 'pointer-to-...' types (e.g. 'long *', 'wstring *') are not
 * supported and therefore cannot be used as element types.
 *
 * Note that for GUID arrays you should use SafeGUIDArray and
 * SafeConstGUIDArray, customized SafeArray<> specializations.
 *
 * Also note that in order to pass input BSTR array parameters declared
 * using the ComSafeArrayIn(IN_BSTR, aParam) macro to the SafeArray<>
 * constructor using the ComSafeArrayInArg() macro, you should use IN_BSTR
 * as the SafeArray<> template argument, not just BSTR.
 *
 * Arrays of interface pointers are also supported but they require to use a
 * special SafeArray implementation, com::SafeIfacePointer, which takes the
 * interface class name as a template argument (e.g.
 * com::SafeIfacePointer\<IUnknown\>). This implementation functions
 * identically to com::SafeArray.
 */

#ifdef VBOX_WITH_XPCOM
# include <nsMemory.h>
#endif

#include "VBox/com/defs.h"

#if RT_GNUC_PREREQ(4, 6) || (defined(_MSC_VER) && (_MSC_VER >= 1600))
/** @def VBOX_WITH_TYPE_TRAITS
 * Type traits are a C++ 11 feature, so not available everywhere (yet).
 * Only GCC 4.6 or newer and MSVC++ 16.0 (Visual Studio 2010) or newer.
 */
# define VBOX_WITH_TYPE_TRAITS
#endif

#ifdef VBOX_WITH_TYPE_TRAITS
# include <type_traits>
#endif

#include "VBox/com/ptr.h"
#include "VBox/com/assert.h"
#include "iprt/cpp/list.h"

/** @def ComSafeArrayAsInParam
 * Wraps the given com::SafeArray instance to generate an expression that is
 * suitable for passing it to functions that take input safearray parameters
 * declared using the ComSafeArrayIn macro.
 *
 * @param aArray    com::SafeArray instance to pass as an input parameter.
 */

/** @def ComSafeArrayAsOutParam
 * Wraps the given com::SafeArray instance to generate an expression that is
 * suitable for passing it to functions that take output safearray parameters
 * declared using the ComSafeArrayOut macro.
 *
 * @param aArray    com::SafeArray instance to pass as an output parameter.
 */

/** @def ComSafeArrayNullInParam
 * Helper for passing a NULL array parameter to a COM / XPCOM method.
 */

#ifdef VBOX_WITH_XPCOM

# define ComSafeArrayAsInParam(aArray) \
    (PRUint32)(aArray).size(), (aArray).__asInParam_Arr((aArray).raw())

# define ComSafeArrayAsOutParam(aArray) \
    (aArray).__asOutParam_Size(), (aArray).__asOutParam_Arr()

# define ComSafeArrayNullInParam()      0, NULL

#else /* !VBOX_WITH_XPCOM */

# define ComSafeArrayAsInParam(aArray)  (aArray).__asInParam()

# define ComSafeArrayAsOutParam(aArray) (aArray).__asOutParam()

# define ComSafeArrayNullInParam()      (NULL)

#endif /* !VBOX_WITH_XPCOM */

/**
 *
 */
namespace com
{

/** Used for dummy element access in com::SafeArray, avoiding crashes. */
extern const char Zeroes[16];


#ifdef VBOX_WITH_XPCOM

////////////////////////////////////////////////////////////////////////////////

/**
 * Provides various helpers for SafeArray.
 *
 * @param T Type of array elements.
 */
template<typename T>
struct SafeArrayTraits
{
protected:

    /** Initializes memory for aElem. */
    static void Init(T &aElem) { aElem = (T)0; }

    /** Initializes memory occupied by aElem. */
    static void Uninit(T &aElem) { RT_NOREF(aElem); }

    /** Creates a deep copy of aFrom and stores it in aTo. */
    static void Copy(const T &aFrom, T &aTo) { aTo = aFrom; }

public:

    /* Magic to workaround strict rules of par. 4.4.4 of the C++ standard (that
     * in particular forbid casts of 'char **' to 'const char **'). Then initial
     * reason for this magic is that XPIDL declares input strings
     * (char/PRUnichar pointers) as const but doesn't do so for pointers to
     * arrays. */
    static T *__asInParam_Arr(T *aArr) { return aArr; }
    static T *__asInParam_Arr(const T *aArr) { return const_cast<T *>(aArr); }
};

template<typename T>
struct SafeArrayTraits<T *>
{
    // Arbitrary pointers are not supported
};

template<>
struct SafeArrayTraits<PRUnichar *>
{
protected:

    static void Init(PRUnichar * &aElem) { aElem = NULL; }

    static void Uninit(PRUnichar * &aElem)
    {
        if (aElem)
        {
            ::SysFreeString(aElem);
            aElem = NULL;
        }
    }

    static void Copy(const PRUnichar * aFrom, PRUnichar * &aTo)
    {
        AssertCompile(sizeof(PRUnichar) == sizeof(OLECHAR));
        aTo = aFrom ? ::SysAllocString((const OLECHAR *)aFrom) : NULL;
    }

public:

    /* Magic to workaround strict rules of par. 4.4.4 of the C++ standard */
    static const PRUnichar **__asInParam_Arr(PRUnichar **aArr)
    {
        return const_cast<const PRUnichar **>(aArr);
    }
    static const PRUnichar **__asInParam_Arr(const PRUnichar **aArr) { return aArr; }
};

template<>
struct SafeArrayTraits<const PRUnichar *>
{
protected:

    static void Init(const PRUnichar * &aElem) { aElem = NULL; }
    static void Uninit(const PRUnichar * &aElem)
    {
        if (aElem)
        {
            ::SysFreeString(const_cast<PRUnichar *>(aElem));
            aElem = NULL;
        }
    }

    static void Copy(const PRUnichar * aFrom, const PRUnichar * &aTo)
    {
        AssertCompile(sizeof(PRUnichar) == sizeof(OLECHAR));
        aTo = aFrom ? ::SysAllocString((const OLECHAR *)aFrom) : NULL;
    }

public:

    /* Magic to workaround strict rules of par. 4.4.4 of the C++ standard */
    static const PRUnichar **__asInParam_Arr(const PRUnichar **aArr) { return aArr; }
};

template<>
struct SafeArrayTraits<nsID *>
{
protected:

    static void Init(nsID * &aElem) { aElem = NULL; }

    static void Uninit(nsID * &aElem)
    {
        if (aElem)
        {
            ::nsMemory::Free(aElem);
            aElem = NULL;
        }
    }

    static void Copy(const nsID * aFrom, nsID * &aTo)
    {
        if (aFrom)
        {
            aTo = (nsID *) ::nsMemory::Alloc(sizeof(nsID));
            if (aTo)
                *aTo = *aFrom;
        }
        else
            aTo = NULL;
    }

    /* This specification is also reused for SafeConstGUIDArray, so provide a
     * no-op Init() and Uninit() which are necessary for SafeArray<> but should
     * be never called in context of SafeConstGUIDArray. */

    static void Init(const nsID * &aElem) { NOREF(aElem); AssertFailed(); }
    static void Uninit(const nsID * &aElem) { NOREF(aElem); AssertFailed(); }

public:

    /** Magic to workaround strict rules of par. 4.4.4 of the C++ standard. */
    static const nsID **__asInParam_Arr(nsID **aArr)
    {
        return const_cast<const nsID **>(aArr);
    }
    static const nsID **__asInParam_Arr(const nsID **aArr) { return aArr; }
};

#else /* !VBOX_WITH_XPCOM */

////////////////////////////////////////////////////////////////////////////////

struct SafeArrayTraitsBase
{
protected:

    static SAFEARRAY *CreateSafeArray(VARTYPE aVarType, SAFEARRAYBOUND *aBound)
    { return SafeArrayCreate(aVarType, 1, aBound); }
};

/**
 * Provides various helpers for SafeArray.
 *
 * @param T Type of array elements.
 *
 * Specializations of this template must provide the following methods:
 *
    // Returns the VARTYPE of COM SafeArray elements to be used for T
    static VARTYPE VarType();

    // Returns the number of VarType() elements necessary for aSize
    // elements of T
    static ULONG VarCount(size_t aSize);

    // Returns the number of elements of T that fit into the given number of
    // VarType() elements (opposite to VarCount(size_t aSize)).
    static size_t Size(ULONG aVarCount);

    // Creates a deep copy of aFrom and stores it in aTo
    static void Copy(ULONG aFrom, ULONG &aTo);
 */
template<typename T>
struct SafeArrayTraits : public SafeArrayTraitsBase
{
protected:

    // Arbitrary types are treated as passed by value and each value is
    // represented by a number of VT_Ix type elements where VT_Ix has the
    // biggest possible bitness necessary to represent T w/o a gap. COM enums
    // fall into this category.

    static VARTYPE VarType()
    {
#ifdef VBOX_WITH_TYPE_TRAITS
        if (    std::is_integral<T>::value
            && !std::is_signed<T>::value)
        {
            if (sizeof(T) % 8 == 0) return VT_UI8;
            if (sizeof(T) % 4 == 0) return VT_UI4;
            if (sizeof(T) % 2 == 0) return VT_UI2;
            return VT_UI1;
        }
#endif
        if (sizeof(T) % 8 == 0) return VT_I8;
        if (sizeof(T) % 4 == 0) return VT_I4;
        if (sizeof(T) % 2 == 0) return VT_I2;
        return VT_I1;
    }

    /*
     * Fallback method in case type traits (VBOX_WITH_TYPE_TRAITS)
     * are not available. Always returns unsigned types.
     */
    static VARTYPE VarTypeUnsigned()
    {
        if (sizeof(T) % 8 == 0) return VT_UI8;
        if (sizeof(T) % 4 == 0) return VT_UI4;
        if (sizeof(T) % 2 == 0) return VT_UI2;
        return VT_UI1;
    }

    static ULONG VarCount(size_t aSize)
    {
        if (sizeof(T) % 8 == 0) return (ULONG)((sizeof(T) / 8) * aSize);
        if (sizeof(T) % 4 == 0) return (ULONG)((sizeof(T) / 4) * aSize);
        if (sizeof(T) % 2 == 0) return (ULONG)((sizeof(T) / 2) * aSize);
        return (ULONG)(sizeof(T) * aSize);
    }

    static size_t Size(ULONG aVarCount)
    {
        if (sizeof(T) % 8 == 0) return (size_t)(aVarCount * 8) / sizeof(T);
        if (sizeof(T) % 4 == 0) return (size_t)(aVarCount * 4) / sizeof(T);
        if (sizeof(T) % 2 == 0) return (size_t)(aVarCount * 2) / sizeof(T);
        return (size_t) aVarCount / sizeof(T);
    }

    static void Copy(T aFrom, T &aTo) { aTo = aFrom; }
};

template<typename T>
struct SafeArrayTraits<T *>
{
    // Arbitrary pointer types are not supported
};

/* Although the generic SafeArrayTraits template would work for all integers,
 * we specialize it for some of them in order to use the correct VT_ type */

template<>
struct SafeArrayTraits<LONG> : public SafeArrayTraitsBase
{
protected:

    static VARTYPE VarType() { return VT_I4; }
    static ULONG VarCount(size_t aSize) { return (ULONG)aSize; }
    static size_t Size(ULONG aVarCount) { return (size_t)aVarCount; }

    static void Copy(LONG aFrom, LONG &aTo) { aTo = aFrom; }
};

template<>
struct SafeArrayTraits<ULONG> : public SafeArrayTraitsBase
{
protected:

    static VARTYPE VarType() { return VT_UI4; }
    static ULONG VarCount(size_t aSize) { return (ULONG)aSize; }
    static size_t Size(ULONG aVarCount) { return (size_t)aVarCount; }

    static void Copy(ULONG aFrom, ULONG &aTo) { aTo = aFrom; }
};

template<>
struct SafeArrayTraits<LONG64> : public SafeArrayTraitsBase
{
protected:

    static VARTYPE VarType() { return VT_I8; }
    static ULONG VarCount(size_t aSize) { return (ULONG)aSize; }
    static size_t Size(ULONG aVarCount) { return (size_t)aVarCount; }

    static void Copy(LONG64 aFrom, LONG64 &aTo) { aTo = aFrom; }
};

template<>
struct SafeArrayTraits<ULONG64> : public SafeArrayTraitsBase
{
protected:

    static VARTYPE VarType() { return VT_UI8; }
    static ULONG VarCount(size_t aSize) { return (ULONG)aSize; }
    static size_t Size(ULONG aVarCount) { return (size_t)aVarCount; }

    static void Copy(ULONG64 aFrom, ULONG64 &aTo) { aTo = aFrom; }
};

template<>
struct SafeArrayTraits<BSTR> : public SafeArrayTraitsBase
{
protected:

    static VARTYPE VarType() { return VT_BSTR; }
    static ULONG VarCount(size_t aSize) { return (ULONG)aSize; }
    static size_t Size(ULONG aVarCount) { return (size_t)aVarCount; }

    static void Copy(BSTR aFrom, BSTR &aTo)
    {
        aTo = aFrom ? ::SysAllocString((const OLECHAR *)aFrom) : NULL;
    }
};

template<>
struct SafeArrayTraits<GUID> : public SafeArrayTraitsBase
{
protected:

    /* Use the 64-bit unsigned integer type for GUID */
    static VARTYPE VarType() { return VT_UI8; }

    /* GUID is 128 bit, so we need two VT_UI8 */
    static ULONG VarCount(size_t aSize)
    {
        AssertCompileSize(GUID, 16);
        return (ULONG)(aSize * 2);
    }

    static size_t Size(ULONG aVarCount) { return (size_t)aVarCount / 2; }

    static void Copy(GUID aFrom, GUID &aTo) { aTo = aFrom; }
};

/**
 * Helper for SafeArray::__asOutParam() that automatically updates m.raw after a
 * non-NULL m.arr assignment.
 */
class OutSafeArrayDipper
{
    OutSafeArrayDipper(SAFEARRAY **aArr, void **aRaw)
        : arr(aArr), raw(aRaw) { Assert(*aArr == NULL && *aRaw == NULL); }

    SAFEARRAY **arr;
    void **raw;

    template<class, class> friend class SafeArray;

public:

    ~OutSafeArrayDipper()
    {
        if (*arr != NULL)
        {
            HRESULT rc = SafeArrayAccessData(*arr, raw);
            AssertComRC(rc);
        }
    }

    operator SAFEARRAY **() { return arr; }
};

#endif /* !VBOX_WITH_XPCOM */

////////////////////////////////////////////////////////////////////////////////

/**
 * The SafeArray class represents the safe array type used in COM to pass arrays
 * to/from interface methods.
 *
 * This helper class hides all MSCOM/XPCOM specific implementation details and,
 * together with ComSafeArrayIn, ComSafeArrayOut and ComSafeArrayRet macros,
 * provides a platform-neutral way to handle safe arrays in the method
 * implementation.
 *
 * When an instance of this class is destroyed, it automatically frees all
 * resources occupied by individual elements of the array as well as by the
 * array itself. However, when the value of an element is manually changed
 * using #operator[] or by accessing array data through the #raw() pointer, it is
 * the caller's responsibility to free resources occupied by the previous
 * element's value.
 *
 * Also, objects of this class do not support copy and assignment operations and
 * therefore cannot be returned from functions by value. In other words, this
 * class is just a temporary storage for handling interface method calls and not
 * intended to be used to store arrays as data members and such -- you should
 * use normal list/vector classes for that.
 *
 * @note The current implementation supports only one-dimensional arrays.
 *
 * @note This class is not thread-safe.
 */
template<typename T, class Traits = SafeArrayTraits<T> >
class SafeArray : public Traits
{
public:

    /**
     * Creates a null array.
     */
    SafeArray() { }

    /**
     * Creates a new array of the given size. All elements of the newly created
     * array initialized with null values.
     *
     * @param aSize     Initial number of elements in the array.
     *
     * @note If this object remains null after construction it means that there
     *       was not enough memory for creating an array of the requested size.
     *       The constructor will also assert in this case.
     */
    SafeArray(size_t aSize) { resize(aSize); }

    /**
     * Weakly attaches this instance to the existing array passed in a method
     * parameter declared using the ComSafeArrayIn macro. When using this call,
     * always wrap the parameter name in the ComSafeArrayInArg macro call like
     * this:
     * <pre>
     *  SafeArray safeArray(ComSafeArrayInArg(aArg));
     * </pre>
     *
     * Note that this constructor doesn't take the ownership of the array. In
     * particular, it means that operations that operate on the ownership (e.g.
     * #detachTo()) are forbidden and will assert.
     *
     * @param aArg  Input method parameter to attach to.
     */
    SafeArray(ComSafeArrayIn(T, aArg))
    {
        if (aArg)
        {
#ifdef VBOX_WITH_XPCOM

            m.size = aArgSize;
            m.arr = aArg;
            m.isWeak = true;

#else /* !VBOX_WITH_XPCOM */

            SAFEARRAY *arg = aArg;

            AssertReturnVoid(arg->cDims == 1);

            VARTYPE vt;
            HRESULT rc = SafeArrayGetVartype(arg, &vt);
            AssertComRCReturnVoid(rc);
# ifndef VBOX_WITH_TYPE_TRAITS
            AssertMsgReturnVoid(   vt == Traits::VarType()
                                || vt == Traits::VarTypeUnsigned(),
                                ("Expected vartype %d or %d, got %d.\n",
                                 Traits::VarType(), Traits::VarTypeUnsigned(), vt));
# else  /* VBOX_WITH_TYPE_TRAITS */
            AssertMsgReturnVoid(vt == Traits::VarType(),
                                ("Expected vartype %d, got %d.\n",
                                 Traits::VarType(), vt));
# endif /* VBOX_WITH_TYPE_TRAITS */
            rc = SafeArrayAccessData(arg, (void HUGEP **)&m.raw);
            AssertComRCReturnVoid(rc);

            m.arr = arg;
            m.isWeak = true;

#endif /* !VBOX_WITH_XPCOM */
        }
    }

    /**
     * Creates a deep copy of the given standard C++ container that stores
     * T objects.
     *
     * @param aCntr Container object to copy.
     *
     * @tparam C    Standard C++ container template class (normally deduced from
     *              @c aCntr).
     */
    template<template<typename, typename> class C, class A>
    SafeArray(const C<T, A> & aCntr)
    {
        resize(aCntr.size());
        AssertReturnVoid(!isNull());

        size_t i = 0;
        for (typename C<T, A>::const_iterator it = aCntr.begin();
             it != aCntr.end(); ++ it, ++ i)
#ifdef VBOX_WITH_XPCOM
            SafeArray::Copy(*it, m.arr[i]);
#else
            SafeArray::Copy(*it, m.raw[i]);
#endif
    }

    /**
     * Creates a deep copy of the given standard C++ map that stores T objects
     * as values.
     *
     * @param aMap  Map object to copy.
     *
     * @tparam C    Standard C++ map template class (normally deduced from
     *              @a aMap).
     * @tparam L    Standard C++ compare class (deduced from @a aMap).
     * @tparam A    Standard C++ allocator class (deduced from @a aMap).
     * @tparam K    Map key class (deduced from @a aMap).
     */
    template<template<typename, typename, typename, typename>
              class C, class L, class A, class K>
    SafeArray(const C<K, T, L, A> & aMap)
    {
        typedef C<K, T, L, A> Map;

        resize(aMap.size());
        AssertReturnVoid(!isNull());

        size_t i = 0;
        for (typename Map::const_iterator it = aMap.begin();
             it != aMap.end(); ++ it, ++ i)
#ifdef VBOX_WITH_XPCOM
            SafeArray::Copy(it->second, m.arr[i]);
#else
            SafeArray::Copy(it->second, m.raw[i]);
#endif
    }

    /**
     * Destroys this instance after calling #setNull() to release allocated
     * resources. See #setNull() for more details.
     */
    virtual ~SafeArray() { setNull(); }

    /**
     * Returns @c true if this instance represents a null array.
     */
    bool isNull() const { return m.arr == NULL; }

    /**
     * Returns @c true if this instance does not represents a null array.
     */
    bool isNotNull() const { return m.arr != NULL; }

    /**
     * Resets this instance to null and, if this instance is not a weak one,
     * releases any resources occupied by the array data.
     *
     * @note This method destroys (cleans up) all elements of the array using
     *       the corresponding cleanup routine for the element type before the
     *       array itself is destroyed.
     */
    virtual void setNull() { m.uninit(); }

    /**
     * Returns @c true if this instance is weak. A weak instance doesn't own the
     * array data and therefore operations manipulating the ownership (e.g.
     * #detachTo()) are forbidden and will assert.
     */
    bool isWeak() const { return m.isWeak; }

    /** Number of elements in the array. */
    size_t size() const
    {
#ifdef VBOX_WITH_XPCOM
        if (m.arr)
            return m.size;
        return 0;
#else
        if (m.arr)
            return Traits::Size(m.arr->rgsabound[0].cElements);
        return 0;
#endif
    }

    /**
     * Prepends a copy of the given element at the beginning of the array.
     *
     * The array size is increased by one by this method and the additional
     * space is allocated as needed.
     *
     * This method is handy in cases where you want to assign a copy of the
     * existing value to the array element, for example:
     * <tt>Bstr string; array.push_front(string);</tt>. If you create a string
     * just to put it in the array, you may find #appendedRaw() more useful.
     *
     * @param aElement Element to prepend.
     *
     * @return          @c true on success and @c false if there is not enough
     *                  memory for resizing.
     */
    bool push_front(const T &aElement)
    {
        if (!ensureCapacity(size() + 1))
            return false;

#ifdef VBOX_WITH_XPCOM
        /* For XPCOM, size() is 0 and capacity() is 16 for the first time
         * this function is being called on an empty array.
         * See implementation of ensureCapacity(). */
        for (size_t i = size(); i > 0; --i)
            SafeArray::Copy(m.arr[i - 1], m.arr[i]);
#else
        /* For Windows (COM), size() always matches the array's capacity.
         *
         * So we here need to make sure we don't read beyond the array
         * if this function is being called on an empty array
         * (size is 1 and  there is no element on index 1 yet). */
        for (size_t i = size() - 1; i > 0; --i)
            SafeArray::Copy(m.raw[i - 1], m.raw[i]);
#endif

#ifdef VBOX_WITH_XPCOM
        SafeArray::Copy(aElement, m.arr[0]);
        ++ m.size;
#else
        SafeArray::Copy(aElement, m.raw[0]);
#endif
        return true;
    }

    /**
     * Appends a copy of the given element at the end of the array.
     *
     * The array size is increased by one by this method and the additional
     * space is allocated as needed.
     *
     * This method is handy in cases where you want to assign a copy of the
     * existing value to the array element, for example:
     * <tt>Bstr string; array.push_back(string);</tt>. If you create a string
     * just to put it in the array, you may find #appendedRaw() more useful.
     *
     * @param aElement Element to append.
     *
     * @return          @c true on success and @c false if there is not enough
     *                  memory for resizing.
     */
    bool push_back(const T &aElement)
    {
        if (!ensureCapacity(size() + 1))
            return false;

#ifdef VBOX_WITH_XPCOM
        SafeArray::Copy(aElement, m.arr[m.size]);
        ++ m.size;
#else
        SafeArray::Copy(aElement, m.raw[size() - 1]);
#endif
        return true;
    }

    /**
     * Appends an empty element at the end of the array and returns a raw
     * pointer to it suitable for assigning a raw value (w/o constructing a
     * copy).
     *
     * The array size is increased by one by this method and the additional
     * space is allocated as needed.
     *
     * Note that in case of raw assignment, value ownership (for types with
     * dynamically allocated data and for interface pointers) is transferred to
     * the safe array object.
     *
     * This method is handy for operations like
     * <tt>Bstr("foo").detachTo(array.appendedRaw());</tt>. Don't use it as
     * an l-value (<tt>array.appendedRaw() = SysAllocString(L"tralala");</tt>)
     * since this doesn't check for a NULL condition; use #resize() instead. If
     * you need to assign a copy of the existing value instead of transferring
     * the ownership, look at #push_back().
     *
     * @return          Raw pointer to the added element or NULL if no memory.
     */
    T *appendedRaw()
    {
        if (!ensureCapacity(size() + 1))
            return NULL;

#ifdef VBOX_WITH_XPCOM
        SafeArray::Init(m.arr[m.size]);
        ++ m.size;
        return &m.arr[m.size - 1];
#else
        /* nothing to do here, SafeArrayCreate() has performed element
         * initialization */
        return &m.raw[size() - 1];
#endif
    }

    /**
     * Resizes the array preserving its contents when possible. If the new size
     * is larger than the old size, new elements are initialized with null
     * values. If the new size is less than the old size, the contents of the
     * array beyond the new size is lost.
     *
     * @param aNewSize  New number of elements in the array.
     * @return          @c true on success and @c false if there is not enough
     *                  memory for resizing.
     */
    bool resize(size_t aNewSize)
    {
        if (!ensureCapacity(aNewSize))
            return false;

#ifdef VBOX_WITH_XPCOM

        if (m.size < aNewSize)
        {
            /* initialize the new elements */
            for (size_t i = m.size; i < aNewSize; ++ i)
                SafeArray::Init(m.arr[i]);
        }

        /** @todo Fix this! */
        m.size = (PRUint32)aNewSize;
#else
        /* nothing to do here, SafeArrayCreate() has performed element
         * initialization */
#endif
        return true;
    }

    /**
     * Reinitializes this instance by preallocating space for the given number
     * of elements. The previous array contents is lost.
     *
     * @param aNewSize  New number of elements in the array.
     * @return          @c true on success and @c false if there is not enough
     *                  memory for resizing.
     */
    bool reset(size_t aNewSize)
    {
        m.uninit();
        return resize(aNewSize);
    }

    /**
     * Returns a pointer to the raw array data. Use this raw pointer with care
     * as no type or bound checking is done for you in this case.
     *
     * @note This method returns @c NULL when this instance is null.
     * @see #operator[]
     */
    T *raw()
    {
#ifdef VBOX_WITH_XPCOM
        return m.arr;
#else
        return m.raw;
#endif
    }

    /**
     * Const version of #raw().
     */
    const T *raw() const
    {
#ifdef VBOX_WITH_XPCOM
        return m.arr;
#else
        return m.raw;
#endif
    }

    /**
     * Array access operator that returns an array element by reference. A bit
     * safer than #raw(): asserts and returns a reference to a static zero
     * element (const, i.e. writes will fail) if this instance is null or
     * if the index is out of bounds.
     *
     * @note For weak instances, this call will succeed but the behavior of
     *       changing the contents of an element of the weak array instance is
     *       undefined and may lead to a program crash on some platforms.
     */
    T &operator[] (size_t aIdx)
    {
        /** @todo r=klaus should do this as a AssertCompile, but cannot find a way which works. */
        Assert(sizeof(T) <= sizeof(Zeroes));
        AssertReturn(m.arr != NULL, *(T *)&Zeroes[0]);
        AssertReturn(aIdx < size(), *(T *)&Zeroes[0]);
#ifdef VBOX_WITH_XPCOM
        return m.arr[aIdx];
#else
        AssertReturn(m.raw != NULL, *(T *)&Zeroes[0]);
        return m.raw[aIdx];
#endif
    }

    /**
     * Const version of #operator[] that returns an array element by value.
     */
    const T operator[] (size_t aIdx) const
    {
        AssertReturn(m.arr != NULL, *(const T *)&Zeroes[0]);
        AssertReturn(aIdx < size(), *(const T *)&Zeroes[0]);
#ifdef VBOX_WITH_XPCOM
        return m.arr[aIdx];
#else
        AssertReturn(m.raw != NULL, *(const T *)&Zeroes[0]);
        return m.raw[aIdx];
#endif
    }

    /**
     * Creates a copy of this array and stores it in a method parameter declared
     * using the ComSafeArrayOut macro. When using this call, always wrap the
     * parameter name in the ComSafeArrayOutArg macro call like this:
     * <pre>
     *  safeArray.cloneTo(ComSafeArrayOutArg(aArg));
     * </pre>
     *
     * @note It is assumed that the ownership of the returned copy is
     * transferred to the caller of the method and he is responsible to free the
     * array data when it is no longer needed.
     *
     * @param aArg  Output method parameter to clone to.
     */
    virtual const SafeArray &cloneTo(ComSafeArrayOut(T, aArg)) const
    {
        /// @todo Implement me!
#ifdef VBOX_WITH_XPCOM
        NOREF(aArgSize);
        NOREF(aArg);
#else
        NOREF(aArg);
#endif
        AssertFailedReturn(*this);
    }

    HRESULT cloneTo(SafeArray<T>& aOther) const
    {
        aOther.reset(size());
        return aOther.initFrom(*this);
    }


    /**
     * Transfers the ownership of this array's data to the specified location
     * declared using the ComSafeArrayOut macro and makes this array a null
     * array. When using this call, always wrap the parameter name in the
     * ComSafeArrayOutArg macro call like this:
     * <pre>
     *  safeArray.detachTo(ComSafeArrayOutArg(aArg));
     * </pre>
     *
     * Detaching the null array is also possible in which case the location will
     * receive NULL.
     *
     * @note Since the ownership of the array data is transferred to the
     * caller of the method, he is responsible to free the array data when it is
     * no longer needed.
     *
     * @param aArg  Location to detach to.
     */
    virtual SafeArray &detachTo(ComSafeArrayOut(T, aArg))
    {
        AssertReturn(!m.isWeak, *this);

#ifdef VBOX_WITH_XPCOM

        AssertReturn(aArgSize != NULL, *this);
        AssertReturn(aArg != NULL, *this);

        *aArgSize = m.size;
        *aArg = m.arr;

        m.isWeak = false;
        m.size = 0;
        m.arr = NULL;

#else /* !VBOX_WITH_XPCOM */

        AssertReturn(aArg != NULL, *this);
        *aArg = m.arr;

        if (m.raw)
        {
            HRESULT rc = SafeArrayUnaccessData(m.arr);
            AssertComRCReturn(rc, *this);
            m.raw = NULL;
        }

        m.isWeak = false;
        m.arr = NULL;

#endif /* !VBOX_WITH_XPCOM */

        return *this;
    }

    /**
     * Returns a copy of this SafeArray as RTCList<T>.
     */
    RTCList<T> toList()
    {
        RTCList<T> list(size());
        for (size_t i = 0; i < size(); ++i)
#ifdef VBOX_WITH_XPCOM
            list.append(m.arr[i]);
#else
            list.append(m.raw[i]);
#endif
        return list;
    }

    inline HRESULT initFrom(const com::SafeArray<T> & aRef);
    inline HRESULT initFrom(const T* aPtr, size_t aSize);

    // Public methods for internal purposes only.

#ifdef VBOX_WITH_XPCOM

    /** Internal function. Never call it directly. */
    PRUint32 *__asOutParam_Size() { setNull(); return &m.size; }

    /** Internal function Never call it directly. */
    T **__asOutParam_Arr() { Assert(isNull()); return &m.arr; }

#else /* !VBOX_WITH_XPCOM */

    /** Internal function Never call it directly. */
    SAFEARRAY * __asInParam() { return m.arr; }

    /** Internal function Never call it directly. */
    OutSafeArrayDipper __asOutParam()
    { setNull(); return OutSafeArrayDipper(&m.arr, (void **)&m.raw); }

#endif /* !VBOX_WITH_XPCOM */

    static const SafeArray Null;

protected:

    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(SafeArray);

    /**
     * Ensures that the array is big enough to contain aNewSize elements.
     *
     * If the new size is greater than the current capacity, a new array is
     * allocated and elements from the old array are copied over. The size of
     * the array doesn't change, only the capacity increases (which is always
     * greater than the size). Note that the additionally allocated elements are
     * left uninitialized by this method.
     *
     * If the new size is less than the current size, the existing array is
     * truncated to the specified size and the elements outside the new array
     * boundary are freed.
     *
     * If the new size is the same as the current size, nothing happens.
     *
     * @param aNewSize  New size of the array.
     *
     * @return @c true on success and @c false if not enough memory.
     */
    bool ensureCapacity(size_t aNewSize)
    {
        AssertReturn(!m.isWeak, false);

#ifdef VBOX_WITH_XPCOM

        /* Note: we distinguish between a null array and an empty (zero
         * elements) array. Therefore we never use zero in malloc (even if
         * aNewSize is zero) to make sure we get a non-null pointer. */

        if (m.size == aNewSize && m.arr != NULL)
            return true;

        /* Allocate in 16-byte pieces. */
        size_t newCapacity = RT_MAX((aNewSize + 15) / 16 * 16, 16);

        if (m.capacity != newCapacity)
        {
            T *newArr = (T *)nsMemory::Alloc(RT_MAX(newCapacity, 1) * sizeof(T));
            AssertReturn(newArr != NULL, false);

            if (m.arr != NULL)
            {
                if (m.size > aNewSize)
                {
                    /* Truncation takes place, uninit exceeding elements and
                     * shrink the size. */
                    for (size_t i = aNewSize; i < m.size; ++ i)
                        SafeArray::Uninit(m.arr[i]);

                    /** @todo Fix this! */
                    m.size = (PRUint32)aNewSize;
                }

                /* Copy the old contents. */
                memcpy(newArr, m.arr, m.size * sizeof(T));
                nsMemory::Free((void *)m.arr);
            }

            m.arr = newArr;
        }
        else
        {
            if (m.size > aNewSize)
            {
                /* Truncation takes place, uninit exceeding elements and
                 * shrink the size. */
                for (size_t i = aNewSize; i < m.size; ++ i)
                    SafeArray::Uninit(m.arr[i]);

                /** @todo Fix this! */
                m.size = (PRUint32)aNewSize;
            }
        }

        /** @todo Fix this! */
        m.capacity = (PRUint32)newCapacity;

#else

        SAFEARRAYBOUND bound = { Traits::VarCount(aNewSize), 0 };
        HRESULT rc;

        if (m.arr == NULL)
        {
            m.arr = Traits::CreateSafeArray(Traits::VarType(), &bound);
            AssertReturn(m.arr != NULL, false);
        }
        else
        {
            SafeArrayUnaccessData(m.arr);

            rc = SafeArrayRedim(m.arr, &bound);
            AssertComRCReturn(rc == S_OK, false);
        }

        rc = SafeArrayAccessData(m.arr, (void HUGEP **)&m.raw);
        AssertComRCReturn(rc, false);

#endif
        return true;
    }

    struct Data
    {
        Data()
            : isWeak(false)
#ifdef VBOX_WITH_XPCOM
            , capacity(0), size(0), arr(NULL)
#else
            , arr(NULL), raw(NULL)
#endif
        {}

        ~Data() { uninit(); }

        void uninit()
        {
#ifdef VBOX_WITH_XPCOM

            if (arr)
            {
                if (!isWeak)
                {
                    for (size_t i = 0; i < size; ++ i)
                        SafeArray::Uninit(arr[i]);

                    nsMemory::Free((void *)arr);
                }
                else
                    isWeak = false;

                arr = NULL;
            }

            size = capacity = 0;

#else /* !VBOX_WITH_XPCOM */

            if (arr)
            {
                if (raw)
                {
                    SafeArrayUnaccessData(arr);
                    raw = NULL;
                }

                if (!isWeak)
                {
                    HRESULT rc = SafeArrayDestroy(arr);
                    AssertComRCReturnVoid(rc);
                }
                else
                    isWeak = false;

                arr = NULL;
            }

#endif /* !VBOX_WITH_XPCOM */
        }

        bool isWeak : 1;

#ifdef VBOX_WITH_XPCOM
        PRUint32 capacity;
        PRUint32 size;
        T *arr;
#else
        SAFEARRAY *arr;
        T *raw;
#endif
    };

    Data m;
};

/* Few fast specializations for primitive array types */
template<>
inline HRESULT com::SafeArray<BYTE>::initFrom(const com::SafeArray<BYTE> & aRef)
{
    size_t sSize = aRef.size();
    if (resize(sSize))
    {
        ::memcpy(raw(), aRef.raw(), sSize);
        return S_OK;
    }
    return E_OUTOFMEMORY;
}
template<>
inline HRESULT com::SafeArray<BYTE>::initFrom(const BYTE *aPtr, size_t aSize)
{
    if (resize(aSize))
    {
        ::memcpy(raw(), aPtr, aSize);
        return S_OK;
    }
    return E_OUTOFMEMORY;
}


template<>
inline HRESULT com::SafeArray<SHORT>::initFrom(const com::SafeArray<SHORT> & aRef)
{
    size_t sSize = aRef.size();
    if (resize(sSize))
    {
        ::memcpy(raw(), aRef.raw(), sSize * sizeof(SHORT));
        return S_OK;
    }
    return E_OUTOFMEMORY;
}
template<>
inline HRESULT com::SafeArray<SHORT>::initFrom(const SHORT *aPtr, size_t aSize)
{
    if (resize(aSize))
    {
        ::memcpy(raw(), aPtr, aSize * sizeof(SHORT));
        return S_OK;
    }
    return E_OUTOFMEMORY;
}

template<>
inline HRESULT com::SafeArray<USHORT>::initFrom(const com::SafeArray<USHORT> & aRef)
{
    size_t sSize = aRef.size();
    if (resize(sSize))
    {
        ::memcpy(raw(), aRef.raw(), sSize * sizeof(USHORT));
        return S_OK;
    }
    return E_OUTOFMEMORY;
}
template<>
inline HRESULT com::SafeArray<USHORT>::initFrom(const USHORT *aPtr, size_t aSize)
{
    if (resize(aSize))
    {
        ::memcpy(raw(), aPtr, aSize * sizeof(USHORT));
        return S_OK;
    }
    return E_OUTOFMEMORY;
}

template<>
inline HRESULT com::SafeArray<LONG>::initFrom(const com::SafeArray<LONG> & aRef)
{
    size_t sSize = aRef.size();
    if (resize(sSize))
    {
        ::memcpy(raw(), aRef.raw(), sSize * sizeof(LONG));
        return S_OK;
    }
    return E_OUTOFMEMORY;
}
template<>
inline HRESULT com::SafeArray<LONG>::initFrom(const LONG *aPtr, size_t aSize)
{
    if (resize(aSize))
    {
        ::memcpy(raw(), aPtr, aSize * sizeof(LONG));
        return S_OK;
    }
    return E_OUTOFMEMORY;
}


////////////////////////////////////////////////////////////////////////////////

#ifdef VBOX_WITH_XPCOM

/**
 * Version of com::SafeArray for arrays of GUID.
 *
 * In MS COM, GUID arrays store GUIDs by value and therefore input arrays are
 * represented using |GUID *| and out arrays -- using |GUID **|. In XPCOM,
 * GUID arrays store pointers to nsID so that input arrays are |const nsID **|
 * and out arrays are |nsID ***|. Due to this difference, it is impossible to
 * work with arrays of GUID on both platforms by simply using com::SafeArray
 * <GUID>. This class is intended to provide some level of cross-platform
 * behavior.
 *
 * The basic usage pattern is basically similar to com::SafeArray<> except that
 * you use ComSafeGUIDArrayIn* and ComSafeGUIDArrayOut* macros instead of
 * ComSafeArrayIn* and ComSafeArrayOut*. Another important nuance is that the
 * raw() array type is different (nsID **, or GUID ** on XPCOM and GUID * on MS
 * COM) so it is recommended to use operator[] instead which always returns a
 * GUID by value.
 *
 * Note that due to const modifiers, you cannot use SafeGUIDArray for input GUID
 * arrays. Please use SafeConstGUIDArray for this instead.
 *
 * Other than mentioned above, the functionality of this class is equivalent to
 * com::SafeArray<>. See the description of that template and its methods for
 * more information.
 *
 * Output GUID arrays are handled by a separate class, SafeGUIDArrayOut, since
 * this class cannot handle them because of const modifiers.
 */
class SafeGUIDArray : public SafeArray<nsID *>
{
public:

    typedef SafeArray<nsID *> Base;

    class nsIDRef
    {
    public:

        nsIDRef(nsID * &aVal) : mVal(aVal) { AssertCompile(sizeof(nsID) <= sizeof(Zeroes)); }

        operator const nsID &() const { return mVal ? *mVal : *(const nsID *)&Zeroes[0]; }
        operator nsID() const { return mVal ? *mVal : *(nsID *)&Zeroes[0]; }

        const nsID *operator&() const { return mVal ? mVal : (const nsID *)&Zeroes[0]; }

        nsIDRef &operator= (const nsID &aThat)
        {
            if (mVal == NULL)
                SafeGUIDArray::Copy(&aThat, mVal);
            else
                *mVal = aThat;
            return *this;
        }

    private:

        nsID * &mVal;

        friend class SafeGUIDArray;
    };

    /** See SafeArray<>::SafeArray(). */
    SafeGUIDArray() {}

    /** See SafeArray<>::SafeArray(size_t). */
    SafeGUIDArray(size_t aSize) : Base(aSize) {}

    /**
     * Array access operator that returns an array element by reference. As a
     * special case, the return value of this operator on XPCOM is an nsID (GUID)
     * reference, instead of an nsID pointer (the actual SafeArray template
     * argument), for compatibility with the MS COM version.
     *
     * The rest is equivalent to SafeArray<>::operator[].
     */
    nsIDRef operator[] (size_t aIdx)
    {
        Assert(m.arr != NULL);
        Assert(aIdx < size());
        return nsIDRef(m.arr[aIdx]);
    }

    /**
    * Const version of #operator[] that returns an array element by value.
    */
    const nsID &operator[] (size_t aIdx) const
    {
        Assert(m.arr != NULL);
        Assert(aIdx < size());
        return m.arr[aIdx] ? *m.arr[aIdx] : *(const nsID *)&Zeroes[0];
    }
};

/**
 * Version of com::SafeArray for const arrays of GUID.
 *
 * This class is used to work with input GUID array parameters in method
 * implementations. See SafeGUIDArray for more details.
 */
class SafeConstGUIDArray : public SafeArray<const nsID *,
                                            SafeArrayTraits<nsID *> >
{
public:

    typedef SafeArray<const nsID *, SafeArrayTraits<nsID *> > Base;

    /** See SafeArray<>::SafeArray(). */
    SafeConstGUIDArray() { AssertCompile(sizeof(nsID) <= sizeof(Zeroes)); }

    /* See SafeArray<>::SafeArray(ComSafeArrayIn(T, aArg)). */
    SafeConstGUIDArray(ComSafeGUIDArrayIn(aArg))
        : Base(ComSafeGUIDArrayInArg(aArg)) {}

    /**
     * Array access operator that returns an array element by reference. As a
     * special case, the return value of this operator on XPCOM is nsID (GUID)
     * instead of nsID *, for compatibility with the MS COM version.
     *
     * The rest is equivalent to SafeArray<>::operator[].
     */
    const nsID &operator[] (size_t aIdx) const
    {
        AssertReturn(m.arr != NULL,  *(const nsID *)&Zeroes[0]);
        AssertReturn(aIdx < size(), *(const nsID *)&Zeroes[0]);
        return *m.arr[aIdx];
    }

private:

    /* These are disabled because of const. */
    bool reset(size_t aNewSize) { NOREF(aNewSize); return false; }
};

#else /* !VBOX_WITH_XPCOM */

typedef SafeArray<GUID> SafeGUIDArray;
typedef SafeArray<const GUID, SafeArrayTraits<GUID> > SafeConstGUIDArray;

#endif /* !VBOX_WITH_XPCOM */

////////////////////////////////////////////////////////////////////////////////

#ifdef VBOX_WITH_XPCOM

template<class I>
struct SafeIfaceArrayTraits
{
protected:

    static void Init(I * &aElem) { aElem = NULL; }
    static void Uninit(I * &aElem)
    {
        if (aElem)
        {
            aElem->Release();
            aElem = NULL;
        }
    }

    static void Copy(I * aFrom, I * &aTo)
    {
        if (aFrom != NULL)
        {
            aTo = aFrom;
            aTo->AddRef();
        }
        else
            aTo = NULL;
    }

public:

    /* Magic to workaround strict rules of par. 4.4.4 of the C++ standard. */
    static I **__asInParam_Arr(I **aArr) { return aArr; }
    static I **__asInParam_Arr(const I **aArr) { return const_cast<I **>(aArr); }
};

#else /* !VBOX_WITH_XPCOM */

template<class I>
struct SafeIfaceArrayTraits
{
protected:

    static VARTYPE VarType() { return VT_DISPATCH; }
    static ULONG VarCount(size_t aSize) { return (ULONG)aSize; }
    static size_t Size(ULONG aVarCount) { return (size_t)aVarCount; }

    static void Copy(I * aFrom, I * &aTo)
    {
        if (aFrom != NULL)
        {
            aTo = aFrom;
            aTo->AddRef();
        }
        else
            aTo = NULL;
    }

    static SAFEARRAY *CreateSafeArray(VARTYPE aVarType, SAFEARRAYBOUND *aBound)
    {
        NOREF(aVarType);
        return SafeArrayCreateEx(VT_DISPATCH, 1, aBound, (PVOID)&COM_IIDOF(I));
    }
};

#endif /* !VBOX_WITH_XPCOM */

////////////////////////////////////////////////////////////////////////////////

/**
 * Version of com::SafeArray for arrays of interface pointers.
 *
 * Except that it manages arrays of interface pointers, the usage of this class
 * is identical to com::SafeArray.
 *
 * @param I     Interface class (no asterisk).
 */
template<class I>
class SafeIfaceArray : public SafeArray<I *, SafeIfaceArrayTraits<I> >
{
public:

    typedef SafeArray<I *, SafeIfaceArrayTraits<I> > Base;

    /**
     * Creates a null array.
     */
    SafeIfaceArray() {}

    /**
     * Creates a new array of the given size. All elements of the newly created
     * array initialized with null values.
     *
     * @param aSize     Initial number of elements in the array. Must be greater
     *                  than 0.
     *
     * @note If this object remains null after construction it means that there
     *       was not enough memory for creating an array of the requested size.
     *       The constructor will also assert in this case.
     */
    SafeIfaceArray(size_t aSize) { Base::resize(aSize); }

    /**
     * Weakly attaches this instance to the existing array passed in a method
     * parameter declared using the ComSafeArrayIn macro. When using this call,
     * always wrap the parameter name in the ComSafeArrayOutArg macro call like
     * this:
     * <pre>
     *  SafeArray safeArray(ComSafeArrayInArg(aArg));
     * </pre>
     *
     * Note that this constructor doesn't take the ownership of the array. In
     * particular, this means that operations that operate on the ownership
     * (e.g. #detachTo()) are forbidden and will assert.
     *
     * @param aArg  Input method parameter to attach to.
     */
    SafeIfaceArray(ComSafeArrayIn(I *, aArg))
    {
        if (aArg)
        {
#ifdef VBOX_WITH_XPCOM

            Base::m.size = aArgSize;
            Base::m.arr = aArg;
            Base::m.isWeak = true;

#else /* !VBOX_WITH_XPCOM */

            SAFEARRAY *arg = aArg;

            AssertReturnVoid(arg->cDims == 1);

            VARTYPE vt;
            HRESULT rc = SafeArrayGetVartype(arg, &vt);
            AssertComRCReturnVoid(rc);
            AssertMsgReturnVoid(vt == VT_UNKNOWN || vt == VT_DISPATCH,
                                ("Expected vartype VT_UNKNOWN or VT_DISPATCH, got %d.\n",
                                 vt));
            GUID guid;
            rc = SafeArrayGetIID(arg, &guid);
            AssertComRCReturnVoid(rc);
            AssertMsgReturnVoid(InlineIsEqualGUID(COM_IIDOF(I), guid) || arg->rgsabound[0].cElements == 0 /* IDispatch if empty */,
                                ("Expected IID {%RTuuid}, got {%RTuuid}.\n", &COM_IIDOF(I), &guid));

            rc = SafeArrayAccessData(arg, (void HUGEP **)&this->m.raw);
            AssertComRCReturnVoid(rc);

            this->m.arr = arg;
            this->m.isWeak = true;

#endif /* !VBOX_WITH_XPCOM */
        }
    }

    /**
     * Creates a deep copy of the given standard C++ container that stores
     * interface pointers as objects of the ComPtr\<I\> class.
     *
     * @param aCntr Container object to copy.
     *
     * @tparam C    Standard C++ container template class (normally deduced from
     *              @c aCntr).
     * @tparam A    Standard C++ allocator class (deduced from @c aCntr).
     * @tparam OI   Argument to the ComPtr template (deduced from @c aCntr).
     */
    template<template<typename, typename> class C, class A, class OI>
    SafeIfaceArray(const C<ComPtr<OI>, A> & aCntr)
    {
        typedef C<ComPtr<OI>, A> List;

        Base::resize(aCntr.size());
        AssertReturnVoid(!Base::isNull());

        size_t i = 0;
        for (typename List::const_iterator it = aCntr.begin(); it != aCntr.end(); ++it, ++i)
#ifdef VBOX_WITH_XPCOM
            SafeIfaceArray::Copy(*it, Base::m.arr[i]);
#else
            SafeIfaceArray::Copy(*it, Base::m.raw[i]);
#endif
    }

    /**
     * Creates a deep copy of the given standard C++ container that stores
     * interface pointers as objects of the ComObjPtr\<I\> class.
     *
     * @param aCntr Container object to copy.
     *
     * @tparam C    Standard C++ container template class (normally deduced from
     *              @c aCntr).
     * @tparam A    Standard C++ allocator class (deduced from @c aCntr).
     * @tparam OI   Argument to the ComObjPtr template (deduced from @c aCntr).
     */
    template<template<typename, typename> class C, class A, class OI>
    SafeIfaceArray(const C<ComObjPtr<OI>, A> & aCntr)
    {
        typedef C<ComObjPtr<OI>, A> List;

        Base::resize(aCntr.size());
        AssertReturnVoid(!Base::isNull());

        size_t i = 0;
        for (typename List::const_iterator it = aCntr.begin(); it != aCntr.end(); ++it, ++i)
#ifdef VBOX_WITH_XPCOM
            SafeIfaceArray::Copy(*it, Base::m.arr[i]);
#else
            SafeIfaceArray::Copy(*it, Base::m.raw[i]);
#endif
    }

    /**
     * Creates a deep copy of the given standard C++ map whose values are
     * interface pointers stored as objects of the ComPtr\<I\> class.
     *
     * @param aMap  Map object to copy.
     *
     * @tparam C    Standard C++ map template class (normally deduced from
     *              @c aCntr).
     * @tparam L    Standard C++ compare class (deduced from @c aCntr).
     * @tparam A    Standard C++ allocator class (deduced from @c aCntr).
     * @tparam K    Map key class (deduced from @c aCntr).
     * @tparam OI   Argument to the ComPtr template (deduced from @c aCntr).
     */
    template<template<typename, typename, typename, typename>
              class C, class L, class A, class K, class OI>
    SafeIfaceArray(const C<K, ComPtr<OI>, L, A> & aMap)
    {
        typedef C<K, ComPtr<OI>, L, A> Map;

        Base::resize(aMap.size());
        AssertReturnVoid(!Base::isNull());

        size_t i = 0;
        for (typename Map::const_iterator it = aMap.begin(); it != aMap.end(); ++it, ++i)
#ifdef VBOX_WITH_XPCOM
            SafeIfaceArray::Copy(it->second, Base::m.arr[i]);
#else
            SafeIfaceArray::Copy(it->second, Base::m.raw[i]);
#endif
    }

    /**
     * Creates a deep copy of the given standard C++ map whose values are
     * interface pointers stored as objects of the ComObjPtr\<I\> class.
     *
     * @param aMap  Map object to copy.
     *
     * @tparam C    Standard C++ map template class (normally deduced from
     *              @c aCntr).
     * @tparam L    Standard C++ compare class (deduced from @c aCntr).
     * @tparam A    Standard C++ allocator class (deduced from @c aCntr).
     * @tparam K    Map key class (deduced from @c aCntr).
     * @tparam OI   Argument to the ComObjPtr template (deduced from @c aCntr).
     */
    template<template<typename, typename, typename, typename>
              class C, class L, class A, class K, class OI>
    SafeIfaceArray(const C<K, ComObjPtr<OI>, L, A> & aMap)
    {
        typedef C<K, ComObjPtr<OI>, L, A> Map;

        Base::resize(aMap.size());
        AssertReturnVoid(!Base::isNull());

        size_t i = 0;
        for (typename Map::const_iterator it = aMap.begin();
             it != aMap.end(); ++it, ++i)
#ifdef VBOX_WITH_XPCOM
            SafeIfaceArray::Copy(it->second, Base::m.arr[i]);
#else
            SafeIfaceArray::Copy(it->second, Base::m.raw[i]);
#endif
    }

    void setElement(size_t iIdx, I* obj)
    {
#ifdef VBOX_WITH_XPCOM
        SafeIfaceArray::Copy(obj, Base::m.arr[iIdx]);
#else
        SafeIfaceArray::Copy(obj, Base::m.raw[iIdx]);
#endif
    }
};

} /* namespace com */

/** @} */

#endif /* !VBOX_INCLUDED_com_array_h */

