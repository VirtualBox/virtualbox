/** $Id: VBoxSFFind.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSF - OS/2 Shared Folders, Find File IFS EPs.
 */

/*
 * Copyright (c) 2007-2018 knut st. osmundsen <bird-src-spam@anduin.net>
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
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/err.h>



/**
 * Checks if the given name is 8-dot-3 compatible.
 *
 * @returns true if compatible, false if not.
 * @param   pwszName    The name to inspect (UTF-16).
 * @param   cwcName     The length of the name.
 * @param   pszTmp      Buffer for test conversions.
 * @param   cbTmp       The size of the buffer.
 */
static bool vboxSfOs2IsUtf16Name8dot3(PRTUTF16 pwszName, size_t cwcName, char *pszTmp, size_t cbTmp)
{
    /* Reject names that must be too long. */
    if (cwcName > 8 + 1 + 3)
        return false;

    /* First char cannot be a dot, nor can it be an empty string. */
    if (*pwszName == '.' || !*pwszName)
        return false;

    /*
     * To basic checks on code point level before doing full conversion.
     */
    for (unsigned off = 0; ; off++)
    {
        RTUTF16 wc = pwszName[off];
        if (wc == '.')
        {
            unsigned const offMax = off + 3;
            for (++off;; off++)
            {
                wc = pwszName[off];
                if (!wc)
                    break;
                if (wc == '.')
                    return false;
                if (off > offMax)
                    return false;
            }
            break;
        }
        if (!wc)
            break;
        if (off >= 8)
            return false;
    }

    /*
     * Convert to the native code page.
     */
    APIRET rc = SafeKernStrFromUcs(NULL, pszTmp, pwszName, cbTmp, cwcName);
    if (rc != NO_ERROR)
    {
        LogRel(("vboxSfOs2IsUtf8Name8dot3: SafeKernStrFromUcs failed: %d\n", rc));
        return false;
    }

    /*
     * Redo the check.
     * Note! This could be bogus if a DBCS leadin sequence collides with '.'.
     */
    for (unsigned cch = 0; ; cch++)
    {
        char ch = *pszTmp++;
        if (ch == '.')
            break;
        if (ch == '\0')
            return true;
        if (cch >= 8)
            return false;
    }
    for (unsigned cch = 0; ; cch++)
    {
        char ch = *pszTmp++;
        if (ch == '\0')
            return true;
        if (ch == '.')
            return false;
        if (cch >= 3)
            return false;
    }
}


/**
 * @returns Updated pbDst on success, NULL on failure.
 */
static uint8_t *vboxSfOs2CopyUtf16Name(uint8_t *pbDst, PRTUTF16 pwszSrc, size_t cwcSrc)
{
    char *pszDst = (char *)pbDst + 1;
    APIRET rc = SafeKernStrFromUcs(NULL, pszDst, pwszSrc, CCHMAXPATHCOMP, cwcSrc);
    if (rc == NO_ERROR)
    {
        size_t cchDst = strlen(pszDst);
        *pbDst++ = (uint8_t)cchDst;
        pbDst   += cchDst;
        *pbDst++ = '\0';
        return pbDst;
    }
    LogRel(("vboxSfOs2CopyUtf8Name: SafeKernStrFromUcs failed: %d\n", rc));
    return NULL;
}


/**
 * @returns Updated pbDst on success, NULL on failure.
 */
static uint8_t *vboxSfOs2CopyUtf16NameAndUpperCase(uint8_t *pbDst, PRTUTF16 pwszSrc, size_t cwcSrc)
{
    char *pszDst = (char *)(pbDst + 1);
    APIRET rc = SafeKernStrFromUcs(NULL, pszDst, RTUtf16ToUpper(pwszSrc), CCHMAXPATHCOMP, cwcSrc);
    if (rc == NO_ERROR)
    {
        size_t cchDst = strlen(pszDst);
        *pbDst++ = (uint8_t)cchDst;
        pbDst   += cchDst;
        *pbDst++ = '\0';
        return pbDst;
    }
    LogRel(("vboxSfOs2CopyUtf16NameAndUpperCase: SafeKernStrFromUcs failed: %#x\n", rc));
    return NULL;
}



/**
 * Worker for FS32_FINDFIRST, FS32_FINDNEXT and FS32_FINDFROMNAME.
 *
 * @returns OS/2 status code.
 * @param   pFolder     The folder we're working on.
 * @param   pFsFsd      The search handle data.
 * @param   pDataBuf    The search data buffer (some handle data there too).
 * @param   uLevel      The info level to return.
 * @param   fFlags      Position flag.
 * @param   pbData      The output buffer.
 * @param   cbData      The size of the output buffer.
 * @param   cMaxMatches The maximum number of matches to return.
 * @param   pcMatches   Where to set the number of returned matches.
 */
