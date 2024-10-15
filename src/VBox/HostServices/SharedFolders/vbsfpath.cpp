/* $Id: vbsfpath.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Folders Service - guest/host path convertion and verification.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SHARED_FOLDERS
#ifdef UNITTEST
# include "testcase/tstSharedFolderService.h"
#endif

#include "vbsfpath.h"
#include "mappings.h"
#include "vbsf.h"
#include "shflhandle.h"

#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/fs.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/symlink.h>
#include <iprt/uni.h>
#include <iprt/stream.h>
#ifdef RT_OS_DARWIN
# include <Carbon/Carbon.h>
#endif
#include <VBox/com/defs.h> /* For S_OK. */

#ifdef UNITTEST
# include "teststubs.h"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define SHFL_RT_LINK(pClient) ((pClient)->fu32Flags & SHFL_CF_SYMLINKS ? RTPATH_F_ON_LINK : RTPATH_F_FOLLOW_LINK)


/**
 * @todo find a better solution for supporting the execute bit for non-windows
 * guests on windows host. Search for "0111" to find all the relevant places.
 */

/**
 * Corrects the casing of the final component
 *
 * @returns
 * @param   pClient             .
 * @param   pszFullPath         .
 * @param   pszStartComponent   .
 */
static int vbsfCorrectCasing(SHFLCLIENTDATA *pClient, char *pszFullPath, char *pszStartComponent)
{
    Log2(("vbsfCorrectCasing: %s %s\n", pszFullPath, pszStartComponent));

    AssertReturn((uintptr_t)pszFullPath < (uintptr_t)pszStartComponent - 1U, VERR_INTERNAL_ERROR_2);
    AssertReturn(pszStartComponent[-1] == RTPATH_DELIMITER, VERR_INTERNAL_ERROR_5);

    /*
     * Allocate a buffer that can hold really long file name entries as well as
     * the initial search pattern.
     */
    size_t cchComponent = strlen(pszStartComponent);
    size_t cchParentDir = pszStartComponent - pszFullPath;
    size_t cchFullPath  = cchParentDir + cchComponent;
    Assert(strlen(pszFullPath) == cchFullPath);

    size_t cbDirEntry   = 4096;
    if (cchFullPath + 4 > cbDirEntry - RT_OFFSETOF(RTDIRENTRYEX, szName))
        cbDirEntry = RT_OFFSETOF(RTDIRENTRYEX, szName) + cchFullPath + 4;

    PRTDIRENTRYEX pDirEntry = (PRTDIRENTRYEX)RTMemAlloc(cbDirEntry);
    if (pDirEntry == NULL)
        return VERR_NO_MEMORY;

    /*
     * Construct the search criteria in the szName member of pDirEntry.
     */
    /** @todo This is quite inefficient, especially for directories with many
     *        files.  If any of the typically case sensitive host systems start
     *        supporting opendir wildcard filters, it would make sense to build
     *        one here with '?' for case foldable charaters. */
    /** @todo Use RTDirOpen here and drop the whole uncessary path copying? */
    int rc = RTPathJoinEx(pDirEntry->szName, cbDirEntry - RT_OFFSETOF(RTDIRENTRYEX, szName),
                          pszFullPath, cchParentDir,
                          RT_STR_TUPLE("*"), RTPATH_STR_F_STYLE_HOST);
    AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        RTDIR hSearch = NULL;
        rc = RTDirOpenFiltered(&hSearch, pDirEntry->szName, RTDIRFILTER_WINNT, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            for (;;)
            {
                size_t cbDirEntrySize = cbDirEntry;

                rc = RTDirReadEx(hSearch, pDirEntry, &cbDirEntrySize, RTFSOBJATTRADD_NOTHING, SHFL_RT_LINK(pClient));
                if (rc == VERR_NO_MORE_FILES)
                    break;

                if (   rc != VINF_SUCCESS
                    && rc != VWRN_NO_DIRENT_INFO)
                {
                    if (   rc == VERR_NO_TRANSLATION
                        || rc == VERR_INVALID_UTF8_ENCODING)
                        continue;
                    AssertMsgFailed(("%Rrc\n", rc));
                    break;
                }

                Log2(("vbsfCorrectCasing: found %s\n", &pDirEntry->szName[0]));
                if (    pDirEntry->cbName == cchComponent
                    &&  !RTStrICmp(pszStartComponent, &pDirEntry->szName[0]))
                {
                    Log(("Found original name %s (%s)\n", &pDirEntry->szName[0], pszStartComponent));
                    strcpy(pszStartComponent, &pDirEntry->szName[0]);
                    rc = VINF_SUCCESS;
                    break;
                }
            }

            RTDirClose(hSearch);
        }
    }

    if (RT_FAILURE(rc))
        Log(("vbsfCorrectCasing %s failed with %Rrc\n", pszStartComponent, rc));

    RTMemFree(pDirEntry);

    return rc;
}

