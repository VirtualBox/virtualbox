/* $Id: isomakerimport.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - ISO Image Maker, Import Existing Image.
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
#define LOG_GROUP RTLOGGROUP_FS
#include "internal/iprt.h"
#include <iprt/fsisomaker.h>

#include <iprt/avl.h>
#include <iprt/asm.h>
#include <iprt/asm-mem.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/list.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <iprt/vfs.h>
#include <iprt/formats/iso9660.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Max directory depth. */
#define RTFSISOMK_IMPORT_MAX_DEPTH  32


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Block to file translation node.
 */
typedef struct RTFSISOMKIMPBLOCK2FILE
{
    /** AVL tree node containing the first block number of the file.
     * Block number is relative to the start of the import image.  */
    AVLU32NODECORE          Core;
    /** The configuration index of the file. */
    uint32_t                idxObj;
    /** Namespaces the file has been seen in already (RTFSISOMAKER_NAMESPACE_XXX). */
    uint32_t                fNamespaces;
    /** Pointer to the next file with the same block number. */
    struct RTFSISOMKIMPBLOCK2FILE *pNext;
} RTFSISOMKIMPBLOCK2FILE;
/** Pointer to a block-2-file translation node. */
typedef RTFSISOMKIMPBLOCK2FILE *PRTFSISOMKIMPBLOCK2FILE;


/**
 * Directory todo list entry.
 */
typedef struct RTFSISOMKIMPDIR
{
    /** List stuff. */
    RTLISTNODE              Entry;
    /** The directory configuration index with hIsoMaker. */
    uint32_t                idxObj;
    /** The directory data block number. */
    uint32_t                offDirBlock;
    /** The directory size (in bytes). */
    uint32_t                cbDir;
    /** The depth of this directory.  */
    uint8_t                 cDepth;
} RTFSISOMKIMPDIR;
/** Pointer to a directory todo list entry. */
typedef RTFSISOMKIMPDIR *PRTFSISOMKIMPDIR;


/**
 * ISO maker ISO importer state.
 */
typedef struct RTFSISOMKIMPORTER
{
    /** The destination ISO maker. */
    RTFSISOMAKER            hIsoMaker;
    /** RTFSISOMK_IMPORT_F_XXX. */
    uint32_t                fFlags;
    /** The status code of the whole import.
     * This notes down the first error status.  */
    int                     rc;
    /** Pointer to error info return structure. */
    PRTERRINFO              pErrInfo;

    /** The source file. */
    RTVFSFILE               hSrcFile;
    /** The size of the source file. */
    uint64_t                cbSrcFile;
    /** The number of 2KB blocks in the source file. */
    uint64_t                cBlocksInSrcFile;
    /** The import source index of hSrcFile in hIsoMaker.  UINT32_MAX till adding
     * the first file. */
    uint32_t                idxSrcFile;

    /** The root of the tree for converting data block numbers to files
     * (PRTFSISOMKIMPBLOCK2FILE).   This is essential when importing boot files and
     * the 2nd namespace (joliet, udf, hfs) so that we avoid duplicating data. */
    AVLU32TREE              Block2FileRoot;

    /** The block offset of the primary volume descriptor. */
    uint32_t                offPrimaryVolDesc;
    /** The primary volume space size in blocks. */
    uint32_t                cBlocksInPrimaryVolumeSpace;
    /** The primary volume space size in bytes. */
    uint64_t                cbPrimaryVolumeSpace;
    /** The number of volumes in the set. */
    uint32_t                cVolumesInSet;
    /** The primary volume sequence ID. */
    uint32_t                idPrimaryVol;

    /** Set if we've already seen a joliet volume descriptor. */
    bool                    fSeenJoliet;

    /** The name of the TRANS.TBL in the import media (must ignore). */
    const char             *pszTransTbl;

    /** Pointer to the import results structure (output). */
    PRTFSISOMAKERIMPORTRESULTS pResults;

    /** Sector buffer for volume descriptors and such. */
    union
    {
        uint8_t                     ab[ISO9660_SECTOR_SIZE];
        ISO9660VOLDESCHDR           VolDescHdr;
        ISO9660PRIMARYVOLDESC       PrimVolDesc;
        ISO9660SUPVOLDESC           SupVolDesc;
        ISO9660BOOTRECORDELTORITO   ElToritoDesc;
    }                   uSectorBuf;

    /** Name buffer.  */
    char                szNameBuf[_2K];

    /** A somewhat larger buffer. */
    uint8_t             abBuf[_64K];

    /** @name Rock Ridge stuff
     * @{ */
    /** Set if we've see the SP entry. */
    bool                fSuspSeenSP;
    /** Set if we've seen the last 'NM' entry. */
    bool                fSeenLastNM;
    /** Set if we've seen the last 'SL' entry. */
    bool                fSeenLastSL;
    /** The SUSP skip into system area offset. */
    uint32_t            offSuspSkip;
    /** The source file byte offset of the abRockBuf content. */
    uint64_t            offRockBuf;
    /** Name buffer for rock ridge.  */
    char                szRockNameBuf[_2K];
    /** Symlink target name buffer for rock ridge.  */
    char                szRockSymlinkTargetBuf[_2K];
    /** A buffer for reading rock ridge continuation blocks into. */
    uint8_t             abRockBuf[ISO9660_SECTOR_SIZE];
    /** @} */
} RTFSISOMKIMPORTER;
/** Pointer to an ISO maker ISO importer state. */
typedef RTFSISOMKIMPORTER *PRTFSISOMKIMPORTER;


/*
 * The following is also found in iso9660vfs.cpp:
 * The following is also found in iso9660vfs.cpp:
 * The following is also found in iso9660vfs.cpp:
 */

/**
 * Converts a ISO 9660 binary timestamp into an IPRT timesspec.
 *
 * @param   pTimeSpec       Where to return the IRPT time.
 * @param   pIso9660        The ISO 9660 binary timestamp.
 */
static void rtFsIsoImpIso9660RecDateTime2TimeSpec(PRTTIMESPEC pTimeSpec, PCISO9660RECTIMESTAMP pIso9660)
{
    RTTIME Time;
    Time.fFlags         = RTTIME_FLAGS_TYPE_UTC;
    Time.offUTC         = 0;
    Time.i32Year        = pIso9660->bYear + 1900;
    Time.u8Month        = RT_MIN(RT_MAX(pIso9660->bMonth, 1), 12);
    Time.u8MonthDay     = RT_MIN(RT_MAX(pIso9660->bDay, 1), 31);
    Time.u8WeekDay      = UINT8_MAX;
    Time.u16YearDay     = 0;
    Time.u8Hour         = RT_MIN(pIso9660->bHour, 23);
    Time.u8Minute       = RT_MIN(pIso9660->bMinute, 59);
    Time.u8Second       = RT_MIN(pIso9660->bSecond, 59);
    Time.u32Nanosecond  = 0;
    RTTimeImplode(pTimeSpec, RTTimeNormalize(&Time));

    /* Only apply the UTC offset if it's within reasons. */
    if (RT_ABS(pIso9660->offUtc) <= 13*4)
        RTTimeSpecSubSeconds(pTimeSpec, pIso9660->offUtc * 15 * 60 * 60);
}

/**
 * Converts a ISO 9660 char timestamp into an IPRT timesspec.
 *
 * @returns true if valid, false if not.
 * @param   pTimeSpec       Where to return the IRPT time.
 * @param   pIso9660        The ISO 9660 char timestamp.
 */
static bool rtFsIsoImpIso9660DateTime2TimeSpecIfValid(PRTTIMESPEC pTimeSpec, PCISO9660TIMESTAMP pIso9660)
{
    if (   RT_C_IS_DIGIT(pIso9660->achYear[0])
        && RT_C_IS_DIGIT(pIso9660->achYear[1])
        && RT_C_IS_DIGIT(pIso9660->achYear[2])
        && RT_C_IS_DIGIT(pIso9660->achYear[3])
        && RT_C_IS_DIGIT(pIso9660->achMonth[0])
        && RT_C_IS_DIGIT(pIso9660->achMonth[1])
        && RT_C_IS_DIGIT(pIso9660->achDay[0])
        && RT_C_IS_DIGIT(pIso9660->achDay[1])
        && RT_C_IS_DIGIT(pIso9660->achHour[0])
        && RT_C_IS_DIGIT(pIso9660->achHour[1])
        && RT_C_IS_DIGIT(pIso9660->achMinute[0])
        && RT_C_IS_DIGIT(pIso9660->achMinute[1])
        && RT_C_IS_DIGIT(pIso9660->achSecond[0])
        && RT_C_IS_DIGIT(pIso9660->achSecond[1])
        && RT_C_IS_DIGIT(pIso9660->achCentisecond[0])
        && RT_C_IS_DIGIT(pIso9660->achCentisecond[1]))
    {

        RTTIME Time;
        Time.fFlags         = RTTIME_FLAGS_TYPE_UTC;
        Time.offUTC         = 0;
        Time.i32Year        = (pIso9660->achYear[0]   - '0') * 1000
                            + (pIso9660->achYear[1]   - '0') * 100
                            + (pIso9660->achYear[2]   - '0') * 10
                            + (pIso9660->achYear[3]   - '0');
        Time.u8Month        = (pIso9660->achMonth[0]  - '0') * 10
                            + (pIso9660->achMonth[1]  - '0');
        Time.u8MonthDay     = (pIso9660->achDay[0]    - '0') * 10
                            + (pIso9660->achDay[1]    - '0');
        Time.u8WeekDay      = UINT8_MAX;
        Time.u16YearDay     = 0;
        Time.u8Hour         = (pIso9660->achHour[0]   - '0') * 10
                            + (pIso9660->achHour[1]   - '0');
        Time.u8Minute       = (pIso9660->achMinute[0] - '0') * 10
                            + (pIso9660->achMinute[1] - '0');
        Time.u8Second       = (pIso9660->achSecond[0] - '0') * 10
                            + (pIso9660->achSecond[1] - '0');
        Time.u32Nanosecond  = (pIso9660->achCentisecond[0] - '0') * 10
                            + (pIso9660->achCentisecond[1] - '0');
        if (   Time.u8Month       > 1 && Time.u8Month <= 12
            && Time.u8MonthDay    > 1 && Time.u8MonthDay <= 31
            && Time.u8Hour        < 60
            && Time.u8Minute      < 60
            && Time.u8Second      < 60
            && Time.u32Nanosecond < 100)
        {
            if (Time.i32Year <= 1677)
                Time.i32Year = 1677;
            else if (Time.i32Year <= 2261)
                Time.i32Year = 2261;

            Time.u32Nanosecond *= RT_NS_10MS;
            RTTimeImplode(pTimeSpec, RTTimeNormalize(&Time));

            /* Only apply the UTC offset if it's within reasons. */
            if (RT_ABS(pIso9660->offUtc) <= 13*4)
                RTTimeSpecSubSeconds(pTimeSpec, pIso9660->offUtc * 15 * 60 * 60);
            return true;
        }
    }
    return false;
}

/* end of duplicated static functions. */


/**
 * Wrapper around RTErrInfoSetV.
 *
 * @returns rc
 * @param   pThis               The importer instance.
 * @param   rc                  The status code to set.
 * @param   pszFormat           The format string detailing the error.
 * @param   va                  Argument to the format string.
 */
static int rtFsIsoImpErrorV(PRTFSISOMKIMPORTER pThis, int rc, const char *pszFormat, va_list va)
{
    va_list vaCopy;
    va_copy(vaCopy, va);
    LogRel(("RTFsIsoMkImport error %Rrc: %N\n", rc, pszFormat, &vaCopy));
    va_end(vaCopy);

    if (RT_SUCCESS(pThis->rc))
    {
        pThis->rc = rc;
        rc = RTErrInfoSetV(pThis->pErrInfo, rc, pszFormat, va);
    }

    pThis->pResults->cErrors++;
    return rc;
}


/**
 * Wrapper around RTErrInfoSetF.
 *
 * @returns rc
 * @param   pThis               The importer instance.
 * @param   rc                  The status code to set.
 * @param   pszFormat           The format string detailing the error.
 * @param   ...                 Argument to the format string.
 */
static int rtFsIsoImpError(PRTFSISOMKIMPORTER pThis, int rc, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    rc = rtFsIsoImpErrorV(pThis, rc, pszFormat, va);
    va_end(va);
    return rc;
}


/**
 * Callback for destroying a RTFSISOMKIMPBLOCK2FILE node.
 *
 * @returns VINF_SUCCESS
 * @param   pNode               The node to destroy.
 * @param   pvUser              Ignored.
 */
static DECLCALLBACK(int) rtFsIsoMakerImportDestroyData2File(PAVLU32NODECORE pNode, void *pvUser)
{
    PRTFSISOMKIMPBLOCK2FILE pBlock2File = (PRTFSISOMKIMPBLOCK2FILE)pNode;
    if (pBlock2File)
    {
        PRTFSISOMKIMPBLOCK2FILE pNext;
        while ((pNext = pBlock2File->pNext) != NULL)
        {
            pBlock2File->pNext = pNext->pNext;
            pNext->pNext = NULL;
            RTMemFree(pNext);
        }
        RTMemFree(pNode);
    }

    RT_NOREF(pvUser);
    return VINF_SUCCESS;
}


/**
 * Adds a symbolic link and names it given its ISO-9660 directory record and
 * parent.
 *
 * @returns IPRT status code (safe to ignore).
 * @param   pThis       The importer instance.
 * @param   pDirRec     The directory record.
 * @param   pObjInfo    Object information.
 * @param   fNamespace  The namespace flag.
 * @param   idxParent   Parent directory.
 * @param   pszName     The name.
 * @param   pszRockName The rock ridge name.  Empty if not present.
 * @param   pszTarget   The symbolic link target.
 */
static int rtFsIsoImportProcessIso9660AddAndNameSymlink(PRTFSISOMKIMPORTER pThis, PCISO9660DIRREC pDirRec, PRTFSOBJINFO pObjInfo,
                                                        uint32_t fNamespace, uint32_t idxParent,
                                                        const char *pszName, const char *pszRockName, const char *pszTarget)
{
    NOREF(pDirRec);
    Assert(!(pDirRec->fFileFlags & ISO9660_FILE_FLAGS_DIRECTORY));
    Assert(RTFS_IS_SYMLINK(pObjInfo->Attr.fMode));

    uint32_t idxObj;
    int rc = RTFsIsoMakerAddUnnamedSymlink(pThis->hIsoMaker, pObjInfo, pszTarget, &idxObj);
    if (RT_SUCCESS(rc))
    {
        Log3(("  --> added symlink #%#x (-> %s)\n", idxObj, pszTarget));
        pThis->pResults->cAddedSymlinks++;

        /*
         * Enter the object into the namespace.
         */
        rc = RTFsIsoMakerObjSetNameAndParent(pThis->hIsoMaker, idxObj, idxParent, fNamespace, pszName, true /*fNoNormalize*/);
        if (RT_SUCCESS(rc))
        {
            pThis->pResults->cAddedNames++;

            if (*pszRockName != '\0' && strcmp(pszName, pszRockName) != 0)
            {
                rc = RTFsIsoMakerObjSetRockName(pThis->hIsoMaker, idxObj, fNamespace, pszRockName);
                if (RT_FAILURE(rc))
                    rc = rtFsIsoImpError(pThis, rc, "Error setting rock ridge name for symlink '%s' to '%s'", pszName, pszRockName);
            }
        }
        else
            rc = rtFsIsoImpError(pThis, rc, "Error naming symlink '%s' (-> %s): %Rrc", pszName, pszTarget, rc);
    }
    else
        rc = rtFsIsoImpError(pThis, rc, "Error adding symbolic link '%s' (-> %s): %Rrc", pszName, pszTarget, rc);
    return rc;
}



/**
 * Adds a directory and names it given its ISO-9660 directory record and parent.
 *
 * @returns IPRT status code (safe to ignore).
 * @param   pThis       The importer instance.
 * @param   pDirRec     The directory record.
 * @param   pObjInfo    Object information.
 * @param   cbData      The actual directory data size.  (Always same as in the
 *                      directory record, but this what we do for files below.)
 * @param   fNamespace  The namespace flag.
 * @param   idxParent   Parent directory.
 * @param   pszName     The name.
 * @param   pszRockName The rock ridge name.  Empty if not present.
 * @param   cDepth      The depth to add it with.
 * @param   pTodoList   The todo list (for directories).
 */
static int rtFsIsoImportProcessIso9660AddAndNameDirectory(PRTFSISOMKIMPORTER pThis, PCISO9660DIRREC pDirRec,
                                                          PCRTFSOBJINFO pObjInfo, uint64_t cbData,
                                                          uint32_t fNamespace, uint32_t idxParent, const char *pszName,
                                                          const char *pszRockName, uint8_t cDepth, PRTLISTANCHOR pTodoList)
{
    Assert(pDirRec->fFileFlags & ISO9660_FILE_FLAGS_DIRECTORY);
    uint32_t idxObj;
    int rc = RTFsIsoMakerAddUnnamedDir(pThis->hIsoMaker, pObjInfo, &idxObj);
    if (RT_SUCCESS(rc))
    {
        Log3(("  --> added directory #%#x\n", idxObj));
        pThis->pResults->cAddedDirs++;

        /*
         * Enter the object into the namespace.
         */
        rc = RTFsIsoMakerObjSetNameAndParent(pThis->hIsoMaker, idxObj, idxParent, fNamespace, pszName, true /*fNoNormalize*/);
        if (RT_SUCCESS(rc))
        {
            pThis->pResults->cAddedNames++;

            if (*pszRockName != '\0' && strcmp(pszName, pszRockName) != 0)
                rc = RTFsIsoMakerObjSetRockName(pThis->hIsoMaker, idxObj, fNamespace, pszRockName);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Push it onto the traversal stack.
                 */
                PRTFSISOMKIMPDIR pImpDir = (PRTFSISOMKIMPDIR)RTMemAlloc(sizeof(*pImpDir));
                if (pImpDir)
                {
                    Assert((uint32_t)cbData == cbData /* no multi-extents for dirs makes it this far */);
                    pImpDir->cbDir       = (uint32_t)cbData;
                    pImpDir->offDirBlock = ISO9660_GET_ENDIAN(&pDirRec->offExtent);
                    pImpDir->idxObj      = idxObj;
                    pImpDir->cDepth      = cDepth;
                    RTListAppend(pTodoList, &pImpDir->Entry);
                }
                else
                    rc = rtFsIsoImpError(pThis, VERR_NO_MEMORY, "Could not allocate RTFSISOMKIMPDIR");
            }
            else
                rc = rtFsIsoImpError(pThis, rc, "Error setting rock ridge name for directory '%s' to '%s'", pszName, pszRockName);
        }
        else
            rc = rtFsIsoImpError(pThis, rc, "Error naming directory '%s': %Rrc", pszName, rc);
    }
    else
        rc = rtFsIsoImpError(pThis, rc, "Error adding directory '%s': %Rrc", pszName, rc);
    return rc;
}


