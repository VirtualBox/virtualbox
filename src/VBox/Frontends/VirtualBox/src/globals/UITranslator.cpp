/* $Id: UITranslator.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UITranslator class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QDir>
#include <QKeySequence>
#include <QRegularExpression>
#ifdef Q_OS_UNIX
# include <QLibraryInfo>
#endif

/* GUI includes: */
#include "UIConverter.h"
#include "UIMessageCenter.h"
#include "UITranslator.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
#endif

/* Other VBox includes: */
#include <iprt/assert.h>
#include <iprt/path.h>
#ifdef Q_OS_UNIX
# include <iprt/env.h>
#endif

/* External includes: */
#include <math.h>


/** Port config cache. */
struct PortConfig
{
    const char *name;
    const ulong IRQ;
    const ulong IOBase;
};

/** Known port config COM ports. */
static const PortConfig kComKnownPorts[] =
{
    { "COM1", 4, 0x3F8 },
    { "COM2", 3, 0x2F8 },
    { "COM3", 4, 0x3E8 },
    { "COM4", 3, 0x2E8 },
    /* Must not contain an element with IRQ=0 and IOBase=0 used to cause
     * toCOMPortName() to return the "User-defined" string for these values. */
};


/* static */
UITranslator *UITranslator::s_pTranslator = 0;
bool UITranslator::s_fTranslationInProgress = false;
QString UITranslator::s_strLoadedLanguageId = UITranslator::vboxBuiltInLanguageName();

