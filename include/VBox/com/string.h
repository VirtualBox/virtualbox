/* $Id: string.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MS COM / XPCOM Abstraction Layer - Smart string classes declaration.
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

#ifndef VBOX_INCLUDED_com_string_h
#define VBOX_INCLUDED_com_string_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Make sure all the stdint.h macros are included - must come first! */
#ifndef __STDC_LIMIT_MACROS
# define __STDC_LIMIT_MACROS
#endif
#ifndef __STDC_CONSTANT_MACROS
# define __STDC_CONSTANT_MACROS
#endif

#if defined(VBOX_WITH_XPCOM)
# include <nsMemory.h>
#endif

#include "VBox/com/defs.h"
#include "VBox/com/assert.h"

#include <iprt/mem.h>
#include <iprt/utf16.h>
#include <iprt/cpp/ministring.h>


/** @defgroup grp_com_str   Smart String Classes
 * @ingroup grp_com
 * @{
 */

namespace com
{

class Utf8Str;

// global constant in glue/string.cpp that represents an empty BSTR
extern const BSTR g_bstrEmpty;

/**
 *  String class used universally in Main for COM-style Utf-16 strings.
 *
 * Unfortunately COM on Windows uses UTF-16 everywhere, requiring conversions
 * back and forth since most of VirtualBox and our libraries use UTF-8.
 *
 * To make things more obscure, on Windows, a COM-style BSTR is not just a
 * pointer to a null-terminated wide character array, but the four bytes (32
 * bits) BEFORE the memory that the pointer points to are a length DWORD. One
 * must therefore avoid pointer arithmetic and always use SysAllocString and
 * the like to deal with BSTR pointers, which manage that DWORD correctly.
 *
 * For platforms other than Windows, we provide our own versions of the Sys*
 * functions in Main/xpcom/helpers.cpp which do NOT use length prefixes though
 * to be compatible with how XPCOM allocates string parameters to public
 * functions.
 *
 * The Bstr class hides all this handling behind a std::string-like interface
 * and also provides automatic conversions to RTCString and Utf8Str instances.
 *
 * The one advantage of using the SysString* routines is that this makes it
 * possible to use it as a type of member variables of COM/XPCOM components and
 * pass their values to callers through component methods' output parameters
 * using the #cloneTo() operation.  Also, the class can adopt (take ownership
 * of) string buffers returned in output parameters of COM methods using the
 * #asOutParam() operation and correctly free them afterwards.
 *
 * Starting with VirtualBox 3.2, like Utf8Str, Bstr no longer differentiates
 * between NULL strings and empty strings. In other words, Bstr("") and
 * Bstr(NULL) behave the same. In both cases, Bstr allocates no memory,
 * reports a zero length and zero allocated bytes for both, and returns an
 * empty C wide string from raw().
 *
 * @note    All Bstr methods ASSUMES valid UTF-16 or UTF-8 input strings.
 *          The VirtualBox policy in this regard is to validate strings coming
 *          from external sources before passing them to Bstr or Utf8Str.
 */
class Bstr
{
public:

    Bstr()
        : m_bstr(NULL)
    { }

    Bstr(const Bstr &that)
    {
        copyFrom((const OLECHAR *)that.m_bstr);
    }

    Bstr(CBSTR that)
    {
        copyFrom((const OLECHAR *)that);
    }

#if defined(VBOX_WITH_XPCOM)
    Bstr(const wchar_t *that)
    {
        AssertCompile(sizeof(wchar_t) == sizeof(OLECHAR));
        copyFrom((const OLECHAR *)that);
    }
#endif

    Bstr(const RTCString &that)
    {
        copyFrom(that.c_str());
    }

    Bstr(const char *that)
    {
        copyFrom(that);
    }

    Bstr(const char *a_pThat, size_t a_cchMax)
    {
        copyFromN(a_pThat, a_cchMax);
    }

    ~Bstr()
    {
        setNull();
    }

    Bstr &operator=(const Bstr &that)
    {
        cleanupAndCopyFrom((const OLECHAR *)that.m_bstr);
        return *this;
    }

    Bstr &operator=(CBSTR that)
    {
        cleanupAndCopyFrom((const OLECHAR *)that);
        return *this;
    }

#if defined(VBOX_WITH_XPCOM)
    Bstr &operator=(const wchar_t *that)
    {
        cleanupAndCopyFrom((const OLECHAR *)that);
        return *this;
    }
#endif

    Bstr &setNull()
    {
        cleanup();
        return *this;
    }

    /**
     * Extended assignment method that returns a COM status code instead of an
     * exception on failure.
     *
     * @returns S_OK or E_OUTOFMEMORY.
     * @param   a_rSrcStr   The source string
     */
    HRESULT assignEx(const Bstr &a_rSrcStr) RT_NOEXCEPT
    {
        return cleanupAndCopyFromEx((const OLECHAR *)a_rSrcStr.m_bstr);
    }

    /**
     * Extended assignment method that returns a COM status code instead of an
     * exception on failure.
     *
     * @returns S_OK or E_OUTOFMEMORY.
     * @param   a_pSrcStr   The source string
     */
    HRESULT assignEx(CBSTR a_pSrcStr) RT_NOEXCEPT
    {
        return cleanupAndCopyFromEx((const OLECHAR *)a_pSrcStr);
    }

    /**
     * Assign the value of a RTCString/Utf8Str string, no exceptions.
     *
     * @returns S_OK or E_OUTOFMEMORY.
     * @param   a_rSrcStr   The source string
     */
    HRESULT assignEx(RTCString const &a_rSrcStr) RT_NOEXCEPT
    {
        return cleanupAndCopyFromNoThrow(a_rSrcStr.c_str(), a_rSrcStr.length());
    }

    /**
     * Assign the value of a RTCString/Utf8Str substring, no exceptions.
     *
     * @returns S_OK, E_OUTOFMEMORY or E_INVALIDARG.
     * @param   a_rSrcStr   The source string
     * @param   a_offSrc    The character (byte) offset of the substring.
     * @param   a_cchSrc    The number of characters (bytes) to copy from the source
     *                      string.
     */
    HRESULT assignEx(RTCString const &a_rSrcStr, size_t a_offSrc, size_t a_cchSrc) RT_NOEXCEPT
    {
        size_t const cchTmp = a_rSrcStr.length();
        if (   a_offSrc + a_cchSrc < cchTmp
            && a_offSrc < cchTmp)
            return cleanupAndCopyFromNoThrow(a_rSrcStr.c_str() + a_offSrc, a_cchSrc);
        return E_INVALIDARG;
    }