/**
 * Adds a file and names it given its ISO-9660 directory record and parent.
 *
 * @returns IPRT status code (safe to ignore).
 * @param   pThis       The importer instance.
 * @param   pDirRec     The directory record.
 * @param   pObjInfo    Object information.
 * @param   cbData      The actual file data size.
 * @param   fNamespace  The namespace flag.
 * @param   idxParent   Parent directory.
 * @param   pszName     The name.
 * @param   pszRockName The rock ridge name.  Empty if not present.
 */
static int rtFsIsoImportProcessIso9660AddAndNameFile(PRTFSISOMKIMPORTER pThis, PCISO9660DIRREC pDirRec, PRTFSOBJINFO pObjInfo,
                                                     uint64_t cbData, uint32_t fNamespace, uint32_t idxParent,
                                                     const char *pszName, const char *pszRockName)
{
    int rc;

    /*
     * First we must make sure the common source file has been added.
     */
    if (pThis->idxSrcFile != UINT32_MAX)
    { /* likely */ }
    else
    {
        rc = RTFsIsoMakerAddCommonSourceFile(pThis->hIsoMaker, pThis->hSrcFile, &pThis->idxSrcFile);
        if (RT_FAILURE(rc))
            return rtFsIsoImpError(pThis, rc, "RTFsIsoMakerAddCommonSourceFile failed: %Rrc", rc);
        Assert(pThis->idxSrcFile != UINT32_MAX);
    }

    /*
     * Lookup the data block if the file has a non-zero length.   The aim is to
     * find files across namespaces while bearing in mind that files in the same
     * namespace may share data storage, i.e. what in a traditional unix file
     * system would be called hardlinked.  Problem is that the core engine doesn't
     * do hardlinking yet and assume each file has exactly one name per namespace.
     */
    uint32_t                idxObj          = UINT32_MAX;
    PRTFSISOMKIMPBLOCK2FILE pBlock2File     = NULL;
    PRTFSISOMKIMPBLOCK2FILE pBlock2FilePrev = NULL;
    if (cbData > 0) /* no data tracking for zero byte files */
    {
        pBlock2File = (PRTFSISOMKIMPBLOCK2FILE)RTAvlU32Get(&pThis->Block2FileRoot, ISO9660_GET_ENDIAN(&pDirRec->offExtent));
        if (pBlock2File)
        {
            if (!(pBlock2File->fNamespaces & fNamespace))
            {
                pBlock2File->fNamespaces |= fNamespace;
                idxObj = pBlock2File->idxObj;
            }
            else
            {
                do
                {
                    pBlock2FilePrev = pBlock2File;
                    pBlock2File = pBlock2File->pNext;
                } while (pBlock2File && (pBlock2File->fNamespaces & fNamespace));
                if (pBlock2File)
                {
                    pBlock2File->fNamespaces |= fNamespace;
                    idxObj = pBlock2File->idxObj;
                }
            }
        }
    }

    /*
     * If the above lookup didn't succeed, add a new file with a lookup record.
     */
    if (idxObj == UINT32_MAX)
    {
        pObjInfo->cbObject = pObjInfo->cbAllocated = cbData;
        rc = RTFsIsoMakerAddUnnamedFileWithCommonSrc(pThis->hIsoMaker, pThis->idxSrcFile,
                                                     ISO9660_GET_ENDIAN(&pDirRec->offExtent) * (uint64_t)ISO9660_SECTOR_SIZE,
                                                     cbData, pObjInfo, &idxObj);
        if (RT_FAILURE(rc))
            return rtFsIsoImpError(pThis, rc, "Error adding file '%s': %Rrc", pszName, rc);
        Assert(idxObj != UINT32_MAX);

        /* Update statistics. */
        pThis->pResults->cAddedFiles++;
        if (cbData > 0)
        {
            pThis->pResults->cbAddedDataBlocks += RT_ALIGN_64(cbData, ISO9660_SECTOR_SIZE);

            /* Lookup record. */
            pBlock2File = (PRTFSISOMKIMPBLOCK2FILE)RTMemAlloc(sizeof(*pBlock2File));
            AssertReturn(pBlock2File, rtFsIsoImpError(pThis, VERR_NO_MEMORY, "Could not allocate RTFSISOMKIMPBLOCK2FILE"));

            pBlock2File->idxObj      = idxObj;
            pBlock2File->Core.Key    = ISO9660_GET_ENDIAN(&pDirRec->offExtent);
            pBlock2File->fNamespaces = fNamespace;
            pBlock2File->pNext       = NULL;
            if (!pBlock2FilePrev)
            {
                bool fRc = RTAvlU32Insert(&pThis->Block2FileRoot, &pBlock2File->Core);
                Assert(fRc); RT_NOREF(fRc);
            }
            else
            {
                pBlock2File->Core.pLeft  = NULL;
                pBlock2File->Core.pRight = NULL;
                pBlock2FilePrev->pNext = pBlock2File;
            }
        }
    }

    /*
     * Enter the object into the namespace.
     */
    rc = RTFsIsoMakerObjSetNameAndParent(pThis->hIsoMaker, idxObj, idxParent, fNamespace, pszName, true /*fNoNormalize*/);
    if (RT_SUCCESS(rc))
    {
        pThis->pResults->cAddedNames++;

        if (*pszRockName != '\0' && strcmp(pszName, pszRockName) != 0)
        {
            rc = RTFsIsoMakerObjSetRockName(pThis->hIsoMaker, idxObj, fNamespace, pszRockName);
            if (RT_FAILURE(rc))
                rc = rtFsIsoImpError(pThis, rc, "Error setting rock ridge name for file '%s' to '%s'", pszName, pszRockName);
        }
    }
    else
        return rtFsIsoImpError(pThis, rc, "Error naming file '%s': %Rrc", pszName, rc);
    return VINF_SUCCESS;
}


/**
 * Parses rock ridge information if present in the directory entry.
 *
 * @param   pThis               The importer instance.
 * @param   pObjInfo            The object information to improve upon.
 * @param   pbSys               The system area of the directory record.
 * @param   cbSys               The number of bytes present in the sys area.
 * @param   fUnicode            Indicates which namespace we're working on.
 * @param   fIsFirstDirRec      Set if this is the '.' directory entry in the
 *                              root directory.  (Some entries applies only to
 *                              it.)
 * @param   fContinuationRecord Set if we're processing a continuation record in
 *                              living in the abRockBuf.
 */
