/* $Id: string.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MS COM / XPCOM Abstraction Layer - UTF-8 and UTF-16 string classes.
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

#include "VBox/com/string.h"

#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/uni.h>

namespace com
{

// BSTR representing a null wide char with 32 bits of length prefix (0);
// this will work on Windows as well as other platforms where BSTR does
// not use length prefixes
const OLECHAR g_achEmptyBstr[3] = { 0, 0, 0 };
const BSTR g_bstrEmpty = (BSTR)&g_achEmptyBstr[2];

/* static */
const Bstr Bstr::Empty; /* default ctor is OK */


Bstr &Bstr::printf(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    HRESULT hrc = printfVNoThrow(pszFormat, va);
    va_end(va);
#ifdef RT_EXCEPTIONS_ENABLED
    if (hrc == S_OK)
    { /* likely */ }
    else
        throw std::bad_alloc();
#else
    Assert(hrc == S_OK); RT_NOREF(hrc);
#endif
    return *this;
}

HRESULT Bstr::printfNoThrow(const char *pszFormat, ...) RT_NOEXCEPT
{
    va_list va;
    va_start(va, pszFormat);
    HRESULT hrc = printfVNoThrow(pszFormat, va);
    va_end(va);
    return hrc;
}


Bstr &Bstr::printfV(const char *pszFormat, va_list va)
{
    HRESULT hrc = printfVNoThrow(pszFormat, va);
#ifdef RT_EXCEPTIONS_ENABLED
    if (hrc == S_OK)
    { /* likely */ }
    else
        throw std::bad_alloc();
#else
    Assert(hrc == S_OK); RT_NOREF(hrc);
#endif
    return *this;
}

struct BSTRNOTHROW
{
    Bstr   *pThis;
    size_t  cwcAlloc;
    size_t  offDst;
    HRESULT hrc;
};

/**
 * Callback used with RTStrFormatV by Bstr::printfVNoThrow.
 *
 * @returns The number of bytes added (not used).
 *
 * @param   pvArg           Pointer to a BSTRNOTHROW structure.
 * @param   pachChars       The characters to append.
 * @param   cbChars         The number of characters.  0 on the final callback.
 */
/*static*/ DECLCALLBACK(size_t)
Bstr::printfOutputCallbackNoThrow(void *pvArg, const char *pachChars, size_t cbChars) RT_NOEXCEPT
{
    BSTRNOTHROW *pArgs = (BSTRNOTHROW *)pvArg;
    if (cbChars)
    {
        size_t cwcAppend;
        int vrc = ::RTStrCalcUtf16LenEx(pachChars, cbChars, &cwcAppend);
        AssertRCReturnStmt(vrc, pArgs->hrc = E_UNEXPECTED, 0);

        /*
         * Ensure we've got sufficient memory.
         */
        Bstr *pThis = pArgs->pThis;
        size_t const cwcBoth = pArgs->offDst + cwcAppend;
        if (cwcBoth >= pArgs->cwcAlloc)
        {
            if (pArgs->hrc == S_OK)
            {
                /* Double the buffer size, if it's less that _1M. Align sizes like
                   for append. */
                size_t cwcAlloc = RT_ALIGN_Z(pArgs->cwcAlloc, 128);
                cwcAlloc += RT_MIN(cwcAlloc, _1M);
                if (cwcAlloc <= cwcBoth)
                    cwcAlloc = RT_ALIGN_Z(cwcBoth + 1, 512);
                pArgs->hrc = pThis->reserveNoThrow(cwcAlloc, true /*fForce*/);
                AssertMsgReturn(pArgs->hrc == S_OK, ("cwcAlloc=%#zx\n", cwcAlloc), 0);
                pArgs->cwcAlloc = cwcAlloc;
            }
            else
                return 0;
        }

        /*
         * Do the conversion.
         */
        PRTUTF16 pwszDst = pThis->m_bstr + pArgs->offDst;
        Assert(pArgs->cwcAlloc > pArgs->offDst);
        vrc = ::RTStrToUtf16Ex(pachChars, cbChars, &pwszDst, pArgs->cwcAlloc - pArgs->offDst, &cwcAppend);
        AssertRCReturnStmt(vrc, pArgs->hrc = E_UNEXPECTED, 0);
        pArgs->offDst += cwcAppend;
    }
    return cbChars;
}

