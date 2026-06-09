/* $Id: tstGuestCtrlHGCM.cpp 114280 2026-06-09 07:31:19Z andreas.loeffler@oracle.com $ */
/** @file
 * Guest Control HGCM test cases.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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

#define LOG_ENABLED
#define LOG_GROUP LOG_GROUP_MAIN
#include <VBox/log.h>

#include "../include/GuestCtrlImplPrivate.h"

using namespace com;

#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct TSTGUESTCTRLHGCMBADPTR
{
    uintptr_t   uPtr;
    const char *pszDesc;
} TSTGUESTCTRLHGCMBADPTR;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static TSTGUESTCTRLHGCMBADPTR const g_aBadPtrs[] =
{
    { (uintptr_t)0x1,                         "low address" },
    { (uintptr_t)0xfff,                       "last byte before first page" },
#if UINTPTR_MAX > UINT32_MAX
    { (uintptr_t)UINT64_C(0x7fffffffffffffff), "top user-looking canonical boundary" },
    { (uintptr_t)UINT64_C(0x8000000000000000), "high-bit boundary" },
    { (uintptr_t)UINT64_C(0xff00000000000000), "tagged high byte" },
    { (uintptr_t)UINT64_C(0xffff800000000000), "kernel-space-looking canonical address" },
    { (uintptr_t)UINT64_C(0xffffffffffffffff), "all bits set" },
    { (uintptr_t)UINT64_C(0xdeadbeefdeadbeef), "deadbeef poison" },
    { (uintptr_t)UINT64_C(0xdeadbeefdeadbedf), "near-deadbeef poison" },
#endif
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/**
 * Initializes a valid guest directory entry test structure.
 *
 * @param   pDirEntryEx         Directory entry to initialize.
 * @param   pszName             Valid UTF-8 name to copy into the entry.
 * @param   pcbDirEntryEx       Where to return the effective entry size.
 */
static void tstInitDirEntry(PGSTCTLDIRENTRYEX pDirEntryEx, const char *pszName, uint32_t *pcbDirEntryEx)
{
    AssertPtrReturnVoid(pDirEntryEx);
    AssertPtrReturnVoid(pszName);
    AssertPtrReturnVoid(pcbDirEntryEx);

    size_t const cbName = strlen(pszName);
    AssertRelease(cbName < sizeof(pDirEntryEx->szName));

    memset(pDirEntryEx, 0, sizeof(*pDirEntryEx));
    memcpy(pDirEntryEx->szName, pszName, cbName + 1);
    pDirEntryEx->cbName = (uint16_t)cbName;
    *pcbDirEntryEx = (uint32_t)(RT_UOFFSETOF(GSTCTLDIRENTRYEX, szName) + cbName + 1);
}


/**
 * Initializes a guest directory entry using raw filename bytes.
 *
 * @param   pDirEntryEx         Directory entry to initialize.
 * @param   pbName              Raw filename bytes to copy.
 * @param   cbName              Number of bytes in @a pbName.
 * @param   pcbDirEntryEx       Where to return the effective entry size.
 */
static void tstInitDirEntryBytes(PGSTCTLDIRENTRYEX pDirEntryEx, const uint8_t *pbName, uint16_t cbName,
                                 uint32_t *pcbDirEntryEx)
{
    AssertPtrReturnVoid(pDirEntryEx);
    AssertPtrReturnVoid(pbName);
    AssertPtrReturnVoid(pcbDirEntryEx);
    AssertRelease(cbName < sizeof(pDirEntryEx->szName));

    memset(pDirEntryEx, 0, sizeof(*pDirEntryEx));
    memcpy(pDirEntryEx->szName, pbName, cbName);
    pDirEntryEx->szName[cbName] = '\0';
    pDirEntryEx->cbName = cbName;
    *pcbDirEntryEx = (uint32_t)(RT_UOFFSETOF(GSTCTLDIRENTRYEX, szName) + cbName + 1);
}


/**
 * Initializes a valid guest filesystem information test structure.
 *
 * @param   pFsInfo             Filesystem information structure to initialize.
 */
static void tstInitFsInfo(PGSTCTLFSINFO pFsInfo)
{
    AssertPtrReturnVoid(pFsInfo);

    RT_ZERO(*pFsInfo);
    pFsInfo->cbFree        = _1M;
    pFsInfo->cbTotalSize   = _1G;
    pFsInfo->cbBlockSize   = _4K;
    pFsInfo->cbSectorSize  = 512;
    pFsInfo->cMaxComponent = 255;
    RTStrCopy(pFsInfo->szName, sizeof(pFsInfo->szName), "ext4");
    RTStrCopy(pFsInfo->szLabel, sizeof(pFsInfo->szLabel), "root");
    pFsInfo->cbMountpoint = 0;
}


