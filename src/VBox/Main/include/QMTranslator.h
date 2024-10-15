/* $Id: QMTranslator.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef MAIN_INCLUDED_QMTranslator_h
#define MAIN_INCLUDED_QMTranslator_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

class QMTranslator_Impl;

class QMTranslator
{
public:
    QMTranslator();
    virtual ~QMTranslator();

    /**
     * Gets translation from loaded QM file
     *
     * @param   pszContext      QM context to look for translation
     * @param   pszSource       Source string in one-byte encoding
     * @param   ppszSafeSource  Where to return pointer to a safe copy of @a
     *                          pszSource for the purpose of reverse translation.
     *                          Will be set to NULL if @a pszSource is returned.
     * @param   pszDisamb       Disambiguationg comment, empty by default
     * @param   aNum            Plural form indicator.
     *
     * @returns Pointer to a translation (UTF-8 encoding), source string on failure.
     */
    const char *translate(const char *pszContext, const char *pszSource, const char **ppszSafeSource,
                          const char *pszDisamb = NULL, const size_t aNum = ~(size_t)0) const RT_NOEXCEPT;

    /**
     * Loads and parses QM file
     *
     * @param   pszFilename The name of the file to load
     * @param   hStrCache   The string cache to use for storing strings.
     *
     * @returns VBox status code.
     */
    int load(const char *pszFilename, RTSTRCACHE hStrCache) RT_NOEXCEPT;

private:
    /** QMTranslator implementation.
     * To separate all the code from the interface */
    QMTranslator_Impl *m_impl;

    /* If copying is required, please define the following operators */
    void operator=(QMTranslator &);
    QMTranslator(const QMTranslator &);
};

#endif /* !MAIN_INCLUDED_QMTranslator_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
