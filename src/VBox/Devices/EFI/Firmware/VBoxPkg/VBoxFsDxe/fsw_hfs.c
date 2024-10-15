/* $Id: fsw_hfs.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * fsw_hfs.c - HFS file system driver code, see
 *
 *   https://developer.apple.com/legacy/library/technotes/tn/tn1150.html
 *   (formerly http://developer.apple.com/technotes/tn/tn1150.html)
 *
 * Current limitations:
 *  - Doesn't support permissions
 *  - Complete Unicode case-insensitiveness disabled (large tables)
 *  - No links
 *  - Only supports pure HFS+ (i.e. no HFS, or HFS+ embedded to HFS)
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#include "fsw_hfs.h"

#ifdef HOST_POSIX
#include <assert.h>
#define DPRINT(x) printf(x)
#define DPRINT2(x,y) printf(x,y)
#define BP(msg)    do { printf("ERROR: %s", msg); assert(0); } while (0)
#elif defined DEBUG_LEVEL
#define CONCAT(x,y) x##y
#define DPRINT(x) Print(CONCAT(L,x))
#define DPRINT2(x,y) Print(CONCAT(L,x), y)
#define BP(msg) DPRINT(msg)
#else
#include <Library/PrintLib.h>
#define DPRINT(x) do { } while (0)
#define DPRINT2(x,y) do { } while (0)
#define BP(msg) do { } while (0)
#endif

// functions
#if 0
void dump_str(fsw_u16* p, fsw_u32 len, int swap)
{
    int i;

    for (i=0; i<len; i++)
    {
        fprintf(stderr, "%c", swap ? be16_to_cpu(p[i]) : p[i]);
    }
    fprintf(stderr, "\n");
}
#endif

static fsw_status_t fsw_hfs_volume_mount(struct fsw_hfs_volume *vol);
static void         fsw_hfs_volume_free(struct fsw_hfs_volume *vol);
static fsw_status_t fsw_hfs_volume_stat(struct fsw_hfs_volume *vol, struct fsw_volume_stat *sb);

static fsw_status_t fsw_hfs_dnode_fill(struct fsw_hfs_volume *vol, struct fsw_hfs_dnode *dno);
static void         fsw_hfs_dnode_free(struct fsw_hfs_volume *vol, struct fsw_hfs_dnode *dno);
static fsw_status_t fsw_hfs_dnode_stat(struct fsw_hfs_volume *vol, struct fsw_hfs_dnode *dno,
                                           struct fsw_dnode_stat *sb);
static fsw_status_t fsw_hfs_get_extent(struct fsw_hfs_volume *vol, struct fsw_hfs_dnode *dno,
                                           struct fsw_extent *extent);

static fsw_status_t fsw_hfs_dir_lookup(struct fsw_hfs_volume *vol, struct fsw_hfs_dnode *dno,
                                           struct fsw_string *lookup_name, struct fsw_hfs_dnode **child_dno);
static fsw_status_t fsw_hfs_dir_read(struct fsw_hfs_volume *vol, struct fsw_hfs_dnode *dno,
                                         struct fsw_shandle *shand, struct fsw_hfs_dnode **child_dno);
#if 0
static fsw_status_t fsw_hfs_read_dirrec(struct fsw_shandle *shand, struct hfs_dirrec_buffer *dirrec_buffer);
#endif

static fsw_status_t fsw_hfs_readlink(struct fsw_hfs_volume *vol, struct fsw_hfs_dnode *dno,
                                         struct fsw_string *link);

//
// Dispatch Table
//

struct fsw_fstype_table   FSW_FSTYPE_TABLE_NAME(hfs) = {
    { FSW_STRING_TYPE_ISO88591, 4, 4, "hfs" },
    sizeof(struct fsw_hfs_volume),
    sizeof(struct fsw_hfs_dnode),

    fsw_hfs_volume_mount,
    fsw_hfs_volume_free,
    fsw_hfs_volume_stat,
    fsw_hfs_dnode_fill,
    fsw_hfs_dnode_free,
    fsw_hfs_dnode_stat,
    fsw_hfs_get_extent,
    fsw_hfs_dir_lookup,
    fsw_hfs_dir_read,
    fsw_hfs_readlink,
};

static fsw_s32
fsw_hfs_read_block (struct fsw_hfs_dnode    * dno,
                    fsw_u32                   log_bno,
                    fsw_u32                   off,
                    fsw_s32                   len,
                    fsw_u8                  * buf)
{
    fsw_status_t          status;
    struct fsw_extent     extent;
    fsw_u32               phys_bno;
    fsw_u8*                 buffer;

    extent.log_start = log_bno;
    status = fsw_hfs_get_extent(dno->g.vol, dno, &extent);
    if (status)
        return status;

    phys_bno = extent.phys_start;
    status = fsw_block_get(dno->g.vol, phys_bno, 0, (void **)&buffer);
    if (status)
        return status;

    fsw_memcpy(buf, buffer + off, len);

    fsw_block_release(dno->g.vol, phys_bno, buffer);

    return FSW_SUCCESS;

}

/* Read data from HFS file. */
static fsw_s32
fsw_hfs_read_file (struct fsw_hfs_dnode    * dno,
                   fsw_u64                   pos,
                   fsw_s32                   len,
                   fsw_u8                  * buf)
{

    fsw_status_t          status;
    fsw_u32               log_bno;
    fsw_u32               block_size_bits = dno->g.vol->block_size_shift;
    fsw_u32               block_size = (1 << block_size_bits);
    fsw_u32               block_size_mask = block_size - 1;
    fsw_s32               read = 0;

    while (len > 0)
    {
        fsw_u32 off = (fsw_u32)(pos & block_size_mask);
        fsw_s32 next_len = len;

        log_bno = (fsw_u32)RShiftU64(pos, block_size_bits);

        if (   next_len >= 0
            && (fsw_u32)next_len >  block_size)
            next_len = block_size;
        status = fsw_hfs_read_block(dno, log_bno, off, next_len, buf);
        if (status)
            return -1;
        buf  += next_len;
        pos  += next_len;
        len  -= next_len;
        read += next_len;
    }

    return read;
}


