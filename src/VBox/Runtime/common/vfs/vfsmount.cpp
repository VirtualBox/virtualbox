/* $Id: vfsmount.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Virtual File System, Mounting.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP RTLOGGROUP_VFS
#include <iprt/vfs.h>

#define RT_ASM_INCLUDE_PAGE_SIZE
#include <iprt/asm-mem.h>
#include <iprt/asm.h> /* ASMByteSwapU32/U16 on sparc */
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/fsvfs.h>
#include <iprt/mem.h>
#include <iprt/log.h>
#include <iprt/string.h>
#include <iprt/vfslowlevel.h>

#include <iprt/formats/fat.h>
#include <iprt/formats/iso9660.h>
#include <iprt/formats/udf.h>
#include <iprt/formats/ext.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Buffer structure for the detection routines. */
typedef union RTVFSMOUNTBUF
{
    uint8_t             ab[2048];
    uint32_t            au32[2048/4];
    FATBOOTSECTOR       Bootsector;
    ISO9660VOLDESCHDR   IsoHdr;
} RTVFSMOUNTBUF;
AssertCompileSize(RTVFSMOUNTBUF, 2048);
typedef RTVFSMOUNTBUF *PRTVFSMOUNTBUF;



/**
 * Checks if the given 2K sector at offset 32KB looks like ISO-9660 or UDF.
 *
 * @returns true if likely ISO or UDF, otherwise false.
 * @param   pVolDescHdr         Whatever is at offset 32KB.  2KB buffer.
 */
static bool rtVfsMountIsIsoFs(PCISO9660VOLDESCHDR pVolDescHdr)
{
    if (   memcmp(pVolDescHdr->achStdId, RT_STR_TUPLE(ISO9660VOLDESC_STD_ID)) == 0
        && pVolDescHdr->bDescType <= ISO9660VOLDESC_TYPE_PARTITION
        && pVolDescHdr->bDescVersion != 0
        && pVolDescHdr->bDescVersion <= 3 /* don't be too picky, just increase the likelyhood */ )
        return true;

    if (   memcmp(pVolDescHdr->achStdId, RT_STR_TUPLE(UDF_EXT_VOL_DESC_STD_ID_BEGIN)) == 0
        && pVolDescHdr->bDescType    == UDF_EXT_VOL_DESC_TYPE
        && pVolDescHdr->bDescVersion == UDF_EXT_VOL_DESC_VERSION)
        return true;

    return false;
}


/**
 * Check if the given bootsector is a NTFS boot sector.
 *
 * @returns true if NTFS, false if not.
 * @param   pBootSector         The boot sector to inspect.
 */