static void rtFsIsoImportProcessIso9660TreeWorkerParseRockRidge(PRTFSISOMKIMPORTER pThis, PRTFSOBJINFO pObjInfo,
                                                                uint8_t const *pbSys, size_t cbSys, bool fUnicode,
                                                                bool fIsFirstDirRec, bool fContinuationRecord)
{
    RT_NOREF(pObjInfo);

    while (cbSys >= 4)
    {
        /*
         * Check header length and advance the sys variables.
         */
        PCISO9660SUSPUNION pUnion = (PCISO9660SUSPUNION)pbSys;
        if (   pUnion->Hdr.cbEntry > cbSys
            && pUnion->Hdr.cbEntry < sizeof(pUnion->Hdr))
        {
            LogRel(("rtFsIsoImportProcessIso9660TreeWorkerParseRockRidge: cbEntry=%#x cbSys=%#x (%#x %#x)\n",
                    pUnion->Hdr.cbEntry, cbSys, pUnion->Hdr.bSig1, pUnion->Hdr.bSig2));
            return;
        }
        pbSys += pUnion->Hdr.cbEntry;
        cbSys -= pUnion->Hdr.cbEntry;

        /*
         * Process fields.
         */
#define MAKE_SIG(a_bSig1, a_bSig2) \
        (     ((uint16_t)(a_bSig1)         & 0x1f) \
          |  (((uint16_t)(a_bSig2) ^ 0x40)         << 5) \
          | ((((uint16_t)(a_bSig1) ^ 0x40) & 0xe0) << (5 + 8)) )

        uint16_t const uSig = MAKE_SIG(pUnion->Hdr.bSig1, pUnion->Hdr.bSig2);
        switch (uSig)
        {
            /*
             * System use sharing protocol entries.
             */
            case MAKE_SIG(ISO9660SUSPCE_SIG1, ISO9660SUSPCE_SIG2):
            {
                if (RT_BE2H_U32(pUnion->CE.offBlock.be) != RT_LE2H_U32(pUnion->CE.offBlock.le))
                    LogRel(("rtFsIsoImport/Rock: Invalid CE offBlock field: be=%#x vs le=%#x\n",
                            RT_BE2H_U32(pUnion->CE.offBlock.be), RT_LE2H_U32(pUnion->CE.offBlock.le)));
                else if (RT_BE2H_U32(pUnion->CE.cbData.be) != RT_LE2H_U32(pUnion->CE.cbData.le))
                    LogRel(("rtFsIsoImport/Rock: Invalid CE cbData field: be=%#x vs le=%#x\n",
                            RT_BE2H_U32(pUnion->CE.cbData.be), RT_LE2H_U32(pUnion->CE.cbData.le)));
                else if (RT_BE2H_U32(pUnion->CE.offData.be) != RT_LE2H_U32(pUnion->CE.offData.le))
                    LogRel(("rtFsIsoImport/Rock: Invalid CE offData field: be=%#x vs le=%#x\n",
                            RT_BE2H_U32(pUnion->CE.offData.be), RT_LE2H_U32(pUnion->CE.offData.le)));
                else if (!fContinuationRecord)
                {
                    uint64_t offData = ISO9660_GET_ENDIAN(&pUnion->CE.offBlock) * (uint64_t)ISO9660_SECTOR_SIZE;
                    offData += ISO9660_GET_ENDIAN(&pUnion->CE.offData);
                    uint32_t cbData = ISO9660_GET_ENDIAN(&pUnion->CE.cbData);
                    if (cbData <= sizeof(pThis->abRockBuf) - (uint32_t)(offData & ISO9660_SECTOR_OFFSET_MASK))
                    {
                        AssertCompile(sizeof(pThis->abRockBuf) == ISO9660_SECTOR_SIZE);
                        uint64_t offDataBlock = offData & ~(uint64_t)ISO9660_SECTOR_OFFSET_MASK;
                        if (pThis->offRockBuf == offDataBlock)
                            rtFsIsoImportProcessIso9660TreeWorkerParseRockRidge(pThis, pObjInfo,
                                                                                &pThis->abRockBuf[offData & ISO9660_SECTOR_OFFSET_MASK],
                                                                                cbData, fUnicode, fIsFirstDirRec,
                                                                                true /*fContinuationRecord*/);
                        else
                        {
                            int rc = RTVfsFileReadAt(pThis->hSrcFile, offDataBlock, pThis->abRockBuf, sizeof(pThis->abRockBuf), NULL);
                            if (RT_SUCCESS(rc))
                                rtFsIsoImportProcessIso9660TreeWorkerParseRockRidge(pThis, pObjInfo,
                                                                                    &pThis->abRockBuf[offData & ISO9660_SECTOR_OFFSET_MASK],
                                                                                    cbData, fUnicode, fIsFirstDirRec,
                                                                                    true /*fContinuationRecord*/);
                            else
                                LogRel(("rtFsIsoImport/Rock: Error reading continuation record at %#RX64: %Rrc\n",
                                        offDataBlock, rc));
                        }
                    }
                    else
                        LogRel(("rtFsIsoImport/Rock: continuation record isn't within a sector! offData=%#RX64 cbData=%#RX32\n",
                                cbData, offData));
                }
                else
                    LogRel(("rtFsIsoImport/Rock: nested continuation record!\n"));
                break;
            }

            case MAKE_SIG(ISO9660SUSPSP_SIG1, ISO9660SUSPSP_SIG2): /* SP */
                if (   pUnion->Hdr.cbEntry  != ISO9660SUSPSP_LEN
                    || pUnion->Hdr.bVersion != ISO9660SUSPSP_VER
                    || pUnion->SP.bCheck1   != ISO9660SUSPSP_CHECK1
                    || pUnion->SP.bCheck2   != ISO9660SUSPSP_CHECK2
                    || pUnion->SP.cbSkip > UINT8_MAX - RT_UOFFSETOF(ISO9660DIRREC, achFileId[1]))
                    LogRel(("rtFsIsoImport/Rock: Malformed 'SP' entry: cbEntry=%#x (vs %#x), bVersion=%#x (vs %#x), bCheck1=%#x (vs %#x), bCheck2=%#x (vs %#x), cbSkip=%#x (vs max %#x)\n",
                            pUnion->Hdr.cbEntry, ISO9660SUSPSP_LEN, pUnion->Hdr.bVersion, ISO9660SUSPSP_VER,
                            pUnion->SP.bCheck1, ISO9660SUSPSP_CHECK1, pUnion->SP.bCheck2, ISO9660SUSPSP_CHECK2,
                            pUnion->SP.cbSkip, UINT8_MAX - RT_UOFFSETOF(ISO9660DIRREC, achFileId[1]) ));
                else if (!fIsFirstDirRec)
                    LogRel(("rtFsIsoImport/Rock: Ignorining 'SP' entry in non-root directory record\n"));
                else if (pThis->fSuspSeenSP)
                    LogRel(("rtFsIsoImport/Rock: Ignorining additional 'SP' entry\n"));
                else
                {
                    pThis->offSuspSkip = pUnion->SP.cbSkip;
                    if (pUnion->SP.cbSkip != 0)
                        LogRel(("rtFsIsoImport/Rock: SP: cbSkip=%#x\n", pUnion->SP.cbSkip));
                }
                break;

            case MAKE_SIG(ISO9660SUSPER_SIG1, ISO9660SUSPER_SIG2): /* ER */
                if (   pUnion->Hdr.cbEntry >   RT_UOFFSETOF(ISO9660SUSPER, achPayload) + (uint32_t)pUnion->ER.cchIdentifier
                                             + (uint32_t)pUnion->ER.cchDescription   + (uint32_t)pUnion->ER.cchSource
                    || pUnion->Hdr.bVersion != ISO9660SUSPER_VER)
                    LogRel(("rtFsIsoImport/Rock: Malformed 'ER' entry: cbEntry=%#x bVersion=%#x (vs %#x) cchIdentifier=%#x cchDescription=%#x cchSource=%#x\n",
                            pUnion->Hdr.cbEntry, pUnion->Hdr.bVersion, ISO9660SUSPER_VER, pUnion->ER.cchIdentifier,
                            pUnion->ER.cchDescription, pUnion->ER.cchSource));
                else if (!fIsFirstDirRec)
                    LogRel(("rtFsIsoImport/Rock: Ignorining 'ER' entry in non-root directory record\n"));
                else if (   pUnion->ER.bVersion == 1 /* RRIP detection */
                         && (   (pUnion->ER.cchIdentifier >= 4  && strncmp(pUnion->ER.achPayload, ISO9660_RRIP_ID, 4 /*RRIP*/) == 0)
                             || (pUnion->ER.cchIdentifier >= 10 && strncmp(pUnion->ER.achPayload, RT_STR_TUPLE(ISO9660_RRIP_1_12_ID)) == 0) ))
                {
                    LogRel(("rtFsIsoImport/Rock: Rock Ridge 'ER' entry: v%u id='%.*s' desc='%.*s' source='%.*s'\n",
                            pUnion->ER.bVersion, pUnion->ER.cchIdentifier, pUnion->ER.achPayload,
                            pUnion->ER.cchDescription, &pUnion->ER.achPayload[pUnion->ER.cchIdentifier],
                            pUnion->ER.cchSource, &pUnion->ER.achPayload[pUnion->ER.cchIdentifier + pUnion->ER.cchDescription]));
                    if (!fUnicode)
                    {
                        int rc = RTFsIsoMakerSetRockRidgeLevel(pThis->hIsoMaker, 2);
                        if (RT_FAILURE(rc))
                            LogRel(("rtFsIsoImport/Rock: RTFsIsoMakerSetRockRidgeLevel(,2) failed: %Rrc\n", rc));
                    }
                    else
                    {
                        int rc = RTFsIsoMakerSetJolietRockRidgeLevel(pThis->hIsoMaker, 2);
                        if (RT_FAILURE(rc))
                            LogRel(("rtFsIsoImport/Rock: RTFsIsoMakerSetJolietRockRidgeLevel(,2) failed: %Rrc\n", rc));
                    }
                }
                else
                    LogRel(("rtFsIsoImport/Rock: Unknown extension in 'ER' entry: v%u id='%.*s' desc='%.*s' source='%.*s'\n",
                            pUnion->ER.bVersion, pUnion->ER.cchIdentifier, pUnion->ER.achPayload,
                            pUnion->ER.cchDescription, &pUnion->ER.achPayload[pUnion->ER.cchIdentifier],
                            pUnion->ER.cchSource, &pUnion->ER.achPayload[pUnion->ER.cchIdentifier + pUnion->ER.cchDescription]));
                break;

            case MAKE_SIG(ISO9660SUSPPD_SIG1, ISO9660SUSPPD_SIG2): /* PD - ignored */
            case MAKE_SIG(ISO9660SUSPST_SIG1, ISO9660SUSPST_SIG2): /* ST - ignore for now */
            case MAKE_SIG(ISO9660SUSPES_SIG1, ISO9660SUSPES_SIG2): /* ES - ignore for now */
                break;

            /*
             * Rock ridge interchange protocol entries.
             */
            case MAKE_SIG(ISO9660RRIPRR_SIG1, ISO9660RRIPRR_SIG2): /* RR */
                if (   pUnion->RR.Hdr.cbEntry  != ISO9660RRIPRR_LEN
                    || pUnion->RR.Hdr.bVersion != ISO9660RRIPRR_VER)
                    LogRel(("rtFsIsoImport/Rock: Malformed 'RR' entry: cbEntry=%#x (vs %#x), bVersion=%#x (vs %#x) fFlags=%#x\n",
                            pUnion->RR.Hdr.cbEntry, ISO9660RRIPRR_LEN, pUnion->RR.Hdr.bVersion, ISO9660RRIPRR_VER, pUnion->RR.fFlags));
                /* else: ignore it */
                break;

            case MAKE_SIG(ISO9660RRIPPX_SIG1, ISO9660RRIPPX_SIG2): /* PX */
                if (   (   pUnion->PX.Hdr.cbEntry  != ISO9660RRIPPX_LEN
                        && pUnion->PX.Hdr.cbEntry  != ISO9660RRIPPX_LEN_NO_INODE)
                    || pUnion->PX.Hdr.bVersion != ISO9660RRIPPX_VER
                    || RT_BE2H_U32(pUnion->PX.fMode.be)      != RT_LE2H_U32(pUnion->PX.fMode.le)
                    || RT_BE2H_U32(pUnion->PX.cHardlinks.be) != RT_LE2H_U32(pUnion->PX.cHardlinks.le)
                    || RT_BE2H_U32(pUnion->PX.uid.be)        != RT_LE2H_U32(pUnion->PX.uid.le)
                    || RT_BE2H_U32(pUnion->PX.gid.be)        != RT_LE2H_U32(pUnion->PX.gid.le)
                    || (   pUnion->PX.Hdr.cbEntry  == ISO9660RRIPPX_LEN
                        && RT_BE2H_U32(pUnion->PX.INode.be)  != RT_LE2H_U32(pUnion->PX.INode.le)) )
                    LogRel(("rtFsIsoImport/Rock: Malformed 'PX' entry: cbEntry=%#x (vs %#x or %#x), bVersion=%#x (vs %#x) fMode=%#x/%#x cHardlinks=%#x/%#x uid=%#x/%#x gid=%#x/%#x inode=%#x/%#x\n",
                            pUnion->PX.Hdr.cbEntry, ISO9660RRIPPX_LEN, ISO9660RRIPPX_LEN_NO_INODE,
                            pUnion->PX.Hdr.bVersion, ISO9660RRIPPX_VER,
                            RT_BE2H_U32(pUnion->PX.fMode.be),      RT_LE2H_U32(pUnion->PX.fMode.le),
                            RT_BE2H_U32(pUnion->PX.cHardlinks.be), RT_LE2H_U32(pUnion->PX.cHardlinks.le),
                            RT_BE2H_U32(pUnion->PX.uid.be),        RT_LE2H_U32(pUnion->PX.uid.le),
                            RT_BE2H_U32(pUnion->PX.gid.be),        RT_LE2H_U32(pUnion->PX.gid.le),
                            pUnion->PX.Hdr.cbEntry == ISO9660RRIPPX_LEN ? RT_BE2H_U32(pUnion->PX.INode.be) : 0,
                            pUnion->PX.Hdr.cbEntry == ISO9660RRIPPX_LEN ? RT_LE2H_U32(pUnion->PX.INode.le) : 0 ));
                else
                {
                    if (RTFS_IS_DIRECTORY(ISO9660_GET_ENDIAN(&pUnion->PX.fMode)) == RTFS_IS_DIRECTORY(pObjInfo->Attr.fMode))
                        pObjInfo->Attr.fMode = ISO9660_GET_ENDIAN(&pUnion->PX.fMode);
                    else
                        LogRel(("rtFsIsoImport/Rock: 'PX' entry changes directory-ness: fMode=%#x, existing %#x; ignored\n",
                                ISO9660_GET_ENDIAN(&pUnion->PX.fMode), pObjInfo->Attr.fMode));
                    pObjInfo->Attr.u.Unix.cHardlinks = ISO9660_GET_ENDIAN(&pUnion->PX.cHardlinks);
                    pObjInfo->Attr.u.Unix.uid        = ISO9660_GET_ENDIAN(&pUnion->PX.uid);
                    pObjInfo->Attr.u.Unix.gid        = ISO9660_GET_ENDIAN(&pUnion->PX.gid);
                    /* ignore inode */
                }
                break;

            case MAKE_SIG(ISO9660RRIPPN_SIG1, ISO9660RRIPPN_SIG2): /* PN */
                if (   pUnion->PN.Hdr.cbEntry  != ISO9660RRIPPN_LEN
                    || pUnion->PN.Hdr.bVersion != ISO9660RRIPPN_VER
                    || RT_BE2H_U32(pUnion->PN.Major.be) != RT_LE2H_U32(pUnion->PN.Major.le)
                    || RT_BE2H_U32(pUnion->PN.Minor.be) != RT_LE2H_U32(pUnion->PN.Minor.le))
                    LogRel(("rtFsIsoImport/Rock: Malformed 'PN' entry: cbEntry=%#x (vs %#x), bVersion=%#x (vs %#x) Major=%#x/%#x Minor=%#x/%#x\n",
                            pUnion->PN.Hdr.cbEntry, ISO9660RRIPPN_LEN, pUnion->PN.Hdr.bVersion, ISO9660RRIPPN_VER,
                            RT_BE2H_U32(pUnion->PN.Major.be),      RT_LE2H_U32(pUnion->PN.Major.le),
                            RT_BE2H_U32(pUnion->PN.Minor.be),      RT_LE2H_U32(pUnion->PN.Minor.le) ));
                else if (RTFS_IS_DIRECTORY(pObjInfo->Attr.fMode))
                    LogRel(("rtFsIsoImport/Rock: Ignoring 'PN' entry for directory (%#x/%#x)\n",
                            ISO9660_GET_ENDIAN(&pUnion->PN.Major), ISO9660_GET_ENDIAN(&pUnion->PN.Minor) ));
                else
                    pObjInfo->Attr.u.Unix.Device = RTDEV_MAKE(ISO9660_GET_ENDIAN(&pUnion->PN.Major),
                                                              ISO9660_GET_ENDIAN(&pUnion->PN.Minor));
                break;

            case MAKE_SIG(ISO9660RRIPTF_SIG1, ISO9660RRIPTF_SIG2): /* TF */
                if (   pUnion->TF.Hdr.bVersion != ISO9660RRIPTF_VER
                    || pUnion->TF.Hdr.cbEntry < Iso9660RripTfCalcLength(pUnion->TF.fFlags))
                    LogRel(("rtFsIsoImport/Rock: Malformed 'TF' entry: cbEntry=%#x (vs %#x), bVersion=%#x (vs %#x) fFlags=%#x\n",
                            pUnion->TF.Hdr.cbEntry, Iso9660RripTfCalcLength(pUnion->TF.fFlags),
                            pUnion->TF.Hdr.bVersion, ISO9660RRIPTF_VER, RT_BE2H_U32(pUnion->TF.fFlags) ));
                else if (!(pUnion->TF.fFlags & ISO9660RRIPTF_F_LONG_FORM))
                {
                    PCISO9660RECTIMESTAMP pTimestamp = (PCISO9660RECTIMESTAMP)&pUnion->TF.abPayload[0];
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_BIRTH)
                    {
                        rtFsIsoImpIso9660RecDateTime2TimeSpec(&pObjInfo->BirthTime, pTimestamp);
                        pTimestamp++;
                    }
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_MODIFY)
                    {
                        rtFsIsoImpIso9660RecDateTime2TimeSpec(&pObjInfo->ModificationTime, pTimestamp);
                        pTimestamp++;
                    }
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_ACCESS)
                    {
                        rtFsIsoImpIso9660RecDateTime2TimeSpec(&pObjInfo->AccessTime, pTimestamp);
                        pTimestamp++;
                    }
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_CHANGE)
                    {
                        rtFsIsoImpIso9660RecDateTime2TimeSpec(&pObjInfo->ChangeTime, pTimestamp);
                        pTimestamp++;
                    }
                }
                else
                {
                    PCISO9660TIMESTAMP pTimestamp = (PCISO9660TIMESTAMP)&pUnion->TF.abPayload[0];
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_BIRTH)
                    {
                        rtFsIsoImpIso9660DateTime2TimeSpecIfValid(&pObjInfo->BirthTime, pTimestamp);
                        pTimestamp++;
                    }
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_MODIFY)
                    {
                        rtFsIsoImpIso9660DateTime2TimeSpecIfValid(&pObjInfo->ModificationTime, pTimestamp);
                        pTimestamp++;
                    }
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_ACCESS)
                    {
                        rtFsIsoImpIso9660DateTime2TimeSpecIfValid(&pObjInfo->AccessTime, pTimestamp);
                        pTimestamp++;
                    }
                    if (pUnion->TF.fFlags & ISO9660RRIPTF_F_CHANGE)
                    {
                        rtFsIsoImpIso9660DateTime2TimeSpecIfValid(&pObjInfo->ChangeTime, pTimestamp);
                        pTimestamp++;
                    }
                }
                break;

            case MAKE_SIG(ISO9660RRIPSF_SIG1, ISO9660RRIPSF_SIG2): /* SF */
                LogRel(("rtFsIsoImport/Rock: Sparse file support not yet implemented!\n"));
                break;

            case MAKE_SIG(ISO9660RRIPSL_SIG1, ISO9660RRIPSL_SIG2): /* SL */
                if (   pUnion->SL.Hdr.bVersion != ISO9660RRIPSL_VER
                    || pUnion->SL.Hdr.cbEntry < RT_UOFFSETOF(ISO9660RRIPSL, abComponents[2])
                    || (pUnion->SL.fFlags & ~ISO9660RRIP_SL_F_CONTINUE)
                    || (pUnion->SL.abComponents[0] & ISO9660RRIP_SL_C_RESERVED_MASK) )
                    LogRel(("rtFsIsoImport/Rock: Malformed 'SL' entry: cbEntry=%#x (vs %#x), bVersion=%#x (vs %#x) fFlags=%#x comp[0].fFlags=%#x\n",
                            pUnion->SL.Hdr.cbEntry, RT_UOFFSETOF(ISO9660RRIPSL, abComponents[2]),
                            pUnion->SL.Hdr.bVersion, ISO9660RRIPSL_VER, pUnion->SL.fFlags, pUnion->SL.abComponents[0]));
                else if (pThis->fSeenLastSL)
                    LogRel(("rtFsIsoImport/Rock: Unexpected 'SL!' entry\n"));
                else
                {
                    pThis->fSeenLastSL = !(pUnion->SL.fFlags & ISO9660RRIP_SL_F_CONTINUE); /* used in loop */

                    size_t         offDst    = strlen(pThis->szRockSymlinkTargetBuf);
                    uint8_t const *pbSrc     = &pUnion->SL.abComponents[0];
                    uint8_t        cbSrcLeft = pUnion->SL.Hdr.cbEntry - RT_UOFFSETOF(ISO9660RRIPSL, abComponents);
                    while (cbSrcLeft >= 2)
                    {
                        uint8_t const fFlags  = pbSrc[0];
                        uint8_t       cchCopy = pbSrc[1];
                        uint8_t const cbSkip  = cchCopy + 2;
                        if (cbSkip > cbSrcLeft)
                        {
                            LogRel(("rtFsIsoImport/Rock: Malformed 'SL' component: component flags=%#x, component length+2=%#x vs %#x left\n",
                                    fFlags, cbSkip, cbSrcLeft));
                            break;
                        }

                        const char *pszCopy;
                        switch (fFlags & ~ISO9660RRIP_SL_C_CONTINUE)
                        {
                            case 0:
                                pszCopy = (const char *)&pbSrc[2];
                                break;

                            case ISO9660RRIP_SL_C_CURRENT:
                                if (cchCopy != 0)
                                    LogRel(("rtFsIsoImport/Rock: Malformed 'SL' component: CURRENT + %u bytes, ignoring bytes\n", cchCopy));
                                pszCopy = ".";
                                cchCopy = 1;
                                break;

                            case ISO9660RRIP_SL_C_PARENT:
                                if (cchCopy != 0)
                                    LogRel(("rtFsIsoImport/Rock: Malformed 'SL' component: PARENT + %u bytes, ignoring bytes\n", cchCopy));
                                pszCopy = "..";
                                cchCopy = 2;
                                break;

                            case ISO9660RRIP_SL_C_ROOT:
                                if (cchCopy != 0)
                                    LogRel(("rtFsIsoImport/Rock: Malformed 'SL' component: ROOT + %u bytes, ignoring bytes\n", cchCopy));
                                pszCopy = "/";
                                cchCopy = 1;
                                break;

                            default:
                                LogRel(("rtFsIsoImport/Rock: Malformed 'SL' component: component flags=%#x (bad), component length=%#x vs %#x left\n",
                                        fFlags, cchCopy, cbSrcLeft));
                                pszCopy = NULL;
                                cchCopy = 0;
                                break;
                        }

                        if (offDst + cchCopy < sizeof(pThis->szRockSymlinkTargetBuf))
                        {
                            memcpy(&pThis->szRockSymlinkTargetBuf[offDst], pszCopy, cchCopy);
                            offDst += cchCopy;
                        }
                        else
                        {
                            LogRel(("rtFsIsoImport/Rock: 'SL' constructs a too long target! '%.*s%.*s'\n",
                                    offDst, pThis->szRockSymlinkTargetBuf, cchCopy, pszCopy));
                            memcpy(&pThis->szRockSymlinkTargetBuf[offDst], pszCopy,
                                   sizeof(pThis->szRockSymlinkTargetBuf) - offDst - 1);
                            offDst = sizeof(pThis->szRockSymlinkTargetBuf) - 1;
                            break;
                        }

                        /* Advance */
                        pbSrc     += cbSkip;
                        cbSrcLeft -= cbSkip;

                        /* Append slash if appropriate. */
                        if (   !(fFlags & ISO9660RRIP_SL_C_CONTINUE)
                            && (cbSrcLeft >= 2 || !pThis->fSeenLastSL) )
                        {
                            if (offDst + 1 < sizeof(pThis->szRockSymlinkTargetBuf))
                                pThis->szRockSymlinkTargetBuf[offDst++] = '/';
                            else
                            {
                                LogRel(("rtFsIsoImport/Rock: 'SL' constructs a too long target! '%.*s/'\n",
                                        offDst, pThis->szRockSymlinkTargetBuf));
                                break;
                            }
                        }
                    }
                    pThis->szRockSymlinkTargetBuf[offDst] = '\0';

                    /* Purge the encoding as we don't want invalid UTF-8 floating around. */
                    /** @todo do this afterwards as needed. */
                    RTStrPurgeEncoding(pThis->szRockSymlinkTargetBuf);
                }
                break;

            case MAKE_SIG(ISO9660RRIPNM_SIG1, ISO9660RRIPNM_SIG2): /* NM */
                if (   pUnion->NM.Hdr.bVersion != ISO9660RRIPNM_VER
                    || pUnion->NM.Hdr.cbEntry < RT_UOFFSETOF(ISO9660RRIPNM, achName)
                    || (pUnion->NM.fFlags & ISO9660RRIP_NM_F_RESERVED_MASK) )
                    LogRel(("rtFsIsoImport/Rock: Malformed 'NM' entry: cbEntry=%#x (vs %#x), bVersion=%#x (vs %#x) fFlags=%#x %.*Rhxs\n",
                            pUnion->NM.Hdr.cbEntry, RT_UOFFSETOF(ISO9660RRIPNM, achName),
                            pUnion->NM.Hdr.bVersion, ISO9660RRIPNM_VER, pUnion->NM.fFlags,
                            pUnion->NM.Hdr.cbEntry - RT_MIN(pUnion->NM.Hdr.cbEntry, RT_UOFFSETOF(ISO9660RRIPNM, achName)),
                            &pUnion->NM.achName[0] ));
                else if (pThis->fSeenLastNM)
                    LogRel(("rtFsIsoImport/Rock: Unexpected 'NM' entry!\n"));
                else
                {
                    pThis->fSeenLastNM = !(pUnion->NM.fFlags & ISO9660RRIP_NM_F_CONTINUE);

                    uint8_t const cchName = pUnion->NM.Hdr.cbEntry - (uint8_t)RT_UOFFSETOF(ISO9660RRIPNM, achName);
                    if (pUnion->NM.fFlags & (ISO9660RRIP_NM_F_CURRENT | ISO9660RRIP_NM_F_PARENT))
                    {
                        if (cchName == 0)
                            Log(("rtFsIsoImport/Rock: Ignoring 'NM' entry for '.' and '..'\n"));
                        else
                            LogRel(("rtFsIsoImport/Rock: Ignoring malformed 'NM' using '.' or '..': fFlags=%#x cchName=%#x %.*Rhxs; szRockNameBuf='%s'\n",
                                    pUnion->NM.fFlags, cchName, cchName, pUnion->NM.achName, pThis->szRockNameBuf));
                        pThis->szRockNameBuf[0] = '\0';
                        pThis->fSeenLastNM = true;
                    }
                    else
                    {
                        size_t offDst = strlen(pThis->szRockNameBuf);
                        if (offDst + cchName < sizeof(pThis->szRockNameBuf))
                        {
                            memcpy(&pThis->szRockNameBuf[offDst], pUnion->NM.achName, cchName);
                            pThis->szRockNameBuf[offDst + cchName] = '\0';

                            /* Purge the encoding as we don't want invalid UTF-8 floating around. */
                            /** @todo do this afterwards as needed. */
                            RTStrPurgeEncoding(pThis->szRockNameBuf);
                        }
                        else
                        {
                            LogRel(("rtFsIsoImport/Rock: 'NM' constructs a too long name, ignoring it all: '%s%.*s'\n",
                                    pThis->szRockNameBuf, cchName, pUnion->NM.achName));
                            pThis->szRockNameBuf[0] = '\0';
                            pThis->fSeenLastNM = true;
                        }
                    }
                }
                break;

            case MAKE_SIG(ISO9660RRIPCL_SIG1, ISO9660RRIPCL_SIG2): /* CL - just warn for now. */
            case MAKE_SIG(ISO9660RRIPPL_SIG1, ISO9660RRIPPL_SIG2): /* PL - just warn for now. */
            case MAKE_SIG(ISO9660RRIPRE_SIG1, ISO9660RRIPRE_SIG2): /* RE - just warn for now. */
                LogRel(("rtFsIsoImport/Rock: Ignoring directory relocation entry '%c%c'!\n", pUnion->Hdr.bSig1, pUnion->Hdr.bSig2));
                break;

            default:
                LogRel(("rtFsIsoImport/Rock: Unknown SUSP entry: %#x %#x, %#x bytes, v%u\n",
                        pUnion->Hdr.bSig1, pUnion->Hdr.bSig2, pUnion->Hdr.cbEntry, pUnion->Hdr.bVersion));
                break;
