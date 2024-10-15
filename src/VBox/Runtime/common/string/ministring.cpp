/* $Id: ministring.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Mini C++ string class.
 *
 * This is a base for both Utf8Str and other places where IPRT may want to use
 * a lean C++ string class.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/cpp/ministring.h>
#include "internal/iprt.h"

#include <iprt/ctype.h>
#include <iprt/uni.h>
#include <iprt/err.h>



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
const size_t RTCString::npos = ~(size_t)0;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Allocation block alignment used when appending bytes to a string. */
#define IPRT_MINISTRING_APPEND_ALIGNMENT    64


RTCString &RTCString::assign(const RTCString &a_rSrc)
{
    Assert(&a_rSrc != this);
    size_t const cchSrc = a_rSrc.length();
    if (cchSrc > 0)
    {
        reserve(cchSrc + 1);
        memcpy(m_psz, a_rSrc.c_str(), cchSrc);
        m_psz[cchSrc] = '\0';
        m_cch = cchSrc;
        return *this;
    }
    setNull();
    return *this;

}

int RTCString::assignNoThrow(const RTCString &a_rSrc) RT_NOEXCEPT
{
    AssertReturn(&a_rSrc != this, VINF_SUCCESS);
    size_t const cchSrc = a_rSrc.length();
    if (cchSrc > 0)
    {
        int rc = reserveNoThrow(cchSrc + 1);
        if (RT_SUCCESS(rc))
        {
            memcpy(m_psz, a_rSrc.c_str(), cchSrc);
            m_psz[cchSrc] = '\0';
            m_cch = cchSrc;
            return VINF_SUCCESS;
        }
        return rc;
    }
    setNull();
    return VINF_SUCCESS;

}

RTCString &RTCString::assign(const char *a_pszSrc)
{
    if (a_pszSrc)
    {
        size_t cchSrc = strlen(a_pszSrc);
        if (cchSrc)
        {
            Assert((uintptr_t)&a_pszSrc - (uintptr_t)m_psz >= (uintptr_t)m_cbAllocated);

            reserve(cchSrc + 1);
            memcpy(m_psz, a_pszSrc, cchSrc);
            m_psz[cchSrc] = '\0';
            m_cch = cchSrc;
            return *this;
        }
    }
    setNull();
    return *this;
}

int RTCString::assignNoThrow(const char *a_pszSrc) RT_NOEXCEPT
{
    if (a_pszSrc)
    {
        size_t cchSrc = strlen(a_pszSrc);
        if (cchSrc)
        {
            Assert((uintptr_t)&a_pszSrc - (uintptr_t)m_psz >= (uintptr_t)m_cbAllocated);

            int rc = reserveNoThrow(cchSrc + 1);
            if (RT_SUCCESS(rc))
            {
                memcpy(m_psz, a_pszSrc, cchSrc);
                m_psz[cchSrc] = '\0';
                m_cch = cchSrc;
                return VINF_SUCCESS;
            }
            return rc;
        }
    }
    setNull();
    return VINF_SUCCESS;
}

RTCString &RTCString::assign(const RTCString &a_rSrc, size_t a_offSrc, size_t a_cchSrc /*= npos*/)
{
    AssertReturn(&a_rSrc != this, *this);
    if (a_offSrc < a_rSrc.length())
    {
        size_t cchMax = a_rSrc.length() - a_offSrc;
        if (a_cchSrc > cchMax)
            a_cchSrc = cchMax;
        reserve(a_cchSrc + 1);
        memcpy(m_psz, a_rSrc.c_str() + a_offSrc, a_cchSrc);
        m_psz[a_cchSrc] = '\0';
        m_cch = a_cchSrc;
    }
    else
        setNull();
    return *this;
}

int RTCString::assignNoThrow(const RTCString &a_rSrc, size_t a_offSrc, size_t a_cchSrc /*= npos*/) RT_NOEXCEPT
{
    AssertReturn(&a_rSrc != this, VINF_SUCCESS);
    if (a_offSrc < a_rSrc.length())
    {
        size_t cchMax = a_rSrc.length() - a_offSrc;
        if (a_cchSrc > cchMax)
            a_cchSrc = cchMax;
        int rc = reserveNoThrow(a_cchSrc + 1);
        if (RT_SUCCESS(rc))
        {
            memcpy(m_psz, a_rSrc.c_str() + a_offSrc, a_cchSrc);
            m_psz[a_cchSrc] = '\0';
            m_cch = a_cchSrc;
            return VINF_SUCCESS;
        }
        return rc;
    }
    setNull();
    return VINF_SUCCESS;
}

RTCString &RTCString::assign(const char *a_pszSrc, size_t a_cchSrc)
{
    if (a_cchSrc)
    {
        a_cchSrc = RTStrNLen(a_pszSrc, a_cchSrc);
        if (a_cchSrc)
        {
            Assert((uintptr_t)&a_pszSrc - (uintptr_t)m_psz >= (uintptr_t)m_cbAllocated);

            reserve(a_cchSrc + 1);
            memcpy(m_psz, a_pszSrc, a_cchSrc);
            m_psz[a_cchSrc] = '\0';
            m_cch = a_cchSrc;
            return *this;
        }
    }
    setNull();
    return *this;
}