static bool rtVfsMountIsNtfs(PCFATBOOTSECTOR pBootSector)
{
    if (memcmp(pBootSector->achOemName, RT_STR_TUPLE("NTFS    ")) != 0)
        return false;

    uint16_t cbSector = RT_LE2H_U16(pBootSector->Bpb.Bpb331.cbSector);
    if (   cbSector < 0x100
        || cbSector >= 0x1000
        || (cbSector & 0xff) != 0)
    {
        Log2(("rtVfsMountIsNtfs: cbSector=%#x: out of range\n", cbSector));
        return false;
    }

    if (   !RT_IS_POWER_OF_TWO(pBootSector->Bpb.Bpb331.cSectorsPerCluster)
        || pBootSector->Bpb.Bpb331.cSectorsPerCluster == 0
        || pBootSector->Bpb.Bpb331.cSectorsPerCluster > 128)
    {
        Log2(("rtVfsMountIsNtfs: cSectorsPerCluster=%#x: out of range\n", pBootSector->Bpb.Bpb331.cSectorsPerCluster));
        return false;
    }

    if ((uint32_t)pBootSector->Bpb.Bpb331.cSectorsPerCluster * cbSector > _64K)
    {
        Log2(("rtVfsMountIsNtfs: cSectorsPerCluster=%#x * cbSector=%#x => %#x: out of range\n",
              pBootSector->Bpb.Bpb331.cSectorsPerCluster, cbSector,
              (uint32_t)pBootSector->Bpb.Bpb331.cSectorsPerCluster * cbSector));
        return false;
    }

    if (   pBootSector->Bpb.Bpb331.cReservedSectors != 0
        || pBootSector->Bpb.Bpb331.cMaxRootDirEntries != 0
        || pBootSector->Bpb.Bpb331.cTotalSectors16 != 0
        || pBootSector->Bpb.Bpb331.cTotalSectors32 != 0
        || pBootSector->Bpb.Bpb331.cSectorsPerFat != 0
        || pBootSector->Bpb.Bpb331.cFats != 0)
    {
        Log2(("rtVfsMountIsNtfs: cReservedSectors=%#x cMaxRootDirEntries=%#x cTotalSectors=%#x cTotalSectors32=%#x cSectorsPerFat=%#x cFats=%#x: should all be zero, but one or more aren't\n",
              RT_LE2H_U16(pBootSector->Bpb.Bpb331.cReservedSectors),
              RT_LE2H_U16(pBootSector->Bpb.Bpb331.cMaxRootDirEntries),
              RT_LE2H_U16(pBootSector->Bpb.Bpb331.cTotalSectors16),
              RT_LE2H_U32(pBootSector->Bpb.Bpb331.cTotalSectors32),
              RT_LE2H_U16(pBootSector->Bpb.Bpb331.cSectorsPerFat),
              pBootSector->Bpb.Bpb331.cFats));
        return false;
    }

    /** @todo NTFS specific checks: MFT cluster number, cluster per index block. */

    return true;
}


/**
 * Check if the given bootsector is a HPFS boot sector.
 *
 * @returns true if NTFS, false if not.
 * @param   pBootSector         The boot sector to inspect.
 * @param   hVfsFileIn          The volume file.
 * @param   pBuf2               A 2nd buffer.
 */
static bool rtVfsMountIsHpfs(PCFATBOOTSECTOR pBootSector, RTVFSFILE hVfsFileIn, PRTVFSMOUNTBUF pBuf2)
{
    if (memcmp(pBootSector->Bpb.Ebpb.achType, RT_STR_TUPLE("HPFS    ")) != 0)
        return false;

    /* Superblock is at sector 16, spare superblock at 17. */
    int rc = RTVfsFileReadAt(hVfsFileIn, 16 * 512, pBuf2, 512 * 2, NULL);
    if (RT_FAILURE(rc))
    {
        Log2(("rtVfsMountIsHpfs: Error reading superblock: %Rrc\n", rc));
        return false;
    }

    if (   RT_LE2H_U32(pBuf2->au32[0])         != UINT32_C(0xf995e849)
        || RT_LE2H_U32(pBuf2->au32[1])         != UINT32_C(0xfa53e9c5)
        || RT_LE2H_U32(pBuf2->au32[512/4 + 0]) != UINT32_C(0xf9911849)
        || RT_LE2H_U32(pBuf2->au32[512/4 + 1]) != UINT32_C(0xfa5229c5))
    {
        Log2(("rtVfsMountIsHpfs: Superblock or spare superblock signature mismatch: %#x %#x %#x %#x\n",
              RT_LE2H_U32(pBuf2->au32[0]),         RT_LE2H_U32(pBuf2->au32[1]),
              RT_LE2H_U32(pBuf2->au32[512/4 + 0]), RT_LE2H_U32(pBuf2->au32[512/4 + 1]) ));
        return false;
    }

    return true;
}


/**
 * Check if the given bootsector is a FAT boot sector.
 *
 * @returns true if NTFS, false if not.
 * @param   pBootSector         The boot sector to inspect.
 * @param   pbRaw               Pointer to the raw boot sector buffer.
 * @param   cbRaw               Number of bytes read starting with the boot
 *                              sector (which @a pbRaw points to).
 * @param   hVfsFileIn          The volume file.
 * @param   pBuf2               A 2nd buffer.
 */
