/* $Id: ntfsvfs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - NTFS Virtual Filesystem, currently only for reading allocation bitmap.
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
#define LOG_GROUP RTLOGGROUP_FS
#include <iprt/fsvfs.h>

#include <iprt/asm-mem.h>
#include <iprt/asm.h>
#include <iprt/avl.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/vfs.h>
#include <iprt/vfslowlevel.h>
#include <iprt/utf16.h>
#include <iprt/formats/ntfs.h>

#include <internal/fs.h> /* For RTFSMODE_SYMLINK_REPARSE_TAG. */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The maximum bitmap size to try cache in its entirity (in bytes).  */
#define RTFSNTFS_MAX_WHOLE_BITMAP_CACHE     _64K
/** The maximum node cache size (in bytes).    */
#if ARCH_BITS >= 64
# define RTFSNTFS_MAX_CORE_CACHE_SIZE       _512K
#else
# define RTFSNTFS_MAX_CORE_CACHE_SIZE       _128K
#endif
/** The maximum node cache size (in bytes).    */
#if ARCH_BITS >= 64
# define RTFSNTFS_MAX_NODE_CACHE_SIZE       _1M
#else
# define RTFSNTFS_MAX_NODE_CACHE_SIZE       _256K
#endif

/** Makes a combined NTFS version value.
 * @see RTFSNTFSVOL::uNtfsVersion  */
#define RTFSNTFS_MAKE_VERSION(a_uMajor, a_uMinor)   RT_MAKE_U16(a_uMinor, a_uMajor)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to the instance data for a NTFS volume. */
typedef struct RTFSNTFSVOL *PRTFSNTFSVOL;
/** Pointer to a NTFS MFT record. */
typedef struct RTFSNTFSMFTREC *PRTFSNTFSMFTREC;
/** Poitner to a NTFS core object record.   */
typedef struct RTFSNTFSCORE *PRTFSNTFSCORE;
/** Pointer to an index node. */
typedef struct RTFSNTFSIDXNODE *PRTFSNTFSIDXNODE;
/** Pointer to a shared NTFS directory object. */
typedef struct RTFSNTFSDIRSHRD *PRTFSNTFSDIRSHRD;
/** Pointer to a shared NTFS file object. */
typedef struct RTFSNTFSFILESHRD *PRTFSNTFSFILESHRD;


/**
 * NTFS disk allocation extent (internal representation).
 */
typedef struct RTFSNTFSEXTENT
{
    /** The disk or partition byte offset.
     * This is set to UINT64_MAX for parts of sparse files that aren't recorded. */
    uint64_t            off;
    /** The size of the extent in bytes. */
    uint64_t            cbExtent;
} RTFSNTFSEXTENT;
/** Pointer to an NTFS 9660 extent. */
typedef RTFSNTFSEXTENT *PRTFSNTFSEXTENT;
/** Pointer to a const NTFS 9660 extent. */
typedef RTFSNTFSEXTENT const *PCRTFSNTFSEXTENT;

/**
 * An array of zero or more extents.
 */
typedef struct RTFSNTFSEXTENTS
{
    /** Number of bytes covered by the extents. */
    uint64_t            cbData;
    /** Number of allocation extents. */
    uint32_t            cExtents;
    /** Array of allocation extents. */
    PRTFSNTFSEXTENT     paExtents;
} RTFSNTFSEXTENTS;
/** Pointer to an extent array. */
typedef RTFSNTFSEXTENTS *PRTFSNTFSEXTENTS;
/** Pointer to a const extent array. */
typedef RTFSNTFSEXTENTS const *PCRTFSNTFSEXTENTS;


/**
 * NTFS MFT record.
 *
 * These are kept in a tree to , so
 */
typedef struct RTFSNTFSMFTREC
{
    /** MFT record number (index) as key. */
    AVLU64NODECORE      TreeNode;
    /** Pointer to the next MFT record if chained.  Holds a reference.  */
    PRTFSNTFSMFTREC     pNext;
    union
    {
        /** Generic record pointer.  RTFSNTFSVOL::cbMftRecord in size. */
        uint8_t        *pbRec;
        /** Pointer to the file record. */
        PNTFSRECFILE    pFileRec;
    } RT_UNION_NM(u);
    /** Pointer to the core object with the parsed data.
     * This is a weak reference.  Non-base MFT record all point to the base one. */
    PRTFSNTFSCORE       pCore;
    /** Reference counter. */
    uint32_t volatile   cRefs;
    /** Set if this is a base MFT record. */
    bool                fIsBase;
} RTFSNTFSMFTREC;


/** Pointer to a attribute subrecord structure. */
typedef struct RTFSNTFSATTRSUBREC *PRTFSNTFSATTRSUBREC;

/**
 * An attribute subrecord.
 *
 * This is for covering non-resident attributes that have had their allocation
 * list split.
 */
typedef struct RTFSNTFSATTRSUBREC
{
    /** Pointer to the next one. */
    PRTFSNTFSATTRSUBREC pNext;
    /** Pointer to the attribute header.
     * The MFT is held down by RTFSNTFSCORE via pMftEntry. */
    PNTFSATTRIBHDR      pAttrHdr;
    /** Disk space allocation if non-resident. */
    RTFSNTFSEXTENTS     Extents;
} RTFSNTFSATTRSUBREC;

/**
 * An attribute.
 */
typedef struct RTFSNTFSATTR
{
    /** List entry (head RTFSNTFSCORE::AttribHead). */
    RTLISTNODE          ListEntry;
    /** Pointer to the core object this attribute belongs to. */
    PRTFSNTFSCORE       pCore;
    /** Pointer to the attribute header.
     * The MFT is held down by RTFSNTFSCORE via pMftEntry. */
    PNTFSATTRIBHDR      pAttrHdr;
    /** The offset of the attribute header in the MFT record.
     * This is needed to validate header relative offsets. */
    uint32_t            offAttrHdrInMftRec;
    /** Number of resident bytes available (can be smaller than cbValue).
     *  Set to zero for non-resident attributes. */
    uint32_t            cbResident;
    /** The (uncompressed) attribute size.  */
    uint64_t            cbValue;
    /** Disk space allocation if non-resident. */
    RTFSNTFSEXTENTS     Extents;
    /** Pointer to any subrecords containing further allocation extents. */
    PRTFSNTFSATTRSUBREC pSubRecHead;
    /** Pointer to the VFS object for this attribute.
     * This is a weak reference since it's the VFS object that is referencing us. */
    union
    {
        /** Pointer to a shared directory (NTFS_AT_DIRECTORY). */
        PRTFSNTFSDIRSHRD    pSharedDir;
        /** Pointer to a shared file (NTFS_AT_DATA). */
        PRTFSNTFSFILESHRD   pSharedFile;
    } uObj;
} RTFSNTFSATTR;
/** Pointer to a attribute structure. */
typedef RTFSNTFSATTR *PRTFSNTFSATTR;


/**
 * NTFS file system object, shared part.
 */
typedef struct RTFSNTFSCORE
{
    /** Entry in either the RTFSNTFSVOL::CoreInUseHead or CoreUnusedHead.
     * Instances is moved to/from CoreUnusedHead as cRefs reaches zero and one
     * respectively. */
    RTLISTNODE          ListEntry;
    /** Reference counter.   */
    uint32_t volatile   cRefs;
    /** The estimated memory cost of this object. */
    uint32_t            cbCost;
    /** Pointer to the volume. */
    PRTFSNTFSVOL        pVol;
    /** Pointer to the head of the MFT record chain for this object.
     * Holds a reference.  */
    PRTFSNTFSMFTREC     pMftRec;
    /** List of attributes (RTFSNTFSATTR). */
    RTLISTANCHOR        AttribHead;
} RTFSNTFSCORE;


/**
 * Node lookup information for facilitating binary searching of node.
 */
typedef struct RTFSNTFSIDXNODEINFO
{
    /** The index header. */
    PCNTFSINDEXHDR      pIndexHdr;
    /** Number of entries. */
    uint32_t            cEntries;
    /** Set if internal node. */
    bool                fInternal;
    /** Array with pointers to the entries. */
    PCNTFSIDXENTRYHDR  *papEntries;
    /** Pointer to the index node this info is for, NULL if root node.
     * This is for reducing the enumeration stack entry size. */
    PRTFSNTFSIDXNODE    pNode;
    /** Pointer to the NTFS volume instace. */
    PRTFSNTFSVOL        pVol;
} RTFSNTFSIDXNODEINFO;
/** Pointer to index node lookup info. */
typedef RTFSNTFSIDXNODEINFO *PRTFSNTFSIDXNODEINFO;
/** Pointer to const index node lookup info. */
typedef RTFSNTFSIDXNODEINFO const *PCRTFSNTFSIDXNODEINFO;

/**
 * Index node, cached.
 *
 * These are cached to avoid reading, validating and parsing things each time a
 * subnode is accessed.
 */
typedef struct RTFSNTFSIDXNODE
{
    /** Entry in RTFSNTFSVOL::IdxNodeCahceRoot, key is disk byte offset. */
    AVLU64NODECORE          TreeNode;
    /** List entry on the unused list.  Gets removed from it when cRefs is
     * increase to one, and added when it reaches zero. */
    RTLISTNODE              UnusedListEntry;
    /** Reference counter. */
    uint32_t volatile       cRefs;
    /** The estimated memory cost of this node. */
    uint32_t                cbCost;
    /** Pointer to the node data. */
    PNTFSATINDEXALLOC       pNode;
    /** Node info. */
    RTFSNTFSIDXNODEINFO     NodeInfo;
} RTFSNTFSIDXNODE;

/**
 * Common index root structure.
 */
typedef struct RTFSNTFSIDXROOTINFO
{
    /** Pointer to the index root attribute value. */
    PCNTFSATINDEXROOT       pRoot;
    /** Pointer to the index allocation attribute, if present.
     * This and the bitmap may be absent if the whole directory fits into the
     * root index. */
    PRTFSNTFSATTR           pAlloc;
    /** End of the node addresses range (exclusive). */
    uint64_t                uEndNodeAddresses;
    /** Node address misalignement mask. */
    uint32_t                fNodeAddressMisalign;
    /** The byte shift count for node addresses. */
    uint8_t                 cNodeAddressByteShift;
    /** Node info for the root. */
    RTFSNTFSIDXNODEINFO     NodeInfo;
    /** Pointer to the index root attribute.  We reference the core thru this and
     *  use it to zero RTFSNTFSATTR::uObj::pSharedDir on destruction. */
    PRTFSNTFSATTR           pRootAttr;
} RTFSNTFSIDXROOTINFO;
/** Pointer to an index root structure. */
typedef RTFSNTFSIDXROOTINFO *PRTFSNTFSIDXROOTINFO;
/** Pointer to a const index root structure. */
typedef RTFSNTFSIDXROOTINFO const *PCRTFSNTFSIDXROOTINFO;

/**
 * Shared NTFS directory object.
 */
typedef struct RTFSNTFSDIRSHRD
{
    /** Reference counter.   */
    uint32_t volatile       cRefs;
    /** Index root information. */
    RTFSNTFSIDXROOTINFO     RootInfo;
} RTFSNTFSDIRSHRD;

/**
 * Index stack entry for index enumeration.
 */
typedef struct RTFSNTFSIDXSTACKENTRY
{
    /** The next entry to process in this stack entry. */
    uint32_t                iNext;
    /** Set if we need to descend first. */
    bool                    fDescend;
    /** Pointer to the node info for this entry. */
    PRTFSNTFSIDXNODEINFO    pNodeInfo;
} RTFSNTFSIDXSTACKENTRY;
/** Pointer to an index enumeration stack entry. */
typedef RTFSNTFSIDXSTACKENTRY *PRTFSNTFSIDXSTACKENTRY;


/**
 * Open directory instance.
 */
typedef struct RTFSNTFSDIR
{
    /** Pointer to the shared directory instance (referenced). */
    PRTFSNTFSDIRSHRD        pShared;
    /** Set if we've reached the end of the directory enumeration. */
    bool                    fNoMoreFiles;
    /** The enumeration stack size. */
    uint32_t                cEnumStackEntries;
    /** The allocated enumeration stack depth.    */
    uint32_t                cEnumStackMaxDepth;
    /** The numeration stack.  Allocated as needed. */
    PRTFSNTFSIDXSTACKENTRY  paEnumStack;
} RTFSNTFSDIR;
/** Pointer to an open directory instance. */
typedef RTFSNTFSDIR *PRTFSNTFSDIR;


/**
 * Shared NTFS file object.
 */
typedef struct RTFSNTFSFILESHRD
{
    /** Reference counter. */
    uint32_t volatile       cRefs;
    /** Pointer to the data attribute (core is referenced thru this). */
    PRTFSNTFSATTR           pData;
} RTFSNTFSFILESHRD;
/** Pointer to shared data for a file or data stream. */
typedef RTFSNTFSFILESHRD *PRTFSNTFSFILESHRD;


/**
 * Open NTFS file instance.
 */
typedef struct RTFSNTFSFILE
{
    /** Pointer to the shared file data (referenced). */
    PRTFSNTFSFILESHRD       pShared;
    /** Current file offset. */
    uint64_t                offFile;
} RTFSNTFSFILE;
/** Pointer to an NTFS open file instance. */
typedef RTFSNTFSFILE *PRTFSNTFSFILE;

/**
 * Instance data for an NTFS volume.
 */
typedef struct RTFSNTFSVOL
{
    /** Handle to itself. */
    RTVFS               hVfsSelf;
    /** The file, partition, or whatever backing the NTFS volume. */
    RTVFSFILE           hVfsBacking;
    /** The size of the backing thingy. */
    uint64_t            cbBacking;
    /** The formatted size of the volume. */
    uint64_t            cbVolume;
    /** cbVolume expressed as a cluster count. */
    uint64_t            cClusters;

    /** RTVFSMNT_F_XXX. */
    uint32_t            fMntFlags;
    /** RTFSNTVFS_F_XXX (currently none defined). */
    uint32_t            fNtfsFlags;

    /** The (logical) sector size. */
    uint32_t            cbSector;

    /** The (logical) cluster size. */
    uint32_t            cbCluster;
    /** Max cluster count value that won't overflow a signed 64-bit when
     * converted to bytes.  Inclusive. */
    uint64_t            iMaxVirtualCluster;
    /** The shift count for converting between bytes and clusters. */
    uint8_t             cClusterShift;

    /** Explicit padding. */
    uint8_t             abReserved[3];
    /** The NTFS version of the volume (RTFSNTFS_MAKE_VERSION). */
    uint16_t            uNtfsVersion;
    /** The NTFS_VOLUME_F_XXX. */
    uint16_t            fVolumeFlags;

    /** The logical cluster number of the MFT. */
    uint64_t            uLcnMft;
    /** The logical cluster number of the mirror MFT. */
    uint64_t            uLcnMftMirror;

    /** The MFT record size. */
    uint32_t            cbMftRecord;
    /** The default index (B-tree) node size. */
    uint32_t            cbDefaultIndexNode;

    /** The volume serial number. */
    uint64_t            uSerialNo;

    /** @name MFT record and core object cache.
     * @{ */
    /** The '$Mft' data attribute. */
    PRTFSNTFSATTR       pMftData;
    /** Root of the MFT record tree (RTFSNTFSMFTREC). */
    AVLU64TREE          MftRoot;
    /** List of in use core objects (RTFSNTFSCORE::cRefs > 0). (RTFSNTFSCORE) */
    RTLISTANCHOR        CoreInUseHead;
    /** List of unused core objects (RTFSNTFSCORE::cRefs == 0). (RTFSNTFSCORE)
     * The most recently used nodes are found at the of the list.  So, when
     * cbCoreObjects gets to high, we remove and destroy objects from the tail. */
    RTLISTANCHOR        CoreUnusedHead;
    /** Total core object memory cost (sum of all RTFSNTFSCORE::cbCost). */
    size_t              cbCoreObjects;
    /** @} */

    /** @name Allocation bitmap and cache.
     * @{ */
    /** The '$Bitmap' data attribute. */
    PRTFSNTFSATTR       pMftBitmap;
    /** The first cluster currently loaded into the bitmap cache . */
    uint64_t            iFirstBitmapCluster;
    /** The number of clusters currently loaded into the bitmap cache */
    uint32_t            cBitmapClusters;
    /** The size of the pvBitmap allocation. */
    uint32_t            cbBitmapAlloc;
    /** Allocation bitmap cache buffer. */
    void               *pvBitmap;
    /** @} */

    /** @name Directory/index related.
     * @{ */
    /** Tree of index nodes, index by disk byte offset. (RTFSNTFSIDXNODE) */
    AVLU64TREE          IdxNodeCacheRoot;
    /** List of currently unreferenced index nodes. (RTFSNTFSIDXNODE)
     * Most recently used nodes are found at the end of the list.  Nodes are added
     * when their reference counter reaches zero.  They are removed when it
     * increases to one again.
     *
     * The nodes are still in the index node cache tree (IdxNodeCacheRoot), but
     * we'll trim this from the end when we reach a certain size. */
    RTLISTANCHOR        IdxNodeUnusedHead;
    /** Number of unreferenced index nodes. */
    uint32_t            cUnusedIdxNodes;
    /** Number of cached index nodes. */
    uint32_t            cIdxNodes;
    /** Total index node memory cost. */
    size_t              cbIdxNodes;
    /** The root directory. */
    PRTFSNTFSDIRSHRD    pRootDir;
    /** Lower to uppercase conversion table for this filesystem.
     * This always has 64K valid entries. */
    PRTUTF16            pawcUpcase;
    /** @} */

} RTFSNTFSVOL;



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static uint32_t rtFsNtfsCore_Destroy(PRTFSNTFSCORE pThis);
static void     rtFsNtfsIdxVol_TrimCoreObjectCache(PRTFSNTFSVOL pThis);
static uint32_t rtFsNtfsCore_Release(PRTFSNTFSCORE pThis);
static uint32_t rtFsNtfsCore_Retain(PRTFSNTFSCORE pThis);
#ifdef LOG_ENABLED
static void     rtFsNtfsVol_LogIndexRoot(PCNTFSATINDEXROOT pIdxRoot, uint32_t cbIdxRoot);
#endif
static int      rtFsNtfsVol_NewDirFromShared(PRTFSNTFSVOL pThis, PRTFSNTFSDIRSHRD pSharedDir, PRTVFSDIR phVfsDir);
static uint32_t rtFsNtfsDirShrd_Retain(PRTFSNTFSDIRSHRD pThis);
static void     rtFsNtfsIdxVol_TrimIndexNodeCache(PRTFSNTFSVOL pThis);
static uint32_t rtFsNtfsIdxNode_Release(PRTFSNTFSIDXNODE pNode);
static uint32_t rtFsNtfsIdxNode_Retain(PRTFSNTFSIDXNODE pNode);



/**
 * Checks if a bit is set in an NTFS bitmap (little endian).
 *
 * @returns true if set, false if not.
 * @param   pvBitmap            The bitmap buffer.
 * @param   iBit                The bit.
 */
DECLINLINE(bool) rtFsNtfsBitmap_IsSet(void *pvBitmap, uint32_t iBit)
{
#if 0 //def RT_LITTLE_ENDIAN
    return ASMBitTest(pvBitmap, iBit);
#else
    uint8_t b = ((uint8_t const *)pvBitmap)[iBit >> 3];
    return RT_BOOL(b & (1 << (iBit & 7)));
#endif
}



static PRTFSNTFSMFTREC rtFsNtfsVol_NewMftRec(PRTFSNTFSVOL pVol, uint64_t idMft)
{
    PRTFSNTFSMFTREC pRec = (PRTFSNTFSMFTREC)RTMemAllocZ(sizeof(*pRec));
    if (pRec)
    {
        pRec->pbRec = (uint8_t *)RTMemAllocZ(pVol->cbMftRecord);
        if (pRec->pbRec)
        {
            pRec->TreeNode.Key = idMft;
            pRec->pNext        = NULL;
            pRec->cRefs        = 1;
            if (RTAvlU64Insert(&pVol->MftRoot, &pRec->TreeNode))
                return pRec;
            RTMemFree(pRec->pbRec);
        }

        RTMemFree(pRec);
    }
    return NULL;
}


static uint32_t rtFsNtfsMftRec_Destroy(PRTFSNTFSMFTREC pThis, PRTFSNTFSVOL pVol)
{
    RTMemFree(pThis->pbRec);
    pThis->pbRec = NULL;

    PAVLU64NODECORE pRemoved = RTAvlU64Remove(&pVol->MftRoot, pThis->TreeNode.Key);
    Assert(pRemoved == &pThis->TreeNode); NOREF(pRemoved);

    RTMemFree(pThis);

    return 0;
}


static uint32_t rtFsNtfsMftRec_Retain(PRTFSNTFSMFTREC pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs < 64);
    return cRefs;
}

static uint32_t rtFsNtfsMftRec_Release(PRTFSNTFSMFTREC pThis, PRTFSNTFSVOL pVol)
{
    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    Assert(cRefs < 64);
    if (cRefs != 0)
        return cRefs;
    return rtFsNtfsMftRec_Destroy(pThis, pVol);
}


#ifdef LOG_ENABLED
/**
 * Logs the MFT record
 *
 * @param   pRec            The MFT record to log.
 * @param   cbMftRecord     MFT record size (from RTFSNTFSVOL).
 */
