/* $Id: QMTranslatorImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox API translation handling class
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#include <vector>
#include <set>
#include <algorithm>
#include <iprt/sanitized/iterator>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/asm.h>
#include <iprt/string.h>
#include <iprt/strcache.h>
#include <VBox/com/string.h>
#include <VBox/log.h>
#include <QMTranslator.h>

/* QM File Magic Number */
static const size_t g_cbMagic = 16;
static const uint8_t g_abMagic[g_cbMagic] =
{
    0x3c, 0xb8, 0x64, 0x18, 0xca, 0xef, 0x9c, 0x95,
    0xcd, 0x21, 0x1c, 0xbf, 0x60, 0xa1, 0xbd, 0xdd
};

/* Used internally */
class QMException : public std::exception
{
    const char *m_str;
public:
    QMException(const char *str) : m_str(str) {}
    virtual const char *what() const throw() { return m_str; }
};

/* Bytes stream. Used by the parser to iterate through the data */
class QMBytesStream
{
    size_t         m_cbSize;
    const uint8_t * const m_dataStart;
    const uint8_t *m_iter;
    const uint8_t *m_end;

public:

    QMBytesStream(const uint8_t *const dataStart, size_t cbSize)
        : m_cbSize(dataStart ? cbSize : 0)
        , m_dataStart(dataStart)
        , m_iter(dataStart)
    {
        setEnd();
    }

    /** Sets end pointer.
     * Used in message reader to detect the end of message block */
    inline void setEnd(size_t pos = 0)
    {
        m_end = m_dataStart + (pos && pos < m_cbSize ? pos : m_cbSize);
    }

    inline uint8_t read8()
    {
        checkSize(1);
        return *m_iter++;
    }

    inline uint32_t read32()
    {
        checkSize(4);
        uint32_t result = *reinterpret_cast<const uint32_t *>(m_iter);
        m_iter += 4;
        return RT_BE2H_U32(result);
    }

    /** Reads string in UTF16 and converts it into a UTF8 string */
    inline com::Utf8Str readUtf16String()
    {
        uint32_t size = read32();
        checkSize(size);
        if (size & 1)
            throw QMException("Incorrect string size");

        /* UTF-16 can encode up to codepoint U+10ffff, which UTF-8 needs 4 bytes
           to encode, so reserve twice the size plus a terminator for the result. */
        com::Utf8Str result;
        result.reserve(size * 2 + 1);
        char *pszStr = result.mutableRaw();
        int vrc = RTUtf16BigToUtf8Ex((PCRTUTF16)m_iter, size >> 1, &pszStr, result.capacity(), NULL);
        if (RT_SUCCESS(vrc))
            result.jolt();
        else
            throw QMException("Translation from UTF-16 to UTF-8 failed");

        m_iter += size;
        return result;
    }

    /**
     *  Reads a string, forcing UTF-8 encoding.
     */
    inline com::Utf8Str readString()
    {
        uint32_t size = read32();
        checkSize(size);

        com::Utf8Str result(reinterpret_cast<const char *>(m_iter), size);
        if (size > 0)
        {
            RTStrPurgeEncoding(result.mutableRaw());
            result.jolt();
        }

        m_iter += size;
        return result;
    }

    /**
     *  Reads memory block
     *  Returns number of bytes read
     */
    inline uint32_t read(char *bBuf, uint32_t cbSize)
    {
        if (!bBuf || !cbSize)
            return 0;
        cbSize = RT_MIN(cbSize, (uint32_t)(m_end - m_iter));
        memcpy(bBuf, m_iter, cbSize);
        m_iter += cbSize;
        return cbSize;
    }

    /** Checks the magic number.
     * Should be called when in the beginning of the data
     * @throws exception on mismatch  */
    inline void checkMagic()
    {
        checkSize(g_cbMagic);
        if (RT_LIKELY(memcmp(&(*m_iter), g_abMagic, g_cbMagic) == 0))
            m_iter += g_cbMagic;
        else
            throw QMException("Wrong magic number");
    }