int RTCString::assignNoThrow(const char *a_pszSrc, size_t a_cchSrc) RT_NOEXCEPT
{
    if (a_cchSrc)
    {
        a_cchSrc = RTStrNLen(a_pszSrc, a_cchSrc);
        if (a_cchSrc)
        {
            Assert((uintptr_t)&a_pszSrc - (uintptr_t)m_psz >= (uintptr_t)m_cbAllocated);

            int rc = reserveNoThrow(a_cchSrc + 1);
            if (RT_SUCCESS(rc))
            {
                memcpy(m_psz, a_pszSrc, a_cchSrc);
                m_psz[a_cchSrc] = '\0';
                m_cch = a_cchSrc;
                return VINF_SUCCESS;
            }
            return rc;
        }
    }
    setNull();
    return VINF_SUCCESS;
}

RTCString &RTCString::assign(size_t a_cTimes, char a_ch)
{
    reserve(a_cTimes + 1);
    memset(m_psz, a_ch, a_cTimes);
    m_psz[a_cTimes] = '\0';
    m_cch = a_cTimes;
    return *this;
}


int RTCString::assignNoThrow(size_t a_cTimes, char a_ch) RT_NOEXCEPT
{
    int rc = reserveNoThrow(a_cTimes + 1);
    if (RT_SUCCESS(rc))
    {
        memset(m_psz, a_ch, a_cTimes);
        m_psz[a_cTimes] = '\0';
        m_cch = a_cTimes;
        return VINF_SUCCESS;
    }
    return rc;
}


RTCString &RTCString::printf(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    printfV(pszFormat, va);
    va_end(va);
    return *this;
}

int RTCString::printfNoThrow(const char *pszFormat, ...) RT_NOEXCEPT
{
    va_list va;
    va_start(va, pszFormat);
    int rc = printfVNoThrow(pszFormat, va);
    va_end(va);
    return rc;
}

/**
 * Callback used with RTStrFormatV by RTCString::printfV.
 *
 * @returns The number of bytes added (not used).
 *
 * @param   pvArg           The string object.
 * @param   pachChars       The characters to append.
 * @param   cbChars         The number of characters.  0 on the final callback.
 */
/*static*/ DECLCALLBACK(size_t)
RTCString::printfOutputCallback(void *pvArg, const char *pachChars, size_t cbChars)
{
    RTCString *pThis = (RTCString *)pvArg;
    if (cbChars)
    {
        size_t const cchBoth = pThis->m_cch + cbChars;
        if (cchBoth >= pThis->m_cbAllocated)
        {
            /* Double the buffer size, if it's less that _4M. Align sizes like
               for append. */
            size_t cbAlloc = RT_ALIGN_Z(pThis->m_cbAllocated, IPRT_MINISTRING_APPEND_ALIGNMENT);
            cbAlloc += RT_MIN(cbAlloc, _4M);
            if (cbAlloc <= cchBoth)
                cbAlloc = RT_ALIGN_Z(cchBoth + 1, IPRT_MINISTRING_APPEND_ALIGNMENT);
            pThis->reserve(cbAlloc);
#ifndef RT_EXCEPTIONS_ENABLED
            AssertReleaseReturn(pThis->capacity() > cchBoth, 0);
#endif
        }

        memcpy(&pThis->m_psz[pThis->m_cch], pachChars, cbChars);
        pThis->m_cch = cchBoth;
        pThis->m_psz[cchBoth] = '\0';
    }
    return cbChars;
}

RTCString &RTCString::printfV(const char *pszFormat, va_list va)
{
    cleanup();
    RTStrFormatV(printfOutputCallback, this, NULL, NULL, pszFormat, va);
    return *this;
}

RTCString &RTCString::appendPrintfV(const char *pszFormat, va_list va)
{
    RTStrFormatV(printfOutputCallback, this, NULL, NULL, pszFormat, va);
    return *this;
}

struct RTCSTRINGOTHROW
{
    RTCString  *pThis;
    int         rc;
};

/**
 * Callback used with RTStrFormatV by RTCString::printfVNoThrow.
 *
 * @returns The number of bytes added (not used).
 *
 * @param   pvArg           Pointer to a RTCSTRINGOTHROW structure.
 * @param   pachChars       The characters to append.
 * @param   cbChars         The number of characters.  0 on the final callback.
 */
/*static*/ DECLCALLBACK(size_t)
RTCString::printfOutputCallbackNoThrow(void *pvArg, const char *pachChars, size_t cbChars) RT_NOEXCEPT
{
    RTCString *pThis = ((RTCSTRINGOTHROW *)pvArg)->pThis;
    if (cbChars)
    {
        size_t const cchBoth = pThis->m_cch + cbChars;
        if (cchBoth >= pThis->m_cbAllocated)
        {
            /* Double the buffer size, if it's less that _4M. Align sizes like
               for append. */
            size_t cbAlloc = RT_ALIGN_Z(pThis->m_cbAllocated, IPRT_MINISTRING_APPEND_ALIGNMENT);
            cbAlloc += RT_MIN(cbAlloc, _4M);
            if (cbAlloc <= cchBoth)
                cbAlloc = RT_ALIGN_Z(cchBoth + 1, IPRT_MINISTRING_APPEND_ALIGNMENT);
            int rc = pThis->reserveNoThrow(cbAlloc);
            if (RT_SUCCESS(rc))
            { /* likely */ }
            else
            {
                ((RTCSTRINGOTHROW *)pvArg)->rc = rc;
                return cbChars;
            }
        }

        memcpy(&pThis->m_psz[pThis->m_cch], pachChars, cbChars);
        pThis->m_cch = cchBoth;
        pThis->m_psz[cchBoth] = '\0';
    }
    return cbChars;
}