static void rtfsNtfsMftRec_Log(PRTFSNTFSMFTREC pRec, uint32_t cbMftRecord)
{
    if (LogIs2Enabled())
    {
        PCNTFSRECFILE  pFileRec = pRec->pFileRec;
        Log2(("NTFS: MFT #%#RX64\n", pRec->TreeNode.Key));
        if (pFileRec->Hdr.uMagic == NTFSREC_MAGIC_FILE)
        {
            size_t const          cbRec = cbMftRecord;
            uint8_t const * const pbRec = pRec->pbRec;

            Log2(("NTFS: FILE record: \n"));
            Log2(("NTFS:   UpdateSeqArray  %#x L %#x\n", RT_LE2H_U16(pFileRec->Hdr.offUpdateSeqArray), RT_LE2H_U16(pFileRec->Hdr.cUpdateSeqEntries) ));
            Log2(("NTFS:   uLsn            %#RX64\n", RT_LE2H_U64(pFileRec->uLsn)));
            Log2(("NTFS:   uRecReuseSeqNo  %#RX16\n", RT_LE2H_U16(pFileRec->uRecReuseSeqNo)));
            Log2(("NTFS:   cLinks          %#RX16\n", RT_LE2H_U16(pFileRec->cLinks)));
            Log2(("NTFS:   offFirstAttrib  %#RX16\n", RT_LE2H_U16(pFileRec->offFirstAttrib)));
            Log2(("NTFS:   fFlags          %#RX16%s%s\n", RT_LE2H_U16(pFileRec->fFlags),
                  RT_LE2H_U16(pFileRec->fFlags) & NTFSRECFILE_F_IN_USE    ? " in-use"    : "",
                  RT_LE2H_U16(pFileRec->fFlags) & NTFSRECFILE_F_DIRECTORY ? " directory" : ""));
            Log2(("NTFS:   cbRecUsed       %#RX32\n", RT_LE2H_U32(pFileRec->cbRecUsed)));
            Log2(("NTFS:   BaseMftRec      %#RX64, sqn %#x\n",
                  NTFSMFTREF_GET_IDX(&pFileRec->BaseMftRec), NTFSMFTREF_GET_SEQ(&pFileRec->BaseMftRec)));
            Log2(("NTFS:   idNextAttrib    %#RX16\n", RT_LE2H_U16(pFileRec->idNextAttrib)));
            if (   RT_LE2H_U16(pFileRec->offFirstAttrib)         >= sizeof(*pFileRec)
                && (   RT_LE2H_U16(pFileRec->Hdr.offUpdateSeqArray) >= sizeof(*pFileRec)
                    || pFileRec->Hdr.offUpdateSeqArray == 0))
            {
                Log2(("NTFS:   uPaddingOrUsa   %#RX16\n", pFileRec->uPaddingOrUsa));
                Log2(("NTFS:   idxMftSelf      %#RX32\n", RT_LE2H_U32(pFileRec->idxMftSelf)));
            }

            uint32_t offRec = pFileRec->offFirstAttrib;
            size_t   cbRecUsed = RT_MIN(cbRec, pFileRec->cbRecUsed);
            while (offRec + NTFSATTRIBHDR_SIZE_RESIDENT <= cbRecUsed)
            {
                PCNTFSATTRIBHDR pHdr     = (PCNTFSATTRIBHDR)&pbRec[offRec];
                uint32_t const  cbAttrib = RT_LE2H_U32(pHdr->cbAttrib);
                Log2(("NTFS:   @%#05x: Attrib record: %#x LB %#x, instance #%#x, fFlags=%#RX16, %s\n", offRec,
                      RT_LE2H_U32(pHdr->uAttrType), cbAttrib, RT_LE2H_U16(pHdr->idAttrib), RT_LE2H_U16(pHdr->fFlags),
                      pHdr->fNonResident == 0 ? "resident" : pHdr->fNonResident == 1 ? "non-resident" : "bad-resident-flag"));
                if (pHdr->offName && pHdr->cwcName)
                {
                    if (offRec + RT_LE2H_U16(pHdr->offName) + pHdr->cwcName * sizeof(RTUTF16) <= cbRec)
                        Log2(("NTFS:     Name %.*ls\n", pHdr->cwcName,&pbRec[offRec + RT_LE2H_U16(pHdr->offName)]));
                    else
                        Log2(("NTFS:     Name <!out of bounds!> %#x L %#x\n", RT_LE2H_U16(pHdr->offName), pHdr->cwcName));
                }
                switch (pHdr->uAttrType)
                {
                    case NTFS_AT_UNUSED:                    Log2(("NTFS:     Type: UNUSED\n")); break;
                    case NTFS_AT_STANDARD_INFORMATION:      Log2(("NTFS:     Type: STANDARD_INFORMATION\n")); break;
                    case NTFS_AT_ATTRIBUTE_LIST:            Log2(("NTFS:     Type: ATTRIBUTE_LIST\n")); break;
                    case NTFS_AT_FILENAME:                  Log2(("NTFS:     Type: FILENAME\n")); break;
                    case NTFS_AT_OBJECT_ID:                 Log2(("NTFS:     Type: OBJECT_ID\n")); break;
                    case NTFS_AT_SECURITY_DESCRIPTOR:       Log2(("NTFS:     Type: SECURITY_DESCRIPTOR\n")); break;
                    case NTFS_AT_VOLUME_NAME:               Log2(("NTFS:     Type: VOLUME_NAME\n")); break;
                    case NTFS_AT_VOLUME_INFORMATION:        Log2(("NTFS:     Type: VOLUME_INFORMATION\n")); break;
                    case NTFS_AT_DATA:                      Log2(("NTFS:     Type: DATA\n")); break;
                    case NTFS_AT_INDEX_ROOT:                Log2(("NTFS:     Type: INDEX_ROOT\n")); break;
                    case NTFS_AT_INDEX_ALLOCATION:          Log2(("NTFS:     Type: INDEX_ALLOCATION\n")); break;
                    case NTFS_AT_BITMAP:                    Log2(("NTFS:     Type: BITMAP\n")); break;
                    case NTFS_AT_REPARSE_POINT:             Log2(("NTFS:     Type: REPARSE_POINT\n")); break;
                    case NTFS_AT_EA_INFORMATION:            Log2(("NTFS:     Type: EA_INFORMATION\n")); break;
                    case NTFS_AT_EA:                        Log2(("NTFS:     Type: EA\n")); break;
                    case NTFS_AT_PROPERTY_SET:              Log2(("NTFS:     Type: PROPERTY_SET\n")); break;
                    case NTFS_AT_LOGGED_UTILITY_STREAM:     Log2(("NTFS:     Type: LOGGED_UTILITY_STREAM\n")); break;
                    default:
                        if (RT_LE2H_U32(pHdr->uAttrType) >= RT_LE2H_U32_C(NTFS_AT_FIRST_USER_DEFINED))
                            Log2(("NTFS:     Type: unknown user defined - %#x!\n", RT_LE2H_U32(pHdr->uAttrType)));
                        else
                            Log2(("NTFS:     Type: unknown - %#x!\n", RT_LE2H_U32(pHdr->uAttrType)));
                        break;
                }

                size_t const cbMaxAttrib = cbRec - offRec;
                if (!pHdr->fNonResident)
                {
                    uint16_t const offValue = RT_LE2H_U16(pHdr->u.Res.offValue);
                    uint32_t const cbValue  = RT_LE2H_U32(pHdr->u.Res.cbValue);
                    Log2(("NTFS:     Value: %#x LB %#x, fFlags=%#x bReserved=%#x\n",
                          offValue, cbValue, pHdr->u.Res.fFlags, pHdr->u.Res.bReserved));
                    if (   offValue < cbMaxAttrib
                        && cbValue  < cbMaxAttrib
                        && offValue + cbValue <= cbMaxAttrib)
                    {
                        uint8_t const *pbValue = &pbRec[offRec + offValue];
                        RTTIMESPEC     Spec;
                        char           sz[80];
                        switch (pHdr->uAttrType)
                        {
                            case NTFS_AT_STANDARD_INFORMATION:
                            {
                                PCNTFSATSTDINFO pInfo = (PCNTFSATSTDINFO)pbValue;
                                if (cbValue >= NTFSATSTDINFO_SIZE_NTFS_V12)
                                {
                                    Log2(("NTFS:     iCreationTime      %#RX64 %s\n", RT_LE2H_U64(pInfo->iCreationTime),
                                          RTTimeSpecToString(RTTimeSpecSetNtTime(&Spec, RT_LE2H_U64(pInfo->iCreationTime)), sz, sizeof(sz)) ));
                                    Log2(("NTFS:     iLastDataModTime   %#RX64 %s\n", RT_LE2H_U64(pInfo->iLastDataModTime),
                                          RTTimeSpecToString(RTTimeSpecSetNtTime(&Spec, RT_LE2H_U64(pInfo->iLastDataModTime)), sz, sizeof(sz)) ));
                                    Log2(("NTFS:     iLastMftModTime    %#RX64 %s\n", RT_LE2H_U64(pInfo->iLastMftModTime),
                                          RTTimeSpecToString(RTTimeSpecSetNtTime(&Spec, RT_LE2H_U64(pInfo->iLastMftModTime)), sz, sizeof(sz)) ));
                                    Log2(("NTFS:     iLastAccessTime    %#RX64 %s\n", RT_LE2H_U64(pInfo->iLastAccessTime),
                                          RTTimeSpecToString(RTTimeSpecSetNtTime(&Spec, RT_LE2H_U64(pInfo->iLastAccessTime)), sz, sizeof(sz)) ));
                                    Log2(("NTFS:     fFileAttribs       %#RX32\n", RT_LE2H_U32(pInfo->fFileAttribs) ));
                                    Log2(("NTFS:     cMaxFileVersions   %#RX32\n", RT_LE2H_U32(pInfo->cMaxFileVersions) ));
                                    Log2(("NTFS:     uFileVersion       %#RX32\n", RT_LE2H_U32(pInfo->uFileVersion) ));
                                }
                                else
                                    Log2(("NTFS:     Error! cbValue=%#x is smaller than expected (%#x) for NTFSATSTDINFO!\n",
                                          cbValue, NTFSATSTDINFO_SIZE_NTFS_V12));
                                if (cbValue >= sizeof(*pInfo))
                                {
                                    Log2(("NTFS:     idClass            %#RX32\n", RT_LE2H_U32(pInfo->idClass) ));
                                    Log2(("NTFS:     idOwner            %#RX32\n", RT_LE2H_U32(pInfo->idOwner) ));
                                    Log2(("NTFS:     idSecurity         %#RX32\n", RT_LE2H_U32(pInfo->idSecurity) ));
                                    Log2(("NTFS:     cbQuotaChared      %#RX64\n", RT_LE2H_U64(pInfo->cbQuotaChared) ));
                                    Log2(("NTFS:     idxUpdateSequence  %#RX64\n", RT_LE2H_U64(pInfo->idxUpdateSequence) ));
                                }
                                if (cbValue > sizeof(*pInfo))
                                    Log2(("NTFS:     Undefined data: %.*Rhxs\n", cbValue - sizeof(*pInfo), &pbValue[sizeof(*pInfo)]));
                                break;
                            }

                            case NTFS_AT_ATTRIBUTE_LIST:
                            {
                                uint32_t iEntry   = 0;
                                uint32_t offEntry = 0;
                                while (offEntry + NTFSATLISTENTRY_SIZE_MINIMAL < cbValue)
                                {
                                    PCNTFSATLISTENTRY pInfo = (PCNTFSATLISTENTRY)&pbValue[offEntry];
                                    Log2(("NTFS:     attr[%u]: %#x in %#RX64 (sqn %#x), instance %#x, VNC=%#RX64-, name %#x L %#x\n",
                                          iEntry, RT_LE2H_U32(pInfo->uAttrType),  NTFSMFTREF_GET_IDX(&pInfo->InMftRec),
                                          NTFSMFTREF_GET_SEQ(&pInfo->InMftRec), RT_LE2H_U16(pInfo->idAttrib),
                                          RT_LE2H_U64(pInfo->iVcnFirst), pInfo->offName, pInfo->cwcName));
                                    if (   pInfo->cwcName > 0
                                        && pInfo->offName < pInfo->cbEntry)
                                        Log2(("NTFS:               name '%.*ls'\n", pInfo->cwcName, (uint8_t *)pInfo + pInfo->offName));

                                    /* next */
                                    if (pInfo->cbEntry < NTFSATLISTENTRY_SIZE_MINIMAL)
                                    {
                                        Log2(("NTFS:     cbEntry is too small! cbEntry=%#x, min %#x\n",
                                              pInfo->cbEntry, NTFSATLISTENTRY_SIZE_MINIMAL));
                                        break;
                                    }
                                    iEntry++;
                                    offEntry += RT_ALIGN_32(pInfo->cbEntry, 8);
                                }
                                break;
                            }

                            case NTFS_AT_FILENAME:
                            {
                                PCNTFSATFILENAME pInfo = (PCNTFSATFILENAME)pbValue;
                                if (cbValue >= RT_UOFFSETOF(NTFSATFILENAME, wszFilename))
                                {
                                    Log2(("NTFS:     ParentDirMftRec    %#RX64, sqn %#x\n",
                                          NTFSMFTREF_GET_IDX(&pInfo->ParentDirMftRec), NTFSMFTREF_GET_SEQ(&pInfo->ParentDirMftRec) ));
                                    Log2(("NTFS:     iCreationTime      %#RX64 %s\n", RT_LE2H_U64(pInfo->iCreationTime),
                                          RTTimeSpecToString(RTTimeSpecSetNtTime(&Spec, RT_LE2H_U64(pInfo->iCreationTime)), sz, sizeof(sz)) ));
                                    Log2(("NTFS:     iLastDataModTime   %#RX64 %s\n", RT_LE2H_U64(pInfo->iLastDataModTime),
                                          RTTimeSpecToString(RTTimeSpecSetNtTime(&Spec, RT_LE2H_U64(pInfo->iLastDataModTime)), sz, sizeof(sz)) ));
                                    Log2(("NTFS:     iLastMftModTime    %#RX64 %s\n", RT_LE2H_U64(pInfo->iLastMftModTime),
                                          RTTimeSpecToString(RTTimeSpecSetNtTime(&Spec, RT_LE2H_U64(pInfo->iLastMftModTime)), sz, sizeof(sz)) ));
                                    Log2(("NTFS:     iLastAccessTime    %#RX64 %s\n", RT_LE2H_U64(pInfo->iLastAccessTime),
                                          RTTimeSpecToString(RTTimeSpecSetNtTime(&Spec, RT_LE2H_U64(pInfo->iLastAccessTime)), sz, sizeof(sz)) ));
                                    Log2(("NTFS:     cbAllocated        %#RX64 (%Rhcb)\n",
                                          RT_LE2H_U64(pInfo->cbAllocated), RT_LE2H_U64(pInfo->cbAllocated)));
                                    Log2(("NTFS:     cbData             %#RX64 (%Rhcb)\n",
                                          RT_LE2H_U64(pInfo->cbData), RT_LE2H_U64(pInfo->cbData)));
                                    Log2(("NTFS:     fFileAttribs       %#RX32\n", RT_LE2H_U32(pInfo->fFileAttribs) ));
                                    if (RT_LE2H_U32(pInfo->fFileAttribs) & NTFS_FA_REPARSE_POINT)
                                        Log2(("NTFS:     uReparseTag        %#RX32\n", RT_LE2H_U32(pInfo->u.uReparseTag) ));
                                    else
                                        Log2(("NTFS:     cbPackedEas        %#RX16\n", RT_LE2H_U16(pInfo->u.cbPackedEas) ));
                                    Log2(("NTFS:     cwcFilename        %#x\n", pInfo->cwcFilename));
                                    Log2(("NTFS:     fFilenameType      %#x\n", pInfo->fFilenameType));
                                    if (RT_UOFFSETOF_DYN(NTFSATFILENAME, wszFilename[pInfo->cwcFilename]) <= cbValue)
                                        Log2(("NTFS:     wszFilename       '%.*ls'\n", pInfo->cwcFilename, pInfo->wszFilename ));
                                    else
                                        Log2(("NTFS:     Error! Truncated filename!!\n"));
                                }
                                else
                                    Log2(("NTFS:     Error! cbValue=%#x is smaller than expected (%#zx) for NTFSATFILENAME!\n",
                                          cbValue, RT_UOFFSETOF(NTFSATFILENAME, wszFilename) ));
                                break;
                            }

                            //case NTFS_AT_OBJECT_ID:
                            //case NTFS_AT_SECURITY_DESCRIPTOR:
                            //case NTFS_AT_VOLUME_NAME:
                            //case NTFS_AT_VOLUME_INFORMATION:
                            //case NTFS_AT_DATA:

                            case NTFS_AT_INDEX_ROOT:
                                rtFsNtfsVol_LogIndexRoot((PCNTFSATINDEXROOT)pbValue, cbValue);
                                break;

                            //case NTFS_AT_INDEX_ALLOCATION:
                            //case NTFS_AT_BITMAP:
                            //case NTFS_AT_REPARSE_POINT:
                            //case NTFS_AT_EA_INFORMATION:
                            //case NTFS_AT_EA:
                            //case NTFS_AT_PROPERTY_SET:
                            //case NTFS_AT_LOGGED_UTILITY_STREAM:

                            default:
                                if (cbValue <= 24)
                                    Log2(("NTFS:     %.*Rhxs\n", cbValue, pbValue));
                                else
                                    Log2(("%.*Rhxd\n", cbValue, pbValue));
                                break;
                        }

                    }
                    else
                        Log2(("NTFS:     !Value is out of bounds!\n"));
                }
                else if (RT_MAX(cbAttrib, NTFSATTRIBHDR_SIZE_NONRES_UNCOMPRESSED) <= cbMaxAttrib)
                {
                    Log2(("NTFS:     VNC range          %#RX64 .. %#RX64 (%#RX64 clusters)\n",
                          RT_LE2H_U64(pHdr->u.NonRes.iVcnFirst), RT_LE2H_U64(pHdr->u.NonRes.iVcnLast),
                          RT_LE2H_U64(pHdr->u.NonRes.iVcnLast) - RT_LE2H_U64(pHdr->u.NonRes.iVcnFirst) + 1));
                    Log2(("NTFS:     cbAllocated        %#RX64 (%Rhcb)\n",
                          RT_LE2H_U64(pHdr->u.NonRes.cbAllocated), RT_LE2H_U64(pHdr->u.NonRes.cbAllocated)));
                    Log2(("NTFS:     cbData             %#RX64 (%Rhcb)\n",
                          RT_LE2H_U64(pHdr->u.NonRes.cbData), RT_LE2H_U64(pHdr->u.NonRes.cbData)));
                    Log2(("NTFS:     cbInitialized      %#RX64 (%Rhcb)\n",
                          RT_LE2H_U64(pHdr->u.NonRes.cbInitialized), RT_LE2H_U64(pHdr->u.NonRes.cbInitialized)));
                    uint16_t const offMappingPairs = RT_LE2H_U16(pHdr->u.NonRes.offMappingPairs);
                    Log2(("NTFS:     offMappingPairs    %#RX16\n", offMappingPairs));
                    if (   pHdr->u.NonRes.abReserved[0] || pHdr->u.NonRes.abReserved[1]
                        || pHdr->u.NonRes.abReserved[2] || pHdr->u.NonRes.abReserved[3] || pHdr->u.NonRes.abReserved[4] )
                        Log2(("NTFS:     abReserved         %.7Rhxs\n", pHdr->u.NonRes.abReserved));
                    if (pHdr->u.NonRes.uCompressionUnit != 0)
                        Log2(("NTFS:     Compression unit   2^%u clusters\n", pHdr->u.NonRes.uCompressionUnit));

                    if (   cbMaxAttrib >= NTFSATTRIBHDR_SIZE_NONRES_COMPRESSED
                        && cbAttrib    >= NTFSATTRIBHDR_SIZE_NONRES_COMPRESSED
                        && (   offMappingPairs >= NTFSATTRIBHDR_SIZE_NONRES_COMPRESSED
                            || offMappingPairs <  NTFSATTRIBHDR_SIZE_NONRES_UNCOMPRESSED))
                        Log2(("NTFS:     cbCompressed       %#RX64 (%Rhcb)\n",
                              RT_LE2H_U64(pHdr->u.NonRes.cbCompressed), RT_LE2H_U64(pHdr->u.NonRes.cbCompressed)));
                    else if (   pHdr->u.NonRes.uCompressionUnit != 0
                             && pHdr->u.NonRes.uCompressionUnit != 64
                             && pHdr->u.NonRes.iVcnFirst == 0)
                        Log2(("NTFS:     !Error! Compressed attrib fields are out of bound!\n"));

                    if (   offMappingPairs < cbAttrib
                        && offMappingPairs >= NTFSATTRIBHDR_SIZE_NONRES_UNCOMPRESSED)
                    {
                        uint8_t const *pbPairs    = &pbRec[offRec + offMappingPairs];
                        uint32_t const cbMaxPairs = cbAttrib - offMappingPairs;
                        int64_t iVnc = pHdr->u.NonRes.iVcnFirst;
                        if (cbMaxPairs < 48)
                            Log2(("NTFS:     Mapping Pairs: cbMaxPairs=%#x %.*Rhxs\n", cbMaxPairs, cbMaxPairs, pbPairs));
                        else
                            Log2(("NTFS:     Mapping Pairs: cbMaxPairs=%#x\n%.*Rhxd\n", cbMaxPairs, cbMaxPairs, pbPairs));
                        if (!iVnc && !*pbPairs)
                            Log2(("NTFS:         [0]: Empty\n"));
                        else
                        {
                            if (iVnc != 0)
                                Log2(("NTFS:         [00/0x000]: VCN=%#012RX64 L %#012RX64 - not mapped\n", 0, iVnc));
                            int64_t  iLnc     = 0;
                            uint32_t iPair    = 0;
                            uint32_t offPairs = 0;
                            while (offPairs < cbMaxPairs)
                            {
                                /* First byte: 4-bit length of each of the pair values */
                                uint8_t const bLengths = pbPairs[offPairs];
                                if (!bLengths)
                                    break;
                                uint8_t const cbRun    = (bLengths & 0x0f) + (bLengths >> 4);
                                if (offPairs + cbRun > cbMaxPairs)
                                {
                                    Log2(("NTFS:         [%02d/%#05x]: run overrun! cbRun=%#x bLengths=%#x offPairs=%#x cbMaxPairs=%#x\n",
                                          iPair, offPairs, cbRun, bLengths, offPairs, cbMaxPairs));
                                    break;
                                }
                                //Log2(("NTFS:         @%#05x: %.*Rhxs\n", offPairs, cbRun + 1, &pbPairs[offPairs]));

                                /* Value 1: Number of (virtual) clusters in this run. */
                                int64_t cClustersInRun;
                                uint8_t cbNum = (bLengths & 0xf);
                                if (cbNum)
                                {
                                    uint8_t const *pbNum = &pbPairs[offPairs + cbNum]; /* last byte */
                                    cClustersInRun = (int8_t)*pbNum--;
                                    while (cbNum-- > 1)
                                        cClustersInRun = (cClustersInRun << 8) + *pbNum--;
                                }
                                else
                                    cClustersInRun = -1;

                                /* Value 2: The logical cluster delta to get to the first cluster in the run. */
                                cbNum = bLengths >> 4;
                                if (cbNum)
                                {
                                    uint8_t const *pbNum = &pbPairs[offPairs + cbNum + (bLengths & 0xf)]; /* last byte */
                                    int64_t cLcnDelta = (int8_t)*pbNum--;
                                    while (cbNum-- > 1)
                                        cLcnDelta = (cLcnDelta << 8) + *pbNum--;
                                    iLnc += cLcnDelta;
                                    Log2(("NTFS:         [%02d/%#05x]: VNC=%#012RX64 L %#012RX64 => LNC=%#012RX64\n",
                                          iPair, offPairs, iVnc, cClustersInRun, iLnc));
                                }
                                else
                                    Log2(("NTFS:         [%02d/%#05x]: VNC=%#012RX64 L %#012RX64 => HOLE\n",
                                          iPair, offPairs, iVnc, cClustersInRun));

                                /* Advance. */
                                iVnc     += cClustersInRun;
                                offPairs += 1 + cbRun;
                                iPair++;
                            }
                        }
                    }
                    else if (   cbAttrib != NTFSATTRIBHDR_SIZE_NONRES_COMPRESSED
                             && cbAttrib != NTFSATTRIBHDR_SIZE_NONRES_UNCOMPRESSED)
                    {
                        Log2(("NTFS:     Warning! Odd non-resident attribute size: %#x!\n", cbAttrib));
                        if (cbAttrib >= NTFSATTRIBHDR_SIZE_NONRES_UNCOMPRESSED)
                            Log2(("NTFS:     @%05x: %.*Rhxs!\n", offRec + NTFSATTRIBHDR_SIZE_NONRES_UNCOMPRESSED,
                                  cbAttrib - NTFSATTRIBHDR_SIZE_NONRES_UNCOMPRESSED,
                                  &pbRec[offRec + NTFSATTRIBHDR_SIZE_NONRES_UNCOMPRESSED]));
                    }
                }
                else
                    Log2(("NTFS:     !Attrib header is out of bound!\n"));

                /* Advance. */
                offRec += RT_MAX(cbAttrib, NTFSATTRIBHDR_SIZE_RESIDENT);
            }

            /* Anything left? */
            if (offRec < cbRecUsed)
                Log2(("NTFS:   @%#05x: Tail: %.*Rhxs\n", offRec, cbRecUsed - offRec, &pbRec[offRec]));
        }
        else
            Log2(("NTFS:   Unknown record type: %.4Rhxs\n", pFileRec));
    }
}
#endif /* LOG_ENABLED */


static int rtFsNtfsAttr_ParseExtents(PRTFSNTFSATTR pAttrib, PRTFSNTFSEXTENTS pExtents, uint8_t cClusterShift, int64_t iVcnFirst,
                                     uint64_t cbVolume, PRTERRINFO pErrInfo, uint64_t idxMft, uint32_t offAttrib)
{
    PCNTFSATTRIBHDR pAttrHdr = pAttrib->pAttrHdr;
    Assert(pAttrHdr->fNonResident);
    Assert(pExtents->cExtents  == 0);
    Assert(pExtents->paExtents == NULL);

    /** @todo Not entirely sure how to best detect empty mapping pair program.
     *        Not sure if this is a real problem as zero length stuff can be
     *        resident.  */
    uint16_t const offMappingPairs = RT_LE2H_U16(pAttrHdr->u.NonRes.offMappingPairs);
    uint32_t const cbAttrib        = RT_LE2H_U32(pAttrHdr->cbAttrib);
    if (   offMappingPairs != cbAttrib
        && offMappingPairs != 0)
    {
        if (pAttrHdr->u.NonRes.iVcnFirst < iVcnFirst)
            return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                           "Bad MFT record %#RX64: Attribute (@%#x) has a lower starting VNC than expected: %#RX64, %#RX64",
                                           idxMft, offAttrib, pAttrHdr->u.NonRes.iVcnFirst, iVcnFirst);

        if (   offMappingPairs >= cbAttrib
            || offMappingPairs < NTFSATTRIBHDR_SIZE_NONRES_UNCOMPRESSED)
            return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                           "Bad MFT record %#RX64: Mapping pair program for attribute (@%#x) is out of bounds: %#x, cbAttrib=%#x",
                                           idxMft, offAttrib, offMappingPairs, cbAttrib);

        /*
         * Count the pairs.
         */
        uint8_t const  * const  pbPairs  = (const uint8_t *)pAttrHdr + pAttrHdr->u.NonRes.offMappingPairs;
        uint32_t const          cbPairs  = cbAttrib - offMappingPairs;
        uint32_t                offPairs = 0;
        uint32_t                cPairs   = 0;
        while (offPairs < cbPairs)
        {
            uint8_t const bLengths = pbPairs[offPairs];
            if (bLengths)
            {
                uint8_t const cbRunField = bLengths & 0x0f;
                uint8_t const cbLcnField = bLengths >> 4;
                if (   cbRunField > 0
                    && cbRunField <= 8)
                {
                    if (cbLcnField <= 8)
                    {
                        cPairs++;

                        /* Advance and check for overflow/end. */
                        offPairs += 1 + cbRunField + cbLcnField;
                        if (offPairs <= cbAttrib)
                            continue;
                        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                       "Bad MFT record %#RX64: Mapping pair #%#x for attribute (@%#x) is out of bounds",
                                                       idxMft, cPairs - 1, offAttrib);
                    }
                    return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                   "Bad MFT record %#RX64: Mapping pair #%#x for attribute (@%#x): cbLcnField is out of bound: %u",
                                                   idxMft, cPairs - 1, offAttrib, cbLcnField);

                }
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "Bad MFT record %#RX64: Mapping pair #%#x for attribute (@%#x): cbRunField is out of bound: %u",
                                               idxMft, cPairs - 1, offAttrib, cbRunField);
            }
            break;
        }

        /*
         * Allocate an the extent table for them.
         */
        uint32_t const cExtents  = cPairs + (pAttrHdr->u.NonRes.iVcnFirst != iVcnFirst);
        if (cExtents)
        {
            PRTFSNTFSEXTENT paExtents = (PRTFSNTFSEXTENT)RTMemAllocZ(sizeof(paExtents[0]) * cExtents);
            AssertReturn(paExtents, VERR_NO_MEMORY);

            /*
             * Fill the table.
             */
            uint32_t iExtent = 0;

            /* A sparse hole between this and the previous extent table? */
            if (pAttrHdr->u.NonRes.iVcnFirst != iVcnFirst)
            {
                paExtents[iExtent].off      = UINT64_MAX;
                paExtents[iExtent].cbExtent = (pAttrHdr->u.NonRes.iVcnFirst - iVcnFirst) << cClusterShift;
                Log3(("   paExtent[%#04x]: %#018RX64 LB %#010RX64\n", iExtent, paExtents[iExtent].off, paExtents[iExtent].cbExtent));
                iExtent++;
            }

            /* Run the program again, now with values and without verbose error checking. */
            uint64_t cMaxClustersInRun = (INT64_MAX >> cClusterShift) - pAttrHdr->u.NonRes.iVcnFirst;
            uint64_t cbData            = 0;
            int64_t  iLcn              = 0;
            int      rc                = VINF_SUCCESS;
            offPairs = 0;
            for (; iExtent < cExtents; iExtent++)
            {
                uint8_t const bLengths = pbPairs[offPairs++];
                uint8_t const cbRunField = bLengths & 0x0f;
                uint8_t const cbLcnField = bLengths >> 4;
                AssertBreakStmt((unsigned)cbRunField - 1U <= 7U, rc = VERR_VFS_BOGUS_FORMAT);
                AssertBreakStmt((unsigned)cbLcnField      <= 8U, rc = VERR_VFS_BOGUS_FORMAT);

                AssertBreakStmt(!(pbPairs[offPairs + cbRunField - 1] & 0x80),
                                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                             "Bad MFT record %#RX64: Extent #%#x for attribute (@%#x): Negative runlength value",
                                                             idxMft, iExtent, offAttrib));
                uint64_t cClustersInRun = 0;
                switch (cbRunField)
                {
                    case 8: cClustersInRun |= (uint64_t)pbPairs[offPairs + 7] << 56; RT_FALL_THRU();
                    case 7: cClustersInRun |= (uint64_t)pbPairs[offPairs + 6] << 48; RT_FALL_THRU();
                    case 6: cClustersInRun |= (uint64_t)pbPairs[offPairs + 5] << 40; RT_FALL_THRU();
                    case 5: cClustersInRun |= (uint64_t)pbPairs[offPairs + 4] << 32; RT_FALL_THRU();
                    case 4: cClustersInRun |= (uint32_t)pbPairs[offPairs + 3] << 24; RT_FALL_THRU();
                    case 3: cClustersInRun |= (uint32_t)pbPairs[offPairs + 2] << 16; RT_FALL_THRU();
                    case 2: cClustersInRun |= (uint16_t)pbPairs[offPairs + 1] <<  8; RT_FALL_THRU();
                    case 1: cClustersInRun |= (uint16_t)pbPairs[offPairs + 0] <<  0;
                }
                offPairs += cbRunField;
                AssertBreakStmt(cClustersInRun <= cMaxClustersInRun,
                                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                             "Bad MFT record %#RX64: Extent #%#x for attribute (@%#x): too many clusters %#RX64, max %#RX64",
                                                             idxMft, iExtent, offAttrib, cClustersInRun, cMaxClustersInRun));
                cMaxClustersInRun          -= cClustersInRun;
                paExtents[iExtent].cbExtent = cClustersInRun << cClusterShift;
                cbData                     += cClustersInRun << cClusterShift;

                if (cbLcnField)
                {
                    unsigned offVncDelta = cbLcnField;
                    int64_t  cLncDelta   = (int8_t)pbPairs[--offVncDelta + offPairs];
                    while (offVncDelta-- > 0)
                        cLncDelta = (cLncDelta << 8) | pbPairs[offVncDelta + offPairs];
                    offPairs += cbLcnField;

                    iLcn += cLncDelta;
                    if (iLcn >= 0)
                    {
                        paExtents[iExtent].off = (uint64_t)iLcn << cClusterShift;
                        AssertBreakStmt((paExtents[iExtent].off >> cClusterShift) == (uint64_t)iLcn,
                                        rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                                     "Bad MFT record %#RX64: Extent #%#x for attribute (@%#x): iLcn %RX64 overflows when shifted by %u",
                                                                     idxMft, iExtent, offAttrib, iLcn, cClusterShift));
                        AssertBreakStmt(   paExtents[iExtent].off < cbVolume
                                        || paExtents[iExtent].cbExtent < cbVolume
                                        || paExtents[iExtent].off + paExtents[iExtent].cbExtent <= cbVolume,
                                        rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                                     "Bad MFT record %#RX64: Extent #%#x for attribute (@%#x) outside volume: %#RX64 LB %#RX64, cbVolume=%#RX64",
                                                                     idxMft, iExtent, offAttrib, paExtents[iExtent].off,
                                                                     paExtents[iExtent].cbExtent, cbVolume));
                    }
                    else
                        paExtents[iExtent].off = UINT64_MAX;
                }
                else
                    paExtents[iExtent].off = UINT64_MAX;
                Log3(("   paExtent[%#04x]: %#018RX64 LB %#010RX64\n", iExtent, paExtents[iExtent].off, paExtents[iExtent].cbExtent));
            }

            /* Commit if everything went fine? */
            if (RT_SUCCESS(rc))
            {
                pExtents->cbData    = cbData;
                pExtents->cExtents  = cExtents;
                pExtents->paExtents = paExtents;
            }
            else
            {
                RTMemFree(paExtents);
                return rc;
            }
        }
    }
    return VINF_SUCCESS;
}


/**
 * Parses the given MTF record and all related records, putting the result in
 * pRec->pCore (with one reference for the caller).
 *
 * ASSUMES caller will insert pRec->pCore into the CoreInUseHead list on
 * success, and destroy it on failure.  It is better to have caller do the
 * inserting/destroy, since we don't want to cache a failed parsing attempt.
 * (It is also preferable to add RTFSNTFSCORE::cbCost once it's fully calculated
 * and in the place as the insertion.)
 *
 * @returns IPRT status code.
 * @param   pThis       The volume.
 * @param   pRec        The MFT record to parse.
 * @param   pErrInfo    Where to return additional error information.  Optional.
 */
static int rtFsNtfsVol_ParseMft(PRTFSNTFSVOL pThis, PRTFSNTFSMFTREC pRec, PRTERRINFO pErrInfo)
{
    AssertReturn(!pRec->pCore, VERR_INTERNAL_ERROR_4);

    /*
     * Check that it is a file record and that its base MFT record number is zero.
     * Caller should do the base record resolving.
     */
    PNTFSRECFILE pFileRec = pRec->pFileRec;
    if (pFileRec->Hdr.uMagic != NTFSREC_MAGIC_FILE)
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "Bad MFT record %#RX64: Not a FILE entry (%.4Rhxs)",
                                       pRec->TreeNode.Key, &pFileRec->Hdr);
    if (pFileRec->BaseMftRec.u64 != 0)
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "Bad MFT record %#RX64: Not a base record (%#RX64, sqn %#x)",
                                       pRec->TreeNode.Key, NTFSMFTREF_GET_IDX(&pFileRec->BaseMftRec),
                                       NTFSMFTREF_GET_SEQ(&pFileRec->BaseMftRec) );

     /*
      * Create a core node (1 reference, returned even on error).
      */
    PRTFSNTFSCORE pCore = (PRTFSNTFSCORE)RTMemAllocZ(sizeof(*pCore));
    AssertReturn(pCore, VERR_NO_MEMORY);

    pCore->cRefs    = 1;
    pCore->cbCost   = pThis->cbMftRecord + sizeof(*pCore);
    pCore->pVol     = pThis;
    RTListInit(&pCore->AttribHead);
    pCore->pMftRec  = pRec;
    rtFsNtfsMftRec_Retain(pRec);
    pRec->pCore     = pCore;

    /*
     * Parse attributes.
     * We process any attribute list afterwards, skipping attributes in this MFT record.
     */
    PRTFSNTFSATTR       pAttrList = NULL;
    uint8_t * const     pbRec     = pRec->pbRec;
    uint32_t            offRec    = pFileRec->offFirstAttrib;
    uint32_t const      cbRecUsed = RT_MIN(pThis->cbMftRecord, pFileRec->cbRecUsed);
    while (offRec + NTFSATTRIBHDR_SIZE_RESIDENT <= cbRecUsed)
    {
        PNTFSATTRIBHDR  pAttrHdr  = (PNTFSATTRIBHDR)&pbRec[offRec];

        /*
         * Validate the attribute data.
         */
        uint32_t const  cbAttrib  = RT_LE2H_U32(pAttrHdr->cbAttrib);
        uint32_t const  cbMin     = !pAttrHdr->fNonResident                  ? NTFSATTRIBHDR_SIZE_RESIDENT
                                  : pAttrHdr->u.NonRes.uCompressionUnit == 0 ? NTFSATTRIBHDR_SIZE_NONRES_UNCOMPRESSED
                                  :                                            NTFSATTRIBHDR_SIZE_NONRES_COMPRESSED;
        if (cbAttrib < cbMin)
            return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                           "Bad MFT record %#RX64: Attribute (@%#x) is too small (%#x, cbMin=%#x)",
                                           pRec->TreeNode.Key, offRec, cbAttrib, cbMin);
        if (offRec + cbAttrib > cbRecUsed)
            return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                           "Bad MFT record %#RX64: Attribute (@%#x) is too long (%#x, cbRecUsed=%#x)",
                                           pRec->TreeNode.Key, offRec, cbAttrib, cbRecUsed);
        if (cbAttrib & 0x7)
            return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                           "Bad MFT record %#RX64: Attribute (@%#x) size is misaligned: %#x",
                                           pRec->TreeNode.Key, offRec, cbAttrib);
        if (pAttrHdr->fNonResident)
        {
            int64_t const cbAllocated = RT_LE2H_U64(pAttrHdr->u.NonRes.cbAllocated);
            if (cbAllocated < 0)
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "Bad MFT record %#RX64: Attribute (@%#x): cbAllocated (%#RX64) is negative",
                                               pRec->TreeNode.Key, offRec, cbAllocated);
            if ((uint64_t)cbAllocated & (pThis->cbCluster - 1))
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "Bad MFT record %#RX64: Attribute (@%#x): cbAllocated (%#RX64) isn't cluster aligned (cbCluster=%#x)",
                                               pRec->TreeNode.Key, offRec, cbAllocated, pThis->cbCluster);

            int64_t const cbData = RT_LE2H_U64(pAttrHdr->u.NonRes.cbData);
            if (cbData < 0)
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "Bad MFT record %#RX64: Attribute (@%#x): cbData (%#RX64) is negative",
                                               pRec->TreeNode.Key, offRec, cbData);

            int64_t const cbInitialized = RT_LE2H_U64(pAttrHdr->u.NonRes.cbInitialized);
            if (cbInitialized < 0)
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "Bad MFT record %#RX64: Attribute (@%#x): cbInitialized (%#RX64) is negative",
                                               pRec->TreeNode.Key, offRec, cbInitialized);

            int64_t const iVcnFirst = RT_LE2H_U64(pAttrHdr->u.NonRes.iVcnFirst);
            int64_t const iVcnLast  = RT_LE2H_U64(pAttrHdr->u.NonRes.iVcnLast);
            if (   iVcnFirst > iVcnLast
                && (   iVcnLast != -1
                    || cbAllocated != 0))
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "Bad MFT record %#RX64: Attribute (@%#x): iVcnFirst (%#RX64) is higher than iVcnLast (%#RX64)",
                                               pRec->TreeNode.Key, offRec, iVcnFirst, iVcnLast);
            if (iVcnFirst < 0)
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "Bad MFT record %#RX64: Attribute (@%#x): iVcnFirst (%#RX64) is negative",
                                               pRec->TreeNode.Key, offRec, iVcnFirst);
            if ((uint64_t)iVcnLast > pThis->iMaxVirtualCluster)
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "Bad MFT record %#RX64: Attribute (@%#x): iVcnLast (%#RX64) is too high, max %RX64 (shift %#x)",
                                               pRec->TreeNode.Key, offRec, iVcnLast, pThis->cClusterShift, pThis->iMaxVirtualCluster);
            uint16_t const offMappingPairs = RT_LE2H_U16(pAttrHdr->u.NonRes.offMappingPairs);
            if (   (offMappingPairs != 0 && offMappingPairs < cbMin)
                || offMappingPairs > cbAttrib)
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "Bad MFT record %#RX64: Attribute (@%#x): offMappingPairs (%#x) is out of bounds (cbAttrib=%#x, cbMin=%#x)",
                                               pRec->TreeNode.Key, offRec, offMappingPairs, cbAttrib, cbMin);
            if (pAttrHdr->u.NonRes.uCompressionUnit > 16)
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "Bad MFT record %#RX64: Attribute (@%#x): uCompressionUnit (%#x) is too high",
                                               pRec->TreeNode.Key, offRec, pAttrHdr->u.NonRes.uCompressionUnit);

            if (cbMin >= NTFSATTRIBHDR_SIZE_NONRES_COMPRESSED)
            {
                int64_t const cbCompressed = RT_LE2H_U64(pAttrHdr->u.NonRes.cbCompressed);
                if (cbAllocated < 0)
                    return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                   "Bad MFT record %#RX64: Attribute (@%#x): cbCompressed (%#RX64) is negative",
                                                   pRec->TreeNode.Key, offRec, cbCompressed);
            }
        }
        else
        {
            uint16_t const offValue = RT_LE2H_U32(pAttrHdr->u.Res.offValue);
            if (   offValue > cbAttrib
                || offValue < NTFSATTRIBHDR_SIZE_RESIDENT)
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "Bad MFT record %#RX64: Attribute (@%#x): offValue (%#RX16) is out of bounds (cbAttrib=%#RX32, cbValue=%#RX32)",
                                               pRec->TreeNode.Key, offRec, offValue, cbAttrib, RT_LE2H_U32(pAttrHdr->u.Res.cbValue));
            if ((pAttrHdr->fFlags & NTFS_AF_COMPR_FMT_MASK) != NTFS_AF_COMPR_FMT_NONE)
            {
#if 1 /* Seen on INDEX_ROOT of ReportQueue on w7, so turned into debug log warning. */
                Log(("NTFS: Warning! Bad MFT record %#RX64: Attribute (@%#x): fFlags (%#RX16) indicate compression of a resident attribute\n",
                     pRec->TreeNode.Key, offRec, RT_LE2H_U16(pAttrHdr->fFlags) ));
#else
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "Bad MFT record %#RX64: Attribute (@%#x): fFlags (%#RX16) indicate compression of a resident attribute",
                                               pRec->TreeNode.Key, offRec, RT_LE2H_U16(pAttrHdr->fFlags));
