/* $Id: VBoxVga.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVga.h
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

  Cirrus Logic 5430 Controller Driver

  Copyright (c) 2006 - 2007, Intel Corporation
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

#ifndef _VBOX_VGA_H_
#define _VBOX_VGA_H_


#include <Uefi.h>
#include <Protocol/UgaDraw.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/PciIo.h>
#include <Protocol/DriverSupportedEfiVersion.h>
#include <Protocol/EdidOverride.h>
#include <Protocol/EdidDiscovered.h>
#include <Protocol/EdidActive.h>
#include <Protocol/DevicePath.h>

#include <Library/DebugLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>
#include <Library/PcdLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/TimerLib.h>

#include <IndustryStandard/Pci.h>

#include "VBoxPkg.h"
#include "DevEFI.h"
#include "VBox/Graphics/VBoxVideoVBE.h"
#include "VBox/Graphics/VBoxVideoVBEPrivate.h"

//
// VirtualBox VGA PCI Configuration Header values
//
#define VBOX_VENDOR_ID          0x80ee
#define VBOX_VGA_DEVICE_ID      0xbeef


//
// VMSVGA II PCI Configuration Header values
//
#define VMSVGA_VENDOR_ID        0x15ad
#define VMSVGA_II_DEVICE_ID     0x0405

// Port offsets relative to BAR 0
#define SVGA_INDEX_PORT     0
#define SVGA_VALUE_PORT     1

// SVGA_REG_ENABLE bits
#define SVGA_REG_ENABLE_DISABLE     0
#define SVGA_REG_ENABLE_ENABLE      1

// Registers
#define SVGA_REG_ENABLE             1
#define SVGA_REG_WIDTH              2
#define SVGA_REG_HEIGHT             3
#define SVGA_REG_MAX_WIDTH          4
#define SVGA_REG_MAX_HEIGHT         5
#define SVGA_REG_DEPTH              6
#define SVGA_REG_BITS_PER_PIXEL     7
#define SVGA_REG_BYTES_PER_LINE     12
#define SVGA_REG_FB_START           13
#define SVGA_REG_FB_OFFSET          14
#define SVGA_REG_VRAM_SIZE          15
#define SVGA_REG_CONFIG_DONE        20      ///@todo: Why do we need this?

//
// VirtualBox VGA Graphical Mode Data
//
typedef struct {
  UINT32  ModeNumber;
  UINT32  HorizontalResolution;
  UINT32  VerticalResolution;
  UINT32  ColorDepth;
  UINT32  RefreshRate;
} VBOX_VGA_MODE_DATA;

#define GRAPHICS_OUTPUT_INVALIDE_MODE_NUMBER  0xffff

//
// VirtualBox VGA Private Data Structure
//
#define VBOX_VGA_PRIVATE_DATA_SIGNATURE  SIGNATURE_32 ('V', 'B', 'V', 'D')

typedef struct {
  UINT64                                Signature;
  EFI_HANDLE                            Handle;
  EFI_PCI_IO_PROTOCOL                   *PciIo;
  UINT64                                OriginalPciAttributes;
  EFI_UGA_DRAW_PROTOCOL                 UgaDraw;
  EFI_GRAPHICS_OUTPUT_PROTOCOL          GraphicsOutput;
  EFI_EDID_DISCOVERED_PROTOCOL          EdidDiscovered;
  EFI_EDID_ACTIVE_PROTOCOL              EdidActive;
  EFI_DEVICE_PATH_PROTOCOL              *GopDevicePath;
  EFI_DEVICE_PATH_PROTOCOL              *UgaDevicePath;
  UINTN                                 CurrentMode;
  UINTN                                 MaxMode;
  VBOX_VGA_MODE_DATA                    *ModeData;
  BOOLEAN                               HardwareNeedsStarting;
  UINT8                                 BarIndexFB;
  UINT16                                DeviceType;
  UINT16                                IOBase;
  UINT32                                VRAMSize;
} VBOX_VGA_PRIVATE_DATA;

///
/// Video Mode structure
///
typedef struct {
  UINT32  Width;
  UINT32  Height;
  UINT32  ColorDepth;
  UINT32  RefreshRate;
  /// CRTC settings are optional. If NULL then VBE is used
  UINT8   *CrtcSettings;
  /// Sequencer settings are optional. If NULL then defaults are used
  UINT8   *SeqSettings;
  UINT8   MiscSetting;
} VBOX_VGA_VIDEO_MODES;

#define VBOX_VGA_PRIVATE_DATA_FROM_UGA_DRAW_THIS(a) \
  CR(a, VBOX_VGA_PRIVATE_DATA, UgaDraw, VBOX_VGA_PRIVATE_DATA_SIGNATURE)

#define VBOX_VGA_PRIVATE_DATA_FROM_GRAPHICS_OUTPUT_THIS(a) \
  CR(a, VBOX_VGA_PRIVATE_DATA, GraphicsOutput, VBOX_VGA_PRIVATE_DATA_SIGNATURE)


//
// Global Variables
//
extern UINT8                                      AttributeController[];
extern UINT8                                      GraphicsController[];
extern UINT8                                      Crtc_640_480_256_60[];
extern UINT8                                      Seq_640_480_256_60[];
extern UINT8                                      Crtc_800_600_256_60[];
extern UINT8                                      Seq_800_600_256_60[];
extern UINT8                                      Crtc_1024_768_256_60[];
extern UINT8                                      Seq_1024_768_256_60[];
extern VBOX_VGA_VIDEO_MODES                       VBoxVgaVideoModes[];
extern const UINT32                               VBoxVgaVideoModeCount;
extern EFI_DRIVER_BINDING_PROTOCOL                gVBoxVgaDriverBinding;
extern EFI_COMPONENT_NAME_PROTOCOL                gVBoxVgaComponentName;
extern EFI_COMPONENT_NAME2_PROTOCOL               gVBoxVgaComponentName2;
extern EFI_DRIVER_SUPPORTED_EFI_VERSION_PROTOCOL  gVBoxVgaDriverSupportedEfiVersion;

//
// Io Registers defined by VGA
//
#define CRTC_ADDRESS_REGISTER   0x3d4
#define CRTC_DATA_REGISTER      0x3d5
#define SEQ_ADDRESS_REGISTER    0x3c4
#define SEQ_DATA_REGISTER       0x3c5
#define GRAPH_ADDRESS_REGISTER  0x3ce
#define GRAPH_DATA_REGISTER     0x3cf
#define ATT_ADDRESS_REGISTER    0x3c0
#define ATT_DATA_REGISTER       0x3c1
#define MISC_OUTPUT_REGISTER    0x3c2
#define INPUT_STATUS_1_REGISTER 0x3da
#define DAC_PIXEL_MASK_REGISTER 0x3c6
#define PALETTE_INDEX_REGISTER  0x3c8
#define PALETTE_DATA_REGISTER   0x3c9


//
// UGA Draw Hardware abstraction internal worker functions
//
EFI_STATUS
VBoxVgaUgaDrawConstructor (
  VBOX_VGA_PRIVATE_DATA  *Private
  );

EFI_STATUS
VBoxVgaUgaDrawDestructor (
  VBOX_VGA_PRIVATE_DATA  *Private
  );

//
// Graphics Output Hardware abstraction internal worker functions
//
EFI_STATUS
VBoxVgaGraphicsOutputConstructor (
  VBOX_VGA_PRIVATE_DATA  *Private
  );

EFI_STATUS
VBoxVgaGraphicsOutputDestructor (
  VBOX_VGA_PRIVATE_DATA  *Private
  );


//
// EFI_DRIVER_BINDING_PROTOCOL Protocol Interface
//
/**
  TODO: Add function description

  @param  This TODO: add argument description
  @param  Controller TODO: add argument description
  @param  RemainingDevicePath TODO: add argument description

  TODO: add return values

**/
EFI_STATUS
EFIAPI
VBoxVgaControllerDriverSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  );

