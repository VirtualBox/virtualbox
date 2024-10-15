/* $Id: VBoxDD2.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Built-in drivers & devices part 2 header.
 *
 * These drivers and devices are in separate modules because of LGPL.
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

#ifndef VBOX_INCLUDED_SRC_build_VBoxDD2_h
#define VBOX_INCLUDED_SRC_build_VBoxDD2_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/pdm.h>

RT_C_DECLS_BEGIN

#ifdef IN_VBOXDD2
extern DECLEXPORT(const unsigned char)  g_abPcBiosBinary386[];
extern DECLEXPORT(const unsigned)       g_cbPcBiosBinary386;
extern DECLEXPORT(const unsigned char)  g_abPcBiosBinary286[];
extern DECLEXPORT(const unsigned)       g_cbPcBiosBinary286;
extern DECLEXPORT(const unsigned char)  g_abPcBiosBinary8086[];
extern DECLEXPORT(const unsigned)       g_cbPcBiosBinary8086;
extern DECLEXPORT(const unsigned char)  g_abVgaBiosBinary386[];
extern DECLEXPORT(const unsigned)       g_cbVgaBiosBinary386;
extern DECLEXPORT(const unsigned char)  g_abVgaBiosBinary286[];
extern DECLEXPORT(const unsigned)       g_cbVgaBiosBinary286;
extern DECLEXPORT(const unsigned char)  g_abVgaBiosBinary8086[];
extern DECLEXPORT(const unsigned)       g_cbVgaBiosBinary8086;
# ifdef VBOX_WITH_PXE_ROM
extern DECLEXPORT(const unsigned char)  g_abNetBiosBinary[];
extern DECLEXPORT(const unsigned)       g_cbNetBiosBinary;
# endif
# ifdef VBOX_WITH_EFI_IN_DD2
extern DECLEXPORT(const unsigned char)  g_abEfiFirmware32[];
extern DECLEXPORT(const unsigned)       g_cbEfiFirmware32;
extern DECLEXPORT(const unsigned char)  g_abEfiFirmware64[];
extern DECLEXPORT(const unsigned)       g_cbEfiFirmware64;
#  ifdef VBOX_WITH_VIRT_ARMV8
extern DECLEXPORT(const unsigned char)  g_abEfiFirmwareAArch32[];
extern DECLEXPORT(const unsigned)       g_cbEfiFirmwareAArch32;
extern DECLEXPORT(const unsigned char)  g_abEfiFirmwareAArch64[];
extern DECLEXPORT(const unsigned)       g_cbEfiFirmwareAArch64;
#  endif
# endif
#else  /* !IN_VBOXDD2 */
extern DECLIMPORT(const unsigned char)  g_abPcBiosBinary386[];
extern DECLIMPORT(const unsigned)       g_cbPcBiosBinary386;
extern DECLIMPORT(const unsigned char)  g_abPcBiosBinary286[];
extern DECLIMPORT(const unsigned)       g_cbPcBiosBinary286;
extern DECLIMPORT(const unsigned char)  g_abPcBiosBinary8086[];
extern DECLIMPORT(const unsigned)       g_cbPcBiosBinary8086;
extern DECLIMPORT(const unsigned char)  g_abVgaBiosBinary386[];
extern DECLIMPORT(const unsigned)       g_cbVgaBiosBinary386;
extern DECLIMPORT(const unsigned char)  g_abVgaBiosBinary286[];
extern DECLIMPORT(const unsigned)       g_cbVgaBiosBinary286;
extern DECLIMPORT(const unsigned char)  g_abVgaBiosBinary8086[];
extern DECLIMPORT(const unsigned)       g_cbVgaBiosBinary8086;
# ifdef VBOX_WITH_PXE_ROM
extern DECLIMPORT(const unsigned char)  g_abNetBiosBinary[];
extern DECLIMPORT(const unsigned)       g_cbNetBiosBinary;
# endif
# ifdef VBOX_WITH_EFI_IN_DD2
extern DECLIMPORT(const unsigned char)  g_abEfiFirmware32[];
extern DECLIMPORT(const unsigned)       g_cbEfiFirmware32;
extern DECLIMPORT(const unsigned char)  g_abEfiFirmware64[];
extern DECLIMPORT(const unsigned)       g_cbEfiFirmware64;
#  ifdef VBOX_WITH_VIRT_ARMV8
extern DECLIMPORT(const unsigned char)  g_abEfiFirmwareAArch32[];
extern DECLIMPORT(const unsigned)       g_cbEfiFirmwareAArch32;
extern DECLIMPORT(const unsigned char)  g_abEfiFirmwareAArch64[];
extern DECLIMPORT(const unsigned)       g_cbEfiFirmwareAArch64;
#  endif
# endif
#endif /* !IN_VBOXDD2 */

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_build_VBoxDD2_h */