    /**
     * Assign the value of a zero terminated UTF-8 string, no exceptions.
     *
     * @returns S_OK or E_OUTOFMEMORY.
     * @param   a_pszSrcStr The source string.
     */
    HRESULT assignEx(const char *a_pszSrcStr) RT_NOEXCEPT
    {
        return cleanupAndCopyFromNoThrow(a_pszSrcStr, RTSTR_MAX);
    }

    /**
     * Assign the value of a UTF-8 substring, no exceptions.
     *
     * @returns S_OK or E_OUTOFMEMORY.
     * @param   a_pszSrcStr The source string.
     * @param   a_cchSrc    The number of characters (bytes) to copy from the source
     *                      string.
     */
    HRESULT assignEx(const char *a_pszSrcStr, size_t a_cchSrc) RT_NOEXCEPT
    {
        return cleanupAndCopyFromNoThrow(a_pszSrcStr, a_cchSrc);
    }

#ifdef _MSC_VER
# if _MSC_VER >= 1400
    RTMEMEF_NEW_AND_DELETE_OPERATORS();
# endif
#else
    RTMEMEF_NEW_AND_DELETE_OPERATORS();
#endif

    /** Case sensitivity selector. */
    enum CaseSensitivity
    {
        CaseSensitive,
        CaseInsensitive
    };

    /**
     * Compares the member string to str.
     * @param str
     * @param cs Whether comparison should be case-sensitive.
     * @return
     */
    int compare(CBSTR str, CaseSensitivity cs = CaseSensitive) const
    {
        if (cs == CaseSensitive)
            return ::RTUtf16Cmp((PRTUTF16)m_bstr, (PRTUTF16)str);
        return ::RTUtf16LocaleICmp((PRTUTF16)m_bstr, (PRTUTF16)str);
    }

    int compare(BSTR str, CaseSensitivity cs = CaseSensitive) const
    {
        return compare((CBSTR)str, cs);
    }

    int compare(const Bstr &that, CaseSensitivity cs = CaseSensitive) const
    {
        return compare(that.m_bstr, cs);
    }

    bool operator==(const Bstr &that) const { return !compare(that.m_bstr); }
    bool operator==(CBSTR that) const       { return !compare(that); }
    bool operator==(BSTR that) const        { return !compare(that); }
    bool operator!=(const Bstr &that) const { return !!compare(that.m_bstr); }
    bool operator!=(CBSTR that) const       { return !!compare(that); }
    bool operator!=(BSTR that) const        { return !!compare(that); }
    bool operator<(const Bstr &that) const  { return compare(that.m_bstr) <  0; }
    bool operator<(CBSTR that) const        { return compare(that)        <  0; }
    bool operator<(BSTR that) const         { return compare(that)        <  0; }
    bool operator<=(const Bstr &that) const { return compare(that.m_bstr) <= 0; }
    bool operator<=(CBSTR that) const       { return compare(that)        <= 0; }
    bool operator<=(BSTR that) const        { return compare(that)        <= 0; }
    bool operator>(const Bstr &that) const  { return compare(that.m_bstr) >  0; }
    bool operator>(CBSTR that) const        { return compare(that)        >  0; }
    bool operator>(BSTR that) const         { return compare(that)        >  0; }
    bool operator>=(const Bstr &that) const { return compare(that.m_bstr) >= 0; }
    bool operator>=(CBSTR that) const       { return compare(that)        >= 0; }
    bool operator>=(BSTR that) const        { return compare(that)        >= 0; }

    /**
     * Compares this string to an UTF-8 C style string.
     *
     * @retval  0 if equal
     * @retval -1 if this string is smaller than the UTF-8 one.
     * @retval  1 if the UTF-8 string is smaller than this.
     *
     * @param   a_pszRight  The string to compare with.
     * @param   a_enmCase   Whether comparison should be case-sensitive.
     */
    int compareUtf8(const char *a_pszRight, CaseSensitivity a_enmCase = CaseSensitive) const;

    /** Java style compare method.
     * @returns true if @a a_pszRight equals this string.
     * @param   a_pszRight The (UTF-8) string to compare with. */
    bool equals(const char *a_pszRight) const           { return compareUtf8(a_pszRight, CaseSensitive) == 0; }

    /** Java style case-insensitive compare method.
     * @returns true if @a a_pszRight equals this string.
     * @param   a_pszRight The (UTF-8) string to compare with. */
    bool equalsIgnoreCase(const char *a_pszRight) const { return compareUtf8(a_pszRight, CaseInsensitive) == 0; }

    /** Java style compare method.
     * @returns true if @a a_rThat equals this string.
     * @param   a_rThat     The other Bstr instance to compare with. */
    bool equals(const Bstr &a_rThat) const              { return compare(a_rThat.m_bstr, CaseSensitive) == 0; }
    /** Java style case-insensitive compare method.
     * @returns true if @a a_rThat equals this string.
     * @param   a_rThat     The other Bstr instance to compare with. */
    bool equalsIgnoreCase(const Bstr &a_rThat) const    { return compare(a_rThat.m_bstr, CaseInsensitive) == 0; }

    /** Java style compare method.
     * @returns true if @a a_pThat equals this string.
     * @param   a_pThat    The native const BSTR to compare with. */
    bool equals(CBSTR a_pThat) const                    { return compare(a_pThat, CaseSensitive) == 0; }
    /** Java style case-insensitive compare method.
     * @returns true if @a a_pThat equals this string.
     * @param   a_pThat    The native const BSTR to compare with. */
    bool equalsIgnoreCase(CBSTR a_pThat) const          { return compare(a_pThat, CaseInsensitive) == 0; }

    /** Java style compare method.
     * @returns true if @a a_pThat equals this string.
     * @param   a_pThat    The native BSTR to compare with. */
    bool equals(BSTR a_pThat) const                     { return compare(a_pThat, CaseSensitive) == 0; }
    /** Java style case-insensitive compare method.
     * @returns true if @a a_pThat equals this string.
     * @param   a_pThat    The native BSTR to compare with. */
    bool equalsIgnoreCase(BSTR a_pThat) const           { return compare(a_pThat, CaseInsensitive) == 0; }