/* Temporary stand-in for RTPathExistEx. */
static int vbsfQueryExistsEx(const char *pszPath, uint32_t fFlags)
{
#if 0 /** @todo Fix the symlink issue on windows! */
    return RTPathExistsEx(pszPath, fFlags);
#else
    RTFSOBJINFO IgnInfo;
    return RTPathQueryInfoEx(pszPath, &IgnInfo, RTFSOBJATTRADD_NOTHING, fFlags);
#endif
}

/**
 * Helper for vbsfBuildFullPath that performs case corrections on the path
 * that's being build.
 *
 * @returns VINF_SUCCESS at the moment.
 * @param   pClient                 The client data.
 * @param   pszFullPath             Pointer to the full path.  This is the path
 *                                  which may need case corrections.  The
 *                                  corrections will be applied in place.
 * @param   cchFullPath             The length of the full path.
 * @param   fWildCard               Whether the last component may contain
 *                                  wildcards and thus might require exclusion
 *                                  from the case correction.
 * @param   fPreserveLastComponent  Always exclude the last component from case
 *                                  correction if set.
 */
static int vbsfCorrectPathCasing(SHFLCLIENTDATA *pClient, char *pszFullPath, size_t cchFullPath,
                                 bool fWildCard, bool fPreserveLastComponent)
{
    /*
     * Hide the last path component if it needs preserving.  This is required
     * in the following cases:
     *    - Contains the wildcard(s).
     *    - Is a 'rename' target.
     */
    char *pszLastComponent = NULL;
    if (fWildCard || fPreserveLastComponent)
    {
        char *pszSrc = pszFullPath + cchFullPath - 1;
        Assert(strchr(pszFullPath, '\0') == pszSrc + 1);
        while ((uintptr_t)pszSrc > (uintptr_t)pszFullPath)
        {
            if (*pszSrc == RTPATH_DELIMITER)
                break;
            pszSrc--;
        }
        if (*pszSrc == RTPATH_DELIMITER)
        {
            if (   fPreserveLastComponent
                /* Or does it really have wildcards? */
                || strchr(pszSrc + 1, '*') != NULL
                || strchr(pszSrc + 1, '?') != NULL
                || strchr(pszSrc + 1, '>') != NULL
                || strchr(pszSrc + 1, '<') != NULL
                || strchr(pszSrc + 1, '"') != NULL )
            {
                pszLastComponent = pszSrc;
                *pszLastComponent = '\0';
            }
        }
    }

    /*
     * If the path/file doesn't exist, we need to attempt case correcting it.
     */
    /** @todo Don't check when creating files or directories; waste of time. */
    int rc = vbsfQueryExistsEx(pszFullPath, SHFL_RT_LINK(pClient));
    if (rc == VERR_FILE_NOT_FOUND || rc == VERR_PATH_NOT_FOUND)
    {
        Log(("Handle case insensitive guest fs on top of host case sensitive fs for %s\n", pszFullPath));

        /*
         * Work from the end of the path to find a partial path that's valid.
         */
        char *pszSrc = pszLastComponent ? pszLastComponent - 1 : pszFullPath + cchFullPath - 1;
        Assert(strchr(pszFullPath, '\0') == pszSrc + 1);

        while ((uintptr_t)pszSrc > (uintptr_t)pszFullPath)
        {
            if (*pszSrc == RTPATH_DELIMITER)
            {
                *pszSrc = '\0';
                rc = vbsfQueryExistsEx(pszFullPath, SHFL_RT_LINK(pClient));
                *pszSrc = RTPATH_DELIMITER;
                if (RT_SUCCESS(rc))
                {
#ifdef DEBUG
                    *pszSrc = '\0';
                    Log(("Found valid partial path %s\n", pszFullPath));
                    *pszSrc = RTPATH_DELIMITER;
#endif
                    break;
                }
            }

            pszSrc--;
        }
        Assert(*pszSrc == RTPATH_DELIMITER && RT_SUCCESS(rc));
        if (   *pszSrc == RTPATH_DELIMITER
            && RT_SUCCESS(rc))
        {
            /*
             * Turn around and work the other way case correcting the components.
             */
            pszSrc++;
            for (;;)
            {
                bool fEndOfString = true;

                /* Find the end of the component. */
                char *pszEnd = pszSrc;
                while (*pszEnd)
                {
                    if (*pszEnd == RTPATH_DELIMITER)
                        break;
                    pszEnd++;
                }

                if (*pszEnd == RTPATH_DELIMITER)
                {
                    fEndOfString = false;
                    *pszEnd = '\0';
#if 0 /** @todo Please, double check this. The original code is in the #if 0, what I hold as correct is in the #else. */
                    rc = RTPathQueryInfoEx(pszSrc, &info, RTFSOBJATTRADD_NOTHING, SHFL_RT_LINK(pClient));
#else
                    rc = vbsfQueryExistsEx(pszFullPath, SHFL_RT_LINK(pClient));
#endif
                    Assert(rc == VINF_SUCCESS || rc == VERR_FILE_NOT_FOUND || rc == VERR_PATH_NOT_FOUND);
                }
                else if (pszEnd == pszSrc)
                    rc = VINF_SUCCESS;  /* trailing delimiter */
                else
                    rc = VERR_FILE_NOT_FOUND;

                if (rc == VERR_FILE_NOT_FOUND || rc == VERR_PATH_NOT_FOUND)
                {
                    /* Path component is invalid; try to correct the casing. */
                    rc = vbsfCorrectCasing(pClient, pszFullPath, pszSrc);
                    if (RT_FAILURE(rc))
                    {
                        /* Failed, so don't bother trying any further components. */
                        if (!fEndOfString)
                            *pszEnd = RTPATH_DELIMITER; /* Restore the original full path. */
                        break;
                    }
                }

                /* Next (if any). */
                if (fEndOfString)
                    break;

                *pszEnd = RTPATH_DELIMITER;
                pszSrc = pszEnd + 1;
            }
            if (RT_FAILURE(rc))
                Log(("Unable to find suitable component rc=%d\n", rc));
        }
        else
            rc = VERR_FILE_NOT_FOUND;

    }

    /* Restore the final component if it was dropped. */
    if (pszLastComponent)
        *pszLastComponent = RTPATH_DELIMITER;

    /* might be a new file so don't fail here! */
    return VINF_SUCCESS;
}


