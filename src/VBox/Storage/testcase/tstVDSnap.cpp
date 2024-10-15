/* $Id: tstVDSnap.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Snapshot VBox HDD container test utility.
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

#include <VBox/vd.h>
#include <iprt/errcore.h>
#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/dir.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/initterm.h>
#include <iprt/rand.h>

/**
 * A VD snapshot test.
 */
typedef struct VDSNAPTEST
{
    /** Backend to use */
    const char *pcszBackend;
    /** Base image name */
    const char *pcszBaseImage;
    /** Diff image ending */
    const char *pcszDiffSuff;
    /** Number of iterations before the test exits */
    uint32_t    cIterations;
    /** Test pattern size */
    size_t      cbTestPattern;
    /** Minimum number of disk segments */
    uint32_t    cDiskSegsMin;
    /** Miaximum number of disk segments */
    uint32_t    cDiskSegsMax;
    /** Minimum number of diffs needed before a merge
     * operation can occur */
    unsigned    cDiffsMinBeforeMerge;
    /** Chance to get create instead of a merge operation */
    uint32_t    uCreateDiffChance;
    /** Chance to change a segment after a diff was created */
    uint32_t    uChangeSegChance;
    /** Numer of allocated blocks in the base image in percent */
    uint32_t    uAllocatedBlocks;
    /** Merge direction */
    bool        fForward;
} VDSNAPTEST, *PVDSNAPTEST;

/**
 * Structure defining a disk segment.
 */
typedef struct VDDISKSEG
{
    /** Start offset in the disk. */
    uint64_t                   off;
    /** Size of the segment. */
    uint64_t                   cbSeg;
    /** Pointer to the start of the data in the test pattern used for the segment. */
    uint8_t                   *pbData;
    /** Pointer to the data for a diff write */
    uint8_t                   *pbDataDiff;
} VDDISKSEG, *PVDDISKSEG;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The error count. */
unsigned g_cErrors = 0;
/** Global RNG state. */
RTRAND   g_hRand;

static DECLCALLBACK(void) tstVDError(void *pvUser, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list va)
{
    RT_NOREF1(pvUser);
    g_cErrors++;
    RTPrintf("tstVDSnap: Error %Rrc at %s:%u (%s): ", rc, RT_SRC_POS_ARGS);
    RTPrintfV(pszFormat, va);
    RTPrintf("\n");
}

static DECLCALLBACK(int) tstVDMessage(void *pvUser, const char *pszFormat, va_list va)
{
    RT_NOREF1(pvUser);
    RTPrintf("tstVDSnap: ");
    RTPrintfV(pszFormat, va);
    return VINF_SUCCESS;
}

/**
 * Returns true with the given chance in percent.
 *
 * @returns true or false
 * @param   iPercentage   The percentage of the chance to return true.
 */
static bool tstVDSnapIsTrue(int iPercentage)
{
    int uRnd = RTRandAdvU32Ex(g_hRand, 0, 100);

    return (uRnd <= iPercentage); /* This should be enough for our purpose */
}

static void tstVDSnapSegmentsDice(PVDSNAPTEST pTest, PVDDISKSEG paDiskSeg, uint32_t cDiskSegments,
                                  uint8_t *pbTestPattern, size_t cbTestPattern)
{
    for (uint32_t i = 0; i < cDiskSegments; i++)
    {
        /* Do we want to change the current segment? */
        if (tstVDSnapIsTrue(pTest->uChangeSegChance))
            paDiskSeg[i].pbDataDiff = pbTestPattern + RT_ALIGN_64(RTRandAdvU64Ex(g_hRand, 0, cbTestPattern - paDiskSeg[i].cbSeg - 512), 512);
    }
}