static fsw_s32
fsw_hfs_compute_shift(fsw_u32 size)
{
    fsw_s32 i;

    for (i=0; i<32; i++)
    {
        if ((size >> i) == 0)
            return i - 1;
    }

    BP("BUG\n");
    return 0;
}

/**
 * Mount an HFS+ volume. Reads the superblock and constructs the
 * root directory dnode.
 */

static fsw_status_t fsw_hfs_volume_mount(struct fsw_hfs_volume *vol)
{
    fsw_status_t           status, rv;
    void                  *buffer = NULL;
    HFSPlusVolumeHeader   *voldesc;
    fsw_u32                blockno;
    struct fsw_string      s;

    rv = FSW_UNSUPPORTED;

    vol->primary_voldesc = NULL;
    fsw_set_blocksize(vol, HFS_BLOCKSIZE, HFS_BLOCKSIZE);
    blockno = HFS_SUPERBLOCK_BLOCKNO;

#define CHECK(s)         \
        if (status)  {   \
            rv = status; \
            break;       \
        }

    vol->emb_block_off = 0;
    vol->hfs_kind = 0;
    do {
        fsw_u16       signature;
        BTHeaderRec   tree_header;
        fsw_s32       r;
        fsw_u32       block_size;

        status = fsw_block_get(vol, blockno, 0, &buffer);
        CHECK(status);
        voldesc = (HFSPlusVolumeHeader *)buffer;
        signature = be16_to_cpu(voldesc->signature);

        if ((signature == kHFSPlusSigWord) || (signature == kHFSXSigWord))
        {
            if (vol->hfs_kind == 0)
            {
                DPRINT("found HFS+\n");
                vol->hfs_kind = FSW_HFS_PLUS;
            }
        }
        else if (signature == kHFSSigWord)
        {
            HFSMasterDirectoryBlock* mdb = (HFSMasterDirectoryBlock*)buffer;

            if (be16_to_cpu(mdb->drEmbedSigWord) == kHFSPlusSigWord)
            {
                DPRINT("found HFS+ inside HFS, untested\n");
                vol->hfs_kind = FSW_HFS_PLUS_EMB;
                vol->emb_block_off = be32_to_cpu(mdb->drEmbedExtent.startBlock);
                blockno += vol->emb_block_off;
                /* retry */
                continue;
            }
            else
            {
                DPRINT("found plain HFS, unsupported\n");
                vol->hfs_kind = FSW_HFS_PLAIN;
            }
            rv = FSW_UNSUPPORTED;
            break;
        }
        else
        {
            rv = FSW_UNSUPPORTED;
            break;
        }

        status = fsw_memdup((void **)&vol->primary_voldesc, voldesc,
                            sizeof(*voldesc));
        CHECK(status);


        block_size = be32_to_cpu(voldesc->blockSize);
        vol->block_size_shift = fsw_hfs_compute_shift(block_size);

        fsw_block_release(vol, blockno, buffer);
        buffer = NULL;
        voldesc = NULL;
        fsw_set_blocksize(vol, block_size, block_size);

        /* get volume name */
        s.type = FSW_STRING_TYPE_ISO88591;
        s.size = s.len = kHFSMaxVolumeNameChars;
        s.data = "HFS+ volume\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"; /* Otherwise buffer overflow reading beyond the end of the buffer. */
        status = fsw_strdup_coerce(&vol->g.label, vol->g.host_string_type, &s);
        CHECK(status);

        /* Setup catalog dnode */
        status = fsw_dnode_create_root(vol, kHFSCatalogFileID, &vol->catalog_tree.file);
        CHECK(status);
        fsw_memcpy (vol->catalog_tree.file->extents,
                    vol->primary_voldesc->catalogFile.extents,
                    sizeof vol->catalog_tree.file->extents);
        vol->catalog_tree.file->g.size =
                be64_to_cpu(vol->primary_voldesc->catalogFile.logicalSize);

        /* Setup extents overflow file */
        status = fsw_dnode_create_root(vol, kHFSExtentsFileID, &vol->extents_tree.file);
        fsw_memcpy (vol->extents_tree.file->extents,
                    vol->primary_voldesc->extentsFile.extents,
                    sizeof vol->extents_tree.file->extents);
        vol->extents_tree.file->g.size =
                be64_to_cpu(vol->primary_voldesc->extentsFile.logicalSize);

        /* Setup the root dnode */
        status = fsw_dnode_create_root(vol, kHFSRootFolderID, &vol->g.root);
        CHECK(status);

        /*
         * Read catalog file, we know that first record is in the first node, right after
         * the node descriptor.
         */
        r = fsw_hfs_read_file(vol->catalog_tree.file,
                              sizeof (BTNodeDescriptor),
                              sizeof (BTHeaderRec), (fsw_u8 *) &tree_header);
        if (r <= 0)
        {
            status = FSW_VOLUME_CORRUPTED;
            break;
        }
        vol->case_sensitive =
                (signature == kHFSXSigWord) &&
                (tree_header.keyCompareType == kHFSBinaryCompare);
        vol->catalog_tree.root_node = be32_to_cpu (tree_header.rootNode);
        vol->catalog_tree.node_size = be16_to_cpu (tree_header.nodeSize);

        /* Read extents overflow file */
        r = fsw_hfs_read_file(vol->extents_tree.file,
                              sizeof (BTNodeDescriptor),
                              sizeof (BTHeaderRec), (fsw_u8 *) &tree_header);
        if (r <= 0)
        {
            status = FSW_VOLUME_CORRUPTED;
            break;
        }

        vol->extents_tree.root_node = be32_to_cpu (tree_header.rootNode);
        vol->extents_tree.node_size = be16_to_cpu (tree_header.nodeSize);

        rv = FSW_SUCCESS;
    } while (0);

#undef CHECK


    if (buffer != NULL)
        fsw_block_release(vol, blockno, buffer);

    return rv;
}