int RTCString::printfVNoThrow(const char *pszFormat, va_list va) RT_NOEXCEPT
{
    cleanup();
    RTCSTRINGOTHROW Args = { this, VINF_SUCCESS };
    RTStrFormatV(printfOutputCallbackNoThrow, &Args, NULL, NULL, pszFormat, va);
    return Args.rc;
}

int RTCString::appendPrintfVNoThrow(const char *pszFormat, va_list va) RT_NOEXCEPT
{
    RTCSTRINGOTHROW Args = { this, VINF_SUCCESS };
    RTStrFormatV(printfOutputCallbackNoThrow, &Args, NULL, NULL, pszFormat, va);
    return Args.rc;
}

RTCString &RTCString::appendPrintf(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    appendPrintfV(pszFormat, va);
    va_end(va);
    return *this;
}

int RTCString::appendPrintfNoThrow(const char *pszFormat, ...) RT_NOEXCEPT
{
    va_list va;
    va_start(va, pszFormat);
    int rc = appendPrintfVNoThrow(pszFormat, va);
    va_end(va);
    return rc;
}

RTCString &RTCString::append(const RTCString &that)
{
    Assert(&that != this);
    return appendWorker(that.c_str(), that.length());
}

int RTCString::appendNoThrow(const RTCString &that) RT_NOEXCEPT
{
    Assert(&that != this);
    return appendWorkerNoThrow(that.c_str(), that.length());
}

RTCString &RTCString::append(const char *pszThat)
{
    return appendWorker(pszThat, strlen(pszThat));
}

int RTCString::appendNoThrow(const char *pszThat) RT_NOEXCEPT
{
    return appendWorkerNoThrow(pszThat, strlen(pszThat));
}

RTCString &RTCString::append(const RTCString &rThat, size_t offStart, size_t cchMax /*= RTSTR_MAX*/)
{
    if (offStart < rThat.length())
    {
        size_t cchLeft = rThat.length() - offStart;
        return appendWorker(rThat.c_str() + offStart, RT_MIN(cchLeft, cchMax));
    }
    return *this;
}

int RTCString::appendNoThrow(const RTCString &rThat, size_t offStart, size_t cchMax /*= RTSTR_MAX*/) RT_NOEXCEPT
{
    if (offStart < rThat.length())
    {
        size_t cchLeft = rThat.length() - offStart;
        return appendWorkerNoThrow(rThat.c_str() + offStart, RT_MIN(cchLeft, cchMax));
    }
    return VINF_SUCCESS;
}

RTCString &RTCString::append(const char *pszThat, size_t cchMax)
{
    return appendWorker(pszThat, RTStrNLen(pszThat, cchMax));
}

int RTCString::appendNoThrow(const char *pszThat, size_t cchMax) RT_NOEXCEPT
{
    return appendWorkerNoThrow(pszThat, RTStrNLen(pszThat, cchMax));
}

RTCString &RTCString::appendWorker(const char *pszSrc, size_t cchSrc)
{
    if (cchSrc)
    {
        Assert((uintptr_t)&pszSrc - (uintptr_t)m_psz >= (uintptr_t)m_cbAllocated);

        size_t cchThis = length();
        size_t cchBoth = cchThis + cchSrc;

        if (cchBoth >= m_cbAllocated)
        {
            reserve(RT_ALIGN_Z(cchBoth + 1, IPRT_MINISTRING_APPEND_ALIGNMENT));
            // calls realloc(cchBoth + 1) and sets m_cbAllocated; may throw bad_alloc.
#ifndef RT_EXCEPTIONS_ENABLED
            AssertRelease(capacity() > cchBoth);
#endif
        }

        memcpy(&m_psz[cchThis], pszSrc, cchSrc);
        m_psz[cchBoth] = '\0';
        m_cch = cchBoth;
    }
    return *this;
}

int RTCString::appendWorkerNoThrow(const char *pszSrc, size_t cchSrc) RT_NOEXCEPT
{
    if (cchSrc)
    {
        Assert((uintptr_t)&pszSrc - (uintptr_t)m_psz >= (uintptr_t)m_cbAllocated);

        size_t cchThis = length();
        size_t cchBoth = cchThis + cchSrc;

        if (cchBoth >= m_cbAllocated)
        {
            int rc = reserveNoThrow(RT_ALIGN_Z(cchBoth + 1, IPRT_MINISTRING_APPEND_ALIGNMENT));
            if (RT_SUCCESS(rc))
            { /* likely */ }
            else
                return rc;
        }

        memcpy(&m_psz[cchThis], pszSrc, cchSrc);
        m_psz[cchBoth] = '\0';
        m_cch = cchBoth;
    }
    return VINF_SUCCESS;
}

