/* $Id: RTFsCmdLs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - /bin/ls like utility for testing the VFS code.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#include <iprt/vfs.h>

#include <iprt/buildconfig.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/sort.h>
#include <iprt/stream.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Display entry.
 */
typedef struct RTCMDLSENTRY
{
    /** The information about the entry. */
    RTFSOBJINFO Info;
    /** Symbolic link target (allocated after the name). */
    const char *pszTarget;
    /** Owner if applicable(allocated after the name). */
    const char *pszOwner;
    /** Group if applicable (allocated after the name). */
    const char *pszGroup;
    /** The length of szName. */
    size_t      cchName;
    /** The entry name. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char        szName[RT_FLEXIBLE_ARRAY];
} RTCMDLSENTRY;
/** Pointer to a ls display entry. */
typedef RTCMDLSENTRY *PRTCMDLSENTRY;
/** Pointer to a ls display entry pointer. */
typedef PRTCMDLSENTRY *PPRTCMDLSENTRY;


/**
 * Collection of display entries.
 */
typedef struct RTCMDLSCOLLECTION
{
    /** Current size of papEntries. */
    size_t          cEntries;
    /** Memory allocated for papEntries. */
    size_t          cEntriesAllocated;
    /** Current entries pending sorting and display. */
    PPRTCMDLSENTRY  papEntries;

    /** Total number of bytes allocated for the above entries. */
    uint64_t        cbTotalAllocated;
    /** Total number of file content bytes.    */
    uint64_t        cbTotalFiles;

    /** The collection name (path). */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char            szName[RT_FLEXIBLE_ARRAY];
} RTCMDLSCOLLECTION;
/** Pointer to a display entry collection.  */
typedef RTCMDLSCOLLECTION *PRTCMDLSCOLLECTION;
/** Pointer to a display entry collection pointer.  */
typedef PRTCMDLSCOLLECTION *PPRTCMDLSCOLLECTION;


/** Sorting. */
typedef enum RTCMDLSSORT
{
    RTCMDLSSORT_INVALID = 0,
    RTCMDLSSORT_NONE,
    RTCMDLSSORT_NAME,
    RTCMDLSSORT_EXTENSION,
    RTCMDLSSORT_SIZE,
    RTCMDLSSORT_TIME,
    RTCMDLSSORT_VERSION
} RTCMDLSSORT;

/** Time selection. */
typedef enum RTCMDLSTIME
{
    RTCMDLSTIME_INVALID = 0,
    RTCMDLSTIME_BTIME,
    RTCMDLSTIME_CTIME,
    RTCMDLSTIME_MTIME,
    RTCMDLSTIME_ATIME
} RTCMDLSTIME;

/** Time display style. */
typedef enum RTCMDLSTIMESTYLE
{
    RTCMDLSTIMESTYLE_INVALID = 0,
    RTCMDLSTIMESTYLE_FULL_ISO,
    RTCMDLSTIMESTYLE_LONG_ISO,
    RTCMDLSTIMESTYLE_ISO,
    RTCMDLSTIMESTYLE_LOCALE,
    RTCMDLSTIMESTYLE_CUSTOM
} RTCMDLSTIMESTYLE;

/** Coloring selection. */
typedef enum RTCMDLSCOLOR
{
    RTCMDLSCOLOR_INVALID = 0,
    RTCMDLSCOLOR_NONE
} RTCMDLSCOLOR;

/** Formatting. */
typedef enum RTCMDLSFORMAT
{
    RTCMDLSFORMAT_INVALID = 0,
    RTCMDLSFORMAT_COLS_VERTICAL,            /**< -C/default */
    RTCMDLSFORMAT_COLS_HORIZONTAL,          /**< -x */
    RTCMDLSFORMAT_COMMAS,                   /**< -m */
    RTCMDLSFORMAT_SINGLE,                   /**< -1 */
    RTCMDLSFORMAT_LONG,                     /**< -l */
    RTCMDLSFORMAT_MACHINE_READABLE          /**< --machine-readable */
} RTCMDLSFORMAT;


/**
 * LS command options and state.
 */
typedef struct RTCMDLSOPTS
{
    /** @name Traversal.
     * @{ */
    bool            fFollowSymlinksInDirs;  /**< -L */
    bool            fFollowSymlinkToAnyArgs;
    bool            fFollowSymlinkToDirArgs;
    bool            fFollowDirectoryArgs;   /**< Inverse -d/--directory. */
    bool            fRecursive;             /**< -R */
    /** @} */


    /** @name Filtering.
     * @{ */
    bool            fShowHidden;            /**< -a/--all or -A/--almost-all */
    bool            fShowDotAndDotDot;      /**< -a vs -A */
    bool            fShowBackups;           /**< Inverse -B/--ignore-backups (*~). */
    /** @} */

    /** @name Sorting
     * @{ */
    RTCMDLSSORT     enmSort;                /**< --sort */
    bool            fReverseSort;           /**< -r */
    bool            fGroupDirectoriesFirst; /**< fGroupDirectoriesFirst */
    /** @} */

    /** @name Formatting
     * @{ */
    RTCMDLSFORMAT   enmFormat;              /**< --format */

    bool            fEscapeNonGraphicChars; /**< -b, --escape */
    bool            fEscapeControlChars;
    bool            fHideControlChars;      /**< -q/--hide-control-chars, --show-control-chars */

    bool            fHumanReadableSizes;    /**< -h */
    bool            fSiUnits;               /**< --si */
    uint32_t        cbBlock;                /**< --block-size=N, -k */

    bool            fShowOwner;
    bool            fShowGroup;
    bool            fNumericalIds;          /**< -n  */
    bool            fShowINode;
    bool            fShowAllocatedSize;     /**< -s */
    uint8_t         cchTab;                 /**< -T */
    uint32_t        cchWidth;               /**< -w */

    RTCMDLSCOLOR    enmColor;               /**< --color */

    RTCMDLSTIME     enmTime;                /**< --time */
    RTCMDLSTIMESTYLE enmTimeStyle;          /**< --time-style, --full-time */
    const char     *pszTimeCustom;          /**< --time-style=+xxx */
    /** @} */

    /** @name State
     * @{ */
    /** Current size of papCollections. */
    size_t              cCollections;
    /** Memory allocated for papCollections. */
    size_t              cCollectionsAllocated;
    /** Current entry collection pending display, the last may also be pending
     * sorting. */
    PPRTCMDLSCOLLECTION papCollections;
    /** @} */
} RTCMDLSOPTS;
/** Pointer to ls options and state. */
typedef RTCMDLSOPTS *PRTCMDLSOPTS;




/** @callback_method_impl{FNRTSORTCMP, Dirs first + Unsorted} */
static DECLCALLBACK(int) rtCmdLsEntryCmpDirFirstUnsorted(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    return !RTFS_IS_DIRECTORY(pEntry1->Info.Attr.fMode) - !RTFS_IS_DIRECTORY(pEntry2->Info.Attr.fMode);
}


/** @callback_method_impl{FNRTSORTCMP, Name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpName(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    return RTStrCmp(pEntry1->szName, pEntry2->szName);
}


/** @callback_method_impl{FNRTSORTCMP, Dirs first + Name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpDirFirstName(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    int iDiff = !RTFS_IS_DIRECTORY(pEntry1->Info.Attr.fMode) - !RTFS_IS_DIRECTORY(pEntry2->Info.Attr.fMode);
    if (!iDiff)
        iDiff = rtCmdLsEntryCmpName(pEntry1, pEntry2, pvUser);
    return iDiff;
}


/** @callback_method_impl{FNRTSORTCMP, extension} */
static DECLCALLBACK(int) rtCmdLsEntryCmpExtension(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    int iDiff = RTStrCmp(RTPathSuffix(pEntry1->szName), RTPathSuffix(pEntry2->szName));
    if (!iDiff)
        iDiff = RTStrCmp(pEntry1->szName, pEntry2->szName);
    return iDiff;
}


/** @callback_method_impl{FNRTSORTCMP, Dirs first + Ext + Name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpDirFirstExtension(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    int iDiff = !RTFS_IS_DIRECTORY(pEntry1->Info.Attr.fMode) - !RTFS_IS_DIRECTORY(pEntry2->Info.Attr.fMode);
    if (!iDiff)
        iDiff = rtCmdLsEntryCmpExtension(pEntry1, pEntry2, pvUser);
    return iDiff;
}


/** @callback_method_impl{FNRTSORTCMP, Allocated size + Name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpAllocated(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    if (pEntry1->Info.cbAllocated == pEntry2->Info.cbAllocated)
        return rtCmdLsEntryCmpName(pEntry1, pEntry2, pvUser);
    return pEntry1->Info.cbAllocated < pEntry2->Info.cbAllocated ? -1 : 1;
}


/** @callback_method_impl{FNRTSORTCMP, Dirs first + Allocated size + Name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpDirFirstAllocated(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    int iDiff = !RTFS_IS_DIRECTORY(pEntry1->Info.Attr.fMode) - !RTFS_IS_DIRECTORY(pEntry2->Info.Attr.fMode);
    if (!iDiff)
        iDiff = rtCmdLsEntryCmpAllocated(pEntry1, pEntry2, pvUser);
    return iDiff;
}


/** @callback_method_impl{FNRTSORTCMP, Content size + Name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpSize(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    if (pEntry1->Info.cbObject == pEntry2->Info.cbObject)
        return rtCmdLsEntryCmpName(pEntry1, pEntry2, pvUser);
    return pEntry1->Info.cbObject < pEntry2->Info.cbObject ? -1 : 1;
}


/** @callback_method_impl{FNRTSORTCMP, Dirs first + Content size + Name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpDirFirstSize(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    int iDiff = !RTFS_IS_DIRECTORY(pEntry1->Info.Attr.fMode) - !RTFS_IS_DIRECTORY(pEntry2->Info.Attr.fMode);
    if (!iDiff)
        iDiff = rtCmdLsEntryCmpSize(pEntry1, pEntry2, pvUser);
    return iDiff;
}


/** @callback_method_impl{FNRTSORTCMP, Modification time + name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpMTime(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    int iDiff = RTTimeSpecCompare(&pEntry1->Info.ModificationTime, &pEntry2->Info.ModificationTime);
    if (!iDiff)
        iDiff = rtCmdLsEntryCmpName(pEntry1, pEntry2, pvUser);
    return iDiff;
}


/** @callback_method_impl{FNRTSORTCMP, Dirs first + Modification time + Name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpDirFirstMTime(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    int iDiff = !RTFS_IS_DIRECTORY(pEntry1->Info.Attr.fMode) - !RTFS_IS_DIRECTORY(pEntry2->Info.Attr.fMode);
    if (!iDiff)
        iDiff = rtCmdLsEntryCmpMTime(pEntry1, pEntry2, pvUser);
    return iDiff;
}


/** @callback_method_impl{FNRTSORTCMP, Birth time + name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpBTime(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    int iDiff = RTTimeSpecCompare(&pEntry1->Info.BirthTime, &pEntry2->Info.BirthTime);
    if (!iDiff)
        iDiff = rtCmdLsEntryCmpName(pEntry1, pEntry2, pvUser);
    return iDiff;
}


/** @callback_method_impl{FNRTSORTCMP, Dirs first + Birth time + Name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpDirFirstBTime(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    int iDiff = !RTFS_IS_DIRECTORY(pEntry1->Info.Attr.fMode) - !RTFS_IS_DIRECTORY(pEntry2->Info.Attr.fMode);
    if (!iDiff)
        iDiff = rtCmdLsEntryCmpBTime(pEntry1, pEntry2, pvUser);
    return iDiff;
}


/** @callback_method_impl{FNRTSORTCMP, Change time + name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpCTime(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    int iDiff = RTTimeSpecCompare(&pEntry1->Info.ChangeTime, &pEntry2->Info.ChangeTime);
    if (!iDiff)
        iDiff = rtCmdLsEntryCmpName(pEntry1, pEntry2, pvUser);
    return iDiff;
}


/** @callback_method_impl{FNRTSORTCMP, Dirs first + Change time + Name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpDirFirstCTime(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    int iDiff = !RTFS_IS_DIRECTORY(pEntry1->Info.Attr.fMode) - !RTFS_IS_DIRECTORY(pEntry2->Info.Attr.fMode);
    if (!iDiff)
        iDiff = rtCmdLsEntryCmpCTime(pEntry1, pEntry2, pvUser);
    return iDiff;
}


/** @callback_method_impl{FNRTSORTCMP, Accessed time + name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpATime(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    int iDiff = RTTimeSpecCompare(&pEntry1->Info.AccessTime, &pEntry2->Info.AccessTime);
    if (!iDiff)
        iDiff = rtCmdLsEntryCmpName(pEntry1, pEntry2, pvUser);
    return iDiff;
}


/** @callback_method_impl{FNRTSORTCMP, Dirs first + Accessed time + Name} */
static DECLCALLBACK(int) rtCmdLsEntryCmpDirFirstATime(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    int iDiff = !RTFS_IS_DIRECTORY(pEntry1->Info.Attr.fMode) - !RTFS_IS_DIRECTORY(pEntry2->Info.Attr.fMode);
    if (!iDiff)
        iDiff = rtCmdLsEntryCmpATime(pEntry1, pEntry2, pvUser);
    return iDiff;
}


/** @callback_method_impl{FNRTSORTCMP, Name as version} */
static DECLCALLBACK(int) rtCmdLsEntryCmpVersion(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    return RTStrVersionCompare(pEntry1->szName, pEntry2->szName);
}


/** @callback_method_impl{FNRTSORTCMP, Dirs first + Name as version} */
static DECLCALLBACK(int) rtCmdLsEntryCmpDirFirstVersion(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF(pvUser);
    PRTCMDLSENTRY pEntry1 = (PRTCMDLSENTRY)pvElement1;
    PRTCMDLSENTRY pEntry2 = (PRTCMDLSENTRY)pvElement2;
    int iDiff = !RTFS_IS_DIRECTORY(pEntry1->Info.Attr.fMode) - !RTFS_IS_DIRECTORY(pEntry2->Info.Attr.fMode);
    if (!iDiff)
        iDiff = rtCmdLsEntryCmpVersion(pEntry1, pEntry2, pvUser);
    return iDiff;
}


/**
 * Sorts the entries in the collections according the sorting options.
 *
 * @param   pOpts           The options and state.
 */
static void rtCmdLsSortCollections(PRTCMDLSOPTS pOpts)
{
    /*
     * Sort the entries in each collection.
     */
    PFNRTSORTCMP pfnCmp;
    switch (pOpts->enmSort)
    {
        case RTCMDLSSORT_NONE:
            pfnCmp = pOpts->fGroupDirectoriesFirst ? rtCmdLsEntryCmpDirFirstUnsorted      : NULL;
            break;
        default: AssertFailed(); RT_FALL_THRU();
        case RTCMDLSSORT_NAME:
            pfnCmp = pOpts->fGroupDirectoriesFirst ? rtCmdLsEntryCmpDirFirstName          : rtCmdLsEntryCmpName;
            break;
        case RTCMDLSSORT_EXTENSION:
            pfnCmp = pOpts->fGroupDirectoriesFirst ? rtCmdLsEntryCmpDirFirstExtension     : rtCmdLsEntryCmpExtension;
            break;
        case RTCMDLSSORT_SIZE:
            if (pOpts->fShowAllocatedSize)
                pfnCmp = pOpts->fGroupDirectoriesFirst ? rtCmdLsEntryCmpDirFirstAllocated : rtCmdLsEntryCmpAllocated;
            else
                pfnCmp = pOpts->fGroupDirectoriesFirst ? rtCmdLsEntryCmpDirFirstSize      : rtCmdLsEntryCmpSize;
            break;
        case RTCMDLSSORT_TIME:
            switch (pOpts->enmTime)
            {
                default: AssertFailed(); RT_FALL_THRU();
                case RTCMDLSTIME_MTIME: pfnCmp = pOpts->fGroupDirectoriesFirst ? rtCmdLsEntryCmpDirFirstMTime : rtCmdLsEntryCmpMTime; break;
                case RTCMDLSTIME_BTIME: pfnCmp = pOpts->fGroupDirectoriesFirst ? rtCmdLsEntryCmpDirFirstBTime : rtCmdLsEntryCmpBTime; break;
                case RTCMDLSTIME_CTIME: pfnCmp = pOpts->fGroupDirectoriesFirst ? rtCmdLsEntryCmpDirFirstCTime : rtCmdLsEntryCmpCTime; break;
                case RTCMDLSTIME_ATIME: pfnCmp = pOpts->fGroupDirectoriesFirst ? rtCmdLsEntryCmpDirFirstATime : rtCmdLsEntryCmpATime; break;
            }
            break;
        case RTCMDLSSORT_VERSION:
            pfnCmp = pOpts->fGroupDirectoriesFirst ? rtCmdLsEntryCmpDirFirstVersion       : rtCmdLsEntryCmpVersion;
            break;
    }
    if (pfnCmp)
    {
        /*
         * Walk thru the collections and sort their entries.
         */
        size_t i = pOpts->cCollections;
        while (i-- > 0)
        {
            PRTCMDLSCOLLECTION pCollection = pOpts->papCollections[i];
            RTSortApvShell((void **)pCollection->papEntries, pCollection->cEntries, pfnCmp, NULL);

            if (pOpts->fReverseSort)
            {
                PPRTCMDLSENTRY papEntries  = pCollection->papEntries;
                size_t         iHead       = 0;
                size_t         iTail       = pCollection->cEntries;
                while (iHead < iTail)
                {
                    PRTCMDLSENTRY pTmp = papEntries[iHead];
                    papEntries[iHead]  = papEntries[iTail];
                    papEntries[iTail]  = pTmp;
                    iHead++;
                    iTail--;
                }
            }
        }
    }

    /** @todo sort the collections too, except for the first one. */
}


/**
 * Format human readable size.
 */
static const char *rtCmdLsFormatSizeHumanReadable(PRTCMDLSOPTS pOpts, uint64_t cb, char *pszDst, size_t cbDst)
{
    if (pOpts->fHumanReadableSizes)
    {
        if (!pOpts->fSiUnits)
        {
            size_t cch = RTStrPrintf(pszDst, cbDst, "%Rhub", cb);
            if (pszDst[cch - 1] == 'i')
                pszDst[cch - 1] = '\0'; /* drop the trailing 'i' */
        }
        else
            RTStrPrintf(pszDst, cbDst, "%Rhui", cb);
    }
    else if (pOpts->cbBlock)
        RTStrFormatU64(pszDst, cbDst, (cb + pOpts->cbBlock - 1) / pOpts->cbBlock, 10, 0, 0, 0);
    else
        RTStrFormatU64(pszDst, cbDst, cb, 10, 0, 0, 0);
    return pszDst;
}


/**
 * Format block count.
 */
static const char *rtCmdLsFormatBlocks(PRTCMDLSOPTS pOpts, uint64_t cb, char *pszDst, size_t cbDst)
{
    if (pOpts->fHumanReadableSizes)
        return rtCmdLsFormatSizeHumanReadable(pOpts, cb, pszDst, cbDst);

    uint32_t cbBlock = pOpts->cbBlock;
    if (cbBlock == 0)
        cbBlock = _1K;
    RTStrFormatU64(pszDst, cbDst, (cb + cbBlock / 2 - 1) / cbBlock, 10, 0, 0, 0);
    return pszDst;
}


/**
 * Format file size.
 */
static const char *rtCmdLsFormatSize(PRTCMDLSOPTS pOpts, uint64_t cb, char *pszDst, size_t cbDst)
{
    if (pOpts->fHumanReadableSizes)
        return rtCmdLsFormatSizeHumanReadable(pOpts, cb, pszDst, cbDst);
    if (pOpts->cbBlock > 0)
        return rtCmdLsFormatBlocks(pOpts, cb, pszDst, cbDst);
    RTStrFormatU64(pszDst, cbDst, cb, 10, 0, 0, 0);
    return pszDst;
}


/**
 * Format name, i.e. escape, hide, quote stuff.
 */
static const char *rtCmdLsFormatName(PRTCMDLSOPTS pOpts, const char *pszName, char *pszDst, size_t cbDst)
{
    if (   !pOpts->fEscapeNonGraphicChars
        && !pOpts->fEscapeControlChars
        && !pOpts->fHideControlChars)
        return pszName;
    /** @todo implement name formatting.   */
    RT_NOREF(pszDst, cbDst);
    return pszName;
}


/**
 * Figures out the length for a 32-bit number when formatted as decimal.
 * @returns Number of digits.
 * @param   uValue              The number.
 */
DECLINLINE(size_t) rtCmdLsDecimalFormatLengthU32(uint32_t uValue)
{
    if (uValue < 10)
        return 1;
    if (uValue < 100)
        return 2;
    if (uValue < 1000)
        return 3;
    if (uValue < 10000)
        return 4;
    if (uValue < 100000)
        return 5;
    if (uValue < 1000000)
        return 6;
    if (uValue < 10000000)
        return 7;
    if (uValue < 100000000)
        return 8;
    if (uValue < 1000000000)
        return 9;
    return 10;
}


/**
 * Formats the given group ID according to the specified options.
 *
 * @returns pszDst
 * @param   pOpts           The options and state.
 * @param   gid             The GID to format.
 * @param   pszOwner        The owner returned by the FS.
 * @param   pszDst          The output buffer.
 * @param   cbDst           The output buffer size.
 */
static const char *rtCmdLsDecimalFormatGroup(PRTCMDLSOPTS pOpts, RTGID gid, const char *pszGroup, char *pszDst, size_t cbDst)
{
    if (!pOpts->fNumericalIds)
    {
        if (pszGroup)
        {
            RTStrCopy(pszDst, cbDst, pszGroup);
            return pszDst;
        }
        if (gid == NIL_RTGID)
            return "<Nil>";
    }
    RTStrFormatU64(pszDst, cbDst, gid, 10, 0, 0, 0);
    return pszDst;
}


/**
 * Formats the given user ID according to the specified options.
 *
 * @returns pszDst
 * @param   pOpts           The options and state.
 * @param   uid             The UID to format.
 * @param   pszOwner        The owner returned by the FS.
 * @param   pszDst          The output buffer.
 * @param   cbDst           The output buffer size.
 */
static const char *rtCmdLsDecimalFormatOwner(PRTCMDLSOPTS pOpts, RTUID uid, const char *pszOwner, char *pszDst, size_t cbDst)
{
    if (!pOpts->fNumericalIds)
    {
        if (pszOwner)
        {
            RTStrCopy(pszDst, cbDst, pszOwner);
            return pszDst;
        }
        if (uid == NIL_RTUID)
            return "<Nil>";
    }
    RTStrFormatU64(pszDst, cbDst, uid, 10, 0, 0, 0);
    return pszDst;
}


/**
 * Formats the given timestamp according to the desired --time-style.
 *
 * @returns pszDst
 * @param   pOpts           The options and state.
 * @param   pTimestamp      The timestamp.
 * @param   pszDst          The output buffer.
 * @param   cbDst           The output buffer size.
 */
static const char *rtCmdLsFormatTimestamp(PRTCMDLSOPTS pOpts, PCRTTIMESPEC pTimestamp, char *pszDst, size_t cbDst)
{
    /** @todo timestamp formatting according to the given style.   */
    RT_NOREF(pOpts);
    return RTTimeSpecToString(pTimestamp, pszDst, cbDst);
}



/**
 * RTCMDLSFORMAT_MACHINE_READABLE: --machine-readable
 */
static RTEXITCODE rtCmdLsDisplayCollectionInMachineReadableFormat(PRTCMDLSOPTS pOpts, PRTCMDLSCOLLECTION pCollection,
                                                                  char *pszTmp, size_t cbTmp)
{
    RT_NOREF(pOpts, pCollection, pszTmp, cbTmp);
    RTMsgError("Machine readable format not implemented\n");
    return RTEXITCODE_FAILURE;
}


/**
 * RTCMDLSFORMAT_COMMAS: -m
 */
static RTEXITCODE rtCmdLsDisplayCollectionInCvsFormat(PRTCMDLSOPTS pOpts, PRTCMDLSCOLLECTION pCollection,
                                                      char *pszTmp, size_t cbTmp)
{
    RT_NOREF(pOpts, pCollection, pszTmp, cbTmp);
    RTMsgError("Table output formats not implemented\n");
    return RTEXITCODE_FAILURE;
}


/**
 * RTCMDLSFORMAT_LONG: -l
 */
static RTEXITCODE rtCmdLsDisplayCollectionInLongFormat(PRTCMDLSOPTS pOpts, PRTCMDLSCOLLECTION pCollection,
                                                       char *pszTmp, size_t cbTmp, size_t cchAllocatedCol)
{
    /*
     * Figure the width of the size, the link count, the uid, the gid, and the inode columns.
     */
    size_t cchSizeCol  = 1;
    size_t cchLinkCol  = 1;
    size_t cchUidCol   = pOpts->fShowOwner ? 1 : 0;
    size_t cchGidCol   = pOpts->fShowGroup ? 1 : 0;
    size_t cchINodeCol = pOpts->fShowINode ? 1 : 0;

    size_t i = pCollection->cEntries;
    while (i-- > 0)
    {
        PRTCMDLSENTRY pEntry = pCollection->papEntries[i];

        rtCmdLsFormatSize(pOpts, pEntry->Info.cbObject, pszTmp, cbTmp);
        size_t cchTmp = strlen(pszTmp);
        if (cchTmp > cchSizeCol)
            cchSizeCol = cchTmp;

        cchTmp = rtCmdLsDecimalFormatLengthU32(pEntry->Info.Attr.u.Unix.cHardlinks) + 1;
        if (cchTmp > cchLinkCol)
            cchLinkCol = cchTmp;

        if (pOpts->fShowOwner)
        {
            rtCmdLsDecimalFormatOwner(pOpts, pEntry->Info.Attr.u.Unix.uid, pEntry->pszOwner, pszTmp, cbTmp);
            cchTmp = strlen(pszTmp);
            if (cchTmp > cchUidCol)
                cchUidCol = cchTmp;
        }

        if (pOpts->fShowGroup)
        {
            rtCmdLsDecimalFormatGroup(pOpts, pEntry->Info.Attr.u.Unix.gid, pEntry->pszGroup, pszTmp, cbTmp);
            cchTmp = strlen(pszTmp);
            if (cchTmp > cchGidCol)
                cchGidCol = cchTmp;
        }

        if (pOpts->fShowINode)
        {
            cchTmp = RTStrFormatU64(pszTmp, cchTmp, pEntry->Info.Attr.u.Unix.INodeId, 10, 0, 0, 0);
            if (cchTmp > cchINodeCol)
                cchINodeCol = cchTmp;
        }
    }

    /*
     * Determin time member offset.
     */
    size_t offTime;
    switch (pOpts->enmTime)
    {
        default: AssertFailed(); RT_FALL_THRU();
        case RTCMDLSTIME_MTIME: offTime = RT_UOFFSETOF(RTCMDLSENTRY, Info.ModificationTime); break;
        case RTCMDLSTIME_BTIME: offTime = RT_UOFFSETOF(RTCMDLSENTRY, Info.BirthTime); break;
        case RTCMDLSTIME_CTIME: offTime = RT_UOFFSETOF(RTCMDLSENTRY, Info.ChangeTime); break;
        case RTCMDLSTIME_ATIME: offTime = RT_UOFFSETOF(RTCMDLSENTRY, Info.AccessTime); break;
    }

    /*
     * Display the entries.
     */
    for (i = 0; i < pCollection->cEntries; i++)
    {
        PRTCMDLSENTRY pEntry = pCollection->papEntries[i];

        if (cchINodeCol)
            RTPrintf("%*RU64 ", cchINodeCol, pEntry->Info.Attr.u.Unix.INodeId);
        if (cchAllocatedCol)
            RTPrintf("%*s ", cchAllocatedCol, rtCmdLsFormatBlocks(pOpts, pEntry->Info.cbAllocated, pszTmp, cbTmp));

        RTFMODE fMode = pEntry->Info.Attr.fMode;
        switch (fMode & RTFS_TYPE_MASK)
        {
            case RTFS_TYPE_FIFO:        RTPrintf("f"); break;
            case RTFS_TYPE_DEV_CHAR:    RTPrintf("c"); break;
            case RTFS_TYPE_DIRECTORY:   RTPrintf("d"); break;
            case RTFS_TYPE_DEV_BLOCK:   RTPrintf("b"); break;
            case RTFS_TYPE_FILE:        RTPrintf("-"); break;
            case RTFS_TYPE_SYMLINK:     RTPrintf("l"); break;
            case RTFS_TYPE_SOCKET:      RTPrintf("s"); break;
            case RTFS_TYPE_WHITEOUT:    RTPrintf("w"); break;
            default:                    RTPrintf("?"); AssertFailed(); break;
        }
        /** @todo sticy bits++ */
        RTPrintf("%c%c%c",
                 fMode & RTFS_UNIX_IRUSR ? 'r' : '-',
                 fMode & RTFS_UNIX_IWUSR ? 'w' : '-',
                 fMode & RTFS_UNIX_IXUSR ? 'x' : '-');
        RTPrintf("%c%c%c",
                 fMode & RTFS_UNIX_IRGRP ? 'r' : '-',
                 fMode & RTFS_UNIX_IWGRP ? 'w' : '-',
                 fMode & RTFS_UNIX_IXGRP ? 'x' : '-');
        RTPrintf("%c%c%c",
                 fMode & RTFS_UNIX_IROTH ? 'r' : '-',
                 fMode & RTFS_UNIX_IWOTH ? 'w' : '-',
                 fMode & RTFS_UNIX_IXOTH ? 'x' : '-');
        if (1)
        {
            RTPrintf(" %c%c%c%c%c%c%c%c%c%c%c%c%c%c",
                     fMode & RTFS_DOS_READONLY          ? 'R' : '-',
                     fMode & RTFS_DOS_HIDDEN            ? 'H' : '-',
                     fMode & RTFS_DOS_SYSTEM            ? 'S' : '-',
                     fMode & RTFS_DOS_DIRECTORY         ? 'D' : '-',
                     fMode & RTFS_DOS_ARCHIVED          ? 'A' : '-',
                     fMode & RTFS_DOS_NT_DEVICE         ? 'd' : '-',
                     fMode & RTFS_DOS_NT_NORMAL         ? 'N' : '-',
                     fMode & RTFS_DOS_NT_TEMPORARY      ? 'T' : '-',
                     fMode & RTFS_DOS_NT_SPARSE_FILE    ? 'P' : '-',
                     fMode & RTFS_DOS_NT_REPARSE_POINT  ? 'J' : '-',
                     fMode & RTFS_DOS_NT_COMPRESSED     ? 'C' : '-',
                     fMode & RTFS_DOS_NT_OFFLINE        ? 'O' : '-',
                     fMode & RTFS_DOS_NT_NOT_CONTENT_INDEXED ? 'I' : '-',
                     fMode & RTFS_DOS_NT_ENCRYPTED      ? 'E' : '-');
        }
        RTPrintf(" %*u", cchLinkCol, pEntry->Info.Attr.u.Unix.cHardlinks);
        if (cchUidCol)
            RTPrintf(" %*s", cchUidCol,
                     rtCmdLsDecimalFormatOwner(pOpts, pEntry->Info.Attr.u.Unix.uid, pEntry->pszOwner, pszTmp, cbTmp));
        if (cchGidCol)
            RTPrintf(" %*s", cchGidCol,
                     rtCmdLsDecimalFormatGroup(pOpts, pEntry->Info.Attr.u.Unix.gid, pEntry->pszGroup, pszTmp, cbTmp));
        RTPrintf(" %*s", cchSizeCol, rtCmdLsFormatSize(pOpts, pEntry->Info.cbObject, pszTmp, cbTmp));

        PCRTTIMESPEC pTime = (PCRTTIMESPEC)((uintptr_t)pEntry + offTime);
        RTPrintf(" %s", rtCmdLsFormatTimestamp(pOpts, pTime, pszTmp, cbTmp));

        RTPrintf(" %s\n", rtCmdLsFormatName(pOpts, pEntry->szName, pszTmp, cbTmp));
    }

    return RTEXITCODE_SUCCESS;
}


/**
 * RTCMDLSFORMAT_SINGLE: -1
 */
static RTEXITCODE rtCmdLsDisplayCollectionInSingleFormat(PRTCMDLSOPTS pOpts, PRTCMDLSCOLLECTION pCollection,
                                                         char *pszTmp, size_t cbTmp, size_t cchAllocatedCol)
{
    if (cchAllocatedCol > 0)
        for (size_t i = 0; i < pCollection->cEntries; i++)
        {
            PRTCMDLSENTRY pEntry = pCollection->papEntries[i];
            RTPrintf("%*s %s\n",
                     cchAllocatedCol, rtCmdLsFormatBlocks(pOpts, pEntry->Info.cbAllocated, pszTmp, cbTmp / 4),
                     rtCmdLsFormatName(pOpts, pEntry->szName, &pszTmp[cbTmp / 4], cbTmp / 4 * 3));
        }
    else
        for (size_t i = 0; i < pCollection->cEntries; i++)
        {
            PRTCMDLSENTRY pEntry = pCollection->papEntries[i];
            RTPrintf("%s\n", rtCmdLsFormatName(pOpts, pEntry->szName, pszTmp, cbTmp));
        }

    return RTEXITCODE_SUCCESS;
}


/**
 * RTCMDLSFORMAT_COLS_VERTICAL: default, -C; RTCMDLSFORMAT_COLS_HORIZONTAL: -x
 */
static RTEXITCODE rtCmdLsDisplayCollectionInTableFormat(PRTCMDLSOPTS pOpts, PRTCMDLSCOLLECTION pCollection,
                                                        char *pszTmp, size_t cbTmp, size_t cchAllocatedCol)
{
    RT_NOREF(pOpts, pCollection, pszTmp, cbTmp, cchAllocatedCol);
    RTMsgError("Table output formats not implemented\n");
    return RTEXITCODE_FAILURE;
}


/**
 * Does the actual displaying of the entry collections.
 *
 * @returns Program exit code.
 * @param   pOpts           The options and state.
 */
static RTEXITCODE rtCmdLsDisplayCollections(PRTCMDLSOPTS pOpts)
{
    rtCmdLsSortCollections(pOpts);

    bool const fNeedCollectionName = pOpts->cCollections > 2
                                  || (   pOpts->cCollections == 2
                                      && pOpts->papCollections[0]->cEntries > 0);
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    for (size_t iCollection = 0; iCollection < pOpts->cCollections; iCollection++)
    {
        PRTCMDLSCOLLECTION  pCollection = pOpts->papCollections[iCollection];
        char                szTmp[RTPATH_MAX*2];

        /* The header. */
        if (iCollection != 0)
        {
            if (   iCollection > 1
                || pOpts->papCollections[0]->cEntries > 0)
                RTPrintf("\n");
            if (fNeedCollectionName)
                RTPrintf("%s:\n", rtCmdLsFormatName(pOpts, pCollection->szName, szTmp, sizeof(szTmp)));
            RTPrintf("total %s\n", rtCmdLsFormatBlocks(pOpts, pCollection->cbTotalAllocated, szTmp, sizeof(szTmp)));
        }

        /* Format the entries. */
        RTEXITCODE rcExit2;
        if (pOpts->enmFormat == RTCMDLSFORMAT_MACHINE_READABLE)
            rcExit2 = rtCmdLsDisplayCollectionInMachineReadableFormat(pOpts, pCollection, szTmp, sizeof(szTmp));
        else if (pOpts->enmFormat == RTCMDLSFORMAT_COMMAS)
            rcExit2 = rtCmdLsDisplayCollectionInCvsFormat(pOpts, pCollection, szTmp, sizeof(szTmp));
        else
        {
            /* If the allocated size is requested, calculate the column width.  */
            size_t cchAllocatedCol = 0;
            if (pOpts->fShowAllocatedSize)
            {
                size_t i = pCollection->cEntries;
                while (i-- > 0)
                {
                    rtCmdLsFormatBlocks(pOpts, pCollection->papEntries[i]->Info.cbAllocated, szTmp, sizeof(szTmp));
                    size_t cchTmp = strlen(szTmp);
                    if (cchTmp > cchAllocatedCol)
                        cchAllocatedCol = cchTmp;
                }
            }

            /* Do the individual formatting. */
            if (pOpts->enmFormat == RTCMDLSFORMAT_LONG)
                rcExit2 = rtCmdLsDisplayCollectionInLongFormat(pOpts, pCollection, szTmp, sizeof(szTmp), cchAllocatedCol);
            else if (pOpts->enmFormat == RTCMDLSFORMAT_SINGLE)
                rcExit2 = rtCmdLsDisplayCollectionInSingleFormat(pOpts, pCollection, szTmp, sizeof(szTmp), cchAllocatedCol);
            else
                rcExit2 = rtCmdLsDisplayCollectionInTableFormat(pOpts, pCollection, szTmp, sizeof(szTmp), cchAllocatedCol);
        }
        if (rcExit2 != RTEXITCODE_SUCCESS)
            rcExit = rcExit2;
    }
    return rcExit;
}


/**
 * Frees all collections and their entries.
 * @param   pOpts           The options and state.
 */
static void rtCmdLsFreeCollections(PRTCMDLSOPTS pOpts)
{
    size_t i = pOpts->cCollections;
    while (i-- > 0)
    {
        PRTCMDLSCOLLECTION pCollection = pOpts->papCollections[i];
        PPRTCMDLSENTRY     papEntries  = pCollection->papEntries;
        size_t             j           = pCollection->cEntries;
        while (j-- > 0)
        {
            RTMemFree(papEntries[j]);
            papEntries[j] = NULL;
        }
        RTMemFree(papEntries);
        pCollection->papEntries        = NULL;
        pCollection->cEntries          = 0;
        pCollection->cEntriesAllocated = 0;
        RTMemFree(pCollection);
        pOpts->papCollections[i] = NULL;
    }

    RTMemFree(pOpts->papCollections);
    pOpts->papCollections        = NULL;
    pOpts->cCollections          = 0;
    pOpts->cCollectionsAllocated = 0;
}


/**
 * Allocates a new collection.
 *
 * @returns Pointer to the collection.
 * @param   pOpts           The options and state.
 * @param   pszName         The collection name.  Empty for special first
 *                          collection.
 */
static PRTCMDLSCOLLECTION rtCmdLsNewCollection(PRTCMDLSOPTS pOpts, const char *pszName)
{
    /* Grow the pointer table? */
    if (pOpts->cCollections >= pOpts->cCollectionsAllocated)
    {
        size_t cNew = pOpts->cCollectionsAllocated ? pOpts->cCollectionsAllocated * 2 : 16;
        void *pvNew = RTMemRealloc(pOpts->papCollections, cNew * sizeof(pOpts->papCollections[0]));
        if (!pvNew)
        {
            RTMsgError("Out of memory! (resize collections)");
            return NULL;
        }
        pOpts->cCollectionsAllocated = cNew;
        pOpts->papCollections = (PPRTCMDLSCOLLECTION)pvNew;

        /* If this is the first time and pszName isn't empty, add the zero'th
           entry for the command line stuff (hardcoded first collection). */
        if (   pOpts->cCollections == 0
            && *pszName)
        {
            PRTCMDLSCOLLECTION pCollection = (PRTCMDLSCOLLECTION)RTMemAllocZ(RT_UOFFSETOF(RTCMDLSCOLLECTION, szName[1]));
            if (!pCollection)
            {
                RTMsgError("Out of memory! (collection)");
                return NULL;
            }
            pOpts->papCollections[0] = pCollection;
            pOpts->cCollections      = 1;
        }
    }

    /* Add new collection. */
    size_t cbName = strlen(pszName) + 1;
    PRTCMDLSCOLLECTION pCollection = (PRTCMDLSCOLLECTION)RTMemAllocZ(RT_UOFFSETOF_DYN(RTCMDLSCOLLECTION, szName[cbName]));
    if (pCollection)
    {
        memcpy(pCollection->szName, pszName, cbName);
        pOpts->papCollections[pOpts->cCollections++] = pCollection;
    }
    else
        RTMsgError("Out of memory! (collection)");
    return pCollection;
}


/**
 * Adds one entry to a collection.
 * @returns Program exit code
 * @param   pCollection         The collection.
 * @param   pszEntry            The entry name.
 * @param   pInfo               The entry info.
 * @param   pszOwner            The owner name if available, otherwise NULL.
 * @param   pszGroup            The group anme if available, otherwise NULL.
 * @param   pszTarget           The symbolic link target if applicable and
 *                              available, otherwise NULL.
 */
static RTEXITCODE rtCmdLsAddOne(PRTCMDLSCOLLECTION pCollection, const char *pszEntry, PRTFSOBJINFO pInfo,
                                const char *pszOwner, const char *pszGroup, const char *pszTarget)
{

    /* Make sure there is space in the collection for the new entry. */
    if (pCollection->cEntries >= pCollection->cEntriesAllocated)
    {
        size_t cNew = pCollection->cEntriesAllocated ? pCollection->cEntriesAllocated * 2 : 16;
        void *pvNew = RTMemRealloc(pCollection->papEntries, cNew * sizeof(pCollection->papEntries[0]));
        if (!pvNew)
            return RTMsgErrorExitFailure("Out of memory! (resize entries)");
        pCollection->papEntries        = (PPRTCMDLSENTRY)pvNew;
        pCollection->cEntriesAllocated = cNew;
    }

    /* Create and insert a new entry. */
    size_t const cchEntry = strlen(pszEntry);
    size_t const cbOwner  = pszOwner  ? strlen(pszOwner)  + 1 : 0;
    size_t const cbGroup  = pszGroup  ? strlen(pszGroup)  + 1 : 0;
    size_t const cbTarget = pszTarget ? strlen(pszTarget) + 1 : 0;
    size_t const cbEntry  = RT_UOFFSETOF_DYN(RTCMDLSENTRY, szName[cchEntry + 1 + cbOwner + cbGroup + cbTarget]);
    PRTCMDLSENTRY pEntry = (PRTCMDLSENTRY)RTMemAlloc(cbEntry);
    if (pEntry)
    {
        pEntry->Info      = *pInfo;
        pEntry->pszTarget = NULL; /** @todo symbolic links. */
        pEntry->pszOwner  = NULL;
        pEntry->pszGroup  = NULL;
        pEntry->cchName   = cchEntry;
        memcpy(pEntry->szName, pszEntry, cchEntry);
        pEntry->szName[cchEntry] = '\0';

        char *psz = &pEntry->szName[cchEntry + 1];
        if (pszTarget)
        {
            pEntry->pszTarget = psz;
            memcpy(psz, pszTarget, cbTarget);
            psz += cbTarget;
        }
        if (pszOwner)
        {
            pEntry->pszOwner = psz;
            memcpy(psz, pszOwner, cbOwner);
            psz += cbOwner;
        }
        if (pszGroup)
        {
            pEntry->pszGroup = psz;
            memcpy(psz, pszGroup, cbGroup);
        }

        pCollection->papEntries[pCollection->cEntries++] = pEntry;
        pCollection->cbTotalAllocated += pEntry->Info.cbAllocated;
        pCollection->cbTotalFiles     += pEntry->Info.cbObject;
        return RTEXITCODE_SUCCESS;
    }
    return RTMsgErrorExitFailure("Out of memory! (entry)");
}


/**
 * Checks if the entry is to be filtered out.
 *
 * @returns true if filtered out, false if included.
 * @param   pOpts           The options and state.
 * @param   pszEntry        The entry name.
 * @param   pInfo           The entry info.
 */
static bool rtCmdLsIsFilteredOut(PRTCMDLSOPTS pOpts,  const char *pszEntry, PCRTFSOBJINFO pInfo)
{
    /*
     * Should we filter out this entry?
     */
    if (   !pOpts->fShowHidden
        && (pInfo->Attr.fMode & RTFS_DOS_HIDDEN))
        return true;

    size_t const cchEntry = strlen(pszEntry);
    if (   !pOpts->fShowDotAndDotDot
        && cchEntry <= 2
        && pszEntry[0] == '.'
        && (   cchEntry == 1
            || pszEntry[1] == '.' ))
        return true;

    if (   !pOpts->fShowBackups
        && pszEntry[cchEntry - 1] == '~')
        return true;
    return false;
}


/**
 * Processes a directory, recursing into subdirectories if desired.
 *
 * @returns Program exit code.
 * @param   pOpts       The options.
 * @param   hVfsDir     The directory.
 * @param   pszPath     Path buffer, RTPATH_MAX in size.
 * @param   cchPath     The length of the current path.
 * @param   pInfo       The parent information.
 */
static RTEXITCODE rtCmdLsProcessDirectory(PRTCMDLSOPTS pOpts, RTVFSDIR hVfsDir, char *pszPath, size_t cchPath, PCRTFSOBJINFO pInfo)
{
    /*
     * Create a new collection for this directory.
     */
    RT_NOREF(pInfo);
    PRTCMDLSCOLLECTION pCollection = rtCmdLsNewCollection(pOpts, pszPath);
    if (!pCollection)
        return RTEXITCODE_FAILURE;

    /*
     * Process the directory entries.
     */
    RTEXITCODE      rcExit            = RTEXITCODE_SUCCESS;
    size_t          cbDirEntryAlloced = sizeof(RTDIRENTRYEX);
    PRTDIRENTRYEX   pDirEntry         = (PRTDIRENTRYEX)RTMemTmpAlloc(cbDirEntryAlloced);
    if (!pDirEntry)
        return RTMsgErrorExitFailure("Out of memory! (direntry buffer)");

    for (;;)
    {
        /*
         * Read the next entry.
         */
        size_t cbDirEntry = cbDirEntryAlloced;
        int rc = RTVfsDirReadEx(hVfsDir, pDirEntry, &cbDirEntry, RTFSOBJATTRADD_UNIX);
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_BUFFER_OVERFLOW)
            {
                RTMemTmpFree(pDirEntry);
                cbDirEntryAlloced = RT_ALIGN_Z(RT_MIN(cbDirEntry, cbDirEntryAlloced) + 64, 64);
                pDirEntry  = (PRTDIRENTRYEX)RTMemTmpAlloc(cbDirEntryAlloced);
                if (pDirEntry)
                    continue;
                rcExit = RTMsgErrorExitFailure("Out of memory (direntry buffer)");
            }
            else if (rc != VERR_NO_MORE_FILES)
                rcExit = RTMsgErrorExitFailure("RTVfsDirReadEx failed: %Rrc\n", rc);
            break;
        }

        /*
         * Process the entry.
         */
        if (rtCmdLsIsFilteredOut(pOpts, pDirEntry->szName, &pDirEntry->Info))
            continue;


        const char *pszOwner = NULL;
        RTFSOBJINFO OwnerInfo;
        if (pDirEntry->Info.Attr.u.Unix.uid != NIL_RTUID && pOpts->fShowOwner)
        {
            rc = RTVfsDirQueryPathInfo(hVfsDir, pDirEntry->szName, &OwnerInfo, RTFSOBJATTRADD_UNIX_OWNER, RTPATH_F_ON_LINK);
            if (RT_SUCCESS(rc) && OwnerInfo.Attr.u.UnixOwner.szName[0])
                pszOwner = &OwnerInfo.Attr.u.UnixOwner.szName[0];
        }

        const char *pszGroup = NULL;
        RTFSOBJINFO GroupInfo;
        if (pDirEntry->Info.Attr.u.Unix.gid != NIL_RTGID && pOpts->fShowGroup)
        {
            rc = RTVfsDirQueryPathInfo(hVfsDir, pDirEntry->szName, &GroupInfo, RTFSOBJATTRADD_UNIX_GROUP, RTPATH_F_ON_LINK);
            if (RT_SUCCESS(rc) && GroupInfo.Attr.u.UnixGroup.szName[0])
                pszGroup = &GroupInfo.Attr.u.UnixGroup.szName[0];
        }

        RTEXITCODE rcExit2 = rtCmdLsAddOne(pCollection, pDirEntry->szName, &pDirEntry->Info, pszOwner, pszGroup, NULL);
        if (rcExit2 != RTEXITCODE_SUCCESS)
            rcExit = rcExit2;
    }

    RTMemTmpFree(pDirEntry);

    /*
     * Recurse into subdirectories if requested.
     */
    if (pOpts->fRecursive)
    {
        for (uint32_t i = 0; i < pCollection->cEntries; i++)
        {
            PRTCMDLSENTRY pEntry = pCollection->papEntries[i];
            if (RTFS_IS_SYMLINK(pEntry->Info.Attr.fMode))
            {
                if (!pOpts->fFollowSymlinksInDirs)
                    continue;
                /** @todo implement following symbolic links in the tree.   */
                continue;
            }
            else if (   !RTFS_IS_DIRECTORY(pEntry->Info.Attr.fMode)
                     || (   pEntry->szName[0] == '.'
                         && (   pEntry->szName[1] == '\0'
                             || (   pEntry->szName[1] == '.'
                                 && pEntry->szName[2] == '\0'))) )
                continue;

            /* Open subdirectory and process it. */
            RTVFSDIR hSubDir;
            int rc = RTVfsDirOpenDir(hVfsDir, pEntry->szName, 0 /*fFlags*/, &hSubDir);
            if (RT_SUCCESS(rc))
            {
                if (cchPath + 1 + pEntry->cchName  + 1 < RTPATH_MAX)
                {
                    pszPath[cchPath] = RTPATH_SLASH;
                    memcpy(&pszPath[cchPath + 1], pEntry->szName, pEntry->cchName + 1);
                    RTEXITCODE rcExit2 = rtCmdLsProcessDirectory(pOpts, hSubDir, pszPath,
                                                                 cchPath + 1 + pEntry->cchName, &pEntry->Info);
                    if (rcExit2 != RTEXITCODE_SUCCESS)
                        rcExit = rcExit2;
                    pszPath[cchPath] = '\0';
                }
                else
                    rcExit = RTMsgErrorExitFailure("Too deep recursion: %s%c%s", pszPath, RTPATH_SLASH, pEntry->szName);
                RTVfsDirRelease(hSubDir);
            }
            else
                rcExit = RTMsgErrorExitFailure("RTVfsDirOpenDir failed on %s in %s: %Rrc\n", pEntry->szName, pszPath, rc);
        }
    }
    return rcExit;
}