/**
 * Free the volume data structure. Called by the core after an unmount or after
 * an unsuccessful mount to release the memory used by the file system type specific
 * part of the volume structure.
 */

static void fsw_hfs_volume_free(struct fsw_hfs_volume *vol)
{
    if (vol->primary_voldesc)
    {
        fsw_free(vol->primary_voldesc);
        vol->primary_voldesc = NULL;
    }
}

/**
 * Get in-depth information on a volume.
 */

static fsw_status_t fsw_hfs_volume_stat(struct fsw_hfs_volume *vol, struct fsw_volume_stat *sb)
{
    sb->total_bytes = be32_to_cpu(vol->primary_voldesc->totalBlocks) << vol->block_size_shift;
    sb->free_bytes = be32_to_cpu(vol->primary_voldesc->freeBlocks) << vol->block_size_shift;
    return FSW_SUCCESS;
}

/**
 * Get full information on a dnode from disk. This function is called by the core
 * whenever it needs to access fields in the dnode structure that may not
 * be filled immediately upon creation of the dnode.
 */

static fsw_status_t fsw_hfs_dnode_fill(struct fsw_hfs_volume *vol, struct fsw_hfs_dnode *dno)
{
    return FSW_SUCCESS;
}

/**
 * Free the dnode data structure. Called by the core when deallocating a dnode
 * structure to release the memory used by the file system type specific part
 * of the dnode structure.
 */

static void fsw_hfs_dnode_free(struct fsw_hfs_volume *vol, struct fsw_hfs_dnode *dno)
{
}

static fsw_u32 mac_to_posix(fsw_u32 mac_time)
{
  /* Mac time is 1904 year based */
  return mac_time ?  mac_time - 2082844800 : 0;
}

/**
 * Get in-depth information on a dnode. The core makes sure that fsw_hfs_dnode_fill
 * has been called on the dnode before this function is called. Note that some
 * data is not directly stored into the structure, but passed to a host-specific
 * callback that converts it to the host-specific format.
 */

static fsw_status_t fsw_hfs_dnode_stat(struct fsw_hfs_volume *vol,
                                       struct fsw_hfs_dnode  *dno,
                                       struct fsw_dnode_stat *sb)
{
  sb->used_bytes = dno->used_bytes;
  sb->store_time_posix(sb, FSW_DNODE_STAT_CTIME, mac_to_posix(dno->ctime));
  sb->store_time_posix(sb, FSW_DNODE_STAT_MTIME, mac_to_posix(dno->mtime));
  sb->store_time_posix(sb, FSW_DNODE_STAT_ATIME, 0);
  sb->store_attr_posix(sb, 0700);

  return FSW_SUCCESS;
}

static int
fsw_hfs_find_block(HFSPlusExtentRecord * exts,
                   fsw_u32             * lbno,
                   fsw_u32             * pbno)
{
    int i;
    fsw_u32 cur_lbno = *lbno;

    for (i = 0; i < 8; i++)
    {
        fsw_u32 start = be32_to_cpu ((*exts)[i].startBlock);
        fsw_u32 count = be32_to_cpu ((*exts)[i].blockCount);

        if (cur_lbno < count)
        {
            *pbno = start + cur_lbno;
            return 1;
        }

        cur_lbno -= count;
    }

    *lbno = cur_lbno;

    return 0;
}

/* Find record offset, numbering starts from the end */
static fsw_u32
fsw_hfs_btree_recoffset (struct fsw_hfs_btree * btree,
                         BTNodeDescriptor     * node,
                         fsw_u32                index)
{
  fsw_u8 *cnode = (fsw_u8 *) node;
  fsw_u16 *recptr;
  recptr = (fsw_u16 *) (cnode+btree->node_size - index * 2 - 2);
  return be16_to_cpu(*recptr);
}

/* Pointer to the key inside node */
static BTreeKey *
fsw_hfs_btree_rec (struct fsw_hfs_btree   * btree,
                   BTNodeDescriptor       * node,
                   fsw_u32                  index)
{
  fsw_u8 *cnode = (fsw_u8 *) node;
  fsw_u32 offset;
  offset = fsw_hfs_btree_recoffset (btree, node, index);
  return (BTreeKey *) (cnode + offset);
}