HRESULT Bstr::printfVNoThrow(const char *pszFormat, va_list va) RT_NOEXCEPT
{
    cleanup();

    BSTRNOTHROW Args = { this, 0, 0, S_OK };
    RTStrFormatV(printfOutputCallbackNoThrow, &Args, NULL, NULL, pszFormat, va);
    if (Args.hrc == S_OK)
    {
        Args.hrc = joltNoThrow(Args.offDst);
        if (Args.hrc == S_OK)
            return S_OK;
    }

    cleanup();
    return Args.hrc;
}

void Bstr::copyFromN(const char *a_pszSrc, size_t a_cchMax)
{
    /*
     * Initialize m_bstr first in case of throws further down in the code, then
     * check for empty input (m_bstr == NULL means empty, there are no NULL
     * strings).
     */
    m_bstr = NULL;
    if (!a_cchMax || !a_pszSrc || !*a_pszSrc)
        return;

    /*
     * Calculate the length and allocate a BSTR string buffer of the right
     * size, i.e. optimize heap usage.
     */
    size_t cwc;
    int vrc = ::RTStrCalcUtf16LenEx(a_pszSrc, a_cchMax, &cwc);
    if (RT_SUCCESS(vrc))
    {
        m_bstr = ::SysAllocStringByteLen(NULL, (unsigned)(cwc * sizeof(OLECHAR)));
        if (RT_LIKELY(m_bstr))
        {
            PRTUTF16 pwsz = (PRTUTF16)m_bstr;
            vrc = ::RTStrToUtf16Ex(a_pszSrc, a_cchMax, &pwsz, cwc + 1, NULL);
            if (RT_SUCCESS(vrc))
                return;

            /* This should not happen! */
            AssertRC(vrc);
            cleanup();
        }
    }
    else /* ASSUME: input is valid Utf-8. Fake out of memory error. */
        AssertLogRelMsgFailed(("%Rrc %.*Rhxs\n", vrc, RTStrNLen(a_pszSrc, a_cchMax), a_pszSrc));
#ifdef RT_EXCEPTIONS_ENABLED
    throw std::bad_alloc();
#endif
}

HRESULT Bstr::cleanupAndCopyFromNoThrow(const char *a_pszSrc, size_t a_cchMax) RT_NOEXCEPT
{
    /*
     * Check for empty input (m_bstr == NULL means empty, there are no NULL strings).
     */
    cleanup();
    if (!a_cchMax || !a_pszSrc || !*a_pszSrc)
        return S_OK;

    /*
     * Calculate the length and allocate a BSTR string buffer of the right
     * size, i.e. optimize heap usage.
     */
    HRESULT hrc;
    size_t cwc;
    int vrc = ::RTStrCalcUtf16LenEx(a_pszSrc, a_cchMax, &cwc);
    if (RT_SUCCESS(vrc))
    {
        m_bstr = ::SysAllocStringByteLen(NULL, (unsigned)(cwc * sizeof(OLECHAR)));
        if (RT_LIKELY(m_bstr))
        {
            PRTUTF16 pwsz = (PRTUTF16)m_bstr;
            vrc = ::RTStrToUtf16Ex(a_pszSrc, a_cchMax, &pwsz, cwc + 1, NULL);
            if (RT_SUCCESS(vrc))
                return S_OK;

            /* This should not happen! */
            AssertRC(vrc);
            cleanup();
            hrc = E_UNEXPECTED;
        }
        else
            hrc = E_OUTOFMEMORY;
    }
    else
    {
        /* Unexpected: Invalid UTF-8 input. */
        AssertLogRelMsgFailed(("%Rrc %.*Rhxs\n", vrc, RTStrNLen(a_pszSrc, a_cchMax), a_pszSrc));
        hrc = E_UNEXPECTED;
    }
    return hrc;
}


