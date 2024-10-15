/** $Id: VBoxSF.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSF - OS/2 Shared Folders, the FS and FSD level IFS EPs
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEFAULT
#include "VBoxSFInternal.h"

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/path.h>

#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Max folder name length, including terminator.
 * Easier to deal with stack buffers if we put a reasonable limit on the. */
#define VBOXSFOS2_MAX_FOLDER_NAME   64


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** VMMDEV_HVF_XXX (set during init). */
uint32_t            g_fHostFeatures = 0;
/** The shared mutex protecting folders list, drives and the connection. */
MutexLock_t         g_MtxFolders;
/** The shared folder service client structure. */
VBGLSFCLIENT        g_SfClient;
/** Set if g_SfClient is valid, clear if not. */
bool                g_fIsConnectedToService = false;
/** List of active folder (PVBOXSFFOLDER). */
RTLISTANCHOR        g_FolderHead;
/** This is incremented everytime g_FolderHead is modified. */
uint32_t volatile   g_uFolderRevision;
/** Folders mapped on drive letters.  Pointers include a reference. */
PVBOXSFFOLDER       g_apDriveFolders[26];



/**
 * Generic IPRT -> OS/2 status code converter.
 *
 * @returns OS/2 status code.
 * @param   vrc             IPRT/VBox status code.
 * @param   rcDefault       The OS/2 status code to return when there
 *                          is no translation.
 */
APIRET vboxSfOs2ConvertStatusToOs2(int vrc, APIRET rcDefault)
{
    switch (vrc)
    {
        default:                        return rcDefault;

        case VERR_FILE_NOT_FOUND:       return ERROR_FILE_NOT_FOUND;
        case VERR_PATH_NOT_FOUND:       return ERROR_PATH_NOT_FOUND;
        case VERR_SHARING_VIOLATION:    return ERROR_SHARING_VIOLATION;
        case VERR_ACCESS_DENIED:        return ERROR_ACCESS_DENIED;
        case VERR_ALREADY_EXISTS:       return ERROR_ACCESS_DENIED;
        case VERR_WRITE_PROTECT:        return ERROR_WRITE_PROTECT;
        case VERR_IS_A_DIRECTORY:       return ERROR_DIRECTORY;
        case VERR_DISK_FULL:            return ERROR_DISK_FULL;
        case VINF_SUCCESS:              return NO_ERROR;
    }
}


/**
 * Gets the delta for the local timezone, in minutes.
 *
 * We need to do this once for each API call rather than over and over again for
 * each date/time conversion, so as not to create an update race.
 *
 * @returns Delta in minutes.  Current thinking is that positive means timezone
 *          is west of UTC, while negative is east of it.
 */
int16_t vboxSfOs2GetLocalTimeDelta(void)
{
    GINFOSEG volatile *pGis = (GINFOSEG volatile *)&KernSISData;
    if (pGis)
    {
        uint16_t cDelta = pGis->timezone;
        if (cDelta != 0 && cDelta != 0xffff)
            return (int16_t)cDelta;
    }
    return 0;
}


/**
 * Helper for converting from IPRT timespec format to OS/2 DATE/TIME.
 *
 * @param   pDosDate            The output DOS date.
 * @param   pDosTime            The output DOS time.
 * @param   SrcTimeSpec         The IPRT input timestamp.
 * @param   cMinLocalTimeDelta  The timezone delta in minutes.
 */
void vboxSfOs2DateTimeFromTimeSpec(FDATE *pDosDate, FTIME *pDosTime, RTTIMESPEC SrcTimeSpec, int16_t cMinLocalTimeDelta)
{
    if (cMinLocalTimeDelta != 0)
        RTTimeSpecAddSeconds(&SrcTimeSpec, -cMinLocalTimeDelta * 60);

    RTTIME Time;
    if (   RTTimeSpecGetNano(&SrcTimeSpec) >= RTTIME_OFFSET_DOS_TIME
        && RTTimeExplode(&Time, &SrcTimeSpec))
    {
        pDosDate->year    = Time.i32Year - 1980;
        pDosDate->month   = Time.u8Month;
        pDosDate->day     = Time.u8MonthDay;
        pDosTime->hours   = Time.u8Hour;
        pDosTime->minutes = Time.u8Minute;
        pDosTime->twosecs = Time.u8Second / 2;
    }
    else
    {
        pDosDate->year    = 0;
        pDosDate->month   = 1;
        pDosDate->day     = 1;
        pDosTime->hours   = 0;
        pDosTime->minutes = 0;
        pDosTime->twosecs = 0;
    }
}


/**
 * Helper for converting from OS/2 DATE/TIME to IPRT timespec format.
 *
 * @returns pDstTimeSpec on success, NULL if invalid input.
 * @param   DosDate             The input DOS date.
 * @param   DosTime             The input DOS time.
 * @param   cMinLocalTimeDelta  The timezone delta in minutes.
 * @param   pDstTimeSpec        The IPRT output timestamp.
 */
PRTTIMESPEC vboxSfOs2DateTimeToTimeSpec(FDATE DosDate, FTIME DosTime, int16_t cMinLocalTimeDelta, PRTTIMESPEC pDstTimeSpec)
{
    RTTIME Time;
    Time.i32Year        = DosDate.year + 1980;
    Time.u8Month        = DosDate.month;
    Time.u8WeekDay      = UINT8_MAX;
    Time.u16YearDay     = 0;
    Time.u8MonthDay     = DosDate.day;
    Time.u8Hour         = DosTime.hours;
    Time.u8Minute       = DosTime.minutes;
    Time.u8Second       = DosTime.twosecs * 2;
    Time.u32Nanosecond  = 0;
    Time.fFlags         = RTTIME_FLAGS_TYPE_LOCAL;
    Time.offUTC         = -cMinLocalTimeDelta;
    if (RTTimeLocalNormalize(&Time))
        return RTTimeImplode(pDstTimeSpec, &Time);
    return NULL;
}


/*********************************************************************************************************************************
*   Shared Folder String Buffer Management                                                                                       *
*********************************************************************************************************************************/

/**
 * Allocates a SHFLSTRING buffer (UTF-16).
 *
 * @returns Pointer to a SHFLSTRING buffer, NULL if out of memory.
 * @param   cwcLength   The desired string buffer length in UTF-16 units
 *                      (excluding terminator).
 */
PSHFLSTRING vboxSfOs2StrAlloc(size_t cwcLength)
{
    AssertReturn(cwcLength <= 0x1000, NULL);
    uint16_t cb = (uint16_t)cwcLength + 1;
    cb *= sizeof(RTUTF16);

    PSHFLSTRING pStr = (PSHFLSTRING)VbglR0PhysHeapAlloc(SHFLSTRING_HEADER_SIZE + cb);
    if (pStr)
    {
        pStr->u16Size         = cb;
        pStr->u16Length       = 0;
        pStr->String.utf16[0] = '\0';
        return pStr;
    }
    return NULL;
}


/**
 * Duplicates a shared folders string buffer (UTF-16).
 *
 * @returns Pointer to a SHFLSTRING buffer containing the copy.
 *          NULL if out of memory or the string is too long.
 * @param   pSrc        The string to clone.
 */
PSHFLSTRING vboxSfOs2StrDup(PCSHFLSTRING pSrc)
{
    PSHFLSTRING pDst = (PSHFLSTRING)VbglR0PhysHeapAlloc(SHFLSTRING_HEADER_SIZE + pSrc->u16Length + sizeof(RTUTF16));
    if (pDst)
    {
        pDst->u16Size   = pSrc->u16Length + (uint16_t)sizeof(RTUTF16);
        pDst->u16Length = pSrc->u16Length;
        memcpy(&pDst->String, &pSrc->String, pSrc->u16Length);
        pDst->String.utf16[pSrc->u16Length / sizeof(RTUTF16)] = '\0';
        return pDst;
    }
    return NULL;
}


/**
 * Frees a SHLFSTRING buffer.
 *
 * @param   pStr        The buffer to free.
 */
void vboxSfOs2StrFree(PSHFLSTRING pStr)
{
    if (pStr)
        VbglR0PhysHeapFree(pStr);
}



/*********************************************************************************************************************************
*   Folders, Paths and Service Connection.                                                                                       *
*********************************************************************************************************************************/

/**
 * Ensures that we're connected to the host service.
 *
 * @returns VBox status code.
 * @remarks Caller owns g_MtxFolder exclusively!
 */
static int vboxSfOs2EnsureConnected(void)
{
    if (g_fIsConnectedToService)
        return VINF_SUCCESS;

    int rc = VbglR0SfConnect(&g_SfClient);
    if (RT_SUCCESS(rc))
        g_fIsConnectedToService = true;
    else
        LogRel(("VbglR0SfConnect failed: %Rrc\n", rc));
    return rc;
}


/**
 * Destroys a folder when the reference count has reached zero.
 *
 * @param   pFolder         The folder to destroy.
 */
static void vboxSfOs2DestroyFolder(PVBOXSFFOLDER pFolder)
{
    /* Note! We won't get there while the folder is on the list. */
    LogRel(("vboxSfOs2ReleaseFolder: Destroying %p [%s]\n", pFolder, pFolder->szName));
    VbglR0SfHostReqUnmapFolderSimple(pFolder->idHostRoot);
    RT_ZERO(pFolder);
    RTMemFree(pFolder);
}


/**
 * Releases a reference to a folder.
 *
 * @param   pFolder         The folder to release.
 */
void vboxSfOs2ReleaseFolder(PVBOXSFFOLDER pFolder)
{
    if (pFolder)
    {
        uint32_t cRefs = ASMAtomicDecU32(&pFolder->cRefs);
        AssertMsg(cRefs < _64K, ("%#x\n", cRefs));
        if (!cRefs)
            vboxSfOs2DestroyFolder(pFolder);
    }
}


/**
 * Retain a reference to a folder.
 *
 * @param   pFolder         The folder to release.
 */
void vboxSfOs2RetainFolder(PVBOXSFFOLDER pFolder)
{
    uint32_t cRefs = ASMAtomicIncU32(&pFolder->cRefs);
    AssertMsg(cRefs < _64K, ("%#x\n", cRefs));
}


/**
 * Locates and retains a folder structure.
 *
 * @returns Folder matching the name, NULL of not found.
 * @remarks Caller owns g_MtxFolder.
 */
static PVBOXSFFOLDER vboxSfOs2FindAndRetainFolder(const char *pachName, size_t cchName)
{
    PVBOXSFFOLDER pCur;
    RTListForEach(&g_FolderHead, pCur, VBOXSFFOLDER, ListEntry)
    {
        if (   pCur->cchName == cchName
            && RTStrNICmpAscii(pCur->szName, pachName, cchName) == 0)
        {
            uint32_t cRefs = ASMAtomicIncU32(&pCur->cRefs);
            AssertMsg(cRefs < _64K, ("%#x\n", cRefs));
            return pCur;
        }
    }
    return NULL;
}


/**
 * Maps a folder, linking it into the list of folders.
 *
 * One reference is retained for the caller, which must pass it on or release
 * it.  The list also have a reference to it.
 *
 * @returns VBox status code.
 * @param   pName       The name of the folder to map - ASCII (not UTF-16!).
 *                      Must be large enough to hold UTF-16 expansion of the
 *                      string, will do so upon return.
 * @param   pszTag      Folder tag (for the VBoxService automounter).  Optional.
 * @param   ppFolder    Where to return the folder structure on success.
 *
 * @remarks Caller owns g_MtxFolder exclusively!
 */
