/** @file
 * VirtualBox - Global Guest Operating System definition.
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

#ifndef VBOX_INCLUDED_ostypes_h
#define VBOX_INCLUDED_ostypes_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>

RT_C_DECLS_BEGIN

/**
 * Global list of guest operating system types.
 *
 * New entries can be added, however other components depend on the values (e.g.
 * the guest additions) so the existing values MUST stay the same.
 *
 * Summary of the value bits:
 *      - bits 31:24 - 0xff000000 - Reserved; more OS version bits if needed.
 *      - bits 23:16 - 0x00ff0000 - OS family.
 *      - bits 15:12 - 0x0000f000 - Major OS version or (Linux) distro.
 *      - bit  11    - 0x00000800 - Should've been arch, only use by WinNT31.
 *      - bits 10:08 - 0x00000700 - Architecture.
 *      - bits 07:00 - 0x000000ff - Major distro release (Linux), or more bits
 *                                  for major OS version.
 *
 * They are grouped into families.  A family identifer is always has
 * mod 0x10000 == 0.  The bits making up the family component is given by
 * VBOXOSTYPE_OsFamilyMask, as we probably won't need 32K OS families and
 * currently restrict ourselves to 256 (currently only 14 + unknown have been
 * defined).
 *
 * The VBOXOSTYPE_OsTypeMask value contains the OS family and the major OS
 * version / distro. So use with care.
 *
 *
 * The VBOXOSTYPE_OsMask value contains all bits except for the architecture.
 * Use this and VBOXOSTYPE_ArchitectureMask for morphing the architecture of an
 * specific OS type.
 *
 * The subdivision of the OS type bits other than the family (and architecture),
 * i.e. VBOXOSTYPE_OsTypeMask & ~VBOXOSTYPE_OsFamilyMask, are specific to each
 * OS family.  For Linux the 0xf000 mask currently identifies the distro and
 * 0xff the major release of that distro in most cases.  While for most other
 * families 0xf000 is used for identifying major OS releases (Windows is out of
 * space with Windows 11, so will have to think of something new there when/if
 * the next major release is out).
 *
 * @note    Do NOT use bit 11 (0x800) for anything. It is currently only used by
 *          VBOXOSTYPE_WinNT3x and we'd like to keep it that way in case we ever
 *          end up needing more than three new architectures.
 *
 * @note    Cleaning up the value structure here is a bit late.  The problem is
 *          the use of this type in VBoxGuestInfo (VMMDevReq_ReportGuestInfo).
 *          Any radical change here would require a new version (v3) of that
 *          request and a safe protocol for dealing with backwards / forwards
 *          compatiblity on both sides.  Then we need a very thoughful
 *          partitioning of the value bits, picking new constants and mapping
 *          from old to new types (we only need the one direction).
 *
 * @todo    r=bird: A more sensible layout of the bits would, imo be:
 *              - bit  31    - 0x80000000 - the sign bit, forever unused.
 *              - bits 30:24 - 0x7f000000 - OS family
 *              - bits 23:16 - 0x00ff0000 - OS sub-family / distro.
 *              - bits 15:08 - 0x0000ff00 - OS major release.
 *              - bits 07:04 - 0x000000f0 - Reserved for future hacks.
 *              - bits 03:00 - 0x0000000f - Architecture.
 *          Basic conversion could be done by value shifting, though using a map
 *          with known values would allow for more thorough cleanup.
 */
