/* $Id: Docs-CodingGuidelines.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMM - Coding Guidelines.
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


/** @page pg_vmm_guideline                  VMM Coding Guidelines
 *
 * The guidelines extends the VBox coding guidelines (@ref pg_vbox_guideline)
 * and consists of a compulsory part and an optional part. It is very important
 * that the rules of the compulsory part is followed. That will prevent obvious
 * bugs, and it will ease porting the code to 32/64 and 64/32 bits setups.
 *
 *
 *
 * @section sec_vmm_guideline_compulsory        Compulsory
 *
 * It is of vital importance is to distinguish between addresses - both virtual
 * and physical - applying to Guest Context and Host Context. To assist the
 * coder in this, a set of types and macros have been created. Another vital
 * thing is that structures shared between the two contexts ends up with the
 * same size and member offsets in both places. There are types and macros
 * for that too.
 *
 *
 * The rules:
 *
 *      - When declaring pointers in shared structures use the RCPTRTYPE(),
 *        R0PTRTYPE() and R3PTRTYPE() macros.
 *
 *      - Use RTGCPTR and RTHCPTR when dealing with the other context in
 *        none shared structures, parameter lists, stack variables and such.
 *
 *      - Following the above rules, pointers will in a context other than the
 *        one a pointer was defined for, appear as unsigned integers.
 *
 *      - It is NOT permitted to subject a pointer from the other context to pointer
 *        types of the current context by direct cast or by definition.
 *
 *      - When doing pointer arithmetic cast using uintptr_t, intptr_t or char *.
 *        Never cast a pointer to anything else for this purpose, that will not
 *        work everywhere! (1)
 *
 *      - Physical addresses are also specific to their context. Use RTGCPHYS
 *        and RTHCPHYS when dealing when them. Both types are unsigned integers.
 *
 *      - Integers in shared structures should be using a RT integer type or
 *        any of the [u]int[0-9]+_t types. (2)
 *
 *      - If code is shared between the contexts, GCTYPE() can be used to declare
 *        things differently. If GCTYPE() usage is extensive, don't share the code.
 *
 *      - The context is part of all public symbols which are specific to a single
 *        context.
 *
 *
 * (1) Talking about porting between 32-bit and 64-bit architectures and even
 *     between 64-bit platforms. On 64-bit linux int is 32-bit, long is 64-bit.
 *     However on 64-bit windows both int and long are 32-bit - there is no
 *     standard 64 bit type (_int64 is not a standard type, it's an stupid
 *     extension).
 *
 * (2) The VBox integer types are RTINT, RTUINT, RTGCINT, RTGCUINT,
 *     RTGCINTPTR, RTGCUINTPTR, RTHCINT, RTHCUINT, RTHCINTPTR and
 *     RTHCUINTPTR.
 *
 *
 *
 * @section sec_vmm_guideline_optional          Optional
 *
 * There are the general VBox guidelines, see @ref sec_vbox_guideline_optional.
 * In addition to these for the following rules applies to the VMM:
 *
 *      - Prefixes GCPtr and HCPtr are preferred over suffixes HC and GC of
 *        pointers.
 *
 *      - Prefixes GCPhys and HCPhys are generally used for physical addresses,
 *        types RTGCPHYS and RTHCPHYS respectively.
 *
 */