    /** Has we reached the end pointer? */
    inline bool hasFinished()
    {
        return m_iter == m_end;
    }

    /** Returns current stream position */
    inline size_t tellPos()
    {
        return (size_t)(m_iter - m_dataStart);
    }

    /** Moves current pointer to a desired position */
    inline void seek(uint32_t offSkip)
    {
        size_t cbLeft = (size_t)(m_end - m_iter);
        if (cbLeft >= offSkip)
            m_iter += offSkip;
        else
            m_iter = m_end; /** @todo r=bird: Or throw exception via checkSize? */
    }

    /** Checks whether stream has enough data to read size bytes */
    inline void checkSize(size_t size)
    {
        if (RT_LIKELY((size_t)(m_end - m_iter) >= size))
            return;
        throw QMException("Incorrect item size");
    }
};

/* Internal QMTranslator implementation */
class QMTranslator_Impl
{
    /** Used while parsing */
    struct QMMessageParse
    {
        /* Everything is in UTF-8 */
        std::vector<com::Utf8Str> astrTranslations;
        com::Utf8Str strContext;
        com::Utf8Str strComment;
        com::Utf8Str strSource;

        QMMessageParse() {}
    };

    struct QMMessage
    {
        const char *pszContext;
        const char *pszSource;
        const char *pszComment;
        std::vector<const char *> vecTranslations;
        uint32_t    hash;

        QMMessage() : pszContext(NULL), pszSource(NULL), pszComment(NULL), hash(0)
        {}

        QMMessage(RTSTRCACHE hStrCache, const QMMessageParse &rSrc)
            : pszContext(addStr(hStrCache, rSrc.strContext))
            , pszSource(addStr(hStrCache, rSrc.strSource))
            , pszComment(addStr(hStrCache, rSrc.strComment))
            , hash(RTStrHash1(pszSource))
        {
            for (size_t i = 0; i < rSrc.astrTranslations.size(); i++)
                vecTranslations.push_back(addStr(hStrCache, rSrc.astrTranslations[i]));
        }

        /** Helper. */
        static const char *addStr(RTSTRCACHE hStrCache, const com::Utf8Str &rSrc)
        {
            if (rSrc.isNotEmpty())
            {
                const char *psz = RTStrCacheEnterN(hStrCache, rSrc.c_str(), rSrc.length());
                if (RT_LIKELY(psz))
                    return psz;
                throw std::bad_alloc();
            }
            return NULL;
        }

    };

    struct HashOffset
    {
        uint32_t hash;
        uint32_t offset;

        HashOffset(uint32_t a_hash = 0, uint32_t a_offs = 0) : hash(a_hash), offset(a_offs) {}

        bool operator<(const HashOffset &obj) const
        {
            return (hash != obj.hash ? hash < obj.hash : offset < obj.offset);
        }

    };

    typedef std::set<HashOffset> QMHashSet;
    typedef QMHashSet::const_iterator QMHashSetConstIter;
    typedef std::vector<QMMessage> QMMessageArray;
    typedef std::vector<uint8_t> QMByteArray;

    QMHashSet      m_hashSet;
    QMMessageArray m_messageArray;
    QMByteArray    m_pluralRules;

public:

    QMTranslator_Impl() {}

    enum PluralOpCodes
    {
        Pl_Eq          = 0x01,
        Pl_Lt          = 0x02,
        Pl_Leq         = 0x03,
        Pl_Between     = 0x04,

        Pl_OpMask      = 0x07,

        Pl_Not         = 0x08,
        Pl_Mod10       = 0x10,
        Pl_Mod100      = 0x20,
        Pl_Lead1000    = 0x40,

        Pl_And         = 0xFD,
        Pl_Or          = 0xFE,
        Pl_NewRule     = 0xFF,

        Pl_LMask       = 0x80,
    };