typedef enum VBOXOSTYPE
{
    VBOXOSTYPE_Unknown          = 0x00000,
    VBOXOSTYPE_Unknown_x64      = 0x00100,
    VBOXOSTYPE_Unknown_arm32    = 0x00200,
    VBOXOSTYPE_Unknown_arm64    = 0x00300,

    /** @name DOS and it's descendants
     * @{ */
    VBOXOSTYPE_DOS              = 0x10000,
    VBOXOSTYPE_Win31            = 0x15000,

    VBOXOSTYPE_Win9x            = 0x20000,
    VBOXOSTYPE_Win95            = 0x21000,
    VBOXOSTYPE_Win98            = 0x22000,
    VBOXOSTYPE_WinMe            = 0x23000,

    VBOXOSTYPE_WinNT            = 0x30000,
    VBOXOSTYPE_WinNT_x64        = 0x30100,
    VBOXOSTYPE_WinNT3x          = 0x30800,
    VBOXOSTYPE_WinNT4           = 0x31000,
    VBOXOSTYPE_Win2k            = 0x32000,
    VBOXOSTYPE_WinXP            = 0x33000,
    VBOXOSTYPE_WinXP_x64        = 0x33100,
    VBOXOSTYPE_Win2k3           = 0x34000,
    VBOXOSTYPE_Win2k3_x64       = 0x34100,
    VBOXOSTYPE_WinVista         = 0x35000,
    VBOXOSTYPE_WinVista_x64     = 0x35100,
    VBOXOSTYPE_Win2k8           = 0x36000,
    VBOXOSTYPE_Win2k8_x64       = 0x36100,
    VBOXOSTYPE_Win7             = 0x37000,
    VBOXOSTYPE_Win7_x64         = 0x37100,
    VBOXOSTYPE_Win8             = 0x38000,
    VBOXOSTYPE_Win8_x64         = 0x38100,
    VBOXOSTYPE_Win2k12_x64      = 0x39100,
    VBOXOSTYPE_Win81            = 0x3A000,
    VBOXOSTYPE_Win81_x64        = 0x3A100,
    VBOXOSTYPE_Win10            = 0x3B000,
    VBOXOSTYPE_Win10_x64        = 0x3B100,
    VBOXOSTYPE_Win2k16_x64      = 0x3C100,
    VBOXOSTYPE_Win2k19_x64      = 0x3D100,
    VBOXOSTYPE_Win11_x64        = 0x3E100,
    VBOXOSTYPE_Win2k22_x64      = 0x3F100,
    VBOXOSTYPE_Win2k25_x64      = 0x3F101,

    VBOXOSTYPE_OS2              = 0x40000,
    VBOXOSTYPE_OS2Warp3         = 0x41000,
    VBOXOSTYPE_OS2Warp4         = 0x42000,
    VBOXOSTYPE_OS2Warp45        = 0x43000,
    VBOXOSTYPE_ECS              = 0x44000,
    VBOXOSTYPE_ArcaOS           = 0x45000,
    VBOXOSTYPE_OS21x            = 0x48000,
    /** @} */
    /** @name Linux
     * @{ */
    VBOXOSTYPE_Linux            = 0x50000,
    VBOXOSTYPE_Linux_x64        = 0x50100,
    VBOXOSTYPE_Linux22          = 0x51000,
    VBOXOSTYPE_Linux24          = 0x52000,
    VBOXOSTYPE_Linux24_x64      = 0x52100,
    VBOXOSTYPE_Linux26          = 0x53000,
    VBOXOSTYPE_Linux26_x64      = 0x53100,
    VBOXOSTYPE_ArchLinux        = 0x54000,
    VBOXOSTYPE_ArchLinux_x64    = 0x54100,
    VBOXOSTYPE_ArchLinux_arm64  = 0x54300,
    VBOXOSTYPE_Debian           = 0x55000,
    VBOXOSTYPE_Debian_x64       = 0x55100,
    VBOXOSTYPE_Debian_arm32     = 0x55200,
    VBOXOSTYPE_Debian_arm64     = 0x55300,
    VBOXOSTYPE_Debian31         = 0x55001,  // 32-bit only
    VBOXOSTYPE_Debian4          = 0x55002,
    VBOXOSTYPE_Debian4_x64      = 0x55102,
    VBOXOSTYPE_Debian5          = 0x55003,
    VBOXOSTYPE_Debian5_x64      = 0x55103,
    VBOXOSTYPE_Debian6          = 0x55004,
    VBOXOSTYPE_Debian6_x64      = 0x55104,
    VBOXOSTYPE_Debian7          = 0x55005,
    VBOXOSTYPE_Debian7_x64      = 0x55105,
    VBOXOSTYPE_Debian8          = 0x55006,
    VBOXOSTYPE_Debian8_x64      = 0x55106,
    VBOXOSTYPE_Debian9          = 0x55007,
    VBOXOSTYPE_Debian9_x64      = 0x55107,
    VBOXOSTYPE_Debian9_arm64    = 0x55307,
    VBOXOSTYPE_Debian10         = 0x55008,
    VBOXOSTYPE_Debian10_x64     = 0x55108,
    VBOXOSTYPE_Debian10_arm64   = 0x55308,
    VBOXOSTYPE_Debian11         = 0x55009,
    VBOXOSTYPE_Debian11_x64     = 0x55109,
    VBOXOSTYPE_Debian11_arm64   = 0x55309,
    VBOXOSTYPE_Debian12         = 0x5500a,
    VBOXOSTYPE_Debian12_x64     = 0x5510a,
    VBOXOSTYPE_Debian12_arm64   = 0x5530a,
    VBOXOSTYPE_Debian_latest_x64 = VBOXOSTYPE_Debian12_x64,
    VBOXOSTYPE_Debian_latest_arm64 = VBOXOSTYPE_Debian12_arm64,
    VBOXOSTYPE_OpenSUSE         = 0x56000,
    VBOXOSTYPE_OpenSUSE_x64     = 0x56100,
    VBOXOSTYPE_OpenSUSE_Leap_x64         = 0x56101,  // 64-bit only
    VBOXOSTYPE_OpenSUSE_Leap_arm64       = 0x56301,
    VBOXOSTYPE_OpenSUSE_Tumbleweed       = 0x56002,
    VBOXOSTYPE_OpenSUSE_Tumbleweed_x64   = 0x56102,
    VBOXOSTYPE_OpenSUSE_Tumbleweed_arm64 = 0x56302,
    VBOXOSTYPE_SUSE_LE          = 0x56003,
    VBOXOSTYPE_SUSE_LE_x64      = 0x56103,
    VBOXOSTYPE_FedoraCore       = 0x57000,
    VBOXOSTYPE_FedoraCore_x64   = 0x57100,
    VBOXOSTYPE_FedoraCore_arm64 = 0x57300,
    VBOXOSTYPE_Gentoo           = 0x58000,
    VBOXOSTYPE_Gentoo_x64       = 0x58100,
    VBOXOSTYPE_Mandriva         = 0x59000,
    VBOXOSTYPE_Mandriva_x64     = 0x59100,
    VBOXOSTYPE_OpenMandriva_Lx  = 0x59001,
    VBOXOSTYPE_OpenMandriva_Lx_x64 = 0x59101,
    VBOXOSTYPE_PCLinuxOS        = 0x59002,
    VBOXOSTYPE_PCLinuxOS_x64    = 0x59102,
    VBOXOSTYPE_Mageia           = 0x59003,
    VBOXOSTYPE_Mageia_x64       = 0x59103,
    VBOXOSTYPE_RedHat           = 0x5A000,
    VBOXOSTYPE_RedHat_x64       = 0x5A100,
    VBOXOSTYPE_RedHat_arm32     = 0x5A200,
    VBOXOSTYPE_RedHat_arm64     = 0x5A300,
    VBOXOSTYPE_RedHat3          = 0x5A001,
    VBOXOSTYPE_RedHat3_x64      = 0x5A101,
    VBOXOSTYPE_RedHat4          = 0x5A002,
    VBOXOSTYPE_RedHat4_x64      = 0x5A102,
    VBOXOSTYPE_RedHat5          = 0x5A003,
    VBOXOSTYPE_RedHat5_x64      = 0x5A103,
    VBOXOSTYPE_RedHat6          = 0x5A004,
    VBOXOSTYPE_RedHat6_x64      = 0x5A104,
    VBOXOSTYPE_RedHat7_x64      = 0x5A105,  // 64-bit only
    VBOXOSTYPE_RedHat8_x64      = 0x5A106,  // 64-bit only
    VBOXOSTYPE_RedHat9_x64      = 0x5A107,  // 64-bit only
    VBOXOSTYPE_RedHat_latest_x64 = VBOXOSTYPE_RedHat9_x64,
    VBOXOSTYPE_Turbolinux       = 0x5B000,
    VBOXOSTYPE_Turbolinux_x64   = 0x5B100,
    VBOXOSTYPE_Ubuntu           = 0x5C000,
    VBOXOSTYPE_Ubuntu_x64       = 0x5C100,
    VBOXOSTYPE_Ubuntu_arm32     = 0x5C200,
    VBOXOSTYPE_Ubuntu_arm64     = 0x5C300,
    VBOXOSTYPE_Xubuntu          = 0x5C001,
    VBOXOSTYPE_Xubuntu_x64      = 0x5C101,
    VBOXOSTYPE_Lubuntu          = 0x5C002,
    VBOXOSTYPE_Lubuntu_x64      = 0x5C102,
    VBOXOSTYPE_Ubuntu10_LTS     = 0x5C003,
    VBOXOSTYPE_Ubuntu10_LTS_x64 = 0x5C103,
    VBOXOSTYPE_Ubuntu10         = 0x5C004,
    VBOXOSTYPE_Ubuntu10_x64     = 0x5C104,
    VBOXOSTYPE_Ubuntu11         = 0x5C005,
    VBOXOSTYPE_Ubuntu11_x64     = 0x5C105,
    VBOXOSTYPE_Ubuntu12_LTS     = 0x5C006,
    VBOXOSTYPE_Ubuntu12_LTS_x64 = 0x5C106,
    VBOXOSTYPE_Ubuntu12         = 0x5C007,
    VBOXOSTYPE_Ubuntu12_x64     = 0x5C107,
    VBOXOSTYPE_Ubuntu13         = 0x5C008,
    VBOXOSTYPE_Ubuntu13_x64     = 0x5C108,
    VBOXOSTYPE_Ubuntu14_LTS     = 0x5C009,
    VBOXOSTYPE_Ubuntu14_LTS_x64 = 0x5C109,
    VBOXOSTYPE_Ubuntu14         = 0x5C00a,
    VBOXOSTYPE_Ubuntu14_x64     = 0x5C10a,
    VBOXOSTYPE_Ubuntu15         = 0x5C00b,
    VBOXOSTYPE_Ubuntu15_x64     = 0x5C10b,
    VBOXOSTYPE_Ubuntu16_LTS     = 0x5C00c,
    VBOXOSTYPE_Ubuntu16_LTS_x64 = 0x5C10c,
    VBOXOSTYPE_Ubuntu16         = 0x5C00d,
    VBOXOSTYPE_Ubuntu16_x64     = 0x5C10d,
    VBOXOSTYPE_Ubuntu17         = 0x5C00e,
    VBOXOSTYPE_Ubuntu17_x64     = 0x5C10e,
    VBOXOSTYPE_Ubuntu18_LTS     = 0x5C00f,
    VBOXOSTYPE_Ubuntu18_LTS_x64 = 0x5C10f,
    VBOXOSTYPE_Ubuntu18         = 0x5C010,
    VBOXOSTYPE_Ubuntu18_x64     = 0x5C110,
    VBOXOSTYPE_Ubuntu19         = 0x5C011,
    VBOXOSTYPE_Ubuntu19_x64     = 0x5C111,
    VBOXOSTYPE_Ubuntu20_LTS_x64 = 0x5C112,  // 64-bit only
    VBOXOSTYPE_Ubuntu20_x64     = 0x5C113,  // 64-bit only
    VBOXOSTYPE_Ubuntu21_x64     = 0x5C114,  // 64-bit only
    VBOXOSTYPE_Ubuntu22_LTS_x64 = 0x5C115,  // 64-bit only
    VBOXOSTYPE_Ubuntu22_x64     = 0x5C116,  // 64-bit only
    VBOXOSTYPE_Ubuntu22_arm64   = 0x5C316,  // 64-bit only
    VBOXOSTYPE_Ubuntu23_x64     = 0x5C117,  // 64-bit only
    VBOXOSTYPE_Ubuntu23_arm64   = 0x5C317,  // 64-bit only
    VBOXOSTYPE_Ubuntu24_LTS_x64 = 0x5C118,  // 64-bit only
    VBOXOSTYPE_Ubuntu24_LTS_arm64 = 0x5C318,  // 64-bit only
    VBOXOSTYPE_Ubuntu_latest_x64 = VBOXOSTYPE_Ubuntu24_LTS_x64,
    VBOXOSTYPE_Ubuntu_latest_arm64 = VBOXOSTYPE_Ubuntu24_LTS_arm64,
    VBOXOSTYPE_Xandros          = 0x5D000,
    VBOXOSTYPE_Xandros_x64      = 0x5D100,
    VBOXOSTYPE_Oracle           = 0x5E000,
    VBOXOSTYPE_Oracle_x64       = 0x5E100,
    VBOXOSTYPE_Oracle_arm32     = 0x5E200,
    VBOXOSTYPE_Oracle_arm64     = 0x5E300,
    VBOXOSTYPE_Oracle4          = 0x5E001,
    VBOXOSTYPE_Oracle4_x64      = 0x5E101,
    VBOXOSTYPE_Oracle5          = 0x5E002,
    VBOXOSTYPE_Oracle5_x64      = 0x5E102,
    VBOXOSTYPE_Oracle6          = 0x5E003,
    VBOXOSTYPE_Oracle6_x64      = 0x5E103,
    VBOXOSTYPE_Oracle7_x64      = 0x5E104,  // 64-bit only
    VBOXOSTYPE_Oracle8_x64      = 0x5E105,  // 64-bit only
    VBOXOSTYPE_Oracle9_x64      = 0x5E106,  // 64-bit only
    VBOXOSTYPE_Oracle9_arm64    = 0x5E306,
    VBOXOSTYPE_Oracle_latest_x64 = VBOXOSTYPE_Oracle9_x64,
    VBOXOSTYPE_Oracle_latest_arm64 = VBOXOSTYPE_Oracle9_arm64,
    /** @} */
    /** @name BSD and it's descendants
     * @{ */
    VBOXOSTYPE_FreeBSD          = 0x60000,
    VBOXOSTYPE_FreeBSD_x64      = 0x60100,
    VBOXOSTYPE_FreeBSD_arm64    = 0x60300,
    VBOXOSTYPE_OpenBSD          = 0x61000,
    VBOXOSTYPE_OpenBSD_x64      = 0x61100,
    VBOXOSTYPE_OpenBSD_arm64    = 0x61300,
    VBOXOSTYPE_NetBSD           = 0x62000,
    VBOXOSTYPE_NetBSD_x64       = 0x62100,
    VBOXOSTYPE_NetBSD_arm64     = 0x62300,
    /** @} */
    VBOXOSTYPE_Netware          = 0x70000,
    /** @name Solaris
     * @{ */
    VBOXOSTYPE_Solaris          = 0x80000,  // Solaris 10U7 (5/09) and earlier
    VBOXOSTYPE_Solaris_x64      = 0x80100,  // Solaris 10U7 (5/09) and earlier
    VBOXOSTYPE_Solaris10U8_or_later     = 0x80001,
    VBOXOSTYPE_Solaris10U8_or_later_x64 = 0x80101,
    VBOXOSTYPE_OpenSolaris      = 0x81000,
    VBOXOSTYPE_OpenSolaris_x64  = 0x81100,
    VBOXOSTYPE_Solaris11_x64    = 0x82100,
    /** @} */
    VBOXOSTYPE_L4               = 0x90000,
    VBOXOSTYPE_QNX              = 0xA0000,
    /** @name MacOS
     * @{ */
    VBOXOSTYPE_MacOS            = 0xB0000,
    VBOXOSTYPE_MacOS_x64        = 0xB0100,
    VBOXOSTYPE_MacOS106         = 0xB2000,
    VBOXOSTYPE_MacOS106_x64     = 0xB2100,
    VBOXOSTYPE_MacOS107_x64     = 0xB3100,
    VBOXOSTYPE_MacOS108_x64     = 0xB4100,
    VBOXOSTYPE_MacOS109_x64     = 0xB5100,
    VBOXOSTYPE_MacOS1010_x64    = 0xB6100,
    VBOXOSTYPE_MacOS1011_x64    = 0xB7100,
    VBOXOSTYPE_MacOS1012_x64    = 0xB8100,
    VBOXOSTYPE_MacOS1013_x64    = 0xB9100,
    /** @} */
    /** @name Other OSes and stuff
     * @{ */
    VBOXOSTYPE_JRockitVE        = 0xC0000,
    VBOXOSTYPE_Haiku            = 0xD0000,
    VBOXOSTYPE_Haiku_x64        = 0xD0100,
    VBOXOSTYPE_VBoxBS_x64       = 0xE0100,
    /** @} */

    /** OS family.   */
    VBOXOSTYPE_OsFamilyMask  = 0x00ff0000,
    /** OS type mask - family + distro / major version.
     * @note Do not use bit 11 (0x800) ever. It's only used for WinNT3x and that was
     *       a bit unfortunate. If out of bits, start with 24 and up rather than
     *       using bit 11. */
    VBOXOSTYPE_OsTypeMask    = 0x00fff800,
    /** All the OS bits except architecture.
     * Useful for changing the architecture of a value.  */
    VBOXOSTYPE_OsMask        = 0x00fff8ff,

    /** @name Architecture Type
     * @{ */
    /** Mask containing the architecture value. */
    VBOXOSTYPE_ArchitectureMask = 0x00700,
    /** Architecture value for 16-bit and 32-bit x86. */
    VBOXOSTYPE_x86              = 0x00000,
    /** Architecture value for 64-bit x86 (AMD64). */
    VBOXOSTYPE_x64              = 0x00100,
    /** Architecture value for 32-bit ARM. */
    VBOXOSTYPE_arm32            = 0x00200,
    /** Architecture value for 64-bit ARM. */
    VBOXOSTYPE_arm64            = 0x00300,
    /** Architecture value for unknown or unsupported architectures. */
    VBOXOSTYPE_UnknownArch      = 0x00700,
    /** @} */

    /** The usual 32-bit hack. */
    VBOXOSTYPE_32BIT_HACK    = 0x7fffffff
} VBOXOSTYPE;


/**
 * Global list of guest OS families.
 */
typedef enum VBOXOSFAMILY
{
    VBOXOSFAMILY_Unknown          = 0,
    VBOXOSFAMILY_Windows32        = 1,
    VBOXOSFAMILY_Windows64        = 2,
    VBOXOSFAMILY_Linux32          = 3,
    VBOXOSFAMILY_Linux64          = 4,
    VBOXOSFAMILY_FreeBSD32        = 5,
    VBOXOSFAMILY_FreeBSD64        = 6,
    VBOXOSFAMILY_Solaris32        = 7,
    VBOXOSFAMILY_Solaris64        = 8,
    VBOXOSFAMILY_MacOSX32         = 9,
    VBOXOSFAMILY_MacOSX64         = 10,
    /** The usual 32-bit hack. */
    VBOXOSFAMILY_32BIT_HACK = 0x7fffffff
} VBOXOSFAMILY;

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_ostypes_h */