static int tstVDSnapWrite(PVDISK pVD, PVDDISKSEG paDiskSegments, uint32_t cDiskSegments, uint64_t cbDisk, bool fInit)
{
    RT_NOREF1(cbDisk);
    int rc = VINF_SUCCESS;

    for (uint32_t i = 0; i < cDiskSegments; i++)
    {
        if (fInit || paDiskSegments[i].pbDataDiff)
        {
            size_t cbWrite  = paDiskSegments[i].cbSeg;
            uint64_t off    = paDiskSegments[i].off;
            uint8_t *pbData =   fInit
                              ? paDiskSegments[i].pbData
                              : paDiskSegments[i].pbDataDiff;

            if (pbData)
            {
                rc = VDWrite(pVD, off, pbData, cbWrite);
                if (RT_FAILURE(rc))
                    return rc;
            }
        }
    }

    return rc;
}

static int tstVDSnapReadVerify(PVDISK pVD, PVDDISKSEG paDiskSegments, uint32_t cDiskSegments, uint64_t cbDisk)
{
    RT_NOREF1(cbDisk);
    int rc = VINF_SUCCESS;
    uint8_t *pbBuf = (uint8_t *)RTMemAlloc(_1M);

    for (uint32_t i = 0; i < cDiskSegments; i++)
    {
        size_t cbRead  = paDiskSegments[i].cbSeg;
        uint64_t off   = paDiskSegments[i].off;
        uint8_t *pbCmp = paDiskSegments[i].pbData;

        Assert(!paDiskSegments[i].pbDataDiff);

        while (cbRead)
        {
            size_t cbToRead = RT_MIN(cbRead, _1M);

            rc = VDRead(pVD, off, pbBuf, cbToRead);
            if (RT_FAILURE(rc))
                return rc;

            if (pbCmp)
            {
                if (memcmp(pbCmp, pbBuf, cbToRead))
                {
                    for (unsigned iCmp = 0; iCmp < cbToRead; iCmp++)
                    {
                        if (pbCmp[iCmp] != pbBuf[iCmp])
                        {
                            RTPrintf("Unexpected data at %llu expected %#x got %#x\n", off+iCmp, pbCmp[iCmp], pbBuf[iCmp]);
                            break;
                        }
                    }
                    return VERR_INTERNAL_ERROR;
                }
            }
            else
            {
                /* Verify that the block is 0 */
                for (unsigned iCmp = 0; iCmp < cbToRead; iCmp++)
                {
                    if (pbBuf[iCmp] != 0)
                    {
                        RTPrintf("Zero block contains data at %llu\n", off+iCmp);
                        return VERR_INTERNAL_ERROR;
                    }
                }
            }

            cbRead -= cbToRead;
            off    += cbToRead;

            if (pbCmp)
                pbCmp  += cbToRead;
        }
    }

    RTMemFree(pbBuf);

    return rc;
}

