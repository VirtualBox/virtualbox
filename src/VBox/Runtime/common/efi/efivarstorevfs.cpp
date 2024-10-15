/* $Id: efivarstorevfs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Expose a EFI variable store as a Virtual Filesystem.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#include <iprt/efi.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/crc.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/utf16.h>
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>
#include <iprt/formats/efi-fv.h>
#include <iprt/formats/efi-varstore.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to the varstore filesystem data. */
typedef struct RTEFIVARSTORE *PRTEFIVARSTORE;


/**
 * EFI variable entry.
 */
typedef struct RTEFIVAR
{
    /** Pointer to the owning variable store. */
    PRTEFIVARSTORE      pVarStore;
    /** Offset of the variable data located in the backing image - 0 if not written yet. */
    uint64_t            offVarData;
    /** Pointer to the in memory data, NULL if not yet read. */
    void                *pvData;
    /** Monotonic counter value. */
    uint64_t            cMonotonic;
    /** Size of the variable data in bytes. */
    uint32_t            cbData;
    /** Index of the assoicated public key. */
    uint32_t            idPubKey;
    /** Attributes for the variable. */
    uint32_t            fAttr;
    /** Flag whether the variable was deleted. */
    bool                fDeleted;
    /** Name of the variable. */
    char                *pszName;
    /** The raw EFI timestamp as read from the header. */
    EFI_TIME            EfiTimestamp;
    /** The creation/update time. */
    RTTIMESPEC          Time;
    /** The vendor UUID of the variable. */
    RTUUID              Uuid;
} RTEFIVAR;
/** Pointer to an EFI variable. */
typedef RTEFIVAR *PRTEFIVAR;


/**
 * EFI GUID entry.
 */
typedef struct RTEFIGUID
{
    /** The UUID representation of the GUID. */
    RTUUID              Uuid;
    /** Pointer to the array of indices into RTEFIVARSTORE::paVars. */
    uint32_t            *paidxVars;
    /** Number of valid indices in the array. */
    uint32_t            cVars;
    /** Maximum number of indices the array can hold. */
    uint32_t            cVarsMax;
} RTEFIGUID;
/** Pointer to an EFI variable. */
typedef RTEFIGUID *PRTEFIGUID;


/**
 * EFI variable store filesystem volume.
 */
typedef struct RTEFIVARSTORE
{
    /** Handle to itself. */
    RTVFS               hVfsSelf;
    /** The file, partition, or whatever backing the volume has. */
    RTVFSFILE           hVfsBacking;
    /** The size of the backing thingy. */
    uint64_t            cbBacking;

    /** RTVFSMNT_F_XXX. */
    uint32_t            fMntFlags;
    /** RTEFIVARSTOREVFS_F_XXX (currently none defined). */
    uint32_t            fVarStoreFlags;

    /** Size of the variable store (minus the header). */
    uint64_t            cbVarStore;
    /** Start offset into the backing image where the variable data starts. */
    uint64_t            offStoreData;
    /** Flag whether the variable store uses authenticated variables. */
    bool                fAuth;
    /** Number of bytes occupied by existing variables. */
    uint64_t            cbVarData;

    /** Pointer to the array of variables sorted by start offset. */
    PRTEFIVAR           paVars;
    /** Number of valid variables in the array. */
    uint32_t            cVars;
    /** Maximum number of variables the array can hold. */
    uint32_t            cVarsMax;

    /** Pointer to the array of vendor GUIDS. */
    PRTEFIGUID          paGuids;
    /** Number of valid GUIDS in the array. */
    uint32_t            cGuids;
    /** Maximum number of GUIDS the array can hold. */
    uint32_t            cGuidsMax;

} RTEFIVARSTORE;


/**
 * Variable store directory type.
 */
typedef enum RTEFIVARSTOREDIRTYPE
{
    /** Invalid directory type. */
    RTEFIVARSTOREDIRTYPE_INVALID = 0,
    /** Root directory type. */
    RTEFIVARSTOREDIRTYPE_ROOT,
    /** 'by-name' directory. */
    RTEFIVARSTOREDIRTYPE_BY_NAME,
    /** 'by-uuid' directory. */
    RTEFIVARSTOREDIRTYPE_BY_GUID,
    /** 'raw' directory. */
    RTEFIVARSTOREDIRTYPE_RAW,
    /** Specific 'by-uuid/{...}' directory. */
    RTEFIVARSTOREDIRTYPE_GUID,
    /** Specific 'raw/{...}' directory. */
    RTEFIVARSTOREDIRTYPE_RAW_ENTRY,
    /** 32bit blowup hack. */
    RTEFIVARSTOREDIRTYPE_32BIT_HACK = 0x7fffffff
} RTEFIVARSTOREDIRTYPE;


/**
 * EFI variable store directory entry.
 */
typedef struct RTEFIVARSTOREDIRENTRY
{
    /** Name of the directory if constant. */
    const char              *pszName;
    /** Size of the name. */
    size_t                  cbName;
    /** Entry type. */
    RTEFIVARSTOREDIRTYPE    enmType;
    /** Parent entry type. */
    RTEFIVARSTOREDIRTYPE    enmParentType;
} RTEFIVARSTOREDIRENTRY;
/** Pointer to a EFI variable store directory entry. */
typedef RTEFIVARSTOREDIRENTRY *PRTEFIVARSTOREDIRENTRY;
/** Pointer to a const EFI variable store directory entry. */
typedef const RTEFIVARSTOREDIRENTRY *PCRTEFIVARSTOREDIRENTRY;


/**
 * Variable store directory.
 */
typedef struct RTEFIVARSTOREDIR
{
    /* Flag whether we reached the end of directory entries. */
    bool                    fNoMoreFiles;
    /** The index of the next item to read. */
    uint32_t                idxNext;
    /** Directory entry. */
    PCRTEFIVARSTOREDIRENTRY pEntry;
    /** The variable store associated with this directory. */
    PRTEFIVARSTORE          pVarStore;
    /** Time when the directory was created. */
    RTTIMESPEC              Time;
    /** Pointer to the GUID entry, only valid for RTEFIVARSTOREDIRTYPE_GUID. */
    PRTEFIGUID              pGuid;
    /** The variable ID, only valid for RTEFIVARSTOREDIRTYPE_RAW_ENTRY. */
    uint32_t                idVar;
} RTEFIVARSTOREDIR;
/** Pointer to an Variable store directory. */
typedef RTEFIVARSTOREDIR *PRTEFIVARSTOREDIR;


/**
 * File type.
 */
typedef enum RTEFIVARSTOREFILETYPE
{
    /** Invalid type, do not use. */
    RTEFIVARSTOREFILETYPE_INVALID = 0,
    /** File accesses the data portion of the variable. */
    RTEFIVARSTOREFILETYPE_DATA,
    /** File accesses the attributes of the variable. */
    RTEFIVARSTOREFILETYPE_ATTR,
    /** File accesses the UUID of the variable. */
    RTEFIVARSTOREFILETYPE_UUID,
    /** File accesses the public key index of the variable. */
    RTEFIVARSTOREFILETYPE_PUBKEY,
    /** File accesses the raw EFI Time of the variable. */
    RTEFIVARSTOREFILETYPE_TIME,
    /** The monotonic counter (deprecated). */
    RTEFIVARSTOREFILETYPE_MONOTONIC,
    /** 32bit hack. */
    RTEFIVARSTOREFILETYPE_32BIT_HACK = 0x7fffffff
} RTEFIVARSTOREFILETYPE;


/**
 * Raw file type entry.
 */
typedef struct RTEFIVARSTOREFILERAWENTRY
{
    /** Name of the entry. */
    const char              *pszName;
    /** The associated file type. */
    RTEFIVARSTOREFILETYPE   enmType;
    /** File size of the object, 0 if dynamic. */
    size_t                  cbObject;
    /** Offset of the item in the variable header. */
    uint32_t                offObject;
} RTEFIVARSTOREFILERAWENTRY;
/** Pointer to a raw file type entry. */
typedef RTEFIVARSTOREFILERAWENTRY *PRTEFIVARSTOREFILERAWENTRY;
/** Pointer to a const file type entry. */
typedef const RTEFIVARSTOREFILERAWENTRY *PCRTEFIVARSTOREFILERAWENTRY;


/**
 * Open file instance.
 */
typedef struct RTEFIVARFILE
{
    /** The file type. */
    PCRTEFIVARSTOREFILERAWENTRY pEntry;
    /** Variable store this file belongs to. */
    PRTEFIVARSTORE              pVarStore;
    /** The underlying variable structure. */
    PRTEFIVAR                   pVar;
    /** Current offset into the file for I/O. */
    RTFOFF                      offFile;
} RTEFIVARFILE;
/** Pointer to an open file instance. */
typedef RTEFIVARFILE *PRTEFIVARFILE;


/**
 * Directories.
 */
static const RTEFIVARSTOREDIRENTRY g_aDirs[] =
{
    { NULL,      0,            RTEFIVARSTOREDIRTYPE_ROOT,      RTEFIVARSTOREDIRTYPE_ROOT    },
    { RT_STR_TUPLE("by-name"), RTEFIVARSTOREDIRTYPE_BY_NAME,   RTEFIVARSTOREDIRTYPE_ROOT    },
    { RT_STR_TUPLE("by-uuid"), RTEFIVARSTOREDIRTYPE_BY_GUID,   RTEFIVARSTOREDIRTYPE_ROOT    },
    { RT_STR_TUPLE("raw"),     RTEFIVARSTOREDIRTYPE_RAW,       RTEFIVARSTOREDIRTYPE_ROOT    },
    { NULL,      0,            RTEFIVARSTOREDIRTYPE_GUID,      RTEFIVARSTOREDIRTYPE_BY_GUID },
    { NULL,      0,            RTEFIVARSTOREDIRTYPE_RAW_ENTRY, RTEFIVARSTOREDIRTYPE_RAW     },
};


/**
 * Raw files for accessing specific items in the variable header.
 */
static const RTEFIVARSTOREFILERAWENTRY g_aRawFiles[] =
{
    { "attr",      RTEFIVARSTOREFILETYPE_ATTR,      sizeof(uint32_t), RT_UOFFSETOF(RTEFIVAR, fAttr)        },
    { "data",      RTEFIVARSTOREFILETYPE_DATA,                     0,                                    0 },
    { "uuid",      RTEFIVARSTOREFILETYPE_UUID,      sizeof(RTUUID),   RT_UOFFSETOF(RTEFIVAR, Uuid)         },
    { "pubkey",    RTEFIVARSTOREFILETYPE_PUBKEY,    sizeof(uint32_t), RT_UOFFSETOF(RTEFIVAR, idPubKey)     },
    { "time",      RTEFIVARSTOREFILETYPE_TIME,      sizeof(EFI_TIME), RT_UOFFSETOF(RTEFIVAR, EfiTimestamp) },
    { "monotonic", RTEFIVARSTOREFILETYPE_MONOTONIC, sizeof(uint64_t), RT_UOFFSETOF(RTEFIVAR, cMonotonic)   }
};

#define RTEFIVARSTORE_FILE_ENTRY_DATA 1


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtEfiVarStore_NewDirByType(PRTEFIVARSTORE pThis, RTEFIVARSTOREDIRTYPE enmDirType,
                                      PRTEFIGUID pGuid, uint32_t idVar, PRTVFSOBJ phVfsObj);


#ifdef LOG_ENABLED

/**
 * Logs a firmware volume header.
 *
 * @param   pFvHdr              The firmware volume header.
 */
static void rtEfiVarStoreFvHdr_Log(PCEFI_FIRMWARE_VOLUME_HEADER pFvHdr)
{
    if (LogIs2Enabled())
    {
        Log2(("EfiVarStore: Volume Header:\n"));
        Log2(("EfiVarStore:   abZeroVec                   %#.*Rhxs\n", sizeof(pFvHdr->abZeroVec), &pFvHdr->abZeroVec[0]));
        Log2(("EfiVarStore:   GuidFilesystem              %#.*Rhxs\n", sizeof(pFvHdr->GuidFilesystem), &pFvHdr->GuidFilesystem));
        Log2(("EfiVarStore:   cbFv                        %#RX64\n", RT_LE2H_U64(pFvHdr->cbFv)));
        Log2(("EfiVarStore:   u32Signature                %#RX32\n", RT_LE2H_U32(pFvHdr->u32Signature)));
        Log2(("EfiVarStore:   fAttr                       %#RX32\n", RT_LE2H_U32(pFvHdr->fAttr)));
        Log2(("EfiVarStore:   cbFvHdr                     %#RX16\n", RT_LE2H_U16(pFvHdr->cbFvHdr)));
        Log2(("EfiVarStore:   u16Chksum                   %#RX16\n", RT_LE2H_U16(pFvHdr->u16Chksum)));
        Log2(("EfiVarStore:   offExtHdr                   %#RX16\n", RT_LE2H_U16(pFvHdr->offExtHdr)));
        Log2(("EfiVarStore:   bRsvd                       %#RX8\n", pFvHdr->bRsvd));
        Log2(("EfiVarStore:   bRevision                   %#RX8\n", pFvHdr->bRevision));
    }
}


