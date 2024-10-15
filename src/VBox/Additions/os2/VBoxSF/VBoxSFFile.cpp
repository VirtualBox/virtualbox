/** $Id: VBoxSFFile.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSF - OS/2 Shared Folders, the file level IFS EPs.
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
#include <iprt/err.h>
#include <iprt/mem.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** A preallocated buffer. */
typedef struct
{
    RTCCPHYS        PhysAddr;
    void           *pvBuf;
    bool volatile   fBusy;
} VBOXSFOS2BUF;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Buffer spinlock. */
static SpinLock_t   g_BufferLock;
/** 64KB buffers. */
static VBOXSFOS2BUF g_aBigBuffers[4];



/**
 * Initialize file buffers.
 */
void vboxSfOs2InitFileBuffers(void)
{
    KernAllocSpinLock(&g_BufferLock);

    for (uint32_t i = 0; i < RT_ELEMENTS(g_aBigBuffers); i++)
    {
        g_aBigBuffers[i].pvBuf = RTMemContAlloc(&g_aBigBuffers[i].PhysAddr, _64K);
        g_aBigBuffers[i].fBusy = g_aBigBuffers[i].pvBuf == NULL;
    }
}


/**
 * Allocates a big buffer.
 * @returns Pointer to buffer on success, NULL on failure.
 * @param   pPhysAddr           The physical address of the buffer.
 */
DECLINLINE(void *) vboxSfOs2AllocBigBuffer(RTGCPHYS *pPhysAddr)
{
    KernAcquireSpinLock(&g_BufferLock);
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aBigBuffers); i++)
        if (!g_aBigBuffers[i].fBusy)
        {
            g_aBigBuffers[i].fBusy = true;
            KernReleaseSpinLock(&g_BufferLock);

            *pPhysAddr = g_aBigBuffers[i].PhysAddr;
            return g_aBigBuffers[i].pvBuf;
        }
    KernReleaseSpinLock(&g_BufferLock);
    *pPhysAddr = NIL_RTGCPHYS;
    return NULL;
}


/**
 * Frees a big buffer.
 * @param   pvBuf               The address of the buffer to be freed.
 */
DECLINLINE(void) vboxSfOs2FreeBigBuffer(void *pvBuf)
{
    Assert(pvBuf);
    KernAcquireSpinLock(&g_BufferLock);
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aBigBuffers); i++)
        if (g_aBigBuffers[i].pvBuf == pvBuf)
        {
            Assert(g_aBigBuffers[i].fBusy);
            g_aBigBuffers[i].fBusy = false;
            KernReleaseSpinLock(&g_BufferLock);
            return;
        }
    KernReleaseSpinLock(&g_BufferLock);
    AssertFailed();
}


/**
 * Checks a EA buffer intended for file or directory creation.
 *
 * @retval  NO_ERROR if empty list.
 * @retval  ERROR_EAS_NOT_SUPPORTED not empty.
 * @retval  ERROR_PROTECTION_VIOLATION if the address is invalid.
 *
 * @param   pEaOp       The EA buffer to check out.
 */
DECL_NO_INLINE(RT_NOTHING, APIRET) vboxSfOs2CheckEaOpForCreation(EAOP const *pEaOp)
{
    EAOP   EaOp = { NULL, NULL, 0 };
    APIRET rc   = KernCopyIn(&EaOp, pEaOp, sizeof(EaOp));
    Log(("vboxSfOs2CheckEasForCreation: %p: rc=%u %#x %#x %#x\n", pEaOp, rc, EaOp.fpFEAList, EaOp.fpGEAList, EaOp.oError));
    if (rc == NO_ERROR)
    {
        EaOp.fpFEAList = (PFEALIST)KernSelToFlat((uintptr_t)EaOp.fpFEAList);
        if (EaOp.fpFEAList)
        {
            FEALIST FeaList = { 0, {0, 0, 0} };
            rc = KernCopyIn(&FeaList, EaOp.fpFEAList, sizeof(FeaList));
            Log(("vboxSfOs2CheckEasForCreation: FeaList %p: rc=%u: %#x {%#x %#x %#x}\n",
                 EaOp.fpFEAList, rc, FeaList.cbList, FeaList.list[0].cbName, FeaList.list[0].cbValue, FeaList.list[0].fEA));
            if (rc != NO_ERROR)
            {
                rc = KernCopyIn(&FeaList, EaOp.fpFEAList, sizeof(FeaList.cbList));
                Log(("vboxSfOs2CheckEasForCreation: FeaList %p: rc=%u: %#x\n", EaOp.fpFEAList, rc, FeaList.cbList));
            }
            if (rc == NO_ERROR && FeaList.cbList > sizeof(FeaList.cbList))
                rc = ERROR_EAS_NOT_SUPPORTED;
        }
    }
    return rc;
}