int Bstr::compareUtf8(const char *a_pszRight, CaseSensitivity a_enmCase /*= CaseSensitive*/) const
{
    PCRTUTF16 pwszLeft = m_bstr;

    /*
     * Special case for null/empty strings.  Unlike RTUtf16Cmp we
     * treat null and empty equally.
     */
    if (!pwszLeft)
        return !a_pszRight || *a_pszRight == '\0' ? 0 : -1;
    if (!a_pszRight)
        return *pwszLeft == '\0'                  ? 0 :  1;

    /*
     * Compare with a UTF-8 string by enumerating them char by char.
     */
    for (;;)
    {
        RTUNICP ucLeft;
        int vrc = RTUtf16GetCpEx(&pwszLeft, &ucLeft);
        AssertRCReturn(vrc, 1);

        RTUNICP ucRight;
        vrc = RTStrGetCpEx(&a_pszRight, &ucRight);
        AssertRCReturn(vrc, -1);
        if (ucLeft == ucRight)
        {
            if (ucLeft)
                continue;
            return 0;
        }

        if (a_enmCase == CaseInsensitive)
        {
            if (RTUniCpToUpper(ucLeft) == RTUniCpToUpper(ucRight))
                continue;
            if (RTUniCpToLower(ucLeft) == RTUniCpToLower(ucRight))
                continue;
        }

        return ucLeft < ucRight ? -1 : 1;
    }
}


bool Bstr::startsWith(Bstr const &a_rStart) const
{
    return RTUtf16NCmp(m_bstr, a_rStart.m_bstr, a_rStart.length()) == 0;
}


bool Bstr::startsWith(RTCString const &a_rStart) const
{
    return RTUtf16NCmpUtf8(m_bstr, a_rStart.c_str(), RTSTR_MAX, a_rStart.length()) == 0;
}


bool Bstr::startsWith(const char *a_pszStart) const
{
    return RTUtf16NCmpUtf8(m_bstr, a_pszStart, RTSTR_MAX, strlen(a_pszStart)) == 0;
}


#ifndef VBOX_WITH_XPCOM

HRESULT Bstr::joltNoThrow(ssize_t cwcNew /* = -1*/) RT_NOEXCEPT
{
    if (m_bstr)
    {
        size_t const cwcAlloc  = ::SysStringLen(m_bstr);
        size_t const cwcActual = cwcNew < 0 ? ::RTUtf16Len(m_bstr) : (size_t)cwcNew;
        Assert(cwcNew < 0 || cwcActual == ::RTUtf16Len(m_bstr));
        if (cwcActual != cwcAlloc)
        {
            Assert(cwcActual <= cwcAlloc);
            Assert((unsigned int)cwcActual == cwcActual);

            /* Official way: Reallocate the string.   We could of course just update the size-prefix if we dared... */
            if (!::SysReAllocStringLen(&m_bstr, NULL, (unsigned int)cwcActual))
            {
                AssertFailed();
                return E_OUTOFMEMORY;
            }
        }
    }
    else
        Assert(cwcNew <= 0);
    return S_OK;
}


void Bstr::jolt(ssize_t cwcNew /* = -1*/)
{
    HRESULT hrc = joltNoThrow(cwcNew);
# ifdef RT_EXCEPTIONS_ENABLED
    if (hrc != S_OK)
        throw std::bad_alloc();
# else
    Assert(hrc == S_OK); RT_NOREF(hrc);
# endif
}

#endif /* !VBOX_WITH_XPCOM */


HRESULT Bstr::reserveNoThrow(size_t cwcMin, bool fForce /*= false*/) RT_NOEXCEPT
{
    /* If not forcing the string to the cwcMin length, check cwcMin against the
       current string length: */
    if (!fForce)
    {
        size_t cwcCur = m_bstr ? ::SysStringLen(m_bstr) : 0;
        if (cwcCur >= cwcMin)
            return S_OK;
    }

    /* The documentation for SysReAllocStringLen hints about it being allergic
       to NULL in some way or another, so we call SysAllocStringLen directly
       when appropriate: */
    if (m_bstr)
        AssertReturn(::SysReAllocStringLen(&m_bstr, NULL, (unsigned int)cwcMin) != FALSE, E_OUTOFMEMORY);
    else if (cwcMin > 0)
    {
        m_bstr = ::SysAllocStringLen(NULL, (unsigned int)cwcMin);
        AssertReturn(m_bstr, E_OUTOFMEMORY);
    }

    return S_OK;
}


void Bstr::reserve(size_t cwcMin, bool fForce /*= false*/)
{
    HRESULT hrc = reserveNoThrow(cwcMin, fForce);
#ifdef RT_EXCEPTIONS_ENABLED
    if (hrc != S_OK)
        throw std::bad_alloc();
#else
    Assert(hrc == S_OK); RT_NOREF(hrc);
#endif
}


