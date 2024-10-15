/* $Id: tstSharedFolderService.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Testcase for the shared folder service vbsf API.
 *
 * Note that this is still very threadbare (there is an awful lot which should
 * really be tested, but it already took too long to produce this much).  The
 * idea is that anyone who makes changes to the shared folders service and who
 * cares about unit testing them should add tests to the skeleton framework to
 * exercise the bits they change before and after changing them.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#include "tstSharedFolderService.h"
#include "vbsf.h"

#include <iprt/fs.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/symlink.h>
#include <iprt/stream.h>
#include <iprt/test.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#include "teststubs.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest = NIL_RTTEST;


/*********************************************************************************************************************************
*   Declarations                                                                                                                 *
*********************************************************************************************************************************/
extern "C" DECLCALLBACK(DECLEXPORT(int)) VBoxHGCMSvcLoad (VBOXHGCMSVCFNTABLE *ptable);


/*********************************************************************************************************************************
*   Helpers                                                                                                                      *
*********************************************************************************************************************************/

/** Simple call handle structure for the guest call completion callback */
struct VBOXHGCMCALLHANDLE_TYPEDEF
{
    /** Where to store the result code */
    int32_t rc;
};

/** Call completion callback for guest calls. */
static DECLCALLBACK(int) callComplete(VBOXHGCMCALLHANDLE callHandle, int32_t rc)
{
    callHandle->rc = rc;
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) stamRegisterV(void *pvInstance, void *pvSample, STAMTYPE enmType, STAMVISIBILITY enmVisibility,
                                       STAMUNIT enmUnit, const char *pszDesc, const char *pszName, va_list va)
{
    RT_NOREF(pvInstance, pvSample, enmType, enmVisibility, enmUnit, pszDesc, pszName, va);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) stamDeregisterV(void *pvInstance, const char *pszPatFmt, va_list va)
{
    RT_NOREF(pvInstance, pszPatFmt, va);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) infoRegister(void *pvInstance, const char *pszName, const char *pszDesc,
                                      PFNDBGFHANDLEREXT pfnHandler, void *pvUser)
{
    RT_NOREF(pvInstance, pszName, pszDesc, pfnHandler, pvUser);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) infoDeregister(void *pvInstance, const char *pszName)
{
    RT_NOREF(pvInstance, pszName);
    return VINF_SUCCESS;
}

/**
 * Initialise the HGCM service table as much as we need to start the
 * service
 * @param  pTable the table to initialise
 */
static void initTable(VBOXHGCMSVCFNTABLE *pTable, VBOXHGCMSVCHELPERS *pHelpers)
{
    pTable->cbSize               = sizeof (VBOXHGCMSVCFNTABLE);
    pTable->u32Version           = VBOX_HGCM_SVC_VERSION;
    pHelpers->pfnCallComplete    = callComplete;
    pHelpers->pfnStamRegisterV   = stamRegisterV;
    pHelpers->pfnStamDeregisterV = stamDeregisterV;
    pHelpers->pfnInfoRegister    = infoRegister;
    pHelpers->pfnInfoDeregister  = infoDeregister;
    pTable->pHelpers             = pHelpers;
}

#define LLUIFY(a) ((unsigned long long)(a))

static void bufferFromPath(char *pszDst, size_t cbDst, const char *pcszSrc)
{
    RTStrCopy(pszDst, cbDst, pcszSrc);
    uintptr_t const uDstEnd = (uintptr_t)&pszDst[cbDst];
    for (char *psz = pszDst; psz && (uintptr_t)psz < uDstEnd; ++psz)
        if (*psz == '\\')
            *psz = '/';
}

#define ARRAY_FROM_PATH(a, b) \
    do { \
        char *p = (a); NOREF(p); \
        Assert((a) == p); /* Constant parameter */ \
        Assert(sizeof((a)) > 0); \
        bufferFromPath(a, sizeof(a), b); \
    } while (0)


/*********************************************************************************************************************************
*   Stub functions and data                                                                                                      *
*********************************************************************************************************************************/
static bool g_fFailIfNotLowercase = false;

static RTDIR g_testRTDirClose_hDir = NIL_RTDIR;

extern int testRTDirClose(RTDIR hDir)
{
 /* RTPrintf("%s: hDir=%p\n", __PRETTY_FUNCTION__, hDir); */
    g_testRTDirClose_hDir = hDir;
    return VINF_SUCCESS;
}

static char g_testRTDirCreate_szPath[256];
//static RTFMODE testRTDirCreateMode; - unused

extern int testRTDirCreate(const char *pszPath, RTFMODE fMode, uint32_t fCreate)
{
    RT_NOREF2(fMode, fCreate);
 /* RTPrintf("%s: pszPath=%s, fMode=0x%llx\n", __PRETTY_FUNCTION__, pszPath,
             LLUIFY(fMode)); */
    if (g_fFailIfNotLowercase && !RTStrIsLowerCased(strpbrk(pszPath, "/\\")))
        return VERR_FILE_NOT_FOUND;
    ARRAY_FROM_PATH(g_testRTDirCreate_szPath, pszPath);
    return 0;
}

static char g_testRTDirOpen_szName[256];
static struct TESTDIRHANDLE
{
    int iEntry;
    int iDir;
} g_aTestDirHandles[4];
static int g_iNextDirHandle = 0;
static RTDIR g_testRTDirOpen_hDir;

extern int testRTDirOpen(RTDIR *phDir, const char *pszPath)
{
 /* RTPrintf("%s: pszPath=%s\n", __PRETTY_FUNCTION__, pszPath); */
    if (g_fFailIfNotLowercase && !RTStrIsLowerCased(strpbrk(pszPath, "/\\")))
        return VERR_FILE_NOT_FOUND;
    ARRAY_FROM_PATH(g_testRTDirOpen_szName, pszPath);
    *phDir = g_testRTDirOpen_hDir;
    g_testRTDirOpen_hDir = NIL_RTDIR;
    if (!*phDir && g_fFailIfNotLowercase)
        *phDir = (RTDIR)&g_aTestDirHandles[g_iNextDirHandle++ % RT_ELEMENTS(g_aTestDirHandles)];
    if (*phDir)
    {
        struct TESTDIRHANDLE *pRealDir = (struct TESTDIRHANDLE *)*phDir;
        pRealDir->iEntry = 0;
        pRealDir->iDir   = 0;
        const char *pszSlash = pszPath - 1;
        while ((pszSlash = strpbrk(pszSlash + 1, "\\/")) != NULL)
            pRealDir->iDir += 1;
        /*RTPrintf("opendir %s = %d \n", pszPath, pRealDir->iDir);*/
    }
    return VINF_SUCCESS;
}

/** @todo Do something useful with the last two arguments. */
extern int testRTDirOpenFiltered(RTDIR *phDir, const char *pszPath, RTDIRFILTER, uint32_t)
{
 /* RTPrintf("%s: pszPath=%s\n", __PRETTY_FUNCTION__, pszPath); */
    if (g_fFailIfNotLowercase && !RTStrIsLowerCased(strpbrk(pszPath, "/\\")))
        return VERR_FILE_NOT_FOUND;
    ARRAY_FROM_PATH(g_testRTDirOpen_szName, pszPath);
    *phDir = g_testRTDirOpen_hDir;
    g_testRTDirOpen_hDir = NIL_RTDIR;
    if (!*phDir && g_fFailIfNotLowercase)
        *phDir = (RTDIR)&g_aTestDirHandles[g_iNextDirHandle++ % RT_ELEMENTS(g_aTestDirHandles)];
    if (*phDir)
    {
        struct TESTDIRHANDLE *pRealDir = (struct TESTDIRHANDLE *)*phDir;
        pRealDir->iEntry = 0;
        pRealDir->iDir   = 0;
        const char *pszSlash = pszPath - 1;
        while ((pszSlash = strpbrk(pszSlash + 1, "\\/")) != NULL)
            pRealDir->iDir += 1;
        pRealDir->iDir -= 1;
        /*RTPrintf("openfiltered %s = %d\n", pszPath, pRealDir->iDir);*/
    }
    return VINF_SUCCESS;
}

static RTDIR g_testRTDirQueryInfo_hDir;
static RTTIMESPEC g_testRTDirQueryInfo_ATime;

extern int testRTDirQueryInfo(RTDIR hDir, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs)
{
    RT_NOREF1(enmAdditionalAttribs);
 /* RTPrintf("%s: hDir=%p, enmAdditionalAttribs=0x%llx\n", __PRETTY_FUNCTION__,
             hDir, LLUIFY(enmAdditionalAttribs)); */
    g_testRTDirQueryInfo_hDir = hDir;
    RT_ZERO(*pObjInfo);
    pObjInfo->AccessTime = g_testRTDirQueryInfo_ATime;
    RT_ZERO(g_testRTDirQueryInfo_ATime);
    return VINF_SUCCESS;
}

extern int testRTDirRemove(const char *pszPath)
{
    if (g_fFailIfNotLowercase && !RTStrIsLowerCased(strpbrk(pszPath, "/\\")))
        return VERR_FILE_NOT_FOUND;
    RTPrintf("%s\n", __PRETTY_FUNCTION__);
    return 0;
}

static RTDIR g_testRTDirReadEx_hDir;