/**
 * Logs a variable store header.
 *
 * @param   pStoreHdr           The variable store header.
 */
static void rtEfiVarStoreHdr_Log(PCEFI_VARSTORE_HEADER pStoreHdr)
{
    if (LogIs2Enabled())
    {
        Log2(("EfiVarStore: Variable Store Header:\n"));
        Log2(("EfiVarStore:   GuidVarStore                %#.*Rhxs\n", sizeof(pStoreHdr->GuidVarStore), &pStoreHdr->GuidVarStore));
        Log2(("EfiVarStore:   cbVarStore                  %#RX32\n", RT_LE2H_U32(pStoreHdr->cbVarStore)));
        Log2(("EfiVarStore:   bFmt                        %#RX8\n", pStoreHdr->bFmt));
        Log2(("EfiVarStore:   bState                      %#RX8\n", pStoreHdr->bState));
    }
}


/**
 * Logs a authenticated variable header.
 *
 * @param   pVarHdr             The authenticated variable header.
 * @param   offVar              Offset of the authenticated variable header.
 */
static void rtEfiVarStoreAuthVarHdr_Log(PCEFI_AUTH_VAR_HEADER pVarHdr, uint64_t offVar)
{
    if (LogIs2Enabled())
    {
        Log2(("EfiVarStore: Authenticated Variable Header at offset %#RU64:\n", offVar));
        Log2(("EfiVarStore:   u16StartId                  %#RX16\n", RT_LE2H_U16(pVarHdr->u16StartId)));
        Log2(("EfiVarStore:   bState                      %#RX8\n", pVarHdr->bState));
        Log2(("EfiVarStore:   bRsvd                       %#RX8\n", pVarHdr->bRsvd));
        Log2(("EfiVarStore:   fAttr                       %#RX32\n", RT_LE2H_U32(pVarHdr->fAttr)));
        Log2(("EfiVarStore:   cMonotonic                  %#RX64\n", RT_LE2H_U64(pVarHdr->cMonotonic)));
        Log2(("EfiVarStore:   Timestamp.u16Year           %#RX16\n", RT_LE2H_U16(pVarHdr->Timestamp.u16Year)));
        Log2(("EfiVarStore:   Timestamp.u8Month           %#RX8\n", pVarHdr->Timestamp.u8Month));
        Log2(("EfiVarStore:   Timestamp.u8Day             %#RX8\n", pVarHdr->Timestamp.u8Day));
        Log2(("EfiVarStore:   Timestamp.u8Hour            %#RX8\n", pVarHdr->Timestamp.u8Hour));
        Log2(("EfiVarStore:   Timestamp.u8Minute          %#RX8\n", pVarHdr->Timestamp.u8Minute));
        Log2(("EfiVarStore:   Timestamp.u8Second          %#RX8\n", pVarHdr->Timestamp.u8Second));
        Log2(("EfiVarStore:   Timestamp.bPad0             %#RX8\n", pVarHdr->Timestamp.bPad0));
        Log2(("EfiVarStore:   Timestamp.u32Nanosecond     %#RX32\n", RT_LE2H_U32(pVarHdr->Timestamp.u32Nanosecond)));
        Log2(("EfiVarStore:   Timestamp.iTimezone         %#RI16\n", RT_LE2H_S16(pVarHdr->Timestamp.iTimezone)));
        Log2(("EfiVarStore:   Timestamp.u8Daylight        %#RX8\n", pVarHdr->Timestamp.u8Daylight));
        Log2(("EfiVarStore:   Timestamp.bPad1             %#RX8\n", pVarHdr->Timestamp.bPad1));
        Log2(("EfiVarStore:   idPubKey                    %#RX32\n", RT_LE2H_U32(pVarHdr->idPubKey)));
        Log2(("EfiVarStore:   cbName                      %#RX32\n", RT_LE2H_U32(pVarHdr->cbName)));
        Log2(("EfiVarStore:   cbData                      %#RX32\n", RT_LE2H_U32(pVarHdr->cbData)));
        Log2(("EfiVarStore:   GuidVendor                  %#.*Rhxs\n", sizeof(pVarHdr->GuidVendor), &pVarHdr->GuidVendor));
    }
}

#endif /* LOG_ENABLED */

/**
 * Worker for rtEfiVarStoreFile_QueryInfo() and rtEfiVarStoreDir_QueryInfo().
 *
 * @returns IPRT status code.
 * @param   cbObject            Size of the object in bytes.
 * @param   fIsDir              Flag whether the object is a directory or file.
 * @param   pTime               The time to use.
 * @param   pObjInfo            The FS object information structure to fill in.
 * @param   enmAddAttr          What to fill in.
 */
static int rtEfiVarStore_QueryInfo(uint64_t cbObject, bool fIsDir, PCRTTIMESPEC pTime, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    pObjInfo->cbObject              = cbObject;
    pObjInfo->cbAllocated           = cbObject;
    pObjInfo->AccessTime            = *pTime;
    pObjInfo->ModificationTime      = *pTime;
    pObjInfo->ChangeTime            = *pTime;
    pObjInfo->BirthTime             = *pTime;
    pObjInfo->Attr.fMode            =   fIsDir
                                      ? RTFS_TYPE_DIRECTORY | RTFS_UNIX_ALL_ACCESS_PERMS
                                      : RTFS_TYPE_FILE | RTFS_UNIX_IWOTH | RTFS_UNIX_IROTH
                                                       | RTFS_UNIX_IWGRP | RTFS_UNIX_IRGRP
                                                       | RTFS_UNIX_IWUSR | RTFS_UNIX_IRUSR;
    pObjInfo->Attr.enmAdditional    = enmAddAttr;

    switch (enmAddAttr)
    {
        case RTFSOBJATTRADD_NOTHING: RT_FALL_THRU();
        case RTFSOBJATTRADD_UNIX:
            pObjInfo->Attr.u.Unix.uid           = NIL_RTUID;
            pObjInfo->Attr.u.Unix.gid           = NIL_RTGID;
            pObjInfo->Attr.u.Unix.cHardlinks    = 1;
            pObjInfo->Attr.u.Unix.INodeIdDevice = 0;
            pObjInfo->Attr.u.Unix.INodeId       = 0;
            pObjInfo->Attr.u.Unix.fFlags        = 0;
            pObjInfo->Attr.u.Unix.GenerationId  = 0;
            pObjInfo->Attr.u.Unix.Device        = 0;
            break;
        case RTFSOBJATTRADD_UNIX_OWNER:
            pObjInfo->Attr.u.UnixOwner.uid       = 0;
            pObjInfo->Attr.u.UnixOwner.szName[0] = '\0';
            break;
        case RTFSOBJATTRADD_UNIX_GROUP:
            pObjInfo->Attr.u.UnixGroup.gid       = 0;
            pObjInfo->Attr.u.UnixGroup.szName[0] = '\0';
            break;
        case RTFSOBJATTRADD_EASIZE:
            pObjInfo->Attr.u.EASize.cb = 0;
            break;
        default:
            return VERR_INVALID_PARAMETER;
    }
    return VINF_SUCCESS;
}


/**
 * Tries to find and return the GUID entry for the given UUID.
 *
 * @returns Pointer to the GUID entry or NULL if not found.
 * @param   pThis               The EFI variable store instance.
 * @param   pUuid               The UUID to look for.
 */
static PRTEFIGUID rtEfiVarStore_GetGuid(PRTEFIVARSTORE pThis, PCRTUUID pUuid)
{
    for (uint32_t i = 0; i < pThis->cGuids; i++)
        if (!RTUuidCompare(&pThis->paGuids[i].Uuid, pUuid))
            return &pThis->paGuids[i];

    return NULL;
}


/**
 * Adds the given UUID to the array of known GUIDs.
 *
 * @returns Pointer to the GUID entry or NULL if out of memory.
 * @param   pThis               The EFI variable store instance.
 * @param   pUuid               The UUID to add.
 */
static PRTEFIGUID rtEfiVarStore_AddGuid(PRTEFIVARSTORE pThis, PCRTUUID pUuid)
{
    if (pThis->cGuids == pThis->cGuidsMax)
    {
        /* Grow the array. */
        uint32_t cGuidsMaxNew = pThis->cGuidsMax + 10;
        PRTEFIGUID paGuidsNew = (PRTEFIGUID)RTMemRealloc(pThis->paGuids, cGuidsMaxNew * sizeof(RTEFIGUID));
        if (!paGuidsNew)
            return NULL;

        pThis->paGuids   = paGuidsNew;
        pThis->cGuidsMax = cGuidsMaxNew;
    }

    PRTEFIGUID pGuid = &pThis->paGuids[pThis->cGuids++];
    pGuid->Uuid      = *pUuid;
    pGuid->paidxVars = NULL;
    pGuid->cVars     = 0;
    pGuid->cVarsMax  = 0;
    return pGuid;
}


/**
 * Adds the given variable to the GUID array.
 *
 * @returns IPRT status code.
 * @param   pThis               The EFI variable store instance.
 * @param   pUuid               The UUID of the variable.
 * @param   idVar               The variable index into the array.
 */