/**
 * Initializes a valid guest filesystem object information test structure.
 *
 * @param   pFsObjInfo          Filesystem object information structure to initialize.
 */
static void tstInitFsObjInfo(PGSTCTLFSOBJINFO pFsObjInfo)
{
    AssertPtrReturnVoid(pFsObjInfo);

    RT_ZERO(*pFsObjInfo);
    pFsObjInfo->cbObject           = 42;
    pFsObjInfo->cbAllocated        = _4K;
    pFsObjInfo->Attr.enmAdditional = GSTCTLFSOBJATTRADD_NOTHING;
}


/**
 * Returns the primary bad pointer candidate used by legacy single-pointer tests.
 *
 * @returns Bad pointer value.
 */
static void *tstBadPtr(void)
{
    return (void *)g_aBadPtrs[0].uPtr;
}


/**
 * Checks that a validator rejects all syntactically invalid bad pointer candidates.
 *
 * @tparam  PayloadType         Payload structure type accepted by @a pfnValidate.
 * @param   pszWhat             Human-readable payload name for debug output.
 * @param   pfnValidate         Validator function to call.
 * @param   cbPayload           Payload size to pass to @a pfnValidate.
 */
template<typename PayloadType>
static void tstCheckBadPtrs(const char *pszWhat, int (*pfnValidate)(const PayloadType *, uint32_t), uint32_t cbPayload)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aBadPtrs); i++)
    {
        const PayloadType * const pPayload = (const PayloadType *)g_aBadPtrs[i].uPtr;
        if (!RT_VALID_PTR(pPayload))
            RTTESTI_CHECK_RC(pfnValidate(pPayload, cbPayload), VERR_INVALID_POINTER);
        else
            RTTestIPrintf(RTTESTLVL_DEBUG, "Skipping RT_VALID_PTR bad %s pointer candidate '%s': %p\n",
                          pszWhat, g_aBadPtrs[i].pszDesc, pPayload);
    }
}


/**
 * Tests guest wait event payload copying and typed payload extraction.
 */
