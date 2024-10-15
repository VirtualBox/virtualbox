/* $Id: VBoxGuestR3LibDrmClient.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, DRM client handling.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "VBoxGuestR3LibInternal.h"

#include <iprt/env.h>
#include <iprt/path.h>
#include <iprt/process.h>

#if defined(RT_OS_LINUX)
# include <VBox/HostServices/GuestPropertySvc.h>

/** Defines the DRM client executable (image). */
# define VBOX_DRMCLIENT_EXECUTABLE           "/usr/bin/VBoxDRMClient"
# define VBOX_DRMCLIENT_LEGACY_EXECUTABLE    "/usr/bin/VBoxClient"
/** Defines the guest property that defines if the DRM resizing client needs to be active or not. */
# define VBOX_DRMCLIENT_GUEST_PROP_RESIZE    "/VirtualBox/GuestAdd/DRMResize"

/**
 * Check if specified guest property exist.
 *
 * @returns \c true if the property exists and its flags do match, \c false otherwise.
 * @param   pszPropName     Guest property name.
 * @param   fPropFlags      Guest property flags mask to verify if property exist.
 *                          If \p fPropFlags is 0, flags verification is omitted.
 */
static bool vbglR3DrmClientCheckProp(const char *pszPropName, uint32_t fPropFlags)
{
    bool fExist = false;
# if defined(VBOX_WITH_GUEST_PROPS)
    uint32_t idClient;

    int rc = VbglR3GuestPropConnect(&idClient);
    if (RT_SUCCESS(rc))
    {
        char *pcszFlags = NULL;

        rc = VbglR3GuestPropReadEx(idClient, pszPropName, NULL /* ppszValue */, &pcszFlags, NULL);
        if (RT_SUCCESS(rc))
        {
            /* Check property flags match. */
            if (fPropFlags)
            {
                uint32_t fFlags = 0;

                rc = GuestPropValidateFlags(pcszFlags, &fFlags);
                fExist = RT_SUCCESS(rc) && (fFlags == fPropFlags);
            }
            else
                fExist = true;

            RTStrFree(pcszFlags);
        }

        VbglR3GuestPropDisconnect(idClient);
    }
# endif /* VBOX_WITH_GUEST_PROPS */
    return fExist;
}
#endif /* RT_OS_LINUX */

/**
 * Returns true if the DRM resizing client is needed.
 * This is achieved by querying existence of a guest property.
 *
 * @returns \c true if the DRM resizing client is needed, \c false if not.
 */
VBGLR3DECL(bool) VbglR3DrmClientIsNeeded(void)
{
#if defined(RT_OS_LINUX)
    return vbglR3DrmClientCheckProp(VBOX_DRMCLIENT_GUEST_PROP_RESIZE, 0);
#else
    return false;
#endif
}

/**
 * Returns true if the DRM IPC server socket access should be restricted.
 *
 * Restricted access means that only users from a certain group should
 * be granted with read and write access permission to IPC socket. Check
 * is done by examining \c VBGLR3DRMIPCPROPRESTRICT guest property. Property
 * is only considered valid if is read-only for guest. I.e., the following
 * property should be set on the host side:
 *
 *  VBoxManage guestproperty set <VM> /VirtualBox/GuestAdd/DRMIpcRestricted 1 --flags RDONLYGUEST
 *
 * @returns \c true if restricted socket access is required, \c false otherwise.
 */
VBGLR3DECL(bool) VbglR3DrmRestrictedIpcAccessIsNeeded(void)
{
#if defined(RT_OS_LINUX)
    return vbglR3DrmClientCheckProp(VBGLR3DRMIPCPROPRESTRICT, GUEST_PROP_F_RDONLYGUEST);
#else
    return false;
#endif
}

/**
 * Returns true if the DRM resizing client already is running.
 * This is achieved by querying existence of a guest property.
 *
 * @returns \c true if the DRM resizing client is running, \c false if not.
 */
VBGLR3DECL(bool) VbglR3DrmClientIsRunning(void)
{
    return VbglR3DrmClientIsNeeded();
}

#if defined(RT_OS_LINUX)
static int VbglR3DrmStart(const char *pszCmd, const char **apszArgs)
{
    return RTProcCreate(pszCmd, apszArgs, RTENV_DEFAULT,
                        RTPROC_FLAGS_DETACHED | RTPROC_FLAGS_SEARCH_PATH, NULL);
}
#endif

/**
 * Starts (executes) the DRM resizing client process ("VBoxDRMClient").
 *
 * @returns VBox status code.
 */
VBGLR3DECL(int) VbglR3DrmClientStart(void)
{
#if defined(RT_OS_LINUX)
    const char *apszArgs[2] = { VBOX_DRMCLIENT_EXECUTABLE, NULL }; /** @todo r=andy Pass path + process name as argv0? */
    return VbglR3DrmStart(VBOX_DRMCLIENT_EXECUTABLE, apszArgs);
#else
    return VERR_NOT_SUPPORTED;
#endif
}

/**
 * Starts (executes) the legacy DRM resizing client process ("VBoxClient --vmsvga").
 *
 * @returns VBox status code.
 */
VBGLR3DECL(int) VbglR3DrmLegacyClientStart(void)
{
#if defined(RT_OS_LINUX)
    const char *apszArgs[3] = { VBOX_DRMCLIENT_LEGACY_EXECUTABLE, "--vmsvga", NULL };
    return VbglR3DrmStart(VBOX_DRMCLIENT_LEGACY_EXECUTABLE, apszArgs);
#else
    return VERR_NOT_SUPPORTED;
#endif
}

/**
 * Starts (executes) the legacy X11 resizing agent process ("VBoxClient --display").
 *
 * @returns VBox status code.
 */
VBGLR3DECL(int) VbglR3DrmLegacyX11AgentStart(void)
{
#if defined(RT_OS_LINUX)
    const char *apszArgs[3] = { VBOX_DRMCLIENT_LEGACY_EXECUTABLE, "--display", NULL };
    return VbglR3DrmStart(VBOX_DRMCLIENT_LEGACY_EXECUTABLE, apszArgs);
#else
    return VERR_NOT_SUPPORTED;
#endif
}