Bstr &Bstr::append(const Bstr &rThat)
{
    if (rThat.isNotEmpty())
        return appendWorkerUtf16(rThat.m_bstr, rThat.length());
    return *this;
}


HRESULT Bstr::appendNoThrow(const Bstr &rThat) RT_NOEXCEPT
{
    if (rThat.isNotEmpty())
        return appendWorkerUtf16NoThrow(rThat.m_bstr, rThat.length());
    return S_OK;
}


Bstr &Bstr::append(const RTCString &rThat)
{
    if (rThat.isNotEmpty())
        return appendWorkerUtf8(rThat.c_str(), rThat.length());
    return *this;
}


HRESULT Bstr::appendNoThrow(const RTCString &rThat) RT_NOEXCEPT
{
    if (rThat.isNotEmpty())
        return appendWorkerUtf8NoThrow(rThat.c_str(), rThat.length());
    return S_OK;
}


Bstr &Bstr::append(CBSTR pwszSrc)
{
    if (pwszSrc && *pwszSrc)
        return appendWorkerUtf16(pwszSrc, RTUtf16Len(pwszSrc));
    return *this;
}


HRESULT Bstr::appendNoThrow(CBSTR pwszSrc) RT_NOEXCEPT
{
    if (pwszSrc && *pwszSrc)
        return appendWorkerUtf16NoThrow(pwszSrc, RTUtf16Len(pwszSrc));
    return S_OK;
}


Bstr &Bstr::append(const char *pszSrc)
{
    if (pszSrc && *pszSrc)
        return appendWorkerUtf8(pszSrc, strlen(pszSrc));
    return *this;
}


HRESULT Bstr::appendNoThrow(const char *pszSrc) RT_NOEXCEPT
{
    if (pszSrc && *pszSrc)
        return appendWorkerUtf8NoThrow(pszSrc, strlen(pszSrc));
    return S_OK;
}


Bstr &Bstr::append(const Bstr &rThat, size_t offStart, size_t cwcMax /*= RTSTR_MAX*/)
{
    size_t cwcSrc = rThat.length();
    if (offStart < cwcSrc)
        return appendWorkerUtf16(rThat.raw() + offStart, RT_MIN(cwcSrc - offStart, cwcMax));
    return *this;
}


HRESULT Bstr::appendNoThrow(const Bstr &rThat, size_t offStart, size_t cwcMax /*= RTSTR_MAX*/) RT_NOEXCEPT
{
    size_t cwcSrc = rThat.length();
    if (offStart < cwcSrc)
        return appendWorkerUtf16NoThrow(rThat.raw() + offStart, RT_MIN(cwcSrc - offStart, cwcMax));
    return S_OK;
}


Bstr &Bstr::append(const RTCString &rThat, size_t offStart, size_t cchMax /*= RTSTR_MAX*/)
{
    if (offStart < rThat.length())
        return appendWorkerUtf8(rThat.c_str() + offStart, RT_MIN(rThat.length() - offStart, cchMax));
    return *this;
}


HRESULT Bstr::appendNoThrow(const RTCString &rThat, size_t offStart, size_t cchMax /*= RTSTR_MAX*/) RT_NOEXCEPT
{
    if (offStart < rThat.length())
        return appendWorkerUtf8NoThrow(rThat.c_str() + offStart, RT_MIN(rThat.length() - offStart, cchMax));
    return S_OK;
}


Bstr &Bstr::append(CBSTR pwszThat, size_t cchMax)
{
    return appendWorkerUtf16(pwszThat, RTUtf16NLen(pwszThat, cchMax));
}


HRESULT Bstr::appendNoThrow(CBSTR pwszThat, size_t cchMax) RT_NOEXCEPT
{
    return appendWorkerUtf16NoThrow(pwszThat, RTUtf16NLen(pwszThat, cchMax));
}


Bstr &Bstr::append(const char *pszThat, size_t cchMax)
{
    return appendWorkerUtf8(pszThat, RTStrNLen(pszThat, cchMax));
}


HRESULT Bstr::appendNoThrow(const char *pszThat, size_t cchMax) RT_NOEXCEPT
{
    return appendWorkerUtf8NoThrow(pszThat, RTStrNLen(pszThat, cchMax));
}


