/* $Id: efi-signature.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT, EFI signature database definitions.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_formats_efi_signature_h
#define IPRT_INCLUDED_formats_efi_signature_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/types.h>
#include <iprt/assertcompile.h>
#include <iprt/formats/efi-common.h>


/*
 * Definitions come from the UEFI 2.6 specification, chapter 30.4.1
 */

/** The GUID used for setting and retrieving variables from the variable store. */
#define EFI_IMAGE_SECURITY_DATABASE_GUID \
    { 0xd719b2cb, 0x3d3a, 0x4596, { 0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f }}
/** The GUID used for setting and retrieving the MOK (Machine Owner Key) from the variable store. */
#define EFI_IMAGE_MOK_DATABASE_GUID \
    { 0x605dab50, 0xe046, 0x4300, { 0xab, 0xb6, 0x3d, 0xd8, 0x10, 0xdd, 0x8b, 0x23 }}


/**
 * Signature entry data.
 */
typedef struct EFI_SIGNATURE_DATA
{
    /** The GUID of the owner of the signature. */
    EFI_GUID                GuidOwner;
    /** The signature data follows (size varies depending on the signature type). */
} EFI_SIGNATURE_DATA;
AssertCompileSize(EFI_SIGNATURE_DATA, 16);
/** Pointer to a signature entry. */
typedef EFI_SIGNATURE_DATA *PEFI_SIGNATURE_DATA;
/** Pointer to a const signature entry. */
typedef const EFI_SIGNATURE_DATA *PCEFI_SIGNATURE_DATA;

/** Microsoft's GUID for signatures. */
#define EFI_SIGNATURE_OWNER_GUID_MICROSOFT \
    { 0x77fa9abd, 0x0359, 0x4d32, { 0xbd, 0x60, 0x28, 0xf4, 0xe7, 0x8f, 0x78, 0x4b }}

/** VirtualBox's GUID for signatures. */
#define EFI_SIGNATURE_OWNER_GUID_VBOX \
    { 0x9400896a, 0x146c, 0x4f4c, { 0x96, 0x47, 0x2c, 0x73, 0x62, 0x0c, 0xa8, 0x94 }}


/**
 * Signature list header.
 */
typedef struct EFI_SIGNATURE_LIST
{
    /** The signature type stored in this list. */
    EFI_GUID                GuidSigType;
    /** Size of the signature list in bytes. */
    uint32_t                cbSigLst;
    /** Size of the optional signature header following this header in bytes. */
    uint32_t                cbSigHdr;
    /** Size of each signature entry in bytes, must be at least the size of EFI_SIGNATURE_DATA. */
    uint32_t                cbSig;
    // uint8_t              abSigHdr[];
    // EFI_SIGNATURE_DATA   aSigs[];
} EFI_SIGNATURE_LIST;
AssertCompileSize(EFI_SIGNATURE_LIST, 28);
/** Pointer to a signature list header. */
typedef EFI_SIGNATURE_LIST *PEFI_SIGNATURE_LIST;
/** Pointer to a const signature list header. */
typedef const EFI_SIGNATURE_LIST *PCEFI_SIGNATURE_LIST;

/** Signature contains a SHA256 hash. */
#define EFI_SIGNATURE_TYPE_GUID_SHA256 \
    { 0xc1c41626, 0x504c, 0x4092, { 0xac, 0xa9, 0x41, 0xf9, 0x36, 0x93, 0x43, 0x28 }}
/** Size of a SHA256 signature entry (GUID + 32 bytes for the hash). */
#define EFI_SIGNATURE_TYPE_SZ_SHA256            UINT32_C(48)

/** Signature contains a RSA2048 key. */
#define EFI_SIGNATURE_TYPE_GUID_RSA2048 \
    { 0x3c5766e8, 0x269c, 0x4e34, { 0xaa, 0x14, 0xed, 0x77, 0x6e, 0x85, 0xb3, 0xb6 }}
/** Size of a RSA2048 signature entry (GUID + 256 for the key). */
#define EFI_SIGNATURE_TYPE_SZ_RSA2048           UINT32_C(272)

/** Signature contains a RSA2048 signature of a SHA256 hash. */
#define EFI_SIGNATURE_TYPE_GUID_RSA2048_SHA256 \
    { 0xe2b36190, 0x879b, 0x4a3d, { 0xad, 0x8d, 0xf2, 0xe7, 0xbb, 0xa3, 0x27, 0x84 }}
/** Size of a RSA2048 signature entry (GUID + 256 for the key). */
#define EFI_SIGNATURE_TYPE_SZ_RSA2048_SHA256    UINT32_C(272)

/** Signature contains a SHA1 hash. */
#define EFI_SIGNATURE_TYPE_GUID_SHA1 \
    { 0x826ca512, 0xcf10, 0x4ac9, { 0xb1, 0x87, 0xbe, 0x01, 0x49, 0x66, 0x31, 0xbd }}
/** Size of a SHA1 signature entry (GUID + 20 bytes for the hash). */
#define EFI_SIGNATURE_TYPE_SZ_SHA1              UINT32_C(36)

/** Signature contains a RSA2048 signature of a SHA1 hash. */
#define EFI_SIGNATURE_TYPE_GUID_RSA2048_SHA1 \
    { 0x67f8444f, 0x8743, 0x48f1, { 0xa3, 0x28, 0x1e, 0xaa, 0xb8, 0x73, 0x60, 0x80 }}
/** Size of a RSA2048 signature entry (GUID + 256 for the key). */
#define EFI_SIGNATURE_TYPE_SZ_RSA2048_SHA1      UINT32_C(272)

/** Signature contains a DER encoded X.509 certificate (size varies with each certificate). */
#define EFI_SIGNATURE_TYPE_GUID_X509 \
    { 0xa5c059a1, 0x94e4, 0x4aa7, { 0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72 }}

#endif /* !IPRT_INCLUDED_formats_efi_signature_h */