/* static */
void UITranslator::loadLanguage(const QString &strLangId /* = QString() */)
{
    QString strEffectiveLangId = strLangId.isEmpty()
                               ? systemLanguageId()
                               : strLangId;
    QString strLanguageFileName;
    QString strSelectedLangId = vboxBuiltInLanguageName();

    /* If C is selected we change it temporary to en. This makes sure any extra
     * "en" translation file will be loaded. This is necessary for loading the
     * plural forms of some of our translations. */
    bool fResetToC = false;
    if (strEffectiveLangId == "C")
    {
        strEffectiveLangId = "en";
        fResetToC = true;
    }

    char szNlsPath[RTPATH_MAX];
    int rc;

    rc = RTPathAppPrivateNoArch(szNlsPath, sizeof(szNlsPath));
    AssertRC(rc);

    QString strNlsPath = QString(szNlsPath) + vboxLanguageSubDirectory();
    QDir nlsDir(strNlsPath);

    Assert(!strEffectiveLangId.isEmpty());
    if (!strEffectiveLangId.isEmpty() && strEffectiveLangId != vboxBuiltInLanguageName())
    {
        const QRegularExpression re(vboxLanguageIdRegExp());
        const QRegularExpressionMatch mt = re.match(strEffectiveLangId);
        /* The language ID should match the regexp completely: */
        AssertReturnVoid(mt.capturedStart() == 0);

        QString strStrippedLangId = mt.captured(2);

        if (nlsDir.exists(vboxLanguageFileBase() + strEffectiveLangId + vboxLanguageFileExtension()))
        {
            strLanguageFileName = nlsDir.absoluteFilePath(vboxLanguageFileBase() +
                                                          strEffectiveLangId +
                                                          vboxLanguageFileExtension());
            strSelectedLangId = strEffectiveLangId;
        }
        else if (nlsDir.exists(vboxLanguageFileBase() + strStrippedLangId + vboxLanguageFileExtension()))
        {
            strLanguageFileName = nlsDir.absoluteFilePath(vboxLanguageFileBase() +
                                                          strStrippedLangId +
                                                          vboxLanguageFileExtension());
            strSelectedLangId = strStrippedLangId;
        }
        else
        {
            /* Never complain when the default language is requested.  In any
             * case, if no explicit language file exists, we will simply
             * fall-back to English (built-in). */
            if (!strLangId.isNull() && strEffectiveLangId != "en")
                msgCenter().cannotFindLanguage(strEffectiveLangId, strNlsPath);
            /* strSelectedLangId remains built-in here: */
            AssertReturnVoid(strSelectedLangId == vboxBuiltInLanguageName());
        }
    }

    /* Lock listener: */
    s_fTranslationInProgress = true;
    /* A list of translators to install: */
    QList<QTranslator*> translators;

    /* Delete the old translator if there is one: */
    if (s_pTranslator)
    {
        /* QTranslator destructor will call qApp->removeTranslator() for
         * us. It will also delete all its child translations we attach to it
         * below, so we don't have to care about them specially. */
        delete s_pTranslator;
    }

    /* Load new language files: */
    s_pTranslator = new UITranslator(qApp);
    Assert(s_pTranslator);
    bool fLoadOk = true;
    if (s_pTranslator)
    {
        if (strSelectedLangId != vboxBuiltInLanguageName())
        {
            Assert(!strLanguageFileName.isNull());
            fLoadOk = s_pTranslator->loadFile(strLanguageFileName);
        }
        /* We install the translator in any case: on failure, this will
         * activate an empty translator that will give us English (built-in): */
        translators << s_pTranslator;
    }
    else
        fLoadOk = false;

    if (fLoadOk)
        s_strLoadedLanguageId = strSelectedLangId;
    else
    {
        msgCenter().cannotLoadLanguage(strLanguageFileName);
        s_strLoadedLanguageId = vboxBuiltInLanguageName();
    }

    /* Try to load the corresponding Qt translation: */
    if (languageId() != vboxBuiltInLanguageName() && languageId() != "en")
    {
#ifdef Q_OS_UNIX
        // We use system installations of Qt on Linux systems, so first, try
        // to load the Qt translation from the system location.
        strLanguageFileName = QLibraryInfo::path(QLibraryInfo::TranslationsPath) + "/qt_" +
                              languageId() + vboxLanguageFileExtension();
        QTranslator *pQtSysTr = new QTranslator(s_pTranslator);
        Assert(pQtSysTr);
        if (pQtSysTr && pQtSysTr->load(strLanguageFileName))
            translators << pQtSysTr;
        // Note that the Qt translation supplied by Oracle is always loaded
        // afterwards to make sure it will take precedence over the system
        // translation (it may contain more decent variants of translation
        // that better correspond to VirtualBox UI). We need to load both
        // because a newer version of Qt may be installed on the user computer
        // and the Oracle version may not fully support it. We don't do it on
        // Win32 because we supply a Qt library there and therefore the
        // Oracle translation is always the best one. */
#endif
        strLanguageFileName = nlsDir.absoluteFilePath(QString("qt_") +
                                                      languageId() +
                                                      vboxLanguageFileExtension());
        QTranslator *pQtTr = new QTranslator(s_pTranslator);
        Assert(pQtTr);
        if (pQtTr && (fLoadOk = pQtTr->load(strLanguageFileName)))
            translators << pQtTr;
        /* The below message doesn't fit 100% (because it's an additional
         * language and the main one won't be reset to built-in on failure)
         * but the load failure is so rare here that it's not worth a separate
         * message (but still, having something is better than having none) */
        if (!fLoadOk && !strLangId.isNull())
            msgCenter().cannotLoadLanguage(strLanguageFileName);
    }
    if (fResetToC)
        s_strLoadedLanguageId = vboxBuiltInLanguageName();
#ifdef VBOX_WS_MAC
    // Qt doesn't translate the items in the Application menu initially.
    // Manually trigger an update.
    ::darwinRetranslateAppMenu();
#endif

    /* Iterate through all the translators: */
    for (int i = 0; i < translators.size(); ++i)
    {
        /* Unlock listener before the last one translator: */
        if (i == translators.size() - 1)
        {
            QCoreApplication::sendPostedEvents(0, QEvent::LanguageChange);
            s_fTranslationInProgress = false;
        }

        /* Install current one: */
        qApp->installTranslator(translators.at(i));
    }

    /* Unlock listener in case if it's still locked: */
    s_fTranslationInProgress = false;
}

/* static */
QString UITranslator::vboxLanguageSubDirectory()
{
    return "/nls";
}

/* static */
QString UITranslator::vboxLanguageFileBase()
{
    return "VirtualBox_";
}

/* static */
QString UITranslator::vboxLanguageFileExtension()
{
    return ".qm";
}

/* static */
QString UITranslator::vboxLanguageIdRegExp()
{
    return "(([a-z]{2})(?:_([A-Z]{2}))?)|(C)";
}

/* static */
QString UITranslator::vboxBuiltInLanguageName()
{
    return "C";
}

/* static */
QString UITranslator::languageId()
{
    /* Note that it may not match with UIExtraDataManager::languageId() if the specified language cannot be loaded.
     *
     * If the built-in language is active, this method returns "C". "C" is treated as the built-in language for
     * simplicity -- the C locale is used in unix environments as a fallback when the requested locale is invalid.
     * This way we don't need to process both the "built_in" language and the "C" language (which is a valid
     * environment setting) separately. */

    return s_strLoadedLanguageId;
}