    /*
     * Rules format:
     * <O><2>[<3>][<&&><O><2>[<3>]]...[<||><O><2>[<3>][<&&><O><2>[<3>]]...]...[<New><O>...]...
     * where:
     *    <O> - OpCode
     *    <2> - Second operand
     *    <3> - Third operand
     *    <&&> - 'And' operation
     *    <||> - 'Or' operation
     *    <New> - Start of rule for next plural form
     * Rules are ordered by plural forms, i.e:
     *   <rule for first form (i.e. single)><New><rule for next form>...
     */
    bool checkPlural(const QMByteArray &aRules) const
    {
        if (aRules.empty())
            return true;

        uint32_t iPos = 0;
        do {
            uint8_t bOpCode = aRules[iPos];

            /* Invalid place of And/Or/NewRule */
            if (bOpCode & Pl_LMask)
                return false;

            /* 2nd operand */
            iPos++;

            /* 2nd operand missing */
            if (iPos == aRules.size())
                return false;

            /* Invalid OpCode */
            if ((bOpCode & Pl_OpMask) == 0)
                return false;

            if ((bOpCode & Pl_OpMask) == Pl_Between)
            {
                /* 3rd operand */
                iPos++;

                /* 3rd operand missing */
                if (iPos == aRules.size())
                    return false;
            }

            /* And/Or/NewRule */
            iPos++;

            /* All rules checked */
            if (iPos == aRules.size())
                return true;

        } while (   (   (aRules[iPos] == Pl_And)
                     || (aRules[iPos] == Pl_Or)
                     || (aRules[iPos] == Pl_NewRule))
                 && ++iPos != aRules.size());

        return false;
    }

    size_t plural(size_t aNum) const
    {
        if (aNum == ~(size_t)0 || m_pluralRules.empty())
            return 0;

        size_t   uPluralNumber = 0;
        uint32_t iPos = 0;

        /* Rules loop */
        for (;;)
        {
            bool fOr = false;
            /* 'Or' loop */
            for (;;)
            {
                bool fAnd = true;
                /* 'And' loop */
                for (;;)
                {
                    int    iOpCode = m_pluralRules[iPos++];
                    size_t iOpLeft = aNum;
                    if (iOpCode & Pl_Mod10)
                        iOpLeft %= 10;
                    else if (iOpCode & Pl_Mod100)
                        iOpLeft %= 100;
                    else if (iOpCode & Pl_Lead1000)
                    {
                        while (iOpLeft >= 1000)
                            iOpLeft /= 1000;
                    }
                    size_t iOpRight = m_pluralRules[iPos++];
                    int    iOp = iOpCode & Pl_OpMask;
                    size_t iOpRight1 = 0;
                    if (iOp == Pl_Between)
                        iOpRight1 = m_pluralRules[iPos++];

                    bool fResult =    (iOp == Pl_Eq      && iOpLeft == iOpRight)
                                   || (iOp == Pl_Lt      && iOpLeft <  iOpRight)
                                   || (iOp == Pl_Leq     && iOpLeft <= iOpRight)
                                   || (iOp == Pl_Between && iOpLeft >= iOpRight && iOpLeft <= iOpRight1);
                    if (iOpCode & Pl_Not)
                        fResult = !fResult;

                    fAnd = fAnd && fResult;
                    if (iPos == m_pluralRules.size() || m_pluralRules[iPos] != Pl_And)
                        break;
                    iPos++;
                }
                fOr = fOr || fAnd;
                if (iPos == m_pluralRules.size() || m_pluralRules[iPos] != Pl_Or)
                    break;
                iPos++;
            }
            if (fOr)
                return uPluralNumber;

            /* Qt returns last plural number if none of rules are match. */
            uPluralNumber++;

            if (iPos >= m_pluralRules.size())
                return uPluralNumber;

            iPos++; // Skip Pl_NewRule
        }
    }

