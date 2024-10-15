/* $Id: VBoxVgaGraphicsOutput.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVgaGraphicsOutput.c
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

Copyright (c) 2007, Intel Corporation
All rights reserved. This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

Module Name:

  UefiVBoxVgaGraphicsOutput.c

Abstract:

  This file produces the graphics abstraction of Graphics Output Protocol. It is called by
  VBoxVga.c file which deals with the EFI 1.1 driver model.
  This file just does graphics.

*/
#include "VBoxVga.h"
#include <IndustryStandard/Acpi.h>


STATIC
VOID
VBoxVgaCompleteModeInfo (
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info
  )
{
  Info->Version = 0;
  Info->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  Info->PixelsPerScanLine = Info->HorizontalResolution;
}


STATIC
EFI_STATUS
VBoxVgaCompleteModeData (
  IN  VBOX_VGA_PRIVATE_DATA    *Private,
  OUT EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode
  )
{
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR     *FrameBufDesc;

  Info = Mode->Info;
  VBoxVgaCompleteModeInfo (Info);

  Private->PciIo->GetBarAttributes (
                        Private->PciIo,
                        Private->BarIndexFB,
                        NULL,
                        (VOID**) &FrameBufDesc
                        );

  DEBUG((DEBUG_INFO, "%a:%d FrameBufferBase:%x\n", __FILE__, __LINE__, FrameBufDesc->AddrRangeMin));
  Mode->FrameBufferBase = FrameBufDesc->AddrRangeMin;
  Mode->FrameBufferSize = Info->PixelsPerScanLine * Info->VerticalResolution
                        * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);    /* 32bpp only! */

  return EFI_SUCCESS;
}


//
// Graphics Output Protocol Member Functions
//
EFI_STATUS
EFIAPI
VBoxVgaGraphicsOutputQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
/*++

Routine Description:

  Graphics Output protocol interface to query video mode

  Arguments:
    This                  - Protocol instance pointer.
    ModeNumber            - The mode number to return information on.
    Info                  - Caller allocated buffer that returns information about ModeNumber.
    SizeOfInfo            - A pointer to the size, in bytes, of the Info buffer.

  Returns:
    EFI_SUCCESS           - Mode information returned.
    EFI_BUFFER_TOO_SMALL  - The Info buffer was too small.
    EFI_DEVICE_ERROR      - A hardware error occurred trying to retrieve the video mode.
    EFI_NOT_STARTED       - Video display is not initialized. Call SetMode ()
    EFI_INVALID_PARAMETER - One of the input args was NULL.

--*/
{
  VBOX_VGA_PRIVATE_DATA  *Private;

  Private = VBOX_VGA_PRIVATE_DATA_FROM_GRAPHICS_OUTPUT_THIS (This);

  if (Private->HardwareNeedsStarting) {
    return EFI_NOT_STARTED;
  }

  if (Info == NULL || SizeOfInfo == NULL || ModeNumber >= This->Mode->MaxMode) {
    return EFI_INVALID_PARAMETER;
  }

  *Info = AllocatePool (sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION));
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

  (*Info)->HorizontalResolution = Private->ModeData[ModeNumber].HorizontalResolution;
  (*Info)->VerticalResolution   = Private->ModeData[ModeNumber].VerticalResolution;
  VBoxVgaCompleteModeInfo (*Info);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
VBoxVgaGraphicsOutputSetMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  IN  UINT32                       ModeNumber
  )