/* static */
QString UITranslator::yearsToString(uint32_t cVal)
{
    return QApplication::translate("UITranslator", "%n year(s)", "", cVal);
}

/* static */
QString UITranslator::monthsToString(uint32_t cVal)
{
    return QApplication::translate("UITranslator", "%n month(s)", "", cVal);
}

/* static */
QString UITranslator::daysToString(uint32_t cVal)
{
    return QApplication::translate("UITranslator", "%n day(s)", "", cVal);
}

/* static */
QString UITranslator::hoursToString(uint32_t cVal)
{
    return QApplication::translate("UITranslator", "%n hour(s)", "", cVal);
}

/* static */
QString UITranslator::minutesToString(uint32_t cVal)
{
    return QApplication::translate("UITranslator", "%n minute(s)", "", cVal);
}

/* static */
QString UITranslator::secondsToString(uint32_t cVal)
{
    return QApplication::translate("UITranslator", "%n second(s)", "", cVal);
}

/* static */
QString UITranslator::yearsToStringAgo(uint32_t cVal)
{
    return QApplication::translate("UITranslator", "%n year(s) ago", "", cVal);
}

/* static */
QString UITranslator::monthsToStringAgo(uint32_t cVal)
{
    return QApplication::translate("UITranslator", "%n month(s) ago", "", cVal);
}

/* static */
QString UITranslator::daysToStringAgo(uint32_t cVal)
{
    return QApplication::translate("UITranslator", "%n day(s) ago", "", cVal);
}

/* static */
QString UITranslator::hoursToStringAgo(uint32_t cVal)
{
    return QApplication::translate("UITranslator", "%n hour(s) ago", "", cVal);
}

/* static */
QString UITranslator::minutesToStringAgo(uint32_t cVal)
{
    return QApplication::translate("UITranslator", "%n minute(s) ago", "", cVal);
}

/* static */
QString UITranslator::secondsToStringAgo(uint32_t cVal)
{
    return QApplication::translate("UITranslator", "%n second(s) ago", "", cVal);
}

/* static */
QString UITranslator::decimalSep()
{
    return QString(QLocale::system().decimalPoint());
}

/* static */
QString UITranslator::sizeRegexp()
{
    /* This regexp will capture 5 groups of text:
     * - cap(1): integer number in case when no decimal point is present
     *           (if empty, it means that decimal point is present)
     * - cap(2): size suffix in case when no decimal point is present (may be empty)
     * - cap(3): integer number in case when decimal point is present (may be empty)
     * - cap(4): fraction number (hundredth) in case when decimal point is present
     * - cap(5): size suffix in case when decimal point is present (note that
     *           B cannot appear there). */

    const QString strRegexp =
        QString("^(?:(?:(\\d+)(?:\\s?(%2|%3|%4|%5|%6|%7))?)|(?:(\\d*)%1(\\d{1,2})(?:\\s?(%3|%4|%5|%6|%7))))$")
            .arg(decimalSep())
            .arg(tr("B", "size suffix Bytes"))
            .arg(tr("KB", "size suffix KBytes=1024 Bytes"))
            .arg(tr("MB", "size suffix MBytes=1024 KBytes"))
            .arg(tr("GB", "size suffix GBytes=1024 MBytes"))
            .arg(tr("TB", "size suffix TBytes=1024 GBytes"))
            .arg(tr("PB", "size suffix PBytes=1024 TBytes"));
    return strRegexp;
}

