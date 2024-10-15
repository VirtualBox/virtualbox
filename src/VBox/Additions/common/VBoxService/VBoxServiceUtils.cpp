/* $Id: VBoxServiceUtils.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxServiceUtils - Some utility functions.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
# include <iprt/param.h>
# include <iprt/path.h>
#endif
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include <VBox/VBoxGuestLib.h>
#include "VBoxServiceInternal.h"
#include "VBoxServiceUtils.h"


#ifdef VBOX_WITH_GUEST_PROPS
/**
 * Reads a guest property as a 32-bit value.
 *
 * @returns VBox status code, fully bitched.
 *
 * @param   u32ClientId         The HGCM client ID for the guest property session.
 * @param   pszPropName         The property name.
 * @param   pu32                Where to store the 32-bit value.
 *
 */
int VGSvcReadPropUInt32(uint32_t u32ClientId, const char *pszPropName, uint32_t *pu32, uint32_t u32Min, uint32_t u32Max)
{
    char *pszValue;
    int rc = VbglR3GuestPropReadEx(u32ClientId, pszPropName, &pszValue, NULL /* ppszFlags */, NULL /* puTimestamp */);
    if (RT_SUCCESS(rc))
    {
        char *pszNext;
        rc = RTStrToUInt32Ex(pszValue, &pszNext, 0, pu32);
        if (   RT_SUCCESS(rc)
            && (*pu32 < u32Min || *pu32 > u32Max))
            rc = VGSvcError("The guest property value %s = %RU32 is out of range [%RU32..%RU32].\n",
                            pszPropName, *pu32, u32Min, u32Max);
        RTStrFree(pszValue);
    }
    return rc;
}

/**
 * Reads a guest property from the host side.
 *
 * @returns IPRT status code, fully bitched.
 * @param   u32ClientId         The HGCM client ID for the guest property session.
 * @param   pszPropName         The property name.
 * @param   fReadOnly           Whether or not this property needs to be read only
 *                              by the guest side. Otherwise VERR_ACCESS_DENIED will
 *                              be returned.
 * @param   ppszValue           Where to return the value.  This is always set
 *                              to NULL.  Free it using RTStrFree().
 * @param   ppszFlags           Where to return the value flags. Free it
 *                              using RTStrFree().  Optional.
 * @param   puTimestamp         Where to return the timestamp.  This is only set
 *                              on success.  Optional.
 */
int VGSvcReadHostProp(uint32_t u32ClientId, const char *pszPropName, bool fReadOnly,
                      char **ppszValue, char **ppszFlags, uint64_t *puTimestamp)
{
    AssertPtrReturn(ppszValue, VERR_INVALID_PARAMETER);

    char *pszValue = NULL;
    char *pszFlags = NULL;
    int rc = VbglR3GuestPropReadEx(u32ClientId, pszPropName, &pszValue, &pszFlags, puTimestamp);
    if (RT_SUCCESS(rc))
    {
        /* Check security bits. */
        if (   fReadOnly /* Do we except a guest read-only property */
            && !RTStrStr(pszFlags, "RDONLYGUEST"))
        {
            /* If we want a property which is read-only on the guest
             * and it is *not* marked as such, deny access! */
            rc = VERR_ACCESS_DENIED;
        }

        if (RT_SUCCESS(rc))
        {
            *ppszValue = pszValue;

            if (ppszFlags)
                *ppszFlags = pszFlags;
            else if (pszFlags)
                RTStrFree(pszFlags);
        }
        else
        {
            if (pszValue)
                RTStrFree(pszValue);
            if (pszFlags)
                RTStrFree(pszFlags);
        }
    }

    return rc;
}


/**
 * Wrapper around VbglR3GuestPropWriteValue that does value formatting and
 * logging.
 *
 * @returns VBox status code. Errors will be logged.
 *
 * @param   u32ClientId     The HGCM client ID for the guest property session.
 * @param   pszName         The property name.
 * @param   pszValueFormat  The property format string.  If this is NULL then
 *                          the property will be deleted (if possible).
 * @param   ...             Format arguments.
 */