static int vboxSfOs2MapFolder(PSHFLSTRING pName, const char *pszTag, PVBOXSFFOLDER *ppFolder)
{
    int rc;
    size_t const  cbTag = pszTag ? strlen(pszTag) + 1 :  NULL;
    PVBOXSFFOLDER pNew  = (PVBOXSFFOLDER)RTMemAlloc(RT_UOFFSETOF_DYN(VBOXSFFOLDER, szName[pName->u16Length + 1 + cbTag]));
    if (pNew != NULL)
    {
        pNew->u32Magic      = VBOXSFFOLDER_MAGIC;
        pNew->cRefs         = 2; /* (List reference + the returned reference.) */
        pNew->cOpenFiles    = 0;
        pNew->cOpenSearches = 0;
        pNew->cDrives       = 0;
        pNew->idHostRoot    = SHFL_ROOT_NIL;
        pNew->hVpb          = 0;
        pNew->cbNameAndTag  = pName->u16Length + (uint16_t)cbTag;
        pNew->cchName       = (uint8_t)pName->u16Length;
        pNew->cchName       = (uint8_t)pName->u16Length;
        memcpy(pNew->szName, pName->String.utf8, pName->u16Length);
        pNew->szName[pName->u16Length] = '\0';
        if (cbTag)
            memcpy(&pNew->szName[pName->u16Length + 1], pszTag, cbTag);

        /* Expand the folder name to UTF-16. */
        uint8_t                 off     = pNew->cchName;
        uint8_t volatile const *pbSrc   = &pName->String.utf8[0];
        RTUTF16 volatile       *pwcDst  = &pName->String.utf16[0];
        do
            pwcDst[off] = pbSrc[off];
        while (off-- > 0);
        pName->u16Length *= sizeof(RTUTF16);
        Assert(pName->u16Length + sizeof(RTUTF16) <= pName->u16Size);

        /* Try do the mapping.*/
        VBOXSFMAPFOLDERWITHBUFREQ *pReq = (VBOXSFMAPFOLDERWITHBUFREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
        if (pReq)
        {
            rc = VbglR0SfHostReqMapFolderWithBuf(pReq, pName, RTPATH_DELIMITER, false /*fCaseSensitive*/);
            if (RT_SUCCESS(rc))
            {
                pNew->idHostRoot = pReq->Parms.id32Root.u.value32;

                RTListAppend(&g_FolderHead, &pNew->ListEntry);
                ASMAtomicIncU32(&g_uFolderRevision);
                LogRel(("vboxSfOs2MapFolder: %p - %s\n", pNew, pNew->szName));

                *ppFolder = pNew;
                pNew = NULL;
            }
            else
                LogRel(("vboxSfOs2MapFolder: VbglR0SfHostReqMapFolderWithBuf(,%s,) -> %Rrc\n", pNew->szName, rc));
            VbglR0PhysHeapFree(pReq);
        }
        else
            LogRel(("vboxSfOs2MapFolder: Out of physical heap :-(\n"));
        RTMemFree(pNew);
    }
    else
    {
        LogRel(("vboxSfOs2MapFolder: Out of memory :-(\n"));
        rc = VERR_NO_MEMORY;
    }
    return rc;
}


/**
 * Worker for vboxSfOs2UncPrefixLength.
 */
DECLINLINE(size_t) vboxSfOs2CountLeadingSlashes(const char *pszPath)
{
    size_t cchSlashes = 0;
    char ch;
    while ((ch = *pszPath) == '\\' || ch == '/')
        cchSlashes++, pszPath++;
    return cchSlashes;
}


/**
 * Checks for a VBox UNC prefix (server + slashes) and determins its length when
 * found.
 *
 * @returns Length of VBoxSF UNC prefix, 0 if not VBoxSF UNC prefix.
 * @param   pszPath             The possible UNC path.
 */
DECLINLINE(size_t) vboxSfOs2UncPrefixLength(const char *pszPath)
{
    char ch;
    if (   ((ch = pszPath[0]) == '\\' || ch == '/')
        && ((ch = pszPath[1]) == '\\' || ch == '/')
        && ((ch = pszPath[2]) == 'V'  || ch == 'v')
        && ((ch = pszPath[3]) == 'B'  || ch == 'b')
        && ((ch = pszPath[4]) == 'O'  || ch == 'o')
        && ((ch = pszPath[5]) == 'X'  || ch == 'x')
        && ((ch = pszPath[6]) == 'S'  || ch == 's')
       )
    {
        /* \\VBoxSf\ */
        if (   ((ch = pszPath[7]) == 'F'  || ch == 'f')
            && ((ch = pszPath[8]) == '\\' || ch == '/') )
            return vboxSfOs2CountLeadingSlashes(&pszPath[9]) + 9;

        /* \\VBoxSvr\ */
        if (   ((ch = pszPath[7]) == 'V'  || ch == 'v')
            && ((ch = pszPath[8]) == 'R'  || ch == 'r')
            && ((ch = pszPath[9]) == '\\' || ch == '/') )
            return vboxSfOs2CountLeadingSlashes(&pszPath[10]) + 10;

        /* \\VBoxSrv\ */
        if (   ((ch = pszPath[7]) == 'R'  || ch == 'r')
            && ((ch = pszPath[8]) == 'V'  || ch == 'v')
            && ((ch = pszPath[9]) == '\\' || ch == '/') )
            return vboxSfOs2CountLeadingSlashes(&pszPath[10]) + 10;
    }

    return 0;
}


/**
 * Converts a path to UTF-16 and puts it in a VBGL friendly buffer.
 *
 * @returns OS/2 status code
 * @param   pszFolderPath   The path to convert.
 * @param   ppStr           Where to return the pointer to the buffer.  Free
 *                          using vboxSfOs2FreePath.
 */
APIRET vboxSfOs2ConvertPath(const char *pszFolderPath, PSHFLSTRING *ppStr)
{
    /*
     * Skip unnecessary leading slashes.
     */
    char ch = *pszFolderPath;
    if (ch == '\\' || ch == '/')
        while ((ch = pszFolderPath[1]) == '\\' || ch == '/')
            pszFolderPath++;

    /*
     * Since the KEE unicode conversion routines does not seem to know of
     * surrogate pairs, we will get a very good output size estimate by
     * using strlen() on the input.
     */
    size_t cchSrc = strlen(pszFolderPath);
    PSHFLSTRING pDst = vboxSfOs2StrAlloc(cchSrc + 4 /*fudge*/);
    if (pDst)
    {
        APIRET rc = SafeKernStrToUcs(NULL, &pDst->String.utf16[0], (char *)pszFolderPath, cchSrc + 4, cchSrc);
        if (rc == NO_ERROR)
        {
            pDst->u16Length = (uint16_t)RTUtf16Len(pDst->String.utf16) * (uint16_t)sizeof(RTUTF16);
            Assert(pDst->u16Length < pDst->u16Size);
            pDst->u16Size   = pDst->u16Length + (uint16_t)sizeof(RTUTF16); /* (limit how much is copied to the host) */
            *ppStr = pDst;
            return NO_ERROR;
        }
        VbglR0PhysHeapFree(pDst);

        /*
         * This shouldn't happen, but just in case we try again with twice
         * the buffer size.
         */
        if (rc == 0x20412 /*ULS_BUFFERFULL*/)
        {
            pDst = vboxSfOs2StrAlloc((cchSrc + 16) * 2);
            if (pDst)
            {
                rc = SafeKernStrToUcs(NULL, pDst->String.utf16, (char *)pszFolderPath, (cchSrc + 16) * 2, cchSrc);
                if (rc == NO_ERROR)
                {
                    pDst->u16Length = (uint16_t)RTUtf16Len(pDst->String.utf16) * (uint16_t)sizeof(RTUTF16);
                    Assert(pDst->u16Length < pDst->u16Size);
                    pDst->u16Size   = pDst->u16Length + (uint16_t)sizeof(RTUTF16);
                    *ppStr = pDst;
                    return NO_ERROR;
                }
                VbglR0PhysHeapFree(pDst);
                LogRel(("vboxSfOs2ConvertPath: SafeKernStrToUcs returns %#x for %.*Rhxs\n", rc, cchSrc, pszFolderPath));
            }
        }
        else
            LogRel(("vboxSfOs2ConvertPath: SafeKernStrToUcs returns %#x for %.*Rhxs\n", rc, cchSrc, pszFolderPath));
    }

    LogRel(("vboxSfOs2ConvertPath: Out of memory - cchSrc=%#x\n", cchSrc));
    *ppStr = NULL;
    return ERROR_NOT_ENOUGH_MEMORY;
}


/**
 * Converts a path to UTF-16 and puts it in a VBGL friendly buffer within a
 * larger buffer.
 *
 * @returns OS/2 status code
 * @param   pszFolderPath   The path to convert.
 * @param   offStrInBuf     The offset of the SHLFSTRING in the return buffer.
 *                          This first part of the buffer is zeroed.
 * @param   ppvBuf          Where to return the pointer to the buffer.  Free
 *                          using vboxSfOs2FreePath.
 */
APIRET vboxSfOs2ConvertPathEx(const char *pszFolderPath, uint32_t offStrInBuf, void **ppvBuf)
{
    /*
     * Skip unnecessary leading slashes.
     */
    char ch = *pszFolderPath;
    if (ch == '\\' || ch == '/')
        while ((ch = pszFolderPath[1]) == '\\' || ch == '/')
            pszFolderPath++;

    /*
     * Since the KEE unicode conversion routines does not seem to know of
     * surrogate pairs, we will get a very good output size estimate by
     * using strlen() on the input.
     */
    size_t cchSrc = strlen(pszFolderPath);
    void *pvBuf = VbglR0PhysHeapAlloc(offStrInBuf + SHFLSTRING_HEADER_SIZE + (cchSrc + 4) * sizeof(RTUTF16));
    if (pvBuf)
    {
        RT_BZERO(pvBuf, offStrInBuf);
        PSHFLSTRING pDst = (PSHFLSTRING)((uint8_t *)pvBuf + offStrInBuf);

        APIRET rc = SafeKernStrToUcs(NULL, &pDst->String.utf16[0], (char *)pszFolderPath, cchSrc + 4, cchSrc);
        if (rc == NO_ERROR)
        {
            pDst->u16Length = (uint16_t)RTUtf16Len(pDst->String.utf16) * (uint16_t)sizeof(RTUTF16);
            Assert(pDst->u16Length < (cchSrc + 4) * sizeof(RTUTF16));
            pDst->u16Size   = pDst->u16Length + (uint16_t)sizeof(RTUTF16); /* (limit how much is copied to the host) */
            *ppvBuf = pvBuf;
            return NO_ERROR;
        }
        VbglR0PhysHeapFree(pvBuf);

        /*
         * This shouldn't happen, but just in case we try again with twice
         * the buffer size.
         */
        if (rc == 0x20412 /*ULS_BUFFERFULL*/)
        {
            pvBuf = VbglR0PhysHeapAlloc(offStrInBuf + SHFLSTRING_HEADER_SIZE + (cchSrc + 16) * sizeof(RTUTF16) * 2);
            if (pvBuf)
            {
                RT_BZERO(pvBuf, offStrInBuf);
                pDst = (PSHFLSTRING)((uint8_t *)pvBuf + offStrInBuf);

                rc = SafeKernStrToUcs(NULL, pDst->String.utf16, (char *)pszFolderPath, (cchSrc + 16) * 2, cchSrc);
                if (rc == NO_ERROR)
                {
                    pDst->u16Length = (uint16_t)RTUtf16Len(pDst->String.utf16) * (uint16_t)sizeof(RTUTF16);
                    Assert(pDst->u16Length < (cchSrc + 16) * 2 * sizeof(RTUTF16));
                    pDst->u16Size   = pDst->u16Length + (uint16_t)sizeof(RTUTF16);
                    *ppvBuf = pvBuf;
                    return NO_ERROR;
                }
                VbglR0PhysHeapFree(pDst);
                LogRel(("vboxSfOs2ConvertPath: SafeKernStrToUcs returns %#x for %.*Rhxs\n", rc, cchSrc, pszFolderPath));
            }
        }
        else
            LogRel(("vboxSfOs2ConvertPath: SafeKernStrToUcs returns %#x for %.*Rhxs\n", rc, cchSrc, pszFolderPath));
    }

    LogRel(("vboxSfOs2ConvertPath: Out of memory - cchSrc=%#x offStrInBuf=%#x\n", cchSrc, offStrInBuf));
    *ppvBuf = NULL;
    return ERROR_NOT_ENOUGH_MEMORY;
}


/**
 * Counterpart to vboxSfOs2ResolvePath.
 *
 * @param   pStrPath        The path to free.
 * @param   pFolder         The folder to release.
 */
void vboxSfOs2ReleasePathAndFolder(PSHFLSTRING pStrPath, PVBOXSFFOLDER pFolder)
{
    if (pStrPath)
        VbglR0PhysHeapFree(pStrPath);
    if (pFolder)
    {
        uint32_t cRefs = ASMAtomicDecU32(&pFolder->cRefs);
        Assert(cRefs < _64K);
        if (!cRefs)
            vboxSfOs2DestroyFolder(pFolder);
    }
}


/**
 * Worker for vboxSfOs2ResolvePath() for dynamically mapping folders for UNC
 * paths.
 *
 * @returns OS/2 status code.
 * @param   pachFolderName  The folder to map.  Not necessarily zero terminated
 *                          at the end of the folder name!
 * @param   cchFolderName   The length of the folder name.
 * @param   uRevBefore      The previous folder list revision.
 * @param   ppFolder        Where to return the pointer to the retained folder.
 */
DECL_NO_INLINE(static, int) vboxSfOs2AttachUncAndRetain(const char *pachFolderName, size_t cchFolderName,
                                                        uint32_t uRevBefore, PVBOXSFFOLDER *ppFolder)
{
    KernRequestExclusiveMutex(&g_MtxFolders);

    /*
     * Check if someone raced us to it.
     */
    if (uRevBefore != g_uFolderRevision)
    {
        PVBOXSFFOLDER pFolder = vboxSfOs2FindAndRetainFolder(pachFolderName, cchFolderName);
        if (pFolder)
        {
            KernReleaseExclusiveMutex(&g_MtxFolders);
            *ppFolder = pFolder;
            return NO_ERROR;
        }
    }

    int rc = vboxSfOs2EnsureConnected();
    if (RT_SUCCESS(rc))
    {
        /*
         * Copy the name into the buffer format that Vbgl desires.
         */
        PSHFLSTRING pStrName = vboxSfOs2StrAlloc(cchFolderName);
        if (pStrName)
        {
            memcpy(pStrName->String.ach, pachFolderName, cchFolderName);
            pStrName->String.ach[cchFolderName] = '\0';
            pStrName->u16Length = (uint16_t)cchFolderName;

            /*
             * Do the attaching.
             */
            rc = vboxSfOs2MapFolder(pStrName, NULL, ppFolder);
            vboxSfOs2StrFree(pStrName);
            if (RT_SUCCESS(rc))
            {
                KernReleaseExclusiveMutex(&g_MtxFolders);
                LogRel(("vboxSfOs2AttachUncAndRetain: Successfully attached '%s' (as UNC).\n", (*ppFolder)->szName));
                return NO_ERROR;
            }

            if (rc == VERR_NO_MEMORY)
                rc = ERROR_NOT_ENOUGH_MEMORY;
            else
                rc = ERROR_PATH_NOT_FOUND;
        }
        else
            rc = ERROR_NOT_ENOUGH_MEMORY;
    }
    else
        rc = ERROR_PATH_NOT_FOUND;

    KernReleaseExclusiveMutex(&g_MtxFolders);
    return rc;
}


/**
 * Resolves the given path to a folder structure and folder relative string.
 *
 * @returns OS/2 status code.
 * @param   pszPath         The path to resolve.
 * @param   pCdFsd          The IFS dependent CWD structure if present.
 * @param   offCurDirEnd    The offset into @a pszPath of the CWD.  -1 if not
 *                          CWD relative path.
 * @param   ppFolder        Where to return the referenced pointer to the folder
 *                          structure.  Call vboxSfOs2ReleaseFolder() when done.
 * @param   ppStrFolderPath Where to return a buffer holding the folder relative
 *                          path component.  Free using vboxSfOs2FreePath().
 */
APIRET vboxSfOs2ResolvePath(const char *pszPath, PVBOXSFCD pCdFsd, LONG offCurDirEnd,
                            PVBOXSFFOLDER *ppFolder, PSHFLSTRING *ppStrFolderPath)
{
    APIRET rc;

    /*
     * UNC path?  Reject the prefix to be on the safe side.
     */
    char ch = pszPath[0];
    if (ch == '\\' || ch == '/')
    {
        size_t cchPrefix = vboxSfOs2UncPrefixLength(pszPath);
        if (cchPrefix > 0)
        {
            /* Find the length of the folder name (share). */
            const char *pszFolderName = &pszPath[cchPrefix];
            size_t      cchFolderName = 0;
            while ((ch = pszFolderName[cchFolderName]) != '\0' && ch != '\\' && ch != '/')
            {
                if ((uint8_t)ch >= 0x20 && (uint8_t)ch <= 0x7f && ch != ':')
                    cchFolderName++;
                else
                {
                    LogRel(("vboxSfOs2ResolvePath: Invalid share name (@%u): %.*Rhxs\n",
                            cchPrefix + cchFolderName, strlen(pszPath), pszPath));
                    return ERROR_INVALID_NAME;
                }
            }
            if (cchFolderName >= VBOXSFOS2_MAX_FOLDER_NAME)
            {
                LogRel(("vboxSfOs2ResolvePath: Folder name is too long: %u, max %u (%s)\n",
                        cchFolderName, VBOXSFOS2_MAX_FOLDER_NAME, pszPath));
                return ERROR_FILENAME_EXCED_RANGE;
            }

            /*
             * Look for the share.
             */
            KernRequestSharedMutex(&g_MtxFolders);
            PVBOXSFFOLDER pFolder = *ppFolder = vboxSfOs2FindAndRetainFolder(pszFolderName, cchFolderName);
            if (pFolder)
            {
                vboxSfOs2RetainFolder(pFolder);
                KernReleaseSharedMutex(&g_MtxFolders);
            }
            else
            {
                uint32_t const uRevBefore = g_uFolderRevision;
                KernReleaseSharedMutex(&g_MtxFolders);
                rc = vboxSfOs2AttachUncAndRetain(pszFolderName, cchFolderName, uRevBefore, ppFolder);
                if (rc == NO_ERROR)
                    pFolder = *ppFolder;
                else
                    return rc;
            }

            /*
             * Convert the path and put it in a Vbgl compatible buffer..
             */
            rc = vboxSfOs2ConvertPath(&pszFolderName[cchFolderName], ppStrFolderPath);
            if (rc == NO_ERROR)
                return rc;

            vboxSfOs2ReleaseFolder(pFolder);
            *ppFolder = NULL;
            return rc;
        }

        LogRel(("vboxSfOs2ResolvePath: Unexpected path: %s\n", pszPath));
        return ERROR_PATH_NOT_FOUND;
    }

    /*
     * Drive letter?
     */
    ch &= ~0x20; /* upper case */
    if (   ch >= 'A'
        && ch <= 'Z'
        && pszPath[1] == ':')
    {
        unsigned iDrive = ch - 'A';
        ch  = pszPath[2];
        if (ch == '\\' || ch == '/')
        {
            KernRequestSharedMutex(&g_MtxFolders);
            PVBOXSFFOLDER pFolder = *ppFolder = g_apDriveFolders[iDrive];
            if (pFolder)
            {
                vboxSfOs2RetainFolder(pFolder);
                KernReleaseSharedMutex(&g_MtxFolders);

                /*
                 * Convert the path and put it in a Vbgl compatible buffer..
                 */
                rc = vboxSfOs2ConvertPath(&pszPath[3], ppStrFolderPath);
                if (rc == NO_ERROR)
                    return rc;

                vboxSfOs2ReleaseFolder(pFolder);
                *ppFolder = NULL;
                return rc;
            }
            KernReleaseSharedMutex(&g_MtxFolders);
            LogRel(("vboxSfOs2ResolvePath: No folder mapped on '%s'. Detach race?\n", pszPath));
            return ERROR_PATH_NOT_FOUND;
        }
        LogRel(("vboxSfOs2ResolvePath: No root slash: '%s'\n", pszPath));
        return ERROR_PATH_NOT_FOUND;
    }
    LogRel(("vboxSfOs2ResolvePath: Unexpected path: %s\n", pszPath));
    RT_NOREF_PV(pCdFsd); RT_NOREF_PV(offCurDirEnd);
    return ERROR_PATH_NOT_FOUND;
}


/**
 * Resolves the given path to a folder structure and folder relative string,
 * the latter placed within a larger request buffer.
 *
 * @returns OS/2 status code.
 * @param   pszPath         The path to resolve.
 * @param   pCdFsd          The IFS dependent CWD structure if present.
 * @param   offCurDirEnd    The offset into @a pszPath of the CWD.  -1 if not
 *                          CWD relative path.
 * @param   offStrInBuf     The offset of the SHLFSTRING in the return buffer.
 *                          This first part of the buffer is zeroed.
 * @param   ppFolder        Where to return the referenced pointer to the folder
 *                          structure.  Call vboxSfOs2ReleaseFolder() when done.
 * @param   ppvBuf          Where to return the Pointer to the buffer.  Free
 *                          using VbglR0PhysHeapFree.
 */
APIRET vboxSfOs2ResolvePathEx(const char *pszPath, PVBOXSFCD pCdFsd, LONG offCurDirEnd, uint32_t offStrInBuf,
                              PVBOXSFFOLDER *ppFolder, void **ppvBuf)
{
    APIRET rc;

    /*
     * UNC path?  Reject the prefix to be on the safe side.
     */
    char ch = pszPath[0];
    if (ch == '\\' || ch == '/')
    {
        size_t cchPrefix = vboxSfOs2UncPrefixLength(pszPath);
        if (cchPrefix > 0)
        {
            /* Find the length of the folder name (share). */
            const char *pszFolderName = &pszPath[cchPrefix];
            size_t      cchFolderName = 0;
            while ((ch = pszFolderName[cchFolderName]) != '\0' && ch != '\\' && ch != '/')
            {
                if ((uint8_t)ch >= 0x20 && (uint8_t)ch <= 0x7f && ch != ':')
                    cchFolderName++;
                else
                {
                    LogRel(("vboxSfOs2ResolvePath: Invalid share name (@%u): %.*Rhxs\n",
                            cchPrefix + cchFolderName, strlen(pszPath), pszPath));
                    return ERROR_INVALID_NAME;
                }
            }
            if (cchFolderName >= VBOXSFOS2_MAX_FOLDER_NAME)
            {
                LogRel(("vboxSfOs2ResolvePath: Folder name is too long: %u, max %u (%s)\n",
                        cchFolderName, VBOXSFOS2_MAX_FOLDER_NAME, pszPath));
                return ERROR_FILENAME_EXCED_RANGE;
            }

            /*
             * Look for the share.
             */
            KernRequestSharedMutex(&g_MtxFolders);
            PVBOXSFFOLDER pFolder = *ppFolder = vboxSfOs2FindAndRetainFolder(pszFolderName, cchFolderName);
            if (pFolder)
            {
                vboxSfOs2RetainFolder(pFolder);
                KernReleaseSharedMutex(&g_MtxFolders);
            }
            else
            {
                uint32_t const uRevBefore = g_uFolderRevision;
                KernReleaseSharedMutex(&g_MtxFolders);
                rc = vboxSfOs2AttachUncAndRetain(pszFolderName, cchFolderName, uRevBefore, ppFolder);
                if (rc == NO_ERROR)
                    pFolder = *ppFolder;
                else
                    return rc;
            }

            /*
             * Convert the path and put it in a Vbgl compatible buffer..
             */
            rc = vboxSfOs2ConvertPathEx(&pszFolderName[cchFolderName], offStrInBuf, ppvBuf);
            if (rc == NO_ERROR)
                return rc;

            vboxSfOs2ReleaseFolder(pFolder);
            *ppFolder = NULL;
            return rc;
        }

        LogRel(("vboxSfOs2ResolvePath: Unexpected path: %s\n", pszPath));
        return ERROR_PATH_NOT_FOUND;
    }

    /*
     * Drive letter?
     */
    ch &= ~0x20; /* upper case */
    if (   ch >= 'A'
        && ch <= 'Z'
        && pszPath[1] == ':')
    {
        unsigned iDrive = ch - 'A';
        ch  = pszPath[2];
        if (ch == '\\' || ch == '/')
        {
            KernRequestSharedMutex(&g_MtxFolders);
            PVBOXSFFOLDER pFolder = *ppFolder = g_apDriveFolders[iDrive];
            if (pFolder)
            {
                vboxSfOs2RetainFolder(pFolder);
                KernReleaseSharedMutex(&g_MtxFolders);

                /*
                 * Convert the path and put it in a Vbgl compatible buffer..
                 */
                rc = vboxSfOs2ConvertPathEx(&pszPath[3], offStrInBuf, ppvBuf);
                if (rc == NO_ERROR)
                    return rc;

                vboxSfOs2ReleaseFolder(pFolder);
                *ppFolder = NULL;
                return rc;
            }
            KernReleaseSharedMutex(&g_MtxFolders);
            LogRel(("vboxSfOs2ResolvePath: No folder mapped on '%s'. Detach race?\n", pszPath));
            return ERROR_PATH_NOT_FOUND;
        }
        LogRel(("vboxSfOs2ResolvePath: No root slash: '%s'\n", pszPath));
        return ERROR_PATH_NOT_FOUND;
    }
    LogRel(("vboxSfOs2ResolvePath: Unexpected path: %s\n", pszPath));
    RT_NOREF_PV(pCdFsd); RT_NOREF_PV(offCurDirEnd);
    return ERROR_PATH_NOT_FOUND;
}


DECLASM(void)
FS32_EXIT(ULONG uid, ULONG pid, ULONG pdb)
{
    LogFlow(("FS32_EXIT: uid=%u pid=%u pdb=%#x\n", uid, pid, pdb));
    NOREF(uid); NOREF(pid); NOREF(pdb);
}


DECLASM(APIRET)
FS32_SHUTDOWN(ULONG uType, ULONG uReserved)
{
    LogFlow(("FS32_SHUTDOWN: type=%u uReserved=%u\n", uType, uReserved));
    NOREF(uType); NOREF(uReserved);
    return NO_ERROR;
}


/**
 * FS32_ATTACH worker: FS_ATTACH
 */
static APIRET vboxSfOs2Attach(PCSZ pszDev, PVBOXSFVP pVpFsd, PVBOXSFCD pCdFsd, PBYTE pszParam, PUSHORT pcbParam,
                              PSHFLSTRING *ppCleanup)
{
    /*
     * Check out the parameters, copying the pszParam into a suitable string buffer.
     */
    if (pszDev == NULL || !*pszDev || !RT_C_IS_ALPHA(pszDev[0]) || pszDev[1] != ':' || pszDev[2] != '\0')
    {
        LogRel(("vboxSfOs2Attach: Invalid pszDev value:%p:{%s}\n", pszDev, pszDev));
        return ERROR_INVALID_PARAMETER;
    }
    unsigned const iDrive = (pszDev[0] & ~0x20) - 'A';

    if (pszParam == NULL || pcbParam == NULL)
    {
        LogRel(("vboxSfOs2Attach: NULL parameter buffer or buffer length\n"));
        return ERROR_INVALID_PARAMETER;
    }

    PSHFLSTRING pStrName = *ppCleanup = vboxSfOs2StrAlloc(VBOXSFOS2_MAX_FOLDER_NAME - 1);
    pStrName->u16Length = *pcbParam;
    if (pStrName->u16Length < 1 || pStrName->u16Length > VBOXSFOS2_MAX_FOLDER_NAME)
    {
        LogRel(("vboxSfOs2Attach: Parameter buffer length is out of bounds: %u (min: 1, max " RT_XSTR(VBOXSFOS2_MAX_FOLDER_NAME) ")\n",
                pStrName->u16Length));
        return ERROR_INVALID_PARAMETER;
    }

    int rc = KernCopyIn(pStrName->String.utf8, pszParam, pStrName->u16Length);
    if (rc != NO_ERROR)
        return rc;

    pStrName->u16Length -= 1;
    if (pStrName->String.utf8[pStrName->u16Length] != '\0')
    {
        LogRel(("vboxSfOs2Attach: Parameter not null terminated\n"));
        return ERROR_INVALID_PARAMETER;
    }

    /* Make sure it's only ascii and contains not weird stuff.
       Note! There could be a 2nd tag string, so identify that one. */
    const char *pszTag = NULL;
    unsigned    off = pStrName->u16Length;
    while (off-- > 0)
    {
        char const ch = pStrName->String.utf8[off];
        if (ch < 0x20 || ch >= 0x7f || ch == ':' || ch == '\\' || ch == '/')
        {
            if (ch == '\0' && !pszTag && off + 1 < pStrName->u16Length && off > 0)
            {
                pszTag = &pStrName->String.ach[off + 1];
                pStrName->u16Length = (uint16_t)off;
            }
            else
            {
                LogRel(("vboxSfOs2Attach: Malformed folder name: %.*Rhxs (off %#x)\n", pStrName->u16Length, pStrName->String.utf8, off));
                return ERROR_INVALID_PARAMETER;
            }
        }
    }

    /* Is there a tag following the name? */

    if (!pVpFsd)
    {
        LogRel(("vboxSfOs2Attach: pVpFsd is NULL\n"));
        return ERROR_INVALID_PARAMETER;
    }

    /*
     * Look for the folder to see if we're already using it.  Map it if needed.
     */
    KernRequestExclusiveMutex(&g_MtxFolders);
    if (g_apDriveFolders[iDrive] == NULL)
    {

        PVBOXSFFOLDER pFolder = vboxSfOs2FindAndRetainFolder(pStrName->String.ach, pStrName->u16Length);
        if (!pFolder)
        {
            rc = vboxSfOs2EnsureConnected();
            if (RT_SUCCESS(rc))
                rc = vboxSfOs2MapFolder(pStrName, pszTag, &pFolder);
        }
        if (pFolder && RT_SUCCESS(rc))
        {
            pFolder->cDrives += 1;
            g_apDriveFolders[iDrive] = pFolder;

            pVpFsd->u32Magic = VBOXSFVP_MAGIC;
            pVpFsd->pFolder  = pFolder;

            KernReleaseExclusiveMutex(&g_MtxFolders);

            LogRel(("vboxSfOs2Attach: Successfully attached '%s' to '%s'.\n", pFolder->szName, pszDev));
            return NO_ERROR;
        }

        KernReleaseExclusiveMutex(&g_MtxFolders);
        return ERROR_FILE_NOT_FOUND;
    }
    KernReleaseExclusiveMutex(&g_MtxFolders);

    LogRel(("vboxSfOs2Attach: Already got a folder on '%s'!\n", pszDev));
    RT_NOREF(pCdFsd);
    return ERROR_BUSY_DRIVE;
}


/**
 * FS32_ATTACH worker: FS_DETACH
 */
static APIRET vboxSfOs2Detach(PCSZ pszDev, PVBOXSFVP pVpFsd, PVBOXSFCD pCdFsd, PBYTE pszParam, PUSHORT pcbParam)
{
    /*
     * Validate the volume data and assocated folder.
     */
    AssertPtrReturn(pVpFsd, ERROR_SYS_INTERNAL);
    AssertReturn(pVpFsd->u32Magic == VBOXSFVP_MAGIC, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pVpFsd->pFolder;
    AssertPtrReturn(pFolder, ERROR_SYS_INTERNAL);
    AssertReturn(pFolder->u32Magic == VBOXSFFOLDER_MAGIC, ERROR_SYS_INTERNAL);

    uint8_t idxDrive = UINT8_MAX;
    if (   pszDev
        && RT_C_IS_ALPHA(*pszDev))
        idxDrive = (*pszDev & ~0x20) - 'A';

    /*
     * Can we detach it?
     */
    APIRET rc;
    KernRequestExclusiveMutex(&g_MtxFolders);
    if (   pFolder->cOpenFiles == 0
        && pFolder->cOpenSearches == 0)
    {
        /*
         * Check that we've got the right folder/drive combo.
         */
        if (   idxDrive < RT_ELEMENTS(g_apDriveFolders)
            && g_apDriveFolders[idxDrive] == pFolder)
        {
            g_apDriveFolders[idxDrive] = NULL;
            uint8_t cDrives = --pFolder->cDrives;
            AssertMsg(cDrives < 30, ("%#x\n", cDrives));

            uint32_t cRefs = ASMAtomicDecU32(&pFolder->cRefs);
            AssertMsg(cRefs < _32K, ("%#x\n", cRefs));
            if (cRefs)
            {
                /* If there are now zero drives, unlink it from the list and release
                   the list reference.  This should almost always drop end up with us
                   destroying the folder.*/
                if (cDrives == 0)
                {
                    RTListNodeRemove(&pFolder->ListEntry);
                    cRefs = ASMAtomicDecU32(&pFolder->cRefs);
                    AssertMsg(cRefs < _32K, ("%#x\n", cRefs));
                    if (!cRefs)
                        vboxSfOs2DestroyFolder(pFolder);
                }
            }
            else
            {
                LogRel(("vboxSfOs2Detach: cRefs=0?!?\n"));
                vboxSfOs2DestroyFolder(pFolder);
            }
            rc = NO_ERROR;
        }
        else
        {
            LogRel(("vboxSfOs2Detach: g_apDriveFolders[%#x]=%p pFolder=%p\n",
                    idxDrive, idxDrive < RT_ELEMENTS(g_apDriveFolders) ? g_apDriveFolders[idxDrive] : NULL, pFolder));
            rc = ERROR_NOT_SUPPORTED;
        }
    }
    else
        rc = ERROR_BUSY_DRIVE;
    KernReleaseExclusiveMutex(&g_MtxFolders);

    RT_NOREF(pszDev, pVpFsd, pCdFsd, pszParam, pcbParam);
    return rc;
}


/**
 * FS32_ATTACH worker: FSA_ATTACH_INFO
 */
static APIRET vboxSfOs2QueryAttachInfo(PCSZ pszDev, PVBOXSFVP pVpFsd, PVBOXSFCD pCdFsd, PBYTE pbData, PUSHORT pcbParam)
{
    /*
     * Userland calls the kernel with a FSQBUFFER buffer, the kernel
     * fills in the first part of us and hands us &FSQBUFFER::cbFSAData
     * to do the rest.  We could return the share name here, for instance.
     */
    APIRET rc;
    USHORT cbParam = *pcbParam;
    if (   pszDev == NULL
        || (pszDev[0] != '\\' && pszDev[0] != '/'))
    {
        /* Validate the volume data and assocated folder. */
        AssertPtrReturn(pVpFsd, ERROR_SYS_INTERNAL);
        AssertReturn(pVpFsd->u32Magic == VBOXSFVP_MAGIC, ERROR_SYS_INTERNAL);
        PVBOXSFFOLDER pFolder = pVpFsd->pFolder;
        AssertPtrReturn(pFolder, ERROR_SYS_INTERNAL);
        AssertReturn(pFolder->u32Magic == VBOXSFFOLDER_MAGIC, ERROR_SYS_INTERNAL);

        /* Try copy out the data. */
        if (cbParam >= sizeof(USHORT) + pFolder->cbNameAndTag)
        {
            *pcbParam = (uint16_t)sizeof(USHORT) + pFolder->cbNameAndTag;
            cbParam = pFolder->cchName + 1;
            rc = KernCopyOut(pbData, &cbParam, sizeof(cbParam));
            if (rc == NO_ERROR)
                rc = KernCopyOut(pbData + sizeof(USHORT), pFolder->szName, pFolder->cbNameAndTag);
        }
        else
            rc = ERROR_BUFFER_OVERFLOW;
    }
    else
    {
        /* Looks like a device query, so return zero bytes. */
        if (cbParam >= sizeof(USHORT))
        {
            *pcbParam = sizeof(USHORT);
            cbParam   = 0;
            rc = KernCopyOut(pbData, &cbParam, sizeof(cbParam));
        }
        else
            rc = ERROR_BUFFER_OVERFLOW;
    }

    RT_NOREF(pCdFsd);
    return rc;
}


DECLASM(APIRET)
FS32_ATTACH(ULONG fFlags, PCSZ pszDev, PVBOXSFVP pVpFsd, PVBOXSFCD pCdFsd, PBYTE pszParam, PUSHORT pcbParam)
{
    LogFlow(("FS32_ATTACH: fFlags=%#x  pszDev=%p:{%s} pVpFsd=%p pCdFsd=%p pszParam=%p pcbParam=%p\n",
             fFlags, pszDev, pszDev, pVpFsd, pCdFsd, pszParam, pcbParam));
    APIRET rc;
    if (pVpFsd)
    {
        PSHFLSTRING pCleanup = NULL;

        if (fFlags == FS_ATTACH)
            rc = vboxSfOs2Attach(pszDev, pVpFsd, pCdFsd, pszParam, pcbParam, &pCleanup);
        else if (fFlags == FSA_DETACH)
            rc = vboxSfOs2Detach(pszDev, pVpFsd, pCdFsd, pszParam, pcbParam);
        else if (fFlags == FSA_ATTACH_INFO)
            rc = vboxSfOs2QueryAttachInfo(pszDev, pVpFsd, pCdFsd, pszParam, pcbParam);
        else
        {
            LogRel(("FS32_ATTACH: Unsupported fFlags value: %#x\n", fFlags));
            rc = ERROR_NOT_SUPPORTED;
        }

        if (pCleanup)
            vboxSfOs2StrFree(pCleanup);
    }
    else
        rc = ERROR_NOT_SUPPORTED; /* We don't support device attaching. */
    LogFlow(("FS32_ATTACH: returns %u\n", rc));
    return rc;
}


DECLASM(APIRET)
FS32_VERIFYUNCNAME(ULONG uType, PCSZ pszName)
{
    LogFlow(("FS32_VERIFYUNCNAME: uType=%#x pszName=%p:{%s}\n", uType, pszName, pszName));
    RT_NOREF(uType); /* pass 1 or pass 2 doesn't matter to us, we've only got one 'server'. */

    if (vboxSfOs2UncPrefixLength(pszName) > 0)
        return NO_ERROR;
    return ERROR_NOT_SUPPORTED;
}


DECLASM(APIRET)
FS32_FLUSHBUF(USHORT hVPB, ULONG fFlags)
{
    NOREF(hVPB); NOREF(fFlags);
    return NO_ERROR;
}


DECLASM(APIRET)
FS32_FSINFO(ULONG fFlags, USHORT hVpb, PBYTE pbData, ULONG cbData, ULONG uLevel)
{
    LogFlow(("FS32_FSINFO: fFlags=%#x hVpb=%#x pbData=%p cbData=%#x uLevel=%p\n", fFlags, hVpb, pbData, cbData, uLevel));

    /*
     * Resolve hVpb and do parameter validation.
     */
    PVPFSI pVpFsi = NULL;
    PVBOXSFVP pVpFsd = Fsh32GetVolParams(hVpb, &pVpFsi);
    Log(("FS32_FSINFO: hVpb=%#x -> pVpFsd=%p pVpFsi=%p\n", hVpb, pVpFsd, pVpFsi));

    AssertPtrReturn(pVpFsd, ERROR_SYS_INTERNAL);
    AssertReturn(pVpFsd->u32Magic == VBOXSFVP_MAGIC, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pVpFsd->pFolder;      /** @todo need to retain it behind locks. */
    AssertPtrReturn(pFolder, ERROR_SYS_INTERNAL);
    AssertReturn(pFolder->u32Magic == VBOXSFFOLDER_MAGIC, ERROR_SYS_INTERNAL);

    APIRET rc;

    /*
     * Queries.
     */
    if (fFlags == INFO_RETREIVE)
    {
        /* Check that buffer/level matches up. */
        switch (uLevel)
        {
            case FSIL_ALLOC:
                if (cbData >= sizeof(FSALLOCATE))
                    break;
                LogFlow(("FS32_FSINOF: cbData=%u < sizeof(FSALLOCATE) -> ERROR_BUFFER_OVERFLOW\n", cbData));
                return ERROR_BUFFER_OVERFLOW;

            case FSIL_VOLSER:
                if (cbData >= sizeof(FSINFO))
                    break;
                LogFlow(("FS32_FSINOF: cbData=%u < sizeof(FSINFO) -> ERROR_BUFFER_OVERFLOW\n", cbData));
                return ERROR_BUFFER_OVERFLOW;

            default:
                LogRel(("FS32_FSINFO: Unsupported info level %u!\n", uLevel));
                return ERROR_INVALID_LEVEL;
        }

        /* Work buffer union to keep it to a single allocation and no stack. */
        union FsInfoBufs
        {
            struct
            {
                VBOXSFCREATEREQ Req;
                uint8_t         PathStringSpace[4 * sizeof(RTUTF16)];
            } Open;
            struct
            {
                VBOXSFVOLINFOREQ Req;
                union
                {
                    FSALLOCATE  Alloc;
                    FSINFO      FsInfo;
                };
            } Info;
            VBOXSFCLOSEREQ Close;
        } *pu = (union FsInfoBufs *)VbglR0PhysHeapAlloc(sizeof(*pu));
        if (!pu)
            return ERROR_NOT_ENOUGH_MEMORY;

        /*
         * To get the info we need to open the root of the folder.
         */
        RT_ZERO(pu->Open.Req);
        pu->Open.Req.CreateParms.CreateFlags = SHFL_CF_DIRECTORY   | SHFL_CF_ACT_FAIL_IF_NEW  | SHFL_CF_ACT_OPEN_IF_EXISTS
                                             | SHFL_CF_ACCESS_READ | SHFL_CF_ACCESS_ATTR_READ | SHFL_CF_ACCESS_DENYNONE;
        pu->Open.Req.StrPath.u16Size   = 3 * sizeof(RTUTF16);
        pu->Open.Req.StrPath.u16Length = 2 * sizeof(RTUTF16);
        pu->Open.Req.StrPath.String.utf16[0] = '\\';
        pu->Open.Req.StrPath.String.utf16[1] = '.';
        pu->Open.Req.StrPath.String.utf16[2] = '\0';

        int vrc = VbglR0SfHostReqCreate(pFolder->idHostRoot, &pu->Open.Req);
        LogFlow(("FS32_FSINFO: VbglR0SfHostReqCreate -> %Rrc Result=%d Handle=%#RX64\n",
                 vrc, pu->Open.Req.CreateParms.Result, pu->Open.Req.CreateParms.Handle));
        if (   RT_SUCCESS(vrc)
            && pu->Open.Req.CreateParms.Handle != SHFL_HANDLE_NIL)
        {
            SHFLHANDLE volatile hHandle = pu->Open.Req.CreateParms.Handle;

            RT_ZERO(pu->Info.Req);
            vrc = VbglR0SfHostReqQueryVolInfo(pFolder->idHostRoot, &pu->Info.Req, hHandle);
            if (RT_SUCCESS(vrc))
            {
                /*
                 * Construct and copy out the requested info.
                 */
                if (uLevel == FSIL_ALLOC)
                {
                    pu->Info.Alloc.idFileSystem = 0; /* unknown */
                    uint32_t const cbSector = RT_MAX(pu->Info.Req.VolInfo.ulBytesPerSector, 1);
                    pu->Info.Alloc.cSectorUnit  = pu->Info.Req.VolInfo.ulBytesPerAllocationUnit               / cbSector;
                    pu->Info.Alloc.cUnit        = (uint32_t)(pu->Info.Req.VolInfo.ullTotalAllocationBytes     / cbSector);
                    pu->Info.Alloc.cUnitAvail   = (uint32_t)(pu->Info.Req.VolInfo.ullAvailableAllocationBytes / cbSector);
                    pu->Info.Alloc.cbSector     = (uint16_t)pu->Info.Req.VolInfo.ulBytesPerSector;
                    rc = KernCopyOut(pbData, &pu->Info.Alloc, sizeof(pu->Info.Alloc));
                }
                else
                {
                    RT_ZERO(pu->Info.FsInfo);
                    pu->Info.FsInfo.vol.cch = (uint8_t)RT_MIN(pFolder->cchName, sizeof(pu->Info.FsInfo.vol.szVolLabel) - 1);
                    memcpy(pu->Info.FsInfo.vol.szVolLabel, pFolder->szName, pu->Info.FsInfo.vol.cch);
                    *(uint32_t *)&pu->Info.FsInfo.fdateCreation = pu->Info.Req.VolInfo.ulSerial;
                    rc = KernCopyOut(pbData, &pu->Info.FsInfo, sizeof(pu->Info.FsInfo));
                }
            }
            else
            {
                LogRel(("FS32_FSINFO: VbglR0SfHostReqQueryVolInfo failed: %Rrc\n", vrc));
                rc = ERROR_GEN_FAILURE;
            }

            vrc = VbglR0SfHostReqClose(pFolder->idHostRoot, &pu->Close, hHandle);
            AssertRC(vrc);
        }
        else
            rc = ERROR_GEN_FAILURE;

        VbglR0PhysHeapFree(pu);
    }
    /*
     * We don't allow setting anything.
     */
    else if (fFlags == INFO_SET)
    {
        LogRel(("FS32_FSINFO: Attempting to set volume info (uLevel=%u, cbData=%#x) -> ERROR_ACCESS_DENIED\n", uLevel, cbData));
        rc = ERROR_ACCESS_DENIED;
    }
    else
    {
        LogRel(("FS32_FSINFO: Unknown flags: %#x\n", fFlags));
        rc = ERROR_SYS_INTERNAL;
    }

    LogFlow(("FS32_FSINFO: returns %#x\n", rc));
    return rc;
}


DECLASM(APIRET)
FS32_FSCTL(union argdat *pArgData, ULONG iArgType, ULONG uFunction,
           PVOID pvParm, USHORT cbParm, PUSHORT pcbParmIO,
           PVOID pvData, USHORT cbData, PUSHORT pcbDataIO)
{
    LogFlow(("FS32_FSCTL: pArgData=%p iArgType=%#x uFunction=%#x pvParam=%p cbParam=%#x pcbParmIO=%p pvData=%p cbData=%#x pcbDataIO=%p\n",
             pArgData, iArgType, uFunction, pvParm, cbParm, pcbParmIO, pvData, cbData, pcbDataIO));
    NOREF(pArgData); NOREF(iArgType); NOREF(uFunction); NOREF(pvParm); NOREF(cbParm); NOREF(pcbParmIO);
    NOREF(pvData); NOREF(cbData); NOREF(pcbDataIO);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(APIRET)
FS32_PROCESSNAME(PSZ pszName)
{
    LogFlow(("FS32_PROCESSNAME: '%s'\n", pszName));
    NOREF(pszName);
    return NO_ERROR;
}


DECLASM(APIRET)
FS32_CHDIR(ULONG fFlags, PCDFSI pCdFsi, PVBOXSFCD pCdFsd, PCSZ pszDir, LONG offCurDirEnd)
{
    LogFlow(("FS32_CHDIR: fFlags=%#x pCdFsi=%p:{%#x,%s} pCdFsd=%p pszDir=%p:{%s} offCurDirEnd=%d\n",
             fFlags, pCdFsi, pCdFsi ? pCdFsi->cdi_hVPB : 0xffff, pCdFsi ? pCdFsi->cdi_curdir : "", pCdFsd, pszDir, pszDir, offCurDirEnd));

    /*
     * We do not keep any information about open directory, just verify
     * them before they are CD'ed into and when asked to revalidate them.
     * If there were any path walking benefits, we could consider opening the
     * directory and keeping it open, but there isn't, so we don't do that.
     */
    APIRET rc = NO_ERROR;
    if (   fFlags == CD_EXPLICIT
        || fFlags == CD_VERIFY)
    {
        if (fFlags == CD_VERIFY)
            pszDir = pCdFsi->cdi_curdir;

        PVBOXSFFOLDER       pFolder;
        VBOXSFCREATEREQ    *pReq;
        rc = vboxSfOs2ResolvePathEx(pszDir, pCdFsd, offCurDirEnd, RT_UOFFSETOF(VBOXSFCREATEREQ, StrPath),
                                    &pFolder, (void **)&pReq);
        if (rc == NO_ERROR)
        {
            pReq->CreateParms.CreateFlags = SHFL_CF_LOOKUP;

            int vrc = VbglR0SfHostReqCreate(pFolder->idHostRoot, pReq);
            LogFlow(("FS32_CHDIR: VbglR0SfHostReqCreate -> %Rrc Result=%d fMode=%#x\n",
                     vrc, pReq->CreateParms.Result, pReq->CreateParms.Info.Attr.fMode));
            if (RT_SUCCESS(vrc))
            {
                switch (pReq->CreateParms.Result)
                {
                    case SHFL_FILE_EXISTS:
                        if (RTFS_IS_DIRECTORY(pReq->CreateParms.Info.Attr.fMode))
                            rc = NO_ERROR;
                        else
                            rc = ERROR_ACCESS_DENIED;
                        break;

                    case SHFL_PATH_NOT_FOUND:
                        rc = ERROR_PATH_NOT_FOUND;
                        break;

                    default:
                    case SHFL_FILE_NOT_FOUND:
                        rc = ERROR_FILE_NOT_FOUND;
                        break;
                }
            }
            else
                rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_PATH_NOT_FOUND);
        }

        VbglR0PhysHeapFree(pReq);
        vboxSfOs2ReleaseFolder(pFolder);
    }
    else if (fFlags == CD_FREE)
    {
        /* nothing to do here. */
    }
    else
    {
        LogRel(("FS32_CHDIR: Unexpected fFlags value: %#x\n", fFlags));
        rc = ERROR_NOT_SUPPORTED;
    }

    LogFlow(("FS32_CHDIR: returns %u\n", rc));
    return rc;
}


DECLASM(APIRET)
FS32_MKDIR(PCDFSI pCdFsi, PVBOXSFCD pCdFsd, PCSZ pszDir, LONG offCurDirEnd, PEAOP pEaOp, ULONG fFlags)
{
    LogFlow(("FS32_MKDIR: pCdFsi=%p pCdFsd=%p pszDir=%p:{%s} pEAOp=%p fFlags=%#x\n", pCdFsi, pCdFsd, pszDir, pszDir, offCurDirEnd, pEaOp, fFlags));
    RT_NOREF(fFlags);

    /*
     * We don't do EAs.
     */
    APIRET rc;
    if (pEaOp == NULL)
        rc = NO_ERROR;
    else
        rc = vboxSfOs2CheckEaOpForCreation(pEaOp);
    if (rc == NO_ERROR)
    {
        /*
         * Resolve the path.
         */
        PVBOXSFFOLDER       pFolder;
        VBOXSFCREATEREQ    *pReq;
        rc = vboxSfOs2ResolvePathEx(pszDir, pCdFsd, offCurDirEnd, RT_UOFFSETOF(VBOXSFCREATEREQ, StrPath),
                                    &pFolder, (void **)&pReq);
        if (rc == NO_ERROR)
        {
            /*
             * The silly interface for creating directories amounts an open call that
             * fails if it exists and we get a file handle back that needs closing.  Sigh.
             */
            pReq->CreateParms.CreateFlags = SHFL_CF_DIRECTORY | SHFL_CF_ACT_CREATE_IF_NEW | SHFL_CF_ACT_FAIL_IF_EXISTS
                                          | SHFL_CF_ACCESS_READ | SHFL_CF_ACCESS_DENYNONE;

            int vrc = VbglR0SfHostReqCreate(pFolder->idHostRoot, pReq);
            LogFlow(("FS32_MKDIR: VbglR0SfHostReqCreate -> %Rrc Result=%d fMode=%#x\n",
                     vrc, pReq->CreateParms.Result, pReq->CreateParms.Info.Attr.fMode));
            if (RT_SUCCESS(vrc))
            {
                switch (pReq->CreateParms.Result)
                {
                    case SHFL_FILE_CREATED:
                        if (pReq->CreateParms.Handle != SHFL_HANDLE_NIL)
                        {
                            AssertCompile(RTASSERT_OFFSET_OF(VBOXSFCREATEREQ, CreateParms.Handle) > sizeof(VBOXSFCLOSEREQ)); /* no aliasing issues */
                            vrc = VbglR0SfHostReqClose(pFolder->idHostRoot, (VBOXSFCLOSEREQ *)pReq, pReq->CreateParms.Handle);
                            AssertRC(vrc);
                        }
                        rc = NO_ERROR;
                        break;

                    case SHFL_FILE_EXISTS:
                        rc = ERROR_ACCESS_DENIED;
                        break;

                    case SHFL_PATH_NOT_FOUND:
                        rc = ERROR_PATH_NOT_FOUND;
                        break;

                    default:
                    case SHFL_FILE_NOT_FOUND:
                        rc = ERROR_FILE_NOT_FOUND;
                        break;
                }
            }
            else if (vrc == VERR_ALREADY_EXISTS)
                rc = ERROR_ACCESS_DENIED;
            else
                rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_FILE_NOT_FOUND);

            VbglR0PhysHeapFree(pReq);
            vboxSfOs2ReleaseFolder(pFolder);
        }
    }
    else
        Log(("FS32_MKDIR: EA trouble %p: %u%s\n", pEaOp, rc, rc == ERROR_EAS_NOT_SUPPORTED ? " (ERROR_EAS_NOT_SUPPORTED)" : ""));

    RT_NOREF_PV(pCdFsi);
    LogFlow(("FS32_MMDIR: returns %u\n", rc));
    return rc;
}


DECLASM(APIRET)
FS32_RMDIR(PCDFSI pCdFsi, PVBOXSFCD pCdFsd, PCSZ pszDir, LONG offCurDirEnd)
{
    LogFlow(("FS32_RMDIR: pCdFsi=%p pCdFsd=%p pszDir=%p:{%s} offCurDirEnd=%d\n", pCdFsi, pCdFsd, pszDir, pszDir, offCurDirEnd));

    /*
     * Resolve the path.
     */
    PVBOXSFFOLDER pFolder;
    VBOXSFREMOVEREQ    *pReq;
    APIRET rc = vboxSfOs2ResolvePathEx(pszDir, pCdFsd, offCurDirEnd, RT_UOFFSETOF(VBOXSFREMOVEREQ, StrPath),
                                       &pFolder, (void **)&pReq);
    if (rc == NO_ERROR)
    {
        int vrc = VbglR0SfHostReqRemove(pFolder->idHostRoot, pReq, SHFL_REMOVE_DIR);
        LogFlow(("FS32_RMDIR: VbglR0SfHostReqRemove -> %Rrc\n", vrc));
        if (RT_SUCCESS(vrc))
            rc = NO_ERROR;
        else
            rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_ACCESS_DENIED);

        VbglR0PhysHeapFree(pReq);
        vboxSfOs2ReleaseFolder(pFolder);
    }

    RT_NOREF_PV(pCdFsi);
    LogFlow(("FS32_RMDIR: returns %u\n", rc));
    return rc;
}


DECLASM(APIRET)
FS32_COPY(ULONG fFlags, PCDFSI pCdFsi, PVBOXSFCD pCdFsd, PCSZ pszSrc, LONG offSrcCurDirEnd,
          PCSZ pszDst, LONG offDstCurDirEnd, ULONG uNameType)
{
    LogFlow(("FS32_COPY: fFlags=%#x pCdFsi=%p pCdFsd=%p pszSrc=%p:{%s} offSrcCurDirEnd=%d pszDst=%p:{%s} offDstCurDirEnd=%d uNameType=%#x\n",
             fFlags, pCdFsi, pCdFsd, pszSrc, pszSrc, offSrcCurDirEnd, pszDst, pszDst, offDstCurDirEnd, uNameType));
    NOREF(fFlags); NOREF(pCdFsi); NOREF(pCdFsd); NOREF(pszSrc); NOREF(offSrcCurDirEnd);
    NOREF(pszDst); NOREF(offDstCurDirEnd); NOREF(uNameType);

    /* Let DOSCALL1.DLL do the work for us till we get a host side function for doing this. */
    return ERROR_CANNOT_COPY;
}


DECLASM(APIRET)
FS32_MOVE(PCDFSI pCdFsi, PVBOXSFCD pCdFsd, PCSZ pszSrc, LONG offSrcCurDirEnd, PCSZ pszDst, LONG offDstCurDirEnd, ULONG uNameType)
{
    LogFlow(("FS32_MOVE: pCdFsi=%p pCdFsd=%p pszSrc=%p:{%s} offSrcCurDirEnd=%d pszDst=%p:{%s} offDstcurDirEnd=%d uNameType=%#x\n",
             pCdFsi, pCdFsd, pszSrc, pszSrc, offSrcCurDirEnd, pszDst, pszDst, offDstCurDirEnd, uNameType));

    /*
     * Resolve the source and destination paths and check that they
     * refer to the same folder.
     */
    PVBOXSFFOLDER pSrcFolder;
    PSHFLSTRING   pSrcFolderPath;
    APIRET rc = vboxSfOs2ResolvePath(pszSrc, pCdFsd, offSrcCurDirEnd, &pSrcFolder, &pSrcFolderPath);
    if (rc == NO_ERROR)
    {
        PVBOXSFFOLDER               pDstFolder;
        VBOXSFRENAMEWITHSRCBUFREQ  *pReq;
        rc = vboxSfOs2ResolvePathEx(pszDst, pCdFsd, offDstCurDirEnd, RT_UOFFSETOF(VBOXSFRENAMEWITHSRCBUFREQ, StrDstPath),
                                    &pDstFolder, (void **)&pReq);
        if (rc == NO_ERROR)
        {
            if (pSrcFolder == pDstFolder)
            {
                /*
                 * Do the renaming.
                 * Note! Requires 6.0.0beta2+ or 5.2.24+ host for renaming files.
                 */
                int vrc = VbglR0SfHostReqRenameWithSrcBuf(pSrcFolder->idHostRoot, pReq, pSrcFolderPath,
                                                          SHFL_RENAME_FILE | SHFL_RENAME_DIR);
                if (RT_SUCCESS(vrc))
                    rc = NO_ERROR;
                else
                {
                    Log(("FS32_MOVE: VbglR0SfHostReqRenameWithSrcBuf failed: %Rrc\n", rc));
                    rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_ACCESS_DENIED);
                }
            }
            else
            {
                Log(("FS32_MOVE: source folder '%s' != destiation folder '%s'\n", pszSrc, pszDst));
                rc = ERROR_NOT_SAME_DEVICE;
            }
            VbglR0PhysHeapFree(pReq);
            vboxSfOs2ReleaseFolder(pDstFolder);
        }
        vboxSfOs2ReleasePathAndFolder(pSrcFolderPath, pSrcFolder);
    }

    RT_NOREF_PV(pCdFsi); RT_NOREF_PV(uNameType);
    return rc;
}


