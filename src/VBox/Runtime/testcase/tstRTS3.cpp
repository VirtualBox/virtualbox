/* $Id: tstRTS3.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Simple Storage Service (S3) Communication API
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
#include <iprt/s3.h>
#include <iprt/stream.h>
#include <iprt/initterm.h>
#include <iprt/errcore.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/* Manual configuration of this testcase */
#define TSTS3_CREATEBUCKET
#define TSTS3_PUTGETKEY
//#define TSTS3_SHOWPROGRESS

#ifdef TSTS3_CREATEBUCKET
//# define TSTS3_CREATEBUCKET_BUCKETNAME "tstS3CreateBucket"
# define TSTS3_CREATEBUCKET_BUCKETNAME "tt9"
#endif /* TSTS3_CREATEBUCKET */

#ifdef TSTS3_PUTGETKEY
# define TSTS3_PUTGETKEY_BUCKETNAME "tstS3PutGetBucket"
# define TSTS3_PUTGETKEY_KEYNAME "tstS3PutGetKey"
# define TSTS3_PUTGETKEY_PUTFILE "tstS3"
# define TSTS3_PUTGETKEY_GETFILE "tstS3_fetched"
#endif /* TSTS3_PUTGETKEY */

static DECLCALLBACK(int) progress(unsigned uPercent, void *pvUser)
{
#ifdef TSTS3_SHOWPROGRESS
    RTTestIPrintf(RTTESTLVL_ALWAYS, " Progress for %s - %d%% done.\n", (char*)pvUser, (int)uPercent);
#else
    RT_NOREF2(uPercent, pvUser);
#endif
    return VINF_SUCCESS;
}

void fetchAllBuckets(RTS3 hS3)
{
    /* Fetch all available buckets */
    RTTestIPrintf(RTTESTLVL_ALWAYS, " List all buckets...\n");
    char pszTitle[] = "RTS3GetBuckets";
    RTS3SetProgressCallback(hS3, progress, pszTitle);
    PCRTS3BUCKETENTRY pBuckets = NULL;
    int rc = RTS3GetBuckets(hS3, &pBuckets);
    if (RT_SUCCESS(rc))
    {
        if (pBuckets)
        {
            PCRTS3BUCKETENTRY pTmpBuckets = pBuckets;
            while (pBuckets)
            {
                RTTestIPrintf(RTTESTLVL_ALWAYS, "  > %s, %s\n", pBuckets->pszName, pBuckets->pszCreationDate);
                pBuckets = pBuckets->pNext;
            }
            RTS3BucketsDestroy(pTmpBuckets);
        }
        else
            RTTestIPrintf(RTTESTLVL_ALWAYS, "  > empty\n");
    }
    else
        RTTestIFailed("RTS3GetBuckets -> %Rrc", rc);
}

void createBucket(RTS3 hS3, const char *pszBucketName)
{
    /* Create the bucket */
    RTTestIPrintf(RTTESTLVL_ALWAYS, " Create bucket '%s'...\n", pszBucketName);
    char pszTitle[] = "RTS3CreateBucket";
    RTS3SetProgressCallback(hS3, progress, pszTitle);
    int rc = RTS3CreateBucket(hS3, pszBucketName);
    if (RT_FAILURE(rc))
        RTTestIFailed("RTS3CreateBucket  -> %Rrc", rc);
}

void deleteBucket(RTS3 hS3, const char *pszBucketName)
{
    /* Delete the bucket */
    RTTestIPrintf(RTTESTLVL_ALWAYS, " Delete bucket '%s'...\n", pszBucketName);
    char pszTitle[] = "RTS3DeleteBucket";
    RTS3SetProgressCallback(hS3, progress, pszTitle);
    int rc = RTS3DeleteBucket(hS3, pszBucketName);
    if (RT_FAILURE(rc))
        RTTestIFailed("RTS3DeleteBucket -> %Rrc", rc);
}

void fetchAllKeys(RTS3 hS3, const char *pszBucketName)
{
    /* Fetch all available keys of a specific bucket */
    RTTestIPrintf(RTTESTLVL_ALWAYS, " List all keys of bucket '%s'...\n", pszBucketName);
    PCRTS3KEYENTRY pKeys = NULL;
    char pszTitle[] = "RTS3GetBucketKeys";
    RTS3SetProgressCallback(hS3, progress, pszTitle);
    int rc = RTS3GetBucketKeys(hS3, pszBucketName, &pKeys);
    if (RT_SUCCESS(rc))
    {
        if (pKeys)
        {
            PCRTS3KEYENTRY pTmpKeys = pKeys;
            while (pKeys)
            {
                RTTestIPrintf(RTTESTLVL_ALWAYS, "  > %s, %s, %lu\n", pKeys->pszName, pKeys->pszLastModified, pKeys->cbFile);
                pKeys = pKeys->pNext;
            }
            RTS3KeysDestroy(pTmpKeys);
        }
        else
            RTTestIPrintf(RTTESTLVL_ALWAYS, "  > empty\n");
    }
    else
        RTTestIFailed("RTS3GetBucketKeys -> %Rrc", rc);
}