#endif
            }
        }

        if (pAttrHdr->cwcName != 0)
        {
            uint16_t offName = RT_LE2H_U16(pAttrHdr->offName);
            if (   offName < cbMin
                || offName >= cbAttrib)
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "Bad MFT record %#RX64: Attribute (@%#x): offName (%#RX16) is out of bounds (cbAttrib=%#RX32, cbMin=%#RX32)",
                                               pRec->TreeNode.Key, offRec, offName, cbAttrib, cbMin);
            if (offName + pAttrHdr->cwcName * sizeof(RTUTF16) > cbAttrib)
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "Bad MFT record %#RX64: Attribute (@%#x): offName (%#RX16) + cwcName (%#x) is out of bounds (cbAttrib=%#RX32)",
                                               pRec->TreeNode.Key, offRec, offName, pAttrHdr->cwcName, cbAttrib);
        }

        /*
         * Allocate and initialize a new attribute.
         */
        PRTFSNTFSATTR pAttrib = (PRTFSNTFSATTR)RTMemAllocZ(sizeof(*pAttrib));
        AssertReturn(pAttrib, VERR_NO_MEMORY);
        pAttrib->pAttrHdr           = pAttrHdr;
        pAttrib->offAttrHdrInMftRec = offRec;
        pAttrib->pCore              = pCore;
        //pAttrib->cbResident         = 0;
        //pAttrib->cbValue            = 0;
        //pAttrib->Extents.cExtents   = 0;
        //pAttrib->Extents.paExtents  = NULL;
        //pAttrib->pSubRecHead        = NULL;
        if (pAttrHdr->fNonResident)
        {
            pAttrib->cbValue        = RT_LE2H_U64(pAttrHdr->u.NonRes.cbData);
            int rc = rtFsNtfsAttr_ParseExtents(pAttrib, &pAttrib->Extents, pThis->cClusterShift, 0 /*iVncFirst*/,
                                               pThis->cbVolume, pErrInfo, pRec->TreeNode.Key, offRec);
            if (RT_FAILURE(rc))
            {
                RTMemFree(pAttrib);
                return rc;
            }
        }
        else
        {
            pAttrib->cbValue        = RT_LE2H_U32(pAttrHdr->u.Res.cbValue);
            if (   (uint32_t)pAttrib->cbValue > 0
                && RT_LE2H_U16(pAttrHdr->u.Res.offValue) < cbAttrib)
            {
                pAttrib->cbResident = cbAttrib - RT_LE2H_U16(pAttrHdr->u.Res.offValue);
                if (pAttrib->cbResident > (uint32_t)pAttrib->cbValue)
                    pAttrib->cbResident = (uint32_t)pAttrib->cbValue;
            }
        }

        RTListAppend(&pCore->AttribHead, &pAttrib->ListEntry);

        if (pAttrHdr->uAttrType == NTFS_AT_ATTRIBUTE_LIST)
            pAttrList = pAttrib;

        /* Advance. */
        offRec += cbAttrib;
    }

    /*
     * Process any attribute list.
     */
    if (pAttrList)
    {
        /** @todo    */
    }

    return VINF_SUCCESS;
}


/**
 * Translates a attribute value offset to a disk offset.
 *
 * @returns Disk offset, UINT64_MAX if not translatable for some reason.
 * @param   pAttr           The
 * @param   off             The offset to translate.
 * @param   pcbValid        Where to return the run length at the return offset.
 *                          Optional.
 */
static uint64_t rtFsNtfsAttr_OffsetToDisk(PRTFSNTFSATTR pAttr, uint64_t off, uint64_t *pcbValid)
{
    /*
     * Searching the extend list is a tad complicated since it starts in one
     * structure and continues in a different one.  But whatever.
     */
    PRTFSNTFSEXTENTS    pTable   = &pAttr->Extents;
    PRTFSNTFSATTRSUBREC pCurSub  = NULL;
    for (;;)
    {
        if (off < pTable->cbData)
        {
            uint32_t iExtent  = 0;
            while (   iExtent < pTable->cExtents
                   && off >= pTable->paExtents[iExtent].cbExtent)
            {
                off -= pTable->paExtents[iExtent].cbExtent;
                iExtent++;
            }
            AssertReturn(iExtent < pTable->cExtents, UINT64_MAX);
            if (pcbValid)
                *pcbValid = pTable->paExtents[iExtent].cbExtent - off;
            return pTable->paExtents[iExtent].off != UINT64_MAX ? pTable->paExtents[iExtent].off + off : UINT64_MAX;
        }

        /* Next table. */
        off -= pTable->cbData;
        if (!pCurSub)
            pCurSub = pAttr->pSubRecHead;
        else
            pCurSub = pCurSub->pNext;
        if (!pCurSub)
        {
            if (pcbValid)
                *pcbValid = 0;
            return UINT64_MAX;
        }
        pTable = &pCurSub->Extents;
    }
    /* not reached */
}


static int rtFsNtfsAttr_Read(PRTFSNTFSATTR pAttr, uint64_t off, void *pvBuf, size_t cbToRead)
{
    PRTFSNTFSVOL pVol = pAttr->pCore->pVol;
    int          rc;
    if (!pAttr->pAttrHdr->fNonResident)
    {
        /*
         * The attribute is resident.
         */
        uint32_t cbAttrib = RT_LE2H_U32(pAttr->pAttrHdr->cbAttrib);
        uint32_t cbValue  = RT_LE2H_U32(pAttr->pAttrHdr->u.Res.cbValue);
        uint16_t offValue = RT_LE2H_U16(pAttr->pAttrHdr->u.Res.offValue);
        if (   off            <  cbValue
            && cbToRead       <= cbValue
            && off + cbToRead <= cbValue)
        {
            if (offValue <= cbAttrib)
            {
                cbAttrib -= offValue;
                if (off < cbAttrib)
                {
                    /** @todo check if its possible to have cbValue larger than the attribute and
                     *        reading those extra bytes as zero. */
                    if (   pAttr->offAttrHdrInMftRec + offValue + cbAttrib <= pVol->cbMftRecord
                        && cbAttrib <= pVol->cbMftRecord)
                    {
                        size_t cbToCopy = cbAttrib - off;
                        if (cbToCopy > cbToRead)
                            cbToCopy = cbToRead;
                        memcpy(pvBuf, (uint8_t *)pAttr->pAttrHdr + offValue, cbToCopy);
                        pvBuf     = (uint8_t *)pvBuf + cbToCopy;
                        cbToRead -= cbToCopy;
                        rc = VINF_SUCCESS;
                    }
                    else
                    {
                        rc = VERR_VFS_BOGUS_OFFSET;
                        Log(("rtFsNtfsAttr_Read: bad resident attribute!\n"));
                    }
                }
                else
                    rc = VINF_SUCCESS;
            }
            else
                rc = VERR_VFS_BOGUS_FORMAT;
        }
        else
            rc = VERR_EOF;
    }
    else if (pAttr->pAttrHdr->u.NonRes.uCompressionUnit == 0)
    {
        /*
         * Uncompressed non-resident attribute.
         */
        uint64_t const cbAllocated   = RT_LE2H_U64(pAttr->pAttrHdr->u.NonRes.cbAllocated);
        if (   off >= cbAllocated
            || cbToRead > cbAllocated
            || off + cbToRead > cbAllocated)
            rc = VERR_EOF;
        else
        {
            rc = VINF_SUCCESS;

            uint64_t const cbInitialized = RT_LE2H_U64(pAttr->pAttrHdr->u.NonRes.cbInitialized);
            if (   off < cbInitialized
                && cbToRead > 0)
            {
                /*
                 * Locate the first extent.  This is a tad complicated.
                 *
                 * We move off along as we traverse the extent tables, so that it is relative
                 * to the start of the current extent.
                 */
                PRTFSNTFSEXTENTS    pTable   = &pAttr->Extents;
                uint32_t            iExtent  = 0;
                PRTFSNTFSATTRSUBREC pCurSub  = NULL;
                for (;;)
                {
                    if (off < pTable->cbData)
                    {
                        while (   iExtent < pTable->cExtents
                               && off >= pTable->paExtents[iExtent].cbExtent)
                        {
                            off -= pTable->paExtents[iExtent].cbExtent;
                            iExtent++;
                        }
                        AssertReturn(iExtent < pTable->cExtents, VERR_INTERNAL_ERROR_2);
                        break;
                    }

                    /* Next table. */
                    off -= pTable->cbData;
                    if (!pCurSub)
                        pCurSub = pAttr->pSubRecHead;
                    else
                        pCurSub = pCurSub->pNext;
                    if (!pCurSub)
                    {
                        iExtent = UINT32_MAX;
                        break;
                    }
                    pTable  = &pCurSub->Extents;
                    iExtent = 0;
                }

                /*
                 * The read loop.
                 */
                while (iExtent != UINT32_MAX)
                {
                    uint64_t cbMaxRead = pTable->paExtents[iExtent].cbExtent;
                    Assert(off < cbMaxRead);
                    cbMaxRead -= off;
                    size_t const cbThisRead = cbMaxRead >= cbToRead ? cbToRead : (size_t)cbMaxRead;
                    if (pTable->paExtents[iExtent].off == UINT64_MAX)
                        RT_BZERO(pvBuf, cbThisRead);
                    else
                    {
                        rc = RTVfsFileReadAt(pVol->hVfsBacking, pTable->paExtents[iExtent].off + off, pvBuf, cbThisRead, NULL);
                        Log4(("NTFS: Volume read: @%#RX64 LB %#zx -> %Rrc\n", pTable->paExtents[iExtent].off + off, cbThisRead, rc));
                        if (RT_FAILURE(rc))
                            break;
                    }
                    pvBuf     = (uint8_t *)pvBuf + cbThisRead;
                    cbToRead -= cbThisRead;
                    if (!cbToRead)
                        break;
                    off = 0;

                    /*
                     * Advance to the next extent.
                     */
                    iExtent++;
                    if (iExtent >= pTable->cExtents)
                    {
                        pCurSub = pCurSub ? pCurSub->pNext : pAttr->pSubRecHead;
                        if (!pCurSub)
                            break;
                        pTable  = &pCurSub->Extents;
                        iExtent = 0;
                    }
                }
            }
        }
    }
    else
    {
        LogRel(("rtFsNtfsAttr_Read: Compressed files are not supported\n"));
        rc = VERR_NOT_SUPPORTED;
    }

    /*
     * Anything else beyond the end of what's stored/initialized?
     */
    if (   cbToRead > 0
        && RT_SUCCESS(rc))
    {
        RT_BZERO(pvBuf, cbToRead);
    }

    return rc;
}


/**
 *
 * @note    Only modifying non-resident data is currently supported.  No
 *          shrinking or growing.  Metadata is not modified.
 */
static int rtFsNtfsAttr_Write(PRTFSNTFSATTR pAttr, uint64_t off, void const *pvBuf, size_t cbToWrite)
{
    PRTFSNTFSVOL pVol = pAttr->pCore->pVol;
    int          rc;
    if (!pAttr->pAttrHdr->fNonResident)
    {
        /*
         * The attribute is resident.  Currently not supported.
         */
#if 0
        uint32_t cbAttrib = RT_LE2H_U32(pAttr->pAttrHdr->cbAttrib);
        uint32_t cbValue  = RT_LE2H_U32(pAttr->pAttrHdr->u.Res.cbValue);
        uint16_t offValue = RT_LE2H_U16(pAttr->pAttrHdr->u.Res.offValue);
        if (   off             <  cbValue
            && cbToWrite       <= cbValue
            && off + cbToWrite <= cbValue)
        {
            if (offValue <= cbAttrib)
            {
                cbAttrib -= offValue;
                if (off < cbAttrib)
                {
                    /** @todo check if its possible to have cbValue larger than the attribute and
                     *        reading those extra bytes as zero. */
                    if (   pAttr->offAttrHdrInMftRec + offValue + cbAttrib <= pVol->cbMftRecord
                        && cbAttrib <= pVol->cbMftRecord)
                    {
                        size_t cbToCopy = cbAttrib - off;
                        if (cbToCopy > cbToWrite)
                            cbToCopy = cbToWrite;
                        memcpy(pvBuf, (uint8_t *)pAttr->pAttrHdr + offValue, cbToCopy);
                        pvBuf      = (uint8_t *)pvBuf + cbToCopy;
                        cbToWrite -= cbToCopy;
                        rc = VINF_SUCCESS;
                    }
                    else
                    {
                        rc = VERR_VFS_BOGUS_OFFSET;
                        Log(("rtFsNtfsAttr_Write: bad resident attribute!\n"));
                    }
                }
                else
                    rc = VINF_SUCCESS;
            }
            else
                rc = VERR_VFS_BOGUS_FORMAT;
        }
        else
            rc = VERR_EOF;
#else
        LogRel(("rtFsNtfsAttr_Write: file too small to write to.\n"));
        rc = VERR_INTERNAL_ERROR_3;
#endif
    }
    else if (pAttr->pAttrHdr->u.NonRes.uCompressionUnit == 0)
    {
        /*
         * Uncompressed non-resident attribute.
         * Note! We currently
         */
        uint64_t const cbAllocated   = RT_LE2H_U64(pAttr->pAttrHdr->u.NonRes.cbAllocated);
        if (   off >= cbAllocated
            || cbToWrite > cbAllocated
            || off + cbToWrite > cbAllocated)
            rc = VERR_EOF;
        else
        {
            rc = VINF_SUCCESS;

            uint64_t const cbInitialized = RT_LE2H_U64(pAttr->pAttrHdr->u.NonRes.cbInitialized);
            if (   off < cbInitialized
                && cbToWrite > 0)
            {
                /*
                 * Locate the first extent.  This is a tad complicated.
                 *
                 * We move off along as we traverse the extent tables, so that it is relative
                 * to the start of the current extent.
                 */
                PRTFSNTFSEXTENTS    pTable   = &pAttr->Extents;
                uint32_t            iExtent  = 0;
                PRTFSNTFSATTRSUBREC pCurSub  = NULL;
                for (;;)
                {
                    if (off < pTable->cbData)
                    {
                        while (   iExtent < pTable->cExtents
                               && off >= pTable->paExtents[iExtent].cbExtent)
                        {
                            off -= pTable->paExtents[iExtent].cbExtent;
                            iExtent++;
                        }
                        AssertReturn(iExtent < pTable->cExtents, VERR_INTERNAL_ERROR_2);
                        break;
                    }

                    /* Next table. */
                    off -= pTable->cbData;
                    if (!pCurSub)
                        pCurSub = pAttr->pSubRecHead;
                    else
                        pCurSub = pCurSub->pNext;
                    if (!pCurSub)
                    {
                        iExtent = UINT32_MAX;
                        break;
                    }
                    pTable  = &pCurSub->Extents;
                    iExtent = 0;
                }

                /*
                 * The write loop.
                 */
                while (iExtent != UINT32_MAX)
                {
                    uint64_t cbMaxWrite = pTable->paExtents[iExtent].cbExtent;
                    Assert(off < cbMaxWrite);
                    cbMaxWrite -= off;
                    size_t const cbThisWrite = cbMaxWrite >= cbToWrite ? cbToWrite : (size_t)cbMaxWrite;
                    if (pTable->paExtents[iExtent].off == UINT64_MAX)
                    {
                        if (!ASMMemIsZero(pvBuf, cbThisWrite))
                        {
                            LogRel(("rtFsNtfsAttr_Write: Unable to modify sparse section of file!\n"));
                            rc = VERR_INTERNAL_ERROR_2;
                            break;
                        }
                    }
                    else
                    {
                        rc = RTVfsFileWriteAt(pVol->hVfsBacking, pTable->paExtents[iExtent].off + off, pvBuf, cbThisWrite, NULL);
                        Log4(("NTFS: Volume write: @%#RX64 LB %#zx -> %Rrc\n", pTable->paExtents[iExtent].off + off, cbThisWrite, rc));
                        if (RT_FAILURE(rc))
                            break;
                    }
                    pvBuf      = (uint8_t const *)pvBuf + cbThisWrite;
                    cbToWrite -= cbThisWrite;
                    if (!cbToWrite)
                        break;
                    off = 0;

                    /*
                     * Advance to the next extent.
                     */
                    iExtent++;
                    if (iExtent >= pTable->cExtents)
                    {
                        pCurSub = pCurSub ? pCurSub->pNext : pAttr->pSubRecHead;
                        if (!pCurSub)
                            break;
                        pTable  = &pCurSub->Extents;
                        iExtent = 0;
                    }
                }
            }
        }
    }
    else
    {
        LogRel(("rtFsNtfsAttr_Write: Compressed files are not supported\n"));
        rc = VERR_NOT_SUPPORTED;
    }

    /*
     * Anything else beyond the end of what's stored/initialized?
     */
    if (   cbToWrite > 0
        && RT_SUCCESS(rc))
    {
        LogRel(("rtFsNtfsAttr_Write: Unable to modify sparse section (tail) of file!\n"));
        rc = VERR_INTERNAL_ERROR_2;
    }

    return rc;
}


/**
 *
 * @returns
 * @param   pRecHdr             .
 * @param   cbRec               .
 * @param   fRelaxedUsa         .
 * @param   pErrInfo            .
 *
 * @see https://msdn.microsoft.com/en-us/library/bb470212%28v=vs.85%29.aspx
 */
static int rtFsNtfsRec_DoMultiSectorFixups(PNTFSRECHDR pRecHdr, uint32_t cbRec, bool fRelaxedUsa, PRTERRINFO pErrInfo)
{
    /*
     * Do sanity checking.
     */
    uint16_t offUpdateSeqArray = RT_LE2H_U16(pRecHdr->offUpdateSeqArray);
    uint16_t cUpdateSeqEntries = RT_LE2H_U16(pRecHdr->cUpdateSeqEntries);
    if (   !(cbRec & (NTFS_MULTI_SECTOR_STRIDE - 1))
        && !(offUpdateSeqArray & 1) /* two byte aligned */
        && cUpdateSeqEntries == 1 + cbRec / NTFS_MULTI_SECTOR_STRIDE
        && offUpdateSeqArray + (uint32_t)cUpdateSeqEntries * 2U < NTFS_MULTI_SECTOR_STRIDE - 2U)
    {
        uint16_t const *pauUsa = (uint16_t const *)((uint8_t *)pRecHdr + offUpdateSeqArray);

        /*
         * The first update seqence array entry is the value stored at
         * the fixup locations at the end of the blocks.  We read this
         * and check each of the blocks.
         */
        uint16_t const uCheck = *pauUsa++;
        cUpdateSeqEntries--;
        for (uint16_t iBlock = 0; iBlock < cUpdateSeqEntries; iBlock++)
        {
            uint16_t const *puBlockCheck = (uint16_t const *)((uint8_t *)pRecHdr + (iBlock + 1) * NTFS_MULTI_SECTOR_STRIDE - 2U);
            if (*puBlockCheck == uCheck)
            { /* likely */ }
            else if (!fRelaxedUsa)
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_OFFSET,
                                               "Multisector transfer error: block #%u ends with %#x instead of %#x (fixup: %#x)",
                                               iBlock, RT_LE2H_U16(*puBlockCheck), RT_LE2H_U16(uCheck), RT_LE2H_U16(pauUsa[iBlock]) );
            else
            {
                Log(("NTFS: Multisector transfer warning: block #%u ends with %#x instead of %#x (fixup: %#x)\n",
                     iBlock, RT_LE2H_U16(*puBlockCheck), RT_LE2H_U16(uCheck), RT_LE2H_U16(pauUsa[iBlock]) ));
                return VINF_SUCCESS;
            }
        }

        /*
         * Apply the fixups.
         * Note! We advanced pauUsa above, so it's now at the fixup values.
         */
        for (uint16_t iBlock = 0; iBlock < cUpdateSeqEntries; iBlock++)
        {
            uint16_t *puFixup = (uint16_t *)((uint8_t *)pRecHdr + (iBlock + 1) * NTFS_MULTI_SECTOR_STRIDE - 2U);
            *puFixup = pauUsa[iBlock];
        }
        return VINF_SUCCESS;
    }
    if (fRelaxedUsa)
    {
        Log(("NTFS: Ignoring bogus multisector update sequence: cbRec=%#x uMagic=%#RX32 offUpdateSeqArray=%#x cUpdateSeqEntries=%#x\n",
             cbRec, RT_LE2H_U32(pRecHdr->uMagic), offUpdateSeqArray, cUpdateSeqEntries ));
        return VINF_SUCCESS;
    }
    return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_OFFSET,
                                   "Bogus multisector update sequence: cbRec=%#x uMagic=%#RX32 offUpdateSeqArray=%#x cUpdateSeqEntries=%#x",
                                   cbRec, RT_LE2H_U32(pRecHdr->uMagic), offUpdateSeqArray, cUpdateSeqEntries);
}


/**
 * Allocate and parse an MFT record, returning a core object structure.
 *
 * @returns IPRT status code.
 * @param   pThis               The NTFS volume instance.
 * @param   idxMft              The index of the MTF record.
 * @param   fRelaxedUsa         Relaxed update sequence checking. Won't fail if
 *                              checks doesn't work or not present.
 * @param   ppCore              Where to return the core object structure.
 * @param   pErrInfo            Where to return error details.  Optional.
 */
static int rtFsNtfsVol_NewCoreForMftIdx(PRTFSNTFSVOL pThis, uint64_t idxMft, bool fRelaxedUsa,
                                        PRTFSNTFSCORE *ppCore, PRTERRINFO pErrInfo)
{
    *ppCore = NULL;
    Assert(pThis->pMftData);
    Assert(RTAvlU64Get(&pThis->MftRoot, idxMft) == NULL);

    PRTFSNTFSMFTREC pRec = rtFsNtfsVol_NewMftRec(pThis, idxMft);
    AssertReturn(pRec, VERR_NO_MEMORY);

    uint64_t offRec = idxMft * pThis->cbMftRecord;
    int rc = rtFsNtfsAttr_Read(pThis->pMftData, offRec, pRec->pbRec, pThis->cbMftRecord);
    if (RT_SUCCESS(rc))
        rc = rtFsNtfsRec_DoMultiSectorFixups(&pRec->pFileRec->Hdr, pThis->cbMftRecord, fRelaxedUsa, pErrInfo);
    if (RT_SUCCESS(rc))
    {
#ifdef LOG_ENABLED
        rtfsNtfsMftRec_Log(pRec, pThis->cbMftRecord);
#endif
        rc = rtFsNtfsVol_ParseMft(pThis, pRec, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            PRTFSNTFSCORE pCore = pRec->pCore;
            rtFsNtfsMftRec_Release(pRec, pThis);

            /* Insert core into the cache list and update the cost, maybe trimming the cache. */
            RTListAppend(&pThis->CoreInUseHead, &pCore->ListEntry);
            pThis->cbCoreObjects += pCore->cbCost;
            if (pThis->cbCoreObjects > RTFSNTFS_MAX_CORE_CACHE_SIZE)
                rtFsNtfsIdxVol_TrimCoreObjectCache(pThis);

            *ppCore = pCore;
            return VINF_SUCCESS;
        }

        if (pRec->pCore)
            rtFsNtfsCore_Destroy(pRec->pCore);
        rtFsNtfsMftRec_Release(pRec, pThis);
    }
    return rc;
}


/**
 * Queries the core object struct for the given MFT record reference.
 *
 * Does caching.
 *
 * @returns IPRT status code.
 * @param   pThis           The NTFS volume instance.
 * @param   pMftRef         The MFT reference to get the corresponding core
 *                          for.
 * @param   fRelaxedUsa     Relaxed update sequence checking. Won't fail if
 *                          checks doesn't work or not present.
 * @param   ppCore          Where to return the referenced core object
 *                          structure.
 * @param   pErrInfo        Where to return error details.  Optional.
 */
static int rtFsNtfsVol_QueryCoreForMftRef(PRTFSNTFSVOL pThis, PCNTFSMFTREF pMftRef , bool fRelaxedUsa,
                                          PRTFSNTFSCORE *ppCore, PRTERRINFO pErrInfo)
{
    *ppCore = NULL;
    Assert(pThis->pMftData);

    int rc;
    PRTFSNTFSMFTREC pMftRec = (PRTFSNTFSMFTREC)RTAvlU64Get(&pThis->MftRoot, NTFSMFTREF_GET_IDX(pMftRef));
    if (pMftRec)
    {
        /*
         * Cache hit.  Check that the resure sequence number matches.
         * To be slightly paranoid, also check that it's a base MFT record and that it has been parsed already.
         */
        if (RT_LE2H_U16(pMftRec->pFileRec->uRecReuseSeqNo) == NTFSMFTREF_GET_SEQ(pMftRef))
        {
            if (   NTFSMFTREF_IS_ZERO(&pMftRec->pFileRec->BaseMftRec)
                && pMftRec->pCore)
            {
                rtFsNtfsCore_Retain(pMftRec->pCore);
                *ppCore = pMftRec->pCore;
                rc = VINF_SUCCESS;
            }
            else
                AssertLogRelMsgFailedStmt(("pCore=%p; BaseMftRec=%#RX64 sqn %#x\n", pMftRec->pCore,
                                           NTFSMFTREF_GET_IDX(&pMftRec->pFileRec->BaseMftRec),
                                           NTFSMFTREF_GET_SEQ(&pMftRec->pFileRec->BaseMftRec)),
                                           rc = VERR_INTERNAL_ERROR_3 );
        }
        else
            rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_OFFSET,
                                         "Stale parent directory MFT reference: %#RX64 sqn %#x - current sqn %#x",
                                         NTFSMFTREF_GET_IDX(pMftRef), NTFSMFTREF_GET_SEQ(pMftRef),
                                         RT_LE2H_U16(pMftRec->pFileRec->uRecReuseSeqNo) );
    }
    else
    {
        /*
         * Load new and check that the reuse sequence number match.
         */
        rc = rtFsNtfsVol_NewCoreForMftIdx(pThis, NTFSMFTREF_GET_IDX(pMftRef), fRelaxedUsa, ppCore, pErrInfo);
        if (RT_SUCCESS(rc))
        {
            PRTFSNTFSCORE pCore = *ppCore;
            if (RT_LE2H_U16(pCore->pMftRec->pFileRec->uRecReuseSeqNo) == NTFSMFTREF_GET_SEQ(pMftRef))
                rc = VINF_SUCCESS;
            else
            {
                rtFsNtfsCore_Release(pCore);
                *ppCore = NULL;
                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_OFFSET,
                                             "Stale parent directory MFT reference: %#RX64 sqn %#x - current sqn %#x",
                                             NTFSMFTREF_GET_IDX(pMftRef), NTFSMFTREF_GET_SEQ(pMftRef),
                                             RT_LE2H_U16(pCore->pMftRec->pFileRec->uRecReuseSeqNo) );
            }
        }
    }
    return rc;
}


/**
 * Destroys a core structure.
 *
 * ASSUMES the caller has remove @a pThis from the list it's on and updated the
 * cbCoreObjects as necessary.
 *
 * @returns 0
 * @param   pThis               The core structure.
 */
static uint32_t rtFsNtfsCore_Destroy(PRTFSNTFSCORE pThis)
{
    /*
     * Free attributes.
     */
    PRTFSNTFSATTR pCurAttr;
    PRTFSNTFSATTR pNextAttr;
    RTListForEachSafe(&pThis->AttribHead, pCurAttr, pNextAttr, RTFSNTFSATTR, ListEntry)
    {
        PRTFSNTFSATTRSUBREC pSub = pCurAttr->pSubRecHead;
        while (pSub)
        {
            pCurAttr->pSubRecHead = pSub->pNext;
            RTMemFree(pSub->Extents.paExtents);
            pSub->Extents.paExtents = NULL;
            pSub->pAttrHdr          = NULL;
            pSub->pNext             = NULL;
            RTMemFree(pSub);

            pSub = pCurAttr->pSubRecHead;
        }

        pCurAttr->pCore    = NULL;
        pCurAttr->pAttrHdr = NULL;
        RTMemFree(pCurAttr->Extents.paExtents);
        pCurAttr->Extents.paExtents = NULL;
    }

    /*
     * Release the MFT chain.
     */
    PRTFSNTFSMFTREC pMftRec = pThis->pMftRec;
    while (pMftRec)
    {
        pThis->pMftRec = pMftRec->pNext;
        Assert(pMftRec->pCore == pThis);
        pMftRec->pNext = NULL;
        pMftRec->pCore = NULL;
        rtFsNtfsMftRec_Release(pMftRec, pThis->pVol);

        pMftRec = pThis->pMftRec;
    }

    RTMemFree(pThis);

    return 0;
}


/**
 * Trims the core object cache down to RTFSNTFS_MAX_CORE_CACHE_SIZE.
 *
 * @param   pThis               The NTFS volume instance.
 */
static void rtFsNtfsIdxVol_TrimCoreObjectCache(PRTFSNTFSVOL pThis)
{
    while (pThis->cbCoreObjects > RTFSNTFS_MAX_CORE_CACHE_SIZE)
    {
        PRTFSNTFSCORE pCore = RTListRemoveFirst(&pThis->CoreUnusedHead, RTFSNTFSCORE, ListEntry);
        if (!pCore)
            break;
        pThis->cbCoreObjects -= pCore->cbCost;
        rtFsNtfsCore_Destroy(pCore);
    }
}


/**
 * Releases a refernece to a core structure, maybe destroying it.
 *
 * @returns New reference count.
 * @param   pThis               The core structure.
 */
static uint32_t rtFsNtfsCore_Release(PRTFSNTFSCORE pThis)
{
    if (pThis)
    {
        uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
        Assert(cRefs < 128);
        if (cRefs != 0)
            return cRefs;

        /* Move from in-use list to unused list.  Trim the cache if too big. */
        RTListNodeRemove(&pThis->ListEntry);

        PRTFSNTFSVOL pVol = pThis->pVol;
        RTListAppend(&pVol->CoreUnusedHead, &pThis->ListEntry);
        if (pVol->cbCoreObjects > RTFSNTFS_MAX_CORE_CACHE_SIZE)
            rtFsNtfsIdxVol_TrimCoreObjectCache(pVol);
    }
    return 0;
}


/**
 * Retains a refernece to a core structure.
 *
 * @returns New reference count.
 * @param   pThis               The core structure.
 */
static uint32_t rtFsNtfsCore_Retain(PRTFSNTFSCORE pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    if (cRefs == 1)
    {
        /* Move from unused list to in-use list. */
        RTListNodeRemove(&pThis->ListEntry);
        RTListAppend(&pThis->pVol->CoreInUseHead, &pThis->ListEntry);
    }
    Assert(cRefs < 128);
    return cRefs;
}


/**
 * Finds an unnamed attribute.
 *
 * @returns Pointer to the attribute structure if found, NULL if not.
 * @param   pThis               The core object structure to search.
 * @param   uAttrType           The attribute type to find.
 */
static PRTFSNTFSATTR rtFsNtfsCore_FindUnnamedAttribute(PRTFSNTFSCORE pThis, uint32_t uAttrType)
{
    PRTFSNTFSATTR pCurAttr;
    RTListForEach(&pThis->AttribHead, pCurAttr, RTFSNTFSATTR, ListEntry)
    {
        PNTFSATTRIBHDR pAttrHdr = pCurAttr->pAttrHdr;
        if (   pAttrHdr->uAttrType == uAttrType
            && pAttrHdr->cwcName == 0)
            return pCurAttr;
    }
    return NULL;
}


/**
 * Finds a named attribute, case insensitive ASCII variant.
 *
 * @returns Pointer to the attribute structure if found, NULL if not.
 * @param   pThis               The core object structure to search.
 * @param   uAttrType           The attribute type to find.
 * @param   pszAttrib           The attribute name, predefined 7-bit ASCII name.
 * @param   cchAttrib           The length of the attribute.
 */
static PRTFSNTFSATTR rtFsNtfsCore_FindNamedAttributeAscii(PRTFSNTFSCORE pThis, uint32_t uAttrType,
                                                          const char *pszAttrib, size_t cchAttrib)
{
    Assert(cchAttrib > 0);
    PRTFSNTFSATTR pCurAttr;
    RTListForEach(&pThis->AttribHead, pCurAttr, RTFSNTFSATTR, ListEntry)
    {
        PNTFSATTRIBHDR pAttrHdr = pCurAttr->pAttrHdr;
        if (   pAttrHdr->uAttrType == uAttrType
            && pAttrHdr->cwcName == cchAttrib
            && RTUtf16NICmpAscii(NTFSATTRIBHDR_GET_NAME(pAttrHdr), pszAttrib, cchAttrib) == 0)
            return pCurAttr;
    }
    return NULL;
}