static bool rtVfsMountIsFat(PCFATBOOTSECTOR pBootSector, uint8_t const *pbRaw, size_t cbRaw,
                            RTVFSFILE hVfsFileIn, PRTVFSMOUNTBUF pBuf2)
{
    Assert(cbRaw >= 1024);

    /*
     * Check the DOS signature first.  The PC-DOS 1.0 boot floppy does not have
     * a signature and we ASSUME this is the case for all floppies formated by it.
     */
    if (pBootSector->uSignature != FATBOOTSECTOR_SIGNATURE)
    {
        if (pBootSector->uSignature != 0)
            return false;

        /*
         * PC-DOS 1.0 does a 2fh byte short jump w/o any NOP following it.
         * Instead the following are three words and a 9 byte build date
         * string.  The remaining space is zero filled.
         *
         * Note! No idea how this would look like for 8" floppies, only got 5"1/4'.
         *
         * ASSUME all non-BPB disks are using this format.
         */
        if (   pBootSector->abJmp[0] != 0xeb /* jmp rel8 */
            || pBootSector->abJmp[1] <  0x2f
            || pBootSector->abJmp[1] >= 0x80
            || pBootSector->abJmp[2] == 0x90 /* nop */)
        {
            Log2(("rtVfsMountIsFat: No DOS v1.0 bootsector either - invalid jmp: %.3Rhxs\n", pBootSector->abJmp));
            return false;
        }

        /* Check the FAT ID so we can tell if this is double or single sided, as well as being a valid FAT12 start. */
        if (   (pbRaw[512] != 0xfe && pbRaw[0] != 0xff)
            || pbRaw[512 + 1] != 0xff
            || pbRaw[512 + 2] != 0xff)
        {
            Log2(("rtVfsMountIsFat: No DOS v1.0 bootsector either - unexpected start of FAT: %.3Rhxs\n", &pbRaw[512]));
            return false;
        }

        uint32_t const offJump      = 2 + pBootSector->abJmp[1];
        uint32_t const offFirstZero = 2 /*jmp */ + 3 * 2 /* words */ + 9 /* date string */;
        Assert(offFirstZero >= RT_UOFFSETOF(FATBOOTSECTOR, Bpb));
        uint32_t const cbZeroPad    = RT_MIN(offJump - offFirstZero,
                                             sizeof(pBootSector->Bpb.Bpb20) - (offFirstZero - RT_UOFFSETOF(FATBOOTSECTOR, Bpb)));

        if (!ASMMemIsAllU8((uint8_t const *)pBootSector + offFirstZero, cbZeroPad, 0))
        {
            Log2(("rtVfsMountIsFat: No DOS v1.0 bootsector either - expected zero padding %#x LB %#x: %.*Rhxs\n",
                  offFirstZero, cbZeroPad, cbZeroPad, (uint8_t const *)pBootSector + offFirstZero));
            return false;
        }
    }
    else
    {
        /*
         * DOS 2.0 or later.
         *
         * Start by checking if we've got a known jump instruction first, because
         * that will give us a max (E)BPB size hint.
         */
        uint8_t offJmp = UINT8_MAX;
        if (   pBootSector->abJmp[0] == 0xeb
            && pBootSector->abJmp[1] <= 0x7f)
            offJmp = pBootSector->abJmp[1] + 2;
        else if (   pBootSector->abJmp[0] == 0x90
                 && pBootSector->abJmp[1] == 0xeb
                 && pBootSector->abJmp[2] <= 0x7f)
            offJmp = pBootSector->abJmp[2] + 3;
        else if (   pBootSector->abJmp[0] == 0xe9
                 && pBootSector->abJmp[2] <= 0x7f)
            offJmp = RT_MIN(127, RT_MAKE_U16(pBootSector->abJmp[1], pBootSector->abJmp[2]));
        uint8_t const cbMaxBpb = offJmp - RT_UOFFSETOF(FATBOOTSECTOR, Bpb);
        if (cbMaxBpb < sizeof(FATBPB20))
        {
            Log2(("rtVfsMountIsFat: DOS signature, but jmp too short for any BPB: %#x (max %#x BPB)\n", offJmp, cbMaxBpb));
            return false;
        }

        if (   pBootSector->Bpb.Bpb20.cFats == 0
            || pBootSector->Bpb.Bpb20.cFats > 4)
        {
            if (pBootSector->Bpb.Bpb20.cFats == 0)
                Log2(("rtVfsMountIsFat: DOS signature, number of FATs is zero, so not FAT file system\n"));
            else
                Log2(("rtVfsMountIsFat: DOS signature, too many FATs: %#x\n", pBootSector->Bpb.Bpb20.cFats));
            return false;
        }

        if (!FATBPB_MEDIA_IS_VALID(pBootSector->Bpb.Bpb20.bMedia))
        {
            Log2(("rtVfsMountIsFat: DOS signature, invalid media byte: %#x\n", pBootSector->Bpb.Bpb20.bMedia));
            return false;
        }

        uint16_t cbSector = RT_LE2H_U16(pBootSector->Bpb.Bpb20.cbSector);
        if (   cbSector != 512
            && cbSector != 4096
            && cbSector != 1024
            && cbSector != 128)
        {
            Log2(("rtVfsMountIsFat: DOS signature, unsupported sector size: %#x\n", cbSector));
            return false;
        }

        if (   !RT_IS_POWER_OF_TWO(pBootSector->Bpb.Bpb20.cSectorsPerCluster)
            || !pBootSector->Bpb.Bpb20.cSectorsPerCluster)
        {
            Log2(("rtVfsMountIsFat: DOS signature, cluster size not non-zero power of two: %#x",
                  pBootSector->Bpb.Bpb20.cSectorsPerCluster));
            return false;
        }

        uint16_t const cReservedSectors = RT_LE2H_U16(pBootSector->Bpb.Bpb20.cReservedSectors);
        if (   cReservedSectors == 0
            || cReservedSectors >= _32K)
        {
            Log2(("rtVfsMountIsFat: DOS signature, bogus reserved sector count: %#x\n", cReservedSectors));
            return false;
        }

        /*
         * Match the media byte with the first FAT byte and check that the next
         * 4 bits are set.  (To match further bytes in the FAT we'd need to
         * determin the FAT type, which is too much hazzle to do here.)
         */
        uint8_t const *pbFat;
        if ((size_t)cReservedSectors * cbSector < cbRaw)
            pbFat = &pbRaw[cReservedSectors * cbSector];
        else
        {
            int rc = RTVfsFileReadAt(hVfsFileIn, cReservedSectors * cbSector, pBuf2, 512, NULL);
            if (RT_FAILURE(rc))
            {
                Log2(("rtVfsMountIsFat: error reading first FAT sector at %#x: %Rrc\n", cReservedSectors * cbSector, rc));
                return false;
            }
            pbFat = pBuf2->ab;
        }
        if (*pbFat != pBootSector->Bpb.Bpb20.bMedia)
        {
            Log2(("rtVfsMountIsFat: Media byte and FAT ID mismatch: %#x vs %#x (%.8Rhxs)\n",
                  pbFat[0], pBootSector->Bpb.Bpb20.bMedia, pbFat));
            return false;
        }
        if ((pbFat[1] & 0xf) != 0xf)
        {
            Log2(("rtVfsMountIsFat: Media byte and FAT ID mismatch: %#x vs %#x (%.8Rhxs)\n",
                  pbFat[0], pBootSector->Bpb.Bpb20.bMedia, pbFat));
            return false;
        }
    }

    return true;
}


