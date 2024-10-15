/* $Id: spc-template.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Crypto - Microsoft SPC / Authenticode, Code Generator Template.
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

#define RTASN1TMPL_DECL         RTDECL


/*
 * One SPC Serialized Page Hashes V2 Object.
 */
#define RTASN1TMPL_TYPE         RTCRSPCSERIALIZEDPAGEHASHES
#define RTASN1TMPL_EXT_NAME     RTCrSpcSerializedPageHashes
#define RTASN1TMPL_INT_NAME     rtCrSpcSerializedPageHashes
RTASN1TMPL_BEGIN_SETCORE();
RTASN1TMPL_MEMBER(              RawData,               RTASN1OCTETSTRING,           RTAsn1OctetString);
RTASN1TMPL_EXEC_DECODE(         rc = RTCrSpcSerializedPageHashes_UpdateDerivedData(pThis) ) /* no ; */
RTASN1TMPL_EXEC_CLONE(          rc = RTCrSpcSerializedPageHashes_UpdateDerivedData(pThis) ) /* no ; */
RTASN1TMPL_END_SETCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * One SPC Serialized Object Attribute.
 */
#define RTASN1TMPL_TYPE         RTCRSPCSERIALIZEDOBJECTATTRIBUTE
#define RTASN1TMPL_EXT_NAME     RTCrSpcSerializedObjectAttribute
#define RTASN1TMPL_INT_NAME     rtCrSpcSerializedObjectAttribute
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              Type,               RTASN1OBJID,                    RTAsn1ObjId);
RTASN1TMPL_MEMBER_DYN_BEGIN(    Type, RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE, enmType, Allocation);
RTASN1TMPL_MEMBER_DYN(          u, pPageHashes, V1Hashes,   RTCRSPCSERIALIZEDPAGEHASHES, RTCrSpcSerializedPageHashes, Allocation,
    Type, enmType, RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_PAGE_HASHES_V1, RTCRSPC_PE_IMAGE_HASHES_V1_OID);
RTASN1TMPL_MEMBER_DYN(          u, pPageHashes, V2Hashes,   RTCRSPCSERIALIZEDPAGEHASHES, RTCrSpcSerializedPageHashes, Allocation,
    Type, enmType, RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_PAGE_HASHES_V2, RTCRSPC_PE_IMAGE_HASHES_V2_OID);
RTASN1TMPL_MEMBER_DYN_DEFAULT(  u, pCore,                   RTASN1CORE, RTAsn1Core, Allocation,
    Type, enmType, RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE_UNKNOWN);
RTASN1TMPL_MEMBER_DYN_END(      Type, RTCRSPCSERIALIZEDOBJECTATTRIBUTETYPE, enmType, Allocation);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME

/*
 * Set of SPC Serialized Object Attributes.
 */
#define RTASN1TMPL_TYPE         RTCRSPCSERIALIZEDOBJECTATTRIBUTES
#define RTASN1TMPL_EXT_NAME     RTCrSpcSerializedObjectAttributes
#define RTASN1TMPL_INT_NAME     rtCrSpcSerializedObjectAttributes
RTASN1TMPL_SET_OF(RTCRSPCSERIALIZEDOBJECTATTRIBUTE, RTCrSpcSerializedObjectAttribute);
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * One SPC Serialized Object.
 */
#define RTASN1TMPL_TYPE         RTCRSPCSERIALIZEDOBJECT
#define RTASN1TMPL_EXT_NAME     RTCrSpcSerializedObject
#define RTASN1TMPL_INT_NAME     rtCrSpcSerializedObject
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER_EX(           Uuid,               RTASN1OCTETSTRING,              RTAsn1OctetString,
                                RTASN1TMPL_MEMBER_CONSTR_MIN_MAX(Uuid, RTASN1OCTETSTRING, RTAsn1OctetString, 16, 16, RT_NOTHING));
RTASN1TMPL_MEMBER(              SerializedData,     RTASN1OCTETSTRING,              RTAsn1OctetString);
RTASN1TMPL_EXEC_DECODE(         rc = rtCrSpcSerializedObject_DecodeMore(pCursor, fFlags, pThis, pszErrorTag) ) /* no ; */
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * Choosy SPC strings.
 */
#define RTASN1TMPL_TYPE         RTCRSPCSTRING
#define RTASN1TMPL_EXT_NAME     RTCrSpcString
#define RTASN1TMPL_INT_NAME     rtCrSpcString
RTASN1TMPL_BEGIN_PCHOICE();
RTASN1TMPL_PCHOICE_ITAG_CP(     0, RTCRSPCSTRINGCHOICE_UCS2,     u.pUcs2,   Ucs2,   RTASN1STRING, RTAsn1BmpString);
RTASN1TMPL_PCHOICE_ITAG_CP(     1, RTCRSPCSTRINGCHOICE_ASCII,    u.pAscii,  Ascii,  RTASN1STRING, RTAsn1Ia5String);
RTASN1TMPL_END_PCHOICE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * SPC Link.
 */