#ifdef RT_OS_DARWIN
/* Misplaced hack! See todo! */

/** Normalize the string using kCFStringNormalizationFormD.
 *
 * @param pwszSrc  The input UTF-16 string.
 * @param cwcSrc   Length of the input string in characters.
 * @param ppwszDst Where to store the pointer to the resulting normalized string.
 * @param pcwcDst  Where to store length of the normalized string in characters (without the trailing nul).
 */
static int vbsfNormalizeStringDarwin(PCRTUTF16 pwszSrc, uint32_t cwcSrc, PRTUTF16 *ppwszDst, uint32_t *pcwcDst)
{
    /** @todo This belongs in rtPathToNative or in the windows shared folder file system driver...
     * The question is simply whether the NFD normalization is actually applied on a (virtual) file
     * system level in darwin, or just by the user mode application libs. */

    PRTUTF16 pwszNFD;
    uint32_t cwcNFD;

    CFMutableStringRef inStr = ::CFStringCreateMutable(NULL, 0);

    /* Is 8 times length enough for decomposed in worst case...? */
    size_t cbNFDAlloc = cwcSrc * 8 + 2;
    pwszNFD = (PRTUTF16)RTMemAllocZ(cbNFDAlloc);
    if (!pwszNFD)
    {
        return VERR_NO_MEMORY;
    }

    ::CFStringAppendCharacters(inStr, (UniChar*)pwszSrc, cwcSrc);
    ::CFStringNormalize(inStr, kCFStringNormalizationFormD);
    cwcNFD = ::CFStringGetLength(inStr);

    CFRange rangeCharacters;
    rangeCharacters.location = 0;
    rangeCharacters.length = cwcNFD;
    ::CFStringGetCharacters(inStr, rangeCharacters, pwszNFD);

    pwszNFD[cwcNFD] = 0x0000; /* NULL terminated */

    CFRelease(inStr);

    *ppwszDst = pwszNFD;
    *pcwcDst = cwcNFD;
    return VINF_SUCCESS;
}
#endif