void deleteKey(RTS3 hS3, const char *pszBucketName, const char *pszKeyName)
{
    /* Delete the key */
    RTTestIPrintf(RTTESTLVL_ALWAYS, " Delete key '%s' in bucket '%s'...\n", pszKeyName, pszBucketName);
    char pszTitle[] = "RTS3DeleteKey";
    RTS3SetProgressCallback(hS3, progress, pszTitle);
    int rc = RTS3DeleteKey(hS3, pszBucketName, pszKeyName);
    if (RT_FAILURE(rc))
        RTTestIFailed("RTS3DeleteKey -> %Rrc", rc);
}

void getKey(RTS3 hS3, const char *pszBucketName, const char *pszKeyName, const char *pszFilename)
{
    /* Fetch the content of a key */
    RTTestIPrintf(RTTESTLVL_ALWAYS, " Get key '%s' from bucket '%s' into '%s' ...\n", pszKeyName, pszBucketName, pszFilename);
    char pszTitle[] = "RTS3GetKey";
    RTS3SetProgressCallback(hS3, progress, pszTitle);
    int rc = RTS3GetKey(hS3, pszBucketName, pszKeyName, pszFilename);
    if (RT_FAILURE(rc))
        RTTestIFailed("RTS3GetKey -> %Rrc", rc);
}

void putKey(RTS3 hS3, const char *pszBucketName, const char *pszKeyName, const char *pszFilename)
{
    /* Fetch the content of a key */
    RTTestIPrintf(RTTESTLVL_ALWAYS, " Put '%s' into key '%s' in bucket '%s' ...\n", pszFilename, pszKeyName, pszBucketName);
    char pszTitle[] = "RTS3PutKey";
    RTS3SetProgressCallback(hS3, progress, pszTitle);
    int rc = RTS3PutKey(hS3, pszBucketName, pszKeyName, pszFilename);
    if (RT_FAILURE(rc))
        RTTestIFailed("RTS3PutKey -> %Rrc", rc);
}

int main(int argc, char **argv)
{
    /*
     * Initialize IPRT and create the test.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTS3", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * If no args, display usage.
     */
    if (argc <= 2)
    {
        RTTestPrintf(hTest, RTTESTLVL_ALWAYS, "Syntax: %s [Access Key] [Secret Key]\n", argv[0]);
        return RTTestSkipAndDestroy(hTest, "Missing required arguments\n");
    }

    RTTestSubF(hTest, "Create S3");
    RTS3 hS3;
    rc = RTS3Create(&hS3, argv[1], argv[2], "object.storage.network.com", "tstS3-agent/1.0");
    if (RT_FAILURE(rc))
    {
        RTTestIFailed("RTS3Create -> %Rrc", rc);
        return RTTestSummaryAndDestroy(hTest);
    }

    RTTestSub(hTest, "Fetch buckets");
    fetchAllBuckets(hS3);
    RTTestSub(hTest, "Fetch keys");
    fetchAllKeys(hS3, "bla");

#ifdef TSTS3_CREATEBUCKET
    RTTestSub(hTest, "Create bucket");
    createBucket(hS3, TSTS3_CREATEBUCKET_BUCKETNAME);
    fetchAllBuckets(hS3);
    deleteBucket(hS3, TSTS3_CREATEBUCKET_BUCKETNAME);
    fetchAllBuckets(hS3);
#endif /* TSTS3_CREATEBUCKET */


#ifdef TSTS3_PUTGETKEY
    RTTestSub(hTest, "Put key");
    createBucket(hS3, TSTS3_PUTGETKEY_BUCKETNAME);
    putKey(hS3, TSTS3_PUTGETKEY_BUCKETNAME, TSTS3_PUTGETKEY_KEYNAME, TSTS3_PUTGETKEY_PUTFILE);
    fetchAllKeys(hS3, TSTS3_PUTGETKEY_BUCKETNAME);
    getKey(hS3, TSTS3_PUTGETKEY_BUCKETNAME, TSTS3_PUTGETKEY_KEYNAME, TSTS3_PUTGETKEY_GETFILE);
    deleteKey(hS3, TSTS3_PUTGETKEY_BUCKETNAME, TSTS3_PUTGETKEY_KEYNAME);
    fetchAllKeys(hS3, TSTS3_PUTGETKEY_BUCKETNAME);
    deleteBucket(hS3, TSTS3_PUTGETKEY_BUCKETNAME);
#endif /* TSTS3_PUTGETKEY */

    RTS3Destroy(hS3);

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}