static fsw_status_t
fsw_hfs_btree_search (struct fsw_hfs_btree * btree,
                      BTreeKey             * key,
                      int (*compare_keys) (BTreeKey* key1, BTreeKey* key2),
                      BTNodeDescriptor    ** result,
                      fsw_u32              * key_offset)
{
    BTNodeDescriptor* node;
    fsw_u32 currnode;
    fsw_u32 rec;
    fsw_status_t status;
    fsw_u8* buffer = NULL;

    currnode = btree->root_node;
    status = fsw_alloc(btree->node_size, &buffer);
    if (status)
        return status;
    node = (BTNodeDescriptor*)buffer;

    while (1)
    {
        int cmp = 0;
        int match;
        fsw_u32 count;

    readnode:
        match = 0;
        /* Read a node.  */
        if (fsw_hfs_read_file (btree->file,
                               (fsw_u64)currnode * btree->node_size,
                               btree->node_size, buffer) <= 0)
        {
            status = FSW_VOLUME_CORRUPTED;
            break;
        }

        if (be16_to_cpu(*(fsw_u16*)(buffer + btree->node_size - 2)) != sizeof(BTNodeDescriptor))
            BP("corrupted node\n");

        count = be16_to_cpu (node->numRecords);

#if 1
        for (rec = 0; rec < count; rec++)
        {
             BTreeKey *currkey;

             currkey = fsw_hfs_btree_rec (btree, node, rec);
             cmp = compare_keys (currkey, key);
             //fprintf(stderr, "rec=%d cmp=%d kind=%d \n", rec, cmp, node->kind);

             /* Leaf node. */
             if (node->kind == kBTLeafNode)
             {
               if (cmp == 0)
               {
                 /* Found!  */
                 *result = node;
                 *key_offset = rec;

                 status = FSW_SUCCESS;
                 goto done;
               }
             }
             else if (node->kind == kBTIndexNode)
             {
                 fsw_u32 *pointer;

                 if (cmp > 0)
                     break;

                 pointer = (fsw_u32 *) ((char *) currkey
                                        + be16_to_cpu (currkey->length16)
                                        + 2);
                 currnode = be32_to_cpu (*pointer);
                 match = 1;
             }
        }

        if (node->kind == kBTLeafNode && cmp < 0 && node->fLink)
        {
            currnode = be32_to_cpu(node->fLink);
            goto readnode;
        }
        else if (!match)
        {
            status = FSW_NOT_FOUND;
            break;
        }
#else
         /* Perform binary search */
         fsw_u32 lower = 0;
         fsw_u32 upper = count - 1;
         fsw_s32 cmp = -1;
         BTreeKey *currkey = NULL;

         if (count == 0)
         {
             status = FSW_NOT_FOUND;
             goto done;
         }

         while (lower <= upper)
         {
             fsw_u32 index = (lower + upper) / 2;

             currkey = fsw_hfs_btree_rec (btree, node, index);

             cmp = compare_keys (currkey, key);
             if (cmp < 0)  upper = index - 1;
             if (cmp > 0)  lower = index + 1;
             if (cmp == 0)
             {
                 /* Found!  */
                 *result = node;
                 *key_offset = rec;

                 status = FSW_SUCCESS;
                 goto done;
             }
         }

         if (cmp < 0)
             currkey = fsw_hfs_btree_rec (btree, node, upper);

         if (node->kind == kBTIndexNode && currkey)
         {
             fsw_u32 *pointer;

             pointer = (fsw_u32 *) ((char *) currkey
                                    + be16_to_cpu (currkey->length16)
                                    + 2);
             currnode = be32_to_cpu (*pointer);
         }
         else
         {
             status = FSW_NOT_FOUND;
             break;
         }
#endif
    }


  done:
    if (buffer != NULL && status != FSW_SUCCESS)
        fsw_free(buffer);

    return status;
}

typedef struct
{
    fsw_u32                 id;
    fsw_u32                 type;
    struct fsw_string     * name;
    fsw_u64                 size;
    fsw_u64                 used;
    fsw_u32                 ctime;
    fsw_u32                 mtime;
    fsw_u32                 node_num;
    HFSPlusExtentRecord     extents;
} file_info_t;

typedef struct
{
    fsw_u32                 cur_pos; /* current position */
    fsw_u32                 parent;
    struct fsw_hfs_volume * vol;

    struct fsw_shandle *    shandle; /* this one track iterator's state */
    file_info_t             file_info;
} visitor_parameter_t;

static void hfs_fill_info(struct fsw_hfs_volume *vol, HFSPlusCatalogKey *file_key, file_info_t *file_info)
{
    fsw_u8    * base;
    fsw_u16     rec_type;

    /* for plain HFS "-(keySize & 1)" would be needed */
    base = (fsw_u8*)file_key + be16_to_cpu(file_key->keyLength) + 2;
    rec_type =  be16_to_cpu(*(fsw_u16*)base);

    /** @todo read additional info */
    switch (rec_type)
    {
        case kHFSPlusFolderRecord:
        {
            HFSPlusCatalogFolder* info = (HFSPlusCatalogFolder*)base;

            file_info->id = be32_to_cpu(info->folderID);
            file_info->type = FSW_DNODE_TYPE_DIR;
            /** @todo return number of elements, maybe use smth else */
            file_info->size = be32_to_cpu(info->valence);
            file_info->used = be32_to_cpu(info->valence);
            file_info->ctime = be32_to_cpu(info->createDate);
            file_info->mtime = be32_to_cpu(info->contentModDate);
            break;
        }
        case kHFSPlusFileRecord:
        {
            HFSPlusCatalogFile* info = (HFSPlusCatalogFile*)base;
            uint32_t    creator = be32_to_cpu(info->userInfo.fdCreator);
            uint32_t    crtype  = be32_to_cpu(info->userInfo.fdType);

            file_info->id = be32_to_cpu(info->fileID);
            file_info->type = FSW_DNODE_TYPE_FILE;
            file_info->size = be64_to_cpu(info->dataFork.logicalSize);
            file_info->used = LShiftU64(be32_to_cpu(info->dataFork.totalBlocks), vol->block_size_shift);
            file_info->ctime = be32_to_cpu(info->createDate);
            file_info->mtime = be32_to_cpu(info->contentModDate);
            fsw_memcpy(&file_info->extents, &info->dataFork.extents,
                       sizeof file_info->extents);
            if (creator == kHFSPlusCreator && crtype == kHardLinkFileType)
            {
                /* Only hard links currently supported. */
                file_info->type     = FSW_DNODE_TYPE_SYMLINK;
                file_info->node_num = be32_to_cpu(info->bsdInfo.special.iNodeNum);
            }
            break;
        }
        case kHFSPlusFolderThreadRecord:
        case kHFSPlusFileThreadRecord:
        {
            /* Do nothing. */
            break;
        }
        default:
            BP("unknown file type\n");
            file_info->type = FSW_DNODE_TYPE_UNKNOWN;

            break;
    }
}