DECLASM(APIRET)
FS32_OPENCREATE(PCDFSI pCdFsi, PVBOXSFCD pCdFsd, PCSZ pszName, LONG offCurDirEnd,
                PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, ULONG fOpenMode, USHORT fOpenFlags,
                PUSHORT puAction, ULONG fAttribs, EAOP const *pEaOp, PUSHORT pfGenFlag)
{
    LogFlow(("FS32_OPENCREATE: pCdFsi=%p pCdFsd=%p pszName=%p:{%s} offCurDirEnd=%d pSfFsi=%p pSfFsd=%p fOpenMode=%#x fOpenFlags=%#x puAction=%p fAttribs=%#x pEaOp=%p pfGenFlag=%p\n",
             pCdFsi, pCdFsd, pszName, pszName, offCurDirEnd, pSfFsi, pSfFsd, fOpenMode, fOpenFlags, puAction, fAttribs, pEaOp, pfGenFlag));
    RT_NOREF(pfGenFlag, pCdFsi);

    /*
     * Validate and convert parameters.
     */
    /* No EAs. We may need to put in some effort to determin the absense of EAs,
       because CMD.exe likes to supply them when opening the source file of a
       copy operation. */
    if (!pEaOp)
    { /* likely */ }
    else
    {
        switch (fOpenFlags & 0x13)
        {
            case OPEN_ACTION_FAIL_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW:      /* 0x00 */
            case OPEN_ACTION_OPEN_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW:      /* 0x01 */
                LogFlow(("FS32_OPENCREATE: Ignoring EAOP for non-create/replace action (%u).\n",
                         vboxSfOs2CheckEaOpForCreation(pEaOp)));
                break;

            case OPEN_ACTION_FAIL_IF_EXISTS | OPEN_ACTION_CREATE_IF_NEW:    /* 0x10 */
            case OPEN_ACTION_OPEN_IF_EXISTS | OPEN_ACTION_CREATE_IF_NEW:    /* 0x11 */ /** @todo */
            case OPEN_ACTION_REPLACE_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW:   /* 0x02 */
            case OPEN_ACTION_REPLACE_IF_EXISTS | OPEN_ACTION_CREATE_IF_NEW: /* 0x12 */
            {
                APIRET rc = vboxSfOs2CheckEaOpForCreation(pEaOp);
                if (rc == NO_ERROR)
                {
                    Log(("FS32_OPENCREATE: Ignoring empty EAOP.\n"));
                    break;
                }
                Log(("FS32_OPENCREATE: Returns %u%s [%p];\n",
                     rc, rc == ERROR_EAS_NOT_SUPPORTED ? " (ERROR_EAS_NOT_SUPPORTED)" : "", pEaOp));
                return rc;
            }

            default:
                LogRel(("FS32_OPENCREATE: Invalid file open flags: %#x\n", fOpenFlags));
                return VERR_INVALID_PARAMETER;
        }
    }

    /* No direct access. */
    if (!(fOpenMode & OPEN_FLAGS_DASD))
    { /* likely */ }
    else
    {
        LogRel(("FS32_OPENCREATE: Returns ERROR_ACCESS_DENIED [DASD];\n"));
        return ERROR_ACCESS_DENIED;
    }

    /*
     * Allocate request buffer and resovle the path to folder and folder relative path.
     */
    PVBOXSFFOLDER       pFolder;
    VBOXSFCREATEREQ    *pReq;
    APIRET rc = vboxSfOs2ResolvePathEx(pszName, pCdFsd, offCurDirEnd, RT_UOFFSETOF(VBOXSFCREATEREQ, StrPath),
                                       &pFolder, (void **)&pReq);
    LogFlow(("FS32_OPENCREATE: vboxSfOs2ResolvePath: -> %u pFolder=%p\n", rc, pFolder));
    if (rc == NO_ERROR)
    { /* likely */ }
    else
        return rc;

    /*
     * Continue validating and converting parameters.
     */
    /* access: */
    if (fOpenMode & OPEN_ACCESS_READWRITE)
        pReq->CreateParms.CreateFlags = SHFL_CF_ACCESS_READWRITE | SHFL_CF_ACCESS_ATTR_READWRITE;
    else if (fOpenMode & OPEN_ACCESS_WRITEONLY)
        pReq->CreateParms.CreateFlags = SHFL_CF_ACCESS_WRITE     | SHFL_CF_ACCESS_ATTR_WRITE;
    else
        pReq->CreateParms.CreateFlags = SHFL_CF_ACCESS_READ      | SHFL_CF_ACCESS_ATTR_READ; /* read or/and exec */

    /* Sharing: */
    switch (fOpenMode & (OPEN_SHARE_DENYNONE | OPEN_SHARE_DENYREADWRITE | OPEN_SHARE_DENYREAD | OPEN_SHARE_DENYWRITE))
    {
        case OPEN_SHARE_DENYNONE:       pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_DENYNONE; break;
        case OPEN_SHARE_DENYWRITE:      pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_DENYWRITE; break;
        case OPEN_SHARE_DENYREAD:       pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_DENYREAD; break;
        case OPEN_SHARE_DENYREADWRITE:  pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_DENYALL; break;
        case 0:                         pReq->CreateParms.CreateFlags |= SHFL_CF_ACCESS_DENYWRITE; break; /* compatibility */
        default:
            LogRel(("FS32_OPENCREATE: Invalid file sharing mode: %#x\n", fOpenMode));
            VbglR0PhysHeapFree(pReq);
            return VERR_INVALID_PARAMETER;

    }

    /* How to open the file: */
    switch (fOpenFlags & 0x13)
    {
        case                                 OPEN_ACTION_FAIL_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW:      /* 0x00 */
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_FAIL_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW;
            break;
        case                                 OPEN_ACTION_FAIL_IF_EXISTS | OPEN_ACTION_CREATE_IF_NEW:    /* 0x10 */
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_FAIL_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW;
            break;
        case                                 OPEN_ACTION_OPEN_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW:      /* 0x01 */
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW;
            break;
        case                                 OPEN_ACTION_OPEN_IF_EXISTS | OPEN_ACTION_CREATE_IF_NEW:    /* 0x11 */
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW;
            break;
        case                                 OPEN_ACTION_REPLACE_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW:   /* 0x02 */
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_REPLACE_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW;
            break;
        case                                 OPEN_ACTION_REPLACE_IF_EXISTS | OPEN_ACTION_CREATE_IF_NEW: /* 0x12 */
            pReq->CreateParms.CreateFlags |= SHFL_CF_ACT_REPLACE_IF_EXISTS | SHFL_CF_ACT_CREATE_IF_NEW;
            break;
        default:
            LogRel(("FS32_OPENCREATE: Invalid file open flags: %#x\n", fOpenFlags));
            VbglR0PhysHeapFree(pReq);
            return VERR_INVALID_PARAMETER;
    }

    /* Misc: cache, etc? There seems to be no API for that. */

    /* Attributes: */
    pReq->CreateParms.Info.Attr.fMode = ((uint32_t)fAttribs << RTFS_DOS_SHIFT) & RTFS_DOS_MASK_OS2;

    /* Initial size: */
    if (pSfFsi->sfi_sizel > 0)
        pReq->CreateParms.Info.cbObject = pSfFsi->sfi_sizel;

    /*
     * Try open the file.
     */
    int vrc = VbglR0SfHostReqCreate(pFolder->idHostRoot, pReq);
    LogFlow(("FS32_OPENCREATE: VbglR0SfHostReqCreate -> %Rrc Result=%d fMode=%#x\n",
             vrc, pReq->CreateParms.Result, pReq->CreateParms.Info.Attr.fMode));
    if (RT_SUCCESS(vrc))
    {
        switch (pReq->CreateParms.Result)
        {
            case SHFL_FILE_EXISTS:
                if (pReq->CreateParms.Handle == SHFL_HANDLE_NIL)
                {
                    rc = ERROR_OPEN_FAILED; //ERROR_FILE_EXISTS;
                    break;
                }
                if (RTFS_IS_DIRECTORY(pReq->CreateParms.Info.Attr.fMode))
                {
                    LogFlow(("FS32_OPENCREATE: directory, closing and returning ERROR_ACCESS_DENIED!\n"));
                    AssertCompile(RTASSERT_OFFSET_OF(VBOXSFCREATEREQ, CreateParms.Handle) > sizeof(VBOXSFCLOSEREQ)); /* no aliasing issues */
                    VbglR0SfHostReqClose(pFolder->idHostRoot, (VBOXSFCLOSEREQ *)pReq, pReq->CreateParms.Handle);
                    rc = ERROR_ACCESS_DENIED;
                    break;
                }
                RT_FALL_THRU();
            case SHFL_FILE_CREATED:
            case SHFL_FILE_REPLACED:
                if (   pReq->CreateParms.Info.cbObject < _2G
                    || (fOpenMode & OPEN_FLAGS_LARGEFILE))
                {
                    pSfFsd->u32Magic    = VBOXSFSYFI_MAGIC;
                    pSfFsd->pSelf       = pSfFsd;
                    pSfFsd->hHostFile   = pReq->CreateParms.Handle;
                    pSfFsd->pFolder     = pFolder;

                    uint32_t cOpenFiles = ASMAtomicIncU32(&pFolder->cOpenFiles);
                    Assert(cOpenFiles < _32K);
                    pFolder = NULL; /* Reference now taken by pSfFsd->pFolder. */

                    pSfFsi->sfi_sizel   = pReq->CreateParms.Info.cbObject;
                    pSfFsi->sfi_type    = STYPE_FILE;
                    pSfFsi->sfi_DOSattr = (uint8_t)((pReq->CreateParms.Info.Attr.fMode & RTFS_DOS_MASK_OS2) >> RTFS_DOS_SHIFT);
                    int16_t cMinLocalTimeDelta = vboxSfOs2GetLocalTimeDelta();
                    vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_cdate, &pSfFsi->sfi_ctime, pReq->CreateParms.Info.BirthTime,        cMinLocalTimeDelta);
                    vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_adate, &pSfFsi->sfi_atime, pReq->CreateParms.Info.AccessTime,       cMinLocalTimeDelta);
                    vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_mdate, &pSfFsi->sfi_mtime, pReq->CreateParms.Info.ModificationTime, cMinLocalTimeDelta);
                    if (pReq->CreateParms.Result == SHFL_FILE_CREATED)
                        pSfFsi->sfi_tstamp |= ST_PCREAT | ST_SCREAT | ST_PWRITE | ST_SWRITE | ST_PREAD | ST_SREAD;

                    *puAction = pReq->CreateParms.Result == SHFL_FILE_CREATED ? FILE_CREATED
                              : pReq->CreateParms.Result == SHFL_FILE_EXISTS  ? FILE_EXISTED
                              :                                                 FILE_TRUNCATED;

                    Log(("FS32_OPENCREATE: hHandle=%#RX64 for '%s'\n", pSfFsd->hHostFile, pszName));
                    rc = NO_ERROR;
                }
                else
                {
                    LogRel(("FS32_OPENCREATE: cbObject=%#RX64 no OPEN_FLAGS_LARGEFILE (%s)\n", pReq->CreateParms.Info.cbObject, pszName));
                    AssertCompile(RTASSERT_OFFSET_OF(VBOXSFCREATEREQ, CreateParms.Handle) > sizeof(VBOXSFCLOSEREQ)); /* no aliasing issues */
                    VbglR0SfHostReqClose(pFolder->idHostRoot, (VBOXSFCLOSEREQ *)pReq, pReq->CreateParms.Handle);
                    rc = ERROR_ACCESS_DENIED;
                }
                break;

            case SHFL_PATH_NOT_FOUND:
                rc = ERROR_PATH_NOT_FOUND;
                break;

            default:
            case SHFL_FILE_NOT_FOUND:
                rc = ERROR_OPEN_FAILED;
                break;
        }
    }
    else if (vrc == VERR_ALREADY_EXISTS)
        rc = ERROR_ACCESS_DENIED;
    else if (vrc == VERR_FILE_NOT_FOUND)
        rc = ERROR_OPEN_FAILED;
    else
        rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_PATH_NOT_FOUND);
    VbglR0PhysHeapFree(pReq);
    vboxSfOs2ReleaseFolder(pFolder);
    LogFlow(("FS32_OPENCREATE: returns %u\n", rc));
    return rc;
}


DECLASM(APIRET)
FS32_CLOSE(ULONG uType, ULONG fIoFlags, PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd)
{
    LogFlow(("FS32_CLOSE: uType=%#x fIoFlags=%#x pSfFsi=%p pSfFsd=%p:{%#x, %#llx}\n",
             uType, fIoFlags, pSfFsi, pSfFsd, pSfFsd->u32Magic, pSfFsd->hHostFile));

    /*
     * Validate input.
     */
    AssertReturn(pSfFsd->u32Magic == VBOXSFSYFI_MAGIC, ERROR_SYS_INTERNAL);
    AssertReturn(pSfFsd->pSelf    == pSfFsd, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pSfFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenFiles > 0);

    /*
     * We only care for when the system is done truly with the file
     * and we can close it.
     */
    if (uType != FS_CL_FORSYS)
        return NO_ERROR;

    /** @todo flush file if fIoFlags says so? */
    RT_NOREF(fIoFlags);

    int vrc = VbglR0SfHostReqCloseSimple(pFolder->idHostRoot, pSfFsd->hHostFile);
    AssertRC(vrc);

    pSfFsd->hHostFile = SHFL_HANDLE_NIL;
    pSfFsd->pSelf     = NULL;
    pSfFsd->u32Magic  = ~VBOXSFSYFI_MAGIC;
    pSfFsd->pFolder   = NULL;

    ASMAtomicDecU32(&pFolder->cOpenFiles);
    vboxSfOs2ReleaseFolder(pFolder);

    RT_NOREF(pSfFsi);
    LogFlow(("FS32_CLOSE: returns NO_ERROR\n"));
    return NO_ERROR;
}


