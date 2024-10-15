/* $Id: UILibraryDefs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Global library definitions.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_globals_UILibraryDefs_h
#define FEQT_INCLUDED_SRC_globals_UILibraryDefs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Other VBox includes: */
#include <iprt/cdefs.h> // for DECLEXPORT_CLASS / DECLIMPORT_CLASS stuff

/* Define shared library stuff: */
#ifdef VBOX_GUI_LIBRARY
# define SHARED_LIBRARY_STUFF DECLEXPORT_CLASS
#else
# define SHARED_LIBRARY_STUFF DECLIMPORT_CLASS
#endif

#endif /* !FEQT_INCLUDED_SRC_globals_UILibraryDefs_h */