static int rtEfiVarStore_AddVarByGuid(PRTEFIVARSTORE pThis, PCRTUUID pUuid, uint32_t idVar)
{
    PRTEFIGUID pGuid = rtEfiVarStore_GetGuid(pThis, pUuid);
    if (!pGuid)
        pGuid = rtEfiVarStore_AddGuid(pThis, pUuid);

    if (   pGuid
        && pGuid->cVars == pGuid->cVarsMax)
    {
        /* Grow the array. */
        uint32_t cVarsMaxNew = pGuid->cVarsMax + 10;
        uint32_t *paidxVarsNew = (uint32_t *)RTMemRealloc(pGuid->paidxVars, cVarsMaxNew * sizeof(uint32_t));
        if (!paidxVarsNew)
            return VERR_NO_MEMORY;

        pGuid->paidxVars = paidxVarsNew;
        pGuid->cVarsMax  = cVarsMaxNew;
    }

    int rc = VINF_SUCCESS;
    if (pGuid)
        pGuid->paidxVars[pGuid->cVars++] = idVar;
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Reads variable data from the given memory area.
 *
 * @returns IPRT status code.
 * @param   pThis               The EFI variable file instance.
 * @param   pbData              Pointer to the start of the data.
 * @param   cbData              Size of the variable data in bytes.
 * @param   off                 Where to start reading relative from the data start offset.
 * @param   pSgBuf              Where to store the read data.
 * @param   pcbRead             Where to return the number of bytes read, optional.
 */
static int rtEfiVarStoreFile_ReadMem(PRTEFIVARFILE pThis, const uint8_t *pbData, size_t cbData,
                                     RTFOFF off, PRTSGBUF pSgBuf, size_t *pcbRead)
{
    int          rc;
    size_t       cbSeg      = 0;
    void * const pvSeg      = RTSgBufGetCurrentSegment(pSgBuf, ~(size_t)0, &cbSeg);
    size_t const cbDataLeft = (uint64_t)off < cbData ? cbData - (size_t)off : 0;
    if (!pcbRead)
    {
        if (cbSeg <= cbDataLeft)
        {
            memcpy(pvSeg, &pbData[off], cbSeg);
            pThis->offFile = off + cbSeg;
            Log6(("rtEfiVarStoreFile_ReadMem: off=%#RX64 cbSeg=%#x -> VINF_SUCCESS\n", off, cbSeg));
            RTSgBufAdvance(pSgBuf, cbSeg);
            rc = VINF_SUCCESS;
        }
        else
        {
            Log6(("rtEfiVarStoreFile_ReadMem: off=%#RX64 cbSeg=%#x -> VERR_EOF (cbDataLeft=%#x)\n", off, cbSeg, cbDataLeft));
            rc = VERR_EOF;
        }
    }
    else if (cbDataLeft == 0)
    {
        Log6(("rtEfiVarStoreFile_ReadMem: off=%#RX64 cbSeg=%#x -> VINF_EOF *pcbRead=0\n", off, cbSeg));
        *pcbRead = 0;
        rc = VINF_EOF;
    }
    else
    {
        size_t const cbThisRead = RT_MAX(cbSeg, cbDataLeft);
        memcpy(pvSeg, &pbData[off], cbThisRead);

        pThis->offFile = off + cbThisRead;
        *pcbRead = cbThisRead;
        RTSgBufAdvance(pSgBuf, cbThisRead);

        /* Return VINF_EOF if beyond end-of-file. */
        rc = cbThisRead == cbSeg ? VINF_SUCCESS : VINF_EOF;
        Log6(("rtEfiVarStoreFile_ReadMem: off=%#RX64 cbSeg=%#x -> %Rrc *pcbRead=%#x\n", off, cbSeg, rc, cbThisRead));
    }

    return rc;
}


/**
 * Writes variable data from the given memory area.
 *
 * @returns IPRT status code.
 * @param   pThis               The EFI variable file instance.
 * @param   pbData              Pointer to the start of the data.
 * @param   cbData              Size of the variable data in bytes.
 * @param   off                 Where to start writing relative from the data start offset.
 * @param   pSgBuf              The data to write.
 * @param   pcbWritten          Where to return the number of bytes written, optional.
 */
static int rtEfiVarStoreFile_WriteMem(PRTEFIVARFILE pThis, uint8_t *pbData, size_t cbData,
                                      RTFOFF off, PRTSGBUF pSgBuf, size_t *pcbWritten)
{
    int                rc;
    size_t             cbSeg      = 0;
    void const * const pvSeg      = RTSgBufGetCurrentSegment(pSgBuf, ~(size_t)0, &cbSeg);
    size_t const       cbDataLeft = (uint64_t)off < cbData ? cbData - (size_t)off : 0;
    if (!pcbWritten)
    {
        if (cbSeg <= cbDataLeft)
        {
            memcpy(&pbData[off], pvSeg, cbSeg);
            pThis->offFile = off + cbSeg;
            RTSgBufAdvance(pSgBuf, cbSeg);
            Log6(("rtEfiVarStoreFile_WriteMem: off=%#RX64 cbSeg=%#x -> VINF_SUCCESS\n", off, cbSeg));
            rc = VINF_SUCCESS;
        }
        else
        {
            Log6(("rtEfiVarStoreFile_WriteMem: off=%#RX64 cbSeg=%#x -> VERR_EOF\n", off, cbSeg));
            rc = VERR_EOF; /** @todo r=bird: This status seems bogus. Use VERR_FILE_TOO_BIG instead? */
        }
    }
    else if (cbDataLeft == 0)
    {
        *pcbWritten = 0;
        rc = VINF_EOF; /** @todo r=bird: This is weird. Implementing the read EOF protocol for a write... */
    }
    else
    {
        size_t const cbThisWrite = RT_MAX(cbSeg, cbDataLeft);
        memcpy(&pbData[off], pvSeg, cbThisWrite);

        pThis->offFile = off + cbThisWrite;
        *pcbWritten = cbThisWrite;
        RTSgBufAdvance(pSgBuf, cbThisWrite);

        /* Return VINF_EOF if beyond end-of-file. */
        /** @todo r=bird: This is weird. Implementing the read EOF protocol for a write... */
        rc = cbThisWrite == cbSeg ? VINF_SUCCESS : VINF_EOF;
        Log6(("rtEfiVarStoreFile_WriteMem: off=%#RX64 cbSeg=%#x -> %Rrc *pcbWritten=%#x\n", off, cbSeg, rc, cbThisWrite));
    }
    return rc;
}


/**
 * Reads variable data from the given range.
 *
 * @returns IPRT status code.
 * @param   pThis               The EFI variable file instance.
 * @param   offData             Where the data starts in the backing storage.
 * @param   cbData              Size of the variable data in bytes.
 * @param   off                 Where to start reading relative from the data start offset.
 * @param   pSgBuf              Where to store the read data.
 * @param   pcbRead             Where to return the number of bytes read, optional.
 */
static int rtEfiVarStoreFile_ReadFile(PRTEFIVARFILE pThis, uint64_t offData, size_t cbData,
                                      RTFOFF off, PRTSGBUF pSgBuf, size_t *pcbRead)
{
    int                  rc;
    PRTEFIVARSTORE const pVarStore  = pThis->pVarStore;
    size_t               cbSeg      = 0;
    void * const         pvSeg      = RTSgBufGetCurrentSegment(pSgBuf, ~(size_t)0, &cbSeg);
    size_t const         cbDataLeft = (uint64_t)off < cbData ? cbData - (size_t)off : 0;
    uint64_t const       offBacking = offData + off;
    if (!pcbRead)
    {
        if (cbSeg <= cbDataLeft)
        {
            rc = RTVfsFileSgRead(pVarStore->hVfsBacking, offBacking, pSgBuf, true /*fBlocking*/, NULL /*pcbRead*/);
            if (RT_SUCCESS(rc))
                pThis->offFile = off + cbSeg;
            Log6(("rtEfiVarStoreFile_ReadFile: off=%#RX64 cbSeg=%#x -> %Rrc\n", off, cbSeg, rc));
        }
        else
        {
            Log6(("rtEfiVarStoreFile_ReadFile: off=%#RX64 cbSeg=%#x cbDataLeft=%#zx -> VERR_EOF\n", off, cbSeg, cbDataLeft));
            rc = VERR_EOF;
        }
    }
    else if (cbDataLeft == 0)
    {
        Log6(("rtEfiVarStoreFile_ReadFile: off=%#RX64 cbSeg=%#x -> VINF_EOF\n", off, cbSeg));
        *pcbRead = 0;
        rc = VINF_EOF;
    }
    else
    {
        size_t const cbThisRead = RT_MAX(cbSeg, cbDataLeft);
        rc = RTVfsFileReadAt(pVarStore->hVfsBacking, offBacking, pvSeg, cbThisRead, NULL /*pcbRead*/);
        if (RT_SUCCESS(rc))
        {
            pThis->offFile = off + cbThisRead;
            *pcbRead = cbThisRead;
            RTSgBufAdvance(pSgBuf, cbThisRead);

            /* Return VINF_EOF if beyond end-of-file. */
            rc = cbThisRead == cbSeg ? VINF_SUCCESS : VINF_EOF;
        }
        else
            *pcbRead = 0;
        Log6(("rtFsEfiVarStore_Read: off=%#RX64 cbSeg=%#x -> %Rrc *pcbRead=%#x\n", off, cbSeg, rc, *pcbRead));
    }
    return rc;
}


/**
 * Ensures that the variable data is available before any modification.
 *
 * @returns IPRT status code.
 * @param   pVar                The variable instance.
 */
static int rtEfiVarStore_VarReadData(PRTEFIVAR pVar)
{
    if (RT_LIKELY(   !pVar->offVarData
                  || !pVar->cbData))
        return VINF_SUCCESS;

    Assert(!pVar->pvData);
    pVar->pvData = RTMemAlloc(pVar->cbData);
    if (RT_UNLIKELY(!pVar->pvData))
        return VERR_NO_MEMORY;

    PRTEFIVARSTORE pVarStore = pVar->pVarStore;
    int rc = RTVfsFileReadAt(pVarStore->hVfsBacking, pVar->offVarData, pVar->pvData, pVar->cbData, NULL);
    if (RT_SUCCESS(rc))
        pVar->offVarData = 0; /* Marks the variable data as in memory. */
    else
    {
        RTMemFree(pVar->pvData);
        pVar->pvData = NULL;
    }

    return rc;
}


/**
 * Ensures that the given variable has the given data size.
 *
 * @returns IPRT status code.
 * @retval  VERR_DISK_FULL if the new size would exceed the variable storage size.
 * @param   pVar                The variable instance.
 * @param   cbData              New number of bytes of data for the variable.
 */
static int rtEfiVarStore_VarEnsureDataSz(PRTEFIVAR pVar, size_t cbData)
{
    PRTEFIVARSTORE pVarStore = pVar->pVarStore;

    if (pVar->cbData == cbData)
        return VINF_SUCCESS;

    if ((uint32_t)cbData != cbData)
        return VERR_FILE_TOO_BIG;

    int rc = VINF_SUCCESS;
    if (cbData < pVar->cbData)
    {
        /* Shrink. */
        void *pvNew = RTMemRealloc(pVar->pvData, cbData);
        if (pvNew)
        {
            pVar->pvData = pvNew;
            pVarStore->cbVarData -= pVar->cbData - cbData;
            pVar->cbData = (uint32_t)cbData;
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else if (cbData > pVar->cbData)
    {
        /* Grow. */
        if (pVarStore->cbVarStore - pVarStore->cbVarData >= cbData - pVar->cbData)
        {
            void *pvNew = RTMemRealloc(pVar->pvData, cbData);
            if (pvNew)
            {
                pVar->pvData = pvNew;
                pVarStore->cbVarData += cbData - pVar->cbData;
                pVar->cbData          = (uint32_t)cbData;
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_DISK_FULL;
    }

    return rc;
}


/**
 * Flush the variable store to the backing storage. This will remove any
 * deleted variables in the backing storage.
 *
 * @returns IPRT status code.
 * @param   pThis               The EFI variable store instance.
 */
static int rtEfiVarStore_Flush(PRTEFIVARSTORE pThis)
{
    int rc = VINF_SUCCESS;
    uint64_t offCur = pThis->offStoreData;

    for (uint32_t i = 0; i < pThis->cVars && RT_SUCCESS(rc); i++)
    {
        PRTUTF16 pwszName = NULL;
        size_t cwcLen = 0;
        PRTEFIVAR pVar = &pThis->paVars[i];

        if (!pVar->fDeleted)
        {
            rc = RTStrToUtf16Ex(pVar->pszName, RTSTR_MAX, &pwszName, 0, &cwcLen);
            if (RT_SUCCESS(rc))
            {
                cwcLen++; /* Include the terminator. */

                /* Read in the data of the variable if it exists. */
                rc = rtEfiVarStore_VarReadData(pVar);
                if (RT_SUCCESS(rc))
                {
                    /* Write out the variable. */
                    EFI_AUTH_VAR_HEADER VarHdr;
                    size_t cbName = cwcLen * sizeof(RTUTF16);

                    VarHdr.u16StartId = RT_H2LE_U16(EFI_AUTH_VAR_HEADER_START);
                    VarHdr.bState     = EFI_AUTH_VAR_HEADER_STATE_ADDED;
                    VarHdr.bRsvd      = 0;
                    VarHdr.fAttr      = RT_H2LE_U32(pVar->fAttr);
                    VarHdr.cMonotonic = RT_H2LE_U64(pVar->cMonotonic);
                    VarHdr.idPubKey   = RT_H2LE_U32(pVar->idPubKey);
                    VarHdr.cbName     = RT_H2LE_U32((uint32_t)cbName);
                    VarHdr.cbData     = RT_H2LE_U32(pVar->cbData);
                    RTEfiGuidFromUuid(&VarHdr.GuidVendor, &pVar->Uuid);
                    memcpy(&VarHdr.Timestamp, &pVar->EfiTimestamp, sizeof(pVar->EfiTimestamp));

                    rc = RTVfsFileWriteAt(pThis->hVfsBacking, offCur, &VarHdr, sizeof(VarHdr), NULL);
                    if (RT_SUCCESS(rc))
                        rc = RTVfsFileWriteAt(pThis->hVfsBacking, offCur + sizeof(VarHdr), pwszName, cbName, NULL);
                    if (RT_SUCCESS(rc))
                        rc = RTVfsFileWriteAt(pThis->hVfsBacking, offCur + sizeof(VarHdr) + cbName, pVar->pvData, pVar->cbData, NULL);
                    if (RT_SUCCESS(rc))
                    {
                        offCur += sizeof(VarHdr) + cbName + pVar->cbData;
                        uint64_t offCurAligned = RT_ALIGN_64(offCur, sizeof(uint32_t));
                        if (offCurAligned > offCur)
                        {
                            /* Should be at most 3 bytes to align the next variable to a 32bit boundary. */
                            Assert(offCurAligned - offCur <= 3);
                            uint8_t abFill[3] = { 0xff };
                            rc = RTVfsFileWriteAt(pThis->hVfsBacking, offCur, &abFill[0], offCurAligned - offCur, NULL);
                        }

                        offCur = offCurAligned;
                    }
                }

                RTUtf16Free(pwszName);
            }
        }
    }

    if (RT_SUCCESS(rc))
    {
        /* Fill the remainder with 0xff as it would be the case for a real NAND flash device. */
        uint8_t abFF[512];
        memset(&abFF[0], 0xff, sizeof(abFF));

        uint64_t offStart = offCur;
        uint64_t offEnd   = pThis->offStoreData + pThis->cbVarStore;
        while (   offStart < offEnd
               && RT_SUCCESS(rc))
        {
            size_t cbThisWrite = RT_MIN(sizeof(abFF), offEnd - offStart);
            rc = RTVfsFileWriteAt(pThis->hVfsBacking, offStart, &abFF[0], cbThisWrite, NULL);
            offStart += cbThisWrite;
        }
    }

    return rc;
}


/**
 * Tries to find a variable with the given name.
 *
 * @returns Pointer to the variable if found or NULL otherwise.
 * @param   pThis               The variable store instance.
 * @param   pszName             Name of the variable to look for.
 * @param   pidVar              Where to store the index of the variable, optional.
 */
static PRTEFIVAR rtEfiVarStore_VarGet(PRTEFIVARSTORE pThis, const char *pszName, uint32_t *pidVar)
{
    for (uint32_t i = 0; i < pThis->cVars; i++)
        if (   !pThis->paVars[i].fDeleted
            && !strcmp(pszName, pThis->paVars[i].pszName))
        {
            if (pidVar)
                *pidVar = i;
            return &pThis->paVars[i];
        }

    return NULL;
}


/**
 * Maybe grows the array of variables to hold more entries.
 *
 * @returns IPRT status code.
 * @param   pThis               The variable store instance.
 */
static int rtEfiVarStore_VarMaybeGrowEntries(PRTEFIVARSTORE pThis)
{
    if (pThis->cVars == pThis->cVarsMax)
    {
        /* Grow the variable array. */
        uint32_t cVarsMaxNew = pThis->cVarsMax + 10;
        PRTEFIVAR paVarsNew = (PRTEFIVAR)RTMemRealloc(pThis->paVars, cVarsMaxNew * sizeof(RTEFIVAR));
        if (!paVarsNew)
            return VERR_NO_MEMORY;

        pThis->paVars   = paVarsNew;
        pThis->cVarsMax = cVarsMaxNew;
    }

    return VINF_SUCCESS;
}


/**
 * Add a variable with the given name.
 *
 * @returns Pointer to the entry or NULL if out of memory.
 * @param   pThis               The variable store instance.
 * @param   pszName             Name of the variable to add.
 * @param   pUuid               The UUID of the variable owner.
 * @param   pidVar              Where to store the variable index on success, optional
 */
static PRTEFIVAR rtEfiVarStore_VarAdd(PRTEFIVARSTORE pThis, const char *pszName, PCRTUUID pUuid, uint32_t *pidVar)
{
    Assert(!rtEfiVarStore_VarGet(pThis, pszName, NULL));

    int rc = rtEfiVarStore_VarMaybeGrowEntries(pThis);
    if (RT_SUCCESS(rc))
    {
        PRTEFIVAR pVar = &pThis->paVars[pThis->cVars];
        RT_ZERO(*pVar);

        pVar->pszName = RTStrDup(pszName);
        if (pVar->pszName)
        {
            pVar->pVarStore  = pThis;
            pVar->offVarData = 0;
            pVar->fDeleted   = false;
            pVar->Uuid       = *pUuid;
            RTTimeNow(&pVar->Time);

            rc = rtEfiVarStore_AddVarByGuid(pThis, pUuid, pThis->cVars);
            AssertRC(rc); /** @todo */

            if (pidVar)
                *pidVar = pThis->cVars;
            pThis->cVars++;
            return pVar;
        }
    }

    return NULL;
}


/**
 * Delete the given variable.
 *
 * @returns IPRT status code.
 * @param   pThis               The variable store instance.
 * @param   pVar                The variable.
 */
static int rtEfiVarStore_VarDel(PRTEFIVARSTORE pThis, PRTEFIVAR pVar)
{
    pVar->fDeleted = true;
    if (pVar->pvData)
        RTMemFree(pVar->pvData);
    pVar->pvData = NULL;
    pThis->cbVarData -= sizeof(EFI_AUTH_VAR_HEADER) + pVar->cbData;
    /** @todo Delete from GUID entry. */
    return VINF_SUCCESS;
}


/**
 * Delete the variable with the given index.
 *
 * @returns IPRT status code.
 * @param   pThis               The variable store instance.
 * @param   idVar               The variable index.
 */
static int rtEfiVarStore_VarDelById(PRTEFIVARSTORE pThis, uint32_t idVar)
{
    return rtEfiVarStore_VarDel(pThis, &pThis->paVars[idVar]);
}


/**
 * Delete the variable with the given name.
 *
 * @returns IPRT status code.
 * @param   pThis               The variable store instance.
 * @param   pszName             Name of the variable to delete.
 */
static int rtEfiVarStore_VarDelByName(PRTEFIVARSTORE pThis, const char *pszName)
{
    PRTEFIVAR pVar = rtEfiVarStore_VarGet(pThis, pszName, NULL);
    if (pVar)
        return rtEfiVarStore_VarDel(pThis, pVar);

    return VERR_FILE_NOT_FOUND;
}


/*
 *
 * File operations.
 * File operations.
 * File operations.
 *
 */

/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtEfiVarStoreFile_Close(void *pvThis)
{
    PRTEFIVARFILE pThis = (PRTEFIVARFILE)pvThis;
    LogFlow(("rtEfiVarStoreFile_Close(%p/%p)\n", pThis, pThis->pVar));
    RT_NOREF(pThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtEfiVarStoreFile_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTEFIVARFILE pThis = (PRTEFIVARFILE)pvThis;
    uint64_t cbObject =   pThis->pEntry->cbObject > 0
                        ? pThis->pEntry->cbObject
                        : pThis->pVar->cbData;
    return rtEfiVarStore_QueryInfo(cbObject, false /*fIsDir*/, &pThis->pVar->Time, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtEfiVarStoreFile_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTEFIVARFILE pThis = (PRTEFIVARFILE)pvThis;
    PRTEFIVAR pVar = pThis->pVar;
    AssertReturn(pSgBuf->cSegs == 1, VERR_INTERNAL_ERROR_3);
    RT_NOREF(fBlocking);

    if (off == -1)
        off = pThis->offFile;
    else
        AssertReturn(off >= 0, VERR_INTERNAL_ERROR_3);

    int rc;
    if (pThis->pEntry->cbObject)
        rc = rtEfiVarStoreFile_ReadMem(pThis, (const uint8_t *)pVar + pThis->pEntry->offObject, pThis->pEntry->cbObject, off,
                                       pSgBuf, pcbRead);
    else
    {
        /* Data section. */
        if (!pVar->offVarData)
            rc = rtEfiVarStoreFile_ReadMem(pThis, (const uint8_t *)pVar->pvData, pVar->cbData, off, pSgBuf, pcbRead);
        else
            rc = rtEfiVarStoreFile_ReadFile(pThis, pVar->offVarData, pVar->cbData, off, pSgBuf, pcbRead);
    }

    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) rtEfiVarStoreFile_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    PRTEFIVARFILE pThis = (PRTEFIVARFILE)pvThis;
    PRTEFIVARSTORE pVarStore = pThis->pVarStore;
    PRTEFIVAR pVar = pThis->pVar;
    AssertReturn(pSgBuf->cSegs == 1, VERR_INTERNAL_ERROR_3);
    RT_NOREF(fBlocking);

    if (pVarStore->fMntFlags & RTVFSMNT_F_READ_ONLY)
        return VERR_WRITE_PROTECT;

    if (off == -1)
        off = pThis->offFile;
    else
        AssertReturn(off >= 0, VERR_INTERNAL_ERROR_3);

    int rc;
    if (pThis->pEntry->cbObject) /* These can't grow. */
        rc = rtEfiVarStoreFile_WriteMem(pThis, (uint8_t *)pVar + pThis->pEntry->offObject, pThis->pEntry->cbObject,
                                        off, pSgBuf, pcbWritten);
    else
    {
        /* Writing data section. */
        rc = rtEfiVarStore_VarReadData(pVar);
        if (RT_SUCCESS(rc))
        {
            if (off + pSgBuf->paSegs[0].cbSeg > pVar->cbData)
                rc = rtEfiVarStore_VarEnsureDataSz(pVar, off + pSgBuf->paSegs[0].cbSeg);
            if (RT_SUCCESS(rc))
                rc = rtEfiVarStoreFile_WriteMem(pThis, (uint8_t *)pVar->pvData, pVar->cbData, off, pSgBuf, pcbWritten);
        }
    }

    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtEfiVarStoreFile_Flush(void *pvThis)
{
    RT_NOREF(pvThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtEfiVarStoreFile_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTEFIVARFILE pThis = (PRTEFIVARFILE)pvThis;
    *poffActual = pThis->offFile;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtEfiVarStoreFile_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    RT_NOREF(pvThis, fMode, fMask);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtEfiVarStoreFile_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                                    PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    RT_NOREF(pvThis, pAccessTime, pModificationTime, pChangeTime, pBirthTime);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtEfiVarStoreFile_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    RT_NOREF(pvThis, uid, gid);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSeek}
 */
static DECLCALLBACK(int) rtEfiVarStoreFile_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual)
{
    PRTEFIVARFILE pThis = (PRTEFIVARFILE)pvThis;
    RTFOFF offNew;
    switch (uMethod)
    {
        case RTFILE_SEEK_BEGIN:
            offNew = offSeek;
            break;
        case RTFILE_SEEK_END:
            offNew = pThis->pVar->cbData + offSeek;
            break;
        case RTFILE_SEEK_CURRENT:
            offNew = (RTFOFF)pThis->offFile + offSeek;
            break;
        default:
            return VERR_INVALID_PARAMETER;
    }
    if (offNew >= 0)
    {
        pThis->offFile = offNew;
        *poffActual    = offNew;
        return VINF_SUCCESS;
    }
    return VERR_NEGATIVE_SEEK;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQuerySize}
 */
static DECLCALLBACK(int) rtEfiVarStoreFile_QuerySize(void *pvThis, uint64_t *pcbFile)
{
    PRTEFIVARFILE pThis = (PRTEFIVARFILE)pvThis;
    if (pThis->pEntry->cbObject)
        *pcbFile = pThis->pEntry->cbObject;
    else
        *pcbFile = (uint64_t)pThis->pVar->cbData;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSetSize}
 */
static DECLCALLBACK(int) rtEfiVarStoreFile_SetSize(void *pvThis, uint64_t cbFile, uint32_t fFlags)
{
    PRTEFIVARFILE pThis = (PRTEFIVARFILE)pvThis;
    PRTEFIVAR pVar = pThis->pVar;
    PRTEFIVARSTORE pVarStore = pThis->pVarStore;

    RT_NOREF(fFlags);

    if (pVarStore->fMntFlags & RTVFSMNT_F_READ_ONLY)
        return VERR_WRITE_PROTECT;

    int rc = rtEfiVarStore_VarReadData(pVar);
    if (RT_SUCCESS(rc))
        rc = rtEfiVarStore_VarEnsureDataSz(pVar, cbFile);

    return rc;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQueryMaxSize}
 */
static DECLCALLBACK(int) rtEfiVarStoreFile_QueryMaxSize(void *pvThis, uint64_t *pcbMax)
{
    RT_NOREF(pvThis);
    *pcbMax = UINT32_MAX;
    return VINF_SUCCESS;
}


/**
 * EFI variable store file operations.
 */
static const RTVFSFILEOPS g_rtEfiVarStoreFileOps =
{
    { /* Stream */
        { /* Obj */
            RTVFSOBJOPS_VERSION,
            RTVFSOBJTYPE_FILE,
            "EfiVarStore File",
            rtEfiVarStoreFile_Close,
            rtEfiVarStoreFile_QueryInfo,
            NULL,
            RTVFSOBJOPS_VERSION
        },
        RTVFSIOSTREAMOPS_VERSION,
        RTVFSIOSTREAMOPS_FEAT_NO_SG,
        rtEfiVarStoreFile_Read,
        rtEfiVarStoreFile_Write,
        rtEfiVarStoreFile_Flush,
        NULL /*PollOne*/,
        rtEfiVarStoreFile_Tell,
        NULL /*pfnSkip*/,
        NULL /*pfnZeroFill*/,
        RTVFSIOSTREAMOPS_VERSION,
    },
    RTVFSFILEOPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSFILEOPS, ObjSet) - RT_UOFFSETOF(RTVFSFILEOPS, Stream.Obj),
        rtEfiVarStoreFile_SetMode,
        rtEfiVarStoreFile_SetTimes,
        rtEfiVarStoreFile_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtEfiVarStoreFile_Seek,
    rtEfiVarStoreFile_QuerySize,
    rtEfiVarStoreFile_SetSize,
    rtEfiVarStoreFile_QueryMaxSize,
    RTVFSFILEOPS_VERSION
};


/**
 * Creates a new VFS file from the given regular file inode.
 *
 * @returns IPRT status code.
 * @param   pThis               The ext volume instance.
 * @param   fOpen               Open flags passed.
 * @param   pVar                The variable this file accesses.
 * @param   pEntry              File type entry.
 * @param   phVfsFile           Where to store the VFS file handle on success.
 * @param   pErrInfo            Where to record additional error information on error, optional.
 */
static int rtEfiVarStore_NewFile(PRTEFIVARSTORE pThis, uint64_t fOpen, PRTEFIVAR pVar,
                                 PCRTEFIVARSTOREFILERAWENTRY pEntry, PRTVFSOBJ phVfsObj)
{
    RTVFSFILE hVfsFile;
    PRTEFIVARFILE pNewFile;
    int rc = RTVfsNewFile(&g_rtEfiVarStoreFileOps, sizeof(*pNewFile), fOpen, pThis->hVfsSelf, NIL_RTVFSLOCK,
                          &hVfsFile, (void **)&pNewFile);
    if (RT_SUCCESS(rc))
    {
        pNewFile->pEntry    = pEntry;
        pNewFile->pVarStore = pThis;
        pNewFile->pVar      = pVar;
        pNewFile->offFile   = 0;

        *phVfsObj = RTVfsObjFromFile(hVfsFile);
        RTVfsFileRelease(hVfsFile);
        AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
    }

    return rc;
}



/*
 *
 * Directory instance methods
 * Directory instance methods
 * Directory instance methods
 *
 */

/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtEfiVarStoreDir_Close(void *pvThis)
{
    PRTEFIVARSTOREDIR pThis = (PRTEFIVARSTOREDIR)pvThis;
    LogFlowFunc(("pThis=%p\n", pThis));
    pThis->pVarStore = NULL;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtEfiVarStoreDir_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTEFIVARSTOREDIR pThis = (PRTEFIVARSTOREDIR)pvThis;
    LogFlowFunc(("\n"));
    return rtEfiVarStore_QueryInfo(1, true /*fIsDir*/, &pThis->Time, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtEfiVarStoreDir_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    LogFlowFunc(("\n"));
    RT_NOREF(pvThis, fMode, fMask);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtEfiVarStoreDir_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                                   PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    LogFlowFunc(("\n"));
    RT_NOREF(pvThis, pAccessTime, pModificationTime, pChangeTime, pBirthTime);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtEfiVarStoreDir_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    LogFlowFunc(("\n"));
    RT_NOREF(pvThis, uid, gid);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnOpen}
 */
static DECLCALLBACK(int) rtEfiVarStoreDir_Open(void *pvThis, const char *pszEntry, uint64_t fOpen,
                                               uint32_t fFlags, PRTVFSOBJ phVfsObj)
{
    LogFlowFunc(("pszEntry='%s' fOpen=%#RX64 fFlags=%#x\n", pszEntry, fOpen, fFlags));
    PRTEFIVARSTOREDIR pThis     = (PRTEFIVARSTOREDIR)pvThis;
    PRTEFIVARSTORE    pVarStore = pThis->pVarStore;
    int               rc        = VINF_SUCCESS;

    /*
     * Special cases '.' and '.'
     */
    if (pszEntry[0] == '.')
    {
        RTEFIVARSTOREDIRTYPE enmDirTypeNew = RTEFIVARSTOREDIRTYPE_INVALID;
        if (pszEntry[1] == '\0')
            enmDirTypeNew = pThis->pEntry->enmType;
        else if (pszEntry[1] == '.' && pszEntry[2] == '\0')
            enmDirTypeNew = pThis->pEntry->enmParentType;

        if (enmDirTypeNew != RTEFIVARSTOREDIRTYPE_INVALID)
        {
            if (fFlags & RTVFSOBJ_F_OPEN_DIRECTORY)
            {
                if (   (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN
                    || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN_CREATE)
                    rc = rtEfiVarStore_NewDirByType(pVarStore, enmDirTypeNew, NULL /*pGuid*/, 0 /*idVar*/, phVfsObj);
                else
                    rc = VERR_ACCESS_DENIED;
            }
            else
                rc = VERR_IS_A_DIRECTORY;
            return rc;
        }
    }

    /*
     * We can create or replace in certain directories.
     */
    if (   (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN
        || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN_CREATE
        || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_CREATE
        || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_CREATE_REPLACE)
    { /* likely */ }
    else
        return VERR_WRITE_PROTECT;

    switch (pThis->pEntry->enmType)
    {
        case RTEFIVARSTOREDIRTYPE_ROOT:
        {
            if (!strcmp(pszEntry, "by-name"))
                return rtEfiVarStore_NewDirByType(pVarStore, RTEFIVARSTOREDIRTYPE_BY_NAME,
                                                  NULL /*pGuid*/, 0 /*idVar*/, phVfsObj);
            else if (!strcmp(pszEntry, "by-uuid"))
                return rtEfiVarStore_NewDirByType(pVarStore, RTEFIVARSTOREDIRTYPE_BY_GUID,
                                                  NULL /*pGuid*/, 0 /*idVar*/, phVfsObj);
            else if (!strcmp(pszEntry, "raw"))
                return rtEfiVarStore_NewDirByType(pVarStore, RTEFIVARSTOREDIRTYPE_RAW,
                                                  NULL /*pGuid*/, 0 /*idVar*/, phVfsObj);
            else
                rc = VERR_FILE_NOT_FOUND;
            break;
        }
        case RTEFIVARSTOREDIRTYPE_GUID: /** @todo This looks through all variables, not only the ones with the GUID. */
        case RTEFIVARSTOREDIRTYPE_BY_NAME:
        case RTEFIVARSTOREDIRTYPE_RAW:
        {
            /* Look for the name. */
            uint32_t idVar = 0;
            PRTEFIVAR pVar = rtEfiVarStore_VarGet(pVarStore, pszEntry, &idVar);
            if (   !pVar
                && (   (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN_CREATE
                    || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_CREATE
                    || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_CREATE_REPLACE))
            {
                if (pThis->pEntry->enmType == RTEFIVARSTOREDIRTYPE_GUID)
                    pVar = rtEfiVarStore_VarAdd(pVarStore, pszEntry, &pThis->pGuid->Uuid, &idVar);
                else
                {
                    RTUUID UuidNull;
                    RTUuidClear(&UuidNull);
                    pVar = rtEfiVarStore_VarAdd(pVarStore, pszEntry, &UuidNull, &idVar);
                }

                if (!pVar)
                {
                    rc = VERR_NO_MEMORY;
                    break;
                }
            }

            if (pVar)
            {
                if (pThis->pEntry->enmType == RTEFIVARSTOREDIRTYPE_RAW)
                    return rtEfiVarStore_NewDirByType(pVarStore, RTEFIVARSTOREDIRTYPE_RAW_ENTRY,
                                                      NULL /*pGuid*/, idVar, phVfsObj);
                else
                    return rtEfiVarStore_NewFile(pVarStore, fOpen, pVar,
                                                 &g_aRawFiles[RTEFIVARSTORE_FILE_ENTRY_DATA], phVfsObj);
            }

            rc = VERR_FILE_NOT_FOUND;
            break;
        }
        case RTEFIVARSTOREDIRTYPE_BY_GUID:
        {
            /* Look for the name. */
            for (uint32_t i = 0; i < pVarStore->cGuids; i++)
            {
                PRTEFIGUID pGuid = &pVarStore->paGuids[i];
                char szUuid[RTUUID_STR_LENGTH];
                rc = RTUuidToStr(&pGuid->Uuid, szUuid, sizeof(szUuid));
                AssertRC(rc);

                if (!strcmp(pszEntry, szUuid))
                    return rtEfiVarStore_NewDirByType(pVarStore, RTEFIVARSTOREDIRTYPE_GUID,
                                                      pGuid, 0 /*idVar*/, phVfsObj);
            }

            rc = VERR_FILE_NOT_FOUND;
            break;
        }
        case RTEFIVARSTOREDIRTYPE_RAW_ENTRY:
        {
            /* Look for the name. */
            for (uint32_t i = 0; i < RT_ELEMENTS(g_aRawFiles); i++)
                if (!strcmp(pszEntry, g_aRawFiles[i].pszName))
                    return rtEfiVarStore_NewFile(pVarStore, fOpen, &pVarStore->paVars[pThis->idVar],
                                                 &g_aRawFiles[i], phVfsObj);

            rc = VERR_FILE_NOT_FOUND;
            break;
        }
        case RTEFIVARSTOREDIRTYPE_INVALID:
        default:
            AssertFailedReturn(VERR_INTERNAL_ERROR_3);
    }

    LogFlow(("rtEfiVarStoreDir_Open(%s): returns %Rrc\n", pszEntry, rc));
    return rc;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnCreateDir}
 */
static DECLCALLBACK(int) rtEfiVarStoreDir_CreateDir(void *pvThis, const char *pszSubDir, RTFMODE fMode, PRTVFSDIR phVfsDir)
{
    PRTEFIVARSTOREDIR pThis     = (PRTEFIVARSTOREDIR)pvThis;
    PRTEFIVARSTORE    pVarStore = pThis->pVarStore;
    LogFlowFunc(("\n"));

    RT_NOREF(fMode, phVfsDir);

    if (pVarStore->fMntFlags & RTVFSMNT_F_READ_ONLY)
        return VERR_WRITE_PROTECT;

    /* We support creating directories only for GUIDs and RAW variable entries. */
    int rc = VINF_SUCCESS;
    if (pThis->pEntry->enmType == RTEFIVARSTOREDIRTYPE_BY_GUID)
    {
        RTUUID Uuid;
        rc = RTUuidFromStr(&Uuid, pszSubDir);
        if (RT_FAILURE(rc))
            return VERR_NOT_SUPPORTED;

        PRTEFIGUID pGuid = rtEfiVarStore_GetGuid(pVarStore, &Uuid);
        if (pGuid)
            return VERR_ALREADY_EXISTS;

        pGuid = rtEfiVarStore_AddGuid(pVarStore, &Uuid);
        if (!pGuid)
            return VERR_NO_MEMORY;
    }
    else if (pThis->pEntry->enmType == RTEFIVARSTOREDIRTYPE_RAW)
    {
        PRTEFIVAR pVar = rtEfiVarStore_VarGet(pVarStore, pszSubDir, NULL /*pidVar*/);
        if (!pVar)
        {
            if (sizeof(EFI_AUTH_VAR_HEADER) < pVarStore->cbVarStore - pVarStore->cbVarData)
            {
                uint32_t idVar = 0;
                RTUUID UuidNull;
                RTUuidClear(&UuidNull);

                pVar = rtEfiVarStore_VarAdd(pVarStore, pszSubDir, &UuidNull, &idVar);
                if (pVar)
                    pVarStore->cbVarData += sizeof(EFI_AUTH_VAR_HEADER);
                else
                    rc = VERR_NO_MEMORY;
            }
            else
                rc = VERR_DISK_FULL;
        }
        else
            rc = VERR_ALREADY_EXISTS;
    }
    else
        rc = VERR_NOT_SUPPORTED;

    return rc;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnOpenSymlink}
 */
static DECLCALLBACK(int) rtEfiVarStoreDir_OpenSymlink(void *pvThis, const char *pszSymlink, PRTVFSSYMLINK phVfsSymlink)
{
    RT_NOREF(pvThis, pszSymlink, phVfsSymlink);
    LogFlowFunc(("\n"));
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnCreateSymlink}
 */
static DECLCALLBACK(int) rtEfiVarStoreDir_CreateSymlink(void *pvThis, const char *pszSymlink, const char *pszTarget,
                                                        RTSYMLINKTYPE enmType, PRTVFSSYMLINK phVfsSymlink)
{
    RT_NOREF(pvThis, pszSymlink, pszTarget, enmType, phVfsSymlink);
    LogFlowFunc(("\n"));
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnUnlinkEntry}
 */
static DECLCALLBACK(int) rtEfiVarStoreDir_UnlinkEntry(void *pvThis, const char *pszEntry, RTFMODE fType)
{
    PRTEFIVARSTOREDIR pThis     = (PRTEFIVARSTOREDIR)pvThis;
    PRTEFIVARSTORE    pVarStore = pThis->pVarStore;
    LogFlowFunc(("\n"));

    RT_NOREF(fType);

    if (pVarStore->fMntFlags & RTVFSMNT_F_READ_ONLY)
        return VERR_WRITE_PROTECT;

    if (   pThis->pEntry->enmType == RTEFIVARSTOREDIRTYPE_RAW
        || pThis->pEntry->enmType == RTEFIVARSTOREDIRTYPE_BY_NAME
        || pThis->pEntry->enmType == RTEFIVARSTOREDIRTYPE_GUID)
        return rtEfiVarStore_VarDelByName(pVarStore, pszEntry);
    else if (pThis->pEntry->enmType == RTEFIVARSTOREDIRTYPE_BY_GUID)
    {
        /* Look for the name. */
        for (uint32_t i = 0; i < pVarStore->cGuids; i++)
        {
            PRTEFIGUID pGuid = &pVarStore->paGuids[i];
            char szUuid[RTUUID_STR_LENGTH];
            int rc = RTUuidToStr(&pGuid->Uuid, szUuid, sizeof(szUuid));
            AssertRC(rc); RT_NOREF(rc);

            if (!strcmp(pszEntry, szUuid))
            {
                for (uint32_t iVar = 0; iVar < pGuid->cVars; iVar++)
                    rtEfiVarStore_VarDelById(pVarStore, pGuid->paidxVars[iVar]);

                if (pGuid->paidxVars)
                    RTMemFree(pGuid->paidxVars);
                pGuid->paidxVars = NULL;
                pGuid->cVars     = 0;
                pGuid->cVarsMax  = 0;
                RTUuidClear(&pGuid->Uuid);
                return VINF_SUCCESS;
            }
        }

        return VERR_FILE_NOT_FOUND;
    }

    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnRenameEntry}
 */
static DECLCALLBACK(int) rtEfiVarStoreDir_RenameEntry(void *pvThis, const char *pszEntry, RTFMODE fType, const char *pszNewName)
{
    RT_NOREF(pvThis, pszEntry, fType, pszNewName);
    LogFlowFunc(("\n"));
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnRewindDir}
 */
static DECLCALLBACK(int) rtEfiVarStoreDir_RewindDir(void *pvThis)
{
    PRTEFIVARSTOREDIR pThis = (PRTEFIVARSTOREDIR)pvThis;
    LogFlowFunc(("\n"));

    pThis->idxNext = 0;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnReadDir}
 */
static DECLCALLBACK(int) rtEfiVarStoreDir_ReadDir(void *pvThis, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry,
                                                  RTFSOBJATTRADD enmAddAttr)
{
    PRTEFIVARSTOREDIR pThis = (PRTEFIVARSTOREDIR)pvThis;
    PRTEFIVARSTORE    pVarStore = pThis->pVarStore;
    LogFlowFunc(("\n"));

    if (pThis->fNoMoreFiles)
        return VERR_NO_MORE_FILES;

    int        rc       = VINF_SUCCESS;
    char       aszUuid[RTUUID_STR_LENGTH];
    const char *pszName = NULL;
    size_t     cbName   = 0;
    uint64_t   cbObject = 0;
    bool       fIsDir   = false;
    bool       fNoMoreFiles = false;
    RTTIMESPEC   Time;
    PCRTTIMESPEC pTimeSpec = &Time;
    RTTimeNow(&Time);

    switch (pThis->pEntry->enmType)
    {
        case RTEFIVARSTOREDIRTYPE_ROOT:
        {
            if (pThis->idxNext == 0)
            {
                pszName  = "by-name";
                cbName   = sizeof("by-name");
                cbObject = 1;
                fIsDir   = true;
            }
            else if (pThis->idxNext == 1)
            {
                pszName  = "by-uuid";
                cbName   = sizeof("by-uuid");
                cbObject = 1;
                fIsDir   = true;
            }
            else if (pThis->idxNext == 2)
            {
                pszName  = "raw";
                cbName   = sizeof("raw");
                cbObject = 1;
                fIsDir   = true;
                fNoMoreFiles = true;
            }
            break;
        }
        case RTEFIVARSTOREDIRTYPE_BY_NAME:
        case RTEFIVARSTOREDIRTYPE_RAW:
        {
            PRTEFIVAR pVar = &pVarStore->paVars[pThis->idxNext];
            if (pThis->idxNext + 1 == pVarStore->cVars)
                fNoMoreFiles = true;
            pszName  = pVar->pszName;
            cbName   = strlen(pszName) + 1;
            cbObject = pVar->cbData;
            pTimeSpec = &pVar->Time;
            if (pThis->pEntry->enmType == RTEFIVARSTOREDIRTYPE_RAW)
                fIsDir = true;
            break;
        }
        case RTEFIVARSTOREDIRTYPE_BY_GUID:
        {
            PRTEFIGUID pGuid = &pVarStore->paGuids[pThis->idxNext];
            if (pThis->idxNext + 1 == pVarStore->cGuids)
                fNoMoreFiles = true;
            pszName  = &aszUuid[0];
            cbName   = sizeof(aszUuid);
            cbObject = 1;
            rc = RTUuidToStr(&pGuid->Uuid, &aszUuid[0], cbName);
            AssertRC(rc);
            break;
        }
        case RTEFIVARSTOREDIRTYPE_GUID:
        {
            PRTEFIGUID pGuid = pThis->pGuid;
            uint32_t   idVar = pGuid->paidxVars[pThis->idxNext];
            PRTEFIVAR  pVar  = &pVarStore->paVars[idVar];
            if (pThis->idxNext + 1 == pGuid->cVars)
                fNoMoreFiles = true;
            pszName  = pVar->pszName;
            cbName   = strlen(pszName) + 1;
            cbObject = pVar->cbData;
            pTimeSpec = &pVar->Time;
            break;
        }
        case RTEFIVARSTOREDIRTYPE_RAW_ENTRY:
        {
            PCRTEFIVARSTOREFILERAWENTRY pEntry = &g_aRawFiles[pThis->idxNext];
            PRTEFIVAR                   pVar   = &pVarStore->paVars[pThis->idVar];

            if (pThis->idxNext + 1 == RT_ELEMENTS(g_aRawFiles))
                fNoMoreFiles = true;
            pszName  = pEntry->pszName;
            cbName   = strlen(pszName) + 1;
            cbObject = pEntry->cbObject;
            if (!cbObject)
                cbObject = pVar->cbData;
            pTimeSpec = &pVar->Time;
            break;
        }
        case RTEFIVARSTOREDIRTYPE_INVALID:
        default:
            AssertFailedReturn(VERR_INTERNAL_ERROR_3);
    }

    if (cbName <= 255)
    {
        size_t const cbDirEntry = *pcbDirEntry;

        *pcbDirEntry = RT_UOFFSETOF_DYN(RTDIRENTRYEX, szName[cbName + 2]);
        if (*pcbDirEntry <= cbDirEntry)
        {
            memcpy(&pDirEntry->szName[0], pszName, cbName);
            pDirEntry->szName[cbName] = '\0';
            pDirEntry->cbName         = (uint16_t)cbName;
            rc = rtEfiVarStore_QueryInfo(cbObject, fIsDir, pTimeSpec, &pDirEntry->Info, enmAddAttr);
            if (RT_SUCCESS(rc))
            {
                pThis->fNoMoreFiles = fNoMoreFiles;
                pThis->idxNext++;
                return VINF_SUCCESS;
            }
        }
        else
            rc = VERR_BUFFER_OVERFLOW;
    }
    else
        rc = VERR_FILENAME_TOO_LONG;
    return rc;
}


/**
 * EFI variable store directory operations.
 */
static const RTVFSDIROPS g_rtEfiVarStoreDirOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_DIR,
        "EfiVarStore Dir",
        rtEfiVarStoreDir_Close,
        rtEfiVarStoreDir_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSDIROPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSDIROPS, ObjSet) - RT_UOFFSETOF(RTVFSDIROPS, Obj),
        rtEfiVarStoreDir_SetMode,
        rtEfiVarStoreDir_SetTimes,
        rtEfiVarStoreDir_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtEfiVarStoreDir_Open,
    NULL /* pfnFollowAbsoluteSymlink */,
    NULL /* pfnOpenFile */,
    NULL /* pfnOpenDir */,
    rtEfiVarStoreDir_CreateDir,
    rtEfiVarStoreDir_OpenSymlink,
    rtEfiVarStoreDir_CreateSymlink,
    NULL /* pfnQueryEntryInfo */,
    rtEfiVarStoreDir_UnlinkEntry,
    rtEfiVarStoreDir_RenameEntry,
    rtEfiVarStoreDir_RewindDir,
    rtEfiVarStoreDir_ReadDir,
    RTVFSDIROPS_VERSION,
};


static int rtEfiVarStore_NewDirByType(PRTEFIVARSTORE pThis, RTEFIVARSTOREDIRTYPE enmDirType,
                                      PRTEFIGUID pGuid, uint32_t idVar, PRTVFSOBJ phVfsObj)
{
    RTVFSDIR hVfsDir;
    PRTEFIVARSTOREDIR pDir;
    int rc = RTVfsNewDir(&g_rtEfiVarStoreDirOps, sizeof(*pDir), 0 /*fFlags*/, pThis->hVfsSelf, NIL_RTVFSLOCK,
                         &hVfsDir, (void **)&pDir);
    if (RT_SUCCESS(rc))
    {
        PCRTEFIVARSTOREDIRENTRY pEntry = NULL;

        for (uint32_t i = 0; i < RT_ELEMENTS(g_aDirs); i++)
            if (g_aDirs[i].enmType == enmDirType)
            {
                pEntry = &g_aDirs[i];
                break;
            }

        AssertPtr(pEntry);
        pDir->idxNext   = 0;
        pDir->pEntry    = pEntry;
        pDir->pVarStore = pThis;
        pDir->pGuid     = pGuid;
        pDir->idVar     = idVar;
        RTTimeNow(&pDir->Time);

        *phVfsObj = RTVfsObjFromDir(hVfsDir);
        RTVfsDirRelease(hVfsDir);
        AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
    }

    return rc;
}


/*
 *
 * Volume level code.
 * Volume level code.
 * Volume level code.
 *
 */

/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnClose}
 */
static DECLCALLBACK(int) rtEfiVarStore_Close(void *pvThis)
{
    PRTEFIVARSTORE pThis = (PRTEFIVARSTORE)pvThis;

    /* Write the variable store if in read/write mode. */
    if (!(pThis->fMntFlags & RTVFSMNT_F_READ_ONLY))
    {
        int rc = rtEfiVarStore_Flush(pThis);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Backing file and handles.
     */
    RTVfsFileRelease(pThis->hVfsBacking);
    pThis->hVfsBacking = NIL_RTVFSFILE;
    pThis->hVfsSelf    = NIL_RTVFS;
    if (pThis->paVars)
    {
        for (uint32_t i = 0; i < pThis->cVars; i++)
        {
            RTStrFree(pThis->paVars[i].pszName);
            if (pThis->paVars[i].pvData)
                RTMemFree(pThis->paVars[i].pvData);
        }

        RTMemFree(pThis->paVars);
        pThis->paVars   = NULL;
        pThis->cVars    = 0;
        pThis->cVarsMax = 0;
    }

    if (pThis->paGuids)
    {
        for (uint32_t i = 0; i < pThis->cGuids; i++)
        {
            PRTEFIGUID pGuid = &pThis->paGuids[i];

            if (pGuid->paidxVars)
            {
                RTMemFree(pGuid->paidxVars);
                pGuid->paidxVars = NULL;
            }
        }

        RTMemFree(pThis->paGuids);
        pThis->paGuids = NULL;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtEfiVarStore_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    RT_NOREF(pvThis, pObjInfo, enmAddAttr);
    return VERR_WRONG_TYPE;
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnOpenRoot}
 */
static DECLCALLBACK(int) rtEfiVarStore_OpenRoot(void *pvThis, PRTVFSDIR phVfsDir)
{
    PRTEFIVARSTORE pThis = (PRTEFIVARSTORE)pvThis;
    RTVFSOBJ hVfsObj;
    int rc = rtEfiVarStore_NewDirByType(pThis, RTEFIVARSTOREDIRTYPE_ROOT,
                                        NULL /*pGuid*/, 0 /*idVar*/, &hVfsObj);
    if (RT_SUCCESS(rc))
    {
        *phVfsDir = RTVfsObjToDir(hVfsObj);
        RTVfsObjRelease(hVfsObj);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


DECL_HIDDEN_CONST(const RTVFSOPS) g_rtEfiVarStoreOps =
{
    /* .Obj = */
    {
        /* .uVersion = */       RTVFSOBJOPS_VERSION,
        /* .enmType = */        RTVFSOBJTYPE_VFS,
        /* .pszName = */        "EfiVarStore",
        /* .pfnClose = */       rtEfiVarStore_Close,
        /* .pfnQueryInfo = */   rtEfiVarStore_QueryInfo,
        /* .pfnQueryInfoEx = */ NULL,
        /* .uEndMarker = */     RTVFSOBJOPS_VERSION
    },
    /* .uVersion = */           RTVFSOPS_VERSION,
    /* .fFeatures = */          0,
    /* .pfnOpenRoot = */        rtEfiVarStore_OpenRoot,
    /* .pfnQueryRangeState = */ NULL,
    /* .uEndMarker = */         RTVFSOPS_VERSION
};


/**
 * Validates the given firmware header.
 *
 * @returns true if the given header is considered valid, flse otherwise.
 * @param   pThis               The EFI variable store instance.
 * @param   pFvHdr              The firmware volume header to validate.
 * @param   poffData            The offset into the backing where the data area begins.
 * @param   pErrInfo            Where to return additional error info.
 */
static int rtEfiVarStoreFvHdr_Validate(PRTEFIVARSTORE pThis, PCEFI_FIRMWARE_VOLUME_HEADER pFvHdr, uint64_t *poffData,
                                       PRTERRINFO pErrInfo)
{
#ifdef LOG_ENABLED
    rtEfiVarStoreFvHdr_Log(pFvHdr);
#endif

    EFI_GUID GuidNvData = EFI_VARSTORE_FILESYSTEM_GUID;
    if (memcmp(&pFvHdr->GuidFilesystem, &GuidNvData, sizeof(GuidNvData)))
        return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "Filesystem GUID doesn't indicate a variable store");
    if (RT_LE2H_U64(pFvHdr->cbFv) > pThis->cbBacking)
        return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "Firmware volume length exceeds size of backing storage (truncated file?)");
    /* Signature was already verfied by caller. */
    /** @todo Check attributes. */
    if (pFvHdr->bRsvd != 0)
        return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "Reserved field of header is not 0");
    if (pFvHdr->bRevision != EFI_FIRMWARE_VOLUME_HEADER_REVISION)
        return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "Unexpected revision of the firmware volume header");
    if (RT_LE2H_U16(pFvHdr->offExtHdr) != 0)
        return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "Firmware volume header contains unsupported extended headers");

    /* Start calculating the checksum of the main header. */
    uint16_t u16Chksum = 0;
    const uint16_t *pu16 = (const uint16_t *)pFvHdr;
    while (pu16 < (const uint16_t *)pFvHdr + (sizeof(*pFvHdr) / sizeof(uint16_t)))
        u16Chksum += RT_LE2H_U16(*pu16++);

    /* Read in the block map and verify it as well. */
    uint64_t cbFvVol = 0;
    uint64_t cbFvHdr = sizeof(*pFvHdr);
    uint64_t offBlockMap = sizeof(*pFvHdr);
    for (;;)
    {
        EFI_FW_BLOCK_MAP BlockMap;
        int rc = RTVfsFileReadAt(pThis->hVfsBacking, offBlockMap, &BlockMap, sizeof(BlockMap), NULL);
        if (RT_FAILURE(rc))
            return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Reading block map entry from %#RX64 failed", offBlockMap);

        cbFvHdr     += sizeof(BlockMap);
        offBlockMap += sizeof(BlockMap);

        /* A zero entry denotes the end. */
        if (   !RT_LE2H_U32(BlockMap.cBlocks)
            && !RT_LE2H_U32(BlockMap.cbBlock))
            break;

        cbFvVol += RT_LE2H_U32(BlockMap.cBlocks) * RT_LE2H_U32(BlockMap.cbBlock);

        pu16 = (const uint16_t *)&BlockMap;
        while (pu16 < (const uint16_t *)&BlockMap + (sizeof(BlockMap) / sizeof(uint16_t)))
            u16Chksum += RT_LE2H_U16(*pu16++);
    }

    *poffData = offBlockMap;

    if (u16Chksum)
        return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "Firmware volume header has incorrect checksum");
    if (RT_LE2H_U16(pFvHdr->cbFvHdr) != cbFvHdr)
        return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "Unexpected firmware volume header size");

    return VINF_SUCCESS;
}


/**
 * Validates the given variable store header.
 *
 * @returns true if the given header is considered valid, false otherwise.
 * @param   pThis               The EFI variable store instance.
 * @param   pHdr                The variable store header to validate.
 * @param   pfAuth              Where to store whether the variable store uses authenticated variables or not.
 * @param   pErrInfo            Where to return additional error info.
 */
static int rtEfiVarStoreHdr_Validate(PRTEFIVARSTORE pThis, PCEFI_VARSTORE_HEADER pHdr, bool *pfAuth, PRTERRINFO pErrInfo)
{
#ifdef LOG_ENABLED
    rtEfiVarStoreHdr_Log(pHdr);
#endif

    EFI_GUID GuidVarStoreAuth = EFI_VARSTORE_HEADER_GUID_AUTHENTICATED_VARIABLE;
    EFI_GUID GuidVarStore = EFI_VARSTORE_HEADER_GUID_VARIABLE;
    if (!memcmp(&pHdr->GuidVarStore, &GuidVarStoreAuth, sizeof(GuidVarStoreAuth)))
        *pfAuth = true;
    else if (!memcmp(&pHdr->GuidVarStore, &GuidVarStore, sizeof(GuidVarStore)))
        *pfAuth = false;
    else
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "Variable store GUID doesn't indicate a variable store (%RTuuid)", pHdr->GuidVarStore);
    if (RT_LE2H_U32(pHdr->cbVarStore) >= pThis->cbBacking)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "Variable store length exceeds size of backing storage (truncated file?): %#RX32, max %#RX64",
                                   RT_LE2H_U32(pHdr->cbVarStore), pThis->cbBacking);
    if (pHdr->bFmt != EFI_VARSTORE_HEADER_FMT_FORMATTED)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "Variable store is not formatted (%#x)", pHdr->bFmt);
    if (pHdr->bState != EFI_VARSTORE_HEADER_STATE_HEALTHY)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "Variable store is not healthy (%#x)", pHdr->bState);

    return VINF_SUCCESS;
}


