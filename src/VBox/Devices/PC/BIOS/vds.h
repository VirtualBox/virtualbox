/* $Id: vds.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Utility routines for calling the Virtual DMA Services.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_PC_BIOS_vds_h
#define VBOX_INCLUDED_SRC_PC_BIOS_vds_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Virtual DMA Services (VDS) */

#define VDS_FLAGS_OFS   0x7B    /* Offset of VDS flag byte in BDA. */
#define VDS_PRESENT     0x20    /* The VDS present bit. */

/** The DMA descriptor data structure. */
typedef struct
{
    uint32_t    region_size;    /* Region size in bytes. */
    uint32_t    offset;         /* Offset. */
    uint16_t    seg_sel;        /* Segment selector. */
    uint16_t    buf_id;         /* Buffer ID. */
    uint32_t    phys_addr;      /* Physical address. */
} vds_dds;


/** Scatter/gather descriptor entry. */
typedef struct
{
    uint32_t    phys_addr;      /* Physical address. */
    uint32_t    size;           /* Entry size. */
} vds_sg;

/** The extended DDS for scatter/gather.
 * Note that the EDDS contains either S/G descriptors or x86-style PTEs.
 */
typedef struct
{
    uint32_t    region_size;    /* Region size in bytes. */
    uint32_t    offset;         /* Offset. */
    uint16_t    seg_sel;        /* Segment or selector. */
    uint16_t    resvd;          /* Reserved. */
    uint16_t    num_avail;      /* Number of entries available. */
    uint16_t    num_used;       /* Number of entries used. */
    union
    {
        vds_sg      sg[1];      /* S/G entry array. */
        uint32_t    pte[1];     /* Page table entry array. */
    } u;
} vds_edds;


/* VDS services */

#define VDS_SERVICE             0x81

#define VDS_GET_VERSION         0x02    /* Get version */
#define VDS_LOCK_BUFFER         0x03    /* Lock DMA buffer region */
#define VDS_UNLOCK_BUFFER       0x04    /* Unlock DMA buffer region */
#define VDS_SG_LOCK             0x05    /* Scatter/gather lock region */
#define VDS_SG_UNLOCK           0x06    /* Scatter/gather unlock region */
#define VDS_REQUEST_BUFFER      0x07    /* Request DMA buffer */
#define VDS_RELEASE_BUFFER      0x08    /* Release DMA buffer */
#define VDS_BUFFER_COPYIN       0x09    /* Copy into DMA buffer */
#define VDS_BUFFER_COPYOUT      0x0A    /* Copy out of DMA buffer */
#define VDS_DISABLE_DMA_XLAT    0x0B    /* Disable DMA translation */
#define VDS_ENABLE_DMA_XLAT     0x0C    /* Enable DMA translation */

/* VDS error codes */

#define VDS_SUCCESS             0x00    /* No error */
#define VDS_ERR_NOT_CONTIG      0x01    /* Region not contiguous */
#define VDS_ERR_BOUNDRY_CROSS   0x02    /* Rgn crossed phys align boundary */
#define VDS_ERR_CANT_LOCK       0x03    /* Unable to lock pages */
#define VDS_ERR_NO_BUF          0x04    /* No buffer available */
#define VDS_ERR_RGN_TOO_BIG     0x05    /* Region too large for buffer */
#define VDS_ERR_BUF_IN_USE      0x06    /* Buffer currently in use */
#define VDS_ERR_RGN_INVALID     0x07    /* Invalid memory region */
#define VDS_ERR_RGN_NOT_LOCKED  0x08    /* Region was not locked */
#define VDS_ERR_TOO_MANY_PAGES  0x09    /* Num pages greater than table len */
#define VDS_ERR_INVALID_ID      0x0A    /* Invalid buffer ID */
#define VDS_ERR_BNDRY_VIOL      0x0B    /* Buffer boundary violated */
#define VDS_ERR_INVAL_DMACHN    0x0C    /* Invalid DMA channel number */
#define VDS_ERR_COUNT_OVRFLO    0x0D    /* Disable count overflow */
#define VDS_ERR_COUNT_UNDRFLO   0x0E    /* Disable count underflow */
#define VDS_ERR_UNSUPP_FUNC     0x0F    /* Function not supported */
#define VDS_ERR_BAD_FLAG        0x10    /* Reserved flag bits set in DX */

/* VDS option flags */

#define VDSF_AUTOCOPY           0x02    /* Automatic copy to/from buffer */
#define VDSF_NOALLOC            0x04    /* Disable auto buffer allocation */
#define VDSF_NOREMAP            0x08    /* Disable auto remap feature */
#define VDSF_NO64K              0x10    /* Region can't cross 64K boundary */
#define VDSF_NO128K             0x20    /* Region can't cross 128K boundary */
#define VDSF_COPYTBL            0x40    /* Copy page table for S/G remap */
#define VDSF_NPOK               0x80    /* Allow non-present pages for S/G */

/* Higher level routines for utilizing VDS. */

int vds_build_sg_list( vds_edds __far *edds, void __far *buf, uint32_t len );
int vds_free_sg_list( vds_edds __far *edds );

/* Helper for translating 16:16 real mode addresses to 32-bit linear. */

uint32_t vds_real_to_lin( void __far *ptr );

#endif /* !VBOX_INCLUDED_SRC_PC_BIOS_vds_h */