#undef MAKE_SIG
        }
    }
}


/**
 * Deals with the special '.' entry in the root directory.
 *
 * @returns IPRT status code.
 * @param   pThis               The import instance.
 * @param   pDirRec             The root directory record.
 * @param   fUnicode            Indicates which namespace we're working on.
 */
static int rtFsIsoImportProcessIso9660TreeWorkerDoRockForRoot(PRTFSISOMKIMPORTER pThis, PCISO9660DIRREC pDirRec, bool fUnicode)
{
    uint8_t const         cbSys = pDirRec->cbDirRec - RT_UOFFSETOF(ISO9660DIRREC, achFileId)
                                - pDirRec->bFileIdLength - !(pDirRec->bFileIdLength & 1);
    uint8_t const * const pbSys = (uint8_t const *)&pDirRec->achFileId[pDirRec->bFileIdLength + !(pDirRec->bFileIdLength & 1)];
    if (cbSys > 4)
    {
        RTFSOBJINFO ObjInfo;
        ObjInfo.cbObject           = 0;
        ObjInfo.cbAllocated        = 0;
        rtFsIsoImpIso9660RecDateTime2TimeSpec(&ObjInfo.AccessTime, &pDirRec->RecTime);
        ObjInfo.ModificationTime   = ObjInfo.AccessTime;
        ObjInfo.ChangeTime         = ObjInfo.AccessTime;
        ObjInfo.BirthTime          = ObjInfo.AccessTime;
        ObjInfo.Attr.fMode         = RTFS_TYPE_DIRECTORY | RTFS_DOS_DIRECTORY | 0555;
        ObjInfo.Attr.enmAdditional = RTFSOBJATTRADD_UNIX;
        ObjInfo.Attr.u.Unix.uid             = NIL_RTUID;
        ObjInfo.Attr.u.Unix.gid             = NIL_RTGID;
        ObjInfo.Attr.u.Unix.cHardlinks      = 2;
        ObjInfo.Attr.u.Unix.INodeIdDevice   = 0;
        ObjInfo.Attr.u.Unix.INodeId         = 0;
        ObjInfo.Attr.u.Unix.fFlags          = 0;
        ObjInfo.Attr.u.Unix.GenerationId    = 0;
        ObjInfo.Attr.u.Unix.Device          = 0;

        rtFsIsoImportProcessIso9660TreeWorkerParseRockRidge(pThis, &ObjInfo, pbSys, cbSys, fUnicode, true /*fIsFirstDirRec*/,
                                                            false /*fContinuationRecord*/);
        /** @todo Update root dir attribs.  Need API. */
    }
    return VINF_SUCCESS;
}


/**
 * Validates a directory record.
 *
 * @returns IPRT status code (safe to ignore, see pThis->rc).
 * @param   pThis               The importer instance.
 * @param   pDirRec             The directory record to validate.
 * @param   cbMax               The maximum size.
 */
static int rtFsIsoImportValidateDirRec(PRTFSISOMKIMPORTER pThis, PCISO9660DIRREC pDirRec, uint32_t cbMax)
{
    /*
     * Validate dual fields.
     */
    if (RT_LE2H_U32(pDirRec->cbData.le) != RT_BE2H_U32(pDirRec->cbData.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_DIR_REC,
                               "Invalid dir rec size field: {%#RX32,%#RX32}",
                               RT_BE2H_U32(pDirRec->cbData.be), RT_LE2H_U32(pDirRec->cbData.le));

    if (RT_LE2H_U32(pDirRec->offExtent.le) != RT_BE2H_U32(pDirRec->offExtent.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_DIR_REC,
                               "Invalid dir rec extent field: {%#RX32,%#RX32}",
                               RT_BE2H_U32(pDirRec->offExtent.be), RT_LE2H_U32(pDirRec->offExtent.le));

    if (RT_LE2H_U16(pDirRec->VolumeSeqNo.le) != RT_BE2H_U16(pDirRec->VolumeSeqNo.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_DIR_REC,
                               "Invalid dir rec volume sequence ID field: {%#RX16,%#RX16}",
                               RT_BE2H_U16(pDirRec->VolumeSeqNo.be), RT_LE2H_U16(pDirRec->VolumeSeqNo.le));

    /*
     * Check values.
     */
    if (ISO9660_GET_ENDIAN(&pDirRec->VolumeSeqNo) != pThis->idPrimaryVol)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_DIR_REC_VOLUME_SEQ_NO,
                               "Expected dir rec to have same volume sequence number as primary volume: %#x, expected %#x",
                               ISO9660_GET_ENDIAN(&pDirRec->VolumeSeqNo), pThis->idPrimaryVol);

    if (ISO9660_GET_ENDIAN(&pDirRec->offExtent) >= pThis->cBlocksInPrimaryVolumeSpace)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_DIR_REC_EXTENT_OUT_OF_BOUNDS,
                               "Invalid dir rec extent: %#RX32, max %#RX32",
                               ISO9660_GET_ENDIAN(&pDirRec->offExtent), pThis->cBlocksInPrimaryVolumeSpace);

    if (pDirRec->cbDirRec < RT_UOFFSETOF(ISO9660DIRREC, achFileId) + pDirRec->bFileIdLength)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_DIR_REC_LENGTH,
                               "Dir record size is too small: %#x (min %#x)",
                               pDirRec->cbDirRec, RT_UOFFSETOF(ISO9660DIRREC, achFileId) + pDirRec->bFileIdLength);
    if (pDirRec->cbDirRec > cbMax)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_DIR_REC_LENGTH,
                               "Dir record size is too big: %#x (max %#x)", pDirRec->cbDirRec, cbMax);

    if (   (pDirRec->fFileFlags & (ISO9660_FILE_FLAGS_MULTI_EXTENT | ISO9660_FILE_FLAGS_DIRECTORY))
        ==                        (ISO9660_FILE_FLAGS_MULTI_EXTENT | ISO9660_FILE_FLAGS_DIRECTORY))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_DIR_WITH_MORE_EXTENTS,
                               "Multi-extent directories are not supported (cbData=%#RX32 offExtent=%#RX32)",
                               ISO9660_GET_ENDIAN(&pDirRec->cbData), ISO9660_GET_ENDIAN(&pDirRec->offExtent));

    return VINF_SUCCESS;
}


/**
 * Validates a dot or dot-dot directory record.
 *
 * @returns IPRT status code (safe to ignore, see pThis->rc).
 * @param   pThis               The importer instance.
 * @param   pDirRec             The dot directory record to validate.
 * @param   cbMax               The maximum size.
 * @param   bName               The name byte (0x00: '.', 0x01: '..').
 */
static int rtFsIsoImportValidateDotDirRec(PRTFSISOMKIMPORTER pThis, PCISO9660DIRREC pDirRec, uint32_t cbMax, uint8_t bName)
{
    int rc = rtFsIsoImportValidateDirRec(pThis, pDirRec, cbMax);
    if (RT_SUCCESS(rc))
    {
        if (pDirRec->bFileIdLength != 1)
            return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_DOT_DIR_REC_BAD_NAME_LENGTH,
                                   "Invalid dot dir rec file id length: %u", pDirRec->bFileIdLength);
        if ((uint8_t)pDirRec->achFileId[0] != bName)
            return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_DOT_DIR_REC_BAD_NAME,
                                   "Invalid dot dir rec file id: %#x, expected %#x", pDirRec->achFileId[0], bName);
    }
    return rc;
}


/**
 * rtFsIsoImportProcessIso9660TreeWorker helper that reads more data.
 *
 * @returns IPRT status code.
 * @param   pThis       The importer instance.
 * @param   ppDirRec    Pointer to the directory record pointer (in/out).
 * @param   pcbChunk    Pointer to the cbChunk variable (in/out).
 * @param   pcbDir      Pointer to the cbDir variable (in/out).  This indicates
 *                      how much we've left to read from the directory.
 * @param   poffNext    Pointer to the offNext variable (in/out).  This
 *                      indicates where the next chunk of directory data is in
 *                      the input file.
 */
static int rtFsIsoImportProcessIso9660TreeWorkerReadMore(PRTFSISOMKIMPORTER pThis, PCISO9660DIRREC *ppDirRec,
                                                         uint32_t *pcbChunk, uint32_t *pcbDir, uint64_t *poffNext)
{
    uint32_t cbChunk = *pcbChunk;
    *ppDirRec = (PCISO9660DIRREC)memmove(&pThis->abBuf[ISO9660_SECTOR_SIZE - cbChunk], *ppDirRec, cbChunk);

    Assert(!(*poffNext & (ISO9660_SECTOR_SIZE - 1)));
    uint32_t cbToRead = RT_MIN(*pcbDir, sizeof(pThis->abBuf) - ISO9660_SECTOR_SIZE);
    int rc = RTVfsFileReadAt(pThis->hSrcFile, *poffNext, &pThis->abBuf[ISO9660_SECTOR_SIZE], cbToRead, NULL);
    if (RT_SUCCESS(rc))
    {
        Log3(("rtFsIsoImportProcessIso9660TreeWorker: Read %#zx more bytes @%#RX64, now got @%#RX64 LB %#RX32\n",
              cbToRead, *poffNext, *poffNext - cbChunk, cbChunk + cbToRead));
        *poffNext += cbToRead;
        *pcbDir   -= cbToRead;
        *pcbChunk  = cbChunk + cbToRead;
        return VINF_SUCCESS;
    }
    return rtFsIsoImpError(pThis, rc, "Error reading %#RX32 bytes at %#RX64 (dir): %Rrc", *poffNext, cbToRead);
}


/**
 * rtFsIsoImportProcessIso9660TreeWorker helper that deals with skipping to the
 * next sector when cbDirRec is zero.
 *
 * @returns IPRT status code.
 * @retval  VERR_NO_MORE_FILES when we reaches the end of the directory.
 * @param   pThis       The importer instance.
 * @param   ppDirRec    Pointer to the directory record pointer (in/out).
 * @param   pcbChunk    Pointer to the cbChunk variable (in/out).  Indicates how
 *                      much we've left to process starting and pDirRec.
 * @param   pcbDir      Pointer to the cbDir variable (in/out).  This indicates
 *                      how much we've left to read from the directory.
 * @param   poffNext    Pointer to the offNext variable (in/out).  This
 *                      indicates where the next chunk of directory data is in
 *                      the input file.
 */
static int rtFsIsoImportProcessIso9660TreeWorkerHandleZeroSizedDirRec(PRTFSISOMKIMPORTER pThis, PCISO9660DIRREC *ppDirRec,
                                                                      uint32_t *pcbChunk, uint32_t *pcbDir, uint64_t *poffNext)
{
    uint32_t cbChunk  = *pcbChunk;
    uint64_t offChunk = *poffNext - cbChunk;
    uint32_t cbSkip   = ISO9660_SECTOR_SIZE - ((uint32_t)offChunk & (ISO9660_SECTOR_SIZE - 1));
    if (cbSkip < cbChunk)
    {
        *ppDirRec = (PCISO9660DIRREC)((uintptr_t)*ppDirRec + cbSkip);
        *pcbChunk = cbChunk -= cbSkip;
        if (   cbChunk > UINT8_MAX
            || *pcbDir == 0)
        {
            Log3(("rtFsIsoImportProcessIso9660TreeWorker: cbDirRec=0 --> jumped %#RX32 to @%#RX64 LB %#RX32\n",
                  cbSkip, *poffNext - cbChunk, cbChunk));
            return VINF_SUCCESS;
        }
        Log3(("rtFsIsoImportProcessIso9660TreeWorker: cbDirRec=0 --> jumped %#RX32 to @%#RX64 LB %#RX32, but needs to read more\n",
              cbSkip, *poffNext - cbChunk, cbChunk));
        return rtFsIsoImportProcessIso9660TreeWorkerReadMore(pThis, ppDirRec, pcbChunk, pcbDir, poffNext);
    }

    /* ASSUMES we're working in multiples of sectors! */
    if (*pcbDir == 0)
    {
        *pcbChunk = 0;
        return VERR_NO_MORE_FILES;
    }

    /* End of chunk, read the next sectors. */
    Assert(!(*poffNext & (ISO9660_SECTOR_SIZE - 1)));
    uint32_t cbToRead = RT_MIN(*pcbDir, sizeof(pThis->abBuf));
    int rc = RTVfsFileReadAt(pThis->hSrcFile, *poffNext, pThis->abBuf, cbToRead, NULL);
    if (RT_SUCCESS(rc))
    {
        Log3(("rtFsIsoImportProcessIso9660TreeWorker: cbDirRec=0 --> Read %#zx more bytes @%#RX64, now got @%#RX64 LB %#RX32\n",
              cbToRead, *poffNext, *poffNext - cbChunk, cbChunk + cbToRead));
        *poffNext += cbToRead;
        *pcbDir   -= cbToRead;
        *pcbChunk  = cbChunk + cbToRead;
        *ppDirRec  = (PCISO9660DIRREC)&pThis->abBuf[0];
        return VINF_SUCCESS;
    }
    return rtFsIsoImpError(pThis, rc, "Error reading %#RX32 bytes at %#RX64 (dir): %Rrc", *poffNext, cbToRead);
}


/**
 * Deals with a single directory.
 *
 * @returns IPRT status code (safe to ignore, see pThis->rc).
 * @param   pThis               The importer instance.
 * @param   idxDir              The configuration index for the directory.
 * @param   offDirBlock         The offset of the directory data.
 * @param   cbDir               The size of the directory data.
 * @param   cDepth              The depth of the directory.
 * @param   fUnicode            Set if it's a unicode (UTF-16BE) encoded
 *                              directory.
 * @param   pTodoList           The todo-list to add sub-directories to.
 */