DECLASM(APIRET)
FS32_DELETE(PCDFSI pCdFsi, PVBOXSFCD pCdFsd, PCSZ pszFile, LONG offCurDirEnd)
{
    LogFlow(("FS32_DELETE: pCdFsi=%p pCdFsd=%p pszFile=%p:{%s} offCurDirEnd=%d\n", pCdFsi, pCdFsd, pszFile, pszFile, offCurDirEnd));

    /*
     * Resolve the path.
     */
    PVBOXSFFOLDER       pFolder;
    VBOXSFREMOVEREQ    *pReq;
    APIRET rc = vboxSfOs2ResolvePathEx(pszFile, pCdFsd, offCurDirEnd, RT_UOFFSETOF(VBOXSFREMOVEREQ, StrPath),
                                       &pFolder, (void **)&pReq);
    if (rc == NO_ERROR)
    {
        int vrc = VbglR0SfHostReqRemove(pFolder->idHostRoot, pReq, SHFL_REMOVE_FILE);
        LogFlow(("FS32_DELETE: VbglR0SfHostReqRemove -> %Rrc\n", vrc));
        if (RT_SUCCESS(vrc))
            rc = NO_ERROR;
        else
            rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_ACCESS_DENIED);

        VbglR0PhysHeapFree(pReq);
        vboxSfOs2ReleaseFolder(pFolder);
    }

    RT_NOREF_PV(pCdFsi);
    LogFlow(("FS32_DELETE: returns %u\n", rc));
    return rc;
}