RTCString &RTCString::append(char ch)
{
    Assert((unsigned char)ch < 0x80);                  /* Don't create invalid UTF-8. */
    if (ch)
    {
        // allocate in chunks of 20 in case this gets called several times
        if (m_cch + 1 >= m_cbAllocated)
        {
            reserve(RT_ALIGN_Z(m_cch + 2, IPRT_MINISTRING_APPEND_ALIGNMENT));
            // calls realloc(cbBoth) and sets m_cbAllocated; may throw bad_alloc.
#ifndef RT_EXCEPTIONS_ENABLED
            AssertRelease(capacity() > m_cch + 1);
#endif
        }

        m_psz[m_cch] = ch;
        m_psz[++m_cch] = '\0';
    }
    return *this;
}

int RTCString::appendNoThrow(char ch) RT_NOEXCEPT
{
    Assert((unsigned char)ch < 0x80);                  /* Don't create invalid UTF-8. */
    if (ch)
    {
        // allocate in chunks of 20 in case this gets called several times
        if (m_cch + 1 >= m_cbAllocated)
        {
            int rc = reserveNoThrow(RT_ALIGN_Z(m_cch + 2, IPRT_MINISTRING_APPEND_ALIGNMENT));
            if (RT_SUCCESS(rc))
            { /* likely */ }
            else
                return rc;
        }

        m_psz[m_cch] = ch;
        m_psz[++m_cch] = '\0';
    }
    return VINF_SUCCESS;
}

RTCString &RTCString::appendCodePoint(RTUNICP uc)
{
    /*
     * Single byte encoding.
     */
    if (uc < 0x80)
        return RTCString::append((char)uc);

    /*
     * Multibyte encoding.
     * Assume max encoding length when resizing the string, that's simpler.
     */
    AssertReturn(uc <= UINT32_C(0x7fffffff), *this);

    if (m_cch + 6 >= m_cbAllocated)
    {
        reserve(RT_ALIGN_Z(m_cch + 6 + 1, IPRT_MINISTRING_APPEND_ALIGNMENT));
        // calls realloc(cbBoth) and sets m_cbAllocated; may throw bad_alloc.
#ifndef RT_EXCEPTIONS_ENABLED
        AssertRelease(capacity() > m_cch + 6);
#endif
    }

    char *pszNext = RTStrPutCp(&m_psz[m_cch], uc);
    m_cch = pszNext - m_psz;
    *pszNext = '\0';

    return *this;
}

int RTCString::appendCodePointNoThrow(RTUNICP uc) RT_NOEXCEPT
{
    /*
     * Single byte encoding.
     */
    if (uc < 0x80)
        return RTCString::appendNoThrow((char)uc);

    /*
     * Multibyte encoding.
     * Assume max encoding length when resizing the string, that's simpler.
     */
    AssertReturn(uc <= UINT32_C(0x7fffffff), VERR_INVALID_UTF8_ENCODING);

    if (m_cch + 6 >= m_cbAllocated)
    {
        int rc = reserveNoThrow(RT_ALIGN_Z(m_cch + 6 + 1, IPRT_MINISTRING_APPEND_ALIGNMENT));
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
            return rc;
    }

    char *pszNext = RTStrPutCp(&m_psz[m_cch], uc);
    m_cch = pszNext - m_psz;
    *pszNext = '\0';

    return VINF_SUCCESS;
}

RTCString &RTCString::erase(size_t offStart /*= 0*/, size_t cchLength /*= npos*/) RT_NOEXCEPT
{
    size_t cch = length();
    if (offStart < cch)
    {
        if (cchLength >= cch - offStart)
        {
            /* Trail removal, nothing to move.  */
            m_cch = offStart;
            m_psz[offStart] = '\0';
        }
        else if (cchLength > 0)
        {
            /* Pull up the tail to offStart. */
            size_t cchAfter = cch - offStart - cchLength;
            memmove(&m_psz[offStart], &m_psz[offStart + cchLength], cchAfter);
            m_cch = cch -= cchLength;
            m_psz[cch] = '\0';
        }
    }
    return *this;
}

RTCString &RTCString::replace(size_t offStart, size_t cchLength, const RTCString &rStrReplacement)
{
    return replaceWorker(offStart, cchLength, rStrReplacement.c_str(), rStrReplacement.length());
}

int RTCString::replaceNoThrow(size_t offStart, size_t cchLength, const RTCString &rStrReplacement) RT_NOEXCEPT
{
    return replaceWorkerNoThrow(offStart, cchLength, rStrReplacement.c_str(), rStrReplacement.length());
}

RTCString &RTCString::replace(size_t offStart, size_t cchLength, const RTCString &rStrReplacement,
                              size_t offReplacement, size_t cchReplacement)
{
    Assert(this != &rStrReplacement);
    if (cchReplacement > 0)
    {
        if (offReplacement < rStrReplacement.length())
        {
            size_t cchMaxReplacement = rStrReplacement.length() - offReplacement;
            return replaceWorker(offStart, cchLength, rStrReplacement.c_str() + offReplacement,
                                 RT_MIN(cchReplacement, cchMaxReplacement));
        }
        /* Our non-standard handling of out_of_range situations. */
        AssertMsgFailed(("offReplacement=%zu (cchReplacement=%zu) rStrReplacement.length()=%zu\n",
                         offReplacement, cchReplacement, rStrReplacement.length()));
    }
    return replaceWorker(offStart, cchLength, "", 0);
}