/**
 * Validates the given authenticate variable header.
 *
 * @returns true if the given header is considered valid, false otherwise.
 * @param   pThis               The EFI variable store instance.
 * @param   pVarHdr             The variable header to validate.
 * @param   offVar              Offset of the authenticated variable header.
 * @param   pErrInfo            Where to return additional error info.
 */
static int rtEfiVarStoreAuthVar_Validate(PRTEFIVARSTORE pThis, PCEFI_AUTH_VAR_HEADER pVarHdr, uint64_t offVar, PRTERRINFO pErrInfo)
{
#ifdef LOG_ENABLED
    rtEfiVarStoreAuthVarHdr_Log(pVarHdr, offVar);
#endif

    uint32_t cbName = RT_LE2H_U32(pVarHdr->cbName);
    uint32_t cbData = RT_LE2H_U32(pVarHdr->cbData);
    uint64_t cbVarMax = pThis->cbBacking - offVar - sizeof(*pVarHdr);
    if (   cbVarMax <= cbName
        || cbVarMax - cbName <= cbData)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "Variable exceeds remaining space in store (cbName=%u cbData=%u cbVarMax=%llu)",
                                   cbName, cbData, cbVarMax);

    return VINF_SUCCESS;
}


/**
 * Loads the authenticated variable at the given offset.
 *
 * @returns IPRT status code.
 * @retval  VERR_EOF if the end of the store was reached.
 * @param   pThis               The EFI variable store instance.
 * @param   offVar              Offset of the variable to load.
 * @param   poffVarEnd          Where to store the offset pointing to the end of the variable.
 * @param   fIgnoreDelVars      Flag whether to ignore deleted variables.
 * @param   pErrInfo            Where to return additional error info.
 */