/**
 * This attribute conversion code is a slightly modified version of rtFsModeFromDos.
 *
 * @returns IPRT fmode mask.
 * @param   fFileAttribs        The NT file attributes.
 * @param   pFilename           The filename attribute structure, optional.
 * @param   cbFilename          The size of the filename attribute structure.
 */
static RTFMODE rtFsNtfsConvertFileattribsToMode(uint32_t fFileAttribs, PCNTFSATFILENAME pFilename, uint32_t cbFilename)
{
    RTFMODE fMode = (fFileAttribs << RTFS_DOS_SHIFT) & RTFS_DOS_MASK_NT;
    if (fFileAttribs & NTFS_FA_DUP_FILE_NAME_INDEX_PRESENT)
        fMode |= RTFS_DOS_DIRECTORY;

    /* everything is readable. */
    fMode |= RTFS_UNIX_IRUSR | RTFS_UNIX_IRGRP | RTFS_UNIX_IROTH;
    if (fMode & RTFS_DOS_DIRECTORY)
        /* directories are executable. */
        fMode |= RTFS_TYPE_DIRECTORY | RTFS_UNIX_IXUSR | RTFS_UNIX_IXGRP | RTFS_UNIX_IXOTH;
    else
    {
        fMode |= RTFS_TYPE_FILE;
        if (   pFilename
            && pFilename->cwcFilename >= 4
            && RT_UOFFSETOF_DYN(NTFSATFILENAME, wszFilename[pFilename->cwcFilename]) <= cbFilename)
        {
            PCRTUTF16 pwcExt = &pFilename->wszFilename[pFilename->cwcFilename - 4];
            if ( *pwcExt++ == '.')
            {
                /* check for executable extension. */
                if (   (unsigned)pwcExt[0] < 0x7fU
                    && (unsigned)pwcExt[1] < 0x7fU
                    && (unsigned)pwcExt[2] < 0x7fU)
                {
                    char szExt[4];
                    szExt[0] = RT_C_TO_LOWER(pwcExt[0]);
                    szExt[1] = RT_C_TO_LOWER(pwcExt[1]);
                    szExt[2] = RT_C_TO_LOWER(pwcExt[2]);
                    szExt[3] = '\0';
                    if (    !memcmp(szExt, "exe", 4)
                        ||  !memcmp(szExt, "bat", 4)
                        ||  !memcmp(szExt, "com", 4)
                        ||  !memcmp(szExt, "cmd", 4)
                        ||  !memcmp(szExt, "btm", 4)
                       )
                        fMode |= RTFS_UNIX_IXUSR | RTFS_UNIX_IXGRP | RTFS_UNIX_IXOTH;
                }
            }
        }
    }

    /* Is it really a symbolic link? */
    if (   (fMode & RTFS_DOS_NT_REPARSE_POINT)
        && pFilename
        && pFilename->u.uReparseTag == RTFSMODE_SYMLINK_REPARSE_TAG)
        fMode = (fMode & ~RTFS_TYPE_MASK) | RTFS_TYPE_SYMLINK;

    /* writable? */
    if (!(fMode & RTFS_DOS_READONLY))
        fMode |= RTFS_UNIX_IWUSR | RTFS_UNIX_IWGRP | RTFS_UNIX_IWOTH;

    return fMode;
}


/**
 * Worker for various QueryInfo methods.
 *
 * @returns IPRT status code.
 * @param   pThis               The core object structure to return info for.
 * @param   pAttr               The attribute that's being presented.  Take the
 *                              allocation and timestamp info from it, if
 *                              non-resident.
 * @param   pObjInfo            Where to return object info.
 * @param   enmAddAttr          What additional info to return.
 */
static int rtFsNtfsCore_QueryInfo(PRTFSNTFSCORE pThis, PRTFSNTFSATTR pAttr, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    /*
     * Wipe the structure and fill in common dummy value.
     */
    RT_ZERO(*pObjInfo);
    switch (enmAddAttr)
    {
        case RTFSOBJATTRADD_UNIX:
            pObjInfo->Attr.u.Unix.uid           = NIL_RTUID;
            pObjInfo->Attr.u.Unix.gid           = NIL_RTGID;
            pObjInfo->Attr.u.Unix.cHardlinks    = 1;
            //pObjInfo->Attr.u.Unix.INodeIdDevice = 0;
            pObjInfo->Attr.u.Unix.INodeId       = pThis->pMftRec->TreeNode.Key;
            //pObjInfo->Attr.u.Unix.fFlags        = 0;
            //pObjInfo->Attr.u.Unix.GenerationId  = 0;
            //pObjInfo->Attr.u.Unix.Device        = 0;
            break;

        case RTFSOBJATTRADD_UNIX_OWNER:
            pObjInfo->Attr.u.UnixOwner.uid = NIL_RTUID;
            break;

        case RTFSOBJATTRADD_UNIX_GROUP:
            pObjInfo->Attr.u.UnixGroup.gid = NIL_RTGID;
            break;

        default:
            break;
    }

    /*
     * Look for the standard information attribute and use that as basis.
     */
    uint32_t      fFileAttribs;
    PRTFSNTFSATTR pStdInfoAttr = rtFsNtfsCore_FindUnnamedAttribute(pThis, NTFS_AT_STANDARD_INFORMATION);
    if (   pStdInfoAttr
        && pStdInfoAttr->cbResident >= sizeof(NTFSATSTDINFO) )
    {
        Assert(!pStdInfoAttr->pAttrHdr->fNonResident);
        PCNTFSATSTDINFO pStdInfo = (PCNTFSATSTDINFO)NTFSATTRIBHDR_GET_RES_VALUE_PTR(pStdInfoAttr->pAttrHdr);
        RTTimeSpecSetNtTime(&pObjInfo->BirthTime,        RT_LE2H_U64(pStdInfo->iCreationTime));
        RTTimeSpecSetNtTime(&pObjInfo->ModificationTime, RT_LE2H_U64(pStdInfo->iLastDataModTime));
        RTTimeSpecSetNtTime(&pObjInfo->ChangeTime,       RT_LE2H_U64(pStdInfo->iLastMftModTime));
        RTTimeSpecSetNtTime(&pObjInfo->AccessTime,       RT_LE2H_U64(pStdInfo->iLastAccessTime));
        if (enmAddAttr == RTFSOBJATTRADD_UNIX)
        {
            pObjInfo->Attr.u.Unix.uid          = pStdInfo->idOwner;
            pObjInfo->Attr.u.Unix.GenerationId = pStdInfo->uFileVersion;
        }
        else if (enmAddAttr == RTFSOBJATTRADD_UNIX_OWNER)
            pObjInfo->Attr.u.UnixOwner.uid = pStdInfo->idOwner;
        fFileAttribs = pStdInfo->fFileAttribs;
    }
    else
    {
        /** @todo check out the filename record?   */
        switch (pAttr->pAttrHdr->uAttrType)
        {
            default:
                AssertFailed();
                RT_FALL_THRU();
            case NTFS_AT_DATA:
                fFileAttribs = NTFS_FA_NORMAL;
                break;

            case NTFS_AT_INDEX_ROOT:
            case NTFS_AT_INDEX_ALLOCATION:
                fFileAttribs = NTFS_FA_DIRECTORY;
                break;
        }
    }

    /*
     * Take the allocation info from the destilled attribute data.
     */
    pObjInfo->cbObject    = pAttr->cbValue;
    pObjInfo->cbAllocated = pAttr->Extents.cbData;
    if (   pAttr->pAttrHdr->fNonResident
        && (int64_t)pObjInfo->cbAllocated < (int64_t)RT_LE2H_U64(pAttr->pAttrHdr->u.NonRes.cbAllocated))
        pObjInfo->cbAllocated = RT_LE2H_U64(pAttr->pAttrHdr->u.NonRes.cbAllocated);

    /*
     * See if we can find a filename record before we try convert the file attributes to mode.
     */
    PCNTFSATFILENAME pFilename     = NULL;
    PRTFSNTFSATTR    pFilenameAttr = rtFsNtfsCore_FindUnnamedAttribute(pThis, NTFS_AT_FILENAME);
    if (   pFilenameAttr
        && pFilenameAttr->cbResident >= RT_UOFFSETOF(NTFSATFILENAME, wszFilename) )
    {
        Assert(!pFilenameAttr->pAttrHdr->fNonResident);
        pFilename = (PCNTFSATFILENAME)NTFSATTRIBHDR_GET_RES_VALUE_PTR(pFilenameAttr->pAttrHdr);
        if (pStdInfoAttr)
            fFileAttribs |= pFilename->fFileAttribs;
        else
            fFileAttribs = pFilename->fFileAttribs;
    }

    /*
     * Convert attribs to file mode flags.
     */
    pObjInfo->Attr.fMode = rtFsNtfsConvertFileattribsToMode(fFileAttribs, pFilename,
                                                            pFilenameAttr ? pFilenameAttr->cbResident : 0);

    return VINF_SUCCESS;
}




/*
 *
 * File operations.
 * File operations.
 * File operations.
 *
 */

/**
 * Releases a reference to a shared NTFS file structure.
 *
 * @returns New reference count.
 * @param   pShared             The shared NTFS file structure.
 */
static uint32_t rtFsNtfsFileShrd_Release(PRTFSNTFSFILESHRD pShared)
{
    uint32_t cRefs = ASMAtomicDecU32(&pShared->cRefs);
    Assert(cRefs < 64);
    if (cRefs == 0)
    {
        LogFlow(("rtFsNtfsFileShrd_Release(%p): Destroying it\n", pShared));
        Assert(pShared->pData->uObj.pSharedFile == pShared);
        pShared->pData->uObj.pSharedFile = NULL;
        rtFsNtfsCore_Release(pShared->pData->pCore);
        pShared->pData = NULL;
        RTMemFree(pShared);
    }
    return cRefs;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnClose}
 */
static DECLCALLBACK(int) rtFsNtfsFile_Close(void *pvThis)
{
    PRTFSNTFSFILE pThis = (PRTFSNTFSFILE)pvThis;
    LogFlow(("rtFsNtfsFile_Close(%p/%p)\n", pThis, pThis->pShared));

    PRTFSNTFSFILESHRD pShared = pThis->pShared;
    pThis->pShared = NULL;
    if (pShared)
        rtFsNtfsFileShrd_Release(pShared);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtFsNtfsFile_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTFSNTFSFILE       pThis   = (PRTFSNTFSFILE)pvThis;
    PRTFSNTFSATTR       pDataAttr = pThis->pShared->pData;
    return rtFsNtfsCore_QueryInfo(pDataAttr->pCore, pDataAttr, pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnRead}
 */
static DECLCALLBACK(int) rtFsNtfsFile_Read(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbRead)
{
    PRTFSNTFSFILE pThis = (PRTFSNTFSFILE)pvThis;
    AssertReturn(pSgBuf->cSegs == 1, VERR_INTERNAL_ERROR_3);
    RT_NOREF(fBlocking);

    if (off == -1)
        off = pThis->offFile;
    else
        AssertReturn(off >= 0, VERR_INTERNAL_ERROR_3);

    int rc;
    size_t cbRead = pSgBuf->paSegs[0].cbSeg;
    if (!pcbRead)
    {
        rc = rtFsNtfsAttr_Read(pThis->pShared->pData, off, pSgBuf->paSegs[0].pvSeg, cbRead);
        if (RT_SUCCESS(rc))
        {
            pThis->offFile = off + cbRead;
            RTSgBufAdvance(pSgBuf, cbRead);
        }
        Log6(("rtFsNtfsFile_Read: off=%#RX64 cbSeg=%#x -> %Rrc\n", off, pSgBuf->paSegs[0].cbSeg, rc));
    }
    else
    {
        PRTFSNTFSATTR pDataAttr = pThis->pShared->pData;
        if ((uint64_t)off >= pDataAttr->cbValue)
        {
            *pcbRead = 0;
            rc = VINF_EOF;
        }
        else
        {
            if ((uint64_t)off + cbRead <= pDataAttr->cbValue)
                rc = rtFsNtfsAttr_Read(pThis->pShared->pData, off, pSgBuf->paSegs[0].pvSeg, cbRead);
            else
            {
                /* Return VINF_EOF if beyond end-of-file. */
                cbRead = (size_t)(pDataAttr->cbValue - (uint64_t)off);
                rc = rtFsNtfsAttr_Read(pThis->pShared->pData, off, pSgBuf->paSegs[0].pvSeg, cbRead);
                if (RT_SUCCESS(rc))
                    rc = VINF_EOF;
            }
            if (RT_SUCCESS(rc))
            {
                pThis->offFile = off + cbRead;
                *pcbRead = cbRead;
                RTSgBufAdvance(pSgBuf, cbRead);
            }
            else
                *pcbRead = 0;
        }
        Log6(("rtFsNtfsFile_Read: off=%#RX64 cbSeg=%#x -> %Rrc *pcbRead=%#x\n", off, pSgBuf->paSegs[0].cbSeg, rc, *pcbRead));
    }

    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnWrite}
 */
static DECLCALLBACK(int) rtFsNtfsFile_Write(void *pvThis, RTFOFF off, PRTSGBUF pSgBuf, bool fBlocking, size_t *pcbWritten)
{
    PRTFSNTFSFILE pThis = (PRTFSNTFSFILE)pvThis;
    AssertReturn(pSgBuf->cSegs == 1, VERR_INTERNAL_ERROR_3);
    RT_NOREF(fBlocking);

    if (off == -1)
        off = pThis->offFile;
    else
        AssertReturn(off >= 0, VERR_INTERNAL_ERROR_3);

    int           rc;
    PRTFSNTFSATTR pDataAttr = pThis->pShared->pData;
    size_t        cbToWrite = pSgBuf->paSegs[0].cbSeg;
    if ((uint64_t)off + cbToWrite <= pDataAttr->cbValue)
    {
        rc = rtFsNtfsAttr_Write(pThis->pShared->pData, off, pSgBuf->paSegs[0].pvSeg, cbToWrite);
        Log6(("rtFsNtfsFile_Write: off=%#RX64 cbToWrite=%#zx -> %Rrc\n", off, cbToWrite, rc));
        if (RT_SUCCESS(rc))
        {
            pThis->offFile = off + cbToWrite;
            RTSgBufAdvance(pSgBuf, cbToWrite);
        }
        if (pcbWritten)
            *pcbWritten = RT_SUCCESS(rc) ? cbToWrite : 0;
    }
    else if ((uint64_t)off < pDataAttr->cbValue)
    {
        size_t cbWritten = pDataAttr->cbValue - off;
        rc = rtFsNtfsAttr_Write(pThis->pShared->pData, off, pSgBuf->paSegs[0].pvSeg, cbWritten);
        if (RT_SUCCESS(rc))
        {
            Log6(("rtFsNtfsFile_Write: off=%#RX64 cbToWrite=%#zx -> VERR_EOF [EOF: %#RX64, Written: %#zx]\n",
                  off, cbToWrite, pDataAttr->cbValue, cbWritten));
            pThis->offFile = off + cbWritten;
            if (pcbWritten)
                *pcbWritten = cbWritten;
            RTSgBufAdvance(pSgBuf, cbWritten);
            rc = VERR_EOF;
        }
        else
        {
            Log6(("rtFsNtfsFile_Write: off=%#RX64 cbToWrite=%#zx -> %Rrc [EOF: %#RX64]\n", off, cbToWrite, rc, pDataAttr->cbValue));
            if (pcbWritten)
                *pcbWritten = 0;
        }
    }
    else
    {
        Log6(("rtFsNtfsFile_Write: off=%#RX64 cbToWrite=%#zx -> VERR_EOF [EOF: %#RX64]\n", off, cbToWrite, pDataAttr->cbValue));
        rc = VERR_EOF;
        if (pcbWritten)
            *pcbWritten = 0;
    }

    return rc;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnFlush}
 */
static DECLCALLBACK(int) rtFsNtfsFile_Flush(void *pvThis)
{
    RT_NOREF(pvThis);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSIOSTREAMOPS,pfnTell}
 */
static DECLCALLBACK(int) rtFsNtfsFile_Tell(void *pvThis, PRTFOFF poffActual)
{
    PRTFSNTFSFILE pThis = (PRTFSNTFSFILE)pvThis;
    *poffActual = pThis->offFile;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtFsNtfsFile_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    RT_NOREF(pvThis, fMode, fMask);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtFsNtfsFile_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                               PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    RT_NOREF(pvThis, pAccessTime, pModificationTime, pChangeTime, pBirthTime);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtFsNtfsFile_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    RT_NOREF(pvThis, uid, gid);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSeek}
 */
static DECLCALLBACK(int) rtFsNtfsFile_Seek(void *pvThis, RTFOFF offSeek, unsigned uMethod, PRTFOFF poffActual)
{
    PRTFSNTFSFILE pThis = (PRTFSNTFSFILE)pvThis;
    RTFOFF offNew;
    switch (uMethod)
    {
        case RTFILE_SEEK_BEGIN:
            offNew = offSeek;
            break;
        case RTFILE_SEEK_END:
            offNew = (RTFOFF)pThis->pShared->pData->cbValue + offSeek;
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
static DECLCALLBACK(int) rtFsNtfsFile_QuerySize(void *pvThis, uint64_t *pcbFile)
{
    PRTFSNTFSFILE pThis = (PRTFSNTFSFILE)pvThis;
    *pcbFile = pThis->pShared->pData->cbValue;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnSetSize}
 */
static DECLCALLBACK(int) rtFsNtfsFile_SetSize(void *pvThis, uint64_t cbFile, uint32_t fFlags)
{
    NOREF(pvThis); NOREF(cbFile); NOREF(fFlags);
    return VERR_NOT_IMPLEMENTED;
}


/**
 * @interface_method_impl{RTVFSFILEOPS,pfnQueryMaxSize}
 */
static DECLCALLBACK(int) rtFsNtfsFile_QueryMaxSize(void *pvThis, uint64_t *pcbMax)
{
    RT_NOREF(pvThis);
    *pcbMax = INT64_MAX;
    return VINF_SUCCESS;
}


/**
 * NTFS file operations.
 */
static const RTVFSFILEOPS g_rtFsNtfsFileOps =
{
    { /* Stream */
        { /* Obj */
            RTVFSOBJOPS_VERSION,
            RTVFSOBJTYPE_FILE,
            "NTFS File",
            rtFsNtfsFile_Close,
            rtFsNtfsFile_QueryInfo,
            NULL,
            RTVFSOBJOPS_VERSION
        },
        RTVFSIOSTREAMOPS_VERSION,
        RTVFSIOSTREAMOPS_FEAT_NO_SG,
        rtFsNtfsFile_Read,
        rtFsNtfsFile_Write,
        rtFsNtfsFile_Flush,
        NULL /*PollOne*/,
        rtFsNtfsFile_Tell,
        NULL /*pfnSkip*/,
        NULL /*pfnZeroFill*/,
        RTVFSIOSTREAMOPS_VERSION,
    },
    RTVFSFILEOPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSFILEOPS, ObjSet) - RT_UOFFSETOF(RTVFSFILEOPS, Stream.Obj),
        rtFsNtfsFile_SetMode,
        rtFsNtfsFile_SetTimes,
        rtFsNtfsFile_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtFsNtfsFile_Seek,
    rtFsNtfsFile_QuerySize,
    rtFsNtfsFile_SetSize,
    rtFsNtfsFile_QueryMaxSize,
    RTVFSFILEOPS_VERSION
};


static int rtFsNtfsVol_NewFile(PRTFSNTFSVOL pThis, uint64_t fOpen, PCNTFSIDXENTRYHDR pEntryHdr, const char *pszStreamName,
                               PRTVFSFILE phVfsFile, PRTERRINFO pErrInfo, const char *pszWhat)
{
    /*
     * Get the core structure for the MFT record and check that it's a directory we've got.
     */
    PRTFSNTFSCORE pCore;
    int rc = rtFsNtfsVol_QueryCoreForMftRef(pThis, &pEntryHdr->u.FileMftRec, false /*fRelaxedUsa*/, &pCore, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        if (!(pCore->pMftRec->pFileRec->fFlags & NTFSRECFILE_F_DIRECTORY))
        {
            /*
             * Locate the data attribute.
             */
            PRTFSNTFSATTR pDataAttr;
            if (pszStreamName == NULL)
            {
                pDataAttr = rtFsNtfsCore_FindUnnamedAttribute(pCore, NTFS_AT_DATA);
                if (pDataAttr)
                    rc = VINF_SUCCESS;
                else
                    rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_NOT_A_FILE, "%s: no unamed data stream", pszWhat);
            }
            else
            {
                NOREF(pszStreamName);
                rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_NOT_IMPLEMENTED, "%s: named data streams not implemented yet", pszWhat);
                pDataAttr = NULL;
            }
            if (RT_SUCCESS(rc))
            {
                /*
                 * Get a referenced shared file structure, creating it if necessary.
                 */
                PRTFSNTFSFILESHRD pShared = pDataAttr->uObj.pSharedFile;
                if (pShared)
                {
                    uint32_t cRefs = ASMAtomicIncU32(&pShared->cRefs);
                    Assert(cRefs > 1); NOREF(cRefs);
                }
                else
                {
                    pShared = (PRTFSNTFSFILESHRD)RTMemAllocZ(sizeof(*pShared));
                    if (pShared)
                    {
                        pShared->cRefs = 1;
                        pShared->pData = pDataAttr;
                        rtFsNtfsCore_Retain(pCore);
                        pDataAttr->uObj.pSharedFile = pShared;
                    }
                }
                if (pShared)
                {
                    /*
                     * Create the open file instance.
                     */
                    PRTFSNTFSFILE pNewFile;
                    rc = RTVfsNewFile(&g_rtFsNtfsFileOps, sizeof(*pNewFile), fOpen, pThis->hVfsSelf, NIL_RTVFSLOCK,
                                      phVfsFile, (void **)&pNewFile);
                    if (RT_SUCCESS(rc))
                    {
                        pNewFile->offFile = 0;
                        pNewFile->pShared = pShared;
                        rtFsNtfsCore_Release(pCore);
                        return VINF_SUCCESS;
                    }

                    rtFsNtfsFileShrd_Release(pShared);
                }
                else
                    rc = VERR_NO_MEMORY;
            }
        }
        else
            rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_NOT_A_FILE, "%s: fFlags=%#x", pszWhat, pCore->pMftRec->pFileRec->fFlags);
        rtFsNtfsCore_Release(pCore);
    }
    return rc;
}



/*
 *
 * NTFS directory code.
 * NTFS directory code.
 * NTFS directory code.
 *
 */

#ifdef LOG_ENABLED

/**
 * Logs an index header and all the entries.
 *
 * @param   pIdxHdr     The index header.
 * @param   cbIndex     The number of valid bytes starting with the header.
 * @param   offIndex    The offset of the index header into the parent
 *                      structure.
 * @param   pszPrefix   The log prefix.
 * @param   uIdxType    The index type.
 */
static void rtFsNtfsVol_LogIndexHdrAndEntries(PCNTFSINDEXHDR pIdxHdr, uint32_t cbIndex, uint32_t offIndex,
                                              const char *pszPrefix, uint32_t uIdxType)
{
    if (!LogIs2Enabled())
        return;

    /*
     * Do the header.
     */
    if (cbIndex <= sizeof(*pIdxHdr))
    {
        Log2(("NTFS: %s: Error! Not enough space for the index header! cbIndex=%#x, index head needs %#x\n",
              pszPrefix, cbIndex, sizeof(*pIdxHdr)));
        return;
    }

    Log2(("NTFS: %s:    offFirstEntry %#x%s\n", pszPrefix, RT_LE2H_U32(pIdxHdr->offFirstEntry),
          RT_LE2H_U32(pIdxHdr->offFirstEntry) >= cbIndex ? " !out-of-bounds!" : ""));
    Log2(("NTFS: %s:           cbUsed %#x%s\n", pszPrefix, RT_LE2H_U32(pIdxHdr->cbUsed),
          RT_LE2H_U32(pIdxHdr->cbUsed) > cbIndex ? " !out-of-bounds!" : ""));
    Log2(("NTFS: %s:      cbAllocated %#x%s\n", pszPrefix, RT_LE2H_U32(pIdxHdr->cbAllocated),
          RT_LE2H_U32(pIdxHdr->cbAllocated) > cbIndex ? " !out-of-bounds!" : ""));
    Log2(("NTFS: %s:           fFlags %#x (%s%s)\n", pszPrefix, pIdxHdr->fFlags,
          pIdxHdr->fFlags & NTFSINDEXHDR_F_INTERNAL ? "internal" : "leaf",
          pIdxHdr->fFlags & ~NTFSINDEXHDR_F_INTERNAL ? " !!unknown-flags!!" : ""));
    if (pIdxHdr->abReserved[0]) Log2(("NTFS: %s:    abReserved[0] %#x\n", pszPrefix, pIdxHdr->abReserved[0]));
    if (pIdxHdr->abReserved[1]) Log2(("NTFS: %s:    abReserved[0] %#x\n", pszPrefix, pIdxHdr->abReserved[1]));
    if (pIdxHdr->abReserved[2]) Log2(("NTFS: %s:    abReserved[0] %#x\n", pszPrefix, pIdxHdr->abReserved[2]));

    /*
     * The entries.
     */
    bool        fSeenEnd    = false;
    uint32_t    iEntry      = 0;
    uint32_t    offCurEntry = RT_LE2H_U32(pIdxHdr->offFirstEntry);
    while (offCurEntry < cbIndex)
    {
        if (offCurEntry + sizeof(NTFSIDXENTRYHDR) > cbIndex)
        {
            Log2(("NTFS:    Entry[%#04x]:  Out of bounds: %#x LB %#x, max %#x\n",
                  iEntry, offCurEntry, sizeof(NTFSIDXENTRYHDR), cbIndex));
            break;
        }
        PCNTFSIDXENTRYHDR pEntryHdr = (PCNTFSIDXENTRYHDR)((uint8_t const *)pIdxHdr + offCurEntry);
        Log2(("NTFS:    [%#04x]: @%#05x/@%#05x cbEntry=%#x cbKey=%#x fFlags=%#x (%s%s%s)\n",
              iEntry, offCurEntry, offCurEntry + offIndex, RT_LE2H_U16(pEntryHdr->cbEntry), RT_LE2H_U16(pEntryHdr->cbKey),
              RT_LE2H_U16(pEntryHdr->fFlags),
              pEntryHdr->fFlags & NTFSIDXENTRYHDR_F_INTERNAL ? "internal" : "leaf",
              pEntryHdr->fFlags & NTFSIDXENTRYHDR_F_END ? " end" : "",
              pEntryHdr->fFlags & ~(NTFSIDXENTRYHDR_F_INTERNAL | NTFSIDXENTRYHDR_F_END) ? " !unknown!" : ""));
        if (uIdxType == NTFSATINDEXROOT_TYPE_DIR)
            Log2(("NTFS:             FileMftRec %#RX64 sqn %#x\n",
                  NTFSMFTREF_GET_IDX(&pEntryHdr->u.FileMftRec), NTFSMFTREF_GET_SEQ(&pEntryHdr->u.FileMftRec) ));
        else
            Log2(("NTFS:             offData=%#x cbData=%#x uReserved=%#x\n",
                  RT_LE2H_U16(pEntryHdr->u.View.offData), RT_LE2H_U16(pEntryHdr->u.View.cbData),
                  RT_LE2H_U32(pEntryHdr->u.View.uReserved) ));
        if (pEntryHdr->fFlags & NTFSIDXENTRYHDR_F_INTERNAL)
            Log2(("NTFS:             Subnode=%#RX64\n", RT_LE2H_U64(NTFSIDXENTRYHDR_GET_SUBNODE(pEntryHdr)) ));

        if (   RT_LE2H_U16(pEntryHdr->cbKey) >= RT_UOFFSETOF(NTFSATFILENAME, wszFilename)
            && uIdxType == NTFSATINDEXROOT_TYPE_DIR)
        {
            PCNTFSATFILENAME pFilename = (PCNTFSATFILENAME)(pEntryHdr + 1);
            RTTIMESPEC       Spec;
            char             sz[80];
            Log2(("NTFS:             iCreationTime      %#RX64 %s\n", RT_LE2H_U64(pFilename->iCreationTime),
                  RTTimeSpecToString(RTTimeSpecSetNtTime(&Spec, RT_LE2H_U64(pFilename->iCreationTime)), sz, sizeof(sz)) ));
            Log2(("NTFS:             iLastDataModTime   %#RX64 %s\n", RT_LE2H_U64(pFilename->iLastDataModTime),
                  RTTimeSpecToString(RTTimeSpecSetNtTime(&Spec, RT_LE2H_U64(pFilename->iLastDataModTime)), sz, sizeof(sz)) ));
            Log2(("NTFS:             iLastMftModTime    %#RX64 %s\n", RT_LE2H_U64(pFilename->iLastMftModTime),
                  RTTimeSpecToString(RTTimeSpecSetNtTime(&Spec, RT_LE2H_U64(pFilename->iLastMftModTime)), sz, sizeof(sz)) ));
            Log2(("NTFS:             iLastAccessTime    %#RX64 %s\n", RT_LE2H_U64(pFilename->iLastAccessTime),
                  RTTimeSpecToString(RTTimeSpecSetNtTime(&Spec, RT_LE2H_U64(pFilename->iLastAccessTime)), sz, sizeof(sz)) ));
            Log2(("NTFS:             cbAllocated        %#RX64 (%Rhcb)\n",
                  RT_LE2H_U64(pFilename->cbAllocated), RT_LE2H_U64(pFilename->cbAllocated)));
            Log2(("NTFS:             cbData             %#RX64 (%Rhcb)\n",
                  RT_LE2H_U64(pFilename->cbData), RT_LE2H_U64(pFilename->cbData)));
            Log2(("NTFS:             fFileAttribs       %#RX32\n", RT_LE2H_U32(pFilename->fFileAttribs) ));
            if (RT_LE2H_U32(pFilename->fFileAttribs) & NTFS_FA_REPARSE_POINT)
                Log2(("NTFS:             uReparseTag        %#RX32\n", RT_LE2H_U32(pFilename->u.uReparseTag) ));
            else
                Log2(("NTFS:             cbPackedEas        %#RX16\n", RT_LE2H_U16(pFilename->u.cbPackedEas) ));
            Log2(("NTFS:             cwcFilename        %#x\n", pFilename->cwcFilename));
            Log2(("NTFS:             fFilenameType      %#x\n", pFilename->fFilenameType));
            if (RT_UOFFSETOF_DYN(NTFSATFILENAME, wszFilename[pFilename->cwcFilename]) <= RT_LE2H_U16(pEntryHdr->cbKey))
                Log2(("NTFS:             wszFilename       '%.*ls'\n", pFilename->cwcFilename, pFilename->wszFilename ));
            else
                Log2(("NTFS:             Error! Truncated filename!!\n"));
        }


        /* next */
        iEntry++;
        offCurEntry += RT_LE2H_U16(pEntryHdr->cbEntry);
        fSeenEnd = RT_BOOL(pEntryHdr->fFlags & NTFSIDXENTRYHDR_F_END);
        if (fSeenEnd || RT_LE2H_U16(pEntryHdr->cbEntry) < sizeof(*pEntryHdr))
            break;
    }
    if (!fSeenEnd)
        Log2(("NTFS: %s: Warning! Missing NTFSIDXENTRYHDR_F_END node!\n", pszPrefix));
}