extern int testRTDirReadEx(RTDIR hDir, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry,
                           RTFSOBJATTRADD enmAdditionalAttribs, uint32_t fFlags)
{
    RT_NOREF4(pDirEntry, pcbDirEntry, enmAdditionalAttribs, fFlags);
 /* RTPrintf("%s: hDir=%p, pcbDirEntry=%d, enmAdditionalAttribs=%llu, fFlags=0x%llx\n",
             __PRETTY_FUNCTION__, hDir, pcbDirEntry ? (int) *pcbDirEntry : -1,
             LLUIFY(enmAdditionalAttribs), LLUIFY(fFlags)); */
    g_testRTDirReadEx_hDir = hDir;
    if (g_fFailIfNotLowercase && hDir != NIL_RTDIR)
    {
        struct TESTDIRHANDLE *pRealDir = (struct TESTDIRHANDLE *)hDir;
        if (pRealDir->iDir == 2) /* /test/mapping/ */
        {
            if (pRealDir->iEntry == 0)
            {
                pRealDir->iEntry++;
                RT_ZERO(*pDirEntry);
                pDirEntry->Info.Attr.fMode = RTFS_TYPE_DIRECTORY | RTFS_DOS_DIRECTORY | RTFS_UNIX_IROTH | RTFS_UNIX_IXOTH;
                pDirEntry->cbName = 4;
                pDirEntry->cwcShortName = 4;
                strcpy(pDirEntry->szName, "test");
                RTUtf16CopyAscii(pDirEntry->wszShortName, RT_ELEMENTS(pDirEntry->wszShortName), "test");
                /*RTPrintf("readdir: 'test'\n");*/
                return VINF_SUCCESS;
            }
        }
        else if (pRealDir->iDir == 3) /* /test/mapping/test/ */
        {
            if (pRealDir->iEntry == 0)
            {
                pRealDir->iEntry++;
                RT_ZERO(*pDirEntry);
                pDirEntry->Info.Attr.fMode = RTFS_TYPE_FILE | RTFS_DOS_NT_NORMAL | RTFS_UNIX_IROTH | RTFS_UNIX_IXOTH;
                pDirEntry->cbName = 4;
                pDirEntry->cwcShortName = 4;
                strcpy(pDirEntry->szName, "file");
                RTUtf16CopyAscii(pDirEntry->wszShortName, RT_ELEMENTS(pDirEntry->wszShortName), "file");
                /*RTPrintf("readdir: 'file'\n");*/
                return VINF_SUCCESS;
            }
        }
        /*else RTPrintf("%s: iDir=%d\n", pRealDir->iDir);*/
    }
    return VERR_NO_MORE_FILES;
}

static uint64_t g_testRTDirSetMode_fMode;

extern int testRTDirSetMode(RTDIR hDir, RTFMODE fMode)
{
    RT_NOREF1(hDir);
 /* RTPrintf("%s: fMode=%llu\n", __PRETTY_FUNCTION__, LLUIFY(fMode)); */
    g_testRTDirSetMode_fMode = fMode;
    return VINF_SUCCESS;
}

static RTTIMESPEC g_testRTDirSetTimes_ATime;

extern int testRTDirSetTimes(RTDIR hDir, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                             PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    RT_NOREF4(hDir, pModificationTime, pChangeTime, pBirthTime);
 /* RTPrintf("%s: hDir=%p, *pAccessTime=%lli, *pModificationTime=%lli, *pChangeTime=%lli, *pBirthTime=%lli\n",
             __PRETTY_FUNCTION__, hDir,
             pAccessTime ? (long long)RTTimeSpecGetNano(pAccessTime) : -1,
               pModificationTime
             ? (long long)RTTimeSpecGetNano(pModificationTime) : -1,
             pChangeTime ? (long long)RTTimeSpecGetNano(pChangeTime) : -1,
             pBirthTime ? (long long)RTTimeSpecGetNano(pBirthTime) : -1); */
    if (pAccessTime)
        g_testRTDirSetTimes_ATime = *pAccessTime;
    else
        RT_ZERO(g_testRTDirSetTimes_ATime);
    return VINF_SUCCESS;
}

static RTFILE g_testRTFileClose_hFile;

extern int  testRTFileClose(RTFILE File)
{
 /* RTPrintf("%s: File=%p\n", __PRETTY_FUNCTION__, File); */
    g_testRTFileClose_hFile = File;
    return 0;
}

extern int  testRTFileDelete(const char *pszFilename)
{
    if (g_fFailIfNotLowercase && !RTStrIsLowerCased(strpbrk(pszFilename, "/\\")))
        return VERR_FILE_NOT_FOUND;
    RTPrintf("%s\n", __PRETTY_FUNCTION__);
    return 0;
}

static RTFILE g_testRTFileFlush_hFile;

extern int  testRTFileFlush(RTFILE File)
{
 /* RTPrintf("%s: File=%p\n", __PRETTY_FUNCTION__, File); */
    g_testRTFileFlush_hFile = File;
    return VINF_SUCCESS;
}

static RTFILE g_testRTFileLock_hFile;
static unsigned g_testRTFileLock_fLock;
static int64_t g_testRTFileLock_offLock;
static uint64_t g_testRTFileLock_cbLock;

extern int  testRTFileLock(RTFILE hFile, unsigned fLock, int64_t offLock, uint64_t cbLock)
{
 /* RTPrintf("%s: hFile=%p, fLock=%u, offLock=%lli, cbLock=%llu\n",
             __PRETTY_FUNCTION__, hFile, fLock, (long long) offLock,
             LLUIFY(cbLock)); */
    g_testRTFileLock_hFile = hFile;
    g_testRTFileLock_fLock = fLock;
    g_testRTFileLock_offLock = offLock;
    g_testRTFileLock_cbLock = cbLock;
    return VINF_SUCCESS;
}

static char g_testRTFileOpen_szName[256];
static uint64_t g_testRTFileOpen_fOpen;
static RTFILE g_testRTFileOpen_hFile;

extern int  testRTFileOpenEx(const char *pszFilename, uint64_t fOpen, PRTFILE phFile, PRTFILEACTION penmActionTaken)
{
 /* RTPrintf("%s, pszFilename=%s, fOpen=0x%llx\n", __PRETTY_FUNCTION__,
             pszFilename, LLUIFY(fOpen)); */
    ARRAY_FROM_PATH(g_testRTFileOpen_szName, pszFilename);
    g_testRTFileOpen_fOpen = fOpen;
    if (g_fFailIfNotLowercase && !RTStrIsLowerCased(strpbrk(pszFilename, "/\\")))
        return VERR_FILE_NOT_FOUND;
    *phFile = g_testRTFileOpen_hFile;
    *penmActionTaken = RTFILEACTION_CREATED;
    g_testRTFileOpen_hFile = 0;
    return VINF_SUCCESS;
}

static RTFILE g_testRTFileQueryInfo_hFile;
static RTTIMESPEC g_testRTFileQueryInfo_ATime;
static uint32_t g_testRTFileQueryInfo_fMode;

extern int  testRTFileQueryInfo(RTFILE hFile, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs)
{
    RT_NOREF1(enmAdditionalAttribs);
 /* RTPrintf("%s, hFile=%p, enmAdditionalAttribs=0x%llx\n",
             __PRETTY_FUNCTION__, hFile, LLUIFY(enmAdditionalAttribs)); */
    g_testRTFileQueryInfo_hFile = hFile;
    RT_ZERO(*pObjInfo);
    pObjInfo->AccessTime = g_testRTFileQueryInfo_ATime;
    RT_ZERO(g_testRTDirQueryInfo_ATime);
    pObjInfo->Attr.fMode = g_testRTFileQueryInfo_fMode;
    g_testRTFileQueryInfo_fMode = 0;
    return VINF_SUCCESS;
}

static const char *g_testRTFileRead_pszData;

extern int  testRTFileRead(RTFILE File, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    RT_NOREF1(File);
 /* RTPrintf("%s : File=%p, cbToRead=%llu\n", __PRETTY_FUNCTION__, File,
             LLUIFY(cbToRead)); */
    bufferFromPath((char *)pvBuf, cbToRead, g_testRTFileRead_pszData);
    if (pcbRead)
    {
        size_t cchData = strlen(g_testRTFileRead_pszData) + 1;
        *pcbRead = RT_MIN(cbToRead, cchData);
    }
    g_testRTFileRead_pszData = 0;
    return VINF_SUCCESS;
}

extern int  testRTFileReadAt(RTFILE hFile, uint64_t offFile, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    RT_NOREF1(hFile);
    RT_NOREF(offFile);
 /* RTPrintf("%s : File=%p, cbToRead=%llu\n", __PRETTY_FUNCTION__, File,
             LLUIFY(cbToRead)); */
    bufferFromPath((char *)pvBuf, cbToRead, g_testRTFileRead_pszData);
    if (pcbRead)
    {
        size_t cchData = strlen(g_testRTFileRead_pszData) + 1;
        *pcbRead = RT_MIN(cbToRead, cchData);
    }
    g_testRTFileRead_pszData = 0;
    return VINF_SUCCESS;
}

extern int testRTFileSeek(RTFILE hFile, int64_t offSeek, unsigned uMethod, uint64_t *poffActual)
{
    RT_NOREF3(hFile, offSeek, uMethod);
 /* RTPrintf("%s : hFile=%p, offSeek=%llu, uMethod=%u\n", __PRETTY_FUNCTION__,
             hFile, LLUIFY(offSeek), uMethod); */
    if (poffActual)
        *poffActual = 0;
    return VINF_SUCCESS;
}

static uint64_t g_testRTFileSet_fMode;

extern int testRTFileSetMode(RTFILE File, RTFMODE fMode)
{
    RT_NOREF1(File);
 /* RTPrintf("%s: fMode=%llu\n", __PRETTY_FUNCTION__, LLUIFY(fMode)); */
    g_testRTFileSet_fMode = fMode;
    return VINF_SUCCESS;
}

