/* $Id: VBoxOGL.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox 3D Support API
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

#ifndef VBOX_INCLUDED_VBoxOGL_h
#define VBOX_INCLUDED_VBoxOGL_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI and VBox OpenGL code require scaling factor value to be stored in container
 * of type of 'double'. Communication between them is done via Main. In the same time,
 * currently, Main does not like type of 'double' to be used for an interface method parameter.
 * An integer type should be used instead. This value is used in order to specify scaling factor in type
 * of 'integer' units. It is assumed that GUI feeds Main with its internal scaling factor value
 * (which is originally of type of 'double') multiplied by this constant and converted resulting
 * value to type of 'uint32_t'. Then Main provides this data to OpenGL HGCM thread. Finally, VBox OpenGL
 * code divides received scalar by this constant and converts result to type of 'double'.
 * This constant can be increased (multiplied by 10^n) in order to get better precision
 * for scaling factor manipulations. */
#define VBOX_OGL_SCALE_FACTOR_MULTIPLIER 10000.0

#endif /* !VBOX_INCLUDED_VBoxOGL_h */