# if 0 /* unused */
static void rtFsNtfsVol_LogIndexNode(PCNTFSATINDEXALLOC pIdxNode, uint32_t cbIdxNode, uint32_t uType)
{
    if (!LogIs2Enabled())
        return;
    if (cbIdxNode < sizeof(*pIdxNode))
        Log2(("NTFS: Index Node: Error! Too small! cbIdxNode=%#x, index node needs %#x\n", cbIdxNode, sizeof(*pIdxNode)));
    else
    {
        Log2(("NTFS: Index Node:            uMagic %#x\n", RT_LE2H_U32(pIdxNode->RecHdr.uMagic)));
        Log2(("NTFS: Index Node:    UpdateSeqArray %#x L %#x\n",
              RT_LE2H_U16(pIdxNode->RecHdr.offUpdateSeqArray), RT_LE2H_U16(pIdxNode->RecHdr.cUpdateSeqEntries) ));
        Log2(("NTFS: Index Node:              uLsn %#RX64\n", RT_LE2H_U64(pIdxNode->uLsn) ));
        Log2(("NTFS: Index Node:      iSelfAddress %#RX64\n", RT_LE2H_U64(pIdxNode->iSelfAddress) ));
        if (pIdxNode->RecHdr.uMagic == NTFSREC_MAGIC_INDEX_ALLOC)
            rtFsNtfsVol_LogIndexHdrAndEntries(&pIdxNode->Hdr, cbIdxNode - RT_UOFFSETOF(NTFSATINDEXALLOC, Hdr),
                                              RT_UOFFSETOF(NTFSATINDEXALLOC, Hdr), "Index Node Hdr", uType);
        else
            Log2(("NTFS: Index Node: !Error! Invalid magic!\n"));
    }
}
# endif

/**
 * Logs a index root structure and what follows (index header + entries).
 *
 * @param   pIdxRoot    The index root.
 * @param   cbIdxRoot   Number of valid bytes starting with @a pIdxRoot.
 */
static void rtFsNtfsVol_LogIndexRoot(PCNTFSATINDEXROOT pIdxRoot, uint32_t cbIdxRoot)
{
    if (!LogIs2Enabled())
        return;
    if (cbIdxRoot < sizeof(*pIdxRoot))
        Log2(("NTFS: Index Root: Error! Too small! cbIndex=%#x, index head needs %#x\n", cbIdxRoot, sizeof(*pIdxRoot)));
    else
    {
        Log2(("NTFS: Index Root:              cbIdxRoot %#x\n", cbIdxRoot));
        Log2(("NTFS: Index Root:                  uType %#x %s\n", RT_LE2H_U32(pIdxRoot->uType),
              pIdxRoot->uType == NTFSATINDEXROOT_TYPE_VIEW ? "view"
              : pIdxRoot->uType == NTFSATINDEXROOT_TYPE_DIR ? "directory" : "!unknown!"));
        Log2(("NTFS: Index Root:        uCollationRules %#x %s\n", RT_LE2H_U32(pIdxRoot->uCollationRules),
              pIdxRoot->uCollationRules == NTFS_COLLATION_BINARY ? "binary"
              : pIdxRoot->uCollationRules == NTFS_COLLATION_FILENAME ? "filename"
              : pIdxRoot->uCollationRules == NTFS_COLLATION_UNICODE_STRING ? "unicode-string"
              : pIdxRoot->uCollationRules == NTFS_COLLATION_UINT32 ? "uint32"
              : pIdxRoot->uCollationRules == NTFS_COLLATION_SID ? "sid"
              : pIdxRoot->uCollationRules == NTFS_COLLATION_UINT32_PAIR ? "uint32-pair"
              : pIdxRoot->uCollationRules == NTFS_COLLATION_UINT32_SEQ ? "uint32-sequence" : "!unknown!"));
        Log2(("NTFS: Index Root:            cbIndexNode %#x\n", RT_LE2H_U32(pIdxRoot->cbIndexNode) ));
        Log2(("NTFS: Index Root: cAddressesPerIndexNode %#x => cbNodeAddressingUnit=%#x\n",
              pIdxRoot->cAddressesPerIndexNode, RT_LE2H_U32(pIdxRoot->cbIndexNode) / RT_MAX(1, pIdxRoot->cAddressesPerIndexNode) ));
        if (pIdxRoot->abReserved[0]) Log2(("NTFS: Index Root:          abReserved[0] %#x\n", pIdxRoot->abReserved[0]));
        if (pIdxRoot->abReserved[1]) Log2(("NTFS: Index Root:          abReserved[1] %#x\n", pIdxRoot->abReserved[1]));
        if (pIdxRoot->abReserved[2]) Log2(("NTFS: Index Root:          abReserved[2] %#x\n", pIdxRoot->abReserved[2]));

        rtFsNtfsVol_LogIndexHdrAndEntries(&pIdxRoot->Hdr, cbIdxRoot - RT_UOFFSETOF(NTFSATINDEXROOT, Hdr),
                                          RT_UOFFSETOF(NTFSATINDEXROOT, Hdr), "Index Root Hdr", pIdxRoot->uType);
    }
}

#endif /* LOG_ENABLED */


/**
 * Validates an index header.
 *
 * @returns IPRT status code.
 * @param   pRootInfo           Pointer to the index root info.
 * @param   pNodeInfo           Pointer to the node info structure to load.
 * @param   pIndexHdr           Pointer to the index header.
 * @param   cbIndex             Size of the index.
 * @param   pErrInfo            Where to return extra error info.
 * @param   pszWhat             Error prefix.
 */
static int rtFsNtfsVol_LoadIndexNodeInfo(PCRTFSNTFSIDXROOTINFO pRootInfo, PRTFSNTFSIDXNODEINFO pNodeInfo, PCNTFSINDEXHDR pIndexHdr,
                                         uint32_t cbIndex, PRTERRINFO pErrInfo, const char *pszWhat)
{
    uint32_t const cbMinIndex = sizeof(*pIndexHdr) + sizeof(NTFSIDXENTRYHDR);
    if (cbIndex < cbMinIndex)
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "%s: Not enough room for the index header and one entry header! cbIndex=%#x (cbMinIndex=%#x)",
                                       pszWhat, cbIndex, cbMinIndex);
    uint32_t const cbAllocated = RT_LE2H_U32(pIndexHdr->cbAllocated);
    if (   cbAllocated > cbIndex
        || cbAllocated < cbMinIndex
        || (cbAllocated & 7) )
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "%s: Bogus index allocation size: %#x (min %#x, max %#x, 8 byte aligned)",
                                       pszWhat, cbAllocated, cbMinIndex, cbIndex);
    uint32_t const cbUsed = RT_LE2H_U32(pIndexHdr->cbUsed);
    if (   cbUsed > cbAllocated
        || cbUsed < cbMinIndex
        || (cbUsed & 7) )
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "%s: Bogus index used size: %#x (min %#x, max %#x, 8 byte aligned)",
                                       pszWhat, cbUsed, cbMinIndex, cbAllocated);
    uint32_t const offFirstEntry = RT_LE2H_U32(pIndexHdr->offFirstEntry);
    if (   offFirstEntry < sizeof(*pIndexHdr)
        || (   offFirstEntry > cbUsed - sizeof(NTFSIDXENTRYHDR)
            && offFirstEntry != cbUsed /* empty dir */)
        || (offFirstEntry & 7) )
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "%s: Bogus first entry offset: %#x (min %#x, max %#x, 8 byte aligned)",
                                       pszWhat, offFirstEntry, sizeof(*pIndexHdr), cbUsed - sizeof(NTFSIDXENTRYHDR));

    /*
     * The index entries.
     */
    uint32_t const uType = pRootInfo->pRoot->uType;
    uint32_t offEntry = offFirstEntry;
    uint32_t iEntry = 0;
    for (;;)
    {
        if (offEntry + sizeof(NTFSIDXENTRYHDR) > cbUsed)
            return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                           "%s: Entry #%u is out of bound: offset %#x (cbUsed=%#x)",
                                           pszWhat, iEntry, offEntry, cbUsed);
        PCNTFSIDXENTRYHDR pEntryHdr     = (PCNTFSIDXENTRYHDR)((uint8_t const *)pIndexHdr + offEntry);
        uint16_t const    cbEntry       = RT_LE2H_U16(pEntryHdr->cbEntry);
        uint32_t const    cbSubnodeAddr = (pEntryHdr->fFlags & NTFSIDXENTRYHDR_F_INTERNAL ? sizeof(int64_t) : 0);
        uint32_t const    cbMinEntry    = sizeof(*pEntryHdr) + cbSubnodeAddr;
        if (   cbEntry < cbMinEntry
            || offEntry + cbEntry > cbUsed
            || (cbEntry & 7) )
            return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                           "%s: Entry #%u has a bogus size: %#x (min %#x, max %#x, 8 byte aligned)",
                                           pszWhat, iEntry, cbEntry, cbMinEntry, cbUsed - offEntry);

        uint32_t const cbMaxKey = cbEntry - sizeof(*pEntryHdr) - cbSubnodeAddr;
        uint32_t const cbMinKey = (pEntryHdr->fFlags & NTFSIDXENTRYHDR_F_END) ? 0
                                : uType == NTFSATINDEXROOT_TYPE_DIR ? RT_UOFFSETOF(NTFSATFILENAME, wszFilename) : 0;
        uint16_t const cbKey    = RT_LE2H_U16(pEntryHdr->cbKey);
        if (   cbKey < cbMinKey
            || cbKey > cbMaxKey)
            return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                           "%s: Entry #%u has a bogus key size: %#x (min %#x, max %#x)",
                                           pszWhat, iEntry, cbKey, cbMinKey, cbMaxKey);
        if (   !(pEntryHdr->fFlags & NTFSIDXENTRYHDR_F_END)
            && uType == NTFSATINDEXROOT_TYPE_DIR)
        {
            PCNTFSATFILENAME pFilename = (PCNTFSATFILENAME)(pEntryHdr + 1);
            if (RT_UOFFSETOF_DYN(NTFSATFILENAME, wszFilename[pFilename->cwcFilename]) > cbKey)
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "%s: Entry #%u filename is out of bounds: cwcFilename=%#x -> %#x key, max %#x",
                                               pszWhat, iEntry, pFilename->cwcFilename,
                                               RT_UOFFSETOF_DYN(NTFSATFILENAME, wszFilename[pFilename->cwcFilename]), cbKey);
        }

        if (pEntryHdr->fFlags & NTFSIDXENTRYHDR_F_INTERNAL)
        {
            int64_t iSubnode = NTFSIDXENTRYHDR_GET_SUBNODE(pEntryHdr);
            if (   (uint64_t)iSubnode >= pRootInfo->uEndNodeAddresses
                || (iSubnode & pRootInfo->fNodeAddressMisalign) )
                return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "%s: Entry #%u has bogus subnode address: %#RX64 (max %#RX64, misalign %#x)",
                                               pszWhat, iEntry, iSubnode, pRootInfo->uEndNodeAddresses,
                                               pRootInfo->fNodeAddressMisalign);
        }

        /* Advance. */
        offEntry += cbEntry;
        iEntry++;
        if (pEntryHdr->fFlags & NTFSIDXENTRYHDR_F_END)
            break;
    }

    /*
     * Popuplate the node info structure.
     */
    pNodeInfo->pIndexHdr  = pIndexHdr;
    pNodeInfo->fInternal  = RT_BOOL(pIndexHdr->fFlags & NTFSINDEXHDR_F_INTERNAL);
    if (pNodeInfo != &pRootInfo->NodeInfo)
        pNodeInfo->pVol   = pRootInfo->NodeInfo.pVol;
    pNodeInfo->cEntries   = iEntry;
    pNodeInfo->papEntries = (PCNTFSIDXENTRYHDR *)RTMemAlloc(iEntry * sizeof(pNodeInfo->papEntries[0]));
    if (pNodeInfo->papEntries)
    {
        PCNTFSIDXENTRYHDR pEntryHdr = NTFSINDEXHDR_GET_FIRST_ENTRY(pIndexHdr);
        for (iEntry = 0; iEntry < pNodeInfo->cEntries; iEntry++)
        {
            pNodeInfo->papEntries[iEntry] = pEntryHdr;
            pEntryHdr = NTFSIDXENTRYHDR_GET_NEXT(pEntryHdr);
        }
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}


/**
 * Creates a shared directory structure given a MFT core.
 *
 * @returns IPRT status code.
 * @param   pThis       The NTFS volume instance.
 * @param   pCore       The MFT core structure that's allegedly a directory.
 *                      (No reference consumed of course.)
 * @param   ppSharedDir Where to return the pointer to the new shared directory
 *                      structure on success. (Referenced.)
 * @param   pErrInfo    Where to return additions error info.  Optional.
 * @param   pszWhat     Context prefix for error reporting and logging.
 */
static int rtFsNtfsVol_NewSharedDirFromCore(PRTFSNTFSVOL pThis, PRTFSNTFSCORE pCore, PRTFSNTFSDIRSHRD *ppSharedDir,
                                            PRTERRINFO pErrInfo, const char *pszWhat)
{
    *ppSharedDir = NULL;

    /*
     * Look for the index root and validate it.
     */
    PRTFSNTFSATTR pRootAttr = rtFsNtfsCore_FindNamedAttributeAscii(pCore, NTFS_AT_INDEX_ROOT,
                                                                   RT_STR_TUPLE(NTFS_DIR_ATTRIBUTE_NAME));
    if (!pRootAttr)
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "%s: Found no INDEX_ROOT attribute named $I30", pszWhat);
    if (pRootAttr->pAttrHdr->fNonResident)
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "%s: INDEX_ROOT is is not resident", pszWhat);
    if (pRootAttr->cbResident < sizeof(NTFSATINDEXROOT))
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "%s: INDEX_ROOT is too small: %#x, min %#x ",
                                       pszWhat, pRootAttr->cbResident, sizeof(pRootAttr->cbResident));

    PCNTFSATINDEXROOT pIdxRoot = (PCNTFSATINDEXROOT)NTFSATTRIBHDR_GET_RES_VALUE_PTR(pRootAttr->pAttrHdr);
#ifdef LOG_ENABLED
    rtFsNtfsVol_LogIndexRoot(pIdxRoot, pRootAttr->cbResident);
#endif
    if (pIdxRoot->uType != NTFSATINDEXROOT_TYPE_DIR)
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "%s: Wrong INDEX_ROOT type for a directory: %#x, expected %#x",
                                       pszWhat, RT_LE2H_U32(pIdxRoot->uType), RT_LE2H_U32_C(NTFSATINDEXROOT_TYPE_DIR));
    if (pIdxRoot->uCollationRules != NTFS_COLLATION_FILENAME)
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "%s: Wrong collation rules for a directory: %#x, expected %#x",
                                       pszWhat, RT_LE2H_U32(pIdxRoot->uCollationRules), RT_LE2H_U32_C(NTFS_COLLATION_FILENAME));
    uint32_t cbIndexNode = RT_LE2H_U32(pIdxRoot->cbIndexNode);
    if (cbIndexNode < 512 || cbIndexNode > _64K || !RT_IS_POWER_OF_TWO(cbIndexNode))
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "%s: Bogus index node size: %#x (expected power of two between 512 and 64KB)",
                                       pszWhat, cbIndexNode);
    unsigned const cNodeAddressShift = cbIndexNode >= pThis->cbCluster ? pThis->cClusterShift : 9;
    if (((uint32_t)pIdxRoot->cAddressesPerIndexNode << cNodeAddressShift) != cbIndexNode)
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "%s: Bogus addresses per index node value: %#x (cbIndexNode=%#x cNodeAddressShift=%#x)",
                                       pszWhat, pIdxRoot->cAddressesPerIndexNode, cbIndexNode, cNodeAddressShift);
    AssertReturn(pRootAttr->uObj.pSharedDir == NULL, VERR_INTERNAL_ERROR_3);

    /*
     * Check for the node data stream and related allocation bitmap.
     */
    PRTFSNTFSATTR pIndexAlloc  = rtFsNtfsCore_FindNamedAttributeAscii(pCore, NTFS_AT_INDEX_ALLOCATION,
                                                                      RT_STR_TUPLE(NTFS_DIR_ATTRIBUTE_NAME));
    PRTFSNTFSATTR pIndexBitmap = rtFsNtfsCore_FindNamedAttributeAscii(pCore, NTFS_AT_BITMAP,
                                                                      RT_STR_TUPLE(NTFS_DIR_ATTRIBUTE_NAME));
    if (pIndexAlloc && !pIndexBitmap)
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "%s: INDEX_ALLOCATION attribute without BITMAP", pszWhat);
    if (!pIndexAlloc && pIndexBitmap)
        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "%s: BITMAP attribute without INDEX_ALLOCATION", pszWhat);
    uint64_t uNodeAddressEnd = 0;
    if (pIndexAlloc)
    {
        if (!pIndexAlloc->pAttrHdr->fNonResident)
            return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "%s: INDEX_ALLOCATION is resident", pszWhat);
        if (pIndexAlloc->cbValue & (cbIndexNode - 1))
            return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                           "%s: INDEX_ALLOCATION size isn't aligned on node boundrary: %#RX64, cbIndexNode=%#x",
                                           pszWhat, pIndexAlloc->cbValue, cbIndexNode);
        uint64_t const cNodes = pIndexAlloc->cbValue / cbIndexNode;
        if (pIndexBitmap->cbValue < (RT_ALIGN_64(cNodes, 64) >> 3))
            return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                           "%s: BITMAP size does not match INDEX_ALLOCATION: %#RX64, expected min %#RX64 (cbIndexNode=%#x, cNodes=%#RX64)",
                                           pszWhat, pIndexBitmap->cbValue, RT_ALIGN_64(cNodes, 64) >> 3, cbIndexNode, cNodes);
        uNodeAddressEnd = cNodes * pIdxRoot->cAddressesPerIndexNode;
    }

    /*
     * Create a directory instance.
     */
    PRTFSNTFSDIRSHRD pNewDir = (PRTFSNTFSDIRSHRD)RTMemAllocZ(sizeof(*pNewDir));
    if (!pNewDir)
        return VERR_NO_MEMORY;

    pNewDir->cRefs = 1;
    rtFsNtfsCore_Retain(pCore);
    pNewDir->RootInfo.pRootAttr             = pRootAttr;
    pNewDir->RootInfo.pRoot                 = pIdxRoot;
    pNewDir->RootInfo.pAlloc                = pIndexAlloc;
    pNewDir->RootInfo.uEndNodeAddresses     = uNodeAddressEnd;
    pNewDir->RootInfo.cNodeAddressByteShift = cNodeAddressShift;
    pNewDir->RootInfo.fNodeAddressMisalign  = pIdxRoot->cAddressesPerIndexNode - 1;
    pNewDir->RootInfo.NodeInfo.pVol         = pThis;

    /*
     * Finally validate the index header and entries.
     */
    int rc = rtFsNtfsVol_LoadIndexNodeInfo(&pNewDir->RootInfo, &pNewDir->RootInfo.NodeInfo, &pIdxRoot->Hdr,
                                           pRootAttr->cbResident - RT_UOFFSETOF(NTFSATINDEXROOT, Hdr), pErrInfo, pszWhat);
    if (RT_SUCCESS(rc))
    {
        *ppSharedDir = pNewDir;
        pRootAttr->uObj.pSharedDir = pNewDir;
        return VINF_SUCCESS;
    }
    RTMemFree(pNewDir);
    rtFsNtfsCore_Release(pCore);
    return rc;
}


/**
 * Gets a shared directory structure given an MFT record reference, creating a
 * new one if necessary.
 *
 * @returns IPRT status code.
 * @param   pThis               The NTFS volume instance.
 * @param   pDirMftRef          The MFT record reference to follow.
 * @param   ppSharedDir         Where to return the shared directory structure
 *                              (referenced).
 * @param   pErrInfo            Where to return error details. Optional.
 * @param   pszWhat             Error/log prefix.
 */
static int rtFsNtfsVol_QueryOrCreateSharedDirByMftRef(PRTFSNTFSVOL pThis, PCNTFSMFTREF pDirMftRef,
                                                      PRTFSNTFSDIRSHRD *ppSharedDir, PRTERRINFO pErrInfo, const char *pszWhat)
{
    /*
     * Get the core structure for the MFT record and check that it's a directory we've got.
     */
    PRTFSNTFSCORE pCore;
    int rc = rtFsNtfsVol_QueryCoreForMftRef(pThis, pDirMftRef, false /*fRelaxedUsa*/, &pCore, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        if (pCore->pMftRec->pFileRec->fFlags & NTFSRECFILE_F_DIRECTORY)
        {
            /*
             * Locate the $I30 root index attribute as we associate the
             * pointer to the shared directory pointer with it.
             */
            PRTFSNTFSATTR pRootAttr = rtFsNtfsCore_FindNamedAttributeAscii(pCore, NTFS_AT_INDEX_ROOT,
                                                                           RT_STR_TUPLE(NTFS_DIR_ATTRIBUTE_NAME));
            if (pRootAttr)
            {
                if (!pRootAttr->uObj.pSharedDir)
                    rc = rtFsNtfsVol_NewSharedDirFromCore(pThis, pCore, ppSharedDir, pErrInfo, pszWhat);
                else
                {
                    Assert(pRootAttr->uObj.pSharedDir->RootInfo.pRootAttr->pCore == pCore);
                    rtFsNtfsDirShrd_Retain(pRootAttr->uObj.pSharedDir);
                    *ppSharedDir = pRootAttr->uObj.pSharedDir;
                }
            }
            else
                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_NOT_A_DIRECTORY,
                                             "%s: Found INDEX_ROOT attribute named $I30, even though NTFSRECFILE_F_DIRECTORY is set",
                                             pszWhat);
        }
        else
            rc = RTERRINFO_LOG_SET_F(pErrInfo, VERR_NOT_A_DIRECTORY, "%s: fFlags=%#x", pszWhat, pCore->pMftRec->pFileRec->fFlags);
        rtFsNtfsCore_Release(pCore);
    }
    return rc;
}


/**
 * Frees resource kept by an index node info structure.
 *
 * @param   pNodeInfo           The index node info structure to delelte.
 */
static void rtFsNtfsIdxNodeInfo_Delete(PRTFSNTFSIDXNODEINFO pNodeInfo)
{
    RTMemFree(pNodeInfo->papEntries);
    pNodeInfo->papEntries = NULL;
    pNodeInfo->pNode = NULL;
    pNodeInfo->pVol  = NULL;
}


/**
 * Gets or loads the specified subnode.
 *
 * @returns IPRT status code.
 * @param   pRootInfo   The index root info.
 * @param   iNode       The address of the node being queried.
 * @param   ppNode      Where to return the referenced pointer to the node.
 */
static int rtFsNtfsIdxRootInfo_QueryNode(PRTFSNTFSIDXROOTINFO pRootInfo, int64_t iNode, PRTFSNTFSIDXNODE *ppNode)
{
    PRTFSNTFSVOL pVol = pRootInfo->NodeInfo.pVol;

    /*
     * A bit of paranoia.  These has been checked already when loading, but it
     * usually doesn't hurt too much to be careful.
     */
    AssertReturn(!(iNode & pRootInfo->fNodeAddressMisalign), VERR_VFS_BOGUS_OFFSET);
    AssertReturn((uint64_t)iNode < pRootInfo->uEndNodeAddresses, VERR_VFS_BOGUS_OFFSET);
    AssertReturn(pRootInfo->pAlloc, VERR_VFS_BOGUS_OFFSET);

    /*
     * First translate the node address to a disk byte offset and check the index node cache.
     */
    uint64_t offNode = iNode << pRootInfo->cNodeAddressByteShift;
    uint64_t offNodeOnDisk = rtFsNtfsAttr_OffsetToDisk(pRootInfo->pAlloc, offNode, NULL);
    PRTFSNTFSIDXNODE pNode = (PRTFSNTFSIDXNODE)RTAvlU64Get(&pVol->IdxNodeCacheRoot, offNodeOnDisk);
    if (pNode)
    {
        rtFsNtfsIdxNode_Retain(pNode);
        *ppNode = pNode;
        return VINF_SUCCESS;
    }

    /*
     * Need to create a load a new node.
     */
    pNode = (PRTFSNTFSIDXNODE)RTMemAllocZ(sizeof(*pNode));
    AssertReturn(pNode, VERR_NO_MEMORY);

    pNode->TreeNode.Key  = offNodeOnDisk;
    uint32_t cbIndexNode = RT_LE2H_U32(pRootInfo->pRoot->cbIndexNode);
    pNode->cbCost        = sizeof(*pNode) + cbIndexNode;
    pNode->cRefs         = 1;
    pNode->pNode         = (PNTFSATINDEXALLOC)RTMemAllocZ(cbIndexNode);
    int rc;
    if (pNode->pNode)
    {
        rc = rtFsNtfsAttr_Read(pRootInfo->pAlloc, offNode, pNode->pNode, cbIndexNode);
        if (RT_SUCCESS(rc))
        {
            rc = VERR_VFS_BOGUS_FORMAT;
            if (pNode->pNode->RecHdr.uMagic != NTFSREC_MAGIC_INDEX_ALLOC)
                LogRel(("rtFsNtfsIdxRootInfo_QueryNode(iNode=%#x): Invalid node magic %#x -> VERR_VFS_BOGUS_FORMAT\n",
                        iNode, RT_LE2H_U32(pNode->pNode->RecHdr.uMagic) ));
            else if ((int64_t)RT_LE2H_U64(pNode->pNode->iSelfAddress) != iNode)
                LogRel(("rtFsNtfsIdxRootInfo_QueryNode(iNode=%#x): Wrong iSelfAddress: %#x -> VERR_VFS_BOGUS_FORMAT\n",
                        iNode, RT_LE2H_U64(pNode->pNode->iSelfAddress) ));
            else
            {
                rc = rtFsNtfsRec_DoMultiSectorFixups(&pNode->pNode->RecHdr, cbIndexNode, false /*fRelaxedUsa*/, NULL /*pErrInfo*/);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Validate/parse it
                     */
#ifdef LOG_ENABLED
                    rtFsNtfsVol_LogIndexHdrAndEntries(&pNode->pNode->Hdr,
                                                      cbIndexNode - RT_UOFFSETOF(NTFSATINDEXALLOC, Hdr),
                                                      RT_UOFFSETOF(NTFSATINDEXALLOC, Hdr), "index node",
                                                      pRootInfo->pRoot->uType);
#endif
                    rc = rtFsNtfsVol_LoadIndexNodeInfo(pRootInfo, &pNode->NodeInfo, &pNode->pNode->Hdr,
                                                       cbIndexNode - RT_UOFFSETOF(NTFSATINDEXALLOC, Hdr),
                                                       NULL /*pErrInfo*/, "index node");
                    if (RT_SUCCESS(rc))
                    {
                        pNode->cbCost += pNode->NodeInfo.cEntries * sizeof(pNode->NodeInfo.papEntries[0]);

                        /*
                         * Insert it into the cache, trimming the cache if necessary.
                         */
                        bool fInsertOkay = RTAvlU64Insert(&pVol->IdxNodeCacheRoot, &pNode->TreeNode);
                        Assert(fInsertOkay);
                        if (fInsertOkay)
                        {
                            pVol->cIdxNodes  += 1;
                            pVol->cbIdxNodes += pNode->cbCost;
                            if (pVol->cbIdxNodes > RTFSNTFS_MAX_CORE_CACHE_SIZE)
                                rtFsNtfsIdxVol_TrimIndexNodeCache(pVol);

                            *ppNode = pNode;
                            return VINF_SUCCESS;
                        }
                    }
                }
            }
        }

        RTMemFree(pNode->pNode);
        pNode->pNode = NULL;
    }
    else
        rc = VERR_NO_MEMORY;
    RTMemFree(pNode);
    return rc;
}


/**
 * Frees resource kept by an index root info structure.
 *
 * @param   pRootInfo           The index root info structure to delete.
 */
static void rtFsNtfsIdxRootInfo_Delete(PRTFSNTFSIDXROOTINFO pRootInfo)
{
    rtFsNtfsIdxNodeInfo_Delete(&pRootInfo->NodeInfo);
    pRootInfo->pRootAttr->uObj.pSharedDir = NULL;
    rtFsNtfsCore_Release(pRootInfo->pRootAttr->pCore);
    pRootInfo->pRootAttr = NULL;
    pRootInfo->pAlloc    = NULL;
    pRootInfo->pRoot     = NULL;
}


/**
 * Destroys a shared directory structure when the reference count reached zero.
 *
 * @returns zero
 * @param   pThis               The shared directory structure to destroy.
 */
static uint32_t rtFsNtfsDirShrd_Destroy(PRTFSNTFSDIRSHRD pThis)
{
    rtFsNtfsIdxRootInfo_Delete(&pThis->RootInfo);
    RTMemFree(pThis);
    return 0;
}


/**
 * Releases a references to a shared directory structure.
 *
 * @returns New reference count.
 * @param   pThis               The shared directory structure.
 */
static uint32_t rtFsNtfsDirShrd_Release(PRTFSNTFSDIRSHRD pThis)
{
    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    Assert(cRefs < 4096);
    if (cRefs > 0)
        return cRefs;
    return rtFsNtfsDirShrd_Destroy(pThis);
}


/**
 * Retains a references to a shared directory structure.
 *
 * @returns New reference count.
 * @param   pThis               The shared directory structure.
 */
static uint32_t rtFsNtfsDirShrd_Retain(PRTFSNTFSDIRSHRD pThis)
{
    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs > 1);
    Assert(cRefs < 4096);
    return cRefs;
}


/**
 * Compares the two filenames in an case insentivie manner.
 *
 * @retval  -1 if the first filename comes first
 * @retval  0 if equal
 * @retval  1 if the second filename comes first.
 *
 * @param   pwszUpper1      The first filename, this has been uppercase already.
 * @param   cwcUpper1       The length of the first filename.
 * @param   pawcFilename2   The second filename to compare it with.  Not zero
 *                          terminated.
 * @param   cwcFilename2    The length of the second filename.
 * @param   pawcUpcase      The uppercase table. 64K entries.
 */
static int rtFsNtfsIdxComp_Filename(PCRTUTF16 pwszUpper1, uint8_t cwcUpper1, PCRTUTF16 pawcFilename2, uint8_t cwcFilename2,
                                    PCRTUTF16 const pawcUpcase)
{
    while (cwcUpper1 > 0 && cwcFilename2 > 0)
    {
        RTUTF16 uc1 = *pwszUpper1++;
        RTUTF16 uc2 = *pawcFilename2++;
        if (uc1 != uc2)
        {
            uc2 = pawcUpcase[uc2];
            if (uc1 != uc2)
                return uc1 < uc2 ? -1 : 1;
        }

        /* Decrement the lengths and loop. */
        cwcUpper1--;
        cwcFilename2--;
    }

    if (!cwcUpper1)
    {
        if (!cwcFilename2)
            return 0;
        return -1;
    }
    return 1;
}


/**
 * Look up a name in the directory.
 *
 * @returns IPRT status code.
 * @param   pShared     The shared directory structure.
 * @param   pszEntry    The name to lookup.
 * @param   ppFilename  Where to return the pointer to the filename structure.
 * @param   ppEntryHdr  Where to return the poitner to the entry header
 *                      structure.
 * @param   ppNode      Where to return the pointer to the node the filename
 *                      structure resides in.  This must be released.  It will
 *                      be set to NULL if the name was found in the root node.
 */