/*++

Routine Description:

  Graphics Output protocol interface to set video mode

  Arguments:
    This             - Protocol instance pointer.
    ModeNumber       - The mode number to be set.

  Returns:
    EFI_SUCCESS      - Graphics mode was changed.
    EFI_DEVICE_ERROR - The device had an error and could not complete the request.
    EFI_UNSUPPORTED  - ModeNumber is not supported by this device.

--*/
{
  VBOX_VGA_PRIVATE_DATA    *Private;
  VBOX_VGA_MODE_DATA       *ModeData;

  Private = VBOX_VGA_PRIVATE_DATA_FROM_GRAPHICS_OUTPUT_THIS (This);

  DEBUG((DEBUG_INFO, "%a:%d mode:%d\n", __FILE__, __LINE__, ModeNumber));
  if (ModeNumber >= This->Mode->MaxMode) {
    return EFI_UNSUPPORTED;
  }

  ModeData = &Private->ModeData[ModeNumber];

  InitializeGraphicsMode (Private, &VBoxVgaVideoModes[ModeData->ModeNumber]);

  This->Mode->Mode = ModeNumber;
  This->Mode->Info->HorizontalResolution = ModeData->HorizontalResolution;
  This->Mode->Info->VerticalResolution = ModeData->VerticalResolution;
  This->Mode->SizeOfInfo = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

  VBoxVgaCompleteModeData (Private, This->Mode);

  Private->HardwareNeedsStarting  = FALSE;
  /* update current mode */
  Private->CurrentMode = ModeNumber;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
VBoxVgaGraphicsOutputBlt (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  EFI_GRAPHICS_OUTPUT_BLT_PIXEL         *BltBuffer, OPTIONAL
  IN  EFI_GRAPHICS_OUTPUT_BLT_OPERATION     BltOperation,
  IN  UINTN                                 SourceX,
  IN  UINTN                                 SourceY,
  IN  UINTN                                 DestinationX,
  IN  UINTN                                 DestinationY,
  IN  UINTN                                 Width,
  IN  UINTN                                 Height,
  IN  UINTN                                 Delta
  )
/*++

Routine Description:

  Graphics Output protocol instance to block transfer for CirrusLogic device

Arguments:

  This          - Pointer to Graphics Output protocol instance
  BltBuffer     - The data to transfer to screen
  BltOperation  - The operation to perform
  SourceX       - The X coordinate of the source for BltOperation
  SourceY       - The Y coordinate of the source for BltOperation
  DestinationX  - The X coordinate of the destination for BltOperation
  DestinationY  - The Y coordinate of the destination for BltOperation
  Width         - The width of a rectangle in the blt rectangle in pixels
  Height        - The height of a rectangle in the blt rectangle in pixels
  Delta         - Not used for EfiBltVideoFill and EfiBltVideoToVideo operation.
                  If a Delta of 0 is used, the entire BltBuffer will be operated on.
                  If a subrectangle of the BltBuffer is used, then Delta represents
                  the number of bytes in a row of the BltBuffer.

Returns:

  EFI_INVALID_PARAMETER - Invalid parameter passed in
  EFI_SUCCESS - Blt operation success

--*/
{
  VBOX_VGA_PRIVATE_DATA     *Private;
  EFI_TPL                   OriginalTPL;
  UINTN                     DstY;
  UINTN                     SrcY;
  UINT32                    CurrentMode;
  UINTN                     ScreenWidth;
  UINTN                     ScreenHeight;
  EFI_STATUS                Status;

  Private = VBOX_VGA_PRIVATE_DATA_FROM_GRAPHICS_OUTPUT_THIS (This);
  CurrentMode = This->Mode->Mode;
  ScreenWidth = Private->ModeData[CurrentMode].HorizontalResolution;
  ScreenHeight = Private->ModeData[CurrentMode].VerticalResolution;

  if ((BltOperation < 0) || (BltOperation >= EfiGraphicsOutputBltOperationMax)) {
    return EFI_INVALID_PARAMETER;
  }
  if (Width == 0 || Height == 0) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // If Delta is zero, then the entire BltBuffer is being used, so Delta
  // is the number of bytes in each row of BltBuffer.  Since BltBuffer is Width pixels size,
  // the number of bytes in each row can be computed.
  //
  if (Delta == 0) {
    Delta = Width * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);
  }
  // code below assumes a Delta value in pixels, not bytes
  Delta /= sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL);

  //
  // Make sure the SourceX, SourceY, DestinationX, DestinationY, Width, and Height parameters
  // are valid for the operation and the current screen geometry.
  //
  if (BltOperation == EfiBltVideoToBltBuffer || BltOperation == EfiBltVideoToVideo) {
    if (SourceY + Height > ScreenHeight) {
      return EFI_INVALID_PARAMETER;
    }

    if (SourceX + Width > ScreenWidth) {
      return EFI_INVALID_PARAMETER;
    }
  }
  if (BltOperation == EfiBltBufferToVideo || BltOperation == EfiBltVideoToVideo || BltOperation == EfiBltVideoFill) {
    if (DestinationY + Height > ScreenHeight) {
      return EFI_INVALID_PARAMETER;
    }

    if (DestinationX + Width > ScreenWidth) {
      return EFI_INVALID_PARAMETER;
    }
  }

  //
  // We have to raise to TPL Notify, so we make an atomic write the frame buffer.
  // We would not want a timer based event (Cursor, ...) to come in while we are
  // doing this operation.
  //
  OriginalTPL = gBS->RaiseTPL (TPL_NOTIFY);

  switch (BltOperation) {
  case EfiBltVideoToBltBuffer:
    //
    // Video to BltBuffer: Source is Video, destination is BltBuffer
    //
    for (SrcY = SourceY, DstY = DestinationY; DstY < (Height + DestinationY) && BltBuffer; SrcY++, DstY++) {
      /// @todo assumes that color depth is 32 (*4, EfiPciIoWidthUint32) and format matches EFI_GRAPHICS_OUTPUT_BLT_PIXEL
      Status = Private->PciIo->Mem.Read (
                                    Private->PciIo,
                                    EfiPciIoWidthUint32,
                                    Private->BarIndexFB,
                                    ((SrcY * ScreenWidth) + SourceX) * 4,
                                    Width,
                                    BltBuffer + (DstY * Delta) + DestinationX
                                    );
      ASSERT_EFI_ERROR((Status));
    }
    break;

  case EfiBltBufferToVideo:
    //
    // BltBuffer to Video: Source is BltBuffer, destination is Video
    //
    for (SrcY = SourceY, DstY = DestinationY; SrcY < (Height + SourceY); SrcY++, DstY++) {
      /// @todo assumes that color depth is 32 (*4, EfiPciIoWidthUint32) and format matches EFI_GRAPHICS_OUTPUT_BLT_PIXEL
      Status = Private->PciIo->Mem.Write (
                                    Private->PciIo,
                                    EfiPciIoWidthUint32,
                                    Private->BarIndexFB,
                                    ((DstY * ScreenWidth) + DestinationX) * 4,
                                    Width,
                                    BltBuffer + (SrcY * Delta) + SourceX
                                    );
      ASSERT_EFI_ERROR((Status));
    }
    break;

  case EfiBltVideoToVideo:
    //
    // Video to Video: Source is Video, destination is Video
    //
    if (DestinationY <= SourceY) {
      // forward copy
      for (SrcY = SourceY, DstY = DestinationY; SrcY < (Height + SourceY); SrcY++, DstY++) {
        /// @todo assumes that color depth is 32 (*4, EfiPciIoWidthUint32) and format matches EFI_GRAPHICS_OUTPUT_BLT_PIXEL
        Status = Private->PciIo->CopyMem (
                                    Private->PciIo,
                                    EfiPciIoWidthUint32,
                                    Private->BarIndexFB,
                                    ((DstY * ScreenWidth) + DestinationX) * 4,
                                    Private->BarIndexFB,
                                    ((SrcY * ScreenWidth) + SourceX) * 4,
                                    Width
                                    );
        ASSERT_EFI_ERROR((Status));
      }
    } else {
      // reverse copy
      for (SrcY = SourceY + Height - 1, DstY = DestinationY + Height - 1; SrcY >= SourceY && SrcY <= SourceY + Height - 1; SrcY--, DstY--) {
        /// @todo assumes that color depth is 32 (*4, EfiPciIoWidthUint32) and format matches EFI_GRAPHICS_OUTPUT_BLT_PIXEL
        Status = Private->PciIo->CopyMem (
                                    Private->PciIo,
                                    EfiPciIoWidthUint32,
                                    Private->BarIndexFB,
                                    ((DstY * ScreenWidth) + DestinationX) * 4,
                                    Private->BarIndexFB,
                                    ((SrcY * ScreenWidth) + SourceX) * 4,
                                    Width
                                    );
        ASSERT_EFI_ERROR((Status));
      }
    }
    break;

  case EfiBltVideoFill:
    //
    // Video Fill: Source is BltBuffer, destination is Video
    //
    if (DestinationX == 0 && Width == ScreenWidth) {
      /// @todo assumes that color depth is 32 (*4, EfiPciIoWidthFillUint32) and format matches EFI_GRAPHICS_OUTPUT_BLT_PIXEL
      Status = Private->PciIo->Mem.Write (
                                    Private->PciIo,
                                    EfiPciIoWidthFillUint32,
                                    Private->BarIndexFB,
                                    DestinationY * ScreenWidth * 4,
                                    (Width * Height),
                                    BltBuffer
                                    );
      ASSERT_EFI_ERROR((Status));
    } else {
      for (SrcY = SourceY, DstY = DestinationY; SrcY < (Height + SourceY); SrcY++, DstY++) {
        /// @todo assumes that color depth is 32 (*4, EfiPciIoWidthFillUint32) and format matches EFI_GRAPHICS_OUTPUT_BLT_PIXEL
        Status = Private->PciIo->Mem.Write (
                                      Private->PciIo,
                                      EfiPciIoWidthFillUint32,
                                      Private->BarIndexFB,
                                      ((DstY * ScreenWidth) + DestinationX) * 4,
                                      Width,
                                      BltBuffer
                                      );
        ASSERT_EFI_ERROR((Status));
      }
    }
    break;

  default:
    ASSERT (FALSE);
  }

  gBS->RestoreTPL (OriginalTPL);

  return EFI_SUCCESS;
}