Bstr &Bstr::append(char ch)
{
    AssertMsg(ch > 0 && ch < 127, ("%#x\n", ch));
    return appendWorkerUtf8(&ch, 1);
}


HRESULT Bstr::appendNoThrow(char ch) RT_NOEXCEPT
{
    AssertMsg(ch > 0 && ch < 127, ("%#x\n", ch));
    return appendWorkerUtf8NoThrow(&ch, 1);
}


Bstr &Bstr::appendCodePoint(RTUNICP uc)
{
    RTUTF16 wszTmp[3];
    PRTUTF16 pwszEnd = RTUtf16PutCp(wszTmp, uc);
    *pwszEnd = '\0';
    return appendWorkerUtf16(&wszTmp[0], pwszEnd - &wszTmp[0]);
}


HRESULT Bstr::appendCodePointNoThrow(RTUNICP uc) RT_NOEXCEPT
{
    RTUTF16 wszTmp[3];
    PRTUTF16 pwszEnd = RTUtf16PutCp(wszTmp, uc);
    *pwszEnd = '\0';
    return appendWorkerUtf16NoThrow(&wszTmp[0], pwszEnd - &wszTmp[0]);
}


Bstr &Bstr::appendWorkerUtf16(PCRTUTF16 pwszSrc, size_t cwcSrc)
{
    size_t cwcOld = length();
    size_t cwcTotal = cwcOld + cwcSrc;
    reserve(cwcTotal, true /*fForce*/);
    if (cwcSrc)
        memcpy(&m_bstr[cwcOld], pwszSrc, cwcSrc * sizeof(RTUTF16));
    m_bstr[cwcTotal] = '\0';
    return *this;
}


HRESULT Bstr::appendWorkerUtf16NoThrow(PCRTUTF16 pwszSrc, size_t cwcSrc) RT_NOEXCEPT
{
    size_t cwcOld = length();
    size_t cwcTotal = cwcOld + cwcSrc;
    HRESULT hrc = reserveNoThrow(cwcTotal, true /*fForce*/);
    if (hrc == S_OK)
    {
        if (cwcSrc)
            memcpy(&m_bstr[cwcOld], pwszSrc, cwcSrc * sizeof(RTUTF16));
        m_bstr[cwcTotal] = '\0';
    }
    return hrc;
}


Bstr &Bstr::appendWorkerUtf8(const char *pszSrc, size_t cchSrc)
{
    size_t cwcSrc;
    int vrc = RTStrCalcUtf16LenEx(pszSrc, cchSrc, &cwcSrc);
#ifdef RT_EXCEPTIONS_ENABLED
    AssertRCStmt(vrc, throw std::bad_alloc());
#else
    AssertRCReturn(vrc, *this);
#endif

    size_t cwcOld = length();
    size_t cwcTotal = cwcOld + cwcSrc;
    reserve(cwcTotal, true /*fForce*/);
    if (cwcSrc)
    {
        PRTUTF16 pwszDst = &m_bstr[cwcOld];
        vrc = RTStrToUtf16Ex(pszSrc, cchSrc, &pwszDst, cwcSrc + 1, NULL);
#ifdef RT_EXCEPTIONS_ENABLED
        AssertRCStmt(vrc, throw std::bad_alloc());
#else
        AssertRC(vrc);
#endif
    }
    m_bstr[cwcTotal] = '\0';
    return *this;
}


HRESULT Bstr::appendWorkerUtf8NoThrow(const char *pszSrc, size_t cchSrc) RT_NOEXCEPT
{
    size_t cwcSrc;
    int vrc = RTStrCalcUtf16LenEx(pszSrc, cchSrc, &cwcSrc);
    AssertRCReturn(vrc, E_INVALIDARG);

    size_t cwcOld = length();
    size_t cwcTotal = cwcOld + cwcSrc;
    HRESULT hrc = reserveNoThrow(cwcTotal, true /*fForce*/);
    AssertReturn(hrc == S_OK, hrc);
    if (cwcSrc)
    {
        PRTUTF16 pwszDst = &m_bstr[cwcOld];
        vrc = RTStrToUtf16Ex(pszSrc, cchSrc, &pwszDst, cwcSrc + 1, NULL);
        AssertRCReturn(vrc, E_INVALIDARG);
    }
    m_bstr[cwcTotal] = '\0';
    return S_OK;
}