/**
 * Worker for FS32_PATHINFO that handles file stat setting.
 *
 * @returns OS/2 status code
 * @param   pFolder             The folder.
 * @param   hHostFile           The host file handle.
 * @param   fAttribs            The attributes to set.
 * @param   pTimestamps         Pointer to the timestamps.  NULL if none should be
 *                              modified.
 * @param   pObjInfoBuf         Buffer to use when setting the attributes (host
 *                              will return current info upon successful
 *                              return).  This must life on the phys heap.
 * @param   offObjInfoInAlloc   Offset of pObjInfoBuf in the phys heap
 *                              allocation where it lives.
 */
APIRET vboxSfOs2SetInfoCommonWorker(PVBOXSFFOLDER pFolder, SHFLHANDLE hHostFile, ULONG fAttribs,
                                    PFILESTATUS pTimestamps, PSHFLFSOBJINFO pObjInfoBuf, uint32_t offObjInfoInAlloc)
{
    /*
     * Validate the data a little and convert it to host speak.
     * When the date part is zero, the timestamp should not be updated.
     */
    RT_ZERO(*pObjInfoBuf);
    uint16_t cDelta = vboxSfOs2GetLocalTimeDelta();

    /** @todo should we validate attributes?   */
    pObjInfoBuf->Attr.fMode = (fAttribs << RTFS_DOS_SHIFT) & RTFS_DOS_MASK_OS2;
    if (pObjInfoBuf->Attr.fMode == 0)
        pObjInfoBuf->Attr.fMode |= RTFS_DOS_NT_NORMAL;

    if (pTimestamps)
    {
        if (   *(uint16_t *)&pTimestamps->fdateCreation   != 0
            && !vboxSfOs2DateTimeToTimeSpec(pTimestamps->fdateCreation,   pTimestamps->ftimeCreation,   cDelta, &pObjInfoBuf->BirthTime))
        {
            LogRel(("vboxSfOs2SetInfoCommonWorker: Bad creation timestamp: %u-%u-%u %u:%u:%u\n",
                    pTimestamps->fdateCreation.year + 1980, pTimestamps->fdateCreation.month, pTimestamps->fdateCreation.day,
                    pTimestamps->ftimeCreation.hours, pTimestamps->ftimeCreation.minutes, pTimestamps->ftimeCreation.twosecs * 2));
            return ERROR_INVALID_PARAMETER;
        }
        if (   *(uint16_t *)&pTimestamps->fdateLastAccess != 0
            && !vboxSfOs2DateTimeToTimeSpec(pTimestamps->fdateLastAccess, pTimestamps->ftimeLastAccess, cDelta, &pObjInfoBuf->AccessTime))
        {
            LogRel(("vboxSfOs2SetInfoCommonWorker: Bad last access timestamp: %u-%u-%u %u:%u:%u\n",
                    pTimestamps->fdateLastAccess.year + 1980, pTimestamps->fdateLastAccess.month, pTimestamps->fdateLastAccess.day,
                    pTimestamps->ftimeLastAccess.hours, pTimestamps->ftimeLastAccess.minutes, pTimestamps->ftimeLastAccess.twosecs * 2));
            return ERROR_INVALID_PARAMETER;
        }
        if (   *(uint16_t *)&pTimestamps->fdateLastWrite  != 0
            && !vboxSfOs2DateTimeToTimeSpec(pTimestamps->fdateLastWrite,  pTimestamps->ftimeLastWrite,  cDelta, &pObjInfoBuf->ModificationTime))
        {
            LogRel(("vboxSfOs2SetInfoCommonWorker: Bad last access timestamp: %u-%u-%u %u:%u:%u\n",
                    pTimestamps->fdateLastWrite.year + 1980, pTimestamps->fdateLastWrite.month, pTimestamps->fdateLastWrite.day,
                    pTimestamps->ftimeLastWrite.hours, pTimestamps->ftimeLastWrite.minutes, pTimestamps->ftimeLastWrite.twosecs * 2));
            return ERROR_INVALID_PARAMETER;
        }
    }

    /*
     * Call the host to do the updating.
     */
    VBOXSFOBJINFOWITHBUFREQ *pReq = (VBOXSFOBJINFOWITHBUFREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
    if (pReq)
    {
        int vrc = VbglR0SfHostReqSetObjInfoWithBuf(pFolder->idHostRoot, pReq, hHostFile, pObjInfoBuf, offObjInfoInAlloc);
        LogFlow(("vboxSfOs2SetFileInfo: VbglR0SfHostReqSetObjInfoWithBuf -> %Rrc\n", vrc));

        VbglR0PhysHeapFree(pReq);
        if (RT_SUCCESS(vrc))
            return NO_ERROR;
        return vboxSfOs2ConvertStatusToOs2(vrc, ERROR_ACCESS_DENIED);
    }
    return ERROR_NOT_ENOUGH_MEMORY;
}


/**
 * Worker for FS32_FILEATTRIBUTE and FS32_PATHINFO that handles setting stuff.
 *
 * @returns OS/2 status code.
 * @param   pFolder             The folder.
 * @param   pReq                Open/create request buffer with path.
 * @param   fAttribs            New file attributes.
 * @param   pTimestamps         New timestamps.  May be NULL.
 */
static APIRET vboxSfOs2SetPathInfoWorker(PVBOXSFFOLDER pFolder, VBOXSFCREATEREQ *pReq, ULONG fAttribs, PFILESTATUS pTimestamps)

{
    /*
     * In order to do anything we need to open the object.
     */
    APIRET rc;
    pReq->CreateParms.CreateFlags = SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW
                                  | SHFL_CF_ACCESS_ATTR_READWRITE | SHFL_CF_ACCESS_DENYNONE | SHFL_CF_ACCESS_NONE;

    int vrc = VbglR0SfHostReqCreate(pFolder->idHostRoot, pReq);
    LogFlow(("vboxSfOs2SetPathInfoWorker: VbglR0SfHostReqCreate -> %Rrc Result=%d Handle=%#RX64 fMode=%#x\n",
             vrc, pReq->CreateParms.Result, pReq->CreateParms.Handle, pReq->CreateParms.Info.Attr.fMode));
    if (   vrc == VERR_IS_A_DIRECTORY
        || (   RT_SUCCESS(vrc)
            && pReq->CreateParms.Handle == SHFL_HANDLE_NIL
            && RTFS_IS_DIRECTORY(pReq->CreateParms.Info.Attr.fMode)))
    {
        RT_ZERO(pReq->CreateParms);
        pReq->CreateParms.CreateFlags = SHFL_CF_DIRECTORY | SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW
                                      | SHFL_CF_ACCESS_ATTR_READWRITE | SHFL_CF_ACCESS_DENYNONE | SHFL_CF_ACCESS_NONE;
        vrc = VbglR0SfHostReqCreate(pFolder->idHostRoot, pReq);
        LogFlow(("vboxSfOs2SetPathInfoWorker: VbglR0SfHostReqCreate#2 -> %Rrc Result=%d Handle=%#RX64 fMode=%#x\n",
                 vrc, pReq->CreateParms.Result, pReq->CreateParms.Handle, pReq->CreateParms.Info.Attr.fMode));
    }
    if (RT_SUCCESS(vrc))
    {
        switch (pReq->CreateParms.Result)
        {
            case SHFL_FILE_EXISTS:
                if (pReq->CreateParms.Handle != SHFL_HANDLE_NIL)
                {
                    /*
                     * Join up with FS32_FILEINFO to do the actual setting.
                     */
                    rc = vboxSfOs2SetInfoCommonWorker(pFolder, pReq->CreateParms.Handle, fAttribs, pTimestamps,
                                                      &pReq->CreateParms.Info, RT_UOFFSETOF(VBOXSFCREATEREQ, CreateParms.Info));

                    AssertCompile(RTASSERT_OFFSET_OF(VBOXSFCREATEREQ, CreateParms.Handle) > sizeof(VBOXSFCLOSEREQ)); /* no aliasing issues */
                    vrc = VbglR0SfHostReqClose(pFolder->idHostRoot, (VBOXSFCLOSEREQ *)pReq, pReq->CreateParms.Handle);
                    AssertRC(vrc);
                }
                else
                {
                    LogRel(("vboxSfOs2SetPathInfoWorker: No handle! fMode=%#x\n", pReq->CreateParms.Info.Attr.fMode));
                    rc = ERROR_SYS_INTERNAL;
                }
                break;

            case SHFL_PATH_NOT_FOUND:
                rc = ERROR_PATH_NOT_FOUND;
                break;

            default:
            case SHFL_FILE_NOT_FOUND:
                rc = ERROR_FILE_NOT_FOUND;
                break;
        }
    }
    else
        rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_FILE_NOT_FOUND);
    return rc;
}


