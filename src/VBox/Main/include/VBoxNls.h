/* $Id: VBoxNls.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox NLS.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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


#ifndef MAIN_INCLUDED_VBoxNls_h
#define MAIN_INCLUDED_VBoxNls_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef VBOX_WITH_MAIN_NLS

#include <VBox/com/defs.h>
#include <VBox/com/ptr.h>
#include <VBox/com/string.h>
#include "VirtualBoxTranslator.h"


# define DECLARE_TRANSLATION_CONTEXT(ctx) \
struct ctx \
{\
   static const char *tr(const char *pszSource, const char *pszComment = NULL, const size_t aNum = ~(size_t)0) \
   { \
       return VirtualBoxTranslator::translate(NULL, #ctx, pszSource, pszComment, aNum); \
   } \
}
#else
# define DECLARE_TRANSLATION_CONTEXT(ctx) \
struct ctx \
{\
   static const char *tr(const char *pszSource, const char *pszComment = NULL, const size_t aNum = ~(size_t)0) \
   { \
       NOREF(pszComment); \
       NOREF(aNum);       \
       return pszSource;  \
   } \
}
#endif

#endif /* !MAIN_INCLUDED_VBoxNls_h */