static int tstVDOpenCreateWriteMerge(PVDSNAPTEST pTest)
{
    int rc;
    PVDISK pVD = NULL;
    VDGEOMETRY       PCHS = { 0, 0, 0 };
    VDGEOMETRY       LCHS = { 0, 0, 0 };
    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACEERROR VDIfError;

    /** Buffer storing the random test pattern. */
    uint8_t *pbTestPattern = NULL;
    /** Number of disk segments */
    uint32_t cDiskSegments;
    /** Array of disk segments */
    PVDDISKSEG paDiskSeg = NULL;
    unsigned   cDiffs = 0;
    unsigned   idDiff = 0; /* Diff ID counter for the filename */

    /* Delete all images from a previous run. */
    RTFileDelete(pTest->pcszBaseImage);
    for (unsigned i = 0; i < pTest->cIterations; i++)
    {
        char *pszDiffFilename = NULL;

        rc = RTStrAPrintf(&pszDiffFilename, "tstVDSnapDiff%u.%s", i, pTest->pcszDiffSuff);
        if (RT_SUCCESS(rc))
        {
            if (RTFileExists(pszDiffFilename))
                RTFileDelete(pszDiffFilename);
            RTStrFree(pszDiffFilename);
        }
    }

    /* Create the virtual disk test data */
    pbTestPattern = (uint8_t *)RTMemAlloc(pTest->cbTestPattern);
    if (!pbTestPattern)
    {
        RTPrintf("Failed to allocate memory for test pattern\n");
        g_cErrors++;
        return VERR_NO_MEMORY;
    }

    RTRandAdvBytes(g_hRand, pbTestPattern, pTest->cbTestPattern);
    cDiskSegments = RTRandAdvU32Ex(g_hRand, pTest->cDiskSegsMin, pTest->cDiskSegsMax);

    uint64_t cbDisk = 0;

    paDiskSeg = (PVDDISKSEG)RTMemAllocZ(cDiskSegments * sizeof(VDDISKSEG));
    if (!paDiskSeg)
    {
        RTPrintf("Failed to allocate memory for random disk segments\n");
        g_cErrors++;
        return VERR_NO_MEMORY;
    }

    for (unsigned i = 0; i < cDiskSegments; i++)
    {
        paDiskSeg[i].off    = cbDisk;
        paDiskSeg[i].cbSeg  = RT_ALIGN_64(RTRandAdvU64Ex(g_hRand, 512, pTest->cbTestPattern), 512);
        if (tstVDSnapIsTrue(pTest->uAllocatedBlocks))
            paDiskSeg[i].pbData = pbTestPattern + RT_ALIGN_64(RTRandAdvU64Ex(g_hRand, 0, pTest->cbTestPattern - paDiskSeg[i].cbSeg - 512), 512);
        else
            paDiskSeg[i].pbData = NULL; /* Not allocated initially */
        cbDisk += paDiskSeg[i].cbSeg;
    }

    RTPrintf("Disk size is %llu bytes\n", cbDisk);

#define CHECK(str) \
    do \
    { \
        RTPrintf("%s rc=%Rrc\n", str, rc); \
        if (RT_FAILURE(rc)) \
        { \
            if (pbTestPattern) \
                RTMemFree(pbTestPattern); \
            if (paDiskSeg) \
                RTMemFree(paDiskSeg); \
            VDDestroy(pVD); \
            g_cErrors++; \
            return rc; \
        } \
    } while (0)

#define CHECK_BREAK(str) \
    do \
    { \
        RTPrintf("%s rc=%Rrc\n", str, rc); \
        if (RT_FAILURE(rc)) \
        { \
            g_cErrors++; \
            break; \
        } \
    } while (0)

    /* Create error interface. */
    /* Create error interface. */
    VDIfError.pfnError = tstVDError;
    VDIfError.pfnMessage = tstVDMessage;

    rc = VDInterfaceAdd(&VDIfError.Core, "tstVD_Error", VDINTERFACETYPE_ERROR,
                        NULL, sizeof(VDINTERFACEERROR), &pVDIfs);
    AssertRC(rc);


    rc = VDCreate(pVDIfs, VDTYPE_HDD, &pVD);
    CHECK("VDCreate()");

    rc = VDCreateBase(pVD, pTest->pcszBackend, pTest->pcszBaseImage, cbDisk,
                      VD_IMAGE_FLAGS_NONE, "Test image",
                      &PCHS, &LCHS, NULL, VD_OPEN_FLAGS_NORMAL,
                      NULL, NULL);
    CHECK("VDCreateBase()");

    bool fInit = true;
    uint32_t cIteration = 0;

    /* Do the real work now */
    while (   RT_SUCCESS(rc)
           && cIteration < pTest->cIterations)
    {
        /* Write */
        rc = tstVDSnapWrite(pVD, paDiskSeg, cDiskSegments, cbDisk, fInit);
        CHECK_BREAK("tstVDSnapWrite()");

        fInit = false;

        /* Write returned, do we want to create a new diff or merge them? */
        bool fCreate =   cDiffs < pTest->cDiffsMinBeforeMerge
                       ? true
                       : tstVDSnapIsTrue(pTest->uCreateDiffChance);

        if (fCreate)
        {
            char *pszDiffFilename = NULL;

            RTStrAPrintf(&pszDiffFilename, "tstVDSnapDiff%u.%s", idDiff, pTest->pcszDiffSuff);
            CHECK("RTStrAPrintf()");
            idDiff++;
            cDiffs++;

            rc = VDCreateDiff(pVD, pTest->pcszBackend, pszDiffFilename,
                              VD_IMAGE_FLAGS_NONE, "Test diff image", NULL, NULL,
                              VD_OPEN_FLAGS_NORMAL, NULL, NULL);
            CHECK_BREAK("VDCreateDiff()");

            RTStrFree(pszDiffFilename);
            VDDumpImages(pVD);

            /* Change data */
            tstVDSnapSegmentsDice(pTest, paDiskSeg, cDiskSegments, pbTestPattern, pTest->cbTestPattern);
        }
        else
        {
            uint32_t uStartMerge = RTRandAdvU32Ex(g_hRand, 1, cDiffs - 1);
            uint32_t uEndMerge   = RTRandAdvU32Ex(g_hRand, uStartMerge + 1, cDiffs);
            RTPrintf("Merging %u diffs from %u to %u...\n",
                     uEndMerge - uStartMerge,
                     uStartMerge,
                     uEndMerge);
            if (pTest->fForward)
                rc = VDMerge(pVD, uStartMerge, uEndMerge, NULL);
            else
                rc = VDMerge(pVD, uEndMerge, uStartMerge, NULL);
            CHECK_BREAK("VDMerge()");

            cDiffs -= uEndMerge - uStartMerge;

            VDDumpImages(pVD);

            /* Go through the disk segments and reset pointers. */
            for (uint32_t i = 0; i < cDiskSegments; i++)
            {
                if (paDiskSeg[i].pbDataDiff)
                {
                    paDiskSeg[i].pbData     = paDiskSeg[i].pbDataDiff;
                    paDiskSeg[i].pbDataDiff = NULL;
                }
            }

            /* Now compare the result with our test pattern */
            rc = tstVDSnapReadVerify(pVD, paDiskSeg, cDiskSegments, cbDisk);
            CHECK_BREAK("tstVDSnapReadVerify()");
        }
        cIteration++;
    }

    VDDumpImages(pVD);

    VDDestroy(pVD);
    RTMemFree(paDiskSeg);
    RTMemFree(pbTestPattern);

    RTFileDelete(pTest->pcszBaseImage);
    for (unsigned i = 0; i < idDiff; i++)
    {
        char *pszDiffFilename = NULL;

        RTStrAPrintf(&pszDiffFilename, "tstVDSnapDiff%u.%s", i, pTest->pcszDiffSuff);
        RTFileDelete(pszDiffFilename);
        RTStrFree(pszDiffFilename);
    }
#undef CHECK
    return rc;
}