static int rtFsNtfsDirShrd_Lookup(PRTFSNTFSDIRSHRD pShared, const char *pszEntry,
                                  PCNTFSATFILENAME *ppFilename, PCNTFSIDXENTRYHDR *ppEntryHdr, PRTFSNTFSIDXNODE *ppNode)
{
    PRTFSNTFSVOL    pVol = pShared->RootInfo.NodeInfo.pVol;

    *ppFilename = NULL;
    *ppEntryHdr = NULL;
    *ppNode     = NULL;
    /** @todo do streams (split on ':') */

    /*
     * Convert the filename to UTF16 and uppercase.
     */
    PCRTUTF16 const pawcUpcase = pVol->pawcUpcase;
    RTUTF16         wszFilename[256+4];
    PRTUTF16        pwszDst = wszFilename;
    PRTUTF16        pwszEnd = &wszFilename[255];
    const char     *pszSrc = pszEntry;
    for (;;)
    {
        RTUNICP uc;
        int rc = RTStrGetCpEx(&pszSrc, &uc);
        if (RT_SUCCESS(rc))
        {
            if (uc != 0)
            {
                if (uc < _64K)
                    uc = pawcUpcase[uc];
                pwszDst = RTUtf16PutCp(pwszDst, uc);
                if ((uintptr_t)pwszDst <= (uintptr_t)pwszEnd)
                { /* likely */ }
                else
                {
                    Log(("rtFsNtfsDirShrd_Lookup: Filename too long '%s'\n", pszEntry));
                    return VERR_FILENAME_TOO_LONG;
                }
            }
            else
            {
                *pwszDst = '\0';
                break;
            }
        }
        else
        {
            Log(("rtFsNtfsDirShrd_Lookup: Invalid UTF-8 encoding (%Rrc): %.*Rhxs\n", rc, strlen(pszEntry), pszEntry));
            return rc;
        }
    }
    uint8_t const cwcFilename = (uint8_t)(pwszDst - wszFilename);

    /*
     * Do the tree traversal.
     */
    PRTFSNTFSIDXROOTINFO pRootInfo = &pShared->RootInfo;
    PRTFSNTFSIDXNODEINFO pNodeInfo = &pRootInfo->NodeInfo;
    PRTFSNTFSIDXNODE     pNode     = NULL;
    for (;;)
    {
        /*
         * Search it.
         */
        PCNTFSIDXENTRYHDR  *papEntries = pNodeInfo->papEntries;
        uint32_t            iEnd       = pNodeInfo->cEntries;
        AssertReturn(iEnd > 0, VERR_INTERNAL_ERROR_3);

        /* Exclude the end node from the serach as it doesn't have any key. */
        if (papEntries[iEnd - 1]->fFlags & NTFSIDXENTRYHDR_F_END)
            iEnd--;

        uint32_t iEntry;
        if (1 /*iEnd < 8*/ )
        {
            if (iEnd > 0)
            {
                for (iEntry = 0; iEntry < iEnd; iEntry++)
                {
                    PCNTFSATFILENAME pFilename = (PCNTFSATFILENAME)(papEntries[iEntry] + 1);
                    int iDiff = rtFsNtfsIdxComp_Filename(wszFilename, cwcFilename, pFilename->wszFilename,
                                                         pFilename->cwcFilename, pawcUpcase);
                    if (iDiff > 0)
                    { /* likely */ }
                    else if (iDiff == 0)
                    {
                        *ppNode     = pNode;
                        *ppEntryHdr = papEntries[iEntry];
                        *ppFilename = pFilename;
                        LogFlow(("rtFsNtfsDirShrd_Lookup(%s): Found it! (iEntry=%u, FileMftRec=%#RX64 sqn %#x)\n",
                                 pszEntry, iEntry, NTFSMFTREF_GET_IDX(&papEntries[iEntry]->u.FileMftRec),
                                 NTFSMFTREF_GET_SEQ(&papEntries[iEntry]->u.FileMftRec) ));
                        return VINF_SUCCESS;
                    }
                    else
                        break;
                }
            }
            else
                iEntry = iEnd;
        }
        /* else: implement binary search */

        /*
         * Decend thru node iEntry.
         *
         * We could be bold and ASSUME that there is always an END node, but we're
         * playing safe for now.
         */
        if (iEnd < pNodeInfo->cEntries)
        {
            PCNTFSIDXENTRYHDR pEntry = papEntries[iEntry];
            if (pEntry->fFlags & NTFSIDXENTRYHDR_F_INTERNAL)
            {
                int64_t iSubnode = NTFSIDXENTRYHDR_GET_SUBNODE(pEntry);
                rtFsNtfsIdxNode_Release(pNode);
                int rc = rtFsNtfsIdxRootInfo_QueryNode(pRootInfo, iSubnode, &pNode);
                if (RT_SUCCESS(rc))
                {
                    pNodeInfo = &pNode->NodeInfo;
                    continue;
                }
                LogFlow(("rtFsNtfsDirShrd_Lookup(%s): rtFsNtfsIdxRootInfo_QueryNode(%#RX64) error %Rrc!\n",
                         pszEntry, iSubnode, rc));
                return rc;
            }
        }
        rtFsNtfsIdxNode_Release(pNode);
        LogFlow(("rtFsNtfsDirShrd_Lookup(%s): Not found! (#2)\n", pszEntry));
        return VERR_FILE_NOT_FOUND;
    }

    /* not reached */
}


/**
 * Gets the shared directory structure for the parent.
 *
 * @returns IPRT status code.
 * @param   pThis       The directory which parent we want.
 * @param   ppDotDot    Where to return the referenced shared parent dir
 *                      structure.
 *
 */
static int rtFsNtfsDirShrd_QueryParent(PRTFSNTFSDIRSHRD pThis, PRTFSNTFSDIRSHRD *ppDotDot)
{
    /*
     * The root directory has no parent from our perspective.
     */
    if (pThis == pThis->RootInfo.NodeInfo.pVol->pRootDir)
    {
        rtFsNtfsDirShrd_Retain(pThis);
        *ppDotDot = pThis;
        return VINF_SUCCESS;
    }

    /*
     * Look for a filename record so we know where we go from here.
     */
    PRTFSNTFSCORE pCore = pThis->RootInfo.pRootAttr->pCore;
    PRTFSNTFSATTR pCurAttr;
    RTListForEach(&pCore->AttribHead, pCurAttr, RTFSNTFSATTR, ListEntry)
    {
        if (   pCurAttr->pAttrHdr->uAttrType == NTFS_AT_FILENAME
            && pCurAttr->cbResident >= RT_UOFFSETOF(NTFSATFILENAME, wszFilename))
        {
            PCNTFSATFILENAME pFilename = (PCNTFSATFILENAME)NTFSATTRIBHDR_GET_RES_VALUE_PTR(pCurAttr->pAttrHdr);
            int rc = rtFsNtfsVol_QueryOrCreateSharedDirByMftRef(pThis->RootInfo.NodeInfo.pVol, &pFilename->ParentDirMftRec,
                                                                ppDotDot, NULL /*pErrInfo*/, "..");
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;
            LogRel(("rtFsNtfsDirShrd_QueryParent: rtFsNtfsVol_QueryOrCreateSharedDirByMftRef failed: %Rrc\n", rc));
            return rc;
        }
    }

    LogRel(("rtFsNtfsDirShrd_QueryParent: Couldn't find '..' filename for MFT record %RX64!\n",
            pThis->RootInfo.pRootAttr->pCore->pMftRec->TreeNode.Key));
    return VERR_VFS_BOGUS_FORMAT;
}



/**
 * Destroys an index node.
 *
 * This will remove it from the cache tree, however the caller must make sure
 * its not in the reuse list any more.
 *
 * @param   pNode               The node to destroy.
 */
static void rtFsNtfsIdxNode_Destroy(PRTFSNTFSIDXNODE pNode)
{
    PRTFSNTFSVOL pVol = pNode->NodeInfo.pVol;

    /* Remove it from the volume node cache. */
    PAVLU64NODECORE pAssertRemove = RTAvlU64Remove(&pVol->IdxNodeCacheRoot, pNode->TreeNode.Key);
    Assert(pAssertRemove == &pNode->TreeNode); NOREF(pAssertRemove);
    pVol->cIdxNodes--;
    pVol->cbIdxNodes -= pNode->cbCost;

    /* Destroy it. */
    rtFsNtfsIdxNodeInfo_Delete(&pNode->NodeInfo);
    RTMemFree(pNode->pNode);
    pNode->pNode = NULL;
    RTMemFree(pNode);
}


/**
 * Trims the index node cache.
 *
 * @param   pThis               The NTFS volume instance which index node cache
 *                              needs trimming.
 */
static void rtFsNtfsIdxVol_TrimIndexNodeCache(PRTFSNTFSVOL pThis)
{
    while (   pThis->cbIdxNodes > RTFSNTFS_MAX_NODE_CACHE_SIZE
           && pThis->cUnusedIdxNodes)
    {
        PRTFSNTFSIDXNODE pNode = RTListRemoveFirst(&pThis->IdxNodeUnusedHead, RTFSNTFSIDXNODE, UnusedListEntry);
        pThis->cUnusedIdxNodes--;
        rtFsNtfsIdxNode_Destroy(pNode);
    }
}


/**
 * Index node reference reached zero, put it in the unused list and trim the
 * cache.
 *
 * @returns zero
 * @param   pNode               The index node.
 */
static uint32_t rtFsNtfsIdxNode_MaybeDestroy(PRTFSNTFSIDXNODE pNode)
{
    PRTFSNTFSVOL pVol = pNode->NodeInfo.pVol;
    if (pVol)
    {
        RTListAppend(&pVol->IdxNodeUnusedHead, &pNode->UnusedListEntry);
        pVol->cUnusedIdxNodes++;
        if (pVol->cbIdxNodes > RTFSNTFS_MAX_NODE_CACHE_SIZE)
            rtFsNtfsIdxVol_TrimIndexNodeCache(pVol);
        return 0;
    }
    /* not sure if this is needed yet... */
    rtFsNtfsIdxNodeInfo_Delete(&pNode->NodeInfo);
    RTMemFree(pNode);
    return 0;
}


/**
 * Releases a reference to an index node.
 *
 * @returns New reference count.
 * @param   pNode               The index node to release.  NULL is ignored.
 */
static uint32_t rtFsNtfsIdxNode_Release(PRTFSNTFSIDXNODE pNode)
{
    if (pNode)
    {
        uint32_t cRefs = ASMAtomicDecU32(&pNode->cRefs);
        Assert(cRefs < 128);
        if (cRefs > 0)
            return cRefs;
        return rtFsNtfsIdxNode_MaybeDestroy(pNode);
    }
    return 0;
}


/**
 * Retains a reference to an index node.
 *
 * This will remove it from the unused list if necessary.
 *
 * @returns New reference count.
 * @param   pNode               The index to reference.
 */