static int rtEfiVarStoreLoadAuthVar(PRTEFIVARSTORE pThis, uint64_t offVar, uint64_t *poffVarEnd,
                                    bool fIgnoreDelVars, PRTERRINFO pErrInfo)
{
    EFI_AUTH_VAR_HEADER VarHdr;
    int rc = RTVfsFileReadAt(pThis->hVfsBacking, offVar, &VarHdr, sizeof(VarHdr), NULL);
    if (RT_FAILURE(rc))
        return rc;

    rc = rtEfiVarStoreAuthVar_Validate(pThis, &VarHdr, offVar, pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    if (poffVarEnd)
        *poffVarEnd = offVar + sizeof(VarHdr) + RT_LE2H_U32(VarHdr.cbData) + RT_LE2H_U32(VarHdr.cbName);

    /* Only add complete variables or deleted variables when requested. */
    if (   (   fIgnoreDelVars
            && VarHdr.bState != EFI_AUTH_VAR_HEADER_STATE_ADDED)
        || VarHdr.bState == EFI_AUTH_VAR_HEADER_STATE_HDR_VALID_ONLY)
        return VINF_SUCCESS;

    pThis->cbVarData += sizeof(VarHdr) + RT_LE2H_U32(VarHdr.cbData) + RT_LE2H_U32(VarHdr.cbName);

    RTUTF16 awchName[128]; RT_ZERO(awchName);
    if (RT_LE2H_U32(VarHdr.cbName) > sizeof(awchName) - sizeof(RTUTF16))
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "Variable name is too long (%llu vs. %llu)\n",
                                   RT_LE2H_U32(VarHdr.cbName), sizeof(awchName));

    rc = RTVfsFileReadAt(pThis->hVfsBacking, offVar + sizeof(VarHdr), &awchName[0], RT_LE2H_U32(VarHdr.cbName), NULL);
    if (RT_FAILURE(rc))
        return rc;

    Log2(("Variable name '%ls'\n", &awchName[0]));
    rc = rtEfiVarStore_VarMaybeGrowEntries(pThis);
    if (RT_FAILURE(rc))
        return rc;

    PRTEFIVAR pVar = &pThis->paVars[pThis->cVars++];
    pVar->pVarStore  = pThis;
    if (RT_LE2H_U32(VarHdr.cbData))
        pVar->offVarData = offVar + sizeof(VarHdr) + RT_LE2H_U32(VarHdr.cbName);
    else
        pVar->offVarData = 0;
    pVar->fAttr      = RT_LE2H_U32(VarHdr.fAttr);
    pVar->cMonotonic = RT_LE2H_U64(VarHdr.cMonotonic);
    pVar->idPubKey   = RT_LE2H_U32(VarHdr.idPubKey);
    pVar->cbData     = RT_LE2H_U32(VarHdr.cbData);
    pVar->pvData     = NULL;
    pVar->fDeleted   = false;
    memcpy(&pVar->EfiTimestamp, &VarHdr.Timestamp, sizeof(VarHdr.Timestamp));

    if (VarHdr.Timestamp.u8Month)
        RTEfiTimeToTimeSpec(&pVar->Time, &VarHdr.Timestamp);
    else
        RTTimeNow(&pVar->Time);

    RTEfiGuidToUuid(&pVar->Uuid, &VarHdr.GuidVendor);

    rc = RTUtf16ToUtf8(&awchName[0], &pVar->pszName);
    if (RT_FAILURE(rc))
        pThis->cVars--;

    rc = rtEfiVarStore_AddVarByGuid(pThis, &pVar->Uuid, pThis->cVars - 1);

    return rc;
}