DECLASM(APIRET)
FS32_COMMIT(ULONG uType, ULONG fIoFlags, PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd)
{
    LogFlow(("FS32_COMMIT: uType=%#x fIoFlags=%#x pSfFsi=%p pSfFsd=%p:{%#x}\n", uType, fIoFlags, pSfFsi, pSfFsd, pSfFsd->u32Magic));

    /*
     * Validate input.
     */
    AssertReturn(pSfFsd->u32Magic == VBOXSFSYFI_MAGIC, ERROR_SYS_INTERNAL);
    AssertReturn(pSfFsd->pSelf    == pSfFsd, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pSfFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenFiles > 0);
    RT_NOREF(pFolder);

    /*
     * We only need to flush writable files.
     */
    if (   (pSfFsi->sfi_mode & SFMODE_OPEN_ACCESS) == SFMODE_OPEN_WRITEONLY
        || (pSfFsi->sfi_mode & SFMODE_OPEN_ACCESS) == SFMODE_OPEN_READWRITE)
    {
        int vrc = VbglR0SfHostReqFlushSimple(pFolder->idHostRoot, pSfFsd->hHostFile);
        if (RT_FAILURE(vrc))
        {
            LogRel(("FS32_COMMIT: VbglR0SfHostReqFlushSimple failed: %Rrc\n", vrc));
            return ERROR_FLUSHBUF_FAILED;
        }
    }

    NOREF(uType); NOREF(fIoFlags); NOREF(pSfFsi);
    LogFlow(("FS32_COMMIT: returns NO_ERROR\n"));
    return NO_ERROR;
}


extern "C" APIRET APIENTRY
FS32_CHGFILEPTRL(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, LONGLONG off, ULONG uMethod, ULONG fIoFlags)
{
    LogFlow(("FS32_CHGFILEPTRL: pSfFsi=%p pSfFsd=%p off=%RI64 (%#RX64) uMethod=%u fIoFlags=%#x\n",
             pSfFsi, pSfFsd, off, off, uMethod, fIoFlags));

    /*
     * Validate input.
     */
    AssertReturn(pSfFsd->u32Magic == VBOXSFSYFI_MAGIC, ERROR_SYS_INTERNAL);
    AssertReturn(pSfFsd->pSelf    == pSfFsd, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pSfFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenFiles > 0);

    /*
     * Calc absolute offset.
     */
    int64_t offNew;
    switch (uMethod)
    {
        case CFP_RELBEGIN:
            if (off >= 0)
            {
                offNew = off;
                break;
            }
            Log(("FS32_CHGFILEPTRL: Negative seek (BEGIN): %RI64\n", off));
            return ERROR_NEGATIVE_SEEK;

        case CFP_RELCUR:
            offNew = pSfFsi->sfi_positionl + off;
            if (offNew >= 0)
                break;
            Log(("FS32_CHGFILEPTRL: Negative seek (RELCUR): %RU64 + %RI64\n", pSfFsi->sfi_positionl, off));
            return ERROR_NEGATIVE_SEEK;

        case CFP_RELEND:
        {
            /* Have to consult the host to get the current file size. */
            VBOXSFOBJINFOREQ *pReq = (VBOXSFOBJINFOREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
            if (pReq)
                RT_ZERO(*pReq);
            else
                return ERROR_NOT_ENOUGH_MEMORY;

            int vrc = VbglR0SfHostReqQueryObjInfo(pFolder->idHostRoot, pReq, pSfFsd->hHostFile);
            if (RT_SUCCESS(vrc))
            {
                if (pSfFsi->sfi_mode & SFMODE_LARGE_FILE)
                    pSfFsi->sfi_sizel = pReq->ObjInfo.cbObject;
                else
                    pSfFsi->sfi_sizel = RT_MIN(pReq->ObjInfo.cbObject, _2G - 1);
            }
            else
                LogRel(("FS32_CHGFILEPTRL/CFP_RELEND: VbglR0SfFsInfo failed: %Rrc\n", vrc));

            VbglR0PhysHeapFree(pReq);

            offNew = pSfFsi->sfi_sizel + off;
            if (offNew >= 0)
                break;
            Log(("FS32_CHGFILEPTRL: Negative seek (CFP_RELEND): %RI64 + %RI64\n", pSfFsi->sfi_sizel, off));
            return ERROR_NEGATIVE_SEEK;
        }


        default:
            LogRel(("FS32_CHGFILEPTRL: Unknown seek method: %#x\n", uMethod));
            return ERROR_INVALID_FUNCTION;
    }

    /*
     * Commit the seek.
     */
    pSfFsi->sfi_positionl = offNew;
    LogFlow(("FS32_CHGFILEPTRL: returns; sfi_positionl=%RI64\n", offNew));
    RT_NOREF_PV(fIoFlags);
    return NO_ERROR;
}


/** Forwards the call to FS32_CHGFILEPTRL. */
DECLASM(APIRET)
FS32_CHGFILEPTR(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, LONG off, ULONG uMethod, ULONG fIoFlags)
{
    return FS32_CHGFILEPTRL(pSfFsi, pSfFsd, off, uMethod, fIoFlags);
}


/**
 * Worker for FS32_PATHINFO that handles file stat setting.
 *
 * @returns OS/2 status code
 * @param   pFolder         The folder.
 * @param   pSfFsi          The file system independent file structure.  We'll
 *                          update the timestamps and size here.
 * @param   pSfFsd          Out file data.
 * @param   uLevel          The information level.
 * @param   pbData          The stat data to set.
 * @param   cbData          The uLevel specific input size.
 */
static APIRET
vboxSfOs2SetFileInfo(PVBOXSFFOLDER pFolder, PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, ULONG uLevel, PBYTE pbData, ULONG cbData)
{
    APIRET rc;

    /*
     * Data buffer both for caching user data and for issuing the
     * change request to the host.
     */
    struct SetFileInfoBuf
    {
        union
        {
            FILESTATUS      Lvl1;
            FILESTATUS3L    Lvl1L;
        };
        SHFLFSOBJINFO ObjInfo;
    } *pBuf = (struct SetFileInfoBuf *)VbglR0PhysHeapAlloc(sizeof(*pBuf));
    if (pBuf)
    {
        /* Copy in the data. */
        rc = KernCopyIn(&pBuf->Lvl1, pbData, cbData);
        if (rc == NO_ERROR)
        {
            /*
             * Join paths with FS32_PATHINFO and FS32_FILEATTRIBUTE.
             */
            rc = vboxSfOs2SetInfoCommonWorker(pFolder, pSfFsd->hHostFile,
                                              uLevel == FI_LVL_STANDARD ? pBuf->Lvl1.attrFile : pBuf->Lvl1L.attrFile,
                                              &pBuf->Lvl1, &pBuf->ObjInfo, RT_UOFFSETOF(struct SetFileInfoBuf, ObjInfo));
            if (rc == NO_ERROR)
            {
                /*
                 * Update the timestamps in the independent file data with what
                 * the host returned:
                 */
                pSfFsi->sfi_tstamp |= ST_PCREAT | ST_PWRITE | ST_PREAD;
                pSfFsi->sfi_tstamp &= ~(ST_SCREAT | ST_SWRITE| ST_SREAD);
                uint16_t cDelta = vboxSfOs2GetLocalTimeDelta();
                vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_cdate, &pSfFsi->sfi_ctime, pBuf->ObjInfo.BirthTime,        cDelta);
                vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_adate, &pSfFsi->sfi_atime, pBuf->ObjInfo.AccessTime,       cDelta);
                vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_mdate, &pSfFsi->sfi_mtime, pBuf->ObjInfo.ModificationTime, cDelta);

                /* And the size field as we're at it: */
                pSfFsi->sfi_sizel = pBuf->ObjInfo.cbObject;
            }
            else
                rc = ERROR_INVALID_PARAMETER;
        }

        VbglR0PhysHeapFree(pBuf);
    }
    else
        rc = ERROR_NOT_ENOUGH_MEMORY;
    return rc;
}


#if 0