static int
fsw_hfs_btree_visit_node(BTreeKey *record, void* param)
{
    visitor_parameter_t* vp = (visitor_parameter_t*)param;
    fsw_u8* base = (fsw_u8*)record->rawData + be16_to_cpu(record->length16) + 2;
    fsw_u16 rec_type =  be16_to_cpu(*(fsw_u16*)base);
    struct HFSPlusCatalogKey* cat_key = (HFSPlusCatalogKey*)record;
    fsw_u16   name_len;
    fsw_u16   *name_ptr;
    fsw_u32   i;
    struct fsw_string * file_name;

    if (be32_to_cpu(cat_key->parentID) != vp->parent)
        return -1;

    /* not smth we care about */
    if (vp->shandle->pos != vp->cur_pos++)
        return 0;

    if (rec_type == kHFSPlusFolderThreadRecord || rec_type == kHFSPlusFileThreadRecord)
    {
        vp->shandle->pos++;
        return 0;
    }

    hfs_fill_info(vp->vol, cat_key, &vp->file_info);

    name_len = be16_to_cpu(cat_key->nodeName.length);

    file_name =  vp->file_info.name;
    file_name->len = name_len;
    fsw_memdup(&file_name->data, &cat_key->nodeName.unicode[0], 2*name_len);
    file_name->size = 2*name_len;
    file_name->type = FSW_STRING_TYPE_UTF16;
    name_ptr = (fsw_u16*)file_name->data;
    for (i=0; i<name_len; i++)
    {
        name_ptr[i] = be16_to_cpu(name_ptr[i]);
    }
    vp->shandle->pos++;

    return 1;
}

static fsw_status_t
fsw_hfs_btree_iterate_node (struct fsw_hfs_btree * btree,
                            BTNodeDescriptor     * first_node,
                            fsw_u32                first_rec,
                            int                    (*callback) (BTreeKey *record, void* param),
                            void                  * param)
{
  fsw_status_t status;
  /* We modify node, so make a copy */
  BTNodeDescriptor*     node = first_node;
  fsw_u8* buffer = NULL;

  status = fsw_alloc(btree->node_size, &buffer);
  if (status)
      return status;

  while (1)
  {
      fsw_u32 i;
      fsw_u32 count =  be16_to_cpu(node->numRecords);
      fsw_u32 next_node;

      /* Iterate over all records in this node.  */
      for (i = first_rec; i < count; i++)
      {
          int rv = callback(fsw_hfs_btree_rec (btree, node, i), param);

          switch (rv)
          {
              case 1:
                  status = FSW_SUCCESS;
                  goto done;
              case -1:
                  status = FSW_NOT_FOUND;
                  goto done;
          }
          /* if callback returned 0 - continue */
      }

      next_node = be32_to_cpu(node->fLink);

      if (!next_node)
      {
          status = FSW_NOT_FOUND;
          break;
      }

      if (fsw_hfs_read_file (btree->file,
                             next_node * btree->node_size,
                             btree->node_size, buffer) <= 0)
      {
          status = FSW_VOLUME_CORRUPTED;
          return 1;
      }

      node = (BTNodeDescriptor*)buffer;
      first_rec = 0;
  }
 done:
  if (buffer)
      fsw_free(buffer);

  return status;
}

#if 0
void deb(fsw_u16* p, int len, int swap)
{
    int i;
    for (i=0; i<len; i++)
    {
      printf("%c", swap ?  be16_to_cpu(p[i]) : p[i]);
    }
    printf("\n");
}
#endif

static int
fsw_hfs_cmp_extkey(BTreeKey* key1, BTreeKey* key2)
{
    HFSPlusExtentKey* ekey1 = (HFSPlusExtentKey*)key1;
    HFSPlusExtentKey* ekey2 = (HFSPlusExtentKey*)key2;
    int result;

    /* First key is read from the FS data, second is in-memory in CPU endianess */
    result = be32_to_cpu(ekey1->fileID) - ekey2->fileID;

    if (result)
        return result;

    result = ekey1->forkType - ekey2->forkType;

    if (result)
        return result;

    result = be32_to_cpu(ekey1->startBlock) - ekey2->startBlock;
    return result;
}

static int
fsw_hfs_cmp_catkey (BTreeKey *key1, BTreeKey *key2)
{
  HFSPlusCatalogKey *ckey1 = (HFSPlusCatalogKey*)key1;
  HFSPlusCatalogKey *ckey2 = (HFSPlusCatalogKey*)key2;

  int      apos, bpos, lc;
  fsw_u16  ac, bc;
  fsw_u32  parentId1;
  int      key1Len;
  fsw_u16 *p1;
  fsw_u16 *p2;

  parentId1 = be32_to_cpu(ckey1->parentID);

  if (parentId1 > ckey2->parentID)
      return 1;
  if (parentId1 < ckey2->parentID)
      return -1;

  p1 = &ckey1->nodeName.unicode[0];
  p2 = &ckey2->nodeName.unicode[0];
  key1Len = be16_to_cpu (ckey1->nodeName.length);
  apos = bpos = 0;

  while(1)
  {
    /* get next valid character from ckey1 */
    for (lc = 0; lc == 0 && apos < key1Len; apos++) {
      ac = be16_to_cpu(p1[apos]);
      lc = ac;
    };
    ac = (fsw_u16)lc;

    /* get next valid character from ckey2 */
    for (lc = 0; lc == 0 && bpos < ckey2->nodeName.length; bpos++) {
      bc = p2[bpos];
      lc = bc;
    };
    bc = (fsw_u16)lc;

    if (ac != bc || (ac == 0  && bc == 0))
      return ac - bc;
  }
}