static RTFILE g_testRTFileSetSize_hFile;
static RTFOFF g_testRTFileSetSize_cbSize;

extern int  testRTFileSetSize(RTFILE File, uint64_t cbSize)
{
 /* RTPrintf("%s: File=%llu, cbSize=%llu\n", __PRETTY_FUNCTION__, LLUIFY(File),
             LLUIFY(cbSize)); */
    g_testRTFileSetSize_hFile = File;
    g_testRTFileSetSize_cbSize = (RTFOFF) cbSize; /* Why was this signed before? */
    return VINF_SUCCESS;
}

static RTTIMESPEC g_testRTFileSetTimes_ATime;

extern int testRTFileSetTimes(RTFILE File, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                              PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    RT_NOREF4(File, pModificationTime, pChangeTime, pBirthTime);
 /* RTPrintf("%s: pFile=%p, *pAccessTime=%lli, *pModificationTime=%lli, *pChangeTime=%lli, *pBirthTime=%lli\n",
             __PRETTY_FUNCTION__,
             pAccessTime ? (long long)RTTimeSpecGetNano(pAccessTime) : -1,
               pModificationTime
             ? (long long)RTTimeSpecGetNano(pModificationTime) : -1,
             pChangeTime ? (long long)RTTimeSpecGetNano(pChangeTime) : -1,
             pBirthTime ? (long long)RTTimeSpecGetNano(pBirthTime) : -1); */
    if (pAccessTime)
        g_testRTFileSetTimes_ATime = *pAccessTime;
    else
        RT_ZERO(g_testRTFileSetTimes_ATime);
    return VINF_SUCCESS;
}

static RTFILE g_testRTFileUnlock_hFile;
static int64_t g_testRTFileUnlock_offLock;
static uint64_t g_testRTFileUnlock_cbLock;

extern int  testRTFileUnlock(RTFILE File, int64_t offLock, uint64_t cbLock)
{
 /* RTPrintf("%s: hFile=%p, ofLock=%lli, cbLock=%llu\n", __PRETTY_FUNCTION__,
             File, (long long) offLock, LLUIFY(cbLock)); */
    g_testRTFileUnlock_hFile = File;
    g_testRTFileUnlock_offLock = offLock;
    g_testRTFileUnlock_cbLock = cbLock;
    return VINF_SUCCESS;
}

static char g_testRTFileWrite_szData[256];

extern int  testRTFileWrite(RTFILE File, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    RT_NOREF2(File, cbToWrite);
 /* RTPrintf("%s: File=%p, pvBuf=%.*s, cbToWrite=%llu\n", __PRETTY_FUNCTION__,
             File, cbToWrite, (const char *)pvBuf, LLUIFY(cbToWrite)); */
    ARRAY_FROM_PATH(g_testRTFileWrite_szData, (const char *)pvBuf);
    if (pcbWritten)
        *pcbWritten = strlen(g_testRTFileWrite_szData) + 1;
    return VINF_SUCCESS;
}

extern int  testRTFileWriteAt(RTFILE File, uint64_t offFile, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    RT_NOREF3(File, cbToWrite, offFile);
 /* RTPrintf("%s: File=%p, pvBuf=%.*s, cbToWrite=%llu\n", __PRETTY_FUNCTION__,
             File, cbToWrite, (const char *)pvBuf, LLUIFY(cbToWrite)); */
    ARRAY_FROM_PATH(g_testRTFileWrite_szData, (const char *)pvBuf);
    if (pcbWritten)
        *pcbWritten = strlen(g_testRTFileWrite_szData) + 1;
    return VINF_SUCCESS;
}

extern int testRTFsQueryProperties(const char *pszFsPath, PRTFSPROPERTIES pProperties)
{
    RT_NOREF1(pszFsPath);
 /* RTPrintf("%s, pszFsPath=%s\n", __PRETTY_FUNCTION__, pszFsPath);
    RT_ZERO(*pProperties); */
    pProperties->cbMaxComponent = 256;
    pProperties->fCaseSensitive = true;
    return VINF_SUCCESS;
}

extern int testRTFsQuerySerial(const char *pszFsPath, uint32_t *pu32Serial)
{
    RT_NOREF2(pszFsPath, pu32Serial);
    RTPrintf("%s\n", __PRETTY_FUNCTION__);
    return 0;
}
extern int testRTFsQuerySizes(const char *pszFsPath, PRTFOFF pcbTotal, RTFOFF *pcbFree, uint32_t *pcbBlock, uint32_t *pcbSector)
{
    RT_NOREF5(pszFsPath, pcbTotal, pcbFree, pcbBlock, pcbSector);
    RTPrintf("%s\n", __PRETTY_FUNCTION__);
    return 0;
}

extern int testRTPathQueryInfoEx(const char *pszPath, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAdditionalAttribs, uint32_t fFlags)
{
    RT_NOREF2(enmAdditionalAttribs, fFlags);
 /* RTPrintf("%s: pszPath=%s, enmAdditionalAttribs=0x%x, fFlags=0x%x\n",
             __PRETTY_FUNCTION__, pszPath, (unsigned) enmAdditionalAttribs,
             (unsigned) fFlags); */
    if (g_fFailIfNotLowercase && !RTStrIsLowerCased(strpbrk(pszPath, "/\\")))
        return VERR_FILE_NOT_FOUND;
    RT_ZERO(*pObjInfo);
    return VINF_SUCCESS;
}

extern int testRTSymlinkDelete(const char *pszSymlink, uint32_t fDelete)
{
    RT_NOREF2(pszSymlink, fDelete);
    if (g_fFailIfNotLowercase && !RTStrIsLowerCased(strpbrk(pszSymlink, "/\\")))
        return VERR_FILE_NOT_FOUND;
    RTPrintf("%s\n", __PRETTY_FUNCTION__);
    return 0;
}

extern int testRTSymlinkRead(const char *pszSymlink, char *pszTarget, size_t cbTarget, uint32_t fRead)
{
    if (g_fFailIfNotLowercase && !RTStrIsLowerCased(strpbrk(pszSymlink, "/\\")))
        return VERR_FILE_NOT_FOUND;
    RT_NOREF4(pszSymlink, pszTarget, cbTarget, fRead);
    RTPrintf("%s\n", __PRETTY_FUNCTION__);
    return 0;
}

extern int testRTSymlinkCreate(const char *pszSymlink, const char *pszTarget, RTSYMLINKTYPE enmType, uint32_t fCreate)
{
    if (g_fFailIfNotLowercase && !RTStrIsLowerCased(strpbrk(pszSymlink, "/\\")))
        return VERR_FILE_NOT_FOUND;
    RT_NOREF4(pszSymlink, pszTarget, enmType, fCreate);
    return 0;
}

extern int testRTFileCopy(const char *pszSrc, const char *pszDst)
{
    if (g_fFailIfNotLowercase && !RTStrIsLowerCased(strpbrk(pszSrc, "/\\"))
        && !RTStrIsLowerCased(strpbrk(pszDst, "/\\")))
        return VERR_FILE_NOT_FOUND;
    RT_NOREF2(pszSrc, pszDst);
    return 0;
}


/*********************************************************************************************************************************
*   Tests                                                                                                                        *
*********************************************************************************************************************************/

