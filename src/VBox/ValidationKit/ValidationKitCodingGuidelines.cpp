/* $Id: ValidationKitCodingGuidelines.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Validation Kit - Coding Guidelines.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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


/** @page pg_validationkit_guideline    Validation Kit Coding Guidelines
 *
 * The guidelines extends the VBox coding guidelines (@ref pg_vbox_guideline)
 * and currently only defines python prefixes and linting.
 *
 *
 * @section sec_validationkit_guideline_python      Python
 *
 * Python is a typeless language so using prefixes to indicate the intended
 * type of a variable or attribute can be very helpful.
 *
 * Type prefixes:
 *      - 'b' for byte (octet).
 *      - 'ch' for a single character.
 *      - 'f' for boolean and flags.
 *      - 'fn' for function or method references.
 *      - 'fp' for floating point values.
 *      - 'i' for integers.
 *      - 'l' for long integers.
 *      - 'o' for objects, structures and anything with attributes that doesn't
 *        match any of the other type prefixes.
 *      - 'r' for a range or xrange.
 *      - 's' for a string (can be unicode).
 *      - 'su' for a unicode string when the distinction is important.
 *
 * Collection qualifiers:
 *      - 'a' for a list or an array.
 *      - 'd' for a dictionary.
 *      - 'h' for a set (hashed).
 *      - 't' for a tuple.
 *
 * Other qualifiers:
 *      - 'c' for a count. Implies integer or long integer type. Highest
 *        priority.
 *      - 'sec' for a second value. Implies long integer type.
 *      - 'ms' for a millisecond value. Implies long integer type.
 *      - 'us' for a microsecond value. Implies long integer type.
 *      - 'ns' for a nanosecond value. Implies long integer type.
 *
 * The 'ms', 'us', 'ns' and 'se' qualifiers can be capitalized when prefixed by
 * 'c', e.g. cMsElapsed.  While this technically means they are no longer a
 * prefix, it's easier to read and everyone understands what it means.
 *
 * The type collection qualifiers comes first, then the other qualifiers and
 * finally the type qualifier.
 *
 * Python statements are terminated by semicolons (';') as a convention.
 *
 */