    /**
     * Checks if the string starts with @a a_rStart.
     */
    bool startsWith(Bstr const &a_rStart) const;
    /**
     * Checks if the string starts with @a a_rStart.
     */
    bool startsWith(RTCString const &a_rStart) const;
    /**
     * Checks if the string starts with @a a_pszStart.
     */
    bool startsWith(const char *a_pszStart) const;

    /**
     * Returns true if the member string has no length.
     * This is true for instances created from both NULL and "" input strings.
     *
     * @note Always use this method to check if an instance is empty. Do not
     * use length() because that may need to run through the entire string
     * (Bstr does not cache string lengths).
     */
    bool isEmpty() const { return m_bstr == NULL || *m_bstr == 0; }

    /**
     * Returns true if the member string has a length of one or more.
     *
     * @returns true if not empty, false if empty (NULL or "").
     */
    bool isNotEmpty() const { return m_bstr != NULL && *m_bstr != 0; }

    size_t length() const { return isEmpty() ? 0 : ::RTUtf16Len((PRTUTF16)m_bstr); }

    /**
     * Assigns the output of the string format operation (RTStrPrintf).
     *
     * @param   pszFormat       Pointer to the format string,
     *                          @see pg_rt_str_format.
     * @param   ...             Ellipsis containing the arguments specified by
     *                          the format string.
     *
     * @throws  std::bad_alloc  On allocation error.  Object state is undefined.
     *
     * @returns Reference to the object.
     */
    Bstr &printf(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);

    /**
     * Assigns the output of the string format operation (RTStrPrintf).
     *
     * @param   pszFormat       Pointer to the format string,
     *                          @see pg_rt_str_format.
     * @param   ...             Ellipsis containing the arguments specified by
     *                          the format string.
     *
     * @returns S_OK, E_OUTOFMEMORY or E_INVAL (bad encoding).
     */
    HRESULT printfNoThrow(const char *pszFormat, ...) RT_NOEXCEPT RT_IPRT_FORMAT_ATTR(1, 2);