/**
  TODO: Add function description

  @param  This TODO: add argument description
  @param  Controller TODO: add argument description
  @param  RemainingDevicePath TODO: add argument description

  TODO: add return values

**/
EFI_STATUS
EFIAPI
VBoxVgaControllerDriverStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  );

/**
  TODO: Add function description

  @param  This TODO: add argument description
  @param  Controller TODO: add argument description
  @param  NumberOfChildren TODO: add argument description
  @param  ChildHandleBuffer TODO: add argument description

  TODO: add return values

**/
EFI_STATUS
EFIAPI
VBoxVgaControllerDriverStop (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   Controller,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer
  );

//
// EFI Component Name Functions
//
/**
  Retrieves a Unicode string that is the user readable name of the driver.

  This function retrieves the user readable name of a driver in the form of a
  Unicode string. If the driver specified by This has a user readable name in
  the language specified by Language, then a pointer to the driver name is
  returned in DriverName, and EFI_SUCCESS is returned. If the driver specified
  by This does not support the language specified by Language,
  then EFI_UNSUPPORTED is returned.

  @param  This[in]              A pointer to the EFI_COMPONENT_NAME2_PROTOCOL or
                                EFI_COMPONENT_NAME_PROTOCOL instance.

  @param  Language[in]          A pointer to a Null-terminated ASCII string
                                array indicating the language. This is the
                                language of the driver name that the caller is
                                requesting, and it must match one of the
                                languages specified in SupportedLanguages. The
                                number of languages supported by a driver is up
                                to the driver writer. Language is specified
                                in RFC 4646 or ISO 639-2 language code format.

  @param  DriverName[out]       A pointer to the Unicode string to return.
                                This Unicode string is the name of the
                                driver specified by This in the language
                                specified by Language.

  @retval EFI_SUCCESS           The Unicode string for the Driver specified by
                                This and the language specified by Language was
                                returned in DriverName.

  @retval EFI_INVALID_PARAMETER Language is NULL.

  @retval EFI_INVALID_PARAMETER DriverName is NULL.

  @retval EFI_UNSUPPORTED       The driver specified by This does not support
                                the language specified by Language.

**/
EFI_STATUS
EFIAPI
VBoxVgaComponentNameGetDriverName (
  IN  EFI_COMPONENT_NAME_PROTOCOL  *This,
  IN  CHAR8                        *Language,
  OUT CHAR16                       **DriverName
  );