DECLVBGL(int) VbglR0SfFastPhysFsInfo(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile,
                                     uint32_t flags, uint32_t *pcbBuffer, PSHFLDIRINFO pBuffer)
{
    struct FsInfoReq
    {
        VBGLIOCIDCHGCMFASTCALL  Hdr;
        VMMDevHGCMCall          Call;
        VBoxSFParmInformation   Parms;
        HGCMPageListInfo        PgLst;
        RTGCPHYS64              PageTwo;
    } *pReq;
    AssertCompileMemberOffset(struct FsInfoReq, Call, 52);
    AssertCompileMemberOffset(struct FsInfoReq, Parms, 0x60);

    pReq = (struct FsInfoReq *)VbglR0PhysHeapAlloc(sizeof(*pReq));
    if (!pReq)
        return VERR_NO_MEMORY;

    VBGLIOCIDCHGCMFASTCALL_INIT(&pReq->Hdr, VbglR0PhysHeapGetPhysAddr(pReq), &pReq->Call, pClient->idClient,
                                SHFL_FN_INFORMATION, SHFL_CPARMS_INFORMATION, sizeof(*pReq));
#if 0
    VBGLREQHDR_INIT_EX(&pReq->Hdr.Hdr, sizeof(*pReq), sizeof(*pReq));
    pReq->Hdr.GCPhysReq      = VbglR0PhysHeapGetPhysAddr(pReq) + sizeof(pReq->Hdr);
    pReq->Hdr.fInterruptible = false;

    pReq->Call.header.header.size       = sizeof(*pReq) - sizeof(pReq->Hdr);
    pReq->Call.header.header.version    = VBGLREQHDR_VERSION;
    pReq->Call.header.header.requestType= VMMDevReq_HGCMCall32;
    pReq->Call.header.header.rc         = VERR_INTERNAL_ERROR;
    pReq->Call.header.header.reserved1  = 0;
    pReq->Call.header.header.fRequestor = VMMDEV_REQUESTOR_KERNEL        | VMMDEV_REQUESTOR_USR_DRV_OTHER
                                        | VMMDEV_REQUESTOR_CON_DONT_KNOW | VMMDEV_REQUESTOR_TRUST_NOT_GIVEN;
    pReq->Call.header.fu32Flags         = 0;
    pReq->Call.header.result            = VERR_INTERNAL_ERROR;
    pReq->Call.u32ClientID              = pClient->idClient;
    pReq->Call.u32Function              = SHFL_FN_INFORMATION;
    pReq->Call.cParms                   = SHFL_CPARMS_INFORMATION;
#endif
    uint32_t const cbBuffer = *pcbBuffer;
    pReq->Parms.id32Root.type           = VMMDevHGCMParmType_32bit;
    pReq->Parms.id32Root.u.value32      = pMap->root;
    pReq->Parms.u64Handle.type          = VMMDevHGCMParmType_64bit;
    pReq->Parms.u64Handle.u.value64     = hFile;
    pReq->Parms.f32Flags.type           = VMMDevHGCMParmType_32bit;
    pReq->Parms.f32Flags.u.value32      = flags;
    pReq->Parms.cb32.type               = VMMDevHGCMParmType_32bit;
    pReq->Parms.cb32.u.value32          = cbBuffer;
    pReq->Parms.pInfo.type              = VMMDevHGCMParmType_PageList;
    pReq->Parms.pInfo.u.PageList.size   = cbBuffer;
    pReq->Parms.pInfo.u.PageList.offset = RT_UOFFSETOF(struct FsInfoReq, PgLst) - RT_UOFFSETOF(struct FsInfoReq, Call);

    Assert(cbBuffer < _1K);
    pReq->PgLst.flags                   = VBOX_HGCM_F_PARM_DIRECTION_FROM_HOST;
    pReq->PgLst.cPages                  = cbBuffer <= (PAGE_SIZE - ((uintptr_t)pBuffer & PAGE_OFFSET_MASK)) ? 1 : 2;
    pReq->PgLst.offFirstPage            = (uint16_t)((uintptr_t)pBuffer & PAGE_OFFSET_MASK);
    pReq->PgLst.aPages[0]               = VbglR0PhysHeapGetPhysAddr(pBuffer) & ~(RTGCPHYS64)PAGE_OFFSET_MASK;
    if (pReq->PgLst.cPages == 1)
        pReq->PageTwo                   = NIL_RTGCPHYS64;
    else
        pReq->PageTwo                   = pReq->PgLst.aPages[0] + PAGE_SIZE;

    int rc = VbglR0HGCMFastCall(pClient->handle, &pReq->Hdr, sizeof(*pReq));
    if (RT_SUCCESS(rc))
    {
        rc = pReq->Call.header.result;
        *pcbBuffer = pReq->Parms.cb32.u.value32;
    }
    VbglR0PhysHeapFree(pReq);
    return rc;
}


DECLVBGL(int) VbglR0SfPhysFsInfo(PVBGLSFCLIENT pClient, PVBGLSFMAP pMap, SHFLHANDLE hFile,
                                 uint32_t flags, uint32_t *pcbBuffer, PSHFLDIRINFO pBuffer)
{
    uint32_t const cbBuffer = *pcbBuffer;

    struct
    {
        VBoxSFInformation   Core;
        HGCMPageListInfo    PgLst;
        RTGCPHYS64          PageTwo;
    } Req;

    VBGL_HGCM_HDR_INIT_EX(&Req.Core.callInfo, pClient->idClient, SHFL_FN_INFORMATION, SHFL_CPARMS_INFORMATION, sizeof(Req));
    Req.Core.callInfo.fInterruptible = false;

    Req.Core.root.type                      = VMMDevHGCMParmType_32bit;
    Req.Core.root.u.value32                 = pMap->root;

    Req.Core.handle.type                    = VMMDevHGCMParmType_64bit;
    Req.Core.handle.u.value64               = hFile;
    Req.Core.flags.type                     = VMMDevHGCMParmType_32bit;
    Req.Core.flags.u.value32                = flags;
    Req.Core.cb.type                        = VMMDevHGCMParmType_32bit;
    Req.Core.cb.u.value32                   = cbBuffer;
    Req.Core.info.type                      = VMMDevHGCMParmType_PageList;
    Req.Core.info.u.PageList.size           = cbBuffer;
    Req.Core.info.u.PageList.offset         = sizeof(Req.Core);

    Assert(cbBuffer < _1K);
    Req.PgLst.flags                         = VBOX_HGCM_F_PARM_DIRECTION_FROM_HOST;
    Req.PgLst.cPages                        = cbBuffer <= (PAGE_SIZE - ((uintptr_t)pBuffer & PAGE_OFFSET_MASK)) ? 1 : 2;
    Req.PgLst.offFirstPage                  = (uint16_t)((uintptr_t)pBuffer & PAGE_OFFSET_MASK);
    Req.PgLst.aPages[0]                     = VbglR0PhysHeapGetPhysAddr(pBuffer) & ~(RTGCPHYS64)PAGE_OFFSET_MASK;
    if (Req.PgLst.cPages == 1)
        Req.PageTwo                         = NIL_RTGCPHYS64;
    else
        Req.PageTwo                         = Req.PgLst.aPages[0] + PAGE_SIZE;

    int rc = VbglR0HGCMCallRaw(pClient->handle, &Req.Core.callInfo, sizeof(Req));
    //Log(("VBOXSF: VbglR0SfFsInfo: VbglR0HGCMCall rc = %#x, result = %#x\n", rc, data.callInfo.Hdr.rc));
    if (RT_SUCCESS(rc))
    {
        rc = Req.Core.callInfo.Hdr.rc;
        *pcbBuffer = Req.Core.cb.u.value32;
    }
    return rc;
}

#endif


/**
 * Worker for FS32_PATHINFO that handles file stat queries.
 *
 * @returns OS/2 status code
 * @param   pFolder         The folder.
 * @param   pSfFsi          The file system independent file structure.  We'll
 *                          update the timestamps and size here.
 * @param   pSfFsd          Out file data.
 * @param   uLevel          The information level.
 * @param   pbData          Where to return the data (user address).
 * @param   cbData          The amount of data to produce.
 */
static APIRET
vboxSfOs2QueryFileInfo(PVBOXSFFOLDER pFolder, PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, ULONG uLevel, PBYTE pbData, ULONG cbData)
{
    /*
     * Performance notes (@bugref{9172}):
     *
     * This function was used for some performance hacking in an attempt at
     * squeezing more performance out of the HGCM and shared folders code.
     *
     * 0. Skip calling the host and returning zeros:
     *         906 ns / 3653 ticks
     *
     *    This is comparable to JFS (859 ns) and HPFS (1107 ns) and give an
     *    idea what we're up against compared to a "local" file system.
     *
     *    Host build of r126639 with strict VBoxGuest.sys and VBoxSF.ifs
     *    circa r126775, just for establishing some actual base line for (2, 3, +):
     *         (a) 39095 ns / 156757 ticks - VbglR0SfFsInfo.
     *         (b) 35074 ns / 140880 ticks - VbglR0SfPhysFsInfo.
     *
     * 1. Having shortcircuted the host side processing by faking a success when
     *    VMMDevHGCM.cpp is about to do pThis->pHGCMDrv->pfnCall, then measuring
     *    various guest side changes in the request and request submission path:
     *
     *     - Saved by page lists vs virtul address for buffers:
     *         4095 ns / 16253 ticks / %35.
     *
     *       Suspect this is due to expensive memory locking on the guest side and
     *       the host doing extra virtual address conversion.
     *
     *     - Saved by no repackaging the HGCM requests:
     *         450 ns / 1941 ticks / 5.8%.
     *
     *     - Embedding the SHFLFSOBJINFO into the buffer may save a little as well:
     *         286 ns / 1086 ticks / 3.9%.
     *
     *    Raw data:
     *        11843 ns / 47469 ticks - VbglR0SfFsInfo.
     *         7748 ns / 31216 ticks - VbglR0SfPhysFsInfo.
     *         7298 ns / 29275 ticks - VbglR0SfFastPhysFsInfo.
     *         7012 ns / 28189 ticks - Embedded buffer.
     *
     * 2. Interrupt acknowledgement in VBoxGuest goes to ring-3, which is wasteful.
     *    Played around with handling VMMDevReq_AcknowledgeEvents requests in
     *    ring-0, but since it just returns a 32-bit mask of pending events it was
     *    more natural to implement it as a 32-bit IN operation.
     *
     *    Saves 4217 ns / 17048 ticks / 13%.
     *
     *    Raw data:
     *          32027 ns / 128506 ticks - ring-3 VMMDevReq_AcknowledgeEvents.
     *          27810 ns / 111458 ticks - fast ring-0 ACK.
     *
     * 3. Use single release & auto resetting event semaphore in HGCMThread.
     *
     *    Saves 922 ns / 3406 ticks / 3.4%.
     *
     *    Raw data:
     *          27472 ns / 110237 ticks - RTSEMEVENTMULTI
     *          26550 ns / 106831 ticks - RTSEMEVENT
     *
     *    Gain since 0a: 12545 ns / 49926 ticks / 32%
     *    Gain since 0b:  8524 ns / 34049 ticks / 24%
     *
     * 4. Try handle VINF_EM_HALT from HMR0 in ring-0, avoiding 4 context switches
     *    and a EM reschduling.
     *
     *    Saves 1216 ns / 4734 ticks / 4.8%.
     *
     *    Raw data:
     *          25595 ns / 102768 ticks - no ring-0 HLT.
     *          24379 ns /  98034 ticks - ring-0 HLT (42 spins)
     *
     *    Gain since 0a: 14716 ns / 58723 ticks / 38%
     *    Gain since 0b: 10695 ns / 42846 ticks / 30%
     *
     */
#if 0
    APIRET rc;
    PSHFLFSOBJINFO pObjInfo = (PSHFLFSOBJINFO)VbglR0PhysHeapAlloc(sizeof(*pObjInfo));
    if (pObjInfo)
    {
        RT_ZERO(*pObjInfo);
        uint32_t cbObjInfo = sizeof(*pObjInfo);

        int vrc = VbglR0SfFsInfo(&g_SfClient, &pFolder->hHostFolder, pSfFsd->hHostFile,
                                 SHFL_INFO_FILE | SHFL_INFO_GET, &cbObjInfo, (PSHFLDIRINFO)pObjInfo);
        if (RT_SUCCESS(vrc))
        {
            rc = vboxSfOs2FileStatusFromObjInfo(pbData, cbData, uLevel, pObjInfo);
            if (rc == NO_ERROR)
            {
                /* Update the timestamps in the independent file data: */
                int16_t cMinLocalTimeDelta = vboxSfOs2GetLocalTimeDelta();
                vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_cdate, &pSfFsi->sfi_ctime, pObjInfo->BirthTime,        cMinLocalTimeDelta);
                vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_adate, &pSfFsi->sfi_atime, pObjInfo->AccessTime,       cMinLocalTimeDelta);
                vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_mdate, &pSfFsi->sfi_mtime, pObjInfo->ModificationTime, cMinLocalTimeDelta);

                /* And the size field as we're at it: */
                pSfFsi->sfi_sizel = pObjInfo->cbObject;
            }
        }
        else
        {
            Log(("vboxSfOs2QueryFileInfo: VbglR0SfFsInfo failed: %Rrc\n", vrc));
            rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_GEN_FAILURE);
        }
        VbglR0PhysHeapFree(pObjInfo);
    }
    else
        rc = ERROR_NOT_ENOUGH_MEMORY;