/**
 * Check if the given bootsector is an ext2/3/4 super block.
 *
 * @returns true if NTFS, false if not.
 * @param   pSuperBlock         The ext2 superblock.
 */
static bool rtVfsMountIsExt(PCEXTSUPERBLOCK pSuperBlock)
{
    if (RT_LE2H_U16(pSuperBlock->u16Signature) != EXT_SB_SIGNATURE)
        return false;

    uint32_t cShift = RT_LE2H_U32(pSuperBlock->cLogBlockSize);
    if (cShift > 54)
    {
        Log2(("rtVfsMountIsExt: cLogBlockSize=%#x: out of range\n", cShift));
        return false;
    }

    cShift = RT_LE2H_U32(pSuperBlock->cLogClusterSize);
    if (cShift > 54)
    {
        Log2(("rtVfsMountIsExt: cLogClusterSize=%#x: out of range\n", cShift));
        return false;
    }

    /* Some more checks here would be nice actually since a 16-bit word and a
       couple of field limits doesn't feel all that conclusive. */

    return true;
}


/**
 * Does the file system detection and mounting.
 *
 * Since we only support a handful of file systems at the moment and the
 * interface isn't yet extensible in any way, we combine the file system
 * recognition code for all.  This reduces the number of reads we need to do and
 * avoids unnecessary processing.
 *
 * @returns IPRT status code.
 * @param   hVfsFileIn      The volume file.
 * @param   fFlags          RTVFSMTN_F_XXX.
 * @param   pBuf            Pointer to the primary buffer
 * @param   pBuf2           Pointer to the secondary buffer.
 * @param   phVfs           Where to return the .
 * @param   pErrInfo        Where to return additional error information.
 *                          Optional.
 */