/* static */
quint64 UITranslator::parseSize(const QString &strText)
{
    /* Text should be in form of B|KB|MB|GB|TB|PB. */
    const QRegularExpression re(sizeRegexp());
    const QRegularExpressionMatch mt = re.match(strText);
    if (mt.hasMatch())
    {
        QString strInteger = mt.captured(1);
        QString strHundred;
        QString strSuff = mt.captured(2);
        if (strInteger.isEmpty())
        {
            strInteger = mt.captured(3);
            strHundred = mt.captured(4);
            strSuff = mt.captured(5);
        }

        quint64 uDenominator = 0;
        if (strSuff.isEmpty() || strSuff == tr("B", "size suffix Bytes"))
            uDenominator = 1;
        else if (strSuff == tr("KB", "size suffix KBytes=1024 Bytes"))
            uDenominator = _1K;
        else if (strSuff == tr("MB", "size suffix MBytes=1024 KBytes"))
            uDenominator = _1M;
        else if (strSuff == tr("GB", "size suffix GBytes=1024 MBytes"))
            uDenominator = _1G;
        else if (strSuff == tr("TB", "size suffix TBytes=1024 GBytes"))
            uDenominator = _1T;
        else if (strSuff == tr("PB", "size suffix PBytes=1024 TBytes"))
            uDenominator = _1P;

        quint64 iInteger = strInteger.toULongLong();
        if (uDenominator == 1)
            return iInteger;

        quint64 iHundred = strHundred.leftJustified(2, '0').toULongLong();
        iHundred = iHundred * uDenominator / 100;
        iInteger = iInteger * uDenominator + iHundred;
        return iInteger;
    }
    else
        return 0;
}

/* static */
SizeSuffix UITranslator::parseSizeSuffix(const QString &strText)
{
    /* Text should be in form of B|KB|MB|GB|TB|PB. */
    const QRegularExpression re(sizeRegexp());
    const QRegularExpressionMatch mt = re.match(strText);
    if (mt.hasMatch())
    {
        QString strInteger = mt.captured(1);
        QString strSuff = mt.captured(2);
        if (strInteger.isEmpty())
        {
            strInteger = mt.captured(3);
            strSuff = mt.captured(5);
        }

        SizeSuffix enmSizeSuffix = SizeSuffix_Byte;

        if (strSuff.isEmpty() || strSuff == tr("B", "size suffix Bytes"))
            enmSizeSuffix = SizeSuffix_Byte;
        else if (strSuff == tr("KB", "size suffix KBytes=1024 Bytes"))
            enmSizeSuffix = SizeSuffix_KiloByte;
        else if (strSuff == tr("MB", "size suffix MBytes=1024 KBytes"))
            enmSizeSuffix = SizeSuffix_MegaByte;
        else if (strSuff == tr("GB", "size suffix GBytes=1024 MBytes"))
            enmSizeSuffix = SizeSuffix_GigaByte;
        else if (strSuff == tr("TB", "size suffix TBytes=1024 GBytes"))
            enmSizeSuffix = SizeSuffix_TeraByte;
        else if (strSuff == tr("PB", "size suffix PBytes=1024 TBytes"))
            enmSizeSuffix = SizeSuffix_PetaByte;
        return enmSizeSuffix;
    }
    else
        return SizeSuffix_Byte;
}

/* static */
bool UITranslator::hasSizeSuffix(const QString &strText)
{
    /* Text should be in form of B|KB|MB|GB|TB|PB. */
    const QRegularExpression re(sizeRegexp());
    const QRegularExpressionMatch mt = re.match(strText);
    if (mt.hasMatch())
    {
        QString strInteger = mt.captured(1);
        QString strSuff = mt.captured(2);
        if (strInteger.isEmpty())
        {
            strInteger = mt.captured(3);
            strSuff = mt.captured(5);
        }

        if (strSuff.isEmpty())
            return false;
        if (strSuff == tr("B", "size suffix Bytes") ||
            strSuff == tr("KB", "size suffix KBytes=1024 Bytes") ||
            strSuff == tr("MB", "size suffix MBytes=1024 KBytes") ||
            strSuff == tr("GB", "size suffix GBytes=1024 MBytes") ||
            strSuff == tr("TB", "size suffix TBytes=1024 GBytes") ||
            strSuff == tr("PB", "size suffix PBytes=1024 TBytes"))
            return true;
        return false;
    }
    else
        return false;
}

