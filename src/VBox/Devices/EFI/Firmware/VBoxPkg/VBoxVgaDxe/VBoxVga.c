/* $Id: VBoxVga.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVga.c
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

  Cirrus Logic 5430 Controller Driver.
  This driver is a sample implementation of the UGA Draw and Graphics Output
  Protocols for the Cirrus Logic 5430 family of PCI video controllers.
  This driver is only usable in the EFI pre-boot environment.
  This sample is intended to show how the UGA Draw and Graphics output Protocol
  is able to function.
  The UGA I/O Protocol is not implemented in this sample.
  A fully compliant EFI UGA driver requires both
  the UGA Draw and the UGA I/O Protocol.  Please refer to Microsoft's
  documentation on UGA for details on how to write a UGA driver that is able
  to function both in the EFI pre-boot environment and from the OS runtime.

  Copyright (c) 2006 - 2009, Intel Corporation
  All rights reserved. This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

//
// VirtualBox VGA Controller Driver
//
#include "VBoxVga.h"
#include <IndustryStandard/Acpi.h>
#include "iprt/asm.h"


#define BOUTB(storage, count, aport, dport)                                 \
    do {                                                                    \
        for (i = 0 ; i < (count); ++i)                                      \
            if ((dport) == (aport) + 1)                                     \
                ASMOutU16((aport), ((UINT16)storage[i] << 8) | (UINT8)i);   \
            else {                                                          \
                ASMOutU8((aport), (UINT8)i);                                \
                ASMOutU8((dport), storage[i]);                              \
            }                                                               \
    } while (0)



EFI_DRIVER_BINDING_PROTOCOL gVBoxVgaDriverBinding = {
  VBoxVgaControllerDriverSupported,
  VBoxVgaControllerDriverStart,
  VBoxVgaControllerDriverStop,
  0x10,
  NULL,
  NULL
};

///
/// Generic Attribute Controller Register Settings
///
UINT8  AttributeController[21] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x41, 0x00, 0x0F, 0x00, 0x00
};

///
/// Generic Graphics Controller Register Settings
///
UINT8 GraphicsController[9] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xff
};

///
/// Generic Graphics Controller Sequencer Register Settings
///
UINT8 Seq_Default[5] = {
 0x01,  0x01,  0x0f,  0x00,  0x0a
};

#if 0 // CRTC tables not used (and not checked for correctness), as VBE is much simpler
//
// 640 x 480 x 256 color @ 60 Hertz
//
UINT8 Crtc_640_480_256_60[25] = {
    /* r0  =  */0x5f,  /* r1  =  */0x4f,  /* r2  =  */0x50,  /* r3  =  */0x82,
    /* r4  =  */0x54,  /* r5  =  */0x80,  /* r6  =  */0x0b,  /* r7  =  */0x3e,
    /* r8  =  */0x00,  /* r9  =  */0x40,  /* r10 =  */0x00,  /* r11 =  */0x00,
    /* r12 =  */0x00,  /* r13 =  */0x00,  /* r14 =  */0x00,  /* r15 =  */0x00,
    /* r16 =  */0xea,  /* r17 =  */0x0c,  /* r18 =  */0xdf,  /* r19 =  */0x28,
    /* r20 =  */0x4f,  /* r21 =  */0xe7,  /* r22 =  */0x04,  /* r23 =  */0xe3,
    /* r24 =  */0xff
};

//
// 800 x 600 x 256 color @ 60 Hertz
//
UINT8 Crtc_800_600_256_60[25] = {
    /* r0  =  */0x7f,  /* r1  =  */0x63,  /* r2  =  */0x64,  /* r3  =  */0x82,
    /* r4  =  */0x6b,  /* r5  =  */0x80,  /* r6  =  */0x0b,  /* r7  =  */0x3e,
    /* r8  =  */0x00,  /* r9  =  */0x60,  /* r10 =  */0x00,  /* r11 =  */0x00,
    /* r12 =  */0x00,  /* r13 =  */0x00,  /* r14 =  */0x00,  /* r15 =  */0x00,
    /* r16 =  */0xea,  /* r17 =  */0x0c,  /* r18 =  */0xdf,  /* r19 =  */0x28,
    /* r20 =  */0x4f,  /* r21 =  */0xe7,  /* r22 =  */0x04,  /* r23 =  */0xe3,
    /* r24 =  */0xff

};

//
// 1024 x 768 x 256 color @ 60 Hertz
//
UINT8 Crtc_1024_768_256_60[25] = {
    /* r0  =  */0xa3,  /* r1  =  */0x7f,  /* r2  =  */0x81,  /* r3  =  */0x90,
    /* r4  =  */0x88,  /* r5  =  */0x05,  /* r6  =  */0x28,  /* r7  =  */0xfd,
    /* r8  =  */0x00,  /* r9  =  */0x60,  /* r10 =  */0x00,  /* r11 =  */0x00,
    /* r12 =  */0x00,  /* r13 =  */0x00,  /* r14 =  */0x00,  /* r15 =  */0x00,
    /* r16 =  */0x06,  /* r17 =  */0x0f,  /* r18 =  */0xff,  /* r19 =  */0x40,
    /* r20 =  */0x4f,  /* r21 =  */0x05,  /* r22 =  */0x1a,  /* r23 =  */0xe3,
    /* r24 =  */0xff
};
#endif