    const char *translate(const char  *pszContext,
                          const char  *pszSource,
                          const char  *pszDisamb,
                          const size_t aNum,
                          const char **ppszSafeSource) const RT_NOEXCEPT
    {
        QMHashSetConstIter lowerIter, upperIter;

        /* As turned out, comments (pszDisamb) are not kept always in result qm file
         * Therefore, exclude them from the hash */
        uint32_t hash = RTStrHash1(pszSource);
        lowerIter = m_hashSet.lower_bound(HashOffset(hash, 0));
        upperIter = m_hashSet.upper_bound(HashOffset(hash, UINT32_MAX));

        /*
         * Check different combinations with and without context and
         * disambiguation. This can help us to find the translation even
         * if context or disambiguation are not know or properly defined.
         */
        const char *apszCtx[]    = {pszContext, pszContext, NULL,      NULL};
        const char *apszDisabm[] = {pszDisamb,  NULL,       pszDisamb, NULL};
        AssertCompile(RT_ELEMENTS(apszCtx) == RT_ELEMENTS(apszDisabm));

        for (size_t i = 0; i < RT_ELEMENTS(apszCtx); ++i)
        {
            for (QMHashSetConstIter iter = lowerIter; iter != upperIter; ++iter)
            {
                const QMMessage &message = m_messageArray[iter->offset];
                if (   RTStrCmp(message.pszSource, pszSource) == 0
                    && (!apszCtx[i]     || !*apszCtx[i]     || RTStrCmp(message.pszContext, apszCtx[i]) == 0)
                    && (!apszDisabm[i]  || !*apszDisabm[i]  || RTStrCmp(message.pszComment, apszDisabm[i]) == 0 ))
                {
                    *ppszSafeSource = message.pszSource;
                    const std::vector<const char *> &vecTranslations = m_messageArray[iter->offset].vecTranslations;
                    size_t const idxPlural = plural(aNum);
                    return vecTranslations[RT_MIN(idxPlural, vecTranslations.size() - 1)];
                }
            }
        }

        *ppszSafeSource = NULL;
        return pszSource;
    }

    void load(QMBytesStream &stream, RTSTRCACHE hStrCache)
    {
        /* Load into local variables. If we failed during the load,
         * it would allow us to keep the object in a valid (previous) state. */
        QMHashSet hashSet;
        QMMessageArray messageArray;
        QMByteArray pluralRules;

        stream.checkMagic();

        while (!stream.hasFinished())
        {
            uint32_t sectionCode = stream.read8();
            uint32_t sLen = stream.read32();

            /* Hashes and Context sections are ignored. They contain hash tables
             * to speed-up search which is not useful since we recalculate all hashes
             * and don't perform context search by hash */
            switch (sectionCode)
            {
                case Messages:
                    parseMessages(stream, hStrCache, &hashSet, &messageArray, sLen);
                    break;
                case Hashes:
                    /* Only get size information to speed-up vector filling
                     * if Hashes section goes in the file before Message section */
                    if (messageArray.empty())
                        messageArray.reserve(sLen >> 3);
                    stream.seek(sLen);
                    break;
                case NumerusRules:
                {
                    pluralRules.resize(sLen);
                    uint32_t cbSize = stream.read((char *)&pluralRules[0], sLen);
                    if (cbSize < sLen)
                        throw QMException("Incorrect section size");
                    if (!checkPlural(pluralRules))
                        pluralRules.erase(pluralRules.begin(), pluralRules.end());
                    break;
                }
                case Contexts:
                case Dependencies:
                case Language:
                    stream.seek(sLen);
                    break;
                default:
                    throw QMException("Unkown section");
            }
        }

        /* Store the data into member variables.
         * The following functions never generate exceptions */
        m_hashSet.swap(hashSet);
        m_messageArray.swap(messageArray);
        m_pluralRules.swap(pluralRules);
    }

private:

    /* Some QM stuff */
    enum SectionType
    {
        Contexts     = 0x2f,
        Hashes       = 0x42,
        Messages     = 0x69,
        NumerusRules = 0x88,
        Dependencies = 0x96,
        Language     = 0xa7
    };

