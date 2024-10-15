/* $Id: VBoxVideoIPRT.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Video driver, common code - iprt and VirtualBox macros and definitions.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_Graphics_VBoxVideoIPRT_h
#define VBOX_INCLUDED_Graphics_VBoxVideoIPRT_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#if !defined(RT_OS_OS2) || !defined(__IBMC__) /* IBM VACpp 3.08 doesn't properly eliminate unused inline functions */
# include <iprt/asm.h>
# include <iprt/string.h>
#endif
#include <iprt/assert.h>
#include <iprt/cdefs.h>
#include <iprt/err.h>
#include <iprt/list.h>
#include <iprt/stdarg.h>
#include <iprt/stdint.h>
#include <iprt/types.h>

#if !defined(VBOX_XPDM_MINIPORT) && !defined(RT_OS_OS2) && (defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86))
# include <iprt/asm-amd64-x86.h>
#endif

#ifdef VBOX_XPDM_MINIPORT
# include <iprt/nt/miniport.h>
# include <ntddvdeo.h> /* sdk, clean */
# include <iprt/nt/Video.h>
#endif

/** @name Port I/O helpers
 * @{ */

#ifdef VBOX_XPDM_MINIPORT

/** Write an 8-bit value to an I/O port. */
# define VBVO_PORT_WRITE_U8(Port, Value) \
    VideoPortWritePortUchar((PUCHAR)Port, Value)
/** Write a 16-bit value to an I/O port. */
# define VBVO_PORT_WRITE_U16(Port, Value) \
    VideoPortWritePortUshort((PUSHORT)Port, Value)
/** Write a 32-bit value to an I/O port. */
# define VBVO_PORT_WRITE_U32(Port, Value) \
    VideoPortWritePortUlong((PULONG)Port, Value)
/** Read an 8-bit value from an I/O port. */
# define VBVO_PORT_READ_U8(Port) \
    VideoPortReadPortUchar((PUCHAR)Port)
/** Read a 16-bit value from an I/O port. */
# define VBVO_PORT_READ_U16(Port) \
    VideoPortReadPortUshort((PUSHORT)Port)
/** Read a 32-bit value from an I/O port. */
# define VBVO_PORT_READ_U32(Port) \
    VideoPortReadPortUlong((PULONG)Port)

#else  /** @todo make these explicit */

/** Write an 8-bit value to an I/O port. */
# define VBVO_PORT_WRITE_U8(Port, Value) \
    ASMOutU8(Port, Value)
/** Write a 16-bit value to an I/O port. */
# define VBVO_PORT_WRITE_U16(Port, Value) \
    ASMOutU16(Port, Value)
/** Write a 32-bit value to an I/O port. */
# define VBVO_PORT_WRITE_U32(Port, Value) \
    ASMOutU32(Port, Value)
/** Read an 8-bit value from an I/O port. */
# define VBVO_PORT_READ_U8(Port) \
    ASMInU8(Port)
/** Read a 16-bit value from an I/O port. */
# define VBVO_PORT_READ_U16(Port) \
    ASMInU16(Port)
/** Read a 32-bit value from an I/O port. */
# define VBVO_PORT_READ_U32(Port) \
    ASMInU32(Port)
#endif

/** @}  */

#endif /* !VBOX_INCLUDED_Graphics_VBoxVideoIPRT_h */