static void tstGuestWaitEventPayload(void)
{
    RTTestISub("GuestWaitEventPayload");

    CALLBACKDATA_FS_NOTIFY FsNotify;
    memset(&FsNotify, 0, sizeof(FsNotify));
    FsNotify.uType = GUEST_FS_NOTIFYTYPE_QUERY_INFO;
    FsNotify.rc    = VINF_SUCCESS;

    GuestWaitEventPayload Payload(GUEST_FS_NOTIFYTYPE_QUERY_INFO, &FsNotify, sizeof(FsNotify));
    RTTESTI_CHECK(Payload.Type() == GUEST_FS_NOTIFYTYPE_QUERY_INFO);
    RTTESTI_CHECK(Payload.Size() == sizeof(FsNotify));
    RTTESTI_CHECK(Payload.Raw() != NULL);
    RTTESTI_CHECK(Payload.Raw() != &FsNotify);

    FsNotify.uType = GUEST_FS_NOTIFYTYPE_CREATE_TEMP;
    PCALLBACKDATA_FS_NOTIFY pFsNotify = NULL;
    RTTESTI_CHECK_RC(guestCtrlEventPayloadGet(Payload, GUEST_FS_NOTIFYTYPE_QUERY_INFO, &pFsNotify), VINF_SUCCESS);
    RTTESTI_CHECK(pFsNotify != NULL);
    RTTESTI_CHECK(pFsNotify->uType == GUEST_FS_NOTIFYTYPE_QUERY_INFO);

    GuestWaitEventPayload PayloadCopy;
    RTTESTI_CHECK_RC(PayloadCopy.CopyFromDeep(Payload), VINF_SUCCESS);
    RTTESTI_CHECK(PayloadCopy.Type() == Payload.Type());
    RTTESTI_CHECK(PayloadCopy.Size() == Payload.Size());
    RTTESTI_CHECK(PayloadCopy.Raw() != NULL);
    RTTESTI_CHECK(PayloadCopy.Raw() != Payload.Raw());
    RTTESTI_CHECK(memcmp(PayloadCopy.Raw(), Payload.Raw(), Payload.Size()) == 0);

    ((PCALLBACKDATA_FS_NOTIFY)Payload.MutableRaw())->uType = GUEST_FS_NOTIFYTYPE_CREATE_TEMP;
    RTTESTI_CHECK(((PCALLBACKDATA_FS_NOTIFY)PayloadCopy.Raw())->uType == GUEST_FS_NOTIFYTYPE_QUERY_INFO);

    RTTESTI_CHECK_RC(guestCtrlEventPayloadGet(Payload, GUEST_DIR_NOTIFYTYPE_READ, &pFsNotify), VERR_INVALID_PARAMETER);
    RTTESTI_CHECK(pFsNotify == NULL);

    GuestWaitEventPayload ShortPayload(GUEST_FS_NOTIFYTYPE_QUERY_INFO, &FsNotify, sizeof(FsNotify) - 1);
    RTTESTI_CHECK_RC(guestCtrlEventPayloadGet(ShortPayload, GUEST_FS_NOTIFYTYPE_QUERY_INFO, &pFsNotify), VERR_INVALID_PARAMETER);
    RTTESTI_CHECK(pFsNotify == NULL);

    GuestWaitEventPayload EmptyPayload(GUEST_FS_NOTIFYTYPE_QUERY_INFO, NULL, 0);
    RTTESTI_CHECK_RC(guestCtrlEventPayloadGet(EmptyPayload, GUEST_FS_NOTIFYTYPE_QUERY_INFO, &pFsNotify), VERR_INVALID_PARAMETER);
    RTTESTI_CHECK(pFsNotify == NULL);

    RTTESTI_CHECK_RC(guestCtrlEventPayloadGet(PayloadCopy, GUEST_FS_NOTIFYTYPE_QUERY_INFO,
                                             (PCALLBACKDATA_FS_NOTIFY *)NULL), VERR_INVALID_POINTER);

    CALLBACKDATA_DIR_NOTIFY DirNotify;
    memset(&DirNotify, 0, sizeof(DirNotify));
    DirNotify.uType = GUEST_DIR_NOTIFYTYPE_READ;
    DirNotify.rc    = VINF_SUCCESS;
    GuestWaitEventPayload DirPayload(GUEST_DIR_NOTIFYTYPE_READ, &DirNotify, sizeof(DirNotify));
    PCALLBACKDATA_DIR_NOTIFY pDirNotify = NULL;
    RTTESTI_CHECK_RC(guestCtrlEventPayloadGet(DirPayload, GUEST_DIR_NOTIFYTYPE_READ, &pDirNotify), VINF_SUCCESS);
    RTTESTI_CHECK(pDirNotify != NULL);
    RTTESTI_CHECK(pDirNotify->uType == GUEST_DIR_NOTIFYTYPE_READ);

    void * const pvBad = tstBadPtr();
    if (!RT_VALID_PTR(pvBad))
    {
        int vrc = VINF_SUCCESS;
        try
        {
            GuestWaitEventPayload BadPayload(GUEST_FS_NOTIFYTYPE_QUERY_INFO, pvBad, sizeof(FsNotify));
            RT_NOREF(BadPayload);
        }
        catch (int vrcThrown)
        {
            vrc = vrcThrown;
        }
        RTTESTI_CHECK_RC(vrc, VERR_INVALID_POINTER);
    }
}


/**
 * Tests exact-size HGCM buffer extraction.
 */
static void tstHgcmGetBufExact(void)
{
    RTTestISub("guestCtrlHgcmGetBufExact");

    VBOXHGCMSVCPARM Parm;
    uint32_t uValue = 0x12345678;
    HGCMSvcSetPv(&Parm, &uValue, sizeof(uValue));

    const void *pvBuf = NULL;
    RTTESTI_CHECK_RC(guestCtrlHgcmGetBufExact(&Parm, &pvBuf, sizeof(uValue)), VINF_SUCCESS);
    RTTESTI_CHECK(pvBuf == &uValue);

    RTTESTI_CHECK_RC(guestCtrlHgcmGetBufExact(NULL, &pvBuf, sizeof(uValue)), VERR_INVALID_POINTER);
    RTTESTI_CHECK_RC(guestCtrlHgcmGetBufExact(&Parm, NULL, sizeof(uValue)), VERR_INVALID_POINTER);
    RTTESTI_CHECK_RC(guestCtrlHgcmGetBufExact(&Parm, &pvBuf, 0), VERR_INVALID_PARAMETER);
    RTTESTI_CHECK_RC(guestCtrlHgcmGetBufExact(&Parm, &pvBuf, sizeof(uValue) - 1), VERR_INVALID_PARAMETER);
    RTTESTI_CHECK_RC(guestCtrlHgcmGetBufExact(&Parm, &pvBuf, sizeof(uValue) + 1), VERR_INVALID_PARAMETER);

    HGCMSvcSetU32(&Parm, uValue);
    RTTESTI_CHECK_RC(guestCtrlHgcmGetBufExact(&Parm, &pvBuf, sizeof(uValue)), VERR_INVALID_PARAMETER);

    HGCMSvcSetPv(&Parm, &uValue, 0);
    RTTESTI_CHECK_RC(guestCtrlHgcmGetBufExact(&Parm, &pvBuf, sizeof(uValue)), VERR_INVALID_PARAMETER);

    HGCMSvcSetPv(&Parm, NULL, sizeof(uValue));
    RTTESTI_CHECK_RC(guestCtrlHgcmGetBufExact(&Parm, &pvBuf, sizeof(uValue)), VERR_INVALID_PARAMETER);

    void * const pvBad = tstBadPtr();
    if (!RT_VALID_PTR(pvBad))
    {
        HGCMSvcSetPv(&Parm, pvBad, sizeof(uValue));
        RTTESTI_CHECK_RC(guestCtrlHgcmGetBufExact(&Parm, &pvBuf, sizeof(uValue)), VERR_INVALID_PARAMETER);
    }
}