static int
fsw_hfs_cmpi_catkey (BTreeKey *key1, BTreeKey *key2)
{
  HFSPlusCatalogKey *ckey1 = (HFSPlusCatalogKey*)key1;
  HFSPlusCatalogKey *ckey2 = (HFSPlusCatalogKey*)key2;

  int      apos, bpos, lc;
  fsw_u16  ac, bc;
  fsw_u32  parentId1;
  int      key1Len;
  fsw_u16 *p1;
  fsw_u16 *p2;

  parentId1 = be32_to_cpu(ckey1->parentID);

  if (parentId1 > ckey2->parentID)
      return 1;
  if (parentId1 < ckey2->parentID)
      return -1;

  key1Len = be16_to_cpu (ckey1->nodeName.length);

  if (key1Len == 0 && ckey2->nodeName.length == 0)
      return 0;

  p1 = &ckey1->nodeName.unicode[0];
  p2 = &ckey2->nodeName.unicode[0];

  apos = bpos = 0;

  while(1)
  {
    /* get next valid (non-zero) character from ckey1 */
    for (lc = 0; lc == 0 && apos < key1Len; apos++) {
      ac = be16_to_cpu(p1[apos]);
      lc = fsw_to_lower(ac);    /* NB: 0x0000 is translated to 0xffff */
    };
    ac = (fsw_u16)lc;

    /* get next valid (non-zero) character from ckey2 */
    for (lc = 0; lc == 0 && bpos < ckey2->nodeName.length; bpos++) {
      bc = p2[bpos];
      lc = fsw_to_lower(bc);    /* NB: 0x0000 is translated to 0xffff */
    };
    bc = (fsw_u16)lc;

    if (ac != bc || (ac == 0  && bc == 0))
      return ac - bc;
  }
}

/**
 * Retrieve file data mapping information. This function is called by the core when
 * fsw_shandle_read needs to know where on the disk the required piece of the file's
 * data can be found. The core makes sure that fsw_hfs_dnode_fill has been called
 * on the dnode before. Our task here is to get the physical disk block number for
 * the requested logical block number.
 */

static fsw_status_t fsw_hfs_get_extent(struct fsw_hfs_volume * vol,
                                       struct fsw_hfs_dnode  * dno,
                                       struct fsw_extent     * extent)
{
    fsw_status_t         status;
    fsw_u32              lbno;
    HFSPlusExtentRecord  *exts;
    BTNodeDescriptor     *node = NULL;

    extent->type = FSW_EXTENT_TYPE_PHYSBLOCK;
    extent->log_count = 1;
    lbno = extent->log_start;

    /* we only care about data forks atm, do we? */
    exts = &dno->extents;

    while (1)
    {
        struct HFSPlusExtentKey* key;
        struct HFSPlusExtentKey  overflowkey;
        fsw_u32                  ptr;
        fsw_u32                  phys_bno;

        if (fsw_hfs_find_block(exts, &lbno, &phys_bno))
        {
            extent->phys_start = phys_bno + vol->emb_block_off;
            status = FSW_SUCCESS;
            break;
        }


        /* Find appropriate overflow record */
        overflowkey.fileID = dno->g.dnode_id;
        overflowkey.startBlock = extent->log_start - lbno;

        if (node != NULL)
        {
            fsw_free(node);
            node = NULL;
        }

        status = fsw_hfs_btree_search (&vol->extents_tree,
                                       (BTreeKey*)&overflowkey,
                                       fsw_hfs_cmp_extkey,
                                       &node, &ptr);
        if (status)
            break;

        key = (struct HFSPlusExtentKey *)
                fsw_hfs_btree_rec (&vol->extents_tree, node, ptr);
        exts = (HFSPlusExtentRecord*) (key + 1);
    }

    if (node != NULL)
        fsw_free(node);

    return status;
}

static const fsw_u16* g_blacklist[] =
{
    //L"AppleIntelCPUPowerManagement.kext",
    NULL
};


//#define HFS_FILE_INJECTION

#ifdef HFS_FILE_INJECTION
static struct
{
  const fsw_u16* path;
  const fsw_u16* name;
} g_injectList[] =
{
  {
    L"/System/Library/Extensions",
    L"ApplePS2Controller.kext"
  },
  {
    NULL,
    NULL
  }
};
#endif

static fsw_status_t
create_hfs_dnode(struct fsw_hfs_dnode  * dno,
                 file_info_t           * file_info,
                 struct fsw_hfs_dnode ** child_dno_out)
{
    fsw_status_t           status;
    struct fsw_hfs_dnode * baby;

    status = fsw_dnode_create(dno, file_info->id, file_info->type,
                              file_info->name, &baby);
    if (status)
        return status;

    baby->g.size = file_info->size;
    baby->used_bytes = file_info->used;
    baby->ctime = file_info->ctime;
    baby->mtime = file_info->mtime;
    baby->node_num = file_info->node_num;


    /* Fill-in extents info */
    if (file_info->type == FSW_DNODE_TYPE_FILE)
    {
        fsw_memcpy(baby->extents, &file_info->extents, sizeof file_info->extents);
    }

    *child_dno_out = baby;

    return FSW_SUCCESS;
}


/**
 * Lookup a directory's child dnode by name. This function is called on a directory
 * to retrieve the directory entry with the given name. A dnode is constructed for
 * this entry and returned. The core makes sure that fsw_hfs_dnode_fill has been called
 * and the dnode is actually a directory.
 */

static fsw_status_t fsw_hfs_dir_lookup(struct fsw_hfs_volume * vol,
                                       struct fsw_hfs_dnode  * dno,
                                       struct fsw_string     * lookup_name,
                                       struct fsw_hfs_dnode ** child_dno_out)
{
    fsw_status_t               status;
    struct HFSPlusCatalogKey   catkey;
    fsw_u32                    ptr;
    BTNodeDescriptor *         node = NULL;
    struct fsw_string          rec_name;
    int                        free_data = 0, i;
    HFSPlusCatalogKey*         file_key;
    file_info_t                file_info;


    fsw_memzero(&file_info, sizeof file_info);
    file_info.name = &rec_name;