int RTCString::replaceNoThrow(size_t offStart, size_t cchLength, const RTCString &rStrReplacement,
                              size_t offReplacement, size_t cchReplacement) RT_NOEXCEPT
{
    Assert(this != &rStrReplacement);
    if (cchReplacement > 0)
    {
        if (offReplacement < rStrReplacement.length())
        {
            size_t cchMaxReplacement = rStrReplacement.length() - offReplacement;
            return replaceWorkerNoThrow(offStart, cchLength, rStrReplacement.c_str() + offReplacement,
                                        RT_MIN(cchReplacement, cchMaxReplacement));
        }
        return VERR_OUT_OF_RANGE;
    }
    return replaceWorkerNoThrow(offStart, cchLength, "", 0);
}

RTCString &RTCString::replace(size_t offStart, size_t cchLength, const char *pszReplacement)
{
    return replaceWorker(offStart, cchLength, pszReplacement, strlen(pszReplacement));
}

int RTCString::replaceNoThrow(size_t offStart, size_t cchLength, const char *pszReplacement) RT_NOEXCEPT
{
    return replaceWorkerNoThrow(offStart, cchLength, pszReplacement, strlen(pszReplacement));
}

RTCString &RTCString::replace(size_t offStart, size_t cchLength, const char *pszReplacement, size_t cchReplacement)
{
    return replaceWorker(offStart, cchLength, pszReplacement, RTStrNLen(pszReplacement, cchReplacement));
}

int RTCString::replaceNoThrow(size_t offStart, size_t cchLength, const char *pszReplacement, size_t cchReplacement) RT_NOEXCEPT
{
    return replaceWorkerNoThrow(offStart, cchLength, pszReplacement, RTStrNLen(pszReplacement, cchReplacement));
}

RTCString &RTCString::replaceWorker(size_t offStart, size_t cchLength, const char *pszSrc, size_t cchSrc)
{
    Assert((uintptr_t)&pszSrc - (uintptr_t)m_psz >= (uintptr_t)m_cbAllocated || !cchSrc);

    /*
     * Our non-standard handling of out_of_range situations.
     */
    size_t const cchOldLength = length();
    AssertMsgReturn(offStart < cchOldLength, ("offStart=%zu (cchLength=%zu); length()=%zu\n", offStart, cchLength, cchOldLength),
                    *this);

    /*
     * Correct the length parameter.
     */
    size_t cchMaxLength = cchOldLength - offStart;
    if (cchMaxLength < cchLength)
        cchLength = cchMaxLength;

    /*
     * Adjust string allocation if necessary.
     */
    size_t cchNew = cchOldLength - cchLength + cchSrc;
    if (cchNew >= m_cbAllocated)
    {
        reserve(RT_ALIGN_Z(cchNew + 1, IPRT_MINISTRING_APPEND_ALIGNMENT));
        // calls realloc(cchBoth + 1) and sets m_cbAllocated; may throw bad_alloc.
#ifndef RT_EXCEPTIONS_ENABLED
        AssertRelease(capacity() > cchNew);
#endif
    }

    /*
     * Make the change.
     */
    size_t cchAfter = cchOldLength - offStart - cchLength;
    if (cchAfter > 0)
        memmove(&m_psz[offStart + cchSrc], &m_psz[offStart + cchLength], cchAfter);
    memcpy(&m_psz[offStart], pszSrc, cchSrc);
    m_psz[cchNew] = '\0';
    m_cch = cchNew;

    return *this;
}

int RTCString::replaceWorkerNoThrow(size_t offStart, size_t cchLength, const char *pszSrc, size_t cchSrc) RT_NOEXCEPT
{
    Assert((uintptr_t)&pszSrc - (uintptr_t)m_psz >= (uintptr_t)m_cbAllocated || !cchSrc);

    /*
     * Our non-standard handling of out_of_range situations.
     */
    size_t const cchOldLength = length();
    AssertMsgReturn(offStart < cchOldLength, ("offStart=%zu (cchLength=%zu); length()=%zu\n", offStart, cchLength, cchOldLength),
                    VERR_OUT_OF_RANGE);

    /*
     * Correct the length parameter.
     */
    size_t cchMaxLength = cchOldLength - offStart;
    if (cchMaxLength < cchLength)
        cchLength = cchMaxLength;

    /*
     * Adjust string allocation if necessary.
     */
    size_t cchNew = cchOldLength - cchLength + cchSrc;
    if (cchNew >= m_cbAllocated)
    {
        int rc = reserveNoThrow(RT_ALIGN_Z(cchNew + 1, IPRT_MINISTRING_APPEND_ALIGNMENT));
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
            return rc;
    }

    /*
     * Make the change.
     */
    size_t cchAfter = cchOldLength - offStart - cchLength;
    if (cchAfter > 0)
        memmove(&m_psz[offStart + cchSrc], &m_psz[offStart + cchLength], cchAfter);
    memcpy(&m_psz[offStart], pszSrc, cchSrc);
    m_psz[cchNew] = '\0';
    m_cch = cchNew;

    return VINF_SUCCESS;
}