    enum MessageType
    {
        End          = 1,
        SourceText16 = 2,
        Translation  = 3,
        Context16    = 4,
        Obsolete1    = 5,  /**< was Hash */
        SourceText   = 6,
        Context      = 7,
        Comment      = 8
    };

    /* Read messages from the stream. */
    static void parseMessages(QMBytesStream &stream, RTSTRCACHE hStrCache, QMHashSet * const hashSet,
                              QMMessageArray * const messageArray, size_t cbSize)
    {
        stream.setEnd(stream.tellPos() + cbSize);
        uint32_t cMessage = 0;
        while (!stream.hasFinished())
        {
            /* Process the record. Skip anything that doesn't have a source
               string or any valid translations.  Using C++ strings for temporary
               storage here, as we don't want to pollute the cache we bogus strings
               in case of duplicate sub-records or invalid records. */
            QMMessageParse ParsedMsg;
            parseMessageRecord(stream, &ParsedMsg);
            if (   ParsedMsg.astrTranslations.size() > 0
                && ParsedMsg.strSource.isNotEmpty())
            {
                /* Copy the strings over into the string cache and a hashed QMMessage,
                   before adding it to the result. */
                QMMessage HashedMsg(hStrCache, ParsedMsg);
                hashSet->insert(HashOffset(HashedMsg.hash, cMessage++));
                messageArray->push_back(HashedMsg);

            }
            /*else: wtf? */
        }
        stream.setEnd();
    }

    /* Parse one message from the stream */
    static void parseMessageRecord(QMBytesStream &stream, QMMessageParse * const message)
    {
        while (!stream.hasFinished())
        {
            uint8_t type = stream.read8();
            switch (type)
            {
                case End:
                    return;
                /* Ignored as obsolete */
                case Context16:
                case SourceText16:
                    stream.seek(stream.read32());
                    break;
                case Translation:
                    message->astrTranslations.push_back(stream.readUtf16String());
                    break;

                case SourceText:
                    message->strSource = stream.readString();
                    break;

                case Context:
                    message->strContext = stream.readString();
                    break;

                case Comment:
                    message->strComment = stream.readString();
                    break;

                default:
                    /* Ignore unknown/obsolete block */
                    LogRel(("QMTranslator::parseMessageRecord(): Unknown/obsolete message block %x\n", type));
                    break;
            }
        }
    }
};

/* Inteface functions implementation */
QMTranslator::QMTranslator() : m_impl(new QMTranslator_Impl) {}

QMTranslator::~QMTranslator() { delete m_impl; }

const char *QMTranslator::translate(const char *pszContext, const char *pszSource, const char **ppszSafeSource,
                                    const char *pszDisamb /*= NULL*/, const size_t aNum /*= ~(size_t)0*/) const RT_NOEXCEPT

{
    return m_impl->translate(pszContext, pszSource, pszDisamb, aNum, ppszSafeSource);
}

int QMTranslator::load(const char *pszFilename, RTSTRCACHE hStrCache) RT_NOEXCEPT
{
    /* To free safely the file in case of exception */
    struct FileLoader
    {
        uint8_t *data;
        size_t cbSize;
        int vrc;
        FileLoader(const char *pszFname)
        {
            vrc = RTFileReadAll(pszFname, (void**) &data, &cbSize);
        }

        ~FileLoader()
        {
            if (isSuccess())
                RTFileReadAllFree(data, cbSize);
        }
        bool isSuccess() { return RT_SUCCESS(vrc); }
    };

    try
    {
        FileLoader loader(pszFilename);
        if (loader.isSuccess())
        {
            QMBytesStream stream(loader.data, loader.cbSize);
            m_impl->load(stream, hStrCache);
        }
        return loader.vrc;
    }
    catch(std::exception &e)
    {
        LogRel(("QMTranslator::load() failed to load file '%s', reason: %s\n", pszFilename, e.what()));
        return VERR_INTERNAL_ERROR;
    }
    catch(...)
    {
        LogRel(("QMTranslator::load() failed to load file '%s'\n", pszFilename));
        return VERR_GENERAL_FAILURE;
    }
}