///
/// Table of supported video modes (sorted by increasing horizontal, then by
/// increasing vertical resolution)
///
VBOX_VGA_VIDEO_MODES  VBoxVgaVideoModes[] =
{
  {  640,  480, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // VGA 4:3
  {  800,  600, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // SVGA 4:3
  { 1024,  768, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // XGA 4:3
  { 1152,  864, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // XGA+ 4:3
  { 1280,  720, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // HD 16:9
  { 1280,  800, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // WXGA 16:10
  { 1280, 1024, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // SXGA 5:4
  { 1400, 1050, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // SXGA+ 4:3
  { 1440,  900, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // WXGA+ 16:10
  { 1600,  900, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // HD+ 16:9
  { 1600, 1200, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // UXGA 4:3
  { 1680, 1050, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // WSXGA+ 16:10
  { 1920, 1080, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // FHD 16:9
  { 1920, 1200, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // WUXGA 16:10
  { 2048, 1080, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // DCI_2K 19:10
  { 2160, 1440, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // FHD+ 3:2
  { 2304, 1440, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // unnamed 16:10
  { 2560, 1440, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // QHD 16:9
  { 2560, 1600, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // WQXGA 16:10
  { 2880, 1800, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // QWXGA+ 16:10
  { 3200, 1800, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // QHD+ 16:9
  { 3200, 2048, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // WQSXGA 16:10
  { 3840, 2160, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // 4K_UHD 16:9
  { 3840, 2400, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // WQUXGA 16:10
  { 4096, 2160, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // DCI_4K 19:10
  { 4096, 3072, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // HXGA 4:3
  { 5120, 2880, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // UHD+ 16:9
  { 5120, 3200, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // WHXGA 16:10
  { 6400, 4096, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // WHSXGA 16:10
  { 6400, 4800, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // HUXGA 4:3
  { 7680, 4320, 32, 60, NULL /* crtc */, NULL /* sequencer */, 0x01 }, // 8K_UHD2 16:9
  { 0, }, // Custom video mode 0, do not delete, must be at the end!
  { 0, }, // Custom video mode 1, do not delete, must be at the end!
  { 0, }, // Custom video mode 2, do not delete, must be at the end!
  { 0, }, // Custom video mode 3, do not delete, must be at the end!
  { 0, }, // Custom video mode 4, do not delete, must be at the end!
  { 0, }, // Custom video mode 5, do not delete, must be at the end!
  { 0, }, // Custom video mode 6, do not delete, must be at the end!
  { 0, }, // Custom video mode 7, do not delete, must be at the end!
  { 0, }, // Custom video mode 8, do not delete, must be at the end!
  { 0, }, // Custom video mode 9, do not delete, must be at the end!
  { 0, }, // Custom video mode 10, do not delete, must be at the end!
  { 0, }, // Custom video mode 11, do not delete, must be at the end!
  { 0, }, // Custom video mode 12, do not delete, must be at the end!
  { 0, }, // Custom video mode 13, do not delete, must be at the end!
  { 0, }, // Custom video mode 14, do not delete, must be at the end!
  { 0, }  // Custom video mode 15, do not delete, must be at the end!
};

const UINT32 VBoxVgaVideoModeCount = sizeof(VBoxVgaVideoModes) / sizeof(VBoxVgaVideoModes[0]);

typedef struct _APPLE_FRAMEBUFFERINFO_PROTOCOL APPLE_FRAMEBUFFERINFO_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *APPLE_FRAMEBUFFERINFO_PROTOCOL_GET_INFO) (
                   IN  APPLE_FRAMEBUFFERINFO_PROTOCOL   *This,
                   OUT UINT32                           *BaseAddr,
                   OUT UINT32                           *Something,
                   OUT UINT32                           *RowBytes,
                   OUT UINT32                           *Width,
                   OUT UINT32                           *Height,
                   OUT UINT32                           *Depth);

struct _APPLE_FRAMEBUFFERINFO_PROTOCOL {
  APPLE_FRAMEBUFFERINFO_PROTOCOL_GET_INFO         GetInfo;
  VBOX_VGA_PRIVATE_DATA                           *Private;
};

EFI_STATUS EFIAPI
GetFrameBufferInfo(IN  APPLE_FRAMEBUFFERINFO_PROTOCOL   *This,
                   OUT UINT32                           *BaseAddr,
                   OUT UINT32                           *Something,
                   OUT UINT32                           *RowBytes,
                   OUT UINT32                           *Width,
                   OUT UINT32                           *Height,
                   OUT UINT32                           *Depth);

static APPLE_FRAMEBUFFERINFO_PROTOCOL gAppleFrameBufferInfo =
{
    GetFrameBufferInfo,
    NULL
};


/*
 *   @todo move this function to the library.
 */
UINT32 VBoxVgaGetVmVariable(UINT32 Variable, CHAR8* Buffer, UINT32 Size)
{
    UINT32 VarLen, i;

    ASMOutU32(EFI_INFO_PORT, Variable);
    VarLen = ASMInU32(EFI_INFO_PORT);

    for (i = 0; i < VarLen && i < Size; i++)
        Buffer[i] = ASMInU8(EFI_INFO_PORT);

    return VarLen;
}


/**
  VBoxVgaControllerDriverSupported

  TODO:    This - add argument and description to function comment
  TODO:    Controller - add argument and description to function comment
  TODO:    RemainingDevicePath - add argument and description to function comment
**/
EFI_STATUS
EFIAPI
VBoxVgaControllerDriverSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN EFI_DEVICE_PATH_PROTOCOL       *RemainingDevicePath
  )
{
  EFI_STATUS          Status;
  EFI_PCI_IO_PROTOCOL *PciIo;
  PCI_TYPE00          Pci;
  EFI_DEV_PATH        *Node;

  //
  // Open the PCI I/O Protocol
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  (VOID **) &PciIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    DEBUG((DEBUG_INFO, "%a:%d status:%r\n", __FILE__, __LINE__, Status));
    return Status;
  }

  //
  // Read the PCI Configuration Header from the PCI Device
  //
  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint32,
                        0,
                        sizeof (Pci) / sizeof (UINT32),
                        &Pci
                        );
  if (EFI_ERROR (Status)) {
    DEBUG((DEBUG_INFO, "%a:%d status:%r\n", __FILE__, __LINE__, Status));
    goto Done;
  }

  Status = EFI_UNSUPPORTED;
  //
  // See if the I/O enable is on.  Most systems only allow one VGA device to be turned on
  // at a time, so see if this is one that is turned on.
  //
  //  if (((Pci.Hdr.Command & 0x01) == 0x01)) {
  //
  // See if this is a VirtualBox VGA or VMSVGA II PCI controller
  //
  if ( ((Pci.Hdr.VendorId == VBOX_VENDOR_ID) && (Pci.Hdr.DeviceId == VBOX_VGA_DEVICE_ID))
    || ((Pci.Hdr.VendorId == VMSVGA_VENDOR_ID) && (Pci.Hdr.DeviceId == VMSVGA_II_DEVICE_ID))) {

      Status = EFI_SUCCESS;
      if (RemainingDevicePath != NULL) {
        Node = (EFI_DEV_PATH *) RemainingDevicePath;
        //
        // Check if RemainingDevicePath is the End of Device Path Node,
        // if yes, return EFI_SUCCESS
        //
        if (!IsDevicePathEnd (Node)) {
          //
          // If RemainingDevicePath isn't the End of Device Path Node,
          // check its validation
          //
          if (Node->DevPath.Type != ACPI_DEVICE_PATH ||
              Node->DevPath.SubType != ACPI_ADR_DP ||
              DevicePathNodeLength(&Node->DevPath) != sizeof(ACPI_ADR_DEVICE_PATH)) {
            DEBUG((DEBUG_INFO, "%a:%d status:%r\n", __FILE__, __LINE__, Status));
            Status = EFI_UNSUPPORTED;
          }
        }
      }
  }

Done:
  //
  // Close the PCI I/O Protocol
  //
  gBS->CloseProtocol (
        Controller,
        &gEfiPciIoProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  DEBUG((DEBUG_INFO, "%a:%d status:%r\n", __FILE__, __LINE__, Status));
  return Status;
}

/**
  VBoxVgaControllerDriverStart

  TODO:    This - add argument and description to function comment
  TODO:    Controller - add argument and description to function comment
  TODO:    RemainingDevicePath - add argument and description to function comment
**/
EFI_STATUS
EFIAPI
VBoxVgaControllerDriverStart (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN EFI_DEVICE_PATH_PROTOCOL       *RemainingDevicePath
  )
{
  EFI_STATUS                      Status;
  VBOX_VGA_PRIVATE_DATA  *Private;
  BOOLEAN                         PciAttributesSaved;
  EFI_DEVICE_PATH_PROTOCOL        *ParentDevicePath;
  ACPI_ADR_DEVICE_PATH            AcpiDeviceNode;
  PCI_TYPE00                      Pci;

  PciAttributesSaved = FALSE;
  //
  // Allocate Private context data for UGA Draw interface.
  //
  Private = AllocateZeroPool (sizeof (VBOX_VGA_PRIVATE_DATA));
  if (Private == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Error;
  }
  gAppleFrameBufferInfo.Private = Private;
  //
  // Set up context record
  //
  Private->Signature  = VBOX_VGA_PRIVATE_DATA_SIGNATURE;
  Private->Handle     = NULL;

  //
  // Open PCI I/O Protocol
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiPciIoProtocolGuid,
                  (VOID **) &Private->PciIo,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  //
  // Read the PCI Configuration Header from the PCI Device again to figure out the model.
  //
  Status = Private->PciIo->Pci.Read (
                                   Private->PciIo,
                                   EfiPciIoWidthUint32,
                                   0,
                                   sizeof (Pci) / sizeof (UINT32),
                                   &Pci
                                   );
  if (EFI_ERROR (Status)) {
    DEBUG((DEBUG_INFO, "%a:%d status:%r\n", __FILE__, __LINE__, Status));
    goto Error;
  }

  Private->DeviceType = Pci.Hdr.DeviceId;

  //
  // Save original PCI attributes
  //
  Status = Private->PciIo->Attributes (
                    Private->PciIo,
                    EfiPciIoAttributeOperationGet,
                    0,
                    &Private->OriginalPciAttributes
                    );

  if (EFI_ERROR (Status)) {
    goto Error;
  }
  PciAttributesSaved = TRUE;

  Status = Private->PciIo->Attributes (
                            Private->PciIo,
                            EfiPciIoAttributeOperationEnable,
                            EFI_PCI_DEVICE_ENABLE | EFI_PCI_IO_ATTRIBUTE_VGA_MEMORY | EFI_PCI_IO_ATTRIBUTE_VGA_IO,
                            NULL
                            );
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  //
  // Get ParentDevicePath
  //
  Status = gBS->HandleProtocol (
                  Controller,
                  &gEfiDevicePathProtocolGuid,
                  (VOID **) &ParentDevicePath
                  );
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  if (FeaturePcdGet (PcdSupportGop)) {
    //
    // Set Gop Device Path
    //
    if (RemainingDevicePath == NULL) {
      ZeroMem (&AcpiDeviceNode, sizeof (ACPI_ADR_DEVICE_PATH));
      AcpiDeviceNode.Header.Type = ACPI_DEVICE_PATH;
      AcpiDeviceNode.Header.SubType = ACPI_ADR_DP;
      AcpiDeviceNode.ADR = ACPI_DISPLAY_ADR (1, 0, 0, 1, 0, ACPI_ADR_DISPLAY_TYPE_VGA, 0, 0);
      SetDevicePathNodeLength (&AcpiDeviceNode.Header, sizeof (ACPI_ADR_DEVICE_PATH));

      Private->GopDevicePath = AppendDevicePathNode (
                                          ParentDevicePath,
                                          (EFI_DEVICE_PATH_PROTOCOL *) &AcpiDeviceNode
                                          );
    } else if (!IsDevicePathEnd (RemainingDevicePath)) {
      //
      // If RemainingDevicePath isn't the End of Device Path Node,
      // only scan the specified device by RemainingDevicePath
      //
      Private->GopDevicePath = AppendDevicePathNode (ParentDevicePath, RemainingDevicePath);
    } else {
      //
      // If RemainingDevicePath is the End of Device Path Node,
      // don't create child device and return EFI_SUCCESS
      //
      Private->GopDevicePath = NULL;
    }

    if (Private->GopDevicePath != NULL) {
      //
      // Create child handle and device path protocol first
      //
      Private->Handle = NULL;
      Status = gBS->InstallMultipleProtocolInterfaces (
                      &Private->Handle,
                      &gEfiDevicePathProtocolGuid,
                      Private->GopDevicePath,
                      NULL
                      );
    }
  }

  //
  // Now do some model-specific setup.
  //
  if (Private->DeviceType == VMSVGA_II_DEVICE_ID) {
      EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR     *IOPortDesc;

      // VMSVGA
      Private->BarIndexFB = 1;

      Private->PciIo->GetBarAttributes (
                          Private->PciIo,
                          0,    // BAR 0 is the I/O port space
                          NULL,
                          (VOID**) &IOPortDesc
                          );
      Private->IOBase = (UINT16)IOPortDesc->AddrRangeMin;

      //
      // Query the VRAM size (for proper mode filtering)
      //
      ASMOutU32(Private->IOBase + SVGA_INDEX_PORT, SVGA_REG_VRAM_SIZE);
      Private->VRAMSize = ASMInU32(Private->IOBase + SVGA_VALUE_PORT);

#if 0
      // Not used because of buggy emulation(?) which is not fully compatible
      // with the simple "legacy" VMSVGA II register interface.

      // Enable the device, set initial mode
      ASMOutU32(Private->IOBase + SVGA_INDEX_PORT, SVGA_REG_WIDTH);
      ASMOutU32(Private->IOBase + SVGA_VALUE_PORT, 1024);
      ASMOutU32(Private->IOBase + SVGA_INDEX_PORT, SVGA_REG_HEIGHT);
      ASMOutU32(Private->IOBase + SVGA_VALUE_PORT, 768);
      ASMOutU32(Private->IOBase + SVGA_INDEX_PORT, SVGA_REG_BYTES_PER_LINE);
      ASMOutU32(Private->IOBase + SVGA_VALUE_PORT, 768 * 4);
      ASMOutU32(Private->IOBase + SVGA_INDEX_PORT, SVGA_REG_BITS_PER_PIXEL);
      ASMOutU32(Private->IOBase + SVGA_VALUE_PORT, 32);
      ASMOutU32(Private->IOBase + SVGA_INDEX_PORT, SVGA_REG_CONFIG_DONE);
      ASMOutU32(Private->IOBase + SVGA_VALUE_PORT, 1);

      ASMOutU32(Private->IOBase + SVGA_INDEX_PORT, SVGA_REG_ENABLE);
      ASMOutU32(Private->IOBase + SVGA_VALUE_PORT, SVGA_REG_ENABLE_ENABLE);
#endif
  } else {
      // VBoxVGA / VBoxSVGA
      Private->BarIndexFB = 0;
      //
      // Get VRAM size, needed for constructing a correct video mode list
      //
      Private->VRAMSize = ASMInU32(VBE_DISPI_IOPORT_DATA);
  }


  //
  // Construct video mode list
  //
  Status = VBoxVgaVideoModeSetup (Private);
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  if (FeaturePcdGet (PcdSupportUga)) {
    //
    // Start the UGA Draw software stack.
    //
    Status = VBoxVgaUgaDrawConstructor (Private);
    ASSERT_EFI_ERROR (Status);

    Private->UgaDevicePath = ParentDevicePath;
    Status = gBS->InstallMultipleProtocolInterfaces (
                    &Controller,
                    //&gEfiUgaDrawProtocolGuid,
                    //&Private->UgaDraw,
                    &gEfiDevicePathProtocolGuid,
                    Private->UgaDevicePath,
                    NULL
                    );
    Status = gBS->InstallMultipleProtocolInterfaces (
                    &Controller,
                    &gEfiUgaDrawProtocolGuid,
                    &Private->UgaDraw,
                    NULL
                    );

  } else if (FeaturePcdGet (PcdSupportGop)) {
    if (Private->GopDevicePath == NULL) {
      //
      // If RemainingDevicePath is the End of Device Path Node,
      // don't create child device and return EFI_SUCCESS
      //
      Status = EFI_SUCCESS;
    } else {

      //
      // Start the GOP software stack.
      //
      Status = VBoxVgaGraphicsOutputConstructor (Private);
      ASSERT_EFI_ERROR (Status);

      Status = gBS->InstallMultipleProtocolInterfaces (
                      &Private->Handle,
                      &gEfiGraphicsOutputProtocolGuid,
                      &Private->GraphicsOutput,
                      &gEfiEdidDiscoveredProtocolGuid,
                      &Private->EdidDiscovered,
                      &gEfiEdidActiveProtocolGuid,
                      &Private->EdidActive,
                      NULL
                      );
    }
  } else {
    //
    // This driver must support eithor GOP or UGA or both.
    //
    ASSERT (FALSE);
    Status = EFI_UNSUPPORTED;
  }

Error:
  if (EFI_ERROR (Status)) {
    if (Private) {
      if (Private->PciIo) {
        if (PciAttributesSaved == TRUE) {
          //
          // Restore original PCI attributes
          //
          Private->PciIo->Attributes (
                          Private->PciIo,
                          EfiPciIoAttributeOperationSet,
                          Private->OriginalPciAttributes,
                          NULL
                          );
        }
        //
        // Close the PCI I/O Protocol
        //
        gBS->CloseProtocol (
              Private->Handle,
              &gEfiPciIoProtocolGuid,
              This->DriverBindingHandle,
              Private->Handle
              );
      }

      gBS->FreePool (Private);
    }
  }

  return Status;
}

/**
  VBoxVgaControllerDriverStop

  TODO:    This - add argument and description to function comment
  TODO:    Controller - add argument and description to function comment
  TODO:    NumberOfChildren - add argument and description to function comment
  TODO:    ChildHandleBuffer - add argument and description to function comment
  TODO:    EFI_SUCCESS - add return value to function comment
**/
EFI_STATUS
EFIAPI
VBoxVgaControllerDriverStop (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN UINTN                          NumberOfChildren,
  IN EFI_HANDLE                     *ChildHandleBuffer
  )
{
  EFI_UGA_DRAW_PROTOCOL           *UgaDraw;
  EFI_GRAPHICS_OUTPUT_PROTOCOL    *GraphicsOutput;

  EFI_STATUS                      Status;
  VBOX_VGA_PRIVATE_DATA  *Private;

  if (FeaturePcdGet (PcdSupportUga)) {
    Status = gBS->OpenProtocol (
                    Controller,
                    &gEfiUgaDrawProtocolGuid,
                    (VOID **) &UgaDraw,
                    This->DriverBindingHandle,
                    Controller,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    //
    // Get our private context information
    //
    Private = VBOX_VGA_PRIVATE_DATA_FROM_UGA_DRAW_THIS (UgaDraw);
    VBoxVgaUgaDrawDestructor (Private);

    if (FeaturePcdGet (PcdSupportGop)) {
      VBoxVgaGraphicsOutputDestructor (Private);
      //
      // Remove the UGA and GOP protocol interface from the system
      //
      Status = gBS->UninstallMultipleProtocolInterfaces (
                      Private->Handle,
                      &gEfiUgaDrawProtocolGuid,
                      &Private->UgaDraw,
                      &gEfiGraphicsOutputProtocolGuid,
                      &Private->GraphicsOutput,
                      NULL
                      );
    } else {
      //
      // Remove the UGA Draw interface from the system
      //
      Status = gBS->UninstallMultipleProtocolInterfaces (
                      Private->Handle,
                      &gEfiUgaDrawProtocolGuid,
                      &Private->UgaDraw,
                      NULL
                      );
    }
  } else {
    Status = gBS->OpenProtocol (
                    Controller,
                    &gEfiGraphicsOutputProtocolGuid,
                    (VOID **) &GraphicsOutput,
                    This->DriverBindingHandle,
                    Controller,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    //
    // Get our private context information
    //
    Private = VBOX_VGA_PRIVATE_DATA_FROM_GRAPHICS_OUTPUT_THIS (GraphicsOutput);

    VBoxVgaGraphicsOutputDestructor (Private);
    //
    // Remove the GOP protocol interface from the system
    //
    Status = gBS->UninstallMultipleProtocolInterfaces (
                    Private->Handle,
                    &gEfiGraphicsOutputProtocolGuid,
                    &Private->GraphicsOutput,
                    NULL
                    );
  }

  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Private->ModeData) {
      FreePool(Private->ModeData);
      Private->ModeData = NULL;
  }

  //
  // Restore original PCI attributes
  //
  Private->PciIo->Attributes (
                  Private->PciIo,
                  EfiPciIoAttributeOperationSet,
                  Private->OriginalPciAttributes,
                  NULL
                  );

  //
  // Close the PCI I/O Protocol
  //
  gBS->CloseProtocol (
        Controller,
        &gEfiPciIoProtocolGuid,
        This->DriverBindingHandle,
        Controller
        );

  //
  // Free our instance data
  //
  gBS->FreePool (Private);

  return EFI_SUCCESS;
}

/**
  VBoxVgaUgaDrawDestructor

  TODO:    Private - add argument and description to function comment
  TODO:    EFI_SUCCESS - add return value to function comment
**/
EFI_STATUS
VBoxVgaUgaDrawDestructor (
  VBOX_VGA_PRIVATE_DATA  *Private
  )
{
  return EFI_SUCCESS;
}

/**
  TODO: Add function description

  @param  Private TODO: add argument description
  @param  Index TODO: add argument description
  @param  Red TODO: add argument description
  @param  Green TODO: add argument description
  @param  Blue TODO: add argument description

  TODO: add return values

**/
VOID
SetPaletteColor (
  VBOX_VGA_PRIVATE_DATA  *Private,
  UINTN                           Index,
  UINT8                           Red,
  UINT8                           Green,
  UINT8                           Blue
  )
{
  ASMOutU8(PALETTE_INDEX_REGISTER, (UINT8) Index);
  ASMOutU8(PALETTE_DATA_REGISTER, (UINT8) (Red >> 2));
  ASMOutU8(PALETTE_DATA_REGISTER, (UINT8) (Green >> 2));
  ASMOutU8(PALETTE_DATA_REGISTER, (UINT8) (Blue >> 2));
}

/**
  TODO: Add function description

  @param  Private TODO: add argument description

  TODO: add return values

**/
VOID
SetDefaultPalette (
  VBOX_VGA_PRIVATE_DATA  *Private
  )
{
#if 1
  UINTN Index;
  UINTN RedIndex;
  UINTN GreenIndex;
  UINTN BlueIndex;
  Index = 0;
  for (RedIndex = 0; RedIndex < 8; RedIndex++) {
    for (GreenIndex = 0; GreenIndex < 8; GreenIndex++) {
      for (BlueIndex = 0; BlueIndex < 4; BlueIndex++) {
        SetPaletteColor (Private, Index, (UINT8) (RedIndex << 5), (UINT8) (GreenIndex << 5), (UINT8) (BlueIndex << 6));
        Index++;
      }
    }
  }
#else
     {
         int i;
         static const UINT8 s_a3bVgaDac[64*3] =
         {
             0x00, 0x00, 0x00,
             0x00, 0x00, 0x2A,
             0x00, 0x2A, 0x00,
             0x00, 0x2A, 0x2A,
             0x2A, 0x00, 0x00,
             0x2A, 0x00, 0x2A,
             0x2A, 0x2A, 0x00,
             0x2A, 0x2A, 0x2A,
             0x00, 0x00, 0x15,
             0x00, 0x00, 0x3F,
             0x00, 0x2A, 0x15,
             0x00, 0x2A, 0x3F,
             0x2A, 0x00, 0x15,
             0x2A, 0x00, 0x3F,
             0x2A, 0x2A, 0x15,
             0x2A, 0x2A, 0x3F,
             0x00, 0x15, 0x00,
             0x00, 0x15, 0x2A,
             0x00, 0x3F, 0x00,
             0x00, 0x3F, 0x2A,
             0x2A, 0x15, 0x00,
             0x2A, 0x15, 0x2A,
             0x2A, 0x3F, 0x00,
             0x2A, 0x3F, 0x2A,
             0x00, 0x15, 0x15,
             0x00, 0x15, 0x3F,
             0x00, 0x3F, 0x15,
             0x00, 0x3F, 0x3F,
             0x2A, 0x15, 0x15,
             0x2A, 0x15, 0x3F,
             0x2A, 0x3F, 0x15,
             0x2A, 0x3F, 0x3F,
             0x15, 0x00, 0x00,
             0x15, 0x00, 0x2A,
             0x15, 0x2A, 0x00,
             0x15, 0x2A, 0x2A,
             0x3F, 0x00, 0x00,
             0x3F, 0x00, 0x2A,
             0x3F, 0x2A, 0x00,
             0x3F, 0x2A, 0x2A,
             0x15, 0x00, 0x15,
             0x15, 0x00, 0x3F,
             0x15, 0x2A, 0x15,
             0x15, 0x2A, 0x3F,
             0x3F, 0x00, 0x15,
             0x3F, 0x00, 0x3F,
             0x3F, 0x2A, 0x15,
             0x3F, 0x2A, 0x3F,
             0x15, 0x15, 0x00,
             0x15, 0x15, 0x2A,
             0x15, 0x3F, 0x00,
             0x15, 0x3F, 0x2A,
             0x3F, 0x15, 0x00,
             0x3F, 0x15, 0x2A,
             0x3F, 0x3F, 0x00,
             0x3F, 0x3F, 0x2A,
             0x15, 0x15, 0x15,
             0x15, 0x15, 0x3F,
             0x15, 0x3F, 0x15,
             0x15, 0x3F, 0x3F,
             0x3F, 0x15, 0x15,
             0x3F, 0x15, 0x3F,
             0x3F, 0x3F, 0x15,
             0x3F, 0x3F, 0x3F
          };

          for (i = 0; i < 64; ++i)
          {
              ASMOutU8(PALETTE_INDEX_REGISTER, (UINT8)i);
              ASMOutU8(PALETTE_DATA_REGISTER, s_a3bVgaDac[i*3 + 0]);
              ASMOutU8(PALETTE_DATA_REGISTER, s_a3bVgaDac[i*3 + 1]);
              ASMOutU8(PALETTE_DATA_REGISTER, s_a3bVgaDac[i*3 + 2]);
          }
     }

#endif
}

/**
  TODO: Add function description

  @param  Private TODO: add argument description

  TODO: add return values

**/
VOID
ClearScreen (
  VBOX_VGA_PRIVATE_DATA  *Private
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL blt;
  blt.Blue     = 0;
  blt.Green    = 0;
  blt.Red      = 0;
  blt.Reserved = 0;
  Private->PciIo->Mem.Write (
                        Private->PciIo,
                        EfiPciIoWidthFillUint32,
                        Private->BarIndexFB,
                        0,
                          Private->ModeData[Private->CurrentMode].HorizontalResolution
                        * Private->ModeData[Private->CurrentMode].VerticalResolution,
                        &blt
                        );
}

/**
  TODO: Add function description

  @param  Private TODO: add argument description

  TODO: add return values

**/
VOID
DrawLogo (
  VBOX_VGA_PRIVATE_DATA  *Private,
  UINTN                  ScreenWidth,
  UINTN                  ScreenHeight
  )
{
  DEBUG((DEBUG_INFO, "UGA is %a GOP is %a\n",
        FeaturePcdGet(PcdSupportUga) ? "on" : "off",
        FeaturePcdGet(PcdSupportGop) ? "on" : "off"
  ));
}

/**
  TODO: Add function description

  @param  Private TODO: add argument description
  @param  ModeData TODO: add argument description

  TODO: add return values

**/
VOID
InitializeGraphicsMode (
  VBOX_VGA_PRIVATE_DATA  *Private,
  VBOX_VGA_VIDEO_MODES   *ModeData
  )
{
    UINT16 DeviceId;
    EFI_STATUS Status;
    int i;

    DEBUG((DEBUG_INFO, "%a:%d InitializeGraphicsMode: %dx%d bpp:%d\n", __FILE__, __LINE__, ModeData->Width, ModeData->Height, ModeData->ColorDepth));

    //
    // Read the PCI ID from the PCI Device (dummy)
    //
    Status = Private->PciIo->Pci.Read (
             Private->PciIo,
             EfiPciIoWidthUint16,
             PCI_DEVICE_ID_OFFSET,
             1,
             &DeviceId
             );
    ASSERT_EFI_ERROR(Status);

    ASMOutU8(MISC_OUTPUT_REGISTER, 0xc3);
    ASMOutU16(SEQ_ADDRESS_REGISTER, 0x0204);

    ASMInU8(INPUT_STATUS_1_REGISTER);   // reset attribute address/data flip-flop
    ASMOutU8(ATT_ADDRESS_REGISTER, 0);  // blank screen using the attribute address register

    ASMOutU16(CRTC_ADDRESS_REGISTER, 0x0011);

    ASMOutU16(SEQ_ADDRESS_REGISTER, 0x0100);
    if (ModeData->SeqSettings)
      BOUTB(ModeData->SeqSettings, 5, SEQ_ADDRESS_REGISTER, SEQ_DATA_REGISTER);
    else
      BOUTB(Seq_Default, 5, SEQ_ADDRESS_REGISTER, SEQ_DATA_REGISTER);
    ASMOutU16(SEQ_ADDRESS_REGISTER, 0x0300);

    BOUTB(GraphicsController, 9, GRAPH_ADDRESS_REGISTER, GRAPH_DATA_REGISTER);

    ASMInU8(INPUT_STATUS_1_REGISTER);   // reset attribute address/data flip-flop
    BOUTB(AttributeController, 21, ATT_ADDRESS_REGISTER, ATT_DATA_REGISTER);

    ASMOutU8(MISC_OUTPUT_REGISTER, ModeData->MiscSetting);

    if (ModeData->ColorDepth <= 8)
    {
      ASMOutU8(DAC_PIXEL_MASK_REGISTER, 0xff);
      SetDefaultPalette(Private);
    }

    if (!ModeData->CrtcSettings)
    {
        // No CRTC settings, use VBE
        ASMOutU16(VBE_DISPI_IOPORT_INDEX, 0x00); ASMOutU16(VBE_DISPI_IOPORT_DATA, 0xb0c0);                          // ID
        ASMOutU16(VBE_DISPI_IOPORT_INDEX, 0x04); ASMOutU16(VBE_DISPI_IOPORT_DATA, 0);                               // ENABLE
        ASMOutU16(VBE_DISPI_IOPORT_INDEX, 0x01); ASMOutU16(VBE_DISPI_IOPORT_DATA, (UINT16)ModeData->Width);         // XRES
        ASMOutU16(VBE_DISPI_IOPORT_INDEX, 0x02); ASMOutU16(VBE_DISPI_IOPORT_DATA, (UINT16)ModeData->Height);        // YRES
        ASMOutU16(VBE_DISPI_IOPORT_INDEX, 0x03); ASMOutU16(VBE_DISPI_IOPORT_DATA, (UINT16)ModeData->ColorDepth);    // BPP
        ASMOutU16(VBE_DISPI_IOPORT_INDEX, 0x05); ASMOutU16(VBE_DISPI_IOPORT_DATA, 0);                               // BANK
        ASMOutU16(VBE_DISPI_IOPORT_INDEX, 0x06); ASMOutU16(VBE_DISPI_IOPORT_DATA, (UINT16)ModeData->Width);         // VIRT_WIDTH
        ASMOutU16(VBE_DISPI_IOPORT_INDEX, 0x07); ASMOutU16(VBE_DISPI_IOPORT_DATA, (UINT16)ModeData->Height);        // VIRT_HEIGHT
        ASMOutU16(VBE_DISPI_IOPORT_INDEX, 0x08); ASMOutU16(VBE_DISPI_IOPORT_DATA, 0);                               // X_OFFSET
        ASMOutU16(VBE_DISPI_IOPORT_INDEX, 0x09); ASMOutU16(VBE_DISPI_IOPORT_DATA, 0);                               // Y_OFFSET
        ASMOutU16(VBE_DISPI_IOPORT_INDEX, 0x04); ASMOutU16(VBE_DISPI_IOPORT_DATA, 1);                               // ENABLE
        /// @todo enabling VBE is automatically tweaking the CRTC, GC, SC, clears the
        // screen and at the end unblanks graphics. So make sure that nothing is done
        // after this which needs blanking. Way too much magic, but that's how it is...
    }
    else
    {
        BOUTB(ModeData->CrtcSettings, 25, CRTC_ADDRESS_REGISTER, CRTC_DATA_REGISTER);
    }

    ASMInU8(INPUT_STATUS_1_REGISTER);       // reset attribute address/data flip-flop
    ASMOutU8(ATT_ADDRESS_REGISTER, 0x20);   // unblank screen

    ClearScreen(Private);
}

/** Aka know as AppleGraphInfoProtocolGuid in other sources. */
#define EFI_UNKNOWN_2_PROTOCOL_GUID \
  { 0xE316E100, 0x0751, 0x4C49, {0x90, 0x56, 0x48, 0x6C, 0x7E, 0x47, 0x29, 0x03} }

EFI_GUID gEfiAppleFrameBufferInfoGuid = EFI_UNKNOWN_2_PROTOCOL_GUID;

EFI_STATUS EFIAPI
GetFrameBufferInfo(IN  APPLE_FRAMEBUFFERINFO_PROTOCOL   *This,
                   OUT UINT32                           *BaseAddr,
                   OUT UINT32                           *Something,
                   OUT UINT32                           *RowBytes,
                   OUT UINT32                           *Width,
                   OUT UINT32                           *Height,
                   OUT UINT32                           *Depth)
{
    EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR     *FrameBufDesc;
    UINT32 W, H, BPP;
    VBOX_VGA_PRIVATE_DATA  *Private = This->Private;
    UINTN CurrentModeNumber = Private->CurrentMode;
    VBOX_VGA_MODE_DATA const *pCurrentMode = &Private->ModeData[CurrentModeNumber];

    W = pCurrentMode->HorizontalResolution;
    H = pCurrentMode->VerticalResolution;
    BPP = pCurrentMode->ColorDepth;
    DEBUG((DEBUG_INFO, "%a:%d GetFrameBufferInfo: %dx%d bpp:%d\n", __FILE__, __LINE__, W, H, BPP));

    Private->PciIo->GetBarAttributes (
                        Private->PciIo,
                        Private->BarIndexFB,
                        NULL,
                        (VOID**) &FrameBufDesc
                        );


    /* EFI firmware remaps it here */
    *BaseAddr = (UINT32)FrameBufDesc->AddrRangeMin;
    *RowBytes = W * BPP / 8;
    *Width = W;
    *Height = H;
    *Depth = BPP;
    // what *Something shall be?

    return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
InitializeVBoxVga (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
  EFI_STATUS              Status;

  Status = EfiLibInstallDriverBindingComponentName2 (
             ImageHandle,
             SystemTable,
             &gVBoxVgaDriverBinding,
             ImageHandle,
             &gVBoxVgaComponentName,
             &gVBoxVgaComponentName2
             );
  ASSERT_EFI_ERROR (Status);

  //
  // Install EFI Driver Supported EFI Version Protocol required for
  // EFI drivers that are on PCI and other plug in cards.
  //
  gVBoxVgaDriverSupportedEfiVersion.FirmwareVersion = PcdGet32 (PcdDriverSupportedEfiVersion);
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gEfiDriverSupportedEfiVersionProtocolGuid,
                  &gVBoxVgaDriverSupportedEfiVersion,
                  &gEfiAppleFrameBufferInfoGuid,
                  &gAppleFrameBufferInfo,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);

  return Status;
}