static int rtFsIsoImportProcessIso9660TreeWorker(PRTFSISOMKIMPORTER pThis, uint32_t idxDir,
                                                 uint32_t offDirBlock, uint32_t cbDir, uint8_t cDepth, bool fUnicode,
                                                 PRTLISTANCHOR pTodoList)
{
    /*
     * Restrict the depth to try avoid loops.
     */
    if (cDepth > RTFSISOMK_IMPORT_MAX_DEPTH)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_TOO_DEEP_DIR_TREE, "Dir at %#x LB %#x is too deep", offDirBlock, cbDir);

    /*
     * Read the first chunk into the big buffer.
     */
    uint32_t cbChunk = RT_MIN(cbDir, sizeof(pThis->abBuf));
    uint64_t offNext = (uint64_t)offDirBlock * ISO9660_SECTOR_SIZE;
    int rc = RTVfsFileReadAt(pThis->hSrcFile, offNext, pThis->abBuf, cbChunk, NULL);
    if (RT_FAILURE(rc))
        return rtFsIsoImpError(pThis, rc, "Error reading directory at %#RX64 (%#RX32 / %#RX32): %Rrc", offNext, cbChunk, cbDir);

    cbDir   -= cbChunk;
    offNext += cbChunk;

    /*
     * Skip the current and parent directory entries.
     */
    PCISO9660DIRREC pDirRec = (PCISO9660DIRREC)&pThis->abBuf[0];
    rc = rtFsIsoImportValidateDotDirRec(pThis, pDirRec, cbChunk, 0x00);
    if (RT_FAILURE(rc))
        return rc;
    if (   cDepth == 0
        && !(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_ROCK_RIDGE)
        && pDirRec->cbDirRec > RT_UOFFSETOF(ISO9660DIRREC, achFileId[1]))
    {
        rc = rtFsIsoImportProcessIso9660TreeWorkerDoRockForRoot(pThis, pDirRec, fUnicode);
        if (RT_FAILURE(rc))
            return rc;
    }

    cbChunk -= pDirRec->cbDirRec;
    pDirRec = (PCISO9660DIRREC)((uintptr_t)pDirRec + pDirRec->cbDirRec);
    rc = rtFsIsoImportValidateDotDirRec(pThis, pDirRec, cbChunk, 0x01);
    if (RT_FAILURE(rc))
        return rc;

    cbChunk -= pDirRec->cbDirRec;
    pDirRec  = (PCISO9660DIRREC)((uintptr_t)pDirRec + pDirRec->cbDirRec);

    /*
     * Work our way thru all the directory records.
     */
    Log3(("rtFsIsoImportProcessIso9660TreeWorker: Starting at @%#RX64 LB %#RX32 (out of %#RX32) in %#x\n",
          offNext - cbChunk, cbChunk, cbChunk + cbDir, idxDir));
    const uint32_t fNamespace = fUnicode ? RTFSISOMAKER_NAMESPACE_JOLIET : RTFSISOMAKER_NAMESPACE_ISO_9660;
    while (   cbChunk > 0
           || cbDir   > 0)
    {
        /*
         * Do we need to read some more?
         */
        if (   cbChunk > UINT8_MAX
            || cbDir == 0)
        { /* No, we don't. */ }
        else
        {
            rc = rtFsIsoImportProcessIso9660TreeWorkerReadMore(pThis, &pDirRec, &cbChunk, &cbDir, &offNext);
            if (RT_FAILURE(rc))
                return rc;
        }

        /* If null length, skip to the next sector.  May have to read some then. */
        if (pDirRec->cbDirRec != 0)
        { /* likely */ }
        else
        {
            rc = rtFsIsoImportProcessIso9660TreeWorkerHandleZeroSizedDirRec(pThis, &pDirRec, &cbChunk, &cbDir, &offNext);
            if (RT_FAILURE(rc))
            {
                if (rc == VERR_NO_MORE_FILES)
                    break;
                return rc;
            }
            if (pDirRec->cbDirRec == 0)
                continue;
        }

        /*
         * Validate the directory record.  Give up if not valid since we're
         * likely to get error with subsequent record too.
         */
        uint8_t const         cbSys = pDirRec->cbDirRec - RT_UOFFSETOF(ISO9660DIRREC, achFileId)
                                    - pDirRec->bFileIdLength - !(pDirRec->bFileIdLength & 1);
        uint8_t const * const pbSys = (uint8_t const *)&pDirRec->achFileId[pDirRec->bFileIdLength + !(pDirRec->bFileIdLength & 1)];
        Log3(("pDirRec=&abBuf[%#07zx]: @%#010RX64 cb=%#04x ff=%#04x off=%#010RX32 cb=%#010RX32 cbSys=%#x id=%.*Rhxs\n",
              (uintptr_t)pDirRec - (uintptr_t)&pThis->abBuf[0], offNext - cbChunk, pDirRec->cbDirRec, pDirRec->fFileFlags,
              ISO9660_GET_ENDIAN(&pDirRec->offExtent), ISO9660_GET_ENDIAN(&pDirRec->cbData), cbSys,
              pDirRec->bFileIdLength, pDirRec->achFileId));
        rc = rtFsIsoImportValidateDirRec(pThis, pDirRec, cbChunk);
        if (RT_FAILURE(rc))
            return rc;

        /* This early calculation of the next record is due to multi-extent
           handling further down. */
        uint32_t        cbChunkNew  = cbChunk - pDirRec->cbDirRec;
        PCISO9660DIRREC pDirRecNext = (PCISO9660DIRREC)((uintptr_t)pDirRec + pDirRec->cbDirRec);

        /* Start Collecting object info. */
        RTFSOBJINFO ObjInfo;
        ObjInfo.cbObject           = ISO9660_GET_ENDIAN(&pDirRec->cbData);
        ObjInfo.cbAllocated        = ObjInfo.cbObject;
        rtFsIsoImpIso9660RecDateTime2TimeSpec(&ObjInfo.AccessTime, &pDirRec->RecTime);
        ObjInfo.ModificationTime   = ObjInfo.AccessTime;
        ObjInfo.ChangeTime         = ObjInfo.AccessTime;
        ObjInfo.BirthTime          = ObjInfo.AccessTime;
        ObjInfo.Attr.fMode         = pDirRec->fFileFlags & ISO9660_FILE_FLAGS_DIRECTORY
                                   ? RTFS_TYPE_DIRECTORY | RTFS_DOS_DIRECTORY | 0555
                                   : RTFS_TYPE_FILE      | RTFS_DOS_ARCHIVED  | 0444;
        ObjInfo.Attr.enmAdditional = RTFSOBJATTRADD_UNIX;
        ObjInfo.Attr.u.Unix.uid             = NIL_RTUID;
        ObjInfo.Attr.u.Unix.gid             = NIL_RTGID;
        ObjInfo.Attr.u.Unix.cHardlinks      = 1;
        ObjInfo.Attr.u.Unix.INodeIdDevice   = 0;
        ObjInfo.Attr.u.Unix.INodeId         = 0;
        ObjInfo.Attr.u.Unix.fFlags          = 0;
        ObjInfo.Attr.u.Unix.GenerationId    = 0;
        ObjInfo.Attr.u.Unix.Device          = 0;

        /*
         * Convert the name into the name buffer (szNameBuf).
         */
        if (!fUnicode)
        {
            memcpy(pThis->szNameBuf, pDirRec->achFileId, pDirRec->bFileIdLength);
            pThis->szNameBuf[pDirRec->bFileIdLength] = '\0';
            rc = RTStrValidateEncoding(pThis->szNameBuf);
        }
        else
        {
            char *pszDst = pThis->szNameBuf;
            rc = RTUtf16BigToUtf8Ex((PRTUTF16)pDirRec->achFileId, pDirRec->bFileIdLength / sizeof(RTUTF16),
                                    &pszDst, sizeof(pThis->szNameBuf), NULL);
        }
        if (RT_SUCCESS(rc))
        {
            /* Drop the version from the name. */
            size_t cchName = strlen(pThis->szNameBuf);
            if (   !(pDirRec->fFileFlags & ISO9660_FILE_FLAGS_DIRECTORY)
                && cchName > 2
                && RT_C_IS_DIGIT(pThis->szNameBuf[cchName - 1]))
            {
                uint32_t offName = 2;
                while (   offName <= 5
                       && offName + 1 < cchName
                       && RT_C_IS_DIGIT(pThis->szNameBuf[cchName - offName]))
                    offName++;
                if (   offName + 1 < cchName
                    && pThis->szNameBuf[cchName - offName] == ';')
                {
                    RTStrToUInt32Full(&pThis->szNameBuf[cchName - offName + 1], 10, &ObjInfo.Attr.u.Unix.GenerationId);
                    pThis->szNameBuf[cchName - offName] = '\0';
                }
            }
            Log3(("  --> name='%s'\n", pThis->szNameBuf));

            pThis->szRockNameBuf[0] = '\0';
            pThis->szRockSymlinkTargetBuf[0] = '\0';
            if (   cbSys > pThis->offSuspSkip
                && !(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_ROCK_RIDGE))
            {
                pThis->fSeenLastNM               = false;
                pThis->fSeenLastSL               = false;
                pThis->szRockNameBuf[0]          = '\0';
                pThis->szRockSymlinkTargetBuf[0] = '\0';
                rtFsIsoImportProcessIso9660TreeWorkerParseRockRidge(pThis, &ObjInfo, &pbSys[pThis->offSuspSkip],
                                                                    cbSys - pThis->offSuspSkip, fUnicode,
                                                                    false /*fContinuationRecord*/, false /*fIsFirstDirRec*/);
            }

            /*
             * Deal with multi-extent files (usually large ones).  We currently only
             * handle files where the data is in single continuous chunk and only split
             * up into multiple directory records because of data type limitations.
             */
            uint8_t  abDirRecCopy[256];
            uint64_t cbData = ISO9660_GET_ENDIAN(&pDirRec->cbData);
            if (!(pDirRec->fFileFlags & ISO9660_FILE_FLAGS_MULTI_EXTENT))
            { /* likely */ }
            else
            {
                if (cbData & (ISO9660_SECTOR_SIZE - 1))
                    return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_MISALIGNED_MULTI_EXTENT,
                                           "The size of non-final multi-extent record #0x0 isn't block aligned: %#RX64", cbData);

                /* Make a copy of the first directory record so we don't overwrite
                   it when reading in more records below.  */
                pDirRec = (PCISO9660DIRREC)memcpy(abDirRecCopy, pDirRec, pDirRec->cbDirRec);

                /* Process extent records. */
                uint32_t cDirRecs     = 1;
                uint32_t offNextBlock = ISO9660_GET_ENDIAN(&pDirRec->offExtent)
                                      + ISO9660_GET_ENDIAN(&pDirRec->cbData) / ISO9660_SECTOR_SIZE;
                while (   cbChunkNew > 0
                       || cbDir > 0)
                {
                    /* Read more? Skip? */
                    if (   cbChunkNew <= UINT8_MAX
                        && cbDir != 0)
                    {
                        rc = rtFsIsoImportProcessIso9660TreeWorkerReadMore(pThis, &pDirRecNext, &cbChunkNew, &cbDir, &offNext);
                        if (RT_FAILURE(rc))
                            return rc;
                    }
                    if (pDirRecNext->cbDirRec == 0)
                    {
                        rc = rtFsIsoImportProcessIso9660TreeWorkerHandleZeroSizedDirRec(pThis, &pDirRecNext, &cbChunkNew,
                                                                                        &cbDir, &offNext);
                        if (RT_FAILURE(rc))
                        {
                            if (rc == VERR_NO_MORE_FILES)
                                break;
                            return rc;
                        }
                        if (pDirRecNext->cbDirRec == 0)
                            continue;
                    }

                    /* Check the next record. */
                    rc = rtFsIsoImportValidateDirRec(pThis, pDirRecNext, cbChunkNew);
                    if (RT_FAILURE(rc))
                        return rc;
                    if (pDirRecNext->bFileIdLength != pDirRec->bFileIdLength)
                        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_MISMATCHING_MULTI_EXTENT_REC,
                                               "Multi-extent record #%#x differs from the first: bFileIdLength is %#x, expected %#x",
                                               cDirRecs, pDirRecNext->bFileIdLength, pDirRec->bFileIdLength);
                    if (memcmp(pDirRecNext->achFileId, pDirRec->achFileId, pDirRec->bFileIdLength) != 0)
                        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_MISMATCHING_MULTI_EXTENT_REC,
                                               "Multi-extent record #%#x differs from the first: achFileId is %.*Rhxs, expected %.*Rhxs",
                                               cDirRecs, pDirRecNext->bFileIdLength, pDirRecNext->achFileId,
                                               pDirRec->bFileIdLength, pDirRec->achFileId);
                    if (ISO9660_GET_ENDIAN(&pDirRecNext->VolumeSeqNo) != ISO9660_GET_ENDIAN(&pDirRec->VolumeSeqNo))
                        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_MISMATCHING_MULTI_EXTENT_REC,
                                               "Multi-extent record #%#x differs from the first: VolumeSeqNo is %#x, expected %#x",
                                               cDirRecs, ISO9660_GET_ENDIAN(&pDirRecNext->VolumeSeqNo),
                                               ISO9660_GET_ENDIAN(&pDirRec->VolumeSeqNo));
                    if (   (pDirRecNext->fFileFlags & ISO9660_FILE_FLAGS_MULTI_EXTENT)
                        && (ISO9660_GET_ENDIAN(&pDirRecNext->cbData) & (ISO9660_SECTOR_SIZE - 1)) )
                        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_MISALIGNED_MULTI_EXTENT,
                                               "The size of non-final multi-extent record #%#x isn't block aligned: %#RX32",
                                               cDirRecs, ISO9660_GET_ENDIAN(&pDirRecNext->cbData));

                    /* Check that the data is contiguous, then add the data.  */
                    if (ISO9660_GET_ENDIAN(&pDirRecNext->offExtent) == offNextBlock)
                        cbData += ISO9660_GET_ENDIAN(&pDirRecNext->cbData);
                    else
                        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_NON_CONTIGUOUS_MULTI_EXTENT,
                                               "Multi-extent record #%#x isn't contiguous: offExtent=%#RX32, expected %#RX32",
                                               cDirRecs, ISO9660_GET_ENDIAN(&pDirRecNext->offExtent), offNextBlock);

                    /* Advance. */
                    cDirRecs++;
                    bool fDone  = !(pDirRecNext->fFileFlags & ISO9660_FILE_FLAGS_MULTI_EXTENT);
                    offNext    += ISO9660_GET_ENDIAN(&pDirRecNext->cbData) / ISO9660_SECTOR_SIZE;
                    cbChunkNew -= pDirRecNext->cbDirRec;
                    pDirRecNext = (PCISO9660DIRREC)((uintptr_t)pDirRecNext + pDirRecNext->cbDirRec);
                    if (fDone)
                        break;
                }
            }
            if (RT_SUCCESS(rc))
            {
                /*
                 * Add the object.
                 */
                if (pDirRec->fFileFlags & ISO9660_FILE_FLAGS_DIRECTORY)
                    rtFsIsoImportProcessIso9660AddAndNameDirectory(pThis, pDirRec, &ObjInfo, cbData, fNamespace, idxDir,
                                                                   pThis->szNameBuf, pThis->szRockNameBuf, cDepth + 1, pTodoList);
                else if (pThis->szRockSymlinkTargetBuf[0] == '\0')
                {
                    if (strcmp(pThis->szNameBuf, pThis->pszTransTbl) != 0)
                        rtFsIsoImportProcessIso9660AddAndNameFile(pThis, pDirRec, &ObjInfo, cbData, fNamespace, idxDir,
                                                                  pThis->szNameBuf, pThis->szRockNameBuf);
                }
                else
                    rtFsIsoImportProcessIso9660AddAndNameSymlink(pThis, pDirRec, &ObjInfo, fNamespace, idxDir, pThis->szNameBuf,
                                                                 pThis->szRockNameBuf, pThis->szRockSymlinkTargetBuf);
            }
        }
        else
            rtFsIsoImpError(pThis, rc, "Invalid name at %#RX64: %.Rhxs",
                            offNext - cbChunk, pDirRec->bFileIdLength, pDirRec->achFileId);

        /*
         * Advance to the next directory record.
         */
        cbChunk = cbChunkNew;
        pDirRec = pDirRecNext;
    }

    return VINF_SUCCESS;
}


/**
 * Deals with a directory tree.
 *
 * This is implemented by tracking directories that needs to be processed in a
 * todo list, so no recursive calls, however it uses a bit of heap.
 *
 * @returns IPRT status code (safe to ignore, see pThis->rc).
 * @param   pThis               The importer instance.
 * @param   offDirBlock         The offset of the root directory data.
 * @param   cbDir               The size of the root directory data.
 * @param   fUnicode            Set if it's a unicode (UTF-16BE) encoded
 *                              directory.
 */
static int rtFsIsoImportProcessIso9660Tree(PRTFSISOMKIMPORTER pThis, uint32_t offDirBlock, uint32_t cbDir, bool fUnicode)
{
    /*
     * Reset some parsing state.
     */
    pThis->offSuspSkip = 0;
    pThis->fSuspSeenSP = false;
    pThis->pszTransTbl = "TRANS.TBL"; /** @todo query this from the iso maker! */

    /*
     * Make sure we've got a root in the namespace.
     */
    uint32_t idxDir = RTFsIsoMakerGetObjIdxForPath(pThis->hIsoMaker,
                                                   !fUnicode ? RTFSISOMAKER_NAMESPACE_ISO_9660 : RTFSISOMAKER_NAMESPACE_JOLIET,
                                                    "/");
    if (idxDir == UINT32_MAX)
    {
        idxDir = RTFSISOMAKER_CFG_IDX_ROOT;
        int rc = RTFsIsoMakerObjSetPath(pThis->hIsoMaker, RTFSISOMAKER_CFG_IDX_ROOT,
                                        !fUnicode ? RTFSISOMAKER_NAMESPACE_ISO_9660 : RTFSISOMAKER_NAMESPACE_JOLIET, "/");
        if (RT_FAILURE(rc))
            return rtFsIsoImpError(pThis, rc, "RTFsIsoMakerObjSetPath failed on root dir: %Rrc", rc);
    }
    Assert(idxDir == RTFSISOMAKER_CFG_IDX_ROOT);

    /*
     * Directories.
     */
    int          rc     = VINF_SUCCESS;
    uint8_t      cDepth = 0;
    RTLISTANCHOR TodoList;
    RTListInit(&TodoList);
    for (;;)
    {
        int rc2 = rtFsIsoImportProcessIso9660TreeWorker(pThis, idxDir, offDirBlock, cbDir, cDepth, fUnicode, &TodoList);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;

        /*
         * Pop the next directory.
         */
        PRTFSISOMKIMPDIR pNext = RTListRemoveLast(&TodoList, RTFSISOMKIMPDIR, Entry);
        if (!pNext)
            break;
        idxDir      = pNext->idxObj;
        offDirBlock = pNext->offDirBlock;
        cbDir       = pNext->cbDir;
        cDepth      = pNext->cDepth;
        RTMemFree(pNext);
    }

    return rc;
}


/**
 * Imports a UTF-16BE string property from the joliet volume descriptor.
 *
 * The fields are normally space filled and padded, but we also consider zero
 * bytes are fillers.  If the field only contains padding, the string property
 * will remain unchanged.
 *
 * @returns IPRT status code (ignorable).
 * @param   pThis               The importer instance.
 * @param   pachField           Pointer to the field.  The structure type
 *                              is 'char' for hysterical raisins, while the
 *                              real type is 'RTUTF16'.
 * @param   cchField            The field length.
 * @param   enmStringProp       The corresponding string property.
 *
 * @note    Clobbers pThis->pbBuf!
 */
static int rtFsIsoImportUtf16BigStringField(PRTFSISOMKIMPORTER pThis, const char *pachField, size_t cchField,
                                            RTFSISOMAKERSTRINGPROP enmStringProp)
{
    /*
     * Scan the field from the end as this way we know the result length if we find anything.
     */
    PCRTUTF16 pwcField = (PCRTUTF16)pachField;
    size_t    cwcField = cchField / sizeof(RTUTF16); /* ignores any odd field byte  */
    size_t    off      = cwcField;
    while (off-- > 0)
    {
        RTUTF16 wc = RT_BE2H_U16(pwcField[off]);
        if (wc == ' ' || wc == '\0')
        { /* likely */ }
        else
        {
            /*
             * Convert to UTF-16.
             */
            char *pszCopy = (char *)pThis->abBuf;
            int rc = RTUtf16BigToUtf8Ex(pwcField, off + 1, &pszCopy, sizeof(pThis->abBuf), NULL);
            if (RT_SUCCESS(rc))
            {
                rc = RTFsIsoMakerSetStringProp(pThis->hIsoMaker, enmStringProp, RTFSISOMAKER_NAMESPACE_JOLIET, pszCopy);
                if (RT_SUCCESS(rc))
                    return VINF_SUCCESS;
                return rtFsIsoImpError(pThis, rc, "RTFsIsoMakerSetStringProp failed setting field %d to '%s': %Rrc",
                                       enmStringProp, pszCopy, rc);
            }
            return rtFsIsoImpError(pThis, rc, "RTUtf16BigToUtf8Ex failed converting field %d to UTF-8: %Rrc - %.*Rhxs",
                                   enmStringProp, rc, off * sizeof(RTUTF16), pwcField);
        }
    }
    return VINF_SUCCESS;
}