#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
/* See MSDN "Naming Files, Paths, and Namespaces".
 * '<', '>' and '"' are allowed as possible wildcards (see ANSI_DOS_STAR, etc in ntifs.h)
 */
static const char sachCharBlackList[] = ":/\\|";
#else
/* Something else. */
static const char sachCharBlackList[] = "/";
#endif

/** Verify if the character can be used in a host file name.
 * Wildcard characters ('?', '*') are allowed.
 *
 * @param c Character to verify.
 */
static bool vbsfPathIsValidNameChar(unsigned char c)
{
    /* Character 0 is not allowed too. */
    if (c == 0 || strchr(sachCharBlackList, c))
    {
        return false;
    }

#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    /* Characters less than 32 are not allowed. */
    if (c < 32)
    {
        return false;
    }
#endif

    return true;
}

/** Verify if the character is a wildcard.
 *
 * @param c Character to verify.
 */
static bool vbsfPathIsWildcardChar(char c)
{
    if (   c == '*'
        || c == '?'
#ifdef RT_OS_WINDOWS /* See ntifs.h */
        || c == '<' /* ANSI_DOS_STAR */
        || c == '>' /* ANSI_DOS_QM */
        || c == '"' /* ANSI_DOS_DOT */
#endif
       )
    {
        return true;
    }

    return false;
}

/**
 * Validate the symbolic link creation policy inside a guest operating in a shared folder.
 *
 * @returns S_OK or VERR_WRITE_PROTECT.
 * @param pchSymlinkPath    The pathname of the symbolic link within the guest.
 * @param enmSymlinkPolicy  The symbolic link creation policy being evaluated.
 *
 * The enmSymlinkPolicy symlink creation policies are:
 *  - @a none:       no policy set
 *  - @a any:        no restrictions
 *  - @a forbidden:  no symlinks allowed
 *  - @a relative:   relative paths only ('..' path components allowed)
 *  - @a subtree:    relative paths only within the shared folder (no '..' path components allowed)
 */