    /**
     * Assigns the output of the string format operation (RTStrPrintfV).
     *
     * @param   pszFormat       Pointer to the format string,
     *                          @see pg_rt_str_format.
     * @param   va              Argument vector containing the arguments
     *                          specified by the format string.
     *
     * @throws  std::bad_alloc  On allocation error.  Object state is undefined.
     *
     * @returns Reference to the object.
     */
    Bstr &printfV(const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(1, 0);

    /**
     * Assigns the output of the string format operation (RTStrPrintfV).
     *
     * @param   pszFormat       Pointer to the format string,
     *                          @see pg_rt_str_format.
     * @param   va              Argument vector containing the arguments
     *                          specified by the format string.
     *
     * @returns S_OK, E_OUTOFMEMORY or E_INVAL (bad encoding).
     */
    HRESULT printfVNoThrow(const char *pszFormat, va_list va) RT_NOEXCEPT RT_IPRT_FORMAT_ATTR(1, 0);

    /** @name Append methods and operators
     *  @{ */

    /**
     * Appends the string @a that to @a rThat.
     *
     * @param   rThat            The string to append.
     * @throws  std::bad_alloc  On allocation error.  The object is left unchanged.
     * @returns Reference to the object.
     */
    Bstr &append(const Bstr &rThat);

    /**
     * Appends the string @a that to @a rThat.
     *
     * @param   rThat            The string to append.
     * @returns S_OK, E_OUTOFMEMORY or E_INVAL (bad encoding).
     */
    HRESULT appendNoThrow(const Bstr &rThat) RT_NOEXCEPT;

    /**
     * Appends the UTF-8 string @a that to @a rThat.
     *
     * @param   rThat            The string to append.
     * @throws  std::bad_alloc  On allocation error.  The object is left unchanged.
     * @returns Reference to the object.
     */
    Bstr &append(const RTCString &rThat);

    /**
     * Appends the UTF-8 string @a that to @a rThat.
     *
     * @param   rThat            The string to append.
     * @returns S_OK, E_OUTOFMEMORY or E_INVAL (bad encoding).
     */
    HRESULT appendNoThrow(const RTCString &rThat) RT_NOEXCEPT;

    /**
     * Appends the UTF-16 string @a pszSrc to @a this.
     *
     * @param   pwszSrc         The C-style UTF-16 string to append.
     * @throws  std::bad_alloc  On allocation error.  The object is left unchanged.
     * @returns Reference to the object.
     */
    Bstr &append(CBSTR pwszSrc);

    /**
     * Appends the UTF-16 string @a pszSrc to @a this.
     *
     * @param   pwszSrc         The C-style UTF-16 string to append.
     * @returns S_OK, E_OUTOFMEMORY or E_INVAL (bad encoding).
     */
    HRESULT appendNoThrow(CBSTR pwszSrc) RT_NOEXCEPT;

    /**
     * Appends the UTF-8 string @a pszSrc to @a this.
     *
     * @param   pszSrc          The C-style string to append.
     * @throws  std::bad_alloc  On allocation error.  The object is left unchanged.
     * @returns Reference to the object.
     */
    Bstr &append(const char *pszSrc);

    /**
     * Appends the UTF-8 string @a pszSrc to @a this.
     *
     * @param   pszSrc          The C-style string to append.
     * @returns S_OK, E_OUTOFMEMORY or E_INVAL (bad encoding).
     */
    HRESULT appendNoThrow(const char *pszSrc) RT_NOEXCEPT;

    /**
     * Appends the a substring from @a rThat to @a this.
     *
     * @param   rThat           The string to append a substring from.
     * @param   offStart        The start of the substring to append (UTF-16
     *                          offset, not codepoint).
     * @param   cwcMax          The maximum number of UTF-16 units to append.
     * @throws  std::bad_alloc  On allocation error.  The object is left unchanged.
     * @returns Reference to the object.
     */
    Bstr &append(const Bstr &rThat, size_t offStart, size_t cwcMax = RTSTR_MAX);

    /**
     * Appends the a substring from @a rThat to @a this.
     *
     * @param   rThat           The string to append a substring from.
     * @param   offStart        The start of the substring to append (UTF-16
     *                          offset, not codepoint).
     * @param   cwcMax          The maximum number of UTF-16 units to append.
     * @returns S_OK, E_OUTOFMEMORY or E_INVAL (bad encoding).
     */
    HRESULT appendNoThrow(const Bstr &rThat, size_t offStart, size_t cwcMax = RTSTR_MAX) RT_NOEXCEPT;

    /**
     * Appends the a substring from UTF-8 @a rThat to @a this.
     *
     * @param   rThat           The string to append a substring from.
     * @param   offStart        The start of the substring to append (byte offset,
     *                          not codepoint).
     * @param   cchMax          The maximum number of bytes to append.
     * @throws  std::bad_alloc  On allocation error.  The object is left unchanged.
     * @returns Reference to the object.
     */
    Bstr &append(const RTCString &rThat, size_t offStart, size_t cchMax = RTSTR_MAX);

    /**
     * Appends the a substring from UTF-8 @a rThat to @a this.
     *
     * @param   rThat           The string to append a substring from.
     * @param   offStart        The start of the substring to append (byte offset,
     *                          not codepoint).
     * @param   cchMax          The maximum number of bytes to append.
     * @returns S_OK, E_OUTOFMEMORY or E_INVAL (bad encoding).
     */
    HRESULT appendNoThrow(const RTCString &rThat, size_t offStart, size_t cchMax = RTSTR_MAX) RT_NOEXCEPT;

    /**
     * Appends the first @a cchMax chars from UTF-16 string @a pszThat to @a this.
     *
     * @param   pwszThat        The C-style UTF-16 string to append.
     * @param   cchMax          The maximum number of bytes to append.
     * @throws  std::bad_alloc  On allocation error.  The object is left unchanged.
     * @returns Reference to the object.
     */
    Bstr &append(CBSTR pwszThat, size_t cchMax);

    /**
     * Appends the first @a cchMax chars from UTF-16 string @a pszThat to @a this.
     *
     * @param   pwszThat        The C-style UTF-16 string to append.
     * @param   cchMax          The maximum number of bytes to append.
     * @returns S_OK, E_OUTOFMEMORY or E_INVAL (bad encoding).
     */
    HRESULT appendNoThrow(CBSTR pwszThat, size_t cchMax) RT_NOEXCEPT;

    /**
     * Appends the first @a cchMax chars from string @a pszThat to @a this.
     *
     * @param   pszThat         The C-style string to append.
     * @param   cchMax          The maximum number of bytes to append.
     * @throws  std::bad_alloc  On allocation error.  The object is left unchanged.
     * @returns Reference to the object.
     */
    Bstr &append(const char *pszThat, size_t cchMax);

    /**
     * Appends the first @a cchMax chars from string @a pszThat to @a this.
     *
     * @param   pszThat         The C-style string to append.
     * @param   cchMax          The maximum number of bytes to append.
     * @returns S_OK, E_OUTOFMEMORY or E_INVAL (bad encoding).
     */
    HRESULT appendNoThrow(const char *pszThat, size_t cchMax) RT_NOEXCEPT;

    /**
     * Appends the given character to @a this.
     *
     * @param   ch              The character to append.
     * @throws  std::bad_alloc  On allocation error.  The object is left unchanged.
     * @returns Reference to the object.
     */
    Bstr &append(char ch);

    /**
     * Appends the given character to @a this.
     *
     * @param   ch              The character to append.
     * @returns S_OK, E_OUTOFMEMORY or E_INVAL (bad encoding).
     */
    HRESULT appendNoThrow(char ch) RT_NOEXCEPT;

    /**
     * Appends the given unicode code point to @a this.
     *
     * @param   uc              The unicode code point to append.
     * @throws  std::bad_alloc  On allocation error.  The object is left unchanged.
     * @returns Reference to the object.
     */
    Bstr &appendCodePoint(RTUNICP uc);

    /**
     * Appends the given unicode code point to @a this.
     *
     * @param   uc              The unicode code point to append.
     * @returns S_OK, E_OUTOFMEMORY or E_INVAL (bad encoding).
     */
    HRESULT appendCodePointNoThrow(RTUNICP uc) RT_NOEXCEPT;

    /**
     * Appends the output of the string format operation (RTStrPrintf).
     *
     * @param   pszFormat       Pointer to the format string,
     *                          @see pg_rt_str_format.
     * @param   ...             Ellipsis containing the arguments specified by
     *                          the format string.
     *
     * @throws  std::bad_alloc  On allocation error.  Object state is undefined.
     *
     * @returns Reference to the object.
     */
    Bstr &appendPrintf(const char *pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2);

    /**
     * Appends the output of the string format operation (RTStrPrintf).
     *
     * @param   pszFormat       Pointer to the format string,
     *                          @see pg_rt_str_format.
     * @param   ...             Ellipsis containing the arguments specified by
     *                          the format string.
     *
     * @returns S_OK, E_OUTOFMEMORY or E_INVAL (bad encoding).
     */
    HRESULT appendPrintfNoThrow(const char *pszFormat, ...) RT_NOEXCEPT RT_IPRT_FORMAT_ATTR(1, 2);

    /**
     * Appends the output of the string format operation (RTStrPrintfV).
     *
     * @param   pszFormat       Pointer to the format string,
     *                          @see pg_rt_str_format.
     * @param   va              Argument vector containing the arguments
     *                          specified by the format string.
     *
     * @throws  std::bad_alloc  On allocation error.  Object state is undefined.
     *
     * @returns Reference to the object.
     */
    Bstr &appendPrintfV(const char *pszFormat, va_list va) RT_IPRT_FORMAT_ATTR(1, 0);

    /**
     * Appends the output of the string format operation (RTStrPrintfV).
     *
     * @param   pszFormat       Pointer to the format string,
     *                          @see pg_rt_str_format.
     * @param   va              Argument vector containing the arguments
     *                          specified by the format string.
     *
     * @returns S_OK, E_OUTOFMEMORY or E_INVAL (bad encoding).
     */
    HRESULT appendPrintfVNoThrow(const char *pszFormat, va_list va) RT_NOEXCEPT RT_IPRT_FORMAT_ATTR(1, 0);

    /**
     * Shortcut to append(), Bstr variant.
     *
     * @param   rThat           The string to append.
     * @returns Reference to the object.
     */
    Bstr &operator+=(const Bstr &rThat)
    {
        return append(rThat);
    }

    /**
     * Shortcut to append(), RTCString variant.
     *
     * @param   rThat           The string to append.
     * @returns Reference to the object.
     */
    Bstr &operator+=(const RTCString &rThat)
    {
        return append(rThat);
    }

    /**
     * Shortcut to append(), CBSTR variant.
     *
     * @param   pwszThat        The C-style string to append.
     * @returns                 Reference to the object.
     */
    Bstr &operator+=(CBSTR pwszThat)
    {
        return append(pwszThat);
    }

    /**
     * Shortcut to append(), const char * variant.
     *
     * @param   pszThat         The C-style string to append.
     * @returns                 Reference to the object.
     */
    Bstr &operator+=(const char *pszThat)
    {
        return append(pszThat);
    }

    /**
     * Shortcut to append(), char variant.
     *
     * @param ch                The character to append.
     *
     * @returns                 Reference to the object.
     */
    Bstr &operator+=(char ch)
    {
        return append(ch);
    }

    /** @} */

    /**
     * Erases a sequence from the string.
     *
     * @returns Reference to the object.
     * @param   offStart        Where in @a this string to start erasing (UTF-16
     *                          units, not codepoints).
     * @param   cwcLength       How much following @a offStart to erase (UTF-16
     *                          units, not codepoints).
     */
    Bstr &erase(size_t offStart = 0, size_t cwcLength = RTSTR_MAX) RT_NOEXCEPT;


    /** @name BASE64 related methods
     * @{ */
    /**
     * Encodes the given data as BASE64.
     *
     * @returns S_OK or E_OUTOFMEMORY.
     * @param   pvData          Pointer to the data to encode.
     * @param   cbData          Number of bytes to encode.
     * @param   fLineBreaks     Whether to add line breaks (true) or just encode it
     *                          as a continuous string.
     * @sa RTBase64EncodeUtf16
     */
    HRESULT base64Encode(const void *pvData, size_t cbData, bool fLineBreaks = false);

    /**
     * Decodes the string as BASE64.
     *
     * @returns IPRT status code, see RTBase64DecodeUtf16Ex.
     * @param   pvData          Where to return the decoded bytes.
     * @param   cbData          Size of the @a pvData return buffer.
     * @param   pcbActual       Where to return number of bytes actually decoded.
     *                          This is optional and if not specified, the request
     *                          will fail unless @a cbData matches the data size
     *                          exactly.
     * @param   ppwszEnd        Where to return pointer to the first non-base64
     *                          character following the encoded data.  This is
     *                          optional and if NULL, the request will fail if there
     *                          are anything trailing the encoded bytes in the
     *                          string.
     * @sa base64DecodedSize, RTBase64DecodeUtf16
     */
    int base64Decode(void *pvData, size_t cbData, size_t *pcbActual = NULL, PRTUTF16 *ppwszEnd = NULL);

    /**
     * Determins the size of the BASE64 encoded data in the string.
     *
     * @returns The length in bytes. -1 if the encoding is bad.
     *
     * @param   ppwszEnd        If not NULL, this will point to the first char
     *                          following the Base64 encoded text block. If
     *                          NULL the entire string is assumed to be Base64.
     * @sa      base64Decode, RTBase64DecodedUtf16Size
     */
    ssize_t base64DecodedSize(PRTUTF16 *ppwszEnd = NULL);
    /** @} */

#if defined(VBOX_WITH_XPCOM)
    /**
     *  Returns a pointer to the raw member UTF-16 string. If the member string is empty,
     *  returns a pointer to a global variable containing an empty BSTR with a proper zero
     *  length prefix so that Windows is happy.
     */
    CBSTR raw() const
    {
        if (m_bstr)
            return m_bstr;

        return g_bstrEmpty;
    }
#else
    /**
     *  Windows-only hack, as the automatically generated headers use BSTR.
     *  So if we don't want to cast like crazy we have to be more loose than
     *  on XPCOM.
     *
     *  Returns a pointer to the raw member UTF-16 string. If the member string is empty,
     *  returns a pointer to a global variable containing an empty BSTR with a proper zero
     *  length prefix so that Windows is happy.
     */
    BSTR raw() const
    {
        if (m_bstr)
            return m_bstr;

        return g_bstrEmpty;
    }
#endif

    /**
     * Returns a non-const raw pointer that allows modifying the string directly.
     *
     * @note As opposed to raw(), this DOES return NULL if the member string is
     *       empty because we cannot return a mutable pointer to the global variable
     *       with the empty string.
     *
     * @note If modifying the string size (only shrinking it is allows), #jolt() or
     *       #joltNoThrow() must be called!
     *
     * @note Do not modify memory beyond the #length() of the string!
     *
     * @sa   joltNoThrow(), mutalbleRaw(), reserve(), reserveNoThrow()
     */
    BSTR mutableRaw() { return m_bstr; }

    /**
     * Correct the embedded length after using mutableRaw().
     *
     * This is needed on COM (Windows) to update the embedded string length.  It is
     * a stub on hosts using XPCOM.
     *
     * @param   cwcNew      The new string length, if handy, otherwise a negative
     *                      number.
     * @sa      joltNoThrow(), mutalbleRaw(), reserve(), reserveNoThrow()
     */
#ifndef VBOX_WITH_XPCOM
    void jolt(ssize_t cwcNew = -1);
#else
    void jolt(ssize_t cwcNew = -1)
    {
        Assert(cwcNew < 0 || (cwcNew == 0 && !m_bstr) || m_bstr[cwcNew] == '\0'); RT_NOREF(cwcNew);
    }
#endif

    /**
     * Correct the embedded length after using mutableRaw().
     *
     * This is needed on COM (Windows) to update the embedded string length.  It is
     * a stub on hosts using XPCOM.
     *
     * @returns S_OK on success, E_OUTOFMEMORY if shrinking the string failed.
     * @param   cwcNew      The new string length, if handy, otherwise a negative
     *                      number.
     * @sa      jolt(), mutalbleRaw(), reserve(), reserveNoThrow()
     */
#ifndef VBOX_WITH_XPCOM
    HRESULT joltNoThrow(ssize_t cwcNew = -1) RT_NOEXCEPT;
#else
    HRESULT joltNoThrow(ssize_t cwcNew = -1) RT_NOEXCEPT
    {
        Assert(cwcNew < 0 || (cwcNew == 0 && !m_bstr) || m_bstr[cwcNew] == '\0'); RT_NOREF(cwcNew);
        return S_OK;
    }
#endif

    /**
     * Make sure at that least @a cwc of buffer space is reserved.
     *
     * Requests that the contained memory buffer have at least cb bytes allocated.
     * This may expand or shrink the string's storage, but will never truncate the
     * contained string.  In other words, cb will be ignored if it's smaller than
     * length() + 1.
     *
     * @param   cwcMin  The new minimum string length that the can be stored. This
     *                  does not include the terminator.
     * @param   fForce  Force this size.
     *
     * @throws  std::bad_alloc  On allocation error.  The object is left unchanged.
     */
    void reserve(size_t cwcMin, bool fForce = false);

    /**
     * A C like version of the #reserve() method, i.e. return code instead of throw.
     *
     * @returns S_OK or E_OUTOFMEMORY.
     * @param   cwcMin  The new minimum string length that the can be stored.  This
     *                  does not include the terminator.
    * @param   fForce  Force this size.
     */
    HRESULT reserveNoThrow(size_t cwcMin, bool fForce = false) RT_NOEXCEPT;

    /**
     *  Intended to assign copies of instances to |BSTR| out parameters from
     *  within the interface method. Transfers the ownership of the duplicated
     *  string to the caller.
     *
     *  If the member string is empty, this allocates an empty BSTR in *pstr
     *  (i.e. makes it point to a new buffer with a null byte).
     *
     *  @deprecated Use cloneToEx instead to avoid throwing exceptions.
     */
    void cloneTo(BSTR *pstr) const
    {
        if (pstr)
        {
            *pstr = ::SysAllocString((const OLECHAR *)raw());       // raw() returns a pointer to "" if empty
#ifdef RT_EXCEPTIONS_ENABLED
            if (!*pstr)
                throw std::bad_alloc();
#endif
        }
    }

    /**
     *  A version of cloneTo that does not throw any out of memory exceptions, but
     *  returns E_OUTOFMEMORY intead.
     *  @returns S_OK or E_OUTOFMEMORY.
     */
    HRESULT cloneToEx(BSTR *pstr) const
    {
        if (!pstr)
            return S_OK;
        *pstr = ::SysAllocString((const OLECHAR *)raw());       // raw() returns a pointer to "" if empty
        return pstr ? S_OK : E_OUTOFMEMORY;
    }

    /**
     *  Intended to assign instances to |BSTR| out parameters from within the
     *  interface method. Transfers the ownership of the original string to the
     *  caller and resets the instance to null.
     *
     *  As opposed to cloneTo(), this method doesn't create a copy of the
     *  string.
     *
     *  If the member string is empty, this allocates an empty BSTR in *pstr
     *  (i.e. makes it point to a new buffer with a null byte).
     *
     * @param   pbstrDst        The BSTR variable to detach the string to.
     *
     * @throws  std::bad_alloc if we failed to allocate a new empty string.
     */
    void detachTo(BSTR *pbstrDst)
    {
        if (m_bstr)
        {
            *pbstrDst = m_bstr;
            m_bstr = NULL;
        }
        else
        {
            // allocate null BSTR
            *pbstrDst = ::SysAllocString((const OLECHAR *)g_bstrEmpty);
#ifdef RT_EXCEPTIONS_ENABLED
            if (!*pbstrDst)
                throw std::bad_alloc();
#endif
        }
    }

    /**
     *  A version of detachTo that does not throw exceptions on out-of-memory
     *  conditions, but instead returns E_OUTOFMEMORY.
     *
     * @param   pbstrDst        The BSTR variable to detach the string to.
     * @returns S_OK or E_OUTOFMEMORY.
     */
    HRESULT detachToEx(BSTR *pbstrDst)
    {
        if (m_bstr)
        {
            *pbstrDst = m_bstr;
            m_bstr = NULL;
        }
        else
        {
            // allocate null BSTR
            *pbstrDst = ::SysAllocString((const OLECHAR *)g_bstrEmpty);
            if (!*pbstrDst)
                return E_OUTOFMEMORY;
        }
        return S_OK;
    }

    /**
     *  Intended to pass instances as |BSTR| out parameters to methods.
     *  Takes the ownership of the returned data.
     */
    BSTR *asOutParam()
    {
        cleanup();
        return &m_bstr;
    }

    /**
     *  Static immutable empty-string object. May be used for comparison purposes.
     */
    static const Bstr Empty;

protected:

    void cleanup();

    /**
     * Protected internal helper to copy a string. This ignores the previous object
     * state, so either call this from a constructor or call cleanup() first.
     *
     * This variant copies from a zero-terminated UTF-16 string (which need not
     * be a BSTR, i.e. need not have a length prefix).
     *
     * If the source is empty, this sets the member string to NULL.
     *
     * @param   a_bstrSrc           The source string.  The caller guarantees
     *                              that this is valid UTF-16.
     *
     * @throws  std::bad_alloc - the object is representing an empty string.
     */
    void copyFrom(const OLECHAR *a_bstrSrc);

    /** cleanup() + copyFrom() - for assignment operators.  */
    void cleanupAndCopyFrom(const OLECHAR *a_bstrSrc);

    /**
     * Protected internal helper to copy a string, implying cleanup().
     *
     * This variant copies from a zero-terminated UTF-16 string (which need not be a
     * BSTR, i.e. need not have a length prefix).
     *
     * If the source is empty, this sets the member string to NULL.
     *
     * @param   a_bstrSrc           The source string.  The caller guarantees
     *                              that this is valid UTF-16.
     * @returns S_OK or E_OUTOFMEMORY
     */
    HRESULT cleanupAndCopyFromEx(const OLECHAR *a_bstrSrc) RT_NOEXCEPT;

    /**
     * Protected internal helper to copy a string. This ignores the previous object
     * state, so either call this from a constructor or call cleanup() first.
     *
     * This variant copies and converts from a zero-terminated UTF-8 string.
     *
     * If the source is empty, this sets the member string to NULL.
     *
     * @param   a_pszSrc            The source string.  The caller guarantees
     *                              that this is valid UTF-8.
     *
     * @throws  std::bad_alloc - the object is representing an empty string.
     */
    void copyFrom(const char *a_pszSrc)
    {
        copyFromN(a_pszSrc, RTSTR_MAX);
    }

    /**
     * Variant of copyFrom for sub-string constructors.
     *
     * @param   a_pszSrc            The source string.  The caller guarantees
     *                              that this is valid UTF-8.
     * @param   a_cchSrc            The maximum number of chars (not codepoints) to
     *                              copy.  If you pass RTSTR_MAX it'll be exactly
     *                              like copyFrom().
     *
     * @throws  std::bad_alloc - the object is representing an empty string.
     */
    void copyFromN(const char *a_pszSrc, size_t a_cchSrc);

    /** cleanup() + non-throwing copyFromN(). */
    HRESULT cleanupAndCopyFromNoThrow(const char *a_pszSrc, size_t a_cchMax) RT_NOEXCEPT;

    Bstr   &appendWorkerUtf16(PCRTUTF16 pwszSrc, size_t cwcSrc);
    Bstr   &appendWorkerUtf8(const char *pszSrc, size_t cchSrc);
    HRESULT appendWorkerUtf16NoThrow(PCRTUTF16 pwszSrc, size_t cwcSrc) RT_NOEXCEPT;
    HRESULT appendWorkerUtf8NoThrow(const char *pszSrc, size_t cchSrc) RT_NOEXCEPT;

    static DECLCALLBACK(size_t) printfOutputCallbackNoThrow(void *pvArg, const char *pachChars, size_t cbChars) RT_NOEXCEPT;

    BSTR m_bstr;

    friend class Utf8Str; /* to access our raw_copy() */
};

/* symmetric compare operators */
inline bool operator==(CBSTR l, const Bstr &r) { return r.operator==(l); }
inline bool operator!=(CBSTR l, const Bstr &r) { return r.operator!=(l); }
inline bool operator==(BSTR l, const Bstr &r) { return r.operator==(l); }
inline bool operator!=(BSTR l, const Bstr &r) { return r.operator!=(l); }




/**
 * String class used universally in Main for UTF-8 strings.
 *
 * This is based on RTCString, to which some functionality has been
 * moved.  Here we keep things that are specific to Main, such as conversions
 * with UTF-16 strings (Bstr).
 *
 * Like RTCString, Utf8Str does not differentiate between NULL strings
 * and empty strings.  In other words, Utf8Str("") and Utf8Str(NULL) behave the
 * same.  In both cases, RTCString allocates no memory, reports
 * a zero length and zero allocated bytes for both, and returns an empty
 * C string from c_str().
 *
 * @note    All Utf8Str methods ASSUMES valid UTF-8 or UTF-16 input strings.
 *          The VirtualBox policy in this regard is to validate strings coming
 *          from external sources before passing them to Utf8Str or Bstr.
 */
class Utf8Str : public RTCString
{
public:

    Utf8Str() {}

    Utf8Str(const RTCString &that)
        : RTCString(that)
    {}

    Utf8Str(const char *that)
        : RTCString(that)
    {}

    Utf8Str(const Bstr &that)
    {
        copyFrom(that.raw());
    }

    Utf8Str(CBSTR that, size_t a_cwcSize = RTSTR_MAX)
    {
        copyFrom(that, a_cwcSize);
    }

    Utf8Str(const char *a_pszSrc, size_t a_cchSrc)
        : RTCString(a_pszSrc, a_cchSrc)
    {
    }

    /**
     * Constructs a new string given the format string and the list of the
     * arguments for the format string.
     *
     * @param   a_pszFormat     Pointer to the format string (UTF-8),
     *                          @see pg_rt_str_format.
     * @param   a_va            Argument vector containing the arguments
     *                          specified by the format string.
     * @sa      RTCString::printfV
     */
    Utf8Str(const char *a_pszFormat, va_list a_va) RT_IPRT_FORMAT_ATTR(1, 0)
        : RTCString(a_pszFormat, a_va)
    {
    }

    Utf8Str& operator=(const RTCString &that)
    {
        RTCString::operator=(that);
        return *this;
    }

    Utf8Str& operator=(const char *that)
    {
        RTCString::operator=(that);
        return *this;
    }