/**
 * Tests HGCM string validation and duplication into host-owned memory.
 */
static void tstHgcmGetStrDup(void)
{
    RTTestISub("guestCtrlHgcmGetStrDup");

    VBOXHGCMSVCPARM Parm;
    char szValue[] = "hello";
    HGCMSvcSetPv(&Parm, szValue, sizeof(szValue));

    char    *pszDup = NULL;
    uint32_t cbDup  = 0;
    RTTESTI_CHECK_RC(guestCtrlHgcmGetStrDup(&Parm, &pszDup, &cbDup, sizeof(szValue)), VINF_SUCCESS);
    RTTESTI_CHECK(pszDup != NULL);
    RTTESTI_CHECK(pszDup != szValue);
    RTTESTI_CHECK(strcmp(pszDup, szValue) == 0);
    RTTESTI_CHECK(cbDup == sizeof(szValue));
    RTStrFree(pszDup);

    char szUtf8[] = "guest-\xe2\x82\xac";
    HGCMSvcSetPv(&Parm, szUtf8, sizeof(szUtf8));
    pszDup = NULL;
    cbDup  = 0;
    RTTESTI_CHECK_RC(guestCtrlHgcmGetStrDup(&Parm, &pszDup, &cbDup, sizeof(szUtf8)), VINF_SUCCESS);
    RTTESTI_CHECK(pszDup != NULL);
    RTTESTI_CHECK(strcmp(pszDup, szUtf8) == 0);
    RTTESTI_CHECK(cbDup == sizeof(szUtf8));
    RTStrFree(pszDup);

    RTTESTI_CHECK_RC(guestCtrlHgcmGetStrDup(NULL, &pszDup, &cbDup, sizeof(szValue)), VERR_INVALID_POINTER);
    RTTESTI_CHECK_RC(guestCtrlHgcmGetStrDup(&Parm, NULL, &cbDup, sizeof(szValue)), VERR_INVALID_POINTER);
    RTTESTI_CHECK_RC(guestCtrlHgcmGetStrDup(&Parm, &pszDup, NULL, sizeof(szValue)), VERR_INVALID_POINTER);

    HGCMSvcSetPv(&Parm, szValue, sizeof(szValue));
    RTTESTI_CHECK_RC(guestCtrlHgcmGetStrDup(&Parm, &pszDup, &cbDup, sizeof(szValue) - 1), VERR_TOO_MUCH_DATA);
    RTTESTI_CHECK(pszDup == NULL);
    RTTESTI_CHECK(cbDup == 0);

    HGCMSvcSetU32(&Parm, 0);
    RTTESTI_CHECK_RC(guestCtrlHgcmGetStrDup(&Parm, &pszDup, &cbDup, sizeof(szValue)), VERR_INVALID_PARAMETER);

    HGCMSvcSetPv(&Parm, szValue, 0);
    RTTESTI_CHECK_RC(guestCtrlHgcmGetStrDup(&Parm, &pszDup, &cbDup, sizeof(szValue)), VERR_INVALID_PARAMETER);

    HGCMSvcSetPv(&Parm, NULL, sizeof(szValue));
    RTTESTI_CHECK_RC(guestCtrlHgcmGetStrDup(&Parm, &pszDup, &cbDup, sizeof(szValue)), VERR_INVALID_PARAMETER);

    char szUnterminated[] = { 'n', 'o' };
    HGCMSvcSetPv(&Parm, szUnterminated, sizeof(szUnterminated));
    RTTESTI_CHECK(RT_FAILURE(guestCtrlHgcmGetStrDup(&Parm, &pszDup, &cbDup, sizeof(szUnterminated))));
    RTTESTI_CHECK(pszDup == NULL);
    RTTESTI_CHECK(cbDup == 0);

    char szInvalidUtf8[] = { (char)0xc0, (char)0xaf, '\0' };
    HGCMSvcSetPv(&Parm, szInvalidUtf8, sizeof(szInvalidUtf8));
    RTTESTI_CHECK(RT_FAILURE(guestCtrlHgcmGetStrDup(&Parm, &pszDup, &cbDup, sizeof(szInvalidUtf8))));
    RTTESTI_CHECK(pszDup == NULL);
    RTTESTI_CHECK(cbDup == 0);

    char szTrailing[] = { 'o', 'k', '\0', 'x' };
    HGCMSvcSetPv(&Parm, szTrailing, sizeof(szTrailing));
    RTTESTI_CHECK(RT_FAILURE(guestCtrlHgcmGetStrDup(&Parm, &pszDup, &cbDup, sizeof(szTrailing))));
    RTTESTI_CHECK(pszDup == NULL);
    RTTESTI_CHECK(cbDup == 0);

    void * const pvBad = tstBadPtr();
    if (!RT_VALID_PTR(pvBad))
    {
        HGCMSvcSetPv(&Parm, pvBad, sizeof(szValue));
        RTTESTI_CHECK_RC(guestCtrlHgcmGetStrDup(&Parm, &pszDup, &cbDup, sizeof(szValue)), VERR_INVALID_PARAMETER);
    }
}