/* Sub-tests for testMappingsQuery(). */
void testMappingsQuerySimple(RTTEST hTest) { RT_NOREF1(hTest); }
void testMappingsQueryTooFewBuffers(RTTEST hTest) { RT_NOREF1(hTest); }
void testMappingsQueryAutoMount(RTTEST hTest) { RT_NOREF1(hTest); }
void testMappingsQueryArrayWrongSize(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testMappingsQueryName(). */
void testMappingsQueryNameValid(RTTEST hTest) { RT_NOREF1(hTest); }
void testMappingsQueryNameInvalid(RTTEST hTest) { RT_NOREF1(hTest); }
void testMappingsQueryNameBadBuffer(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testMapFolder(). */
void testMapFolderValid(RTTEST hTest) { RT_NOREF1(hTest); }
void testMapFolderInvalid(RTTEST hTest) { RT_NOREF1(hTest); }
void testMapFolderTwice(RTTEST hTest) { RT_NOREF1(hTest); }
void testMapFolderDelimiter(RTTEST hTest) { RT_NOREF1(hTest); }
void testMapFolderCaseSensitive(RTTEST hTest) { RT_NOREF1(hTest); }
void testMapFolderCaseInsensitive(RTTEST hTest) { RT_NOREF1(hTest); }
void testMapFolderBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testUnmapFolder(). */
void testUnmapFolderValid(RTTEST hTest) { RT_NOREF1(hTest); }
void testUnmapFolderInvalid(RTTEST hTest) { RT_NOREF1(hTest); }
void testUnmapFolderBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testCreate(). */
void testCreateBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testClose(). */
void testCloseBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testRead(). */
void testReadBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testWrite(). */
void testWriteBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testLock(). */
void testLockBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testFlush(). */
void testFlushBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testDirList(). */
void testDirListBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testReadLink(). */
void testReadLinkBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testFSInfo(). */
void testFSInfoBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testRemove(). */
void testRemoveBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testRename(). */
void testRenameBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testSymlink(). */
void testSymlinkBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testMappingsAdd(). */
void testMappingsAddBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

/* Sub-tests for testMappingsRemove(). */
void testMappingsRemoveBadParameters(RTTEST hTest) { RT_NOREF1(hTest); }

union TESTSHFLSTRING
{
    SHFLSTRING string;
    char acData[256];
};

static void fillTestShflString(union TESTSHFLSTRING *pDest,
                               const char *pcszSource)
{
    const size_t cchSource = strlen(pcszSource);
    AssertRelease(  cchSource * 2 + 2
                  < sizeof(*pDest) - RT_UOFFSETOF(SHFLSTRING, String));
    pDest->string.u16Length = (uint16_t)(cchSource * sizeof(RTUTF16));
    pDest->string.u16Size   = pDest->string.u16Length + sizeof(RTUTF16);
    /* Copy pcszSource ASCIIZ, including the trailing 0, to the UTF16 pDest->string.String.ucs2. */
    for (unsigned i = 0; i <= cchSource; ++i)
        pDest->string.String.ucs2[i] = (uint16_t)pcszSource[i];
}

static void fillTestShflStringUtf8(union TESTSHFLSTRING *pDest,
                                   const char *pcszSource)
{
    const size_t cchSource = strlen(pcszSource);
    AssertRelease(  cchSource * 2 + 2
                  < sizeof(*pDest) - RT_UOFFSETOF(SHFLSTRING, String));
    pDest->string.u16Length = (uint16_t)cchSource;
    pDest->string.u16Size   = pDest->string.u16Length + 1;
    memcpy(pDest->string.String.utf8, pcszSource, pDest->string.u16Size);
}

static SHFLROOT initWithWritableMapping(RTTEST hTest,
                                        VBOXHGCMSVCFNTABLE *psvcTable,
                                        VBOXHGCMSVCHELPERS *psvcHelpers,
                                        const char *pcszFolderName,
                                        const char *pcszMapping,
                                        bool fCaseSensitive = true,
                                        SymlinkPolicy_T enmSymlinkPolicy = SymlinkPolicy_AllowedToAnyTarget)
{
    VBOXHGCMSVCPARM aParms[RT_MAX(SHFL_CPARMS_ADD_MAPPING,
                                  SHFL_CPARMS_MAP_FOLDER)];
    union TESTSHFLSTRING FolderName;
    union TESTSHFLSTRING Mapping;
    union TESTSHFLSTRING AutoMountPoint;
    VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };
    int rc;

    initTable(psvcTable, psvcHelpers);
    rc = VBoxHGCMSvcLoad(psvcTable);
    AssertReleaseRC(rc);
    AssertRelease(  psvcTable->pvService
                  = RTTestGuardedAllocTail(hTest, psvcTable->cbClient));
    RT_BZERO(psvcTable->pvService, psvcTable->cbClient);
    fillTestShflString(&FolderName, pcszFolderName);
    fillTestShflString(&Mapping, pcszMapping);
    fillTestShflString(&AutoMountPoint, "");
    HGCMSvcSetPv(&aParms[0], &FolderName,   RT_UOFFSETOF(SHFLSTRING, String)
                                      + FolderName.string.u16Size);
    HGCMSvcSetPv(&aParms[1], &Mapping,   RT_UOFFSETOF(SHFLSTRING, String)
                                   + Mapping.string.u16Size);
    HGCMSvcSetU32(&aParms[2], SHFL_ADD_MAPPING_F_WRITABLE | SHFL_ADD_MAPPING_F_CREATE_SYMLINKS);
    HGCMSvcSetPv(&aParms[3], &AutoMountPoint, SHFLSTRING_HEADER_SIZE + AutoMountPoint.string.u16Size);
    HGCMSvcSetU32(&aParms[4], enmSymlinkPolicy);
    rc = psvcTable->pfnHostCall(psvcTable->pvService, SHFL_FN_ADD_MAPPING,
                                SHFL_CPARMS_ADD_MAPPING, aParms);
    AssertReleaseRC(rc);
    HGCMSvcSetPv(&aParms[0], &Mapping,   RT_UOFFSETOF(SHFLSTRING, String)
                                   + Mapping.string.u16Size);
    HGCMSvcSetU32(&aParms[1], 0);  /* root */
    HGCMSvcSetU32(&aParms[2], '/');  /* delimiter */
    HGCMSvcSetU32(&aParms[3], fCaseSensitive);
    psvcTable->pfnCall(psvcTable->pvService, &callHandle, 0,
                       psvcTable->pvService, SHFL_FN_MAP_FOLDER,
                       SHFL_CPARMS_MAP_FOLDER, aParms, 0);
    AssertReleaseRC(callHandle.rc);
    return aParms[1].u.uint32;
}

static SHFLROOT initWithUnwritableMapping(RTTEST hTest,
                                          VBOXHGCMSVCFNTABLE *psvcTable,
                                          VBOXHGCMSVCHELPERS *psvcHelpers,
                                          const char *pcszFolderName,
                                          const char *pcszMapping,
                                          bool fCaseSensitive = true,
                                          SymlinkPolicy_T enmSymlinkPolicy = SymlinkPolicy_AllowedToAnyTarget)
{
    VBOXHGCMSVCPARM aParms[RT_MAX(SHFL_CPARMS_ADD_MAPPING,
                                  SHFL_CPARMS_MAP_FOLDER)];
    union TESTSHFLSTRING FolderName;
    union TESTSHFLSTRING Mapping;
    union TESTSHFLSTRING AutoMountPoint;
    VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };
    int rc;

    initTable(psvcTable, psvcHelpers);
    rc = VBoxHGCMSvcLoad(psvcTable);
    AssertReleaseRC(rc);
    AssertRelease(  psvcTable->pvService
                  = RTTestGuardedAllocTail(hTest, psvcTable->cbClient));
    RT_BZERO(psvcTable->pvService, psvcTable->cbClient);
    fillTestShflString(&FolderName, pcszFolderName);
    fillTestShflString(&Mapping, pcszMapping);
    fillTestShflString(&AutoMountPoint, "");
    HGCMSvcSetPv(&aParms[0], &FolderName,   RT_UOFFSETOF(SHFLSTRING, String)
                                      + FolderName.string.u16Size);
    HGCMSvcSetPv(&aParms[1], &Mapping,   RT_UOFFSETOF(SHFLSTRING, String)
                                   + Mapping.string.u16Size);
    HGCMSvcSetU32(&aParms[2], SHFL_ADD_MAPPING_F_CREATE_SYMLINKS);
    HGCMSvcSetPv(&aParms[3], &AutoMountPoint, SHFLSTRING_HEADER_SIZE + AutoMountPoint.string.u16Size);
    HGCMSvcSetU32(&aParms[4], enmSymlinkPolicy);
    rc = psvcTable->pfnHostCall(psvcTable->pvService, SHFL_FN_ADD_MAPPING,
                                SHFL_CPARMS_ADD_MAPPING, aParms);
    AssertReleaseRC(rc);
    HGCMSvcSetPv(&aParms[0], &Mapping,   RT_UOFFSETOF(SHFLSTRING, String)
                                   + Mapping.string.u16Size);
    HGCMSvcSetU32(&aParms[1], 0);  /* root */
    HGCMSvcSetU32(&aParms[2], '/');  /* delimiter */
    HGCMSvcSetU32(&aParms[3], fCaseSensitive);
    psvcTable->pfnCall(psvcTable->pvService, &callHandle, 0,
                       psvcTable->pvService, SHFL_FN_MAP_FOLDER,
                       SHFL_CPARMS_MAP_FOLDER, aParms, 0);
    AssertReleaseRC(callHandle.rc);
    return aParms[1].u.uint32;
}

/** @todo Mappings should be automatically removed by unloading the service,
 *        but unloading is currently a no-op! */
static void unmapAndRemoveMapping(RTTEST hTest, VBOXHGCMSVCFNTABLE *psvcTable,
                                  SHFLROOT root, const char *pcszFolderName)
{
    RT_NOREF1(hTest);
    VBOXHGCMSVCPARM aParms[RT_MAX(SHFL_CPARMS_UNMAP_FOLDER,
                                  SHFL_CPARMS_REMOVE_MAPPING)];
    VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };
    union TESTSHFLSTRING FolderName;
    int rc;

    HGCMSvcSetU32(&aParms[0], root);
    psvcTable->pfnCall(psvcTable->pvService, &callHandle, 0,
                       psvcTable->pvService, SHFL_FN_UNMAP_FOLDER,
                       SHFL_CPARMS_UNMAP_FOLDER, aParms, 0);
    AssertReleaseRC(callHandle.rc);
    fillTestShflString(&FolderName, pcszFolderName);
    HGCMSvcSetPv(&aParms[0], &FolderName,   RT_UOFFSETOF(SHFLSTRING, String)
                                      + FolderName.string.u16Size);
    rc = psvcTable->pfnHostCall(psvcTable->pvService, SHFL_FN_REMOVE_MAPPING,
                                SHFL_CPARMS_REMOVE_MAPPING, aParms);
    AssertReleaseRC(rc);
}

static int createFile(VBOXHGCMSVCFNTABLE *psvcTable, SHFLROOT Root,
                      const char *pcszFilename, uint32_t fCreateFlags,
                      SHFLHANDLE *pHandle, SHFLCREATERESULT *pResult)
{
    VBOXHGCMSVCPARM aParms[SHFL_CPARMS_CREATE];
    union TESTSHFLSTRING Path;
    SHFLCREATEPARMS CreateParms;
    VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };

    fillTestShflString(&Path, pcszFilename);
    RT_ZERO(CreateParms);
    CreateParms.CreateFlags = fCreateFlags;
    HGCMSvcSetU32(&aParms[0], Root);
    HGCMSvcSetPv(&aParms[1], &Path,   RT_UOFFSETOF(SHFLSTRING, String)
                                + Path.string.u16Size);
    HGCMSvcSetPv(&aParms[2], &CreateParms, sizeof(CreateParms));
    psvcTable->pfnCall(psvcTable->pvService, &callHandle, 0,
                       psvcTable->pvService, SHFL_FN_CREATE,
                       RT_ELEMENTS(aParms), aParms, 0);
    if (RT_FAILURE(callHandle.rc))
        return callHandle.rc;
    if (pHandle)
        *pHandle = CreateParms.Handle;
    if (pResult)
        *pResult = CreateParms.Result;
    return VINF_SUCCESS;
}