#define RTASN1TMPL_TYPE         RTCRSPCLINK
#define RTASN1TMPL_EXT_NAME     RTCrSpcLink
#define RTASN1TMPL_INT_NAME     rtCrSpcLink
RTASN1TMPL_BEGIN_PCHOICE();
RTASN1TMPL_PCHOICE_ITAG_CP(     0, RTCRSPCLINKCHOICE_URL,     u.pUrl,     Url,      RTASN1STRING,            RTAsn1Ia5String);
RTASN1TMPL_PCHOICE_ITAG(        1, RTCRSPCLINKCHOICE_MONIKER, u.pMoniker, Moniker,  RTCRSPCSERIALIZEDOBJECT, RTCrSpcSerializedObject);
RTASN1TMPL_PCHOICE_XTAG(        2, RTCRSPCLINKCHOICE_FILE,    u.pT2, CtxTag2, File, RTCRSPCSTRING,           RTCrSpcString);
RTASN1TMPL_END_PCHOICE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * SPC PE Image Data.
 *
 * Note! This is not correctly declared in available specifications. The file
 *       member is tagged.  Seeing the '--#public--' comment in the specs,
 *       one can't only guess that there are other alternatives in that part
 *       of the structure that microsoft does not wish to document.
 */
#define RTASN1TMPL_TYPE         RTCRSPCPEIMAGEDATA
#define RTASN1TMPL_EXT_NAME     RTCrSpcPeImageData
#define RTASN1TMPL_INT_NAME     rtCrSpcPeImageData
RTASN1TMPL_BEGIN_SEQCORE();
/** @todo The flags defaults to includeResources. Could be expressed here rather
 *        than left to the user to deal with. */
RTASN1TMPL_MEMBER_OPT_ITAG_EX(  Flags,              RTASN1BITSTRING, RTAsn1BitString, ASN1_TAG_BIT_STRING, RTASN1TMPL_ITAG_F_UP,
                                RTASN1TMPL_MEMBER_CONSTR_BITSTRING_MIN_MAX(Flags, 0, 3, RT_NOTHING));
RTASN1TMPL_MEMBER_OPT_XTAG_EX(  T0, CtxTag0, File,  RTCRSPCLINK,     RTCrSpcLink, 0, \
                                RTASN1TMPL_MEMBER_CONSTR_PRESENT(T0.File, RTCrSpcLink, RT_NOTHING));
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * SPC Attribute Type And Optional Value.
 *
 * Note! The value doesn't look very optional in available examples and specs.
 *       The available specs also claim there is an explicit 0 tag around the
 *       data, which isn't there is in signed executables.  Gotta love Microsoft...
 */
#define RTASN1TMPL_TYPE         RTCRSPCATTRIBUTETYPEANDOPTIONALVALUE
#define RTASN1TMPL_EXT_NAME     RTCrSpcAttributeTypeAndOptionalValue
#define RTASN1TMPL_INT_NAME     rtCrSpcAttributeTypeAndOptionalValue
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              Type,               RTASN1OBJID,                    RTAsn1ObjId);
RTASN1TMPL_MEMBER_DYN_BEGIN(    Type, RTCRSPCAAOVTYPE, enmType, Allocation);
RTASN1TMPL_MEMBER_DYN(          uValue, pPeImage, PeImage, RTCRSPCPEIMAGEDATA, RTCrSpcPeImageData, Allocation,
    Type, enmType, RTCRSPCAAOVTYPE_PE_IMAGE_DATA, RTCRSPCPEIMAGEDATA_OID);
RTASN1TMPL_MEMBER_DYN_DEFAULT(  uValue, pCore,             RTASN1CORE,         RTAsn1Core, Allocation,
    Type, enmType, RTCRSPCAAOVTYPE_UNKNOWN);
RTASN1TMPL_MEMBER_DYN_END(      Type, RTCRSPCAAOVTYPE, enmType, Allocation);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME


/*
 * SPC Indirect Data Content.
 */
#define RTASN1TMPL_TYPE         RTCRSPCINDIRECTDATACONTENT
#define RTASN1TMPL_EXT_NAME     RTCrSpcIndirectDataContent
#define RTASN1TMPL_INT_NAME     rtCrSpcIndirectDataContent
RTASN1TMPL_BEGIN_SEQCORE();
RTASN1TMPL_MEMBER(              Data,               RTCRSPCATTRIBUTETYPEANDOPTIONALVALUE,  RTCrSpcAttributeTypeAndOptionalValue);
RTASN1TMPL_MEMBER(              DigestInfo,         RTCRPKCS7DIGESTINFO,                   RTCrPkcs7DigestInfo);
RTASN1TMPL_END_SEQCORE();
#undef RTASN1TMPL_TYPE
#undef RTASN1TMPL_EXT_NAME
#undef RTASN1TMPL_INT_NAME