/**
 * Tests callback payload string validation.
 */
static void tstValidatePayloadString(void)
{
    RTTestISub("guestCtrlValidatePayloadString");

    char szPath[] = "/tmp/vbox-temp";
    RTTESTI_CHECK_RC(guestCtrlValidatePayloadString(szPath, sizeof(szPath), RTPATH_MAX), VINF_SUCCESS);

    char szUser[] = "vbox";
    RTTESTI_CHECK_RC(guestCtrlValidatePayloadString(szUser, sizeof(szUser), GSTCTL_DIRENTRY_MAX_USER_NAME), VINF_SUCCESS);

    char szEmpty[] = "";
    RTTESTI_CHECK_RC(guestCtrlValidatePayloadString(szEmpty, sizeof(szEmpty), GSTCTL_DIRENTRY_MAX_USER_NAME), VINF_SUCCESS);

    RTTESTI_CHECK_RC(guestCtrlValidatePayloadString(szPath, 0, RTPATH_MAX), VERR_INVALID_PARAMETER);
    RTTESTI_CHECK_RC(guestCtrlValidatePayloadString(NULL, sizeof(szPath), RTPATH_MAX), VERR_INVALID_POINTER);
    RTTESTI_CHECK_RC(guestCtrlValidatePayloadString(szPath, sizeof(szPath), sizeof(szPath) - 1), VERR_TOO_MUCH_DATA);

    char szUnterminated[] = { 'n', 'o' };
    RTTESTI_CHECK(RT_FAILURE(guestCtrlValidatePayloadString(szUnterminated, sizeof(szUnterminated), RTPATH_MAX)));

    char szInvalidUtf8[] = { (char)0xc0, (char)0xaf, '\0' };
    RTTESTI_CHECK(RT_FAILURE(guestCtrlValidatePayloadString(szInvalidUtf8, sizeof(szInvalidUtf8), RTPATH_MAX)));

    char szTrailing[] = { 'o', 'k', '\0', 'x' };
    RTTESTI_CHECK(RT_FAILURE(guestCtrlValidatePayloadString(szTrailing, sizeof(szTrailing), RTPATH_MAX)));

    void * const pvBad = tstBadPtr();
    if (!RT_VALID_PTR(pvBad))
        RTTESTI_CHECK_RC(guestCtrlValidatePayloadString((const char *)pvBad, sizeof(szPath), RTPATH_MAX), VERR_INVALID_POINTER);
}


/**
 * Tests guest filesystem object information validation.
 */