static APIRET vboxSfOs2ReadDirEntries(PVBOXSFFOLDER pFolder, PVBOXSFFS pFsFsd, PVBOXSFFSBUF pDataBuf, ULONG uLevel, ULONG fFlags,
                                      PBYTE pbData, ULONG cbData, USHORT cMaxMatches, PUSHORT pcMatches)
{
    APIRET rc = NO_ERROR;

    /*
     * If we're doing EAs, the buffer starts with an EAOP structure.
     */
    EAOP    EaOp;
    PEAOP   pEaOpUser = NULL; /* Shut up gcc */
    switch (uLevel)
    {
        case FI_LVL_EAS_FROM_LIST:
        case FI_LVL_EAS_FROM_LIST_64:
        case FI_LVL_EAS_FULL:
        case FI_LVL_EAS_FULL_5:
        case FI_LVL_EAS_FULL_8:
            if (cbData >= sizeof(EaOp))
            {
                rc = KernCopyIn(&EaOp, pbData, sizeof(EaOp));
                if (rc == NO_ERROR)
                {
                    EaOp.fpGEAList = (PGEALIST)KernSelToFlat((uintptr_t)EaOp.fpGEAList);
                    EaOp.fpFEAList = NULL;

                    pEaOpUser = (PEAOP)pbData;
                    pbData += sizeof(*pEaOpUser);
                    cbData -= sizeof(*pEaOpUser);
                    break;
                }
            }
            else
                rc = ERROR_BUFFER_OVERFLOW;
            Log(("vboxSfOs2ReadDirEntries: Failed to read EAOP: %u\n", rc));
            return rc;
    }

    /*
     * Do the reading.
     */
    USHORT cMatches;
    for (cMatches = 0; cMatches < cMaxMatches;)
    {
        /*
         * Do we need to fetch more directory entries?
         */
        PSHFLDIRINFO pEntry = pDataBuf->pEntry;
        if (   pDataBuf->cEntriesLeft == 0
            || pEntry == NULL /* paranoia */)
        {
            pDataBuf->pEntry = pEntry = pDataBuf->pBuf;
            int vrc = VbglR0SfHostReqListDir(pFolder->idHostRoot, &pDataBuf->Req, pFsFsd->hHostDir, pDataBuf->pFilter,
                                             /*cMaxMatches == 1 ? SHFL_LIST_RETURN_ONE :*/ 0, pDataBuf->pBuf, pDataBuf->cbBuf);
            if (RT_SUCCESS(vrc))
            {
                pDataBuf->cEntriesLeft = pDataBuf->Req.Parms.c32Entries.u.value32;
                pDataBuf->cbValid      = pDataBuf->Req.Parms.cb32Buffer.u.value32;
                //Log(("%.*Rhxd\n", pDataBuf->cbValid, pEntry));
                AssertReturn(pDataBuf->cbValid >= RT_UOFFSETOF(SHFLDIRINFO, name.String), ERROR_SYS_INTERNAL);
                AssertReturn(pDataBuf->cbValid >= RT_UOFFSETOF(SHFLDIRINFO, name.String) + pEntry->name.u16Size, ERROR_SYS_INTERNAL);
                Log4(("vboxSfOs2ReadDirEntries: VbglR0SfHostReqListDir returned %#x matches in %#x bytes\n", pDataBuf->cEntriesLeft, pDataBuf->cbValid));
            }
            else
            {
                if (vrc == VERR_NO_MORE_FILES)
                    Log4(("vboxSfOs2ReadDirEntries: VbglR0SfHostReqListDir returned VERR_NO_MORE_FILES (%d,%d)\n",
                          pDataBuf->Req.Parms.c32Entries.u.value32, pDataBuf->Req.Parms.cb32Buffer.u.value32));
                else
                    Log(("vboxSfOs2ReadDirEntries: VbglR0SfHostReqListDir failed %Rrc (%d,%d)\n", vrc,
                         pDataBuf->Req.Parms.c32Entries.u.value32, pDataBuf->Req.Parms.cb32Buffer.u.value32));
                pDataBuf->pEntry       = NULL;
                pDataBuf->cEntriesLeft = 0;
                pDataBuf->cbValid      = 0;
                if (cMatches == 0)
                {
                    if (vrc == VERR_NO_MORE_FILES)
                        rc = ERROR_NO_MORE_FILES;
                    else
                        rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_GEN_FAILURE);
                }
                break;
            }
        }

        /*
         * Do matching and stuff the return buffer.
         */
        if (   !((pEntry->Info.Attr.fMode >> RTFS_DOS_SHIFT) & pDataBuf->fExcludedAttribs)
            && ((pEntry->Info.Attr.fMode >> RTFS_DOS_SHIFT) & pDataBuf->fMustHaveAttribs) == pDataBuf->fMustHaveAttribs
            && (   pDataBuf->fLongFilenames
                || pEntry->cucShortName
                || vboxSfOs2IsUtf16Name8dot3(pEntry->name.String.utf16, pEntry->name.u16Length / sizeof(RTUTF16),
                                             (char *)pDataBuf->abStaging, sizeof(pDataBuf->abStaging))))
        {
            /*
             * We stages all but FEAs (level 3, 4, 13 and 14).
             */
            PBYTE const pbUserBufStart = pbData; /* In case we need to skip a bad name. */
            uint8_t    *pbToCopy       = pDataBuf->abStaging;
            uint8_t    *pbDst          = pbToCopy;

            /* Position (originally used for FS32_FINDFROMNAME 'position', but since reused
               for FILEFINDBUF3::oNextEntryOffset and FILEFINDBUF4::oNextEntryOffset): */
            if (fFlags & FF_GETPOS)
            {
                *(uint32_t *)pbDst = pFsFsd->offLastFile + 1;
                pbDst += sizeof(uint32_t);
            }

            /* Dates: Creation, Access, Write */
            vboxSfOs2DateTimeFromTimeSpec((FDATE *)pbDst, (FTIME *)(pbDst + 2), pEntry->Info.BirthTime, pDataBuf->cMinLocalTimeDelta);
            pbDst += sizeof(FDATE) + sizeof(FTIME);
            vboxSfOs2DateTimeFromTimeSpec((FDATE *)pbDst, (FTIME *)(pbDst + 2), pEntry->Info.AccessTime, pDataBuf->cMinLocalTimeDelta);
            pbDst += sizeof(FDATE) + sizeof(FTIME);
            vboxSfOs2DateTimeFromTimeSpec((FDATE *)pbDst, (FTIME *)(pbDst + 2), pEntry->Info.ModificationTime, pDataBuf->cMinLocalTimeDelta);
            pbDst += sizeof(FDATE) + sizeof(FTIME);

            /* File size, allocation size, attributes: */
            if (uLevel >= FI_LVL_STANDARD_64)
            {
                *(uint64_t *)pbDst = pEntry->Info.cbObject;
                pbDst += sizeof(uint64_t);
                *(uint64_t *)pbDst = pEntry->Info.cbAllocated;
                pbDst += sizeof(uint64_t);
                *(uint32_t *)pbDst = (pEntry->Info.Attr.fMode & RTFS_DOS_MASK_OS2) >> RTFS_DOS_SHIFT;
                pbDst += sizeof(uint32_t);
            }
            else
            {
                *(uint32_t *)pbDst = (uint32_t)RT_MIN(pEntry->Info.cbObject, _2G - 1);
                pbDst += sizeof(uint32_t);
                *(uint32_t *)pbDst = (uint32_t)RT_MIN(pEntry->Info.cbAllocated, _2G - 1);
                pbDst += sizeof(uint32_t);
                *(uint16_t *)pbDst = (uint16_t)((pEntry->Info.Attr.fMode & RTFS_DOS_MASK_OS2) >> RTFS_DOS_SHIFT);
                pbDst += sizeof(uint16_t); /* (Curious: Who is expanding this to 32-bits for 32-bit callers? */
            }

            /* Extra EA related fields: */
            if (   uLevel == FI_LVL_STANDARD
                || uLevel == FI_LVL_STANDARD_64)
            { /* nothing */ }
            else if (   uLevel == FI_LVL_STANDARD_EASIZE
                     || uLevel == FI_LVL_STANDARD_EASIZE_64)
            {
                /* EA size: */
                *(uint32_t *)pbDst = 0;
                pbDst += sizeof(uint32_t);
            }
            else
            {
                /* Empty FEALIST - flush pending data first: */
                uint32_t cbToCopy = pbDst - pbToCopy;
                if (cbToCopy < cbData)
                {
                    rc = KernCopyOut(pbData, pbToCopy, cbToCopy);
                    if (rc == NO_ERROR)
                    {
                        pbData += cbToCopy;
                        cbData -= cbToCopy;
                        pbDst   = pbToCopy;

                        /* Output empty EA list.  We don't try anticipate filename output length here,
                           instead we'll just handle that when we come to it below. */
                        /** @todo If this overflows, JFS will return ERROR_EAS_DIDNT_FIT and just the
                         * EA size here (i.e. as if FI_LVL_STANDARD_EASIZE or _64 was requested).
                         * I think, however, that ERROR_EAS_DIDNT_FIT should only be considered if
                         * this is the first entry we're returning and we'll have to stop after it. */
                        uint32_t cbWritten = 0;
                        EaOp.fpFEAList = (PFEALIST)pbData;
                        rc = vboxSfOs2MakeEmptyEaListEx(&EaOp, uLevel, cbData, &cbWritten, &pEaOpUser->oError);
                        if (rc == NO_ERROR)
                        {
                            cbData -= cbWritten;
                            pbData += cbWritten;
                        }
                    }
                }
                else
                    rc = ERROR_BUFFER_OVERFLOW;
                if (rc != NO_ERROR)
                    break;
            }

            /* The length prefixed filename. */
            if (pDataBuf->fLongFilenames)
                pbDst = vboxSfOs2CopyUtf16Name(pbDst, pEntry->name.String.utf16, pEntry->name.u16Length / sizeof(RTUTF16));
            else if (pEntry->cucShortName == 0)
                pbDst = vboxSfOs2CopyUtf16NameAndUpperCase(pbDst, pEntry->name.String.utf16, pEntry->name.u16Length / sizeof(RTUTF16));
            else
                pbDst = vboxSfOs2CopyUtf16NameAndUpperCase(pbDst, pEntry->uszShortName, pEntry->cucShortName);
            if (pbDst)
            {
                /*
                 * Copy out the staged data.
                 */
                uint32_t cbToCopy = pbDst - pbToCopy;
                if (cbToCopy <= cbData)
                {
                    rc = KernCopyOut(pbData, pbToCopy, cbToCopy);
                    if (rc == NO_ERROR)
                    {
                        Log4(("vboxSfOs2ReadDirEntries: match #%u LB %#x: '%s'\n", cMatches, cbToCopy, pEntry->name.String.utf8));
                        Log4(("%.*Rhxd\n", cbToCopy, pbToCopy));

                        pbData += cbToCopy;
                        cbData -= cbToCopy;
                        pbDst   = pbToCopy;

                        cMatches++;
                        pFsFsd->offLastFile++;
                    }
                    else
                        break;
                }
                else
                {
                    rc = ERROR_BUFFER_OVERFLOW;
                    break;
                }
            }
            else
            {
                /* Name conversion issue, just skip the entry. */
                Log3(("vboxSfOs2ReadDirEntries: Skipping '%s' due to name conversion issue.\n", pEntry->name.String.utf8));
                cbData -= pbUserBufStart - pbData;
                pbData  = pbUserBufStart;
            }
        }
        else
            Log3(("vboxSfOs2ReadDirEntries: fMode=%#x filter out by %#x/%#x; '%s'\n",
                  pEntry->Info.Attr.fMode, pDataBuf->fMustHaveAttribs, pDataBuf->fExcludedAttribs, pEntry->name.String.utf8));

        /*
         * Advance to the next directory entry from the host.
         */
        if (pDataBuf->cEntriesLeft-- > 1)
        {
            pDataBuf->pEntry = pEntry = (PSHFLDIRINFO)&pEntry->name.String.utf8[pEntry->name.u16Size];
            uintptr_t offEntry = (uintptr_t)pEntry - (uintptr_t)pDataBuf->pBuf;
            AssertMsgReturn(offEntry + RT_UOFFSETOF(SHFLDIRINFO, name.String) <= pDataBuf->cbValid,
                            ("offEntry=%#x cbValid=%#x\n", offEntry, pDataBuf->cbValid), ERROR_SYS_INTERNAL);
            //Log(("next entry: %p / %#x: u16Size=%#x => size: %#x\n", pEntry, offEntry, RT_UOFFSETOF(SHFLDIRINFO, name.String) + pEntry->name.u16Size));
            AssertMsgReturn(offEntry + RT_UOFFSETOF(SHFLDIRINFO, name.String) + pEntry->name.u16Size <= pDataBuf->cbValid,
                            ("offEntry=%#x + offName=%#x + cbName=%#x => %#x; cbValid=%#x\n",
                             offEntry, RT_UOFFSETOF(SHFLDIRINFO, name.String), pEntry->name.u16Size,
                             offEntry + RT_UOFFSETOF(SHFLDIRINFO, name.String) + pEntry->name.u16Size, pDataBuf->cbValid),
                            ERROR_SYS_INTERNAL);
            //Log(("%.*Rhxd\n", RT_UOFFSETOF(SHFLDIRINFO, name.String) + pEntry->name.u16Size, pEntry));
        }
        else
            pDataBuf->pEntry = pEntry = NULL;
    }

    *pcMatches = cMatches;

    /* Ignore buffer overflows if we've got matches to return. */
    if (rc == ERROR_BUFFER_OVERFLOW && cMatches > 0)
        rc = NO_ERROR;
    return rc;
}