#elif  0
    APIRET rc;
    struct MyEmbReq
    {
        VBGLIOCIDCHGCMFASTCALL  Hdr;
        VMMDevHGCMCall          Call;
        VBoxSFParmInformation   Parms;
        SHFLFSOBJINFO           ObjInfo;
    } *pReq = (struct MyEmbReq *)VbglR0PhysHeapAlloc(sizeof(*pReq));
    if (pReq)
    {
        RT_ZERO(pReq->ObjInfo);

        VBGLIOCIDCHGCMFASTCALL_INIT(&pReq->Hdr, VbglR0PhysHeapGetPhysAddr(pReq), &pReq->Call, g_SfClient.idClient,
                                    SHFL_FN_INFORMATION, SHFL_CPARMS_INFORMATION, sizeof(*pReq));
        pReq->Parms.id32Root.type               = VMMDevHGCMParmType_32bit;
        pReq->Parms.id32Root.u.value32          = pFolder->idHostRoot;
        pReq->Parms.u64Handle.type              = VMMDevHGCMParmType_64bit;
        pReq->Parms.u64Handle.u.value64         = pSfFsd->hHostFile;
        pReq->Parms.f32Flags.type               = VMMDevHGCMParmType_32bit;
        pReq->Parms.f32Flags.u.value32          = SHFL_INFO_FILE | SHFL_INFO_GET;
        pReq->Parms.cb32.type                   = VMMDevHGCMParmType_32bit;
        pReq->Parms.cb32.u.value32              = sizeof(pReq->ObjInfo);
        pReq->Parms.pInfo.type                  = VMMDevHGCMParmType_Embedded;
        pReq->Parms.pInfo.u.Embedded.cbData     = sizeof(pReq->ObjInfo);
        pReq->Parms.pInfo.u.Embedded.offData    = RT_UOFFSETOF(struct MyEmbReq, ObjInfo) - sizeof(VBGLIOCIDCHGCMFASTCALL);
        pReq->Parms.pInfo.u.Embedded.fFlags     = VBOX_HGCM_F_PARM_DIRECTION_FROM_HOST;

        int vrc = VbglR0HGCMFastCall(g_SfClient.handle, &pReq->Hdr, sizeof(*pReq));
        if (RT_SUCCESS(vrc))
            vrc = pReq->Call.header.result;
        if (RT_SUCCESS(vrc))
        {
            rc = vboxSfOs2FileStatusFromObjInfo(pbData, cbData, uLevel, &pReq->ObjInfo);
            if (rc == NO_ERROR)
            {
                /* Update the timestamps in the independent file data: */
                int16_t cMinLocalTimeDelta = vboxSfOs2GetLocalTimeDelta();
                vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_cdate, &pSfFsi->sfi_ctime, pReq->ObjInfo.BirthTime,        cMinLocalTimeDelta);
                vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_adate, &pSfFsi->sfi_atime, pReq->ObjInfo.AccessTime,       cMinLocalTimeDelta);
                vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_mdate, &pSfFsi->sfi_mtime, pReq->ObjInfo.ModificationTime, cMinLocalTimeDelta);

                /* And the size field as we're at it: */
                pSfFsi->sfi_sizel = pReq->ObjInfo.cbObject;
            }
        }
        else
        {
            Log(("vboxSfOs2QueryFileInfo: VbglR0SfFsInfo failed: %Rrc\n", vrc));
            rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_GEN_FAILURE);
        }

        VbglR0PhysHeapFree(pReq);
    }
    else
        rc = ERROR_NOT_ENOUGH_MEMORY;
#else /* clean version of the above. */
    APIRET rc;
    VBOXSFOBJINFOREQ *pReq = (VBOXSFOBJINFOREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
    if (pReq)
    {
        int vrc = VbglR0SfHostReqQueryObjInfo(pFolder->idHostRoot, pReq, pSfFsd->hHostFile);
        if (RT_SUCCESS(vrc))
        {
            rc = vboxSfOs2FileStatusFromObjInfo(pbData, cbData, uLevel, &pReq->ObjInfo);
            if (rc == NO_ERROR)
            {
                /* Update the timestamps in the independent file data: */
                int16_t cMinLocalTimeDelta = vboxSfOs2GetLocalTimeDelta();
                vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_cdate, &pSfFsi->sfi_ctime, pReq->ObjInfo.BirthTime,        cMinLocalTimeDelta);
                vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_adate, &pSfFsi->sfi_atime, pReq->ObjInfo.AccessTime,       cMinLocalTimeDelta);
                vboxSfOs2DateTimeFromTimeSpec(&pSfFsi->sfi_mdate, &pSfFsi->sfi_mtime, pReq->ObjInfo.ModificationTime, cMinLocalTimeDelta);

                /* And the size field as we're at it: */
                pSfFsi->sfi_sizel = pReq->ObjInfo.cbObject;
            }
        }
        else
        {
            Log(("vboxSfOs2QueryFileInfo: VbglR0SfHostReqQueryObjInfo failed: %Rrc\n", vrc));
            rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_GEN_FAILURE);
        }

        VbglR0PhysHeapFree(pReq);
    }
    else
        rc = ERROR_NOT_ENOUGH_MEMORY;
#endif
    return rc;
}


