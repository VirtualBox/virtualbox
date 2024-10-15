/* $Id: asn1-ut-string-template2.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ASN.1, XXX STRING Types, Template for type specific wrappers.
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


RTASN1STRING_IMPL(ASN1_TAG_NUMERIC_STRING,      "NUMERIC STRING",   RTAsn1NumericString)
RTASN1STRING_IMPL(ASN1_TAG_PRINTABLE_STRING,    "PRINTABLE STRING", RTAsn1PrintableString)
RTASN1STRING_IMPL(ASN1_TAG_T61_STRING,          "T61 STRING",       RTAsn1T61String)
RTASN1STRING_IMPL(ASN1_TAG_VIDEOTEX_STRING,     "VIDEOTEX STRING",  RTAsn1VideotexString)
RTASN1STRING_IMPL(ASN1_TAG_VISIBLE_STRING,      "VISIBLE STRING",   RTAsn1VisibleString)
RTASN1STRING_IMPL(ASN1_TAG_IA5_STRING,          "IA5 STRING",       RTAsn1Ia5String)
RTASN1STRING_IMPL(ASN1_TAG_GRAPHIC_STRING,      "GRAPHIC STRING",   RTAsn1GraphicString)
RTASN1STRING_IMPL(ASN1_TAG_GENERAL_STRING,      "GENERAL STRING",   RTAsn1GeneralString)
RTASN1STRING_IMPL(ASN1_TAG_UTF8_STRING,         "UTF8 STRING",      RTAsn1Utf8String)
RTASN1STRING_IMPL(ASN1_TAG_BMP_STRING,          "BMP STRING",       RTAsn1BmpString)
RTASN1STRING_IMPL(ASN1_TAG_UNIVERSAL_STRING,    "UNIVERSAL STRING", RTAsn1UniversalString)

