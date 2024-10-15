/* $Id: ExtPackUtil.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Extension Pack Utilities and definitions, VBoxC, VBoxSVC, ++.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_ExtPackUtil_h
#define MAIN_INCLUDED_ExtPackUtil_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifdef __cplusplus
# include <iprt/cpp/ministring.h>
#endif
#include <iprt/fs.h>
#include <iprt/vfs.h>


/** @name VBOX_EXTPACK_DESCRIPTION_NAME
 * The name of the description file in an extension pack.  */
#define VBOX_EXTPACK_DESCRIPTION_NAME   "ExtPack.xml"
/** @name VBOX_EXTPACK_DESCRIPTION_NAME
 * The name of the manifest file in an extension pack.  */
#define VBOX_EXTPACK_MANIFEST_NAME      "ExtPack.manifest"
/** @name VBOX_EXTPACK_SIGNATURE_NAME
 * The name of the signature file in an extension pack.  */
#define VBOX_EXTPACK_SIGNATURE_NAME     "ExtPack.signature"
/** @name VBOX_EXTPACK_LICENSE_NAME_PREFIX
 * The name prefix of a license file in an extension pack. There can be
 * several license files in a pack, the variations being on locale, language
 * and format (HTML, RTF, plain text). All extension packages shall include
 * a  */
#define VBOX_EXTPACK_LICENSE_NAME_PREFIX "ExtPack-license"
/** @name VBOX_EXTPACK_SUFFIX
 * The suffix of a extension pack tarball. */
#define VBOX_EXTPACK_SUFFIX             ".vbox-extpack"

/** The minimum length (strlen) of a extension pack name. */
#define VBOX_EXTPACK_NAME_MIN_LEN       3
/** The max length (strlen) of a extension pack name. */
#define VBOX_EXTPACK_NAME_MAX_LEN       64

/** The architecture-dependent application data subdirectory where the
 * extension packs are installed.  Relative to RTPathAppPrivateArch. */
#define VBOX_EXTPACK_INSTALL_DIR        "ExtensionPacks"
/** The architecture-independent application data subdirectory where the
 * certificates are installed.  Relative to RTPathAppPrivateNoArch. */
#define VBOX_EXTPACK_CERT_DIR           "ExtPackCertificates"

/** The maximum entry name length.
 * Play short and safe. */
#define VBOX_EXTPACK_MAX_MEMBER_NAME_LENGTH 128


#ifdef __cplusplus

/**
 * Plug-in descriptor.
 */
typedef struct VBOXEXTPACKPLUGINDESC
{
    /** The name. */
    RTCString        strName;
    /** The module name. */
    RTCString        strModule;
    /** The description. */
    RTCString        strDescription;
    /** The frontend or component which it plugs into. */
    RTCString        strFrontend;
} VBOXEXTPACKPLUGINDESC;
/** Pointer to a plug-in descriptor. */
typedef VBOXEXTPACKPLUGINDESC *PVBOXEXTPACKPLUGINDESC;

/**
 * Extension pack descriptor
 *
 * This is the internal representation of the ExtPack.xml.
 */
typedef struct VBOXEXTPACKDESC
{
    /** The name. */
    RTCString               strName;
    /** The description. */
    RTCString               strDescription;
    /** The version string. */
    RTCString               strVersion;
    /** The edition string. */
    RTCString               strEdition;
    /** The internal revision number. */
    uint32_t                uRevision;
    /** The name of the main module. */
    RTCString               strMainModule;
    /** The name of the main VM module, empty if none. */
    RTCString               strMainVMModule;
    /** The name of the VRDE module, empty if none. */
    RTCString               strVrdeModule;
    /** The name of the cryptographic module, empty if none. */
    RTCString               strCryptoModule;
    /** The number of plug-in descriptors. */
    uint32_t                cPlugIns;
    /** Pointer to an array of plug-in descriptors. */
    PVBOXEXTPACKPLUGINDESC  paPlugIns;
    /** Whether to show the license prior to installation. */
    bool                    fShowLicense;
} VBOXEXTPACKDESC;

/** Pointer to a extension pack descriptor. */
typedef VBOXEXTPACKDESC *PVBOXEXTPACKDESC;
/** Pointer to a const extension pack descriptor. */
typedef VBOXEXTPACKDESC const *PCVBOXEXTPACKDESC;


void                VBoxExtPackInitDesc(PVBOXEXTPACKDESC a_pExtPackDesc);
RTCString          *VBoxExtPackLoadDesc(const char *a_pszDir, PVBOXEXTPACKDESC a_pExtPackDesc, PRTFSOBJINFO a_pObjInfo);
RTCString          *VBoxExtPackLoadDescFromVfsFile(RTVFSFILE hVfsFile, PVBOXEXTPACKDESC a_pExtPackDesc, PRTFSOBJINFO a_pObjInfo);
RTCString          *VBoxExtPackExtractNameFromTarballPath(const char *pszTarball);
void                VBoxExtPackFreeDesc(PVBOXEXTPACKDESC a_pExtPackDesc);
bool                VBoxExtPackIsValidName(const char *pszName);
bool                VBoxExtPackIsValidMangledName(const char *pszMangledName, size_t cchMax = RTSTR_MAX);
RTCString          *VBoxExtPackMangleName(const char *pszName);
RTCString          *VBoxExtPackUnmangleName(const char *pszMangledName, size_t cbMax);
int                 VBoxExtPackCalcDir(char *pszExtPackDir, size_t cbExtPackDir, const char *pszParentDir, const char *pszName);
bool                VBoxExtPackIsValidVersionString(const char *pszVersion);
bool                VBoxExtPackIsValidEditionString(const char *pszEdition);
bool                VBoxExtPackIsValidModuleString(const char *pszModule);

int                 VBoxExtPackValidateMember(const char *pszName, RTVFSOBJTYPE enmType, RTVFSOBJ hVfsObj, char *pszError, size_t cbError);
int                 VBoxExtPackOpenTarFss(RTFILE hTarballFile, char *pszError, size_t cbError, PRTVFSFSSTREAM phTarFss, PRTMANIFEST phFileManifest);
int                 VBoxExtPackValidateTarball(RTFILE hTarballFile, const char *pszExtPackName,
                                               const char *pszTarball, const char *pszTarballDigest,
                                               char *pszError, size_t cbError,
                                               PRTMANIFEST phValidManifest, PRTVFSFILE phXmlFile, RTCString *pStrDigest);
#endif /* __cplusplus */

#endif /* !MAIN_INCLUDED_ExtPackUtil_h */