Bstr &Bstr::appendPrintf(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    HRESULT hrc = appendPrintfVNoThrow(pszFormat, va);
    va_end(va);
#ifdef RT_EXCEPTIONS_ENABLED
    if (hrc != S_OK)
        throw std::bad_alloc();
#else
    Assert(hrc == S_OK); RT_NOREF(hrc);
#endif
    return *this;
}


HRESULT Bstr::appendPrintfNoThrow(const char *pszFormat, ...) RT_NOEXCEPT
{
    va_list va;
    va_start(va, pszFormat);
    HRESULT hrc = appendPrintfVNoThrow(pszFormat, va);
    va_end(va);
    return hrc;
}


Bstr &Bstr::appendPrintfV(const char *pszFormat, va_list va)
{
    HRESULT hrc = appendPrintfVNoThrow(pszFormat, va);
#ifdef RT_EXCEPTIONS_ENABLED
    if (hrc != S_OK)
        throw std::bad_alloc();
#else
    Assert(hrc == S_OK); RT_NOREF(hrc);
#endif
    return *this;
}


HRESULT Bstr::appendPrintfVNoThrow(const char *pszFormat, va_list va) RT_NOEXCEPT
{
    size_t const cwcOld = length();
    BSTRNOTHROW Args = { this, cwcOld, cwcOld, S_OK };

    RTStrFormatV(printfOutputCallbackNoThrow, &Args, NULL, NULL, pszFormat, va);
    if (Args.hrc == S_OK)
    {
        Args.hrc = joltNoThrow(Args.offDst);
        if (Args.hrc == S_OK)
            return S_OK;
    }

    if (m_bstr)
        m_bstr[cwcOld] = '\0';
    return Args.hrc;
}


Bstr &Bstr::erase(size_t offStart /*= 0*/, size_t cwcLength /*= RTSTR_MAX*/) RT_NOEXCEPT
{
    size_t cwc = length();
    if (offStart < cwc)
    {
        if (cwcLength >= cwc - offStart)
        {
            if (!offStart)
                cleanup();
            else
            {
                /* Trail removal, nothing to move.  */
                m_bstr[offStart] = '\0';
                joltNoThrow(offStart); /* not entirely optimal... */
            }
        }
        else if (cwcLength > 0)
        {
            /* Pull up the tail to offStart. */
            size_t cwcAfter = cwc - offStart - cwcLength;
            memmove(&m_bstr[offStart], &m_bstr[offStart + cwcLength], cwcAfter * sizeof(*m_bstr));
            cwc -= cwcLength;
            m_bstr[cwc] = '\0';
            joltNoThrow(cwc); /* not entirely optimal... */
        }
    }
    return *this;
}


void Bstr::cleanup()
{
    if (m_bstr)
    {
        ::SysFreeString(m_bstr);
        m_bstr = NULL;
    }
}


void Bstr::copyFrom(const OLECHAR *a_bstrSrc)
{
    if (a_bstrSrc && *a_bstrSrc)
    {
        m_bstr = ::SysAllocString(a_bstrSrc);
#ifdef RT_EXCEPTIONS_ENABLED
        if (RT_LIKELY(m_bstr))
        { /* likely */ }
        else
            throw std::bad_alloc();
#else
        Assert(m_bstr);
#endif
    }
    else
        m_bstr = NULL;
}


void Bstr::cleanupAndCopyFrom(const OLECHAR *a_bstrSrc)
{
    cleanup();
    copyFrom(a_bstrSrc);
}


HRESULT Bstr::cleanupAndCopyFromEx(const OLECHAR *a_bstrSrc) RT_NOEXCEPT
{
    cleanup();

    if (a_bstrSrc && *a_bstrSrc)
    {
        m_bstr = ::SysAllocString(a_bstrSrc);
        if (RT_LIKELY(m_bstr))
        { /* likely */ }
        else
            return E_OUTOFMEMORY;
    }
    else
        m_bstr = NULL;
    return S_OK;
}



/*********************************************************************************************************************************
*   Utf8Str Implementation                                                                                                       *
*********************************************************************************************************************************/

/* static */
const Utf8Str Utf8Str::Empty; /* default ctor is OK */