int VGSvcWritePropF(uint32_t u32ClientId, const char *pszName, const char *pszValueFormat, ...)
{
    AssertPtr(pszName);
    int rc;
    if (pszValueFormat != NULL)
    {
        va_list va;
        va_start(va, pszValueFormat);
        VGSvcVerbose(3, "Writing guest property '%s' = '%N'\n", pszName, pszValueFormat, &va);
        va_end(va);

        va_start(va, pszValueFormat);
        rc = VbglR3GuestPropWriteValueV(u32ClientId, pszName, pszValueFormat, va);
        va_end(va);

        if (RT_FAILURE(rc))
             VGSvcError("Error writing guest property '%s' (rc=%Rrc)\n", pszName, rc);
    }
    else
    {
        VGSvcVerbose(3, "Deleting guest property '%s'\n", pszName);
        rc = VbglR3GuestPropWriteValue(u32ClientId, pszName, NULL);
        if (RT_FAILURE(rc))
            VGSvcError("Error deleting guest property '%s' (rc=%Rrc)\n", pszName, rc);
    }
    return rc;
}

#endif /* VBOX_WITH_GUEST_PROPS */
#ifdef RT_OS_WINDOWS

/**
 * Helper for vgsvcUtilGetFileVersion and attempts to read and parse
 * FileVersion.
 *
 * @returns Success indicator.
 */
static bool vgsvcUtilGetFileVersionOwn(LPSTR pVerData, uint32_t *puMajor, uint32_t *puMinor,
                                       uint32_t *puBuildNumber, uint32_t *puRevisionNumber)
{
    UINT    cchStrValue = 0;
    LPTSTR  pStrValue   = NULL;
    if (!VerQueryValueA(pVerData, "\\StringFileInfo\\040904b0\\FileVersion", (LPVOID *)&pStrValue, &cchStrValue))
        return false;

    char *pszNext = pStrValue;
    int rc = RTStrToUInt32Ex(pszNext, &pszNext, 0, puMajor);
    AssertReturn(rc == VWRN_TRAILING_CHARS, false);
    AssertReturn(*pszNext == '.', false);

    rc = RTStrToUInt32Ex(pszNext + 1, &pszNext, 0, puMinor);
    AssertReturn(rc == VWRN_TRAILING_CHARS, false);
    AssertReturn(*pszNext == '.', false);

    rc = RTStrToUInt32Ex(pszNext + 1, &pszNext, 0, puBuildNumber);
    AssertReturn(rc == VWRN_TRAILING_CHARS, false);
    AssertReturn(*pszNext == '.', false);

    rc = RTStrToUInt32Ex(pszNext + 1, &pszNext, 0, puRevisionNumber);
    AssertReturn(rc == VINF_SUCCESS || rc == VWRN_TRAILING_CHARS /*??*/, false);

    return true;
}


/**
 * Worker for VGSvcUtilWinGetFileVersionString.
 *
 * @returns VBox status code.
 * @param   pszFilename         ASCII & ANSI & UTF-8 compliant name.
 * @param   puMajor             Where to return the major version number.
 * @param   puMinor             Where to return the minor version number.
 * @param   puBuildNumber       Where to return the build number.
 * @param   puRevisionNumber    Where to return the revision number.
 */