    catkey.parentID = dno->g.dnode_id;
    catkey.nodeName.length = (fsw_u16)lookup_name->len;

    /* no need to allocate anything */
    if (lookup_name->type == FSW_STRING_TYPE_UTF16)
    {
        fsw_memcpy(catkey.nodeName.unicode, lookup_name->data, lookup_name->size);
        rec_name = *lookup_name;
    } else
    {
        status = fsw_strdup_coerce(&rec_name, FSW_STRING_TYPE_UTF16, lookup_name);
        /* nothing allocated so far */
        if (status)
            goto done;
        free_data = 1;
        fsw_memcpy(catkey.nodeName.unicode, rec_name.data, rec_name.size);
    }

    /* Dirty hack: blacklisting of certain files on FS driver level */
    for (i = 0; g_blacklist[i]; i++)
    {
        if (fsw_memeq(g_blacklist[i], catkey.nodeName.unicode, catkey.nodeName.length*2))
        {
            DPRINT2("Blacklisted %s\n", g_blacklist[i]);
            status = FSW_NOT_FOUND;
            goto done;
        }
    }

#ifdef HFS_FILE_INJECTION
    if (fsw_hfs_inject(vol,
                       dno,
                       catkey.nodeName.unicode,
                       catkey.nodeName.length,
                       &file_info))
    {
        status = FSW_SUCCESS;
        goto create;
    }
#endif

    catkey.keyLength = (fsw_u16)(6 + rec_name.len);

    status = fsw_hfs_btree_search (&vol->catalog_tree,
                                   (BTreeKey*)&catkey,
                                   vol->case_sensitive ?
                                       fsw_hfs_cmp_catkey : fsw_hfs_cmpi_catkey,
                                   &node, &ptr);
    if (status)
        goto done;

    file_key = (HFSPlusCatalogKey *)fsw_hfs_btree_rec (&vol->catalog_tree, node, ptr);
    hfs_fill_info(vol, file_key, &file_info);

#ifdef HFS_FILE_INJECTION
create:
#endif
    status = create_hfs_dnode(dno, &file_info, child_dno_out);
    if (status)
        goto done;

done:

    if (node != NULL)
        fsw_free(node);

    if (free_data)
        fsw_strfree(&rec_name);

    return status;
}

/**
 * Get the next directory entry when reading a directory. This function is called during
 * directory iteration to retrieve the next directory entry. A dnode is constructed for
 * the entry and returned. The core makes sure that fsw_hfs_dnode_fill has been called
 * and the dnode is actually a directory. The shandle provided by the caller is used to
 * record the position in the directory between calls.
 */

static fsw_status_t fsw_hfs_dir_read(struct fsw_hfs_volume *vol,
                                     struct fsw_hfs_dnode  *dno,
                                     struct fsw_shandle    *shand,
                                     struct fsw_hfs_dnode  **child_dno_out)
{
    fsw_status_t               status;
    struct HFSPlusCatalogKey   catkey;
    fsw_u32                    ptr;
    BTNodeDescriptor *         node = NULL;

    visitor_parameter_t        param;
    struct fsw_string          rec_name;

    catkey.parentID = dno->g.dnode_id;
    catkey.nodeName.length = 0;

    fsw_memzero(&param, sizeof(param));

    rec_name.type = FSW_STRING_TYPE_EMPTY;
    param.file_info.name = &rec_name;

    status = fsw_hfs_btree_search (&vol->catalog_tree,
                                   (BTreeKey*)&catkey,
                                   vol->case_sensitive ?
                                       fsw_hfs_cmp_catkey : fsw_hfs_cmpi_catkey,
                                   &node, &ptr);
    if (status)
        goto done;

    /* Iterator updates shand state */
    param.vol = vol;
    param.shandle = shand;
    param.parent = dno->g.dnode_id;
    param.cur_pos = 0;
    status = fsw_hfs_btree_iterate_node (&vol->catalog_tree,
                                         node,
                                         ptr,
                                         fsw_hfs_btree_visit_node,
                                         &param);
    if (status)
      goto done;

    status = create_hfs_dnode(dno, &param.file_info, child_dno_out);

    if (status)
        goto done;

 done:
    fsw_strfree(&rec_name);

    return status;
}

static const char hfs_priv_prefix[] = "/\0\0\0\0HFS+ Private Data/" HFS_INODE_PREFIX;

/**
 * Get the target path of a symbolic link. This function is called when a symbolic
 * link needs to be resolved. The core makes sure that the fsw_hfs_dnode_fill has been
 * called on the dnode and that it really is a symlink.
 *
 */
static fsw_status_t fsw_hfs_readlink(struct fsw_hfs_volume *vol, struct fsw_hfs_dnode *dno,
                                     struct fsw_string *link_target)
{
    fsw_status_t    status;

    if (dno->node_num)
    {
        struct fsw_string   tgt;

        DPRINT2("hfs_readlink: %d\n", dno->node_num);
        tgt.type = FSW_STRING_TYPE_ISO88591;
        tgt.size = sizeof(hfs_priv_prefix) + 10;
        tgt.len  = tgt.size - 1;
        status = fsw_alloc(tgt.size, &tgt.data);
        if (!status)
        {
            char *str = tgt.data;
            fsw_memcpy(tgt.data, hfs_priv_prefix, sizeof(hfs_priv_prefix)); // null chars here!
#ifdef HOST_POSIX
            tgt.len = sprintf(&str[sizeof(hfs_priv_prefix) - 1], "%d", dno->node_num);
#else
            tgt.len = (int)AsciiSPrint(&str[sizeof(hfs_priv_prefix) - 1], tgt.len, "%d", dno->node_num);
#endif
            tgt.len += sizeof(hfs_priv_prefix) - 1;
            status = fsw_strdup_coerce(link_target, vol->g.host_string_type, &tgt);
            fsw_strfree(&tgt);
        }
        return status;
    }