RTCString &RTCString::truncate(size_t cchMax) RT_NOEXCEPT
{
    if (cchMax < m_cch)
    {
        /*
         * Make sure the truncated string ends with a correctly encoded
         * codepoint and is not missing a few bytes.
         */
        if (cchMax > 0)
        {
            char chTail = m_psz[cchMax];
            if (   (chTail & 0x80) == 0         /* single byte codepoint */
                || (chTail & 0xc0) == 0xc0)     /* first byte of codepoint sequence. */
            { /* likely */ }
            else
            {
                /* We need to find the start of the codepoint sequence: */
                do
                    cchMax -= 1;
                while (   cchMax > 0
                       && (m_psz[cchMax] & 0xc0) != 0xc0);
            }
        }

        /*
         * Do the truncating.
         */
        m_psz[cchMax] = '\0';
        m_cch = cchMax;
    }
    return *this;
}


size_t RTCString::find(const char *pszNeedle, size_t offStart /*= 0*/) const RT_NOEXCEPT
{
    if (offStart < length())
    {
        const char *pszThis = c_str();
        if (pszThis)
        {
            if (pszNeedle && *pszNeedle != '\0')
            {
                const char *pszHit = strstr(pszThis + offStart, pszNeedle);
                if (pszHit)
                    return pszHit - pszThis;
            }
        }
    }

    return npos;
}

size_t RTCString::find(const RTCString *pStrNeedle, size_t offStart /*= 0*/) const RT_NOEXCEPT
{
    if (offStart < length())
    {
        const char *pszThis = c_str();
        if (pszThis)
        {
            if (pStrNeedle)
            {
                const char *pszNeedle = pStrNeedle->c_str();
                if (pszNeedle && *pszNeedle != '\0')
                {
                    const char *pszHit = strstr(pszThis + offStart, pszNeedle);
                    if (pszHit)
                        return pszHit - pszThis;
                }
            }
        }
    }

    return npos;
}


size_t RTCString::find(const RTCString &rStrNeedle, size_t offStart /*= 0*/) const RT_NOEXCEPT
{
    return find(&rStrNeedle, offStart);
}


size_t RTCString::find(const char chNeedle, size_t offStart /*= 0*/) const RT_NOEXCEPT
{
    Assert((unsigned int)chNeedle < 128U);
    if (offStart < length())
    {
        const char *pszThis = c_str();
        if (pszThis)
        {
            const char *pszHit = (const char *)memchr(&pszThis[offStart], chNeedle, length() - offStart);
            if (pszHit)
                return pszHit - pszThis;
        }
    }
    return npos;
}


void RTCString::findReplace(char chFind, char chReplace) RT_NOEXCEPT
{
    Assert((unsigned int)chFind    < 128U);
    Assert((unsigned int)chReplace < 128U);

    for (size_t i = 0; i < length(); ++i)
    {
        char *p = &m_psz[i];
        if (*p == chFind)
            *p = chReplace;
    }
}

size_t RTCString::count(char ch) const RT_NOEXCEPT
{
    Assert((unsigned int)ch < 128U);

    size_t      c   = 0;
    const char *psz = m_psz;
    if (psz)
    {
        char    chCur;
        while ((chCur = *psz++) != '\0')
            if (chCur == ch)
                c++;
    }
    return c;
}

#if 0  /** @todo implement these when needed. */
size_t RTCString::count(const char *psz, CaseSensitivity cs = CaseSensitive) const RT_NOEXCEPT
{
}

size_t RTCString::count(const RTCString *pStr, CaseSensitivity cs = CaseSensitive) const RT_NOEXCEPT
{

}
#endif


RTCString &RTCString::strip() RT_NOEXCEPT
{
    stripRight();
    return stripLeft();
}


RTCString &RTCString::stripLeft() RT_NOEXCEPT
{
    char        *psz = m_psz;
    size_t const cch = m_cch;
    size_t       off = 0;
    while (off < cch && RT_C_IS_SPACE(psz[off]))
        off++;
    if (off > 0)
    {
        if (off != cch)
        {
            memmove(psz, &psz[off], cch - off + 1);
            m_cch = cch - off;
        }
        else
            setNull();
    }
    return *this;
}


RTCString &RTCString::stripRight() RT_NOEXCEPT
{
    char  *psz = m_psz;
    size_t cch = m_cch;
    while (cch > 0 && RT_C_IS_SPACE(psz[cch - 1]))
        cch--;
    if (m_cch != cch)
    {
        m_cch = cch;
        psz[cch] = '\0';
    }
    return *this;
}