static int fileCopy(VBOXHGCMSVCFNTABLE *psvcTable, SHFLROOT Root,
                    const char *pcszSrcFilename, const char *pcszDstFilename)
{
    VBOXHGCMSVCPARM aParms[SHFL_CPARMS_COPY_FILE];
    union TESTSHFLSTRING srcPath;
    union TESTSHFLSTRING dstPath;
    VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };

    fillTestShflString(&srcPath, pcszSrcFilename);
    fillTestShflString(&dstPath, pcszDstFilename);

    HGCMSvcSetU32(&aParms[0], Root);
    HGCMSvcSetPv(&aParms[1], &srcPath,   RT_UOFFSETOF(SHFLSTRING, String)
                                + srcPath.string.u16Size);
    HGCMSvcSetU32(&aParms[2], Root);
    HGCMSvcSetPv(&aParms[3], &dstPath,   RT_UOFFSETOF(SHFLSTRING, String)
                                + dstPath.string.u16Size);
    HGCMSvcSetU32(&aParms[4], 0);
    psvcTable->pfnCall(psvcTable->pvService, &callHandle, 0,
                       psvcTable->pvService, SHFL_FN_COPY_FILE,
                       RT_ELEMENTS(aParms), aParms, 0);
    return callHandle.rc;
}

static int createSymlink(VBOXHGCMSVCFNTABLE *psvcTable, SHFLROOT Root,
                         const char *pcszSourcePath, const char *pcszSymlinkPath)
{
    VBOXHGCMSVCPARM aParms[SHFL_CPARMS_SYMLINK];
    union TESTSHFLSTRING sourcePath;
    union TESTSHFLSTRING symlinkPath;
    SHFLFSOBJINFO ObjInfo;
    VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };

    /* vbsfSymlink() only supports UTF-8 */
    fillTestShflStringUtf8(&sourcePath, pcszSourcePath);
    fillTestShflStringUtf8(&symlinkPath, pcszSymlinkPath);
    psvcTable->pfnCall(psvcTable->pvService, &callHandle, 0,
                       psvcTable->pvService, SHFL_FN_SET_UTF8,
                       RT_ELEMENTS(aParms), aParms, 0);

    RT_ZERO(ObjInfo);
    HGCMSvcSetU32(&aParms[0], Root);
    HGCMSvcSetPv(&aParms[1], &symlinkPath,  RT_UOFFSETOF(SHFLSTRING, String)
                                + symlinkPath.string.u16Size);
    HGCMSvcSetPv(&aParms[2], &sourcePath,   RT_UOFFSETOF(SHFLSTRING, String)
                                + sourcePath.string.u16Size);
    HGCMSvcSetPv(&aParms[3], &ObjInfo, sizeof(ObjInfo));
    psvcTable->pfnCall(psvcTable->pvService, &callHandle, 0,
                       psvcTable->pvService, SHFL_FN_SYMLINK,
                       RT_ELEMENTS(aParms), aParms, 0);
    return callHandle.rc;
}

static int readFile(VBOXHGCMSVCFNTABLE *psvcTable, SHFLROOT Root,
                    SHFLHANDLE hFile, uint64_t offSeek, uint32_t cbRead,
                    uint32_t *pcbRead, void *pvBuf, uint32_t cbBuf)
{
    VBOXHGCMSVCPARM aParms[SHFL_CPARMS_READ];
    VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };

    HGCMSvcSetU32(&aParms[0], Root);
    HGCMSvcSetU64(&aParms[1], (uint64_t) hFile);
    HGCMSvcSetU64(&aParms[2], offSeek);
    HGCMSvcSetU32(&aParms[3], cbRead);
    HGCMSvcSetPv(&aParms[4], pvBuf, cbBuf);
    psvcTable->pfnCall(psvcTable->pvService, &callHandle, 0,
                       psvcTable->pvService, SHFL_FN_READ,
                       RT_ELEMENTS(aParms), aParms, 0);
    if (pcbRead)
        *pcbRead = aParms[3].u.uint32;
    return callHandle.rc;
}

static int writeFile(VBOXHGCMSVCFNTABLE *psvcTable, SHFLROOT Root,
                     SHFLHANDLE hFile, uint64_t offSeek, uint32_t cbWrite,
                     uint32_t *pcbWritten, const void *pvBuf, uint32_t cbBuf)
{
    VBOXHGCMSVCPARM aParms[SHFL_CPARMS_WRITE];
    VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };

    HGCMSvcSetU32(&aParms[0], Root);
    HGCMSvcSetU64(&aParms[1], (uint64_t) hFile);
    HGCMSvcSetU64(&aParms[2], offSeek);
    HGCMSvcSetU32(&aParms[3], cbWrite);
    HGCMSvcSetPv(&aParms[4], (void *)pvBuf, cbBuf);
    psvcTable->pfnCall(psvcTable->pvService, &callHandle, 0,
                       psvcTable->pvService, SHFL_FN_WRITE,
                       RT_ELEMENTS(aParms), aParms, 0);
    if (pcbWritten)
        *pcbWritten = aParms[3].u.uint32;
    return callHandle.rc;
}

static int flushFile(VBOXHGCMSVCFNTABLE *psvcTable, SHFLROOT root,
                     SHFLHANDLE handle)
{
    VBOXHGCMSVCPARM aParms[SHFL_CPARMS_FLUSH];
    VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };

    HGCMSvcSetU32(&aParms[0], root);
    HGCMSvcSetU64(&aParms[1], handle);
    psvcTable->pfnCall(psvcTable->pvService, &callHandle, 0,
                       psvcTable->pvService, SHFL_FN_FLUSH,
                       SHFL_CPARMS_FLUSH, aParms, 0);
    return callHandle.rc;
}

static int listDir(VBOXHGCMSVCFNTABLE *psvcTable, SHFLROOT root,
                   SHFLHANDLE handle, uint32_t fFlags,
                   const char *pcszPath, void *pvBuf, uint32_t cbBuf,
                   uint32_t resumePoint, uint32_t *pcFiles)
{
    VBOXHGCMSVCPARM aParms[SHFL_CPARMS_LIST];
    union TESTSHFLSTRING Path;
    VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };

    HGCMSvcSetU32(&aParms[0], root);
    HGCMSvcSetU64(&aParms[1], handle);
    HGCMSvcSetU32(&aParms[2], fFlags);
    HGCMSvcSetU32(&aParms[3], cbBuf);
    if (pcszPath)
    {
        fillTestShflString(&Path, pcszPath);
        HGCMSvcSetPv(&aParms[4], &Path,   RT_UOFFSETOF(SHFLSTRING, String)
                                    + Path.string.u16Size);
    }
    else
        HGCMSvcSetPv(&aParms[4], NULL, 0);
    HGCMSvcSetPv(&aParms[5], pvBuf, cbBuf);
    HGCMSvcSetU32(&aParms[6], resumePoint);
    HGCMSvcSetU32(&aParms[7], 0);
    psvcTable->pfnCall(psvcTable->pvService, &callHandle, 0,
                       psvcTable->pvService, SHFL_FN_LIST,
                       RT_ELEMENTS(aParms), aParms, 0);
    if (pcFiles)
        *pcFiles = aParms[7].u.uint32;
    return callHandle.rc;
}

static int sfInformation(VBOXHGCMSVCFNTABLE *psvcTable, SHFLROOT root,
                         SHFLHANDLE handle, uint32_t fFlags, uint32_t cb,
                         SHFLFSOBJINFO *pInfo)
{
    VBOXHGCMSVCPARM aParms[SHFL_CPARMS_INFORMATION];
    VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };

    HGCMSvcSetU32(&aParms[0], root);
    HGCMSvcSetU64(&aParms[1], handle);
    HGCMSvcSetU32(&aParms[2], fFlags);
    HGCMSvcSetU32(&aParms[3], cb);
    HGCMSvcSetPv(&aParms[4], pInfo, cb);
    psvcTable->pfnCall(psvcTable->pvService, &callHandle, 0,
                       psvcTable->pvService, SHFL_FN_INFORMATION,
                       RT_ELEMENTS(aParms), aParms, 0);
    return callHandle.rc;
}

static int lockFile(VBOXHGCMSVCFNTABLE *psvcTable, SHFLROOT root,
                    SHFLHANDLE handle, int64_t offLock, uint64_t cbLock,
                    uint32_t fFlags)
{
    VBOXHGCMSVCPARM aParms[SHFL_CPARMS_LOCK];
    VBOXHGCMCALLHANDLE_TYPEDEF callHandle = { VINF_SUCCESS };

    HGCMSvcSetU32(&aParms[0], root);
    HGCMSvcSetU64(&aParms[1], handle);
    HGCMSvcSetU64(&aParms[2], offLock);
    HGCMSvcSetU64(&aParms[3], cbLock);
    HGCMSvcSetU32(&aParms[4], fFlags);
    psvcTable->pfnCall(psvcTable->pvService, &callHandle, 0,
                       psvcTable->pvService, SHFL_FN_LOCK,
                       RT_ELEMENTS(aParms), aParms, 0);
    return callHandle.rc;
}