/* static */
QString UITranslator::formatSize(quint64 uSize, uint cDecimal /* = 2 */,
                                 FormatSize enmMode /* = FormatSize_Round */)
{
    /* Text will be in form of B|KB|MB|GB|TB|PB.
     *
     * When enmMode is FormatSize_Round, the result is rounded to the
     *              closest number containing @a aDecimal decimal digits.
     * When enmMode is FormatSize_RoundDown, the result is rounded to the
     *              largest number with @a aDecimal decimal digits that is not greater than
     *              the result. This guarantees that converting the resulting string back to
     *              the integer value in bytes will not produce a value greater that the
     *              initial size parameter.
     * When enmMode is FormatSize_RoundUp, the result is rounded to the
     *              smallest number with @a aDecimal decimal digits that is not less than the
     *              result. This guarantees that converting the resulting string back to the
     *              integer value in bytes will not produce a value less that the initial
     *              size parameter. */

    quint64 uDenominator = 0;
    int iSuffix = 0;

    if (uSize < _1K)
    {
        uDenominator = 1;
        iSuffix = 0;
    }
    else if (uSize < _1M)
    {
        uDenominator = _1K;
        iSuffix = 1;
    }
    else if (uSize < _1G)
    {
        uDenominator = _1M;
        iSuffix = 2;
    }
    else if (uSize < _1T)
    {
        uDenominator = _1G;
        iSuffix = 3;
    }
    else if (uSize < _1P)
    {
        uDenominator = _1T;
        iSuffix = 4;
    }
    else
    {
        uDenominator = _1P;
        iSuffix = 5;
    }

    quint64 uInteger = uSize / uDenominator;
    quint64 uDecimal = uSize % uDenominator;
    quint64 uMult = 1;
    for (uint i = 0; i < cDecimal; ++i)
        uMult *= 10;

    QString strNumber;
    if (uDenominator > 1)
    {
        if (uDecimal)
        {
            uDecimal *= uMult;
            /* Not greater: */
            if (enmMode == FormatSize_RoundDown)
                uDecimal = uDecimal / uDenominator;
            /* Not less: */
            else if (enmMode == FormatSize_RoundUp)
                uDecimal = (uDecimal + uDenominator - 1) / uDenominator;
            /* Nearest: */
            else
                uDecimal = (uDecimal + uDenominator / 2) / uDenominator;
        }
        /* Check for the fractional part overflow due to rounding: */
        if (uDecimal == uMult)
        {
            uDecimal = 0;
            ++uInteger;
            /* Check if we've got 1024 XB after rounding and scale down if so: */
            if (uInteger == 1024 && iSuffix + 1 < (int)SizeSuffix_Max)
            {
                uInteger /= 1024;
                ++iSuffix;
            }
        }
        strNumber = QString::number(uInteger);
        if (cDecimal)
            strNumber += QString("%1%2").arg(decimalSep())
                                        .arg(QString::number(uDecimal).rightJustified(cDecimal, '0'));
    }
    else
    {
        strNumber = QString::number(uInteger);
    }

    return QString("%1 %2").arg(strNumber).arg(gpConverter->toString(static_cast<SizeSuffix>(iSuffix)));
}