RTCString RTCString::substrCP(size_t pos /*= 0*/, size_t n /*= npos*/) const
{
    RTCString ret;

    if (n)
    {
        const char *psz;

        if ((psz = c_str()))
        {
            RTUNICP cp;

            // walk the UTF-8 characters until where the caller wants to start
            size_t i = pos;
            while (*psz && i--)
                if (RT_FAILURE(RTStrGetCpEx(&psz, &cp)))
                    return ret;     // return empty string on bad encoding

            const char *pFirst = psz;

            if (n == npos)
                // all the rest:
                ret = pFirst;
            else
            {
                i = n;
                while (*psz && i--)
                    if (RT_FAILURE(RTStrGetCpEx(&psz, &cp)))
                        return ret;     // return empty string on bad encoding

                size_t cbCopy = psz - pFirst;
                if (cbCopy)
                {
                    ret.reserve(cbCopy + 1); // may throw bad_alloc
#ifndef RT_EXCEPTIONS_ENABLED
                    AssertRelease(capacity() >= cbCopy + 1);
#endif
                    memcpy(ret.m_psz, pFirst, cbCopy);
                    ret.m_cch = cbCopy;
                    ret.m_psz[cbCopy] = '\0';
                }
            }
        }
    }

    return ret;
}

bool RTCString::endsWith(const RTCString &a_rThat) const RT_NOEXCEPT
{
    size_t const cchThis = length();
    size_t const cchThat = a_rThat.length();
    if (   cchThat > 0
        && cchThat <= cchThis)
        return ::memcmp(&m_psz[cchThis - cchThat], a_rThat.m_psz, cchThat) == 0;
    return false;
}

bool RTCString::endsWithI(const RTCString &a_rThat) const RT_NOEXCEPT
{
    size_t const cchThis = length();
    size_t const cchThat = a_rThat.length();
    if (   cchThat > 0
        && cchThat <= cchThis)
        return ::RTStrICmp(&m_psz[cchThis - cchThat], a_rThat.m_psz) == 0;
    return false;
}

bool RTCString::endsWith(const char *a_pszSuffix, size_t a_cchSuffix) const RT_NOEXCEPT
{
    Assert(RTStrNLen(a_pszSuffix, a_cchSuffix) == a_cchSuffix);
    return a_cchSuffix >  0
        && a_cchSuffix <= length()
        && ::memcmp(&m_psz[length() - a_cchSuffix], a_pszSuffix, a_cchSuffix) == 0;
}

bool RTCString::endsWith(const char *a_pszSuffix) const RT_NOEXCEPT
{
    if (a_pszSuffix)
        return endsWith(a_pszSuffix, strlen(a_pszSuffix));
    return false;
}

bool RTCString::endsWithI(const char *a_pszSuffix, size_t a_cchSuffix) const RT_NOEXCEPT
{
    Assert(RTStrNLen(a_pszSuffix, a_cchSuffix) == a_cchSuffix);
    return a_cchSuffix >  0
        && a_cchSuffix <= length()
        && ::RTStrNICmp(&m_psz[length() - a_cchSuffix], a_pszSuffix, a_cchSuffix) == 0;
}

bool RTCString::endsWithI(const char *a_pszSuffix) const RT_NOEXCEPT
{
    if (a_pszSuffix)
        return endsWithI(a_pszSuffix, strlen(a_pszSuffix));
    return false;
}

bool RTCString::startsWith(const RTCString &a_rThat) const RT_NOEXCEPT
{
    size_t const cchThis = length();
    size_t const cchThat = a_rThat.length();
    if (   cchThat > 0
        && cchThat <= cchThis)
        return ::memcmp(m_psz, a_rThat.m_psz, cchThat) == 0;
    return false;
}

bool RTCString::startsWithI(const RTCString &a_rThat) const RT_NOEXCEPT
{
    size_t const cchThis = length();
    size_t const cchThat = a_rThat.length();
    if (   cchThat > 0
        && cchThat <= cchThis)
        return ::RTStrNICmp(m_psz, a_rThat.m_psz, cchThat) == 0;
    return false;
}

bool RTCString::startsWith(const char *a_pszPrefix, size_t a_cchPrefix) const RT_NOEXCEPT
{
    Assert(RTStrNLen(a_pszPrefix, a_cchPrefix) == a_cchPrefix);
    return a_cchPrefix >  0
        && a_cchPrefix <= length()
        && ::memcmp(m_psz, a_pszPrefix, a_cchPrefix) == 0;
}

bool RTCString::startsWith(const char *a_pszPrefix) const RT_NOEXCEPT
{
    if (a_pszPrefix)
        return startsWith(a_pszPrefix, strlen(a_pszPrefix));
    return false;
}

bool RTCString::startsWithI(const char *a_pszPrefix, size_t a_cchPrefix) const RT_NOEXCEPT
{
    Assert(RTStrNLen(a_pszPrefix, a_cchPrefix) == a_cchPrefix);
    return a_cchPrefix >  0
        && a_cchPrefix <= length()
        && ::RTStrNICmp(m_psz, a_pszPrefix, a_cchPrefix) == 0;
}

bool RTCString::startsWithI(const char *a_pszPrefix) const RT_NOEXCEPT
{
    if (a_pszPrefix)
        return startsWithI(a_pszPrefix, strlen(a_pszPrefix));
    return false;
}

