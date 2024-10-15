/* $Id: UIErrorString.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIErrorString class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIErrorString_h
#define FEQT_INCLUDED_SRC_globals_UIErrorString_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QString>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Other VBox includes: */
#include <VBox/com/defs.h>

/* Forward declarations: */
class COMBaseWithEI;
class COMErrorInfo;
class COMResult;
class CProgress;
class CVirtualBoxErrorInfo;

/** Namespace simplifying COM error formatting. */
class SHARED_LIBRARY_STUFF UIErrorString
{
public:

    /** Returns formatted @a rc information. */
    static QString formatRC(HRESULT rc);
    /** Returns full formatted @a rc information. */
    static QString formatRCFull(HRESULT rc);
    /** Returns formatted error information for passed @a comProgress. */
    static QString formatErrorInfo(const CProgress &comProgress);
    /** Returns formatted error information for passed @a comInfo and @a wrapperRC. */
    static QString formatErrorInfo(const COMErrorInfo &comInfo, HRESULT wrapperRC = S_OK);
    /** Returns formatted error information for passed @a comInfo. */
    static QString formatErrorInfo(const CVirtualBoxErrorInfo &comInfo);
    /** Returns formatted error information for passed @a comWrapper. */
    static QString formatErrorInfo(const COMBaseWithEI &comWrapper);
    /** Returns formatted error information for passed @a comRc. */
    static QString formatErrorInfo(const COMResult &comRc);

    /** Returns simplified error information for passed @a comInfo and @a wrapperRC. */
    static QString simplifiedErrorInfo(const COMErrorInfo &comInfo, HRESULT wrapperRC = S_OK);
    /** Returns simplified error information for passed @a comWrapper. */
    static QString simplifiedErrorInfo(const COMBaseWithEI &comWrapper);

private:

    /** Converts passed @a comInfo and @a wrapperRC to string. */
    static QString errorInfoToString(const COMErrorInfo &comInfo, HRESULT wrapperRC = S_OK);

    /** Converts passed @a comInfo and @a wrapperRC to simplified string. */
    static QString errorInfoToSimpleString(const COMErrorInfo &comInfo, HRESULT wrapperRC = S_OK);
};

#endif /* !FEQT_INCLUDED_SRC_globals_UIErrorString_h */