    return FSW_UNSUPPORTED;
}

static int fsw_hfs_btree_find_id(BTreeKey *record, void* param)
{
    visitor_parameter_t         *vp = (visitor_parameter_t*)param;
    fsw_u8                      *base = (fsw_u8*)record->rawData + be16_to_cpu(record->length16) + 2;
    fsw_u16                     rec_type = be16_to_cpu(*(fsw_u16*)base);
    struct HFSPlusCatalogKey    *cat_key = (HFSPlusCatalogKey*)record;
    fsw_u16                     name_len;
    fsw_u16                     *name_ptr;
    fsw_u16                     *old_ptr;
    int                         i;
    struct fsw_string           *file_name;
    struct fsw_string           new_name;

    if (be32_to_cpu(cat_key->parentID) != vp->parent)
        return -1;

    if (!vp->cur_pos)
        vp->cur_pos = be32_to_cpu(cat_key->parentID);

    /* Not what we're looking for. */
    if (vp->file_info.id != vp->cur_pos++)
        return 0;

    if (rec_type == kHFSPlusFolderThreadRecord || rec_type == kHFSPlusFileThreadRecord)
    {
        HFSPlusCatalogThread    *thread;

        thread = (HFSPlusCatalogThread *)base;
        vp->file_info.id = be32_to_cpu(thread->parentID);

        name_len = be16_to_cpu(thread->nodeName.length);

        file_name = vp->file_info.name;

        new_name.len = name_len + 1 + file_name->len;
        new_name.size = sizeof(fsw_u16) * new_name.len;
        fsw_alloc(new_name.size, &new_name.data);
        name_ptr = (fsw_u16*)new_name.data;
        /* Tack on path separator. */
#ifdef HOST_POSIX
        name_ptr[0] = L'/';
#else
        name_ptr[0] = L'\\';
#endif
        /* Copy over + swap the new path component. */
        for (i = 0; i < name_len; i++)
            name_ptr[i + 1] = be16_to_cpu(thread->nodeName.unicode[i]);
        if (file_name->len) {
            /* Tack on the previous path. */
            old_ptr = (fsw_u16*)file_name->data;
            for (++i; i < new_name.len; i++ )
                name_ptr[i] = *old_ptr++;
        }

        fsw_free(file_name->data);
        file_name->len  = new_name.len;
        file_name->size = new_name.size;
        file_name->data = new_name.data;
        file_name->type = FSW_STRING_TYPE_UTF16;

        /* This was it, stop iterating. */
        return 1;
    }

    return 0;
}

/**
 * Obtain the full path of a file given its CNID (Catalog Node ID), i.e.
 * file or folder ID.
 *
 */
static fsw_status_t fsw_hfs_get_path_from_cnid(struct fsw_hfs_volume *vol, fsw_u32 cnid, struct fsw_string *path)
{
    fsw_status_t                status = FSW_UNSUPPORTED;
    fsw_u32                     ptr;
    BTNodeDescriptor            *node = NULL;
    struct HFSPlusCatalogKey    catkey;
    visitor_parameter_t         param;
    struct fsw_string           rec_name;

    /* The CNID must be a valid user node ID. */
    if (cnid < kHFSFirstUserCatalogNodeID)
        goto done;

    fsw_memzero(&param, sizeof(param));
    fsw_memzero(&rec_name, sizeof(rec_name));

    catkey.parentID = cnid;
    catkey.nodeName.length = 0;

    param.vol = vol;
    param.shandle = NULL;
    param.file_info.id = cnid;
    param.parent = cnid;
    param.cur_pos = 0;

    do {
        rec_name.type = FSW_STRING_TYPE_EMPTY;
        param.file_info.name = &rec_name;

        status = fsw_hfs_btree_search(&vol->catalog_tree, (BTreeKey*)&catkey,
                                      vol->case_sensitive ? fsw_hfs_cmp_catkey : fsw_hfs_cmpi_catkey,
                                      &node, &ptr);
        if (status)
            goto done;

        status = fsw_hfs_btree_iterate_node(&vol->catalog_tree, node, ptr,
                                            fsw_hfs_btree_find_id, &param);
        if (status)
            goto done;

        param.parent = param.file_info.id;
        param.cur_pos = 0;

        catkey.parentID = param.file_info.id;
        catkey.nodeName.length = 0;
    } while (catkey.parentID >= kHFSFirstUserCatalogNodeID);

    /* If everything worked out , the final parent ID will be the root folder ID. */
    if (catkey.parentID == kHFSRootFolderID)
    {
        *path = *param.file_info.name;
        status = FSW_SUCCESS;
    }
    else
        status = FSW_NOT_FOUND;

done:
    return status;
}

/**
 * Get the path of the HFS+ blessed file, if any.
 *
 */
/*static*/ fsw_status_t fsw_hfs_get_blessed_file(struct fsw_hfs_volume *vol, struct fsw_string *path)
{
    fsw_status_t                status = FSW_UNSUPPORTED;
    fsw_u32                     bfile_id;
    fsw_u32                     *finderinfo;

    finderinfo = (fsw_u32 *)&vol->primary_voldesc->finderInfo;
    bfile_id = finderinfo[1];
    bfile_id = be32_to_cpu(bfile_id);

    DPRINT2("Blessed file ID: %u\n", bfile_id);

    status = fsw_hfs_get_path_from_cnid(vol, bfile_id, path);
#ifdef HOST_POSIX
    if (!status)
    {
        fsw_u16     *name_ptr;
        int         i;

        printf("Blessed file: ");
        name_ptr = (fsw_u16*)path->data;
        for (i = 0; i < path->len; i++)
            printf("%c", name_ptr[i]);
        printf("\n");
    }
#endif

    return status;
}

// EOF