/**
  Retrieves a Unicode string that is the user readable name of the controller
  that is being managed by a driver.

  This function retrieves the user readable name of the controller specified by
  ControllerHandle and ChildHandle in the form of a Unicode string. If the
  driver specified by This has a user readable name in the language specified by
  Language, then a pointer to the controller name is returned in ControllerName,
  and EFI_SUCCESS is returned.  If the driver specified by This is not currently
  managing the controller specified by ControllerHandle and ChildHandle,
  then EFI_UNSUPPORTED is returned.  If the driver specified by This does not
  support the language specified by Language, then EFI_UNSUPPORTED is returned.

  @param  This[in]              A pointer to the EFI_COMPONENT_NAME2_PROTOCOL or
                                EFI_COMPONENT_NAME_PROTOCOL instance.

  @param  ControllerHandle[in]  The handle of a controller that the driver
                                specified by This is managing.  This handle
                                specifies the controller whose name is to be
                                returned.

  @param  ChildHandle[in]       The handle of the child controller to retrieve
                                the name of.  This is an optional parameter that
                                may be NULL.  It will be NULL for device
                                drivers.  It will also be NULL for a bus drivers
                                that wish to retrieve the name of the bus
                                controller.  It will not be NULL for a bus
                                driver that wishes to retrieve the name of a
                                child controller.

  @param  Language[in]          A pointer to a Null-terminated ASCII string
                                array indicating the language.  This is the
                                language of the driver name that the caller is
                                requesting, and it must match one of the
                                languages specified in SupportedLanguages. The
                                number of languages supported by a driver is up
                                to the driver writer. Language is specified in
                                RFC 4646 or ISO 639-2 language code format.

  @param  ControllerName[out]   A pointer to the Unicode string to return.
                                This Unicode string is the name of the
                                controller specified by ControllerHandle and
                                ChildHandle in the language specified by
                                Language from the point of view of the driver
                                specified by This.

  @retval EFI_SUCCESS           The Unicode string for the user readable name in
                                the language specified by Language for the
                                driver specified by This was returned in
                                DriverName.

  @retval EFI_INVALID_PARAMETER ControllerHandle is not a valid EFI_HANDLE.

  @retval EFI_INVALID_PARAMETER ChildHandle is not NULL and it is not a valid
                                EFI_HANDLE.

  @retval EFI_INVALID_PARAMETER Language is NULL.

  @retval EFI_INVALID_PARAMETER ControllerName is NULL.

  @retval EFI_UNSUPPORTED       The driver specified by This is not currently
                                managing the controller specified by
                                ControllerHandle and ChildHandle.

  @retval EFI_UNSUPPORTED       The driver specified by This does not support
                                the language specified by Language.

**/
EFI_STATUS
EFIAPI
VBoxVgaComponentNameGetControllerName (
  IN  EFI_COMPONENT_NAME_PROTOCOL                     *This,
  IN  EFI_HANDLE                                      ControllerHandle,
  IN  EFI_HANDLE                                      ChildHandle        OPTIONAL,
  IN  CHAR8                                           *Language,
  OUT CHAR16                                          **ControllerName
  );


//
// Local Function Prototypes
//
VOID
InitializeGraphicsMode (
  VBOX_VGA_PRIVATE_DATA  *Private,
  VBOX_VGA_VIDEO_MODES   *ModeData
  );

VOID
SetPaletteColor (
  VBOX_VGA_PRIVATE_DATA  *Private,
  UINTN                           Index,
  UINT8                           Red,
  UINT8                           Green,
  UINT8                           Blue
  );

VOID
SetDefaultPalette (
  VBOX_VGA_PRIVATE_DATA  *Private
  );

VOID
DrawLogo (
  VBOX_VGA_PRIVATE_DATA  *Private,
  UINTN                           ScreenWidth,
  UINTN                           ScreenHeight
  );

EFI_STATUS
VBoxVgaVideoModeSetup (
  VBOX_VGA_PRIVATE_DATA  *Private
  );

UINT32 VBoxVgaGetVmVariable(UINT32 Variable, CHAR8* Buffer, UINT32 Size);

#endif