    Utf8Str& operator=(const Bstr &that)
    {
        cleanup();
        copyFrom(that.raw());
        return *this;
    }

    Utf8Str& operator=(CBSTR that)
    {
        cleanup();
        copyFrom(that);
        return *this;
    }

    /**
     * Extended assignment method that returns a COM status code instead of an
     * exception on failure.
     *
     * @returns S_OK or E_OUTOFMEMORY.
     * @param   a_rSrcStr   The source string
     */
    HRESULT assignEx(Utf8Str const &a_rSrcStr)
    {
        return copyFromExNComRC(a_rSrcStr.m_psz, 0, a_rSrcStr.m_cch);
    }

    /**
     * Extended assignment method that returns a COM status code instead of an
     * exception on failure.
     *
     * @returns S_OK, E_OUTOFMEMORY or E_INVALIDARG.
     * @param   a_rSrcStr   The source string
     * @param   a_offSrc    The character (byte) offset of the substring.
     * @param   a_cchSrc    The number of characters (bytes) to copy from the source
     *                      string.
     */
    HRESULT assignEx(Utf8Str const &a_rSrcStr, size_t a_offSrc, size_t a_cchSrc)
    {
        if (   a_offSrc + a_cchSrc > a_rSrcStr.m_cch
            || a_offSrc > a_rSrcStr.m_cch)
            return E_INVALIDARG;
        return copyFromExNComRC(a_rSrcStr.m_psz, a_offSrc, a_cchSrc);
    }