void testCreateFileSimple(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    const RTFILE hFile = (RTFILE) 0x10000;
    SHFLCREATERESULT Result;
    int rc;

    RTTestSub(hTest, "Create file simple");
    Root = initWithWritableMapping(hTest, &svcTable, &svcHelpers,
                                   "/test/mapping", "testname");
    g_testRTFileOpen_hFile = hFile;
    rc = createFile(&svcTable, Root, "/test/file", SHFL_CF_ACCESS_READ, NULL,
                    &Result);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RTTEST_CHECK_MSG(hTest,
                     !strcmp(&g_testRTFileOpen_szName[RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS ? 2 : 0],
                             "/test/mapping/test/file"),
                     (hTest, "pszFilename=%s\n", &g_testRTFileOpen_szName[RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS ? 2 : 0]));
    RTTEST_CHECK_MSG(hTest, g_testRTFileOpen_fOpen == 0x181,
                     (hTest, "fOpen=%llu\n", LLUIFY(g_testRTFileOpen_fOpen)));
    RTTEST_CHECK_MSG(hTest, Result == SHFL_FILE_CREATED,
                     (hTest, "Result=%d\n", (int) Result));
    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
    RTTEST_CHECK_MSG(hTest, g_testRTFileClose_hFile == hFile,
                     (hTest, "File=%u\n", (uintptr_t)g_testRTFileClose_hFile));
}

void testCreateFileSimpleCaseInsensitive(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    const RTFILE hFile = (RTFILE) 0x10000;
    SHFLCREATERESULT Result;
    int rc;

    g_fFailIfNotLowercase = true;

    RTTestSub(hTest, "Create file case insensitive");
    Root = initWithWritableMapping(hTest, &svcTable, &svcHelpers,
                                   "/test/mapping", "testname", false /*fCaseSensitive*/);
    g_testRTFileOpen_hFile = hFile;
    rc = createFile(&svcTable, Root, "/TesT/FilE", SHFL_CF_ACCESS_READ, NULL,
                    &Result);
    RTTEST_CHECK_RC_OK(hTest, rc);

    RTTEST_CHECK_MSG(hTest,
                     !strcmp(&g_testRTFileOpen_szName[RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS ? 2 : 0],
                             "/test/mapping/test/file"),
                     (hTest, "pszFilename=%s\n", &g_testRTFileOpen_szName[RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS ? 2 : 0]));
    RTTEST_CHECK_MSG(hTest, g_testRTFileOpen_fOpen == 0x181,
                     (hTest, "fOpen=%llu\n", LLUIFY(g_testRTFileOpen_fOpen)));
    RTTEST_CHECK_MSG(hTest, Result == SHFL_FILE_CREATED,
                     (hTest, "Result=%d\n", (int) Result));
    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
    RTTEST_CHECK_MSG(hTest, g_testRTFileClose_hFile == hFile,
                     (hTest, "File=%u\n", (uintptr_t)g_testRTFileClose_hFile));

    g_fFailIfNotLowercase = false;
}

void testCreateDirSimple(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    RTDIR hDir = (RTDIR)&g_aTestDirHandles[g_iNextDirHandle++ % RT_ELEMENTS(g_aTestDirHandles)];
    SHFLCREATERESULT Result;
    int rc;

    RTTestSub(hTest, "Create directory simple");
    Root = initWithWritableMapping(hTest, &svcTable, &svcHelpers,
                                   "/test/mapping", "testname");
    g_testRTDirOpen_hDir = hDir;
    rc = createFile(&svcTable, Root, "test/dir",
                    SHFL_CF_DIRECTORY | SHFL_CF_ACCESS_READ, NULL, &Result);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RTTEST_CHECK_MSG(hTest,
                     !strcmp(&g_testRTDirCreate_szPath[RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS ? 2 : 0],
                             "/test/mapping/test/dir"),
                     (hTest, "pszPath=%s\n", &g_testRTDirCreate_szPath[RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS ? 2 : 0]));
    RTTEST_CHECK_MSG(hTest,
                     !strcmp(&g_testRTDirOpen_szName[RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS ? 2 : 0],
                             "/test/mapping/test/dir"),
                     (hTest, "pszFilename=%s\n", &g_testRTDirOpen_szName[RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS ? 2 : 0]));
    RTTEST_CHECK_MSG(hTest, Result == SHFL_FILE_CREATED,
                     (hTest, "Result=%d\n", (int) Result));
    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
    RTTEST_CHECK_MSG(hTest, g_testRTDirClose_hDir == hDir, (hTest, "hDir=%p\n", g_testRTDirClose_hDir));
}

void testCopyFileReadWrite(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    const RTFILE hFile = (RTFILE) 0x10000;
    int rc;

    RTTestSub(hTest, "Test copying files (RTFileCopy()) to a writable share");
    Root = initWithWritableMapping(hTest, &svcTable, &svcHelpers,
                                   "/test/mapping", "testname");
    rc = fileCopy(&svcTable, Root, "file", "file-copy");
    RTTEST_CHECK_RC_OK(hTest, rc);

    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
    RTTEST_CHECK_MSG(hTest, g_testRTFileClose_hFile == hFile,
                     (hTest, "File=%u\n", (uintptr_t)g_testRTFileClose_hFile));
}

void testCopyFileReadOnly(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    int rc;

    RTTestSub(hTest, "Test copying files (RTFileCopy()) to a read-only share");
    Root = initWithUnwritableMapping(hTest, &svcTable, &svcHelpers,
                                     "/test/mapping", "testname");
    rc = fileCopy(&svcTable, Root, "file", "file-copy");
    RTTEST_CHECK_MSG(hTest, rc == VERR_WRITE_PROTECT,
                     (hTest, "RTFileCopy() copied a file to a read-only share: rc=%Rrc\n", rc));

    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
}

static int testSymlinkCreationForSpecificPolicy(RTTEST hTest, SymlinkPolicy_T enmSymlinkPolicy)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    const RTFILE hFile = (RTFILE) 0x10000;
    SHFLCREATERESULT Result;
    int rc;

    Root = initWithWritableMapping(hTest, &svcTable, &svcHelpers,
                                   "/test/mapping", "testname",
                                   true, enmSymlinkPolicy);
    g_testRTFileOpen_hFile = hFile;
    rc = createFile(&svcTable, Root, "file", SHFL_CF_ACCESS_READ, NULL, &Result);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RTTEST_CHECK_MSG(hTest,
                     !strcmp(&g_testRTFileOpen_szName[RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS ? 2 : 0],
                             "/test/mapping/file"),
                     (hTest, "pszFilename=%s\n", &g_testRTFileOpen_szName[RTPATH_STYLE == RTPATH_STR_F_STYLE_DOS ? 2 : 0]));
    RTTEST_CHECK_MSG(hTest, g_testRTFileOpen_fOpen == 0x181,
                     (hTest, "fOpen=%llu\n", LLUIFY(g_testRTFileOpen_fOpen)));
    RTTEST_CHECK_MSG(hTest, Result == SHFL_FILE_CREATED,
                     (hTest, "Result=%d\n", (int)Result));

    /* regular symlink creation should succeed unless no symlinks allowed */
    rc = createSymlink(&svcTable, Root, "file", "symlink");
    if (enmSymlinkPolicy == SymlinkPolicy_Forbidden)
        RTTEST_CHECK_MSG(hTest, rc == VERR_WRITE_PROTECT,
                         (hTest, "enmSymlinkPolicy=SymlinkPolicy_Forbidden 'ln -s file symlink' failed: rc=%Rrc\n", rc));
    else
        RTTEST_CHECK_RC_OK(hTest, rc);

    /* absolute path to symlink sources only allowed for the 'any' policy */
    rc = createSymlink(&svcTable, Root, "/path/to/file", "abs-symlink");
    if (enmSymlinkPolicy == SymlinkPolicy_AllowedToAnyTarget)
        RTTEST_CHECK_RC_OK(hTest, rc);
    else
        RTTEST_CHECK_MSG(hTest, rc == VERR_WRITE_PROTECT,
                         (hTest, "enmSymlinkPolicy=%d 'ln -s file /absolute/path/symlink' failed: rc=%Rrc\n",
                         enmSymlinkPolicy, rc));

    /* relative path symlink sources with '..' components allowed only with 'relative' policy */
    rc = createSymlink(&svcTable, Root, "./directory/../file", "rel-symlink");
    if (   enmSymlinkPolicy == SymlinkPolicy_Forbidden
        || enmSymlinkPolicy == SymlinkPolicy_AllowedInShareSubtree)
        RTTEST_CHECK_MSG(hTest, rc == VERR_WRITE_PROTECT,
                         (hTest, "enmSymlinkPolicy=%d 'ln -s ./path/../symlink' failed: rc=%Rrc\n",
                         enmSymlinkPolicy, rc));
    else
        RTTEST_CHECK_RC_OK(hTest, rc);

    /* relative path symlink source with no '..' components always OK */
    rc = createSymlink(&svcTable, Root, "./directory/file", "dotslash-symlink");
    if (enmSymlinkPolicy == SymlinkPolicy_Forbidden)
        RTTEST_CHECK_MSG(hTest, rc == VERR_WRITE_PROTECT,
                         (hTest, "enmSymlinkPolicy=%d 'ln -s ./path/../symlink' failed: rc=%Rrc\n",
                         enmSymlinkPolicy, rc));
    else
        RTTEST_CHECK_RC_OK(hTest, rc);

    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
    RTTEST_CHECK_MSG(hTest, g_testRTFileClose_hFile == hFile,
                     (hTest, "File=%u\n", (uintptr_t)g_testRTFileClose_hFile));

    return rc;
}