static int vgsvcUtilGetFileVersion(const char *pszFilename, uint32_t *puMajor, uint32_t *puMinor, uint32_t *puBuildNumber,
                                   uint32_t *puRevisionNumber)
{
    int rc;

    *puMajor = *puMinor = *puBuildNumber = *puRevisionNumber = 0;

    /*
     * Get the file version info.
     */
    DWORD dwHandleIgnored;
    DWORD cbVerData = GetFileVersionInfoSizeA(pszFilename, &dwHandleIgnored);
    if (cbVerData)
    {
        LPTSTR pVerData = (LPTSTR)RTMemTmpAllocZ(cbVerData);
        if (pVerData)
        {
            if (GetFileVersionInfoA(pszFilename, dwHandleIgnored, cbVerData, pVerData))
            {
                /*
                 * Try query and parse the FileVersion string our selves first
                 * since this will give us the correct revision number when
                 * it goes beyond the range of an uint16_t / WORD.
                 */
                if (vgsvcUtilGetFileVersionOwn(pVerData, puMajor, puMinor, puBuildNumber, puRevisionNumber))
                    rc = VINF_SUCCESS;
                else
                {
                    /* Fall back on VS_FIXEDFILEINFO */
                    UINT                 cbFileInfoIgnored = 0;
                    VS_FIXEDFILEINFO    *pFileInfo = NULL;
                    if (VerQueryValue(pVerData, "\\", (LPVOID *)&pFileInfo, &cbFileInfoIgnored))
                    {
                        *puMajor          = HIWORD(pFileInfo->dwFileVersionMS);
                        *puMinor          = LOWORD(pFileInfo->dwFileVersionMS);
                        *puBuildNumber    = HIWORD(pFileInfo->dwFileVersionLS);
                        *puRevisionNumber = LOWORD(pFileInfo->dwFileVersionLS);
                        rc = VINF_SUCCESS;
                    }
                    else
                    {
                        rc = RTErrConvertFromWin32(GetLastError());
                        VGSvcVerbose(3, "No file version value for file '%s' available! (%d / rc=%Rrc)\n",
                                     pszFilename,  GetLastError(), rc);
                    }
                }
            }
            else
            {
                rc = RTErrConvertFromWin32(GetLastError());
                VGSvcVerbose(0, "GetFileVersionInfo(%s) -> %u / %Rrc\n", pszFilename, GetLastError(), rc);
            }

            RTMemTmpFree(pVerData);
        }
        else
        {
            VGSvcVerbose(0, "Failed to allocate %u byte for file version info for '%s'\n", cbVerData, pszFilename);
            rc = VERR_NO_TMP_MEMORY;
        }
    }
    else
    {
        rc = RTErrConvertFromWin32(GetLastError());
        VGSvcVerbose(3, "GetFileVersionInfoSize(%s) -> %u / %Rrc\n", pszFilename, GetLastError(), rc);
    }
    return rc;
}


/**
 * Gets a re-formatted version string from the VS_FIXEDFILEINFO table.
 *
 * @returns VBox status code.  The output buffer is always valid and the status
 *          code can safely be ignored.
 *
 * @param   pszPath         The base path.
 * @param   pszFilename     The filename.
 * @param   pszVersion      Where to return the version string.
 * @param   cbVersion       The size of the version string buffer. This MUST be
 *                          at least 2 bytes!
 */
int VGSvcUtilWinGetFileVersionString(const char *pszPath, const char *pszFilename, char *pszVersion, size_t cbVersion)
{
    /*
     * We will ALWAYS return with a valid output buffer.
     */
    AssertReturn(cbVersion >= 2, VERR_BUFFER_OVERFLOW);
    pszVersion[0] = '-';
    pszVersion[1] = '\0';

    /*
     * Create the path and query the bits.
     */
    char szFullPath[RTPATH_MAX];
    int rc = RTPathJoin(szFullPath, sizeof(szFullPath), pszPath, pszFilename);
    if (RT_SUCCESS(rc))
    {
        uint32_t uMajor, uMinor, uBuild, uRev;
        rc = vgsvcUtilGetFileVersion(szFullPath, &uMajor, &uMinor, &uBuild, &uRev);
        if (RT_SUCCESS(rc))
            RTStrPrintf(pszVersion, cbVersion, "%u.%u.%ur%u", uMajor, uMinor, uBuild, uRev);
    }
    return rc;
}

#endif /* RT_OS_WINDOWS */


/**
 * Resolves the UID to a name as best as we can.
 *
 * @returns Read-only name string.  Only valid till the next cache call.
 * @param   pIdCache        The ID cache.
 * @param   uid             The UID to resolve.
 * @param   pszEntry        The filename of the UID.
 * @param   pszRelativeTo   What @a pszEntry is relative to, NULL if absolute.
 */