DECLASM(APIRET)
FS32_FILEINFO(ULONG fFlags, PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, ULONG uLevel,
              PBYTE pbData, ULONG cbData, ULONG fIoFlags)
{
    LogFlow(("FS32_FILEINFO: fFlags=%#x pSfFsi=%p pSfFsd=%p uLevel=%p pbData=%p cbData=%#x fIoFlags=%#x\n",
             fFlags, pSfFsi, pSfFsd, uLevel, pbData, cbData, fIoFlags));

    /*
     * Validate input.
     */
    AssertReturn(pSfFsd->u32Magic == VBOXSFSYFI_MAGIC, ERROR_SYS_INTERNAL);
    AssertReturn(pSfFsd->pSelf    == pSfFsd, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pSfFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenFiles > 0);

    /*
     * Check the level.
     * Note! See notes in FS32_PATHINFO.
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
        default:
            LogRel(("FS32_PATHINFO: Unsupported info level %u!\n", uLevel));
            return ERROR_INVALID_LEVEL;
    }
    if (cbData < cbMinData || pbData == NULL)
    {
        Log(("FS32_FILEINFO: ERROR_BUFFER_OVERFLOW (cbMinData=%#x, cbData=%#x)\n", cbMinData, cbData));
        return ERROR_BUFFER_OVERFLOW;
    }

    /*
     * Query information.
     */
    APIRET rc;
    if (fFlags == FI_RETRIEVE)
    {
        switch (uLevel)
        {
            case FI_LVL_STANDARD:
            case FI_LVL_STANDARD_EASIZE:
            case FI_LVL_STANDARD_64:
            case FI_LVL_STANDARD_EASIZE_64:
                rc = vboxSfOs2QueryFileInfo(pFolder, pSfFsi, pSfFsd, uLevel, pbData, cbMinData);
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
    }
    /*
     * Update information.
     */
    else if (fFlags == FI_SET)
    {
        switch (uLevel)
        {
            case FI_LVL_STANDARD:
            case FI_LVL_STANDARD_64:
                rc = vboxSfOs2SetFileInfo(pFolder, pSfFsi, pSfFsd, uLevel, pbData, cbMinData);
                break;

            case FI_LVL_STANDARD_EASIZE:
                rc = ERROR_EAS_NOT_SUPPORTED;
                break;

            case FI_LVL_STANDARD_EASIZE_64:
            case FI_LVL_EAS_FROM_LIST:
            case FI_LVL_EAS_FULL:
            case FI_LVL_EAS_FULL_5:
            case FI_LVL_EAS_FULL_8:
                rc = ERROR_INVALID_LEVEL;
                break;

            default:
                AssertFailed();
                rc = ERROR_GEN_FAILURE;
                break;
        }
    }
    else
    {
        LogRel(("FS32_FILEINFO: Unknown flags value: %#x\n", fFlags));
        rc = ERROR_INVALID_PARAMETER;
    }
    RT_NOREF_PV(fIoFlags);
    return rc;
}


DECLASM(APIRET)
FS32_NEWSIZEL(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, LONGLONG cbFile, ULONG fIoFlags)
{
    LogFlow(("FS32_NEWSIZEL: pSfFsi=%p pSfFsd=%p cbFile=%RI64 (%#RX64) fIoFlags=%#x\n", pSfFsi, pSfFsd, cbFile, cbFile, fIoFlags));

    /*
     * Validate input.
     */
    AssertReturn(pSfFsd->u32Magic == VBOXSFSYFI_MAGIC, ERROR_SYS_INTERNAL);
    AssertReturn(pSfFsd->pSelf    == pSfFsd, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pSfFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenFiles > 0);
    if (cbFile < 0)
    {
        LogRel(("FS32_NEWSIZEL: Negative size: %RI64\n", cbFile));
        return ERROR_INVALID_PARAMETER;
    }

    /*
     * This should only be possible on a file that is writable.
     */
    APIRET rc;
    if (   (pSfFsi->sfi_mode & SFMODE_OPEN_ACCESS) == SFMODE_OPEN_WRITEONLY
        || (pSfFsi->sfi_mode & SFMODE_OPEN_ACCESS) == SFMODE_OPEN_READWRITE)
    {
        /*
         * Call the host.
         */
        int vrc = VbglR0SfHostReqSetFileSizeSimple(pFolder->idHostRoot, pSfFsd->hHostFile, cbFile);
        if (RT_SUCCESS(vrc))
        {
            pSfFsi->sfi_sizel = cbFile;
            rc = NO_ERROR;
        }
        else
        {
            LogRel(("FS32_NEWSIZEL: VbglR0SfFsInfo failed: %Rrc\n", vrc));
            rc = vboxSfOs2ConvertStatusToOs2(vrc, ERROR_GEN_FAILURE);
        }
    }
    else
        rc = ERROR_ACCESS_DENIED;

    RT_NOREF(fIoFlags);
    LogFlow(("FS32_NEWSIZEL: returns %u\n", rc));
    return rc;
}


/**
 * Convert KernVMLock page list to HGCM page list.
 *
 * The trouble is that it combine pages.
 */
static void vboxSfOs2ConvertPageList(KernPageList_t volatile *paSrc, RTGCPHYS64 volatile *paDst, ULONG cSrc, uint32_t cDst)
{
    LogFlow(("vboxSfOs2ConvertPageList: %d vs %d\n", cSrc, cDst));

    /* If the list have identical length, the job is easy. */
    if (cSrc == cDst)
        for (uint32_t i = 0; i < cSrc; i++)
            paDst[i] &= ~(uint32_t)PAGE_OFFSET_MASK;
    else
    {
        Assert(cSrc <= cDst);
        Assert(cSrc > 0);

        /*
         * We have fewer source entries than destiation pages, so something needs
         * expanding.  The fact that the first and last pages might be partial ones
         * makes this more interesting.  We have to do it backwards, of course.
         */

        /* Deal with the partial page stuff first. */
        paSrc[0].Size += paSrc[0].Addr & PAGE_OFFSET_MASK;
        paSrc[0].Addr &= ~(ULONG)PAGE_OFFSET_MASK;
        paSrc[cSrc - 1].Size = RT_ALIGN_32(paSrc[cSrc - 1].Size, PAGE_SIZE);

        /* The go do work on the conversion. */
        uint32_t iDst = cDst;
        uint32_t iSrc = cSrc;
        while (iSrc-- > 0)
        {
            ULONG cbSrc    = paSrc[iSrc].Size;
            ULONG uAddrSrc = paSrc[iSrc].Addr + cbSrc;
            Assert(!(cbSrc & PAGE_OFFSET_MASK));
            Assert(!(uAddrSrc & PAGE_OFFSET_MASK));
            while (cbSrc > 0)
            {
                uAddrSrc     -= PAGE_SIZE;
                Assert(iDst > 0);
                paDst[--iDst] = uAddrSrc;
                cbSrc        -= PAGE_SIZE;
            }
        }
        Assert(iDst == 0);
    }
}


/**
 * Helper for FS32_READ.
 *
 * @note Must not called if reading beyond the end of the file, as we would give
 *       sfi_sizel an incorrect value then.
 */
DECLINLINE(uint32_t) vboxSfOs2ReadFinalize(PSFFSI pSfFsi, uint64_t offRead, uint32_t cbActual)
{
    pSfFsi->sfi_positionl = offRead + cbActual;
    if ((uint64_t)pSfFsi->sfi_sizel < offRead + cbActual)
        pSfFsi->sfi_sizel = offRead + cbActual;
    pSfFsi->sfi_tstamp   |= ST_SREAD | ST_PREAD;
    return cbActual;
}


extern "C" APIRET APIENTRY
FS32_READ(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, PVOID pvData, PULONG pcb, ULONG fIoFlags)
{
    LogFlow(("FS32_READ: pSfFsi=%p pSfFsd=%p pvData=%p pcb=%p:{%#x} fIoFlags=%#x\n", pSfFsi, pSfFsd, pvData, pcb, *pcb, fIoFlags));

    /*
     * Validate and extract input.
     */
    AssertReturn(pSfFsd->u32Magic == VBOXSFSYFI_MAGIC, ERROR_SYS_INTERNAL);
    AssertReturn(pSfFsd->pSelf    == pSfFsd, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pSfFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenFiles > 0);
    RT_NOREF(pFolder);

    uint64_t const offRead  = pSfFsi->sfi_positionl;
    uint32_t const cbToRead = *pcb;
    uint32_t       cbActual = cbToRead;

    /*
     * We'll try embedded buffers for reads a smaller than ~2KB if we get
     * a heap block that's entirely within one page so the host can lock it
     * and avoid bouncing it off the heap on completion.
     */
    if (cbToRead <= _2K)
    {
        size_t                 cbReq = RT_UOFFSETOF(VBOXSFREADEMBEDDEDREQ, abData[0]) + cbToRead;
        VBOXSFREADEMBEDDEDREQ *pReq  = (VBOXSFREADEMBEDDEDREQ *)VbglR0PhysHeapAlloc(cbReq);
        if (   pReq != NULL
            && (   PAGE_SIZE - (PAGE_OFFSET_MASK & (uintptr_t)pReq) >= cbReq
                || cbToRead == 0))
        {
            APIRET rc;
            int vrc = VbglR0SfHostReqReadEmbedded(pFolder->idHostRoot, pReq, pSfFsd->hHostFile, offRead, cbToRead);
            if (RT_SUCCESS(vrc))
            {
                cbActual = pReq->Parms.cb32Read.u.value32;
                if (cbActual > 0)
                {
                    AssertStmt(cbActual <= cbToRead, cbActual = cbToRead);
                    rc = KernCopyOut(pvData, &pReq->abData[0], cbActual);
                    if (rc == NO_ERROR)
                    {
                        *pcb = vboxSfOs2ReadFinalize(pSfFsi, offRead, cbActual);
                        LogFlow(("FS32_READ: returns; cbActual=%#x sfi_positionl=%RI64 [embedded]\n", cbActual, pSfFsi->sfi_positionl));
                    }
                }
                else
                {
                    LogFlow(("FS32_READ: returns; cbActual=0 (EOF); sfi_positionl=%RI64 [embedded]\n", pSfFsi->sfi_positionl));
                    *pcb = 0;
                    rc = NO_ERROR;
                }
            }
            else
            {
                Log(("FS32_READ: VbglR0SfHostReqReadEmbedded(off=%#RU64,cb=%#x) -> %Rrc [embedded]\n", offRead, cbToRead, vrc));
                rc = ERROR_BAD_NET_RESP;
            }
            VbglR0PhysHeapFree(pReq);
            return rc;
        }
        if (pReq)
            VbglR0PhysHeapFree(pReq);
    }

    /*
     * Whatever we do now we're going to use a page list request structure.
     * So, we do one allocation large enough for both code paths below.
     */
    uint32_t cPages = ((cbToRead + PAGE_SIZE - 1) >> PAGE_SHIFT) + 1;
    VBOXSFREADPGLSTREQ *pReq = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFREADPGLSTREQ, PgLst.aPages[cPages]));
    if (pReq)
    { /* likely */ }
    else
    {
        LogRel(("FS32_READ: Out of memory for page list request (%u pages)\n", cPages));
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    /*
     * If the request is less than 16KB or smaller, we try bounce it off the
     * physical heap (slab size is 64KB).  For requests up to 64KB we try use
     * one of a handful of preallocated big buffers rather than the phys heap.
     */
    if (cbToRead <= _64K)
    {
        RTGCPHYS GCPhys;
        void    *pvBuf = NULL;
        if (cbToRead <= _16K)
        {
            pvBuf = VbglR0PhysHeapAlloc(cbToRead);
            GCPhys = pvBuf ? VbglR0PhysHeapGetPhysAddr(pvBuf) : NIL_RTGCPHYS;
        }
        else
            pvBuf = vboxSfOs2AllocBigBuffer(&GCPhys);
        if (pvBuf)
        {
            APIRET rc;
            int vrc = VbglR0SfHostReqReadContig(pFolder->idHostRoot, pReq, pSfFsd->hHostFile, offRead, cbToRead, pvBuf, GCPhys);
            if (RT_SUCCESS(vrc))
            {
                cbActual = pReq->Parms.cb32Read.u.value32;
                if (cbActual > 0)
                {
                    AssertStmt(cbActual <= cbToRead, cbActual = cbToRead);
                    rc = KernCopyOut(pvData, pvBuf, cbActual);
                    if (rc == NO_ERROR)
                    {
                        *pcb = vboxSfOs2ReadFinalize(pSfFsi, offRead, cbActual);
                        LogFlow(("FS32_READ: returns; cbActual=%#x sfi_positionl=%RI64 [bounced]\n", cbActual, pSfFsi->sfi_positionl));
                    }
                }
                else
                {
                    LogFlow(("FS32_READ: returns; cbActual=0 (EOF) sfi_positionl=%RI64 [bounced]\n", pSfFsi->sfi_positionl));
                    *pcb = 0;
                    rc = NO_ERROR;
                }
            }
            else
            {
                Log(("FS32_READ: VbglR0SfHostReqReadEmbedded(off=%#RU64,cb=%#x) -> %Rrc [bounced]\n", offRead, cbToRead, vrc));
                rc = ERROR_BAD_NET_RESP;
            }

            if (cbToRead <= _16K)
                VbglR0PhysHeapFree(pvBuf);
            else
                vboxSfOs2FreeBigBuffer(pvBuf);
            VbglR0PhysHeapFree(pReq);
            return rc;
        }
    }

    /*
     * We couldn't use a bounce buffer for it, so lock the buffer pages.
     */
    KernVMLock_t Lock;
    ULONG cPagesRet;
    AssertCompile(sizeof(KernPageList_t) == sizeof(pReq->PgLst.aPages[0]));
    APIRET rc = KernVMLock(VMDHL_LONG | VMDHL_WRITE, (void *)pvData, cbToRead, &Lock,
                           (KernPageList_t *)&pReq->PgLst.aPages[0], &cPagesRet);
    if (rc == NO_ERROR)
    {
        pReq->PgLst.offFirstPage = (uint16_t)(uintptr_t)pvData & (uint16_t)PAGE_OFFSET_MASK;
        cPages = (cbToRead + ((uint16_t)(uintptr_t)pvData & (uint16_t)PAGE_OFFSET_MASK) + PAGE_SIZE - 1) >> PAGE_SHIFT;
        vboxSfOs2ConvertPageList((KernPageList_t volatile *)&pReq->PgLst.aPages[0], &pReq->PgLst.aPages[0], cPagesRet, cPages);

        int vrc = VbglR0SfHostReqReadPgLst(pFolder->idHostRoot, pReq, pSfFsd->hHostFile, offRead, cbToRead, cPages);
        if (RT_SUCCESS(vrc))
        {
            cbActual = pReq->Parms.cb32Read.u.value32;
            if (cbActual > 0)
            {
                AssertStmt(cbActual <= cbToRead, cbActual = cbToRead);
                *pcb = vboxSfOs2ReadFinalize(pSfFsi, offRead, cbActual);
                LogFlow(("FS32_READ: returns; cbActual=%#x sfi_positionl=%RI64 [locked]\n", cbActual, pSfFsi->sfi_positionl));
            }
            else
            {
                LogFlow(("FS32_READ: returns; cbActual=0 (EOF) sfi_positionl=%RI64 [locked]\n", pSfFsi->sfi_positionl));
                *pcb = 0;
                rc = NO_ERROR;
            }
        }
        else
        {
            Log(("FS32_READ: VbglR0SfHostReqReadEmbedded(off=%#RU64,cb=%#x) -> %Rrc [locked]\n", offRead, cbToRead, vrc));
            rc = ERROR_BAD_NET_RESP;
        }

        KernVMUnlock(&Lock);
    }
    else
        Log(("FS32_READ: KernVMLock(,%p,%#x,) failed -> %u\n", pvData, cbToRead, rc));
    VbglR0PhysHeapFree(pReq);
    RT_NOREF_PV(fIoFlags);
    return rc;
}