static uint32_t rtFsNtfsIdxNode_Retain(PRTFSNTFSIDXNODE pNode)
{
    uint32_t cRefs = ASMAtomicIncU32(&pNode->cRefs);
    if (cRefs == 1)
    {
        RTListNodeRemove(&pNode->UnusedListEntry);
        pNode->NodeInfo.pVol->cUnusedIdxNodes--;
    }
    return cRefs;
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
static DECLCALLBACK(int) rtFsNtfsDir_Close(void *pvThis)
{
    PRTFSNTFSDIR pThis = (PRTFSNTFSDIR)pvThis;
    LogFlow(("rtFsNtfsDir_Close(%p/%p)\n", pThis, pThis->pShared));

    PRTFSNTFSDIRSHRD pShared = pThis->pShared;
    pThis->pShared = NULL;
    if (pShared)
        rtFsNtfsDirShrd_Release(pShared);

    while (pThis->cEnumStackEntries > 0)
    {
        PRTFSNTFSIDXSTACKENTRY pEntry = &pThis->paEnumStack[--pThis->cEnumStackEntries];
        rtFsNtfsIdxNode_Release(pEntry->pNodeInfo->pNode);
        pEntry->pNodeInfo = NULL;
    }
    RTMemFree(pThis->paEnumStack);
    pThis->paEnumStack = NULL;
    pThis->cEnumStackMaxDepth = 0;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtFsNtfsDir_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    PRTFSNTFSDIR pThis = (PRTFSNTFSDIR)pvThis;
    Log(("rtFsNtfsDir_QueryInfo\n"));
    return rtFsNtfsCore_QueryInfo(pThis->pShared->RootInfo.pRootAttr->pCore,
                                  pThis->pShared->RootInfo.pAlloc ? pThis->pShared->RootInfo.pAlloc
                                  : pThis->pShared->RootInfo.pRootAttr,
                                  pObjInfo, enmAddAttr);
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnMode}
 */
static DECLCALLBACK(int) rtFsNtfsDir_SetMode(void *pvThis, RTFMODE fMode, RTFMODE fMask)
{
    Log(("rtFsNtfsDir_SetMode\n"));
    RT_NOREF(pvThis, fMode, fMask);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetTimes}
 */
static DECLCALLBACK(int) rtFsNtfsDir_SetTimes(void *pvThis, PCRTTIMESPEC pAccessTime, PCRTTIMESPEC pModificationTime,
                                                 PCRTTIMESPEC pChangeTime, PCRTTIMESPEC pBirthTime)
{
    Log(("rtFsNtfsDir_SetTimes\n"));
    RT_NOREF(pvThis, pAccessTime, pModificationTime, pChangeTime, pBirthTime);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSOBJSETOPS,pfnSetOwner}
 */
static DECLCALLBACK(int) rtFsNtfsDir_SetOwner(void *pvThis, RTUID uid, RTGID gid)
{
    Log(("rtFsNtfsDir_SetOwner\n"));
    RT_NOREF(pvThis, uid, gid);
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnOpen}
 */
static DECLCALLBACK(int) rtFsNtfsDir_Open(void *pvThis, const char *pszEntry, uint64_t fOpen,
                                          uint32_t fFlags, PRTVFSOBJ phVfsObj)
{
    LogFlow(("rtFsNtfsDir_Open: pszEntry='%s' fOpen=%#RX64 fFlags=%#x\n", pszEntry, fOpen, fFlags));
    PRTFSNTFSDIR        pThis   = (PRTFSNTFSDIR)pvThis;
    PRTFSNTFSDIRSHRD    pShared = pThis->pShared;
    PRTFSNTFSVOL        pVol    = pShared->RootInfo.NodeInfo.pVol;
    int rc;

    /*
     * We cannot create or replace anything, just open stuff.
     */
    if (   (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN
        || (fOpen & RTFILE_O_ACTION_MASK) == RTFILE_O_OPEN_CREATE)
    { /* likely */ }
    else
        return VERR_WRITE_PROTECT;

    /*
     * Special cases '.' and '..'
     */
    if (   pszEntry[0] == '.'
        && (   pszEntry[1] == '\0'
            || (   pszEntry[1] == '.'
                && pszEntry[2] == '\0')))
    {
        if (!(fFlags & RTVFSOBJ_F_OPEN_DIRECTORY))
            return VERR_IS_A_DIRECTORY;

        PRTFSNTFSDIRSHRD pSharedToOpen;
        if (pszEntry[1] == '\0')
        {
            pSharedToOpen = pShared;
            rtFsNtfsDirShrd_Retain(pSharedToOpen);
            rc = VINF_SUCCESS;
        }
        else
        {
            pSharedToOpen = NULL;
            rc = rtFsNtfsDirShrd_QueryParent(pShared, &pSharedToOpen);
        }
        if (RT_SUCCESS(rc))
        {
            RTVFSDIR hVfsDir;
            rc = rtFsNtfsVol_NewDirFromShared(pVol, pSharedToOpen, &hVfsDir);
            rtFsNtfsDirShrd_Release(pSharedToOpen);
            if (RT_SUCCESS(rc))
            {
                *phVfsObj = RTVfsObjFromDir(hVfsDir);
                RTVfsDirRelease(hVfsDir);
                AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
            }
        }
        LogFlow(("rtFsNtfsDir_Open(%s): returns %Rrc\n", pszEntry, rc));
        return rc;
    }

    /*
     * Lookup the index entry.
     */
    PRTFSNTFSIDXNODE  pNode;
    PCNTFSIDXENTRYHDR pEntryHdr;
    PCNTFSATFILENAME  pFilename;
    rc = rtFsNtfsDirShrd_Lookup(pShared, pszEntry, &pFilename, &pEntryHdr, &pNode);
    if (RT_SUCCESS(rc))
    {
        uint32_t fFileAttribs = RT_LE2H_U32(pFilename->fFileAttribs);
        switch (fFileAttribs & (NTFS_FA_DIRECTORY | NTFS_FA_REPARSE_POINT | NTFS_FA_DUP_FILE_NAME_INDEX_PRESENT))
        {
            /*
             * File.
             */
            case 0:
                if (fFlags & RTVFSOBJ_F_OPEN_FILE)
                {
                    RTVFSFILE hVfsFile;
                    rc = rtFsNtfsVol_NewFile(pVol, fOpen, pEntryHdr, NULL /*pszStreamName*/, &hVfsFile, NULL, pszEntry);
                    if (RT_SUCCESS(rc))
                    {
                        *phVfsObj = RTVfsObjFromFile(hVfsFile);
                        RTVfsFileRelease(hVfsFile);
                        AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
                    }
                }
                else
                    rc = VERR_IS_A_FILE;
                break;

            /*
             * Directory
             */
            case NTFS_FA_DUP_FILE_NAME_INDEX_PRESENT:
            case NTFS_FA_DIRECTORY | NTFS_FA_DUP_FILE_NAME_INDEX_PRESENT:
            case NTFS_FA_DIRECTORY:
                if (fFlags & RTVFSOBJ_F_OPEN_DIRECTORY)
                {
                    PRTFSNTFSDIRSHRD pSharedToOpen;
                    rc = rtFsNtfsVol_QueryOrCreateSharedDirByMftRef(pVol, &pEntryHdr->u.FileMftRec,
                                                                    &pSharedToOpen, NULL, pszEntry);
                    if (RT_SUCCESS(rc))
                    {
                        RTVFSDIR hVfsDir;
                        rc = rtFsNtfsVol_NewDirFromShared(pVol, pSharedToOpen, &hVfsDir);
                        rtFsNtfsDirShrd_Release(pSharedToOpen);
                        if (RT_SUCCESS(rc))
                        {
                            *phVfsObj = RTVfsObjFromDir(hVfsDir);
                            RTVfsDirRelease(hVfsDir);
                            AssertStmt(*phVfsObj != NIL_RTVFSOBJ, rc = VERR_INTERNAL_ERROR_3);
                        }
                    }
                }
                else
                    rc = VERR_IS_A_DIRECTORY;
                break;

            /*
             * Possible symbolic links.
             */
            case NTFS_FA_REPARSE_POINT:
            case NTFS_FA_REPARSE_POINT | NTFS_FA_DIRECTORY:
            case NTFS_FA_REPARSE_POINT | NTFS_FA_DUP_FILE_NAME_INDEX_PRESENT:
            case NTFS_FA_REPARSE_POINT | NTFS_FA_DIRECTORY | NTFS_FA_DUP_FILE_NAME_INDEX_PRESENT:
                rc = VERR_NOT_IMPLEMENTED;
                break;

            default:
                AssertFailed();
                rc = VERR_FILE_NOT_FOUND;
                break;
        }
        rtFsNtfsIdxNode_Release(pNode);
    }

    LogFlow(("rtFsNtfsDir_Open(%s): returns %Rrc\n", pszEntry, rc));
    return rc;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnCreateDir}
 */
static DECLCALLBACK(int) rtFsNtfsDir_CreateDir(void *pvThis, const char *pszSubDir, RTFMODE fMode, PRTVFSDIR phVfsDir)
{
    RT_NOREF(pvThis, pszSubDir, fMode, phVfsDir);
    Log(("rtFsNtfsDir_CreateDir\n"));
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnOpenSymlink}
 */
static DECLCALLBACK(int) rtFsNtfsDir_OpenSymlink(void *pvThis, const char *pszSymlink, PRTVFSSYMLINK phVfsSymlink)
{
    RT_NOREF(pvThis, pszSymlink, phVfsSymlink);
    Log(("rtFsNtfsDir_OpenSymlink\n"));
    return VERR_NOT_SUPPORTED;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnCreateSymlink}
 */
static DECLCALLBACK(int) rtFsNtfsDir_CreateSymlink(void *pvThis, const char *pszSymlink, const char *pszTarget,
                                                  RTSYMLINKTYPE enmType, PRTVFSSYMLINK phVfsSymlink)
{
    RT_NOREF(pvThis, pszSymlink, pszTarget, enmType, phVfsSymlink);
    Log(("rtFsNtfsDir_CreateSymlink\n"));
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnUnlinkEntry}
 */
static DECLCALLBACK(int) rtFsNtfsDir_UnlinkEntry(void *pvThis, const char *pszEntry, RTFMODE fType)
{
    RT_NOREF(pvThis, pszEntry, fType);
    Log(("rtFsNtfsDir_UnlinkEntry\n"));
    return VERR_WRITE_PROTECT;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnRenameEntry}
 */
static DECLCALLBACK(int) rtFsNtfsDir_RenameEntry(void *pvThis, const char *pszEntry, RTFMODE fType, const char *pszNewName)
{
    RT_NOREF(pvThis, pszEntry, fType, pszNewName);
    Log(("rtFsNtfsDir_RenameEntry\n"));
    return VERR_WRITE_PROTECT;
}


/**
 * Cleans up the directory enumeration stack, releasing all node references.
 *
 * @param   pThis               The open directory instance data.
 */
static void rtFsNtfsDir_StackCleanup(PRTFSNTFSDIR pThis)
{
    while (pThis->cEnumStackEntries > 0)
    {
        PRTFSNTFSIDXSTACKENTRY pEntry = &pThis->paEnumStack[--pThis->cEnumStackEntries];
        rtFsNtfsIdxNode_Release(pEntry->pNodeInfo->pNode);
        pEntry->pNodeInfo = NULL;
    }
    if (pThis->paEnumStack)
        pThis->paEnumStack[0].iNext = 0;
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnRewindDir}
 */
static DECLCALLBACK(int) rtFsNtfsDir_RewindDir(void *pvThis)
{
    PRTFSNTFSDIR pThis = (PRTFSNTFSDIR)pvThis;
    LogFlow(("rtFsNtfsDir_RewindDir\n"));

    rtFsNtfsDir_StackCleanup(pThis);
    pThis->fNoMoreFiles = false;

    return VINF_SUCCESS;
}

/**
 * Descends down @a iSubnode to the first entry in left most leaf node.
 *
 * @returns IPRT status code.
 * @param   pThis               The open directory instance data.
 * @param   pRootInfo           The root info structure.
 * @param   iSubnode            The subnode address to descend thru.
 */
static int rtFsNtfsDir_StackDescend(PRTFSNTFSDIR pThis, PRTFSNTFSIDXROOTINFO pRootInfo, int64_t iSubnode)
{
    for (;;)
    {
        /* Load the node. */
        PRTFSNTFSIDXNODE pNode;
        int rc = rtFsNtfsIdxRootInfo_QueryNode(pRootInfo, iSubnode, &pNode);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
        {
            LogFlow(("rtFsNtfsDir_StackDescend: rtFsNtfsIdxRootInfo_QueryNode(%#RX64) error %Rrc!\n", iSubnode, rc));
            return rc;
        }

        /* Push it onto the stack. */
        uint32_t iStack = pThis->cEnumStackEntries;
        if (iStack + 1 < pThis->cEnumStackMaxDepth)
        { /* likely */ }
        else if (pThis->cEnumStackMaxDepth < 1024)
        {
            Assert(pThis->cEnumStackMaxDepth> 0);
            uint32_t cDepth = pThis->cEnumStackMaxDepth * 2;
            Log5(("rtFsNtfsDir_ReadDir: Growing stack size to %u entries (from %u)\n", cDepth, pThis->cEnumStackMaxDepth));
            void *pvNew = RTMemRealloc(pThis->paEnumStack, cDepth * sizeof(pThis->paEnumStack[0]));
            if (pvNew)
                pThis->paEnumStack = (PRTFSNTFSIDXSTACKENTRY)pvNew;
            else
                return VERR_NO_MEMORY;
            pThis->cEnumStackMaxDepth = cDepth;
        }
        else
        {
            LogRel(("rtFsNtfsDir_StackDescend: Badly unbalanced index! (MFT record #%#RX64) -> VERR_VFS_BOGUS_FORMAT\n",
                    pThis->pShared->RootInfo.pRootAttr->pCore->pMftRec->TreeNode.Key));
            return VERR_VFS_BOGUS_FORMAT;
        }

        Log5(("rtFsNtfsDir_ReadDir: pushing %#RX64 (cEntries=%u, iStack=%u)\n", iSubnode, pNode->NodeInfo.cEntries, iStack));
        pThis->paEnumStack[iStack].iNext     = 0;
        pThis->paEnumStack[iStack].fDescend  = false;
        pThis->paEnumStack[iStack].pNodeInfo = &pNode->NodeInfo;
        pThis->cEnumStackEntries = iStack + 1;

        /* Stop if this is a leaf node. */
        if (   !pNode->NodeInfo.fInternal
            || !pNode->NodeInfo.cEntries /* paranoia */)
            return VINF_SUCCESS;

        /* Get the first entry and check that it's an internal node before trying to following it. */
        PCNTFSIDXENTRYHDR pFirstEntry = pNode->NodeInfo.papEntries[0];
        if (pFirstEntry->fFlags & NTFSIDXENTRYHDR_F_INTERNAL)
        { /* likely */ }
        else
            return VINF_SUCCESS;
        iSubnode = NTFSIDXENTRYHDR_GET_SUBNODE(pFirstEntry);
    }
}


/**
 * @interface_method_impl{RTVFSDIROPS,pfnReadDir}
 */
static DECLCALLBACK(int) rtFsNtfsDir_ReadDir(void *pvThis, PRTDIRENTRYEX pDirEntry, size_t *pcbDirEntry,
                                            RTFSOBJATTRADD enmAddAttr)
{
    PRTFSNTFSDIR     pThis   = (PRTFSNTFSDIR)pvThis;
    PRTFSNTFSDIRSHRD pShared = pThis->pShared;
    int              rc;
    Log(("rtFsNtfsDir_ReadDir\n"));

    /*
     * Return immediately if no files at hand.
     */
    if (pThis->fNoMoreFiles)
        return VERR_NO_MORE_FILES;

    /*
     * Make sure we've got a stack before we jump into the fray.
     */
    if (!pThis->cEnumStackMaxDepth)
    {
        uint32_t cDepth;
        if (!pShared->RootInfo.pAlloc)
            cDepth = 2;
        else
        {
            cDepth = ASMBitFirstSetU64(pShared->RootInfo.pAlloc->cbValue / RT_LE2H_U32(pShared->RootInfo.pRoot->cbIndexNode));
            cDepth += 3;
        }

        pThis->paEnumStack = (PRTFSNTFSIDXSTACKENTRY)RTMemAllocZ(cDepth * sizeof(pThis->paEnumStack[0]));
        if (!pThis->paEnumStack)
            return VERR_NO_MEMORY;
        pThis->cEnumStackMaxDepth   = cDepth;
        pThis->cEnumStackEntries    = 0;
        Log5(("rtFsNtfsDir_ReadDir: Initial stack size: %u entries\n", cDepth));
        //pThis->paEnumStack[0].iNext = 0;
    }

    /*
     * Deal with '.' and '..' by using stack entry zero without setting cEnumStack to zero.
     * This is fine because we've got the fNoMoreFiles flag that got checked already.
     */
    size_t const cbDirEntry = *pcbDirEntry;
    if (pThis->cEnumStackEntries == 0)
    {
        if (pThis->paEnumStack[0].iNext <= 1)
        {

            *pcbDirEntry = RT_UOFFSETOF_DYN(RTDIRENTRYEX, szName[pThis->paEnumStack[0].iNext + 2]);
            if (*pcbDirEntry > cbDirEntry)
                return VERR_BUFFER_OVERFLOW;

            /* Names. */
            pDirEntry->cbName = pThis->paEnumStack[0].iNext + 1;
            pDirEntry->szName[0]                     = '.';
            pDirEntry->szName[pDirEntry->cbName - 1] = '.';
            pDirEntry->szName[pDirEntry->cbName]     = '\0';
            pDirEntry->wszShortName[0]               = '\0';
            pDirEntry->cwcShortName                  = 0;

            /* Get referenced shared directory structure that we return info about. */
            PRTFSNTFSDIRSHRD pDotShared;
            if (pThis->paEnumStack[0].iNext == 0)
            {
                rtFsNtfsDirShrd_Retain(pShared);
                pDotShared = pShared;
            }
            else
            {
                pDotShared = NULL;
                rc = rtFsNtfsDirShrd_QueryParent(pShared, &pDotShared);
                if (RT_FAILURE(rc))
                {
                    LogRel(("rtFsNtfsDir_ReadDir: couldn't find '..' filename! %Rrc\n", rc));
                    return rc;
                }
            }

            /* Get the info. */
            rc = rtFsNtfsCore_QueryInfo(pDotShared->RootInfo.pRootAttr->pCore, pDotShared->RootInfo.pRootAttr,
                                        &pDirEntry->Info, enmAddAttr);
            rtFsNtfsDirShrd_Release(pDotShared);
            if (RT_SUCCESS(rc))
                pThis->paEnumStack[0].iNext++;
            Log5(("rtFsNtfsDir_ReadDir: => '%s' (%Rrc)\n", pDirEntry->szName, rc));
            return rc;
        }

        /*
         * Push the root onto the stack and decend down the left side of the tree.
         */
        PRTFSNTFSIDXNODEINFO pNodeInfo  = &pShared->RootInfo.NodeInfo;
        pThis->paEnumStack[0].pNodeInfo = pNodeInfo;
        pThis->paEnumStack[0].iNext     = 0;
        pThis->cEnumStackEntries        = 1;
        Log5(("rtFsNtfsDir_ReadDir: pushing root\n"));
        if (   pNodeInfo->fInternal
            && pNodeInfo->cEntries > 0
            && (pNodeInfo->papEntries[0]->fFlags & NTFSIDXENTRYHDR_F_INTERNAL) /* parnaoia */ )
        {
            rc = rtFsNtfsDir_StackDescend(pThis, &pShared->RootInfo, NTFSIDXENTRYHDR_GET_SUBNODE(pNodeInfo->papEntries[0]));
            if (RT_FAILURE(rc))
            {
                pThis->fNoMoreFiles = true;
                rtFsNtfsDir_StackCleanup(pThis);
                return rc;
            }
        }
    }

    /*
     * Work the stack.
     */
    int32_t iStack = pThis->cEnumStackEntries - 1;
    while (iStack >= 0)
    {
        PRTFSNTFSIDXNODEINFO pNodeInfo  = pThis->paEnumStack[iStack].pNodeInfo;
        uint32_t             iNext      = pThis->paEnumStack[iStack].iNext;
        if (iNext < pNodeInfo->cEntries)
        {
            PCNTFSIDXENTRYHDR pEntry = pNodeInfo->papEntries[iNext];
            if (   !(pEntry->fFlags & NTFSIDXENTRYHDR_F_INTERNAL)
                || !pThis->paEnumStack[iStack].fDescend)
            {
                if (!(pEntry->fFlags & NTFSIDXENTRYHDR_F_END))
                {
                    /*
                     * Try return the current entry.
                     */
                    PCNTFSATFILENAME pFilename = (PCNTFSATFILENAME)(pEntry + 1);

                    /* Deal with the filename. */
                    size_t cchFilename;
                    rc = RTUtf16CalcUtf8LenEx(pFilename->wszFilename, pFilename->cwcFilename, &cchFilename);
                    if (RT_FAILURE(rc))
                    {
                        cchFilename = 48;
                        LogRel(("rtFsNtfsDir_ReadDir: Bad filename (%Rrc) %.*Rhxs\n",
                                rc, pFilename->cwcFilename * sizeof(RTUTF16), pFilename->wszFilename));
                    }
                    *pcbDirEntry = RT_UOFFSETOF_DYN(RTDIRENTRYEX, szName[cchFilename + 1]);
                    if (*pcbDirEntry > cbDirEntry)
                    {
                        Log5(("rtFsNtfsDir_ReadDir: returns VERR_BUFFER_OVERFLOW (for '%.*ls')\n",
                              pFilename->cwcFilename, pFilename->wszFilename));
                        return VERR_BUFFER_OVERFLOW;
                    }

                    char *pszDst = pDirEntry->szName;
                    if (RT_SUCCESS(rc))
                        rc = RTUtf16ToUtf8Ex(pFilename->wszFilename, pFilename->cwcFilename, &pszDst,
                                             cbDirEntry - RT_UOFFSETOF(RTDIRENTRYEX, szName), &cchFilename);
                    if (RT_FAILURE(rc))
                        cchFilename = RTStrPrintf(pDirEntry->szName, cbDirEntry - RT_UOFFSETOF(RTDIRENTRYEX, szName),
                                                  "{invalid-name-%#RX64}", NTFSMFTREF_GET_IDX(&pEntry->u.FileMftRec));
                    pDirEntry->cbName = (uint16_t)cchFilename;

                    /* Figure out how to detect short names. */
                    pDirEntry->cwcShortName    = 0;
                    pDirEntry->wszShortName[0] = '\0';

                    /* Standard attributes: file mode, sizes and timestamps. */
                    pDirEntry->Info.cbObject    = RT_LE2H_U64(pFilename->cbData);
                    pDirEntry->Info.cbAllocated = RT_LE2H_U64(pFilename->cbAllocated);
                    RTTimeSpecSetNtTime(&pDirEntry->Info.BirthTime,        RT_LE2H_U64(pFilename->iCreationTime));
                    RTTimeSpecSetNtTime(&pDirEntry->Info.ModificationTime, RT_LE2H_U64(pFilename->iLastDataModTime));
                    RTTimeSpecSetNtTime(&pDirEntry->Info.ChangeTime,       RT_LE2H_U64(pFilename->iLastMftModTime));
                    RTTimeSpecSetNtTime(&pDirEntry->Info.AccessTime,       RT_LE2H_U64(pFilename->iLastAccessTime));
                    pDirEntry->Info.Attr.fMode = rtFsNtfsConvertFileattribsToMode(RT_LE2H_U32(pFilename->fFileAttribs), pFilename,
                                                                                  RT_LE2H_U16(pEntry->cbKey));

                    /* additional stuff. */
                    switch (enmAddAttr)
                    {
                        case RTFSOBJATTRADD_NOTHING:
                            enmAddAttr = RTFSOBJATTRADD_UNIX;
                            RT_FALL_THRU();
                        case RTFSOBJATTRADD_UNIX:
                            pDirEntry->Info.Attr.u.Unix.uid             = NIL_RTUID;
                            pDirEntry->Info.Attr.u.Unix.gid             = NIL_RTGID;
                            pDirEntry->Info.Attr.u.Unix.cHardlinks      = 1;
                            pDirEntry->Info.Attr.u.Unix.INodeIdDevice   = 0;
                            pDirEntry->Info.Attr.u.Unix.INodeId         = NTFSMFTREF_GET_IDX(&pEntry->u.FileMftRec);
                            pDirEntry->Info.Attr.u.Unix.fFlags          = 0;
                            pDirEntry->Info.Attr.u.Unix.GenerationId    = 0;
                            pDirEntry->Info.Attr.u.Unix.Device          = 0;
                            break;

                        case RTFSOBJATTRADD_UNIX_OWNER:
                            pDirEntry->Info.Attr.u.UnixOwner.uid = NIL_RTUID;
                            pDirEntry->Info.Attr.u.UnixOwner.szName[0] = '\0';
                            break;

                        case RTFSOBJATTRADD_UNIX_GROUP:
                            pDirEntry->Info.Attr.u.UnixGroup.gid = NIL_RTGID;
                            pDirEntry->Info.Attr.u.UnixGroup.szName[0] = '\0';
                            break;

                        case RTFSOBJATTRADD_EASIZE:
                            if (!(pFilename->fFileAttribs & RT_H2LE_U32_C(NTFS_FA_REPARSE_POINT)))
                                pDirEntry->Info.Attr.u.EASize.cb = pFilename->u.cbPackedEas;
                            else
                                pDirEntry->Info.Attr.u.EASize.cb = 0;
                            break;

                        default:
                            AssertFailed();
                            RT_ZERO(pDirEntry->Info.Attr.u);
                            break;
                    }
                    pDirEntry->Info.Attr.enmAdditional = enmAddAttr;

                    /*
                     * Advance the stack entry to the next entry and return.
                     */
                    Log5(("rtFsNtfsDir_ReadDir: => iStack=%u iNext=%u - '%.*ls'\n",
                          iStack, iNext, pFilename->cwcFilename,  pFilename->wszFilename));
                    pThis->paEnumStack[iStack].iNext    = iNext + 1;
                    pThis->paEnumStack[iStack].fDescend = true;
                    return VINF_SUCCESS;
                }

                /*
                 * End node, so pop it.  We join the beoynd-end-of-entries path
                 * further down, forcing the descend code to use continue.
                 */
            }
            else
            {
                /*
                 * Descend.
                 */
                rc = rtFsNtfsDir_StackDescend(pThis, &pShared->RootInfo,
                                              NTFSIDXENTRYHDR_GET_SUBNODE(pNodeInfo->papEntries[iNext]));
                if (RT_SUCCESS(rc))
                {
                    pThis->paEnumStack[iStack].fDescend = false;
                    iStack = pThis->cEnumStackEntries - 1;
                    continue;
                }
                pThis->fNoMoreFiles = true;
                rtFsNtfsDir_StackCleanup(pThis);
                return rc;
            }
        }

        /*
         * Pop at stack entry.
         */
        Log5(("rtFsNtfsDir_ReadDir: popping %#RX64 (iNext=%u, cEntries=%u, iStack=%u) -> %#RX64 (iNext=%d, cEntries=%u)\n",
              pNodeInfo->pNode ? pNodeInfo->pNode->pNode->iSelfAddress : 0, iNext, pNodeInfo->cEntries, iStack,
              iStack > 0 && pThis->paEnumStack[iStack - 1].pNodeInfo->pNode
              ? pThis->paEnumStack[iStack - 1].pNodeInfo->pNode->pNode->iSelfAddress : UINT64_MAX,
              iStack > 0 ? pThis->paEnumStack[iStack - 1].iNext : -1,
              iStack > 0 ? pThis->paEnumStack[iStack - 1].pNodeInfo->cEntries : 0 ));
        rtFsNtfsIdxNode_Release(pNodeInfo->pNode);
        pThis->paEnumStack[iStack].pNodeInfo = NULL;
        pThis->cEnumStackEntries = iStack;
        iStack--;
        Assert(iStack < 0 || !pThis->paEnumStack[iStack].fDescend);
    }

    /*
     * The End.
     */
    Log5(("rtFsNtfsDir_ReadDir: no more files\n"));
    pThis->fNoMoreFiles = true;
    return VERR_NO_MORE_FILES;
}


/**
 * NTFS directory operations.
 */
static const RTVFSDIROPS g_rtFsNtfsDirOps =
{
    { /* Obj */
        RTVFSOBJOPS_VERSION,
        RTVFSOBJTYPE_DIR,
        "NTFS Dir",
        rtFsNtfsDir_Close,
        rtFsNtfsDir_QueryInfo,
        NULL,
        RTVFSOBJOPS_VERSION
    },
    RTVFSDIROPS_VERSION,
    0,
    { /* ObjSet */
        RTVFSOBJSETOPS_VERSION,
        RT_UOFFSETOF(RTVFSDIROPS, ObjSet) - RT_UOFFSETOF(RTVFSDIROPS, Obj),
        rtFsNtfsDir_SetMode,
        rtFsNtfsDir_SetTimes,
        rtFsNtfsDir_SetOwner,
        RTVFSOBJSETOPS_VERSION
    },
    rtFsNtfsDir_Open,
    NULL /* pfnFollowAbsoluteSymlink */,
    NULL /* pfnOpenFile */,
    NULL /* pfnOpenDir */,
    rtFsNtfsDir_CreateDir,
    rtFsNtfsDir_OpenSymlink,
    rtFsNtfsDir_CreateSymlink,
    NULL /* pfnQueryEntryInfo */,
    rtFsNtfsDir_UnlinkEntry,
    rtFsNtfsDir_RenameEntry,
    rtFsNtfsDir_RewindDir,
    rtFsNtfsDir_ReadDir,
    RTVFSDIROPS_VERSION,
};


/**
 * Creates a new directory instance given a shared directory structure.
 *
 * @returns IPRT status code.
 * @param   pThis               The NTFS volume instance.
 * @param   pSharedDir          The shared directory structure to create a new
 *                              handle to.
 * @param   phVfsDir            Where to return the directory handle.
 */
static int rtFsNtfsVol_NewDirFromShared(PRTFSNTFSVOL pThis, PRTFSNTFSDIRSHRD pSharedDir, PRTVFSDIR phVfsDir)
{
    PRTFSNTFSDIR pNewDir;
    int rc = RTVfsNewDir(&g_rtFsNtfsDirOps, sizeof(*pNewDir), 0 /*fFlags*/, pThis->hVfsSelf, NIL_RTVFSLOCK,
                         phVfsDir, (void **)&pNewDir);
    if (RT_SUCCESS(rc))
    {
        rtFsNtfsDirShrd_Retain(pSharedDir);
        pNewDir->pShared            = pSharedDir;
        pNewDir->cEnumStackEntries  = 0;
        pNewDir->cEnumStackMaxDepth = 0;
        pNewDir->paEnumStack        = NULL;
        return VINF_SUCCESS;
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
 * Slow path for querying the allocation state of a cluster.
 *
 * @returns IPRT status code.
 * @param   pThis               The NTFS volume instance.
 * @param   iCluster            The cluster to query.
 * @param   pfState             Where to return the state.
 */
static int rtFsNtfsVol_QueryClusterStateSlow(PRTFSNTFSVOL pThis, uint64_t iCluster, bool *pfState)
{
    int rc;
    uint64_t const cbWholeBitmap = RT_LE2H_U64(pThis->pMftBitmap->pAttrHdr->u.NonRes.cbData);
    uint64_t const offInBitmap   = iCluster >> 3;
    if (offInBitmap < cbWholeBitmap)
    {
        if (!pThis->pvBitmap)
        {
            /*
             * Try cache the whole bitmap if it's not too large.
             */
            if (   cbWholeBitmap <= RTFSNTFS_MAX_WHOLE_BITMAP_CACHE
                && cbWholeBitmap >= RT_ALIGN_64(pThis->cClusters >> 3, 8))
            {
                pThis->cbBitmapAlloc = RT_ALIGN_Z((uint32_t)cbWholeBitmap, 8);
                pThis->pvBitmap      = RTMemAlloc(pThis->cbBitmapAlloc);
                if (pThis->pvBitmap)
                {
                    memset(pThis->pvBitmap, 0xff, pThis->cbBitmapAlloc);
                    rc = rtFsNtfsAttr_Read(pThis->pMftBitmap, 0, pThis->pvBitmap, (uint32_t)cbWholeBitmap);
                    if (RT_SUCCESS(rc))
                    {
                        pThis->iFirstBitmapCluster = 0;
                        pThis->cBitmapClusters     = pThis->cClusters;
                        *pfState = rtFsNtfsBitmap_IsSet(pThis->pvBitmap, (uint32_t)iCluster);
                        return VINF_SUCCESS;
                    }
                    RTMemFree(pThis->pvBitmap);
                    pThis->pvBitmap      = NULL;
                    pThis->cbBitmapAlloc = 0;
                    return rc;
                }
            }

            /*
             * Do a cluster/4K cache.
             */
            pThis->cbBitmapAlloc = RT_MAX(pThis->cbCluster, _4K);
            pThis->pvBitmap      = RTMemAlloc(pThis->cbBitmapAlloc);
            if (!pThis->pvBitmap)
            {
                pThis->cbBitmapAlloc = 0;
                return VERR_NO_MEMORY;
            }
        }

        /*
         * Load a cache line.
         */
        Assert(RT_IS_POWER_OF_TWO(pThis->cbBitmapAlloc));
        uint64_t offLoad = offInBitmap & ~(uint64_t)(pThis->cbBitmapAlloc - 1);
        uint32_t cbLoad  = (uint32_t)RT_MIN(cbWholeBitmap - offLoad, pThis->cbBitmapAlloc);

        memset(pThis->pvBitmap, 0xff, pThis->cbBitmapAlloc);
        rc = rtFsNtfsAttr_Read(pThis->pMftBitmap, offLoad, pThis->pvBitmap, cbLoad);
        if (RT_SUCCESS(rc))
        {
            pThis->iFirstBitmapCluster = offLoad << 3;
            pThis->cBitmapClusters     = cbLoad  << 3;
            *pfState = rtFsNtfsBitmap_IsSet(pThis->pvBitmap, (uint32_t)(iCluster - pThis->iFirstBitmapCluster));
            return VINF_SUCCESS;
        }
        pThis->cBitmapClusters = 0;
    }
    else
    {
        LogRel(("rtFsNtfsVol_QueryClusterStateSlow: iCluster=%#RX64 is outside the bitmap (%#RX64)\n", iCluster, cbWholeBitmap));
        rc = VERR_OUT_OF_RANGE;
    }
    return rc;
}


/**
 * Query the allocation state of the given cluster.
 *
 * @returns IPRT status code.
 * @param   pThis               The NTFS volume instance.
 * @param   iCluster            The cluster to query.
 * @param   pfState             Where to return the state.
 */
static int rtFsNtfsVol_QueryClusterState(PRTFSNTFSVOL pThis, uint64_t iCluster, bool *pfState)
{
    uint64_t iClusterInCache = iCluster - pThis->iFirstBitmapCluster;
    if (iClusterInCache < pThis->cBitmapClusters)
    {
        *pfState = rtFsNtfsBitmap_IsSet(pThis->pvBitmap, (uint32_t)iClusterInCache);
        return VINF_SUCCESS;
    }
    return rtFsNtfsVol_QueryClusterStateSlow(pThis, iCluster, pfState);
}


/**
 * Callback for RTAvlU64Destroy used by rtFsNtfsVol_Close to destroy the MFT
 * record cache.
 *
 * @returns VINF_SUCCESS
 * @param   pNode               The MFT record to destroy.
 * @param   pvUser              Ignored.
 */
static DECLCALLBACK(int) rtFsNtFsVol_DestroyCachedMftRecord(PAVLU64NODECORE pNode,  void *pvUser)
{
    PRTFSNTFSMFTREC pMftRec = (PRTFSNTFSMFTREC)pNode;
    RT_NOREF(pvUser);

    RTMemFree(pMftRec->pbRec);
    pMftRec->pbRec = NULL;
    RTMemFree(pMftRec);

    return VINF_SUCCESS;
}



/**
 * Callback for RTAvlU64Destroy used by rtFsNtfsVol_Close to destroy the index
 * node cache.
 *
 * @returns VINF_SUCCESS
 * @param   pNode               The index node to destroy.
 * @param   pvUser              Ignored.
 */
static DECLCALLBACK(int) rtFsNtfsVol_DestroyIndexNode(PAVLU64NODECORE pNode,  void *pvUser)
{
    PRTFSNTFSIDXNODE pIdxNode = (PRTFSNTFSIDXNODE)pNode;
    RT_NOREF(pvUser);

    RTMemFree(pIdxNode->pNode);
    RTMemFree(pIdxNode->NodeInfo.papEntries);
    pIdxNode->pNode               = NULL;
    pIdxNode->NodeInfo.papEntries = NULL;
    pIdxNode->NodeInfo.pIndexHdr  = NULL;
    pIdxNode->NodeInfo.pVol       = NULL;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnClose}
 */
static DECLCALLBACK(int) rtFsNtfsVol_Close(void *pvThis)
{
    PRTFSNTFSVOL pThis = (PRTFSNTFSVOL)pvThis;
    Log(("rtFsNtfsVol_Close(%p):\n", pThis));

    /*
     * Index / directory related members.
     */
    if (pThis->pRootDir)
    {
        rtFsNtfsDirShrd_Release(pThis->pRootDir);
        pThis->pRootDir = NULL;
    }

    RTAvlU64Destroy(&pThis->IdxNodeCacheRoot, rtFsNtfsVol_DestroyIndexNode, NULL);

    RTMemFree(pThis->pawcUpcase);
    pThis->pawcUpcase = NULL;

    pThis->IdxNodeUnusedHead.pPrev = pThis->IdxNodeUnusedHead.pNext = NULL;

    /*
     * Allocation bitmap cache.
     */
    if (pThis->pMftBitmap)
    {
        rtFsNtfsCore_Release(pThis->pMftBitmap->pCore);
        pThis->pMftBitmap = NULL;
    }
    RTMemFree(pThis->pvBitmap);
    pThis->pvBitmap = NULL;

    /*
     * The MFT and MFT cache.
     */
    if (pThis->pMftData)
    {
        rtFsNtfsCore_Release(pThis->pMftData->pCore);
        pThis->pMftData = NULL;
    }

    Assert(RTListIsEmpty(&pThis->CoreInUseHead));
    PRTFSNTFSCORE pCurCore, pNextCore;
    RTListForEachSafe(&pThis->CoreInUseHead, pCurCore, pNextCore, RTFSNTFSCORE, ListEntry)
        rtFsNtfsCore_Destroy(pCurCore);
    RTListForEachSafe(&pThis->CoreUnusedHead, pCurCore, pNextCore, RTFSNTFSCORE, ListEntry)
        rtFsNtfsCore_Destroy(pCurCore);

    pThis->CoreInUseHead.pPrev  = pThis->CoreInUseHead.pNext  = NULL;
    pThis->CoreUnusedHead.pPrev = pThis->CoreUnusedHead.pNext = NULL;

    Assert(pThis->MftRoot == NULL);
    RTAvlU64Destroy(&pThis->MftRoot, rtFsNtFsVol_DestroyCachedMftRecord, NULL);

    /*
     * Backing file and handles.
     */
    RTVfsFileRelease(pThis->hVfsBacking);
    pThis->hVfsBacking = NIL_RTVFSFILE;
    pThis->hVfsSelf    = NIL_RTVFS;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnQueryInfo}
 */
static DECLCALLBACK(int) rtFsNtfsVol_QueryInfo(void *pvThis, PRTFSOBJINFO pObjInfo, RTFSOBJATTRADD enmAddAttr)
{
    NOREF(pvThis); NOREF(pObjInfo); NOREF(enmAddAttr);
    return VERR_WRONG_TYPE;
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnOpenRoot}
 */
static DECLCALLBACK(int) rtFsNtfsVol_OpenRoot(void *pvThis, PRTVFSDIR phVfsDir)
{
    PRTFSNTFSVOL pThis = (PRTFSNTFSVOL)pvThis;
    AssertReturn(pThis->pRootDir, VERR_INTERNAL_ERROR_4);
    int rc = rtFsNtfsVol_NewDirFromShared(pThis, pThis->pRootDir, phVfsDir);
    LogFlow(("rtFsNtfsVol_OpenRoot: returns %Rrc\n", rc));
    return rc;
}


/**
 * @interface_method_impl{RTVFSOBJOPS::Obj,pfnQueryRangeState}
 */
static DECLCALLBACK(int) rtFsNtfsVol_QueryRangeState(void *pvThis, uint64_t off, size_t cb, bool *pfUsed)
{
    PRTFSNTFSVOL pThis = (PRTFSNTFSVOL)pvThis;
    *pfUsed = true;

    /*
     * Round to a cluster range.
     */
    uint64_t iCluster  = off >> pThis->cClusterShift;

    Assert(RT_IS_POWER_OF_TWO(pThis->cbCluster));
    cb += off & (pThis->cbCluster - 1);
    cb = RT_ALIGN_Z(cb, pThis->cbCluster);
    size_t   cClusters = cb >> pThis->cClusterShift;

    /*
     * Check the clusters one-by-one.
     * Just to be cautious, we will always check the cluster at off, even when cb is zero.
     */
    do
    {
        bool fState = true;
        int rc = rtFsNtfsVol_QueryClusterState(pThis, iCluster, &fState);
        if (RT_FAILURE(rc))
            return rc;
        if (fState)
        {
            *pfUsed = true;
            LogFlow(("rtFsNtfsVol_QueryRangeState: %RX64 LB %#x - used\n", off & ~(uint64_t)(pThis->cbCluster - 1), cb));
            return VINF_SUCCESS;
        }

        iCluster++;
    } while (cClusters-- > 0);

    LogFlow(("rtFsNtfsVol_QueryRangeState: %RX64 LB %#x - unused\n", off & ~(uint64_t)(pThis->cbCluster - 1), cb));
    *pfUsed = false;
    return VINF_SUCCESS;
}


/**
 * NTFS volume operations.
 */
static const RTVFSOPS g_rtFsNtfsVolOps =
{
    /* .Obj = */
    {
        /* .uVersion = */       RTVFSOBJOPS_VERSION,
        /* .enmType = */        RTVFSOBJTYPE_VFS,
        /* .pszName = */        "NtfsVol",
        /* .pfnClose = */       rtFsNtfsVol_Close,
        /* .pfnQueryInfo = */   rtFsNtfsVol_QueryInfo,
        /* .pfnQueryInfoEx = */ NULL,
        /* .uEndMarker = */     RTVFSOBJOPS_VERSION
    },
    /* .uVersion = */           RTVFSOPS_VERSION,
    /* .fFeatures = */          0,
    /* .pfnOpenRoot = */        rtFsNtfsVol_OpenRoot,
    /* .pfnQueryRangeState = */ rtFsNtfsVol_QueryRangeState,
    /* .uEndMarker = */         RTVFSOPS_VERSION
};


/**
 * Checks that the storage for the given attribute is all marked allocated in
 * the allocation bitmap of the volume.
 *
 * @returns IPRT status code.
 * @param   pThis               The NTFS volume instance.
 * @param   pAttr               The attribute to check.
 * @param   pszDesc             Description of the attribute.
 * @param   pErrInfo            Where to return error details.
 */
static int rtFsNtfsVolCheckBitmap(PRTFSNTFSVOL pThis, PRTFSNTFSATTR pAttr, const char *pszDesc, PRTERRINFO pErrInfo)
{
    PRTFSNTFSATTRSUBREC pSubRec = NULL;
    PRTFSNTFSEXTENTS    pTable  = &pAttr->Extents;
    uint64_t            offFile = 0;
    for (;;)
    {
        uint32_t const  cExtents  = pTable->cExtents;
        PRTFSNTFSEXTENT paExtents = pTable->paExtents;
        for (uint32_t iExtent = 0; iExtent < cExtents; iExtent++)
        {
            uint64_t const off = paExtents[iExtent].off;
            if (off == UINT64_MAX)
                offFile += paExtents[iExtent].cbExtent;
            else
            {
                uint64_t iCluster  = off >> pThis->cClusterShift;
                uint64_t cClusters = paExtents[iExtent].cbExtent >> pThis->cClusterShift;
                Assert((cClusters << pThis->cClusterShift) == paExtents[iExtent].cbExtent);
                Assert(cClusters != 0);

                while (cClusters-- > 0)
                {
                    bool fState = false;
                    int rc = rtFsNtfsVol_QueryClusterState(pThis, iCluster, &fState);
                    if (RT_FAILURE(rc))
                        return RTERRINFO_LOG_REL_SET_F(pErrInfo, rc,
                                                       "Error querying allocation bitmap entry %#RX64 (for %s offset %#RX64)",
                                                       iCluster, pszDesc, offFile);
                    if (!fState)
                        return RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                       "Cluster %#RX64 at offset %#RX64 in %s is not marked allocated",
                                                       iCluster, offFile, pszDesc);
                    offFile += pThis->cbCluster;
                }
            }
        }

        /* Next table. */
        pSubRec = pSubRec ? pSubRec->pNext : pAttr->pSubRecHead;
        if (!pSubRec)
            return VINF_SUCCESS;
        pTable = &pSubRec->Extents;
    }
}


/**
 * Loads, validates and setups the '.' (NTFS_MFT_IDX_ROOT) MFT entry.
 *
 * @returns IPRT status code
 * @param   pThis               The NTFS volume instance.  Will set pawcUpcase.
 * @param   pErrInfo            Where to return additional error info.
 */
static int rtFsNtfsVolLoadRootDir(PRTFSNTFSVOL pThis, PRTERRINFO pErrInfo)
{
    /*
     * Load it and do some checks.
     */
    PRTFSNTFSCORE pCore;
    int rc = rtFsNtfsVol_NewCoreForMftIdx(pThis, NTFS_MFT_IDX_ROOT, false /*fRelaxedUsa*/, &pCore, pErrInfo); // DON'T COMMIT
    if (RT_SUCCESS(rc))
    {
        PRTFSNTFSATTR pFilenameAttr = rtFsNtfsCore_FindUnnamedAttribute(pCore, NTFS_AT_FILENAME);
        if (!pFilenameAttr)
            rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "RootDir: has no FILENAME attribute!");
        else if (pFilenameAttr->pAttrHdr->fNonResident)
            rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "RootDir:  FILENAME attribute is non-resident!");
        else if (pFilenameAttr->pAttrHdr->u.Res.cbValue < RT_UOFFSETOF(NTFSATFILENAME, wszFilename[1]))
            rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                         "RootDir:  FILENAME attribute value size is too small: %#x",
                                         pFilenameAttr->pAttrHdr->u.Res.cbValue);
        else
        {
            PNTFSATFILENAME pFilename = (PNTFSATFILENAME)(  (uint8_t *)pFilenameAttr->pAttrHdr
                                                          + pFilenameAttr->pAttrHdr->u.Res.offValue);
            if (   pFilename->cwcFilename != 1
                || (   RTUtf16NICmpAscii(pFilename->wszFilename, ".", 1) != 0
                    && RTUtf16NICmpAscii(pFilename->wszFilename, "$", 1) != 0))
                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                             "RootDir: FILENAME is not '.' nor '$: '%.*ls'",
                                             pFilename->cwcFilename, pFilename->wszFilename);
            else
            {
                PRTFSNTFSATTR pIndexRoot   = rtFsNtfsCore_FindNamedAttributeAscii(pCore, NTFS_AT_INDEX_ROOT,
                                                                                  RT_STR_TUPLE(NTFS_DIR_ATTRIBUTE_NAME));
                PRTFSNTFSATTR pIndexAlloc  = rtFsNtfsCore_FindNamedAttributeAscii(pCore, NTFS_AT_INDEX_ALLOCATION,
                                                                                  RT_STR_TUPLE(NTFS_DIR_ATTRIBUTE_NAME));
                PRTFSNTFSATTR pIndexBitmap = rtFsNtfsCore_FindNamedAttributeAscii(pCore, NTFS_AT_BITMAP,
                                                                                  RT_STR_TUPLE(NTFS_DIR_ATTRIBUTE_NAME));
                if (!pIndexRoot)
                    rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "RootDir: Found no INDEX_ROOT attribute named $I30");
                else if (!pIndexAlloc && pIndexBitmap)
                    rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "RootDir: Found no INDEX_ALLOCATION attribute named $I30");
                else if (!pIndexBitmap && pIndexAlloc)
                    rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "RootDir: Found no BITMAP attribute named $I30");
                if (RT_SUCCESS(rc) && pIndexAlloc)
                    rc = rtFsNtfsVolCheckBitmap(pThis, pIndexAlloc, "RootDir", pErrInfo);
                if (RT_SUCCESS(rc) && pIndexBitmap)
                    rc = rtFsNtfsVolCheckBitmap(pThis, pIndexBitmap, "RootDir/bitmap", pErrInfo);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Load it as a normal directory.
                     */
                    PRTFSNTFSDIRSHRD pSharedDir;
                    rc = rtFsNtfsVol_NewSharedDirFromCore(pThis, pCore, &pSharedDir, pErrInfo, "RootDir");
                    if (RT_SUCCESS(rc))
                    {
                        rtFsNtfsCore_Release(pCore);
                        pThis->pRootDir = pSharedDir;
                        return VINF_SUCCESS;
                    }
                }
            }
        }
        rtFsNtfsCore_Release(pCore);
    }
    else
        rc = RTERRINFO_LOG_REL_SET(pErrInfo, rc, "Root dir: Error reading MFT record");
    return rc;
}


/**
 * Loads, validates and setups the '$UpCase' (NTFS_MFT_IDX_UP_CASE) MFT entry.
 *
 * This is needed for filename lookups, I think.
 *
 * @returns IPRT status code
 * @param   pThis               The NTFS volume instance.  Will set pawcUpcase.
 * @param   pErrInfo            Where to return additional error info.
 */
static int rtFsNtfsVolLoadUpCase(PRTFSNTFSVOL pThis, PRTERRINFO pErrInfo)
{
    PRTFSNTFSCORE pCore;
    int rc = rtFsNtfsVol_NewCoreForMftIdx(pThis, NTFS_MFT_IDX_UP_CASE, false /*fRelaxedUsa*/, &pCore, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        PRTFSNTFSATTR pDataAttr = rtFsNtfsCore_FindUnnamedAttribute(pCore, NTFS_AT_DATA);
        if (pDataAttr)
        {
            /*
             * Validate the '$Upcase' MFT record.
             */
            uint32_t const cbMin = 512;
            uint32_t const cbMax = _128K;
            if (!pDataAttr->pAttrHdr->fNonResident)
                rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "$UpCase: unnamed DATA attribute is resident!");
            else if (   (uint64_t)RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbAllocated) < cbMin
                     || (uint64_t)RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbAllocated) > cbMax)
                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                             "$UpCase: unnamed DATA attribute allocated size is out of range: %#RX64, expected at least %#RX32 and no more than %#RX32",
                                             RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbAllocated), cbMin, cbMax);
            else if (   (uint64_t)RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbData) < cbMin
                     ||   (uint64_t)RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbData)
                        > (uint64_t)RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbData)
                     || ((uint64_t)RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbData) & 1) )
                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                             "$UpCase: unnamed DATA attribute initialized size is out of range: %#RX64, expected at least %#RX32 and no more than %#RX64",
                                             RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbData), cbMin,
                                             RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbAllocated) );
            else if (   (uint64_t)RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbInitialized) < cbMin
                     ||    (uint64_t)RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbInitialized)
                         > (uint64_t)RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbAllocated)
                     || ((uint64_t)RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbInitialized) & 1) )
                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                             "$UpCase: unnamed DATA attribute initialized size is out of range: %#RX64, expected at least %#RX32 and no more than %#RX64",
                                             RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbInitialized), cbMin,
                                             RT_LE2H_U64(pDataAttr->pAttrHdr->u.NonRes.cbAllocated) );
            else if (pDataAttr->pAttrHdr->u.NonRes.uCompressionUnit != 0)
                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                             "$UpCase: unnamed DATA attribute is compressed: %#x",
                                             pDataAttr->pAttrHdr->u.NonRes.uCompressionUnit);
            else
            {
                PRTFSNTFSATTR pFilenameAttr = rtFsNtfsCore_FindUnnamedAttribute(pCore, NTFS_AT_FILENAME);
                if (!pFilenameAttr)
                    rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "$UpCase has no FILENAME attribute!");
                else if (pFilenameAttr->pAttrHdr->fNonResident)
                    rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "$UpCase FILENAME attribute is non-resident!");
                else if (pFilenameAttr->pAttrHdr->u.Res.cbValue < RT_UOFFSETOF(NTFSATFILENAME, wszFilename[7]))
                    rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                 "$UpCase: FILENAME attribute value size is too small: %#x",
                                                 pFilenameAttr->pAttrHdr->u.Res.cbValue);
                else
                {
                    PNTFSATFILENAME pFilename = (PNTFSATFILENAME)(  (uint8_t *)pFilenameAttr->pAttrHdr
                                                                  + pFilenameAttr->pAttrHdr->u.Res.offValue);
                    if (   pFilename->cwcFilename != 7
                        || RTUtf16NICmpAscii(pFilename->wszFilename, "$UpCase", 7) != 0)
                        rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                     "$UpCase: FILENAME isn't '$UpCase': '%.*ls'",
                                                     pFilename->cwcFilename, pFilename->wszFilename);
                    else
                    {
                        /*
                         * Allocate memory for the uppercase table and read it.
                         */
                        PRTUTF16 pawcUpcase;
                        pThis->pawcUpcase = pawcUpcase = (PRTUTF16)RTMemAlloc(_64K * sizeof(pThis->pawcUpcase[0]));
                        if (pawcUpcase)
                        {
                            for (size_t i = 0; i < _64K; i++)
                                pawcUpcase[i] = (uint16_t)i;

                            rc = rtFsNtfsAttr_Read(pDataAttr, 0, pawcUpcase, pDataAttr->pAttrHdr->u.NonRes.cbData);
                            if (RT_SUCCESS(rc))
                            {
                                /*
                                 * Check the data.
                                 */
                                for (size_t i = 1; i < _64K; i++)
                                    if (pawcUpcase[i] == 0)
                                    {
                                        rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                                     "$UpCase entry %#x is zero!", i);
                                        break;
                                    }

                                /*
                                 * While we still have the $UpCase file open, check it against the allocation bitmap.
                                 */
                                if (RT_SUCCESS(rc))
                                    rc = rtFsNtfsVolCheckBitmap(pThis, pDataAttr, "$UpCase", pErrInfo);

                                /* We're done, no need for special success return here though. */
                            }
                            else
                                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, rc, "Error reading $UpCase data into memory");
                        }
                        else
                            rc = VERR_NO_MEMORY;
                    }
                }
            }
        }
        else
            rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "$UpCase: has no unnamed DATA attribute!");
        rtFsNtfsCore_Release(pCore);
    }
    else
        rc = RTERRINFO_LOG_REL_SET(pErrInfo, rc, "$UpCase: Error reading the MFT record");
    return rc;
}


/**
 * Loads the allocation bitmap and does basic validation of.
 *
 * @returns IPRT status code.
 * @param   pThis               The NTFS volume instance.  Will set up the
 *                              'Allocation bitmap and cache' fields.
 * @param   pErrInfo            Where to return error details.
 */