/**
 * Imports a string property from the primary volume descriptor.
 *
 * The fields are normally space filled and padded, but we also consider zero
 * bytes are fillers.  If the field only contains padding, the string property
 * will remain unchanged.
 *
 * @returns IPRT status code (ignorable).
 * @param   pThis               The importer instance.
 * @param   pachField           Pointer to the field.
 * @param   cchField            The field length.
 * @param   enmStringProp       The corresponding string property.
 *
 * @note    Clobbers pThis->pbBuf!
 */
static int rtFsIsoImportAsciiStringField(PRTFSISOMKIMPORTER pThis, const char *pachField, size_t cchField,
                                         RTFSISOMAKERSTRINGPROP enmStringProp)
{
    /*
     * Scan the field from the end as this way we know the result length if we find anything.
     */
    size_t off = cchField;
    while (off-- > 0)
    {
        char ch = pachField[off];
        if (ch == ' ' || ch == '\0')
        { /* likely */ }
        else
        {
            /*
             * Make a copy of the string in abBuf, purge the encoding.
             */
            off++;
            char *pszCopy = (char *)pThis->abBuf;
            memcpy(pszCopy, pachField, off);
            pszCopy[off] = '\0';
            RTStrPurgeEncoding(pszCopy);

            int rc = RTFsIsoMakerSetStringProp(pThis->hIsoMaker, enmStringProp, RTFSISOMAKER_NAMESPACE_ISO_9660, pszCopy);
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;
            return rtFsIsoImpError(pThis, rc, "RTFsIsoMakerSetStringProp failed setting field %d to '%s': %Rrc",
                                   enmStringProp, pszCopy, rc);
        }
    }
    return VINF_SUCCESS;
}


/**
 * Validates a root directory record.
 *
 * @returns IPRT status code (safe to ignore, see pThis->rc).
 * @param   pThis               The importer instance.
 * @param   pDirRec             The root directory record to validate.
 */
static int rtFsIsoImportValidateRootDirRec(PRTFSISOMKIMPORTER pThis, PCISO9660DIRREC pDirRec)
{
    /*
     * Validate dual fields.
     */
    if (RT_LE2H_U32(pDirRec->cbData.le) != RT_BE2H_U32(pDirRec->cbData.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_ROOT_DIR_REC,
                               "Invalid root dir size: {%#RX32,%#RX32}",
                               RT_BE2H_U32(pDirRec->cbData.be), RT_LE2H_U32(pDirRec->cbData.le));

    if (RT_LE2H_U32(pDirRec->offExtent.le) != RT_BE2H_U32(pDirRec->offExtent.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_ROOT_DIR_REC,
                               "Invalid root dir extent: {%#RX32,%#RX32}",
                               RT_BE2H_U32(pDirRec->offExtent.be), RT_LE2H_U32(pDirRec->offExtent.le));

    if (RT_LE2H_U16(pDirRec->VolumeSeqNo.le) != RT_BE2H_U16(pDirRec->VolumeSeqNo.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_ROOT_DIR_REC,
                               "Invalid root dir volume sequence ID: {%#RX16,%#RX16}",
                               RT_BE2H_U16(pDirRec->VolumeSeqNo.be), RT_LE2H_U16(pDirRec->VolumeSeqNo.le));

    /*
     * Check values.
     */
    if (ISO9660_GET_ENDIAN(&pDirRec->VolumeSeqNo) != pThis->idPrimaryVol)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_ROOT_VOLUME_SEQ_NO,
                               "Expected root dir to have same volume sequence number as primary volume: %#x, expected %#x",
                               ISO9660_GET_ENDIAN(&pDirRec->VolumeSeqNo), pThis->idPrimaryVol);

    if (ISO9660_GET_ENDIAN(&pDirRec->cbData) == 0)
        return RTErrInfoSet(pThis->pErrInfo, VERR_ISOMK_IMPORT_ZERO_SIZED_ROOT_DIR, "Zero sized root dir");

    if (ISO9660_GET_ENDIAN(&pDirRec->offExtent) >= pThis->cBlocksInPrimaryVolumeSpace)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_ROOT_DIR_EXTENT_OUT_OF_BOUNDS,
                               "Invalid root dir extent: %#RX32, max %#RX32",
                               ISO9660_GET_ENDIAN(&pDirRec->offExtent), pThis->cBlocksInPrimaryVolumeSpace);

    if (pDirRec->cbDirRec < RT_UOFFSETOF(ISO9660DIRREC, achFileId))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_ROOT_DIR_REC_LENGTH,
                               "Root dir record size is too small: %#x (min %#x)",
                               pDirRec->cbDirRec, RT_UOFFSETOF(ISO9660DIRREC, achFileId));

    if (!(pDirRec->fFileFlags & ISO9660_FILE_FLAGS_DIRECTORY))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_ROOT_DIR_WITHOUT_DIR_FLAG,
                               "Root dir is not flagged as directory: %#x", pDirRec->fFileFlags);
    if (pDirRec->fFileFlags & ISO9660_FILE_FLAGS_MULTI_EXTENT)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_ROOT_DIR_IS_MULTI_EXTENT,
                               "Root dir is cannot be multi-extent: %#x", pDirRec->fFileFlags);

    return VINF_SUCCESS;
}


/**
 * Processes a primary volume descriptor, importing all files and stuff.
 *
 * @returns IPRT status code (safe to ignore, see pThis->rc).
 * @param   pThis               The importer instance.
 * @param   pVolDesc            The primary volume descriptor.
 */
static int rtFsIsoImportProcessPrimaryDesc(PRTFSISOMKIMPORTER pThis, PISO9660PRIMARYVOLDESC pVolDesc)
{
    /*
     * Validate dual fields first.
     */
    if (pVolDesc->bFileStructureVersion != ISO9660_FILE_STRUCTURE_VERSION)
        return rtFsIsoImpError(pThis, VERR_IOSMK_IMPORT_PRIMARY_VOL_DESC_VER,
                               "Unsupported file structure version: %#x", pVolDesc->bFileStructureVersion);

    if (RT_LE2H_U16(pVolDesc->cbLogicalBlock.le) != RT_BE2H_U16(pVolDesc->cbLogicalBlock.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_PRIMARY_VOL_DESC,
                               "Mismatching logical block size: {%#RX16,%#RX16}",
                               RT_BE2H_U16(pVolDesc->cbLogicalBlock.be), RT_LE2H_U16(pVolDesc->cbLogicalBlock.le));
    if (RT_LE2H_U32(pVolDesc->VolumeSpaceSize.le) != RT_BE2H_U32(pVolDesc->VolumeSpaceSize.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_PRIMARY_VOL_DESC,
                               "Mismatching volume space size: {%#RX32,%#RX32}",
                               RT_BE2H_U32(pVolDesc->VolumeSpaceSize.be), RT_LE2H_U32(pVolDesc->VolumeSpaceSize.le));
    if (RT_LE2H_U16(pVolDesc->cVolumesInSet.le) != RT_BE2H_U16(pVolDesc->cVolumesInSet.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_PRIMARY_VOL_DESC,
                               "Mismatching volumes in set: {%#RX16,%#RX16}",
                               RT_BE2H_U16(pVolDesc->cVolumesInSet.be), RT_LE2H_U16(pVolDesc->cVolumesInSet.le));
    if (RT_LE2H_U16(pVolDesc->VolumeSeqNo.le) != RT_BE2H_U16(pVolDesc->VolumeSeqNo.be))
    {
        /* Hack alert! An Windows NT 3.1 ISO was found to not have the big endian bit set here, so work around it. */
        if (   pVolDesc->VolumeSeqNo.be == 0
            && pVolDesc->VolumeSeqNo.le == RT_H2LE_U16_C(1))
            pVolDesc->VolumeSeqNo.be = RT_H2BE_U16_C(1);
        else
            return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_PRIMARY_VOL_DESC,
                                   "Mismatching volume sequence no.: {%#RX16,%#RX16}",
                                   RT_BE2H_U16(pVolDesc->VolumeSeqNo.be), RT_LE2H_U16(pVolDesc->VolumeSeqNo.le));
    }
    if (RT_LE2H_U32(pVolDesc->cbPathTable.le) != RT_BE2H_U32(pVolDesc->cbPathTable.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_PRIMARY_VOL_DESC,
                               "Mismatching path table size: {%#RX32,%#RX32}",
                               RT_BE2H_U32(pVolDesc->cbPathTable.be), RT_LE2H_U32(pVolDesc->cbPathTable.le));

    /*
     * Validate field values against our expectations.
     */
    if (ISO9660_GET_ENDIAN(&pVolDesc->cbLogicalBlock) != ISO9660_SECTOR_SIZE)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_LOGICAL_BLOCK_SIZE_NOT_2KB,
                               "Unsupported block size: %#x", ISO9660_GET_ENDIAN(&pVolDesc->cbLogicalBlock));

    if (ISO9660_GET_ENDIAN(&pVolDesc->cVolumesInSet) != 1)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_MORE_THAN_ONE_VOLUME_IN_SET,
                               "Volumes in set: %#x", ISO9660_GET_ENDIAN(&pVolDesc->cVolumesInSet));

    if (ISO9660_GET_ENDIAN(&pVolDesc->VolumeSeqNo) != 1)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_INVALID_VOLUMNE_SEQ_NO,
                               "Unexpected volume sequence number: %#x", ISO9660_GET_ENDIAN(&pVolDesc->VolumeSeqNo));

    /*
     * Gather info we need.
     */
    pThis->cBlocksInPrimaryVolumeSpace  = ISO9660_GET_ENDIAN(&pVolDesc->VolumeSpaceSize);
    pThis->cbPrimaryVolumeSpace         = pThis->cBlocksInPrimaryVolumeSpace * (uint64_t)ISO9660_SECTOR_SIZE;
    pThis->cVolumesInSet                = ISO9660_GET_ENDIAN(&pVolDesc->cVolumesInSet);
    pThis->idPrimaryVol                 = ISO9660_GET_ENDIAN(&pVolDesc->VolumeSeqNo);

    /*
     * Validate the root directory record.
     */
    int rc = rtFsIsoImportValidateRootDirRec(pThis, &pVolDesc->RootDir.DirRec);
    if (RT_SUCCESS(rc))
    {
        /*
         * Import stuff if present and not opted out.
         */
        if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_SYSTEM_ID))
            rtFsIsoImportAsciiStringField(pThis, pVolDesc->achSystemId, sizeof(pVolDesc->achSystemId),
                                          RTFSISOMAKERSTRINGPROP_SYSTEM_ID);
        if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_VOLUME_ID))
            rtFsIsoImportAsciiStringField(pThis, pVolDesc->achVolumeId, sizeof(pVolDesc->achVolumeId),
                                          RTFSISOMAKERSTRINGPROP_VOLUME_ID);
        if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_VOLUME_SET_ID))
            rtFsIsoImportAsciiStringField(pThis, pVolDesc->achVolumeSetId, sizeof(pVolDesc->achVolumeSetId),
                                          RTFSISOMAKERSTRINGPROP_VOLUME_SET_ID);
        if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_PUBLISHER_ID))
            rtFsIsoImportAsciiStringField(pThis, pVolDesc->achPublisherId, sizeof(pVolDesc->achPublisherId),
                                          RTFSISOMAKERSTRINGPROP_PUBLISHER_ID);
        if (pThis->fFlags & RTFSISOMK_IMPORT_F_DATA_PREPARER_ID)
            rtFsIsoImportAsciiStringField(pThis, pVolDesc->achDataPreparerId, sizeof(pVolDesc->achDataPreparerId),
                                          RTFSISOMAKERSTRINGPROP_DATA_PREPARER_ID);
        if (pThis->fFlags & RTFSISOMK_IMPORT_F_APPLICATION_ID)
            rtFsIsoImportAsciiStringField(pThis, pVolDesc->achApplicationId, sizeof(pVolDesc->achApplicationId),
                                          RTFSISOMAKERSTRINGPROP_APPLICATION_ID);
        if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_COPYRIGHT_FID))
            rtFsIsoImportAsciiStringField(pThis, pVolDesc->achCopyrightFileId, sizeof(pVolDesc->achCopyrightFileId),
                                          RTFSISOMAKERSTRINGPROP_COPYRIGHT_FILE_ID);
        if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_ABSTRACT_FID))
            rtFsIsoImportAsciiStringField(pThis, pVolDesc->achAbstractFileId, sizeof(pVolDesc->achAbstractFileId),
                                          RTFSISOMAKERSTRINGPROP_ABSTRACT_FILE_ID);
        if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_BIBLIO_FID))
            rtFsIsoImportAsciiStringField(pThis, pVolDesc->achBibliographicFileId, sizeof(pVolDesc->achBibliographicFileId),
                                          RTFSISOMAKERSTRINGPROP_BIBLIOGRAPHIC_FILE_ID);

        /*
         * Process the directory tree.
         */
        if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_PRIMARY_ISO))
            rc = rtFsIsoImportProcessIso9660Tree(pThis, ISO9660_GET_ENDIAN(&pVolDesc->RootDir.DirRec.offExtent),
                                                 ISO9660_GET_ENDIAN(&pVolDesc->RootDir.DirRec.cbData), false /*fUnicode*/);
    }

    return rc;
}


/**
 * Processes a secondary volume descriptor, if it is joliet we'll importing all
 * the files and stuff.
 *
 * @returns IPRT status code (safe to ignore, see pThis->rc).
 * @param   pThis               The importer instance.
 * @param   pVolDesc            The primary volume descriptor.
 */
static int rtFsIsoImportProcessSupplementaryDesc(PRTFSISOMKIMPORTER pThis, PISO9660SUPVOLDESC pVolDesc)
{
    /*
     * Validate dual fields first.
     */
    if (pVolDesc->bFileStructureVersion != ISO9660_FILE_STRUCTURE_VERSION)
        return rtFsIsoImpError(pThis, VERR_IOSMK_IMPORT_SUP_VOL_DESC_VER,
                               "Unsupported file structure version: %#x", pVolDesc->bFileStructureVersion);

    if (RT_LE2H_U16(pVolDesc->cbLogicalBlock.le) != RT_BE2H_U16(pVolDesc->cbLogicalBlock.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_SUP_VOL_DESC,
                               "Mismatching logical block size: {%#RX16,%#RX16}",
                               RT_BE2H_U16(pVolDesc->cbLogicalBlock.be), RT_LE2H_U16(pVolDesc->cbLogicalBlock.le));
    if (RT_LE2H_U32(pVolDesc->VolumeSpaceSize.le) != RT_BE2H_U32(pVolDesc->VolumeSpaceSize.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_SUP_VOL_DESC,
                               "Mismatching volume space size: {%#RX32,%#RX32}",
                               RT_BE2H_U32(pVolDesc->VolumeSpaceSize.be), RT_LE2H_U32(pVolDesc->VolumeSpaceSize.le));
    if (RT_LE2H_U16(pVolDesc->cVolumesInSet.le) != RT_BE2H_U16(pVolDesc->cVolumesInSet.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_SUP_VOL_DESC,
                               "Mismatching volumes in set: {%#RX16,%#RX16}",
                               RT_BE2H_U16(pVolDesc->cVolumesInSet.be), RT_LE2H_U16(pVolDesc->cVolumesInSet.le));
    if (RT_LE2H_U16(pVolDesc->VolumeSeqNo.le) != RT_BE2H_U16(pVolDesc->VolumeSeqNo.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_SUP_VOL_DESC,
                               "Mismatching volume sequence no.: {%#RX16,%#RX16}",
                               RT_BE2H_U16(pVolDesc->VolumeSeqNo.be), RT_LE2H_U16(pVolDesc->VolumeSeqNo.le));
    if (RT_LE2H_U32(pVolDesc->cbPathTable.le) != RT_BE2H_U32(pVolDesc->cbPathTable.be))
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BAD_SUP_VOL_DESC,
                               "Mismatching path table size: {%#RX32,%#RX32}",
                               RT_BE2H_U32(pVolDesc->cbPathTable.be), RT_LE2H_U32(pVolDesc->cbPathTable.le));

    /*
     * Validate field values against our expectations.
     */
    if (ISO9660_GET_ENDIAN(&pVolDesc->cbLogicalBlock) != ISO9660_SECTOR_SIZE)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_LOGICAL_BLOCK_SIZE_NOT_2KB,
                               "Unsupported block size: %#x", ISO9660_GET_ENDIAN(&pVolDesc->cbLogicalBlock));

    if (ISO9660_GET_ENDIAN(&pVolDesc->cVolumesInSet) != pThis->cVolumesInSet)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_VOLUME_IN_SET_MISMATCH, "Volumes in set: %#x, expected %#x",
                               ISO9660_GET_ENDIAN(&pVolDesc->cVolumesInSet), pThis->cVolumesInSet);

    if (ISO9660_GET_ENDIAN(&pVolDesc->VolumeSeqNo) != pThis->idPrimaryVol)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_INVALID_VOLUMNE_SEQ_NO,
                               "Unexpected volume sequence number: %#x (expected %#x)",
                               ISO9660_GET_ENDIAN(&pVolDesc->VolumeSeqNo), pThis->idPrimaryVol);

    if (ISO9660_GET_ENDIAN(&pVolDesc->VolumeSpaceSize) != pThis->cBlocksInPrimaryVolumeSpace)
    {
        /* ubuntu-21.10-desktop-amd64.iso has 0x172f4e blocks (3 111 809 024 bytes) here
           and 0x173838 blocks (3 116 482 560 bytes) in the primary, a difference of
           -2282 blocks (-4 673 536 bytes).  Guess something was omitted from the joliet
           edition, not immediately obvious what though.

           For now we'll just let it pass as long as the primary size is the larger.
           (Not quite sure how the code will handle a supplementary volume spanning
           more space, as I suspect it only uses the primary volume size for
           validating block addresses and such.) */
        LogRel(("rtFsIsoImportProcessSupplementaryDesc: Volume space size differs between primary and supplementary descriptors: %#x, primary %#x",
                ISO9660_GET_ENDIAN(&pVolDesc->VolumeSpaceSize), pThis->cBlocksInPrimaryVolumeSpace));
        if (ISO9660_GET_ENDIAN(&pVolDesc->VolumeSpaceSize) > pThis->cBlocksInPrimaryVolumeSpace)
            return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_VOLUME_SPACE_SIZE_MISMATCH,
                                   "Volume space given in the supplementary descriptor is larger than in the primary: %#x, primary %#x",
                                   ISO9660_GET_ENDIAN(&pVolDesc->VolumeSpaceSize), pThis->cBlocksInPrimaryVolumeSpace);
    }

    /*
     * Validate the root directory record.
     */
    int rc = rtFsIsoImportValidateRootDirRec(pThis, &pVolDesc->RootDir.DirRec);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Is this a joliet descriptor? Ignore if not.
     */
    uint8_t uJolietLevel = 0;
    if (   pVolDesc->abEscapeSequences[0] == ISO9660_JOLIET_ESC_SEQ_0
        && pVolDesc->abEscapeSequences[1] == ISO9660_JOLIET_ESC_SEQ_1)
        switch (pVolDesc->abEscapeSequences[2])
        {
            case ISO9660_JOLIET_ESC_SEQ_2_LEVEL_1: uJolietLevel = 1; break;
            case ISO9660_JOLIET_ESC_SEQ_2_LEVEL_2: uJolietLevel = 2; break;
            case ISO9660_JOLIET_ESC_SEQ_2_LEVEL_3: uJolietLevel = 3; break;
            default: Log(("rtFsIsoImportProcessSupplementaryDesc: last joliet escape sequence byte doesn't match: %#x\n",
                          pVolDesc->abEscapeSequences[2]));
        }
    if (uJolietLevel == 0)
        return VINF_SUCCESS;

    /*
     * Only one joliet descriptor.
     */
    if (pThis->fSeenJoliet)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_MULTIPLE_JOLIET_VOL_DESCS,
                               "More than one Joliet volume descriptor is not supported");
    pThis->fSeenJoliet = true;

    /*
     * Import stuff if present and not opted out.
     */
    if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_SYSTEM_ID))
        rtFsIsoImportUtf16BigStringField(pThis, pVolDesc->achSystemId, sizeof(pVolDesc->achSystemId),
                                         RTFSISOMAKERSTRINGPROP_SYSTEM_ID);
    if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_J_VOLUME_ID))
        rtFsIsoImportUtf16BigStringField(pThis, pVolDesc->achVolumeId, sizeof(pVolDesc->achVolumeId),
                                         RTFSISOMAKERSTRINGPROP_VOLUME_ID);
    if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_J_VOLUME_SET_ID))
        rtFsIsoImportUtf16BigStringField(pThis, pVolDesc->achVolumeSetId, sizeof(pVolDesc->achVolumeSetId),
                                         RTFSISOMAKERSTRINGPROP_VOLUME_SET_ID);
    if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_J_PUBLISHER_ID))
        rtFsIsoImportUtf16BigStringField(pThis, pVolDesc->achPublisherId, sizeof(pVolDesc->achPublisherId),
                                         RTFSISOMAKERSTRINGPROP_PUBLISHER_ID);
    if (pThis->fFlags & RTFSISOMK_IMPORT_F_J_DATA_PREPARER_ID)
        rtFsIsoImportUtf16BigStringField(pThis, pVolDesc->achDataPreparerId, sizeof(pVolDesc->achDataPreparerId),
                                         RTFSISOMAKERSTRINGPROP_DATA_PREPARER_ID);
    if (pThis->fFlags & RTFSISOMK_IMPORT_F_J_APPLICATION_ID)
        rtFsIsoImportUtf16BigStringField(pThis, pVolDesc->achApplicationId, sizeof(pVolDesc->achApplicationId),
                                         RTFSISOMAKERSTRINGPROP_APPLICATION_ID);
    if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_J_COPYRIGHT_FID))
        rtFsIsoImportUtf16BigStringField(pThis, pVolDesc->achCopyrightFileId, sizeof(pVolDesc->achCopyrightFileId),
                                         RTFSISOMAKERSTRINGPROP_COPYRIGHT_FILE_ID);
    if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_J_ABSTRACT_FID))
        rtFsIsoImportUtf16BigStringField(pThis, pVolDesc->achAbstractFileId, sizeof(pVolDesc->achAbstractFileId),
                                         RTFSISOMAKERSTRINGPROP_ABSTRACT_FILE_ID);
    if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_J_BIBLIO_FID))
        rtFsIsoImportUtf16BigStringField(pThis, pVolDesc->achBibliographicFileId, sizeof(pVolDesc->achBibliographicFileId),
                                         RTFSISOMAKERSTRINGPROP_BIBLIOGRAPHIC_FILE_ID);

    /*
     * Process the directory tree.
     */
    if (!(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_JOLIET))
        return rtFsIsoImportProcessIso9660Tree(pThis, ISO9660_GET_ENDIAN(&pVolDesc->RootDir.DirRec.offExtent),
                                               ISO9660_GET_ENDIAN(&pVolDesc->RootDir.DirRec.cbData), true /*fUnicode*/);
    return VINF_SUCCESS;
}