/**
 * Looks for the next variable starting at the given offset.
 *
 * @returns IPRT status code.
 * @retval  VERR_EOF if the end of the store was reached.
 * @param   pThis               The EFI variable store instance.
 * @param   offStart            Where in the image to start looking.
 * @param   poffVar             Where to store the start of the next variable if found.
 */
static int rtEfiVarStoreFindVar(PRTEFIVARSTORE pThis, uint64_t offStart, uint64_t *poffVar)
{
    /* Try to find the ID indicating a variable start by loading data in chunks. */
    uint64_t offEnd = pThis->offStoreData + pThis->cbVarStore;
    while (offStart < offEnd)
    {
        uint16_t au16Tmp[_1K / sizeof(uint16_t)];
        size_t cbThisRead = RT_MIN(sizeof(au16Tmp), offEnd - offStart);
        int rc = RTVfsFileReadAt(pThis->hVfsBacking, offStart, &au16Tmp[0], sizeof(au16Tmp), NULL);
        if (RT_FAILURE(rc))
            return rc;

        for (uint32_t i = 0; i < RT_ELEMENTS(au16Tmp); i++)
            if (RT_LE2H_U16(au16Tmp[i]) == EFI_AUTH_VAR_HEADER_START)
            {
                *poffVar = offStart + i * sizeof(uint16_t);
                return VINF_SUCCESS;
            }

        offStart += cbThisRead;
    }

    return VERR_EOF;
}