static int rtVfsMountInner(RTVFSFILE hVfsFileIn, uint32_t fFlags, RTVFSMOUNTBUF *pBuf,
                           RTVFSMOUNTBUF *pBuf2, PRTVFS phVfs, PRTERRINFO pErrInfo)
{
    AssertCompile(sizeof(*pBuf) >= ISO9660_SECTOR_SIZE);

    /* Start by checking for ISO-9660 and UDFS since these may have confusing
       data at the start of the volume. */
    int rc = RTVfsFileReadAt(hVfsFileIn, _32K, pBuf, ISO9660_SECTOR_SIZE, NULL);
    if (RT_SUCCESS(rc))
    {
        if (rtVfsMountIsIsoFs(&pBuf->IsoHdr))
        {
            Log(("RTVfsMount: Detected ISO-9660 or UDF.\n"));
            return RTFsIso9660VolOpen(hVfsFileIn, 0 /*fFlags*/, phVfs, pErrInfo);
        }
    }

    /* Now read the boot sector and whatever the next 1536 bytes may contain.
       With ext2 superblock at 1024, we can recognize quite a bit thru this read. */
    rc = RTVfsFileReadAt(hVfsFileIn, 0, pBuf, sizeof(*pBuf), NULL);
    if (RT_FAILURE(rc))
        return RTErrInfoSet(pErrInfo, rc, "Error reading boot sector");

    if (rtVfsMountIsNtfs(&pBuf->Bootsector))
        return RTFsNtfsVolOpen(hVfsFileIn, fFlags, 0 /*fNtfsFlags*/, phVfs, pErrInfo);

    if (rtVfsMountIsHpfs(&pBuf->Bootsector, hVfsFileIn, pBuf2))
        return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "HPFS not yet supported");

    if (rtVfsMountIsFat(&pBuf->Bootsector, pBuf->ab, sizeof(*pBuf), hVfsFileIn, pBuf2))
    {
        Log(("RTVfsMount: Detected ISO-9660 or UDF.\n"));
        return RTFsFatVolOpen(hVfsFileIn, RT_BOOL(fFlags & RTVFSMNT_F_READ_ONLY), 0 /*offBootSector*/, phVfs, pErrInfo);
    }

    AssertCompile(sizeof(*pBuf) >= 1024 + sizeof(EXTSUPERBLOCK));
    if (rtVfsMountIsExt((PCEXTSUPERBLOCK)&pBuf->ab[1024]))
    {
        Log(("RTVfsMount: Detected EXT2/3/4.\n"));
        return RTFsExtVolOpen(hVfsFileIn, fFlags, 0 /*fExt2Flags*/, phVfs, pErrInfo);
    }

    return VERR_VFS_UNSUPPORTED_FORMAT;
}