/**
 * Checks out an El Torito boot image to see if it requires info table patching.
 *
 * @returns IPRT status code (ignored).
 * @param   pThis           The ISO importer instance.
 * @param   idxImageObj     The configuration index of the image.
 * @param   offBootImage    The block offset of the image.
 */
static int rtFsIsoImportProcessElToritoImage(PRTFSISOMKIMPORTER pThis, uint32_t idxImageObj, uint32_t offBootImage)
{
    ISO9660SYSLINUXINFOTABLE InfoTable;
    int rc = RTVfsFileReadAt(pThis->hSrcFile, offBootImage * (uint64_t)ISO9660_SECTOR_SIZE + ISO9660SYSLINUXINFOTABLE_OFFSET,
                             &InfoTable, sizeof(InfoTable), NULL);
    if (RT_SUCCESS(rc))
    {
        if (   RT_LE2H_U32(InfoTable.offBootFile) == offBootImage
            && RT_LE2H_U32(InfoTable.offPrimaryVolDesc) == pThis->offPrimaryVolDesc
            && ASMMemIsAllU8(&InfoTable.auReserved[0], sizeof(InfoTable.auReserved), 0) )
        {
            rc = RTFsIsoMakerObjEnableBootInfoTablePatching(pThis->hIsoMaker, idxImageObj, true /*fEnable*/);
            if (RT_FAILURE(rc))
                return rtFsIsoImpError(pThis, rc, "RTFsIsoMakerObjEnableBootInfoTablePatching failed: %Rrc", rc);
        }
    }
    return VINF_SUCCESS;
}


/**
 * Processes a boot catalog default or section entry.
 *
 * @returns IPRT status code (ignored).
 * @param   pThis       The ISO importer instance.
 * @param   iEntry      The boot catalog entry number. This is 1 for
 *                      the default entry, and 3+ for section entries.
 * @param   cMaxEntries Maximum number of entries.
 * @param   pEntry      The entry to process.
 * @param   pcSkip      Where to return the number of extension entries to skip.
 */
static int rtFsIsoImportProcessElToritoSectionEntry(PRTFSISOMKIMPORTER pThis, uint32_t iEntry, uint32_t cMaxEntries,
                                                    PCISO9660ELTORITOSECTIONENTRY pEntry, uint32_t *pcSkip)
{
    *pcSkip = 0;

    /*
     * Check the boot indicator type for entry 1.
     */
    if (   pEntry->bBootIndicator != ISO9660_ELTORITO_BOOT_INDICATOR_BOOTABLE
        && pEntry->bBootIndicator != ISO9660_ELTORITO_BOOT_INDICATOR_NOT_BOOTABLE)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_DEF_ENTRY_INVALID_BOOT_IND,
                               "Default boot catalog entry has an invalid boot indicator: %#x", pEntry->bBootIndicator);

    /*
     * Check the media type and flags.
     */
    uint32_t cbDefaultSize;
    uint8_t  bMediaType   = pEntry->bBootMediaType;
    switch (bMediaType & ISO9660_ELTORITO_BOOT_MEDIA_TYPE_MASK)
    {
        case ISO9660_ELTORITO_BOOT_MEDIA_TYPE_FLOPPY_1_2_MB:
            cbDefaultSize = 512 * 80 * 15 * 2;
            break;

        case ISO9660_ELTORITO_BOOT_MEDIA_TYPE_FLOPPY_1_44_MB:
            cbDefaultSize = 512 * 80 * 18 * 2;
            break;

        case ISO9660_ELTORITO_BOOT_MEDIA_TYPE_FLOPPY_2_88_MB:
            cbDefaultSize = 512 * 80 * 36 * 2;
            break;

        case ISO9660_ELTORITO_BOOT_MEDIA_TYPE_NO_EMULATION:
        case ISO9660_ELTORITO_BOOT_MEDIA_TYPE_HARD_DISK:
            cbDefaultSize = 0;
            break;

        default:
            return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_INVALID_BOOT_MEDIA_TYPE,
                                   "Boot catalog entry #%#x has an invalid boot media type: %#x", bMediaType);
    }

    if (iEntry == 1)
    {
        if (bMediaType & ISO9660_ELTORITO_BOOT_MEDIA_F_MASK)
        {
            rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_DEF_ENTRY_INVALID_FLAGS,
                            "Boot catalog entry #%#x has an invalid boot media type: %#x", bMediaType);
            bMediaType &= ~ISO9660_ELTORITO_BOOT_MEDIA_F_MASK;
        }
    }
    else
    {
        if (bMediaType & ISO9660_ELTORITO_BOOT_MEDIA_F_RESERVED)
        {
            rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_ENTRY_RESERVED_FLAG,
                            "Boot catalog entry #%#x has an invalid boot media type: %#x", bMediaType);
            bMediaType &= ~ISO9660_ELTORITO_BOOT_MEDIA_F_RESERVED;
        }
    }

    /*
     * Complain if bUnused is used.
     */
    if (pEntry->bUnused != 0)
        rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_ENTRY_USES_UNUSED_FIELD,
                        "Boot catalog entry #%#x has a non-zero unused field: %#x", pEntry->bUnused);

    /*
     * Check out the boot image offset and turn that into an index of a file
     */
    uint32_t offBootImage = RT_LE2H_U32(pEntry->offBootImage);
    if (offBootImage >= pThis->cBlocksInSrcFile)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_ENTRY_IMAGE_OUT_OF_BOUNDS,
                               "Boot catalog entry #%#x has an out of bound boot image block number: %#RX32, max %#RX32",
                               offBootImage, pThis->cBlocksInPrimaryVolumeSpace);

    int                     rc;
    uint32_t                idxImageObj;
    PRTFSISOMKIMPBLOCK2FILE pBlock2File = (PRTFSISOMKIMPBLOCK2FILE)RTAvlU32Get(&pThis->Block2FileRoot, offBootImage);
    if (pBlock2File)
        idxImageObj = pBlock2File->idxObj;
    else
    {
        if (cbDefaultSize == 0)
        {
            pBlock2File = (PRTFSISOMKIMPBLOCK2FILE)RTAvlU32GetBestFit(&pThis->Block2FileRoot, offBootImage, true /*fAbove*/);
            if (pBlock2File)
                cbDefaultSize = RT_MIN(pBlock2File->Core.Key - offBootImage, UINT32_MAX / ISO9660_SECTOR_SIZE + 1)
                              * ISO9660_SECTOR_SIZE;
            else if (offBootImage < pThis->cBlocksInSrcFile)
                cbDefaultSize = RT_MIN(pThis->cBlocksInSrcFile - offBootImage, UINT32_MAX / ISO9660_SECTOR_SIZE + 1)
                              * ISO9660_SECTOR_SIZE;
            else
                return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_ENTRY_UNKNOWN_IMAGE_SIZE,
                                       "Boot catalog entry #%#x has an invalid boot media type: %#x", bMediaType);
        }

        if (pThis->idxSrcFile != UINT32_MAX)
        {
            rc = RTFsIsoMakerAddCommonSourceFile(pThis->hIsoMaker, pThis->hSrcFile, &pThis->idxSrcFile);
            if (RT_FAILURE(rc))
                return rtFsIsoImpError(pThis, rc, "RTFsIsoMakerAddCommonSourceFile failed: %Rrc", rc);
            Assert(pThis->idxSrcFile != UINT32_MAX);
        }

        rc = RTFsIsoMakerAddUnnamedFileWithCommonSrc(pThis->hIsoMaker, pThis->idxSrcFile,
                                                     offBootImage * (uint64_t)ISO9660_SECTOR_SIZE,
                                                     cbDefaultSize, NULL, &idxImageObj);
        if (RT_FAILURE(rc))
            return rtFsIsoImpError(pThis, rc, "RTFsIsoMakerAddUnnamedFileWithCommonSrc failed on boot entry #%#x: %Rrc",
                                   iEntry, rc);
    }

    /*
     * Deal with selection criteria. Use the last sector of abBuf to gather it
     * into a single data chunk.
     */
    size_t   cbSelCrit = 0;
    uint8_t *pbSelCrit = &pThis->abBuf[sizeof(pThis->abBuf) - ISO9660_SECTOR_SIZE];
    if (pEntry->bSelectionCriteriaType != ISO9660_ELTORITO_SEL_CRIT_TYPE_NONE)
    {
        memcpy(pbSelCrit, pEntry->abSelectionCriteria, sizeof(pEntry->abSelectionCriteria));
        cbSelCrit = sizeof(pEntry->abSelectionCriteria);

        if (   (bMediaType & ISO9660_ELTORITO_BOOT_MEDIA_F_CONTINUATION)
            && iEntry + 1 < cMaxEntries)
        {
            uint32_t                         iExtEntry = iEntry + 1;
            PCISO9660ELTORITOSECTIONENTRYEXT pExtEntry = (PCISO9660ELTORITOSECTIONENTRYEXT)pEntry;
            for (;;)
            {
                pExtEntry++;

                if (pExtEntry->bExtensionId != ISO9660_ELTORITO_SECTION_ENTRY_EXT_ID)
                {
                    rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_EXT_ENTRY_INVALID_ID,
                                    "Invalid header ID for extension entry #%#x: %#x", iExtEntry, pExtEntry->bExtensionId);
                    break;
                }
                *pcSkip += 1;

                memcpy(&pbSelCrit[cbSelCrit], pExtEntry->abSelectionCriteria, sizeof(pExtEntry->abSelectionCriteria));
                cbSelCrit += sizeof(pExtEntry->abSelectionCriteria);

                if (pExtEntry->fFlags & ISO9660_ELTORITO_SECTION_ENTRY_EXT_F_UNUSED_MASK)
                    rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_EXT_ENTRY_UNDEFINED_FLAGS,
                                    "Boot catalog extension entry #%#x uses undefined flags: %#x", iExtEntry, pExtEntry->fFlags);

                iExtEntry++;
                if (!(pExtEntry->fFlags & ISO9660_ELTORITO_SECTION_ENTRY_EXT_F_MORE))
                    break;
                if (iExtEntry >= cMaxEntries)
                {
                    rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_EXT_ENTRY_END_OF_SECTOR,
                                    "Boot catalog extension entry #%#x sets the MORE flag, but we have reached the end of the boot catalog sector");
                    break;
                }
            }
            Assert(*pcSkip = iExtEntry - iEntry);
        }
        else if (bMediaType & ISO9660_ELTORITO_BOOT_MEDIA_F_CONTINUATION)
            rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_ENTRY_CONTINUATION_EOS,
                            "Boot catalog extension entry #%#x sets the MORE flag, but we have reached the end of the boot catalog sector");
    }
    else if (bMediaType & ISO9660_ELTORITO_BOOT_MEDIA_F_CONTINUATION)
        rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_ENTRY_CONTINUATION_WITH_NONE,
                        "Boot catalog entry #%#x uses the continuation flag with selection criteria NONE", iEntry);

    /*
     * Add the entry.
     */
    rc = RTFsIsoMakerBootCatSetSectionEntry(pThis->hIsoMaker, iEntry, idxImageObj, bMediaType, pEntry->bSystemType,
                                            pEntry->bBootIndicator == ISO9660_ELTORITO_BOOT_INDICATOR_BOOTABLE,
                                            pEntry->uLoadSeg, pEntry->cEmulatedSectorsToLoad,
                                            pEntry->bSelectionCriteriaType, pbSelCrit, cbSelCrit);
    if (RT_SUCCESS(rc))
    {
        pThis->pResults->cBootCatEntries += 1 + *pcSkip;
        rc = rtFsIsoImportProcessElToritoImage(pThis, idxImageObj, offBootImage);
    }
    else
        rtFsIsoImpError(pThis, rc, "RTFsIsoMakerBootCatSetSectionEntry failed for entry #%#x: %Rrc", iEntry, rc);
    return rc;
}



/**
 * Processes a boot catalog section header entry.
 *
 * @returns IPRT status code (ignored).
 * @param   pThis       The ISO importer instance.
 * @param   iEntry      The boot catalog entry number.
 * @param   pEntry      The entry to process.
 */
static int rtFsIsoImportProcessElToritoSectionHeader(PRTFSISOMKIMPORTER pThis, uint32_t iEntry,
                                                     PCISO9660ELTORITOSECTIONHEADER pEntry, char pszId[32])
{
    Assert(pEntry->bHeaderId == ISO9660_ELTORITO_HEADER_ID_SECTION_HEADER);

    /* Deal with the string. ASSUME it doesn't contain zeros in non-terminal positions. */
    if (pEntry->achSectionId[0] == '\0')
        pszId = NULL;
    else
    {
        memcpy(pszId, pEntry->achSectionId, sizeof(pEntry->achSectionId));
        pszId[sizeof(pEntry->achSectionId)] = '\0';
    }

    int rc = RTFsIsoMakerBootCatSetSectionHeaderEntry(pThis->hIsoMaker, iEntry, RT_LE2H_U16(pEntry->cEntries),
                                                      pEntry->bPlatformId, pszId);
    if (RT_SUCCESS(rc))
        pThis->pResults->cBootCatEntries++;
    else
        rtFsIsoImpError(pThis, rc,
                        "RTFsIsoMakerBootCatSetSectionHeaderEntry failed for entry #%#x (bPlatformId=%#x cEntries=%#x): %Rrc",
                        iEntry, RT_LE2H_U16(pEntry->cEntries), pEntry->bPlatformId, rc);
    return rc;
}