void testSymlinkCreation(RTTEST hTest)
{
    SymlinkPolicy_T aEnmSymlinkPolicy[4] = {
        SymlinkPolicy_AllowedToAnyTarget,
        SymlinkPolicy_AllowedInShareSubtree,
        SymlinkPolicy_AllowedToRelativeTargets,
        SymlinkPolicy_Forbidden
    };

    RTTestSub(hTest, "Create variety of symlinks with different symlink policies");
    for (size_t i = 0; i < RT_ELEMENTS(aEnmSymlinkPolicy); i++)
        testSymlinkCreationForSpecificPolicy(hTest, aEnmSymlinkPolicy[i]);
}

void testSymlinkReadOnlyCreation(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    int rc;

    RTTestSub(hTest, "Test creating symlink in a read-only share");
    Root = initWithUnwritableMapping(hTest, &svcTable, &svcHelpers,
                                     "/test/mapping", "testname");

    rc = createSymlink(&svcTable, Root, "file", "symlink");
    RTTEST_CHECK_MSG(hTest, rc == VERR_WRITE_PROTECT,
                     (hTest, "RTSymlinkCreate() created a symlink in a read-only share: rc=%Rrc\n", rc));

    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
}

void testReadFileSimple(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    const RTFILE hFile = (RTFILE) 0x10000;
    SHFLHANDLE Handle;
    const char *pcszReadData = "Data to read";
    char achBuf[sizeof(pcszReadData) + 10];
    uint32_t cbRead;
    int rc;

    RTTestSub(hTest, "Read file simple");
    Root = initWithWritableMapping(hTest, &svcTable, &svcHelpers,
                                   "/test/mapping", "testname");
    g_testRTFileOpen_hFile = hFile;
    rc = createFile(&svcTable, Root, "/test/file", SHFL_CF_ACCESS_READ,
                    &Handle, NULL);
    RTTEST_CHECK_RC_OK(hTest, rc);
    g_testRTFileRead_pszData = pcszReadData;
    memset(achBuf, 'f', sizeof(achBuf));
    rc = readFile(&svcTable, Root, Handle, 0, (uint32_t)strlen(pcszReadData) + 1,
                  &cbRead, achBuf, (uint32_t)sizeof(achBuf));
    RTTEST_CHECK_RC_OK(hTest, rc);
    RTTEST_CHECK_MSG(hTest,
                     !strncmp(achBuf, pcszReadData, sizeof(achBuf)),
                     (hTest, "pvBuf=%.*s Handle=%#RX64\n", sizeof(achBuf), achBuf, Handle));
    RTTEST_CHECK_MSG(hTest, cbRead == strlen(pcszReadData) + 1,
                     (hTest, "cbRead=%llu\n", LLUIFY(cbRead)));
    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    RTTEST_CHECK_MSG(hTest, g_testRTFileClose_hFile == hFile, (hTest, "File=%u\n", g_testRTFileClose_hFile));
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
}

void testWriteFileSimple(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    const RTFILE hFile = (RTFILE) 0x10000;
    SHFLHANDLE Handle;
    const char *pcszWrittenData = "Data to write";
    uint32_t cbToWrite = (uint32_t)strlen(pcszWrittenData) + 1;
    uint32_t cbWritten;
    int rc;

    RTTestSub(hTest, "Write file simple");
    Root = initWithWritableMapping(hTest, &svcTable, &svcHelpers,
                                   "/test/mapping", "testname");
    g_testRTFileOpen_hFile = hFile;
    rc = createFile(&svcTable, Root, "/test/file", SHFL_CF_ACCESS_READ,
                    &Handle, NULL);
    RTTEST_CHECK_RC_OK(hTest, rc);
    rc = writeFile(&svcTable, Root, Handle, 0, cbToWrite, &cbWritten,
                   pcszWrittenData, cbToWrite);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RTTEST_CHECK_MSG(hTest,
                     !strcmp(g_testRTFileWrite_szData, pcszWrittenData),
                     (hTest, "pvBuf=%s\n", g_testRTFileWrite_szData));
    RTTEST_CHECK_MSG(hTest, cbWritten == cbToWrite,
                     (hTest, "cbWritten=%llu\n", LLUIFY(cbWritten)));
    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    RTTEST_CHECK_MSG(hTest, g_testRTFileClose_hFile == hFile, (hTest, "File=%u\n", g_testRTFileClose_hFile));
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
}

void testFlushFileSimple(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    const RTFILE hFile = (RTFILE) 0x10000;
    SHFLHANDLE Handle;
    int rc;

    RTTestSub(hTest, "Flush file simple");
    Root = initWithWritableMapping(hTest, &svcTable, &svcHelpers,
                                   "/test/mapping", "testname");
    g_testRTFileOpen_hFile = hFile;
    rc = createFile(&svcTable, Root, "/test/file", SHFL_CF_ACCESS_READ,
                    &Handle, NULL);
    RTTEST_CHECK_RC_OK(hTest, rc);
    rc = flushFile(&svcTable, Root, Handle);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RTTEST_CHECK_MSG(hTest, g_testRTFileFlush_hFile == hFile, (hTest, "File=%u\n", g_testRTFileFlush_hFile));
    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
    RTTEST_CHECK_MSG(hTest, g_testRTFileClose_hFile == hFile, (hTest, "File=%u\n", g_testRTFileClose_hFile));
}

void testDirListEmpty(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    RTDIR hDir = (RTDIR)&g_aTestDirHandles[g_iNextDirHandle++ % RT_ELEMENTS(g_aTestDirHandles)];
    SHFLHANDLE Handle;
    union
    {
        SHFLDIRINFO DirInfo;
        uint8_t     abBuffer[sizeof(SHFLDIRINFO) + 2 * sizeof(RTUTF16)];
    } Buf;
    uint32_t cFiles;
    int rc;

    RTTestSub(hTest, "List empty directory");
    Root = initWithWritableMapping(hTest, &svcTable, &svcHelpers,
                                   "/test/mapping", "testname");
    g_testRTDirOpen_hDir = hDir;
    rc = createFile(&svcTable, Root, "test/dir",
                    SHFL_CF_DIRECTORY | SHFL_CF_ACCESS_READ, &Handle, NULL);
    RTTEST_CHECK_RC_OK(hTest, rc);
    rc = listDir(&svcTable, Root, Handle, 0, NULL, &Buf.DirInfo, sizeof(Buf), 0, &cFiles);
    RTTEST_CHECK_RC(hTest, rc, VERR_NO_MORE_FILES);
    RTTEST_CHECK_MSG(hTest, g_testRTDirReadEx_hDir == hDir, (hTest, "Dir=%p\n", g_testRTDirReadEx_hDir));
    RTTEST_CHECK_MSG(hTest, cFiles == 0,
                     (hTest, "cFiles=%llu\n", LLUIFY(cFiles)));
    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
    RTTEST_CHECK_MSG(hTest, g_testRTDirClose_hDir == hDir, (hTest, "hDir=%p\n", g_testRTDirClose_hDir));
}

void testFSInfoQuerySetFMode(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    const RTFILE hFile = (RTFILE) 0x10000;
    const uint32_t fMode = 0660;
    SHFLFSOBJINFO Info;
    int rc;

    RTTestSub(hTest, "Query and set file size");
    Root = initWithWritableMapping(hTest, &svcTable, &svcHelpers,
                                   "/test/mapping", "testname");
    SHFLHANDLE Handle = SHFL_HANDLE_NIL;
    g_testRTFileOpen_hFile = hFile;
    rc = createFile(&svcTable, Root, "/test/file", SHFL_CF_ACCESS_READ,
                    &Handle, NULL);
    RTTEST_CHECK_RC_OK_RETV(hTest, rc);

    RT_ZERO(Info);
    g_testRTFileQueryInfo_fMode = fMode;
    rc = sfInformation(&svcTable, Root, Handle, SHFL_INFO_FILE, sizeof(Info),
                       &Info);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RTTEST_CHECK_MSG(hTest, g_testRTFileQueryInfo_hFile == hFile, (hTest, "File=%u\n", g_testRTFileQueryInfo_hFile));
    RTTEST_CHECK_MSG(hTest, Info.Attr.fMode == fMode,
                     (hTest, "cbObject=%llu\n", LLUIFY(Info.cbObject)));
    RT_ZERO(Info);
    Info.Attr.fMode = fMode;
    rc = sfInformation(&svcTable, Root, Handle, SHFL_INFO_SET | SHFL_INFO_FILE,
                       sizeof(Info), &Info);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RTTEST_CHECK_MSG(hTest, g_testRTFileSet_fMode == fMode,
                     (hTest, "Size=%llu\n", LLUIFY(g_testRTFileSet_fMode)));
    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
    RTTEST_CHECK_MSG(hTest, g_testRTFileClose_hFile == hFile, (hTest, "File=%u\n", g_testRTFileClose_hFile));
}