DECLASM(APIRET)
FS32_FILEATTRIBUTE(ULONG fFlags, PCDFSI pCdFsi, PVBOXSFCD pCdFsd, PCSZ pszName, LONG offCurDirEnd, PUSHORT pfAttr)
{
    LogFlow(("FS32_FILEATTRIBUTE: fFlags=%#x pCdFsi=%p:{%#x,%s} pCdFsd=%p pszName=%p:{%s} offCurDirEnd=%d pfAttr=%p={%#x}\n",
             fFlags, pCdFsi, pCdFsi->cdi_hVPB, pCdFsi->cdi_curdir, pCdFsd, pszName, pszName, offCurDirEnd, pfAttr, *pfAttr));
    RT_NOREF(pCdFsi, offCurDirEnd);

    APIRET rc;
    if (   fFlags == FA_RETRIEVE
        || fFlags == FA_SET)
    {
        /* Both setting and querying needs to make a create request. */
        PVBOXSFFOLDER       pFolder;
        VBOXSFCREATEREQ    *pReq;
        rc = vboxSfOs2ResolvePathEx(pszName, pCdFsd, offCurDirEnd, RT_UOFFSETOF(VBOXSFCREATEREQ, StrPath),
                                    &pFolder, (void **)&pReq);
        if (rc == NO_ERROR)
        {
            if (fFlags == FA_RETRIEVE)
            {
                /*
                 * Query it.
                 */
                pReq->CreateParms.CreateFlags = SHFL_CF_LOOKUP;

                int vrc = VbglR0SfHostReqCreate(pFolder->idHostRoot, pReq);
                LogFlow(("FS32_FILEATTRIBUTE: VbglR0SfHostReqCreate -> %Rrc Result=%d fMode=%#x\n",
                         vrc, pReq->CreateParms.Result, pReq->CreateParms.Info.Attr.fMode));
                if (RT_SUCCESS(vrc))
                {
                    switch (pReq->CreateParms.Result)
                    {
                        case SHFL_FILE_EXISTS:
                            *pfAttr = (uint16_t)((pReq->CreateParms.Info.Attr.fMode & RTFS_DOS_MASK_OS2) >> RTFS_DOS_SHIFT);
                            rc = NO_ERROR;
                            break;

                        case SHFL_PATH_NOT_FOUND:
                            rc = ERROR_PATH_NOT_FOUND;
                            break;

                        default:
                        case SHFL_FILE_NOT_FOUND:
                            rc = ERROR_FILE_NOT_FOUND;
                            break;
                    }
                }
                else
                    rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_FILE_NOT_FOUND);
            }
            else
            {
                /*
                 * Set the info.  Join paths with FS32_PATHINFO.
                 */
                rc = vboxSfOs2SetPathInfoWorker(pFolder, pReq, *pfAttr, NULL);
            }
            VbglR0PhysHeapFree(pReq);
            vboxSfOs2ReleaseFolder(pFolder);
        }
    }
    else
    {
        LogRel(("FS32_FILEATTRIBUTE: Unknwon flag value: %#x\n", fFlags));
        rc = ERROR_NOT_SUPPORTED;
    }
    LogFlow(("FS32_FILEATTRIBUTE: returns %u\n", rc));
    return rc;
}