/**
 * Processes one argument.
 *
 * @returns Program exit code.
 * @param   pOpts               The options.
 * @param   pszArg              The argument.
 */
static RTEXITCODE rtCmdLsProcessArgument(PRTCMDLSOPTS pOpts, const char *pszArg)
{
    /*
     * Query info about the object 'pszArg' indicates.
     */
    RTERRINFOSTATIC     ErrInfo;
    uint32_t            offError;
    RTFSOBJINFO         Info;
    uint32_t            fPath = pOpts->fFollowSymlinkToAnyArgs ? RTPATH_F_FOLLOW_LINK : RTPATH_F_ON_LINK;
    int rc = RTVfsChainQueryInfo(pszArg, &Info, RTFSOBJATTRADD_UNIX, fPath, &offError, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
        return RTVfsChainMsgErrorExitFailure("RTVfsChainQueryInfo", pszArg, rc, offError, &ErrInfo.Core);

    /* Symbolic links requires special handling of course. */
    if (RTFS_IS_SYMLINK(Info.Attr.fMode))
    {
        if (pOpts->fFollowSymlinkToDirArgs)
        {
            RTFSOBJINFO Info2;
            rc = RTVfsChainQueryInfo(pszArg, &Info2, RTFSOBJATTRADD_UNIX, RTPATH_F_FOLLOW_LINK,
                                     &offError, RTErrInfoInitStatic(&ErrInfo));
            if (RT_SUCCESS(rc) && !RTFS_IS_DIRECTORY(Info.Attr.fMode))
                Info  = Info2;
        }
    }

    /*
     * If it's not a directory or we've been told to process directories
     * without going into them, just add it to the default collection.
     */
    if (   !pOpts->fFollowDirectoryArgs
        || !RTFS_IS_DIRECTORY(Info.Attr.fMode))
    {
        if (   pOpts->cCollections > 0
            || rtCmdLsNewCollection(pOpts, "") != NULL)
        {
            const char *pszOwner = NULL;
            RTFSOBJINFO OwnerInfo;
            if (Info.Attr.u.Unix.uid != NIL_RTUID && pOpts->fShowOwner)
            {
                rc = RTVfsChainQueryInfo(pszArg, &OwnerInfo, RTFSOBJATTRADD_UNIX_OWNER, fPath, NULL, NULL);
                if (RT_SUCCESS(rc) && OwnerInfo.Attr.u.UnixOwner.szName[0])
                    pszOwner = &OwnerInfo.Attr.u.UnixOwner.szName[0];
            }

            const char *pszGroup = NULL;
            RTFSOBJINFO GroupInfo;
            if (Info.Attr.u.Unix.gid != NIL_RTGID && pOpts->fShowGroup)
            {
                rc = RTVfsChainQueryInfo(pszArg, &GroupInfo, RTFSOBJATTRADD_UNIX_GROUP, fPath, NULL, NULL);
                if (RT_SUCCESS(rc) && GroupInfo.Attr.u.UnixGroup.szName[0])
                    pszGroup = &GroupInfo.Attr.u.UnixGroup.szName[0];
            }

            return rtCmdLsAddOne(pOpts->papCollections[0], pszArg, &Info, pszOwner, pszGroup, NULL);
        }
        return RTEXITCODE_FAILURE;
    }

    /*
     * Open the directory.
     */
    RTVFSDIR hVfsDir;
    rc = RTVfsChainOpenDir(pszArg, 0 /*fFlags*/, &hVfsDir, &offError, RTErrInfoInitStatic(&ErrInfo));
    if (RT_FAILURE(rc))
        return RTVfsChainMsgErrorExitFailure("RTVfsChainOpenDir", pszArg, rc, offError, &ErrInfo.Core);

    RTEXITCODE  rcExit;
    char        szPath[RTPATH_MAX];
    size_t      cchPath = strlen(pszArg);
    if (cchPath < sizeof(szPath))
    {
        memcpy(szPath, pszArg, cchPath + 1);
        rcExit = rtCmdLsProcessDirectory(pOpts, hVfsDir, szPath, cchPath, &Info);
    }
    else
        rcExit = RTMsgErrorExitFailure("Too long argument: %s", pszArg);
    RTVfsDirRelease(hVfsDir);
    return rcExit;
}


/**
 * A /bin/ls clone.
 *
 * @returns Program exit code.
 *
 * @param   cArgs               The number of arguments.
 * @param   papszArgs           The argument vector.  (Note that this may be
 *                              reordered, so the memory must be writable.)
 */
RTR3DECL(RTEXITCODE) RTFsCmdLs(unsigned cArgs, char **papszArgs)
{
    /*
     * Parse the command line.
     */
#define OPT_AUTHOR                                  1000
#define OPT_BLOCK_SIZE                              1001
#define OPT_COLOR                                   1002
#define OPT_FILE_TYPE                               1003
#define OPT_FORMAT                                  1004
#define OPT_FULL_TIME                               1005
#define OPT_GROUP_DIRECTORIES_FIRST                 1006
#define OPT_SI                                      1007
#define OPT_DEREFERENCE_COMMAND_LINE_SYMLINK_TO_DIR 1008
#define OPT_HIDE                                    1009
#define OPT_INDICATOR_STYLE                         1010
#define OPT_MACHINE_READABLE                        1011
#define OPT_SHOW_CONTROL_CHARS                      1012
#define OPT_QUOTING_STYLE                           1013
#define OPT_SORT                                    1014
#define OPT_TIME                                    1015
#define OPT_TIME_STYLE                              1016
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--all",                              'a',            RTGETOPT_REQ_NOTHING },
        { "--almost-all",                       'A',            RTGETOPT_REQ_NOTHING },
        //{ "--author",                           OPT_AUTHOR,     RTGETOPT_REQ_NOTHING },
        { "--escape",                           'b',            RTGETOPT_REQ_NOTHING },
        { "--block-size",                       OPT_BLOCK_SIZE, RTGETOPT_REQ_UINT32 },
        { "--ctime",                            'c',            RTGETOPT_REQ_NOTHING },
        //{ "--columns",                          'C',            RTGETOPT_REQ_NOTHING },
        //{ "--color",                            OPT_COLOR,      RTGETOPT_OPT_STRING },
        { "--directory",                        'd',            RTGETOPT_REQ_NOTHING },
        //{ "--dired",                            'D',            RTGETOPT_REQ_NOTHING },
        { "--dash-f",                           'f',            RTGETOPT_REQ_NOTHING },
        //{ "--classify",                         'F',            RTGETOPT_REQ_NOTHING },
        //{ "--file-type",                        OPT_FILE_TYPE,  RTGETOPT_REQ_NOTHING },
        { "--format",                           OPT_FORMAT,     RTGETOPT_REQ_STRING },
        { "--full-time",                        OPT_FULL_TIME,  RTGETOPT_REQ_NOTHING },
        { "--dash-g",                           'g',            RTGETOPT_REQ_NOTHING },
        { "--group-directories-first",          OPT_GROUP_DIRECTORIES_FIRST, RTGETOPT_REQ_NOTHING },
        { "--no-group",                         'G',            RTGETOPT_REQ_NOTHING },
        { "--human-readable",                   'h',            RTGETOPT_REQ_NOTHING },
        { "--si",                               OPT_SI,         RTGETOPT_REQ_NOTHING },
        { "--dereference-command-line",         'H',            RTGETOPT_REQ_NOTHING },
        { "--dereference-command-line-symlink-to-dir", OPT_DEREFERENCE_COMMAND_LINE_SYMLINK_TO_DIR, RTGETOPT_REQ_NOTHING },
        //{ "--hide"                              OPT_HIDE,       RTGETOPT_REQ_STRING },
        //{ "--indicator-style"                   OPT_INDICATOR_STYLE, RTGETOPT_REQ_STRING },
        { "--inode",                             'i',            RTGETOPT_REQ_NOTHING },
        { "--block-size-1kib",                   'k',            RTGETOPT_REQ_NOTHING },
        { "--long",                              'l',            RTGETOPT_REQ_NOTHING },
        { "--dereference",                       'L',            RTGETOPT_REQ_NOTHING },
        { "--format-commas",                     'm',            RTGETOPT_REQ_NOTHING },
        { "--machinereadable",                   OPT_MACHINE_READABLE, RTGETOPT_REQ_NOTHING },
        { "--machine-readable",                  OPT_MACHINE_READABLE, RTGETOPT_REQ_NOTHING },
        { "--numeric-uid-gid",                   'n',            RTGETOPT_REQ_NOTHING },
        { "--literal",                           'N',            RTGETOPT_REQ_NOTHING },
        { "--long-without-group-info",           'o',            RTGETOPT_REQ_NOTHING },
        //{ "--indicator-style",                   'p',            RTGETOPT_REQ_STRING },
        { "--hide-control-chars",                'q',            RTGETOPT_REQ_NOTHING },
        { "--show-control-chars",                OPT_SHOW_CONTROL_CHARS, RTGETOPT_REQ_NOTHING },
        //{ "--quote-name",                        'Q',            RTGETOPT_REQ_NOTHING },
        //{ "--quoting-style",                     OPT_QUOTING_STYLE, RTGETOPT_REQ_STRING },
        { "--reverse",                           'r',            RTGETOPT_REQ_NOTHING },
        { "--recursive",                         'R',            RTGETOPT_REQ_NOTHING },
        { "--size",                              's',            RTGETOPT_REQ_NOTHING },
        { "--sort-by-size",                      'S',            RTGETOPT_REQ_NOTHING },
        { "--sort",                              OPT_SORT,       RTGETOPT_REQ_STRING },
        { "--time",                              OPT_TIME,       RTGETOPT_REQ_STRING },
        { "--time-style",                        OPT_TIME_STYLE, RTGETOPT_REQ_STRING },
        { "--sort-by-time",                      't',            RTGETOPT_REQ_NOTHING },
        { "--tabsize",                           'T',            RTGETOPT_REQ_UINT8 },
        { "--atime",                             'u',            RTGETOPT_REQ_NOTHING },
        { "--unsorted",                          'U',            RTGETOPT_REQ_NOTHING },
        { "--version-sort",                      'v',            RTGETOPT_REQ_NOTHING },
        { "--width",                             'w',            RTGETOPT_REQ_UINT32 },
        { "--list-by-line",                      'x',            RTGETOPT_REQ_NOTHING },
        { "--sort-by-extension",                 'X',            RTGETOPT_REQ_NOTHING },
        { "--one-file-per-line",                 '1',            RTGETOPT_REQ_NOTHING },
        { "--help",                              '?',            RTGETOPT_REQ_NOTHING },
    };

    RTCMDLSOPTS Opts;
    Opts.fFollowSymlinksInDirs          = false;
    Opts.fFollowSymlinkToAnyArgs        = false;
    Opts.fFollowSymlinkToDirArgs        = false;
    Opts.fFollowDirectoryArgs           = true;
    Opts.fRecursive                     = false;
    Opts.fShowHidden                    = false;
    Opts.fShowDotAndDotDot              = false;
    Opts.fShowBackups                   = true;
    Opts.enmSort                        = RTCMDLSSORT_NAME;
    Opts.fReverseSort                   = false;
    Opts.fGroupDirectoriesFirst         = false;
    Opts.enmFormat                      = RTCMDLSFORMAT_COLS_VERTICAL;
    Opts.fEscapeNonGraphicChars         = false;
    Opts.fEscapeControlChars            = true;
    Opts.fHideControlChars              = false;
    Opts.fHumanReadableSizes            = false;    /**< -h */
    Opts.fSiUnits                       = false;
    Opts.cbBlock                        = 0;
    Opts.fShowOwner                     = true;
    Opts.fShowGroup                     = true;
    Opts.fNumericalIds                  = false;
    Opts.fShowINode                     = false;
    Opts.fShowAllocatedSize             = false;
    Opts.cchTab                         = 8;
    Opts.cchWidth                       = 80;
    Opts.enmColor                       = RTCMDLSCOLOR_NONE;
    Opts.enmTime                        = RTCMDLSTIME_MTIME;
    Opts.enmTimeStyle                   = RTCMDLSTIMESTYLE_LOCALE;
    Opts.pszTimeCustom                  = NULL;

    Opts.cCollections                   = 0;
    Opts.cCollectionsAllocated          = 0;
    Opts.papCollections                 = NULL;


    RTEXITCODE      rcExit      = RTEXITCODE_SUCCESS;
    unsigned        cProcessed  = 0;
    RTGETOPTSTATE   GetState;
    int rc = RTGetOptInit(&GetState, cArgs, papszArgs, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
                          RTGETOPTINIT_FLAGS_OPTS_FIRST);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "RTGetOptInit: %Rrc", rc);

    for (;;)
    {
        RTGETOPTUNION ValueUnion;
        int chOpt = RTGetOpt(&GetState, &ValueUnion);
        switch (chOpt)
        {
            case 0:
                /* When reaching the end of arguments without having processed any
                   files/dirs/whatever yet, we do the current directory. */
                if (cProcessed > 0)
                {
                    RTEXITCODE rcExit2 = rtCmdLsDisplayCollections(&Opts);
                    if (rcExit2 != RTEXITCODE_SUCCESS)
                        rcExit = rcExit2;
                    rtCmdLsFreeCollections(&Opts);
                    return rcExit;
                }
                ValueUnion.psz = ".";
                RT_FALL_THRU();
            case VINF_GETOPT_NOT_OPTION:
            {
                RTEXITCODE rcExit2 = rtCmdLsProcessArgument(&Opts, ValueUnion.psz);
                if (rcExit2 != RTEXITCODE_SUCCESS)
                    rcExit = rcExit2;
                cProcessed++;
                break;
            }

            case 'a':
                Opts.fShowHidden                = true;
                Opts.fShowDotAndDotDot          = true;
                break;

            case 'A':
                Opts.fShowHidden                = true;
                Opts.fShowDotAndDotDot          = false;
                break;

            case 'b':
                Opts.fEscapeNonGraphicChars     = true;
                break;

            case OPT_BLOCK_SIZE:
                if (!ValueUnion.u32)
                {
                    Assert(!Opts.papCollections);
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Invalid block size: %u", ValueUnion.u32);
                }
                Opts.cbBlock                    = ValueUnion.u32;
                Opts.fHumanReadableSizes        = false;
                Opts.fSiUnits                   = false;
                break;

            case 'c':
                Opts.enmTime                    = RTCMDLSTIME_CTIME;
                break;

            case 'C':
                Opts.enmFormat                  = RTCMDLSFORMAT_COLS_VERTICAL;
                break;

            case 'd':
                Opts.fFollowDirectoryArgs       = false;
                Opts.fFollowSymlinkToAnyArgs    = false;
                Opts.fFollowSymlinkToDirArgs    = false;
                Opts.fRecursive                 = false;
                break;

            case 'f':
                Opts.fShowHidden                = true;
                Opts.fShowDotAndDotDot          = true;
                if (Opts.enmFormat == RTCMDLSFORMAT_LONG)
                    Opts.enmFormat              = RTCMDLSFORMAT_COLS_VERTICAL;
                Opts.enmColor                   = RTCMDLSCOLOR_NONE;
                Opts.enmSort                    = RTCMDLSSORT_NONE;
                break;

            case OPT_FORMAT:
                if (   strcmp(ValueUnion.psz, "across") == 0
                    || strcmp(ValueUnion.psz, "horizontal") == 0)
                    Opts.enmFormat              = RTCMDLSFORMAT_COLS_HORIZONTAL;
                else if (strcmp(ValueUnion.psz, "commas") == 0)
                    Opts.enmFormat              = RTCMDLSFORMAT_COMMAS;
                else if (   strcmp(ValueUnion.psz, "long") == 0
                         || strcmp(ValueUnion.psz, "verbose") == 0)
                    Opts.enmFormat              = RTCMDLSFORMAT_LONG;
                else if (strcmp(ValueUnion.psz, "single-column") == 0)
                    Opts.enmFormat              = RTCMDLSFORMAT_SINGLE;
                else if (strcmp(ValueUnion.psz, "vertical") == 0)
                    Opts.enmFormat              = RTCMDLSFORMAT_COLS_VERTICAL;
                else if (strcmp(ValueUnion.psz, "machine-readable") == 0)
                    Opts.enmFormat              = RTCMDLSFORMAT_MACHINE_READABLE;
                else
                {
                    Assert(!Opts.papCollections);
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown format: %s", ValueUnion.psz);
                }
                break;

            case OPT_FULL_TIME:
                Opts.enmFormat                  = RTCMDLSFORMAT_LONG;
                Opts.enmTimeStyle               = RTCMDLSTIMESTYLE_FULL_ISO;
                break;

            case 'g':
                Opts.enmFormat                  = RTCMDLSFORMAT_LONG;
                Opts.fShowOwner                 = false;
                break;

            case OPT_GROUP_DIRECTORIES_FIRST:
                Opts.fGroupDirectoriesFirst     = true;
                break;

            case 'G':
                Opts.fShowGroup                 = false;
                break;

            case 'h':
                Opts.fHumanReadableSizes        = true;
                Opts.fSiUnits                   = false;
                break;

            case OPT_SI:
                Opts.fHumanReadableSizes        = true;
                Opts.fSiUnits                   = true;
                break;

            case 'H':
                Opts.fFollowSymlinkToAnyArgs    = true;
                Opts.fFollowSymlinkToDirArgs    = true;
                break;

            case OPT_DEREFERENCE_COMMAND_LINE_SYMLINK_TO_DIR:
                Opts.fFollowSymlinkToAnyArgs    = false;
                Opts.fFollowSymlinkToDirArgs    = true;
                break;

            case 'i':
                Opts.fShowINode                 = true;
                break;

            case 'k':
                Opts.cbBlock                    = _1K;
                Opts.fHumanReadableSizes        = false;
                Opts.fSiUnits                   = false;
                break;

            case 'l':
                Opts.enmFormat                  = RTCMDLSFORMAT_LONG;
                break;

            case 'L':
                Opts.fFollowSymlinksInDirs      = true;
                Opts.fFollowSymlinkToAnyArgs    = true;
                Opts.fFollowSymlinkToDirArgs    = true;
                break;

            case 'm':
                Opts.enmFormat                  = RTCMDLSFORMAT_COMMAS;
                break;

            case OPT_MACHINE_READABLE:
                Opts.enmFormat                  = RTCMDLSFORMAT_MACHINE_READABLE;
                break;

            case 'n':
                Opts.fNumericalIds              = true;
                break;

            case 'N':
                Opts.fEscapeNonGraphicChars     = false;
                Opts.fEscapeControlChars        = false;
                Opts.fHideControlChars          = false;
                break;

            case 'o':
                Opts.enmFormat                  = RTCMDLSFORMAT_LONG;
                Opts.fShowGroup                 = false;
                break;

            case 'q':
                Opts.fHideControlChars          = true;
                break;

            case OPT_SHOW_CONTROL_CHARS:
                Opts.fHideControlChars          = true;
                break;

            case 'r':
                Opts.fReverseSort               = true;
                break;

            case 'R':
                Opts.fRecursive                 = true;
                break;

            case 's':
                Opts.fShowAllocatedSize         = true;
                break;

            case 'S':
                Opts.enmSort                    = RTCMDLSSORT_SIZE;
                break;

            case OPT_SORT:
                if (strcmp(ValueUnion.psz, "none") == 0)
                    Opts.enmSort                = RTCMDLSSORT_NONE;
                else if (strcmp(ValueUnion.psz, "extension") == 0)
                    Opts.enmSort                = RTCMDLSSORT_EXTENSION;
                else if (strcmp(ValueUnion.psz, "size") == 0)
                    Opts.enmSort                = RTCMDLSSORT_SIZE;
                else if (strcmp(ValueUnion.psz, "time") == 0)
                    Opts.enmSort                = RTCMDLSSORT_TIME;
                else if (strcmp(ValueUnion.psz, "version") == 0)
                    Opts.enmSort                = RTCMDLSSORT_VERSION;
                else
                {
                    Assert(!Opts.papCollections);
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown sort by: %s", ValueUnion.psz);
                }
                break;

            case OPT_TIME:
                if (   strcmp(ValueUnion.psz, "btime") == 0
                    || strcmp(ValueUnion.psz, "birth") == 0)
                    Opts.enmTime                = RTCMDLSTIME_BTIME;
                else if (   strcmp(ValueUnion.psz, "ctime") == 0
                         || strcmp(ValueUnion.psz, "status") == 0)
                    Opts.enmTime                = RTCMDLSTIME_CTIME;
                else if (   strcmp(ValueUnion.psz, "mtime") == 0
                         || strcmp(ValueUnion.psz, "write") == 0
                         || strcmp(ValueUnion.psz, "modify") == 0)
                    Opts.enmTime                = RTCMDLSTIME_MTIME;
                else if (   strcmp(ValueUnion.psz, "atime") == 0
                         || strcmp(ValueUnion.psz, "access") == 0
                         || strcmp(ValueUnion.psz, "use") == 0)
                    Opts.enmTime                = RTCMDLSTIME_ATIME;
                else
                {
                    Assert(!Opts.papCollections);
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown time attribute: %s", ValueUnion.psz);
                }
                break;

            case OPT_TIME_STYLE:
                if (strcmp(ValueUnion.psz, "full-iso") == 0)
                    Opts.enmTimeStyle           = RTCMDLSTIMESTYLE_FULL_ISO;
                else if (strcmp(ValueUnion.psz, "long-iso") == 0)
                    Opts.enmTimeStyle           = RTCMDLSTIMESTYLE_LONG_ISO;
                else if (strcmp(ValueUnion.psz, "iso") == 0)
                    Opts.enmTimeStyle           = RTCMDLSTIMESTYLE_ISO;
                else if (strcmp(ValueUnion.psz, "locale") == 0)
                    Opts.enmTimeStyle           = RTCMDLSTIMESTYLE_LOCALE;
                else if (*ValueUnion.psz == '+')
                {
                    Opts.enmTimeStyle           = RTCMDLSTIMESTYLE_CUSTOM;
                    Opts.pszTimeCustom          = ValueUnion.psz;
                }
                else
                {
                    Assert(!Opts.papCollections);
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown sort by: %s", ValueUnion.psz);
                }
                break;

            case 't':
                Opts.enmSort                    = RTCMDLSSORT_TIME;
                break;

            case 'T':
                Opts.cchTab                     = ValueUnion.u8;
                break;

            case 'u':
                Opts.enmTime                    = RTCMDLSTIME_ATIME;
                break;

            case 'U':
                Opts.enmSort                    = RTCMDLSSORT_NONE;
                break;

            case 'v':
                Opts.enmSort                    = RTCMDLSSORT_VERSION;
                break;

            case 'w':
                Opts.cchWidth                   = ValueUnion.u32;
                break;

            case 'x':
                Opts.enmFormat                  = RTCMDLSFORMAT_COLS_HORIZONTAL;
                break;

            case 'X':
                Opts.enmSort                    = RTCMDLSSORT_EXTENSION;
                break;

            case '1':
                Opts.enmFormat                  = RTCMDLSFORMAT_SINGLE;
                break;

            case '?':
            {
                RTPrintf("Usage: to be written\n"
                         "Options dump:\n");
                for (unsigned i = 0; i < RT_ELEMENTS(s_aOptions); i++)
                    if (s_aOptions[i].iShort < 127 && s_aOptions[i].iShort >= 0x20)
                        RTPrintf(" -%c,%s\n", s_aOptions[i].iShort, s_aOptions[i].pszLong);
                    else
                        RTPrintf(" %s\n", s_aOptions[i].pszLong);
#ifdef RT_OS_WINDOWS
                const char *pszProgNm = RTPathFilename(papszArgs[0]);
                RTPrintf("\n"
                         "The path prefix '\\\\:iprtnt:\\' can be used to access the NT namespace.\n"
                         "To list devices:              %s -la \\\\:iprtnt:\\Device\n"
                         "To list win32 devices:        %s -la \\\\:iprtnt:\\GLOBAL??\n"
                         "To list the root (hack/bug):  %s -la \\\\:iprtnt:\\\n",
                         pszProgNm, pszProgNm, pszProgNm);
#endif
                Assert(!Opts.papCollections);
                return RTEXITCODE_SUCCESS;
            }

            case 'V':
                RTPrintf("%sr%d\n", RTBldCfgVersion(), RTBldCfgRevision());
                Assert(!Opts.papCollections);
                return RTEXITCODE_SUCCESS;

            default:
                Assert(!Opts.papCollections);
                return RTGetOptPrintError(chOpt, &ValueUnion);
        }
    }
}