DECLASM(APIRET)
FS32_FINDFIRST(PCDFSI pCdFsi, PVBOXSFCD pCdFsd, PCSZ pszPath, LONG offCurDirEnd, ULONG fAttribs,
               PFSFSI pFsFsi, PVBOXSFFS pFsFsd, PBYTE pbData, ULONG cbData, PUSHORT pcMatches, ULONG uLevel, ULONG fFlags)
{
    LogFlow(("FS32_FINDFIRST: pCdFsi=%p pCdFsd=%p pszPath=%p:{%s} offCurDirEnd=%d fAttribs=%#x pFsFsi=%p pFsFsd=%p pbData=%p cbData=%#x pcMatches=%p:{%#x} uLevel=%#x fFlags=%#x\n",
             pCdFsi, pCdFsd, pszPath, pszPath, offCurDirEnd, fAttribs, pFsFsi, pFsFsd, pbData, cbData, pcMatches, *pcMatches, uLevel, fFlags));
    USHORT const cMaxMatches = *pcMatches;
    *pcMatches = 0;

    /*
     * Input validation.
     */
    switch (uLevel)
    {
        case FI_LVL_STANDARD:
        case FI_LVL_STANDARD_64:
        case FI_LVL_STANDARD_EASIZE:
        case FI_LVL_STANDARD_EASIZE_64:
            break;

        case FI_LVL_EAS_FROM_LIST:
        case FI_LVL_EAS_FROM_LIST_64:
            if (cbData < sizeof(EAOP))
            {
                Log(("FS32_FINDFIRST: Buffer smaller than EAOP: %#x\n", cbData));
                return ERROR_BUFFER_OVERFLOW;
            }
            break;

        default:
            LogRel(("FS32_FINDFIRST: Unsupported info level %u!\n", uLevel));
            return ERROR_INVALID_LEVEL;
    }

    /*
     * Resolve path to a folder and folder relative path.
     */
    RT_NOREF(pCdFsi);
    PVBOXSFFOLDER       pFolder;
    VBOXSFCREATEREQ    *pReq;
    APIRET rc = vboxSfOs2ResolvePathEx(pszPath, pCdFsd, offCurDirEnd, RT_UOFFSETOF(VBOXSFCREATEREQ, StrPath),
                                       &pFolder, (void **)&pReq);
    LogFlow(("FS32_FINDFIRST: vboxSfOs2ResolvePathEx: -> %u pReq=%p\n", rc, pReq));
    if (rc == NO_ERROR)
    {
        PSHFLSTRING pStrFolderPath = &pReq->StrPath;

        /*
         * Look for a wildcard filter at the end of the path, saving it all for
         * later in NT filter speak if present.
         */
        PSHFLSTRING pFilter = NULL;
        PRTUTF16 pwszFilter = RTPathFilenameUtf16(pStrFolderPath->String.utf16);
        if (   pwszFilter
            && (   RTUtf16Chr(pwszFilter, '*') != NULL
                || RTUtf16Chr(pwszFilter, '?') != NULL))
        {
            if (RTUtf16CmpAscii(pwszFilter, "*.*") == 0)
            {
                /* All files, no filtering needed. Just drop the filter expression from the directory path. */
                *pwszFilter = '\0';
                pStrFolderPath->u16Length = (uint16_t)((uint8_t *)pwszFilter - &pStrFolderPath->String.utf8[0]);
            }
            else
            {
                /* Duplicate the whole path. */
                pFilter = vboxSfOs2StrDup(pStrFolderPath);
                if (pFilter)
                {
                    /* Drop filter from directory path. */
                    *pwszFilter = '\0';
                    pStrFolderPath->u16Length = (uint16_t)((uint8_t *)pwszFilter - &pStrFolderPath->String.utf8[0]);

                    /* Convert filter part of the copy to NT speak. */
                    pwszFilter = (PRTUTF16)&pFilter->String.utf8[(uint8_t *)pwszFilter - &pStrFolderPath->String.utf8[0]];
                    for (;;)
                    {
                        RTUTF16 wc = *pwszFilter;
                        if (wc == '?')
                            *pwszFilter = '>';      /* The DOS question mark: Matches one char, but dots and end-of-name eats them. */
                        else if (wc == '.')
                        {
                            RTUTF16 wc2 = pwszFilter[1];
                            if (wc2 == '*' || wc2 == '?')
                                *pwszFilter = '"';  /* The DOS dot: Matches a dot or end-of-name. */
                        }
                        else if (wc == '*')
                        {
                            if (pwszFilter[1] == '.')
                                *pwszFilter = '<';  /* The DOS star: Matches zero or more chars except the DOS dot.*/
                        }
                        else if (wc == '\0')
                            break;
                        pwszFilter++;
                    }
                }
                else
                    rc = ERROR_NOT_ENOUGH_MEMORY;
            }
        }
        /*
         * When no wildcard is specified, we're supposed to return a single entry
         * with the name in the final component.  Exception is the root, where we
         * always list the whole thing.
         *
         * Not sure if we'll ever see a trailing slash here (pszFilter == NULL),
         * but if we do we should accept it only for the root.
         */
        else if (pwszFilter)
        {
            /* Copy the whole path for filtering. */
            pFilter = vboxSfOs2StrDup(pStrFolderPath);
            if (pFilter)
            {
                /* Strip the filename off the one we're opening. */
                pStrFolderPath->u16Length = (uint16_t)((uintptr_t)pwszFilter - (uintptr_t)pStrFolderPath->String.utf16);
                pStrFolderPath->u16Size   = pStrFolderPath->u16Length + (uint16_t)sizeof(RTUTF16);
                pStrFolderPath->String.utf16[pStrFolderPath->u16Length / sizeof(RTUTF16)] = '\0';
            }
            else
                rc = ERROR_NOT_ENOUGH_MEMORY;
        }
        else if (!pwszFilter && pStrFolderPath->u16Length > 1)
        {
            LogFlow(("FS32_FINDFIRST: Trailing slash (%ls)\n", pStrFolderPath->String.utf16));
            rc = ERROR_PATH_NOT_FOUND;
        }
        else
            LogFlow(("FS32_FINDFIRST: Root dir (%ls)\n", pStrFolderPath->String.utf16));

        /*
         * Allocate data/request buffer and another buffer for receiving entries in.
         */
        if (rc == NO_ERROR)
        {
            PVBOXSFFSBUF pDataBuf = (PVBOXSFFSBUF)VbglR0PhysHeapAlloc(sizeof(*pDataBuf));
            if (pDataBuf)
            {
#define MIN_BUF_SIZE (  RT_ALIGN_32(sizeof(SHFLDIRINFO) + CCHMAXPATHCOMP * sizeof(RTUTF16) + 64 /*fudge*/ + ALLOC_HDR_SIZE, 64) \
                      - ALLOC_HDR_SIZE)
                RT_ZERO(*pDataBuf);
                pDataBuf->cbBuf = cMaxMatches == 1 ? MIN_BUF_SIZE : _16K - ALLOC_HDR_SIZE;
                pDataBuf->pBuf  = (PSHFLDIRINFO)VbglR0PhysHeapAlloc(pDataBuf->cbBuf);
                if (pDataBuf->pBuf)
                { /* likely */ }
                else
                {
                    pDataBuf->pBuf = (PSHFLDIRINFO)VbglR0PhysHeapAlloc(MIN_BUF_SIZE);
                    if (pDataBuf->pBuf)
                        pDataBuf->cbBuf = MIN_BUF_SIZE;
                    else
                        rc = ERROR_NOT_ENOUGH_MEMORY;
                }
            }
            if (rc == NO_ERROR)
            {
                /*
                 * Now, try open the directory for reading.
                 */
                pReq->CreateParms.CreateFlags = SHFL_CF_DIRECTORY   | SHFL_CF_ACT_FAIL_IF_NEW  | SHFL_CF_ACT_OPEN_IF_EXISTS
                                              | SHFL_CF_ACCESS_READ | SHFL_CF_ACCESS_ATTR_READ | SHFL_CF_ACCESS_DENYNONE;

                int vrc = VbglR0SfHostReqCreate(pFolder->idHostRoot, pReq);
                LogFlow(("FS32_FINDFIRST: VbglR0SfHostReqCreate(%ls) -> %Rrc Result=%d fMode=%#x hHandle=%#RX64\n",
                         pStrFolderPath->String.utf16, vrc, pReq->CreateParms.Result, pReq->CreateParms.Info.Attr.fMode,
                         pReq->CreateParms.Handle));
                if (RT_SUCCESS(vrc))
                {
                    switch (pReq->CreateParms.Result)
                    {
                        case SHFL_FILE_EXISTS:
                            if (pReq->CreateParms.Handle != SHFL_HANDLE_NIL)
                            {
                                /*
                                 * Initialize the structures.
                                 */
                                pFsFsd->hHostDir            = pReq->CreateParms.Handle;
                                pFsFsd->u32Magic            = VBOXSFFS_MAGIC;
                                pFsFsd->pFolder             = pFolder;
                                pFsFsd->pBuf                = pDataBuf;
                                pFsFsd->offLastFile         = 0;
                                pDataBuf->u32Magic          = VBOXSFFSBUF_MAGIC;
                                pDataBuf->cbValid           = 0;
                                pDataBuf->cEntriesLeft      = 0;
                                pDataBuf->pEntry            = NULL;
                                pDataBuf->pFilter           = pFilter;
                                pDataBuf->fMustHaveAttribs  = (uint8_t)(  (fAttribs >> 8)
                                                                        & (FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_DIRECTORY | FILE_ARCHIVED));
                                pDataBuf->fExcludedAttribs  = (uint8_t)(~fAttribs & (FILE_HIDDEN | FILE_SYSTEM | FILE_DIRECTORY));
                                pDataBuf->fLongFilenames    = RT_BOOL(fAttribs & FF_ATTR_LONG_FILENAME);
                                LogFlow(("FS32_FINDFIRST: fMustHaveAttribs=%#x fExcludedAttribs=%#x fLongFilenames=%d (fAttribs=%#x)\n",
                                         pDataBuf->fMustHaveAttribs, pDataBuf->fExcludedAttribs, pDataBuf->fLongFilenames, fAttribs));
                                pDataBuf->cMinLocalTimeDelta = vboxSfOs2GetLocalTimeDelta();

                                rc = vboxSfOs2ReadDirEntries(pFolder, pFsFsd, pDataBuf, uLevel, fFlags,
                                                             pbData, cbData, cMaxMatches ? cMaxMatches : UINT16_MAX, pcMatches);
                                if (rc != ERROR_BUFFER_OVERFLOW)
                                { /* likely */ }
                                else if (   uLevel == FI_LVL_EAS_FROM_LIST
                                         || uLevel == FI_LVL_EAS_FROM_LIST_64)
                                {
                                    /* If we've got a buffer overflow asking for EAs from a LIST, we are allowed (indeed
                                       expected) to fall back to level 2 (EA size) and return ERROR_EAS_DIDNT_FIT.
                                       See http://www.edm2.com/index.php/DosFindFirst for somewhat dated details. */
                                    rc = vboxSfOs2ReadDirEntries(pFolder, pFsFsd, pDataBuf,
                                                                 uLevel == FI_LVL_EAS_FROM_LIST_64
                                                                 ? FI_LVL_STANDARD_EASIZE_64 : FI_LVL_STANDARD_EASIZE,
                                                                 fFlags, pbData, cbData, 1 /* no more than one! */, pcMatches);
                                    if (rc == NO_ERROR)
                                        rc = ERROR_EAS_DIDNT_FIT;
                                }
                                if (rc == NO_ERROR || rc == ERROR_EAS_DIDNT_FIT)
                                {
                                    uint32_t cRefs = ASMAtomicIncU32(&pFolder->cOpenSearches);
                                    Assert(cRefs < _4K); RT_NOREF(cRefs);

                                    /* We keep these on success: */
                                    if (pFilter == pStrFolderPath)
                                        pStrFolderPath = NULL;
                                    pFilter  = NULL;
                                    pDataBuf = NULL;
                                    pFolder  = NULL;
                                }
                                else
                                {
                                    AssertCompile(sizeof(VBOXSFCLOSEREQ) < sizeof(*pReq));
                                    vrc = VbglR0SfHostReqClose(pFolder->idHostRoot, (VBOXSFCLOSEREQ *)pReq, pFsFsd->hHostDir);
                                    AssertRC(vrc);
                                    pFsFsd->u32Magic = ~VBOXSFFS_MAGIC;
                                    pDataBuf->u32Magic = ~VBOXSFFSBUF_MAGIC;
                                    pFsFsd->pFolder  = NULL;
                                    pFsFsd->hHostDir = NULL;
                                }
                            }
                            else
                            {
                                LogFlow(("FS32_FINDFIRST: VbglR0SfHostReqCreate returns NIL handle for '%ls'\n",
                                         pStrFolderPath->String.utf16));
                                rc = ERROR_PATH_NOT_FOUND;
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
                    rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_GEN_FAILURE);
            }

            if (pDataBuf)
            {
                VbglR0PhysHeapFree(pDataBuf->pBuf);
                pDataBuf->pBuf = NULL;
                VbglR0PhysHeapFree(pDataBuf);
            }
        }
        vboxSfOs2StrFree(pFilter);
        VbglR0PhysHeapFree(pReq);
        vboxSfOs2ReleaseFolder(pFolder);
    }

    RT_NOREF_PV(pFsFsi);
    LogFlow(("FS32_FINDFIRST: returns %u\n", rc));
    return rc;
}


DECLASM(APIRET)
FS32_FINDFROMNAME(PFSFSI pFsFsi, PVBOXSFFS pFsFsd, PBYTE pbData, ULONG cbData, PUSHORT pcMatches,
                  ULONG uLevel, ULONG uPosition, PCSZ pszName, ULONG fFlags)
{
    LogFlow(("FS32_FINDFROMNAME: pFsFsi=%p pFsFsd=%p pbData=%p cbData=%#x pcMatches=%p:{%#x} uLevel=%#x uPosition=%#x pszName=%p:{%s} fFlags=%#x\n",
             pFsFsi, pFsFsd, pbData, cbData, pcMatches, *pcMatches, uLevel, uPosition, pszName, pszName, fFlags));

    /*
     * Input validation.
     */
    USHORT const cMaxMatches = *pcMatches;
    *pcMatches = 0;
    AssertReturn(pFsFsd->u32Magic == VBOXSFFS_MAGIC, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pFsFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenSearches > 0);
    PVBOXSFFSBUF pDataBuf = pFsFsd->pBuf;
    AssertReturn(pDataBuf, ERROR_SYS_INTERNAL);
    Assert(pDataBuf->u32Magic == VBOXSFFSBUF_MAGIC);

    switch (uLevel)
    {
        case FI_LVL_STANDARD:
        case FI_LVL_STANDARD_64:
        case FI_LVL_STANDARD_EASIZE:
        case FI_LVL_STANDARD_EASIZE_64:
        case FI_LVL_EAS_FROM_LIST:
        case FI_LVL_EAS_FROM_LIST_64:
            break;

        default:
            LogRel(("FS32_FINDFROMNAME: Unsupported info level %u!\n", uLevel));
            return ERROR_INVALID_LEVEL;
    }

    /*
     * Check if we're just continuing.  This is usually the case.
     */
    APIRET rc;
    if (uPosition == pFsFsd->offLastFile)
        rc = vboxSfOs2ReadDirEntries(pFolder, pFsFsd, pDataBuf, uLevel, fFlags, pbData, cbData,
                                     cMaxMatches ? cMaxMatches : UINT16_MAX, pcMatches);
    else
    {
        Log(("TODO: uPosition differs: %#x, expected %#x (%s)\n", uPosition, pFsFsd->offLastFile, pszName));
        rc = vboxSfOs2ReadDirEntries(pFolder, pFsFsd, pDataBuf, uLevel, fFlags, pbData, cbData,
                                     cMaxMatches ? cMaxMatches : UINT16_MAX, pcMatches);
    }
    if (rc != ERROR_BUFFER_OVERFLOW)
    { /* likely */ }
    else if (   uLevel == FI_LVL_EAS_FROM_LIST
             || uLevel == FI_LVL_EAS_FROM_LIST_64)
    {
        /* If we've got a buffer overflow asking for EAs from a LIST, we are allowed (indeed
           expected) to fall back to level 2 (EA size) and return ERROR_EAS_DIDNT_FIT. */
        rc = vboxSfOs2ReadDirEntries(pFolder, pFsFsd, pDataBuf,
                                     uLevel == FI_LVL_EAS_FROM_LIST_64 ? FI_LVL_STANDARD_EASIZE_64 : FI_LVL_STANDARD_EASIZE,
                                     fFlags, pbData, cbData, 1 /* no more than one! */, pcMatches);
        if (rc == NO_ERROR)
            rc = ERROR_EAS_DIDNT_FIT;
    }

    RT_NOREF(pFsFsi, pszName);
    LogFlow(("FS32_FINDFROMNAME: returns %u (*pcMatches=%#x)\n", rc, *pcMatches));
    return rc;
}


DECLASM(APIRET)
FS32_FINDNEXT(PFSFSI pFsFsi, PVBOXSFFS pFsFsd, PBYTE pbData, ULONG cbData, PUSHORT pcMatches, ULONG uLevel, ULONG fFlags)
{
    LogFlow(("FS32_FINDNEXT: pFsFsi=%p pFsFsd=%p pbData=%p cbData=%#x pcMatches=%p:{%#x} uLevel=%#x fFlags=%#x\n",
             pFsFsi, pFsFsd, pbData, cbData, pcMatches, *pcMatches, uLevel, fFlags));

    /*
     * Input validation.
     */
    USHORT const cMaxMatches = *pcMatches;
    *pcMatches = 0;
    AssertReturn(pFsFsd->u32Magic == VBOXSFFS_MAGIC, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pFsFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenSearches > 0);
    PVBOXSFFSBUF pDataBuf = pFsFsd->pBuf;
    AssertReturn(pDataBuf, ERROR_SYS_INTERNAL);
    Assert(pDataBuf->u32Magic == VBOXSFFSBUF_MAGIC);

    switch (uLevel)
    {
        case FI_LVL_STANDARD:
        case FI_LVL_STANDARD_64:
        case FI_LVL_STANDARD_EASIZE:
        case FI_LVL_STANDARD_EASIZE_64:
        case FI_LVL_EAS_FROM_LIST:
        case FI_LVL_EAS_FROM_LIST_64:
            break;

        default:
            LogRel(("FS32_FINDNEXT: Unsupported info level %u!\n", uLevel));
            return ERROR_INVALID_LEVEL;
    }

    /*
     * Read more.
     */
    APIRET rc = vboxSfOs2ReadDirEntries(pFolder, pFsFsd, pDataBuf, uLevel, fFlags, pbData, cbData,
                                        cMaxMatches ? cMaxMatches : UINT16_MAX, pcMatches);
    if (rc != ERROR_BUFFER_OVERFLOW)
    { /* likely */ }
    else if (   uLevel == FI_LVL_EAS_FROM_LIST
             || uLevel == FI_LVL_EAS_FROM_LIST_64)
    {
        /* If we've got a buffer overflow asking for EAs from a LIST, we are allowed (indeed
           expected) to fall back to level 2 (EA size) and return ERROR_EAS_DIDNT_FIT.
           See http://www.edm2.com/index.php/DosFindNext for somewhat dated details. */
        rc = vboxSfOs2ReadDirEntries(pFolder, pFsFsd, pDataBuf,
                                     uLevel == FI_LVL_EAS_FROM_LIST_64 ? FI_LVL_STANDARD_EASIZE_64 : FI_LVL_STANDARD_EASIZE,
                                     fFlags, pbData, cbData, 1 /* no more than one! */, pcMatches);
        if (rc == NO_ERROR)
            rc = ERROR_EAS_DIDNT_FIT;
    }

    NOREF(pFsFsi);
    LogFlow(("FS32_FINDNEXT: returns %u (*pcMatches=%#x)\n", rc, *pcMatches));
    return rc;
}


DECLASM(APIRET)
FS32_FINDCLOSE(PFSFSI pFsFsi, PVBOXSFFS pFsFsd)
{
    /*
     * Input validation.
     */
    AssertReturn(pFsFsd->u32Magic == VBOXSFFS_MAGIC, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pFsFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenSearches > 0);
    PVBOXSFFSBUF pDataBuf = pFsFsd->pBuf;
    AssertReturn(pDataBuf, ERROR_SYS_INTERNAL);
    Assert(pDataBuf->u32Magic == VBOXSFFSBUF_MAGIC);

    /*
     * Close it.
     */
    if (pFsFsd->hHostDir != SHFL_HANDLE_NIL)
    {
        int vrc = VbglR0SfHostReqCloseSimple(pFolder->idHostRoot, pFsFsd->hHostDir);
        AssertRC(vrc);
    }

    pFsFsd->u32Magic   = ~VBOXSFFS_MAGIC;
    pFsFsd->hHostDir   = SHFL_HANDLE_NIL;
    pFsFsd->pFolder    = NULL;
    pFsFsd->pBuf       = NULL;
    vboxSfOs2StrFree(pDataBuf->pFilter);
    pDataBuf->pFilter  = NULL;
    pDataBuf->u32Magic = ~VBOXSFFSBUF_MAGIC;
    pDataBuf->cbBuf    = 0;

    VbglR0PhysHeapFree(pDataBuf->pBuf);
    pDataBuf->pBuf = NULL;
    VbglR0PhysHeapFree(pDataBuf);

    uint32_t cRefs = ASMAtomicDecU32(&pFolder->cOpenSearches);
    Assert(cRefs < _4K); RT_NOREF(cRefs);
    vboxSfOs2ReleaseFolder(pFolder);

    RT_NOREF(pFsFsi);
    LogFlow(("FS32_FINDCLOSE: returns NO_ERROR\n"));
    return NO_ERROR;
}





DECLASM(APIRET)
FS32_FINDNOTIFYFIRST(PCDFSI pCdFsi, PVBOXSFCD pCdFsd, PCSZ pszPath, LONG offCurDirEnd, ULONG fAttribs,
                     PUSHORT phHandle, PBYTE pbData, ULONG cbData, PUSHORT pcMatches,
                     ULONG uLevel, ULONG fFlags)
{
    RT_NOREF(pCdFsi, pCdFsd, pszPath, offCurDirEnd, fAttribs, phHandle, pbData, cbData, pcMatches, uLevel, fFlags);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(APIRET)
FS32_FINDNOTIFYNEXT(ULONG hHandle, PBYTE pbData, ULONG cbData, PUSHORT pcMatchs, ULONG uLevel, ULONG cMsTimeout)
{
    RT_NOREF(hHandle, pbData, cbData, pcMatchs, uLevel, cMsTimeout);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(APIRET)
FS32_FINDNOTIFYCLOSE(ULONG hHandle)
{
    NOREF(hHandle);
    return ERROR_NOT_SUPPORTED;
}