EFI_STATUS
VBoxVgaGraphicsOutputConstructor (
  VBOX_VGA_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS                    Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *GraphicsOutput;
  UINT32                        Index;
  UINT32                        HorizontalResolution = 1024;
  UINT32                        VerticalResolution = 768;
  UINT32                        ColorDepth = 32;

  DEBUG((DEBUG_INFO, "%a:%d construct\n", __FILE__, __LINE__));

  GraphicsOutput            = &Private->GraphicsOutput;
  GraphicsOutput->QueryMode = VBoxVgaGraphicsOutputQueryMode;
  GraphicsOutput->SetMode   = VBoxVgaGraphicsOutputSetMode;
  GraphicsOutput->Blt       = VBoxVgaGraphicsOutputBlt;

  //
  // Initialize the private data
  //
  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  sizeof (EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE),
                  (VOID **) &Private->GraphicsOutput.Mode
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
                  (VOID **) &Private->GraphicsOutput.Mode->Info
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Private->GraphicsOutput.Mode->MaxMode = (UINT32) Private->MaxMode;
  Private->GraphicsOutput.Mode->Mode    = GRAPHICS_OUTPUT_INVALIDE_MODE_NUMBER;
  Private->HardwareNeedsStarting        = TRUE;

  //
  // Initialize the hardware
  //
  VBoxVgaGetVmVariable(EFI_INFO_INDEX_HORIZONTAL_RESOLUTION, (CHAR8 *)&HorizontalResolution,
                       sizeof(HorizontalResolution));
  VBoxVgaGetVmVariable(EFI_INFO_INDEX_VERTICAL_RESOLUTION, (CHAR8 *)&VerticalResolution,
                       sizeof(VerticalResolution));
  for (Index = 0; Index < Private->MaxMode; Index++)
  {
    if (   HorizontalResolution == Private->ModeData[Index].HorizontalResolution
        && VerticalResolution == Private->ModeData[Index].VerticalResolution
        && ColorDepth == Private->ModeData[Index].ColorDepth)
      break;
  }
  // not found? try mode number
  if (Index >= Private->MaxMode)
  {
    VBoxVgaGetVmVariable(EFI_INFO_INDEX_GRAPHICS_MODE, (CHAR8 *)&Index, sizeof(Index));
    // try with mode 2 (usually 1024x768) as a fallback
    if (Index >= Private->MaxMode)
      Index = 2;
    // try with mode 0 (usually 640x480) as a fallback
    if (Index >= Private->MaxMode)
      Index = 0;
  }

  // skip mode setting completely if there is no valid mode
  if (Index >= Private->MaxMode)
    return EFI_UNSUPPORTED;

  GraphicsOutput->SetMode (GraphicsOutput, Index);

  DrawLogo (
    Private,
    Private->ModeData[Private->GraphicsOutput.Mode->Mode].HorizontalResolution,
    Private->ModeData[Private->GraphicsOutput.Mode->Mode].VerticalResolution
    );

  PcdSet32S(PcdVideoHorizontalResolution, Private->ModeData[Private->GraphicsOutput.Mode->Mode].HorizontalResolution);
  PcdSet32S(PcdVideoVerticalResolution, Private->ModeData[Private->GraphicsOutput.Mode->Mode].VerticalResolution);

  return EFI_SUCCESS;
}

EFI_STATUS
VBoxVgaGraphicsOutputDestructor (
  VBOX_VGA_PRIVATE_DATA  *Private
  )
/*++

Routine Description:

Arguments:

Returns:

  None

--*/
{
  if (Private->GraphicsOutput.Mode != NULL) {
    if (Private->GraphicsOutput.Mode->Info != NULL) {
      gBS->FreePool (Private->GraphicsOutput.Mode->Info);
    }
    gBS->FreePool (Private->GraphicsOutput.Mode);
  }

  return EFI_SUCCESS;
}