/**
 * Creates an empty full EA list given a GEALIST and info level.
 *
 * @returns OS/2 status code.
 * @param   pEaOp           Kernel copy of the EA request with flattened pointers.
 * @param   uLevel          The info level being queried.
 * @param   cbFullEasLeft   The size of the full EA buffer, ~(ULONG)0 if it
 *                          should be read in from pEaOp->fpFEAList->cbList.
 * @param   pcbWritten      Where to return the length of the resulting list.  Optional.
 * @param   poffError       User buffer address of EAOP.oError for reporting GEALIST issues.
 */
APIRET vboxSfOs2MakeEmptyEaListEx(PEAOP pEaOp, ULONG uLevel, ULONG cbFullEasLeft, uint32_t *pcbWritten, ULONG *poffError)
{
    ULONG  cbDstList;
    APIRET rc;

    /*
     * Levels 8 and 5 are simple.
     */
    if (   pEaOp->fpGEAList == NULL
        || uLevel == FI_LVL_EAS_FULL_8
        || uLevel == FI_LVL_EAS_FULL_5)
    {
        Log2(("vboxSfOs2MakeEmptyEaList: #1\n"));
        cbDstList = RT_UOFFSET_AFTER(FEALIST, cbList);
        rc = NO_ERROR;
    }
    /*
     * For levels 3 and 4 we have to do work when a request list is present.
     */
    else
    {
        ULONG cbGetEasLeft = 0;
        rc = KernCopyIn(&cbGetEasLeft, &pEaOp->fpGEAList->cbList, sizeof(pEaOp->fpGEAList->cbList));
        if (rc == NO_ERROR && cbFullEasLeft == ~(ULONG)0)
            rc = KernCopyIn(&cbFullEasLeft, &pEaOp->fpFEAList->cbList, sizeof(cbFullEasLeft));
        if (   rc == NO_ERROR
            && cbGetEasLeft  >= sizeof(pEaOp->fpGEAList->cbList)
            && cbFullEasLeft >= sizeof(pEaOp->fpFEAList->cbList))
        {
            cbGetEasLeft  -= sizeof(pEaOp->fpGEAList->cbList);
            cbFullEasLeft -= sizeof(pEaOp->fpFEAList->cbList);

            char *pszNameBuf = (char *)RTMemAlloc(256 + 1);
            if (!pszNameBuf)
                return ERROR_NOT_ENOUGH_MEMORY;
            /* Start of no-return zone. */

            uint8_t const *pbSrc = (uint8_t const *)&pEaOp->fpGEAList->list[0]; /* user buffer! */
            uint8_t       *pbDst = (uint8_t       *)&pEaOp->fpFEAList->list[0]; /* user buffer! */
            Log2(("vboxSfOs2MakeEmptyEaList: %p LB %#x -> %p LB %#x...\n", pbSrc, cbGetEasLeft, pbDst, cbFullEasLeft));
            while (cbGetEasLeft > 0)
            {
                /*
                 * pbSrc: GEA: BYTE cbName; char szName[];
                 */
                /* Get name length (we call it cchName instead of cbName since
                   it does not include the zero terminator). */
                uint8_t cchName = 0;
                rc = KernCopyIn(&cchName, pbSrc, sizeof(cchName));
                Log3(("vboxSfOs2MakeEmptyEaList: cchName=%#x rc=%u\n", cchName, rc));
                if (rc != NO_ERROR)
                    break;
                pbSrc++;
                cbGetEasLeft--;
                if (cchName + 1U > cbGetEasLeft)
                {
                    cbDstList = pbSrc - 1 - (uint8_t *)pEaOp->fpGEAList;
                    rc = KernCopyOut(poffError, &cbDstList, sizeof(pEaOp->oError));
                    if (rc == NO_ERROR)
                        rc = ERROR_EA_LIST_INCONSISTENT;
                    Log(("vboxSfOs2MakeEmptyEaList: ERROR_EA_LIST_INCONSISTENT\n"));
                    break;
                }

                /* Copy in name. */
                rc = KernCopyIn(pszNameBuf, pbSrc, cchName + 1);
                if (rc != NO_ERROR)
                    break;
                Log3(("vboxSfOs2MakeEmptyEaList: szName: %.*Rhxs\n", cchName + 1, pszNameBuf));
                if ((char *)memchr(pszNameBuf, '\0', cchName + 1) != &pszNameBuf[cchName])
                {
                    cbDstList = pbSrc - 1 - (uint8_t *)pEaOp->fpGEAList;
                    rc = KernCopyOut(poffError, &cbDstList, sizeof(pEaOp->oError));
                    if (rc == NO_ERROR)
                        rc = ERROR_INVALID_EA_NAME;
                    Log(("vboxSfOs2MakeEmptyEaList: ERROR_INVALID_EA_NAME\n"));
                    break;
                }

                /* Skip input. */
                cbGetEasLeft -= cchName + 1;
                pbSrc        += cchName + 1;

                /*
                 * Construct and emit output.
                 * Note! We should technically skip duplicates here, but who cares...
                 */
                if (cchName > 0)
                {
                    FEA Result;
                    if (sizeof(Result) + cchName + 1 <= cbFullEasLeft)
                        cbFullEasLeft -= sizeof(Result) + cchName + 1;
                    else
                    {
                        Log(("vboxSfOs2MakeEmptyEaList: ERROR_BUFFER_OVERFLOW (%#x vs %#x)\n", sizeof(Result) + cchName + 1, cbFullEasLeft));
                        rc = ERROR_BUFFER_OVERFLOW;
                        break;
                    }

                    Result.fEA     = 0;
                    Result.cbName  = cchName;
                    Result.cbValue = 0;
                    rc = KernCopyOut(pbDst, &Result, sizeof(Result));
                    if (rc != NO_ERROR)
                        break;
                    pbDst += sizeof(Result);

                    rc = KernCopyOut(pbDst, pszNameBuf, cchName + 1);
                    if (rc != NO_ERROR)
                        break;
                    pbDst += cchName + 1;
                }
            } /* (while more GEAs) */

            /* End of no-return zone. */
            RTMemFree(pszNameBuf);

            cbDstList = (uintptr_t)pbDst - (uintptr_t)pEaOp->fpFEAList;
        }
        else
        {
            if (rc == NO_ERROR)
                rc = ERROR_BUFFER_OVERFLOW;
            cbDstList = 0; /* oh, shut up. */
        }

    }

    /* Set the list length. */
    if (rc == NO_ERROR)
        rc = KernCopyOut(&pEaOp->fpFEAList->cbList, &cbDstList, sizeof(pEaOp->fpFEAList->cbList));

    if (pcbWritten)
        *pcbWritten = cbDstList;

    Log(("vboxSfOs2MakeEmptyEaList: return %u (cbDstList=%#x)\n", rc, cbDstList));
    return rc;
}