#if defined(VBOX_WITH_XPCOM)
void Utf8Str::cloneTo(char **pstr) const
{
    size_t cb = length() + 1;
    *pstr = (char *)nsMemory::Alloc(cb);
    if (RT_LIKELY(*pstr))
        memcpy(*pstr, c_str(), cb);
    else
#ifdef RT_EXCEPTIONS_ENABLED
        throw std::bad_alloc();
#else
        AssertFailed();
#endif
}

HRESULT Utf8Str::cloneToEx(char **pstr) const
{
    size_t cb = length() + 1;
    *pstr = (char *)nsMemory::Alloc(cb);
    if (RT_LIKELY(*pstr))
    {
        memcpy(*pstr, c_str(), cb);
        return S_OK;
    }
    return E_OUTOFMEMORY;
}
#endif

HRESULT Utf8Str::cloneToEx(BSTR *pbstr) const RT_NOEXCEPT
{
    if (!pbstr)
        return S_OK;
    Bstr bstr;
    HRESULT hrc = bstr.assignEx(*this);
    if (SUCCEEDED(hrc))
        hrc = bstr.detachToEx(pbstr);
    return hrc;
}

Utf8Str& Utf8Str::stripTrailingSlash()
{
    if (length())
    {
        ::RTPathStripTrailingSlash(m_psz);
        jolt();
    }
    return *this;
}

Utf8Str& Utf8Str::stripFilename()
{
    if (length())
    {
        RTPathStripFilename(m_psz);
        jolt();
    }
    return *this;
}

Utf8Str& Utf8Str::stripPath()
{
    if (length())
    {
        char *pszName = ::RTPathFilename(m_psz);
        if (pszName)
        {
            size_t cchName = length() - (pszName - m_psz);
            memmove(m_psz, pszName, cchName + 1);
            jolt();
        }
        else
            cleanup();
    }
    return *this;
}

Utf8Str& Utf8Str::stripSuffix()
{
    if (length())
    {
        RTPathStripSuffix(m_psz);
        jolt();
    }
    return *this;
}

size_t Utf8Str::parseKeyValue(Utf8Str &a_rKey, Utf8Str &a_rValue, size_t a_offStart /* = 0*/,
                              const Utf8Str &a_rPairSeparator /*= ","*/, const Utf8Str &a_rKeyValueSeparator /*= "="*/) const
{
    /* Find the end of the next pair, skipping empty pairs.
       Note! The skipping allows us to pass the return value of a parseKeyValue()
             call as offStart to the next call. */
    size_t offEnd;
    while (   a_offStart == (offEnd = find(&a_rPairSeparator, a_offStart))
           && offEnd != npos)
        a_offStart++;

    /* Look for a key/value separator before the end of the pair.
       ASSUMES npos value returned by find when the substring is not found is
       really high. */
    size_t offKeyValueSep = find(&a_rKeyValueSeparator, a_offStart);
    if (offKeyValueSep < offEnd)
    {
        a_rKey = substr(a_offStart, offKeyValueSep - a_offStart);
        if (offEnd == npos)
            offEnd = m_cch; /* No confusing npos when returning strings. */
        a_rValue = substr(offKeyValueSep + 1, offEnd - offKeyValueSep - 1);
    }
    else
    {
        a_rKey.setNull();
        a_rValue.setNull();
    }

    return offEnd;
}

/**
 * Internal function used in Utf8Str copy constructors and assignment when
 * copying from a UTF-16 string.
 *
 * As with the RTCString::copyFrom() variants, this unconditionally sets the
 * members to a copy of the given other strings and makes no assumptions about
 * previous contents.  This can therefore be used both in copy constructors,
 * when member variables have no defined value, and in assignments after having
 * called cleanup().
 *
 * This variant converts from a UTF-16 string, most probably from
 * a Bstr assignment.
 *
 * @param   a_pbstr         The source string.  The caller guarantees that this
 *                          is valid UTF-16.
 * @param   a_cwcMax        The number of characters to be copied. If set to RTSTR_MAX,
 *                          the entire string will be copied.
 *
 * @sa      RTCString::copyFromN
 */
