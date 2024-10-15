/* $Id: VBoxStubBld.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxStubBld - VirtualBox's Windows installer stub builder.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_StubBld_VBoxStubBld_h
#define VBOX_INCLUDED_SRC_StubBld_VBoxStubBld_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>

/**/
#define VBOXSTUB_MAX_PACKAGES 128

/** */
#define VBOXSTUBPKGHEADER_MAGIC_SZ  "VBoxInstV1\0\0\0\0"

/**
 * VBox installer stub header, aka "MANIFEST".
 *
 * This just holds the number of packages present in the image.
 */
typedef struct VBOXSTUBPKGHEADER
{
    /** Magic value/string (VBOXSTUBPKGHEADER_MAGIC_SZ) */
    char    szMagic[11 + 4];
    /** Number of packages following the header. */
    uint8_t cPackages;
} VBOXSTUBPKGHEADER;
AssertCompileSize(VBOXSTUBPKGHEADER, 16);
typedef VBOXSTUBPKGHEADER *PVBOXSTUBPKGHEADER;

typedef enum VBOXSTUBPKGARCH
{
    /** Always extract.   */
    VBOXSTUBPKGARCH_ALL = 1,
    /** Extract on x86 hosts. */
    VBOXSTUBPKGARCH_X86,
    /** Extract on AMD64 hosts. */
    VBOXSTUBPKGARCH_AMD64
} VBOXSTUBPKGARCH;

/**
 * Package header/descriptor.
 *
 * This is found as "HDR_xx" where xx is replaced by the decimal package number,
 * zero padded to two digits.
 */
typedef struct VBOXSTUBPKG
{
    /** The architecture for the file. */
    VBOXSTUBPKGARCH enmArch;
    /** The name of the resource holding the file bytes.
     * This is a pointless field, because the resource name is "BIN_xx"
     * corresponding to the name of the resource containing this struct. */
    char szResourceName[28];
    /** The filename.   */
    char szFilename[224];
} VBOXSTUBPKG;
AssertCompileSize(VBOXSTUBPKG, 256);
typedef VBOXSTUBPKG *PVBOXSTUBPKG;

#endif /* !VBOX_INCLUDED_SRC_StubBld_VBoxStubBld_h */