static int rtFsNtfsVolLoadBitmap(PRTFSNTFSVOL pThis, PRTERRINFO pErrInfo)
{
    PRTFSNTFSCORE pCore;
    int rc = rtFsNtfsVol_NewCoreForMftIdx(pThis, NTFS_MFT_IDX_BITMAP, false /*fRelaxedUsa*/, &pCore, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        PRTFSNTFSATTR pMftBitmap;
        pThis->pMftBitmap = pMftBitmap = rtFsNtfsCore_FindUnnamedAttribute(pCore, NTFS_AT_DATA);
        if (pMftBitmap)
        {
            /*
             * Validate the '$Bitmap' MFT record.
             * We expect the bitmap to be fully initialized and be sized according to the
             * formatted volume size.  Allegedly, NTFS pads it to an even 8 byte in size.
             */
            uint64_t const cbMinBitmap      = RT_ALIGN_64(pThis->cbVolume >> (pThis->cClusterShift + 3), 8);
            uint64_t const cbMaxBitmap      = RT_ALIGN_64(cbMinBitmap, pThis->cbCluster);
            //uint64_t const cbMinInitialized = RT_ALIGN_64((RT_MAX(pThis->uLcnMft, pThis->uLcnMftMirror) + 16) >> 3, 8);
            if (!pMftBitmap->pAttrHdr->fNonResident)
                rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "MFT record #6 unnamed DATA attribute is resident!");
            else if (   (uint64_t)RT_LE2H_U64(pMftBitmap->pAttrHdr->u.NonRes.cbAllocated) < cbMinBitmap
                     || (uint64_t)RT_LE2H_U64(pMftBitmap->pAttrHdr->u.NonRes.cbAllocated) > cbMaxBitmap)
                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                             "$Bitmap: unnamed DATA attribute allocated size is out of range: %#RX64, expected at least %#RX64 and no more than %#RX64",
                                             RT_LE2H_U64(pMftBitmap->pAttrHdr->u.NonRes.cbAllocated), cbMinBitmap, cbMaxBitmap);
            else if (   (uint64_t)RT_LE2H_U64(pMftBitmap->pAttrHdr->u.NonRes.cbData) < cbMinBitmap
                     ||    (uint64_t)RT_LE2H_U64(pMftBitmap->pAttrHdr->u.NonRes.cbData)
                         > (uint64_t)RT_LE2H_U64(pMftBitmap->pAttrHdr->u.NonRes.cbData))
                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                             "$Bitmap: unnamed DATA attribute initialized size is out of range: %#RX64, expected at least %#RX64 and no more than %#RX64",
                                             RT_LE2H_U64(pMftBitmap->pAttrHdr->u.NonRes.cbData), cbMinBitmap,
                                             RT_LE2H_U64(pMftBitmap->pAttrHdr->u.NonRes.cbAllocated) );
            else if (   (uint64_t)RT_LE2H_U64(pMftBitmap->pAttrHdr->u.NonRes.cbInitialized) < cbMinBitmap
                     ||    (uint64_t)RT_LE2H_U64(pMftBitmap->pAttrHdr->u.NonRes.cbInitialized)
                         > (uint64_t)RT_LE2H_U64(pMftBitmap->pAttrHdr->u.NonRes.cbAllocated))
                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                             "$Bitmap: unnamed DATA attribute initialized size is out of range: %#RX64, expected at least %#RX64 and no more than %#RX64",
                                             RT_LE2H_U64(pMftBitmap->pAttrHdr->u.NonRes.cbInitialized), cbMinBitmap,
                                             RT_LE2H_U64(pMftBitmap->pAttrHdr->u.NonRes.cbAllocated) );
            else if (pMftBitmap->pAttrHdr->u.NonRes.uCompressionUnit != 0)
                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                             "$Bitmap: unnamed DATA attribute is compressed: %#x",
                                             pMftBitmap->pAttrHdr->u.NonRes.uCompressionUnit);
            else if (pMftBitmap->Extents.cExtents != 1) /* paranoia for now */
                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                             "$Bitmap: unnamed DATA attribute is expected to have a single extent: %u extents",
                                             pMftBitmap->Extents.cExtents);
            else if (pMftBitmap->Extents.paExtents[0].off == UINT64_MAX)
                rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "MFT record #6 unnamed DATA attribute is sparse");
            else
            {
                PRTFSNTFSATTR pFilenameAttr = rtFsNtfsCore_FindUnnamedAttribute(pCore, NTFS_AT_FILENAME);
                if (!pFilenameAttr)
                    rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "MFT record #6 has no FILENAME attribute!");
                else if (pFilenameAttr->pAttrHdr->fNonResident)
                    rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "MFT record #6 FILENAME attribute is non-resident!");
                else if (pFilenameAttr->pAttrHdr->u.Res.cbValue < RT_UOFFSETOF(NTFSATFILENAME, wszFilename[7]))
                    rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                 "$Bitmap FILENAME attribute value size is too small: %#x",
                                                 pFilenameAttr->pAttrHdr->u.Res.cbValue);
                else
                {
                    PNTFSATFILENAME pFilename = (PNTFSATFILENAME)(  (uint8_t *)pFilenameAttr->pAttrHdr
                                                                  + pFilenameAttr->pAttrHdr->u.Res.offValue);
                    if (   pFilename->cwcFilename != 7
                        || RTUtf16NICmpAscii(pFilename->wszFilename, "$Bitmap", 7) != 0)
                        rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                     "$Bitmap: FILENAME isn't '$Bitmap': '%.*ls'",
                                                     pFilename->cwcFilename, pFilename->wszFilename);
                    else
                    {
                        /*
                         * Read some of it into the buffer and check that essential stuff is flagged as allocated.
                         */
                        /* The boot sector. */
                        bool fState = false;
                        rc = rtFsNtfsVol_QueryClusterState(pThis, 0, &fState);
                        if (RT_SUCCESS(rc) && !fState)
                            rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                       "MFT allocation bitmap error: Bootsector isn't marked allocated!");
                        else if (RT_FAILURE(rc))
                            rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                         "MFT allocation bitmap (offset 0) read error: %Rrc", rc);

                        /* The bitmap ifself, the MFT data, and the MFT bitmap. */
                        if (RT_SUCCESS(rc))
                            rc = rtFsNtfsVolCheckBitmap(pThis, pThis->pMftBitmap, "allocation bitmap", pErrInfo);
                        if (RT_SUCCESS(rc))
                            rc = rtFsNtfsVolCheckBitmap(pThis, pThis->pMftData, "MFT", pErrInfo);
                        if (RT_SUCCESS(rc))
                            rc = rtFsNtfsVolCheckBitmap(pThis,
                                                        rtFsNtfsCore_FindUnnamedAttribute(pThis->pMftData->pCore, NTFS_AT_BITMAP),
                                                        "MFT Bitmap", pErrInfo);
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Looks like the bitmap is good.
                             */
                            return VINF_SUCCESS;
                        }
                    }
                }
            }
            pThis->pMftBitmap = NULL;
        }
        else
            rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "$Bitmap: has no unnamed DATA attribute!");
        rtFsNtfsCore_Release(pCore);
    }
    else
        rc = RTERRINFO_LOG_REL_SET(pErrInfo, rc, "$Bitmap: Error MFT record");
    return rc;
}


/**
 * Loads, validates and setups the '$Volume' (NTFS_MFT_IDX_VOLUME) MFT entry.
 *
 * @returns IPRT status code
 * @param   pThis               The NTFS volume instance.  Will set uNtfsVersion
 *                              and fVolumeFlags.
 * @param   pErrInfo            Where to return additional error info.
 */
static int rtFsNtfsVolLoadVolumeInfo(PRTFSNTFSVOL pThis, PRTERRINFO pErrInfo)
{
    PRTFSNTFSCORE pCore;
    int rc = rtFsNtfsVol_NewCoreForMftIdx(pThis, NTFS_MFT_IDX_VOLUME, false /*fRelaxedUsa*/, &pCore, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        PRTFSNTFSATTR pVolInfoAttr = rtFsNtfsCore_FindUnnamedAttribute(pCore, NTFS_AT_VOLUME_INFORMATION);
        if (pVolInfoAttr)
        {
            /*
             * Validate the '$Volume' MFT record.
             */
            if (pVolInfoAttr->pAttrHdr->fNonResident)
                rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "$Volume unnamed VOLUME_INFORMATION attribute is not resident!");
            else if (   pVolInfoAttr->cbResident != sizeof(NTFSATVOLUMEINFO)
                     || pVolInfoAttr->cbValue    != sizeof(NTFSATVOLUMEINFO))
                rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                             "$Volume VOLUME_INFORMATION attribute has the wrong size: cbValue=%#RX64, cbResident=%#RX32, expected %#x\n",
                                             pVolInfoAttr->cbValue, pVolInfoAttr->cbResident, sizeof(NTFSATVOLUMEINFO));
            else
            {
                PRTFSNTFSATTR pFilenameAttr = rtFsNtfsCore_FindUnnamedAttribute(pCore, NTFS_AT_FILENAME);
                if (!pFilenameAttr)
                    rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "$Volume has no FILENAME attribute!");
                else if (pFilenameAttr->pAttrHdr->fNonResident)
                    rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "$Volume FILENAME attribute is non-resident!");
                else if (pFilenameAttr->pAttrHdr->u.Res.cbValue < RT_UOFFSETOF(NTFSATFILENAME, wszFilename[7]))
                    rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                 "$Volume FILENAME attribute value size is too small: %#x",
                                                 pFilenameAttr->pAttrHdr->u.Res.cbValue);
                else
                {
                    PNTFSATFILENAME pFilename = (PNTFSATFILENAME)(  (uint8_t *)pFilenameAttr->pAttrHdr
                                                                  + pFilenameAttr->pAttrHdr->u.Res.offValue);
                    if (   pFilename->cwcFilename != 7
                        || RTUtf16NICmpAscii(pFilename->wszFilename, "$Volume", 7) != 0)
                        rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                     "$Volume FILENAME isn't '$Volume': '%.*ls'",
                                                     pFilename->cwcFilename, pFilename->wszFilename);
                    else
                    {
                        /*
                         * Look at the information.
                         */
                        PCNTFSATVOLUMEINFO pVolInfo;
                        pVolInfo = (PCNTFSATVOLUMEINFO)((uint8_t *)pVolInfoAttr->pAttrHdr + pVolInfoAttr->pAttrHdr->u.Res.offValue);
                        pThis->uNtfsVersion = RTFSNTFS_MAKE_VERSION(pVolInfo->uMajorVersion, pVolInfo->uMinorVersion);
                        pThis->fVolumeFlags = RT_LE2H_U16(pVolInfo->fFlags);
                        Log(("NTFS: Version %u.%u, flags=%#x\n", pVolInfo->uMajorVersion, pVolInfo->uMinorVersion, pThis->fVolumeFlags));

                        /* We're done, no need for special success return here though. */
                    }
                }
            }
        }
        else
            rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "MFT record $Volume has no unnamed VOLUME_INFORMATION attribute!");
        rtFsNtfsCore_Release(pCore);
    }
    else
        rc = RTERRINFO_LOG_REL_SET(pErrInfo, rc, "Error reading $Volume MFT record");
    return rc;
}


/**
 * Loads, validates and setups the '$Mft' (NTFS_MFT_IDX_MFT) MFT entry.
 *
 * This is the first thing we do after we've checked out the boot sector and
 * extracted information from it, since everything else depends on us being able
 * to access the MFT data.
 *
 * @returns IPRT status code
 * @param   pThis               The NTFS volume instance.  Will set pMftData.
 * @param   pErrInfo            Where to return additional error info.
 */
static int rtFsNtfsVolLoadMft(PRTFSNTFSVOL pThis, PRTERRINFO pErrInfo)
{
    /*
     * Bootstrap the MFT data stream.
     */
    PRTFSNTFSMFTREC pRec = rtFsNtfsVol_NewMftRec(pThis, NTFS_MFT_IDX_MFT);
    AssertReturn(pRec, VERR_NO_MEMORY);

#if 0 && defined(LOG_ENABLED)
    for (uint32_t i = 0; i < 128; i++)
    {
        uint64_t const offDisk = (pThis->uLcnMft << pThis->cClusterShift) + i * pThis->cbMftRecord;
        int rc = RTVfsFileReadAt(pThis->hVfsBacking, offDisk, pRec->pbRec, pThis->cbMftRecord, NULL);
        if (RT_SUCCESS(rc))
        {
            pRec->TreeNode.Key = i;
            rtfsNtfsMftRec_Log(pRec, pThis->cbMftRecord);
            pRec->TreeNode.Key = 0;
        }
    }
#endif

    uint64_t const offDisk = pThis->uLcnMft << pThis->cClusterShift;
    int rc = RTVfsFileReadAt(pThis->hVfsBacking, offDisk, pRec->pbRec, pThis->cbMftRecord, NULL);
    if (RT_SUCCESS(rc))
    {
        rc = rtFsNtfsRec_DoMultiSectorFixups(&pRec->pFileRec->Hdr, pThis->cbMftRecord, true, pErrInfo);
        if (RT_SUCCESS(rc))
        {
#ifdef LOG_ENABLED
            rtfsNtfsMftRec_Log(pRec, pThis->cbMftRecord);
#endif
            rc = rtFsNtfsVol_ParseMft(pThis, pRec, pErrInfo);
        }
        if (RT_SUCCESS(rc))
        {
            PRTFSNTFSCORE pCore = pRec->pCore;
            PRTFSNTFSATTR pMftData;
            pThis->pMftData = pMftData = rtFsNtfsCore_FindUnnamedAttribute(pCore, NTFS_AT_DATA);
            if (pMftData)
            {
                /*
                 * Validate the '$Mft' MFT record.
                 */
                PNTFSATTRIBHDR pAttrHdr = pMftData->pAttrHdr;
                if (!pAttrHdr->fNonResident)
                    rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "MFT record #0 unnamed DATA attribute is resident!");
                else if (   (uint64_t)RT_LE2H_U64(pAttrHdr->u.NonRes.cbAllocated) <  pThis->cbMftRecord * 16U
                         || (uint64_t)RT_LE2H_U64(pAttrHdr->u.NonRes.cbAllocated) >= pThis->cbBacking)
                    rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                 "MFT record #0 unnamed DATA attribute allocated size is out of range: %#RX64",
                                                 RT_LE2H_U64(pAttrHdr->u.NonRes.cbAllocated));
                else if (   (uint64_t)RT_LE2H_U64(pAttrHdr->u.NonRes.cbInitialized) <  pThis->cbMftRecord * 16U
                         || (uint64_t)RT_LE2H_U64(pAttrHdr->u.NonRes.cbInitialized) >= pThis->cbBacking)
                    rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                 "MFT record #0 unnamed DATA attribute initialized size is out of range: %#RX64",
                                                 RT_LE2H_U64(pAttrHdr->u.NonRes.cbInitialized));
                else if (   (uint64_t)RT_LE2H_U64(pAttrHdr->u.NonRes.cbData) <  pThis->cbMftRecord * 16U
                         || (uint64_t)RT_LE2H_U64(pAttrHdr->u.NonRes.cbData) >= pThis->cbBacking)
                    rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                 "MFT record #0 unnamed DATA attribute allocated size is out of range: %#RX64",
                                                 RT_LE2H_U64(pAttrHdr->u.NonRes.cbData));
                else if (pAttrHdr->u.NonRes.uCompressionUnit != 0)
                    rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                 "MFT record #0 unnamed DATA attribute is compressed: %#x",
                                                 pAttrHdr->u.NonRes.uCompressionUnit);
                else if (pMftData->Extents.cExtents == 0)
                    rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                               "MFT record #0 unnamed DATA attribute has no data on the disk");
                else if (pMftData->Extents.paExtents[0].off != offDisk)
                    rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                 "MFT record #0 unnamed DATA attribute has a bogus disk offset: %#RX64, expected %#RX64",
                                                 pMftData->Extents.paExtents[0].off, offDisk);
                else if (!rtFsNtfsCore_FindUnnamedAttribute(pCore, NTFS_AT_BITMAP))
                    rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "MFT record #0 has no unnamed BITMAP attribute!");
                else
                {
                    PRTFSNTFSATTR pFilenameAttr = rtFsNtfsCore_FindUnnamedAttribute(pCore, NTFS_AT_FILENAME);
                    if (!pFilenameAttr)
                        rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "MFT record #0 has no FILENAME attribute!");
                    else if (pFilenameAttr->pAttrHdr->fNonResident)
                        rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "MFT record #0 FILENAME attribute is non-resident!");
                    else if (pFilenameAttr->pAttrHdr->u.Res.cbValue < RT_UOFFSETOF(NTFSATFILENAME, wszFilename[4]))
                        rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                     "MFT record #0 FILENAME attribute value size is too small: %#x",
                                                     pFilenameAttr->pAttrHdr->u.Res.cbValue);
                    else
                    {
                        PNTFSATFILENAME pFilename = (PNTFSATFILENAME)(  (uint8_t *)pFilenameAttr->pAttrHdr
                                                                      + pFilenameAttr->pAttrHdr->u.Res.offValue);
                        if (   pFilename->cwcFilename != 4
                            || RTUtf16NICmpAscii(pFilename->wszFilename, "$Mft", 4) != 0)
                            rc = RTERRINFO_LOG_REL_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                                         "MFT record #0 FILENAME isn't '$Mft': '%.*ls'",
                                                         pFilename->cwcFilename, pFilename->wszFilename);
                        else
                        {
                            /*
                             * Looks like we're good.  Insert core record into the cache.
                             */
                            RTListAppend(&pThis->CoreInUseHead, &pCore->ListEntry);
                            pThis->cbCoreObjects += pCore->cbCost;

                            Assert(pCore->cRefs == 1);
                            Assert(pRec->cRefs == 2);
                            rtFsNtfsMftRec_Release(pRec, pThis);

                            return VINF_SUCCESS;
                        }
                    }
                }
                pThis->pMftData = NULL;
            }
            else
                rc = RTERRINFO_LOG_REL_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "MFT record #0 has no unnamed DATA attribute!");
        }
        if (pRec->pCore)
            rtFsNtfsCore_Destroy(pRec->pCore);
        rtFsNtfsMftRec_Release(pRec, pThis);
    }
    else
        rc = RTERRINFO_LOG_REL_SET(pErrInfo, rc, "Error reading MFT record #0");
    return rc;
}


/**
 * Loads the bootsector and parses it, copying values into the instance data.
 *
 * @returns IRPT status code.
 * @param   pThis           The instance data.
 * @param   pvBuf           The buffer.
 * @param   cbBuf           The buffer size.
 * @param   pErrInfo        Where to return additional error details.
 */
static int rtFsNtfsVolLoadAndParseBootsector(PRTFSNTFSVOL pThis, void *pvBuf, size_t cbBuf, PRTERRINFO pErrInfo)
{
    AssertReturn(cbBuf >= sizeof(FATBOOTSECTOR), VERR_INTERNAL_ERROR_2);

    /*
     * Read the boot sector and check that it makes sense for a NTFS volume.
     *
     * Note! There are two potential backup locations of the boot sector, however we
     *       currently don't implement falling back on these on corruption/read errors.
     */
    PFATBOOTSECTOR pBootSector = (PFATBOOTSECTOR)pvBuf;
    int rc = RTVfsFileReadAt(pThis->hVfsBacking, 0, pBootSector, sizeof(*pBootSector), NULL);
    if (RT_FAILURE(rc))
        return RTERRINFO_LOG_SET(pErrInfo, rc, "Error reading boot sector");

    if (memcmp(pBootSector->achOemName, RT_STR_TUPLE(NTFS_OEM_ID_MAGIC)) != 0)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNKNOWN_FORMAT,
                                   "Not NTFS - OEM field mismatch: %.8Rhxs'", pBootSector->achOemName);

    /* Check must-be-zero BPB fields. */
    if (pBootSector->Bpb.Ntfs.Bpb.cReservedSectors != 0)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNKNOWN_FORMAT, "Not NTFS - MBZ: BPB.cReservedSectors=%u",
                                   RT_LE2H_U16(pBootSector->Bpb.Ntfs.Bpb.cReservedSectors));
    if (pBootSector->Bpb.Ntfs.Bpb.cFats != 0)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNKNOWN_FORMAT, "Not NTFS - MBZ: BPB.cFats=%u",
                                   pBootSector->Bpb.Ntfs.Bpb.cFats);
    if (pBootSector->Bpb.Ntfs.Bpb.cMaxRootDirEntries != 0)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNKNOWN_FORMAT, "Not NTFS - MBZ: BPB.cMaxRootDirEntries=%u",
                                   RT_LE2H_U16(pBootSector->Bpb.Ntfs.Bpb.cMaxRootDirEntries));
    if (pBootSector->Bpb.Ntfs.Bpb.cTotalSectors16 != 0)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNKNOWN_FORMAT, "Not NTFS - MBZ: BPB.cTotalSectors16=%u",
                                   RT_LE2H_U16(pBootSector->Bpb.Ntfs.Bpb.cTotalSectors16));
    if (pBootSector->Bpb.Ntfs.Bpb.cSectorsPerFat != 0)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNKNOWN_FORMAT, "Not NTFS - MBZ: BPB.cSectorsPerFat=%u",
                                   RT_LE2H_U16(pBootSector->Bpb.Ntfs.Bpb.cSectorsPerFat));

    /* Check other relevant BPB fields. */
    uint32_t cbSector = RT_LE2H_U16(pBootSector->Bpb.Ntfs.Bpb.cbSector);
    if (   cbSector != 512
        && cbSector != 1024
        && cbSector != 2048
        && cbSector != 4096)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNKNOWN_FORMAT, "Not NTFS - BPB.cbSector is ouf of range: %u", cbSector);
    pThis->cbSector = cbSector;
    Log2(("NTFS BPB: cbSector=%#x\n", cbSector));

    uint32_t cClusterPerSector = RT_LE2H_U16(pBootSector->Bpb.Ntfs.Bpb.cSectorsPerCluster);
    if (   !RT_IS_POWER_OF_TWO(cClusterPerSector)
        || cClusterPerSector == 0)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNKNOWN_FORMAT,
                                   "Not NTFS - BPB.cCluster is ouf of range: %u", cClusterPerSector);

    pThis->cbCluster = cClusterPerSector * cbSector;
    if (pThis->cbCluster > _64K)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "cluster size exceeds 64KB: %#x", pThis->cbCluster);
    pThis->cClusterShift = ASMBitFirstSetU32(pThis->cbCluster) - 1;
    Log2(("NTFS BPB: cClusterPerSector=%#x => %#x bytes, %u shift\n", cClusterPerSector, pThis->cbCluster, pThis->cClusterShift));
    pThis->iMaxVirtualCluster = (uint64_t)INT64_MAX >> pThis->cClusterShift;
    Log2(("NTFS BPB: iMaxVirtualCluster=%#RX64\n", pThis->iMaxVirtualCluster));

    /* NTFS BPB: cSectors. */
    uint64_t cSectors = RT_LE2H_U64(pBootSector->Bpb.Ntfs.cSectors);
    if (cSectors > pThis->cbBacking / pThis->cbSector)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "NTFS sector count exceeds volume size: %#RX64 vs %#RX64",
                                   cSectors, pThis->cbBacking / pThis->cbSector);
    if (cSectors < 256)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "NTFS sector count too small: %#RX64", cSectors);
    pThis->cbVolume  = cSectors * pThis->cbSector;
    pThis->cClusters = cSectors / cClusterPerSector;
    Log2(("NTFS BPB: cSectors=%#RX64 => %#xu bytes (%Rhcb) => cClusters=%#RX64\n",
          cSectors, pThis->cbVolume, pThis->cbVolume, pThis->cClusters));

    /* NTFS BPB: MFT location. */
    uint64_t uLcn = RT_LE2H_U64(pBootSector->Bpb.Ntfs.uLcnMft);
    if (   uLcn < 1
        || uLcn >= pThis->cClusters)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "NTFS MFT location is out of bounds: %#RX64 (%#RX64 clusters)", uLcn, pThis->cClusters);
    pThis->uLcnMft = uLcn;
    Log2(("NTFS BPB: uLcnMft=%#RX64 (byte offset %#RX64)\n", uLcn, uLcn << pThis->cClusterShift));

    /* NTFS BPB: Mirror MFT location. */
    uLcn = RT_LE2H_U64(pBootSector->Bpb.Ntfs.uLcnMftMirror);
    if (   uLcn < 1
        || uLcn >= pThis->cClusters)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "NTFS mirror MFT location is out of bounds: %#RX64 (%#RX64 clusters)", uLcn, pThis->cClusters);
    pThis->uLcnMftMirror = uLcn;
    Log2(("NTFS BPB: uLcnMftMirror=%#RX64 (byte offset %#RX64)\n", uLcn, uLcn << pThis->cClusterShift));

    /* NTFS BPB: Size of MFT file record. */
    if (pBootSector->Bpb.Ntfs.cClustersPerMftRecord >= 0)
    {
        if (   !RT_IS_POWER_OF_TWO((uint32_t)pBootSector->Bpb.Ntfs.cClustersPerMftRecord)
            || pBootSector->Bpb.Ntfs.cClustersPerMftRecord == 0)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                       "NTFS clusters-per-mft-record value is zero or not a power of two: %#x",
                                        pBootSector->Bpb.Ntfs.cClustersPerMftRecord);
        pThis->cbMftRecord = (uint32_t)pBootSector->Bpb.Ntfs.cClustersPerMftRecord << pThis->cClusterShift;
        Assert(pThis->cbMftRecord == pBootSector->Bpb.Ntfs.cClustersPerMftRecord * pThis->cbCluster);
    }
    else if (   pBootSector->Bpb.Ntfs.cClustersPerMftRecord < -20
             || pBootSector->Bpb.Ntfs.cClustersPerMftRecord > -9)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "NTFS clusters-per-mft-record is out of shift range: %d",
                                    pBootSector->Bpb.Ntfs.cClustersPerMftRecord);
    else
        pThis->cbMftRecord = UINT32_C(1) << -pBootSector->Bpb.Ntfs.cClustersPerMftRecord;
    Log2(("NTFS BPB: cbMftRecord=%#x\n", pThis->cbMftRecord));
    if (   pThis->cbMftRecord > _32K
        || pThis->cbMftRecord < 256)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "Unsupported NTFS MFT record size: %#x", pThis->cbMftRecord);

    /* NTFS BPB: Default index node size */
    if (pBootSector->Bpb.Ntfs.cClustersPerIndexNode >= 0)
    {
        if (   !RT_IS_POWER_OF_TWO((uint32_t)pBootSector->Bpb.Ntfs.cClustersPerIndexNode)
            || pBootSector->Bpb.Ntfs.cClustersPerIndexNode == 0)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                       "NTFS default clusters-per-index-tree-node is zero or not a power of two: %#x",
                                        pBootSector->Bpb.Ntfs.cClustersPerIndexNode);
        pThis->cbDefaultIndexNode = (uint32_t)pBootSector->Bpb.Ntfs.cClustersPerIndexNode << pThis->cClusterShift;
        Assert(pThis->cbDefaultIndexNode == pBootSector->Bpb.Ntfs.cClustersPerIndexNode * pThis->cbCluster);
    }
    else if (   pBootSector->Bpb.Ntfs.cClustersPerIndexNode < -32
             || pBootSector->Bpb.Ntfs.cClustersPerIndexNode > -9)
        return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                   "NTFS default clusters-per-index-tree-node is out of shift range: %d",
                                    pBootSector->Bpb.Ntfs.cClustersPerIndexNode);
    else
        pThis->cbDefaultIndexNode = UINT32_C(1) << -pBootSector->Bpb.Ntfs.cClustersPerMftRecord;
    Log2(("NTFS BPB: cbDefaultIndexNode=%#x\n", pThis->cbDefaultIndexNode));

    pThis->uSerialNo = RT_LE2H_U64(pBootSector->Bpb.Ntfs.uSerialNumber);
    Log2(("NTFS BPB: uSerialNo=%#x\n", pThis->uSerialNo));


    return VINF_SUCCESS;
}


RTDECL(int) RTFsNtfsVolOpen(RTVFSFILE hVfsFileIn, uint32_t fMntFlags, uint32_t fNtfsFlags, PRTVFS phVfs, PRTERRINFO pErrInfo)
{
    AssertPtrReturn(phVfs, VERR_INVALID_POINTER);
    AssertReturn(!(fMntFlags & ~RTVFSMNT_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(!fNtfsFlags, VERR_INVALID_FLAGS);

    uint32_t cRefs = RTVfsFileRetain(hVfsFileIn);
    AssertReturn(cRefs != UINT32_MAX, VERR_INVALID_HANDLE);

    /*
     * Create a VFS instance and initialize the data so rtFsNtfsVol_Close works.
     */
    RTVFS        hVfs;
    PRTFSNTFSVOL pThis;
    int rc = RTVfsNew(&g_rtFsNtfsVolOps, sizeof(*pThis), NIL_RTVFS, RTVFSLOCK_CREATE_RW, &hVfs, (void **)&pThis);
    if (RT_SUCCESS(rc))
    {
        pThis->hVfsBacking    = hVfsFileIn;
        pThis->hVfsSelf       = hVfs;
        pThis->fMntFlags      = fMntFlags;
        pThis->fNtfsFlags     = fNtfsFlags;
        RTListInit(&pThis->CoreInUseHead);
        RTListInit(&pThis->CoreUnusedHead);
        RTListInit(&pThis->IdxNodeUnusedHead);

        rc = RTVfsFileQuerySize(pThis->hVfsBacking, &pThis->cbBacking);
        if (RT_SUCCESS(rc))
        {
            void *pvBuf = RTMemTmpAlloc(_64K);
            if (pvBuf)
            {
                rc = rtFsNtfsVolLoadAndParseBootsector(pThis, pvBuf, _64K, pErrInfo);
                if (RT_SUCCESS(rc))
                    rc = rtFsNtfsVolLoadMft(pThis, pErrInfo);
                if (RT_SUCCESS(rc))
                    rc = rtFsNtfsVolLoadVolumeInfo(pThis, pErrInfo);
                if (RT_SUCCESS(rc))
                    rc = rtFsNtfsVolLoadBitmap(pThis, pErrInfo);
                if (RT_SUCCESS(rc))
                    rc = rtFsNtfsVolLoadUpCase(pThis, pErrInfo);
                if (RT_SUCCESS(rc))
                    rc = rtFsNtfsVolLoadRootDir(pThis, pErrInfo);
                RTMemTmpFree(pvBuf);
                if (RT_SUCCESS(rc))
                {
                    *phVfs = hVfs;
                    return VINF_SUCCESS;
                }
            }
            else
                rc = VERR_NO_TMP_MEMORY;
        }

        RTVfsRelease(hVfs);
        *phVfs = NIL_RTVFS;
    }
    else
        RTVfsFileRelease(hVfsFileIn);

    return rc;
}


/**
 * @interface_method_impl{RTVFSCHAINELEMENTREG,pfnValidate}
 */
static DECLCALLBACK(int) rtVfsChainNtfsVol_Validate(PCRTVFSCHAINELEMENTREG pProviderReg, PRTVFSCHAINSPEC pSpec,
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
static DECLCALLBACK(int) rtVfsChainNtfsVol_Instantiate(PCRTVFSCHAINELEMENTREG pProviderReg, PCRTVFSCHAINSPEC pSpec,
                                                       PCRTVFSCHAINELEMSPEC pElement, RTVFSOBJ hPrevVfsObj,
                                                       PRTVFSOBJ phVfsObj, uint32_t *poffError, PRTERRINFO pErrInfo)
{
    RT_NOREF(pProviderReg, pSpec, poffError);

    int         rc;
    RTVFSFILE   hVfsFileIn = RTVfsObjToFile(hPrevVfsObj);
    if (hVfsFileIn != NIL_RTVFSFILE)
    {
        RTVFS hVfs;
        rc = RTFsNtfsVolOpen(hVfsFileIn, (uint32_t)pElement->uProvider, (uint32_t)(pElement->uProvider >> 32), &hVfs, pErrInfo);
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
static DECLCALLBACK(bool) rtVfsChainNtfsVol_CanReuseElement(PCRTVFSCHAINELEMENTREG pProviderReg,
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
static RTVFSCHAINELEMENTREG g_rtVfsChainNtfsVolReg =
{
    /* uVersion = */            RTVFSCHAINELEMENTREG_VERSION,
    /* fReserved = */           0,
    /* pszName = */             "ntfs",
    /* ListEntry = */           { NULL, NULL },
    /* pszHelp = */             "Open a NTFS file system, requires a file object on the left side.\n"
                                "First argument is an optional 'ro' (read-only) or 'rw' (read-write) flag.\n",
    /* pfnValidate = */         rtVfsChainNtfsVol_Validate,
    /* pfnInstantiate = */      rtVfsChainNtfsVol_Instantiate,
    /* pfnCanReuseElement = */  rtVfsChainNtfsVol_CanReuseElement,
    /* uEndMarker = */          RTVFSCHAINELEMENTREG_VERSION
};

RTVFSCHAIN_AUTO_REGISTER_ELEMENT_PROVIDER(&g_rtVfsChainNtfsVolReg, rtVfsChainNtfsVolReg);