static int vbsfPathEvalSymlinkPolicy(const char *pchSymlinkPath, SymlinkPolicy_T enmSymlinkPolicy)
{
    /* If no symlink policy has been set we continue the historical behaviour of applying no
     * additional restrictions.  The "any" policy also has no symlink path limitations. */
    if (   enmSymlinkPolicy == SymlinkPolicy_None
        || enmSymlinkPolicy == SymlinkPolicy_AllowedToAnyTarget)
        return S_OK;

    /* No absolute paths allowed except for the "any" policy.  The symlink path can't
     * contain '..' components if the "subtree" policy in effect. */
    if (   RTPathStartsWithRoot(pchSymlinkPath)
        || enmSymlinkPolicy == SymlinkPolicy_Forbidden
        || (   enmSymlinkPolicy == SymlinkPolicy_AllowedInShareSubtree
            && RTStrStr(pchSymlinkPath, "..")))
        return VERR_WRITE_PROTECT;

    return S_OK;
}

int vbsfPathGuestToHost(SHFLCLIENTDATA *pClient, SHFLROOT hRoot,
                        PCSHFLSTRING pGuestString, uint32_t cbGuestString,
                        char **ppszHostPath, uint32_t *pcbHostPathRoot,
                        uint32_t fu32Options,
                        uint32_t *pfu32PathFlags)
{
#ifdef VBOX_STRICT
    /*
     * Check that the pGuestPath has correct size and encoding.
     */
    if (ShflStringIsValidIn(pGuestString, cbGuestString, RT_BOOL(pClient->fu32Flags & SHFL_CF_UTF8)) == false)
    {
        LogFunc(("Invalid input string\n"));
        return VERR_INTERNAL_ERROR;
    }
#else
    NOREF(cbGuestString);
#endif

    /*
     * Resolve the root handle into a string.
     */
    uint32_t cbRootLen = 0;
    const char *pszRoot = NULL;
    int rc = vbsfMappingsQueryHostRootEx(hRoot, &pszRoot, &cbRootLen);
    if (RT_FAILURE(rc))
    {
        LogFunc(("invalid root\n"));
        return rc;
    }

    AssertReturn(cbRootLen > 0, VERR_INTERNAL_ERROR_2); /* vbsfMappingsQueryHostRootEx ensures this. */

    /*
     * Get the UTF8 string with the relative path provided by the guest.
     * If guest uses UTF-16 then convert it to UTF-8.
     */
    uint32_t    cbGuestPath = 0;        /* Shut up MSC */
    const char *pchGuestPath = NULL;    /* Ditto. */
    char *pchGuestPathAllocated = NULL; /* Converted from UTF-16. */
    if (BIT_FLAG(pClient->fu32Flags, SHFL_CF_UTF8))
    {
        /* UTF-8 */
        cbGuestPath = pGuestString->u16Length;
        pchGuestPath = pGuestString->String.ach;
    }
    else
    {
        /* UTF-16 */

#ifdef RT_OS_DARWIN /* Misplaced hack! See todo! */
        uint32_t cwcSrc  = 0;
        PRTUTF16 pwszSrc = NULL;
        rc = vbsfNormalizeStringDarwin(&pGuestString->String.utf16[0],
                                       pGuestString->u16Length / sizeof(RTUTF16),
                                       &pwszSrc, &cwcSrc);
#else
        uint32_t  const cwcSrc  = pGuestString->u16Length / sizeof(RTUTF16);
        PCRTUTF16 const pwszSrc = &pGuestString->String.utf16[0];
#endif

        if (RT_SUCCESS(rc))
        {
            size_t cbPathAsUtf8 = RTUtf16CalcUtf8Len(pwszSrc);
            if (cbPathAsUtf8 >= cwcSrc)
            {
                /* Allocate buffer that will be able to contain the converted UTF-8 string. */
                pchGuestPathAllocated = (char *)RTMemAlloc(cbPathAsUtf8 + 1);
                if (RT_LIKELY(pchGuestPathAllocated != NULL))
                {
                    if (RT_LIKELY(cbPathAsUtf8))
                    {
                        size_t cchActual;
                        char *pszDst = pchGuestPathAllocated;
                        rc = RTUtf16ToUtf8Ex(pwszSrc, cwcSrc, &pszDst, cbPathAsUtf8 + 1, &cchActual);
                        AssertRC(rc);
                        AssertStmt(RT_FAILURE(rc) || cchActual == cbPathAsUtf8, rc = VERR_INTERNAL_ERROR_4);
                        Assert(strlen(pszDst) == cbPathAsUtf8);
                    }

                    if (RT_SUCCESS(rc))
                    {
                        /* Terminate the string. */
                        pchGuestPathAllocated[cbPathAsUtf8] = '\0';

                        cbGuestPath = (uint32_t)cbPathAsUtf8; Assert(cbGuestPath == cbPathAsUtf8);
                        pchGuestPath = pchGuestPathAllocated;
                    }
                }
                else
                {
                    rc = VERR_NO_MEMORY;
                }
            }
            else
            {
                AssertFailed();
                rc = VERR_INTERNAL_ERROR_3;
            }

#ifdef RT_OS_DARWIN
            RTMemFree(pwszSrc);
#endif
        }
    }

    char *pszFullPath = NULL;

    if (RT_SUCCESS(rc))
    {
        LogFlowFunc(("Root %s path %.*s\n", pszRoot, cbGuestPath, pchGuestPath));

        /*
         * Allocate enough memory to build the host full path from the root and the relative path.
         */
        const uint32_t cbFullPathAlloc = cbRootLen + 1 + cbGuestPath + 1; /* root + possible_slash + relative + 0 */
        pszFullPath = (char *)RTMemAlloc(cbFullPathAlloc);
        if (RT_LIKELY(pszFullPath != NULL))
        {
            /* Buffer for the verified guest path. */
            char *pchVerifiedPath = (char *)RTMemAlloc(cbGuestPath + 1);
            if (RT_LIKELY(pchVerifiedPath != NULL))
            {
                /* Init the pointer for the guest relative path. */
                uint32_t cbSrc = cbGuestPath;
                const char *pchSrc = pchGuestPath;

                /* when validating source file pathnames for symbolic links we don't modify the path */
                if (!(fu32Options & VBSF_O_PATH_CHECK_SYMLINK_POLICY))
                {
                    /* Strip leading delimiters from the path the guest specified. */
                    while (   cbSrc > 0
                           && *pchSrc == pClient->PathDelimiter)
                    {
                        ++pchSrc;
                        --cbSrc;
                    }
                }

                /*
                 * Iterate the guest path components, verify each of them replacing delimiters with the host slash.
                 */
                char *pchDst = pchVerifiedPath;
                bool fLastComponentHasWildcard = false;
                for (; cbSrc > 0; --cbSrc, ++pchSrc)
                {
                    if (RT_LIKELY(*pchSrc != pClient->PathDelimiter))
                    {
                        if (RT_LIKELY(vbsfPathIsValidNameChar(*pchSrc)))
                        {
                            if (pfu32PathFlags && vbsfPathIsWildcardChar(*pchSrc))
                            {
                                fLastComponentHasWildcard = true;
                            }

                            *pchDst++ = *pchSrc;
                        }
                        else
                        {
                            rc = VERR_INVALID_NAME;
                            break;
                        }
                    }
                    else
                    {
                        /* Replace with the host slash. */
                        *pchDst++ = RTPATH_SLASH;

                        if (pfu32PathFlags && fLastComponentHasWildcard && cbSrc > 1)
                        {
                            /* Processed component has a wildcard and there are more characters in the path. */
                            *pfu32PathFlags |= VBSF_F_PATH_HAS_WILDCARD_IN_PREFIX;
                        }
                        fLastComponentHasWildcard = false;
                    }
                }

                if (RT_SUCCESS(rc))
                {
                    *pchDst++ = 0;

                    /* check if a symbolic link creation policy has been set */
                    if (fu32Options & VBSF_O_PATH_CHECK_SYMLINK_POLICY)
                    {
                        /* copy the verified symlink source file path to be returned to the caller */
                        rc = RTStrCopy(pszFullPath, cbFullPathAlloc, pchVerifiedPath);
                        if (RT_SUCCESS(rc))
                        {
                            SymlinkPolicy_T enmSymlinkPolicy;
                            rc = vbsfMappingsQuerySymlinkPolicy(pClient, hRoot, &enmSymlinkPolicy);
                            if (RT_SUCCESS(rc))
                                rc = vbsfPathEvalSymlinkPolicy(pchVerifiedPath, enmSymlinkPolicy);
                        }
                    }
                    else
                    {
                        /* Construct the full host path removing '.' and '..'. */
                        rc = vbsfPathAbs(pszRoot, pchVerifiedPath, pszFullPath, cbFullPathAlloc);
                    }
                    if (RT_SUCCESS(rc))
                    {
                        if (pfu32PathFlags && fLastComponentHasWildcard)
                        {
                            *pfu32PathFlags |= VBSF_F_PATH_HAS_WILDCARD_IN_LAST;
                        }

                        /* Check if the full path is still within the shared folder. */
                        if (fu32Options & VBSF_O_PATH_CHECK_ROOT_ESCAPE)
                        {
                            if (!RTPathStartsWith(pszFullPath, pszRoot))
                            {
                                rc = VERR_INVALID_NAME;
                            }
                        }

                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * If the host file system is case sensitive and the guest expects
                             * a case insensitive fs, then correct the path components casing.
                             */
                            if (    vbsfIsHostMappingCaseSensitive(hRoot)
                                && !vbsfIsGuestMappingCaseSensitive(hRoot))
                            {
                                const bool fWildCard = RT_BOOL(fu32Options & VBSF_O_PATH_WILDCARD);
                                const bool fPreserveLastComponent = RT_BOOL(fu32Options & VBSF_O_PATH_PRESERVE_LAST_COMPONENT);
                                rc = vbsfCorrectPathCasing(pClient, pszFullPath, strlen(pszFullPath),
                                                           fWildCard, fPreserveLastComponent);
                            }

                            if (RT_SUCCESS(rc))
                            {
                               LogFlowFunc(("%s\n", pszFullPath));

                               /* Return the full host path. */
                               *ppszHostPath = pszFullPath;

                               if (pcbHostPathRoot)
                               {
                                   /* Return the length of the root path without the trailing slash. */
                                   *pcbHostPathRoot = RTPATH_IS_SLASH(pszFullPath[cbRootLen - 1]) ?
                                                          cbRootLen - 1 : /* pszRoot already had the trailing slash. */
                                                          cbRootLen; /* pszRoot did not have the trailing slash. */
                               }
                            }
                        }
                    }
                    else
                    {
                        if (fu32Options & VBSF_O_PATH_CHECK_SYMLINK_POLICY)
                            LogFunc(("vbsfPathEvalSymlinkPolicy() returns rc=%Rrc\n", rc));
                        else
                            LogFunc(("vbsfPathAbs %Rrc\n", rc));
                    }
                }

                RTMemFree(pchVerifiedPath);
            }
            else
            {
                rc = VERR_NO_MEMORY;
            }
        }
        else
        {
            rc = VERR_NO_MEMORY;
        }
    }

    /*
     * Cleanup.
     */
    RTMemFree(pchGuestPathAllocated);

    if (RT_SUCCESS(rc))
    {
        return rc;
    }

    /*
     * Cleanup on failure.
     */
    RTMemFree(pszFullPath);

    LogFunc(("%Rrc\n", rc));
    return rc;
}

void vbsfFreeHostPath(char *pszHostPath)
{
    RTMemFree(pszHostPath);
}