/**
 * Helper for FS32_WRITE.
 */
DECLINLINE(uint32_t) vboxSfOs2WriteFinalize(PSFFSI pSfFsi, uint64_t offWrite, uint32_t cbActual)
{
    pSfFsi->sfi_positionl = offWrite + cbActual;
    if ((uint64_t)pSfFsi->sfi_sizel < offWrite + cbActual)
        pSfFsi->sfi_sizel = offWrite + cbActual;
    pSfFsi->sfi_tstamp   |= ST_SWRITE | ST_PWRITE;
    return cbActual;
}


extern "C" APIRET APIENTRY
FS32_WRITE(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, void const *pvData, PULONG pcb, ULONG fIoFlags)
{
    /*
     * Validate and extract input.
     */
    AssertReturn(pSfFsd->u32Magic == VBOXSFSYFI_MAGIC, ERROR_SYS_INTERNAL);
    AssertReturn(pSfFsd->pSelf    == pSfFsd, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pSfFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenFiles > 0);
    RT_NOREF(pFolder);

    uint64_t offWrite  = pSfFsi->sfi_positionl;
    uint32_t cbToWrite = *pcb;
    uint32_t cbActual  = cbToWrite;

    /*
     * We'll try embedded buffers for writes a smaller than ~2KB if we get
     * a heap block that's entirely within one page so the host can lock it
     * and avoid bouncing it off the heap on completion.
     */
    if (cbToWrite <= _2K)
    {
        size_t                  cbReq = RT_UOFFSETOF(VBOXSFWRITEEMBEDDEDREQ, abData[0]) + cbToWrite;
        VBOXSFWRITEEMBEDDEDREQ *pReq  = (VBOXSFWRITEEMBEDDEDREQ *)VbglR0PhysHeapAlloc(cbReq);
        if (   pReq != NULL
            && (   PAGE_SIZE - (PAGE_OFFSET_MASK & (uintptr_t)pReq) >= cbReq
                || cbToWrite == 0))
        {
            APIRET rc = KernCopyIn(&pReq->abData[0], pvData, cbToWrite);
            if (rc == NO_ERROR)
            {
                int vrc = VbglR0SfHostReqWriteEmbedded(pFolder->idHostRoot, pReq, pSfFsd->hHostFile, offWrite, cbToWrite);
                if (RT_SUCCESS(vrc))
                {
                    cbActual = pReq->Parms.cb32Write.u.value32;
                    AssertStmt(cbActual <= cbToWrite, cbActual = cbToWrite);
                    *pcb = vboxSfOs2WriteFinalize(pSfFsi, offWrite, cbActual);
                    LogFlow(("FS32_WRITE: returns; cbActual=%#x sfi_positionl=%RI64 [embedded]\n", cbActual, pSfFsi->sfi_positionl));
                }
                else
                {
                    Log(("FS32_WRITE: VbglR0SfHostReqWriteEmbedded(off=%#RU64,cb=%#x) -> %Rrc [embedded]\n", offWrite, cbToWrite, vrc));
                    rc = ERROR_BAD_NET_RESP;
                }
            }
            VbglR0PhysHeapFree(pReq);
            return rc;
        }
        if (pReq)
            VbglR0PhysHeapFree(pReq);
    }

    /*
     * Whatever we do now we're going to use a page list request structure.
     * So, we do one allocation large enough for both code paths below.
     */
    uint32_t cPages = ((cbToWrite + PAGE_SIZE - 1) >> PAGE_SHIFT) + 1;
    VBOXSFWRITEPGLSTREQ *pReq = (VBOXSFWRITEPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFWRITEPGLSTREQ, PgLst.aPages[cPages]));
    if (pReq)
    { /* likely */ }
    else
    {
        LogRel(("FS32_WRITE: Out of memory for page list request (%u pages)\n", cPages));
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    /*
     * If the request is less than 16KB or smaller, we try bounce it off the
     * physical heap (slab size is 64KB).  For requests up to 64KB we try use
     * one of a handful of preallocated big buffers rather than the phys heap.
     */
    if (cbToWrite <= _64K)
    {
        RTGCPHYS GCPhys;
        void    *pvBuf = NULL;
        if (cbToWrite <= _16K)
        {
            pvBuf = VbglR0PhysHeapAlloc(cbToWrite);
            GCPhys = pvBuf ? VbglR0PhysHeapGetPhysAddr(pvBuf) : NIL_RTGCPHYS;
        }
        else
            pvBuf = vboxSfOs2AllocBigBuffer(&GCPhys);
        if (pvBuf)
        {
            APIRET rc = KernCopyIn(pvBuf, pvData, cbToWrite);
            if (rc == NO_ERROR)
            {
                int vrc = VbglR0SfHostReqWriteContig(pFolder->idHostRoot, pReq, pSfFsd->hHostFile,
                                                     offWrite, cbToWrite, pvBuf, GCPhys);
                if (RT_SUCCESS(vrc))
                {
                    cbActual = pReq->Parms.cb32Write.u.value32;
                    AssertStmt(cbActual <= cbToWrite, cbActual = cbToWrite);
                    *pcb = vboxSfOs2WriteFinalize(pSfFsi, offWrite, cbActual);
                    LogFlow(("FS32_WRITE: returns; cbActual=%#x sfi_positionl=%RI64 [bounced]\n", cbActual, pSfFsi->sfi_positionl));
                }
                else
                {
                    Log(("FS32_WRITE: VbglR0SfHostReqWriteEmbedded(off=%#RU64,cb=%#x) -> %Rrc [bounced]\n", offWrite, cbToWrite, vrc));
                    rc = ERROR_BAD_NET_RESP;
                }
            }

            if (cbToWrite <= _16K)
                VbglR0PhysHeapFree(pvBuf);
            else
                vboxSfOs2FreeBigBuffer(pvBuf);
            VbglR0PhysHeapFree(pReq);
            return rc;
        }
    }

    /*
     * We couldn't use a bounce buffer for it, so lock the buffer pages.
     */
    KernVMLock_t Lock;
    ULONG cPagesRet;
    AssertCompile(sizeof(KernPageList_t) == sizeof(pReq->PgLst.aPages[0]));
    APIRET rc = KernVMLock(VMDHL_LONG, (void *)pvData, cbToWrite, &Lock, (KernPageList_t *)&pReq->PgLst.aPages[0], &cPagesRet);
    if (rc == NO_ERROR)
    {
        pReq->PgLst.offFirstPage = (uint16_t)(uintptr_t)pvData & (uint16_t)PAGE_OFFSET_MASK;
        cPages = (cbToWrite + ((uint16_t)(uintptr_t)pvData & (uint16_t)PAGE_OFFSET_MASK) + PAGE_SIZE - 1) >> PAGE_SHIFT;
        vboxSfOs2ConvertPageList((KernPageList_t volatile *)&pReq->PgLst.aPages[0], &pReq->PgLst.aPages[0], cPagesRet, cPages);

        int vrc = VbglR0SfHostReqWritePgLst(pFolder->idHostRoot, pReq, pSfFsd->hHostFile, offWrite, cbToWrite, cPages);
        if (RT_SUCCESS(vrc))
        {
            cbActual = pReq->Parms.cb32Write.u.value32;
            AssertStmt(cbActual <= cbToWrite, cbActual = cbToWrite);
            *pcb = vboxSfOs2WriteFinalize(pSfFsi, offWrite, cbActual);
            LogFlow(("FS32_WRITE: returns; cbActual=%#x sfi_positionl=%RI64 [locked]\n", cbActual, pSfFsi->sfi_positionl));
        }
        else
        {
            Log(("FS32_WRITE: VbglR0SfHostReqWriteEmbedded(off=%#RU64,cb=%#x) -> %Rrc [locked]\n", offWrite, cbToWrite, vrc));
            rc = ERROR_BAD_NET_RESP;
        }

        KernVMUnlock(&Lock);
    }
    else
        Log(("FS32_WRITE: KernVMLock(,%p,%#x,) failed -> %u\n", pvData, cbToWrite, rc));
    VbglR0PhysHeapFree(pReq);
    RT_NOREF_PV(fIoFlags);
    return rc;
}