void Utf8Str::copyFrom(CBSTR a_pbstr, size_t a_cwcMax)
{
    if (a_pbstr && *a_pbstr)
    {
        int vrc = RTUtf16ToUtf8Ex((PCRTUTF16)a_pbstr,
                                  a_cwcMax,        // size_t cwcString: translate entire string
                                  &m_psz,           // char **ppsz: output buffer
                                  0,                // size_t cch: if 0, func allocates buffer in *ppsz
                                  &m_cch);          // size_t *pcch: receives the size of the output string, excluding the terminator.
        if (RT_SUCCESS(vrc))
            m_cbAllocated = m_cch + 1;
        else
        {
            if (   vrc != VERR_NO_STR_MEMORY
                && vrc != VERR_NO_MEMORY)
            {
                /* ASSUME: input is valid Utf-16. Fake out of memory error. */
                AssertLogRelMsgFailed(("%Rrc %.*Rhxs\n", vrc, RTUtf16Len(a_pbstr) * sizeof(RTUTF16), a_pbstr));
            }

            m_cch = 0;
            m_cbAllocated = 0;
            m_psz = NULL;

#ifdef RT_EXCEPTIONS_ENABLED
            throw std::bad_alloc();
#else
            AssertFailed();
#endif
        }
    }
    else
    {
        m_cch = 0;
        m_cbAllocated = 0;
        m_psz = NULL;
    }
}

/**
 * A variant of Utf8Str::copyFrom that does not throw any exceptions but returns
 * E_OUTOFMEMORY instead.
 *
 * @param   a_pbstr         The source string.
 * @returns S_OK or E_OUTOFMEMORY.
 */
HRESULT Utf8Str::copyFromEx(CBSTR a_pbstr)
{
    if (a_pbstr && *a_pbstr)
    {
        int vrc = RTUtf16ToUtf8Ex((PCRTUTF16)a_pbstr,
                                  RTSTR_MAX,        // size_t cwcString: translate entire string
                                  &m_psz,           // char **ppsz: output buffer
                                  0,                // size_t cch: if 0, func allocates buffer in *ppsz
                                  &m_cch);          // size_t *pcch: receives the size of the output string, excluding the terminator.
        if (RT_SUCCESS(vrc))
            m_cbAllocated = m_cch + 1;
        else
        {
            if (   vrc != VERR_NO_STR_MEMORY
                && vrc != VERR_NO_MEMORY)
            {
                /* ASSUME: input is valid Utf-16. Fake out of memory error. */
                AssertLogRelMsgFailed(("%Rrc %.*Rhxs\n", vrc, RTUtf16Len(a_pbstr) * sizeof(RTUTF16), a_pbstr));
            }

            m_cch = 0;
            m_cbAllocated = 0;
            m_psz = NULL;

            return E_OUTOFMEMORY;
        }
    }
    else
    {
        m_cch = 0;
        m_cbAllocated = 0;
        m_psz = NULL;
    }
    return S_OK;
}


/**
 * A variant of Utf8Str::copyFromN that does not throw any exceptions but
 * returns E_OUTOFMEMORY instead.
 *
 * @param   a_pcszSrc   The source string.
 * @param   a_offSrc    Start offset to copy from.
 * @param   a_cchSrc    How much to copy
 * @returns S_OK or E_OUTOFMEMORY.
 *
 * @remarks This calls cleanup() first, so the caller doesn't have to. (Saves
 *          code space.)
 */
HRESULT Utf8Str::copyFromExNComRC(const char *a_pcszSrc, size_t a_offSrc, size_t a_cchSrc)
{
    Assert(!a_cchSrc || !m_psz || (uintptr_t)&a_pcszSrc[a_offSrc] - (uintptr_t)m_psz >= (uintptr_t)m_cbAllocated);
    cleanup();
    if (a_cchSrc)
    {
        m_psz = RTStrAlloc(a_cchSrc + 1);
        if (RT_LIKELY(m_psz))
        {
            m_cch = a_cchSrc;
            m_cbAllocated = a_cchSrc + 1;
            memcpy(m_psz, a_pcszSrc + a_offSrc, a_cchSrc);
            m_psz[a_cchSrc] = '\0';
        }
        else
        {
            m_cch = 0;
            m_cbAllocated = 0;
            return E_OUTOFMEMORY;
        }
    }
    else
    {
        m_cch = 0;
        m_cbAllocated = 0;
        m_psz = NULL;
    }
    return S_OK;
}

} /* namespace com */
