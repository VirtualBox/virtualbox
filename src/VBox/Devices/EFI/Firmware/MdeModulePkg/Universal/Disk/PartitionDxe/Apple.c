/* $Id: Apple.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Apple.c
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

#include "Partition.h"

#define DPISTRLEN       32

#pragma pack(1)
typedef struct APPLE_PT_HEADER {
    UINT16      sbSig;          /* must be BE 0x4552 */
    UINT16      sbBlkSize;      /* block size of device */
    UINT32      sbBlkCount;     /* number of blocks on device */
    UINT16      sbDevType;      /* device type */
    UINT16      sbDevId;        /* device id */
    UINT32      sbData;         /* not used */
    UINT16      sbDrvrCount;    /* driver descriptor count */
    UINT16      sbMap[247];     /* descriptor map */
} APPLE_PT_HEADER;

typedef struct APPLE_PT_ENTRY  {
    UINT16       signature          ; /* must be BE 0x504D for new style PT */
    UINT16       reserved_1         ;
    UINT32       map_entries        ; /* how many PT entries are there */
    UINT32       pblock_start       ; /* first physical block */
    UINT32       pblocks            ; /* number of physical blocks */
    char         name[DPISTRLEN]    ; /* name of partition */
    char         type[DPISTRLEN]    ; /* type of partition */
    /* Some more data we don't really need */
} APPLE_PT_ENTRY;
#pragma pack()

static UINT16
be16_to_cpu(UINT16 x)
{
    return SwapBytes16(x);
}

static UINT32
be32_to_cpu(UINT32 x)
{
    return SwapBytes32(x);
}


/**
  Install child handles if the Handle supports Apple partition table format.

  @param[in]  This        Calling context.
  @param[in]  Handle      Parent Handle
  @param[in]  DiskIo      Parent DiskIo interface
  @param[in]  BlockIo     Parent BlockIo interface
  @param[in]  DevicePath  Parent Device Path


  @retval EFI_SUCCESS         Child handle(s) was added
  @retval EFI_MEDIA_CHANGED   Media changed Detected
  @retval other               no child handle was added

**/
EFI_STATUS
PartitionInstallAppleChildHandles (
  IN  EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN  EFI_HANDLE                   Handle,
  IN  EFI_DISK_IO_PROTOCOL         *DiskIo,
  IN  EFI_DISK_IO2_PROTOCOL        *DiskIo2,
  IN  EFI_BLOCK_IO_PROTOCOL        *BlockIo,
  IN  EFI_BLOCK_IO2_PROTOCOL       *BlockIo2,
  IN  EFI_DEVICE_PATH_PROTOCOL     *DevicePath
  )
{
  EFI_STATUS                Status;
  UINT32                    Lba;
  EFI_BLOCK_IO_MEDIA       *Media;
  VOID                     *Block;
  //UINTN                   MaxIndex;
  /** @todo wrong, as this PT can be on both HDD or CD */
  CDROM_DEVICE_PATH         CdDev;
  //EFI_DEVICE_PATH_PROTOCOL  Dev;
  EFI_STATUS                Found;
  UINT32                    Partition;
  UINT32                    PartitionEntries;
  UINT32                    SubBlockSize;
  UINT32                    BlkPerSec;
  EFI_PARTITION_INFO_PROTOCOL  PartitionInfo;

  VBoxLogFlowFuncEnter();
  Found         = EFI_NOT_FOUND;
  Media         = BlockIo->Media;

  Block = AllocatePool ((UINTN) Media->BlockSize);

  if (Block == NULL) {
    return EFI_NOT_FOUND;
  }

  do {
      APPLE_PT_HEADER * Header;

      /* read PT header first */
      Lba = 0;

      Status = DiskIo->ReadDisk (
                       DiskIo,
                       Media->MediaId,
                       MultU64x32 (Lba, Media->BlockSize),
                       Media->BlockSize,
                       Block
                       );
      if (EFI_ERROR (Status))
      {
          Found = Status;
          break;
      }

      Header = (APPLE_PT_HEADER *)Block;
      if (be16_to_cpu(Header->sbSig) != 0x4552)
      {
          break;
      }
      SubBlockSize = be16_to_cpu(Header->sbBlkSize);
      BlkPerSec    = Media->BlockSize / SubBlockSize;

      /* Fail if media block size isn't an exact multiple */
      if (Media->BlockSize != SubBlockSize * BlkPerSec)
      {
          break;
      }

      /* Now iterate over PT entries and install child handles */
      PartitionEntries = 1;
      for (Partition = 1; Partition <= PartitionEntries; Partition++)
      {
          APPLE_PT_ENTRY * Entry;
          UINT32 StartLba;
          UINT32 SizeLbs;

          Status = DiskIo->ReadDisk (
                       DiskIo,
                       Media->MediaId,
                       MultU64x32 (Partition, SubBlockSize),
                       SubBlockSize,
                       Block
                       );

          if (EFI_ERROR (Status)) {
              Status = EFI_NOT_FOUND;
              goto done; /* would break, but ... */
          }

          Entry = (APPLE_PT_ENTRY *)Block;

          if (be16_to_cpu(Entry->signature) != 0x504D)
          {
              Print(L"Not a new PT entry: %x", Entry->signature);
              continue;
          }

          /* First partition contains partitions count */
          if (Partition == 1)
          {
             PartitionEntries  = be32_to_cpu(Entry->map_entries);
          }

          StartLba = be32_to_cpu(Entry->pblock_start);
          SizeLbs  = be32_to_cpu(Entry->pblocks);

          if (0 && CompareMem("Apple_HFS", Entry->type, 10) == 0)
              Print(L"HFS partition (%d of %d) at LBA 0x%x size=%dM\n",
                    Partition, PartitionEntries, StartLba,
                    (UINT32)(DivU64x32(MultU64x32(SizeLbs, SubBlockSize), (1024 * 1024))));

          ZeroMem (&CdDev, sizeof (CdDev));
          CdDev.Header.Type     = MEDIA_DEVICE_PATH;
          CdDev.Header.SubType  = MEDIA_CDROM_DP;
          SetDevicePathNodeLength (&CdDev.Header, sizeof (CdDev));

          CdDev.BootEntry = 0;
          /* Convert from partition to media blocks */
          CdDev.PartitionStart = StartLba / BlkPerSec;  /* start, LBA */
          CdDev.PartitionSize  = SizeLbs / BlkPerSec;   /* size,  LBs */

          Status = PartitionInstallChildHandle (
              This,
              Handle,
              DiskIo,
              DiskIo2,
              BlockIo,
              BlockIo2,
              DevicePath,
              (EFI_DEVICE_PATH_PROTOCOL *) &CdDev,
              &PartitionInfo,
              CdDev.PartitionStart,
              CdDev.PartitionStart + CdDev.PartitionSize - 1,
              SubBlockSize,
              NULL);

          if (!EFI_ERROR (Status)) {
              Found = EFI_SUCCESS;
          }
      }

  } while (0);

 done:
  FreePool (Block);

  return Found;
}