void testFSInfoQuerySetDirATime(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    const RTDIR hDir = (RTDIR)&g_aTestDirHandles[g_iNextDirHandle++ % RT_ELEMENTS(g_aTestDirHandles)];
    const int64_t ccAtimeNano = 100000;
    SHFLFSOBJINFO Info;
    SHFLHANDLE Handle;
    int rc;

    RTTestSub(hTest, "Query and set directory atime");
    Root = initWithWritableMapping(hTest, &svcTable, &svcHelpers,
                                   "/test/mapping", "testname");
    g_testRTDirOpen_hDir = hDir;
    rc = createFile(&svcTable, Root, "test/dir",
                    SHFL_CF_DIRECTORY | SHFL_CF_ACCESS_READ, &Handle, NULL);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RT_ZERO(Info);
    RTTimeSpecSetNano(&g_testRTDirQueryInfo_ATime, ccAtimeNano);
    rc = sfInformation(&svcTable, Root, Handle, SHFL_INFO_FILE, sizeof(Info),
                       &Info);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RTTEST_CHECK_MSG(hTest, g_testRTDirQueryInfo_hDir == hDir, (hTest, "Dir=%p\n", g_testRTDirQueryInfo_hDir));
    RTTEST_CHECK_MSG(hTest, RTTimeSpecGetNano(&Info.AccessTime) == ccAtimeNano,
                     (hTest, "ATime=%llu\n",
                      LLUIFY(RTTimeSpecGetNano(&Info.AccessTime))));
    RT_ZERO(Info);
    RTTimeSpecSetNano(&Info.AccessTime, ccAtimeNano);
    rc = sfInformation(&svcTable, Root, Handle, SHFL_INFO_SET | SHFL_INFO_FILE,
                       sizeof(Info), &Info);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RTTEST_CHECK_MSG(hTest,    RTTimeSpecGetNano(&g_testRTDirSetTimes_ATime)
                            == ccAtimeNano,
                     (hTest, "ATime=%llu\n",
                      LLUIFY(RTTimeSpecGetNano(&g_testRTDirSetTimes_ATime))));
    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
    RTTEST_CHECK_MSG(hTest, g_testRTDirClose_hDir == hDir, (hTest, "hDir=%p\n", g_testRTDirClose_hDir));
}

void testFSInfoQuerySetFileATime(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    const RTFILE hFile = (RTFILE) 0x10000;
    const int64_t ccAtimeNano = 100000;
    SHFLFSOBJINFO Info;
    SHFLHANDLE Handle;
    int rc;

    RTTestSub(hTest, "Query and set file atime");
    Root = initWithWritableMapping(hTest, &svcTable, &svcHelpers,
                                   "/test/mapping", "testname");
    g_testRTFileOpen_hFile = hFile;
    rc = createFile(&svcTable, Root, "/test/file", SHFL_CF_ACCESS_READ,
                    &Handle, NULL);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RT_ZERO(Info);
    RTTimeSpecSetNano(&g_testRTFileQueryInfo_ATime, ccAtimeNano);
    rc = sfInformation(&svcTable, Root, Handle, SHFL_INFO_FILE, sizeof(Info),
                       &Info);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RTTEST_CHECK_MSG(hTest, g_testRTFileQueryInfo_hFile == hFile, (hTest, "File=%u\n", g_testRTFileQueryInfo_hFile));
    RTTEST_CHECK_MSG(hTest, RTTimeSpecGetNano(&Info.AccessTime) == ccAtimeNano,
                     (hTest, "ATime=%llu\n",
                      LLUIFY(RTTimeSpecGetNano(&Info.AccessTime))));
    RT_ZERO(Info);
    RTTimeSpecSetNano(&Info.AccessTime, ccAtimeNano);
    rc = sfInformation(&svcTable, Root, Handle, SHFL_INFO_SET | SHFL_INFO_FILE,
                       sizeof(Info), &Info);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RTTEST_CHECK_MSG(hTest,    RTTimeSpecGetNano(&g_testRTFileSetTimes_ATime)
                            == ccAtimeNano,
                     (hTest, "ATime=%llu\n",
                      LLUIFY(RTTimeSpecGetNano(&g_testRTFileSetTimes_ATime))));
    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
    RTTEST_CHECK_MSG(hTest, g_testRTFileClose_hFile == hFile, (hTest, "File=%u\n", g_testRTFileClose_hFile));
}

void testFSInfoQuerySetEndOfFile(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    const RTFILE hFile = (RTFILE) 0x10000;
    const RTFOFF cbNew = 50000;
    SHFLFSOBJINFO Info;
    SHFLHANDLE Handle;
    int rc;

    RTTestSub(hTest, "Set end of file position");
    Root = initWithWritableMapping(hTest, &svcTable, &svcHelpers,
                                   "/test/mapping", "testname");
    g_testRTFileOpen_hFile = hFile;
    rc = createFile(&svcTable, Root, "/test/file", SHFL_CF_ACCESS_READ,
                    &Handle, NULL);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RT_ZERO(Info);
    Info.cbObject = cbNew;
    rc = sfInformation(&svcTable, Root, Handle, SHFL_INFO_SET | SHFL_INFO_SIZE,
                       sizeof(Info), &Info);
    RTTEST_CHECK_RC_OK(hTest, rc);
    RTTEST_CHECK_MSG(hTest, g_testRTFileSetSize_hFile == hFile, (hTest, "File=%u\n", g_testRTFileSetSize_hFile));
    RTTEST_CHECK_MSG(hTest, g_testRTFileSetSize_cbSize == cbNew,
                     (hTest, "Size=%llu\n", LLUIFY(g_testRTFileSetSize_cbSize)));
    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
    RTTEST_CHECK_MSG(hTest, g_testRTFileClose_hFile == hFile, (hTest, "File=%u\n", g_testRTFileClose_hFile));
}

void testLockFileSimple(RTTEST hTest)
{
    VBOXHGCMSVCFNTABLE  svcTable;
    VBOXHGCMSVCHELPERS  svcHelpers;
    SHFLROOT Root;
    const RTFILE hFile = (RTFILE) 0x10000;
    const int64_t offLock = 50000;
    const uint64_t cbLock = 4000;
    SHFLHANDLE Handle;
    int rc;

    RTTestSub(hTest, "Simple file lock and unlock");
    Root = initWithWritableMapping(hTest, &svcTable, &svcHelpers,
                                   "/test/mapping", "testname");
    g_testRTFileOpen_hFile = hFile;
    rc = createFile(&svcTable, Root, "/test/file", SHFL_CF_ACCESS_READ,
                    &Handle, NULL);
    RTTEST_CHECK_RC_OK(hTest, rc);
    rc = lockFile(&svcTable, Root, Handle, offLock, cbLock, SHFL_LOCK_SHARED);
    RTTEST_CHECK_RC_OK(hTest, rc);
#ifdef RT_OS_WINDOWS  /* Locking is a no-op elsewhere. */
    RTTEST_CHECK_MSG(hTest, g_testRTFileLock_hFile == hFile, (hTest, "File=%u\n", g_testRTFileLock_hFile));
    RTTEST_CHECK_MSG(hTest, g_testRTFileLock_fLock == 0,
                     (hTest, "fLock=%u\n", g_testRTFileLock_fLock));
    RTTEST_CHECK_MSG(hTest, g_testRTFileLock_offLock == offLock,
                     (hTest, "Offs=%llu\n", (long long) g_testRTFileLock_offLock));
    RTTEST_CHECK_MSG(hTest, g_testRTFileLock_cbLock == cbLock,
                     (hTest, "Size=%llu\n", LLUIFY(g_testRTFileLock_cbLock)));
#endif
    rc = lockFile(&svcTable, Root, Handle, offLock, cbLock, SHFL_LOCK_CANCEL);
    RTTEST_CHECK_RC_OK(hTest, rc);
#ifdef RT_OS_WINDOWS
    RTTEST_CHECK_MSG(hTest, g_testRTFileUnlock_hFile == hFile, (hTest, "File=%u\n", g_testRTFileUnlock_hFile));
    RTTEST_CHECK_MSG(hTest, g_testRTFileUnlock_offLock == offLock,
                     (hTest, "Offs=%llu\n",
                      (long long) g_testRTFileUnlock_offLock));
    RTTEST_CHECK_MSG(hTest, g_testRTFileUnlock_cbLock == cbLock,
                     (hTest, "Size=%llu\n", LLUIFY(g_testRTFileUnlock_cbLock)));
#endif
    unmapAndRemoveMapping(hTest, &svcTable, Root, "testname");
    rc = svcTable.pfnDisconnect(NULL, 0, svcTable.pvService);
    AssertReleaseRC(rc);
    rc = svcTable.pfnUnload(NULL);
    AssertReleaseRC(rc);
    RTTestGuardedFree(hTest, svcTable.pvService);
    RTTEST_CHECK_MSG(hTest, g_testRTFileClose_hFile == hFile, (hTest, "File=%u\n", g_testRTFileClose_hFile));
}


/*********************************************************************************************************************************
*   Main code                                                                                                                    *
*********************************************************************************************************************************/

static void testAPI(RTTEST hTest)
{
    testMappingsQuery(hTest);
    testMappingsQueryName(hTest);
    testMapFolder(hTest);
    testUnmapFolder(hTest);
    testCreate(hTest);
    testClose(hTest);
    testRead(hTest);
    testWrite(hTest);
    testLock(hTest);
    testFlush(hTest);
    testDirList(hTest);
    testReadLink(hTest);
    testFSInfo(hTest);
    testRemove(hTest);
    testRename(hTest);
    testCopyFile(hTest);
    testSymlink(hTest);
    testMappingsAdd(hTest);
    testMappingsRemove(hTest);
    /* testSetStatusLed(hTest); */
}

int main(int argc, char **argv)
{
    RT_NOREF1(argc);
    RTEXITCODE rcExit = RTTestInitAndCreate(RTPathFilename(argv[0]), &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    RTTestBanner(g_hTest);
    testAPI(g_hTest);
    return RTTestSummaryAndDestroy(g_hTest);
}