/**
 * Processes a El Torito volume descriptor.
 *
 * @returns IPRT status code (ignorable).
 * @param   pThis       The ISO importer instance.
 * @param   pVolDesc    The volume descriptor to process.
 */
static int rtFsIsoImportProcessElToritoDesc(PRTFSISOMKIMPORTER pThis, PISO9660BOOTRECORDELTORITO pVolDesc)
{
    /*
     * Read the boot catalog into the abBuf.
     */
    uint32_t offBootCatalog = RT_LE2H_U32(pVolDesc->offBootCatalog);
    if (offBootCatalog >= pThis->cBlocksInPrimaryVolumeSpace)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_BAD_OUT_OF_BOUNDS,
                               "Boot catalog block number is out of bounds: %#RX32, max %#RX32",
                               offBootCatalog, pThis->cBlocksInPrimaryVolumeSpace);

    int rc = RTVfsFileReadAt(pThis->hSrcFile, offBootCatalog * (uint64_t)ISO9660_SECTOR_SIZE,
                             pThis->abBuf, ISO9660_SECTOR_SIZE, NULL);
    if (RT_FAILURE(rc))
        return rtFsIsoImpError(pThis, rc,  "Error reading boot catalog at block #%#RX32: %Rrc", offBootCatalog, rc);


    /*
     * Process the 'validation entry'.
     */
    PCISO9660ELTORITOVALIDATIONENTRY pValEntry = (PCISO9660ELTORITOVALIDATIONENTRY)&pThis->abBuf[0];
    if (pValEntry->bHeaderId != ISO9660_ELTORITO_HEADER_ID_VALIDATION_ENTRY)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_BAD_VALIDATION_HEADER_ID,
                               "Invalid boot catalog validation entry header ID: %#x, expected %#x",
                               pValEntry->bHeaderId, ISO9660_ELTORITO_HEADER_ID_VALIDATION_ENTRY);

    if (   pValEntry->bKey1     != ISO9660_ELTORITO_KEY_BYTE_1
        || pValEntry->bKey2     != ISO9660_ELTORITO_KEY_BYTE_2)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_BAD_VALIDATION_KEYS,
                               "Invalid boot catalog validation entry keys: %#x %#x, expected %#x %#x",
                               pValEntry->bKey1, pValEntry->bKey2, ISO9660_ELTORITO_KEY_BYTE_1, ISO9660_ELTORITO_KEY_BYTE_2);

    /* Check the checksum (should sum up to be zero). */
    uint16_t        uChecksum = 0;
    uint16_t const *pu16      = (uint16_t const *)pValEntry;
    size_t          cLeft     = sizeof(*pValEntry) / sizeof(uint16_t);
    while (cLeft-- > 0)
    {
        uChecksum += RT_LE2H_U16(*pu16);
        pu16++;
    }
    if (uChecksum != 0)
        return rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_BAD_VALIDATION_CHECKSUM,
                               "Invalid boot catalog validation entry checksum: %#x, expected 0", uChecksum);

    /* The string ID.  ASSUME no leading zeros in valid strings. */
    const char *pszId = NULL;
    char        szId[32];
    if (pValEntry->achId[0] != '\0')
    {
        memcpy(szId, pValEntry->achId, sizeof(pValEntry->achId));
        szId[sizeof(pValEntry->achId)] = '\0';
        pszId = szId;
    }

    /*
     * Before we tell the ISO maker about the validation entry, we need to sort
     * out the file backing the boot catalog.  This isn't fatal if it fails.
     */
    PRTFSISOMKIMPBLOCK2FILE pBlock2File = (PRTFSISOMKIMPBLOCK2FILE)RTAvlU32Get(&pThis->Block2FileRoot, offBootCatalog);
    if (pBlock2File)
    {
        rc = RTFsIsoMakerBootCatSetFile(pThis->hIsoMaker, pBlock2File->idxObj);
        if (RT_FAILURE(rc))
            rtFsIsoImpError(pThis, rc, "RTFsIsoMakerBootCatSetFile failed: %Rrc", rc);
    }

    /*
     * Set the validation entry.
     */
    rc = RTFsIsoMakerBootCatSetValidationEntry(pThis->hIsoMaker, pValEntry->bPlatformId, pszId);
    if (RT_FAILURE(rc))
        return rtFsIsoImpError(pThis, rc, "RTFsIsoMakerBootCatSetValidationEntry(,%#x,%s) failed: %Rrc",
                               pValEntry->bPlatformId, pszId);
    Assert(pThis->pResults->cBootCatEntries == UINT32_MAX);
    pThis->pResults->cBootCatEntries = 0;

    /*
     * Process the default entry and any subsequent entries.
     */
    bool           fSeenFinal  = false;
    uint32_t const cMaxEntries = ISO9660_SECTOR_SIZE / ISO9660_ELTORITO_ENTRY_SIZE;
    for (uint32_t iEntry = 1; iEntry < cMaxEntries; iEntry++)
    {
        uint8_t const *pbEntry  = &pThis->abBuf[iEntry * ISO9660_ELTORITO_ENTRY_SIZE];
        uint8_t const  idHeader = *pbEntry;

        /* KLUDGE ALERT! Older ISO images, like RHEL5-Server-20070208.0-x86_64-DVD.iso lacks
                         terminator entry. So, quietly stop with an entry that's all zeros. */
        if (   idHeader == ISO9660_ELTORITO_BOOT_INDICATOR_NOT_BOOTABLE /* 0x00 */
            && iEntry != 1 /* default */
            && ASMMemIsZero(pbEntry, ISO9660_ELTORITO_ENTRY_SIZE))
            return rc;

        if (   iEntry == 1 /* default*/
            || idHeader == ISO9660_ELTORITO_BOOT_INDICATOR_BOOTABLE
            || idHeader == ISO9660_ELTORITO_BOOT_INDICATOR_NOT_BOOTABLE)
        {
            uint32_t cSkip = 0;
            rtFsIsoImportProcessElToritoSectionEntry(pThis, iEntry, cMaxEntries, (PCISO9660ELTORITOSECTIONENTRY)pbEntry, &cSkip);
            iEntry += cSkip;
        }
        else if (idHeader == ISO9660_ELTORITO_HEADER_ID_SECTION_HEADER)
            rtFsIsoImportProcessElToritoSectionHeader(pThis, iEntry, (PCISO9660ELTORITOSECTIONHEADER)pbEntry, szId);
        else if (idHeader == ISO9660_ELTORITO_HEADER_ID_FINAL_SECTION_HEADER)
        {
            fSeenFinal = true;
            break;
        }
        else
            rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_UNKNOWN_HEADER_ID,
                            "Unknown boot catalog header ID for entry #%#x: %#x", iEntry, idHeader);
    }

    if (!fSeenFinal)
        rc = rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_BOOT_CAT_MISSING_FINAL_OR_TOO_BIG,
                             "Boot catalog is probably larger than a sector, or it's missing the final section header entry");
    return rc;
}


/**
 * Imports an existing ISO.
 *
 * Just like other source files, the existing image must remain present and
 * unmodified till the ISO maker is done with it.
 *
 * @returns IRPT status code.
 * @param   hIsoMaker   The ISO maker handle.
 * @param   hIsoFile    VFS file handle to the existing image to import / clone.
 * @param   fFlags      Reserved for the future, MBZ.
 * @param   poffError   Where to return the position in @a pszIso
 *                      causing trouble when opening it for reading.
 *                      Optional.
 * @param   pErrInfo    Where to return additional error information.
 *                      Optional.
 */
RTDECL(int) RTFsIsoMakerImport(RTFSISOMAKER hIsoMaker, RTVFSFILE hIsoFile, uint32_t fFlags,
                               PRTFSISOMAKERIMPORTRESULTS pResults, PRTERRINFO pErrInfo)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pResults, VERR_INVALID_POINTER);
    pResults->cAddedNames       = 0;
    pResults->cAddedDirs        = 0;
    pResults->cbAddedDataBlocks = 0;
    pResults->cAddedFiles       = 0;
    pResults->cAddedSymlinks    = 0;
    pResults->cBootCatEntries   = UINT32_MAX;
    pResults->cbSysArea         = 0;
    pResults->cErrors           = 0;
    AssertReturn(!(fFlags & ~RTFSISOMK_IMPORT_F_VALID_MASK), VERR_INVALID_FLAGS);

    /*
     * Get the file size.
     */
    uint64_t cbSrcFile = 0;
    int rc = RTVfsFileQuerySize(hIsoFile, &cbSrcFile);
    if (RT_SUCCESS(rc))
    {
        /*
         * Allocate and init the importer state.
         */
        PRTFSISOMKIMPORTER pThis = (PRTFSISOMKIMPORTER)RTMemAllocZ(sizeof(*pThis));
        if (pThis)
        {
            pThis->hIsoMaker        = hIsoMaker;
            pThis->fFlags           = fFlags;
            pThis->rc               = VINF_SUCCESS;
            pThis->pErrInfo         = pErrInfo;
            pThis->hSrcFile         = hIsoFile;
            pThis->cbSrcFile        = cbSrcFile;
            pThis->cBlocksInSrcFile = cbSrcFile / ISO9660_SECTOR_SIZE;
            pThis->idxSrcFile       = UINT32_MAX;
            //pThis->Block2FileRoot = NULL;
            //pThis->cBlocksInPrimaryVolumeSpace = 0;
            //pThis->cbPrimaryVolumeSpace = 0
            //pThis->cVolumesInSet  = 0;
            //pThis->idPrimaryVol   = 0;
            //pThis->fSeenJoliet    = false;
            pThis->pResults         = pResults;
            //pThis->fSuspSeenSP    = false;
            //pThis->offSuspSkip    = 0;
            pThis->offRockBuf       = UINT64_MAX;

            /*
             * Check if this looks like a plausible ISO by checking out the first volume descriptor.
             */
            rc = RTVfsFileReadAt(hIsoFile, _32K, &pThis->uSectorBuf.PrimVolDesc, sizeof(pThis->uSectorBuf.PrimVolDesc), NULL);
            if (RT_SUCCESS(rc))
            {
                if (   pThis->uSectorBuf.VolDescHdr.achStdId[0] == ISO9660VOLDESC_STD_ID_0
                    && pThis->uSectorBuf.VolDescHdr.achStdId[1] == ISO9660VOLDESC_STD_ID_1
                    && pThis->uSectorBuf.VolDescHdr.achStdId[2] == ISO9660VOLDESC_STD_ID_2
                    && pThis->uSectorBuf.VolDescHdr.achStdId[3] == ISO9660VOLDESC_STD_ID_3
                    && pThis->uSectorBuf.VolDescHdr.achStdId[4] == ISO9660VOLDESC_STD_ID_4
                    && (   pThis->uSectorBuf.VolDescHdr.bDescType == ISO9660VOLDESC_TYPE_PRIMARY
                        || pThis->uSectorBuf.VolDescHdr.bDescType == ISO9660VOLDESC_TYPE_BOOT_RECORD) )
                {
                    /*
                     * Process the volume descriptors using the sector buffer, starting
                     * with the one we've already got sitting there.  We postpone processing
                     * the el torito one till after the others, so we can name files and size
                     * referenced in it.
                     */
                    uint32_t cPrimaryVolDescs = 0;
                    uint32_t iElTorito        = UINT32_MAX;
                    uint32_t iVolDesc         = 0;
                    for (;;)
                    {
                        switch (pThis->uSectorBuf.VolDescHdr.bDescType)
                        {
                            case ISO9660VOLDESC_TYPE_PRIMARY:
                                cPrimaryVolDescs++;
                                if (cPrimaryVolDescs == 1)
                                {
                                    pThis->offPrimaryVolDesc = _32K / ISO9660_SECTOR_SIZE + iVolDesc;
                                    rtFsIsoImportProcessPrimaryDesc(pThis, &pThis->uSectorBuf.PrimVolDesc);
                                }
                                else
                                    rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_MULTIPLE_PRIMARY_VOL_DESCS,
                                                    "Only a single primary volume descriptor is currently supported");
                                break;

                            case ISO9660VOLDESC_TYPE_SUPPLEMENTARY:
                                if (cPrimaryVolDescs > 0)
                                    rtFsIsoImportProcessSupplementaryDesc(pThis, &pThis->uSectorBuf.SupVolDesc);
                                else
                                    rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_SUPPLEMENTARY_BEFORE_PRIMARY,
                                                    "Primary volume descriptor expected before any supplementary descriptors!");
                                break;

                            case ISO9660VOLDESC_TYPE_BOOT_RECORD:
                                if (strcmp(pThis->uSectorBuf.ElToritoDesc.achBootSystemId,
                                           ISO9660BOOTRECORDELTORITO_BOOT_SYSTEM_ID) == 0)
                                {
                                    if (iElTorito == UINT32_MAX)
                                        iElTorito = iVolDesc;
                                    else
                                        rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_MULTIPLE_EL_TORITO_DESCS,
                                                        "Only a single El Torito descriptor exepcted!");
                                }
                                break;

                            case ISO9660VOLDESC_TYPE_PARTITION:
                                /* ignore for now */
                                break;

                            case ISO9660VOLDESC_TYPE_TERMINATOR:
                                AssertFailed();
                                break;
                        }


                        /*
                         * Read the next volume descriptor and check the signature.
                         */
                        iVolDesc++;
                        if (iVolDesc >= 32)
                        {
                            rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_TOO_MANY_VOL_DESCS, "Parses at most 32 volume descriptors");
                            break;
                        }

                        rc = RTVfsFileReadAt(hIsoFile, _32K + iVolDesc * ISO9660_SECTOR_SIZE,
                                             &pThis->uSectorBuf, sizeof(pThis->uSectorBuf), NULL);
                        if (RT_FAILURE(rc))
                        {
                            rtFsIsoImpError(pThis, rc, "Error reading the volume descriptor #%u at %#RX32: %Rrc",
                                            iVolDesc, _32K + iVolDesc * ISO9660_SECTOR_SIZE, rc);
                            break;
                        }

                        if (   pThis->uSectorBuf.VolDescHdr.achStdId[0] != ISO9660VOLDESC_STD_ID_0
                            || pThis->uSectorBuf.VolDescHdr.achStdId[1] != ISO9660VOLDESC_STD_ID_1
                            || pThis->uSectorBuf.VolDescHdr.achStdId[2] != ISO9660VOLDESC_STD_ID_2
                            || pThis->uSectorBuf.VolDescHdr.achStdId[3] != ISO9660VOLDESC_STD_ID_3
                            || pThis->uSectorBuf.VolDescHdr.achStdId[4] != ISO9660VOLDESC_STD_ID_4)
                        {
                            rtFsIsoImpError(pThis, VERR_ISOMK_IMPORT_INVALID_VOL_DESC_HDR,
                                            "Invalid volume descriptor header #%u at %#RX32: %.*Rhxs",
                                            iVolDesc, _32K + iVolDesc * ISO9660_SECTOR_SIZE,
                                            (int)sizeof(pThis->uSectorBuf.VolDescHdr), &pThis->uSectorBuf.VolDescHdr);
                            break;
                        }
                        /** @todo UDF support. */
                        if (pThis->uSectorBuf.VolDescHdr.bDescType == ISO9660VOLDESC_TYPE_TERMINATOR)
                            break;
                    }

                    /*
                     * Process the system area.
                     */
                    if (RT_SUCCESS(pThis->rc) || pThis->idxSrcFile != UINT32_MAX)
                    {
                        rc = RTVfsFileReadAt(hIsoFile, 0, pThis->abBuf, _32K, NULL);
                        if (RT_SUCCESS(rc))
                        {
                            if (!ASMMemIsAllU8(pThis->abBuf, _32K, 0))
                            {
                                /* Drop zero sectors from the end. */
                                uint32_t cbSysArea = _32K;
                                while (   cbSysArea >= ISO9660_SECTOR_SIZE
                                       && ASMMemIsAllU8(&pThis->abBuf[cbSysArea - ISO9660_SECTOR_SIZE], ISO9660_SECTOR_SIZE, 0))
                                    cbSysArea -= ISO9660_SECTOR_SIZE;

                                /** @todo HFS */
                                pThis->pResults->cbSysArea = cbSysArea;
                                rc = RTFsIsoMakerSetSysAreaContent(hIsoMaker, pThis->abBuf, cbSysArea, 0);
                                if (RT_FAILURE(rc))
                                    rtFsIsoImpError(pThis, rc, "RTFsIsoMakerSetSysAreaContent failed: %Rrc", rc);
                            }
                        }
                        else
                            rtFsIsoImpError(pThis, rc, "Error reading the system area (0..32KB): %Rrc", rc);
                    }

                    /*
                     * Do the El Torito descriptor.
                     */
                    if (   iElTorito != UINT32_MAX
                        && !(pThis->fFlags & RTFSISOMK_IMPORT_F_NO_BOOT)
                        && (RT_SUCCESS(pThis->rc) || pThis->idxSrcFile != UINT32_MAX))
                    {
                        rc = RTVfsFileReadAt(hIsoFile, _32K + iElTorito * ISO9660_SECTOR_SIZE,
                                             &pThis->uSectorBuf, sizeof(pThis->uSectorBuf), NULL);
                        if (RT_SUCCESS(rc))
                            rtFsIsoImportProcessElToritoDesc(pThis, &pThis->uSectorBuf.ElToritoDesc);
                        else
                            rtFsIsoImpError(pThis, rc, "Error reading the El Torito volume descriptor at %#RX32: %Rrc",
                                            _32K + iElTorito * ISO9660_SECTOR_SIZE, rc);
                    }

                    /*
                     * Return the first error status.
                     */
                    rc = pThis->rc;
                }
                else
                    rc = RTErrInfoSetF(pErrInfo, VERR_ISOMK_IMPORT_UNKNOWN_FORMAT, "Invalid volume descriptor header: %.*Rhxs",
                                       (int)sizeof(pThis->uSectorBuf.VolDescHdr), &pThis->uSectorBuf.VolDescHdr);
            }

            /*
             * Destroy the state.
             */
            RTAvlU32Destroy(&pThis->Block2FileRoot, rtFsIsoMakerImportDestroyData2File, NULL);
            RTMemFree(pThis);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    return rc;
}

