/* $Id: VBoxCredentialProvider.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxCredentialProvider - Main file of the VirtualBox Credential Provider.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef GA_INCLUDED_SRC_WINNT_VBoxCredProv_VBoxCredentialProvider_h
#define GA_INCLUDED_SRC_WINNT_VBoxCredProv_VBoxCredentialProvider_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/win/windows.h>
#include <iprt/win/credentialprovider.h>
#include <Shlguid.h>

#include "VBoxCredProvUtils.h"

/** The VirtualBox credential provider class ID -- must not be changed. */
DEFINE_GUID(CLSID_VBoxCredProvider, 0x275d3bcc, 0x22bb, 0x4948, 0xa7, 0xf6, 0x3a, 0x30, 0x54, 0xeb, 0xa9, 0x2b);

/**
 * The credential provider's UI field IDs, used for
 * handling / identifying them.
 */
enum VBOXCREDPROV_FIELDID
{
    VBOXCREDPROV_FIELDID_TILEIMAGE       = 0,
    VBOXCREDPROV_FIELDID_USERNAME        = 1,
    VBOXCREDPROV_FIELDID_PASSWORD        = 2,
    VBOXCREDPROV_FIELDID_DOMAINNAME      = 3,
    VBOXCREDPROV_FIELDID_SUBMIT_BUTTON   = 4,
    VBOXCREDPROV_FIELDID_PROVIDER_LOGO   = 5,
    VBOXCREDPROV_FIELDID_PROVIDER_LABEL  = 6
};

/* Note: If new fields are added to VBOXCREDPROV_FIELDID and s_VBoxCredProvFields,
         don't forget to increase this define! */
#define VBOXCREDPROV_NUM_FIELDS            7

/** Maximum credential provider field length (in characters). */
#define VBOXCREDPROV_MAX_FIELD_LEN         255

struct VBOXCREDPROV_FIELD
{
    /** The actual description of this field: It's label,
     *  official field type ID, ... */
    CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR desc;
    /** The field's display state within the. */
    CREDENTIAL_PROVIDER_FIELD_STATE state;
    /** The interactive state: Used when this field gets shown to determine
     *  its state -- currently, only focussing is implemented. */
    CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE stateInteractive;
};

#ifndef PCREDENTIAL_PROVIDER_FIELD_DESCRIPTOR
# define PCREDENTIAL_PROVIDER_FIELD_DESCRIPTOR CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR*
#endif

#if !defined(NTDDI_VERSION) || !defined(NTDDI_WIN8) || NTDDI_VERSION < NTDDI_WIN8 /* < Windows 8 */
/* 2d837775-f6cd-464e-a745-482fd0b47493, introduced in windows 8 */
DEFINE_GUID(CPFG_CREDENTIAL_PROVIDER_LOGO, 0x2d837775, 0xf6cd, 0x464e, 0xa7, 0x45, 0x48, 0x2f, 0xd0, 0xb4, 0x74, 0x93);
/* 286BBFF3-BAD4-438F-B007-79B7267C3D48, introduced in windows 8. */
DEFINE_GUID(CPFG_CREDENTIAL_PROVIDER_LABEL, 0x286BBFF3, 0xBAD4, 0x438F, 0xB0 ,0x07, 0x79, 0xB7, 0x26, 0x7C, 0x3D, 0x48);
#endif


/** The credential provider's default fields. */
static const VBOXCREDPROV_FIELD s_VBoxCredProvDefaultFields[] =
{
    /** The user's profile image (tile). */
    { { VBOXCREDPROV_FIELDID_TILEIMAGE,      CPFT_TILE_IMAGE,    (LPWSTR)L"Tile Image",     {0}                            }, CPFS_DISPLAY_IN_BOTH,            CPFIS_NONE    },
    { { VBOXCREDPROV_FIELDID_USERNAME,       CPFT_LARGE_TEXT,    (LPWSTR)L"Username",       CPFG_LOGON_USERNAME            }, CPFS_DISPLAY_IN_BOTH,            CPFIS_NONE    },
    { { VBOXCREDPROV_FIELDID_PASSWORD,       CPFT_PASSWORD_TEXT, (LPWSTR)L"Password",       CPFG_LOGON_PASSWORD            }, CPFS_DISPLAY_IN_SELECTED_TILE,   CPFIS_FOCUSED },
    { { VBOXCREDPROV_FIELDID_DOMAINNAME,     CPFT_LARGE_TEXT,    (LPWSTR)L"",               {0}                            }, CPFS_DISPLAY_IN_SELECTED_TILE,   CPFIS_FOCUSED },
    { { VBOXCREDPROV_FIELDID_SUBMIT_BUTTON,  CPFT_SUBMIT_BUTTON, (LPWSTR)L"Submit",         {0}                            }, CPFS_DISPLAY_IN_SELECTED_TILE,   CPFIS_FOCUSED },
    /** New since Windows 8: The image used to represent a credential provider on the logon page. */
    { { VBOXCREDPROV_FIELDID_PROVIDER_LOGO,  CPFT_TILE_IMAGE,    (LPWSTR)L"Provider Logo",  CPFG_CREDENTIAL_PROVIDER_LOGO  }, CPFS_HIDDEN /* Not used yet. */, CPFIS_NONE },
    /** New since Windows 8: The label associated with a credential provider on the logon page. */
    { { VBOXCREDPROV_FIELDID_PROVIDER_LABEL, CPFT_SMALL_TEXT,    (LPWSTR)L"Provider Label", CPFG_CREDENTIAL_PROVIDER_LABEL }, CPFS_HIDDEN /* Not used yet. */, CPFIS_NONE }
};

/** Prototypes. */
void VBoxCredentialProviderAcquire(void);
void VBoxCredentialProviderRelease(void);
LONG VBoxCredentialProviderRefCount(void);

HRESULT VBoxCredentialProviderCreate(REFCLSID classID,
                                     REFIID interfaceID, void **ppvInterface);

#endif /* !GA_INCLUDED_SRC_WINNT_VBoxCredProv_VBoxCredentialProvider_h */