extern "C" APIRET APIENTRY
FS32_READFILEATCACHE(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, ULONG fIoFlags, LONGLONG off, ULONG pcb, KernCacheList_t **ppCacheList)
{
    /*
     * Validate input.
     */
    AssertReturn(pSfFsd->u32Magic == VBOXSFSYFI_MAGIC, ERROR_SYS_INTERNAL);
    AssertReturn(pSfFsd->pSelf    == pSfFsd, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pSfFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenFiles > 0);
    RT_NOREF(pFolder);

    /* I think this is used for sendfile(). */

    NOREF(pSfFsi); NOREF(pSfFsd); NOREF(fIoFlags); NOREF(off); NOREF(pcb); NOREF(ppCacheList);
    return ERROR_NOT_SUPPORTED;
}


extern "C" APIRET APIENTRY
FS32_RETURNFILECACHE(KernCacheList_t *pCacheList)
{
    NOREF(pCacheList);
    return ERROR_NOT_SUPPORTED;
}


/* oddments */

DECLASM(APIRET)
FS32_CANCELLOCKREQUESTL(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, struct filelockl *pLockRange)
{
    /*
     * Validate input.
     */
    AssertReturn(pSfFsd->u32Magic == VBOXSFSYFI_MAGIC, ERROR_SYS_INTERNAL);
    AssertReturn(pSfFsd->pSelf    == pSfFsd, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pSfFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenFiles > 0);
    RT_NOREF(pFolder);

    NOREF(pSfFsi); NOREF(pSfFsd); NOREF(pLockRange);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(APIRET)
FS32_CANCELLOCKREQUEST(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, struct filelock *pLockRange)
{
    /*
     * Validate input.
     */
    AssertReturn(pSfFsd->u32Magic == VBOXSFSYFI_MAGIC, ERROR_SYS_INTERNAL);
    AssertReturn(pSfFsd->pSelf    == pSfFsd, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pSfFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenFiles > 0);
    RT_NOREF(pFolder);

    NOREF(pSfFsi); NOREF(pSfFsd); NOREF(pLockRange);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(APIRET)
FS32_FILELOCKSL(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, struct filelockl *pUnLockRange,
                struct filelockl *pLockRange, ULONG cMsTimeout, ULONG fFlags)
{
    /*
     * Validate input.
     */
    AssertReturn(pSfFsd->u32Magic == VBOXSFSYFI_MAGIC, ERROR_SYS_INTERNAL);
    AssertReturn(pSfFsd->pSelf    == pSfFsd, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pSfFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenFiles > 0);
    RT_NOREF(pFolder);

    NOREF(pSfFsi); NOREF(pSfFsd); NOREF(pUnLockRange); NOREF(pLockRange); NOREF(cMsTimeout); NOREF(fFlags);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(APIRET)
FS32_FILELOCKS(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, struct filelock *pUnLockRange,
               struct filelock *pLockRange, ULONG cMsTimeout, ULONG fFlags)
{
    /*
     * Validate input.
     */
    AssertReturn(pSfFsd->u32Magic == VBOXSFSYFI_MAGIC, ERROR_SYS_INTERNAL);
    AssertReturn(pSfFsd->pSelf    == pSfFsd, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pSfFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenFiles > 0);
    RT_NOREF(pFolder);

    NOREF(pSfFsi); NOREF(pSfFsd); NOREF(pUnLockRange); NOREF(pLockRange); NOREF(cMsTimeout); NOREF(fFlags);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(APIRET)
FS32_IOCTL(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, USHORT uCategory, USHORT uFunction,
           PVOID pvParm, USHORT cbParm, PUSHORT pcbParmIO,
           PVOID pvData, USHORT cbData, PUSHORT pcbDataIO)
{
    /*
     * Validate input.
     */
    AssertReturn(pSfFsd->u32Magic == VBOXSFSYFI_MAGIC, ERROR_SYS_INTERNAL);
    AssertReturn(pSfFsd->pSelf    == pSfFsd, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pSfFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenFiles > 0);
    RT_NOREF(pFolder);

    NOREF(pSfFsi); NOREF(pSfFsd); NOREF(uCategory); NOREF(uFunction); NOREF(pvParm); NOREF(cbParm); NOREF(pcbParmIO);
    NOREF(pvData); NOREF(cbData); NOREF(pcbDataIO);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(APIRET)
FS32_FILEIO(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, PBYTE pbCmdList, USHORT cbCmdList,
            PUSHORT poffError, USHORT fIoFlag)
{
    /*
     * Validate input.
     */
    AssertReturn(pSfFsd->u32Magic == VBOXSFSYFI_MAGIC, ERROR_SYS_INTERNAL);
    AssertReturn(pSfFsd->pSelf    == pSfFsd, ERROR_SYS_INTERNAL);
    PVBOXSFFOLDER pFolder = pSfFsd->pFolder;
    AssertReturn(pFolder != NULL, ERROR_SYS_INTERNAL);
    Assert(pFolder->u32Magic == VBOXSFFOLDER_MAGIC);
    Assert(pFolder->cOpenFiles > 0);
    RT_NOREF(pFolder);

    NOREF(pSfFsi); NOREF(pSfFsd); NOREF(pbCmdList); NOREF(cbCmdList); NOREF(poffError); NOREF(fIoFlag);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(APIRET)
FS32_NMPIPE(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, USHORT uOpType, union npoper *pOpRec,
            PBYTE pbData, PCSZ pszName)
{
    NOREF(pSfFsi); NOREF(pSfFsd); NOREF(uOpType); NOREF(pOpRec); NOREF(pbData); NOREF(pszName);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(APIRET)
FS32_OPENPAGEFILE(PULONG pfFlags, PULONG pcMaxReq, PCSZ pszName, PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd,
                  USHORT fOpenMode, USHORT fOpenFlags, USHORT fAttr, ULONG uReserved)
{
    NOREF(pfFlags); NOREF(pcMaxReq); NOREF(pszName); NOREF(pSfFsi); NOREF(pSfFsd); NOREF(fOpenMode); NOREF(fOpenFlags);
    NOREF(fAttr); NOREF(uReserved);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(APIRET)
FS32_SETSWAP(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd)
{
    NOREF(pSfFsi); NOREF(pSfFsd);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(APIRET)
FS32_ALLOCATEPAGESPACE(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, ULONG cb, USHORT cbWantContig)
{
    NOREF(pSfFsi); NOREF(pSfFsd); NOREF(cb); NOREF(cbWantContig);
    return ERROR_NOT_SUPPORTED;
}


DECLASM(APIRET)
FS32_DOPAGEIO(PSFFSI pSfFsi, PVBOXSFSYFI pSfFsd, struct PageCmdHeader *pList)
{
    NOREF(pSfFsi); NOREF(pSfFsd); NOREF(pList);
    return ERROR_NOT_SUPPORTED;
}