int main(int argc, char *argv[])
{
    RTR3InitExe(argc, &argv, 0);
    int rc;
    VDSNAPTEST Test;

    RTPrintf("tstVDSnap: TESTING...\n");

    rc = RTRandAdvCreateParkMiller(&g_hRand);
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstVDSnap: Creating RNG failed rc=%Rrc\n", rc);
        return 1;
    }

    RTRandAdvSeed(g_hRand, 0x12345678);

    Test.pcszBackend          = "vmdk";
    Test.pcszBaseImage        = "tstVDSnapBase.vmdk";
    Test.pcszDiffSuff         = "vmdk";
    Test.cIterations          = 30;
    Test.cbTestPattern        = 10 * _1M;
    Test.cDiskSegsMin         = 10;
    Test.cDiskSegsMax         = 50;
    Test.cDiffsMinBeforeMerge = 5;
    Test.uCreateDiffChance    = 50; /* % */
    Test.uChangeSegChance     = 50; /* % */
    Test.uAllocatedBlocks     = 50; /* 50% allocated */
    Test.fForward             = true;
    tstVDOpenCreateWriteMerge(&Test);

    /* Same test with backwards merge */
    Test.fForward             = false;
    tstVDOpenCreateWriteMerge(&Test);

    rc = VDShutdown();
    if (RT_FAILURE(rc))
    {
        RTPrintf("tstVDSnap: unloading backends failed! rc=%Rrc\n", rc);
        g_cErrors++;
    }
     /*
     * Summary
     */
    if (!g_cErrors)
        RTPrintf("tstVDSnap: SUCCESS\n");
    else
        RTPrintf("tstVDSnap: FAILURE - %d errors\n", g_cErrors);

    RTRandAdvDestroy(g_hRand);

    return !!g_cErrors;
}

