/* $Id: isomaker.cpp 115330 2026-09-25 17:56:38Z knut.osmundsen@oracle.com $ */
/** @file
 * IPRT - ISO Image Maker.
 */

/*
 * Copyright (C) 2017-2025 Oracle and/or its affiliates.
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
#include <iprt/buildconfig.h>
#include <iprt/err.h>
#include <iprt/crc.h>
#include <iprt/ctype.h>
#include <iprt/md5.h>
#include <iprt/file.h>
#include <iprt/list.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>
#include <iprt/utf16.h>
#include <iprt/uni.h>
#include <iprt/zero.h>
#include <iprt/formats/iso9660.h>
#include <iprt/formats/udf.h>

#include <internal/magics.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Asserts valid handle, returns @a a_rcRet if not. */
#define RTFSISOMAKER_ASSERT_VALID_HANDLE_RET_EX(a_pThis, a_rcRet) \
    do { AssertPtrReturn(a_pThis, a_rcRet); \
         AssertReturn((a_pThis)->uMagic == RTFSISOMAKERINT_MAGIC, a_rcRet); \
    } while (0)

/** Asserts valid handle, returns VERR_INVALID_HANDLE if not. */
#define RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(a_pThis) RTFSISOMAKER_ASSERT_VALID_HANDLE_RET_EX(a_pThis, VERR_INVALID_HANDLE)

/** The sector size. */
#define RTFSISOMAKER_SECTOR_SIZE                _2K
/** The sector offset mask. */
#define RTFSISOMAKER_SECTOR_OFFSET_MASK         (_2K - 1)
/** Maximum number of objects. */
#define RTFSISOMAKER_MAX_OBJECTS                _16M
/** Maximum number of objects per directory. */
#define RTFSISOMAKER_MAX_OBJECTS_PER_DIR        _256K /**< @todo check limit */

/** Number of bytes to store per dir record when using multiple extents. */
#define RTFSISOMAKER_MAX_ISO9660_EXTENT_SIZE    UINT32_C(0xfffff800)

/** UTF-8 name buffer.  */
#define RTFSISOMAKER_MAX_NAME_BUF               768

/** Max symbolic link target length.  */
#define RTFSISOMAKER_MAX_SYMLINK_TARGET_LEN     260

/** TRANS.TBL left padding length.
 * We keep the amount of padding low to avoid wasing memory when generating
 * these long obsolete files. */
#define RTFSISOMAKER_TRANS_TBL_LEFT_PAD         12

/** Tests if @a a_ch is in the set of d-characters. */
#define RTFSISOMAKER_IS_IN_D_CHARS(a_ch)        (RT_C_IS_UPPER(a_ch) || RT_C_IS_DIGIT(a_ch) || (a_ch) == '_')

/** Tests if @a a_ch is in the set of d-characters when uppercased. */
#define RTFSISOMAKER_IS_UPPER_IN_D_CHARS(a_ch)  (RT_C_IS_ALNUM(a_ch) || (a_ch) == '_')


/** Calculates the path table record size given the name length.
 * @note  The root directory length is 1 (name byte is 0x00), we make sure this
 *        is the case in rtFsIsoMakerNormalizeNameForNamespace. */
#define RTFSISOMAKER_CALC_PATHREC_SIZE(a_cbNameInDirRec) \
    ( RT_UOFFSETOF_DYN(ISO9660PATHREC, achDirId[(a_cbNameInDirRec) + ((a_cbNameInDirRec) & 1)]) )


/** UDF: The unique ID offset for adding to idxObj/cObjects. */
#define RTISOMAKER_UDF_UNIQUE_ID_OFFSET         15


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to an ISO maker object name space node. */
typedef struct RTFSISOMAKERNAME *PRTFSISOMAKERNAME;
/** Pointer to a const ISO maker object name space node. */
typedef struct RTFSISOMAKERNAME const *PCRTFSISOMAKERNAME;
/** Pointer to an ISO maker object name space node pointer. */
typedef PRTFSISOMAKERNAME *PPRTFSISOMAKERNAME;

/** Pointer to a common ISO image maker file system object. */
typedef struct RTFSISOMAKEROBJ *PRTFSISOMAKEROBJ;
/** Pointer to a const common ISO image maker file system object. */
typedef struct RTFSISOMAKEROBJ const *PCRTFSISOMAKEROBJ;

/** Pointer to a ISO maker file object. */
typedef struct RTFSISOMAKERFILE *PRTFSISOMAKERFILE;
/** Pointer to a const ISO maker file object. */
typedef struct RTFSISOMAKERFILE const *PCRTFSISOMAKERFILE;

/** Generic sector content producer callback. */
typedef DECLCALLBACKTYPE(int, FNRTFSISOMAKERPRODUCER,(struct RTFSISOMAKERINT *pThis, void *pvSector,
                                                      uint32_t offLocation, uint32_t uInfo));
/** Pointer to a generic sector content producer callback. */
typedef FNRTFSISOMAKERPRODUCER *PFNRTFSISOMAKERPRODUCER;

/** Producer callback entry. */
typedef struct FSISOMAKERPRODUCERENTRY
{
    /** The producer function to call. */
    PFNRTFSISOMAKERPRODUCER pfnProducer;
    /** Additional info. */
    uint32_t                uInfo;
} FSISOMAKERPRODUCERENTRY;
/** Pointer to a producer callback entry. */
typedef FSISOMAKERPRODUCERENTRY *PFSISOMAKERPRODUCERENTRY;
/** Pointer to a const producer callback entry. */
typedef FSISOMAKERPRODUCERENTRY const *PCFSISOMAKERPRODUCERENTRY;

/**
 * Callback for reading a byte range on the ISO.
 */
typedef DECLCALLBACKTYPE(int, FNRTFSISOMAKERRANGEREAD,(struct RTFSISOMAKEROUTPUTFILE *pThis, struct RTFSISOMAKERINT *pIsoMaker,
                                                       struct RTFSISOMAKERRANGENODE const *pRangeNode,
                                                       uint64_t offInRange, uint8_t *pbBuf, size_t cbToRead));
/** Pointer to ISO byte range reader callback. */
typedef FNRTFSISOMAKERRANGEREAD *PFNRTFSISOMAKERRANGEREAD;

/**
 * ISO byte range node.
 *
 * These are kept in RTFSISOMAKERINT::RangeTree and used by the
 * rtFsIsoMakerOutFile_Read() method.
 */
typedef struct RTFSISOMAKERRANGENODE
{
    /** AVL core. */
    AVLRU64NODECORE             Core;
    /** Callback for reading from the range. */
    PFNRTFSISOMAKERRANGEREAD    pfnRead;
} RTFSISOMAKERRANGENODE;
/** Pointer to an ISO byte range node. */
typedef RTFSISOMAKERRANGENODE *PRTFSISOMAKERRANGENODE;
/** Pointer to a const ISO byte range node. */
typedef RTFSISOMAKERRANGENODE const *PCRTFSISOMAKERRANGENODE;


/**
 * Filesystem object type.
 */
typedef enum RTFSISOMAKEROBJTYPE
{
    RTFSISOMAKEROBJTYPE_INVALID = 0,
    RTFSISOMAKEROBJTYPE_DIR,
    RTFSISOMAKEROBJTYPE_FILE,
    RTFSISOMAKEROBJTYPE_SYMLINK,
    RTFSISOMAKEROBJTYPE_END
} RTFSISOMAKEROBJTYPE;

/**
 * Extra name space information required for directories.
 */
typedef struct RTFSISOMAKERNAMEDIR
{
    /** The range node for the directory entries. */
    RTFSISOMAKERRANGENODE   RangeNode;
    /** The size of the directory. */
    uint32_t                cbDir;
    /** Number of children. */
    uint32_t                cChildren;
    /** Sorted array of children. */
    PPRTFSISOMAKERNAME      papChildren;
    /** The translate table file. */
    PRTFSISOMAKERFILE       pTransTblFile;

    /** The offset in the path table (ISO-9660).
     * This is set when finalizing the image.  */
    uint32_t                offPathTable;
    /** The path table identifier of this directory (ISO-9660).
    * This is set when finalizing the image.  */
    uint16_t                idPathTable;
    /** The size of the first directory record (0x00 - '.').
     * UDF: This is actually '..'.  */
    uint8_t                 cbDirRec00;
    /** The size of the second directory record (0x01 - '..').
     * UDF: Unused, always zero. */
    uint8_t                 cbDirRec01;
    /** Pointer to back to the namespace node this belongs to (for the finalized
     *  entry list). */
    PRTFSISOMAKERNAME       pName;
    /** Entry in the list of finalized directories. */
    RTLISTNODE              FinalizedEntry;
} RTFSISOMAKERNAMEDIR;
/** Pointer to directory specfic namespace node info. */
typedef RTFSISOMAKERNAMEDIR *PRTFSISOMAKERNAMEDIR;
/** Pointer to const directory specfic namespace node info. */
typedef const RTFSISOMAKERNAMEDIR *PCRTFSISOMAKERNAMEDIR;


/**
 * ISO maker object namespace node.
 */
typedef struct RTFSISOMAKERNAME
{
    /** Pointer to the file system object. */
    PRTFSISOMAKEROBJ        pObj;
    /** Pointer to the parent directory, NULL if root dir. */
    PRTFSISOMAKERNAME       pParent;

    /** Pointer to the directory information if this is a directory, NULL if not a
     * directory. */
    PRTFSISOMAKERNAMEDIR    pDir;

    /** The name specified when creating this namespace node.  Helps navigating
     * the namespace when we mangle or otherwise change the names.
     * Allocated together with of this structure, no spearate free necessary. */
    const char             *pszSpecNm;

    /** Alternative rock ridge name. */
    char                   *pszRockRidgeNm;
    /** Alternative TRANS.TBL name. */
    char                   *pszTransNm;
    /** Length of pszSpecNm. */
    uint16_t                cchSpecNm;
    /** Length of pszRockRidgeNm. */
    uint16_t                cchRockRidgeNm;
    /** Length of pszTransNm. */
    uint16_t                cchTransNm;

    /** The depth in the namespace tree of this name. */
    uint8_t                 uDepth;
    /** Set if pszTransNm is allocated separately.  Normally same as pszSpecNm. */
    bool                    fRockRidgeNmAlloced : 1;
    /** Set if pszTransNm is allocated separately.  Normally same as pszSpecNm. */
    bool                    fTransNmAlloced : 1;
    /** Set if we need to emit an ER entry (root only). */
    bool                    fRockNeedER : 1;
    /** Set if we need to emit a RR entry in the directory record. */
    bool                    fRockNeedRRInDirRec : 1;
    /** Set if we need to emit a RR entry in the spill file. */
    bool                    fRockNeedRRInSpill : 1;

    /** The mode mask.
     * Starts out as a copy of RTFSISOMAKEROBJ::fMode. */
    RTFMODE                 fMode;
    /** The owner ID.
     * Starts out as a copy of RTFSISOMAKEROBJ::uid. */
    RTUID                   uid;
    /** The group ID.
     * Starts out as a copy of RTFSISOMAKEROBJ::gid. */
    RTGID                   gid;
    /** The device number if a character or block device.
     * This is for Rock Ridge.  */
    RTDEV                   Device;
    /** The number of hardlinks to report in the file stats.
     * This is for Rock Ridge.  */
    uint32_t                cHardlinks;

    /** UDF: ICB sector numbers for UDF, otherwise not used. */
    uint32_t                uUdfIcbSector;

    /** The offset of the directory entry in the parent directory. */
    uint32_t                offDirRec;
    /** Size of the directory record (ISO-9660).
     * This is set when the image is being finalized. */
    uint16_t                cbDirRec;
    /** Number of directory records needed to cover the entire file size.
     * UDF: Always 1. */
    uint16_t                cDirRecs;
    /** The total directory record size (cbDirRec * cDirRecs), including end of
     *  sector zero padding.
     * UDF: This is the same as cbDirRec, unless padding was required to prevent
     * the next FID crossing a sector boundary. */
    uint16_t                cbDirRecTotal;

    /** Rock ridge flags (ISO9660RRIP_RR_F_XXX). */
    uint8_t                 fRockEntries;
    /** Number of rock ridge data bytes in the directory record.  Unaligned! */
    uint8_t                 cbRockInDirRec;
    /** Rock ridge spill file data offset, UINT32_MAX if placed in dir record. */
    uint32_t                offRockSpill;
    /** Size of rock data in spill file. */
    uint16_t                cbRockSpill;

    /** The number of bytes the name requires in the directory record.
     * UDF: Top bit (RTFSISOMAKERNAME_UDF_8BIT_NAME_FLAG) indicates 8-bit encoding
     * (set) or UTF-16 (clear). */
    uint16_t                cbNameInDirRec;
    /** The name length. */
    uint16_t                cchName;
    /** The name. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char                    szName[RT_FLEXIBLE_ARRAY];
} RTFSISOMAKERNAME;

/** UDF: Flag in RTFSISOMAKERNAME::cbNameInDirRec for indicating 8-bit chars. */
#define RTFSISOMAKERNAME_UDF_8BIT_NAME_FLAG     UINT16_C(0x8000)


/**
 * A ISO maker namespace.
 */
typedef struct RTFSISOMAKERNAMESPACE
{
    /** The namespace root. */
    PRTFSISOMAKERNAME       pRoot;
    /** Total number of name nodes in the namespace. */
    uint32_t                cNames;
    /** Total number of directories in the namespace.
     * Set by rtFsIsoMakerFinalizeGatherDirs(). */
    uint32_t                cDirs;
    /** The namespace selector (RTFSISOMAKER_NAMESPACE_XXX). */
    uint32_t                fNamespace;
    /** Offset into RTFSISOMAKERNAMESPACE of the name member. */
    uint32_t                offName;
    /** The configuration level for this name space.
     *     - For the primary ISO-9660 namespace this is 1, 2, or 3.
     *     - For the joliet namespace this 0 (joliet disabled), 1, 2, or 3.
     *     - For UDF this the ECMA edition is 0 (disabled), 2 or 3.
     *     - For HFS namespace this is either @c true or @c false. */
    uint8_t                 uLevel;
    /** The rock ridge level: 1 - enabled; 2 - with ER tag.
     * Linux behaves a little different when seeing the ER tag. */
    uint8_t                 uRockRidgeLevel;
    /** UDF: This the UDF specification revision (e.g. 0x0102 for 1.02). */
    uint16_t                uSpecRev;
    /** The TRANS.TBL filename if enabled, NULL if disabled.
     * When not NULL, this may be pointing to heap or g_szTransTbl. */
    char                   *pszTransTbl;
    /** The system ID (ISO9660PRIMARYVOLDESC::achSystemId). Empty if NULL.
     * When not NULL, this may be pointing to heap or g_szSystemId. */
    char                   *pszSystemId;
    /** The volume ID / label (ISO9660PRIMARYVOLDESC::achVolumeId).
     * A string representation of RTFSISOMAKERINT::ImageCreationTime if NULL. */
    char                   *pszVolumeId;
    /** The volume set ID (ISO9660PRIMARYVOLDESC::achVolumeSetId). Empty if NULL. */
    char                   *pszVolumeSetId;
    /** The publisher ID or (root) file reference (ISO9660PRIMARYVOLDESC::achPublisherId).  Empty if NULL.
     * UDF: Placed in the LVInfo1 field.  */
    char                   *pszPublisherId;
    /** The data preperer ID or (root) file reference (ISO9660PRIMARYVOLDESC::achDataPreparerId). Empty if NULL.
     * UDF: Placed in the LVInfo2 field. */
    char                   *pszDataPreparerId;
    /* The application ID or (root) file reference (ISO9660PRIMARYVOLDESC::achApplicationId).
     * Defaults to g_szAppIdPrimaryIso or g_szAppIdJoliet. */
    char                   *pszApplicationId;
    /** The copyright (root) file identifier (ISO9660PRIMARYVOLDESC::achCopyrightFileId).  None if NULL. */
    char                   *pszCopyrightFileId;
    /** The abstract (root) file identifier (ISO9660PRIMARYVOLDESC::achAbstractFileId). None if NULL. */
    char                   *pszAbstractFileId;
    /** The bibliographic (root) file identifier (ISO9660PRIMARYVOLDESC::achBibliographicFileId).  None if NULL. */
    char                   *pszBibliographicFileId;

    /** @name Finalized data
     * @{  */
    /** The range node for the little endian path table in this namespace. */
    RTFSISOMAKERRANGENODE   RangeNodePathTableL;
    /** The range node for the big endian path table in this namespace. */
    RTFSISOMAKERRANGENODE   RangeNodePathTableM;
    /** The size of the path table.   */
    uint32_t                cbPathTable;
    /** List of finalized directories for this namespace.
     * The list is in path table order so it can be generated on the fly.  The
     * directories will be ordered in the same way. */
    RTLISTANCHOR            FinalizedDirs;
    /** Rock ridge spill file. */
    PRTFSISOMAKERFILE       pRRSpillFile;
    /** @} */
} RTFSISOMAKERNAMESPACE;
/** Pointer to a namespace. */
typedef RTFSISOMAKERNAMESPACE *PRTFSISOMAKERNAMESPACE;
/** Pointer to a const namespace. */
typedef RTFSISOMAKERNAMESPACE const *PCRTFSISOMAKERNAMESPACE;


/**
 * Common base structure for the file system objects.
 *
 * The times are shared across all namespaces, while the uid, gid and mode are
 * duplicates in each namespace.
 */
typedef struct RTFSISOMAKEROBJ
{
    /** The linear list entry of the image content. */
    RTLISTNODE              Entry;
    /** The object index.
     * UDF: This is used for idUnique and INode on UDF with an offset of 16 for all
     * but the root directory. */
    uint32_t                idxObj;
    /** The type of this object. */
    RTFSISOMAKEROBJTYPE     enmType;

    /** The primary ISO-9660 name space name. */
    PRTFSISOMAKERNAME       pPrimaryName;
    /** The joliet name space name. */
    PRTFSISOMAKERNAME       pJolietName;
    /** The UDF name space name. */
    PRTFSISOMAKERNAME       pUdfName;
    /** The HFS name space name. */
    PRTFSISOMAKERNAME       pHfsName;

    /** Birth (creation) time. */
    RTTIMESPEC              BirthTime;
    /** Attribute change time. */
    RTTIMESPEC              ChangeTime;
    /** Modification time. */
    RTTIMESPEC              ModificationTime;
    /** Accessed time. */
    RTTIMESPEC              AccessedTime;

    /** Owner ID. */
    RTUID                   uid;
    /** Group ID. */
    RTGID                   gid;
    /** Attributes (unix permissions bits mainly). */
    RTFMODE                 fMode;

    /** Used to make sure things like the boot catalog stays in the image even if
     * it's not mapped into any of the namespaces. */
    uint32_t                cNotOrphan;
} RTFSISOMAKEROBJ;


/**
 * File source type.
 */
typedef enum RTFSISOMAKERSRCTYPE
{
    RTFSISOMAKERSRCTYPE_INVALID = 0,
    RTFSISOMAKERSRCTYPE_PATH,
    RTFSISOMAKERSRCTYPE_VFS_FILE,
    RTFSISOMAKERSRCTYPE_COMMON,
    RTFSISOMAKERSRCTYPE_TRANS_TBL,
    RTFSISOMAKERSRCTYPE_RR_SPILL,
    RTFSISOMAKERSRCTYPE_END
} RTFSISOMAKERSRCTYPE;

/**
 * ISO maker file object.
 */
typedef struct RTFSISOMAKERFILE
{
    /** The common bit. */
    RTFSISOMAKEROBJ         Core;
    /** The file data size. */
    uint64_t                cbData;
    /** The data range node.
     *  The Core.Key member is set to UINT64_MAX util the location is finalized. */
    RTFSISOMAKERRANGENODE   RangeNode;

    /** The type of source object. */
    RTFSISOMAKERSRCTYPE     enmSrcType;
    /** The source data. */
    union
    {
        /** Path to the source file.
         * Allocated together with this structure.  */
        const char         *pszSrcPath;
        /** Source VFS file. */
        RTVFSFILE           hVfsFile;
        /** Source is a part of a common VFS file. */
        struct
        {
            /** The offset into the file */
            uint64_t        offData;
            /** The index of the common file. */
            uint32_t        idxSrc;
        } Common;
        /** The directory the translation table belongs to. */
        PRTFSISOMAKERNAME   pTransTblDir;
        /** The namespace for a rock ridge spill file.. */
        PRTFSISOMAKERNAMESPACE pRockSpillNamespace;
    } u;

    /** Boot info table to patch into the file.
     * This is calculated during file finalization as it needs the file location. */
    PISO9660SYSLINUXINFOTABLE pBootInfoTable;

    /** Entry in the list of finalized directories. */
    RTLISTNODE              FinalizedEntry;
} RTFSISOMAKERFILE;


/**
 * ISO maker directory object.
 *
 * Unlike files, the allocation info is name space specific and lives in the
 * corresponding RTFSISOMAKERNAMEDIR structures.
 */
typedef struct RTFSISOMAKERDIR
{
    /** The common bit. */
    RTFSISOMAKEROBJ         Core;
} RTFSISOMAKERDIR;
/** Pointer to an ISO maker directory object.  */
typedef RTFSISOMAKERDIR *PRTFSISOMAKERDIR;


/**
 * ISO maker symlink object.
 */
typedef struct RTFSISOMAKERSYMLINK
{
    /** The common bit. */
    RTFSISOMAKEROBJ         Core;
    /** The size of the rock ridge 'SL' records for this link. */
    uint16_t                cbSlRockRidge;
    /** The symbolic link target length. */
    uint16_t                cchTarget;
    /** The symbolic link target. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char                    szTarget[RT_FLEXIBLE_ARRAY];
} RTFSISOMAKERSYMLINK;
/** Pointer to an ISO maker directory object.  */
typedef RTFSISOMAKERSYMLINK *PRTFSISOMAKERSYMLINK;
/** Pointer to a const ISO maker directory object.  */
typedef const RTFSISOMAKERSYMLINK *PCRTFSISOMAKERSYMLINK;



/**
 * Instance data for a ISO image maker.
 */
typedef struct RTFSISOMAKERINT
{
    /** Magic value (RTFSISOMAKERINT_MAGIC). */
    uint32_t                uMagic;
    /** Reference counter. */
    uint32_t volatile       cRefs;

    /** Set after we've been fed the first bit of content.
     * This means that the namespace configuration has been finalized and can no
     * longer be changed because it's simply too much work to do adjustments
     * after having started to add files. */
    bool                    fSeenContent;
    /** Set once we've finalized the image structures.
     * After this no more changes are allowed.  */
    bool                    fFinalized;

    /** The primary ISO-9660 namespace. */
    RTFSISOMAKERNAMESPACE   PrimaryIso;
    /** The joliet namespace. */
    RTFSISOMAKERNAMESPACE   Joliet;
    /** The UDF namespace. */
    RTFSISOMAKERNAMESPACE   Udf;
    /** The hybrid HFS+ namespace. */
    RTFSISOMAKERNAMESPACE   Hfs;

    /** The list of objects (RTFSISOMAKEROBJ). */
    RTLISTANCHOR            ObjectHead;
    /** Number of objects in the image (ObjectHead).
     * This is used to number them, i.e. create RTFSISOMAKEROBJ::idxObj.  */
    uint32_t                cObjects;

    /** Amount of file data. */
    uint64_t                cbData;
    /** Number of volume descriptors. */
    uint32_t                cVolumeDescriptors;
    /** The image (trail) padding in bytes. */
    uint32_t                cbImagePadding;

    /** The 'now' timestamp we use for the whole image.
     * This way we'll save lots of RTTimeNow calls and have similar timestamps
     * over the whole image. */
    RTTIMESPEC              ImageCreationTime;
    /** Indicates strict or non-strict attribute handling style.
     * See RTFsIsoMakerSetAttributeStyle() for details.  */
    bool                    fStrictAttributeStyle;
    /** The default owner ID. */
    RTUID                   uidDefault;
    /** The default group ID. */
    RTGID                   gidDefault;
    /** The default file mode mask. */
    RTFMODE                 fDefaultFileMode;
    /** The default file mode mask. */
    RTFMODE                 fDefaultDirMode;

    /** Forced file mode mask (permissions only). */
    RTFMODE                 fForcedFileMode;
    /** Set if fForcedFileMode is active. */
    bool                    fForcedFileModeActive;
    /** Set if fForcedDirMode is active. */
    bool                    fForcedDirModeActive;
    /** Forced directory mode mask (permissions only). */
    RTFMODE                 fForcedDirMode;

    /** Number of common source files. */
    uint32_t                cCommonSources;
    /** Array of common source file handles. */
    PRTVFSFILE              paCommonSources;

    /** @name Boot related stuff
     * @{ */
    /** The boot catalog file. */
    PRTFSISOMAKERFILE       pBootCatFile;
    /** Per boot catalog entry data needed for updating offsets when finalizing. */
    struct
    {
        /** The type (ISO9660_ELTORITO_HEADER_ID_VALIDATION_ENTRY,
         * ISO9660_ELTORITO_HEADER_ID_SECTION_HEADER,
         * ISO9660_ELTORITO_HEADER_ID_FINAL_SECTION_HEADER,
         * ISO9660_ELTORITO_BOOT_INDICATOR_BOOTABLE or
         * ISO9660_ELTORITO_BOOT_INDICATOR_NOT_BOOTABLE). */
        uint8_t             bType;
        /** Number of entries related to this one.  This is zero for unused entries,
         *  2 for the validation entry, 2+ for section headers, and 1 for images. */
        uint8_t             cEntries;
        /** The boot file. */
        PRTFSISOMAKERFILE   pBootFile;
    }                       aBootCatEntries[64];
    /** @} */

    /** @name Finalized image stuff
     * @{ */
    /** The finalized image size. */
    uint64_t                cbFinalizedImage;
    /** The root of the image byte range tree (RTFSISOMAKERRANGENODE). */
    AVLRU64TREE             RangeTree;

    /** System area content (sectors 0 thru 15).  This is NULL if the system area
     * are all zeros, which is often the case.   Hybrid ISOs have an MBR followed by
     * a GUID partition table here, helping making the image bootable when
     * transfered to a USB stick. */
    uint8_t                *pbSysArea;
    /** Number of non-zero system area bytes pointed to by pbSysArea.  */
    size_t                  cbSysArea;
    /** The system area range node. */
    RTFSISOMAKERRANGENODE   SysAreaRange;

    /** Pointer to the buffer holding the volume descriptors.
     * Location: Sector 16 (offset 0x8000).  */
    uint8_t                *pbVolDescs;
    /** The ISO-9660 volume descriptor range node. */
    RTFSISOMAKERRANGENODE   VolDescSeqRange;
    /** Pointer to the primary volume descriptor. */
    PISO9660PRIMARYVOLDESC  pPrimaryVolDesc;
    /** El Torito volume descriptor. */
    PISO9660BOOTRECORDELTORITO pElToritoDesc;
    /** Pointer to the primary volume descriptor. */
    PISO9660SUPVOLDESC      pJolietVolDesc;
    /** Terminating ISO-9660 volume descriptor. */
    PISO9660VOLDESCHDR      pTerminatorVolDesc;

    /** UDF: The range node for the AVDP & VDS sequences. */
    RTFSISOMAKERRANGENODE   UdfAvdpAndVdsRange;
    /** UDF: The range node for the file set descriptor sequence. */
    RTFSISOMAKERRANGENODE   UdfFileSetDescSeqRange;
    /** UDF: The byte offset of the UDF partition. */
    uint64_t                offUdfPartition;
    /** UDF: Number of sectors in the UDF partition. */
    uint32_t                cUdfPartitionSectors;
    /** UDF: Number of entries in the sector sequence at 256 that starts with AVDP.
     * We typically do AVDP, followed by the primary VDS, followed by the integrity
     * sequence followed by the reserve VDS. */
    uint8_t                 cUdfAvdpAndVdsEntries;
    /** UDF: The sequence starting at sector 256. */
    FSISOMAKERPRODUCERENTRY aUdfAvdpAndVdsEntries[24];
    /** UDF: The block of ICBs.
     * The assignments follow directory order for better locality. */
    RTFSISOMAKERRANGENODE   UdfIcbsForObjsRange;
    /** UDF: Object data pointers running parallel to UdfIcbsForFilesRange. */
    PRTFSISOMAKEROBJ       *papUdfIcbToObjs;
    /** UDF: The range node the final AVDP. */
    RTFSISOMAKERRANGENODE   UdfFinalAvdpRange;

    /** Finalized file head (RTFSISOMAKERFILE).
     * The list is ordered by disk location.  Files are following the
     * directories and path tables. */
    RTLISTANCHOR            FinalizedFiles;
    /** @} */
} RTFSISOMAKERINT;
/** Pointer to an ISO maker instance. */
typedef RTFSISOMAKERINT *PRTFSISOMAKERINT;


/**
 * Instance data of an ISO maker output file.
 */
typedef struct RTFSISOMAKEROUTPUTFILE
{
    /** The ISO maker (owns a reference). */
    PRTFSISOMAKERINT        pIsoMaker;
    /** The current file position. */
    uint64_t                offCurPos;
    /** The source file hVfsSrcFile corresponds to. */
    PRTFSISOMAKERFILE       pSrcFile;
    /** Source file corresponding to pSrcFile.
     * This is also used when dealing with a RTFSISOMAKERSRCTYPE_VFS_FILE or
     * RTFSISOMAKERSRCTYPE_TRANS_TBL file. */
    RTVFSFILE               hVfsSrcFile;
    /** Current directory hint for the primary ISO namespace. */
    PRTFSISOMAKERNAMEDIR    pDirHintPrimaryIso;
    /** Current directory hint for the joliet namespace. */
    PRTFSISOMAKERNAMEDIR    pDirHintJoliet;
    /** Joliet directory child index hint. */
    uint32_t                iChildPrimaryIso;
    /** Joliet directory child index hint. */
    uint32_t                iChildJoliet;
} RTFSISOMAKEROUTPUTFILE;
/** Pointer to the instance data of an ISO maker output file. */
typedef RTFSISOMAKEROUTPUTFILE *PRTFSISOMAKEROUTPUTFILE;


/**
 * Directory entry type.
 */
typedef enum RTFSISOMAKERDIRTYPE
{
    /** Invalid directory entry. */
    RTFSISOMAKERDIRTYPE_INVALID = 0,
    /** Entry for the current directory, aka ".". */
    RTFSISOMAKERDIRTYPE_CURRENT,
    /** Entry for the parent directory, aka "..". */
    RTFSISOMAKERDIRTYPE_PARENT,
    /** Entry for a regular directory entry. */
    RTFSISOMAKERDIRTYPE_OTHER
} RTFSISOMAKERDIRTYPE;


/**
 * ISO Image Maker space allocator.
 *
 * This is divided into one or two areas, the first is ISO-9660 only data and
 * the second is for data shared with UDF.
 */
typedef struct RTFSISOMAKERALLOCATOR
{
    /** Next free byte offset for ISO-9660/non-UDF data.
     * When offUdf isn't UINT64_MAX, this won't go higher than sector 256.
     * If there isn't any room for the requested data, it will be allocated in
     * the UDF space. */
    uint64_t    offIso9660;
    /** Next free byte offset for UDF data, UINT64_MAX if UDF is disabled. */
    uint64_t    offUdf;
} RTFSISOMAKERALLOCATOR;
/** Pointer to the ISO Image Maker space allocator. */
typedef RTFSISOMAKERALLOCATOR *PRTFSISOMAKERALLOCATOR;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int rtFsIsoMakerObjSetName(PRTFSISOMAKERINT pThis, PRTFSISOMAKERNAMESPACE pNamespace, PRTFSISOMAKEROBJ pObj,
                                  PRTFSISOMAKERNAME pParent, const char *pchSpec, size_t cchSpec, bool fNoNormalize,
                                  PPRTFSISOMAKERNAME ppNewName);
static int rtFsIsoMakerObjUnsetName(PRTFSISOMAKERINT pThis, PRTFSISOMAKERNAMESPACE pNamespace, PRTFSISOMAKEROBJ pObj);
static int rtFsIsoMakerAddUnnamedDirWorker(PRTFSISOMAKERINT pThis, PCRTFSOBJINFO pObjInfo, PRTFSISOMAKERDIR *ppDir);
static int rtFsIsoMakerAddUnnamedFileWorker(PRTFSISOMAKERINT pThis, PCRTFSOBJINFO pObjInfo, size_t cbExtra,
                                            PRTFSISOMAKERFILE *ppFile);
static int rtFsIsoMakerObjRemoveWorker(PRTFSISOMAKERINT pThis, PRTFSISOMAKEROBJ pObj);

static ssize_t rtFsIsoMakerOutFile_RockRidgeGenSL(const char *pszTarget, uint8_t *pbBuf, size_t cbBuf);
static DECLCALLBACK(int) rtFsIsoMakerOutFile_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual);

static FNRTFSISOMAKERPRODUCER rtFsIsoMakerUdfProducePrimaryVolumeDesc;
static FNRTFSISOMAKERPRODUCER rtFsIsoMakerUdfProduceLogicalVolumeDesc;
static FNRTFSISOMAKERPRODUCER rtFsIsoMakerUdfProducePartitionDesc;
static FNRTFSISOMAKERPRODUCER rtFsIsoMakerUdfProduceUnallocatedSpaceDesc;
static FNRTFSISOMAKERPRODUCER rtFsIsoMakerUdfProduceImplementationUseVolumeDesc;
static FNRTFSISOMAKERPRODUCER rtFsIsoMakerUdfProduceTerminatingDesc;
static FNRTFSISOMAKERPRODUCER rtFsIsoMakerUdfProduceVolumeIntegrityDesc;
static FNRTFSISOMAKERPRODUCER rtFsIsoMakerUdfProduceFileSetDesc;

static FNRTFSISOMAKERRANGEREAD rtFsIsoMakerOutFile_ReadSysArea;
static FNRTFSISOMAKERRANGEREAD rtFsIsoMakerOutFile_ReadIso9660Vds16;
static FNRTFSISOMAKERRANGEREAD rtFsIsoMakerOutFile_ReadIso9660Dir;
static FNRTFSISOMAKERRANGEREAD rtFsIsoMakerOutFile_ReadIso9660DirJoliet;
static FNRTFSISOMAKERRANGEREAD rtFsIsoMakerOutFile_ReadIso9660PathTableBig;
static FNRTFSISOMAKERRANGEREAD rtFsIsoMakerOutFile_ReadIso9660PathTableLittle;
static FNRTFSISOMAKERRANGEREAD rtFsIsoMakerOutFile_ReadUdfVds256;
static FNRTFSISOMAKERRANGEREAD rtFsIsoMakerOutFile_ReadUdfFileSetDescSeq;
static FNRTFSISOMAKERRANGEREAD rtFsIsoMakerOutFile_ReadUdfIcbs;
static FNRTFSISOMAKERRANGEREAD rtFsIsoMakerOutFile_ReadUdfDir;
static FNRTFSISOMAKERRANGEREAD rtFsIsoMakerOutFile_ReadFileData;
static FNRTFSISOMAKERRANGEREAD rtFsIsoMakerOutFile_ReadUdfAvdp;


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Help for iterating over namespaces.
 */
static const struct
{
    /** The RTFSISOMAKER_NAMESPACE_XXX indicator.  */
    uint32_t        fNamespace;
    /** Offset into RTFSISOMAKERINT of the namespace member. */
    uintptr_t       offNamespace;
    /** Offset into RTFSISOMAKERNAMESPACE of the name member. */
    uintptr_t       offName;
    /** Namespace name for debugging purposes. */
    const char     *pszName;
} g_aRTFsIsoNamespaces[] =
{
    {   RTFSISOMAKER_NAMESPACE_ISO_9660, RT_UOFFSETOF(RTFSISOMAKERINT, PrimaryIso), RT_UOFFSETOF(RTFSISOMAKEROBJ, pPrimaryName), "iso-9660" },
    {   RTFSISOMAKER_NAMESPACE_JOLIET,   RT_UOFFSETOF(RTFSISOMAKERINT, Joliet),     RT_UOFFSETOF(RTFSISOMAKEROBJ, pJolietName),  "joliet" },
    {   RTFSISOMAKER_NAMESPACE_UDF,      RT_UOFFSETOF(RTFSISOMAKERINT, Udf),        RT_UOFFSETOF(RTFSISOMAKEROBJ, pUdfName),     "udf" },
    {   RTFSISOMAKER_NAMESPACE_HFS,      RT_UOFFSETOF(RTFSISOMAKERINT, Hfs),        RT_UOFFSETOF(RTFSISOMAKEROBJ, pHfsName),     "hfs" },
};

/**
 * Translates a single namespace flag (RTFSISOMAKER_NAMESPACE_XXX) to an
 * index into g_aRTFsIsoNamespaces.
 */
static const uint8_t g_aidxRTFsIsoNamespaceFlagToIdx[] =
{
    /*[0]                               = */ RT_ELEMENTS(g_aRTFsIsoNamespaces),
    /*[RTFSISOMAKER_NAMESPACE_ISO_9660] = */ 0,
    /*[RTFSISOMAKER_NAMESPACE_JOLIET]   = */ 1,
    /*[3]                               = */ RT_ELEMENTS(g_aRTFsIsoNamespaces),
    /*[RTFSISOMAKER_NAMESPACE_UDF]      = */ 2,
    /*[5]                               = */ RT_ELEMENTS(g_aRTFsIsoNamespaces),
    /*[6]                               = */ RT_ELEMENTS(g_aRTFsIsoNamespaces),
    /*[7]                               = */ RT_ELEMENTS(g_aRTFsIsoNamespaces),
    /*[RTFSISOMAKER_NAMESPACE_HFS]      = */ 3,
    /*[9]                               = */ RT_ELEMENTS(g_aRTFsIsoNamespaces),
    /*[10]                              = */ RT_ELEMENTS(g_aRTFsIsoNamespaces),
    /*[11]                              = */ RT_ELEMENTS(g_aRTFsIsoNamespaces),
    /*[12]                              = */ RT_ELEMENTS(g_aRTFsIsoNamespaces),
    /*[13]                              = */ RT_ELEMENTS(g_aRTFsIsoNamespaces),
    /*[14]                              = */ RT_ELEMENTS(g_aRTFsIsoNamespaces),
    /*[15]                              = */ RT_ELEMENTS(g_aRTFsIsoNamespaces),
};

/** The default translation table filename. */
static const char   g_szTransTbl[] = "TRANS.TBL";
/** The default application ID for the primary ISO-9660 volume descriptor. */
static char         g_szAppIdPrimaryIso[64] = "";
/** The default application ID for the joliet volume descriptor. */
static char         g_szAppIdJoliet[64]     = "";
/** The default system ID the primary ISO-9660 volume descriptor. */
static char         g_szSystemId[64] = "";

/** Special partition number value used to match map entry to descriptor. */
static uint16_t const   g_uUdfPartNo    = 0x2442;
/** Our application ID. */
static char const       g_szUdfAppId[]  = "*IPRT ISO Maker";
/** Our implemenation ID. */
static char const       g_szUdfImplId[] = "*IPRT";
/** The UDF OS class (@udf260{6.3.1}) */
#ifdef RT_OS_WINDOWS
static uint8_t const    g_bUdfOsClass   = 6;
#elif defined(RT_OS_OS2)
static uint8_t const    g_bUdfOsClass   = 2;
#else
static uint8_t const    g_bUdfOsClass   = 4;
#endif
/** The UDF OS identifier (@udf260{6.3.2}) */
#ifdef RT_OS_LINUX
static uint8_t const    g_bUdfOsId      = 5;
#elif defined(RT_OS_FREEBSD)
static uint8_t const    g_bUdfOsId      = 7;
#elif defined(RT_OS_NETBSD)
static uint8_t const    g_bUdfOsId      = 9;
#elif defined(RT_OS_SOLARIS)
static uint8_t const    g_bUdfOsId      = 2;
#else
static uint8_t const    g_bUdfOsId      = 0;
#endif

/** UDF: Our File Set Descriptor Sequence. */
static const FSISOMAKERPRODUCERENTRY s_aFileSetDescSeqEntries[] =
{
    { rtFsIsoMakerUdfProduceFileSetDesc, 0 },
    { rtFsIsoMakerUdfProduceTerminatingDesc, 0 },
};


/**
 * Creates an ISO maker instance.
 *
 * @returns IPRT status code.
 * @param   phIsoMaker          Where to return the handle to the new ISO maker.
 */
RTDECL(int) RTFsIsoMakerCreate(PRTFSISOMAKER phIsoMaker)
{
    /*
     * Do some integrity checks first.
     */
    AssertReturn(g_aRTFsIsoNamespaces[g_aidxRTFsIsoNamespaceFlagToIdx[RTFSISOMAKER_NAMESPACE_ISO_9660]].fNamespace == RTFSISOMAKER_NAMESPACE_ISO_9660,
                 VERR_ISOMK_IPE_TABLE);
    AssertReturn(g_aRTFsIsoNamespaces[g_aidxRTFsIsoNamespaceFlagToIdx[RTFSISOMAKER_NAMESPACE_JOLIET]].fNamespace   == RTFSISOMAKER_NAMESPACE_JOLIET,
                 VERR_ISOMK_IPE_TABLE);
    AssertReturn(g_aRTFsIsoNamespaces[g_aidxRTFsIsoNamespaceFlagToIdx[RTFSISOMAKER_NAMESPACE_UDF]].fNamespace      == RTFSISOMAKER_NAMESPACE_UDF,
                 VERR_ISOMK_IPE_TABLE);
    AssertReturn(g_aRTFsIsoNamespaces[g_aidxRTFsIsoNamespaceFlagToIdx[RTFSISOMAKER_NAMESPACE_HFS]].fNamespace      == RTFSISOMAKER_NAMESPACE_HFS,
                 VERR_ISOMK_IPE_TABLE);

    if (g_szAppIdPrimaryIso[0] == '\0')
        RTStrPrintf(g_szAppIdPrimaryIso, sizeof(g_szAppIdPrimaryIso), "IPRT ISO MAKER V%u.%u.%u R%s",
                    RTBldCfgVersionMajor(), RTBldCfgVersionMinor(), RTBldCfgVersionBuild(), RTBldCfgRevisionStr());
    if (g_szAppIdJoliet[0] == '\0')
        RTStrPrintf(g_szAppIdJoliet, sizeof(g_szAppIdJoliet),
                    "IPRT ISO Maker v%s r%s", RTBldCfgVersion(), RTBldCfgRevisionStr());
    if (g_szSystemId[0] == '\0')
    {
        RTStrCopy(g_szSystemId, sizeof(g_szSystemId), RTBldCfgTargetDotArch());
        RTStrToUpper(g_szSystemId);
    }

    /*
     * Create the instance with defaults.
     */
    int              rc;
    PRTFSISOMAKERINT pThis = (PRTFSISOMAKERINT)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
    {
        pThis->uMagic                       = RTFSISOMAKERINT_MAGIC;
        pThis->cRefs                        = 1;
        //pThis->fSeenContent               = false;
        //pThis->fFinalized                 = false;

        pThis->PrimaryIso.fNamespace        = RTFSISOMAKER_NAMESPACE_ISO_9660;
        pThis->PrimaryIso.offName           = RT_UOFFSETOF(RTFSISOMAKEROBJ, pPrimaryName);
        pThis->PrimaryIso.uLevel            = 3; /* 30 char names, large files */
        pThis->PrimaryIso.uRockRidgeLevel   = 1;
        //pThis->PrimaryIso.uSpecRev        = 0;
        pThis->PrimaryIso.pszTransTbl       = (char *)g_szTransTbl;
        pThis->PrimaryIso.pszSystemId       = g_szSystemId;
        //pThis->PrimaryIso.pszVolumeId     = NULL;
        //pThis->PrimaryIso.pszSetVolumeId  = NULL;
        //pThis->PrimaryIso.pszPublisherId  = NULL;
        //pThis->PrimaryIso.pszDataPreparerId = NULL;
        pThis->PrimaryIso.pszApplicationId  = g_szAppIdPrimaryIso;
        //pThis->PrimaryIso.pszCopyrightFileId = NULL;
        //pThis->PrimaryIso.pszAbstractFileId = NULL;
        //pThis->PrimaryIso.pszBibliographicFileId = NULL;
        pThis->PrimaryIso.cbPathTable       = 0;
        RTListInit(&pThis->PrimaryIso.FinalizedDirs);
        //pThis->PrimaryIso.pRRSpillFile    = NULL;

        pThis->Joliet.fNamespace            = RTFSISOMAKER_NAMESPACE_JOLIET;
        pThis->Joliet.offName               = RT_UOFFSETOF(RTFSISOMAKEROBJ, pJolietName);
        pThis->Joliet.uLevel                = 3;
        //pThis->Joliet.uRockRidgeLevel     = 0;
        //pThis->Joliet.uSpecRev            = 0;
        //pThis->Joliet.pszTransTbl         = NULL;
        //pThis->Joliet.pszSystemId         = NULL;
        //pThis->Joliet.pszVolumeId         = NULL;
        //pThis->Joliet.pszSetVolumeId      = NULL;
        //pThis->Joliet.pszPublisherId      = NULL;
        //pThis->Joliet.pszDataPreparerId   = NULL;
        pThis->Joliet.pszApplicationId      = g_szAppIdJoliet;
        //pThis->Joliet.pszCopyrightFileId  = NULL;
        //pThis->Joliet.pszAbstractFileId   = NULL;
        //pThis->Joliet.pszBibliographicFileId = NULL;
        pThis->Joliet.cbPathTable           = 0;
        RTListInit(&pThis->Joliet.FinalizedDirs);
        //pThis->Joliet.pRRSpillFile        = NULL;

        pThis->Udf.fNamespace               = RTFSISOMAKER_NAMESPACE_UDF;
        pThis->Udf.offName                  = RT_UOFFSETOF(RTFSISOMAKEROBJ, pUdfName);
        //pThis->Udf.uLevel                 = 0;
        //pThis->Udf.uRockRidgeLevel        = 0;
        pThis->Udf.uSpecRev                 = 0x102; /* UDF 1.02 */
        //pThis->Udf.pszTransTbl            = NULL;
        //pThis->Udf.uRockRidgeLevel        = 0;
        //pThis->Udf.pszTransTbl            = NULL;
        //pThis->Udf.pszSystemId            = NULL;
        //pThis->Udf.pszVolumeId            = NULL;
        //pThis->Udf.pszSetVolumeId         = NULL;
        //pThis->Udf.pszPublisherId         = NULL;
        //pThis->Udf.pszDataPreparerId      = NULL;
        //pThis->Udf.pszApplicationId       = NULL;
        //pThis->Udf.pszCopyrightFileId     = NULL;
        //pThis->Udf.pszAbstractFileId      = NULL;
        //pThis->Udf.pszBibliographicFileId = NULL;
        pThis->Udf.cbPathTable              = 0;
        RTListInit(&pThis->Udf.FinalizedDirs);
        //pThis->H.pRRSpillFile             = NULL;

        pThis->Hfs.fNamespace               = RTFSISOMAKER_NAMESPACE_HFS;
        pThis->Hfs.offName                  = RT_UOFFSETOF(RTFSISOMAKEROBJ, pHfsName);
        //pThis->Hfs.uLevel                 = 0;
        //pThis->Hfs.uRockRidgeLevel        = 0;
        //pThis->Hfs.uSpecRev               = 0;
        //pThis->Hfs.pszTransTbl            = NULL;
        //pThis->Hfs.pszSystemId            = NULL;
        //pThis->Hfs.pszVolumeId            = NULL;
        //pThis->Hfs.pszSetVolumeId         = NULL;
        //pThis->Hfs.pszPublisherId         = NULL;
        //pThis->Hfs.pszDataPreparerId      = NULL;
        //pThis->Hfs.pszApplicationId       = NULL;
        //pThis->Hfs.pszCopyrightFileId     = NULL;
        //pThis->Hfs.pszAbstractFileId      = NULL;
        //pThis->Hfs.pszBibliographicFileId = NULL;
        pThis->Hfs.cbPathTable              = 0;
        RTListInit(&pThis->Hfs.FinalizedDirs);
        //pThis->H.pRRSpillFile             = NULL;

        RTListInit(&pThis->ObjectHead);
        //pThis->cObjects                   = 0;
        //pThis->cbData                     = 0;

        pThis->cVolumeDescriptors           = 3; /* primary, secondary joliet, terminator. */
        pThis->cbImagePadding               = 150 * RTFSISOMAKER_SECTOR_SIZE;

        //pThis->fStrictAttributeStyle      = false;
        pThis->uidDefault                   = NIL_RTUID;  /* NIL -> 0/root for rock-ridge */
        pThis->gidDefault                   = NIL_RTGID;
        pThis->fDefaultFileMode             = 0444 | RTFS_TYPE_FILE      | RTFS_DOS_ARCHIVED  | RTFS_DOS_READONLY;
        pThis->fDefaultDirMode              = 0555 | RTFS_TYPE_DIRECTORY | RTFS_DOS_DIRECTORY | RTFS_DOS_READONLY;

        //pThis->fForcedFileMode            = 0;
        //pThis->fForcedFileModeActive      = false;
        //pThis->fForcedDirModeActive       = false;
        //pThis->fForcedDirMode             = 0;

        //pThis->cCommonSources             = 0;
        //pThis->paCommonSources            = NULL;

        //pThis->pBootCatFile               = NULL;

        pThis->cbFinalizedImage             = UINT64_MAX;
        //pThis->pbSysArea                  = NULL;
        //pThis->cbSysArea                  = 0;
        //pThis->pbVolDescs                 = NULL;
        //pThis->pPrimaryVolDesc            = NULL;
        //pThis->pElToritoDesc              = NULL;
        //pThis->pJolietVolDesc             = NULL;

        RTListInit(&pThis->FinalizedFiles);

        RTTimeNow(&pThis->ImageCreationTime);

        /*
         * Add the root directory node with idObj == 0.
         */
        PRTFSISOMAKERDIR pDirRoot;
        rc = rtFsIsoMakerAddUnnamedDirWorker(pThis, NULL /*pObjInfo*/, &pDirRoot);
        if (RT_SUCCESS(rc))
        {
            *phIsoMaker = pThis;
            return VINF_SUCCESS;
        }

        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Frees an object.
 *
 * This is a worker for rtFsIsoMakerDestroy and RTFsIsoMakerObjRemove.
 *
 * @param   pObj                The object to free.
 */
DECLINLINE(void) rtFsIsoMakerObjDestroy(PRTFSISOMAKEROBJ pObj)
{
    if (pObj->enmType == RTFSISOMAKEROBJTYPE_FILE)
    {
        PRTFSISOMAKERFILE pFile = (PRTFSISOMAKERFILE)pObj;
        switch (pFile->enmSrcType)
        {
            case RTFSISOMAKERSRCTYPE_PATH:
                pFile->u.pszSrcPath = NULL;
                break;

            case RTFSISOMAKERSRCTYPE_TRANS_TBL:
                pFile->u.pTransTblDir = NULL;
                break;

            case RTFSISOMAKERSRCTYPE_VFS_FILE:
                RTVfsFileRelease(pFile->u.hVfsFile);
                pFile->u.hVfsFile = NIL_RTVFSFILE;
                break;

            case RTFSISOMAKERSRCTYPE_COMMON:
            case RTFSISOMAKERSRCTYPE_RR_SPILL:
                break;

            case RTFSISOMAKERSRCTYPE_INVALID:
            case RTFSISOMAKERSRCTYPE_END:
                AssertFailed();
                break;

            /* no default, want warnings */
        }
        if (pFile->pBootInfoTable)
        {
            RTMemFree(pFile->pBootInfoTable);
            pFile->pBootInfoTable = NULL;
        }
    }

    RTMemFree(pObj);
}


/**
 * Frees a namespace node.
 *
 * This is a worker for rtFsIsoMakerDestroyTree and rtFsIsoMakerObjUnsetName.
 *
 * @param   pName               The node to free.
 */
DECLINLINE(void) rtFsIsoMakerDestroyName(PRTFSISOMAKERNAME pName)
{
    if (pName->fRockRidgeNmAlloced)
    {
        RTMemFree(pName->pszRockRidgeNm);
        pName->pszRockRidgeNm = NULL;
    }
    if (pName->fTransNmAlloced)
    {
        RTMemFree(pName->pszTransNm);
        pName->pszTransNm = NULL;
    }
    PRTFSISOMAKERNAMEDIR pDir = pName->pDir;
    if (pDir != NULL)
    {
        Assert(pDir->cChildren == 0);
        RTMemFree(pDir->papChildren);
        pDir->papChildren = NULL;
        RTMemFree(pDir);
        pName->pDir = NULL;
    }
    RTMemFree(pName);
}


/**
 * Destroys a namespace.
 *
 * @param   pNamespace          The namespace to destroy.
 */
static void rtFsIsoMakerDestroyTree(PRTFSISOMAKERNAMESPACE pNamespace)
{
    /*
     * Recursively destroy the tree first.
     */
    PRTFSISOMAKERNAME pCur = pNamespace->pRoot;
    if (pCur)
    {
        Assert(!pCur->pParent);
        for (;;)
        {
            if (   pCur->pDir
                && pCur->pDir->cChildren)
                pCur = pCur->pDir->papChildren[pCur->pDir->cChildren - 1];
            else
            {
                PRTFSISOMAKERNAME pNext = pCur->pParent;
                rtFsIsoMakerDestroyName(pCur);

                /* Unlink from parent, we're the last entry. */
                if (pNext)
                {
                    Assert(pNext->pDir->cChildren > 0);
                    pNext->pDir->cChildren--;
                    Assert(pNext->pDir->papChildren[pNext->pDir->cChildren] == pCur);
                    pNext->pDir->papChildren[pNext->pDir->cChildren] = NULL;
                    pCur = pNext;
                }
                else
                {
                    Assert(pNamespace->pRoot == pCur);
                    break;
                }
            }
        }
        pNamespace->pRoot = NULL;
    }

    /*
     * Free the translation table filename if allocated.
     */
    if (pNamespace->pszTransTbl)
    {
        if (pNamespace->pszTransTbl != g_szTransTbl)
            RTStrFree(pNamespace->pszTransTbl);
        pNamespace->pszTransTbl = NULL;
    }

    /*
     * Free string IDs.
     */
    if (pNamespace->pszSystemId)
    {
        if (pNamespace->pszSystemId != g_szSystemId)
            RTStrFree(pNamespace->pszSystemId);
        pNamespace->pszSystemId = NULL;
    }

    if (pNamespace->pszVolumeId)
    {
        RTStrFree(pNamespace->pszVolumeId);
        pNamespace->pszVolumeId = NULL;
    }

    if (pNamespace->pszVolumeSetId)
    {
        RTStrFree(pNamespace->pszVolumeSetId);
        pNamespace->pszVolumeSetId = NULL;
    }

    if (pNamespace->pszPublisherId)
    {
        RTStrFree(pNamespace->pszPublisherId);
        pNamespace->pszPublisherId = NULL;
    }

    if (pNamespace->pszDataPreparerId)
    {
        RTStrFree(pNamespace->pszDataPreparerId);
        pNamespace->pszDataPreparerId = NULL;
    }

    if (pNamespace->pszApplicationId)
    {
        if (   pNamespace->pszApplicationId != g_szAppIdPrimaryIso
            && pNamespace->pszApplicationId != g_szAppIdJoliet)
            RTStrFree(pNamespace->pszApplicationId);
        pNamespace->pszApplicationId = NULL;
    }

    if (pNamespace->pszCopyrightFileId)
    {
        RTStrFree(pNamespace->pszCopyrightFileId);
        pNamespace->pszCopyrightFileId = NULL;
    }

    if (pNamespace->pszAbstractFileId)
    {
        RTStrFree(pNamespace->pszAbstractFileId);
        pNamespace->pszAbstractFileId = NULL;
    }

    if (pNamespace->pszBibliographicFileId)
    {
        RTStrFree(pNamespace->pszBibliographicFileId);
        pNamespace->pszBibliographicFileId = NULL;
    }
}


/**
 * Destroys an ISO maker instance.
 *
 * @param   pThis               The ISO maker instance to destroy.
 */
static void rtFsIsoMakerDestroy(PRTFSISOMAKERINT pThis)
{
    rtFsIsoMakerDestroyTree(&pThis->PrimaryIso);
    rtFsIsoMakerDestroyTree(&pThis->Joliet);
    rtFsIsoMakerDestroyTree(&pThis->Udf);
    rtFsIsoMakerDestroyTree(&pThis->Hfs);

    PRTFSISOMAKEROBJ pCur;
    PRTFSISOMAKEROBJ pNext;
    RTListForEachSafe(&pThis->ObjectHead, pCur, pNext, RTFSISOMAKEROBJ, Entry)
    {
        RTListNodeRemove(&pCur->Entry);
        rtFsIsoMakerObjDestroy(pCur);
    }

    if (pThis->paCommonSources)
    {
        for (uint32_t i = 0; i < pThis->cCommonSources; i++)
        {
            RTVfsFileRelease(pThis->paCommonSources[i]);
            pThis->paCommonSources[i] = NIL_RTVFSFILE;
        }
        RTMemFree(pThis->paCommonSources);
        pThis->paCommonSources = NULL;
    }

    if (pThis->pbVolDescs)
    {
        RTMemFree(pThis->pbVolDescs);
        pThis->pbVolDescs = NULL;
    }

    if (pThis->pbSysArea)
    {
        RTMemFree(pThis->pbSysArea);
        pThis->pbSysArea = NULL;
    }

    if (pThis->papUdfIcbToObjs)
    {
        RTMemFree(pThis->papUdfIcbToObjs);
        pThis->papUdfIcbToObjs = NULL;
    }

    pThis->uMagic = ~RTFSISOMAKERINT_MAGIC;
    RTMemFree(pThis);
}


/**
 * Retains a references to an ISO maker instance.
 *
 * @returns New reference count on success, UINT32_MAX if invalid handle.
 * @param   hIsoMaker           The ISO maker handle.
 */
RTDECL(uint32_t) RTFsIsoMakerRetain(RTFSISOMAKER hIsoMaker)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->uMagic == RTFSISOMAKERINT_MAGIC, UINT32_MAX);
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs > 1);
    Assert(cRefs < _64K);
    return cRefs;
}


/**
 * Releases a references to an ISO maker instance.
 *
 * @returns New reference count on success, UINT32_MAX if invalid handle.
 * @param   hIsoMaker           The ISO maker handle.  NIL is ignored.
 */
RTDECL(uint32_t) RTFsIsoMakerRelease(RTFSISOMAKER hIsoMaker)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    uint32_t         cRefs;
    if (pThis == NIL_RTFSISOMAKER)
        cRefs = 0;
    else
    {
        AssertPtrReturn(pThis, UINT32_MAX);
        AssertReturn(pThis->uMagic == RTFSISOMAKERINT_MAGIC, UINT32_MAX);
        cRefs = ASMAtomicDecU32(&pThis->cRefs);
        Assert(cRefs < _64K);
        if (!cRefs)
            rtFsIsoMakerDestroy(pThis);
    }
    return cRefs;
}


/**
 * Sets the ISO-9660 level.
 *
 * @returns IPRT status code
 * @param   hIsoMaker           The ISO maker handle.
 * @param   uIsoLevel           The level, 1-3.
 */
RTDECL(int) RTFsIsoMakerSetIso9660Level(RTFSISOMAKER hIsoMaker, uint8_t uIsoLevel)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(uIsoLevel <= 3, VERR_INVALID_PARAMETER);
    AssertReturn(uIsoLevel > 0, VERR_INVALID_PARAMETER); /* currently not possible to disable this */
    AssertReturn(!pThis->fSeenContent, VERR_WRONG_ORDER);

    pThis->PrimaryIso.uLevel = uIsoLevel;
    return VINF_SUCCESS;
}


/**
 * Gets the ISO-9660 level.
 *
 * @returns The level, UINT8_MAX if invalid handle.
 * @param   hIsoMaker           The ISO maker handle.
 */
RTDECL(uint8_t) RTFsIsoMakerGetIso9660Level(RTFSISOMAKER hIsoMaker)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET_EX(pThis, UINT8_MAX);
    return pThis->PrimaryIso.uLevel;
}


/**
 * Sets the joliet level.
 *
 * @returns IPRT status code
 * @param   hIsoMaker           The ISO maker handle.
 * @param   uJolietLevel        The joliet UCS-2 level 1-3, or 0 to disable
 *                              joliet.
 */
RTDECL(int) RTFsIsoMakerSetJolietUcs2Level(RTFSISOMAKER hIsoMaker, uint8_t uJolietLevel)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(uJolietLevel <= 3, VERR_INVALID_PARAMETER);
    AssertReturn(!pThis->fSeenContent, VERR_WRONG_ORDER);

    if (pThis->Joliet.uLevel != uJolietLevel)
    {
        if (uJolietLevel == 0)
            pThis->cVolumeDescriptors--;
        else if (pThis->Joliet.uLevel == 0)
            pThis->cVolumeDescriptors++;
        pThis->Joliet.uLevel = uJolietLevel;
    }
    return VINF_SUCCESS;
}


/**
 * Sets the rock ridge support level (on the primary ISO-9660 namespace).
 *
 * @returns IPRT status code
 * @param   hIsoMaker           The ISO maker handle.
 * @param   uLevel              0 if disabled, 1 to just enable, 2 to enable and
 *                              write the ER tag.
 */
RTDECL(int) RTFsIsoMakerSetRockRidgeLevel(RTFSISOMAKER hIsoMaker, uint8_t uLevel)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(uLevel <= 2, VERR_INVALID_PARAMETER);
    AssertReturn(   !pThis->fSeenContent
                 || (uLevel >= pThis->PrimaryIso.uRockRidgeLevel && pThis->PrimaryIso.uRockRidgeLevel > 0), VERR_WRONG_ORDER);
    AssertReturn(!pThis->fSeenContent, VERR_WRONG_ORDER);

    pThis->PrimaryIso.uRockRidgeLevel = uLevel;
    return VINF_SUCCESS;
}


/**
 * Sets the rock ridge support level on the joliet namespace (experimental).
 *
 * @returns IPRT status code
 * @param   hIsoMaker           The ISO maker handle.
 * @param   uLevel              0 if disabled, 1 to just enable, 2 to enable and
 *                              write the ER tag.
 */
RTDECL(int) RTFsIsoMakerSetJolietRockRidgeLevel(RTFSISOMAKER hIsoMaker, uint8_t uLevel)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(uLevel <= 2, VERR_INVALID_PARAMETER);
    AssertReturn(   !pThis->fSeenContent
                 || (uLevel >= pThis->Joliet.uRockRidgeLevel && pThis->Joliet.uRockRidgeLevel > 0), VERR_WRONG_ORDER);

    pThis->Joliet.uRockRidgeLevel = uLevel;
    return VINF_SUCCESS;
}


/**
 * Gets the rock ridge support level (on the primary ISO-9660 namespace).
 *
 * @returns 0 if disabled, 1 just enabled, 2 if enabled with ER tag, and
 *          UINT8_MAX if the handle is invalid.
 * @param   hIsoMaker           The ISO maker handle.
 */
RTDECL(uint8_t) RTFsIsoMakerGetRockRidgeLevel(RTFSISOMAKER hIsoMaker)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET_EX(pThis, UINT8_MAX);
    return pThis->PrimaryIso.uRockRidgeLevel;
}


/**
 * Gets the rock ridge support level on the joliet namespace (experimental).
 *
 * @returns 0 if disabled, 1 just enabled, 2 if enabled with ER tag, and
 *          UINT8_MAX if the handle is invalid.
 * @param   hIsoMaker           The ISO maker handle.
 */
RTDECL(uint8_t) RTFsIsoMakerGetJolietRockRidgeLevel(RTFSISOMAKER hIsoMaker)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET_EX(pThis, UINT8_MAX);
    return pThis->Joliet.uRockRidgeLevel;
}


/**
 * Sets the UDF ECMA standard version/level.
 *
 * @returns IPRT status code
 * @param   hIsoMaker           The ISO maker handle.
 * @param   uEcmaLevel          The ECMA standard version (2 or 3), or 0 to
 *                              disable UDF.
 */
RTDECL(int) RTFsIsoMakerSetUdfLevel(RTFSISOMAKER hIsoMaker, uint8_t uEcmaLevel)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(uEcmaLevel <= 3, VERR_INVALID_PARAMETER);
    AssertReturn(!pThis->fSeenContent, VERR_WRONG_ORDER);

    if (pThis->Udf.uLevel != uEcmaLevel)
    {
        if (uEcmaLevel == 0)
            pThis->cVolumeDescriptors -= 3;
        else if (pThis->Udf.uLevel == 0)
            pThis->cVolumeDescriptors += 3;
        pThis->Udf.uLevel = uEcmaLevel;
    }
    return VINF_SUCCESS;
}


/**
 * Changes the file attribute (mode, owner, group) inherit style (from source).
 *
 * The strict style will use the exact attributes from the source, where as the
 * non-strict (aka rational and default) style will use 0 for the owner and
 * group IDs and normalize the mode bits along the lines of 'chmod a=rX',
 * stripping set-uid/gid bits on files but preserving sticky ones on
 * directories.
 *
 * When disabling strict style, the default dir and file modes will be restored
 * to default values.
 *
 * @returns IRPT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   fStrict             Indicates strict (true) or non-strict (false)
 *                              style.
 */
RTDECL(int) RTFsIsoMakerSetAttribInheritStyle(RTFSISOMAKER hIsoMaker, bool fStrict)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);

    pThis->fStrictAttributeStyle = fStrict;
    if (!fStrict)
    {
        pThis->fDefaultFileMode = 0444 | RTFS_TYPE_FILE      | RTFS_DOS_ARCHIVED  | RTFS_DOS_READONLY;
        pThis->fDefaultDirMode  = 0555 | RTFS_TYPE_DIRECTORY | RTFS_DOS_DIRECTORY | RTFS_DOS_READONLY;
    }

    return VINF_SUCCESS;
}


/**
 * Sets the default file mode settings.
 *
 * @returns IRPT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   fMode               The default file mode.
 */
RTDECL(int) RTFsIsoMakerSetDefaultFileMode(RTFSISOMAKER hIsoMaker, RTFMODE fMode)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    Assert(!(fMode & ~RTFS_UNIX_ALL_PERMS));

    pThis->fDefaultFileMode &= ~RTFS_UNIX_ALL_PERMS;
    pThis->fDefaultFileMode |= fMode & RTFS_UNIX_ALL_PERMS;
    return VINF_SUCCESS;
}


/**
 * Sets the default dir mode settings.
 *
 * @returns IRPT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   fMode               The default dir mode.
 */
RTDECL(int) RTFsIsoMakerSetDefaultDirMode(RTFSISOMAKER hIsoMaker, RTFMODE fMode)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    Assert(!(fMode & ~RTFS_UNIX_ALL_PERMS));

    pThis->fDefaultDirMode &= ~RTFS_UNIX_ALL_PERMS;
    pThis->fDefaultDirMode |= fMode & RTFS_UNIX_ALL_PERMS;
    return VINF_SUCCESS;
}


/**
 * Sets the forced file mode, if @a fForce is true also the default mode is set.
 *
 * @returns IRPT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   fMode               The file mode.
 * @param   fForce              Indicate whether forced mode is active or not.
 */
RTDECL(int) RTFsIsoMakerSetForcedFileMode(RTFSISOMAKER hIsoMaker, RTFMODE fMode, bool fForce)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    Assert(!(fMode & ~RTFS_UNIX_ALL_PERMS));

    pThis->fForcedFileMode       = fMode & RTFS_UNIX_ALL_PERMS;
    pThis->fForcedFileModeActive = fForce;
    if (fForce)
    {
        pThis->fDefaultFileMode &= ~RTFS_UNIX_ALL_PERMS;
        pThis->fDefaultFileMode |= fMode & RTFS_UNIX_ALL_PERMS;
    }
    return VINF_SUCCESS;
}


/**
 * Sets the forced dir mode, if @a fForce is true also the default mode is set.
 *
 * @returns IRPT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   fMode               The dir mode.
 * @param   fForce              Indicate whether forced mode is active or not.
 */
RTDECL(int) RTFsIsoMakerSetForcedDirMode(RTFSISOMAKER hIsoMaker, RTFMODE fMode, bool fForce)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    Assert(!(fMode & ~RTFS_UNIX_ALL_PERMS));

    pThis->fForcedDirModeActive  = fForce;
    pThis->fForcedDirMode        = fMode & RTFS_UNIX_ALL_PERMS;
    if (fForce)
    {
        pThis->fDefaultDirMode  &= ~RTFS_UNIX_ALL_PERMS;
        pThis->fDefaultDirMode  |= fMode & RTFS_UNIX_ALL_PERMS;
    }
    return VINF_SUCCESS;
}


/**
 * Sets the content of the system area, i.e. the first 32KB of the image.
 *
 * This can be used to put generic boot related stuff.
 *
 * @note    Other settings may overwrite parts of the content (yet to be
 *          determined which).
 *
 * @returns IPRT status code
 * @param   hIsoMaker           The ISO maker handle.
 * @param   pvContent           The content to put in the system area.
 * @param   cbContent           The size of the content.
 * @param   off                 The offset into the system area.
 */
RTDECL(int) RTFsIsoMakerSetSysAreaContent(RTFSISOMAKER hIsoMaker, void const *pvContent, size_t cbContent, uint32_t off)
{
    /*
     * Validate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);
    AssertReturn(cbContent > 0, VERR_OUT_OF_RANGE);
    AssertReturn(cbContent <= _32K, VERR_OUT_OF_RANGE);
    AssertReturn(off < _32K, VERR_OUT_OF_RANGE);
    size_t cbSysArea = off + cbContent;
    AssertReturn(cbSysArea <= _32K, VERR_OUT_OF_RANGE);

    /*
     * Adjust the allocation and copy over the new/additional content.
     */
    if (pThis->cbSysArea < cbSysArea)
    {
        void *pvNew = RTMemRealloc(pThis->pbSysArea, cbSysArea);
        AssertReturn(pvNew, VERR_NO_MEMORY);
        pThis->pbSysArea = (uint8_t *)pvNew;
        memset(&pThis->pbSysArea[pThis->cbSysArea], 0, cbSysArea - pThis->cbSysArea);
    }

    memcpy(&pThis->pbSysArea[off], pvContent, cbContent);

    return VINF_SUCCESS;
}


/**
 * Sets a string property in one or more namespaces.
 *
 * @returns IPRT status code.
 * @param   hIsoMaker       The ISO maker handle.
 * @param   enmStringProp   The string property to set.
 * @param   fNamespaces     The namespaces to set it in.
 * @param   pszValue        The value to set it to.  NULL is treated like an
 *                          empty string.  The value will be silently truncated
 *                          to fit the available space.
 */
RTDECL(int) RTFsIsoMakerSetStringProp(RTFSISOMAKER hIsoMaker, RTFSISOMAKERSTRINGPROP enmStringProp,
                                      uint32_t fNamespaces, const char *pszValue)
{
    /*
     * Validate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(    enmStringProp > RTFSISOMAKERSTRINGPROP_INVALID
                  && enmStringProp < RTFSISOMAKERSTRINGPROP_END, VERR_INVALID_PARAMETER);
    AssertReturn(!(fNamespaces & ~RTFSISOMAKER_NAMESPACE_VALID_MASK), VERR_INVALID_FLAGS);
    if (pszValue)
    {
        AssertPtrReturn(pszValue, VERR_INVALID_POINTER);
        if (*pszValue == '\0')
            pszValue = NULL;
    }
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    /*
     * Work the namespaces.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aRTFsIsoNamespaces); i++)
        if (fNamespaces & g_aRTFsIsoNamespaces[i].fNamespace)
        {
            PRTFSISOMAKERNAMESPACE pNamespace = (PRTFSISOMAKERNAMESPACE)((uintptr_t)pThis + g_aRTFsIsoNamespaces[i].offNamespace);
            if (pNamespace->uLevel > 0)
            {
                /* Get a pointer to the field. */
                char **ppszValue;
                switch (enmStringProp)
                {
                    case RTFSISOMAKERSTRINGPROP_SYSTEM_ID:              ppszValue = &pNamespace->pszSystemId; break;
                    case RTFSISOMAKERSTRINGPROP_VOLUME_ID:              ppszValue = &pNamespace->pszVolumeId; break;
                    case RTFSISOMAKERSTRINGPROP_VOLUME_SET_ID:          ppszValue = &pNamespace->pszVolumeSetId; break;
                    case RTFSISOMAKERSTRINGPROP_PUBLISHER_ID:           ppszValue = &pNamespace->pszPublisherId; break;
                    case RTFSISOMAKERSTRINGPROP_DATA_PREPARER_ID:       ppszValue = &pNamespace->pszDataPreparerId; break;
                    case RTFSISOMAKERSTRINGPROP_APPLICATION_ID:         ppszValue = &pNamespace->pszApplicationId; break;
                    case RTFSISOMAKERSTRINGPROP_COPYRIGHT_FILE_ID:      ppszValue = &pNamespace->pszCopyrightFileId; break;
                    case RTFSISOMAKERSTRINGPROP_ABSTRACT_FILE_ID:       ppszValue = &pNamespace->pszAbstractFileId; break;
                    case RTFSISOMAKERSTRINGPROP_BIBLIOGRAPHIC_FILE_ID:  ppszValue = &pNamespace->pszBibliographicFileId; break;
                    default:                                            AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
                }

                /* Free the old value. */
                char *pszOld = *ppszValue;
                if (   pszOld
                    && pszOld != g_szAppIdPrimaryIso
                    && pszOld != g_szAppIdJoliet
                    && pszOld != g_szSystemId)
                    RTStrFree(pszOld);

                /* Set the new value. */
                if (!pszValue)
                    *ppszValue = NULL;
                else
                {
                    *ppszValue = RTStrDup(pszValue);
                    AssertReturn(*ppszValue, VERR_NO_STR_MEMORY);
                }
            }
        }
    return VINF_SUCCESS;
}


/**
 * Specifies image padding.
 *
 * @returns IPRT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   cSectors            Number of sectors to pad the image with.
 */
RTDECL(int) RTFsIsoMakerSetImagePadding(RTFSISOMAKER hIsoMaker, uint32_t cSectors)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(cSectors <= _64K, VERR_OUT_OF_RANGE);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    pThis->cbImagePadding = cSectors * RTFSISOMAKER_SECTOR_SIZE;
    return VINF_SUCCESS;
}





/*
 *
 * Name space related internals.
 * Name space related internals.
 * Name space related internals.
 *
 */


/**
 * Gets the pointer to the name member for the given namespace.
 *
 * @returns Pointer to name member.
 * @param   pObj                The object to find a name member in.
 * @param   pNamespace          The namespace which name to calculate.
 */
DECLINLINE(PPRTFSISOMAKERNAME) rtFsIsoMakerObjGetNameForNamespace(PRTFSISOMAKEROBJ pObj, PCRTFSISOMAKERNAMESPACE pNamespace)
{
    return (PPRTFSISOMAKERNAME)((uintptr_t)pObj + pNamespace->offName);
}


/**
 * Locates a child object by its namespace name.
 *
 * @returns Pointer to the child if found, NULL if not.
 * @param   pDirObj             The directory object to search.
 * @param   pszEntry            The (namespace) entry name.
 * @param   cchEntry            The length of the name.
 */
static PRTFSISOMAKERNAME rtFsIsoMakerFindObjInDir(PRTFSISOMAKERNAME pDirObj, const char *pszEntry, size_t cchEntry)
{
    if (pDirObj)
    {
        PRTFSISOMAKERNAMEDIR pDir = pDirObj->pDir;
        AssertReturn(pDir, NULL);

        uint32_t i = pDir->cChildren;
        while (i-- > 0)
        {
            PRTFSISOMAKERNAME pChild = pDir->papChildren[i];
            if (   pChild->cchName == cchEntry
                && RTStrNICmp(pChild->szName, pszEntry, cchEntry) == 0)
                return pChild;
        }
    }
    return NULL;
}


/**
 * Compares the two names according to ISO-9660 directory sorting rules.
 *
 * As long as we don't want to do case insensitive joliet sorting, this works
 * for joliet names to, I think.
 *
 * @returns 0 if equal, -1 if pszName1 comes first, 1 if pszName2 comes first.
 * @param   pszName1            The first name.
 * @param   pszName2            The second name.
 */
DECLINLINE(int) rtFsIsoMakerCompareIso9660Names(const char *pszName1, const char *pszName2)
{
    for (;;)
    {
        char const ch1 = *pszName1++;
        char const ch2 = *pszName2++;
        if (ch1 == ch2)
        {
            if (ch1)
            { /* likely */ }
            else
                return 0;
        }
        else if (ch1 == ';' || ch2 == ';')
            return ch1 == ';' ? -1 : 1;
        else if (ch1 == '.' || ch2 == '.')
            return ch1 == '.' ? -1 : 1;
        else
            return (unsigned char)ch1 < (unsigned char)ch2 ? -1 : 1;
    }
}


/**
 * Finds the index into papChildren where the given name should be inserted.
 *
 * @returns Index of the given name.
 * @param   pNamespace          The namspace.
 * @param   pDir                The parent directory.
 * @param   pszName             The name.
 */
static uint32_t
rtFsIsoMakerFindInsertIndex(PRTFSISOMAKERNAMESPACE pNamespace, PRTFSISOMAKERNAMEDIR pDir, const char *pszName)
{
    uint32_t idxRet = pDir->cChildren;
    if (idxRet > 0)
    {
        /*
         * The idea is to do binary search using a namespace specific compare
         * function.  However, it looks like we can get away with using the
         * same compare function for all namespaces.
         */
        uint32_t            idxStart = 0;
        uint32_t            idxEnd   = idxRet;
        PPRTFSISOMAKERNAME  papChildren = pDir->papChildren;
        switch (pNamespace->fNamespace)
        {
            case RTFSISOMAKER_NAMESPACE_ISO_9660:
            case RTFSISOMAKER_NAMESPACE_JOLIET:
            case RTFSISOMAKER_NAMESPACE_UDF: /* (UDF is officially unsorted, see @udf260{2.3.5.4,55}. */
            case RTFSISOMAKER_NAMESPACE_HFS:
                for (;;)
                {
                    idxRet = idxStart + (idxEnd - idxStart) / 2;
                    PRTFSISOMAKERNAME pCur = papChildren[idxRet];
                    int iDiff = rtFsIsoMakerCompareIso9660Names(pszName, pCur->szName);
                    if (iDiff < 0)
                    {
                        if (idxRet > idxStart)
                            idxEnd = idxRet;
                        else
                            break;
                    }
                    else
                    {
                        idxRet++;
                        if (   iDiff != 0
                            && idxRet < idxEnd)
                            idxStart = idxRet;
                        else
                            break;
                    }
                }
                break;

            default:
                AssertFailed();
                break;
        }
    }
    return idxRet;
}



/**
 * Locates a child entry by its specified name.
 *
 * @returns Pointer to the child if found, NULL if not.
 * @param   pDirName            The directory name to search.
 * @param   pszEntry            The (specified) entry name.
 * @param   cchEntry            The length of the name.
 */
static PRTFSISOMAKERNAME rtFsIsoMakerFindEntryInDirBySpec(PRTFSISOMAKERNAME pDirName, const char *pszEntry, size_t cchEntry)
{
    if (pDirName)
    {
        PRTFSISOMAKERNAMEDIR pDir = pDirName->pDir;
        AssertReturn(pDir, NULL);

        uint32_t i = pDir->cChildren;
        while (i-- > 0)
        {
            PRTFSISOMAKERNAME pChild = pDir->papChildren[i];
            if (   pChild->cchSpecNm == cchEntry
                && RTStrNICmp(pChild->pszSpecNm, pszEntry, cchEntry) == 0)
                return pChild;
        }
    }
    return NULL;
}


/**
 * Locates a subdir object in any namespace by its specified name.
 *
 * This is used to avoid having one instance of RTFSISOMAKERDIR in each
 * namespace for the same directory.
 *
 * @returns Pointer to the subdir object if found, NULL if not.
 * @param   pDirObj             The directory object to search.
 * @param   pszEntry            The (specified) entry name.
 * @param   cchEntry            The length of the name.
 * @param   fSkipNamespaces     Namespaces to skip.
 * @sa      rtFsIsoMakerFindEntryInDirBySpec
 */
static PRTFSISOMAKERDIR rtFsIsoMakerFindSubdirBySpec(PRTFSISOMAKERDIR pDirObj, const char *pszEntry, size_t cchEntry,
                                                     uint32_t fSkipNamespaces)
{
    AssertReturn(pDirObj, NULL);
    AssertReturn(pDirObj->Core.enmType == RTFSISOMAKEROBJTYPE_DIR, NULL);
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aRTFsIsoNamespaces); i++)
        if (!(fSkipNamespaces & g_aRTFsIsoNamespaces[i].fNamespace))
        {
            PRTFSISOMAKERNAME pDirName = *(PPRTFSISOMAKERNAME)((uintptr_t)pDirObj + g_aRTFsIsoNamespaces[i].offName);
            if (pDirName)
            {
                PRTFSISOMAKERNAMEDIR pDir = pDirName->pDir;
                AssertStmt(pDir, continue);

                uint32_t iChild = pDir->cChildren;
                while (iChild-- > 0)
                {
                    PRTFSISOMAKERNAME pChild = pDir->papChildren[iChild];
                    if (   pChild->cchSpecNm == cchEntry
                        && pChild->pDir      != NULL
                        && RTStrNICmp(pChild->pszSpecNm, pszEntry, cchEntry) == 0)
                        return (PRTFSISOMAKERDIR)pChild->pObj;
                }
            }
        }
    return NULL;
}


/**
 * Walks the given path by specified object names in a namespace.
 *
 * @returns IPRT status code.
 * @param   pNamespace  The namespace to walk the path in.
 * @param   pszPath     The path to walk.
 * @param   ppName      Where to return the name node that the path ends with.
 */
static int rtFsIsoMakerWalkPathBySpec(PRTFSISOMAKERNAMESPACE pNamespace, const char *pszPath, PPRTFSISOMAKERNAME ppName)
{
    *ppName = NULL;
    AssertReturn(RTPATH_IS_SLASH(*pszPath), VERR_INVALID_NAME);

    /*
     * Deal with the special case of the root.
     */
    while (RTPATH_IS_SLASH(*pszPath))
        pszPath++;

    PRTFSISOMAKERNAME pCur = pNamespace->pRoot;
    if (!pCur)
        return *pszPath ? VERR_PATH_NOT_FOUND : VERR_FILE_NOT_FOUND;
    if (!*pszPath)
    {
        *ppName = pCur;
        return VINF_SUCCESS;
    }

    /*
     * Now, do the rest of the path.
     */
    for (;;)
    {
        /*
         * Find the end of the component.
         */
        char ch;
        size_t cchComponent = 0;
        while ((ch = pszPath[cchComponent]) != '\0' && !RTPATH_IS_SLASH(ch))
            cchComponent++;
        if (!cchComponent)
        {
            *ppName = pCur;
            return VINF_SUCCESS;
        }

        size_t offNext = cchComponent;
        while (RTPATH_IS_SLASH(ch))
            ch = pszPath[++offNext];

        /*
         * Deal with dot and dot-dot.
         */
        if (cchComponent == 1 && pszPath[0] == '.')
        { /* nothing to do */ }
        else if (cchComponent == 2 && pszPath[0] == '.' && pszPath[1] == '.')
        {
            if (pCur->pParent)
                pCur = pCur->pParent;
        }
        /*
         * Look up the name.
         */
        else
        {
            PRTFSISOMAKERNAME pChild = rtFsIsoMakerFindEntryInDirBySpec(pCur, pszPath, cchComponent);
            if (!pChild)
                return pszPath[offNext] ? VERR_PATH_NOT_FOUND : VERR_FILE_NOT_FOUND;
            if (   (offNext > cchComponent)
                && !pChild->pDir)
                return VERR_NOT_A_DIRECTORY;
            pCur = pChild;
        }

        /*
         * Skip ahead in the path.
         */
        pszPath += offNext;
    }
}


/**
 * Copy and convert a name to valid ISO-9660 (d-characters only).
 *
 * Worker for rtFsIsoMakerNormalizeNameForNamespace.  ASSUMES it deals with
 * dots.
 *
 * @returns Length of the resulting string.
 * @param   pszDst      The output buffer.
 * @param   cchDstMax   The maximum number of (d-chars) to put in the output
 *                      buffer.
 * @param   pchSrc      The UTF-8 source string (not neccessarily terminated).
 * @param   cchSrc      The maximum number of chars to copy from the source
 *                      string.
 */
static size_t rtFsIsoMakerCopyIso9660Name(char *pszDst, size_t cchDstMax, const char *pchSrc, size_t cchSrc)
{
    const char *pchSrcIn = pchSrc;
    size_t      offDst = 0;
    while ((size_t)(pchSrc - pchSrcIn) < cchSrc)
    {
        RTUNICP uc;
        int rc = RTStrGetCpEx(&pchSrc, &uc);
        if (RT_SUCCESS(rc))
        {
            if (   uc < 128
                && RTFSISOMAKER_IS_UPPER_IN_D_CHARS((char)uc))
            {
                pszDst[offDst++] = RT_C_TO_UPPER((char)uc);
                if (offDst >= cchDstMax)
                    break;
            }
        }
    }
    pszDst[offDst] = '\0';
    return offDst;
}


/**
 * Normalizes a name for the primary ISO-9660 namespace.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO maker instance.
 * @param   pParent         The parent directory.  NULL if root.
 * @param   pchSrc          The specified name to normalize (not necessarily zero
 *                          terminated).
 * @param   cchSrc          The length of the specified name.
 * @param   fNoNormalize    Don't normalize the name very strictly (imported or
 *                          such).
 * @param   fIsDir          Indicates whether it's a directory or file (like).
 * @param   pszDst          The output buffer.  Must be at least 32 bytes.
 * @param   cbDst           The size of the output buffer.
 * @param   pcchDst         Where to return the length of the returned string (i.e.
 *                          not counting the terminator).
 * @param   pcbInDirRec     Where to return the name size in the directory record.
 */
static int rtFsIsoMakerNormalizeNameForPrimaryIso9660(PRTFSISOMAKERINT pThis, PRTFSISOMAKERNAME pParent, const char *pchSrc,
                                                      size_t cchSrc, bool fNoNormalize, bool fIsDir,
                                                      char *pszDst, size_t cbDst, size_t *pcchDst, size_t *pcbInDirRec)
{
    AssertReturn(cbDst > ISO9660_MAX_NAME_LEN + 2, VERR_ISOMK_IPE_BUFFER_SIZE);

    /* Skip leading dots. */
    while (cchSrc > 0 && *pchSrc == '.')
        pchSrc++, cchSrc--;
    if (!cchSrc)
    {
        pchSrc = "DOTS";
        cchSrc = 4;
    }

    /*
     * Produce a first name.
     */
    uint8_t const uIsoLevel = !fNoNormalize ? pThis->PrimaryIso.uLevel : RT_MAX(pThis->PrimaryIso.uLevel, 3);
    size_t cchDst;
    size_t offDstDot;
    if (fIsDir && !fNoNormalize)
        offDstDot = cchDst = rtFsIsoMakerCopyIso9660Name(pszDst, uIsoLevel >= 2 ? ISO9660_MAX_NAME_LEN : 8,
                                                         pchSrc, cchSrc);
    else
    {
        /* Look for the last dot and try preserve the extension when doing the conversion. */
        size_t offLastDot = cchSrc;
        for (size_t off = 0; off < cchSrc; off++)
            if (pchSrc[off] == '.')
                offLastDot = off;

        if (fNoNormalize)
        {
            /* Try preserve the imported name, though, put the foot down if too long. */
            offDstDot = offLastDot;
            cchDst    = cchSrc;
            if (cchSrc > ISO9660_MAX_NAME_LEN)
            {
                cchDst = ISO9660_MAX_NAME_LEN;
                if (offDstDot > cchDst)
                    offDstDot = cchDst;
            }
            memcpy(pszDst, pchSrc, cchDst);
            pszDst[cchDst] = '\0';
        }
        else if (offLastDot == cchSrc)
            offDstDot = cchDst = rtFsIsoMakerCopyIso9660Name(pszDst, uIsoLevel >= 2 ? ISO9660_MAX_NAME_LEN : 8,
                                                             pchSrc, cchSrc);
        else
        {
            const char * const pchSrcExt = &pchSrc[offLastDot + 1];
            size_t       const cchSrcExt = cchSrc - offLastDot - 1;
            if (uIsoLevel < 2)
            {
                cchDst = rtFsIsoMakerCopyIso9660Name(pszDst, 8, pchSrc, cchSrc);
                offDstDot = cchDst;
                pszDst[cchDst++] = '.';
                cchDst += rtFsIsoMakerCopyIso9660Name(&pszDst[cchDst], 3, pchSrcExt, cchSrcExt);
            }
            else
            {
                size_t cchDstExt = rtFsIsoMakerCopyIso9660Name(pszDst, ISO9660_MAX_NAME_LEN - 2, pchSrcExt, cchSrcExt);
                if (cchDstExt > 0)
                {
                    size_t cchBasename = rtFsIsoMakerCopyIso9660Name(pszDst, ISO9660_MAX_NAME_LEN - 2,
                                                                     pchSrc, offLastDot);
                    if (cchBasename + 1 + RT_MIN(cchDstExt, 3) + (fIsDir ? 0 : 2) <= ISO9660_MAX_NAME_LEN)
                        cchDst = cchBasename;
                    else
                        cchDst = ISO9660_MAX_NAME_LEN - 1 - RT_MIN(cchDstExt, 3) - (fIsDir ? 0 : 2);
                    offDstDot = cchDst;
                    pszDst[cchDst++] = '.';
                    cchDst += rtFsIsoMakerCopyIso9660Name(&pszDst[cchDst], ISO9660_MAX_NAME_LEN - 1 - cchDst,
                                                          pchSrcExt, cchSrcExt);
                }
                else
                    offDstDot = cchDst = rtFsIsoMakerCopyIso9660Name(pszDst, ISO9660_MAX_NAME_LEN, pchSrc, cchSrc);
            }
        }
    }

    /* Append version if not directory */
    if (!fIsDir)
    {
        pszDst[cchDst++] = ';';
        pszDst[cchDst++] = '1';
        pszDst[cchDst]   = '\0';
    }

    /*
     * Unique name?
     */
    if (!rtFsIsoMakerFindObjInDir(pParent, pszDst, cchDst))
    {
        *pcchDst     = cchDst;
        *pcbInDirRec = cchDst;
        return VINF_SUCCESS;
    }

    /*
     * Mangle the name till we've got a unique one.
     */
    size_t const cchMaxBasename = (uIsoLevel >= 2 ? ISO9660_MAX_NAME_LEN : 8) - (cchDst - offDstDot);
    size_t       cchInserted = 0;
    for (uint32_t i = 0; i < _32K; i++)
    {
        /* Add a numberic infix. */
        char szOrd[64];
        size_t cchOrd = RTStrFormatU32(szOrd, sizeof(szOrd), i + 1, 10, -1, -1, 0 /*fFlags*/);
        Assert((ssize_t)cchOrd > 0);

        /* Do we need to shuffle the suffix? */
        if (cchOrd > cchInserted)
        {
            if (offDstDot < cchMaxBasename)
            {
                memmove(&pszDst[offDstDot + 1], &pszDst[offDstDot], cchDst + 1 - offDstDot);
                cchDst++;
                offDstDot++;
            }
            cchInserted = cchOrd;
        }

        /* Insert the new infix and try again. */
        memcpy(&pszDst[offDstDot - cchOrd], szOrd, cchOrd);
        if (!rtFsIsoMakerFindObjInDir(pParent, pszDst, cchDst))
        {
            *pcchDst     = cchDst;
            *pcbInDirRec = cchDst;
            return VINF_SUCCESS;
        }
    }
    AssertFailed();
    return VERR_DUPLICATE;
}


/**
 * Normalizes a name for the UDF namespace.
 *
 * @returns IPRT status code.
 * @param   pParent         The parent directory.  NULL if root.
 * @param   pchSrc          The specified name to normalize (not necessarily zero
 *                          terminated).
 * @param   cchSrc          The length of the specified name.
 * @param   pszDst          The output buffer.  Must be at least 32 bytes.
 * @param   cbDst           The size of the output buffer.
 * @param   pcchDst         Where to return the length of the returned string (i.e.
 *                          not counting the terminator).
 * @param   pcbInDirRec     Where to return the name size in the directory record.
 */
static int rtFsIsoMakerNormalizeNameForUdf(PRTFSISOMAKERNAME pParent, const char *pchSrc, size_t cchSrc,
                                           char *pszDst, size_t cbDst, size_t *pcchDst, size_t *pcbInDirRec)
{
    /*
     * The max unterminated length is 255 for 8-bit and 127 utf-16 codepoints for 16-bit.
     * In the latter case, this means U+10FFFF as UTF-8, which requires 4 bytes.
     */
    AssertReturn(cbDst > 255 && cbDst > 127 * 4, VERR_ISOMK_IPE_BUFFER_SIZE);

    /*
     * Decode codepoint by codepoint and decide upon the final encoding length.
     */
    bool         fModified   = false;
    char * const pszDstStart = pszDst;
    size_t       cUnits      = 0;
    size_t       cMaxUnits   = 254; /* (255 minus encoding byte prefix.) */
    for (;;)
    {
        RTUNICP uc;
        int rc = RTStrGetCpNEx(&pchSrc, &cchSrc, &uc);
        if (RT_SUCCESS(rc))
        {
            if (uc <= 255)
            {
                if (uc != 0)
                {
                    if (cUnits < cMaxUnits)
                        cUnits += 1;
                    else
                    {
                        fModified = true;
                        break; /* truncate it */
                    }
                }
                else
                    break;
            }
            else if (RTUniCpIsBMP(uc))
            {
                if (uc < 0xfffe)
                {
                    if (cUnits < 127)
                    {
                        cUnits   += 1;
                        cMaxUnits = 127;
                    }
                    else
                    {
                        fModified = true;
                        break; /* truncate it */
                    }
                }
                else
                {
                    fModified = true;
                    continue; /* drop 0xffff and 0xfffe */
                }
            }
            else if (RTUniCpIsValid(uc))
            {
                if (cUnits < 127 - 1)
                {
                    cUnits   += 2;
                    cMaxUnits = 127;
                }
                else
                {
                    fModified = true;
                    break; /* truncate it */
                }
            }
            else
            {
                fModified = true;
                continue; /* drop codepoint as we cannot recode it as UTF-16 */
            }
        }
        else if (rc == VERR_END_OF_STRING)
            break;
        else
            AssertRCReturn(rc, rc);

        pszDst = RTStrPutCp(pszDst, uc);
    }
    RT_NOREF(fModified);
    Assert(cUnits <= cMaxUnits);

    /*
     * Complete the returned string and the length of the FID string.
     */
    *pszDst      = '\0';
    *pcchDst     = (size_t)(pszDst - pszDstStart);
    *pcbInDirRec = cMaxUnits > 127 ? 1 + cUnits + RTFSISOMAKERNAME_UDF_8BIT_NAME_FLAG : 1 + cUnits * sizeof(RTUTF16);

    /*
     * Unique name?
     */
    if (!rtFsIsoMakerFindObjInDir(pParent, pszDstStart, *pcchDst))
        return VINF_SUCCESS;
    return VERR_DUPLICATE;
}


/**
 * Normalizes a name for the specified name space.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO maker instance.
 * @param   pNamespace      The namespace which rules to normalize it according to.
 * @param   pParent         The parent directory.  NULL if root.
 * @param   pchSrc          The specified name to normalize (not necessarily zero
 *                          terminated).
 * @param   cchSrc          The length of the specified name.
 * @param   fIsDir          Indicates whether it's a directory or file (like).
 * @param   fNoNormalize    Don't normalize the name very strictly (imported or
 *                          such).
 * @param   pszDst          The output buffer.  Must be at least 32 bytes.
 * @param   cbDst           The size of the output buffer.
 * @param   pcchDst         Where to return the length of the returned string (i.e.
 *                          not counting the terminator).
 * @param   pcbInDirRec     Where to return the name size in the directory record.
 */
static int rtFsIsoMakerNormalizeNameForNamespace(PRTFSISOMAKERINT pThis, PRTFSISOMAKERNAMESPACE pNamespace,
                                                 PRTFSISOMAKERNAME pParent, const char *pchSrc, size_t cchSrc,
                                                 bool fNoNormalize, bool fIsDir,
                                                 char *pszDst, size_t cbDst, size_t *pcchDst, size_t *pcbInDirRec)
{
    if (cchSrc > 0)
    {
        /*
         * Check that the object doesn't already exist.
         */
        AssertReturn(!rtFsIsoMakerFindEntryInDirBySpec(pParent, pchSrc, cchSrc), VERR_ALREADY_EXISTS);

        /*
         * Do namespace specific normalization.
         */
        switch (pNamespace->fNamespace)
        {
            /*
             * This one is a lot of work, so separate function.
             */
            case RTFSISOMAKER_NAMESPACE_ISO_9660:
                return rtFsIsoMakerNormalizeNameForPrimaryIso9660(pThis, pParent, pchSrc, cchSrc, fNoNormalize, fIsDir,
                                                                  pszDst, cbDst, pcchDst, pcbInDirRec);

            /*
             * At the moment we don't give darn about UCS-2 limitations here...
             */
            case RTFSISOMAKER_NAMESPACE_JOLIET:
            {
/** @todo Joliet name limit and check for duplicates.   */
                AssertReturn(cbDst > cchSrc, VERR_BUFFER_OVERFLOW);
                memcpy(pszDst, pchSrc, cchSrc);
                pszDst[cchSrc] = '\0';
                *pcchDst     = cchSrc;
                *pcbInDirRec = RTStrCalcUtf16Len(pszDst) * sizeof(RTUTF16);
                return VINF_SUCCESS;
            }

            case RTFSISOMAKER_NAMESPACE_UDF:
                return rtFsIsoMakerNormalizeNameForUdf(pParent, pchSrc, cchSrc, pszDst, cbDst, pcchDst, pcbInDirRec);

            case RTFSISOMAKER_NAMESPACE_HFS:
                AssertFailedReturn(VERR_NOT_IMPLEMENTED);

            default:
                AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
        }
    }
    else
    {
        /*
         * Root special case.
         *
         * For ISO-9660 and joliet, we enter it with a length of 1 byte.  The
         * value byte value is zero.  The path tables we generate won't be
         * accepted by windows unless we do this.
         */
        *pszDst      = '\0';
        *pcchDst     = 0;
        if (pNamespace->fNamespace & (RTFSISOMAKER_NAMESPACE_ISO_9660 | RTFSISOMAKER_NAMESPACE_JOLIET))
            *pcbInDirRec = 1;
        else if (pNamespace->fNamespace & RTFSISOMAKER_NAMESPACE_UDF)
            *pcbInDirRec = UDFFILEIDDESC_CALC_SIZE_EX(0, 0);
        else
            *pcbInDirRec = 0;

        AssertReturn(!pParent, VERR_ISOMK_IPE_NAMESPACE_3);
        return VINF_SUCCESS;
    }
}


/**
 * Creates a TRANS.TBL file object for a newly named directory.
 *
 * The file is associated with the namespace node for the directory.  The file
 * will be generated on the fly from the directory object.
 *
 * @returns IPRT status code.
 * @param   pThis       The ISO maker instance.
 * @param   pNamespace  The namespace.
 * @param   pDirName    The new name space node for the directory.
 */
static int rtFsIsoMakerAddTransTblFileToNewDir(PRTFSISOMAKERINT pThis, PRTFSISOMAKERNAMESPACE pNamespace,
                                               PRTFSISOMAKERNAME pDirName)
{
    /*
     * Create a file object for it.
     */
    PRTFSISOMAKERFILE pFile;
    int rc = rtFsIsoMakerAddUnnamedFileWorker(pThis, NULL, 0, &pFile);
    if (RT_SUCCESS(rc))
    {
        pFile->enmSrcType     = RTFSISOMAKERSRCTYPE_TRANS_TBL;
        pFile->u.pTransTblDir = pDirName;
        pFile->pBootInfoTable = NULL;
        pDirName->pDir->pTransTblFile = pFile;

        /*
         * Add it to the directory.
         */
        PRTFSISOMAKERNAME pTransTblNm;
        rc = rtFsIsoMakerObjSetName(pThis, pNamespace, &pFile->Core, pDirName, pNamespace->pszTransTbl,
                                    strlen(pNamespace->pszTransTbl), false /*fNoNormalize*/, &pTransTblNm);
        if (RT_SUCCESS(rc))
        {
            pTransTblNm->cchTransNm = 0;
            return VINF_SUCCESS;
        }

        /*
         * Bail.
         */
        pDirName->pDir->pTransTblFile = NULL;
        rtFsIsoMakerObjRemoveWorker(pThis, &pFile->Core);
    }
    return rc;
}


/**
 * Helper for ensuring sufficent space for adding another entry in a directory.
 */
static int rtFsIsoMakerDirEnsureSpaceForAnotherEntry(PRTFSISOMAKERNAMEDIR pDir)
{
    AssertReturn(pDir, VERR_ISOMK_IPE_NAMESPACE_1);
    uint32_t cChildren = pDir->cChildren;
    if (cChildren & 31)
    { /* likely */ }
    else
    {
        AssertReturn(cChildren < RTFSISOMAKER_MAX_OBJECTS_PER_DIR, VERR_TOO_MUCH_DATA);
        void *pvNew = RTMemRealloc(pDir->papChildren, (cChildren + 32) * sizeof(pDir->papChildren[0]));
        AssertReturn(pvNew, VERR_NO_MEMORY);
        pDir->papChildren = (PPRTFSISOMAKERNAME)pvNew;
    }
    return VINF_SUCCESS;
}


/**
 * Inserts @a pName into @a pDir.
 *
 * Caller must call rtFsIsoMakerDirEnsureSpaceForAnotherEntry() before this
 * function!
 */
static void rtFsIsoMakerDirInsertChild(PRTFSISOMAKERNAMESPACE pNamespace, PRTFSISOMAKERNAMEDIR pDir, PRTFSISOMAKERNAME pName)
{
    uint32_t const idxName   = rtFsIsoMakerFindInsertIndex(pNamespace, pDir, pName->szName);
    uint32_t const cChildren = pDir->cChildren;
    if (idxName < cChildren)
        memmove(&pDir->papChildren[idxName + 1], &pDir->papChildren[idxName],
                (cChildren - idxName) * sizeof(pDir->papChildren[0]));
    pDir->papChildren[idxName] = pName;
    pDir->cChildren = cChildren + 1;
    pNamespace->cNames++;
}


/**
 * Removes a child from the directory.
 *
 * @returns pName on success, NULL on failure.
 * @param   pNamespace  The namespace.
 * @param   pDir        The directory.
 * @param   pName       The name to remove.
 */
static PRTFSISOMAKERNAME
rtFsIsoMakerDirRemoveChild(PRTFSISOMAKERNAMESPACE pNamespace, PRTFSISOMAKERNAMEDIR pDir, PRTFSISOMAKERNAME pName)
{
    uint32_t iChild = pDir->cChildren;
    while (iChild-- > 0)
        if (pDir->papChildren[iChild] == pName)
        {
            uint32_t cToMove = pDir->cChildren - iChild - 1;
            if (cToMove > 0)
                memmove(&pDir->papChildren[iChild], &pDir->papChildren[iChild + 1], cToMove * sizeof(pDir->papChildren[0]));
            pDir->cChildren--;
            pNamespace->cNames--;
            pName->pParent = NULL;
            return pName;
        }

    return NULL;
}


/**
 * Sets the name of an object in a namespace.
 *
 * If the object is already named in the name space, it will first be removed
 * from that namespace.  Should we run out of memory or into normalization
 * issues after removing it, its original state will _not_ be restored.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO maker instance.
 * @param   pNamespace      The namespace.
 * @param   pObj            The object to name.
 * @param   pParent         The parent namespace entry
 * @param   pchSpec         The specified name (not necessarily terminated).
 * @param   cchSpec         The specified name length.
 * @param   fNoNormalize    Don't normalize the name (imported or such).
 * @param   ppNewName       Where to return the name entry.  Optional.
 */
static int rtFsIsoMakerObjSetName(PRTFSISOMAKERINT pThis, PRTFSISOMAKERNAMESPACE pNamespace, PRTFSISOMAKEROBJ pObj,
                                  PRTFSISOMAKERNAME pParent, const char *pchSpec, size_t cchSpec, bool fNoNormalize,
                                  PPRTFSISOMAKERNAME ppNewName)
{
    Assert(cchSpec < _32K);

    /*
     * If this is a file, check the size against the ISO level.
     * This ASSUMES that only files which size we already know will be 4GB+ sized.
     */
    if (   (pNamespace->fNamespace & RTFSISOMAKER_NAMESPACE_ISO_9660)
        && pNamespace->uLevel < 3
        && pObj->enmType == RTFSISOMAKEROBJTYPE_FILE)
    {
        PRTFSISOMAKERFILE pFile = (PRTFSISOMAKERFILE)pObj;
        if (pFile->cbData >= _4G)
            return VERR_ISOMK_FILE_TOO_BIG_REQ_ISO_LEVEL_3;
    }

    /*
     * If this is a symbolic link, refuse to add it to a namespace that isn't
     * configured to support symbolic links.
     */
    if (   pObj->enmType == RTFSISOMAKEROBJTYPE_SYMLINK
        && (pNamespace->fNamespace & (RTFSISOMAKER_NAMESPACE_ISO_9660 | RTFSISOMAKER_NAMESPACE_JOLIET))
        && pNamespace->uRockRidgeLevel == 0)
        return VERR_ISOMK_SYMLINK_REQ_ROCK_RIDGE;

    /*
     * If the object is already named, unset that name before continuing.
     */
    if (*rtFsIsoMakerObjGetNameForNamespace(pObj, pNamespace))
    {
        int rc = rtFsIsoMakerObjUnsetName(pThis, pNamespace, pObj);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * To avoid need to revert anything, make sure papChildren in the parent is
     * large enough.  If root object, make sure we haven't got a root already.
     */
    if (pParent)
    {
        int rc = rtFsIsoMakerDirEnsureSpaceForAnotherEntry(pParent->pDir);
        if (RT_FAILURE(rc))
            return rc;
    }
    else
        AssertReturn(pNamespace->pRoot == NULL, VERR_ISOMK_IPE_NAMESPACE_2);

    /*
     * Normalize the name for this namespace.
     */
    size_t cchName        = 0;
    size_t cbNameInDirRec = 0;
    char   szName[RTFSISOMAKER_MAX_NAME_BUF];
    int rc = rtFsIsoMakerNormalizeNameForNamespace(pThis, pNamespace, pParent, pchSpec, cchSpec, fNoNormalize,
                                                   pObj->enmType == RTFSISOMAKEROBJTYPE_DIR,
                                                   szName, sizeof(szName), &cchName, &cbNameInDirRec);
    if (RT_SUCCESS(rc))
    {
        Assert(cbNameInDirRec > 0);

        PRTFSISOMAKERNAME    const pName = (PRTFSISOMAKERNAME)RTMemAllocZ(RT_UOFFSETOF(RTFSISOMAKERNAME, szName)
                                                                          + cchName + 1 + cchSpec + 1);
        PRTFSISOMAKERNAMEDIR const pDir  = pObj->enmType != RTFSISOMAKEROBJTYPE_DIR ? NULL
                                         : (PRTFSISOMAKERNAMEDIR)RTMemAllocZ(sizeof(*pDir));
        if (   pName != NULL
            && (pObj->enmType != RTFSISOMAKEROBJTYPE_DIR || pDir != NULL))
        {
            pName->pObj                 = pObj;
            pName->pParent              = pParent;
            pName->cbNameInDirRec       = (uint16_t)cbNameInDirRec;
            pName->cchName              = (uint16_t)cchName;

            char *pszDst = &pName->szName[cchName + 1];
            memcpy(pszDst, pchSpec, cchSpec);
            pszDst[cchSpec] = '\0';
            pName->pszSpecNm            = pszDst;
            pName->pszRockRidgeNm       = pszDst;
            pName->pszTransNm           = pszDst;
            pName->cchSpecNm            = (uint16_t)cchSpec;
            pName->cchRockRidgeNm       = (uint16_t)cchSpec;
            pName->cchTransNm           = (uint16_t)cchSpec;
            pName->uDepth               = pParent ? pParent->uDepth + 1 : 0;
            pName->fRockRidgeNmAlloced  = false;
            pName->fTransNmAlloced      = false;
            pName->fRockNeedER          = false;
            pName->fRockNeedRRInDirRec  = false;
            pName->fRockNeedRRInSpill   = false;

            pName->fMode                = pObj->fMode;
            pName->uid                  = pObj->uid;
            pName->gid                  = pObj->gid;
            pName->Device               = 0;
            pName->cHardlinks           = 1;
            pName->uUdfIcbSector        = UINT32_MAX;
            pName->offDirRec            = UINT32_MAX;
            pName->cbDirRec             = 0;
            pName->cDirRecs             = 1;
            pName->cbDirRecTotal        = 0;
            pName->fRockEntries         = 0;
            pName->cbRockInDirRec       = 0;
            pName->offRockSpill         = UINT32_MAX;
            pName->cbRockSpill          = 0;

            memcpy(pName->szName, szName, cchName);
            pName->szName[cchName] = '\0';

            pName->pDir                 = pDir;
            if (pObj->enmType == RTFSISOMAKEROBJTYPE_DIR)
            {
                pDir->RangeNode.Core.Key     = UINT64_MAX;
                pDir->RangeNode.Core.KeyLast = UINT64_MAX;
                pDir->cbDir                  = 0;
                pDir->cChildren              = 0;
                pDir->papChildren            = NULL;
                pDir->pTransTblFile          = NULL;
                pDir->pName                  = pName;
                pDir->offPathTable           = UINT32_MAX;
                pDir->idPathTable            = UINT16_MAX;
                pDir->cbDirRec00             = 0;
                pDir->cbDirRec01             = 0;
                RTListInit(&pDir->FinalizedEntry);

                /* Create the TRANS.TBL file object and enter it into this directory as the first entry. */
                if (pNamespace->pszTransTbl)
                {
                    rc = rtFsIsoMakerAddTransTblFileToNewDir(pThis, pNamespace, pName);
                    if (RT_FAILURE(rc))
                    {
                        RTMemFree(pName);
                        RTMemFree(pDir);
                        return rc;
                    }
                }
            }

            /*
             * Do the linking and stats.  We practice insertion sorting.
             */
            if (pParent)
                rtFsIsoMakerDirInsertChild(pNamespace, pParent->pDir, pName);
            else
            {
                pNamespace->pRoot = pName;
                pNamespace->cNames++;
            }
            *rtFsIsoMakerObjGetNameForNamespace(pObj, pNamespace) = pName;

            /*
             * Done.
             */
            if (ppNewName)
                *ppNewName = pName;
            return VINF_SUCCESS;
        }
        rc = VERR_NO_MEMORY;
    }
    return rc;
}


/**
 * Walks the path up to the parent, creating missing directories as needed.
 *
 * As usual, we walk the specified names rather than the mangled ones.
 *
 * @returns IPRT status code.
 * @param   pThis       The ISO maker instance.
 * @param   pNamespace  The namespace to walk.
 * @param   pszPath     The path to walk.
 * @param   ppParent    Where to return the pointer to the parent
 *                      namespace node.
 * @param   ppszEntry   Where to return the pointer to the final name component.
 * @param   pcchEntry   Where to return the length of the final name component.
 */
static int rtFsIsoMakerCreatePathToParent(PRTFSISOMAKERINT pThis, PRTFSISOMAKERNAMESPACE pNamespace, const char *pszPath,
                                          PPRTFSISOMAKERNAME ppParent, const char **ppszEntry, size_t *pcchEntry)
{
    *ppParent  = NULL; /* shut up gcc */
    *ppszEntry = NULL; /* shut up gcc */
    *pcchEntry = 0;    /* shut up gcc */

    int rc;
    AssertReturn(RTPATH_IS_SLASH(*pszPath), VERR_ISOMK_IPE_ROOT_SLASH);

    /*
     * Deal with the special case of the root.
     */
    while (RTPATH_IS_SLASH(*pszPath))
        pszPath++;
    AssertReturn(*pszPath, VERR_ISOMK_IPE_EMPTY_PATH); /* We should not be called on a root path. */

    PRTFSISOMAKERNAME pParent = pNamespace->pRoot;
    if (!pParent)
    {
        PRTFSISOMAKERDIR pDir = RTListGetFirst(&pThis->ObjectHead, RTFSISOMAKERDIR, Core.Entry);
#ifdef RT_STRICT
        Assert(pDir);
        Assert(pDir->Core.idxObj == 0);
        Assert(pDir->Core.enmType == RTFSISOMAKEROBJTYPE_DIR);
        Assert(*rtFsIsoMakerObjGetNameForNamespace(&pDir->Core, pNamespace) == NULL);
#endif

        rc = rtFsIsoMakerObjSetName(pThis, pNamespace, &pDir->Core, NULL /*pParent*/, "", 0, false /*fNoNormalize*/, &pParent);
        AssertRCReturn(rc, rc);
        pParent = pNamespace->pRoot;
        AssertReturn(pParent, VERR_ISOMK_IPE_NAMESPACE_4);
    }

    /*
     * Now, do the rest of the path.
     */
    for (;;)
    {
        /*
         * Find the end of the component and see if its the final one or not.
         */
        char ch;
        size_t cchComponent = 0;
        while ((ch = pszPath[cchComponent]) != '\0' && !RTPATH_IS_SLASH(ch))
            cchComponent++;
        AssertReturn(cchComponent > 0, VERR_ISOMK_IPE_EMPTY_COMPONENT);

        size_t offNext = cchComponent;
        while (RTPATH_IS_SLASH(ch))
            ch = pszPath[++offNext];

        if (ch == '\0')
        {
            /*
             * Final component.  Make sure it is not dot or dot-dot before returning.
             */
            AssertReturn(   pszPath[0] != '.'
                         || cchComponent > 2
                         || (   cchComponent == 2
                             && pszPath[1] != '.'),
                         VERR_INVALID_NAME);

            *ppParent  = pParent;
            *ppszEntry = pszPath;
            *pcchEntry = cchComponent;
            return VINF_SUCCESS;
        }

        /*
         * Deal with dot and dot-dot.
         */
        if (cchComponent == 1 && pszPath[0] == '.')
        { /* nothing to do */ }
        else if (cchComponent == 2 && pszPath[0] == '.' && pszPath[1] == '.')
        {
            if (pParent->pParent)
                pParent = pParent->pParent;
        }
        /*
         * Look it up.
         */
        else
        {
            PRTFSISOMAKERNAME pChild = rtFsIsoMakerFindEntryInDirBySpec(pParent, pszPath, cchComponent);
            if (pChild)
            {
                if (pChild->pDir)
                    pParent = pChild;
                else
                    return VERR_NOT_A_DIRECTORY;
            }
            else
            {
                /* Try see if we've got a directory with the same spec name in a different namespace.
                   (We don't want to waste heap by creating a directory instance per namespace.) */
                PRTFSISOMAKERDIR pChildObj = rtFsIsoMakerFindSubdirBySpec((PRTFSISOMAKERDIR)pParent->pObj,
                                                                           pszPath, cchComponent, pNamespace->fNamespace);
                if (pChildObj)
                {
                    PPRTFSISOMAKERNAME ppChildName = rtFsIsoMakerObjGetNameForNamespace(&pChildObj->Core, pNamespace);
                    if (!*ppChildName)
                    {
                        rc = rtFsIsoMakerObjSetName(pThis, pNamespace, &pChildObj->Core, pParent, pszPath, cchComponent,
                                                    false /*fNoNormalize*/, &pChild);
                        if (RT_FAILURE(rc))
                            return rc;
                        AssertReturn(pChild != NULL, VERR_ISOMK_IPE_NAMESPACE_5);
                    }
                }
                /* If we didn't have luck in other namespaces, create a new directory. */
                if (!pChild)
                {
                    rc = rtFsIsoMakerAddUnnamedDirWorker(pThis, NULL /*pObjInfo*/, &pChildObj);
                    if (RT_SUCCESS(rc))
                        rc = rtFsIsoMakerObjSetName(pThis, pNamespace, &pChildObj->Core, pParent, pszPath, cchComponent,
                                                    false /*fNoNormalize*/, &pChild);
                    if (RT_FAILURE(rc))
                        return rc;
                    AssertReturn(pChild != NULL, VERR_ISOMK_IPE_NAMESPACE_5);
                }
                pParent = pChild;
            }
        }

        /*
         * Skip ahead in the path.
         */
        pszPath += offNext;
    }
}


/**
 * Worker for RTFsIsoMakerObjSetPath that operates on a single namespace.
 *
 * @returns IPRT status code.
 * @param   pThis           The ISO maker instance.
 * @param   pNamespace      The namespace to name it in.
 * @param   pObj            The filesystem object to name.
 * @param   pszPath         The path to the entry in the namespace.
 */
static int rtFsIsoMakerObjSetPathInOne(PRTFSISOMAKERINT pThis, PRTFSISOMAKERNAMESPACE pNamespace,
                                       PRTFSISOMAKEROBJ pObj, const char *pszPath)
{
    AssertReturn(*rtFsIsoMakerObjGetNameForNamespace(pObj, pNamespace) == NULL, VERR_WRONG_ORDER);
    AssertReturn(RTPATH_IS_SLASH(*pszPath), VERR_ISOMK_IPE_ROOT_SLASH);

    /*
     * Figure out where the parent is.
     * This will create missing parent name space entries and directory nodes.
     */
    PRTFSISOMAKERNAME   pParent;
    const char         *pszEntry;
    size_t              cchEntry;
    int                 rc;
    if (pszPath[1] != '\0')
        rc = rtFsIsoMakerCreatePathToParent(pThis, pNamespace, pszPath, &pParent, &pszEntry, &cchEntry);
    else
    {
        /*
         * Special case for the root directory.
         */
        Assert(pObj->enmType == RTFSISOMAKEROBJTYPE_DIR);
        AssertReturn(pNamespace->pRoot == NULL, VERR_WRONG_ORDER);
        pszEntry = "/";
        cchEntry = 0;
        pParent  = NULL;
        rc       = VINF_SUCCESS;
    }

    /*
     * Do the job on the final path component.
     */
    if (RT_SUCCESS(rc))
    {
        AssertReturn(!RTPATH_IS_SLASH(pszEntry[cchEntry]) || pObj->enmType == RTFSISOMAKEROBJTYPE_DIR,
                     VERR_NOT_A_DIRECTORY);
        rc = rtFsIsoMakerObjSetName(pThis, pNamespace, pObj, pParent, pszEntry, cchEntry, false /*fNoNormalize*/, NULL);
    }
    return rc;
}


/**
 * Removes an object from the given namespace.
 *
 * @returns IPRT status code.
 * @param   pThis       The ISO maker instance.
 * @param   pNamespace  The namespace.
 * @param   pObj        The object to name.
 */
static int rtFsIsoMakerObjUnsetName(PRTFSISOMAKERINT pThis, PRTFSISOMAKERNAMESPACE pNamespace, PRTFSISOMAKEROBJ pObj)
{
    LogFlow(("rtFsIsoMakerObjUnsetName: idxObj=#%#x\n", pObj->idxObj));

    /*
     * First check if there is anything to do here at all.
     */
    PPRTFSISOMAKERNAME ppName = rtFsIsoMakerObjGetNameForNamespace(pObj, pNamespace);
    PRTFSISOMAKERNAME  pName = *ppName;
    if (!pName)
        return VINF_SUCCESS;

    /*
     * We don't support this on the root.
     */
    AssertReturn(pName->pParent, VERR_ACCESS_DENIED);

    /*
     * If this is a directory, we're in for some real fun here as we need to
     * unset the names of all the children too.
     */
    PRTFSISOMAKERNAMEDIR const pDir = pName->pDir;
    if (pDir)
    {
        uint32_t iChild = pDir->cChildren;
        while (iChild-- > 0)
        {
            int rc = rtFsIsoMakerObjUnsetName(pThis, pNamespace, pDir->papChildren[iChild]->pObj);
            if (RT_FAILURE(rc))
                return rc;
        }
        AssertReturn(pDir->cChildren == 0, VERR_DIR_NOT_EMPTY);
    }

    /*
     * Unlink the pName from the parent.
     */
    if (rtFsIsoMakerDirRemoveChild(pNamespace, pName->pParent->pDir, pName))
    {
        /*
         * NULL the name member in the object and free the structure.
         */
        *ppName = NULL;
        RTMemFree(pName);

        return VINF_SUCCESS;
    }

    /* Not found. This can't happen. */
    AssertFailedReturn(VERR_ISOMK_IPE_NAMESPACE_6);
}


/**
 * Gets currently populated namespaces.
 *
 * @returns Set of namespaces (RTFSISOMAKER_NAMESPACE_XXX), UINT32_MAX on error.
 * @param   hIsoMaker           The ISO maker handle.
 */
RTDECL(uint32_t) RTFsIsoMakerGetPopulatedNamespaces(RTFSISOMAKER hIsoMaker)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET_EX(pThis, UINT32_MAX);

    uint32_t fRet = 0;
    if (pThis->PrimaryIso.cNames > 0)
        fRet |= RTFSISOMAKER_NAMESPACE_ISO_9660;
    if (pThis->Joliet.cNames     > 0)
        fRet |= RTFSISOMAKER_NAMESPACE_JOLIET;
    if (pThis->Udf.cNames        > 0)
        fRet |= RTFSISOMAKER_NAMESPACE_UDF;
    if (pThis->Hfs.cNames        > 0)
        fRet |= RTFSISOMAKER_NAMESPACE_HFS;

    return fRet;
}




/*
 *
 * Object level config
 * Object level config
 * Object level config
 *
 */


/**
 * Translates an object index number to an object pointer, slow path.
 *
 * @returns Pointer to object, NULL if not found.
 * @param   pThis               The ISO maker instance.
 * @param   idxObj              The object index too resolve.
 */
DECL_NO_INLINE(static, PRTFSISOMAKEROBJ) rtFsIsoMakerIndexToObjSlow(PRTFSISOMAKERINT pThis, uint32_t idxObj)
{
    PRTFSISOMAKEROBJ pObj;
    RTListForEachReverse(&pThis->ObjectHead, pObj, RTFSISOMAKEROBJ, Entry)
    {
        if (pObj->idxObj == idxObj)
            return pObj;
    }
    return NULL;
}


/**
 * Translates an object index number to an object pointer.
 *
 * @returns Pointer to object, NULL if not found.
 * @param   pThis               The ISO maker instance.
 * @param   idxObj              The object index too resolve.
 */
DECLINLINE(PRTFSISOMAKEROBJ) rtFsIsoMakerIndexToObj(PRTFSISOMAKERINT pThis, uint32_t idxObj)
{
    PRTFSISOMAKEROBJ pObj = RTListGetLast(&pThis->ObjectHead, RTFSISOMAKEROBJ, Entry);
    if (!pObj || RT_LIKELY(pObj->idxObj == idxObj))
        return pObj;
    return rtFsIsoMakerIndexToObjSlow(pThis, idxObj);
}


/**
 * Resolves a path into a object ID.
 *
 * This will be doing the looking up using the specified object names rather
 * than the version adjusted and mangled according to the namespace setup.
 *
 * @returns The object ID corresponding to @a pszPath, or UINT32_MAX if not
 *          found or invalid parameters.
 * @param   hIsoMaker           The ISO maker instance.
 * @param   fNamespaces         The namespace to resolve @a pszPath in.  It's
 *                              possible to specify multiple namespaces here, of
 *                              course, but that's inefficient.
 * @param   pszPath             The path to the object.
 */
RTDECL(uint32_t) RTFsIsoMakerGetObjIdxForPath(RTFSISOMAKER hIsoMaker, uint32_t fNamespaces, const char *pszPath)
{
    /*
     * Validate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET_EX(pThis, UINT32_MAX);

    /*
     * Do the searching.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aRTFsIsoNamespaces); i++)
        if (fNamespaces & g_aRTFsIsoNamespaces[i].fNamespace)
        {
            PRTFSISOMAKERNAMESPACE pNamespace = (PRTFSISOMAKERNAMESPACE)((uintptr_t)pThis + g_aRTFsIsoNamespaces[i].offNamespace);
            if (pNamespace->pRoot)
            {
                PRTFSISOMAKERNAME pName;
                int rc = rtFsIsoMakerWalkPathBySpec(pNamespace, pszPath, &pName);
                if (RT_SUCCESS(rc))
                    return pName->pObj->idxObj;
            }
        }

    return UINT32_MAX;
}


/**
 * Removes the specified object from the image.
 *
 * This is a worker for RTFsIsoMakerObjRemove and
 * rtFsIsoMakerFinalizeRemoveOrphans.
 *
 * @returns IPRT status code.
 * @param   hIsoMaker           The ISO maker instance.
 * @param   pObj                The object to remove from the image.
 */
static int rtFsIsoMakerObjRemoveWorker(PRTFSISOMAKERINT pThis, PRTFSISOMAKEROBJ pObj)
{
    /*
     * Don't allow removing trans.tbl files and the boot catalog.
     */
    if (pObj->enmType == RTFSISOMAKEROBJTYPE_FILE)
    {
        PRTFSISOMAKERFILE pFile = (PRTFSISOMAKERFILE)pObj;
        if (pFile->enmSrcType == RTFSISOMAKERSRCTYPE_TRANS_TBL)
            return VWRN_DANGLING_OBJECTS; /* HACK ALERT! AssertReturn(pFile->enmSrcType != RTFSISOMAKERSRCTYPE_TRANS_TBL, VERR_ACCESS_DENIED); */
        AssertReturn(pFile != pThis->pBootCatFile, VERR_ACCESS_DENIED);
    }

    /*
     * Remove the object from all name spaces.
     */
    int rc = VINF_SUCCESS;
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aRTFsIsoNamespaces); i++)
    {
        PRTFSISOMAKERNAMESPACE pNamespace = (PRTFSISOMAKERNAMESPACE)((uintptr_t)pThis + g_aRTFsIsoNamespaces[i].offNamespace);
        int rc2 = rtFsIsoMakerObjUnsetName(pThis, pNamespace, pObj);
        if (RT_SUCCESS(rc2) || RT_FAILURE(rc))
            continue;
        rc = rc2;
    }

    /*
     * If that succeeded, remove the object itself.
     */
    if (RT_SUCCESS(rc))
    {
        RTListNodeRemove(&pObj->Entry);
        if (pObj->enmType == RTFSISOMAKEROBJTYPE_FILE)
        {
            uint64_t cbData = ((PRTFSISOMAKERFILE)pObj)->cbData;
            pThis->cbData -= RT_ALIGN_64(cbData, RTFSISOMAKER_SECTOR_SIZE);
        }
        pThis->cObjects--;
        rtFsIsoMakerObjDestroy(pObj);
    }
    return rc;
}


/**
 * Removes the specified object from the image.
 *
 * @returns IPRT status code.
 * @param   hIsoMaker           The ISO maker instance.
 * @param   idxObj              The index of the object to remove.
 */
RTDECL(int) RTFsIsoMakerObjRemove(RTFSISOMAKER hIsoMaker, uint32_t idxObj)
{
    /*
     * Validate and translate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    PRTFSISOMAKEROBJ pObj = rtFsIsoMakerIndexToObj(pThis, idxObj);
    AssertReturn(pObj, VERR_OUT_OF_RANGE);
    AssertReturn(   pObj->enmType != RTFSISOMAKEROBJTYPE_FILE
                 || ((PRTFSISOMAKERFILE)pObj)->enmSrcType != RTFSISOMAKERSRCTYPE_RR_SPILL, VERR_ACCESS_DENIED);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    /*
     * Call worker.
     */
    return rtFsIsoMakerObjRemoveWorker(pThis, pObj);
}


/**
 * Sets the path (name) of an object in the selected namespaces.
 *
 * The name will be transformed as necessary.
 *
 * The initial implementation does not allow this function to be called more
 * than once on an object.
 *
 * @returns IPRT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   idxObj              The configuration index of to name.
 * @param   fNamespaces         The namespaces to apply the path to
 *                              (RTFSISOMAKER_NAMESPACE_XXX).
 * @param   pszPath             The path.
 */
RTDECL(int) RTFsIsoMakerObjSetPath(RTFSISOMAKER hIsoMaker, uint32_t idxObj, uint32_t fNamespaces, const char *pszPath)
{
    /*
     * Validate and translate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(!(fNamespaces & ~RTFSISOMAKER_NAMESPACE_VALID_MASK), VERR_INVALID_FLAGS);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(RTPATH_IS_SLASH(*pszPath), VERR_INVALID_NAME);
    PRTFSISOMAKEROBJ pObj = rtFsIsoMakerIndexToObj(pThis, idxObj);
    AssertReturn(pObj, VERR_OUT_OF_RANGE);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    /*
     * Execute requested actions.
     */
    uint32_t cAdded = 0;
    int      rc = VINF_SUCCESS;
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aRTFsIsoNamespaces); i++)
        if (fNamespaces & g_aRTFsIsoNamespaces[i].fNamespace)
        {
            PRTFSISOMAKERNAMESPACE pNamespace = (PRTFSISOMAKERNAMESPACE)((uintptr_t)pThis + g_aRTFsIsoNamespaces[i].offNamespace);
            if (pNamespace->uLevel > 0)
            {
                int rc2 = rtFsIsoMakerObjSetPathInOne(pThis, pNamespace, pObj, pszPath);
                if (RT_SUCCESS(rc2))
                    cAdded++;
                else if (RT_SUCCESS(rc) || rc == VERR_ISOMK_SYMLINK_REQ_ROCK_RIDGE)
                    rc = rc2;
            }
        }
    return rc != VERR_ISOMK_SYMLINK_REQ_ROCK_RIDGE || cAdded == 0 ? rc : VINF_ISOMK_SYMLINK_REQ_ROCK_RIDGE;
}


/**
 * Sets the name of an object in the selected namespaces, placing it under the
 * given directory.
 *
 * The name will be transformed as necessary.
 *
 * @returns IPRT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   idxObj              The configuration index of to name.
 * @param   idxParentObj        The parent directory object.
 * @param   fNamespaces         The namespaces to apply the path to
 *                              (RTFSISOMAKER_NAMESPACE_XXX).
 * @param   pszName             The name.
 * @param   fNoNormalize        Don't normalize the name (imported or such).
 */
RTDECL(int) RTFsIsoMakerObjSetNameAndParent(RTFSISOMAKER hIsoMaker, uint32_t idxObj, uint32_t idxParentObj,
                                            uint32_t fNamespaces, const char *pszName, bool fNoNormalize)
{
    /*
     * Validate and translate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(!(fNamespaces & ~RTFSISOMAKER_NAMESPACE_VALID_MASK), VERR_INVALID_FLAGS);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    size_t cchName = strlen(pszName);
    AssertReturn(cchName > 0, VERR_INVALID_NAME);
    AssertReturn(memchr(pszName, '/', cchName) == NULL, VERR_INVALID_NAME);
    PRTFSISOMAKEROBJ pObj = rtFsIsoMakerIndexToObj(pThis, idxObj);
    AssertReturn(pObj, VERR_OUT_OF_RANGE);
    PRTFSISOMAKEROBJ pParentObj = rtFsIsoMakerIndexToObj(pThis, idxParentObj);
    AssertReturn(pParentObj, VERR_OUT_OF_RANGE);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    /*
     * Execute requested actions.
     */
    uint32_t cAdded = 0;
    int rc = VINF_SUCCESS;
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aRTFsIsoNamespaces); i++)
        if (fNamespaces & g_aRTFsIsoNamespaces[i].fNamespace)
        {
            PRTFSISOMAKERNAMESPACE pNamespace = (PRTFSISOMAKERNAMESPACE)((uintptr_t)pThis + g_aRTFsIsoNamespaces[i].offNamespace);
            if (pNamespace->uLevel > 0)
            {
                PRTFSISOMAKERNAME pParentName = *rtFsIsoMakerObjGetNameForNamespace(pParentObj, pNamespace);
                if (pParentName)
                {
                    int rc2 = rtFsIsoMakerObjSetName(pThis, pNamespace, pObj, pParentName, pszName, cchName,
                                                     fNoNormalize, NULL /*ppNewName*/);
                    if (RT_SUCCESS(rc2))
                        cAdded++;
                    else if (RT_SUCCESS(rc) || rc == VERR_ISOMK_SYMLINK_REQ_ROCK_RIDGE)
                        rc = rc2;
                }
            }
        }
    return rc != VERR_ISOMK_SYMLINK_REQ_ROCK_RIDGE || cAdded == 0 ? rc : VINF_ISOMK_SYMLINK_REQ_ROCK_RIDGE;
}


/**
 * Changes the rock ridge name for the object in the selected namespaces.
 *
 * The object must already be entered into the namespaces by
 * RTFsIsoMakerObjSetNameAndParent, RTFsIsoMakerObjSetPath or similar.
 *
 * @returns IPRT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   idxObj              The configuration index of to name.
 * @param   fNamespaces         The namespaces to apply the path to
 *                              (RTFSISOMAKER_NAMESPACE_XXX).
 * @param   pszRockName         The rock ridge name.  Passing NULL will restore
 *                              it back to the specified name, while an empty
 *                              string will restore it to the namespace name.
 */
RTDECL(int) RTFsIsoMakerObjSetRockName(RTFSISOMAKER hIsoMaker, uint32_t idxObj, uint32_t fNamespaces, const char *pszRockName)
{
    /*
     * Validate and translate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(!(fNamespaces & ~RTFSISOMAKER_NAMESPACE_VALID_MASK), VERR_INVALID_FLAGS);
    size_t cchRockName;
    if (pszRockName)
    {
        AssertPtrReturn(pszRockName, VERR_INVALID_POINTER);
        cchRockName = strlen(pszRockName);
        AssertReturn(cchRockName < _1K, VERR_FILENAME_TOO_LONG);
        AssertReturn(memchr(pszRockName, '/', cchRockName) == NULL, VERR_INVALID_NAME);
    }
    else
        cchRockName = 0;
    PRTFSISOMAKEROBJ pObj = rtFsIsoMakerIndexToObj(pThis, idxObj);
    AssertReturn(pObj, VERR_OUT_OF_RANGE);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    /*
     * Execute requested actions.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aRTFsIsoNamespaces); i++)
        if (fNamespaces & g_aRTFsIsoNamespaces[i].fNamespace)
        {
            PRTFSISOMAKERNAMESPACE pNamespace = (PRTFSISOMAKERNAMESPACE)((uintptr_t)pThis + g_aRTFsIsoNamespaces[i].offNamespace);
            if (   pNamespace->uLevel > 0
                && pNamespace->uRockRidgeLevel > 0)
            {
                PRTFSISOMAKERNAME pName = *rtFsIsoMakerObjGetNameForNamespace(pObj, pNamespace);
                if (pName)
                {
                    /* Free the old rock ridge name. */
                    if (pName->fRockRidgeNmAlloced)
                    {
                        RTMemFree(pName->pszRockRidgeNm);
                        pName->pszRockRidgeNm      = NULL;
                        pName->fRockRidgeNmAlloced = false;
                    }

                    /* Set new rock ridge name. */
                    if (cchRockName > 0)
                    {
                        pName->pszRockRidgeNm = (char *)RTMemDup(pszRockName, cchRockName + 1);
                        if (!pName->pszRockRidgeNm)
                        {
                            pName->pszRockRidgeNm = (char *)pName->pszSpecNm;
                            pName->cchRockRidgeNm = pName->cchSpecNm;
                            return VERR_NO_MEMORY;
                        }
                        pName->cchRockRidgeNm = (uint16_t)cchRockName;
                        pName->fRockRidgeNmAlloced = true;
                    }
                    else if (pszRockName == NULL)
                    {
                        pName->pszRockRidgeNm = (char *)pName->pszSpecNm;
                        pName->cchRockRidgeNm = pName->cchSpecNm;
                    }
                    else
                    {
                        pName->pszRockRidgeNm = pName->szName;
                        pName->cchRockRidgeNm = pName->cchName;
                    }
                }
            }
        }
    return VINF_SUCCESS;
}


/**
 * Enables or disable syslinux boot info table patching of a file.
 *
 * @returns IPRT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   idxObj              The configuration index.
 * @param   fEnable             Whether to enable or disable patching.
 */
RTDECL(int) RTFsIsoMakerObjEnableBootInfoTablePatching(RTFSISOMAKER hIsoMaker, uint32_t idxObj, bool fEnable)
{
    /*
     * Validate and translate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);
    PRTFSISOMAKEROBJ pObj = rtFsIsoMakerIndexToObj(pThis, idxObj);
    AssertReturn(pObj, VERR_OUT_OF_RANGE);
    AssertReturn(pObj->enmType == RTFSISOMAKEROBJTYPE_FILE, VERR_WRONG_TYPE);
    PRTFSISOMAKERFILE pFile = (PRTFSISOMAKERFILE)pObj;
    AssertReturn(   pFile->enmSrcType == RTFSISOMAKERSRCTYPE_PATH
                 || pFile->enmSrcType == RTFSISOMAKERSRCTYPE_VFS_FILE
                 || pFile->enmSrcType == RTFSISOMAKERSRCTYPE_COMMON,
                 VERR_WRONG_TYPE);

    /*
     * Do the job.
     */
    if (fEnable)
    {
        if (!pFile->pBootInfoTable)
        {
            pFile->pBootInfoTable = (PISO9660SYSLINUXINFOTABLE)RTMemAllocZ(sizeof(*pFile->pBootInfoTable));
            AssertReturn(pFile->pBootInfoTable, VERR_NO_MEMORY);
        }
    }
    else if (pFile->pBootInfoTable)
    {
        RTMemFree(pFile->pBootInfoTable);
        pFile->pBootInfoTable = NULL;
    }
    return VINF_SUCCESS;
}


/**
 * Gets the data size of an object.
 *
 * Currently only supported on file objects.
 *
 * @returns IPRT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   idxObj              The configuration index.
 * @param   pcbData             Where to return the size.
 */
RTDECL(int) RTFsIsoMakerObjQueryDataSize(RTFSISOMAKER hIsoMaker, uint32_t idxObj, uint64_t *pcbData)
{
    /*
     * Validate and translate input.
     */
    AssertPtrReturn(pcbData, VERR_INVALID_POINTER);
    *pcbData = UINT64_MAX;
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    PRTFSISOMAKEROBJ pObj = rtFsIsoMakerIndexToObj(pThis, idxObj);
    AssertReturn(pObj, VERR_OUT_OF_RANGE);

    /*
     * Do the job.
     */
    if (pObj->enmType == RTFSISOMAKEROBJTYPE_FILE)
    {
        PRTFSISOMAKERFILE pFile = (PRTFSISOMAKERFILE)pObj;
        if (   pFile->enmSrcType != RTFSISOMAKERSRCTYPE_TRANS_TBL
            && pFile->enmSrcType != RTFSISOMAKERSRCTYPE_RR_SPILL)
        {
            *pcbData = ((PRTFSISOMAKERFILE)pObj)->cbData;
            return VINF_SUCCESS;
        }
    }
    return VERR_WRONG_TYPE;
}


/**
 * Initalizes the common part of a file system object and links it into global
 * chain.
 *
 * @returns IPRT status code
 * @param   pThis               The ISO maker instance.
 * @param   pObj                The common object.
 * @param   enmType             The object type.
 * @param   pObjInfo            The object information (typically source).
 *                              Optional.
 */
static int rtFsIsoMakerInitCommonObj(PRTFSISOMAKERINT pThis, PRTFSISOMAKEROBJ pObj,
                                     RTFSISOMAKEROBJTYPE enmType, PCRTFSOBJINFO pObjInfo)
{
    Assert(!pThis->fFinalized);
    AssertReturn(pThis->cObjects < RTFSISOMAKER_MAX_OBJECTS, VERR_OUT_OF_RANGE);

    pObj->enmType       = enmType;
    pObj->pPrimaryName  = NULL;
    pObj->pJolietName   = NULL;
    pObj->pUdfName      = NULL;
    pObj->pHfsName      = NULL;
    pObj->idxObj        = pThis->cObjects++;
    pObj->cNotOrphan    = 0;
    if (pObjInfo)
    {
        pObj->BirthTime         = pObjInfo->BirthTime;
        pObj->ChangeTime        = pObjInfo->ChangeTime;
        pObj->ModificationTime  = pObjInfo->ModificationTime;
        pObj->AccessedTime      = pObjInfo->AccessTime;
        if (!pThis->fStrictAttributeStyle)
        {
            if (enmType == RTFSISOMAKEROBJTYPE_DIR)
                pObj->fMode     = (pObjInfo->Attr.fMode & ~07222) | 0555;
            else
            {
                pObj->fMode     = (pObjInfo->Attr.fMode & ~00222) | 0444;
                if (pObj->fMode & 0111)
                    pObj->fMode |= 0111;
            }
            pObj->uid           = pThis->uidDefault;
            pObj->gid           = pThis->gidDefault;
        }
        else
        {
            pObj->fMode         = pObjInfo->Attr.fMode;
            pObj->uid           = pObjInfo->Attr.u.Unix.uid != NIL_RTUID ? pObjInfo->Attr.u.Unix.uid : pThis->uidDefault;
            pObj->gid           = pObjInfo->Attr.u.Unix.gid != NIL_RTGID ? pObjInfo->Attr.u.Unix.gid : pThis->gidDefault;
        }
        if (enmType == RTFSISOMAKEROBJTYPE_DIR ? pThis->fForcedDirModeActive : pThis->fForcedFileModeActive)
            pObj->fMode = (pObj->fMode & ~RTFS_UNIX_ALL_PERMS)
                        | (enmType == RTFSISOMAKEROBJTYPE_DIR ? pThis->fForcedDirMode : pThis->fForcedFileMode);
    }
    else
    {
        pObj->BirthTime         = pThis->ImageCreationTime;
        pObj->ChangeTime        = pThis->ImageCreationTime;
        pObj->ModificationTime  = pThis->ImageCreationTime;
        pObj->AccessedTime      = pThis->ImageCreationTime;
        pObj->fMode             = enmType == RTFSISOMAKEROBJTYPE_DIR ? pThis->fDefaultDirMode : pThis->fDefaultFileMode;
        pObj->uid               = pThis->uidDefault;
        pObj->gid               = pThis->gidDefault;
    }

    RTListAppend(&pThis->ObjectHead, &pObj->Entry);
    return VINF_SUCCESS;
}


/**
 * Internal function for adding an unnamed directory.
 *
 * @returns IPRT status code.
 * @param   pThis               The ISO make instance.
 * @param   pObjInfo            Pointer to object attributes, must be set to
 *                              UNIX.  The size and hardlink counts are ignored.
 *                              Optional.
 * @param   ppDir               Where to return the directory.
 */
static int rtFsIsoMakerAddUnnamedDirWorker(PRTFSISOMAKERINT pThis, PCRTFSOBJINFO pObjInfo, PRTFSISOMAKERDIR *ppDir)
{
    PRTFSISOMAKERDIR pDir = (PRTFSISOMAKERDIR)RTMemAllocZ(sizeof(*pDir));
    AssertReturn(pDir, VERR_NO_MEMORY);
    int rc = rtFsIsoMakerInitCommonObj(pThis, &pDir->Core, RTFSISOMAKEROBJTYPE_DIR, pObjInfo);
    if (RT_SUCCESS(rc))
    {
        *ppDir = pDir;
        return VINF_SUCCESS;
    }
    RTMemFree(pDir);
    return rc;
}


/**
 * Adds an unnamed directory to the image.
 *
 * The directory must explictly be entered into the desired namespaces.
 *
 * @returns IPRT status code
 * @param   hIsoMaker           The ISO maker handle.
 * @param   pObjInfo            Pointer to object attributes, must be set to
 *                              UNIX.  The size and hardlink counts are ignored.
 *                              Optional.
 * @param   pidxObj             Where to return the configuration index of the
 *                              directory.
 * @sa      RTFsIsoMakerAddDir, RTFsIsoMakerObjSetPath
 */
RTDECL(int) RTFsIsoMakerAddUnnamedDir(RTFSISOMAKER hIsoMaker, PCRTFSOBJINFO pObjInfo, uint32_t *pidxObj)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertPtrReturn(pidxObj, VERR_INVALID_POINTER);
    if (pObjInfo)
    {
        AssertPtrReturn(pObjInfo, VERR_INVALID_POINTER);
        AssertReturn(pObjInfo->Attr.enmAdditional == RTFSOBJATTRADD_UNIX, VERR_INVALID_PARAMETER);
        AssertReturn(RTFS_IS_DIRECTORY(pObjInfo->Attr.fMode), VERR_INVALID_FLAGS);
    }
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    PRTFSISOMAKERDIR pDir;
    int rc = rtFsIsoMakerAddUnnamedDirWorker(pThis, pObjInfo, &pDir);
    *pidxObj = RT_SUCCESS(rc) ? pDir->Core.idxObj : UINT32_MAX;
    return rc;
}


/**
 * Adds a directory to the image in all namespaces and default attributes.
 *
 * @returns IPRT status code
 * @param   hIsoMaker           The ISO maker handle.
 * @param   pszDir              The path (UTF-8) to the directory in the ISO.
 *
 * @param   pidxObj             Where to return the configuration index of the
 *                              directory.  Optional.
 * @sa      RTFsIsoMakerAddUnnamedDir, RTFsIsoMakerObjSetPath
 */
RTDECL(int) RTFsIsoMakerAddDir(RTFSISOMAKER hIsoMaker, const char *pszDir, uint32_t *pidxObj)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertPtrReturn(pszDir, VERR_INVALID_POINTER);
    AssertReturn(RTPATH_IS_SLASH(*pszDir), VERR_INVALID_NAME);

    uint32_t idxObj;
    int rc = RTFsIsoMakerAddUnnamedDir(hIsoMaker, NULL /*pObjInfo*/, &idxObj);
    if (RT_SUCCESS(rc))
    {
        rc = RTFsIsoMakerObjSetPath(hIsoMaker, idxObj, RTFSISOMAKER_NAMESPACE_ALL, pszDir);
        if (RT_SUCCESS(rc))
        {
            if (pidxObj)
                *pidxObj = idxObj;
        }
        else
            RTFsIsoMakerObjRemove(hIsoMaker, idxObj);
    }
    return rc;
}


/**
 * Internal function for adding an unnamed file.
 *
 * @returns IPRT status code.
 * @param   pThis               The ISO make instance.
 * @param   pObjInfo            Object information.  Optional.
 * @param   cbExtra             Extra space for additional data (e.g. source
 *                              path string copy).
 * @param   ppFile              Where to return the file.
 */
static int rtFsIsoMakerAddUnnamedFileWorker(PRTFSISOMAKERINT pThis, PCRTFSOBJINFO pObjInfo, size_t cbExtra,
                                            PRTFSISOMAKERFILE *ppFile)
{
    PRTFSISOMAKERFILE pFile = (PRTFSISOMAKERFILE)RTMemAllocZ(sizeof(*pFile) + cbExtra);
    AssertReturn(pFile, VERR_NO_MEMORY);
    int rc = rtFsIsoMakerInitCommonObj(pThis, &pFile->Core, RTFSISOMAKEROBJTYPE_FILE, pObjInfo);
    if (RT_SUCCESS(rc))
    {
        pFile->cbData                   = pObjInfo ? pObjInfo->cbObject : 0;
        pThis->cbData                  += RT_ALIGN_64(pFile->cbData, RTFSISOMAKER_SECTOR_SIZE);
        pFile->RangeNode.Core.Key       = UINT64_MAX;
        pFile->RangeNode.Core.KeyLast   = UINT64_MAX;
        pFile->enmSrcType               = RTFSISOMAKERSRCTYPE_INVALID;
        pFile->u.pszSrcPath             = NULL;
        pFile->pBootInfoTable           = NULL;
        RTListInit(&pFile->FinalizedEntry);

        *ppFile = pFile;
        return VINF_SUCCESS;
    }
    RTMemFree(pFile);
    return rc;

}


/**
 * Adds an unnamed file to the image that's backed by a host file.
 *
 * The file must explictly be entered into the desired namespaces.
 *
 * @returns IPRT status code
 * @param   hIsoMaker           The ISO maker handle.
 * @param   pszSrcFile          The source file path.  VFS chain spec allowed.
 * @param   pidxObj             Where to return the configuration index of the
 *                              directory.
 * @sa      RTFsIsoMakerAddFile, RTFsIsoMakerObjSetPath
 */
RTDECL(int) RTFsIsoMakerAddUnnamedFileWithSrcPath(RTFSISOMAKER hIsoMaker, const char *pszSrcFile, uint32_t *pidxObj)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertPtrReturn(pidxObj, VERR_INVALID_POINTER);
    *pidxObj = UINT32_MAX;
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    /*
     * Check that the source file exists and is a file.
     */
    uint32_t    offError = 0;
    RTFSOBJINFO ObjInfo;
    int rc = RTVfsChainQueryInfo(pszSrcFile, &ObjInfo, RTFSOBJATTRADD_UNIX, RTPATH_F_FOLLOW_LINK, &offError, NULL);
    AssertMsgRCReturn(rc, ("%s -> %Rrc offError=%u\n", pszSrcFile, rc, offError), rc);
    AssertMsgReturn(RTFS_IS_FILE(ObjInfo.Attr.fMode), ("%#x - %s\n", ObjInfo.Attr.fMode, pszSrcFile), VERR_NOT_A_FILE);

    /*
     * Create a file object for it.
     */
    size_t const      cbSrcFile = strlen(pszSrcFile) + 1;
    PRTFSISOMAKERFILE pFile;
    rc = rtFsIsoMakerAddUnnamedFileWorker(pThis, &ObjInfo, cbSrcFile, &pFile);
    if (RT_SUCCESS(rc))
    {
        pFile->enmSrcType   = RTFSISOMAKERSRCTYPE_PATH;
        pFile->u.pszSrcPath = (char *)memcpy(pFile + 1, pszSrcFile, cbSrcFile);

        *pidxObj = pFile->Core.idxObj;
    }
    return rc;
}


/**
 * Adds an unnamed file to the image that's backed by a VFS file.
 *
 * The file must explictly be entered into the desired namespaces.
 *
 * @returns IPRT status code
 * @param   hIsoMaker           The ISO maker handle.
 * @param   hVfsFileSrc         The source file handle.
 * @param   pidxObj             Where to return the configuration index of the
 *                              directory.
 * @sa      RTFsIsoMakerAddUnnamedFileWithSrcPath, RTFsIsoMakerObjSetPath
 */
RTDECL(int) RTFsIsoMakerAddUnnamedFileWithVfsFile(RTFSISOMAKER hIsoMaker, RTVFSFILE hVfsFileSrc, uint32_t *pidxObj)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertPtrReturn(pidxObj, VERR_INVALID_POINTER);
    *pidxObj = UINT32_MAX;
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    /*
     * Get the VFS file info.  This implicitly validates the handle.
     */
    RTFSOBJINFO ObjInfo;
    int rc = RTVfsFileQueryInfo(hVfsFileSrc, &ObjInfo, RTFSOBJATTRADD_UNIX);
    AssertMsgRCReturn(rc, ("RTVfsFileQueryInfo(%p) -> %Rrc\n", hVfsFileSrc, rc), rc);

    /*
     * Retain a reference to the file.
     */
    uint32_t cRefs = RTVfsFileRetain(hVfsFileSrc);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Create a file object for it.
     */
    PRTFSISOMAKERFILE pFile;
    rc = rtFsIsoMakerAddUnnamedFileWorker(pThis, &ObjInfo, 0, &pFile);
    if (RT_SUCCESS(rc))
    {
        pFile->enmSrcType   = RTFSISOMAKERSRCTYPE_VFS_FILE;
        pFile->u.hVfsFile   = hVfsFileSrc;

        *pidxObj = pFile->Core.idxObj;
    }
    else
        RTVfsFileRelease(hVfsFileSrc);
    return rc;
}


/**
 * Adds an unnamed file to the image that's backed by a portion of a common
 * source file.
 *
 * The file must explictly be entered into the desired namespaces.
 *
 * @returns IPRT status code
 * @param   hIsoMaker           The ISO maker handle.
 * @param   idxCommonSrc        The common source file index.
 * @param   offData             The offset of the data in the source file.
 *                              Ignored if @a cbData is zero.
 * @param   cbData              The file size.
 * @param   pObjInfo            Pointer to file info.  Optional.
 * @param   pidxObj             Where to return the configuration index of the
 *                              directory.
 * @sa      RTFsIsoMakerAddUnnamedFileWithSrcPath, RTFsIsoMakerObjSetPath
 */
RTDECL(int) RTFsIsoMakerAddUnnamedFileWithCommonSrc(RTFSISOMAKER hIsoMaker, uint32_t idxCommonSrc,
                                                    uint64_t offData, uint64_t cbData, PCRTFSOBJINFO pObjInfo, uint32_t *pidxObj)
{
    /*
     * Validate and fake input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertPtrReturn(pidxObj, VERR_INVALID_POINTER);
    *pidxObj = UINT32_MAX;
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);
    AssertReturn(idxCommonSrc < pThis->cCommonSources, VERR_INVALID_PARAMETER);
    AssertReturn(cbData < (uint64_t)RTFOFF_MAX, VERR_OUT_OF_RANGE);
    if (cbData)
    {
        AssertReturn(offData < (uint64_t)RTFOFF_MAX, VERR_OUT_OF_RANGE);
        AssertReturn(offData + cbData < (uint64_t)RTFOFF_MAX, VERR_OUT_OF_RANGE);
    }
    RTFSOBJINFO ObjInfo;
    if (!pObjInfo)
    {
        ObjInfo.cbObject            = cbData;
        ObjInfo.cbAllocated         = cbData;
        ObjInfo.BirthTime           = pThis->ImageCreationTime;
        ObjInfo.ChangeTime          = pThis->ImageCreationTime;
        ObjInfo.ModificationTime    = pThis->ImageCreationTime;
        ObjInfo.AccessTime          = pThis->ImageCreationTime;
        ObjInfo.Attr.fMode          = pThis->fDefaultFileMode;
        ObjInfo.Attr.enmAdditional  = RTFSOBJATTRADD_UNIX;
        ObjInfo.Attr.u.Unix.uid             = NIL_RTUID;
        ObjInfo.Attr.u.Unix.gid             = NIL_RTGID;
        ObjInfo.Attr.u.Unix.cHardlinks      = 1;
        ObjInfo.Attr.u.Unix.INodeIdDevice   = 0;
        ObjInfo.Attr.u.Unix.INodeId         = 0;
        ObjInfo.Attr.u.Unix.fFlags          = 0;
        ObjInfo.Attr.u.Unix.GenerationId    = 0;
        ObjInfo.Attr.u.Unix.Device          = 0;
        pObjInfo = &ObjInfo;
    }
    else
    {
        AssertPtrReturn(pObjInfo, VERR_INVALID_POINTER);
        AssertReturn(pObjInfo->Attr.enmAdditional == RTFSOBJATTRADD_UNIX, VERR_WRONG_TYPE);
        AssertReturn((uint64_t)pObjInfo->cbObject == cbData, VERR_INVALID_PARAMETER);
    }

    /*
     * Create a file object for it.
     */
    PRTFSISOMAKERFILE pFile;
    int rc = rtFsIsoMakerAddUnnamedFileWorker(pThis, pObjInfo, 0, &pFile);
    if (RT_SUCCESS(rc))
    {
        pFile->enmSrcType       = RTFSISOMAKERSRCTYPE_COMMON;
        pFile->u.Common.idxSrc  = idxCommonSrc;
        pFile->u.Common.offData = cbData ? offData : UINT64_MAX;

        *pidxObj = pFile->Core.idxObj;
    }
    return rc;
}


/**
 * Adds a common source file.
 *
 * Using RTFsIsoMakerAddUnnamedFileWithCommonSrc a sections common source file
 * can be referenced to make up other files.  The typical use case is when
 * importing data from an existing ISO.
 *
 * @returns IPRT status code
 * @param   hIsoMaker           The ISO maker handle.
 * @param   hVfsFile            VFS handle of the common source.  (A reference
 *                              is added, none consumed.)
 * @param   pidxCommonSrc       Where to return the assigned common source
 *                              index.  This is used to reference the file.
 * @sa      RTFsIsoMakerAddUnnamedFileWithCommonSrc
 */
RTDECL(int) RTFsIsoMakerAddCommonSourceFile(RTFSISOMAKER hIsoMaker, RTVFSFILE hVfsFile, uint32_t *pidxCommonSrc)
{
    /*
     * Validate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertPtrReturn(pidxCommonSrc, VERR_INVALID_POINTER);
    *pidxCommonSrc = UINT32_MAX;
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    /*
     * Resize the common source array if necessary.
     */
    if ((pThis->cCommonSources & 15) == 0)
    {
        void *pvNew = RTMemRealloc(pThis->paCommonSources, (pThis->cCommonSources + 16) * sizeof(pThis->paCommonSources[0]));
        AssertReturn(pvNew, VERR_NO_MEMORY);
        pThis->paCommonSources = (PRTVFSFILE)pvNew;
    }

    /*
     * Retain a reference to the source file, thereby validating the handle.
     * Then add it to the array.
     */
    uint32_t cRefs = RTVfsFileRetain(hVfsFile);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    uint32_t idx = pThis->cCommonSources++;
    pThis->paCommonSources[idx] = hVfsFile;

    *pidxCommonSrc = idx;
    return VINF_SUCCESS;
}


/**
 * Adds a file that's backed by a host file to the image in all namespaces and
 * with attributes taken from the source file.
 *
 * @returns IPRT status code
 * @param   hIsoMaker       The ISO maker handle.
 * @param   pszFile         The path to the file in the image.
 * @param   pszSrcFile      The source file path.  VFS chain spec allowed.
 * @param   pidxObj         Where to return the configuration index of the file.
 *                          Optional
 * @sa      RTFsIsoMakerAddFileWithVfsFile,
 *          RTFsIsoMakerAddUnnamedFileWithSrcPath
 */
RTDECL(int) RTFsIsoMakerAddFileWithSrcPath(RTFSISOMAKER hIsoMaker, const char *pszFile, const char *pszSrcFile, uint32_t *pidxObj)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertPtrReturn(pszFile, VERR_INVALID_POINTER);
    AssertReturn(RTPATH_IS_SLASH(*pszFile), VERR_INVALID_NAME);

    uint32_t idxObj;
    int rc = RTFsIsoMakerAddUnnamedFileWithSrcPath(hIsoMaker, pszSrcFile, &idxObj);
    if (RT_SUCCESS(rc))
    {
        rc = RTFsIsoMakerObjSetPath(hIsoMaker, idxObj, RTFSISOMAKER_NAMESPACE_ALL, pszFile);
        if (RT_SUCCESS(rc))
        {
            if (pidxObj)
                *pidxObj = idxObj;
        }
        else
            RTFsIsoMakerObjRemove(hIsoMaker, idxObj);
    }
    return rc;
}


/**
 * Adds a file that's backed by a VFS file to the image in all namespaces and
 * with attributes taken from the source file.
 *
 * @returns IPRT status code
 * @param   hIsoMaker       The ISO maker handle.
 * @param   pszFile         The path to the file in the image.
 * @param   hVfsFileSrc     The source file handle.
 * @param   pidxObj         Where to return the configuration index of the file.
 *                          Optional.
 * @sa      RTFsIsoMakerAddUnnamedFileWithVfsFile,
 *          RTFsIsoMakerAddFileWithSrcPath
 */
RTDECL(int) RTFsIsoMakerAddFileWithVfsFile(RTFSISOMAKER hIsoMaker, const char *pszFile, RTVFSFILE hVfsFileSrc, uint32_t *pidxObj)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertPtrReturn(pszFile, VERR_INVALID_POINTER);
    AssertReturn(RTPATH_IS_SLASH(*pszFile), VERR_INVALID_NAME);

    uint32_t idxObj;
    int rc = RTFsIsoMakerAddUnnamedFileWithVfsFile(hIsoMaker, hVfsFileSrc, &idxObj);
    if (RT_SUCCESS(rc))
    {
        rc = RTFsIsoMakerObjSetPath(hIsoMaker, idxObj, RTFSISOMAKER_NAMESPACE_ALL, pszFile);
        if (RT_SUCCESS(rc))
        {
            if (pidxObj)
                *pidxObj = idxObj;
        }
        else
            RTFsIsoMakerObjRemove(hIsoMaker, idxObj);
    }
    return rc;
}


/**
 * Adds an unnamed symbolic link to the image.
 *
 * The symlink must explictly be entered into the desired namespaces.  Please
 * note that it is not possible to enter a symbolic link into an ISO 9660
 * namespace where rock ridge extensions are disabled, since symbolic links
 * depend on rock ridge.  For HFS and UDF there is no such requirement.
 *
 * Will fail if no namespace is configured that supports symlinks.
 *
 * @returns IPRT status code
 * @retval  VERR_ISOMK_SYMLINK_SUPPORT_DISABLED if not supported.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   pObjInfo            Pointer to object attributes, must be set to
 *                              UNIX.  The size and hardlink counts are ignored.
 *                              Optional.
 * @param   pszTarget           The symbolic link target (UTF-8).
 * @param   pidxObj             Where to return the configuration index of the
 *                              directory.
 * @sa      RTFsIsoMakerAddSymlink, RTFsIsoMakerObjSetPath
 */
RTDECL(int) RTFsIsoMakerAddUnnamedSymlink(RTFSISOMAKER hIsoMaker, PCRTFSOBJINFO pObjInfo, const char *pszTarget, uint32_t *pidxObj)
{
    /*
     * Validate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertPtrReturn(pidxObj, VERR_INVALID_POINTER);
    if (pObjInfo)
    {
        AssertPtrReturn(pObjInfo, VERR_INVALID_POINTER);
        AssertReturn(pObjInfo->Attr.enmAdditional == RTFSOBJATTRADD_UNIX, VERR_INVALID_PARAMETER);
        AssertReturn(RTFS_IS_SYMLINK(pObjInfo->Attr.fMode), VERR_INVALID_FLAGS);
    }
    AssertPtrReturn(pszTarget, VERR_INVALID_POINTER);
    size_t cchTarget = strlen(pszTarget);
    AssertReturn(cchTarget > 0, VERR_INVALID_NAME);
    AssertReturn(cchTarget < RTFSISOMAKER_MAX_SYMLINK_TARGET_LEN, VERR_FILENAME_TOO_LONG);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    /*
     * Check that symlinks are supported by some namespace.
     */
    AssertReturn(   (pThis->PrimaryIso.uLevel > 0 && pThis->PrimaryIso.uRockRidgeLevel > 0)
                 || (pThis->Joliet.uLevel     > 0 && pThis->Joliet.uRockRidgeLevel     > 0)
                 || pThis->Udf.uLevel > 0
                 || pThis->Hfs.uLevel > 0,
                 VERR_ISOMK_SYMLINK_SUPPORT_DISABLED);

    /*
     * Calculate the size of the SL entries.
     */
    uint8_t abTmp[_2K + RTFSISOMAKER_MAX_SYMLINK_TARGET_LEN * 3];
    ssize_t cbSlRockRidge = rtFsIsoMakerOutFile_RockRidgeGenSL(pszTarget, abTmp, sizeof(abTmp));
    AssertReturn(cbSlRockRidge > 0, (int)cbSlRockRidge);

    /*
     * Do the adding.
     */
    PRTFSISOMAKERSYMLINK pSymlink = (PRTFSISOMAKERSYMLINK)RTMemAllocZ(RT_UOFFSETOF_DYN(RTFSISOMAKERSYMLINK, szTarget[cchTarget + 1]));
    AssertReturn(pSymlink, VERR_NO_MEMORY);
    int rc = rtFsIsoMakerInitCommonObj(pThis, &pSymlink->Core, RTFSISOMAKEROBJTYPE_SYMLINK, pObjInfo);
    if (RT_SUCCESS(rc))
    {
        pSymlink->cchTarget     = (uint16_t)cchTarget;
        pSymlink->cbSlRockRidge = (uint16_t)cbSlRockRidge;
        memcpy(pSymlink->szTarget, pszTarget, cchTarget);
        pSymlink->szTarget[cchTarget] = '\0';

        *pidxObj = pSymlink->Core.idxObj;
        return VINF_SUCCESS;
    }
    RTMemFree(pSymlink);
    return rc;
}


/**
 * Adds a directory to the image in all namespaces and default attributes.
 *
 * Will fail if no namespace is configured that supports symlinks.
 *
 * @returns IPRT status code
 * @param   hIsoMaker           The ISO maker handle.
 * @param   pszSymlink          The path (UTF-8) to the symlink in the ISO.
 * @param   pszTarget           The symlink target (UTF-8).
 * @param   pidxObj             Where to return the configuration index of the
 *                              directory.  Optional.
 * @sa      RTFsIsoMakerAddUnnamedSymlink, RTFsIsoMakerObjSetPath
 */
RTDECL(int) RTFsIsoMakerAddSymlink(RTFSISOMAKER hIsoMaker, const char *pszSymlink, const char *pszTarget, uint32_t *pidxObj)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertPtrReturn(pszSymlink, VERR_INVALID_POINTER);
    AssertReturn(RTPATH_IS_SLASH(*pszSymlink), VERR_INVALID_NAME);

    uint32_t idxObj;
    int rc = RTFsIsoMakerAddUnnamedSymlink(hIsoMaker, NULL /*pObjInfo*/, pszTarget, &idxObj);
    if (RT_SUCCESS(rc))
    {
        rc = RTFsIsoMakerObjSetPath(hIsoMaker, idxObj, RTFSISOMAKER_NAMESPACE_ALL, pszSymlink);
        if (RT_SUCCESS(rc))
        {
            if (pidxObj)
                *pidxObj = idxObj;
        }
        else
            RTFsIsoMakerObjRemove(hIsoMaker, idxObj);
    }
    return rc;

}



/*
 *
 * Name space level object config.
 * Name space level object config.
 * Name space level object config.
 *
 */

/**
 * Worker for RTFsIsoMakerSetPathInfo and RTFsIsoMakerSetPathInfoById.
 */
static void rtFsIsoMakerSetPathInfoWorker(PRTFSISOMAKERNAME pName, PRTFSISOMAKEROBJ pObj, PCRTFSOBJINFO pObjInfo, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    pName->fMode = (pName->fMode         &  (RTFS_TYPE_MASK | RTFS_DOS_DIRECTORY))
                 | (pObjInfo->Attr.fMode & ~(RTFS_TYPE_MASK | RTFS_DOS_DIRECTORY));

    if (pObjInfo->Attr.enmAdditional == RTFSOBJATTRADD_UNIX)
    {
        if (pObjInfo->Attr.u.Unix.uid != NIL_RTUID)
            pName->uid = pObjInfo->Attr.u.Unix.uid;
        if (pObjInfo->Attr.u.Unix.gid != NIL_RTGID)
            pName->gid = pObjInfo->Attr.u.Unix.gid;
        if (pObjInfo->Attr.u.Unix.Device != 0 && ~(RTDEV)0)
            pName->Device = pObjInfo->Attr.u.Unix.Device;
    }

    if (RTTimeSpecGetNano(&pObjInfo->BirthTime) != 0)
        pObj->BirthTime        = pObjInfo->BirthTime;
    if (RTTimeSpecGetNano(&pObjInfo->ChangeTime) != 0)
        pObj->ChangeTime       = pObjInfo->ChangeTime;
    if (RTTimeSpecGetNano(&pObjInfo->ModificationTime) != 0)
        pObj->ModificationTime = pObjInfo->ModificationTime;
    if (RTTimeSpecGetNano(&pObjInfo->AccessTime) != 0)
        pObj->AccessedTime     = pObjInfo->AccessTime;
}


/**
 * Modifies the object info for a given path in one or more namespaces.
 *
 * The timestamps are applied to the common object information.
 *
 * @returns IPRT status code.
 * @retval  VWRN_NOT_FOUND if the object wasn't found in any of the specified
 *          namespaces.
 *
 * @param   hIsoMaker           The ISO maker handler.
 * @param   pszPath             The path which mode mask should be modified.
 * @param   fNamespaces         The namespaces to set it in.
 * @param   pObjInfo            The object info to set.  Several fields and
 *                              sub-fields will be ignore, like cbObject and
 *                              filte type.
 * @param   fFlags              Reserved, MBZ.
 * @param   pcHits              Where to return number of paths found. Optional.
 * @sa      RTFsIsoMakerSetPathInfoByObj, RTFsIsoMakerSetPathInfoByParentObj
 */
RTDECL(int) RTFsIsoMakerSetPathInfo(RTFSISOMAKER hIsoMaker, const char *pszPath, uint32_t fNamespaces,
                                    PCRTFSOBJINFO pObjInfo, uint32_t fFlags, uint32_t *pcHits)
{
    /*
     * Validate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(RTPATH_IS_SLASH(*pszPath), VERR_INVALID_NAME);
    AssertReturn(!(fNamespaces & ~RTFSISOMAKER_NAMESPACE_VALID_MASK), VERR_INVALID_FLAGS);
    AssertPtrReturn(pObjInfo, VERR_INVALID_POINTER);
    AssertReturn(pObjInfo->Attr.enmAdditional >= RTFSOBJATTRADD_NOTHING && pObjInfo->Attr.enmAdditional <= RTFSOBJATTRADD_LAST,
                 VERR_INVALID_PARAMETER);
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);
    AssertPtrNullReturn(pcHits, VERR_INVALID_POINTER);

    /*
     * Make the changes namespace by namespace.
     */
    uint32_t cHits = 0;
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aRTFsIsoNamespaces); i++)
        if (fNamespaces & g_aRTFsIsoNamespaces[i].fNamespace)
        {
            PRTFSISOMAKERNAMESPACE pNamespace = (PRTFSISOMAKERNAMESPACE)((uintptr_t)pThis + g_aRTFsIsoNamespaces[i].offNamespace);
            if (pNamespace->uLevel > 0)
            {
                PRTFSISOMAKERNAME pName;
                int rc = rtFsIsoMakerWalkPathBySpec(pNamespace, pszPath, &pName);
                if (RT_SUCCESS(rc))
                {
                    rtFsIsoMakerSetPathInfoWorker(pName, pName->pObj, pObjInfo, fFlags);
                    cHits++;
                }
            }
        }

   if (pcHits)
       *pcHits = cHits;
   if (cHits > 0)
       return VINF_SUCCESS;
   return VWRN_NOT_FOUND;
}


/**
 * Modifies the object info for a object in one or more namespaces.
 *
 * The timestamps are applied to the common object information.
 *
 * @returns IPRT status code.
 * @retval  VWRN_NOT_FOUND if the path wasn't found in any of the specified
 *          namespaces.
 *
 * @param   hIsoMaker           The ISO maker handler.
 * @param   idxObj              The object configuration index of the object to
 *                              set info for.
 * @param   fNamespaces         The namespaces to set it in.
 * @param   pObjInfo            The object info to set.  Several fields and
 *                              sub-fields will be ignore, like cbObject and
 *                              filte type.
 * @param   fFlags              Reserved, MBZ.
 * @param   pcHits              Where to return number of paths found. Optional.
 * @sa      RTFsIsoMakerSetPathInfo, RTFsIsoMakerSetPathInfoByParentObj
 */
RTDECL(int) RTFsIsoMakerSetPathInfoByObj(RTFSISOMAKER hIsoMaker, uint32_t idxObj, uint32_t fNamespaces,
                                         PCRTFSOBJINFO pObjInfo, uint32_t fFlags, uint32_t *pcHits)
{
    /*
     * Validate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(!(fNamespaces & ~RTFSISOMAKER_NAMESPACE_VALID_MASK), VERR_INVALID_FLAGS);
    AssertPtrReturn(pObjInfo, VERR_INVALID_POINTER);
    AssertReturn(pObjInfo->Attr.enmAdditional >= RTFSOBJATTRADD_NOTHING && pObjInfo->Attr.enmAdditional <= RTFSOBJATTRADD_LAST,
                 VERR_INVALID_PARAMETER);
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);
    AssertPtrNullReturn(pcHits, VERR_INVALID_POINTER);
    PRTFSISOMAKEROBJ const pObj = rtFsIsoMakerIndexToObj(pThis, idxObj);
    AssertReturn(pObj, VERR_OUT_OF_RANGE);

    /*
     * Make the changes namespace by namespace.
     */
    uint32_t cHits = 0;
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aRTFsIsoNamespaces); i++)
        if (fNamespaces & g_aRTFsIsoNamespaces[i].fNamespace)
        {
            PRTFSISOMAKERNAME const pName = *(PPRTFSISOMAKERNAME)((uintptr_t)pObj + g_aRTFsIsoNamespaces[i].offName);
            if (pName)
            {
                rtFsIsoMakerSetPathInfoWorker(pName, pObj, pObjInfo, fFlags);
                cHits++;
            }
        }

   if (pcHits)
       *pcHits = cHits;
   if (cHits > 0)
       return VINF_SUCCESS;
   return VWRN_NOT_FOUND;
}


/**
 * Modifies the object info for a directory child object in one
 * or more namespaces.
 *
 * The timestamps are applied to the common object information.
 *
 * @returns IPRT status code.
 * @retval  VWRN_NOT_FOUND if the name wasn't found in any of the specified
 *          namespaces.
 *
 * @param   hIsoMaker           The ISO maker handler.
 * @param   idxParentObj        The object configuration index of the object to
 *                              set info for.
 * @param   pszName             The name of the child to be modified.
 * @param   fNamespaces         The namespaces to set it in.
 * @param   pObjInfo            The object info to set.  Several fields and
 *                              sub-fields will be ignore, like cbObject and
 *                              filte type.
 * @param   fFlags              Reserved, MBZ.
 * @param   pcHits              Where to return number of paths found. Optional.
 * @sa      RTFsIsoMakerSetPathInfo, RTFsIsoMakerSetPathInfoByObj
 */
RTDECL(int) RTFsIsoMakerSetPathInfoByParentObj(RTFSISOMAKER hIsoMaker, uint32_t idxParentObj, const char *pszName,
                                               uint32_t fNamespaces, PCRTFSOBJINFO pObjInfo, uint32_t fFlags, uint32_t *pcHits)
{
    /*
     * Validate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(!(fNamespaces & ~RTFSISOMAKER_NAMESPACE_VALID_MASK), VERR_INVALID_FLAGS);
    AssertPtrReturn(pObjInfo, VERR_INVALID_POINTER);
    AssertReturn(pObjInfo->Attr.enmAdditional >= RTFSOBJATTRADD_NOTHING && pObjInfo->Attr.enmAdditional <= RTFSOBJATTRADD_LAST,
                 VERR_INVALID_PARAMETER);
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);
    AssertPtrNullReturn(pcHits, VERR_INVALID_POINTER);
    PRTFSISOMAKEROBJ const pParentObj = rtFsIsoMakerIndexToObj(pThis, idxParentObj);
    AssertReturn(pParentObj, VERR_OUT_OF_RANGE);
    AssertReturn(pParentObj->enmType == RTFSISOMAKEROBJTYPE_DIR, VERR_NOT_A_DIRECTORY);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    size_t cchName = strlen(pszName);
    AssertReturn(cchName > 0, VERR_INVALID_NAME);
    AssertReturn(memchr(pszName, '/', cchName) == NULL, VERR_INVALID_NAME);

    /*
     * Make the changes namespace by namespace.
     */
    uint32_t cHits = 0;
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aRTFsIsoNamespaces); i++)
        if (fNamespaces & g_aRTFsIsoNamespaces[i].fNamespace)
        {
            PRTFSISOMAKERNAMESPACE const pNamespace  = (PRTFSISOMAKERNAMESPACE)(  (uintptr_t)pThis
                                                                                + g_aRTFsIsoNamespaces[i].offNamespace);
            PRTFSISOMAKERNAME const      pParentName = *rtFsIsoMakerObjGetNameForNamespace(pParentObj, pNamespace);
            if (pParentName)
            {
                PRTFSISOMAKERNAME const  pName       = rtFsIsoMakerFindEntryInDirBySpec(pParentName, pszName, cchName);
                if (pName)
                {
                    rtFsIsoMakerSetPathInfoWorker(pName, pName->pObj, pObjInfo, fFlags);
                    cHits++;
                }
            }
        }

   if (pcHits)
       *pcHits = cHits;
   if (cHits > 0)
       return VINF_SUCCESS;
   return VWRN_NOT_FOUND;
}


/**
 * Modifies the mode mask for a given path in one or more namespaces.
 *
 * The mode mask is used by rock ridge, UDF and HFS.
 *
 * @returns IPRT status code.
 * @retval  VWRN_NOT_FOUND if the path wasn't found in any of the specified
 *          namespaces.
 *
 * @param   hIsoMaker           The ISO maker handler.
 * @param   pszPath             The path which mode mask should be modified.
 * @param   fNamespaces         The namespaces to set it in.
 * @param   fSet                The mode bits to set.
 * @param   fUnset              The mode bits to clear (applied first).
 * @param   fFlags              Reserved, MBZ.
 * @param   pcHits              Where to return number of paths found. Optional.
 */
RTDECL(int) RTFsIsoMakerSetPathMode(RTFSISOMAKER hIsoMaker, const char *pszPath, uint32_t fNamespaces,
                                    RTFMODE fSet, RTFMODE fUnset, uint32_t fFlags, uint32_t *pcHits)
{
    /*
     * Validate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(RTPATH_IS_SLASH(*pszPath), VERR_INVALID_NAME);
    AssertReturn(!(fNamespaces & ~RTFSISOMAKER_NAMESPACE_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(!(fSet   & ~07777), VERR_INVALID_PARAMETER);
    AssertReturn(!(fUnset & ~07777), VERR_INVALID_PARAMETER);
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);
    AssertPtrNullReturn(pcHits, VERR_INVALID_POINTER);

    /*
     * Make the changes namespace by namespace.
     */
    uint32_t cHits = 0;
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aRTFsIsoNamespaces); i++)
        if (fNamespaces & g_aRTFsIsoNamespaces[i].fNamespace)
        {
            PRTFSISOMAKERNAMESPACE pNamespace = (PRTFSISOMAKERNAMESPACE)((uintptr_t)pThis + g_aRTFsIsoNamespaces[i].offNamespace);
            if (pNamespace->uLevel > 0)
            {
                PRTFSISOMAKERNAME pName;
                int rc = rtFsIsoMakerWalkPathBySpec(pNamespace, pszPath, &pName);
                if (RT_SUCCESS(rc))
                {
                    pName->fMode = (pName->fMode & ~fUnset) | fSet;
                    cHits++;
                }
            }
        }

   if (pcHits)
       *pcHits = cHits;
   if (cHits > 0)
       return VINF_SUCCESS;
   return VWRN_NOT_FOUND;
}


/**
 * Modifies the owner ID for a given path in one or more namespaces.
 *
 * The owner ID is used by rock ridge, UDF and HFS.
 *
 * @returns IPRT status code.
 * @retval  VWRN_NOT_FOUND if the path wasn't found in any of the specified
 *          namespaces.
 *
 * @param   hIsoMaker           The ISO maker handler.
 * @param   pszPath             The path which mode mask should be modified.
 * @param   fNamespaces         The namespaces to set it in.
 * @param   idOwner             The new owner ID to set.
 * @param   pcHits              Where to return number of paths found. Optional.
 */
RTDECL(int) RTFsIsoMakerSetPathOwnerId(RTFSISOMAKER hIsoMaker, const char *pszPath, uint32_t fNamespaces,
                                       RTUID idOwner, uint32_t *pcHits)
{
    /*
     * Validate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(RTPATH_IS_SLASH(*pszPath), VERR_INVALID_NAME);
    AssertReturn(!(fNamespaces & ~RTFSISOMAKER_NAMESPACE_VALID_MASK), VERR_INVALID_FLAGS);
    AssertPtrNullReturn(pcHits, VERR_INVALID_POINTER);

    /*
     * Make the changes namespace by namespace.
     */
    uint32_t cHits = 0;
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aRTFsIsoNamespaces); i++)
        if (fNamespaces & g_aRTFsIsoNamespaces[i].fNamespace)
        {
            PRTFSISOMAKERNAMESPACE pNamespace = (PRTFSISOMAKERNAMESPACE)((uintptr_t)pThis + g_aRTFsIsoNamespaces[i].offNamespace);
            if (pNamespace->uLevel > 0)
            {
                PRTFSISOMAKERNAME pName;
                int rc = rtFsIsoMakerWalkPathBySpec(pNamespace, pszPath, &pName);
                if (RT_SUCCESS(rc))
                {
                    pName->uid = idOwner;
                    cHits++;
                }
            }
        }

   if (pcHits)
       *pcHits = cHits;
   if (cHits > 0)
       return VINF_SUCCESS;
   return VWRN_NOT_FOUND;
}


/**
 * Modifies the group ID for a given path in one or more namespaces.
 *
 * The group ID is used by rock ridge, UDF and HFS.
 *
 * @returns IPRT status code.
 * @retval  VWRN_NOT_FOUND if the path wasn't found in any of the specified
 *          namespaces.
 *
 * @param   hIsoMaker           The ISO maker handler.
 * @param   pszPath             The path which mode mask should be modified.
 * @param   fNamespaces         The namespaces to set it in.
 * @param   idGroup             The new group ID to set.
 * @param   pcHits              Where to return number of paths found. Optional.
 */
RTDECL(int) RTFsIsoMakerSetPathGroupId(RTFSISOMAKER hIsoMaker, const char *pszPath, uint32_t fNamespaces,
                                       RTGID idGroup, uint32_t *pcHits)
{
    /*
     * Validate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(RTPATH_IS_SLASH(*pszPath), VERR_INVALID_NAME);
    AssertReturn(!(fNamespaces & ~RTFSISOMAKER_NAMESPACE_VALID_MASK), VERR_INVALID_FLAGS);
    AssertPtrNullReturn(pcHits, VERR_INVALID_POINTER);

    /*
     * Make the changes namespace by namespace.
     */
    uint32_t cHits = 0;
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aRTFsIsoNamespaces); i++)
        if (fNamespaces & g_aRTFsIsoNamespaces[i].fNamespace)
        {
            PRTFSISOMAKERNAMESPACE pNamespace = (PRTFSISOMAKERNAMESPACE)((uintptr_t)pThis + g_aRTFsIsoNamespaces[i].offNamespace);
            if (pNamespace->uLevel > 0)
            {
                PRTFSISOMAKERNAME pName;
                int rc = rtFsIsoMakerWalkPathBySpec(pNamespace, pszPath, &pName);
                if (RT_SUCCESS(rc))
                {
                    pName->gid = idGroup;
                    cHits++;
                }
            }
        }

   if (pcHits)
       *pcHits = cHits;
   if (cHits > 0)
       return VINF_SUCCESS;
   return VWRN_NOT_FOUND;
}


/**
 * Rename an object in one or more namespaces.
 *
 * The object must already be entered into the namespaces by
 * RTFsIsoMakerObjSetNameAndParent, RTFsIsoMakerObjSetPath or similar.
 *
 * @returns IPRT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   fNamespaces         The namespaces to apply the path to
 *                              (RTFSISOMAKER_NAMESPACE_XXX).
 * @param   pszFrom             The name of the object to be renamed.
 * @param   pszTo               The new name of the object.
 */
RTDECL(int) RTFsIsoMakerRename(RTFSISOMAKER hIsoMaker, uint32_t fNamespaces, const char *pszFrom, const char *pszTo)
{
    /*
     * Validate the input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);
    AssertReturn(pszFrom[0] != '\0', VERR_INVALID_NAME);
    AssertReturn(pszTo[0] != '\0', VERR_INVALID_NAME);

    /*
     * Isolate the target filename from the target directory.
     */
    const char * const pszToName     = RTPathFilename(pszTo);
    const char * const pszToNameReal = pszToName ? pszToName : RTPathFilename(pszFrom);
    AssertReturn(pszToNameReal && *pszToNameReal, VERR_INVALID_NAME);
    size_t const       cchToNameReal = strlen(pszToNameReal);
    AssertReturn(cchToNameReal > 2 || (strcmp(pszToNameReal, ".") != 0 && strcmp(pszToNameReal, "..") != 0), VERR_INVALID_NAME);

    size_t             cchToParent   = !pszToName ? strlen(pszTo) : (size_t)(pszToName - pszTo);
    while (cchToParent > 0 && RTPATH_IS_SLASH(pszTo[cchToParent - 1]))
        cchToParent--;
    char szToParent[RTPATH_MAX];
    if (cchToParent >= sizeof(szToParent))
        return VERR_FILENAME_TOO_LONG;
    if (cchToParent)
        memcpy(szToParent, pszTo, cchToParent);
    else
        szToParent[cchToParent++] = '/';
    szToParent[cchToParent] = '\0';

    /*
     * Lookup the from name & parent directory as well as the target directories in each namespace.
     */
    struct
    {
        PRTFSISOMAKERNAME    pFromName;
        PRTFSISOMAKERNAMEDIR pToDir;
        PRTFSISOMAKERNAME    pNewName;
        size_t               cchName;
        size_t               cbNameInDirRec;
        char                 szName[RTFSISOMAKER_MAX_NAME_BUF];
    }           aData[RT_ELEMENTS(g_aRTFsIsoNamespaces)];
    uint32_t    cFound = 0;

    for (uint32_t i = 0; i < RT_ELEMENTS(g_aRTFsIsoNamespaces); i++)
    {
        aData[i].pFromName      = NULL;
        aData[i].pToDir         = NULL;
        aData[i].pNewName       = NULL;
        aData[i].cchName        = 0;
        aData[i].cbNameInDirRec = 0;
        aData[i].szName[0]      = '\0';

        if (fNamespaces & g_aRTFsIsoNamespaces[i].fNamespace)
        {
            PRTFSISOMAKERNAMESPACE pNamespace = (PRTFSISOMAKERNAMESPACE)((uintptr_t)pThis + g_aRTFsIsoNamespaces[i].offNamespace);
            if (pNamespace->pRoot)
            {
                /* The source. */
                PRTFSISOMAKERNAME pFromName;
                int rc = rtFsIsoMakerWalkPathBySpec(pNamespace, pszFrom, &pFromName);
                if (RT_FAILURE(rc))
                    return rc;
                aData[i].pFromName = pFromName;
                AssertReturn(pFromName != pNamespace->pRoot, VERR_INVALID_PARAMETER);

                /* The target parent. */
                PRTFSISOMAKERNAME pToParentName;
                rc = rtFsIsoMakerWalkPathBySpec(pNamespace, szToParent, &pToParentName);
                if (RT_FAILURE(rc))
                    return rc;
                aData[i].pToDir = pToParentName->pDir;
                if (!pToParentName->pDir)
                    return VERR_NOT_A_DIRECTORY;

                /* If pszFrom names a directory, make sure pszTo is _not_ a subdirectory under it. */
                if (pFromName->pDir)
                    for (PRTFSISOMAKERNAME pToAncestorName = pToParentName;
                         pToAncestorName;
                         pToAncestorName = pToAncestorName->pParent)
                        AssertReturn(pToAncestorName != pFromName, VERR_ACCESS_DENIED);

                /* Normalize the name. This also checks for duplicates. */
                rc = rtFsIsoMakerNormalizeNameForNamespace(pThis, pNamespace, pToParentName, pszToNameReal, cchToNameReal,
                                                           false /*fNoNormalize*/, pFromName->pDir != NULL,
                                                           aData[i].szName, sizeof(aData[i].szName),
                                                           &aData[i].cchName, &aData[i].cbNameInDirRec);
                if (RT_FAILURE(rc))
                    return rc;

                cFound++;
            }
        }
    }
    if (!cFound)
        return VERR_NOT_FOUND;

    /*
     * Allocate necessary memory and prep new name nodes.
     */
    int rc = VINF_SUCCESS;
    for (uint32_t i = 0; i < RT_ELEMENTS(aData); i++)
    {
        PRTFSISOMAKERNAME const pFromName = aData[i].pFromName;
        if (pFromName)
        {
            rc = rtFsIsoMakerDirEnsureSpaceForAnotherEntry(pFromName->pParent->pDir);
            AssertRCBreak(rc);

            size_t const cbNewName = RT_UOFFSETOF(RTFSISOMAKERNAME, szName) + aData[i].cchName + 1 + cchToNameReal + 1;
            PRTFSISOMAKERNAME const pNewName = (PRTFSISOMAKERNAME)RTMemAllocZ(cbNewName);
            AssertBreakStmt(pNewName, rc = VERR_NO_MEMORY);
            aData[i].pNewName = pNewName;

            memcpy(pNewName, pFromName, RT_UOFFSETOF(RTFSISOMAKERNAME, szName));
            pNewName->cbNameInDirRec = (uint16_t)aData[i].cbNameInDirRec;
            pNewName->cchName        = (uint16_t)aData[i].cchName;
            memcpy(pNewName->szName, aData[i].szName, aData[i].cchName);
            pNewName->szName[aData[i].cchName] = '\0';
            pNewName->pszSpecNm = (char *)memcpy(&pNewName->szName[aData[i].cchName + 1], pszToNameReal, cchToNameReal + 1);
        }
    }

    /*
     * Do the actual renaming.
     */
    if (RT_SUCCESS(rc))
        for (uint32_t i = 0; i < RT_ELEMENTS(aData); i++)
        {
            PRTFSISOMAKERNAME const pFromName = aData[i].pFromName;
            if (pFromName)
            {
                PRTFSISOMAKERNAMESPACE const pNamespace = (PRTFSISOMAKERNAMESPACE)(  (uintptr_t)pThis
                                                                                   + g_aRTFsIsoNamespaces[i].offNamespace);

                /*
                 * Remove the it from the old directory.
                 */
                if (rtFsIsoMakerDirRemoveChild(pNamespace, pFromName->pParent->pDir, pFromName))
                {
                    pFromName->pParent = NULL;
                    RTMemFree(pFromName);

                    /*
                     * Insert it into the new directory (space check above).
                     */
                    PRTFSISOMAKERNAME const pNewName = aData[i].pNewName;
                    aData[i].pNewName = NULL;
                    rtFsIsoMakerDirInsertChild(pNamespace, aData[i].pToDir, pNewName);

                    /*
                     * Correct name pointers.
                     */
                    PPRTFSISOMAKERNAME const ppObjName = (PPRTFSISOMAKERNAME)(  (uintptr_t)pNewName->pObj
                                                                              + g_aRTFsIsoNamespaces[i].offName);
                    AssertStmt(*ppObjName == pFromName, rc = VERR_ISOMK_IPE_NAMESPACE_6);
                    *ppObjName = pNewName;

                    PRTFSISOMAKERNAMEDIR const pDir = pNewName->pDir;
                    if (pDir)
                    {
                        pDir->pName = pNewName;
                        uint32_t iChild = pDir->cChildren;
                        while (iChild-- > 0)
                            pDir->papChildren[iChild]->pParent = pNewName;
                    }
                }
                else
                    AssertFailedStmt(rc = VERR_ISOMK_IPE_NAMESPACE_6);
            }
        }

    /* Cleanup in case of failure. */
    if (RT_FAILURE(rc))
        for (uint32_t i = 0; i < RT_ELEMENTS(aData); i++)
            RTMemFree(aData[i].pNewName);
    return rc;
}






/*
 *
 * El Torito Booting.
 * El Torito Booting.
 * El Torito Booting.
 * El Torito Booting.
 *
 */

/**
 * Ensures that we've got a boot catalog file.
 *
 * @returns IPRT status code.
 * @param   pThis               The ISO maker instance.
 */
static int rtFsIsoMakerEnsureBootCatFile(PRTFSISOMAKERINT pThis)
{
    if (pThis->pBootCatFile)
        return VINF_SUCCESS;

    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    /* Create a VFS memory file for backing up the file. */
    RTVFSFILE hVfsFile;
    int rc = RTVfsMemFileCreate(NIL_RTVFSIOSTREAM, RTFSISOMAKER_SECTOR_SIZE, &hVfsFile);
    if (RT_SUCCESS(rc))
    {
        /* Create an unnamed VFS backed file and mark it as non-orphaned. */
        PRTFSISOMAKERFILE pFile;
        rc = rtFsIsoMakerAddUnnamedFileWorker(pThis, NULL, 0, &pFile);
        if (RT_SUCCESS(rc))
        {
            pFile->enmSrcType       = RTFSISOMAKERSRCTYPE_VFS_FILE;
            pFile->u.hVfsFile       = hVfsFile;
            pFile->Core.cNotOrphan  = 1;

            /* Save file pointer and allocate a volume descriptor. */
            pThis->pBootCatFile = pFile;
            pThis->cVolumeDescriptors++;

            return VINF_SUCCESS;
        }
        RTVfsFileRelease(hVfsFile);
    }
    return rc;
}


/**
 * Queries the configuration index of the boot catalog file object.
 *
 * The boot catalog file is created as necessary, thus this have to be a query
 * rather than a getter since object creation may fail.
 *
 * @returns IPRT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   pidxObj             Where to return the configuration index.
 */
RTDECL(int) RTFsIsoMakerQueryObjIdxForBootCatalog(RTFSISOMAKER hIsoMaker, uint32_t *pidxObj)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pidxObj, VERR_INVALID_POINTER);
    *pidxObj = UINT32_MAX;
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);

    /*
     * Do the job.
     */
    int rc = rtFsIsoMakerEnsureBootCatFile(pThis);
    if (RT_SUCCESS(rc))
        *pidxObj = pThis->pBootCatFile->Core.idxObj;
    return rc;
}


/**
 * Sets the boot catalog backing file.
 *
 * The content of the given file will be discarded and replaced with the boot
 * catalog, the naming and file attributes (other than size) will be retained.
 *
 * This API exists mainly to assist when importing ISOs.
 *
 * @returns IPRT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   idxObj              The configuration index of the file.
 */
RTDECL(int) RTFsIsoMakerBootCatSetFile(RTFSISOMAKER hIsoMaker, uint32_t idxObj)
{
    /*
     * Validate and translate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);

    PRTFSISOMAKEROBJ pObj = rtFsIsoMakerIndexToObj(pThis, idxObj);
    AssertReturn(pObj, VERR_OUT_OF_RANGE);
    AssertReturn(pObj->enmType == RTFSISOMAKEROBJTYPE_FILE, VERR_WRONG_TYPE);
    PRTFSISOMAKERFILE pFile = (PRTFSISOMAKERFILE)pObj;
    AssertReturn(   pFile->enmSrcType == RTFSISOMAKERSRCTYPE_PATH
                 || pFile->enmSrcType == RTFSISOMAKERSRCTYPE_COMMON
                 || pFile->enmSrcType == RTFSISOMAKERSRCTYPE_VFS_FILE,
                 VERR_WRONG_TYPE);

    /*
     * To reduce the possible combinations here, make sure there is a boot cat
     * file that we're "replacing".
     */
    int rc = rtFsIsoMakerEnsureBootCatFile(pThis);
    if (RT_SUCCESS(rc))
    {
        /*
         * Grab a reference to the boot cat memory VFS so we can destroy it
         * later using regular destructors.
         */
        PRTFSISOMAKERFILE pOldFile = pThis->pBootCatFile;
        RTVFSFILE         hVfsFile = pOldFile->u.hVfsFile;
        uint32_t          cRefs    = RTVfsFileRetain(hVfsFile);
        if (cRefs != UINT32_MAX)
        {
            /*
             * Try remove the existing boot file.
             */
            pOldFile->Core.cNotOrphan--;
            pThis->pBootCatFile = NULL;
            rc = rtFsIsoMakerObjRemoveWorker(pThis, &pOldFile->Core);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Just morph pFile into a boot catalog file.
                 */
                if (pFile->enmSrcType == RTFSISOMAKERSRCTYPE_VFS_FILE)
                {
                    RTVfsFileRelease(pFile->u.hVfsFile);
                    pFile->u.hVfsFile = NIL_RTVFSFILE;
                }

                pThis->cbData -= RT_ALIGN_64(pFile->cbData, RTFSISOMAKER_SECTOR_SIZE);
                pFile->cbData     = 0;
                pFile->Core.cNotOrphan++;
                pFile->enmSrcType = RTFSISOMAKERSRCTYPE_VFS_FILE;
                pFile->u.hVfsFile = hVfsFile;

                pThis->pBootCatFile = pFile;

                return VINF_SUCCESS;
            }

            pThis->pBootCatFile = pOldFile;
            pOldFile->Core.cNotOrphan++;
            RTVfsFileRelease(hVfsFile);
        }
        else
            rc = VERR_ISOMK_IPE_BOOT_CAT_FILE;
    }
    return rc;
}


/**
 * Set the validation entry of the boot catalog (this is the first entry).
 *
 * @returns IPRT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   idPlatform          The platform ID
 *                              (ISO9660_ELTORITO_PLATFORM_ID_XXX).
 * @param   pszString           CD/DVD-ROM identifier.  Optional.
 */
RTDECL(int) RTFsIsoMakerBootCatSetValidationEntry(RTFSISOMAKER hIsoMaker, uint8_t idPlatform, const char *pszString)
{
    /*
     * Validate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    size_t cchString = 0;
    if (pszString)
    {
        cchString = RTStrCalcLatin1Len(pszString);
        AssertReturn(cchString < RT_SIZEOFMEMB(ISO9660ELTORITOVALIDATIONENTRY, achId), VERR_OUT_OF_RANGE);
    }

    /*
     * Make sure we've got a boot file.
     */
    int rc = rtFsIsoMakerEnsureBootCatFile(pThis);
    if (RT_SUCCESS(rc))
    {
        /*
         * Construct the entry data.
         */
        ISO9660ELTORITOVALIDATIONENTRY Entry;
        Entry.bHeaderId = ISO9660_ELTORITO_HEADER_ID_VALIDATION_ENTRY;
        Entry.bPlatformId = idPlatform;
        Entry.u16Reserved = 0;
        RT_ZERO(Entry.achId);
        if (cchString)
        {
            char *pszTmp = Entry.achId;
            rc = RTStrToLatin1Ex(pszString, RTSTR_MAX, &pszTmp, sizeof(Entry.achId), NULL);
            AssertRC(rc);
        }
        Entry.u16Checksum = 0;
        Entry.bKey1 = ISO9660_ELTORITO_KEY_BYTE_1;
        Entry.bKey2 = ISO9660_ELTORITO_KEY_BYTE_2;

        /* Calc checksum. */
        uint16_t uSum = 0;
        uint16_t const *pu16Src = (uint16_t const *)&Entry;
        uint16_t        cLeft   = sizeof(Entry) / sizeof(uint16_t);
        while (cLeft-- > 0)
        {
            uSum += RT_LE2H_U16(*pu16Src);
            pu16Src++;
        }
        Entry.u16Checksum = RT_H2LE_U16((uint16_t)0 - uSum);

        /*
         * Write the entry and update our internal tracker.
         */
        rc = RTVfsFileWriteAt(pThis->pBootCatFile->u.hVfsFile, 0, &Entry, sizeof(Entry), NULL);
        if (RT_SUCCESS(rc))
        {
            pThis->aBootCatEntries[0].bType    = ISO9660_ELTORITO_HEADER_ID_VALIDATION_ENTRY;
            pThis->aBootCatEntries[0].cEntries = 2;
        }
    }
    return rc;
}


/**
 * Set the validation entry of the boot catalog (this is the first entry).
 *
 * @returns IPRT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   idxBootCat          The boot catalog entry.  Zero and two are
 *                              invalid.  Must be less than 63.
 * @param   idxImageObj         The configuration index of the boot image.
 * @param   bBootMediaType      The media type and flag (not for entry 1)
 *                              (ISO9660_ELTORITO_BOOT_MEDIA_TYPE_XXX,
 *                              ISO9660_ELTORITO_BOOT_MEDIA_F_XXX).
 * @param   bSystemType         The partitiona table system ID.
 * @param   fBootable           Whether it's a bootable entry or if we just want
 *                              the BIOS to setup the emulation without booting
 *                              it.
 * @param   uLoadSeg            The load address divided by 0x10 (i.e. the real
 *                              mode segment number).
 * @param   cSectorsToLoad      Number of emulated sectors to load.
 * @param   bSelCritType        The selection criteria type, if none pass
 *                              ISO9660_ELTORITO_SEL_CRIT_TYPE_NONE.
 * @param   pvSelCritData       Pointer to the selection criteria data.
 * @param   cbSelCritData       Size of the selection criteria data.
 */
RTDECL(int) RTFsIsoMakerBootCatSetSectionEntry(RTFSISOMAKER hIsoMaker, uint32_t idxBootCat, uint32_t idxImageObj,
                                               uint8_t bBootMediaType, uint8_t bSystemType, bool fBootable,
                                               uint16_t uLoadSeg, uint16_t cSectorsToLoad,
                                               uint8_t bSelCritType, void const *pvSelCritData, size_t cbSelCritData)
{
    /*
     * Validate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    PRTFSISOMAKERFILE pFile = (PRTFSISOMAKERFILE)rtFsIsoMakerIndexToObj(pThis, idxImageObj);
    AssertReturn(pFile, VERR_OUT_OF_RANGE);
    AssertReturn((bBootMediaType & ISO9660_ELTORITO_BOOT_MEDIA_TYPE_MASK) <= ISO9660_ELTORITO_BOOT_MEDIA_TYPE_HARD_DISK,
                 VERR_INVALID_PARAMETER);
    AssertReturn(!(bBootMediaType & ISO9660_ELTORITO_BOOT_MEDIA_F_MASK) || idxBootCat != 1,
                 VERR_INVALID_PARAMETER);

    AssertReturn(idxBootCat != 0 && idxBootCat != 2 && idxBootCat < RT_ELEMENTS(pThis->aBootCatEntries) - 1U, VERR_OUT_OF_RANGE);

    size_t cExtEntries = 0;
    if (bSelCritType == ISO9660_ELTORITO_SEL_CRIT_TYPE_NONE)
        AssertReturn(cbSelCritData == 0, VERR_INVALID_PARAMETER);
    else
    {
        AssertReturn(idxBootCat > 2, VERR_INVALID_PARAMETER);
        if (cbSelCritData > 0)
        {
            AssertPtrReturn(pvSelCritData, VERR_INVALID_POINTER);

            if (cbSelCritData <= RT_SIZEOFMEMB(ISO9660ELTORITOSECTIONENTRY, abSelectionCriteria))
                cExtEntries = 0;
            else
            {
                cExtEntries = (cbSelCritData - RT_SIZEOFMEMB(ISO9660ELTORITOSECTIONENTRY, abSelectionCriteria)
                               + RT_SIZEOFMEMB(ISO9660ELTORITOSECTIONENTRYEXT, abSelectionCriteria) - 1)
                            / RT_SIZEOFMEMB(ISO9660ELTORITOSECTIONENTRYEXT, abSelectionCriteria);
                AssertReturn(cExtEntries + 1 < RT_ELEMENTS(pThis->aBootCatEntries) - 1, VERR_TOO_MUCH_DATA);
            }
        }
    }

    /*
     * Make sure we've got a boot file.
     */
    int rc = rtFsIsoMakerEnsureBootCatFile(pThis);
    if (RT_SUCCESS(rc))
    {
        /*
         * Construct the entry.
         */
        union
        {
            ISO9660ELTORITOSECTIONENTRY     Entry;
            ISO9660ELTORITOSECTIONENTRYEXT  ExtEntry;
        } u;
        u.Entry.bBootIndicator            = fBootable ? ISO9660_ELTORITO_BOOT_INDICATOR_BOOTABLE
                                          :             ISO9660_ELTORITO_BOOT_INDICATOR_NOT_BOOTABLE;
        u.Entry.bBootMediaType            = bBootMediaType;
        u.Entry.uLoadSeg                  = RT_H2LE_U16(uLoadSeg);
        u.Entry.bSystemType               = cExtEntries == 0
                                          ? bSystemType & ~ISO9660_ELTORITO_BOOT_MEDIA_F_CONTINUATION
                                          : bSystemType | ISO9660_ELTORITO_BOOT_MEDIA_F_CONTINUATION;
        u.Entry.bUnused                   = 0;
        u.Entry.cEmulatedSectorsToLoad    = RT_H2LE_U16(cSectorsToLoad);
        u.Entry.offBootImage              = 0;
        u.Entry.bSelectionCriteriaType    = bSelCritType;
        RT_ZERO(u.Entry.abSelectionCriteria);
        if (cbSelCritData > 0)
            memcpy(u.Entry.abSelectionCriteria, pvSelCritData, RT_MIN(cbSelCritData, sizeof(u.Entry.abSelectionCriteria)));

        /*
         * Write it and update our internal tracker.
         */
        rc = RTVfsFileWriteAt(pThis->pBootCatFile->u.hVfsFile, ISO9660_ELTORITO_ENTRY_SIZE * idxBootCat,
                              &u.Entry, sizeof(u.Entry), NULL);
        if (RT_SUCCESS(rc))
        {
            if (pThis->aBootCatEntries[idxBootCat].pBootFile != pFile)
            {
                if (pThis->aBootCatEntries[idxBootCat].pBootFile)
                    pThis->aBootCatEntries[idxBootCat].pBootFile->Core.cNotOrphan--;
                pFile->Core.cNotOrphan++;
                pThis->aBootCatEntries[idxBootCat].pBootFile = pFile;
            }

            pThis->aBootCatEntries[idxBootCat].bType    = u.Entry.bBootIndicator;
            pThis->aBootCatEntries[idxBootCat].cEntries = 1;
        }

        /*
         * Do add further extension entries with selection criteria.
         */
        if (cExtEntries)
        {
            uint8_t const *pbSrc = (uint8_t const *)pvSelCritData;
            size_t         cbSrc = cbSelCritData;
            pbSrc += sizeof(u.Entry.abSelectionCriteria);
            cbSrc -= sizeof(u.Entry.abSelectionCriteria);

            while (cbSrc > 0)
            {
                u.ExtEntry.bExtensionId = ISO9660_ELTORITO_SECTION_ENTRY_EXT_ID;
                if (cbSrc > sizeof(u.ExtEntry.abSelectionCriteria))
                {
                    u.ExtEntry.fFlags = ISO9660_ELTORITO_SECTION_ENTRY_EXT_F_MORE;
                    memcpy(u.ExtEntry.abSelectionCriteria, pbSrc, sizeof(u.ExtEntry.abSelectionCriteria));
                    pbSrc += sizeof(u.ExtEntry.abSelectionCriteria);
                    cbSrc -= sizeof(u.ExtEntry.abSelectionCriteria);
                }
                else
                {
                    u.ExtEntry.fFlags = 0;
                    RT_ZERO(u.ExtEntry.abSelectionCriteria);
                    memcpy(u.ExtEntry.abSelectionCriteria, pbSrc, cbSrc);
                    cbSrc = 0;
                }

                idxBootCat++;
                rc = RTVfsFileWriteAt(pThis->pBootCatFile->u.hVfsFile, ISO9660_ELTORITO_ENTRY_SIZE * idxBootCat,
                                      &u.Entry, sizeof(u.Entry), NULL);
                if (RT_FAILURE(rc))
                    break;

                /* update the internal tracker. */
                if (pThis->aBootCatEntries[idxBootCat].pBootFile)
                {
                    pThis->aBootCatEntries[idxBootCat].pBootFile->Core.cNotOrphan--;
                    pThis->aBootCatEntries[idxBootCat].pBootFile = NULL;
                }

                pThis->aBootCatEntries[idxBootCat].bType    = ISO9660_ELTORITO_SECTION_ENTRY_EXT_ID;
                pThis->aBootCatEntries[idxBootCat].cEntries = 1;
            }
        }
    }
    return rc;
}


/**
 * Set up a section header entry of the boot catalog.
 *
 * @returns IPRT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   idxBootCat          The boot catalog entry.
 * @param   cEntries            Number of entries in the section.
 * @param   idPlatform          The platform ID
 *                              (ISO9660_ELTORITO_PLATFORM_ID_XXX).
 * @param   pszString           Section identifier or something.  Optional.
 * @param   fFinalEntry         Set if this is the final entry.
 */
RTDECL(int) RTFsIsoMakerBootCatSetSectionHeaderEntry(RTFSISOMAKER hIsoMaker, uint32_t idxBootCat, uint32_t cEntries,
                                                     uint8_t idPlatform, const char *pszString, bool fFinalEntry)
{
    /*
     * Validate input.
     */
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);

    AssertReturn(idxBootCat >= 2 && idxBootCat < RT_ELEMENTS(pThis->aBootCatEntries) - 1U, VERR_OUT_OF_RANGE);
    AssertReturn(cEntries < RT_ELEMENTS(pThis->aBootCatEntries) - 2U - 1U, VERR_OUT_OF_RANGE);
    AssertReturn(idxBootCat + cEntries + 1 < RT_ELEMENTS(pThis->aBootCatEntries), VERR_OUT_OF_RANGE);

    size_t cchString = 0;
    if (pszString)
    {
        cchString = RTStrCalcLatin1Len(pszString);
        AssertReturn(cchString < RT_SIZEOFMEMB(ISO9660ELTORITOVALIDATIONENTRY, achId), VERR_OUT_OF_RANGE);
    }

    /*
     * Make sure we've got a boot file.
     */
    int rc = rtFsIsoMakerEnsureBootCatFile(pThis);
    if (RT_SUCCESS(rc))
    {
        /*
         * Construct the entry data.
         */
        ISO9660ELTORITOSECTIONHEADER Entry;
        Entry.bHeaderId   = fFinalEntry ? ISO9660_ELTORITO_HEADER_ID_FINAL_SECTION_HEADER
                          : ISO9660_ELTORITO_HEADER_ID_SECTION_HEADER;
        Entry.bPlatformId = idPlatform;
        Entry.cEntries    = RT_H2LE_U16(cEntries);
        RT_ZERO(Entry.achSectionId);
        if (cchString)
        {
            char *pszTmp = Entry.achSectionId;
            rc = RTStrToLatin1Ex(pszString, RTSTR_MAX, &pszTmp, sizeof(Entry.achSectionId), NULL);
            AssertRC(rc);
        }

        /*
         * Write the entry and update our internal tracker.
         */
        rc = RTVfsFileWriteAt(pThis->pBootCatFile->u.hVfsFile, ISO9660_ELTORITO_ENTRY_SIZE * idxBootCat,
                              &Entry, sizeof(Entry), NULL);
        if (RT_SUCCESS(rc))
        {
            if (pThis->aBootCatEntries[idxBootCat].pBootFile != NULL)
            {
                pThis->aBootCatEntries[idxBootCat].pBootFile->Core.cNotOrphan--;
                pThis->aBootCatEntries[idxBootCat].pBootFile = NULL;
            }

            pThis->aBootCatEntries[idxBootCat].bType    = Entry.bHeaderId;
            pThis->aBootCatEntries[idxBootCat].cEntries = cEntries + 1;
        }
    }
    return rc;
}



/*
 *
 * UDF content producers.
 * UDF content producers.
 * UDF content producers.
 *
 */


/**
 * Helper for populating a UDFTAG structure, including checksum calculation.
 */
static void rtFsIsoMakerUdfPopulateTag(PRTFSISOMAKERINT pThis, PUDFTAG pTag, uint16_t idTag, size_t cbTagAndDataToCrc,
                                       uint32_t offLocation, uint16_t uTagSerialNo = 0)
{
    pTag->idTag           = idTag;
    uint16_t const uVersion = pThis->Udf.uLevel;
    pTag->uVersion        = uVersion;
    pTag->bReserved       = 0;
    pTag->uTagSerialNo    = uTagSerialNo;
    pTag->offTag          = offLocation;
    Assert(cbTagAndDataToCrc < UINT16_MAX);
    uint16_t const cbCrc  = (uint16_t)(cbTagAndDataToCrc - sizeof(*pTag));
    pTag->cbDescriptorCrc = cbCrc;
    uint16_t const uCrc   = RTCrc16Ccitt(pTag + 1, cbCrc);
    pTag->uDescriptorCrc  = uCrc;

    /* Calculate the checksum from the bits. */
    pTag->uChecksum       = ( idTag              & 0xff)
                          + ((idTag        >> 8) & 0xff)
                          + ( uVersion           & 0xff)
                          + ((uVersion     >> 8) & 0xff)
                          + ( uTagSerialNo       & 0xff)
                          + ((uTagSerialNo >> 8) & 0xff)
                          + ( cbCrc              & 0xff)
                          + ((cbCrc        >> 8) & 0xff)
                          + ( uCrc               & 0xff)
                          + ((uCrc         >> 8) & 0xff)
                          + ( offLocation        & 0xff)
                          + ((offLocation >>  8) & 0xff)
                          + ((offLocation >> 16) & 0xff)
                          + ((offLocation >> 24) & 0xff);
}


/**
 * Initializes a UDFCHARSPEC member according to the UDF specs.
 */
static void rtFsIsoMakerUdfSetCharSpec(PUDFCHARSPEC pCharSpec)
{
    static const UDFCHARSPEC s_CharSpec = { 0, "OSTA Compressed Unicode" };
    memcpy(pCharSpec, &s_CharSpec, sizeof(*pCharSpec));
}


/**
 * Encodes a dstring field to @a pszSrc.
 *
 * @returns IPRT status code.
 * @param   pachDst     Pointer to the dstring field.
 * @param   cchDst      Size of the dstring field.
 * @param   pszSrc      The source string (UTF-8) to put there.  This will be
 *                      quietly truncated.
 */
static int rtFsIsoMakerUdfEncodeDString(PUDFDSTRING pachDst, size_t cchDst, const char *pszSrc)
{
    Assert(cchDst >= 4);
    Assert(cchDst <= 1 + 255 + 1);

    /*
     * Examine the source to see if there are any unicode code points above 255:
     */
    size_t      cCodepoints = 0;
    size_t      cAbove127   = 0;
    size_t      cAbove255   = 0;
    const char *pszCursor   = pszSrc;
    for (;;)
    {
        RTUNICP uc;
        int rc = RTStrGetCpEx(&pszCursor, &uc);
        AssertRCReturn(rc, rc);
        if (uc <= 127)
        {
            if (uc > 0)
                cCodepoints++;
            else
                break;
        }
        else
        {
            cAbove127++;
            if (uc > 255)
                cAbove255++;
            cCodepoints++;
        }
    }

    /*
     * If no codepoints above 255, encode it as 8-bit.
     */
    if (cAbove255 == 0)
    {
        size_t const cchMax = RT_MIN(cchDst - 2, cCodepoints);
        pachDst[0] = 8;
        if (cAbove127 == 0)
            memcpy(&pachDst[1], pszSrc, cCodepoints);
        else
        {
            pszCursor = pszSrc;
            for (size_t off = 1; off <= cchMax; off++)
            {
                RTUNICP uc;
                int rc = RTStrGetCpEx(&pszCursor, &uc);
                AssertRCReturn(rc, rc);
                AssertReturn(uc < 256, VERR_INTERNAL_ERROR_3); /* paranoia^2 */
                pachDst[off] = (char)uc;
            }
        }
        if (cchMax < cchDst - 2)
            RT_BZERO(&pachDst[1 + cchMax], cchDst - 2 - cchMax);
        pachDst[cchDst - 1] = (uint8_t)(1 + cchMax);
        return cCodepoints <= cchMax ? VINF_SUCCESS : VINF_BUFFER_OVERFLOW;
    }

    /*
     * Otherwise encode it as big-endian UTF-16.
     */
    pachDst[0] = 16;
    size_t   cwcMax  = (cchDst - 2) / sizeof(RTUTF16);
    size_t   cwcDst  = 0;
    PRTUTF16 pwszDst = (PRTUTF16)&pachDst[1]; /** @todo misaligned! */
    int rc = RTStrToUtf16BigEx(pszSrc, RTSTR_MAX, &pwszDst, cwcMax, &cwcDst);
    if (RT_SUCCESS(rc))
    {
        pachDst[cchDst - 1] = (uint8_t)(1 + cwcDst * sizeof(RTUTF16));
        return VINF_SUCCESS;
    }
    if (rc == VERR_BUFFER_OVERFLOW)
    {
        pwszDst = NULL;
        rc = RTStrToUtf16BigEx(pszSrc, RTSTR_MAX, &pwszDst, 0, &cwcDst);
        if (RT_SUCCESS(rc))
        {
            Assert(cwcDst >= cwcMax);
            memcpy(&pachDst[1], pwszDst, cwcMax);
            pachDst[cchDst - 1] = (uint8_t)(1 + cwcMax * sizeof(RTUTF16));
            RTUtf16Free(pwszDst);
            return cwcDst > cwcMax ? VINF_BUFFER_OVERFLOW : VINF_SUCCESS;
        }
        AssertRC(rc);
    }
    else
        AssertRC(rc);
    return rc;
}


static void rtFsIsoMakerUdfSetLongAllocDesc(PUDFLONGAD pAllocDesc, uint32_t cb, uint32_t uSectorInPart, uint32_t idUnique = 0,
                                            uint16_t uPartition = 0, uint8_t fType = UDF_AD_TYPE_RECORDED_AND_ALLOCATED)
{
    pAllocDesc->uType                 = fType;
    pAllocDesc->cb                    = cb;
    pAllocDesc->Location.off          = uSectorInPart;
    pAllocDesc->Location.uPartitionNo = uPartition;
    pAllocDesc->ImplementationUse.Fid.fFlags   = 0;
    pAllocDesc->ImplementationUse.Fid.idUnique = idUnique;
}


static void rtFsIsoMakerUdfSetEntityId(PUDFENTITYID pEntityId, const char *pszIdentifier)
{
    RT_ZERO(*pEntityId);
    size_t const cchId = strlen(pszIdentifier);
    memcpy(pEntityId->achIdentifier, pszIdentifier, RT_MIN(cchId, sizeof(pEntityId->achIdentifier)));
}



static void rtFsIsoMakerUdfSetEntityIdDomain(PRTFSISOMAKERINT pThis, PUDFENTITYID pEntityId,
                                             const char *pszIdentifier, uint8_t fDomain = 0)
{
    rtFsIsoMakerUdfSetEntityId(pEntityId, pszIdentifier);
    pEntityId->Suffix.Domain.uUdfRevision = pThis->Udf.uSpecRev;
    pEntityId->Suffix.Domain.fDomain      = fDomain;
}


static void rtFsIsoMakerUdfSetEntityIdUdf(PRTFSISOMAKERINT pThis, PUDFENTITYID pEntityId, const char *pszIdentifier)
{
    rtFsIsoMakerUdfSetEntityId(pEntityId, pszIdentifier);
    pEntityId->Suffix.Udf.uUdfRevision = pThis->Udf.uSpecRev;
    pEntityId->Suffix.Udf.bOsClass     = g_bUdfOsClass;
    pEntityId->Suffix.Udf.idOS         = g_bUdfOsId;
}


static void rtFsIsoMakerUdfSetEntityIdImplementation(PUDFENTITYID pEntityId, const char *pszIdentifier)
{
    rtFsIsoMakerUdfSetEntityId(pEntityId, pszIdentifier);
    pEntityId->Suffix.Implementation.bOsClass     = g_bUdfOsClass;
    pEntityId->Suffix.Implementation.idOS         = g_bUdfOsId;
    uint32_t const uRev = RTBldCfgRevision();
    pEntityId->Suffix.Implementation.achImplUse[0] =  uRev        & 0xff;
    pEntityId->Suffix.Implementation.achImplUse[1] = (uRev >>  8) & 0xff;
    pEntityId->Suffix.Implementation.achImplUse[2] = (uRev >> 16) & 0xff;
    pEntityId->Suffix.Implementation.achImplUse[3] = (uRev >> 24) & 0xff;
}


static void rtFsIsoMakerUdfTimestampFromTime(PUDFTIMESTAMP pDst, PCRTTIME pSrc)
{
    pDst->fType                   = UDFTIMESTAMP_T_LOCAL;
    pDst->offUtcInMin             = pSrc->offUTC; /* Zero for UTC is apparently okay. */
    pDst->iYear                   = (int16_t)pSrc->i32Year;
    pDst->uMonth                  = pSrc->u8Month;
    pDst->uDay                    = pSrc->u8MonthDay;
    pDst->uHour                   = pSrc->u8Hour;
    pDst->uMinute                 = pSrc->u8Minute;
    pDst->uSecond                 = pSrc->u8Second;
    uint32_t ns = pSrc->u32Nanosecond;
    pDst->cCentiseconds           = (uint8_t)(ns / RT_NS_10MS);
    ns         %= RT_NS_10MS;
    pDst->cHundredsOfMicroseconds = (uint8_t)(ns / RT_NS_100US);
    ns         %= RT_NS_100US;
    pDst->cMicroseconds           = (uint8_t)(ns / RT_NS_1US);
}


static void rtFsIsoMakerUdfTimestampFromTimespec(PUDFTIMESTAMP pDst, PCRTTIMESPEC pSrc)
{
    RTTIME Exploded;
    rtFsIsoMakerUdfTimestampFromTime(pDst, RTTimeExplode(&Exploded, pSrc));
}



static int
rtFsIsoMakerUdfSetExtentAllocDescFromProducerEntries(PUDFEXTENTAD pExtentAllocDesc,
                                                     PCFSISOMAKERPRODUCERENTRY paEntries, uint32_t cEntries,
                                                     uint32_t offBaseLocation, PFNRTFSISOMAKERPRODUCER pfnStartProducer)
{
    for (uint32_t idx = 0; idx < cEntries; idx++)
    {
        if (paEntries[idx].pfnProducer == pfnStartProducer)
        {
            pExtentAllocDesc->off = offBaseLocation + idx;
            pExtentAllocDesc->cb  = RTFSISOMAKER_SECTOR_SIZE;
            do
            {
                pExtentAllocDesc->cb += RTFSISOMAKER_SECTOR_SIZE;
                idx++;
            } while (   idx < cEntries
                     && paEntries[idx].pfnProducer != rtFsIsoMakerUdfProduceTerminatingDesc);
            AssertReturn(idx < cEntries, VERR_INTERNAL_ERROR_2);
            return idx;
        }
    }
    AssertFailedReturn(VERR_INTERNAL_ERROR_2);
}


/**
 * UDF Sector Producer: Produce a sector full of ZEROs.
 *
 * This is use for padding and inserting zero sectors as sequence separators.
 */
static DECLCALLBACK(int)
rtFsIsoMakerUdfProduceZero(PRTFSISOMAKERINT pThis, void *pvSector, uint32_t offLocation, uint32_t uInfo)
{
    /* Nothing to do here as the caller zeros everything. */
    Assert(ASMMemIsZero(pvSector, RTFSISOMAKER_SECTOR_SIZE));
    RT_NOREF(pThis, pvSector, offLocation, uInfo);
    return VINF_SUCCESS;
}


/**
 * UDF Sector Producer: Anchor Volume Descriptor Pointer.
 */
static DECLCALLBACK(int)
rtFsIsoMakerUdfProduceAnchorVolumeDescPtr(PRTFSISOMAKERINT pThis, void *pvSector, uint32_t offLocation, uint32_t uInfo)
{
    PUDFANCHORVOLUMEDESCPTR const pDesc = (PUDFANCHORVOLUMEDESCPTR)pvSector;
    RT_NOREF(uInfo);

    /* Determin the two pointers from the descriptor sequence at sector 256. */
    int rc = rtFsIsoMakerUdfSetExtentAllocDescFromProducerEntries(&pDesc->MainVolumeDescSeq,
                                                                  pThis->aUdfAvdpAndVdsEntries, pThis->cUdfAvdpAndVdsEntries,
                                                                  256, rtFsIsoMakerUdfProducePrimaryVolumeDesc);
    AssertRCReturn(rc, rc);
    rc = rtFsIsoMakerUdfSetExtentAllocDescFromProducerEntries(&pDesc->ReserveVolumeDescSeq,
                                                              &pThis->aUdfAvdpAndVdsEntries[rc],
                                                              pThis->cUdfAvdpAndVdsEntries - rc,
                                                              256 + rc, rtFsIsoMakerUdfProducePrimaryVolumeDesc);
    AssertRCReturn(rc, rc);

    /* This must be done last, as it checksums the descriptor. */
    rtFsIsoMakerUdfPopulateTag(pThis, &pDesc->Tag, UDF_TAG_ID_ANCHOR_VOLUME_DESC_PTR, sizeof(*pDesc), offLocation);
    return VINF_SUCCESS;
}


/**
 * Helper for copying pThis->Udf.pszVolumeId to a UDF dstring field.
 */
static void rtFsIsoMakerUdfSetVolumeLabel(PRTFSISOMAKERINT pThis, PUDFDSTRING pachDst, size_t cchDst)
{
    char szTmp[42];
    rtFsIsoMakerUdfEncodeDString(pachDst, cchDst,
                                 pThis->Udf.pszVolumeId
                                 ? pThis->Udf.pszVolumeId
                                 : RTTimeSpecToString(&pThis->ImageCreationTime, szTmp, sizeof(szTmp)));
}


/**
 * UDF Sector Producer: Primary Volume Descriptor.
 *
 * The volume descriptor sequence number is given in @a uInfo.
 */
static DECLCALLBACK(int)
rtFsIsoMakerUdfProducePrimaryVolumeDesc(PRTFSISOMAKERINT pThis, void *pvSector, uint32_t offLocation, uint32_t uInfo)
{
    PUDFPRIMARYVOLUMEDESC const pDesc = (PUDFPRIMARYVOLUMEDESC)pvSector;

    pDesc->uVolumeDescSeqNo     = uInfo;
    /* pDesc->uPrimaryVolumeDescNo = 0; */
    rtFsIsoMakerUdfSetVolumeLabel(pThis, pDesc->achVolumeID, sizeof(pDesc->achVolumeID));
    pDesc->uVolumeSeqNo         = 1;
    pDesc->uMaxVolumeSeqNo      = 1;
    pDesc->uInterchangeLevel    = 2; /* level 2: one volume */
    pDesc->uMaxInterchangeLevel = 3; /* level 3: unrestricted */
    pDesc->fCharacterSets       = 1; /* only CS0 */
    pDesc->fMaxCharacterSets    = 1; /* only CS0 */
    char szTmp[42];
    RTStrPrintf(szTmp, sizeof(szTmp), "%#08x  IPRT  %s", (uint32_t)RTTimeSpecGetSeconds(&pThis->ImageCreationTime),
                pThis->Udf.pszVolumeSetId ? pThis->Udf.pszVolumeSetId : "");
    rtFsIsoMakerUdfEncodeDString(pDesc->achVolumeSetID, sizeof(pDesc->achVolumeSetID), szTmp);
    rtFsIsoMakerUdfSetCharSpec(&pDesc->DescCharSet);
    rtFsIsoMakerUdfSetCharSpec(&pDesc->ExplanatoryCharSet);
    /* pDesc->VolumeAbstract           = 0 LB 0 */
    /* pDesc->VolumeCopyrightNotice    = 0 LB 0 */
    rtFsIsoMakerUdfSetEntityId(&pDesc->idApplication, g_szUdfAppId);
    rtFsIsoMakerUdfTimestampFromTimespec(&pDesc->RecordingTimestamp, &pThis->ImageCreationTime);
    rtFsIsoMakerUdfSetEntityIdImplementation(&pDesc->idImplementation, g_szUdfImplId);
    /* pDesc->abImplementationUse      = unused/zero */
    /* pDesc->offPredecessorVolDescSeq = none */
    /* pDesc->fFlags                   = 0*/ /** @todo consider UDF_PVD_FLAGS_COMMON_VOLUME_SET_ID? */
    /* pDesc->abReserved               = zero */

    /* This must be done last, as it checksums the descriptor. */
    rtFsIsoMakerUdfPopulateTag(pThis, &pDesc->Tag, UDF_TAG_ID_PRIMARY_VOL_DESC, sizeof(*pDesc), offLocation);
    return VINF_SUCCESS;
}


/**
 * UDF Sector Producer: Logical Volume Descriptor.
 *
 * The volume descriptor sequence number is given in @a uInfo.
 */
static DECLCALLBACK(int)
rtFsIsoMakerUdfProduceLogicalVolumeDesc(PRTFSISOMAKERINT pThis, void *pvSector, uint32_t offLocation, uint32_t uInfo)
{
    PUDFLOGICALVOLUMEDESC const pDesc = (PUDFLOGICALVOLUMEDESC)pvSector;

    pDesc->uVolumeDescSeqNo = uInfo;
    rtFsIsoMakerUdfSetCharSpec(&pDesc->DescCharSet);
    rtFsIsoMakerUdfSetVolumeLabel(pThis, pDesc->achLogicalVolumeID, sizeof(pDesc->achLogicalVolumeID));
    pDesc->cbLogicalBlock   = RTFSISOMAKER_SECTOR_SIZE;
    rtFsIsoMakerUdfSetEntityIdDomain(pThis, &pDesc->idDomain, UDF_ENTITY_ID_LVD_DOMAIN);
    rtFsIsoMakerUdfSetLongAllocDesc(&pDesc->ContentsUse.FileSetDescriptor,
                                    pThis->UdfFileSetDescSeqRange.Core.KeyLast - pThis->UdfFileSetDescSeqRange.Core.Key + 1,
                                    pThis->UdfFileSetDescSeqRange.Core.Key - pThis->offUdfPartition);
    pDesc->cbMapTable       = sizeof(UDFPARTMAPTYPE1);
    pDesc->cPartitionMaps   = 1;
    rtFsIsoMakerUdfSetEntityIdImplementation(&pDesc->idImplementation, g_szUdfImplId);
    int rc = rtFsIsoMakerUdfSetExtentAllocDescFromProducerEntries(&pDesc->IntegritySeqExtent,
                                                                  pThis->aUdfAvdpAndVdsEntries, pThis->cUdfAvdpAndVdsEntries,
                                                                  256, rtFsIsoMakerUdfProduceVolumeIntegrityDesc);
    AssertRCReturn(rc, rc);

    PUDFPARTMAPTYPE1 const pPartMapEntry = (PUDFPARTMAPTYPE1)&pDesc->abPartitionMaps[0];
    pPartMapEntry->Hdr.bType    = 1;
    pPartMapEntry->Hdr.cb       = sizeof(*pPartMapEntry);
    pPartMapEntry->uVolumeSeqNo = 1;
    pPartMapEntry->uPartitionNo = g_uUdfPartNo;

    /* This must be done last, as it checksums the descriptor. */
    rtFsIsoMakerUdfPopulateTag(pThis, &pDesc->Tag, UDF_TAG_ID_LOGICAL_VOLUME_DESC, sizeof(*pDesc), offLocation);
    return VINF_SUCCESS;
}


/**
 * UDF Sector Producer: Partition Descriptor.
 *
 * The volume descriptor sequence number is given in @a uInfo.
 */
static DECLCALLBACK(int)
rtFsIsoMakerUdfProducePartitionDesc(PRTFSISOMAKERINT pThis, void *pvSector, uint32_t offLocation, uint32_t uInfo)
{
    PUDFPARTITIONDESC const pDesc = (PUDFPARTITIONDESC)pvSector;

    pDesc->uVolumeDescSeqNo = uInfo;
    pDesc->fFlags           = UDF_PARTITION_FLAGS_ALLOCATED;
    pDesc->uPartitionNo     = g_uUdfPartNo;
    rtFsIsoMakerUdfSetEntityId(&pDesc->idPartitionContents,
                               pThis->Udf.uLevel >= 3
                               ? UDF_ENTITY_ID_PD_PARTITION_CONTENTS_UDF_3
                               : UDF_ENTITY_ID_PD_PARTITION_CONTENTS_UDF_2);
    /* ContentUse.Hdr = zero (no tables or bitmaps - +NSR03 only anyway) */
    pDesc->uAccessType = UDF_PART_ACCESS_TYPE_READ_ONLY;
    pDesc->offLocation = (uint32_t)(pThis->offUdfPartition / RTFSISOMAKER_SECTOR_SIZE);
    pDesc->cSectors    = pThis->cUdfPartitionSectors;
    rtFsIsoMakerUdfSetEntityIdImplementation(&pDesc->idImplementation, g_szUdfImplId);

    /* This must be done last, as it checksums the descriptor. */
    rtFsIsoMakerUdfPopulateTag(pThis, &pDesc->Tag, UDF_TAG_ID_PARTITION_DESC, sizeof(*pDesc), offLocation);
    return VINF_SUCCESS;
}


/**
 * UDF Sector Producer: Unallocated Space Descriptor.
 *
 * The volume descriptor sequence number is given in @a uInfo.
 */
static DECLCALLBACK(int)
rtFsIsoMakerUdfProduceUnallocatedSpaceDesc(PRTFSISOMAKERINT pThis, void *pvSector, uint32_t offLocation, uint32_t uInfo)
{
    PUDFUNALLOCATEDSPACEDESC const pDesc = (PUDFUNALLOCATEDSPACEDESC)pvSector;

    pDesc->uVolumeDescSeqNo = uInfo;
    /* pDesc->cAllocationDescriptors = 0;*/

    /* This must be done last, as it checksums the descriptor. */
    rtFsIsoMakerUdfPopulateTag(pThis, &pDesc->Tag, UDF_TAG_ID_UNALLOCATED_SPACE_DESC, sizeof(*pDesc), offLocation);
    return VINF_SUCCESS;
}


/**
 * UDF Sector Producer: Implementation Use Descriptor.
 *
 * The volume descriptor sequence number is given in @a uInfo.
 */
static DECLCALLBACK(int)
rtFsIsoMakerUdfProduceImplementationUseVolumeDesc(PRTFSISOMAKERINT pThis, void *pvSector, uint32_t offLocation, uint32_t uInfo)
{
    PUDFIMPLEMENTATIONUSEVOLUMEDESC const pDesc = (PUDFIMPLEMENTATIONUSEVOLUMEDESC)pvSector;

    pDesc->uVolumeDescSeqNo = uInfo;
    rtFsIsoMakerUdfSetEntityIdUdf(pThis, &pDesc->idImplementation, UDF_ENTITY_ID_IUVD_IMPLEMENTATION);
    rtFsIsoMakerUdfSetCharSpec(&pDesc->ImplementationUse.Lvi.Charset);
    rtFsIsoMakerUdfSetVolumeLabel(pThis, pDesc->ImplementationUse.Lvi.achVolumeID,
                                  sizeof(pDesc->ImplementationUse.Lvi.achVolumeID));
    if (pThis->Udf.pszPublisherId)
        rtFsIsoMakerUdfEncodeDString(pDesc->ImplementationUse.Lvi.achInfo1, sizeof(pDesc->ImplementationUse.Lvi.achInfo1),
                                     pThis->Udf.pszPublisherId);
    if (pThis->Udf.pszDataPreparerId)
        rtFsIsoMakerUdfEncodeDString(pDesc->ImplementationUse.Lvi.achInfo2, sizeof(pDesc->ImplementationUse.Lvi.achInfo2),
                                     pThis->Udf.pszPublisherId);
    /* pDesc->ImplementationUse.Lvi.achInfo3 = empty */
    rtFsIsoMakerUdfSetEntityIdImplementation(&pDesc->ImplementationUse.Lvi.idImplementation, g_szUdfImplId);
    char szVersionAndHost[108];
    RTStrPrintf(szVersionAndHost, sizeof(szVersionAndHost), "%sr%s (%s.%s)",
                RTBldCfgVersion(), RTBldCfgRevisionStr(), RTBldCfgTarget(), RTBldCfgTargetArch());
    rtFsIsoMakerUdfEncodeDString((char *)&pDesc->ImplementationUse.Lvi.abUse[0], sizeof(pDesc->ImplementationUse.Lvi.abUse),
                                 szVersionAndHost); /* Hopefully nobody minds... */

    rtFsIsoMakerUdfPopulateTag(pThis, &pDesc->Tag, UDF_TAG_ID_IMPLEMENTATION_USE_VOLUME_DESC, sizeof(*pDesc), offLocation);
    return VINF_SUCCESS;
}


/**
 * UDF Sector Producer: Volume Integrity Descriptor.
 */
static DECLCALLBACK(int)
rtFsIsoMakerUdfProduceVolumeIntegrityDesc(PRTFSISOMAKERINT pThis, void *pvSector, uint32_t offLocation, uint32_t uInfo)
{
    PUDFLOGICALVOLINTEGRITYDESC const pDesc = (PUDFLOGICALVOLINTEGRITYDESC)pvSector;
    RT_NOREF(uInfo);

    rtFsIsoMakerUdfTimestampFromTimespec(&pDesc->RecordingTimestamp, &pThis->ImageCreationTime);
    pDesc->uIntegrityType            = UDF_LVID_TYPE_CLOSE;
    /* pDesc->NextIntegrityExtent    = 0 LB 0; */
    pDesc->ContentUse.LVHdr.idUnique = pThis->cObjects + RTISOMAKER_UDF_UNIQUE_ID_OFFSET;
    /* pDesc->ContentUse.LVHdr.abReserved = {0} */
    pDesc->cPartitions               = 1;
    pDesc->cbImplementationUse       = RT_UOFFSETOF(UDFLVIDIMPLUSEUDF, abImplementationUse); /** @todo 'MS CDIMAGE UDF' sets this to 0x30, not 0x2e as we do... */
    pDesc->au32Tables[0]             = 0;                           /* FreeSpace[0] */
    pDesc->au32Tables[1]             = pThis->cUdfPartitionSectors; /* SizeTable[0] */

    PUDFLVIDIMPLUSEUDF const pImplUse = (PUDFLVIDIMPLUSEUDF)&pDesc->au32Tables[2];
    rtFsIsoMakerUdfSetEntityIdImplementation(&pImplUse->idImplementation, g_szUdfImplId);
    pImplUse->cFiles                 = pThis->Udf.cNames - pThis->Udf.cDirs;
    pImplUse->cDirs                  = pThis->Udf.cDirs;
    pImplUse->uMinUdfRevisionRead    = pThis->Udf.uSpecRev;
    pImplUse->uMinUdfRevisionWrite   = pThis->Udf.uSpecRev;
    pImplUse->uMaxUdfRevisionWrite   = 0x260;

    rtFsIsoMakerUdfPopulateTag(pThis, &pDesc->Tag, UDF_TAG_ID_LOGICAL_VOLUME_INTEGRITY_DESC, sizeof(*pDesc), offLocation);
    return VINF_SUCCESS;
}


/**
 * UDF Sector Producer: Terminating Descriptor.
 */
static DECLCALLBACK(int)
rtFsIsoMakerUdfProduceTerminatingDesc(PRTFSISOMAKERINT pThis, void *pvSector, uint32_t offLocation, uint32_t uInfo)
{
    PUDFTERMINATINGDESC const pDesc = (PUDFTERMINATINGDESC)pvSector;
    rtFsIsoMakerUdfPopulateTag(pThis, &pDesc->Tag, UDF_TAG_ID_TERMINATING_DESC, sizeof(*pDesc), offLocation);
    RT_NOREF(uInfo);
    return VINF_SUCCESS;
}


/**
 * UDF Sector Producer: File Set Descriptor.
 *
 * The volume descriptor sequence number is given in @a uInfo.
 */
static DECLCALLBACK(int)
rtFsIsoMakerUdfProduceFileSetDesc(PRTFSISOMAKERINT pThis, void *pvSector, uint32_t offLocation, uint32_t uInfo)
{
    PUDFFILESETDESC const pDesc = (PUDFFILESETDESC)pvSector;

    rtFsIsoMakerUdfTimestampFromTimespec(&pDesc->RecordingTimestamp, &pThis->ImageCreationTime);
    pDesc->uInterchangeLevel    = 3; /* no restrictions */
    pDesc->uMaxInterchangeLevel = 3; /* no restrictions */
    pDesc->fCharacterSets       = 1; /* only CS0 */
    pDesc->fMaxCharacterSets    = 1; /* only CS0 */
    pDesc->uFileSetNo           = 0;
    pDesc->uFileSetDescNo       = uInfo;
    rtFsIsoMakerUdfSetCharSpec(&pDesc->LogicalVolumeIDCharSet);
    rtFsIsoMakerUdfSetVolumeLabel(pThis, pDesc->achLogicalVolumeID, sizeof(pDesc->achLogicalVolumeID));
    rtFsIsoMakerUdfSetCharSpec(&pDesc->FileSetCharSet);
    rtFsIsoMakerUdfSetVolumeLabel(pThis, pDesc->achFileSetID, sizeof(pDesc->achFileSetID)); /* maybe set this to something else... */
    if (pThis->Udf.pszCopyrightFileId)
        rtFsIsoMakerUdfEncodeDString(pDesc->achCopyrightFile, sizeof(pDesc->achCopyrightFile), pThis->Udf.pszCopyrightFileId);
    if (pThis->Udf.pszAbstractFileId)
        rtFsIsoMakerUdfEncodeDString(pDesc->achAbstractFile, sizeof(pDesc->achAbstractFile), pThis->Udf.pszAbstractFileId);
    rtFsIsoMakerUdfSetLongAllocDesc(&pDesc->RootDirIcb, RTFSISOMAKER_SECTOR_SIZE, pThis->Udf.pRoot->uUdfIcbSector, 0 /*idUnique*/);
    rtFsIsoMakerUdfSetEntityIdDomain(pThis, &pDesc->idDomain, UDF_ENTITY_FSD_LVD_DOMAIN);
    /* pDesc->NextExtent        = 0/none */
    /* pDesc->SystemStreamDirIcb = 0/none */

    /* This must be done last, as it checksums the descriptor. */
    rtFsIsoMakerUdfPopulateTag(pThis, &pDesc->Tag, UDF_TAG_ID_FILE_SET_DESC, sizeof(*pDesc), offLocation);
    return VINF_SUCCESS;
}



/*
 *
 * Image finalization.
 * Image finalization.
 * Image finalization.
 *
 */

static void rtFsIsoMakerAddByteRange(PRTFSISOMAKERINT pThis, PRTFSISOMAKERRANGENODE pRangeNode,
                                     uint64_t off, uint64_t cb, PFNRTFSISOMAKERRANGEREAD pfnRead)
{
    pRangeNode->pfnRead      = pfnRead;
    pRangeNode->Core.Key     = off;
    pRangeNode->Core.KeyLast = off + cb - 1;
    bool const fRc = RTAvlrU64Insert(&pThis->RangeTree, &pRangeNode->Core);
    Assert(fRc); RT_NOREF(fRc);
}


static void rtFsIsoMakerAddSectorRange(PRTFSISOMAKERINT pThis, PRTFSISOMAKERRANGENODE pRangeNode,
                                       uint32_t iFirstSector, uint32_t cSectors, PFNRTFSISOMAKERRANGEREAD pfnRead)
{
    pRangeNode->pfnRead      = pfnRead;
    pRangeNode->Core.Key     = (uint64_t)iFirstSector * RTFSISOMAKER_SECTOR_SIZE;
    pRangeNode->Core.KeyLast = ((uint64_t)iFirstSector + cSectors) * RTFSISOMAKER_SECTOR_SIZE - 1;
    bool const fRc = RTAvlrU64Insert(&pThis->RangeTree, &pRangeNode->Core);
    Assert(fRc); RT_NOREF(fRc);
}


static uint64_t rtFsIsoMakerAllocateRange(PRTFSISOMAKERALLOCATOR pAllocator, uint64_t cb, bool f9660Only = false)
{
    uint64_t offRet;
    if (   pAllocator->offUdf == UINT64_MAX
        || (   f9660Only
            && (256U * RTFSISOMAKER_SECTOR_SIZE - pAllocator->offIso9660 >= cb)))
    {
        offRet = pAllocator->offIso9660;
        pAllocator->offIso9660 += RT_ALIGN_64(cb, RTFSISOMAKER_SECTOR_SIZE);
    }
    else
    {
        offRet = pAllocator->offUdf;
        pAllocator->offUdf += RT_ALIGN_64(cb, RTFSISOMAKER_SECTOR_SIZE);
    }
    Assert(!(offRet & RTFSISOMAKER_SECTOR_OFFSET_MASK));
    return offRet;
}


/** Allocates a UDF ICB, returning the partition relative sector number. */
static uint32_t rtFsIsoMakerAllocateUdfIcb(PRTFSISOMAKERINT pThis, PRTFSISOMAKERALLOCATOR pAllocator)
{
    uint64_t const off = pAllocator->offUdf;
    pAllocator->offUdf += RTFSISOMAKER_SECTOR_SIZE;
    Assert(!(off & RTFSISOMAKER_SECTOR_OFFSET_MASK));
    uint64_t idxSector = (off - pThis->offUdfPartition) / RTFSISOMAKER_SECTOR_SIZE;
    Assert(idxSector < UINT32_MAX);
    return (uint32_t)idxSector;
}


static void rtFsIsoMakerAllocateAndAddByteRange(PRTFSISOMAKERINT pThis, PRTFSISOMAKERALLOCATOR pAllocator,
                                                PRTFSISOMAKERRANGENODE pRangeNode, uint64_t cb, PFNRTFSISOMAKERRANGEREAD pfnRead,
                                                bool f9660Only = false)
{
    /*
     * We can only insert non-empty ranges into the tree.
     *
     * Note! The allocator will not waste any space when allocating zero bytes,
     *       but the address will be shared with other objects.
     */
    uint64_t const off = rtFsIsoMakerAllocateRange(pAllocator, cb, f9660Only);
    pRangeNode->Core.Key     = off;
    pRangeNode->Core.KeyLast = off + cb - 1;
    if (cb != 0)
    {
        pRangeNode->pfnRead  = pfnRead;
        bool const fRc = RTAvlrU64Insert(&pThis->RangeTree, &pRangeNode->Core);
        Assert(fRc); RT_NOREF(fRc);
    }
}


/**
 * Remove any orphaned object from the disk.
 *
 * @returns IPRT status code.
 * @param   pThis               The ISO maker instance.
 */
static int rtFsIsoMakerFinalizeRemoveOrphans(PRTFSISOMAKERINT pThis)
{
    for (;;)
    {
        uint32_t         cRemoved = 0;
        PRTFSISOMAKEROBJ pCur;
        PRTFSISOMAKEROBJ pNext;
        RTListForEachSafe(&pThis->ObjectHead, pCur, pNext, RTFSISOMAKEROBJ, Entry)
        {
            if (   pCur->pPrimaryName
                || pCur->pJolietName
                || pCur->pUdfName
                || pCur->pHfsName
                || pCur->cNotOrphan > 0)
            { /* likely */ }
            else
            {
                Log4(("rtFsIsoMakerFinalizeRemoveOrphans: %#x cbData=%#RX64\n", pCur->idxObj,
                      pCur->enmType == RTFSISOMAKEROBJTYPE_FILE ? ((PRTFSISOMAKERFILE)(pCur))->cbData : 0));
                int rc = rtFsIsoMakerObjRemoveWorker(pThis, pCur);
                if (RT_SUCCESS(rc))
                {
                    if (rc != VWRN_DANGLING_OBJECTS) /** */
                        cRemoved++;
                }
                else
                    return rc;
            }
        }
        if (!cRemoved)
            return VINF_SUCCESS;
    }
}


/**
 * Finalizes the El Torito boot stuff, part 1.
 *
 * This includes generating the boot catalog data and fixing the location of all
 * related image files.
 *
 * @returns IPRT status code.
 * @param   pThis               The ISO maker instance.
 */
static int rtFsIsoMakerFinalizeBootStuffPart1(PRTFSISOMAKERINT pThis)
{
    /*
     * Anything?
     */
    if (!pThis->pBootCatFile)
        return VINF_SUCCESS;

    /*
     * Validate the boot catalog file.
     */
    AssertReturn(pThis->aBootCatEntries[0].bType == ISO9660_ELTORITO_HEADER_ID_VALIDATION_ENTRY,
                 VERR_ISOMK_BOOT_CAT_NO_VALIDATION_ENTRY);
    AssertReturn(pThis->aBootCatEntries[1].pBootFile != NULL, VERR_ISOMK_BOOT_CAT_NO_DEFAULT_ENTRY);

    /* Check any sections following the default one. */
    uint32_t iLastSecHdr = UINT32_MAX;
    uint32_t cEntries    = 2;
    while (   cEntries < RT_ELEMENTS(pThis->aBootCatEntries) - 1U
           && pThis->aBootCatEntries[cEntries].cEntries > 0)
    {
        AssertReturn(   pThis->aBootCatEntries[cEntries].bType == ISO9660_ELTORITO_HEADER_ID_SECTION_HEADER
                     || pThis->aBootCatEntries[cEntries].bType == ISO9660_ELTORITO_HEADER_ID_FINAL_SECTION_HEADER,
                     VERR_ISOMK_BOOT_CAT_EXPECTED_SECTION_HEADER);
        for (uint32_t i = 1; i < pThis->aBootCatEntries[cEntries].cEntries; i++)
            AssertReturn(pThis->aBootCatEntries[cEntries + i].pBootFile != NULL,
                         pThis->aBootCatEntries[cEntries + i].cEntries == 0
                         ? VERR_ISOMK_BOOT_CAT_EMPTY_ENTRY : VERR_ISOMK_BOOT_CAT_INVALID_SECTION_SIZE);
        iLastSecHdr = cEntries;
        cEntries   += pThis->aBootCatEntries[cEntries].cEntries;
    }

    /* Calc number of entries in the file. */
    uint32_t const cEntriesInFile = cEntries
                                  + (   iLastSecHdr == UINT32_MAX
                                     ||    pThis->aBootCatEntries[iLastSecHdr].bType
                                        != ISO9660_ELTORITO_HEADER_ID_FINAL_SECTION_HEADER);

    /* Check that the remaining entries are empty. */
    while (cEntries < RT_ELEMENTS(pThis->aBootCatEntries))
    {
        AssertReturn(pThis->aBootCatEntries[cEntries].cEntries == 0, VERR_ISOMK_BOOT_CAT_ERRATIC_ENTRY);
        cEntries++;
    }

    /*
     * Fixate the size of the boot catalog file.
     */
    pThis->pBootCatFile->cbData = cEntriesInFile * ISO9660_ELTORITO_ENTRY_SIZE;
    pThis->cbData  += RT_ALIGN_32(cEntriesInFile * ISO9660_ELTORITO_ENTRY_SIZE, RTFSISOMAKER_SECTOR_SIZE);

    /*
     * Move up the boot images and boot catalog to the start of the image.
     */
    for (uint32_t i = RT_ELEMENTS(pThis->aBootCatEntries) - 2; i > 0; i--)
        if (pThis->aBootCatEntries[i].pBootFile)
        {
            RTListNodeRemove(&pThis->aBootCatEntries[i].pBootFile->Core.Entry);
            RTListPrepend(&pThis->ObjectHead, &pThis->aBootCatEntries[i].pBootFile->Core.Entry);
        }

    /* The boot catalog comes first. */
    RTListNodeRemove(&pThis->pBootCatFile->Core.Entry);
    RTListPrepend(&pThis->ObjectHead, &pThis->pBootCatFile->Core.Entry);

    return VINF_SUCCESS;
}


/**
 * Finalizes the El Torito boot stuff, part 1.
 *
 * This includes generating the boot catalog data and fixing the location of all
 * related image files.
 *
 * @returns IPRT status code.
 * @param   pThis               The ISO maker instance.
 */
static int rtFsIsoMakerFinalizeBootStuffPart2(PRTFSISOMAKERINT pThis)
{
    /*
     * Anything?
     */
    if (!pThis->pBootCatFile)
        return VINF_SUCCESS;

    /*
     * Fill in the descriptor.
     */
    PISO9660BOOTRECORDELTORITO pDesc = pThis->pElToritoDesc;
    pDesc->Hdr.bDescType    = ISO9660VOLDESC_TYPE_BOOT_RECORD;
    pDesc->Hdr.bDescVersion = ISO9660PRIMARYVOLDESC_VERSION;
    memcpy(pDesc->Hdr.achStdId, ISO9660VOLDESC_STD_ID, sizeof(pDesc->Hdr.achStdId));
    memcpy(pDesc->achBootSystemId, RT_STR_TUPLE(ISO9660BOOTRECORDELTORITO_BOOT_SYSTEM_ID));
    pDesc->offBootCatalog   = RT_H2LE_U32((uint32_t)(pThis->pBootCatFile->RangeNode.Core.Key / RTFSISOMAKER_SECTOR_SIZE));

    /*
     * Update the image file locations.
     */
    uint32_t cEntries = 2;
    for (uint32_t i = 1; i < RT_ELEMENTS(pThis->aBootCatEntries) - 1; i++)
        if (pThis->aBootCatEntries[i].pBootFile)
        {
            uint32_t off = pThis->aBootCatEntries[i].pBootFile->RangeNode.Core.Key / RTFSISOMAKER_SECTOR_SIZE;
            off = RT_H2LE_U32(off);
            int rc = RTVfsFileWriteAt(pThis->pBootCatFile->u.hVfsFile,
                                      i * ISO9660_ELTORITO_ENTRY_SIZE + RT_UOFFSETOF(ISO9660ELTORITOSECTIONENTRY, offBootImage),
                                      &off, sizeof(off), NULL /*pcbWritten*/);
            AssertRCReturn(rc, rc);
            if (i == cEntries)
                cEntries = i + 1;
        }

    /*
     * Write end section if necessary.
     */
    uint32_t iLastSecHdr = UINT32_MAX;
    cEntries    = 2;
    while (   cEntries < RT_ELEMENTS(pThis->aBootCatEntries) - 1U
           && pThis->aBootCatEntries[cEntries].cEntries > 0)
    {
        Assert(   pThis->aBootCatEntries[cEntries].bType == ISO9660_ELTORITO_HEADER_ID_SECTION_HEADER
               || pThis->aBootCatEntries[cEntries].bType == ISO9660_ELTORITO_HEADER_ID_FINAL_SECTION_HEADER);
        iLastSecHdr = cEntries;
        cEntries   += pThis->aBootCatEntries[cEntries].cEntries;
    }
    if (   iLastSecHdr == UINT32_MAX
        || pThis->aBootCatEntries[iLastSecHdr].bType != ISO9660_ELTORITO_HEADER_ID_FINAL_SECTION_HEADER)
    {
        ISO9660ELTORITOSECTIONHEADER Entry;
        Entry.bHeaderId   = ISO9660_ELTORITO_HEADER_ID_FINAL_SECTION_HEADER;
        Entry.bPlatformId = ISO9660_ELTORITO_PLATFORM_ID_X86;
        Entry.cEntries    = 0;
        RT_ZERO(Entry.achSectionId);
        int rc = RTVfsFileWriteAt(pThis->pBootCatFile->u.hVfsFile, cEntries * ISO9660_ELTORITO_ENTRY_SIZE,
                                  &Entry, sizeof(Entry), NULL /*pcbWritten*/);
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}


/**
 * Gathers the dirs for an ISO-9660 namespace (e.g. primary or joliet).
 *
 * @param   pNamespace  The namespace. The FinalizedDirs will be worked here.
 */
static void rtFsIsoMakerFinalizeGatherDirs(PRTFSISOMAKERNAMESPACE pNamespace)
{
    RTListInit(&pNamespace->FinalizedDirs);
    uint32_t cDirs = 0;

    /*
     * Enter the root directory (if we got one).
     */
    if (pNamespace->pRoot)
    {
        PRTFSISOMAKERNAMEDIR pCurDir = pNamespace->pRoot->pDir;
        RTListAppend(&pNamespace->FinalizedDirs, &pCurDir->FinalizedEntry);
        cDirs = 1;

        do
        {
            /*
             * Scan pCurDir and add directories.  We don't need to sort anything
             * here because the directory is already in path table compatible order.
             */
            uint32_t            cLeft    = pCurDir->cChildren;
            PPRTFSISOMAKERNAME  ppChild  = pCurDir->papChildren;
            while (cLeft-- > 0)
            {
                PRTFSISOMAKERNAME pChild = *ppChild++;
                if (pChild->pDir)
                {
                    RTListAppend(&pNamespace->FinalizedDirs, &pChild->pDir->FinalizedEntry);
                    cDirs += 1;
                }
            }

            /*
             * Advance to the next directory.
             */
            pCurDir = RTListGetNext(&pNamespace->FinalizedDirs, pCurDir, RTFSISOMAKERNAMEDIR, FinalizedEntry);
        } while (pCurDir);
    }

    pNamespace->cDirs = cDirs;
}


/**
 * Allocates space in the rock ridge spill file.
 *
 * @returns Spill file offset, UINT32_MAX on failure.
 * @param   pRRSpillFile    The spill file.
 * @param   cbRock          Number of bytes to allocate.
 */
static uint32_t rtFsIsoMakerFinalizeAllocRockRidgeSpill(PRTFSISOMAKERFILE pRRSpillFile, uint32_t cbRock)
{
    uint32_t off = pRRSpillFile->cbData;
    if (ISO9660_SECTOR_SIZE - (pRRSpillFile->cbData & ISO9660_SECTOR_OFFSET_MASK) >= cbRock)
    { /* likely */ }
    else
    {
        off |= ISO9660_SECTOR_OFFSET_MASK;
        off++;
        AssertLogRelReturn(off > 0, UINT32_MAX);
        pRRSpillFile->cbData = off;
    }
    pRRSpillFile->cbData += RT_ALIGN_32(cbRock, 4);
    return off;
}


/**
 * Finalizes a directory entry (i.e. namespace node).
 *
 * This calculates the directory record size.
 *
 * @returns IPRT status code.
 * @param   pNamespace      The namespace.
 * @param   pName           The directory entry to finalize.
 * @param   offInDir        The offset in the directory of this record.
 * @param   uRockRidgeLevel This is the rock ridge level.
 * @param   fIsRoot         Set if this is the root.
 */
static int rtFsIsoMakerFinalizeIsoDirectoryEntry(PRTFSISOMAKERNAMESPACE pNamespace, PRTFSISOMAKERNAME pName,
                                                 uint32_t offInDir, uint8_t uRockRidgeLevel, bool fIsRoot)
{
    /* Set directory and translation table offsets.  (These are for
       helping generating data blocks later.) */
    pName->offDirRec = offInDir;

    /* Calculate the minimal directory record size. */
    size_t cbDirRec = RT_UOFFSETOF(ISO9660DIRREC, achFileId) + pName->cbNameInDirRec + !(pName->cbNameInDirRec & 1);
    AssertReturn(cbDirRec <= UINT8_MAX, VERR_FILENAME_TOO_LONG);

    pName->cbDirRec = (uint8_t)cbDirRec;
    pName->cDirRecs = 1;
    if (pName->pObj->enmType == RTFSISOMAKEROBJTYPE_FILE)
    {
        PRTFSISOMAKERFILE pFile = (PRTFSISOMAKERFILE)pName->pObj;
        if (pFile->cbData > UINT32_MAX)
            pName->cDirRecs = (pFile->cbData + RTFSISOMAKER_MAX_ISO9660_EXTENT_SIZE - 1) / RTFSISOMAKER_MAX_ISO9660_EXTENT_SIZE;
    }

    /*
     * Calculate the size of the rock ridge bits we need.
     */
    if (uRockRidgeLevel > 0)
    {
        uint16_t cbRock = 0;
        uint8_t  fFlags = 0;

        /* Level two starts with a 'RR' entry. */
        if (uRockRidgeLevel >= 2)
            cbRock += sizeof(ISO9660RRIPRR);

        /* We always do 'PX' and 'TF' w/ 4 timestamps. */
        cbRock += sizeof(ISO9660RRIPPX)
                + RT_UOFFSETOF(ISO9660RRIPTF, abPayload) + 4 * sizeof(ISO9660RECTIMESTAMP);
        fFlags |= ISO9660RRIP_RR_F_PX | ISO9660RRIP_RR_F_TF;

        /* Devices needs 'PN'. */
        if (   RTFS_IS_DEV_BLOCK(pName->pObj->fMode)
            || RTFS_IS_DEV_CHAR(pName->pObj->fMode))
        {
            cbRock += sizeof(ISO9660RRIPPN);
            fFlags |= ISO9660RRIP_RR_F_PN;
        }

        /* Usually we need a 'NM' entry too. */
        if (   pName->pszRockRidgeNm != pName->szName
            && pName->cchRockRidgeNm > 0
            && (   pName->cbNameInDirRec != 1
                || (uint8_t)pName->szName[0] > (uint8_t)0x01) )  /** @todo only root dir ever uses an ID byte here? [RR NM ./..] */
        {
            uint16_t cchNm = pName->cchRockRidgeNm;
            while (cchNm > ISO9660RRIPNM_MAX_NAME_LEN)
            {
                cbRock += (uint16_t)RT_UOFFSETOF(ISO9660RRIPNM, achName) + ISO9660RRIPNM_MAX_NAME_LEN;
                cchNm  -= ISO9660RRIPNM_MAX_NAME_LEN;
            }
            cbRock += (uint16_t)RT_UOFFSETOF(ISO9660RRIPNM, achName) + cchNm;
            fFlags |= ISO9660RRIP_RR_F_NM;
        }

        /* Symbolic links needs a 'SL' entry. */
        if (pName->pObj->enmType == RTFSISOMAKEROBJTYPE_SYMLINK)
        {
            PRTFSISOMAKERSYMLINK pSymlink = (PRTFSISOMAKERSYMLINK)pName->pObj;
            cbRock += pSymlink->cbSlRockRidge;
            fFlags |= ISO9660RRIP_RR_F_SL;
        }

        /*
         * Decide where stuff goes.  The '.' record of the root dir is special.
         */
        pName->fRockEntries = fFlags;
        if (!fIsRoot)
        {
            if (pName->cbDirRec + cbRock < UINT8_MAX)
            {
                pName->cbRockInDirRec      = cbRock;
                pName->cbRockSpill         = 0;
                pName->fRockNeedRRInDirRec = uRockRidgeLevel >= 2;
                pName->fRockNeedRRInSpill  = false;
            }
            else if (pName->cbDirRec + sizeof(ISO9660SUSPCE) < UINT8_MAX)
            {
                /* Try fit the 'RR' entry in the directory record, but don't bother with anything else. */
                if (uRockRidgeLevel >= 2 && pName->cbDirRec + sizeof(ISO9660SUSPCE) + sizeof(ISO9660RRIPRR) < UINT8_MAX)
                {
                    pName->cbRockInDirRec      = (uint16_t)(sizeof(ISO9660SUSPCE) + sizeof(ISO9660RRIPRR));
                    cbRock -= sizeof(ISO9660RRIPRR);
                    pName->cbRockSpill         = cbRock;
                    pName->fRockNeedRRInDirRec = true;
                    pName->fRockNeedRRInSpill  = false;
                }
                else
                {
                    pName->cbRockInDirRec      = (uint16_t)sizeof(ISO9660SUSPCE);
                    pName->cbRockSpill         = cbRock;
                    pName->fRockNeedRRInDirRec = false;
                    pName->fRockNeedRRInSpill  = uRockRidgeLevel >= 2;
                }
                pName->offRockSpill         = rtFsIsoMakerFinalizeAllocRockRidgeSpill(pNamespace->pRRSpillFile, cbRock);
                AssertReturn(pName->offRockSpill != UINT32_MAX, VERR_ISOMK_RR_SPILL_FILE_FULL);
            }
            else
            {
                LogRel(("RTFsIsoMaker: no space for 'CE' entry: cbDirRec=%#x bytes, name=%s (%#x bytes)\n",
                        pName->cbDirRec, pName->szName, pName->cbNameInDirRec));
                return VERR_ISOMK_RR_NO_SPACE_FOR_CE;
            }
        }
        else
        {
            /* The root starts with a 'SP' record to indicate that SUSP is being used,
               this is always in the directory record.  If we add a 'ER' record (big) too,
               we put all but 'SP' and 'ER' in the spill file too keep things simple. */
            if (uRockRidgeLevel < 2)
            {
                Assert(!(fFlags & (ISO9660RRIP_RR_F_NM | ISO9660RRIP_RR_F_SL | ISO9660RRIP_RR_F_CL | ISO9660RRIP_RR_F_PL | ISO9660RRIP_RR_F_RE)));
                cbRock += sizeof(ISO9660SUSPSP);
                Assert(pName->cbDirRec + cbRock < UINT8_MAX);
                pName->cbRockInDirRec       = cbRock;
                pName->cbRockSpill          = 0;
                pName->fRockNeedER          = false;
                pName->fRockNeedRRInDirRec  = false;
                pName->fRockNeedRRInSpill   = false;
            }
            else
            {
                pName->cbRockInDirRec       = (uint16_t)(sizeof(ISO9660SUSPSP) + sizeof(ISO9660SUSPCE));
                pName->fRockNeedER          = true;
                pName->fRockNeedRRInSpill   = true;
                pName->fRockNeedRRInDirRec  = false;
                cbRock += ISO9660_RRIP_ER_LEN;
                pName->cbRockSpill          = cbRock;
                pName->offRockSpill         = rtFsIsoMakerFinalizeAllocRockRidgeSpill(pNamespace->pRRSpillFile, cbRock);
            }
        }
        pName->cbDirRec += pName->cbRockInDirRec + (pName->cbRockInDirRec & 1);
        Assert(pName->cbDirRec < UINT8_MAX);
    }

    pName->cbDirRecTotal = pName->cbDirRec * pName->cDirRecs;
    return VINF_SUCCESS;
}


/**
 * Finalizes either a primary and secondary ISO namespace.
 *
 * @returns IPRT status code
 * @param   pThis           The ISO maker instance.
 * @param   pNamespace      The namespace.
 * @param   pAllocator      The data allocator (in/out).  We will allocate
 *                          blocks for the directories and the path tables.
 */
static int rtFsIsoMakerFinalizeDirectoriesInIsoNamespace(PRTFSISOMAKERINT pThis, PRTFSISOMAKERNAMESPACE pNamespace,
                                                         PRTFSISOMAKERALLOCATOR pAllocator)
{
    int rc;

    /*
     * Reset the rock ridge spill file (in case we allow finalizing more than once)
     * and create a new spill file if rock ridge is enabled.  The directory entry
     * finalize function uses this as a clue that rock ridge is enabled.
     */
    if (pNamespace->pRRSpillFile)
    {
        pNamespace->pRRSpillFile->Core.cNotOrphan = 0;
        rtFsIsoMakerObjRemoveWorker(pThis, &pNamespace->pRRSpillFile->Core);
        pNamespace->pRRSpillFile = NULL;
    }
    if (pNamespace->uRockRidgeLevel > 0)
    {
        rc = rtFsIsoMakerAddUnnamedFileWorker(pThis, NULL, 0, &pNamespace->pRRSpillFile);
        AssertRCReturn(rc, rc);
        pNamespace->pRRSpillFile->enmSrcType            = RTFSISOMAKERSRCTYPE_RR_SPILL;
        pNamespace->pRRSpillFile->u.pRockSpillNamespace = pNamespace;
        pNamespace->pRRSpillFile->Core.cNotOrphan       = 1;
    }

    uint16_t idPathTable = 1;
    uint32_t cbPathTable = 0;
    if (pNamespace->pRoot)
    {
        /*
         * Precalc the directory record size for the root directory.
         */
        rc = rtFsIsoMakerFinalizeIsoDirectoryEntry(pNamespace, pNamespace->pRoot, 0 /*offInDir*/,
                                                   pNamespace->uRockRidgeLevel, true /*fIsRoot*/);
        AssertRCReturn(rc, rc);

        PFNRTFSISOMAKERRANGEREAD const pfnReadDir = pNamespace->fNamespace == RTFSISOMAKER_NAMESPACE_JOLIET
                                                  ? rtFsIsoMakerOutFile_ReadIso9660DirJoliet
                                                  : rtFsIsoMakerOutFile_ReadIso9660Dir;

        /*
         * Work thru the directories.
         */
        PRTFSISOMAKERNAMEDIR pCurDir;
        RTListForEach(&pNamespace->FinalizedDirs, pCurDir, RTFSISOMAKERNAMEDIR, FinalizedEntry)
        {
            PRTFSISOMAKERNAME pCurName    = pCurDir->pName;
            PRTFSISOMAKERNAME pParentName = pCurName->pParent ? pCurName->pParent : pCurName;

            /* We don't do anything special for the special '.' and '..' directory
               entries, instead we use the directory entry in the parent directory
               with a 1 byte name (00 or 01). */
            /** @todo r=bird: This causes trouble with RR NM records, since we'll be
             *        emitting the real directory name rather than '.' or '..' (or
             *        whatever we should be emitting for these two special dirs).
             *        FreeBSD got confused with this.  The RTFSISOMAKERDIRTYPE stuff is a
             *        workaround for this, however it doesn't hold up if we have to use
             *        the spill file. [RR NM ./..] */
            Assert(pCurName->cbDirRec != 0);
            Assert(pParentName->cbDirRec != 0);
            pCurDir->cbDirRec00 = pCurName->cbDirRec    - pCurName->cbNameInDirRec    - !(pCurName->cbNameInDirRec    & 1) + 1;
            pCurDir->cbDirRec01 = pParentName->cbDirRec - pParentName->cbNameInDirRec - !(pParentName->cbNameInDirRec & 1) + 1;

            uint32_t offInDir   = (uint32_t)pCurDir->cbDirRec00 + pCurDir->cbDirRec01;

            /* Finalize the directory entries. */
            uint32_t            cSubDirs    = 0;
            uint32_t            cbTransTbl  = 0;
            uint32_t            cLeft       = pCurDir->cChildren;
            PPRTFSISOMAKERNAME  ppChild     = pCurDir->papChildren;
            while (cLeft-- > 0)
            {
                PRTFSISOMAKERNAME pChild = *ppChild++;
                rc = rtFsIsoMakerFinalizeIsoDirectoryEntry(pNamespace, pChild, offInDir,
                                                           pNamespace->uRockRidgeLevel, false /*fIsRoot*/);
                AssertRCReturn(rc, rc);

                if ((RTFSISOMAKER_SECTOR_SIZE - (offInDir & RTFSISOMAKER_SECTOR_OFFSET_MASK)) < pChild->cbDirRecTotal)
                {
                    Assert(ppChild[-1] == pChild && &ppChild[-1] != pCurDir->papChildren);
                    if (   pChild->cDirRecs == 1
                        || pChild->cDirRecs <= RTFSISOMAKER_SECTOR_SIZE / pChild->cbDirRec)
                    {
                        ppChild[-2]->cbDirRecTotal += RTFSISOMAKER_SECTOR_SIZE - (offInDir & RTFSISOMAKER_SECTOR_OFFSET_MASK);
                        offInDir = (offInDir | RTFSISOMAKER_SECTOR_OFFSET_MASK) + 1; /* doesn't fit, skip to next sector. */
                        Log4(("rtFsIsoMakerFinalizeDirectoriesInIsoNamespace: zero padding dir rec @%#x: %#x -> %#x; offset %#x -> %#x\n",
                              ppChild[-2]->offDirRec, ppChild[-2]->cbDirRec, ppChild[-2]->cbDirRecTotal, pChild->offDirRec, offInDir));
                        pChild->offDirRec = offInDir;
                    }
                    /* else: too complicated and ulikely, so whatever. */
                }

                offInDir += pChild->cbDirRecTotal;
                if (pChild->cchTransNm)
                    cbTransTbl += 2 /* type & space*/
                               +  RT_MAX(pChild->cchName, RTFSISOMAKER_TRANS_TBL_LEFT_PAD)
                               +  1 /* tab */
                               +  pChild->cchTransNm
                               +  1 /* newline */;

                if (RTFS_IS_DIRECTORY(pChild->fMode))
                    cSubDirs++;
            }

            /* Set the directory size and allocate space for it on the disk. */
            pCurDir->cbDir     = offInDir;
            rtFsIsoMakerAllocateAndAddByteRange(pThis, pAllocator, &pCurDir->RangeNode, offInDir, pfnReadDir, true /*fIso9660Only*/);

            /* Set the translation table file size. */
            if (pCurDir->pTransTblFile)
            {
                pCurDir->pTransTblFile->cbData = cbTransTbl;
                pThis->cbData += RT_ALIGN_32(cbTransTbl, RTFSISOMAKER_SECTOR_SIZE);
            }

            /* Add to the path table size calculation. */
            pCurDir->offPathTable = cbPathTable;
            pCurDir->idPathTable  = idPathTable++;
            cbPathTable += RTFSISOMAKER_CALC_PATHREC_SIZE(pCurName->cbNameInDirRec);

            /* Set the hardlink count. */
            pCurName->cHardlinks = cSubDirs + 2;

            Log4(("rtFsIsoMakerFinalizeDirectoriesInIsoNamespace: idxObj=#%#x cbDir=%#08x cChildren=%#05x %s\n",
                  pCurDir->pName->pObj->idxObj, pCurDir->cbDir, pCurDir->cChildren, pCurDir->pName->szName));
        }
    }

    /*
     * Remove rock ridge spill file if we haven't got any spill.
     * If we have, round the size up to a whole sector to avoid the slow path
     * when reading from it.
     */
    if (pNamespace->pRRSpillFile)
    {
        if (pNamespace->pRRSpillFile->cbData > 0)
        {
            pNamespace->pRRSpillFile->cbData = RT_ALIGN_64(pNamespace->pRRSpillFile->cbData, ISO9660_SECTOR_SIZE);
            pThis->cbData += pNamespace->pRRSpillFile->cbData;
        }
        else
        {
            rc = rtFsIsoMakerObjRemoveWorker(pThis, &pNamespace->pRRSpillFile->Core);
            if (RT_SUCCESS(rc))
                pNamespace->pRRSpillFile = NULL;
        }
    }

    /*
     * Allocate space for the path tables and register reader callbacks.
     */
    pNamespace->cbPathTable = cbPathTable;
    if (cbPathTable > 0)
    {
        rtFsIsoMakerAllocateAndAddByteRange(pThis, pAllocator, &pNamespace->RangeNodePathTableL, cbPathTable,
                                            rtFsIsoMakerOutFile_ReadIso9660PathTableLittle, true /*fIso9660Only*/);
        rtFsIsoMakerAllocateAndAddByteRange(pThis, pAllocator, &pNamespace->RangeNodePathTableM, cbPathTable,
                                            rtFsIsoMakerOutFile_ReadIso9660PathTableBig,    true /*fIso9660Only*/);
    }
    else
        pNamespace->RangeNodePathTableL.Core.Key
            = pNamespace->RangeNodePathTableL.Core.KeyLast
            = pNamespace->RangeNodePathTableM.Core.Key
            = pNamespace->RangeNodePathTableM.Core.KeyLast
            = rtFsIsoMakerAllocateRange(pAllocator, 0, true /*f9660Only*/); /* paranoia */

    return VINF_SUCCESS;
}


/**
 * Finalizes the UDF namespace.
 *
 * @returns IPRT status code
 * @param   pThis           The ISO maker instance.
 * @param   pNamespace      The UDF namespace.
 * @param   pAllocator      The data allocator (in/out).  We will allocate
 *                          blocks for the directories and the path tables.
 */
static int rtFsIsoMakerFinalizeDirectoriesInUdfNamespace(PRTFSISOMAKERINT pThis, PRTFSISOMAKERNAMESPACE pNamespace,
                                                         PRTFSISOMAKERALLOCATOR pAllocator)
{
    PRTFSISOMAKERNAME const pRoot = pNamespace->pRoot;
    if (!pRoot)
        return VINF_SUCCESS;

    AssertReturn(   pThis->offUdfPartition > 256U * RTFSISOMAKER_SECTOR_SIZE
                 && !(pThis->offUdfPartition & RTFSISOMAKER_SECTOR_OFFSET_MASK),
                 VERR_INTERNAL_ERROR_3);

    /*
     * Allocate ICBs for the directories and all their children, and entering
     * them into the papUdfIcbToObjs table.
     */
    pThis->papUdfIcbToObjs = (PRTFSISOMAKEROBJ *)RTMemAllocZ(sizeof(pThis->papUdfIcbToObjs[0]) * pNamespace->cNames);
    AssertReturn(pThis->papUdfIcbToObjs, VERR_NO_MEMORY);

    uint64_t const       offFirstIcb = pAllocator->offUdf;
    size_t               idx         = 0;
    PRTFSISOMAKERNAMEDIR pCurDir;
    RTListForEach(&pNamespace->FinalizedDirs, pCurDir, RTFSISOMAKERNAMEDIR, FinalizedEntry)
    {
        /* Self: */
        if (pCurDir->pName->uUdfIcbSector == UINT32_MAX)
        {
            pCurDir->pName->uUdfIcbSector = rtFsIsoMakerAllocateUdfIcb(pThis, pAllocator);
            Assert(idx < pNamespace->cNames);
            pThis->papUdfIcbToObjs[idx++] = pCurDir->pName->pObj;
        }

        /* Children: */
        uint32_t           cLeft   = pCurDir->cChildren;
        PPRTFSISOMAKERNAME ppChild = pCurDir->papChildren;
        while (cLeft-- > 0)
        {
            PRTFSISOMAKERNAME const pChild = *ppChild++;
            if (pChild->uUdfIcbSector == UINT32_MAX)
            {
                pChild->uUdfIcbSector = rtFsIsoMakerAllocateUdfIcb(pThis, pAllocator);
                Assert(idx < pNamespace->cNames);
                pThis->papUdfIcbToObjs[idx++] = pChild->pObj;
            }
        }
    }
    Assert((pAllocator->offUdf - offFirstIcb) / RTFSISOMAKER_SECTOR_SIZE == pNamespace->cNames);
    rtFsIsoMakerAddByteRange(pThis, &pThis->UdfIcbsForObjsRange, offFirstIcb, pAllocator->offUdf - offFirstIcb,
                             rtFsIsoMakerOutFile_ReadUdfIcbs);

    /*
     * We need to precalculate the size of the root directory.
     */
    Assert(pRoot->cchName == 0);
    pRoot->cbNameInDirRec = 0;
    pRoot->cbDirRec       = UDFFILEIDDESC_CALC_SIZE_EX(0, 0);
    pRoot->cDirRecs       = 1;
    pRoot->cbDirRecTotal  = pRoot->cbDirRec;

    /*
     * Calculate directory sizes and data storage for them.
     */
    RTListForEach(&pNamespace->FinalizedDirs, pCurDir, RTFSISOMAKERNAMEDIR, FinalizedEntry)
    {
        PRTFSISOMAKERNAME const pCurName = pCurDir->pName;

        /* UDF only have the '..' directory, but we use cbDirRec00 for it. */
        Assert(pCurName->cbDirRec != 0);
        Assert((pCurName->pParent ? pCurName->pParent : pCurName)->cbDirRec != 0);
        pCurDir->cbDirRec01 = 0;
        uint32_t offInDir   = pCurDir->cbDirRec00 = RT_UOFFSETOF(UDFFILEIDDESC, abImplementationUse) + 2;
        Assert(!(offInDir & 3));

        /* Finalize the directory entries. */
        uint32_t            cSubDirs    = 0;
        uint32_t            cLeft       = pCurDir->cChildren;
        PPRTFSISOMAKERNAME  ppChild     = pCurDir->papChildren;
        while (cLeft-- > 0)
        {
            PRTFSISOMAKERNAME pChild       = *ppChild++;
            uint32_t const    cbDirRec     = UDFFILEIDDESC_CALC_SIZE_EX(0, (uint8_t)pChild->cbNameInDirRec);
            Assert(cbDirRec < RTFSISOMAKER_SECTOR_SIZE / 2);
            pChild->cbDirRec      = cbDirRec;
            pChild->cbDirRecTotal = cbDirRec;
            pChild->cDirRecs      = 1;

            if ((RTFSISOMAKER_SECTOR_SIZE - (offInDir & RTFSISOMAKER_SECTOR_OFFSET_MASK)) >= pChild->cbDirRecTotal)
            { /* not crossing any boundary */ }
            else
            {
                /* Doesn't fit. Pad the previous FID so we start this in the next sector. */
                Assert(ppChild[-1] == pChild && &ppChild[-1] != pCurDir->papChildren);
                PRTFSISOMAKERNAME const pPrevChild = ppChild[-2];
                pPrevChild->cbDirRecTotal += RTFSISOMAKER_SECTOR_SIZE - (offInDir & RTFSISOMAKER_SECTOR_OFFSET_MASK);
                offInDir = (offInDir | RTFSISOMAKER_SECTOR_OFFSET_MASK) + 1;
            }

            pChild->offDirRec = offInDir;
            offInDir         += cbDirRec;

            if (pChild->pDir)
                cSubDirs++;
        }

        /* Set the directory size and allocate space for it on the disk. */
        pCurDir->cbDir = offInDir;
        rtFsIsoMakerAllocateAndAddByteRange(pThis, pAllocator, &pCurDir->RangeNode, offInDir, rtFsIsoMakerOutFile_ReadUdfDir);

        /* Set the hardlink count. */
        pCurName->cHardlinks = cSubDirs + 1;

        Log4(("rtFsIsoMakerFinalizeDirectoriesInUdfNamespace: idxObj=#%#x cbDir=%#08x @ %#RX64 ICB=%#x cChildren=%#05x %s\n",
              pCurName->pObj->idxObj, pCurDir->cbDir, pCurDir->RangeNode.Core.Key, pCurName->uUdfIcbSector, pCurDir->cChildren,
              pCurName->szName));
    }

    return VINF_SUCCESS;
}



/**
 * Finalizes directories and related stuff.
 *
 * This will not generate actual directory data, but calculate the size of it
 * once it's generated.  Ditto for the path tables.  The exception is the rock
 * ridge spill file, which will be generated in memory.
 *
 * @returns IPRT status code.
 * @param   pThis               The ISO maker instance.
 * @param   pAllocator          The data allocator (in/out).
 */
static int rtFsIsoMakerFinalizeDirectories(PRTFSISOMAKERINT pThis, PRTFSISOMAKERALLOCATOR pAllocator)
{
    /*
     * Locate the directories, width first, inserting them in the finalized lists so
     * we can process them efficiently.
     */
    rtFsIsoMakerFinalizeGatherDirs(&pThis->PrimaryIso);
    rtFsIsoMakerFinalizeGatherDirs(&pThis->Joliet);
    rtFsIsoMakerFinalizeGatherDirs(&pThis->Udf);

    /*
     * Process the primary ISO and joliet namespaces.
     */
    int rc = rtFsIsoMakerFinalizeDirectoriesInIsoNamespace(pThis, &pThis->PrimaryIso, pAllocator);
    if (RT_SUCCESS(rc))
        rc = rtFsIsoMakerFinalizeDirectoriesInIsoNamespace(pThis, &pThis->Joliet, pAllocator);
    if (RT_SUCCESS(rc))
    {
        /*
         * Finalize UDF directories.
         */
        rc = rtFsIsoMakerFinalizeDirectoriesInUdfNamespace(pThis, &pThis->Udf, pAllocator);

        /*
         * Later: HFS.
         */
    }
    return rc;
}


/**
 * Finalizes data allocations.
 *
 * This will set the RTFSISOMAKERFILE::offData members.
 *
 * @returns IPRT status code.
 * @param   pThis               The ISO maker instance.
 * @param   pAllocator          The data allocator (in/out).
 */
static int rtFsIsoMakerFinalizeData(PRTFSISOMAKERINT pThis, PRTFSISOMAKERALLOCATOR pAllocator)
{
    /*
     * We currently does not have any ordering prioritizing implemented, so we
     * just store files in the order they were added.
     */
    PRTFSISOMAKEROBJ pCur;
    RTListForEach(&pThis->ObjectHead, pCur, RTFSISOMAKEROBJ, Entry)
    {
        if (pCur->enmType == RTFSISOMAKEROBJTYPE_FILE)
        {
            PRTFSISOMAKERFILE const pCurFile = (PRTFSISOMAKERFILE)pCur;
            if (pCurFile->RangeNode.Core.Key == UINT64_MAX)
            {
                rtFsIsoMakerAllocateAndAddByteRange(pThis, pAllocator, &pCurFile->RangeNode, pCurFile->cbData,
                                                    rtFsIsoMakerOutFile_ReadFileData,
                                                       (pCurFile->Core.pUdfName == NULL || pThis->Udf.uLevel == 0)
                                                    && (pCurFile->Core.pHfsName == NULL || pThis->Hfs.uLevel == 0) /*f9660Only*/);
                RTListAppend(&pThis->FinalizedFiles, &pCurFile->FinalizedEntry);
                Log4(("rtFsIsoMakerFinalizeData: %#x @%#RX64 cbData=%#RX64\n",
                      pCurFile->Core.idxObj, pCurFile->RangeNode.Core.Key, pCurFile->cbData));
            }

            /*
             * Create the boot info table.
             */
            if (pCurFile->pBootInfoTable)
            {
                /*
                 * Checksum the file.
                 */
                int rc;
                RTVFSFILE hVfsFile;
                uint64_t  offBase;
                switch (pCurFile->enmSrcType)
                {
                    case RTFSISOMAKERSRCTYPE_PATH:
                        rc = RTVfsChainOpenFile(pCurFile->u.pszSrcPath, RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                                                &hVfsFile, NULL, NULL);
                        AssertMsgRCReturn(rc, ("%s -> %Rrc\n", pCurFile->u.pszSrcPath, rc), rc);
                        offBase = 0;
                        break;
                    case RTFSISOMAKERSRCTYPE_VFS_FILE:
                        hVfsFile = pCurFile->u.hVfsFile;
                        offBase = 0;
                        rc = VINF_SUCCESS;
                        break;
                    case RTFSISOMAKERSRCTYPE_COMMON:
                        hVfsFile = pThis->paCommonSources[pCurFile->u.Common.idxSrc];
                        offBase  = pCurFile->u.Common.offData;
                        rc = VINF_SUCCESS;
                        break;
                    default:
                        AssertMsgFailedReturn(("enmSrcType=%d\n", pCurFile->enmSrcType), VERR_IPE_NOT_REACHED_DEFAULT_CASE);
                }

                uint32_t uChecksum = 0;
                uint32_t off       = 64;
                uint32_t cbLeft    = RT_MAX(64, (uint32_t)pCurFile->cbData) - 64;
                while (cbLeft > 0)
                {
                    union
                    {
                        uint8_t     ab[_16K];
                        uint32_t    au32[_16K / sizeof(uint32_t)];
                    }        uBuf;
                    uint32_t cbRead = RT_MIN(sizeof(uBuf), cbLeft);
                    if (cbRead & 3)
                        RT_ZERO(uBuf);
                    rc = RTVfsFileReadAt(hVfsFile, offBase + off, &uBuf, cbRead, NULL);
                    if (RT_FAILURE(rc))
                        break;

                    size_t i = RT_ALIGN_Z(cbRead, sizeof(uint32_t)) / sizeof(uint32_t);
                    while (i-- > 0)
                        uChecksum += RT_LE2H_U32(uBuf.au32[i]);

                    off    += cbRead;
                    cbLeft -= cbRead;
                }

                if (pCurFile->enmSrcType == RTFSISOMAKERSRCTYPE_PATH)
                    RTVfsFileRelease(hVfsFile);
                if (RT_FAILURE(rc))
                    return rc;

                /*
                 * Populate the structure.
                 */
                pCurFile->pBootInfoTable->offPrimaryVolDesc = RT_H2LE_U32(16);
                pCurFile->pBootInfoTable->offBootFile       = RT_H2LE_U32((uint32_t)(  pCurFile->RangeNode.Core.Key
                                                                                     / RTFSISOMAKER_SECTOR_SIZE));
                pCurFile->pBootInfoTable->cbBootFile        = RT_H2LE_U32((uint32_t)pCurFile->cbData);
                pCurFile->pBootInfoTable->uChecksum         = RT_H2LE_U32(uChecksum);
                RT_ZERO(pCurFile->pBootInfoTable->auReserved);
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Copies the given string as UTF-16 and pad unused space in the destination
 * with spaces.
 *
 * @param   pachDst     The destination field.  C type is char, but real life
 *                      type is UTF-16 / UCS-2.
 * @param   cchDst      The size of the destination field.
 * @param   pszSrc      The source string. NULL is treated like empty string.
 */
static void rtFsIsoMakerFinalizeCopyAsUtf16BigAndSpacePad(char *pachDst, size_t cchDst, const char *pszSrc)
{
    size_t cwcSrc = 0;
    if (pszSrc)
    {
        RTUTF16  wszSrc[256];
        PRTUTF16 pwszSrc = wszSrc;
        int rc = RTStrToUtf16BigEx(pszSrc, RTSTR_MAX, &pwszSrc, RT_ELEMENTS(wszSrc), &cwcSrc);
        AssertRCStmt(rc, cwcSrc = 0);

        if (cwcSrc > cchDst / sizeof(RTUTF16))
            cwcSrc = cchDst / sizeof(RTUTF16);
        memcpy(pachDst, wszSrc, cwcSrc * sizeof(RTUTF16));
    }

    /* Space padding.  Note! cchDst can be an odd number. */
    size_t cchWritten = cwcSrc * sizeof(RTUTF16);
    if (cchWritten < cchDst)
    {
        while (cchWritten + 2 <= cchDst)
        {
            pachDst[cchWritten++] = '\0';
            pachDst[cchWritten++] = ' ';
        }
        if (cchWritten < cchDst)
            pachDst[cchWritten] = '\0';
    }
}


/**
 * Copies the given string and pad unused space in the destination with spaces.
 *
 * @param   pachDst     The destination field.
 * @param   cchDst      The size of the destination field.
 * @param   pszSrc      The source string. NULL is treated like empty string.
 */
static void rtFsIsoMakerFinalizeCopyAndSpacePad(char *pachDst, size_t cchDst, const char *pszSrc)
{
    size_t cchSrc;
    if (!pszSrc)
        cchSrc = 0;
    else
    {
        cchSrc = strlen(pszSrc);
        if (cchSrc > cchDst)
            cchSrc = cchDst;
        memcpy(pachDst, pszSrc, cchSrc);
    }
    if (cchSrc < cchDst)
        memset(&pachDst[cchSrc], ' ', cchDst - cchSrc);
}


/**
 * Formats a timespec as an ISO-9660 ascii timestamp.
 *
 * @param   pTime       The timespec to format.
 * @param   pIsoTs      The ISO-9660 timestamp destination buffer.
 */
static void rtFsIsoMakerTimespecToIso9660Timestamp(PCRTTIMESPEC pTime, PISO9660TIMESTAMP pIsoTs)
{
    RTTIME Exploded;
    RTTimeExplode(&Exploded, pTime);

    char szTmp[64];
#define FORMAT_FIELD(a_achDst, a_uSrc) \
        do { \
            RTStrFormatU32(szTmp, sizeof(szTmp), a_uSrc, 10, sizeof(a_achDst), sizeof(a_achDst), \
                           RTSTR_F_ZEROPAD | RTSTR_F_WIDTH | RTSTR_F_PRECISION); \
            memcpy(a_achDst, szTmp, sizeof(a_achDst)); \
        } while (0)
    FORMAT_FIELD(pIsoTs->achYear,   Exploded.i32Year);
    FORMAT_FIELD(pIsoTs->achMonth,  Exploded.u8Month);
    FORMAT_FIELD(pIsoTs->achDay,    Exploded.u8MonthDay);
    FORMAT_FIELD(pIsoTs->achHour,   Exploded.u8Hour);
    FORMAT_FIELD(pIsoTs->achMinute, Exploded.u8Minute);
    FORMAT_FIELD(pIsoTs->achSecond, Exploded.u8Second);
    FORMAT_FIELD(pIsoTs->achCentisecond, Exploded.u32Nanosecond / RT_NS_10MS);
#undef FORMAT_FIELD
    pIsoTs->offUtc = 0;
}

/**
 * Formats zero ISO-9660 ascii timestamp (treated as not specified).
 *
 * @param   pIsoTs      The ISO-9660 timestamp destination buffer.
 */
static void rtFsIsoMakerZero9660Timestamp(PISO9660TIMESTAMP pIsoTs)
{
    memset(pIsoTs, '0', RT_UOFFSETOF(ISO9660TIMESTAMP, offUtc));
    pIsoTs->offUtc = 0;
}


/**
 * Formats a timespec as an ISO-9660 record timestamp.
 *
 * @param   pTime       The timespec to format.
 * @param   pIsoTs      The ISO-9660 timestamp destination buffer.
 */
static void rtFsIsoMakerTimespecToIso9660RecTimestamp(PCRTTIMESPEC pTime, PISO9660RECTIMESTAMP pIsoRecTs)
{
    RTTIME Exploded;
    RTTimeExplode(&Exploded, pTime);

    pIsoRecTs->bYear    = Exploded.i32Year >= 1900 ? Exploded.i32Year - 1900 : 0;
    pIsoRecTs->bMonth   = Exploded.u8Month;
    pIsoRecTs->bDay     = Exploded.u8MonthDay;
    pIsoRecTs->bHour    = Exploded.u8Hour;
    pIsoRecTs->bMinute  = Exploded.u8Minute;
    pIsoRecTs->bSecond  = Exploded.u8Second;
    pIsoRecTs->offUtc   = 0;
}


/**
 * Allocate and prepare the volume descriptors.
 *
 * What's not done here gets done later by rtFsIsoMakerFinalizeBootStuffPart2,
 * or at the very end of the finalization by
 * rtFsIsoMakerFinalizeVolumeDescriptors.
 *
 * @returns IPRT status code
 * @param   pThis               The ISO maker instance.
 */
static int rtFsIsoMakerFinalizePrepVolumeDescriptors(PRTFSISOMAKERINT pThis)
{
    /** @todo Replace this by producer callbacks. */

    /*
     * Allocate and calc pointers.
     */
    RTMemFree(pThis->pbVolDescs);
    pThis->pbVolDescs = (uint8_t *)RTMemAllocZ(pThis->cVolumeDescriptors * RTFSISOMAKER_SECTOR_SIZE);
    AssertReturn(pThis->pbVolDescs, VERR_NO_MEMORY);

    uint32_t offVolDescs = 0;

    pThis->pPrimaryVolDesc = (PISO9660PRIMARYVOLDESC)&pThis->pbVolDescs[offVolDescs];
    offVolDescs += RTFSISOMAKER_SECTOR_SIZE;

    if (!pThis->pBootCatFile)
        pThis->pElToritoDesc = NULL;
    else
    {
        pThis->pElToritoDesc = (PISO9660BOOTRECORDELTORITO)&pThis->pbVolDescs[offVolDescs];
        offVolDescs += RTFSISOMAKER_SECTOR_SIZE;
    }

    if (!pThis->Joliet.uLevel)
        pThis->pJolietVolDesc = NULL;
    else
    {
        pThis->pJolietVolDesc = (PISO9660SUPVOLDESC)&pThis->pbVolDescs[offVolDescs];
        offVolDescs += RTFSISOMAKER_SECTOR_SIZE;
    }

    pThis->pTerminatorVolDesc = (PISO9660VOLDESCHDR)&pThis->pbVolDescs[offVolDescs];
    offVolDescs += RTFSISOMAKER_SECTOR_SIZE;


    if (pThis->Udf.uLevel > 0)
    {
        /* Just do it here as these are just headers w/o any data. */
        static const char s_aIds[3][6] =
        { UDF_EXT_VOL_DESC_STD_ID_BEGIN, UDF_EXT_VOL_DESC_STD_ID_NSR_02, UDF_EXT_VOL_DESC_STD_ID_TERM };
        for (unsigned i = 0; i < 3; i++)
        {
            PISO9660VOLDESCHDR const pHdr = (PISO9660VOLDESCHDR)&pThis->pbVolDescs[offVolDescs];
            offVolDescs += RTFSISOMAKER_SECTOR_SIZE;
            AssertReturn(offVolDescs <= pThis->cVolumeDescriptors * RTFSISOMAKER_SECTOR_SIZE, VERR_ISOMK_IPE_DESC_COUNT);
            pHdr->bDescType             = UDF_EXT_VOL_DESC_TYPE;
            pHdr->bDescVersion          = UDF_EXT_VOL_DESC_VERSION;
            memcpy(pHdr->achStdId, s_aIds[i], sizeof(pHdr->achStdId));
        }
    }
    AssertReturn(offVolDescs == pThis->cVolumeDescriptors * RTFSISOMAKER_SECTOR_SIZE, VERR_ISOMK_IPE_DESC_COUNT);

    /*
     * This may be needed later.
     */
    char szImageCreationTime[42];
    RTTimeSpecToString(&pThis->ImageCreationTime, szImageCreationTime, sizeof(szImageCreationTime));

    /*
     * Initialize the primary descriptor.
     */
    PISO9660PRIMARYVOLDESC pPrimary = pThis->pPrimaryVolDesc;

    pPrimary->Hdr.bDescType             = ISO9660VOLDESC_TYPE_PRIMARY;
    pPrimary->Hdr.bDescVersion          = ISO9660PRIMARYVOLDESC_VERSION;
    memcpy(pPrimary->Hdr.achStdId, ISO9660VOLDESC_STD_ID, sizeof(pPrimary->Hdr.achStdId));
    //pPrimary->bPadding8               = 0;
    rtFsIsoMakerFinalizeCopyAndSpacePad(pPrimary->achSystemId, sizeof(pPrimary->achSystemId), pThis->PrimaryIso.pszSystemId);
    rtFsIsoMakerFinalizeCopyAndSpacePad(pPrimary->achVolumeId, sizeof(pPrimary->achVolumeId),
                                        pThis->PrimaryIso.pszVolumeId ? pThis->PrimaryIso.pszVolumeId : szImageCreationTime);
    //pPrimary->Unused73                = {0}
    //pPrimary->VolumeSpaceSize         = later
    //pPrimary->abUnused89              = {0}
    pPrimary->cVolumesInSet.be          = RT_H2BE_U16_C(1);
    pPrimary->cVolumesInSet.le          = RT_H2LE_U16_C(1);
    pPrimary->VolumeSeqNo.be            = RT_H2BE_U16_C(1);
    pPrimary->VolumeSeqNo.le            = RT_H2LE_U16_C(1);
    pPrimary->cbLogicalBlock.be         = RT_H2BE_U16_C(RTFSISOMAKER_SECTOR_SIZE);
    pPrimary->cbLogicalBlock.le         = RT_H2LE_U16_C(RTFSISOMAKER_SECTOR_SIZE);
    //pPrimary->cbPathTable             = later
    //pPrimary->offTypeLPathTable       = later
    //pPrimary->offOptionalTypeLPathTable = {0}
    //pPrimary->offTypeMPathTable       = later
    //pPrimary->offOptionalTypeMPathTable = {0}
    //pPrimary->RootDir                 = later
    rtFsIsoMakerFinalizeCopyAndSpacePad(pPrimary->achVolumeSetId, sizeof(pPrimary->achVolumeSetId),
                                        pThis->PrimaryIso.pszVolumeSetId);
    rtFsIsoMakerFinalizeCopyAndSpacePad(pPrimary->achPublisherId, sizeof(pPrimary->achPublisherId),
                                        pThis->PrimaryIso.pszPublisherId);
    rtFsIsoMakerFinalizeCopyAndSpacePad(pPrimary->achDataPreparerId, sizeof(pPrimary->achDataPreparerId),
                                        pThis->PrimaryIso.pszDataPreparerId);
    rtFsIsoMakerFinalizeCopyAndSpacePad(pPrimary->achApplicationId, sizeof(pPrimary->achApplicationId),
                                        pThis->PrimaryIso.pszApplicationId);
    rtFsIsoMakerFinalizeCopyAndSpacePad(pPrimary->achCopyrightFileId, sizeof(pPrimary->achCopyrightFileId),
                                        pThis->PrimaryIso.pszCopyrightFileId);
    rtFsIsoMakerFinalizeCopyAndSpacePad(pPrimary->achAbstractFileId, sizeof(pPrimary->achAbstractFileId),
                                        pThis->PrimaryIso.pszAbstractFileId);
    rtFsIsoMakerFinalizeCopyAndSpacePad(pPrimary->achBibliographicFileId, sizeof(pPrimary->achBibliographicFileId),
                                        pThis->PrimaryIso.pszBibliographicFileId);
    rtFsIsoMakerTimespecToIso9660Timestamp(&pThis->ImageCreationTime, &pPrimary->BirthTime);
    rtFsIsoMakerTimespecToIso9660Timestamp(&pThis->ImageCreationTime, &pPrimary->ModifyTime);
    rtFsIsoMakerZero9660Timestamp(&pPrimary->ExpireTime);
    rtFsIsoMakerZero9660Timestamp(&pPrimary->EffectiveTime);
    pPrimary->bFileStructureVersion     = ISO9660_FILE_STRUCTURE_VERSION;
    //pPrimary->bReserved883            = 0;
    //RT_ZERO(pPrimary->abAppUse);
    //RT_ZERO(pPrimary->abReserved1396);

    /*
     * Initialize the joliet descriptor if included.
     */
    PISO9660SUPVOLDESC pJoliet = pThis->pJolietVolDesc;
    if (pJoliet)
    {
        pJoliet->Hdr.bDescType              = ISO9660VOLDESC_TYPE_SUPPLEMENTARY;
        pJoliet->Hdr.bDescVersion           = ISO9660SUPVOLDESC_VERSION;
        memcpy(pJoliet->Hdr.achStdId, ISO9660VOLDESC_STD_ID, sizeof(pJoliet->Hdr.achStdId));
        pJoliet->fVolumeFlags               = ISO9660SUPVOLDESC_VOL_F_ESC_ONLY_REG;
        rtFsIsoMakerFinalizeCopyAsUtf16BigAndSpacePad(pJoliet->achSystemId, sizeof(pJoliet->achSystemId), pThis->Joliet.pszSystemId);
        rtFsIsoMakerFinalizeCopyAsUtf16BigAndSpacePad(pJoliet->achVolumeId, sizeof(pJoliet->achVolumeId),
                                                      pThis->Joliet.pszVolumeId ? pThis->Joliet.pszVolumeId : szImageCreationTime);
        //pJoliet->Unused73                 = {0}
        //pJoliet->VolumeSpaceSize          = later
        memset(pJoliet->abEscapeSequences, ' ', sizeof(pJoliet->abEscapeSequences));
        pJoliet->abEscapeSequences[0]       = ISO9660_JOLIET_ESC_SEQ_0;
        pJoliet->abEscapeSequences[1]       = ISO9660_JOLIET_ESC_SEQ_1;
        pJoliet->abEscapeSequences[2]       = pThis->Joliet.uLevel == 1 ? ISO9660_JOLIET_ESC_SEQ_2_LEVEL_1
                                            : pThis->Joliet.uLevel == 2 ? ISO9660_JOLIET_ESC_SEQ_2_LEVEL_2
                                            :                             ISO9660_JOLIET_ESC_SEQ_2_LEVEL_3;
        pJoliet->cVolumesInSet.be           = RT_H2BE_U16_C(1);
        pJoliet->cVolumesInSet.le           = RT_H2LE_U16_C(1);
        pJoliet->VolumeSeqNo.be             = RT_H2BE_U16_C(1);
        pJoliet->VolumeSeqNo.le             = RT_H2LE_U16_C(1);
        pJoliet->cbLogicalBlock.be          = RT_H2BE_U16_C(RTFSISOMAKER_SECTOR_SIZE);
        pJoliet->cbLogicalBlock.le          = RT_H2LE_U16_C(RTFSISOMAKER_SECTOR_SIZE);
        //pJoliet->cbPathTable              = later
        //pJoliet->offTypeLPathTable        = later
        //pJoliet->offOptionalTypeLPathTable = {0}
        //pJoliet->offTypeMPathTable        = later
        //pJoliet->offOptionalTypeMPathTable = {0}
        //pJoliet->RootDir                  = later
        rtFsIsoMakerFinalizeCopyAsUtf16BigAndSpacePad(pJoliet->achVolumeSetId, sizeof(pJoliet->achVolumeSetId),
                                                      pThis->Joliet.pszVolumeSetId);
        rtFsIsoMakerFinalizeCopyAsUtf16BigAndSpacePad(pJoliet->achPublisherId, sizeof(pJoliet->achPublisherId),
                                                      pThis->Joliet.pszPublisherId);
        rtFsIsoMakerFinalizeCopyAsUtf16BigAndSpacePad(pJoliet->achDataPreparerId, sizeof(pJoliet->achDataPreparerId),
                                                      pThis->Joliet.pszDataPreparerId);
        rtFsIsoMakerFinalizeCopyAsUtf16BigAndSpacePad(pJoliet->achApplicationId, sizeof(pJoliet->achApplicationId),
                                                      pThis->Joliet.pszApplicationId);
        rtFsIsoMakerFinalizeCopyAsUtf16BigAndSpacePad(pJoliet->achCopyrightFileId, sizeof(pJoliet->achCopyrightFileId),
                                                      pThis->Joliet.pszCopyrightFileId);
        rtFsIsoMakerFinalizeCopyAsUtf16BigAndSpacePad(pJoliet->achAbstractFileId, sizeof(pJoliet->achAbstractFileId),
                                                      pThis->Joliet.pszAbstractFileId);
        rtFsIsoMakerFinalizeCopyAsUtf16BigAndSpacePad(pJoliet->achBibliographicFileId, sizeof(pJoliet->achBibliographicFileId),
                                                      pThis->Joliet.pszBibliographicFileId);
        rtFsIsoMakerTimespecToIso9660Timestamp(&pThis->ImageCreationTime, &pJoliet->BirthTime);
        rtFsIsoMakerTimespecToIso9660Timestamp(&pThis->ImageCreationTime, &pJoliet->ModifyTime);
        rtFsIsoMakerZero9660Timestamp(&pJoliet->ExpireTime);
        rtFsIsoMakerZero9660Timestamp(&pJoliet->EffectiveTime);
        pJoliet->bFileStructureVersion      = ISO9660_FILE_STRUCTURE_VERSION;
        //pJoliet->bReserved883             = 0;
        //RT_ZERO(pJoliet->abAppUse);
        //RT_ZERO(pJoliet->abReserved1396);
    }

    /*
     * The ISO-9660 terminator descriptor.
     */
    pThis->pTerminatorVolDesc->bDescType    = ISO9660VOLDESC_TYPE_TERMINATOR;
    pThis->pTerminatorVolDesc->bDescVersion = 1;
    memcpy(pThis->pTerminatorVolDesc->achStdId, ISO9660VOLDESC_STD_ID, sizeof(pThis->pTerminatorVolDesc->achStdId));

    /* Register the image range for these descriptors. */
    rtFsIsoMakerAddSectorRange(pThis, &pThis->VolDescSeqRange, 16, pThis->cVolumeDescriptors,
                               rtFsIsoMakerOutFile_ReadIso9660Vds16);

    /*
     * Now for the UDF VDS stuff if enabled.
     */
    if (pThis->Udf.uLevel > 0)
    {
        uint32_t i         = 0;
        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProduceAnchorVolumeDescPtr;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = 0;

        /* Main VDS: */
        uint32_t idxVolSeq = 0;
        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProducePrimaryVolumeDesc;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = idxVolSeq++;

        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProduceLogicalVolumeDesc;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = idxVolSeq++;

        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProducePartitionDesc;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = idxVolSeq++;

        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProduceUnallocatedSpaceDesc;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = idxVolSeq++;

        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProduceImplementationUseVolumeDesc;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = idxVolSeq++;

        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProduceTerminatingDesc;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = idxVolSeq++;

        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProduceZero;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = 0;

        /* Integrity Sequence: */
        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProduceVolumeIntegrityDesc;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = 0;

        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProduceTerminatingDesc;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = 0;

        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProduceZero;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = 0;

        /* Reserve VDS: */
        idxVolSeq = 0;
        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProducePrimaryVolumeDesc;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = idxVolSeq++;

        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProduceLogicalVolumeDesc;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = idxVolSeq++;

        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProducePartitionDesc;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = idxVolSeq++;

        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProduceUnallocatedSpaceDesc;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = idxVolSeq++;

        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProduceImplementationUseVolumeDesc;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = idxVolSeq++;

        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProduceTerminatingDesc;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = idxVolSeq++;

        pThis->aUdfAvdpAndVdsEntries[i].pfnProducer = rtFsIsoMakerUdfProduceZero;
        pThis->aUdfAvdpAndVdsEntries[i++].uInfo     = 0;

        Assert(i <= RT_ELEMENTS(pThis->aUdfAvdpAndVdsEntries));
        pThis->cUdfAvdpAndVdsEntries = i;

        /* Register the image range for these descriptors. */
        rtFsIsoMakerAddSectorRange(pThis, &pThis->UdfAvdpAndVdsRange, 256, pThis->cUdfAvdpAndVdsEntries,
                                   rtFsIsoMakerOutFile_ReadUdfVds256);
        Log(("ISOMAKER/UDF: cUdfAvdpAndVdsEntries=%#x %#RX64..%#RX64\n",
             pThis->cUdfAvdpAndVdsEntries, pThis->UdfAvdpAndVdsRange.Core.Key, pThis->UdfAvdpAndVdsRange.Core.KeyLast));
    }

    return VINF_SUCCESS;
}


/**
 * Finalizes the volume descriptors.
 *
 * @returns IPRT status code.
 * @param   pThis               The ISO maker instance.
 */
static int rtFsIsoMakerFinalizeVolumeDescriptors(PRTFSISOMAKERINT pThis)
{
    AssertReturn(pThis->pbVolDescs && pThis->pPrimaryVolDesc && pThis->pTerminatorVolDesc, VERR_ISOMK_IPE_FINALIZE_1);

    /*
     * Primary descriptor.
     */
    PISO9660PRIMARYVOLDESC pPrimary = pThis->pPrimaryVolDesc;

    pPrimary->VolumeSpaceSize.be        = RT_H2BE_U32(pThis->cbFinalizedImage / RTFSISOMAKER_SECTOR_SIZE);
    pPrimary->VolumeSpaceSize.le        = RT_H2LE_U32(pThis->cbFinalizedImage / RTFSISOMAKER_SECTOR_SIZE);
    pPrimary->cbPathTable.be            = RT_H2BE_U32(pThis->PrimaryIso.cbPathTable);
    pPrimary->cbPathTable.le            = RT_H2LE_U32(pThis->PrimaryIso.cbPathTable);
    pPrimary->offTypeLPathTable         = RT_H2LE_U32(pThis->PrimaryIso.RangeNodePathTableL.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
    pPrimary->offTypeMPathTable         = RT_H2BE_U32(pThis->PrimaryIso.RangeNodePathTableM.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
    pPrimary->RootDir.DirRec.cbDirRec           = sizeof(pPrimary->RootDir);
    pPrimary->RootDir.DirRec.cExtAttrBlocks     = 0;
    pPrimary->RootDir.DirRec.offExtent.be       = RT_H2BE_U32(pThis->PrimaryIso.pRoot->pDir->RangeNode.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
    pPrimary->RootDir.DirRec.offExtent.le       = RT_H2LE_U32(pThis->PrimaryIso.pRoot->pDir->RangeNode.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
    pPrimary->RootDir.DirRec.cbData.be          = RT_H2BE_U32(pThis->PrimaryIso.pRoot->pDir->cbDir);
    pPrimary->RootDir.DirRec.cbData.le          = RT_H2LE_U32(pThis->PrimaryIso.pRoot->pDir->cbDir);
    rtFsIsoMakerTimespecToIso9660RecTimestamp(&pThis->PrimaryIso.pRoot->pObj->BirthTime, &pPrimary->RootDir.DirRec.RecTime);
    pPrimary->RootDir.DirRec.fFileFlags         = ISO9660_FILE_FLAGS_DIRECTORY;
    pPrimary->RootDir.DirRec.bFileUnitSize      = 0;
    pPrimary->RootDir.DirRec.bInterleaveGapSize = 0;
    pPrimary->RootDir.DirRec.VolumeSeqNo.be     = RT_H2BE_U16_C(1);
    pPrimary->RootDir.DirRec.VolumeSeqNo.le     = RT_H2LE_U16_C(1);
    pPrimary->RootDir.DirRec.bFileIdLength      = 1;
    pPrimary->RootDir.DirRec.achFileId[0]       = 0x00;

    /*
     * Initialize the joliet descriptor if included.
     */
    PISO9660SUPVOLDESC pJoliet = pThis->pJolietVolDesc;
    if (pJoliet)
    {
        pJoliet->VolumeSpaceSize            = pPrimary->VolumeSpaceSize;
        pJoliet->cbPathTable.be             = RT_H2BE_U32(pThis->Joliet.cbPathTable);
        pJoliet->cbPathTable.le             = RT_H2LE_U32(pThis->Joliet.cbPathTable);
        pJoliet->offTypeLPathTable          = RT_H2LE_U32(pThis->Joliet.RangeNodePathTableL.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
        pJoliet->offTypeMPathTable          = RT_H2BE_U32(pThis->Joliet.RangeNodePathTableM.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
        pJoliet->RootDir.DirRec.cbDirRec           = sizeof(pJoliet->RootDir);
        pJoliet->RootDir.DirRec.cExtAttrBlocks     = 0;
        pJoliet->RootDir.DirRec.offExtent.be       = RT_H2BE_U32(pThis->Joliet.pRoot->pDir->RangeNode.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
        pJoliet->RootDir.DirRec.offExtent.le       = RT_H2LE_U32(pThis->Joliet.pRoot->pDir->RangeNode.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
        pJoliet->RootDir.DirRec.cbData.be          = RT_H2BE_U32(pThis->Joliet.pRoot->pDir->cbDir);
        pJoliet->RootDir.DirRec.cbData.le          = RT_H2LE_U32(pThis->Joliet.pRoot->pDir->cbDir);
        rtFsIsoMakerTimespecToIso9660RecTimestamp(&pThis->Joliet.pRoot->pObj->BirthTime, &pJoliet->RootDir.DirRec.RecTime);
        pJoliet->RootDir.DirRec.fFileFlags         = ISO9660_FILE_FLAGS_DIRECTORY;
        pJoliet->RootDir.DirRec.bFileUnitSize      = 0;
        pJoliet->RootDir.DirRec.bInterleaveGapSize = 0;
        pJoliet->RootDir.DirRec.VolumeSeqNo.be     = RT_H2BE_U16_C(1);
        pJoliet->RootDir.DirRec.VolumeSeqNo.le     = RT_H2LE_U16_C(1);
        pJoliet->RootDir.DirRec.bFileIdLength      = 1;
        pJoliet->RootDir.DirRec.achFileId[0]       = 0x00;
    }

#if 0 /* this doesn't quite fool it. */
    /*
     * isomd5sum fake.
     */
    if (1)
    {
        uint8_t      abDigest[RTMD5_HASH_SIZE];
        if (pThis->cbSysArea == 0)
            RTMd5(g_abRTZero4K, ISO9660_SECTOR_SIZE, abDigest);
        else
        {
            RTMD5CONTEXT Ctx;
            RTMd5Init(&Ctx);
            RTMd5Update(&Ctx, pThis->pbSysArea, RT_MIN(pThis->cbSysArea, ISO9660_SECTOR_SIZE));
            if (pThis->cbSysArea < ISO9660_SECTOR_SIZE)
                RTMd5Update(&Ctx, g_abRTZero4K, ISO9660_SECTOR_SIZE - pThis->cbSysArea);
            RTMd5Final(abDigest, &Ctx);
        }
        char szFakeHash[RTMD5_DIGEST_LEN + 1];
        RTMd5ToString(abDigest, szFakeHash, sizeof(szFakeHash));

        size_t cch = RTStrPrintf((char *)&pPrimary->abAppUse[0], sizeof(pPrimary->abAppUse),
                                 "ISO MD5SUM = %s;SKIPSECTORS = %u;RHLISOSTATUS=1;THIS IS JUST A FAKE!",
                                 szFakeHash, pThis->cbFinalizedImage / RTFSISOMAKER_SECTOR_SIZE - 1);
        memset(&pPrimary->abAppUse[cch], ' ', sizeof(pPrimary->abAppUse) - cch);
    }
#endif

    return VINF_SUCCESS;
}


/**
 * Finalizes the image.
 *
 * @returns IPRT status code.
 * @param   hIsoMaker       The ISO maker handle.
 */
RTDECL(int) RTFsIsoMakerFinalize(RTFSISOMAKER hIsoMaker)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(!pThis->fFinalized, VERR_WRONG_ORDER);

    /*
     * Remove orphaned objects and allocate volume descriptors.
     */
    int rc = rtFsIsoMakerFinalizeRemoveOrphans(pThis);
    if (RT_FAILURE(rc))
        return rc;
    AssertReturn(pThis->cObjects > 0, VERR_NO_DATA);

    /* The primary ISO-9660 namespace must be explicitly disabled (for now),
       so we return VERR_NO_DATA if no root dir. */
    AssertReturn(pThis->PrimaryIso.pRoot || pThis->PrimaryIso.uLevel == 0, VERR_NO_DATA);

    /* Automatically disable the joliet namespace if it is empty (no root dir). */
    if (!pThis->Joliet.pRoot && pThis->Joliet.uLevel > 0)
    {
        pThis->Joliet.uLevel = 0;
        pThis->cVolumeDescriptors--;
    }

    /* Ditto for UDF. */
    if (!pThis->Udf.pRoot && pThis->Udf.uLevel > 0)
    {
        pThis->Udf.uLevel = 0;
        pThis->cVolumeDescriptors -= 3;
    }

    if (pThis->cbSysArea)
        rtFsIsoMakerAddByteRange(pThis, &pThis->SysAreaRange, 0, pThis->cbSysArea, rtFsIsoMakerOutFile_ReadSysArea);

    rc = rtFsIsoMakerFinalizePrepVolumeDescriptors(pThis);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * If there is any boot related stuff to be included, it ends up right after
     * the descriptors.
     */
    RTFSISOMAKERALLOCATOR Allocator;
    Allocator.offIso9660 = (16  + pThis->cVolumeDescriptors   ) * RTFSISOMAKER_SECTOR_SIZE;
    if (!pThis->Udf.uLevel)
        Allocator.offUdf = UINT64_MAX;
    else
    {
        Allocator.offUdf = (256 + pThis->cUdfAvdpAndVdsEntries) * RTFSISOMAKER_SECTOR_SIZE;
        rtFsIsoMakerAllocateAndAddByteRange(pThis, &Allocator, &pThis->UdfFileSetDescSeqRange,
                                            RT_ELEMENTS(s_aFileSetDescSeqEntries) * RTFSISOMAKER_SECTOR_SIZE,
                                            rtFsIsoMakerOutFile_ReadUdfFileSetDescSeq);
        pThis->offUdfPartition = pThis->UdfFileSetDescSeqRange.Core.Key;
        //rtFsIsoMakerAllocateRange(&Allocator, RTFSISOMAKER_SECTOR_SIZE); /* A zero block. */
    }

    rc = rtFsIsoMakerFinalizeBootStuffPart1(pThis);
    if (RT_SUCCESS(rc))
    {
        /*
         * Directories and path tables comes next.
         */
        rc = rtFsIsoMakerFinalizeDirectories(pThis, &Allocator);
        if (RT_SUCCESS(rc))
        {
            /*
             * Then we store the file data.
             */
            rc = rtFsIsoMakerFinalizeData(pThis, &Allocator);
            if (RT_SUCCESS(rc))
            {
                if (pThis->Udf.uLevel > 0)
                {
                    /** @todo Ignores cbImagePadding... */
                    rtFsIsoMakerAllocateAndAddByteRange(pThis, &Allocator, &pThis->UdfFinalAvdpRange, RTFSISOMAKER_SECTOR_SIZE,
                                                        rtFsIsoMakerOutFile_ReadUdfAvdp);
                    pThis->cbFinalizedImage     = Allocator.offUdf;
                    pThis->cUdfPartitionSectors = (uint32_t)(  (pThis->cbFinalizedImage - pThis->offUdfPartition)
                                                             / RTFSISOMAKER_SECTOR_SIZE);
                }
                else
                    pThis->cbFinalizedImage = Allocator.offIso9660 + pThis->cbImagePadding;

                /*
                 * Do a 2nd pass over the boot stuff to finalize locations.
                 */
                rc = rtFsIsoMakerFinalizeBootStuffPart2(pThis);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Finally, finalize the volume descriptors as they depend on some of the
                     * block allocations done in the previous steps.
                     */
                    rc = rtFsIsoMakerFinalizeVolumeDescriptors(pThis);
                    if (RT_SUCCESS(rc))
                    {
                        pThis->fFinalized = true;
                        return VINF_SUCCESS;
                    }
                }
            }
        }
    }
    return rc;
}





/*
 *
 * Image I/O.
 * Image I/O.
 * Image I/O.
 *
 */

/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtFsIsoMakerOutFile_Close(void *pvThis)
{
    PRTFSISOMAKEROUTPUTFILE pThis = (PRTFSISOMAKEROUTPUTFILE)pvThis;

    RTFsIsoMakerRelease(pThis->pIsoMaker);
    pThis->pIsoMaker = NULL;

    if (pThis->hVfsSrcFile != NIL_RTVFSFILE)
    {
        RTVfsFileRelease(pThis->hVfsSrcFile);
        pThis->hVfsSrcFile = NIL_RTVFSFILE;
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtFsIsoMakerOutFile_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTFSISOMAKEROUTPUTFILE pThis     = (PRTFSISOMAKEROUTPUTFILE)pvThis;
    PRTFSISOMAKERINT        pIsoMaker = pThis->pIsoMaker;


    pObjInfo->cbObject         = pIsoMaker->cbFinalizedImage;
    pObjInfo->cbAllocated      = pIsoMaker->cbFinalizedImage;
    pObjInfo->AccessTime       = pIsoMaker->ImageCreationTime;
    pObjInfo->ModificationTime = pIsoMaker->ImageCreationTime;
    pObjInfo->ChangeTime       = pIsoMaker->ImageCreationTime;
    pObjInfo->BirthTime        = pIsoMaker->ImageCreationTime;
    pObjInfo->Attr.fMode       = 0444 | RTFS_TYPE_FILE | RTFS_DOS_READONLY;

    switch (enmAddAttr)
    {
        case RTFSOBJATTRADD_NOTHING:
            enmAddAttr = RTFSOBJATTRADD_UNIX;
            RT_FALL_THRU();
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
            pObjInfo->Attr.u.UnixOwner.uid = NIL_RTUID;
            pObjInfo->Attr.u.UnixOwner.szName[0] = '\0';
            break;

        case RTFSOBJATTRADD_UNIX_GROUP:
            pObjInfo->Attr.u.UnixGroup.gid = NIL_RTGID;
            pObjInfo->Attr.u.UnixGroup.szName[0] = '\0';
            break;

        case RTFSOBJATTRADD_EASIZE:
            pObjInfo->Attr.u.EASize.cb = 0;
            break;

        default:
            AssertFailedReturn(VERR_INVALID_PARAMETER);
    }
    pObjInfo->Attr.enmAdditional = enmAddAttr;

    return VINF_SUCCESS;
}


/**
 * Generates the 'SL' records for a symbolic link.
 *
 * This is used both when generating directories records, spill file data and
 * when creating the symbolic link.
 *
 * @returns Number of bytes produced.  Negative IPRT status if buffer overflow.
 * @param   pszTarget   The symbolic link target to encode.
 * @param   pbBuf       The output buffer.
 * @param   cbBuf       The size of the output buffer.
 */
static ssize_t rtFsIsoMakerOutFile_RockRidgeGenSL(const char *pszTarget, uint8_t *pbBuf, size_t cbBuf)
{
    Assert(*pszTarget != '\0');

    PISO9660RRIPSL pEntry = (PISO9660RRIPSL)pbBuf;
    pEntry->Hdr.bSig1    = ISO9660RRIPSL_SIG1;
    pEntry->Hdr.bSig2    = ISO9660RRIPSL_SIG2;
    pEntry->Hdr.cbEntry  = 0; /* set later. */
    pEntry->Hdr.bVersion = ISO9660RRIPSL_VER;
    pEntry->fFlags       = 0;
    size_t  offEntry = 0;
    size_t  off      = RT_UOFFSETOF(ISO9660RRIPSL, abComponents);

    /* Does it start with a root slash? */
    if (RTPATH_IS_SLASH(*pszTarget))
    {
        pbBuf[off++] = ISO9660RRIP_SL_C_ROOT;
        pbBuf[off++] = 0;
        pszTarget++;
    }

    for (;;)
    {
        /* Find the end of the component. */
        size_t cchComponent = 0;
        char   ch;
        while ((ch = pszTarget[cchComponent]) != '\0' && !RTPATH_IS_SLASH(ch))
            cchComponent++;

        /* Check for dots and figure out how much space we need. */
        uint8_t fFlags;
        size_t  cbNeeded;
        if (cchComponent == 1 && *pszTarget == '.')
        {
            fFlags   = ISO9660RRIP_SL_C_CURRENT;
            cbNeeded = 2;
        }
        else if (cchComponent == 2 && pszTarget[0] == '.' && pszTarget[1] == '.')
        {
            fFlags   = ISO9660RRIP_SL_C_PARENT;
            cbNeeded = 2;
        }
        else
        {
            fFlags   = 0;
            cbNeeded = 2 + cchComponent;
        }

        /* Split the SL record if we're out of space. */
        if (   off - offEntry + cbNeeded < UINT8_MAX
            && off + cbNeeded <= cbBuf)
        { /* likely */ }
        else if (cbNeeded + RT_UOFFSETOF(ISO9660RRIPSL, abComponents) < UINT8_MAX)
        {
            AssertReturn(off + cbNeeded + RT_UOFFSETOF(ISO9660RRIPSL, abComponents) <= cbBuf, VERR_BUFFER_OVERFLOW);
            Assert(off - offEntry < UINT8_MAX);
            pEntry->Hdr.cbEntry = (uint8_t)(off - offEntry);
            pEntry->fFlags |= ISO9660RRIP_SL_F_CONTINUE;

            offEntry = off;
            pEntry = (PISO9660RRIPSL)&pbBuf[off];
            pEntry->Hdr.bSig1    = ISO9660RRIPSL_SIG1;
            pEntry->Hdr.bSig2    = ISO9660RRIPSL_SIG2;
            pEntry->Hdr.cbEntry  = 0; /* set later. */
            pEntry->Hdr.bVersion = ISO9660RRIPSL_VER;
            pEntry->fFlags       = 0;
        }
        else
        {
            /* Special case: component doesn't fit in a single SL entry. */
            do
            {
                if (off - offEntry + 3 < UINT8_MAX)
                {
                    size_t cchLeft   = UINT8_MAX - 1 - (off - offEntry) - 2;
                    size_t cchToCopy = RT_MIN(cchLeft, cchComponent);
                    AssertReturn(off + 2 + cchToCopy <= cbBuf, VERR_BUFFER_OVERFLOW);
                    pbBuf[off++] = cchToCopy < cchComponent ? ISO9660RRIP_SL_C_CONTINUE : 0;
                    pbBuf[off++] = (uint8_t)cchToCopy;
                    memcpy(&pbBuf[off], pszTarget, cchToCopy);
                    off          += cchToCopy;
                    pszTarget    += cchToCopy;
                    cchComponent -= cchToCopy;
                    if (!cchComponent)
                        break;
                }

                Assert(off - offEntry < UINT8_MAX);
                pEntry->Hdr.cbEntry = (uint8_t)(off - offEntry);
                pEntry->fFlags |= ISO9660RRIP_SL_F_CONTINUE;

                AssertReturn(off + 2 + cchComponent + RT_UOFFSETOF(ISO9660RRIPSL, abComponents) <= cbBuf, VERR_BUFFER_OVERFLOW);
                offEntry = off;
                pEntry = (PISO9660RRIPSL)&pbBuf[off];
                pEntry->Hdr.bSig1    = ISO9660RRIPSL_SIG1;
                pEntry->Hdr.bSig2    = ISO9660RRIPSL_SIG2;
                pEntry->Hdr.cbEntry  = 0; /* set later. */
                pEntry->Hdr.bVersion = ISO9660RRIPSL_VER;
                pEntry->fFlags       = 0;
            } while (cchComponent > 0);
            if (ch == '\0')
                break;
            pszTarget++;
            continue;
        }

        /* Produce the record. */
        pbBuf[off++] = fFlags;
        pbBuf[off++] = (uint8_t)(cbNeeded - 2);
        if (cchComponent > 0)
        {
            memcpy(&pbBuf[off], pszTarget, cbNeeded - 2);
            off += cbNeeded - 2;
        }

        if (ch == '\0')
            break;
        pszTarget += cchComponent + 1;
    }

    Assert(off - offEntry < UINT8_MAX);
    pEntry->Hdr.cbEntry = (uint8_t)(off - offEntry);
    return off;
}


/**
 * Generates rock ridge data.
 *
 * This is used both for the directory record and for the spill file ('CE').
 *
 * @param   pName           The name to generate rock ridge info for.
 * @param   pbSys           The output buffer.
 * @param   cbSys           The size of the output buffer.
 * @param   fInSpill        Indicates whether we're in a spill file (true) or
 *                          directory record (false).
 * @param   enmDirType      The kind of directory entry this is.
 */
static void rtFsIsoMakerOutFile_GenerateRockRidge(PRTFSISOMAKERNAME pName, uint8_t *pbSys, size_t cbSys,
                                                  bool fInSpill, RTFSISOMAKERDIRTYPE enmDirType)
{
    /*
     * Deal with records specific to the root directory '.' entry.
     */
    if (pName->pParent != NULL)
    { /* likely */ }
    else
    {
        if (!fInSpill)
        {
            PISO9660SUSPSP  pSP = (PISO9660SUSPSP)pbSys;
            Assert(cbSys >= sizeof(*pSP));
            pSP->Hdr.bSig1      = ISO9660SUSPSP_SIG1;
            pSP->Hdr.bSig2      = ISO9660SUSPSP_SIG2;
            pSP->Hdr.cbEntry    = ISO9660SUSPSP_LEN;
            pSP->Hdr.bVersion   = ISO9660SUSPSP_VER;
            pSP->bCheck1        = ISO9660SUSPSP_CHECK1;
            pSP->bCheck2        = ISO9660SUSPSP_CHECK2;
            pSP->cbSkip         = 0;
            pbSys += sizeof(*pSP);
            cbSys -= sizeof(*pSP);
        }
        if (pName->fRockNeedER)
        {
            PISO9660SUSPER  pER = (PISO9660SUSPER)pbSys;
            Assert(cbSys >= ISO9660_RRIP_ER_LEN);
            AssertCompile(ISO9660_RRIP_ER_LEN < UINT8_MAX);
            pER->Hdr.bSig1      = ISO9660SUSPER_SIG1;
            pER->Hdr.bSig2      = ISO9660SUSPER_SIG2;
            pER->Hdr.cbEntry    = ISO9660_RRIP_ER_LEN;
            pER->Hdr.bVersion   = ISO9660SUSPER_VER;
            pER->cchIdentifier  = sizeof(ISO9660_RRIP_ID)   - 1;
            pER->cchDescription = sizeof(ISO9660_RRIP_DESC) - 1;
            pER->cchSource      = sizeof(ISO9660_RRIP_SRC)  - 1;
            pER->bVersion       = ISO9660_RRIP_VER;
            char *pchDst = &pER->achPayload[0]; /* we do this to shut up annoying clang. */
            memcpy(pchDst, RT_STR_TUPLE(ISO9660_RRIP_ID));
            pchDst += sizeof(ISO9660_RRIP_ID) - 1;
            memcpy(pchDst, RT_STR_TUPLE(ISO9660_RRIP_DESC));
            pchDst += sizeof(ISO9660_RRIP_DESC) - 1;
            memcpy(pchDst, RT_STR_TUPLE(ISO9660_RRIP_SRC));
            pbSys += ISO9660_RRIP_ER_LEN;
            cbSys -= ISO9660_RRIP_ER_LEN;
        }
    }

    /*
     * Deal with common stuff.
     */
    if (!fInSpill ? pName->fRockNeedRRInDirRec : pName->fRockNeedRRInSpill)
    {
        PISO9660RRIPRR  pRR = (PISO9660RRIPRR)pbSys;
        Assert(cbSys >= sizeof(*pRR));
        pRR->Hdr.bSig1      = ISO9660RRIPRR_SIG1;
        pRR->Hdr.bSig2      = ISO9660RRIPRR_SIG2;
        pRR->Hdr.cbEntry    = ISO9660RRIPRR_LEN;
        pRR->Hdr.bVersion   = ISO9660RRIPRR_VER;
        pRR->fFlags         = pName->fRockEntries;
        pbSys += sizeof(*pRR);
        cbSys -= sizeof(*pRR);
    }

    /*
     * The following entries all end up in the spill or fully in
     * the directory record.
     */
    if (fInSpill || pName->cbRockSpill == 0)
    {
        if (pName->fRockEntries & ISO9660RRIP_RR_F_PX)
        {
            PISO9660RRIPPX pPX = (PISO9660RRIPPX)pbSys;
            Assert(cbSys >= sizeof(*pPX));
            pPX->Hdr.bSig1      = ISO9660RRIPPX_SIG1;
            pPX->Hdr.bSig2      = ISO9660RRIPPX_SIG2;
            pPX->Hdr.cbEntry    = ISO9660RRIPPX_LEN;
            pPX->Hdr.bVersion   = ISO9660RRIPPX_VER;
            pPX->fMode.be       = RT_H2BE_U32((uint32_t)(pName->fMode & RTFS_UNIX_MASK));
            pPX->fMode.le       = RT_H2LE_U32((uint32_t)(pName->fMode & RTFS_UNIX_MASK));
            pPX->cHardlinks.be  = RT_H2BE_U32((uint32_t)pName->cHardlinks);
            pPX->cHardlinks.le  = RT_H2LE_U32((uint32_t)pName->cHardlinks);
            pPX->uid.be         = pName->uid != NIL_RTUID ? RT_H2BE_U32((uint32_t)pName->uid) : 0;
            pPX->uid.le         = pName->uid != NIL_RTUID ? RT_H2LE_U32((uint32_t)pName->uid) : 0;
            pPX->gid.be         = pName->gid != NIL_RTGID ? RT_H2BE_U32((uint32_t)pName->gid) : 0;
            pPX->gid.le         = pName->gid != NIL_RTGID ? RT_H2LE_U32((uint32_t)pName->gid) : 0;
#if 0 /* This is confusing solaris.  Looks like it has code assuming inode numbers are block numbers and ends up mistaking files for the root dir.  Sigh. */
            pPX->INode.be       = RT_H2BE_U32((uint32_t)pName->pObj->idxObj + 1); /* Don't use zero - isoinfo doesn't like it. */
            pPX->INode.le       = RT_H2LE_U32((uint32_t)pName->pObj->idxObj + 1);
#else
            pPX->INode.be       = 0;
            pPX->INode.le       = 0;
#endif
            pbSys += sizeof(*pPX);
            cbSys -= sizeof(*pPX);
        }

        if (pName->fRockEntries & ISO9660RRIP_RR_F_TF)
        {
            PISO9660RRIPTF pTF = (PISO9660RRIPTF)pbSys;
            pTF->Hdr.bSig1      = ISO9660RRIPTF_SIG1;
            pTF->Hdr.bSig2      = ISO9660RRIPTF_SIG2;
            pTF->Hdr.cbEntry    = Iso9660RripTfCalcLength(ISO9660RRIPTF_F_BIRTH | ISO9660RRIPTF_F_MODIFY | ISO9660RRIPTF_F_ACCESS | ISO9660RRIPTF_F_CHANGE);
            Assert(cbSys >= pTF->Hdr.cbEntry);
            pTF->Hdr.bVersion   = ISO9660RRIPTF_VER;
            pTF->fFlags         = ISO9660RRIPTF_F_BIRTH | ISO9660RRIPTF_F_MODIFY | ISO9660RRIPTF_F_ACCESS | ISO9660RRIPTF_F_CHANGE;
            PISO9660RECTIMESTAMP paTimestamps = (PISO9660RECTIMESTAMP)&pTF->abPayload[0];
            rtFsIsoMakerTimespecToIso9660RecTimestamp(&pName->pObj->BirthTime,        &paTimestamps[0]);
            rtFsIsoMakerTimespecToIso9660RecTimestamp(&pName->pObj->ModificationTime, &paTimestamps[1]);
            rtFsIsoMakerTimespecToIso9660RecTimestamp(&pName->pObj->AccessedTime,     &paTimestamps[2]);
            rtFsIsoMakerTimespecToIso9660RecTimestamp(&pName->pObj->ChangeTime,       &paTimestamps[3]);
            cbSys -= pTF->Hdr.cbEntry;
            pbSys += pTF->Hdr.cbEntry;
        }

        if (pName->fRockEntries & ISO9660RRIP_RR_F_PN)
        {
            PISO9660RRIPPN pPN = (PISO9660RRIPPN)pbSys;
            Assert(cbSys >= sizeof(*pPN));
            pPN->Hdr.bSig1      = ISO9660RRIPPN_SIG1;
            pPN->Hdr.bSig2      = ISO9660RRIPPN_SIG2;
            pPN->Hdr.cbEntry    = ISO9660RRIPPN_LEN;
            pPN->Hdr.bVersion   = ISO9660RRIPPN_VER;
            pPN->Major.be       = RT_H2BE_U32((uint32_t)RTDEV_MAJOR(pName->Device));
            pPN->Major.le       = RT_H2LE_U32((uint32_t)RTDEV_MAJOR(pName->Device));
            pPN->Minor.be       = RT_H2BE_U32((uint32_t)RTDEV_MINOR(pName->Device));
            pPN->Minor.le       = RT_H2LE_U32((uint32_t)RTDEV_MINOR(pName->Device));
            cbSys -= sizeof(*pPN);
            pbSys += sizeof(*pPN);
        }

        if (pName->fRockEntries & ISO9660RRIP_RR_F_NM)
        {
            size_t      cchSrc = pName->cchRockRidgeNm;
            const char *pszSrc = pName->pszRockRidgeNm;
            for (;;)
            {
                size_t         cchThis = RT_MIN(cchSrc, ISO9660RRIPNM_MAX_NAME_LEN);
                PISO9660RRIPNM pNM     = (PISO9660RRIPNM)pbSys;
                Assert(cbSys >= RT_UOFFSETOF_DYN(ISO9660RRIPNM, achName[cchThis]));
                pNM->Hdr.bSig1      = ISO9660RRIPNM_SIG1;
                pNM->Hdr.bSig2      = ISO9660RRIPNM_SIG2;
                pNM->Hdr.cbEntry    = (uint8_t)(RT_UOFFSETOF(ISO9660RRIPNM, achName) + cchThis);
                pNM->Hdr.bVersion   = ISO9660RRIPNM_VER;
                pNM->fFlags         = cchThis == cchSrc ? 0 : ISO9660RRIP_NM_F_CONTINUE;
                /** @todo r=bird: This only works when not using the spill file. The spill
                 *        file entry will be shared between the original and all the '.' and
                 *        '..' entries.  FreeBSD gets confused by this w/o the
                 *        ISO9660RRIP_NM_F_CURRENT and ISO9660RRIP_NM_F_PARENT flags. */
                if (enmDirType == RTFSISOMAKERDIRTYPE_CURRENT)
                    pNM->fFlags    |= ISO9660RRIP_NM_F_CURRENT;
                else if (enmDirType == RTFSISOMAKERDIRTYPE_PARENT)
                    pNM->fFlags    |= ISO9660RRIP_NM_F_PARENT;
                memcpy(&pNM->achName[0], pszSrc, cchThis);
                pbSys  += RT_UOFFSETOF(ISO9660RRIPNM, achName) + cchThis;
                cbSys  -= RT_UOFFSETOF(ISO9660RRIPNM, achName) + cchThis;
                cchSrc -= cchThis;
                if (!cchSrc)
                    break;
            }
        }

        if (pName->fRockEntries & ISO9660RRIP_RR_F_SL)
        {
            AssertReturnVoid(pName->pObj->enmType == RTFSISOMAKEROBJTYPE_SYMLINK);
            PCRTFSISOMAKERSYMLINK pSymlink = (PCRTFSISOMAKERSYMLINK)pName->pObj;

            ssize_t cbSlRockRidge = rtFsIsoMakerOutFile_RockRidgeGenSL(pSymlink->szTarget, pbSys, cbSys);
            AssertReturnVoid(cbSlRockRidge > 0);
            Assert(cbSys >= (size_t)cbSlRockRidge);
            pbSys += (size_t)cbSlRockRidge;
            cbSys -= (size_t)cbSlRockRidge;
        }
    }

    /* finally, zero padding. */
    if (cbSys & 1)
    {
        *pbSys++ = '\0';
        cbSys--;
    }

    Assert(!fInSpill ? cbSys == 0 : cbSys < _2G);
}




/**
 * Reads one or more sectors from a rock ridge spill file.
 *
 * @returns IPRT status code.
 * @param   pThis       The ISO maker output file instance.  We use the
 *                      directory pointer hints and child index hints
 * @param   pIsoMaker   The ISO maker.
 * @param   pFile       The rock ridge spill file.
 * @param   offInFile   The offset into the spill file.  This is sector aligned.
 * @param   pbBuf       The output buffer.
 * @param   cbToRead    The number of bytes to tread.  This is sector aligned.
 */
static int rtFsIsoMakerOutFile_RockRidgeSpillReadSectors(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker,
                                                         PRTFSISOMAKERFILE pFile, uint32_t offInFile, uint8_t *pbBuf,
                                                         size_t cbToRead)
{
    /*
     * We're only working multiple of ISO 9660 sectors.
     *
     * The spill of one directory record will always fit entirely within a
     * sector, we make sure about that during finalization.  There may be
     * zero padding between spill data sequences, especially on the sector
     * boundrary.
     */
    Assert((offInFile & ISO9660_SECTOR_OFFSET_MASK) == 0);
    Assert((cbToRead  & ISO9660_SECTOR_OFFSET_MASK) == 0);
    Assert(cbToRead  >= ISO9660_SECTOR_SIZE);

    /*
     * We generate a sector at a time.
     *
     * So, we start by locating the first directory/child in the block offInFile
     * is pointing to.
     */
    PRTFSISOMAKERNAMESPACE      pNamespace;
    PRTFSISOMAKERNAMEDIR       *ppDirHint;
    uint32_t                   *pidxChildHint;
    if (pFile->u.pRockSpillNamespace->fNamespace & RTFSISOMAKER_NAMESPACE_ISO_9660)
    {
        pNamespace     = &pIsoMaker->PrimaryIso;
        ppDirHint      = &pThis->pDirHintPrimaryIso;
        pidxChildHint  = &pThis->iChildPrimaryIso;
    }
    else
    {
        pNamespace     = &pIsoMaker->Joliet;
        ppDirHint      = &pThis->pDirHintJoliet;
        pidxChildHint  = &pThis->iChildJoliet;
    }

    /* Special case: '.' record in root dir */
    uint32_t             idxChild  = *pidxChildHint;
    PRTFSISOMAKERNAMEDIR pDir      = *ppDirHint;
    if (   offInFile == 0
        && (pDir = RTListGetFirst(&pNamespace->FinalizedDirs, RTFSISOMAKERNAMEDIR, FinalizedEntry)) != NULL
        && pDir->pName->cbRockSpill > 0)
    {
        AssertReturn(pDir, VERR_ISOMK_IPE_RR_READ);
        AssertReturn(pDir->pName->offRockSpill == 0, VERR_ISOMK_IPE_RR_READ);
        idxChild = 0;
    }
    else
    {
        /* Establish where to start searching from. */
        if (  !pDir
            || idxChild >= pDir->cChildren
            || pDir->papChildren[idxChild]->cbRockSpill == 0)
        {
            idxChild = 0;
            pDir = RTListGetFirst(&pNamespace->FinalizedDirs, RTFSISOMAKERNAMEDIR, FinalizedEntry);
            AssertReturn(pDir, VERR_ISOMK_IPE_RR_READ);
        }

        if (pDir->papChildren[idxChild]->offRockSpill == offInFile)
        { /* hit, no need to search */ }
        else if (pDir->papChildren[idxChild]->offRockSpill < offInFile)
        {
            /* search forwards */
            for (;;)
            {
                idxChild++;
                while (   idxChild < pDir->cChildren
                       && (   pDir->papChildren[idxChild]->offRockSpill < offInFile
                           || pDir->papChildren[idxChild]->cbRockSpill  == 0) )
                    idxChild++;
                if (idxChild < pDir->cChildren)
                    break;
                pDir = RTListGetNext(&pNamespace->FinalizedDirs, pDir, RTFSISOMAKERNAMEDIR, FinalizedEntry);
                AssertReturn(pDir, VERR_ISOMK_IPE_RR_READ);
            }
            Assert(pDir->papChildren[idxChild]->offRockSpill == offInFile);
        }
        else
        {
            /* search backwards (no root dir concerns here) */
            for (;;)
            {
                while (   idxChild > 0
                       && (   pDir->papChildren[idxChild - 1]->offRockSpill >= offInFile
                           || pDir->papChildren[idxChild - 1]->cbRockSpill  == 0) )
                    idxChild--;
                if (pDir->papChildren[idxChild]->offRockSpill == offInFile)
                    break;
                pDir = RTListGetPrev(&pNamespace->FinalizedDirs, pDir, RTFSISOMAKERNAMEDIR, FinalizedEntry);
                AssertReturn(pDir, VERR_ISOMK_IPE_RR_READ);
            }
            Assert(pDir->papChildren[idxChild]->offRockSpill == offInFile);
        }
    }

    /*
     * Produce data.
     */
    while (cbToRead > 0)
    {
        PRTFSISOMAKERNAME pChild;
        if (   offInFile > 0
            || pDir->pName->cbRockSpill == 0
            || pDir->pName->pParent     != NULL)
        {
            pChild = pDir->papChildren[idxChild];
            AssertReturn(pChild->offRockSpill == offInFile, VERR_ISOMK_IPE_RR_READ);
            AssertReturn(pChild->cbRockSpill > 0, VERR_ISOMK_IPE_RR_READ);
            idxChild++;
        }
        else
        {   /* root dir special case. */
            pChild = pDir->pName;
            Assert(idxChild == 0);
            Assert(pChild->pParent == NULL);
        }

        AssertReturn(cbToRead >= pChild->cbRockSpill, VERR_ISOMK_IPE_RR_READ);
        /** @todo r=bird: using RTFSISOMAKERDIRTYPE_OTHER is correct as we don't seem to
         *        have separate name entries for '.' and '..'.  However it means that if
         *        any directory ends up in the spill file we'll end up with the wrong
         *        data for the '.' and '..' entries. [RR NM ./..] */
        rtFsIsoMakerOutFile_GenerateRockRidge(pDir->pName, pbBuf, cbToRead, true /*fInSpill*/, RTFSISOMAKERDIRTYPE_OTHER);
        cbToRead  -= pChild->cbRockSpill;
        pbBuf     += pChild->cbRockSpill;
        offInFile += pChild->cbRockSpill;

        /* Advance to the next name, if any. */
        uint32_t offNext = UINT32_MAX;
        do
        {
            while (idxChild < pDir->cChildren)
            {
                pChild = pDir->papChildren[idxChild];
                if (pChild->cbRockSpill == 0)
                    Assert(pChild->offRockSpill == UINT32_MAX);
                else
                {
                    offNext = pChild->offRockSpill;
                    AssertReturn(offNext >= offInFile, VERR_ISOMK_IPE_RR_READ);
                    AssertReturn(offNext < pFile->cbData, VERR_ISOMK_IPE_RR_READ);
                    break;
                }
                idxChild++;
            }
            if (offNext != UINT32_MAX)
                break;
            pDir     = RTListGetNext(&pNamespace->FinalizedDirs, pDir, RTFSISOMAKERNAMEDIR, FinalizedEntry);
            idxChild = 0;
        } while (pDir != NULL);

        if (offNext != UINT32_MAX)
        {
            uint32_t cbToZero = offNext - offInFile;
            if (cbToRead > cbToZero)
                RT_BZERO(pbBuf, cbToZero);
            else
            {
                RT_BZERO(pbBuf, cbToRead);
                *ppDirHint     = pDir;
                *pidxChildHint = idxChild;
                break;
            }
            cbToRead  -= cbToZero;
            pbBuf     += cbToZero;
            offInFile += cbToZero;
        }
        else
        {
            RT_BZERO(pbBuf, cbToRead);
            *ppDirHint     = NULL;
            *pidxChildHint = UINT32_MAX;
            break;
        }
    }

    return VINF_SUCCESS;
}


/**
 * Deals with reads that aren't an exact multiple of sectors.
 *
 * @returns IPRT status code.
 * @param   pThis       The ISO maker output file instance.  We use the
 *                      directory pointer hints and child index hints
 * @param   pIsoMaker   The ISO maker.
 * @param   pFile       The rock ridge spill file.
 * @param   offInFile   The offset into the spill file.
 * @param   pbBuf       The output buffer.
 * @param   cbToRead    The number of bytes to tread.
 */
static int rtFsIsoMakerOutFile_RockRidgeSpillReadUnaligned(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker,
                                                           PRTFSISOMAKERFILE pFile, uint32_t offInFile, uint8_t *pbBuf,
                                                           uint32_t cbToRead)
{
    for (;;)
    {
        /*
         * Deal with unnaligned file offsets and sub-sector sized reads.
         */
        if (   (offInFile & ISO9660_SECTOR_OFFSET_MASK)
            || cbToRead < ISO9660_SECTOR_SIZE)
        {
            uint8_t abSectorBuf[ISO9660_SECTOR_SIZE];
            int rc = rtFsIsoMakerOutFile_RockRidgeSpillReadSectors(pThis, pIsoMaker, pFile,
                                                                   offInFile & ~(uint32_t)ISO9660_SECTOR_OFFSET_MASK,
                                                                   abSectorBuf, sizeof(abSectorBuf));
            if (RT_FAILURE(rc))
                return rc;
            uint32_t offSrcBuf = (size_t)offInFile & (size_t)ISO9660_SECTOR_OFFSET_MASK;
            uint32_t cbToCopy  = RT_MIN(ISO9660_SECTOR_SIZE - offSrcBuf, cbToRead);
            memcpy(pbBuf, &abSectorBuf[offSrcBuf], cbToCopy);
            if (cbToCopy >= cbToRead)
                return VINF_SUCCESS;
            cbToRead  -= cbToCopy;
            offInFile += cbToCopy;
            pbBuf     += cbToCopy;
        }

        /*
         * The offset is aligned now, so try read some sectors directly into the buffer.
         */
        AssertContinue((offInFile & ISO9660_SECTOR_OFFSET_MASK) == 0);
        if (cbToRead >= ISO9660_SECTOR_SIZE)
        {
            uint32_t cbFullSectors = cbToRead & ~(uint32_t)ISO9660_SECTOR_OFFSET_MASK;
            int rc = rtFsIsoMakerOutFile_RockRidgeSpillReadSectors(pThis, pIsoMaker, pFile, offInFile, pbBuf, cbFullSectors);
            if (RT_FAILURE(rc))
                return rc;
            if (cbFullSectors >= cbToRead)
                return VINF_SUCCESS;
            cbToRead  -= cbFullSectors;
            offInFile += cbFullSectors;
            pbBuf     += cbFullSectors;
        }
    }
}



/**
 * Produces the content of a TRANS.TBL file as a memory file.
 *
 * @returns IPRT status code.
 * @param   pThis       The ISO maker output file instance.  The file is
 *                      returned as pThis->hVfsSrcFile.
 * @param   pFile       The TRANS.TBL file.
 */
static int rtFsIsoMakerOutFile_ProduceTransTbl(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERFILE pFile)
{
    /*
     * Create memory file instance.
     */
    RTVFSFILE hVfsFile;
    int rc = RTVfsMemFileCreate(NIL_RTVFSIOSTREAM, pFile->cbData, &hVfsFile);
    AssertRCReturn(rc, rc);

    /*
     * Produce the file content.
     */
    PRTFSISOMAKERNAME *ppChild = pFile->u.pTransTblDir->pDir->papChildren;
    uint32_t           cLeft   = pFile->u.pTransTblDir->pDir->cChildren;
    while (cLeft-- > 0)
    {
        PRTFSISOMAKERNAME pChild = *ppChild++;
        if (pChild->cchTransNm)
        {
            /** @todo TRANS.TBL codeset, currently using UTF-8 which is probably not it.
             *        However, nobody uses this stuff any more, so who cares. */
            char   szEntry[RTFSISOMAKER_MAX_NAME_BUF * 2 + 128];
            size_t cchEntry = RTStrPrintf(szEntry, sizeof(szEntry), "%c %-*s\t%s\n", pChild->pDir ? 'D' : 'F',
                                          RTFSISOMAKER_TRANS_TBL_LEFT_PAD, pChild->szName, pChild->pszTransNm);
            rc = RTVfsFileWrite(hVfsFile, szEntry, cchEntry, NULL);
            if (RT_FAILURE(rc))
            {
                RTVfsFileRelease(hVfsFile);
                return rc;
            }
        }
    }

    /*
     * Check that the size matches our estimate.
     */
    uint64_t cbResult = 0;
    rc = RTVfsFileQuerySize(hVfsFile, &cbResult);
    if (RT_SUCCESS(rc) && cbResult == pFile->cbData)
    {
        pThis->hVfsSrcFile = hVfsFile;
        return VINF_SUCCESS;
    }

    AssertMsgFailed(("rc=%Rrc, cbResult=%#RX64 cbData=%#RX64\n", rc, cbResult, pFile->cbData));
    RTVfsFileRelease(hVfsFile);
    return VERR_ISOMK_IPE_PRODUCE_TRANS_TBL;
}



/**
 * Reads file data.
 *
 * @returns IPRT status code
 * @param   pThis           The instance data for the VFS file.  We use this to
 *                          keep hints about where we are and we which source
 *                          file we've opened/created.
 * @param   pIsoMaker       The ISO maker instance.
 * @param   offInRange      The byte offset into the file of the requested data.
 * @param   pbBuf           The output buffer.
 * @param   cbToRead        How much to read.
 */
static DECLCALLBACK(int)
rtFsIsoMakerOutFile_ReadFileData(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker, PCRTFSISOMAKERRANGENODE pRangeNode,
                                 uint64_t offInRange, uint8_t *pbBuf, size_t cbToRead)
{
    PRTFSISOMAKERFILE const pFile = RT_FROM_MEMBER(pRangeNode, RTFSISOMAKERFILE, RangeNode);
    AssertReturn(offInRange < pFile->cbData, VERR_INTERNAL_ERROR_5);
    AssertReturn(cbToRead <= pFile->cbData - offInRange, VERR_INTERNAL_ERROR_5);

    /*
     * Produce data bits according to the source type.
     */
    int rc;
    switch (pFile->enmSrcType)
    {
        case RTFSISOMAKERSRCTYPE_PATH:
        case RTFSISOMAKERSRCTYPE_TRANS_TBL:
            if (pThis->pSrcFile != pFile)
            {
                if (pThis->hVfsSrcFile != NIL_RTVFSFILE)
                {
                    RTVfsFileRelease(pThis->hVfsSrcFile);
                    pThis->hVfsSrcFile = NIL_RTVFSFILE;
                    pThis->pSrcFile    = NULL;
                }
                if (pFile->enmSrcType == RTFSISOMAKERSRCTYPE_PATH)
                {
                    rc = RTVfsChainOpenFile(pFile->u.pszSrcPath, RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                                            &pThis->hVfsSrcFile, NULL, NULL);
                    AssertMsgRCReturn(rc, ("%s -> %Rrc\n", pFile->u.pszSrcPath, rc), rc);
                }
                else
                {
                    rc = rtFsIsoMakerOutFile_ProduceTransTbl(pThis, pFile);
                    AssertRCReturn(rc, rc);
                }
                pThis->pSrcFile = pFile;
            }
            rc = RTVfsFileReadAt(pThis->hVfsSrcFile, offInRange, pbBuf, cbToRead, NULL);
            AssertRCReturn(rc, rc);
            break;

        case RTFSISOMAKERSRCTYPE_VFS_FILE:
            rc = RTVfsFileReadAt(pFile->u.hVfsFile, offInRange, pbBuf, cbToRead, NULL);
            AssertRCReturn(rc, rc);
            break;

        case RTFSISOMAKERSRCTYPE_COMMON:
            rc = RTVfsFileReadAt(pIsoMaker->paCommonSources[pFile->u.Common.idxSrc],
                                 pFile->u.Common.offData + offInRange, pbBuf, cbToRead, NULL);
            AssertRCReturn(rc, rc);
            break;

        case RTFSISOMAKERSRCTYPE_RR_SPILL:
            Assert(pFile->cbData < UINT32_MAX);
            if (   !(offInRange & ISO9660_SECTOR_OFFSET_MASK)
                && !(cbToRead & ISO9660_SECTOR_OFFSET_MASK)
                && cbToRead > 0)
                rc = rtFsIsoMakerOutFile_RockRidgeSpillReadSectors(pThis, pIsoMaker, pFile, (uint32_t)offInRange,
                                                                   pbBuf, (uint32_t)cbToRead);
            else
                rc = rtFsIsoMakerOutFile_RockRidgeSpillReadUnaligned(pThis, pIsoMaker, pFile, (uint32_t)offInRange,
                                                                     pbBuf, (uint32_t)cbToRead);
            AssertRCReturn(rc, rc);
            break;

        default:
            AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }

    /*
     * Do boot info table patching.
     */
    if (   pFile->pBootInfoTable
        && offInRange            < 64
        && offInRange + cbToRead > 8)
    {
        size_t offInBuf = offInRange <  8 ? 8 - (size_t)offInRange : 0;
        size_t offInTab = offInRange <= 8 ? 0 : (size_t)offInRange - 8;
        size_t cbToCopy = RT_MIN(sizeof(*pFile->pBootInfoTable) - offInTab, cbToRead - offInBuf);
        memcpy(&pbBuf[offInBuf], (uint8_t *)pFile->pBootInfoTable + offInTab, cbToCopy);
    }

    return VINF_SUCCESS;
}


/**
 * Generates ISO-9660 path table record into the specified buffer.
 *
 * @returns Number of bytes copied into the buffer.
 * @param   pName       The directory namespace node.
 * @param   fUnicode    Set if the name should be translated to big endian
 *                      UTF-16 / UCS-2, i.e. we're in the joliet namespace.
 * @param   pbBuf       The buffer.  This is large enough to hold the path
 *                      record (use RTFSISOMAKER_CALC_PATHREC_SIZE) and a zero
 *                      RTUTF16 terminator if @a fUnicode is true.
 */
static uint32_t rtFsIsoMakerOutFile_GeneratePathRec(PRTFSISOMAKERNAME pName, bool fUnicode, bool fLittleEndian, uint8_t *pbBuf)
{
    PISO9660PATHREC pPathRec = (PISO9660PATHREC)pbBuf;
    pPathRec->cbDirId   = pName->cbNameInDirRec;
    pPathRec->cbExtAttr = 0;
    if (fLittleEndian)
    {
        pPathRec->offExtent   = RT_H2LE_U32(pName->pDir->RangeNode.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
        pPathRec->idParentRec = RT_H2LE_U16(pName->pParent ? pName->pParent->pDir->idPathTable : 1);
    }
    else
    {
        pPathRec->offExtent   = RT_H2BE_U32(pName->pDir->RangeNode.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
        pPathRec->idParentRec = RT_H2BE_U16(pName->pParent ? pName->pParent->pDir->idPathTable : 1);
    }
    if (!fUnicode)
    {
        memcpy(&pPathRec->achDirId[0], pName->szName, pName->cbNameInDirRec);
        if (pName->cbNameInDirRec & 1)
            pPathRec->achDirId[pName->cbNameInDirRec] = '\0';
    }
    else
    {
        /* Caller made sure there is space for a zero terminator character. */
        PRTUTF16 pwszTmp   = (PRTUTF16)&pPathRec->achDirId[0];
        size_t   cwcResult = 0;
        int rc = RTStrToUtf16BigEx(pName->szName, RTSTR_MAX, &pwszTmp, pName->cbNameInDirRec / sizeof(RTUTF16) + 1, &cwcResult);
        AssertRC(rc);
        Assert(   cwcResult * sizeof(RTUTF16) == pName->cbNameInDirRec
               || (!pName->pParent && cwcResult == 0 && pName->cbNameInDirRec == 1) );

    }
    return RTFSISOMAKER_CALC_PATHREC_SIZE(pName->cbNameInDirRec);
}


/**
 * Deals with situations where the destination buffer doesn't cover the whole
 * path table record.
 *
 * @returns Number of bytes copied into the buffer.
 * @param   pName       The directory namespace node.
 * @param   fUnicode    Set if the name should be translated to big endian
 *                      UTF-16 / UCS-2, i.e. we're in the joliet namespace.
 * @param   offInRec    The offset into the path table record.
 * @param   pbBuf       The buffer.
 * @param   cbBuf       The buffer size.
 */
static uint32_t rtFsIsoMakerOutFile_GeneratePathRecPartial(PRTFSISOMAKERNAME pName, bool fUnicode, bool fLittleEndian,
                                                           uint32_t offInRec, uint8_t *pbBuf, size_t cbBuf)
{
    uint8_t abTmpRec[256];
    size_t cbToCopy = rtFsIsoMakerOutFile_GeneratePathRec(pName, fUnicode, fLittleEndian, abTmpRec);
    cbToCopy = RT_MIN(cbBuf, cbToCopy - offInRec);
    memcpy(pbBuf, &abTmpRec[offInRec], cbToCopy);
    return (uint32_t)cbToCopy;
}


/**
 * Generate path table records.
 *
 * This will generate record up to the end of the table.  However, it will not
 * supply the zero padding in the last sector, the caller is expected to take
 * care of that.
 *
 * @returns Number of bytes written to the buffer.
 * @param   ppDirHint       Pointer to the directory hint for the namespace.
 * @param   pNamespace      The namespace.
 * @param   fUnicode        Set if the name should be translated to big endian
 *                          UTF-16 / UCS-2, i.e. we're in the joliet namespace.
 * @param   fLittleEndian   Set if we're generating little endian records, clear
 *                          if big endian records.
 * @param   offInTable      Offset into the path table.
 * @param   pbBuf           The output buffer.
 * @param   cbBuf           The buffer size.
 */
static size_t rtFsIsoMakerOutFile_ReadPathTable(PRTFSISOMAKERNAMEDIR *ppDirHint, PRTFSISOMAKERNAMESPACE pNamespace,
                                                bool fUnicode, bool fLittleEndian, uint32_t offInTable,
                                                uint8_t *pbBuf, size_t cbBuf)
{
    /*
     * Figure out which directory to start with.  We keep a hint in the instance.
     */
    PRTFSISOMAKERNAMEDIR pDir = *ppDirHint;
    if (!pDir)
    {
        pDir = RTListGetFirst(&pNamespace->FinalizedDirs, RTFSISOMAKERNAMEDIR, FinalizedEntry);
        AssertReturnStmt(pDir, *pbBuf = 0xff, 1);
    }
    if (offInTable - pDir->offPathTable < RTFSISOMAKER_CALC_PATHREC_SIZE(pDir->pName->cbNameInDirRec))
    { /* hit */ }
    /* Seek forwards: */
    else if (offInTable > pDir->offPathTable)
        do
        {
            pDir = RTListGetNext(&pNamespace->FinalizedDirs, pDir, RTFSISOMAKERNAMEDIR, FinalizedEntry);
            AssertReturnStmt(pDir, *pbBuf = 0xff, 1);
        } while (offInTable - pDir->offPathTable >= RTFSISOMAKER_CALC_PATHREC_SIZE(pDir->pName->cbNameInDirRec));
    /* Back to the start: */
    else if (offInTable == 0)
    {
        pDir = RTListGetFirst(&pNamespace->FinalizedDirs, RTFSISOMAKERNAMEDIR, FinalizedEntry);
        AssertReturnStmt(pDir, *pbBuf = 0xff, 1);
    }
    /* Seek backwards: */
    else
        do
        {
            pDir = RTListGetPrev(&pNamespace->FinalizedDirs, pDir, RTFSISOMAKERNAMEDIR, FinalizedEntry);
            AssertReturnStmt(pDir, *pbBuf = 0xff, 1);
        } while (offInTable - pDir->offPathTable >= RTFSISOMAKER_CALC_PATHREC_SIZE(pDir->pName->cbNameInDirRec));

    /*
     * Generate content.
     */
    size_t cbDone = 0;
    while (   cbBuf > 0
           && pDir)
    {
        PRTFSISOMAKERNAME pName = pDir->pName;
        uint8_t           cbRec = RTFSISOMAKER_CALC_PATHREC_SIZE(pName->cbNameInDirRec);
        uint32_t          cbCopied;
        if (   offInTable == pDir->offPathTable
            && cbBuf      >= cbRec + fUnicode * 2U)
            cbCopied = rtFsIsoMakerOutFile_GeneratePathRec(pName, fUnicode, fLittleEndian, pbBuf);
        else
            cbCopied = rtFsIsoMakerOutFile_GeneratePathRecPartial(pName, fUnicode, fLittleEndian,
                                                                  offInTable - pDir->offPathTable, pbBuf, cbBuf);
        cbDone     += cbCopied;
        offInTable += cbCopied;
        pbBuf      += cbCopied;
        cbBuf      -= cbCopied;
        pDir = RTListGetNext(&pNamespace->FinalizedDirs, pDir, RTFSISOMAKERNAMEDIR, FinalizedEntry);
    }

    /*
     * Update the hint.
     */
    *ppDirHint = pDir;

    return cbDone;
}


/**
 * Range read callbac - ISO-9660 path table, big endian variant.
 */
static DECLCALLBACK(int)
rtFsIsoMakerOutFile_ReadIso9660PathTableBig(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker,
                                            PCRTFSISOMAKERRANGENODE pRangeNode,
                                            uint64_t offInRange, uint8_t *pbBuf, size_t cbToRead)
{
    PRTFSISOMAKERNAMESPACE const pNamespace = RT_FROM_MEMBER(pRangeNode, RTFSISOMAKERNAMESPACE, RangeNodePathTableM);
    bool const                   fJoliet    = pNamespace->fNamespace == RTFSISOMAKER_NAMESPACE_JOLIET;
    size_t cbDone = rtFsIsoMakerOutFile_ReadPathTable(fJoliet ? &pThis->pDirHintJoliet : &pThis->pDirHintPrimaryIso,
                                                      pNamespace, fJoliet, false /*fLittleEndian*/,
                                                      (uint32_t)offInRange, pbBuf, cbToRead);
    AssertReturn(cbDone == cbToRead, VERR_INTERNAL_ERROR_5);
    RT_NOREF(pIsoMaker);
    return VINF_SUCCESS;
}


/**
 * Range read callbac - ISO-9660 path table, little endian variant.
 */
static DECLCALLBACK(int)
rtFsIsoMakerOutFile_ReadIso9660PathTableLittle(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker,
                                               PCRTFSISOMAKERRANGENODE pRangeNode,
                                               uint64_t offInRange, uint8_t *pbBuf, size_t cbToRead)
{
    PRTFSISOMAKERNAMESPACE const pNamespace = RT_FROM_MEMBER(pRangeNode, RTFSISOMAKERNAMESPACE, RangeNodePathTableL);
    bool const                   fJoliet    = pNamespace->fNamespace == RTFSISOMAKER_NAMESPACE_JOLIET;
    size_t cbDone = rtFsIsoMakerOutFile_ReadPathTable(fJoliet ? &pThis->pDirHintJoliet : &pThis->pDirHintPrimaryIso,
                                                      pNamespace, fJoliet, true /*fLittleEndian*/,
                                                      (uint32_t)offInRange, pbBuf, cbToRead);
    AssertReturn(cbDone == cbToRead, VERR_INTERNAL_ERROR_5);
    RT_NOREF(pIsoMaker);
    return VINF_SUCCESS;
}


/**
 * Generates ISO-9660 directory record into the specified buffer.
 *
 * The caller must deal with multi-extent copying and end of sector zero
 * padding.
 *
 * @returns Number of bytes copied into the buffer (pName->cbDirRec).
 * @param   pName           The namespace node.
 * @param   fUnicode        Set if the name should be translated to big endian
 *                          UTF-16BE / UCS-2BE, i.e. we're in the joliet namespace.
 * @param   pbBuf           The buffer.  This is at least pName->cbDirRec bytes
 *                          big (i.e. at most 256 bytes).
 * @param   pNamespace      The namespace.
 * @param   enmDirType      The kind of directory entry this is.
 */
static uint32_t rtFsIsoMakerOutFile_GenerateDirRec(PRTFSISOMAKERNAME pName, bool fUnicode, uint8_t *pbBuf,
                                                   PRTFSISOMAKERNAMESPACE pNamespace, RTFSISOMAKERDIRTYPE enmDirType)
{
    /*
     * Emit a standard ISO-9660 directory record.
     */
    PISO9660DIRREC          pDirRec = (PISO9660DIRREC)pbBuf;
    PCRTFSISOMAKEROBJ       pObj    = pName->pObj;
    PCRTFSISOMAKERNAMEDIR   pDir    = pName->pDir;
    if (pDir)
    {
        pDirRec->offExtent.be       = RT_H2BE_U32(pDir->RangeNode.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
        pDirRec->offExtent.le       = RT_H2LE_U32(pDir->RangeNode.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
        pDirRec->cbData.be          = RT_H2BE_U32(pDir->cbDir);
        pDirRec->cbData.le          = RT_H2LE_U32(pDir->cbDir);
        pDirRec->fFileFlags         = ISO9660_FILE_FLAGS_DIRECTORY;
    }
    else if (pObj->enmType == RTFSISOMAKEROBJTYPE_FILE)
    {
        PRTFSISOMAKERFILE pFile = (PRTFSISOMAKERFILE)pObj;
        pDirRec->offExtent.be       = RT_H2BE_U32(pFile->RangeNode.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
        pDirRec->offExtent.le       = RT_H2LE_U32(pFile->RangeNode.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
        pDirRec->cbData.be          = RT_H2BE_U32(pFile->cbData);
        pDirRec->cbData.le          = RT_H2LE_U32(pFile->cbData);
        pDirRec->fFileFlags         = 0;
    }
    else
    {
        pDirRec->offExtent.be       = 0;
        pDirRec->offExtent.le       = 0;
        pDirRec->cbData.be          = 0;
        pDirRec->cbData.le          = 0;
        pDirRec->fFileFlags         = 0;
    }
    rtFsIsoMakerTimespecToIso9660RecTimestamp(&pObj->BirthTime, &pDirRec->RecTime);

    pDirRec->cbDirRec               = pName->cbDirRec;
    pDirRec->cExtAttrBlocks         = 0;
    pDirRec->bFileUnitSize          = 0;
    pDirRec->bInterleaveGapSize     = 0;
    pDirRec->VolumeSeqNo.be         = RT_H2BE_U16_C(1);
    pDirRec->VolumeSeqNo.le         = RT_H2LE_U16_C(1);
    pDirRec->bFileIdLength          = pName->cbNameInDirRec;

    if (!fUnicode)
    {
        memcpy(&pDirRec->achFileId[0], pName->szName, pName->cbNameInDirRec);
        if (!(pName->cbNameInDirRec & 1))
            pDirRec->achFileId[pName->cbNameInDirRec] = '\0';
    }
    else
    {
        /* Convert to big endian UTF-16.  We're using a separate buffer here
           because of zero terminator (none in pDirRec) and misalignment. */
        RTUTF16  wszTmp[128];
        PRTUTF16 pwszTmp = &wszTmp[0];
        size_t   cwcResult = 0;
        wszTmp[0] = '\0';
        int rc = RTStrToUtf16BigEx(pName->szName, RTSTR_MAX, &pwszTmp, RT_ELEMENTS(wszTmp), &cwcResult);
        AssertRC(rc);
        Assert(   cwcResult * sizeof(RTUTF16) == pName->cbNameInDirRec
               || (!pName->pParent && cwcResult == 0 && pName->cbNameInDirRec == 1) );
        memcpy(&pDirRec->achFileId[0], pwszTmp, pName->cbNameInDirRec);
        pDirRec->achFileId[pName->cbNameInDirRec] = '\0';
    }

    /*
     * Rock ridge fields if enabled.
     */
    if (pName->cbRockInDirRec > 0)
    {
        uint8_t *pbSys = (uint8_t *)&pDirRec->achFileId[pName->cbNameInDirRec + !(pName->cbNameInDirRec & 1)];
        size_t   cbSys = &pbBuf[pName->cbDirRec] - pbSys;
        Assert(cbSys >= pName->cbRockInDirRec);
        if (cbSys > pName->cbRockInDirRec)
            RT_BZERO(&pbSys[pName->cbRockInDirRec], cbSys - pName->cbRockInDirRec);
        if (pName->cbRockSpill == 0)
            rtFsIsoMakerOutFile_GenerateRockRidge(pName, pbSys, cbSys, false /*fInSpill*/, enmDirType);
        else
        {
            /* Maybe emit SP and RR entry, before emitting the CE entry. */
            if (pName->pParent == NULL)
            {
                PISO9660SUSPSP pSP = (PISO9660SUSPSP)pbSys;
                pSP->Hdr.bSig1     = ISO9660SUSPSP_SIG1;
                pSP->Hdr.bSig2     = ISO9660SUSPSP_SIG2;
                pSP->Hdr.cbEntry   = ISO9660SUSPSP_LEN;
                pSP->Hdr.bVersion  = ISO9660SUSPSP_VER;
                pSP->bCheck1       = ISO9660SUSPSP_CHECK1;
                pSP->bCheck2       = ISO9660SUSPSP_CHECK2;
                pSP->cbSkip        = 0;
                pbSys += sizeof(*pSP);
                cbSys -= sizeof(*pSP);
            }
            if (pName->fRockNeedRRInDirRec)
            {
                PISO9660RRIPRR pRR = (PISO9660RRIPRR)pbSys;
                pRR->Hdr.bSig1     = ISO9660RRIPRR_SIG1;
                pRR->Hdr.bSig2     = ISO9660RRIPRR_SIG2;
                pRR->Hdr.cbEntry   = ISO9660RRIPRR_LEN;
                pRR->Hdr.bVersion  = ISO9660RRIPRR_VER;
                pRR->fFlags        = pName->fRockEntries;
                pbSys += sizeof(*pRR);
                cbSys -= sizeof(*pRR);
            }
            PISO9660SUSPCE const pCE = (PISO9660SUSPCE)pbSys;
            pCE->Hdr.bSig1     = ISO9660SUSPCE_SIG1;
            pCE->Hdr.bSig2     = ISO9660SUSPCE_SIG2;
            pCE->Hdr.cbEntry   = ISO9660SUSPCE_LEN;
            pCE->Hdr.bVersion  = ISO9660SUSPCE_VER;
            uint64_t const offData = pNamespace->pRRSpillFile->RangeNode.Core.Key + pName->offRockSpill;
            pCE->offBlock.be   = RT_H2BE_U32((uint32_t)(offData / ISO9660_SECTOR_SIZE));
            pCE->offBlock.le   = RT_H2LE_U32((uint32_t)(offData / ISO9660_SECTOR_SIZE));
            pCE->offData.be    = RT_H2BE_U32((uint32_t)(offData & ISO9660_SECTOR_OFFSET_MASK));
            pCE->offData.le    = RT_H2LE_U32((uint32_t)(offData & ISO9660_SECTOR_OFFSET_MASK));
            pCE->cbData.be     = RT_H2BE_U32((uint32_t)pName->cbRockSpill);
            pCE->cbData.le     = RT_H2LE_U32((uint32_t)pName->cbRockSpill);
            Assert(cbSys >= sizeof(*pCE));
        }
    }

    return pName->cbDirRec;
}


/**
 * Generates ISO-9660 directory records into the specified buffer.
 *
 * @returns Number of bytes copied into the buffer.
 * @param   pName           The namespace node.
 * @param   fUnicode        Set if the name should be translated to big endian
 *                          UTF-16BE / UCS-2BE, i.e. we're in the joliet namespace.
 * @param   pbBuf           The buffer.  This is at least pName->cbDirRecTotal
 *                          bytes big.
 * @param   pNamespace      The namespace.
 */
static uint32_t rtFsIsoMakerOutFile_GenerateDirRecDirect(PRTFSISOMAKERNAME pName, bool fUnicode, uint8_t *pbBuf,
                                                         PRTFSISOMAKERNAMESPACE pNamespace)
{
    /*
     * Normally there is just a single record without any zero padding.
     */
    uint32_t cbReturn = rtFsIsoMakerOutFile_GenerateDirRec(pName, fUnicode, pbBuf, pNamespace, RTFSISOMAKERDIRTYPE_OTHER);
    if (RT_LIKELY(pName->cbDirRecTotal == cbReturn))
        return cbReturn;
    Assert(cbReturn < pName->cbDirRecTotal);

    /*
     * Deal with multiple records.
     */
    if (pName->cDirRecs > 1)
    {
        Assert(pName->pObj->enmType == RTFSISOMAKEROBJTYPE_FILE);
        PRTFSISOMAKERFILE pFile = (PRTFSISOMAKERFILE)pName->pObj;

        /* Set max size and duplicate the first directory record cDirRecs - 1 times. */
        uint32_t const cbOne   = cbReturn;
        PISO9660DIRREC pDirRec = (PISO9660DIRREC)pbBuf;
        pDirRec->cbData.be   = RT_H2BE_U32_C(RTFSISOMAKER_MAX_ISO9660_EXTENT_SIZE);
        pDirRec->cbData.le   = RT_H2LE_U32_C(RTFSISOMAKER_MAX_ISO9660_EXTENT_SIZE);
        pDirRec->fFileFlags |= ISO9660_FILE_FLAGS_MULTI_EXTENT;

        PISO9660DIRREC pCurDirRec = pDirRec;
        uint32_t       offExtent  = (uint32_t)(pFile->RangeNode.Core.Key / RTFSISOMAKER_SECTOR_SIZE);
        Assert(offExtent == ISO9660_GET_ENDIAN(&pDirRec->offExtent));
        for (uint32_t iDirRec = 1; iDirRec < pName->cDirRecs; iDirRec++)
        {
            pCurDirRec = (PISO9660DIRREC)memcpy(&pbBuf[cbReturn], pDirRec, cbOne);

            offExtent += RTFSISOMAKER_MAX_ISO9660_EXTENT_SIZE / RTFSISOMAKER_SECTOR_SIZE;
            pCurDirRec->offExtent.le = RT_H2LE_U32(offExtent);

            cbReturn += cbOne;
        }
        Assert(cbReturn <= pName->cbDirRecTotal);

        /* Adjust the size in the final record. */
        uint32_t cbDataLast = (uint32_t)(pFile->cbData % RTFSISOMAKER_MAX_ISO9660_EXTENT_SIZE);
        pCurDirRec->cbData.be   = RT_H2BE_U32(cbDataLast);
        pCurDirRec->cbData.le   = RT_H2LE_U32(cbDataLast);
        pCurDirRec->fFileFlags &= ~ISO9660_FILE_FLAGS_MULTI_EXTENT;
    }

    /*
     * Do end of sector zero padding.
     */
    if (cbReturn < pName->cbDirRecTotal)
        memset(&pbBuf[cbReturn], 0, (uint32_t)pName->cbDirRecTotal - cbReturn);

    return pName->cbDirRecTotal;
}


/**
 * Deals with situations where the destination buffer doesn't cover the whole
 * directory record.
 *
 * @returns Number of bytes copied into the buffer.
 * @param   pName           The namespace node.
 * @param   fUnicode        Set if the name should be translated to big endian
 *                          UTF-16 / UCS-2, i.e. we're in the joliet namespace.
 * @param   off             The offset into the directory record.
 * @param   pbBuf           The buffer.
 * @param   cbBuf           The buffer size.
 * @param   pNamespace      The namespace.
 */
static uint32_t rtFsIsoMakerOutFile_GenerateDirRecPartial(PRTFSISOMAKERNAME pName, bool fUnicode,
                                                          uint32_t off, uint8_t *pbBuf, size_t cbBuf,
                                                          PRTFSISOMAKERNAMESPACE pNamespace)
{
    Assert(off < pName->cbDirRecTotal);

    /*
     * This is reasonably simple when there is only one directory record and
     * without any padding.
     */
    uint8_t abTmpBuf[256];
    Assert(pName->cbDirRec <= sizeof(abTmpBuf));
    uint32_t const cbOne = rtFsIsoMakerOutFile_GenerateDirRec(pName, fUnicode, abTmpBuf, pNamespace, RTFSISOMAKERDIRTYPE_OTHER);
    Assert(cbOne == pName->cbDirRec);
    if (cbOne == pName->cbDirRecTotal)
    {
        uint32_t cbToCopy = RT_MIN((uint32_t)cbBuf, cbOne - off);
        memcpy(pbBuf, &abTmpBuf[off], cbToCopy);
        return cbToCopy;
    }
    Assert(cbOne < pName->cbDirRecTotal);

    /*
     * Single record and zero padding?
     */
    uint32_t cbCopied = 0;
    if (pName->cDirRecs == 1)
    {
        /* Anything from the record to copy? */
        if (off < cbOne)
        {
            cbCopied = RT_MIN((uint32_t)cbBuf, cbOne - off);
            memcpy(pbBuf, &abTmpBuf[off], cbCopied);
            pbBuf += cbCopied;
            cbBuf -= cbCopied;
            off   += cbCopied;
        }

        /* Anything from the zero padding? */
        if (off >= cbOne && cbBuf > 0)
        {
            uint32_t cbToZero = RT_MIN((uint32_t)cbBuf, (uint32_t)pName->cbDirRecTotal - off);
            memset(pbBuf, 0, cbToZero);
            cbCopied += cbToZero;
        }
    }
    /*
     * Multi-extent stuff.  Need to modify the cbData member as we copy.
     */
    else
    {
        Assert(pName->pObj->enmType == RTFSISOMAKEROBJTYPE_FILE);
        PRTFSISOMAKERFILE pFile = (PRTFSISOMAKERFILE)pName->pObj;

        /* Max out the size. */
        PISO9660DIRREC pDirRec = (PISO9660DIRREC)abTmpBuf;
        pDirRec->cbData.be   = RT_H2BE_U32_C(RTFSISOMAKER_MAX_ISO9660_EXTENT_SIZE);
        pDirRec->cbData.le   = RT_H2LE_U32_C(RTFSISOMAKER_MAX_ISO9660_EXTENT_SIZE);
        pDirRec->fFileFlags |= ISO9660_FILE_FLAGS_MULTI_EXTENT;

        /* Copy directory records. */
        uint32_t offDirRec = pName->offDirRec;
        uint32_t offExtent = pFile->RangeNode.Core.Key / RTFSISOMAKER_SECTOR_SIZE;
        for (uint32_t i = 0; i < pName->cDirRecs && cbBuf > 0; i++)
        {
            uint32_t const offInRec = off - offDirRec;
            if (offInRec < cbOne)
            {
                /* Update the record. */
                pDirRec->offExtent.be = RT_H2BE_U32(offExtent);
                pDirRec->offExtent.le = RT_H2LE_U32(offExtent);
                if (i + 1 == pName->cDirRecs)
                {
                    uint32_t cbDataLast = pFile->cbData % RTFSISOMAKER_MAX_ISO9660_EXTENT_SIZE;
                    pDirRec->cbData.be   = RT_H2BE_U32(cbDataLast);
                    pDirRec->cbData.le   = RT_H2LE_U32(cbDataLast);
                    pDirRec->fFileFlags &= ~ISO9660_FILE_FLAGS_MULTI_EXTENT;
                }

                /* Copy chunk. */
                uint32_t cbToCopy = RT_MIN((uint32_t)cbBuf, cbOne - offInRec);
                memcpy(pbBuf, &abTmpBuf[offInRec], cbToCopy);
                cbCopied += cbToCopy;
                pbBuf    += cbToCopy;
                cbBuf    -= cbToCopy;
                off      += cbToCopy;
            }

            offDirRec += cbOne;
            offExtent += RTFSISOMAKER_MAX_ISO9660_EXTENT_SIZE / RTFSISOMAKER_SECTOR_SIZE;
        }

        /* Anything from the zero padding? */
        if (off >= offDirRec && cbBuf > 0)
        {
            uint32_t cbToZero = RT_MIN((uint32_t)cbBuf, (uint32_t)pName->cbDirRecTotal - offDirRec);
            memset(pbBuf, 0, cbToZero);
            cbCopied += cbToZero;
        }
    }

    return cbCopied;
}


/**
 * Generate a '.' or '..' directory record.
 *
 * This is the same as rtFsIsoMakerOutFile_GenerateDirRec, but with the filename
 * reduced to 1 byte.
 *
 * @returns Number of bytes copied into the buffer.
 * @param   pName           The directory namespace node.
 * @param   fUnicode        Set if the name should be translated to big endian
 *                          UTF-16 / UCS-2, i.e. we're in the joliet namespace.
 * @param   bDirId          The directory ID (0x00 or 0x01).
 * @param   off             The offset into the directory record.
 * @param   pbBuf           The buffer.
 * @param   cbBuf           The buffer size.
 * @param   pNamespace      The namespace.
 */
static uint32_t rtFsIsoMakerOutFile_GenerateSpecialDirRec(PRTFSISOMAKERNAME pName, bool fUnicode, uint8_t bDirId,
                                                          uint32_t off, uint8_t *pbBuf, size_t cbBuf,
                                                          PRTFSISOMAKERNAMESPACE pNamespace)
{
    Assert(off < pName->cbDirRec);
    Assert(pName->pDir);

    /* Generate a regular directory record. */
    uint8_t abTmpBuf[256];
    Assert(off < pName->cbDirRec);
    size_t cbToCopy = rtFsIsoMakerOutFile_GenerateDirRec(pName, fUnicode, abTmpBuf, pNamespace,
                                                         bDirId == 0 ? RTFSISOMAKERDIRTYPE_CURRENT : RTFSISOMAKERDIRTYPE_PARENT);
    Assert(cbToCopy == pName->cbDirRec);

    /** @todo r=bird: This isn't working quite right as the NM record includes the
     *        full directory name. Spill file stuff is shared with the (grand)parent
     *        directory entry. [RR NM ./..] */

    /* Replace the filename part. */
    PISO9660DIRREC pDirRec = (PISO9660DIRREC)abTmpBuf;
    if (pDirRec->bFileIdLength != 1)
    {
        uint8_t offSysUse = pDirRec->bFileIdLength + !(pDirRec->bFileIdLength & 1) + RT_UOFFSETOF(ISO9660DIRREC, achFileId);
        uint8_t cbSysUse  = pDirRec->cbDirRec - offSysUse;
        if (cbSysUse > 0)
            memmove(&pDirRec->achFileId[1], &abTmpBuf[offSysUse], cbSysUse);
        pDirRec->bFileIdLength = 1;
        cbToCopy = RT_UOFFSETOF(ISO9660DIRREC, achFileId) + 1 + cbSysUse;
        pDirRec->cbDirRec = (uint8_t)cbToCopy;
    }
    pDirRec->achFileId[0] = bDirId;

    /* Do the copying. */
    cbToCopy = RT_MIN(cbBuf, cbToCopy - off);
    memcpy(pbBuf, &abTmpBuf[off], cbToCopy);
    return (uint32_t)cbToCopy;
}


/**
 * Returns the (child) index of a directory entry given an byte offset into the
 * directory data.
 */
static uint32_t rtFsIsoMakerOutFile_LookupDirEntryByOffset(PRTFSISOMAKERNAMEDIR pDir, uint32_t offInDir)
{
    /** @todo binary search   */
    for (uint32_t iChild = 0; iChild < pDir->cChildren; iChild++)
    {
        PRTFSISOMAKERNAME pChild = pDir->papChildren[iChild];
        if (offInDir - pChild->offDirRec < pChild->cbDirRecTotal)
            return iChild;
    }
    return pDir->cChildren;
}


/**
 * Read directory records.
 *
 * This locates the directory at @a offUnsigned and generates directory records
 * for it.  Caller must repeat the call to get directory entries for the next
 * directory should there be desire for that.
 *
 * @returns Number of bytes copied into @a pbBuf.
 * @param   pDir            The directory to read from.
 * @param   pIsoMaker       The ISO maker instance.
 * @param   pNamespace      The namespace.
 * @param   fUnicode        Set if the name should be translated to big endian
 *                          UTF-16 / UCS-2, i.e. we're in the joliet namespace.
 * @param   offInDir64      The byte offset in the directory of the requested
 *                          data.
 * @param   pbBuf           The output buffer.
 * @param   cbBuf           How much to read.
 */
static size_t rtFsIsoMakerOutFile_ReadDirRecords(PRTFSISOMAKERNAMEDIR pDir, PRTFSISOMAKERNAMESPACE pNamespace,
                                                 bool fUnicode, uint64_t offInDir64, uint8_t *pbBuf, size_t cbBuf)
{
    /*
     * Generate content.
     */
    size_t cbDone = 0;
    uint32_t offInDir = (uint32_t)offInDir64;
    if (offInDir < pDir->cbDir)
    {
        PRTFSISOMAKERNAME   pDirName      = pDir->pName;
        PRTFSISOMAKERNAME   pParentName   = pDirName->pParent ? pDirName->pParent : pDirName;
        uint32_t            cbSpecialRecs = (uint32_t)pDir->cbDirRec00 + pDir->cbDirRec01;

        /*
         * Special '.' and/or '..' entries requested.
         */
        uint32_t iChild;
        if (offInDir < cbSpecialRecs)
        {
            /* do '.' */
            if (offInDir < pDir->cbDirRec00)
            {
                uint32_t cbCopied = rtFsIsoMakerOutFile_GenerateSpecialDirRec(pDirName, fUnicode, 0, offInDir,
                                                                              pbBuf, cbBuf, pNamespace);
                cbDone   += cbCopied;
                offInDir += cbCopied;
                pbBuf    += cbCopied;
                cbBuf    -= cbCopied;
            }

            /* do '..' */
            if (cbBuf > 0)
            {
                uint32_t cbCopied = rtFsIsoMakerOutFile_GenerateSpecialDirRec(pParentName, fUnicode, 1,
                                                                              offInDir - pDir->cbDirRec00,
                                                                              pbBuf, cbBuf, pNamespace);
                cbDone   += cbCopied;
                offInDir += cbCopied;
                pbBuf    += cbCopied;
                cbBuf    -= cbCopied;
            }

            iChild = 0;
        }
        /*
         * Locate the directory entry we should start with.
         */
        else
        {
            iChild = rtFsIsoMakerOutFile_LookupDirEntryByOffset(pDir, offInDir);
            AssertReturnStmt(iChild < pDir->cChildren, *pbBuf = 0xff, 1);
        }

        /*
         * Normal directory entries.
         */
        while (   cbBuf > 0
               && iChild < pDir->cChildren)
        {
            PRTFSISOMAKERNAME pChild = pDir->papChildren[iChild];
            uint32_t cbCopied;
            if (   offInDir == pChild->offDirRec
                && cbBuf    >= pChild->cbDirRecTotal)
                cbCopied = rtFsIsoMakerOutFile_GenerateDirRecDirect(pChild, fUnicode, pbBuf, pNamespace);
            else
                cbCopied = rtFsIsoMakerOutFile_GenerateDirRecPartial(pChild, fUnicode, offInDir - pChild->offDirRec,
                                                                     pbBuf, cbBuf, pNamespace);

            cbDone   += cbCopied;
            offInDir += cbCopied;
            pbBuf    += cbCopied;
            cbBuf    -= cbCopied;
            iChild++;
        }

        /*
         * Check if we're into the zero padding at the end of the directory now.
         */
        if (   cbBuf > 0
            && iChild >= pDir->cChildren)
        {
            size_t cbZeros = RT_MIN(cbBuf, RTFSISOMAKER_SECTOR_SIZE - (pDir->cbDir & RTFSISOMAKER_SECTOR_OFFSET_MASK));
            memset(pbBuf, 0, cbZeros);
            cbDone += cbZeros;
        }
    }
    else
    {
        cbDone = RT_MIN(cbBuf, RT_ALIGN_32(pDir->cbDir, RTFSISOMAKER_SECTOR_SIZE) - offInDir);
        memset(pbBuf, 0, cbDone);
    }

    return cbDone;
}


/**
 * Callback for reading directory structures for the primary ISO-9660 and joliet
 * namespaces.
 */
static DECLCALLBACK(int)
rtFsIsoMakerOutFile_ReadIso9660Dir(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker,
                                   PCRTFSISOMAKERRANGENODE pRangeNode, uint64_t offInRange, uint8_t *pbBuf, size_t cbToRead)
{
    PRTFSISOMAKERNAMEDIR const pDir = RT_FROM_MEMBER(pRangeNode, RTFSISOMAKERNAMEDIR, RangeNode);
    size_t cbDone = rtFsIsoMakerOutFile_ReadDirRecords(pDir, &pIsoMaker->PrimaryIso, false /*fUnicode*/,
                                                       offInRange, pbBuf, cbToRead);
    AssertReturn(cbDone == cbToRead, VERR_INTERNAL_ERROR_5);
    RT_NOREF(pThis);
    return VINF_SUCCESS;
}


/**
 * Callback for reading directory structures for the primary ISO-9660 and joliet
 * namespaces.
 */
static DECLCALLBACK(int)
rtFsIsoMakerOutFile_ReadIso9660DirJoliet(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker,
                                         PCRTFSISOMAKERRANGENODE pRangeNode, uint64_t offInRange, uint8_t *pbBuf, size_t cbToRead)
{
    PRTFSISOMAKERNAMEDIR const pDir = RT_FROM_MEMBER(pRangeNode, RTFSISOMAKERNAMEDIR, RangeNode);
    size_t cbDone = rtFsIsoMakerOutFile_ReadDirRecords(pDir, &pIsoMaker->Joliet, true /*fUnicode*/, offInRange, pbBuf, cbToRead);
    AssertReturn(cbDone == cbToRead, VERR_INTERNAL_ERROR_5);
    RT_NOREF(pThis);
    return VINF_SUCCESS;
}


/**
 * Helper for reading from a single producer.
 */
int rtFsIsoMakerOutFile_ReadProducerSector(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker,
                                           PCRTFSISOMAKERRANGENODE pRangeNode, uint64_t offInRange,
                                           uint8_t *pbBuf, size_t cbToRead, PFNRTFSISOMAKERPRODUCER pfnProducer)
{
    AssertReturn(offInRange < RTFSISOMAKER_SECTOR_SIZE, VERR_INTERNAL_ERROR_4);
    AssertReturn(cbToRead  <= RTFSISOMAKER_SECTOR_SIZE - offInRange, VERR_INTERNAL_ERROR_4);

    /* Call the producer with a full sector buffer. */
    uint8_t abSectorBuf[RTFSISOMAKER_SECTOR_SIZE] = {0};
    int rc = pfnProducer(pIsoMaker, abSectorBuf, (pRangeNode->Core.Key + offInRange) / RTFSISOMAKER_SECTOR_SIZE, 0);
    AssertRCReturn(rc, rc);

    /* Copy out the requested data. */
    memcpy(pbBuf, &abSectorBuf[(size_t)offInRange], cbToRead);

    RT_NOREF(pThis);
    return VINF_SUCCESS;
}


/**
 * Helper for reading from a producer table.
 */
static int
rtFsIsoMakerOutFile_ReadProducerTable(PRTFSISOMAKERINT pIsoMaker, uint32_t cProducers, PCFSISOMAKERPRODUCERENTRY paProducers,
                                      uint32_t idxBaseLocation, uint64_t offInRange, uint8_t *pbBuf, size_t cbToRead)
{
    for (uint32_t idx = (uint32_t)(offInRange / RTFSISOMAKER_SECTOR_SIZE); ; idx++)
    {
        AssertReturn(idx < cProducers, VERR_INTERNAL_ERROR_4);

        /* Call the producer with a full sector buffer. */
        uint8_t abSectorBuf[RTFSISOMAKER_SECTOR_SIZE] = {0};
        AssertPtrReturn(pIsoMaker->aUdfAvdpAndVdsEntries[idx].pfnProducer, VERR_INTERNAL_ERROR_4);
        int rc = paProducers[idx].pfnProducer(pIsoMaker, abSectorBuf, idxBaseLocation + idx, paProducers[idx].uInfo);
        AssertRCReturn(rc, rc);

        /* Copy from the sector buffer into the reader's buffer. */
        size_t const offSector = offInRange - idx * RTFSISOMAKER_SECTOR_SIZE;
        size_t const cbToCopy  = RT_MIN(cbToRead, RTFSISOMAKER_SECTOR_SIZE - offSector);
        memcpy(pbBuf, &abSectorBuf[offSector], cbToCopy);
        Log(("ISOMAKER/UDF: rtFsIsoMakerOutFile_ReadProducerTable: idx=%u cbToCopy=%#zx\n", idx, cbToCopy));

        /* Advance. */
        if (cbToRead <= cbToCopy)
            break;
        cbToRead   -= cbToCopy;
        pbBuf      += cbToCopy;
        offInRange += cbToCopy;
    }

    return VINF_SUCCESS;
}


/**
 * Callback for reading the file set descriptor sequence.
 */
static DECLCALLBACK(int)
rtFsIsoMakerOutFile_ReadUdfFileSetDescSeq(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker,
                                          PCRTFSISOMAKERRANGENODE pRangeNode, uint64_t offInRange, uint8_t *pbBuf, size_t cbToRead)
{
    Log(("ISOMAKER/UDF: rtFsIsoMakerOutFile_ReadUdfVds256: %#RX64 LB %#zx\n", offInRange, cbToRead));
    RT_NOREF(pThis);
    return rtFsIsoMakerOutFile_ReadProducerTable(pIsoMaker, RT_ELEMENTS(s_aFileSetDescSeqEntries), s_aFileSetDescSeqEntries,
                                                 (uint32_t)(  (pRangeNode->Core.Key - pIsoMaker->offUdfPartition)
                                                            / RTFSISOMAKER_SECTOR_SIZE),
                                                 offInRange, pbBuf, cbToRead);
}


/**
 * Callback for reading standalone UDF AVDP.
 */
static DECLCALLBACK(int)
rtFsIsoMakerOutFile_ReadUdfAvdp(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker, PCRTFSISOMAKERRANGENODE pRangeNode,
                                uint64_t offInRange, uint8_t *pbBuf, size_t cbToRead)
{
    return rtFsIsoMakerOutFile_ReadProducerSector(pThis, pIsoMaker, pRangeNode, offInRange, pbBuf, cbToRead,
                                                  rtFsIsoMakerUdfProduceAnchorVolumeDescPtr);
}


/**
 * Helper for reading a complete UDF file ID descriptor.
 */
static uint32_t rtFsIsoMakerOutFile_ReadUdfDirEntry(PRTFSISOMAKERINT pIsoMaker, PRTFSISOMAKERNAME pChild, uint32_t offLocation,
                                                    uint8_t *pbBuf, bool fDotDot = false)
{
    PUDFFILEIDDESC const pFileIdDesc = (PUDFFILEIDDESC)pbBuf;

    /*
     * Version, Flags, ICB and dot-dot.
     */
    pFileIdDesc->uVersion = 1;
    PCRTFSISOMAKERNAMEDIR const pDir = pChild->pDir;
    if (pDir)
    {
        rtFsIsoMakerUdfSetLongAllocDesc(&pFileIdDesc->Icb, RTFSISOMAKER_SECTOR_SIZE, pChild->uUdfIcbSector,
                                        pChild->pParent ? pChild->pObj->idxObj + RTISOMAKER_UDF_UNIQUE_ID_OFFSET : 0);
        if (fDotDot)
        {
            pFileIdDesc->fFlags                 = UDF_FILE_FLAGS_DIRECTORY | UDF_FILE_FLAGS_PARENT;
            pFileIdDesc->cbName                 = 0;
            pFileIdDesc->cbImplementationUse    = 0;
            pFileIdDesc->abImplementationUse[0] = 0;
            pFileIdDesc->abImplementationUse[1] = 0;
            size_t const cbFid = RT_UOFFSETOF(UDFFILEIDDESC, abImplementationUse) + 2;
            rtFsIsoMakerUdfPopulateTag(pIsoMaker, &pFileIdDesc->Tag, UDF_TAG_ID_FILE_ID_DESC, cbFid, offLocation);
            return cbFid;
        }
        pFileIdDesc->fFlags = UDF_FILE_FLAGS_DIRECTORY;
    }
    else
    {
        Assert(!fDotDot);
        rtFsIsoMakerUdfSetLongAllocDesc(&pFileIdDesc->Icb, RTFSISOMAKER_SECTOR_SIZE, pChild->uUdfIcbSector,
                                        pChild->pObj->idxObj + RTISOMAKER_UDF_UNIQUE_ID_OFFSET);
        pFileIdDesc->fFlags = 0;
    }

    /*
     * Padding and name.
     */
    pFileIdDesc->cbImplementationUse = pChild->cbDirRecTotal - pChild->cbDirRec;
    uint8_t *puchName;
    if (!pFileIdDesc->cbImplementationUse)
        puchName = pFileIdDesc->abImplementationUse;
    else
    {
        RT_BZERO(&pFileIdDesc->abImplementationUse[0], pFileIdDesc->cbImplementationUse);
        puchName = &pFileIdDesc->abImplementationUse[pFileIdDesc->cbImplementationUse];
    }

    const char *pszSrc = pChild->szName;
    pFileIdDesc->cbName = (uint8_t)pChild->cbNameInDirRec;
    AssertCompile(RTFSISOMAKERNAME_UDF_8BIT_NAME_FLAG > UINT8_MAX);
    if (pChild->cbNameInDirRec & RTFSISOMAKERNAME_UDF_8BIT_NAME_FLAG)
    {
        /* 8-bit name. Doing home-cooked UTF-8 decoding here for speed+fun. */
        *puchName++ = 8;
        unsigned cchLeft = (uint8_t)pChild->cbNameInDirRec - 1;
        while (cchLeft-- > 0)
        {
            unsigned char uch = (unsigned char)*pszSrc++;
            if (!(uch & 0x80))
                *puchName++ = uch;
            else
            {
                unsigned char uch2 = (unsigned char)*pszSrc++;
                Assert((uch2 & 0xc0) == 0x80);
                Assert((uch  & 0xfc) == 0xc0);
                *puchName++ = (uch2 & 0x3f)
                            | ((uch & 0x3) << 6);
            }
        }
    }
    else
    {
        /* 16-bit name. */
        *puchName++ = 16;
        for (;;)
        {
            RTUNICP uc;
            RTStrGetCpEx(&pszSrc, &uc);
            if (uc)
                puchName = (uint8_t *)RTUtf16PutCp((PRTUTF16)puchName, uc);
            else
                break;
        }
    }

    /*
     * The descriptor must be a multiple of 4 bytes, so pad it if necessary.
     */
    while (((uintptr_t)puchName - (uintptr_t)pFileIdDesc) & 3)
        *puchName++ = 0;

    /*
     * Finally, popupate the tag and return.
     */
    Assert(pChild->cbDirRecTotal == (uintptr_t)puchName - (uintptr_t)pFileIdDesc);
    rtFsIsoMakerUdfPopulateTag(pIsoMaker, &pFileIdDesc->Tag, UDF_TAG_ID_FILE_ID_DESC, pChild->cbDirRecTotal, offLocation);

    return pChild->cbDirRecTotal;
}


/**
 * Helper for reading a partial UDF file ID descriptor.
 */
static uint32_t
rtFsIsoMakerOutFile_ReadUdfDirEntryPartial(PRTFSISOMAKERINT pIsoMaker, PRTFSISOMAKERNAME pChild,uint32_t offLocation,
                                           uint64_t off, uint8_t *pbBuf, size_t cbToRead, bool fDotDot = false)
{
    uint8_t        abTmp[RTFSISOMAKER_SECTOR_SIZE];
    uint32_t const cbTmp = rtFsIsoMakerOutFile_ReadUdfDirEntry(pIsoMaker, pChild, offLocation, abTmp, fDotDot);
    AssertReturnStmt(cbTmp > off, *pbBuf = 0xff, 1);

    uint32_t const cbToCopy = (uint32_t)RT_MIN(cbTmp - off, cbToRead);
    memcpy(pbBuf, &abTmp[(size_t)off], cbToCopy);
    return cbToCopy;
}


/**
 * Callback for reading a UDF directory.
 */
static DECLCALLBACK(int)
rtFsIsoMakerOutFile_ReadUdfDir(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker, PCRTFSISOMAKERRANGENODE pRangeNode,
                               uint64_t offInRange, uint8_t *pbBuf, size_t cbToRead)
{
    PRTFSISOMAKERNAMEDIR const pDir        = RT_FROM_MEMBER(pRangeNode, RTFSISOMAKERNAMEDIR, RangeNode);
    uint32_t const             offLocation = (pRangeNode->Core.Key - pIsoMaker->offUdfPartition) / RTFSISOMAKER_SECTOR_SIZE;
    RT_NOREF(pThis);
    Assert(offInRange < pDir->cbDir);

    /*
     * Generate content.
     */
    size_t   cbDone   = 0;
    PRTFSISOMAKERNAME   pDirName      = pDir->pName;
    PRTFSISOMAKERNAME   pParentName   = pDirName->pParent ? pDirName->pParent : pDirName;

    /*
     * Do we read '..'?
     */
    uint32_t iChild;
    if (offInRange < pDir->cbDirRec00)
    {
        uint32_t cbCopied;
        if (   offInRange == 0
            && cbToRead   >= pDir->cbDirRec00)
        {
            cbCopied = rtFsIsoMakerOutFile_ReadUdfDirEntry(pIsoMaker, pParentName, offLocation, pbBuf, true /*fDotDot*/);
            Assert(cbCopied == pDir->cbDirRec00);
        }
        else
            cbCopied = rtFsIsoMakerOutFile_ReadUdfDirEntryPartial(pIsoMaker, pParentName, offLocation, offInRange,
                                                                  pbBuf, cbToRead, true /*fDotDot*/);

        cbDone     += cbCopied;
        offInRange += cbCopied;
        pbBuf      += cbCopied;
        cbToRead   -= cbCopied;

        iChild = 0;
    }
    /*
     * Locate the directory entry we should start with.
     */
    else
    {
        iChild = rtFsIsoMakerOutFile_LookupDirEntryByOffset(pDir, (uint32_t)offInRange);
        AssertReturnStmt(iChild < pDir->cChildren, RT_BZERO(pbBuf, cbToRead), VINF_SUCCESS);
    }

    /*
     * Normal directory entries.
     */
    while (   cbToRead > 0
           && iChild < pDir->cChildren)
    {
        PRTFSISOMAKERNAME const pChild = pDir->papChildren[iChild];
        uint32_t cbCopied;
        if (   offInRange == pChild->offDirRec
            && cbToRead   >= pChild->cbDirRecTotal)
            cbCopied = rtFsIsoMakerOutFile_ReadUdfDirEntry(pIsoMaker, pChild, offLocation, pbBuf);
        else
            cbCopied = rtFsIsoMakerOutFile_ReadUdfDirEntryPartial(pIsoMaker, pChild, offLocation, offInRange - pChild->offDirRec,
                                                                  pbBuf, cbToRead);

        cbDone     += cbCopied;
        offInRange += cbCopied;
        pbBuf      += cbCopied;
        cbToRead   -= cbCopied;
        iChild++;
    }
    return VINF_SUCCESS;
}


static void rtFsIsoMakerOutFile_ReadUdfIcbOneEntry(PRTFSISOMAKERINT pIsoMaker, PRTFSISOMAKEROBJ pObj, PUDFFILEENTRY pFileEntry,
                                                   uint32_t offLocation)
{
    PRTFSISOMAKERNAME const    pName = pObj->pUdfName;
    PRTFSISOMAKERNAMEDIR const pDir  = pName->pDir;
    PRTFSISOMAKERFILE const    pFile = pObj->enmType == RTFSISOMAKEROBJTYPE_FILE ? (PRTFSISOMAKERFILE)pObj : NULL;
    Assert((pDir != NULL) == (pObj->enmType == RTFSISOMAKEROBJTYPE_DIR));

    pFileEntry->IcbTag.cEntiresBeforeThis   = 0;
    pFileEntry->IcbTag.uStrategyType        = UDF_ICB_STRATEGY_TYPE_4;
    pFileEntry->IcbTag.abStrategyParams[0]  = 0;
    pFileEntry->IcbTag.abStrategyParams[1]  = 0;
    pFileEntry->IcbTag.cMaxEntries          = 1;
    pFileEntry->IcbTag.bReserved            = 0;
    pFileEntry->IcbTag.bFileType            = pObj->enmType == RTFSISOMAKEROBJTYPE_DIR     ? UDF_FILE_TYPE_DIRECTORY
                                            : pObj->enmType == RTFSISOMAKEROBJTYPE_FILE    ? UDF_FILE_TYPE_REGULAR_FILE
                                            : pObj->enmType == RTFSISOMAKEROBJTYPE_SYMLINK ? UDF_FILE_TYPE_SYMBOLIC_LINK
                                            :                                                UDF_FILE_TYPE_NOT_SPECIFIED;
    Assert(pFileEntry->IcbTag.bFileType != UDF_FILE_TYPE_NOT_SPECIFIED);
    pFileEntry->IcbTag.ParentIcb.off        = 0;
    pFileEntry->IcbTag.ParentIcb.uPartitionNo = 0;
    pFileEntry->IcbTag.fFlags               = UDF_ICB_FLAGS_AD_TYPE_SHORT;
    if (pName->fMode & RTFS_UNIX_ISUID)     pFileEntry->IcbTag.fFlags |= UDF_ICB_FLAGS_SET_UID;
    if (pName->fMode & RTFS_UNIX_ISGID)     pFileEntry->IcbTag.fFlags |= UDF_ICB_FLAGS_SET_GID;
    if (pName->fMode & RTFS_UNIX_ISTXT)     pFileEntry->IcbTag.fFlags |= UDF_ICB_FLAGS_STICKY;
    if (pName->fMode & RTFS_DOS_ARCHIVED)   pFileEntry->IcbTag.fFlags |= UDF_ICB_FLAGS_ARCHIVE; /* not sure we allow setting this... */
    pFileEntry->uid                         = pName->uid != NIL_RTUID ? (uint32_t)pName->uid : UINT32_MAX;
    pFileEntry->gid                         = pName->gid != NIL_RTGID ? (uint32_t)pName->gid : UINT32_MAX;
    pFileEntry->fPermissions                = 0;
    if (pName->fMode & RTFS_UNIX_IRUSR)     pFileEntry->fPermissions |= UDF_PERM_USR_READ;
    if (pName->fMode & RTFS_UNIX_IWUSR)     pFileEntry->fPermissions |= UDF_PERM_USR_WRITE | UDF_PERM_USR_DELETE | UDF_PERM_USR_ATTRIB;
    if (pName->fMode & RTFS_UNIX_IXUSR)     pFileEntry->fPermissions |= UDF_PERM_USR_EXEC;
    if (pName->fMode & RTFS_UNIX_IRGRP)     pFileEntry->fPermissions |= UDF_PERM_GRP_READ;
    if (pName->fMode & RTFS_UNIX_IWGRP)     pFileEntry->fPermissions |= UDF_PERM_GRP_WRITE | UDF_PERM_GRP_DELETE | UDF_PERM_GRP_ATTRIB;
    if (pName->fMode & RTFS_UNIX_IXGRP)     pFileEntry->fPermissions |= UDF_PERM_GRP_EXEC;
    if (pName->fMode & RTFS_UNIX_IROTH)     pFileEntry->fPermissions |= UDF_PERM_OTH_READ;
    if (pName->fMode & RTFS_UNIX_IWOTH)     pFileEntry->fPermissions |= UDF_PERM_OTH_WRITE | UDF_PERM_OTH_DELETE | UDF_PERM_OTH_ATTRIB;
    if (pName->fMode & RTFS_UNIX_IXOTH)     pFileEntry->fPermissions |= UDF_PERM_OTH_EXEC;
    pFileEntry->cHardlinks                  = pName->cHardlinks;
    pFileEntry->uRecordFormat               = UDF_REC_FMT_NOT_SPECIFIED;
    pFileEntry->fRecordDisplayAttribs       = UDF_REC_ATTR_NOT_SPECIFIED;
    pFileEntry->cbRecord                    = 0;
    pFileEntry->cbData                      = pDir ? pDir->cbDir : pFile ? pFile->cbData : 0;
    pFileEntry->cLogicalBlocks              = (pFileEntry->cbData + RTFSISOMAKER_SECTOR_OFFSET_MASK) / RTFSISOMAKER_SECTOR_SIZE;
    rtFsIsoMakerUdfTimestampFromTimespec(&pFileEntry->AccessTime,       &pObj->AccessedTime);
    rtFsIsoMakerUdfTimestampFromTimespec(&pFileEntry->ModificationTime, &pObj->ModificationTime);
    rtFsIsoMakerUdfTimestampFromTimespec(&pFileEntry->ChangeTime,       &pObj->ChangeTime);
    pFileEntry->uCheckpoint                 = 1;
    rtFsIsoMakerUdfSetLongAllocDesc(&pFileEntry->ExtAttribIcb, 0, 0, 0, 0, 0);
    rtFsIsoMakerUdfSetEntityIdImplementation(&pFileEntry->idImplementation, g_szUdfImplId);
    pFileEntry->INodeId                     = !pDir || pName->pParent ? pObj->idxObj + RTISOMAKER_UDF_UNIQUE_ID_OFFSET : 0;

    /* Adding birth time as extra attribute: */
    pFileEntry->cbExtAttribs                = (uint32_t)sizeof(UDFEXTATTRIBHDRDESC)
                                            + (uint32_t)RT_UOFFSETOF_DYN(UDFGEA, u.FileTimes.aTimestamps[1]);

    /* Calc number of allocation descriptors: */
    uint32_t const cbPerAllocDesc = _1G - RTFSISOMAKER_SECTOR_SIZE * 2;
    uint32_t       cAllocDesc     = (pFileEntry->cbData + cbPerAllocDesc - 1) / cbPerAllocDesc;
    pFileEntry->cbAllocDescs                = cAllocDesc * sizeof(UDFSHORTAD);
    if (cAllocDesc > 1)
        pFileEntry->IcbTag.fFlags          |= UDF_ICB_FLAGS_CONTIGUOUS;

    /* Check that we don't run out of space in the sector, making adjustments if we do: */
    uint32_t      cbMaxExtra = RTFSISOMAKER_SECTOR_SIZE - RT_UOFFSETOF(UDFFILEENTRY, abExtAttribs);
    if (pFileEntry->cbAllocDescs + pFileEntry->cbExtAttribs > cbMaxExtra)
    {
        /** @todo We need use UDF_AD_TYPE_NEXT to link to an UDFALLOCATIONEXTENTDESC
         *        here. */
        pFileEntry->cbExtAttribs = 0;
        if (pFileEntry->cbAllocDescs > cbMaxExtra)
        {
            cAllocDesc = cbMaxExtra / sizeof(UDFSHORTAD);
            pFileEntry->cbAllocDescs = cAllocDesc * sizeof(UDFSHORTAD);
        }
    }
    /* Produce the extended attribute: */
    else
    {
        PUDFEXTATTRIBHDRDESC pEaHdr = (PUDFEXTATTRIBHDRDESC)pFileEntry->abExtAttribs;
        pEaHdr->offApplicationAttribs    = pFileEntry->cbExtAttribs; /* not present */
        pEaHdr->offImplementationAttribs = pFileEntry->cbExtAttribs; /* not present */
        rtFsIsoMakerUdfPopulateTag(pIsoMaker, &pEaHdr->Tag, UDF_TAG_ID_EXTENDED_ATTRIB_HDR_DESC, sizeof(*pEaHdr), offLocation);

        PUDFGEA const pGenEa = (PUDFGEA)(pEaHdr + 1);
        pGenEa->uAttribType     = UDFEADATAFILETIMES_ATTRIB_TYPE;
        pGenEa->uAttribSubtype  = UDFEADATAFILETIMES_ATTRIB_SUBTYPE;
        pGenEa->abReserved[0]   = 0;
        pGenEa->abReserved[1]   = 0;
        pGenEa->abReserved[2]   = 0;
        pGenEa->cbAttrib        = RT_UOFFSETOF_DYN(UDFGEA, u.FileTimes.aTimestamps[1]);
        pGenEa->u.FileTimes.cbTimestamps = sizeof(pGenEa->u.FileTimes.aTimestamps[0]);
        pGenEa->u.FileTimes.fFlags       = UDF_FILE_TIMES_EA_F_BIRTH;
        rtFsIsoMakerUdfTimestampFromTimespec(&pGenEa->u.FileTimes.aTimestamps[0], &pObj->BirthTime);
    }

    /* Produce the allocation descriptors: */
    PUDFSHORTAD const paAllocDescs   = (PUDFSHORTAD)&pFileEntry->abExtAttribs[pFileEntry->cbExtAttribs];
    uint64_t          cbLeft         = pFileEntry->cbData;
    uint32_t          offBlockInPart = (  (pFile ? pFile->RangeNode.Core.Key
                                          : pDir ? pDir->RangeNode.Core.Key
                                          : UINT64_MAX / 2)
                                        - pIsoMaker->offUdfPartition)
                                     / RTFSISOMAKER_SECTOR_SIZE;
    for (uint32_t idx = 0; idx < cAllocDesc; idx++)
    {
        uint32_t const cbThis = (uint32_t)RT_MIN(cbLeft, cbPerAllocDesc);
        paAllocDescs[idx].cb    = cbThis;
        paAllocDescs[idx].uType = UDF_AD_TYPE_RECORDED_AND_ALLOCATED;
        paAllocDescs[idx].off   = offBlockInPart;

        cbLeft         -= cbPerAllocDesc;
        offBlockInPart += cbPerAllocDesc / RTFSISOMAKER_SECTOR_SIZE;
    }

    /* Zero any unused bytes in the block. */
    size_t const cbTotal = RT_UOFFSETOF(UDFFILEENTRY, abExtAttribs) + pFileEntry->cbExtAttribs + pFileEntry->cbAllocDescs;
    if (cbTotal < RTFSISOMAKER_SECTOR_SIZE)
        RT_BZERO((uint8_t *)pFileEntry + cbTotal, RTFSISOMAKER_SECTOR_SIZE - cbTotal);

    /* Finally, Tag and CRC the thing.  */
    rtFsIsoMakerUdfPopulateTag(pIsoMaker, &pFileEntry->Tag, UDF_TAG_ID_FILE_ENTRY, cbTotal, offLocation);
}


/**
 * Callback for reading ICBs (for objects in the directory hierarchy).
 */
static DECLCALLBACK(int)
rtFsIsoMakerOutFile_ReadUdfIcbs(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker,
                                PCRTFSISOMAKERRANGENODE pRangeNode, uint64_t offInRange, uint8_t *pbBuf, size_t cbToRead)
{
    uint32_t const idxBaseLocation = (uint32_t)((pRangeNode->Core.Key - pIsoMaker->offUdfPartition) / RTFSISOMAKER_SECTOR_SIZE);
    while (cbToRead > 0)
    {
        uint32_t const         idxObj      = (uint32_t)(offInRange / RTFSISOMAKER_SECTOR_SIZE);
        PRTFSISOMAKEROBJ const pObj        = pIsoMaker->papUdfIcbToObjs[idxObj];
        size_t const           offInSector = (size_t)(offInRange & RTFSISOMAKER_SECTOR_OFFSET_MASK);
        if (offInSector == 0 && cbToRead >= RTFSISOMAKER_SECTOR_SIZE)
        {
            RT_BZERO(pbBuf, RTFSISOMAKER_SECTOR_SIZE);
            rtFsIsoMakerOutFile_ReadUdfIcbOneEntry(pIsoMaker, pObj, (PUDFFILEENTRY)pbBuf, idxBaseLocation + idxObj);
            offInRange += RTFSISOMAKER_SECTOR_SIZE;
            pbBuf      += RTFSISOMAKER_SECTOR_SIZE;
            cbToRead   -= RTFSISOMAKER_SECTOR_SIZE;
        }
        else
        {
            /* double buffer it. */
            uint8_t abTmp[RTFSISOMAKER_SECTOR_SIZE] = {0};
            rtFsIsoMakerOutFile_ReadUdfIcbOneEntry(pIsoMaker, pObj, (PUDFFILEENTRY)abTmp, idxBaseLocation + idxObj);
            size_t const cbToCopy = RT_MIN(RTFSISOMAKER_SECTOR_SIZE - offInSector, cbToRead);
            memcpy(pbBuf, &abTmp[offInSector], cbToCopy);
            offInRange += cbToCopy;
            pbBuf      += cbToCopy;
            cbToRead   -= cbToCopy;
        }
    }

    RT_NOREF(pThis, pRangeNode);
    return VINF_SUCCESS;
}


/**
 * Callback for reading UDF AVDP, VDS, and stuff starting at sector 256.
 */
static DECLCALLBACK(int)
rtFsIsoMakerOutFile_ReadUdfVds256(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker, PCRTFSISOMAKERRANGENODE pRangeNode,
                                  uint64_t offInRange, uint8_t *pbBuf, size_t cbToRead)
{
    RT_NOREF(pThis);
    Log(("ISOMAKER/UDF: rtFsIsoMakerOutFile_ReadUdfVds256: %#RX64 LB %#zx\n", offInRange, cbToRead));
    return rtFsIsoMakerOutFile_ReadProducerTable(pIsoMaker, pIsoMaker->cUdfAvdpAndVdsEntries, pIsoMaker->aUdfAvdpAndVdsEntries,
                                                 (uint32_t)(pRangeNode->Core.Key / RTFSISOMAKER_SECTOR_SIZE),
                                                 offInRange, pbBuf, cbToRead);
}


/**
 * Callback for reading ISO-9660 volume descriptor sequence.
 */
static DECLCALLBACK(int)
rtFsIsoMakerOutFile_ReadIso9660Vds16(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker,
                                      PCRTFSISOMAKERRANGENODE pRangeNode, uint64_t offInRange, uint8_t *pbBuf, size_t cbToRead)
{
    RT_NOREF(pThis, pRangeNode);
    Assert(offInRange < pIsoMaker->cVolumeDescriptors * RTFSISOMAKER_SECTOR_SIZE);
    Assert(cbToRead  <= pIsoMaker->cVolumeDescriptors * RTFSISOMAKER_SECTOR_SIZE - offInRange);
    memcpy(pbBuf, &pIsoMaker->pbVolDescs[(size_t)offInRange], cbToRead);
    return VINF_SUCCESS;
}


/**
 * Callback for reading the system area (sectors 0 thru 15).
 */
static DECLCALLBACK(int)
rtFsIsoMakerOutFile_ReadSysArea(PRTFSISOMAKEROUTPUTFILE pThis, PRTFSISOMAKERINT pIsoMaker,
                                PCRTFSISOMAKERRANGENODE pRangeNode, uint64_t offInRange, uint8_t *pbBuf, size_t cbToRead)
{
    RT_NOREF(pThis, pRangeNode);
    Assert(offInRange < pIsoMaker->cbSysArea);
    Assert(cbToRead  <= pIsoMaker->cbSysArea - offInRange);
    memcpy(pbBuf, &pIsoMaker->pbSysArea[(size_t)offInRange], cbToRead);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtFsIsoMakerOutFile_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTFSISOMAKEROUTPUTFILE pThis     = (PRTFSISOMAKEROUTPUTFILE)pvThis;
    PRTFSISOMAKERINT        pIsoMaker = pThis->pIsoMaker;
    size_t                  cbToRead  = pSgBuf->paSegs[0].cbSeg;
    uint8_t                *pbBuf     = (uint8_t *)pSgBuf->paSegs[0].pvSeg;

    Assert(pSgBuf->cSegs == 1);
    RT_NOREF(fBlocking);

    /*
     * Process the offset, checking for end-of-file.
     */
    uint64_t offUnsigned;
    if (off < 0)
        offUnsigned = pThis->offCurPos;
    else
        offUnsigned = (uint64_t)off;
    if (offUnsigned >= pIsoMaker->cbFinalizedImage)
    {
        if (*pcbRead)
        {
            *pcbRead = 0;
            Log3(("ISOMAKER: rtFsIsoMakerOutFile_Read: off=%#RX64 cbToRead=%#zx -> VINF_EOF *pcbRead=0\n", offUnsigned, cbToRead));
            return VINF_EOF;
        }
        Log3(("ISOMAKER: rtFsIsoMakerOutFile_Read: off=%#RX64 cbToRead=%#zx -> VERR_EOF (1)\n", offUnsigned, cbToRead));
        return VERR_EOF;
    }
    if (   !pcbRead
        && pIsoMaker->cbFinalizedImage - offUnsigned < cbToRead)
    {
        Log3(("ISOMAKER: rtFsIsoMakerOutFile_Read: off=%#RX64 cbToRead=%#zx -> VERR_EOF (2)\n", offUnsigned, cbToRead));
        return VERR_EOF;
    }

    /*
     * Produce the bytes.
     */
    Log3(("ISOMAKER: rtFsIsoMakerOutFile_Read: %#RX64..%#RX64 (cbToRead=%#zx):\n",
          offUnsigned, offUnsigned + cbToRead, cbToRead));
    int    rc     = VINF_SUCCESS;
    size_t cbRead = 0;
    while (cbToRead > 0)
    {
        size_t cbDone;
        PCRTFSISOMAKERRANGENODE pRangeNode = (PCRTFSISOMAKERRANGENODE)RTAvlrU64GetBestFit(&pIsoMaker->RangeTree, offUnsigned,
                                                                                          true /*fAbove*/);
        if (pRangeNode)
        {
            /* If the offset is lower than the range, produce zeros for the gap. */
            if (offUnsigned < pRangeNode->Core.Key)
            {
                Log3(("ISOMAKER: rtFsIsoMakerOutFile_Read:   off=%#RX64 -> %#RX64..%#RX64 -> ZEROs\n",
                      offUnsigned, pRangeNode->Core.Key, pRangeNode->Core.KeyLast));
                uint64_t const cbZeros = pRangeNode->Core.Key - offUnsigned;
                if (cbZeros < cbToRead)
                    RT_BZERO(pbBuf, cbZeros);
                else
                {
                    RT_BZERO(pbBuf, cbToRead);
                    cbRead += cbToRead;
                    break;
                }
                cbRead      += cbZeros;
                offUnsigned += cbZeros;
                pbBuf       += cbZeros;
                cbToRead    -= cbZeros;
            }

            /* Call the range callback to produce the requested data. */
            uint64_t const offInRange = offUnsigned - pRangeNode->Core.Key;
            uint64_t const cbMaxRead  = pRangeNode->Core.KeyLast + 1 - offUnsigned;
            cbDone = RT_MIN(cbToRead, cbMaxRead);
            Log3(("ISOMAKER: rtFsIsoMakerOutFile_Read:   off=%#RX64 -> %#RX64..%#RX64 -> pfnRead=%p(,,,%#RX64,,%#zx)\n",
                  offUnsigned, pRangeNode->Core.Key, pRangeNode->Core.KeyLast, pRangeNode->pfnRead, offInRange, cbDone));
            rc = pRangeNode->pfnRead(pThis, pIsoMaker, pRangeNode, offInRange, pbBuf, cbDone);
            if (RT_FAILURE(rc))
                break;
        }
        else if (offUnsigned < pIsoMaker->cbFinalizedImage)
        {
            Log3(("ISOMAKER: rtFsIsoMakerOutFile_Read:   off=%#RX64 -> Tail ZEROs\n", offUnsigned));
            uint64_t const cbZeros = pIsoMaker->cbFinalizedImage - offUnsigned;
            cbDone = RT_MIN(cbToRead, cbZeros);
            RT_BZERO(pbBuf, cbDone);
        }
        else
        {
            rc = pcbRead ? VINF_EOF : VERR_EOF;
            Log3(("ISOMAKER: rtFsIsoMakerOutFile_Read:   off=%#RX64 -> %Rrc & break\n", offUnsigned, rc));
            break;
        }

        /*
         * Advance.
         */
        cbRead      += cbDone;
        offUnsigned += cbDone;
        pbBuf       += cbDone;
        cbToRead    -= cbDone;
    }

    if (pcbRead)
        *pcbRead = cbRead;
    RTSgBufAdvance(pSgBuf, cbRead);
    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtFsIsoMakerOutFile_Flush(void *pvThis)
{
    RT_NOREF(pvThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtFsIsoMakerOutFile_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTFSISOMAKEROUTPUTFILE pThis = (PRTFSISOMAKEROUTPUTFILE)pvThis;
    *poffActual = pThis->offCurPos;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnSkip}
 */
static DECLCALLBACK(int) rtFsIsoMakerOutFile_Skip(void *pvThis, RTFOFF cb)
{
    RTFOFF offIgnored;
    return rtFsIsoMakerOutFile_Seek(pvThis, cb, RTFILE_SEEK_CURRENT, &offIgnored);
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSeek}
 */
static DECLCALLBACK(int) rtFsIsoMakerOutFile_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual)
{
    PRTFSISOMAKEROUTPUTFILE pThis = (PRTFSISOMAKEROUTPUTFILE)pvThis;

    /*
     * Seek relative to which position.
     */
    uint64_t offWrt;
    switch (uMethod)
    {
        case RTFILE_SEEK_BEGIN:
            offWrt = 0;
            break;

        case RTFILE_SEEK_CURRENT:
            offWrt = pThis->offCurPos;
            break;

        case RTFILE_SEEK_END:
            offWrt = pThis->pIsoMaker->cbFinalizedImage;
            break;

        default:
            return VERR_INVALID_PARAMETER;
    }

    /*
     * Calc new position, take care to stay within RTFOFF type bounds.
     */
    uint64_t offNew;
    if (offSeek == 0)
        offNew = offWrt;
    else if (offSeek > 0)
    {
        offNew = offWrt + offSeek;
        if (   offNew < offWrt
            || offNew > RTFOFF_MAX)
            offNew = RTFOFF_MAX;
    }
    else if ((uint64_t)-offSeek < offWrt)
        offNew = offWrt + offSeek;
    else
        offNew = 0;
    pThis->offCurPos = offNew;

    *poffActual = offNew;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQuerySize}
 */
static DECLCALLBACK(int) rtFsIsoMakerOutFile_QuerySize(void *pvThis, uint64_t *pcbFile)
{
    PRTFSISOMAKEROUTPUTFILE pThis = (PRTFSISOMAKEROUTPUTFILE)pvThis;
    *pcbFile = pThis->pIsoMaker->cbFinalizedImage;
    return VINF_SUCCESS;
}


/**
 * Standard file operations.
 */
DECL_HIDDEN_CONST(const RTVFSFILEOPS) g_rtFsIsoMakerOutputFileOps =
{
    { /* Stream */
        { /* Obj */
            RTVFSOBJOPS_VERSION,
            RTVFSOBJTYPE_FILE,
            "ISO Maker Output File",
            rtFsIsoMakerOutFile_Close,
            rtFsIsoMakerOutFile_QueryInfo,
            NULL,
            RTVFSOBJOPS_VERSION
        },
        RTVFSIOSTREAMOPS_VERSION,
        RTVFSIOSTREAMOPS_FEAT_NO_SG,
        rtFsIsoMakerOutFile_Read,
        NULL /*Write*/,
        rtFsIsoMakerOutFile_Flush,
        NULL /*PollOne*/,
        rtFsIsoMakerOutFile_Tell,
        rtFsIsoMakerOutFile_Skip,
        NULL /*ZeroFill*/,
        RTVFSIOSTREAMOPS_VERSION,
    },
    RTVFSFILEOPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSFILEOPS, ObjSet) - RT_UOFFSETOF(RTVFSFILEOPS, Stream.Obj),
        NULL /*SetMode*/,
        NULL /*SetTimes*/,
        NULL /*SetOwner*/,
        RTVFSOBJSETOPS_VERSION
    },
    rtFsIsoMakerOutFile_Seek,
    rtFsIsoMakerOutFile_QuerySize,
    NULL /*SetSize*/,
    NULL /*QueryMaxSize*/,
    RTVFSFILEOPS_VERSION
};



/**
 * Creates a VFS file for a finalized ISO maker instanced.
 *
 * The file can be used to access the image.  Both sequential and random access
 * are supported, so that this could in theory be hooked up to a CD/DVD-ROM
 * drive emulation and used as a virtual ISO image.
 *
 * @returns IRPT status code.
 * @param   hIsoMaker           The ISO maker handle.
 * @param   phVfsFile           Where to return the handle.
 */
RTDECL(int) RTFsIsoMakerCreateVfsOutputFile(RTFSISOMAKER hIsoMaker, PRTVFSFILE phVfsFile)
{
    PRTFSISOMAKERINT pThis = hIsoMaker;
    RTFSISOMAKER_ASSERT_VALID_HANDLE_RET(pThis);
    AssertReturn(pThis->fFinalized, VERR_WRONG_ORDER);
    AssertPtrReturn(phVfsFile, VERR_INVALID_POINTER);

    uint32_t cRefs = RTFsIsoMakerRetain(pThis);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    PRTFSISOMAKEROUTPUTFILE pFileData;
    RTVFSFILE               hVfsFile;
    int rc = RTVfsNewFile(&g_rtFsIsoMakerOutputFileOps, sizeof(*pFileData), RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_CREATE,
                          NIL_RTVFS, NIL_RTVFSLOCK, &hVfsFile, (void **)&pFileData);
    if (RT_SUCCESS(rc))
    {
        pFileData->pIsoMaker          = pThis;
        pFileData->offCurPos          = 0;
        pFileData->pSrcFile           = NULL;
        pFileData->hVfsSrcFile        = NIL_RTVFSFILE;
        pFileData->pDirHintPrimaryIso = NULL;
        pFileData->pDirHintJoliet     = NULL;
        pFileData->iChildPrimaryIso   = UINT32_MAX;
        pFileData->iChildJoliet       = UINT32_MAX;
        *phVfsFile = hVfsFile;
        return VINF_SUCCESS;
    }

    RTFsIsoMakerRelease(pThis);
    *phVfsFile = NIL_RTVFSFILE;
    return rc;
}


/*
 * Some notes on UDF layout.
 *
 * Example #1: en-us_windows_11_business_editions_version_24h2_x64_dvd_59a1851e.iso
 *
 * Sectors [16,22]: ISO-9660 descriptor sequence:
 *      - primary (type 1)
 *      - boot (type 0)
 *      - terminator (type 255)
 *      - BEA01
 *      - NSR02
 *      - TEA01
 *
 * Sectors [23,255]:
 *      ISO-9660 stuff. Readme file.
 *
 * Sector 256/0x100: UDF ADVP
 *
 * Sector [257/0x101,272/0x110]: UDF volume descriptor sequence:
 *      - Sector 257/0x101/0x80800 - primary volume descriptor.
 *      - Sector 258/0x102/0x81000 - logical volume descriptor (integrity seq 273, 2 sectors).
 *      - Sector 259/0x103/0x81800 - partition descriptor (+NSR02).
 *      - Sector 260/0x104/0x82000 - unallocated space descriptor.
 *      - Sector 261/0x105/0x82800 - implementation use descriptor.
 *      - Sector 262/0x106/0x83000 - terminating
 *      - Sectors [263/0x107/0x83800,272/0x110/0x887ff] - ZEROs
 *
 * Sectors [273/0x111/0x88800,274/0x112/0x897ff]: UDF volume integrity sequence:
 *      - Sector 273/0x111/0x88800 - logical volume integrity desc.
 *      - Sector 274/0x112/0x89000 - terminating
 *
 * Sector [275/0x113/0x89800,280/0x118/0x8c7ff]: UDF alternative volume descriptor sequence:
 *      - Sector 275/0x113/0x89800 - primary volume descriptor.
 *      - Sector 276/0x114/0x8a000 - logical volume descriptor (integrity seq 273, 2 sectors)
 *      - Sector 277/0x115/0x8a800 - partition descriptor (+NSR02).
 *      - Sector 278/0x116/0x8b000 - unallocated space descriptor.
 *      - Sector 279/0x117/0x8b800 - implementation use descriptor.
 *      - Sector 280/0x118/0x8c000 - terminating
 *      - Sectors [281/0x119/0x8c800,290/0x122/0x917ff] - ZEROs
 *
 * Sectors [291/0x123/0x91800,303/0x12f/0x97fff]: ZEROs
 *
 * Sectors [304/0x130/0x98000, 2794000/0x2aa210/0x1551087ff]: Partition 0x0bad - 0x2aa0e1 (2793697) sector
 *  - Sectors [304/0x130/0x98000, 305/0x131/0x987ff]: File set descriptor
 *  - Sectors [306/0x132/0x99000, 305/0x131/0x987ff]: Root Dir ICB (File entry)
 *  - ...
 *  - Sector 2794000/0x2aa210/0x155108000 (END-1): UDF ADVP
 *
 */
