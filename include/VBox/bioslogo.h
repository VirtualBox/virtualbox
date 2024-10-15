/* $Id: bioslogo.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BiosLogo - The Private BIOS Logo Interface. (DEV)
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

#ifndef VBOX_INCLUDED_bioslogo_h
#define VBOX_INCLUDED_bioslogo_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef VBOX_PC_BIOS
# include <iprt/types.h>
# include <iprt/assert.h>
#endif

/** @defgroup grp_bios_logo     The Private BIOS Logo Interface.
 * @ingroup grp_devdrv
 * @internal
 *
 * @remark All this is currently duplicated in logo.c.
 *
 * @{
 */

/** The extra port which is used to show the BIOS logo. */
#define LOGO_IO_PORT         0x3b8

/** The BIOS logo fade in/fade out steps. */
#define LOGO_SHOW_STEPS      16

/** @name The BIOS logo commands.
 * @{
 */
#define LOGO_CMD_NOP         0
#define LOGO_CMD_SET_OFFSET  0x100
#define LOGO_CMD_SHOW_BMP    0x200
/** @} */

/**
 * PC Bios logo data structure.
 */
typedef struct LOGOHDR
{
    /** Signature (LOGO_HDR_MAGIC/0x66BB). */
    uint16_t        u16Signature;
    /** Logo time (msec). */
    uint16_t        u16LogoMillies;
    /** Fade in - boolean. */
    uint8_t         fu8FadeIn;
    /** Fade out - boolean. */
    uint8_t         fu8FadeOut;
    /** Show setup - boolean. */
    uint8_t         fu8ShowBootMenu;
    /** Reserved / padding. */
    uint8_t         u8Reserved;
    /** Logo file size. */
    uint32_t        cbLogo;
} LOGOHDR;
#ifndef VBOX_PC_BIOS
AssertCompileSize(LOGOHDR, 12);
#endif
/** Pointer to a PC BIOS logo header. */
typedef LOGOHDR *PLOGOHDR;
/** Pointer to a const PC BIOS logo header. */
typedef LOGOHDR const *PCLOGOHDR;

/** The value of the LOGOHDR::u16Signature field. */
#define LOGO_HDR_MAGIC      0x66BB

/** The value which will switch you the default logo. */
#define LOGO_DEFAULT_LOGO   0xFFFF

/** @} */

#endif /* !VBOX_INCLUDED_bioslogo_h */