/**
 * Loads and parses the superblock of the filesystem.
 *
 * @returns IPRT status code.
 * @param   pThis               The EFI variable store instance.
 * @param   pErrInfo            Where to return additional error info.
 */
static int rtEfiVarStoreLoad(PRTEFIVARSTORE pThis, PRTERRINFO pErrInfo)
{
    EFI_FIRMWARE_VOLUME_HEADER FvHdr;
    int rc = RTVfsFileReadAt(pThis->hVfsBacking, 0, &FvHdr, sizeof(FvHdr), NULL);
    if (RT_FAILURE(rc))
        return RTERRINFO_LOG_SET(pErrInfo, rc, "Error reading firmware volume header");

    /* Validate the signature. */
    if (RT_LE2H_U32(FvHdr.u32Signature) != EFI_FIRMWARE_VOLUME_HEADER_SIGNATURE)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNKNOWN_FORMAT, "Not a EFI variable store - Signature mismatch: %RX32", RT_LE2H_U16(FvHdr.u32Signature));

    uint64_t offData = 0;
    rc = rtEfiVarStoreFvHdr_Validate(pThis, &FvHdr, &offData, pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    EFI_VARSTORE_HEADER StoreHdr;
    rc = RTVfsFileReadAt(pThis->hVfsBacking, offData, &StoreHdr, sizeof(StoreHdr), NULL);
    if (RT_FAILURE(rc))
        return RTERRINFO_LOG_SET(pErrInfo, rc, "Error reading variable store header");

    rc = rtEfiVarStoreHdr_Validate(pThis, &StoreHdr, &pThis->fAuth, pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    pThis->offStoreData = offData + sizeof(StoreHdr);
    pThis->cbVarStore   = RT_LE2H_U32(StoreHdr.cbVarStore) - sizeof(StoreHdr);

    /* Go over variables and set up the pointers. */
    offData = pThis->offStoreData;
    for (;;)
    {
        uint64_t offVar = 0;

        rc = rtEfiVarStoreFindVar(pThis, offData, &offVar);
        if (RT_FAILURE(rc))
            break;

        rc = rtEfiVarStoreLoadAuthVar(pThis, offVar, &offData, true /* fIgnoreDelVars*/, pErrInfo);
        if (RT_FAILURE(rc))
            break;

        /* Align to 16bit boundary. */
        offData = RT_ALIGN_64(offData, 2);
    }

    if (rc == VERR_EOF) /* Reached end of variable store. */
        rc = VINF_SUCCESS;

    return rc;
}


/**
 * Fills the given range with 0xff to match what a real NAND flash device would return for
 * unwritten storage.
 *
 * @returns IPRT status code.
 * @param   hVfsFile            The VFS file handle to write to.
 * @param   offStart            The start offset to fill.
 * @param   offEnd              Offset to fill up to (exclusive).
 */
static int rtEfiVarStoreFillWithFF(RTVFSFILE hVfsFile, uint64_t offStart, uint64_t offEnd)
{
    int rc = VINF_SUCCESS;
    uint8_t abFF[512];
    memset(&abFF[0], 0xff, sizeof(abFF));

    while (   offStart < offEnd
           && RT_SUCCESS(rc))
    {
        size_t cbThisWrite = RT_MIN(sizeof(abFF), offEnd - offStart);
        rc = RTVfsFileWriteAt(hVfsFile, offStart, &abFF[0], cbThisWrite, NULL);
        offStart += cbThisWrite;
    }

    return rc;
}


RTDECL(int) RTEfiVarStoreOpenAsVfs(RTVFSFILE hVfsFileIn, uint32_t fMntFlags, uint32_t fVarStoreFlags, PRTVFS phVfs, PRTERRINFO pErrInfo)
{
    AssertPtrReturn(phVfs, VERR_INVALID_POINTER);
    AssertReturn(!(fMntFlags & ~RTVFSMNT_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(!fVarStoreFlags, VERR_INVALID_FLAGS);

    uint32_t cRefs = RTVfsFileRetain(hVfsFileIn);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Create a VFS instance and initialize the data so rtFsExtVol_Close works.
     */
    RTVFS            hVfs;
    PRTEFIVARSTORE pThis;
    int rc = RTVfsNew(&g_rtEfiVarStoreOps, sizeof(*pThis), NIL_RTVFS, RTVFSLOCK_CREATE_RW, &hVfs, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->hVfsBacking    = hVfsFileIn;
        pThis->hVfsSelf       = hVfs;
        pThis->fMntFlags      = fMntFlags;
        pThis->fVarStoreFlags = fVarStoreFlags;

        rc = RTVfsFileQuerySize(pThis->hVfsBacking, &pThis->cbBacking);
        if (RT_SUCCESS(rc))
        {
            rc = rtEfiVarStoreLoad(pThis, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                *phVfs = hVfs;
                return VINF_SUCCESS;
            }
        }

        RTVfsRelease(hVfs);
        *phVfs = NIL_RTVFS;
    }
    else
        RTVfsFileRelease(hVfsFileIn);

    return rc;
}


RTDECL(int) RTEfiVarStoreCreate(RTVFSFILE hVfsFile, uint64_t offStore, uint64_t cbStore, uint32_t fFlags, uint32_t cbBlock,
                                PRTERRINFO pErrInfo)
{
    RT_NOREF(pErrInfo);

    /*
     * Validate input.
     */
    if (!cbBlock)
        cbBlock = 4096;
    else
        AssertMsgReturn(cbBlock <= 8192 && RT_IS_POWER_OF_TWO(cbBlock),
                        ("cbBlock=%#x\n", cbBlock), VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & ~RTEFIVARSTORE_CREATE_F_VALID_MASK), VERR_INVALID_FLAGS);

    if (!cbStore)
    {
        uint64_t cbFile;
        int rc = RTVfsFileQuerySize(hVfsFile, &cbFile);
        AssertRCReturn(rc, rc);
        AssertMsgReturn(cbFile > offStore, ("cbFile=%#RX64 offStore=%#RX64\n", cbFile, offStore), VERR_INVALID_PARAMETER);
        cbStore = cbFile - offStore;
    }

    uint32_t cbFtw = 0;
    uint32_t offFtw = 0;
    uint32_t cbVarStore = cbStore;
    uint32_t cbNvEventLog = 0;
    uint32_t offNvEventLog = 0;
    if (!(fFlags & RTEFIVARSTORE_CREATE_F_NO_FTW_WORKING_SPACE))
    {
        /* Split the available space in half for the fault tolerant working area. */
        /** @todo Don't fully understand how these values come together right now but
         * we want to create NVRAM files matching the default OVMF_VARS.fd for now, see
         * https://github.com/tianocore/edk2/commit/b24fca05751f8222acf264853709012e0ab7bf49
         * for the layout.
         * Probably have toadd more arguments to control the different parameters.
         */
        cbNvEventLog  = _4K;
        cbVarStore    = cbStore / 2 - cbNvEventLog - _4K;
        cbFtw         = cbVarStore + _4K;
        offNvEventLog = cbVarStore;
        offFtw        = offNvEventLog + cbNvEventLog;
    }

    uint32_t const cBlocks = (uint32_t)(cbStore / cbBlock);

    EFI_GUID                   GuidVarStore = EFI_VARSTORE_FILESYSTEM_GUID;
    EFI_GUID                   GuidVarAuth  = EFI_VARSTORE_HEADER_GUID_AUTHENTICATED_VARIABLE;
    EFI_FIRMWARE_VOLUME_HEADER FvHdr;       RT_ZERO(FvHdr);
    EFI_FW_BLOCK_MAP           aBlockMap[2]; RT_ZERO(aBlockMap);
    EFI_VARSTORE_HEADER        VarStoreHdr; RT_ZERO(VarStoreHdr);

    /* Firmware volume header. */
    memcpy(&FvHdr.GuidFilesystem, &GuidVarStore, sizeof(GuidVarStore));
    FvHdr.cbFv           = RT_H2LE_U64(cbStore);
    FvHdr.u32Signature   = RT_H2LE_U32(EFI_FIRMWARE_VOLUME_HEADER_SIGNATURE);
    FvHdr.fAttr          = RT_H2LE_U32(0x4feff); /** @todo */
    FvHdr.cbFvHdr        = RT_H2LE_U16(sizeof(FvHdr) + sizeof(aBlockMap));
    FvHdr.bRevision      = EFI_FIRMWARE_VOLUME_HEADER_REVISION;

    /* Start calculating the checksum of the main header. */
    uint16_t u16Chksum = 0;
    const uint16_t *pu16 = (const uint16_t *)&FvHdr;
    while (pu16 < (const uint16_t *)&FvHdr + (sizeof(FvHdr) / sizeof(uint16_t)))
        u16Chksum += RT_LE2H_U16(*pu16++);

    /* Block map, the second entry remains 0 as it serves the delimiter. */
    aBlockMap[0].cbBlock     = RT_H2LE_U32(cbBlock);
    aBlockMap[0].cBlocks     = RT_H2LE_U32(cBlocks);

    pu16 = (const uint16_t *)&aBlockMap[0];
    while (pu16 < (const uint16_t *)&aBlockMap[0] + (sizeof(aBlockMap) / (sizeof(uint16_t))))
        u16Chksum += RT_LE2H_U16(*pu16++);

    FvHdr.u16Chksum          = RT_H2LE_U16(UINT16_MAX - u16Chksum + 1);

    /* Variable store header. */
    memcpy(&VarStoreHdr.GuidVarStore, &GuidVarAuth, sizeof(GuidVarAuth));
    VarStoreHdr.cbVarStore   = RT_H2LE_U32(cbVarStore - sizeof(FvHdr) - sizeof(aBlockMap));
    VarStoreHdr.bFmt         = EFI_VARSTORE_HEADER_FMT_FORMATTED;
    VarStoreHdr.bState       = EFI_VARSTORE_HEADER_STATE_HEALTHY;

    /* Write everything. */
    int rc = RTVfsFileWriteAt(hVfsFile, offStore, &FvHdr, sizeof(FvHdr), NULL);
    if (RT_SUCCESS(rc))
        rc = RTVfsFileWriteAt(hVfsFile, offStore + sizeof(FvHdr), &aBlockMap[0], sizeof(aBlockMap), NULL);
    if (RT_SUCCESS(rc))
        rc = RTVfsFileWriteAt(hVfsFile, offStore + sizeof(FvHdr) + sizeof(aBlockMap), &VarStoreHdr, sizeof(VarStoreHdr), NULL);
    if (RT_SUCCESS(rc))
    {
        /* Fill the remainder with 0xff as it would be the case for a real NAND flash device. */
        uint64_t offStart = offStore + sizeof(FvHdr) + sizeof(aBlockMap) + sizeof(VarStoreHdr);
        uint64_t offEnd   = offStore + cbVarStore;

        rc = rtEfiVarStoreFillWithFF(hVfsFile, offStart, offEnd);
    }

    if (   RT_SUCCESS(rc)
        && !(fFlags & RTEFIVARSTORE_CREATE_F_NO_FTW_WORKING_SPACE))
    {
        EFI_GUID             GuidFtwArea = EFI_WORKING_BLOCK_SIGNATURE_GUID;
        EFI_FTW_BLOCK_HEADER FtwHdr; RT_ZERO(FtwHdr);

        memcpy(&FtwHdr.GuidSignature, &GuidFtwArea, sizeof(GuidFtwArea));
        FtwHdr.fWorkingBlockValid = RT_H2LE_U32(0xfffffffe); /** @todo */
        FtwHdr.cbWriteQueue       = RT_H2LE_U64(0xfe0ULL); /* This comes from the default OVMF variable volume. */
        FtwHdr.u32Chksum          = RTCrc32(&FtwHdr, sizeof(FtwHdr));

        /* The area starts with the event log which defaults to 0xff. */
        rc = rtEfiVarStoreFillWithFF(hVfsFile, offNvEventLog, offNvEventLog + cbNvEventLog);
        if (RT_SUCCESS(rc))
        {
            /* Write the FTW header. */
            rc = RTVfsFileWriteAt(hVfsFile, offFtw, &FtwHdr, sizeof(FtwHdr), NULL);
            if (RT_SUCCESS(rc))
                rc = rtEfiVarStoreFillWithFF(hVfsFile, offFtw + sizeof(FtwHdr), offFtw + cbFtw);
        }
    }

    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnValidate}
 */
static DECLCALLBACK(int) rtVfsChainEfiVarStore_Validate(PCRTVFSCHAINELEMENTREG pProviderReg, PRTVFSCHAINSPEC pSpec,
                                                        PRTVFSCHAINELEMSPEC pElement, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg);

    /*
     * Basic checks.
     */
    if (pElement->enmTypeIn != RTVFSOBJTYPE_FILE)
        return pElement->enmTypeIn == RTVFSOBJTYPE_INVALID ? VERR_VFS_CHAIN_CANNOT_BE_FIRST_ELEMENT : VERR_VFS_CHAIN_TAKES_FILE;
    if (   pElement->enmType != RTVFSOBJTYPE_VFS
        && pElement->enmType != RTVFSOBJTYPE_DIR)
        return VERR_VFS_CHAIN_ONLY_DIR_OR_VFS;
    if (pElement->cArgs > 1)
        return VERR_VFS_CHAIN_AT_MOST_ONE_ARG;

    /*
     * Parse the flag if present, save in pElement->uProvider.
     */
    bool fReadOnly = (pSpec->fOpenFile & RTFILE_O_ACCESS_MASK) == RTFILE_O_READ;
    if (pElement->cArgs > 0)
    {
        const char *psz = pElement->paArgs[0].psz;
        if (*psz)
        {
            if (!strcmp(psz, "ro"))
                fReadOnly = true;
            else if (!strcmp(psz, "rw"))
                fReadOnly = false;
            else
            {
                *poffError = pElement->paArgs[0].offSpec;
                return RTErrInfoSet(pErrInfo, VERR_VFS_CHAIN_INVALID_ARGUMENT, "Expected 'ro' or 'rw' as argument");
            }
        }
    }

    pElement->uProvider = fReadOnly ? RTVFSMNT_F_READ_ONLY : 0;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnInstantiate}
 */
static DECLCALLBACK(int) rtVfsChainEfiVarStore_Instantiate(PCRTVFSCHAINELEMENTREG pProviderReg, PCRTVFSCHAINSPEC pSpec,
                                                           PCRTVFSCHAINELEMSPEC pElement, RTVFSOBJ hPrevVfsObj,
                                                           PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec, poffError);

    int         rc;
    RTVFSFILE   hVfsFileIn = RTVfsObjToFile(hPrevVfsObj);
    if (hVfsFileIn != NIL_RTVFSFILE)
    {
        RTVFS hVfs;
        rc = RTEfiVarStoreOpenAsVfs(hVfsFileIn, (uint32_t)pElement->uProvider, (uint32_t)(pElement->uProvider >> 32), &hVfs, pErrInfo);
        RTVfsFileRelease(hVfsFileIn);
        if (RT_SUCCESS(rc))
        {
            *phVfsObj = RTVfsObjFromVfs(hVfs);
            RTVfsRelease(hVfs);
            if (*phVfsObj != NIL_RTVFSOBJ)
                return VINF_SUCCESS;
            rc = VERR_VFS_CHAIN_CAST_FAILED;
        }
    }
    else
        rc = VERR_VFS_CHAIN_CAST_FAILED;
    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnCanReuseElement}
 */