RTDECL(int) RTVfsMountVol(RTVFSFILE hVfsFileIn, uint32_t fFlags, PRTVFS phVfs, PRTERRINFO pErrInfo)
{
    AssertReturn(!(fFlags & ~RTVFSMNT_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertPtrReturn(hVfsFileIn, VERR_INVALID_HANDLE);
    AssertPtrReturn(phVfs, VERR_INVALID_HANDLE);

    *phVfs = NIL_RTVFS;

    RTVFSMOUNTBUF *pBufs = (RTVFSMOUNTBUF *)RTMemTmpAlloc(sizeof(*pBufs) * 2);
    AssertReturn(pBufs, VERR_NO_TMP_MEMORY);

    int rc = rtVfsMountInner(hVfsFileIn, fFlags, pBufs, pBufs + 1, phVfs, pErrInfo);

    RTMemTmpFree(pBufs);

    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnValidate}
 */
static DECLCALLBACK(int) rtVfsChainMountVol_Validate(PCRTVFSCHAINELEMENTREG pProviderReg, PRTVFSCHAINSPEC pSpec,
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
static DECLCALLBACK(int) rtVfsChainMountVol_Instantiate(PCRTVFSCHAINELEMENTREG pProviderReg, PCRTVFSCHAINSPEC pSpec,
                                                        PCRTVFSCHAINELEMSPEC pElement, RTVFSOBJ hPrevVfsObj,
                                                        PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec, poffError);

    int         rc;
    RTVFSFILE   hVfsFileIn = RTVfsObjToFile(hPrevVfsObj);
    if (hVfsFileIn != NIL_RTVFSFILE)
    {
        RTVFS hVfs;
        rc = RTVfsMountVol(hVfsFileIn, (uint32_t)pElement->uProvider, &hVfs, pErrInfo);
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
static DECLCALLBACK(bool) rtVfsChainMountVol_CanReuseElement(PCRTVFSCHAINELEMENTREG pProviderReg,
                                                             PCRTVFSCHAINSPEC pSpec, PCRTVFSCHAINELEMSPEC pElement,
                                                             PCRTVFSCHAINSPEC pReuseSpec, PCRTVFSCHAINELEMSPEC pReuseElement)
{
    RT_NOREF(pProviderReg, pSpec, pReuseSpec);
    if (   pElement->paArgs[0].uProvider == pReuseElement->paArgs[0].uProvider
        || !pReuseElement->paArgs[0].uProvider)
        return true;
    return false;
}


/** VFS chain element 'file'. */
static RTVFSCHAINELEMENTREG g_rtVfsChainMountVolReg =
{
    /* uVersion = */            RTVFSCHAINELEMENTREG_VERSION,
    /* fReserved = */           0,
    /* pszName = */             "mount",
    /* ListEntry = */           { NULL, NULL },
    /* pszHelp = */             "Open a file system, requires a file object on the left side.\n"
                                "First argument is an optional 'ro' (read-only) or 'rw' (read-write) flag.\n",
    /* pfnValidate = */         rtVfsChainMountVol_Validate,
    /* pfnInstantiate = */      rtVfsChainMountVol_Instantiate,
    /* pfnCanReuseElement = */  rtVfsChainMountVol_CanReuseElement,
    /* uEndMarker = */          RTVFSCHAINELEMENTREG_VERSION
};

RTVFSCHAIN_AUTO_REGISTER_ELEMENT_PROVIDER(&g_rtVfsChainMountVolReg, rtVfsChainMountVolReg);