/**
 * Creates an empty full EA list given a GEALIST and info level.
 *
 * @returns OS/2 status code.
 * @param   pEaOp           The EA request.  User buffer.
 * @param   uLevel          The info level being queried.
 */
DECL_NO_INLINE(RT_NOTHING, APIRET)
vboxSfOs2MakeEmptyEaList(PEAOP pEaOp, ULONG uLevel)
{
    /*
     * Copy the user request into memory, do pointer conversion, and
     * join extended function version.
     */
    EAOP EaOp = { NULL, NULL, 0 };
    APIRET rc = KernCopyIn(&EaOp, pEaOp, sizeof(EaOp));
    if (rc == NO_ERROR)
    {
        Log2(("vboxSfOs2MakeEmptyEaList: #0: %p %p %#x\n", EaOp.fpGEAList, EaOp.fpFEAList, EaOp.oError));
        EaOp.fpFEAList = (PFEALIST)KernSelToFlat((uintptr_t)EaOp.fpFEAList);
        if (   uLevel != FI_LVL_EAS_FULL
            && uLevel != FI_LVL_EAS_FULL_5
            && uLevel != FI_LVL_EAS_FULL_8)
            EaOp.fpGEAList = (PGEALIST)KernSelToFlat((uintptr_t)EaOp.fpGEAList);
        else
            EaOp.fpGEAList = NULL;
        Log2(("vboxSfOs2MakeEmptyEaList: #0b: %p %p\n", EaOp.fpGEAList, EaOp.fpFEAList));

        rc = vboxSfOs2MakeEmptyEaListEx(&EaOp, uLevel, ~(ULONG)0, NULL, &pEaOp->oError);
    }
    return rc;
}