static void tstValidateFsObjInfo(void)
{
    RTTestISub("guestCtrlValidateFsObjInfo");

    GSTCTLFSOBJINFO FsObjInfo;
    tstInitFsObjInfo(&FsObjInfo);
    RTTESTI_CHECK_RC(guestCtrlValidateFsObjInfo(&FsObjInfo, sizeof(FsObjInfo)), VINF_SUCCESS);
    RTTESTI_CHECK_RC(guestCtrlValidateFsObjInfo(NULL, sizeof(FsObjInfo)), VERR_INVALID_POINTER);
    RTTESTI_CHECK_RC(guestCtrlValidateFsObjInfo(&FsObjInfo, sizeof(FsObjInfo) - 1), VERR_INVALID_PARAMETER);
    RTTESTI_CHECK_RC(guestCtrlValidateFsObjInfo(&FsObjInfo, sizeof(FsObjInfo) + 1), VERR_INVALID_PARAMETER);

    tstInitFsObjInfo(&FsObjInfo);
    FsObjInfo.Attr.enmAdditional = GSTCTLFSOBJATTRADD_UNIX;
    RTTESTI_CHECK_RC(guestCtrlValidateFsObjInfo(&FsObjInfo, sizeof(FsObjInfo)), VINF_SUCCESS);

    tstInitFsObjInfo(&FsObjInfo);
    FsObjInfo.Attr.enmAdditional = GSTCTLFSOBJATTRADD_EASIZE;
    RTTESTI_CHECK_RC(guestCtrlValidateFsObjInfo(&FsObjInfo, sizeof(FsObjInfo)), VINF_SUCCESS);

    tstInitFsObjInfo(&FsObjInfo);
    FsObjInfo.Attr.enmAdditional = (GSTCTLFSOBJATTRADD)0;
    RTTESTI_CHECK_RC(guestCtrlValidateFsObjInfo(&FsObjInfo, sizeof(FsObjInfo)), VERR_INVALID_PARAMETER);

    tstInitFsObjInfo(&FsObjInfo);
    FsObjInfo.Attr.enmAdditional = (GSTCTLFSOBJATTRADD)(GSTCTLFSOBJATTRADD_LAST + 1);
    RTTESTI_CHECK_RC(guestCtrlValidateFsObjInfo(&FsObjInfo, sizeof(FsObjInfo)), VERR_INVALID_PARAMETER);

    tstInitFsObjInfo(&FsObjInfo);
    FsObjInfo.Attr.enmAdditional = GSTCTLFSOBJATTRADD_UNIX_OWNER;
    RTStrCopy(FsObjInfo.Attr.u.UnixOwner.szName, sizeof(FsObjInfo.Attr.u.UnixOwner.szName), "owner");
    RTTESTI_CHECK_RC(guestCtrlValidateFsObjInfo(&FsObjInfo, sizeof(FsObjInfo)), VINF_SUCCESS);

    tstInitFsObjInfo(&FsObjInfo);
    FsObjInfo.Attr.enmAdditional = GSTCTLFSOBJATTRADD_UNIX_OWNER;
    memset(FsObjInfo.Attr.u.UnixOwner.szName, 'A', sizeof(FsObjInfo.Attr.u.UnixOwner.szName));
    RTTESTI_CHECK(RT_FAILURE(guestCtrlValidateFsObjInfo(&FsObjInfo, sizeof(FsObjInfo))));

    tstInitFsObjInfo(&FsObjInfo);
    FsObjInfo.Attr.enmAdditional = GSTCTLFSOBJATTRADD_UNIX_OWNER;
    FsObjInfo.Attr.u.UnixOwner.szName[0] = (char)0xc0;
    FsObjInfo.Attr.u.UnixOwner.szName[1] = (char)0xaf;
    FsObjInfo.Attr.u.UnixOwner.szName[2] = '\0';
    RTTESTI_CHECK(RT_FAILURE(guestCtrlValidateFsObjInfo(&FsObjInfo, sizeof(FsObjInfo))));

    tstInitFsObjInfo(&FsObjInfo);
    FsObjInfo.Attr.enmAdditional = GSTCTLFSOBJATTRADD_UNIX_GROUP;
    RTStrCopy(FsObjInfo.Attr.u.UnixGroup.szName, sizeof(FsObjInfo.Attr.u.UnixGroup.szName), "group");
    RTTESTI_CHECK_RC(guestCtrlValidateFsObjInfo(&FsObjInfo, sizeof(FsObjInfo)), VINF_SUCCESS);

    tstInitFsObjInfo(&FsObjInfo);
    FsObjInfo.Attr.enmAdditional = GSTCTLFSOBJATTRADD_UNIX_GROUP;
    memset(FsObjInfo.Attr.u.UnixGroup.szName, 'B', sizeof(FsObjInfo.Attr.u.UnixGroup.szName));
    RTTESTI_CHECK(RT_FAILURE(guestCtrlValidateFsObjInfo(&FsObjInfo, sizeof(FsObjInfo))));

    tstInitFsObjInfo(&FsObjInfo);
    FsObjInfo.Attr.enmAdditional = GSTCTLFSOBJATTRADD_UNIX_GROUP;
    FsObjInfo.Attr.u.UnixGroup.szName[0] = (char)0xc0;
    FsObjInfo.Attr.u.UnixGroup.szName[1] = (char)0xaf;
    FsObjInfo.Attr.u.UnixGroup.szName[2] = '\0';
    RTTESTI_CHECK(RT_FAILURE(guestCtrlValidateFsObjInfo(&FsObjInfo, sizeof(FsObjInfo))));

    tstCheckBadPtrs<GSTCTLFSOBJINFO>("GSTCTLFSOBJINFO", guestCtrlValidateFsObjInfo, sizeof(GSTCTLFSOBJINFO));
}