static DECLCALLBACK(bool) rtVfsChainEfiVarStore_CanReuseElement(PCRTVFSCHAINELEMENTREG pProviderReg,
                                                                PCRTVFSCHAINSPEC pSpec, PCRTVFSCHAINELEMSPEC pElement,
                                                                PCRTVFSCHAINSPEC pReuseSpec, PCRTVFSCHAINELEMSPEC pReuseElement)
{
    RT_NOREF(pProviderReg, pSpec, pReuseSpec);
    if (   pElement->paArgs[0].uProvider == pReuseElement->paArgs[0].uProvider
        || !pReuseElement->paArgs[0].uProvider)
        return true;
    return false;
}


/** VFS chain element 'efivarstore'. */
static RTVFSCHAINELEMENTREG g_rtVfsChainEfiVarStoreReg =
{
    /* uVersion = */            RTVFSCHAINELEMENTREG_VERSION,
    /* fReserved = */           0,
    /* pszName = */             "efivarstore",
    /* ListEntry = */           { NULL, NULL },
    /* pszHelp = */             "Open a EFI variable store, requires a file object on the left side.\n"
                                "First argument is an optional 'ro' (read-only) or 'rw' (read-write) flag.\n",
    /* pfnValidate = */         rtVfsChainEfiVarStore_Validate,
    /* pfnInstantiate = */      rtVfsChainEfiVarStore_Instantiate,
    /* pfnCanReuseElement = */  rtVfsChainEfiVarStore_CanReuseElement,
    /* uEndMarker = */          RTVFSCHAINELEMENTREG_VERSION
};

RTVFSCHAIN_AUTO_REGISTER_ELEMENT_PROVIDER(&g_rtVfsChainEfiVarStoreReg, rtVfsChainEfiVarStoreReg);

