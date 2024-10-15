/* $Id: UIConverter.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIConverter declaration.
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

#ifndef FEQT_INCLUDED_SRC_converter_UIConverter_h
#define FEQT_INCLUDED_SRC_converter_UIConverter_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

/* GUI includes: */
#include "UILibraryDefs.h"

/** High-level interface for different conversions between GUI classes.
  * @todo Replace singleton with static template interface. */
class SHARED_LIBRARY_STUFF UIConverter
{
public:

    /** Returns singleton instance. */
    static UIConverter *instance() { return s_pInstance; }

    /** Creates singleton instance. */
    static void create();
    /** Destroys singleton instance. */
    static void destroy();

    /** Converts QColor <= template class. */
    template<class T> QColor toColor(const T &data) const;

    /** Converts QIcon <= template class. */
    template<class T> QIcon toIcon(const T &data) const;

    /** Converts QPixmap <= template class. */
    template<class T> QPixmap toWarningPixmap(const T &data) const;

    /** Converts QString <= template class. */
    template<class T> QString toString(const T &data) const;
    /** Converts template class <= QString. */
    template<class T> T fromString(const QString &strData) const;

    /** Converts QString <= template class. */
    template<class T> QString toInternalString(const T &data) const;
    /** Converts template class <= QString. */
    template<class T> T fromInternalString(const QString &strData) const;

    /** Converts int <= template class. */
    template<class T> int toInternalInteger(const T &data) const;
    /** Converts template class <= int. */
    template<class T> T fromInternalInteger(const int &iData) const;

private:

    /** Constructs converter. */
    UIConverter() { s_pInstance = this; }
    /** Destructs converter. */
    virtual ~UIConverter() { s_pInstance = 0; }

    /** Holds the static instance. */
    static UIConverter *s_pInstance;
};

/** Singleton UI converter 'official' name. */
#define gpConverter UIConverter::instance()

#endif /* !FEQT_INCLUDED_SRC_converter_UIConverter_h */
