/* $Id: VBoxDD2.h 112403 2026-01-11 19:29:08Z knut.osmundsen@oracle.com $ */
/** @file
 * Built-in drivers & devices part 2 header.
 *
 * These drivers and devices are in separate modules because of LGPL.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_build_VBoxDD2_h
#define VBOX_INCLUDED_SRC_build_VBoxDD2_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/pdm.h>

RT_C_DECLS_BEGIN

#ifdef IN_VBOXDD2
# define DECL_VBOXDD2(a_Type)           DECLEXPORT(a_Type)
#else
# define DECL_VBOXDD2(a_Type)           DECLIMPORT(a_Type)
#endif

extern DECL_VBOXDD2(const unsigned char)  g_abPcBiosBinary386[];
extern DECL_VBOXDD2(const unsigned)       g_cbPcBiosBinary386;
extern DECL_VBOXDD2(const unsigned char)  g_abPcBiosBinary286[];
extern DECL_VBOXDD2(const unsigned)       g_cbPcBiosBinary286;
extern DECL_VBOXDD2(const unsigned char)  g_abPcBiosBinary8086[];
extern DECL_VBOXDD2(const unsigned)       g_cbPcBiosBinary8086;
extern DECL_VBOXDD2(const unsigned char)  g_abVgaBiosBinary386[];
extern DECL_VBOXDD2(const unsigned)       g_cbVgaBiosBinary386;
extern DECL_VBOXDD2(const unsigned char)  g_abVgaBiosBinary286[];
extern DECL_VBOXDD2(const unsigned)       g_cbVgaBiosBinary286;
extern DECL_VBOXDD2(const unsigned char)  g_abVgaBiosBinary8086[];
extern DECL_VBOXDD2(const unsigned)       g_cbVgaBiosBinary8086;
#ifdef VBOX_WITH_PXE_ROM
extern DECL_VBOXDD2(const unsigned char)  g_abNetBiosBinary[];
extern DECL_VBOXDD2(const unsigned)       g_cbNetBiosBinary;
#endif
#ifdef VBOX_WITH_EFI_IN_DD2
extern DECL_VBOXDD2(const unsigned char)  g_abEfiFirmwareX86[];
extern DECL_VBOXDD2(const unsigned)       g_cbEfiFirmwareX86;
extern DECL_VBOXDD2(const unsigned char)  g_abEfiFirmwareAmd64[];
extern DECL_VBOXDD2(const unsigned)       g_cbEfiFirmwareAmd64;
# ifdef VBOX_WITH_VIRT_ARMV8
extern DECL_VBOXDD2(const unsigned char)  g_abEfiFirmwareArm32[];
extern DECL_VBOXDD2(const unsigned)       g_cbEfiFirmwareArm32;
extern DECL_VBOXDD2(const unsigned char)  g_abEfiFirmwareArm64[];
extern DECL_VBOXDD2(const unsigned)       g_cbEfiFirmwareArm64;
# endif
# ifdef VBOX_WITH_VIRT_RISCV
extern DECL_VBOXDD2(const unsigned char)  g_abEfiFirmwareRiscV32[];
extern DECL_VBOXDD2(const unsigned)       g_cbEfiFirmwareRiscV32;
extern DECL_VBOXDD2(const unsigned char)  g_abEfiFirmwareRiscV64[];
extern DECL_VBOXDD2(const unsigned)       g_cbEfiFirmwareRiscV64;
# endif
#endif

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_build_VBoxDD2_h */