/**
 * Tests guest filesystem information validation.
 */
static void tstValidateFsInfo(void)
{
    RTTestISub("guestCtrlValidateFsInfo");

    GSTCTLFSINFO FsInfo;
    tstInitFsInfo(&FsInfo);
    RTTESTI_CHECK_RC(guestCtrlValidateFsInfo(&FsInfo, sizeof(FsInfo)), VINF_SUCCESS);

    RTTESTI_CHECK_RC(guestCtrlValidateFsInfo(NULL, sizeof(FsInfo)), VERR_INVALID_POINTER);
    RTTESTI_CHECK_RC(guestCtrlValidateFsInfo(&FsInfo, sizeof(FsInfo) - 1), VERR_INVALID_PARAMETER);
    RTTESTI_CHECK_RC(guestCtrlValidateFsInfo(&FsInfo, sizeof(FsInfo) + 1), VERR_INVALID_PARAMETER);
    tstCheckBadPtrs<GSTCTLFSINFO>("GSTCTLFSINFO", guestCtrlValidateFsInfo, sizeof(GSTCTLFSINFO));

    tstInitFsInfo(&FsInfo);
    FsInfo.cbMountpoint = sizeof(FsInfo.szMountpoint);
    FsInfo.szMountpoint[0] = '\0';
    RTTESTI_CHECK_RC(guestCtrlValidateFsInfo(&FsInfo, sizeof(FsInfo)), VINF_SUCCESS);

    tstInitFsInfo(&FsInfo);
    memset(FsInfo.szName, 'A', sizeof(FsInfo.szName));
    RTTESTI_CHECK(RT_FAILURE(guestCtrlValidateFsInfo(&FsInfo, sizeof(FsInfo))));

    tstInitFsInfo(&FsInfo);
    memset(FsInfo.szLabel, 'B', sizeof(FsInfo.szLabel));
    RTTESTI_CHECK(RT_FAILURE(guestCtrlValidateFsInfo(&FsInfo, sizeof(FsInfo))));

    tstInitFsInfo(&FsInfo);
    FsInfo.szName[0] = (char)0xc0;
    FsInfo.szName[1] = (char)0xaf;
    FsInfo.szName[2] = '\0';
    RTTESTI_CHECK(RT_FAILURE(guestCtrlValidateFsInfo(&FsInfo, sizeof(FsInfo))));

    tstInitFsInfo(&FsInfo);
    FsInfo.szLabel[0] = (char)0xc0;
    FsInfo.szLabel[1] = (char)0xaf;
    FsInfo.szLabel[2] = '\0';
    RTTESTI_CHECK(RT_FAILURE(guestCtrlValidateFsInfo(&FsInfo, sizeof(FsInfo))));

    tstInitFsInfo(&FsInfo);
    FsInfo.cbMountpoint = (uint16_t)(sizeof(FsInfo.szMountpoint) + 1);
    RTTESTI_CHECK_RC(guestCtrlValidateFsInfo(&FsInfo, sizeof(FsInfo)), VERR_INVALID_PARAMETER);

    tstInitFsInfo(&FsInfo);
    FsInfo.cbMountpoint = UINT16_MAX;
    RTTESTI_CHECK_RC(guestCtrlValidateFsInfo(&FsInfo, sizeof(FsInfo)), VERR_INVALID_PARAMETER);

    tstInitFsInfo(&FsInfo);
    FsInfo.cbMountpoint = sizeof(FsInfo.szMountpoint);
    FsInfo.szMountpoint[0] = 'x';
    RTTESTI_CHECK(RT_FAILURE(guestCtrlValidateFsInfo(&FsInfo, sizeof(FsInfo))));
}


