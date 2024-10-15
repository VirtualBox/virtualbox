/* $Id: UITranslator.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UITranslator class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UITranslator_h
#define FEQT_INCLUDED_SRC_globals_UITranslator_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QTranslator>

/* GUI includes: */
#include "UILibraryDefs.h"

/** Size suffixes. */
enum SizeSuffix
{
    SizeSuffix_Byte = 0,
    SizeSuffix_KiloByte,
    SizeSuffix_MegaByte,
    SizeSuffix_GigaByte,
    SizeSuffix_TeraByte,
    SizeSuffix_PetaByte,
    SizeSuffix_Max
};

/** QTranslator subclass for VBox needs. */
class SHARED_LIBRARY_STUFF UITranslator : public QTranslator
{
    Q_OBJECT;

public:

    /** Size formatting types. */
    enum FormatSize
    {
        FormatSize_Round,
        FormatSize_RoundDown,
        FormatSize_RoundUp
    };

    /** Loads the language by language ID.
      * @param  strLangId  Brings the language ID in in form of xx_YY.
      *                    QString() means the system default language. */
    static void loadLanguage(const QString &strLangId = QString());

    /** Returns VBox language sub-directory. */
    static QString vboxLanguageSubDirectory();
    /** Returns VBox language file-base. */
    static QString vboxLanguageFileBase();
    /** Returns VBox language file-extension. */
    static QString vboxLanguageFileExtension();
    /** Returns VBox language ID reg-exp. */
    static QString vboxLanguageIdRegExp();
    /** Returns built in language name. */
    static QString vboxBuiltInLanguageName();

    /** Returns the loaded (active) language ID. */
    static QString languageId();

    /** Returns tr("%n year(s)"). */
    static QString yearsToString(uint32_t cVal);
    /** Returns tr("%n month(s)"). */
    static QString monthsToString(uint32_t cVal);
    /** Returns tr("%n day(s)"). */
    static QString daysToString(uint32_t cVal);
    /** Returns tr("%n hour(s)"). */
    static QString hoursToString(uint32_t cVal);
    /** Returns tr("%n minute(s)"). */
    static QString minutesToString(uint32_t cVal);
    /** Returns tr("%n second(s)"). */
    static QString secondsToString(uint32_t cVal);

    /** Returns tr("%n year(s) ago"). */
    static QString yearsToStringAgo(uint32_t cVal);
    /** Returns tr("%n month(s) ago"). */
    static QString monthsToStringAgo(uint32_t cVal);
    /** Returns tr("%n day(s) ago"). */
    static QString daysToStringAgo(uint32_t cVal);
    /** Returns tr("%n hour(s) ago"). */
    static QString hoursToStringAgo(uint32_t cVal);
    /** Returns tr("%n minute(s) ago"). */
    static QString minutesToStringAgo(uint32_t cVal);
    /** Returns tr("%n second(s) ago"). */
    static QString secondsToStringAgo(uint32_t cVal);

    /** Returns the decimal separator for the current locale. */
    static QString decimalSep();
    /** Returns the regexp string that defines the format of the human-readable size representation. */
    static QString sizeRegexp();
    /** Parses the given size strText and returns the size value in bytes. */
    static quint64 parseSize(const QString &strText);
    /** Parses the given size strText and returns the size suffix. */
    static SizeSuffix parseSizeSuffix(const QString &strText);
    /** Parses the given string @a strText and returns true if it includes a size suffix. */
    static bool hasSizeSuffix(const QString &strText);
    /** Formats the given @a uSize value in bytes to a human readable string.
      * @param  uSize     Brings the size value in bytes.
      * @param  enmMode   Brings the conversion mode.
      * @param  cDecimal  Brings the number of decimal digits in result. */
    static QString formatSize(quint64 uSize, uint cDecimal = 2, FormatSize enmMode = FormatSize_Round);
    /** Formats the given @a uNumber to that 'k' is added for thousand, 'M' for million and so on. */
    static QString addMetricSuffixToNumber(quint64 uNumber);

    /** Returns the list of the standard COM port names (i.e. "COMx"). */
    static QStringList COMPortNames();
    /** Returns the name of the standard COM port corresponding to the given parameters,
      * or "User-defined" (which is also returned when both @a uIRQ and @a uIOBase are 0). */
    static QString toCOMPortName(ulong uIRQ, ulong uIOBase);
    /** Returns port parameters corresponding to the given standard COM name.
      * Returns @c true on success, or @c false if the given port name is not one of the standard names (i.e. "COMx"). */
    static bool toCOMPortNumbers(const QString &strName, ulong &uIRQ, ulong &uIOBase);

    /** Reformats the input @a strText to highlight it. */
    static QString highlight(QString strText, bool fToolTip = false);
    /** Reformats the input @a strText to emphasize it. */
    static QString emphasize(QString strText);
    /** Removes the first occurrence of the accelerator mark (the ampersand symbol) from the given @a strText. */
    static QString removeAccelMark(QString strText);
    /** Inserts a passed @a strKey into action @a strText. */
    static QString insertKeyToActionText(const QString &strText, const QString &strKey);

    /** Returns whether we are performing translation currently. */
    static bool isTranslationInProgress();

    /* Converts bytes string to megabytes string. */
    static QString byteStringToMegaByteString(const QString &strByteString);
    /* Converts megabytes string to bytes string. */
    static QString megabyteStringToByteString(const QString &strMegaByteString);

private:

    /** Constructs translator passing @a pParent to the base-class. */
    UITranslator(QObject *pParent = 0);

    /** Loads language file with gained @a strFileName. */
    bool loadFile(const QString &strFileName);

    /** Native language name of the currently installed translation. */
    static QString languageName();
    /** Native language country name of the currently installed translation. */
    static QString languageCountry();
    /** Language name of the currently installed translation, in English. */
    static QString languageNameEnglish();
    /** Language country name of the currently installed translation, in English. */
    static QString languageCountryEnglish();
    /** Comma-separated list of authors of the currently installed translation. */
    static QString languageTranslators();

    /** Returns the system language ID. */
    static QString systemLanguageId();

    /** Holds the singleton instance. */
    static UITranslator *s_pTranslator;

    /** Holds whether we are performing translation currently. */
    static bool  s_fTranslationInProgress;

    /** Holds the currently loaded language ID. */
    static QString  s_strLoadedLanguageId;

    /** Holds the loaded data. */
    QByteArray  m_data;
};

#endif /* !FEQT_INCLUDED_SRC_globals_UITranslator_h */