bool RTCString::startsWithWord(const char *pszWord, CaseSensitivity enmCase /*= CaseSensitive*/) const RT_NOEXCEPT
{
    const char *pszSrc  = RTStrStripL(c_str()); /** @todo RTStrStripL doesn't use RTUniCpIsSpace (nbsp) */
    size_t      cchWord = strlen(pszWord);
    if (  enmCase == CaseSensitive
        ? RTStrNCmp(pszSrc, pszWord, cchWord) == 0
        : RTStrNICmp(pszSrc, pszWord, cchWord) == 0)
    {
        if (   pszSrc[cchWord] == '\0'
            || RT_C_IS_SPACE(pszSrc[cchWord])
            || RT_C_IS_PUNCT(pszSrc[cchWord]) )
            return true;
        RTUNICP uc = RTStrGetCp(&pszSrc[cchWord]);
        if (RTUniCpIsSpace(uc))
            return true;
    }
    return false;
}

bool RTCString::startsWithWord(const RTCString &rThat, CaseSensitivity enmCase /*= CaseSensitive*/) const RT_NOEXCEPT
{
    return startsWithWord(rThat.c_str(), enmCase);
}

bool RTCString::contains(const RTCString &that, CaseSensitivity cs /*= CaseSensitive*/) const RT_NOEXCEPT
{
    /** @todo r-bird: Not checking for NULL strings like startsWith does (and
     *        endsWith only does half way). */
    if (cs == CaseSensitive)
        return ::RTStrStr(m_psz, that.m_psz) != NULL;
    return ::RTStrIStr(m_psz, that.m_psz) != NULL;
}

bool RTCString::contains(const char *pszNeedle, CaseSensitivity cs /*= CaseSensitive*/) const RT_NOEXCEPT
{
    /** @todo r-bird: Not checking for NULL strings like startsWith does (and
     *        endsWith only does half way). */
    if (cs == CaseSensitive)
        return ::RTStrStr(m_psz, pszNeedle) != NULL;
    return ::RTStrIStr(m_psz, pszNeedle) != NULL;
}

int RTCString::toInt(uint64_t &i) const RT_NOEXCEPT
{
    if (!m_psz)
        return VERR_NO_DIGITS;
    return RTStrToUInt64Ex(m_psz, NULL, 0, &i);
}

int RTCString::toInt(uint32_t &i) const RT_NOEXCEPT
{
    if (!m_psz)
        return VERR_NO_DIGITS;
    return RTStrToUInt32Ex(m_psz, NULL, 0, &i);
}

RTCList<RTCString, RTCString *>
RTCString::split(const RTCString &a_rstrSep, SplitMode mode /* = RemoveEmptyParts */) const
{
    RTCList<RTCString> strRet;
    if (!m_psz)
        return strRet;
    if (a_rstrSep.isEmpty())
    {
        strRet.append(RTCString(m_psz));
        return strRet;
    }

    size_t      cch    = m_cch;
    char const *pszTmp = m_psz;
    while (cch > 0)
    {
        char const *pszNext = strstr(pszTmp, a_rstrSep.c_str());
        if (!pszNext)
        {
            strRet.append(RTCString(pszTmp, cch));
            break;
        }
        size_t cchNext = pszNext - pszTmp;
        if (   cchNext > 0
            || mode == KeepEmptyParts)
            strRet.append(RTCString(pszTmp, cchNext));
        pszTmp += cchNext + a_rstrSep.length();
        cch    -= cchNext + a_rstrSep.length();
    }

    return strRet;
}

/* static */
RTCString
RTCString::joinEx(const RTCList<RTCString, RTCString *> &a_rList,
                  const RTCString &a_rstrPrefix /* = "" */,
                  const RTCString &a_rstrSep /* = "" */)
{
    RTCString strRet;
    if (a_rList.size() > 1)
    {
        /* calc the required size */
        size_t cbNeeded = a_rstrSep.length() * (a_rList.size() - 1) + 1;
        cbNeeded += a_rstrPrefix.length() * (a_rList.size() - 1) + 1;
        for (size_t i = 0; i < a_rList.size(); ++i)
            cbNeeded += a_rList.at(i).length();
        strRet.reserve(cbNeeded);

        /* do the appending. */
        for (size_t i = 0; i < a_rList.size() - 1; ++i)
        {
            if (a_rstrPrefix.isNotEmpty())
                strRet.append(a_rstrPrefix);
            strRet.append(a_rList.at(i));
            strRet.append(a_rstrSep);
        }
        strRet.append(a_rList.last());
    }
    /* special case: one list item. */
    else if (a_rList.size() > 0)
    {
        if (a_rstrPrefix.isNotEmpty())
            strRet.append(a_rstrPrefix);
        strRet.append(a_rList.last());
    }

    return strRet;
}

/* static */
RTCString
RTCString::join(const RTCList<RTCString, RTCString *> &a_rList,
                const RTCString &a_rstrSep /* = "" */)
{
    return RTCString::joinEx(a_rList,
                             "" /* a_rstrPrefix */, a_rstrSep);
}

RTDECL(const RTCString) operator+(const RTCString &a_rStr1, const RTCString &a_rStr2)
{
    RTCString strRet(a_rStr1);
    strRet += a_rStr2;
    return strRet;
}

RTDECL(const RTCString) operator+(const RTCString &a_rStr1, const char *a_pszStr2)
{
    RTCString strRet(a_rStr1);
    strRet += a_pszStr2;
    return strRet;
}

RTDECL(const RTCString) operator+(const char *a_psz1, const RTCString &a_rStr2)
{
    RTCString strRet(a_psz1);
    strRet += a_rStr2;
    return strRet;
}