const char *VGSvcIdCacheGetUidName(PVGSVCIDCACHE pIdCache, RTUID uid, const char *pszEntry, const char *pszRelativeTo)
{
    /* Check cached entries. */
    for (uint32_t i = 0; i < pIdCache->cEntries; i++)
        if (   pIdCache->aEntries[i].id == uid
            && pIdCache->aEntries[i].fIsUid)
            return pIdCache->aEntries[i].szName;

    /* Miss. */
    RTFSOBJINFO ObjInfo;
    RT_ZERO(ObjInfo); /* shut up msc */
    int rc;
    if (!pszRelativeTo)
        rc = RTPathQueryInfoEx(pszEntry, &ObjInfo, RTFSOBJATTRADD_UNIX_OWNER, RTPATH_F_ON_LINK);
    else
    {
        char szPath[RTPATH_MAX];
        rc = RTPathJoin(szPath, sizeof(szPath), pszRelativeTo, pszEntry);
        if (RT_SUCCESS(rc))
            rc = RTPathQueryInfoEx(szPath, &ObjInfo, RTFSOBJATTRADD_UNIX_OWNER, RTPATH_F_ON_LINK);
    }

    if (   RT_SUCCESS(rc)
        && ObjInfo.Attr.u.UnixOwner.uid == uid)
    {
        uint32_t i = pIdCache->cEntries;
        if (i < RT_ELEMENTS(pIdCache->aEntries))
            pIdCache->cEntries = i + 1;
        else
            i = pIdCache->iNextReplace++ % RT_ELEMENTS(pIdCache->aEntries);
        pIdCache->aEntries[i].id     = uid;
        pIdCache->aEntries[i].fIsUid = true;
        RTStrCopy(pIdCache->aEntries[i].szName, sizeof(pIdCache->aEntries[i].szName), ObjInfo.Attr.u.UnixOwner.szName);
        return pIdCache->aEntries[i].szName;
    }
    return "";
}


/**
 * Resolves the GID to a name as best as we can.
 *
 * @returns Read-only name string.  Only valid till the next cache call.
 * @param   pIdCache        The ID cache.
 * @param   gid             The GID to resolve.
 * @param   pszEntry        The filename of the GID.
 * @param   pszRelativeTo   What @a pszEntry is relative to, NULL if absolute.
 */
const char *VGSvcIdCacheGetGidName(PVGSVCIDCACHE pIdCache, RTGID gid, const char *pszEntry, const char *pszRelativeTo)
{
    /* Check cached entries. */
    for (uint32_t i = 0; i < pIdCache->cEntries; i++)
        if (   pIdCache->aEntries[i].id == gid
            && !pIdCache->aEntries[i].fIsUid)
            return pIdCache->aEntries[i].szName;

    /* Miss. */
    RTFSOBJINFO ObjInfo;
    RT_ZERO(ObjInfo); /* shut up msc */
    int rc;
    if (!pszRelativeTo)
        rc = RTPathQueryInfoEx(pszEntry, &ObjInfo, RTFSOBJATTRADD_UNIX_GROUP, RTPATH_F_ON_LINK);
    else
    {
        char szPath[RTPATH_MAX];
        rc = RTPathJoin(szPath, sizeof(szPath), pszRelativeTo, pszEntry);
        if (RT_SUCCESS(rc))
            rc = RTPathQueryInfoEx(szPath, &ObjInfo, RTFSOBJATTRADD_UNIX_GROUP, RTPATH_F_ON_LINK);
    }

    if (   RT_SUCCESS(rc)
        && ObjInfo.Attr.u.UnixGroup.gid == gid)
    {
        uint32_t i = pIdCache->cEntries;
        if (i < RT_ELEMENTS(pIdCache->aEntries))
            pIdCache->cEntries = i + 1;
        else
            i = pIdCache->iNextReplace++ % RT_ELEMENTS(pIdCache->aEntries);
        pIdCache->aEntries[i].id     = gid;
        pIdCache->aEntries[i].fIsUid = false;
        RTStrCopy(pIdCache->aEntries[i].szName, sizeof(pIdCache->aEntries[i].szName), ObjInfo.Attr.u.UnixGroup.szName);
        return pIdCache->aEntries[i].szName;
    }
    return "";
}