    /**
     * Extended assignment method that returns a COM status code instead of an
     * exception on failure.
     *
     * @returns S_OK or E_OUTOFMEMORY.
     * @param   a_pcszSrc   The source string
     */
    HRESULT assignEx(const char *a_pcszSrc)
    {
        return copyFromExNComRC(a_pcszSrc, 0, a_pcszSrc ? strlen(a_pcszSrc) : 0);
    }

    /**
     * Extended assignment method that returns a COM status code instead of an
     * exception on failure.
     *
     * @returns S_OK or E_OUTOFMEMORY.
     * @param   a_pcszSrc   The source string
     * @param   a_cchSrc    The number of characters (bytes) to copy from the source
     *                      string.
     */
    HRESULT assignEx(const char *a_pcszSrc, size_t a_cchSrc)
    {
        return copyFromExNComRC(a_pcszSrc, 0, a_cchSrc);
    }

    RTMEMEF_NEW_AND_DELETE_OPERATORS();

#if defined(VBOX_WITH_XPCOM)
    /**
     * Intended to assign instances to |char *| out parameters from within the
     * interface method. Transfers the ownership of the duplicated string to the
     * caller.
     *
     * This allocates a single 0 byte in the target if the member string is empty.
     *
     * This uses XPCOM memory allocation and thus only works on XPCOM. MSCOM doesn't
     * like char* strings anyway.
     */
    void cloneTo(char **pstr) const;

