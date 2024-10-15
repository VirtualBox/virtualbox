/* $Id: bs3-cmn-pic.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BS3Kit - Internal PIC Defines, Variables and Functions.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#ifndef BS3KIT_INCLUDED_bs3_cmn_pic_h
#define BS3KIT_INCLUDED_bs3_cmn_pic_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "bs3kit.h"


/** The master PIC port (base).   */
#define BS3_PIC_PORT_MASTER         UINT8_C(0x20)
/** The slave PIC port (base).   */
#define BS3_PIC_PORT_SLAVE          UINT8_C(0xa0)

/** The init command. */
#define BS3_PIC_CMD_INIT            UINT8_C(0x10)
/** 4th init step option for the init command.   */
#define BS3_PIC_CMD_INIT_F_4STEP    UINT8_C(0x01)

/** Auto end of interrupt flag for the 4th init step. */
#define BS3_PIC_I4_F_AUTO_EOI       UINT8_C(0x01)


RT_C_DECLS_BEGIN

extern bool g_fBs3PicConfigured;

RT_C_DECLS_END


#include "bs3kit-mangling-code.h"

#endif /* !BS3KIT_INCLUDED_bs3_cmn_pic_h */