/* static */
QString UITranslator::addMetricSuffixToNumber(quint64 uNumber)
{
    if (uNumber <= 0)
        return QString();
    /* See https://en.wikipedia.org/wiki/Metric_prefix for metric suffixes:*/
    char suffixes[] = {'k', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'};
    int zeroCount = (int)log10((long double)uNumber);
    if (zeroCount < 3)
        return QString::number(uNumber);
    int h = 3 * (zeroCount / 3);
    char result[128];
    sprintf(result, "%.2f", uNumber / (float)pow((double)10, h));
    return QString("%1%2").arg(result).arg(suffixes[h / 3 - 1]);
}

/* static */
QStringList UITranslator::COMPortNames()
{
    QStringList list;
    for (size_t i = 0; i < RT_ELEMENTS(kComKnownPorts); ++i)
        list << kComKnownPorts[i].name;

    return list;
}

/* static */
QString UITranslator::toCOMPortName(ulong uIRQ, ulong uIOBase)
{
    for (size_t i = 0; i < RT_ELEMENTS(kComKnownPorts); ++i)
        if (kComKnownPorts[i].IRQ == uIRQ &&
            kComKnownPorts[i].IOBase == uIOBase)
            return kComKnownPorts[i].name;

    return tr("User-defined", "serial port");;
}

/* static */
bool UITranslator::toCOMPortNumbers(const QString &strName, ulong &uIRQ, ulong &uIOBase)
{
    for (size_t i = 0; i < RT_ELEMENTS(kComKnownPorts); ++i)
        if (strcmp(kComKnownPorts[i].name, strName.toUtf8().data()) == 0)
        {
            uIRQ = kComKnownPorts[i].IRQ;
            uIOBase = kComKnownPorts[i].IOBase;
            return true;
        }

    return false;
}

/* Regular expressions used by both highlight and emphasize.  They use the
   same prefix and suffix expression.  Unfortunately, QRegularExpression isn't
   thread safe, so we only store the string contstants here. */
/** @todo qt6: Both these had bogus suffix sets '[:.-!);]', I've changed them to '[-:.!);]', hope that's correct. */
static char const g_szRxSingleQuotes[] = "((?:^|\\s)[(]?)"
                                         "'([^']*)'"
                                         "(?=[-:.!);]?(?:\\s|$))";
static const char g_szRxUuid[]         = "((?:^|\\s)[(]?)"
                                         "(\\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\\})"
                                         "(?=[-:.!);]?(?:\\s|$))";

/* static */
QString UITranslator::highlight(QString strText, bool fToolTip /* = false */)
{
    /* We should reformat the input strText so that:
     * - strings in single quotes will be put inside <nobr> and marked
     *   with blue color;
     * - UUIDs be put inside <nobr> and marked
     *   with green color;
     * - replaces new line chars with </p><p> constructs to form paragraphs
     *   (note that <p\> and </p> are not appended to the beginning and to the
     *    end of the string respectively, to allow the result be appended
     *    or prepended to the existing paragraph).
     *
     *  If @a fToolTip is true, colouring is not applied, only the <nobr> tag
     *  is added. Also, new line chars are replaced with <br> instead of <p>. */

    QString strFont;
    QString uuidFont;
    QString endFont;
    if (!fToolTip)
    {
        strFont = "<font color=#0000CC>";
        uuidFont = "<font color=#008000>";
        endFont = "</font>";
    }

    /* Replace special entities, '&' -- first! */
    strText.replace('&', "&amp;");
    strText.replace('<', "&lt;");
    strText.replace('>', "&gt;");
    strText.replace('\"', "&quot;");

    /* Mark strings in single quotes with color: */
    strText.replace(QRegularExpression(g_szRxSingleQuotes), QString("\\1%1<nobr>'\\2'</nobr>%2").arg(strFont).arg(endFont));

    /* Mark UUIDs with color: */
    strText.replace(QRegularExpression(g_szRxUuid), QString("\\1%1<nobr>\\2</nobr>%2").arg(uuidFont).arg(endFont));

    /* Split to paragraphs at \n chars: */
    if (!fToolTip)
        strText.replace('\n', "</p><p>");
    else
        strText.replace('\n', "<br>");

    return strText;
}

/* static */
QString UITranslator::emphasize(QString strText)
{
    /* We should reformat the input string @a strText so that:
     * - strings in single quotes will be put inside \<nobr\> and marked
     *   with bold style;
     * - UUIDs be put inside \<nobr\> and marked
     *   with italic style;
     * - replaces new line chars with \</p\>\<p\> constructs to form paragraphs
     *   (note that \<p\> and \</p\> are not appended to the beginning and to the
     *    end of the string respectively, to allow the result be appended
     *    or prepended to the existing paragraph). */

    QString strEmphStart("<b>");
    QString strEmphEnd("</b>");
    QString uuidEmphStart("<i>");
    QString uuidEmphEnd("</i>");

    /* Replace special entities, '&' -- first! */
    strText.replace('&', "&amp;");
    strText.replace('<', "&lt;");
    strText.replace('>', "&gt;");
    strText.replace('\"', "&quot;");

    /* Mark strings in single quotes with bold style: */
    strText.replace(QRegularExpression(g_szRxSingleQuotes), QString("\\1%1<nobr>'\\2'</nobr>%2").arg(strEmphStart).arg(strEmphEnd));

    /* Mark UUIDs with italic style: */
    strText.replace(QRegularExpression(g_szRxUuid), QString("\\1%1<nobr>\\2</nobr>%2").arg(uuidEmphStart).arg(uuidEmphEnd));

    /* Split to paragraphs at \n chars: */
    strText.replace('\n', "</p><p>");

    return strText;
}

/* static */
QString UITranslator::removeAccelMark(QString strText)
{
    /* In order to support accelerators used in non-alphabet languages
     * (e.g. Japanese) that has a form of "(&<L>)" (where <L> is a latin letter),
     * this method first searches for this pattern and, if found, removes it as a
     * whole. If such a pattern is not found, then the '&' character is simply
     * removed from the string. */

    const QRegularExpression re("\\(&[a-zA-Z]\\)");
    const QRegularExpressionMatch mt = re.match(strText);
    int iPos = mt.capturedStart();
    if (iPos >= 0)
        strText.remove(iPos, mt.capturedLength());
    else
    {
        iPos = strText.indexOf('&');
        if (iPos >= 0)
            strText.remove(iPos, 1);
    }

    return strText;
}

/* static */
QString UITranslator::insertKeyToActionText(const QString &strText, const QString &strKey)
{
#ifdef VBOX_WS_MAC
    QString strPattern("%1 (Host+%2)");
#else
    QString strPattern("%1 \tHost+%2");
#endif
    if (   strKey.isEmpty()
        || strKey.compare("None", Qt::CaseInsensitive) == 0)
        return strText;
    else
        return strPattern.arg(strText).arg(QKeySequence(strKey).toString(QKeySequence::NativeText));
}

/* static */
bool UITranslator::isTranslationInProgress()
{
    return s_fTranslationInProgress;
}

/* static */
QString UITranslator::byteStringToMegaByteString(const QString &strByteString)
{
    if (strByteString.isEmpty())
        return QString();
    bool fConversionSuccess = false;
    qulonglong uByte = strByteString.toULongLong(&fConversionSuccess);
    AssertReturn(fConversionSuccess, QString());
    return QString::number(uByte / _1M);
}

/* static */
QString UITranslator::megabyteStringToByteString(const QString &strMegaByteString)
{
    if (strMegaByteString.isEmpty())
        return QString();
    bool fConversionSuccess = false;
    qulonglong uMegaByte = strMegaByteString.toULongLong(&fConversionSuccess);
    AssertReturn(fConversionSuccess, QString());
    return QString::number(uMegaByte * _1M);
}

UITranslator::UITranslator(QObject *pParent /* = 0 */)
    : QTranslator(pParent)
{
}

bool UITranslator::loadFile(const QString &strFileName)
{
    QFile file(strFileName);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    m_data = file.readAll();
    return load((uchar*)m_data.data(), m_data.size());
}

/* static */
QString UITranslator::languageName()
{
    /* Returns "English" if no translation is installed
     * or if the translation file is invalid. */
    return QApplication::translate("@@@", "English",
                                   "Native language name");
}

/* static */
QString UITranslator::languageCountry()
{
    /* Returns "--" if no translation is installed or if the translation file
     * is invalid, or if the language is independent on the country. */
    return QApplication::translate("@@@", "--",
                                   "Native language country name "
                                   "(empty if this language is for all countries)");
}

/* static */
QString UITranslator::languageNameEnglish()
{
    /* Returns "English" if no translation is installed
     * or if the translation file is invalid. */
    return QApplication::translate("@@@", "English",
                                   "Language name, in English");
}

/* static */
QString UITranslator::languageCountryEnglish()
{
    /* Returns "--" if no translation is installed or if the translation file
     * is invalid, or if the language is independent on the country. */
    return QApplication::translate("@@@", "--",
                                   "Language country name, in English "
                                   "(empty if native country name is empty)");
}

/* static */
QString UITranslator::languageTranslators()
{
    /* Returns "Oracle Corporation" if no translation is installed or if the translation file
     * is invalid, or if the translation is supplied by Oracle Corporation. */
    return QApplication::translate("@@@", "Oracle Corporation",
                                   "Comma-separated list of translators");
}

/* static */
QString UITranslator::systemLanguageId()
{
    /* This does exactly the same as QLocale::system().name() but corrects its wrong behavior on Linux systems
     * (LC_NUMERIC for some strange reason takes precedence over any other locale setting in the QLocale::system()
     * implementation). This implementation first looks at LC_ALL (as defined by SUS), then looks at LC_MESSAGES
     * which is designed to define a language for program messages in case if it differs from the language for
     * other locale categories. Then it looks for LANG and finally falls back to QLocale::system().name().
     *
     * The order of precedence is well defined here:
     * http://opengroup.org/onlinepubs/007908799/xbd/envvar.html
     *
     * This method will return "C" when the requested locale is invalid or when the "C" locale is set explicitly. */

#if defined(VBOX_WS_MAC)
    // QLocale return the right id only if the user select the format
    // of the language also. So we use our own implementation */
    return ::darwinSystemLanguage();
#elif defined(Q_OS_UNIX)
    const char *pszValue = RTEnvGet("LC_ALL");
    if (pszValue == 0)
        pszValue = RTEnvGet("LC_MESSAGES");
    if (pszValue == 0)
        pszValue = RTEnvGet("LANG");
    if (pszValue != 0)
        return QLocale(pszValue).name();
#endif
    return QLocale::system().name();
}