    /**
     * A version of cloneTo that does not throw allocation errors but returns
     * E_OUTOFMEMORY instead.
     * @returns S_OK or E_OUTOFMEMORY (COM status codes).
     */
    HRESULT cloneToEx(char **pstr) const;
#endif

    /**
     *  Intended to assign instances to |BSTR| out parameters from within the
     *  interface method. Transfers the ownership of the duplicated string to the
     *  caller.
     */
    void cloneTo(BSTR *pstr) const
    {
        if (pstr)
        {
            Bstr bstr(*this);
            bstr.cloneTo(pstr);
        }
    }

    /**
     * A version of cloneTo that does not throw allocation errors but returns
     * E_OUTOFMEMORY instead.
     *
     * @param   pbstr Where to store a clone of the string.
     * @returns S_OK or E_OUTOFMEMORY (COM status codes).
     */
    HRESULT cloneToEx(BSTR *pbstr) const RT_NOEXCEPT;

    /**
     * Safe assignment from BSTR.
     *
     * @param   pbstrSrc    The source string.
     * @returns S_OK or E_OUTOFMEMORY (COM status codes).
     */
    HRESULT cloneEx(CBSTR pbstrSrc)
    {
        cleanup();
        return copyFromEx(pbstrSrc);
    }

    /**
     * Removes a trailing slash from the member string, if present.
     * Calls RTPathStripTrailingSlash() without having to mess with mutableRaw().
     */
    Utf8Str& stripTrailingSlash();

    /**
     * Removes a trailing filename from the member string, if present.
     * Calls RTPathStripFilename() without having to mess with mutableRaw().
     */
    Utf8Str& stripFilename();

    /**
     * Removes the path component from the member string, if present.
     * Calls RTPathFilename() without having to mess with mutableRaw().
     */
    Utf8Str& stripPath();

    /**
     * Removes a trailing file name suffix from the member string, if present.
     * Calls RTPathStripSuffix() without having to mess with mutableRaw().
     */
    Utf8Str& stripSuffix();

    /**
     * Parses key=value pairs.
     *
     * @returns offset of the @a a_rPairSeparator following the returned value.
     * @retval  npos is returned if there are no more key/value pairs.
     *
     * @param   a_rKey                  Reference to variable that should receive
     *                                  the key substring.  This is set to null if
     *                                  no key/value found.  (It's also possible the
     *                                  key is an empty string, so be careful.)
     * @param   a_rValue                Reference to variable that should receive
     *                                  the value substring.  This is set to null if
     *                                  no key/value found.  (It's also possible the
     *                                  value is an empty string, so be careful.)
     * @param   a_offStart              The offset to start searching from.  This is
     *                                  typically 0 for the first call, and the
     *                                  return value of the previous call for the
     *                                  subsequent ones.
     * @param   a_rPairSeparator        The pair separator string.  If this is an
     *                                  empty string, the whole string will be
     *                                  considered as a single key/value pair.
     * @param   a_rKeyValueSeparator    The key/value separator string.
     */
    size_t parseKeyValue(Utf8Str &a_rKey, Utf8Str &a_rValue, size_t a_offStart = 0,
                         const Utf8Str &a_rPairSeparator = ",", const Utf8Str &a_rKeyValueSeparator = "=") const;

    /**
     *  Static immutable empty-string object. May be used for comparison purposes.
     */
    static const Utf8Str Empty;
protected:

    void copyFrom(CBSTR a_pbstr, size_t a_cwcMax = RTSTR_MAX);
    HRESULT copyFromEx(CBSTR a_pbstr);
    HRESULT copyFromExNComRC(const char *a_pcszSrc, size_t a_offSrc, size_t a_cchSrc);

    friend class Bstr; /* to access our raw_copy() */
};

/**
 * Class with RTCString::printf as constructor for your convenience.
 *
 * Constructing a Utf8Str string object from a format string and a variable
 * number of arguments can easily be confused with the other Utf8Str
 * constructures, thus this child class.
 *
 * The usage of this class is like the following:
 * @code
    Utf8StrFmt strName("program name = %s", argv[0]);
   @endcode
 *
 * @note    Do not use in assignments to Utf8Str variables.  Instead use
 *          RTCString::printf directly on the variable!  This avoid an extra
 *          temporary Utf8Str instance and assignment operation.
 */
class Utf8StrFmt : public Utf8Str
{
public:

    /**
     * Constructs a new string given the format string and the list of the
     * arguments for the format string.
     *
     * @param   a_pszFormat     Pointer to the format string (UTF-8),
     *                          @see pg_rt_str_format.
     * @param   ...             Ellipsis containing the arguments specified by
     *                          the format string.
     */
    explicit Utf8StrFmt(const char *a_pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2)
    {
        va_list va;
        va_start(va, a_pszFormat);
        printfV(a_pszFormat, va);
        va_end(va);
    }

    RTMEMEF_NEW_AND_DELETE_OPERATORS();

protected:
    Utf8StrFmt()
    { }

private:
};

/**
 * Class with Bstr::printf as constructor for your convenience.
 */
class BstrFmt : public Bstr
{
public:

    /**
     * Constructs a new string given the format string and the list of the
     * arguments for the format string.
     *
     * @param a_pszFormat   printf-like format string (in UTF-8 encoding), see
     *                      iprt/string.h for details.
     * @param ...           List of the arguments for the format string.
     */
    explicit BstrFmt(const char *a_pszFormat, ...) RT_IPRT_FORMAT_ATTR(1, 2)
    {
        va_list va;
        va_start(va, a_pszFormat);
        printfV(a_pszFormat, va);
        va_end(va);
    }

    RTMEMEF_NEW_AND_DELETE_OPERATORS();

protected:
    BstrFmt()
    { }
};

/**
 * Class with Bstr::printfV as constructor for your convenience.
 */
class BstrFmtVA : public Bstr
{
public:

    /**
     * Constructs a new string given the format string and the list of the
     * arguments for the format string.
     *
     * @param a_pszFormat   printf-like format string (in UTF-8 encoding), see
     *                      iprt/string.h for details.
     * @param a_va          List of arguments for the format string
     */
    BstrFmtVA(const char *a_pszFormat, va_list a_va) RT_IPRT_FORMAT_ATTR(1, 0)
    {
        printfV(a_pszFormat, a_va);
    }

    RTMEMEF_NEW_AND_DELETE_OPERATORS();

protected:
    BstrFmtVA()
    { }
};

} /* namespace com */

/** @} */

#endif /* !VBOX_INCLUDED_com_string_h */