/**
 * Tests guest directory entry validation.
 */
static void tstValidateDirEntryEx(void)
{
    RTTestISub("guestCtrlValidateDirEntryEx");

    GSTCTLDIRENTRYEX DirEntry;
    uint32_t cbDirEntryEx = 0;
    tstInitDirEntry(&DirEntry, "file.txt", &cbDirEntryEx);
    RTTESTI_CHECK_RC(guestCtrlValidateDirEntryEx(&DirEntry, cbDirEntryEx), VINF_SUCCESS);

    tstInitDirEntry(&DirEntry, "\xd1\x84\xd0\xb0\xd0\xb9\xd0\xbb.txt", &cbDirEntryEx);
    RTTESTI_CHECK_RC(guestCtrlValidateDirEntryEx(&DirEntry, cbDirEntryEx), VINF_SUCCESS);

    RTTESTI_CHECK_RC(guestCtrlValidateDirEntryEx(NULL, cbDirEntryEx), VERR_INVALID_POINTER);
    RTTESTI_CHECK_RC(guestCtrlValidateDirEntryEx(&DirEntry, RT_UOFFSETOF(GSTCTLDIRENTRYEX, szName[2]) - 1),
                     VERR_INVALID_PARAMETER);
    RTTESTI_CHECK_RC(guestCtrlValidateDirEntryEx(&DirEntry, GSTCTL_DIRENTRY_MAX_SIZE + 1), VERR_INVALID_PARAMETER);

    tstInitDirEntry(&DirEntry, "short.txt", &cbDirEntryEx);
    DirEntry.cwcShortName = (uint16_t)(RT_ELEMENTS(DirEntry.wszShortName) + 1);
    RTTESTI_CHECK_RC(guestCtrlValidateDirEntryEx(&DirEntry, cbDirEntryEx), VERR_INVALID_PARAMETER);

    tstInitDirEntry(&DirEntry, "size.txt", &cbDirEntryEx);
    RTTESTI_CHECK_RC(guestCtrlValidateDirEntryEx(&DirEntry, cbDirEntryEx - 1), VERR_INVALID_PARAMETER);

    tstInitDirEntry(&DirEntry, "term.txt", &cbDirEntryEx);
    DirEntry.szName[DirEntry.cbName] = 'x';
    RTTESTI_CHECK_RC(guestCtrlValidateDirEntryEx(&DirEntry, cbDirEntryEx), VERR_INVALID_PARAMETER);

    uint8_t const abInvalidUtf8[] = { 0xc0, 0xaf };
    tstInitDirEntryBytes(&DirEntry, abInvalidUtf8, sizeof(abInvalidUtf8), &cbDirEntryEx);
    RTTESTI_CHECK(RT_FAILURE(guestCtrlValidateDirEntryEx(&DirEntry, cbDirEntryEx)));

    uint8_t const abEmbeddedZero[] = { 'a', '\0', 'b' };
    tstInitDirEntryBytes(&DirEntry, abEmbeddedZero, sizeof(abEmbeddedZero), &cbDirEntryEx);
    RTTESTI_CHECK(RT_FAILURE(guestCtrlValidateDirEntryEx(&DirEntry, cbDirEntryEx)));

    void * const pvBad = tstBadPtr();
    if (!RT_VALID_PTR(pvBad))
        RTTESTI_CHECK_RC(guestCtrlValidateDirEntryEx((PCGSTCTLDIRENTRYEX)pvBad, cbDirEntryEx), VERR_INVALID_POINTER);
}


int main()
{
    RTTEST hTest;
    int vrc = RTTestInitAndCreate("tstGuestCtrlHGCM", &hTest);
    if (vrc)
        return vrc;
    RTTestBanner(hTest);

    RTTestIPrintf(RTTESTLVL_DEBUG, "Initializing COM...\n");
    HRESULT hrc = com::Initialize();
    if (FAILED(hrc))
    {
        RTTestFailed(hTest, "Failed to initialize COM (%Rhrc)!\n", hrc);
        return RTEXITCODE_FAILURE;
    }

    RTAssertSetQuiet(true);
    RTAssertSetMayPanic(false);

    tstGuestWaitEventPayload();
    tstHgcmGetBufExact();
    tstHgcmGetStrDup();
    tstValidatePayloadString();
    tstValidateFsObjInfo();
    tstValidateFsInfo();
    tstValidateDirEntryEx();

    RTTestIPrintf(RTTESTLVL_DEBUG, "Shutting down COM...\n");
    com::Shutdown();

    return RTTestSummaryAndDestroy(hTest);
}
