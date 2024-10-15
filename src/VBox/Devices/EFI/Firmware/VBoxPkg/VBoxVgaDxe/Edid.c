/* $Id: Edid.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Edid.c
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

/*
  This code is based on:

  Read EDID information and parse EDID information.

  Copyright (c) 2008, Intel Corporation
  All rights reserved. This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#include "VBoxVga.h"
#include "VBoxVgaI2c.h"

//
// EDID block
//
typedef struct {
  UINT8   Header[8];                        //EDID header "00 FF FF FF FF FF FF 00"
  UINT16  ManufactureName;                  //EISA 3-character ID
  UINT16  ProductCode;                      //Vendor assigned code
  UINT32  SerialNumber;                     //32-bit serial number
  UINT8   WeekOfManufacture;                //Week number
  UINT8   YearOfManufacture;                //Year
  UINT8   EdidVersion;                      //EDID Structure Version
  UINT8   EdidRevision;                     //EDID Structure Revision
  UINT8   VideoInputDefinition;
  UINT8   MaxHorizontalImageSize;           //cm
  UINT8   MaxVerticalImageSize;             //cm
  UINT8   DisplayTransferCharacteristic;
  UINT8   FeatureSupport;
  UINT8   RedGreenLowBits;                  //Rx1 Rx0 Ry1 Ry0 Gx1 Gx0 Gy1Gy0
  UINT8   BlueWhiteLowBits;                 //Bx1 Bx0 By1 By0 Wx1 Wx0 Wy1 Wy0
  UINT8   RedX;                             //Red-x Bits 9 - 2
  UINT8   RedY;                             //Red-y Bits 9 - 2
  UINT8   GreenX;                           //Green-x Bits 9 - 2
  UINT8   GreenY;                           //Green-y Bits 9 - 2
  UINT8   BlueX;                            //Blue-x Bits 9 - 2
  UINT8   BlueY;                            //Blue-y Bits 9 - 2
  UINT8   WhiteX;                           //White-x Bits 9 - 2
  UINT8   WhiteY;                           //White-x Bits 9 - 2
  UINT8   EstablishedTimings[3];
  UINT8   StandardTimingIdentification[16];
  UINT8   DetailedTimingDescriptions[72];
  UINT8   ExtensionFlag;                    //Number of (optional) 128-byte EDID extension blocks to follow
  UINT8   Checksum;
} EDID_BLOCK;

#define EDID_BLOCK_SIZE                        128
#define VBE_EDID_ESTABLISHED_TIMING_MAX_NUMBER 17

typedef struct {
  UINT16  HorizontalResolution;
  UINT16  VerticalResolution;
  UINT16  RefreshRate;
} EDID_TIMING;

typedef struct {
  UINT32  ValidNumber;
  UINT32  Key[VBE_EDID_ESTABLISHED_TIMING_MAX_NUMBER];
} VALID_EDID_TIMING;

//
// Standard timing defined by VESA EDID
//
EDID_TIMING mVbeEstablishedEdidTiming[] = {
  //
  // Established Timing I
  //
  {800, 600, 60},
  {800, 600, 56},
  {640, 480, 75},
  {640, 480, 72},
  {640, 480, 67},
  {640, 480, 60},
  {720, 400, 88},
  {720, 400, 70},
  //
  // Established Timing II
  //
  {1280, 1024, 75},
  {1024,  768, 75},
  {1024,  768, 70},
  {1024,  768, 60},
  {1024,  768, 87},
  {832,   624, 75},
  {800,   600, 75},
  {800,   600, 72},
  //
  // Established Timing III
  //
  {1152, 870, 75}
};

/**
  Read EDID information from I2C Bus on CirrusLogic.

  @param  Private             Pointer to VBOX_VGA_PRIVATE_DATA.
  @param  EdidDataBlock       Pointer to EDID data block.
  @param  EdidSize            Returned EDID block size.

  @retval EFI_UNSUPPORTED
  @retval EFI_SUCCESS

**/
EFI_STATUS
ReadEdidData (
  VBOX_VGA_PRIVATE_DATA     *Private,
  UINT8                     **EdidDataBlock,
  UINTN                     *EdidSize
  )
{
  UINTN             Index;
  UINT8             EdidData[EDID_BLOCK_SIZE * 2];
  UINT8             *ValidEdid;
  UINT64            Signature;

  for (Index = 0; Index < EDID_BLOCK_SIZE * 2; Index ++) {
    I2cReadByte (Private->PciIo, 0xa0, (UINT8)Index, &EdidData[Index]);
  }

  //
  // Search for the EDID signature
  //
  ValidEdid = &EdidData[0];
  Signature = 0x00ffffffffffff00ull;
  for (Index = 0; Index < EDID_BLOCK_SIZE * 2; Index ++, ValidEdid ++) {
    if (CompareMem (ValidEdid, &Signature, 8) == 0) {
      break;
    }
  }

  if (Index == 256) {
    //
    // No EDID signature found
    //
    return EFI_UNSUPPORTED;
  }

  *EdidDataBlock = AllocateCopyPool (
                     sizeof (EDID_BLOCK_SIZE),
                     ValidEdid
                     );
  if (*EdidDataBlock == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Currently only support EDID 1.x
  //
  *EdidSize = EDID_BLOCK_SIZE;

  return EFI_SUCCESS;
}

/**
  Generate a search key for a specified timing data.

  @param  EdidTiming             Pointer to EDID timing

  @return The 32 bit unique key for search.

**/
UINT32
CalculateEdidKey (
  EDID_TIMING       *EdidTiming
  )
{
  UINT32 Key;

  //
  // Be sure no conflicts for all standard timing defined by VESA.
  //
  Key = (EdidTiming->HorizontalResolution * 2) + EdidTiming->VerticalResolution;
  return Key;
}

/**
  Search a specified Timing in all the valid EDID timings.

  @param  ValidEdidTiming        All valid EDID timing information.
  @param  EdidTiming             The Timing to search for.

  @retval TRUE                   Found.
  @retval FALSE                  Not found.

**/
BOOLEAN
SearchEdidTiming (
  VALID_EDID_TIMING *ValidEdidTiming,
  EDID_TIMING       *EdidTiming
  )
{
  UINT32 Index;
  UINT32 Key;

  Key = CalculateEdidKey (EdidTiming);

  for (Index = 0; Index < ValidEdidTiming->ValidNumber; Index ++) {
    if (Key == ValidEdidTiming->Key[Index]) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  Parse the Established Timing and Standard Timing in EDID data block.

  @param  EdidBuffer             Pointer to EDID data block
  @param  ValidEdidTiming        Valid EDID timing information

  @retval TRUE                   The EDID data is valid.
  @retval FALSE                  The EDID data is invalid.

**/
BOOLEAN
ParseEdidData (
  UINT8                         *EdidBuffer,
  VALID_EDID_TIMING             *ValidEdidTiming
  )
{
  UINT8        CheckSum;
  UINT32       Index;
  UINT32       ValidNumber;
  UINT32       TimingBits;
  UINT8        *BufferIndex;
  UINT16       HorizontalResolution;
  UINT16       VerticalResolution;
  UINT8        AspectRatio;
  UINT8        RefreshRate;
  EDID_TIMING  TempTiming;
  EDID_BLOCK   *EdidDataBlock;

  EdidDataBlock = (EDID_BLOCK *) EdidBuffer;

  //
  // Check the checksum of EDID data
  //
  CheckSum = 0;
  for (Index = 0; Index < EDID_BLOCK_SIZE; Index ++) {
    CheckSum = (UINT8) (CheckSum + EdidBuffer[Index]);
  }
  if (CheckSum != 0) {
    return FALSE;
  }

  ValidNumber = 0;
  SetMem (ValidEdidTiming, sizeof (VALID_EDID_TIMING), 0);

  if ((EdidDataBlock->EstablishedTimings[0] != 0) ||
      (EdidDataBlock->EstablishedTimings[1] != 0) ||
      (EdidDataBlock->EstablishedTimings[2] != 0)
      ) {
    //
    // Established timing data
    //
    TimingBits = EdidDataBlock->EstablishedTimings[0] |
                 (EdidDataBlock->EstablishedTimings[1] << 8) |
                 ((EdidDataBlock->EstablishedTimings[2] & 0x80) << 9) ;
    for (Index = 0; Index < VBE_EDID_ESTABLISHED_TIMING_MAX_NUMBER; Index ++) {
      if (TimingBits & 0x1) {
        ValidEdidTiming->Key[ValidNumber] = CalculateEdidKey (&mVbeEstablishedEdidTiming[Index]);
        ValidNumber ++;
      }
      TimingBits = TimingBits >> 1;
    }
  } else {
    //
    // If no Established timing data, read the standard timing data
    //
    BufferIndex = &EdidDataBlock->StandardTimingIdentification[0];
    for (Index = 0; Index < 8; Index ++) {
      if ((BufferIndex[0] != 0x1) && (BufferIndex[1] != 0x1)){
        //
        // A valid Standard Timing
        //
        HorizontalResolution = (UINT16) (BufferIndex[0] * 8 + 248);
        AspectRatio = (UINT8) (BufferIndex[1] >> 6);
        switch (AspectRatio) {
          case 0:
            VerticalResolution = (UINT16) (HorizontalResolution / 16 * 10);
            break;
          case 1:
            VerticalResolution = (UINT16) (HorizontalResolution / 4 * 3);
            break;
          case 2:
            VerticalResolution = (UINT16) (HorizontalResolution / 5 * 4);
            break;
          case 3:
            VerticalResolution = (UINT16) (HorizontalResolution / 16 * 9);
            break;
          default:
            VerticalResolution = (UINT16) (HorizontalResolution / 4 * 3);
            break;
        }
        RefreshRate = (UINT8) ((BufferIndex[1] & 0x1f) + 60);
        TempTiming.HorizontalResolution = HorizontalResolution;
        TempTiming.VerticalResolution = VerticalResolution;
        TempTiming.RefreshRate = RefreshRate;
        ValidEdidTiming->Key[ValidNumber] = CalculateEdidKey (&TempTiming);
        ValidNumber ++;
      }
      BufferIndex += 2;
    }
  }

  ValidEdidTiming->ValidNumber = ValidNumber;
  return TRUE;
}

static uint16_t in_word(uint16_t port, uint16_t addr)
{
    ASMOutU16(port, addr);
    return ASMInU16(port);
}

static EFI_STATUS VBoxVgaVideoModeInitExtra(void)
{
  UINT16 w, cur_info_ofs, vmode, xres, yres;
  UINTN  Index;
  VBOX_VGA_VIDEO_MODES *VideoMode;

  // Read and check the VBE Extra Data magic
  w = in_word(VBE_EXTRA_PORT, 0);
  if (w != VBEHEADER_MAGIC) {
      DEBUG((DEBUG_INFO, "%a:%d could not find VBE magic, got %x\n", __FILE__, __LINE__, w));
    return EFI_NOT_FOUND;
  }

  cur_info_ofs = sizeof(VBEHeader);

  Index = VBoxVgaVideoModeCount - 16;
  VideoMode = &VBoxVgaVideoModes[Index];
  vmode = in_word(VBE_EXTRA_PORT, cur_info_ofs + OFFSET_OF(ModeInfoListItem, mode));
  while (vmode != VBE_VESA_MODE_END_OF_LIST)
  {
    xres = in_word(VBE_EXTRA_PORT, cur_info_ofs + OFFSET_OF(ModeInfoListItem, info.XResolution));
    yres = in_word(VBE_EXTRA_PORT, cur_info_ofs + OFFSET_OF(ModeInfoListItem, info.YResolution));

    if (vmode >= VBE_VBOX_MODE_CUSTOM1 && vmode <= VBE_VBOX_MODE_CUSTOM16 && xres && yres && Index < VBoxVgaVideoModeCount) {
      VideoMode->Width = xres;
      VideoMode->Height = yres;
      VideoMode->ColorDepth = 32;
      VideoMode->RefreshRate = 60;
      VideoMode->MiscSetting = 0x01;
      VideoMode++;
      Index++;
    }

    cur_info_ofs += sizeof(ModeInfoListItem);
    vmode = in_word(VBE_EXTRA_PORT, cur_info_ofs + OFFSET_OF(ModeInfoListItem, mode));
  }
  return EFI_SUCCESS;
}

/**
  Construct the valid video modes for VBoxVga.

**/
EFI_STATUS
VBoxVgaVideoModeSetup (
  VBOX_VGA_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS                             Status;
  UINT32                                 Index;
  BOOLEAN                                EdidFound;
  EFI_EDID_OVERRIDE_PROTOCOL             *EdidOverride;
  UINT32                                 EdidAttributes;
  BOOLEAN                                EdidOverrideFound;
  UINTN                                  EdidOverrideDataSize;
  UINT8                                  *EdidOverrideDataBlock;
  UINTN                                  EdidDiscoveredDataSize;
  UINT8                                  *EdidDiscoveredDataBlock;
  UINTN                                  EdidActiveDataSize;
  UINT8                                  *EdidActiveDataBlock;
  VALID_EDID_TIMING                      ValidEdidTiming;
  UINT32                                 ValidModeCount;
  VBOX_VGA_MODE_DATA                     *ModeData;
  BOOLEAN                                TimingMatch;
  const VBOX_VGA_VIDEO_MODES             *VideoMode;
  EDID_TIMING                            TempTiming;

  //
  // setup EDID information
  //
  Private->EdidDiscovered.Edid       = NULL;
  Private->EdidDiscovered.SizeOfEdid = 0;
  Private->EdidActive.Edid           = NULL;
  Private->EdidActive.SizeOfEdid     = 0;

  EdidFound               = FALSE;
  EdidOverrideFound       = FALSE;
  EdidAttributes          = 0xff;
  EdidOverrideDataSize    = 0;
  EdidOverrideDataBlock   = NULL;
  EdidActiveDataSize      = 0;
  EdidActiveDataBlock     = NULL;
  EdidDiscoveredDataBlock = NULL;

  //
  // Find EDID Override protocol firstly, this protocol is installed by platform if needed.
  //
  Status = gBS->LocateProtocol (
                   &gEfiEdidOverrideProtocolGuid,
                   NULL,
                   (VOID **) &EdidOverride
                   );
  if (!EFI_ERROR (Status)) {
    //
    // Allocate double size of VESA_BIOS_EXTENSIONS_EDID_BLOCK_SIZE to avoid overflow
    //
    EdidOverrideDataBlock = AllocatePool (sizeof (EDID_BLOCK_SIZE * 2));
    if (NULL == EdidOverrideDataBlock) {
                Status = EFI_OUT_OF_RESOURCES;
      goto Done;
    }

    Status = EdidOverride->GetEdid (
                             EdidOverride,
                             Private->Handle,
                             &EdidAttributes,
                             &EdidOverrideDataSize,
                             (UINT8 **) &EdidOverrideDataBlock
                             );
    if (!EFI_ERROR (Status)  &&
         EdidAttributes == 0 &&
         EdidOverrideDataSize != 0) {
      //
      // Succeeded to get EDID Override Data
      //
      EdidOverrideFound = TRUE;
    }
  }

  if (EdidOverrideFound != TRUE || EdidAttributes == EFI_EDID_OVERRIDE_DONT_OVERRIDE) {
    //
    // If EDID Override data doesn't exist or EFI_EDID_OVERRIDE_DONT_OVERRIDE returned,
    // read EDID information through I2C Bus
    //
    if (ReadEdidData (Private, &EdidDiscoveredDataBlock, &EdidDiscoveredDataSize) == EFI_SUCCESS) {
      Private->EdidDiscovered.SizeOfEdid = (UINT32) EdidDiscoveredDataSize;
      Private->EdidDiscovered.Edid = (UINT8 *) AllocateCopyPool (
                                                          EdidDiscoveredDataSize,
                                                          EdidDiscoveredDataBlock
                                                                );

      if (NULL == Private->EdidDiscovered.Edid) {
          Status = EFI_OUT_OF_RESOURCES;
        goto Done;
      }

      EdidActiveDataSize  = Private->EdidDiscovered.SizeOfEdid;
      EdidActiveDataBlock = Private->EdidDiscovered.Edid;

      EdidFound = TRUE;
    }
  }

  if (EdidFound != TRUE && EdidOverrideFound == TRUE) {
    EdidActiveDataSize  = EdidOverrideDataSize;
    EdidActiveDataBlock = EdidOverrideDataBlock;
    EdidFound = TRUE;
  }

  if (EdidFound == TRUE) {
    //
    // Parse EDID data structure to retrieve modes supported by monitor
    //
    if (ParseEdidData ((UINT8 *) EdidActiveDataBlock, &ValidEdidTiming) == TRUE) {
      //
      // Copy EDID Override Data to EDID Active Data
      //
      Private->EdidActive.SizeOfEdid = (UINT32) EdidActiveDataSize;
      Private->EdidActive.Edid = (UINT8 *) AllocateCopyPool (
                                             EdidActiveDataSize,
                                             EdidActiveDataBlock
                                             );
      if (NULL == Private->EdidActive.Edid) {
                  Status = EFI_OUT_OF_RESOURCES;
        goto Done;
      }
    }
  } else {
    Private->EdidActive.SizeOfEdid = 0;
    Private->EdidActive.Edid = NULL;
    EdidFound = FALSE;
  }

  if (EdidFound && 0) {
    //
    // Initialize the private mode data with the supported modes.
    //
    ValidModeCount = 0;
    Private->ModeData = AllocatePool(sizeof(VBOX_VGA_MODE_DATA) * VBoxVgaVideoModeCount);
    ModeData = &Private->ModeData[0];
    VideoMode = &VBoxVgaVideoModes[0];
    for (Index = 0; Index < VBoxVgaVideoModeCount; Index++) {

      TimingMatch = TRUE;

      //
      // Check whether match with VBoxVga video mode
      //
      TempTiming.HorizontalResolution = (UINT16) VideoMode->Width;
      TempTiming.VerticalResolution   = (UINT16) VideoMode->Height;
      TempTiming.RefreshRate          = (UINT16) VideoMode->RefreshRate;
      if (SearchEdidTiming (&ValidEdidTiming, &TempTiming) != TRUE) {
        TimingMatch = FALSE;
      }

      //
      // Not export Mode 0x0 as GOP mode, this is not defined in spec.
      //
      if ((VideoMode->Width == 0) || (VideoMode->Height == 0)) {
        TimingMatch = FALSE;
      }

      //
      // Check whether the mode would be exceeding the VRAM size.
      //
      if (VideoMode->Width * VideoMode->Height * (VideoMode->ColorDepth / 8) > Private->VRAMSize) {
        TimingMatch = FALSE;
      }

      if (TimingMatch) {
        ModeData->ModeNumber = Index;
        ModeData->HorizontalResolution          = VideoMode->Width;
        ModeData->VerticalResolution            = VideoMode->Height;
        ModeData->ColorDepth                    = VideoMode->ColorDepth;
        ModeData->RefreshRate                   = VideoMode->RefreshRate;

        ModeData ++;
        ValidModeCount ++;
      }

      VideoMode ++;
    }
  } else {
    //
    // If EDID information wasn't found
    //
    VBoxVgaVideoModeInitExtra();
    ValidModeCount = 0;
    Private->ModeData = AllocatePool(sizeof(VBOX_VGA_MODE_DATA) * VBoxVgaVideoModeCount);
    ModeData = &Private->ModeData[0];
    VideoMode = &VBoxVgaVideoModes[0];
    for (Index = 0; Index < VBoxVgaVideoModeCount; Index ++) {

      TimingMatch = TRUE;

      //
      // Not export Mode 0x0 as GOP mode, this is not defined in spec.
      //
      if ((VideoMode->Width == 0) || (VideoMode->Height == 0)) {
        TimingMatch = FALSE;
      }

      //
      // Check whether the mode would be exceeding the VRAM size.
      //
      if (VideoMode->Width * VideoMode->Height * (VideoMode->ColorDepth / 8) > Private->VRAMSize) {
        TimingMatch = FALSE;
      }

      if (TimingMatch) {
        ModeData->ModeNumber = Index;
        ModeData->HorizontalResolution          = VideoMode->Width;
        ModeData->VerticalResolution            = VideoMode->Height;
        ModeData->ColorDepth                    = VideoMode->ColorDepth;
        ModeData->RefreshRate                   = VideoMode->RefreshRate;

        ModeData ++;
        ValidModeCount ++;
      }

      VideoMode ++;
    }
  }

  // Sort list of video modes (keeping duplicates) by increasing X, then Y,
  // then the mode number. This way the custom modes are not overriding the
  // default modes if they are for the same resolution.
  ModeData = &Private->ModeData[0];
  for (Index = 0; Index < ValidModeCount - 1; Index ++) {
    UINT32 Index2;
    VBOX_VGA_MODE_DATA *ModeData2 = ModeData + 1;
    for (Index2 = Index + 1; Index2 < ValidModeCount; Index2 ++) {
        if (   ModeData->HorizontalResolution > ModeData2->HorizontalResolution
            || (   ModeData->HorizontalResolution == ModeData2->HorizontalResolution
                && ModeData->VerticalResolution > ModeData2->VerticalResolution)
            || (   ModeData->HorizontalResolution == ModeData2->HorizontalResolution
                && ModeData->VerticalResolution == ModeData2->VerticalResolution
                && ModeData->ModeNumber > ModeData2->ModeNumber)) {
            VBOX_VGA_MODE_DATA Tmp;
            CopyMem(&Tmp, ModeData, sizeof(Tmp));
            CopyMem(ModeData, ModeData2, sizeof(Tmp));
            CopyMem(ModeData2, &Tmp, sizeof(Tmp));
            DEBUG((DEBUG_INFO, "%a:%d swapped mode entries %d and %d\n", __FILE__, __LINE__, Index, Index2));
        }
        ModeData2++;
    }
    ModeData++;
  }

  // dump mode list for debugging purposes
  ModeData = &Private->ModeData[0];
  for (Index = 0; Index < ValidModeCount; Index ++) {
    DEBUG((DEBUG_INFO, "%a:%d mode %d: %dx%d mode number %d\n", __FILE__, __LINE__, Index, ModeData->HorizontalResolution, ModeData->VerticalResolution, ModeData->ModeNumber));
    ModeData++;
  }

  Private->MaxMode = ValidModeCount;

  if (EdidOverrideDataBlock != NULL) {
    FreePool (EdidOverrideDataBlock);
  }

  return EFI_SUCCESS;

Done:
  if (EdidOverrideDataBlock != NULL) {
    FreePool (EdidOverrideDataBlock);
  }
  if (Private->EdidDiscovered.Edid != NULL) {
    FreePool (Private->EdidDiscovered.Edid);
  }
  if (Private->EdidDiscovered.Edid != NULL) {
    FreePool (Private->EdidActive.Edid);
  }

  return EFI_DEVICE_ERROR;
}