/**
 * Corrects the case of the given path.
 *
 * @returns OS/2 status code
 * @param   pFolder         The folder.
 * @param   pReq            Open/create request buffer with folder path.
 * @param   pszPath         The original path for figuring the drive letter or
 *                          UNC part of the path.
 * @param   pbData          Where to return the data (user address).
 * @param   cbData          The maximum amount of data we can return.
 */
static APIRET vboxSfOs2QueryCorrectCase(PVBOXSFFOLDER pFolder, VBOXSFCREATEREQ *pReq, const char *pszPath,
                                        PBYTE pbData, ULONG cbData)
{
    RT_NOREF(pFolder, pReq);
    APIRET rc;
    size_t cchPath = RTStrNLen(pszPath, CCHMAXPATH + 1);
    if (cchPath <= CCHMAXPATH)
    {
        if (cbData > cchPath)
        {
            /** @todo implement this properly on the host side! */
            rc = KernCopyOut(pbData, pszPath, cbData + 1);
            LogFlow(("vboxSfOs2QueryCorrectCase: returns %u\n", rc));
        }
        else
        {
            LogFlow(("vboxSfOs2QueryCorrectCase: returns %u (ERROR_INSUFFICIENT_BUFFER) - cchPath=%#x cbData=%#x\n",
                     ERROR_INSUFFICIENT_BUFFER, cchPath, cbData));
            rc = ERROR_INSUFFICIENT_BUFFER;
        }
    }
    else
    {
        LogFlow(("vboxSfOs2QueryCorrectCase: returns %u (ERROR_FILENAME_EXCED_RANGE)\n", ERROR_FILENAME_EXCED_RANGE));
        rc = ERROR_FILENAME_EXCED_RANGE;
    }
    return rc;
}


/**
 * Copy out file status info.
 *
 * @returns OS/2 status code.
 * @param   pbDst           User address to put the status info at.
 * @param   cbDst           The size of the structure to produce.
 * @param   uLevel          The info level of the structure to produce.
 * @param   pSrc            The shared folder FS object info source structure.
 * @note    Careful with stack, thus no-inlining.
 */
DECL_NO_INLINE(RT_NOTHING, APIRET)
vboxSfOs2FileStatusFromObjInfo(PBYTE pbDst, ULONG cbDst, ULONG uLevel, SHFLFSOBJINFO const *pSrc)
{
    union
    {
        FILESTATUS      Fst;
        FILESTATUS2     Fst2;
        FILESTATUS3L    Fst3L;
        FILESTATUS4L    Fst4L;
    } uTmp;

    int16_t cMinLocalTimeDelta = vboxSfOs2GetLocalTimeDelta();
    vboxSfOs2DateTimeFromTimeSpec(&uTmp.Fst.fdateCreation,   &uTmp.Fst.ftimeCreation,   pSrc->BirthTime, cMinLocalTimeDelta);
    vboxSfOs2DateTimeFromTimeSpec(&uTmp.Fst.fdateLastAccess, &uTmp.Fst.ftimeLastAccess, pSrc->AccessTime, cMinLocalTimeDelta);
    vboxSfOs2DateTimeFromTimeSpec(&uTmp.Fst.fdateLastWrite,  &uTmp.Fst.ftimeLastWrite,  pSrc->ModificationTime, cMinLocalTimeDelta);
    if (uLevel < FI_LVL_STANDARD_64)
    {
        uTmp.Fst.cbFile       = (uint32_t)RT_MIN(pSrc->cbObject,    UINT32_MAX);
        uTmp.Fst.cbFileAlloc  = (uint32_t)RT_MIN(pSrc->cbAllocated, UINT32_MAX);
        uTmp.Fst.attrFile     = (uint16_t)((pSrc->Attr.fMode & RTFS_DOS_MASK_OS2) >> RTFS_DOS_SHIFT);
        if (uLevel == FI_LVL_STANDARD_EASIZE)
            uTmp.Fst2.cbList = 0;
    }
    else
    {
        uTmp.Fst3L.cbFile      = pSrc->cbObject;
        uTmp.Fst3L.cbFileAlloc = pSrc->cbAllocated;
        uTmp.Fst3L.attrFile    = (pSrc->Attr.fMode & RTFS_DOS_MASK_OS2) >> RTFS_DOS_SHIFT;
        uTmp.Fst4L.cbList      = 0;
    }

    return KernCopyOut(pbDst, &uTmp, cbDst);
}



/**
 * Worker for FS32_PATHINFO that handles file stat queries.
 *
 * @returns OS/2 status code
 * @param   pFolder         The folder.
 * @param   pReq            Open/create request buffer with folder path.
 * @param   uLevel          The information level.
 * @param   pbData          Where to return the data (user address).
 * @param   cbData          The amount of data to produce.
 */
static APIRET vboxSfOs2QueryPathInfo(PVBOXSFFOLDER pFolder, VBOXSFCREATEREQ *pReq, ULONG uLevel, PBYTE pbData, ULONG cbData)
{
    APIRET rc;
    pReq->CreateParms.CreateFlags = SHFL_CF_LOOKUP;

    int vrc = VbglR0SfHostReqCreate(pFolder->idHostRoot, pReq);
    LogFlow(("FS32_PATHINFO: VbglR0SfHostReqCreate -> %Rrc Result=%d fMode=%#x\n",
             vrc, pReq->CreateParms.Result, pReq->CreateParms.Info.Attr.fMode));
    if (RT_SUCCESS(vrc))
    {
        switch (pReq->CreateParms.Result)
        {
            case SHFL_FILE_EXISTS:
                switch (uLevel)
                {
                    /*
                     * Produce the desired file stat data.
                     */
                    case FI_LVL_STANDARD:
                    case FI_LVL_STANDARD_EASIZE:
                    case FI_LVL_STANDARD_64:
                    case FI_LVL_STANDARD_EASIZE_64:
                        rc = vboxSfOs2FileStatusFromObjInfo(pbData, cbData, uLevel, &pReq->CreateParms.Info);
                        break;

                    /*
                     * We don't do EAs and we "just" need to return no-EAs.
                     * However, that's not as easy as you might think.
                     */
                    case FI_LVL_EAS_FROM_LIST:
                    case FI_LVL_EAS_FULL:
                    case FI_LVL_EAS_FULL_5:
                    case FI_LVL_EAS_FULL_8:
                        rc = vboxSfOs2MakeEmptyEaList((PEAOP)pbData, uLevel);
                        break;

                    default:
                        AssertFailed();
                        rc = ERROR_GEN_FAILURE;
                        break;
                }
                break;

            case SHFL_PATH_NOT_FOUND:
                rc = ERROR_PATH_NOT_FOUND;
                break;

            default:
            case SHFL_FILE_NOT_FOUND:
                rc = ERROR_FILE_NOT_FOUND;
                break;
        }
    }
    else
        rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_FILE_NOT_FOUND);
    return rc;
}


DECLASM(APIRET)
FS32_PATHINFO(USHORT fFlags, PCDFSI pCdFsi, PVBOXSFCD pCdFsd, PCSZ pszPath, LONG offCurDirEnd,
              ULONG uLevel, PBYTE pbData, ULONG cbData)
{
    LogFlow(("FS32_PATHINFO: fFlags=%#x pCdFsi=%p:{%#x,%s} pCdFsd=%p pszPath=%p:{%s} offCurDirEnd=%d uLevel=%u pbData=%p cbData=%#x\n",
             fFlags, pCdFsi, pCdFsi->cdi_hVPB, pCdFsi->cdi_curdir, pCdFsd, pszPath, pszPath, offCurDirEnd, uLevel, pbData, cbData));

    /*
     * Check the level.
     *
     * Note! You would think this is FIL_STANDARD, FIL_QUERYEASIZE,
     *       FIL_QUERYEASFROMLISTL and such.  However, there are several levels
     *       (4/14, 6/16, 7/17, 8/18) that are not defined in os2.h and then
     *       there and FIL_QUERYFULLNAME that is used very between the kernel
     *       and the FSD so the kernel can implement DosEnumAttributes.
     *
     * Note! DOSCALL1.DLL has code for converting FILESTATUS to FILESTATUS3
     *       and FILESTATUS2 to FILESTATUS4 as needed.  We don't need to do this.
     *       It also has weird code for doubling the FILESTATUS2.cbList value
     *       for no apparent reason.
     */
    ULONG cbMinData;
    switch (uLevel)
    {
        case FI_LVL_STANDARD:
            cbMinData = sizeof(FILESTATUS);
            AssertCompileSize(FILESTATUS,  0x16);
            break;
        case FI_LVL_STANDARD_64:
            cbMinData = sizeof(FILESTATUS3L);
            AssertCompileSize(FILESTATUS3L, 0x20); /* cbFile and cbFileAlloc are misaligned. */
            break;
        case FI_LVL_STANDARD_EASIZE:
            cbMinData = sizeof(FILESTATUS2);
            AssertCompileSize(FILESTATUS2, 0x1a);
            break;
        case FI_LVL_STANDARD_EASIZE_64:
            cbMinData = sizeof(FILESTATUS4L);
            AssertCompileSize(FILESTATUS4L, 0x24); /* cbFile and cbFileAlloc are misaligned. */
            break;
        case FI_LVL_EAS_FROM_LIST:
        case FI_LVL_EAS_FULL:
        case FI_LVL_EAS_FULL_5:
        case FI_LVL_EAS_FULL_8:
            cbMinData = sizeof(EAOP);
            break;
        case FI_LVL_VERIFY_PATH:
        case FI_LVL_CASE_CORRECT_PATH:
            cbMinData = 1;
            break;
        default:
            LogRel(("FS32_PATHINFO: Unsupported info level %u!\n", uLevel));
            return ERROR_INVALID_LEVEL;
    }
    if (cbData < cbMinData || pbData == NULL)
    {
        Log(("FS32_PATHINFO: ERROR_BUFFER_OVERFLOW (cbMinData=%#x, cbData=%#x, pszPath=%s)\n", cbMinData, cbData, pszPath));
        return ERROR_BUFFER_OVERFLOW;
    }

    /*
     * Resolve the path to a folder and folder path.
     */
    PVBOXSFFOLDER       pFolder;
    VBOXSFCREATEREQ    *pReq;
    int rc = vboxSfOs2ResolvePathEx(pszPath, pCdFsd, offCurDirEnd, RT_UOFFSETOF(VBOXSFCREATEREQ, StrPath),
                                    &pFolder, (void **)&pReq);
    if (rc == NO_ERROR)
    {
        /*
         * Query information.
         */
        if (fFlags == PI_RETRIEVE)
        {
            if (   uLevel != FI_LVL_VERIFY_PATH
                && uLevel != FI_LVL_CASE_CORRECT_PATH)
                rc = vboxSfOs2QueryPathInfo(pFolder, pReq, uLevel, pbData, cbMinData);
            else if (uLevel == FI_LVL_VERIFY_PATH)
                rc = NO_ERROR; /* vboxSfOs2ResolvePath should've taken care of this already */
            else
                rc = vboxSfOs2QueryCorrectCase(pFolder, pReq, pszPath, pbData, cbData);
        }
        /*
         * Update information.
         */
        else if (   fFlags == PI_SET
                 || fFlags == (PI_SET | PI_WRITE_THRU))
        {
            if (   uLevel == FI_LVL_STANDARD
                || uLevel == FI_LVL_STANDARD_64)
            {
                /* Read in the data and join paths with FS32_FILEATTRIBUTE: */
                PFILESTATUS pDataCopy = (PFILESTATUS)VbglR0PhysHeapAlloc(cbMinData);
                if (pDataCopy)
                {
                    rc = KernCopyIn(pDataCopy, pbData, cbMinData);
                    if (rc == NO_ERROR)
                        rc = vboxSfOs2SetPathInfoWorker(pFolder, pReq,
                                                        uLevel == FI_LVL_STANDARD
                                                        ? (ULONG)pDataCopy->attrFile
                                                        : ((PFILESTATUS3L)pDataCopy)->attrFile,
                                                        (PFILESTATUS)pDataCopy);
                    VbglR0PhysHeapFree(pDataCopy);
                }
                else
                    rc = ERROR_NOT_ENOUGH_MEMORY;
            }
            else if (uLevel == FI_LVL_STANDARD_EASIZE)
                rc = ERROR_EAS_NOT_SUPPORTED;
            else
                rc = ERROR_INVALID_LEVEL;
        }
        else
        {
            LogRel(("FS32_PATHINFO: Unknown flags value: %#x (path: %s)\n", fFlags, pszPath));
            rc = ERROR_INVALID_PARAMETER;
        }
        VbglR0PhysHeapFree(pReq);
        vboxSfOs2ReleaseFolder(pFolder);
    }
    RT_NOREF_PV(pCdFsi);
    return rc;
}


DECLASM(APIRET)
FS32_MOUNT(USHORT fFlags, PVPFSI pvpfsi, PVBOXSFVP pVpFsd, USHORT hVPB, PCSZ pszBoot)
{
    NOREF(fFlags); NOREF(pvpfsi); NOREF(pVpFsd); NOREF(hVPB); NOREF(pszBoot);
    return ERROR_NOT_SUPPORTED;
}

