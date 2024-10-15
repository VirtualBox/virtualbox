/* $Id: VBoxVgaI2c.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVgaI2c.c
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

  I2C Bus implementation upon CirrusLogic.

  Copyright (c) 2008 - 2009, Intel Corporation
  All rights reserved. This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#include "VBoxVga.h"
#include "VBoxVgaI2c.h"

#define SEQ_ADDRESS_REGISTER    0x3c4
#define SEQ_DATA_REGISTER       0x3c5

#define I2C_CONTROL             0x08
#define I2CDAT_IN               7
#define I2CCLK_IN               2
#define I2CDAT_OUT              1
#define I2CCLK_OUT              0

#define I2C_BUS_SPEED           100  //100kbps

/**
  PCI I/O byte write function.

  @param  PciIo        The pointer to PCI_IO_PROTOCOL.
  @param  Address      The bit map of I2C Data or I2C Clock pins.
  @param  Data         The date to write.

**/
VOID
I2cOutb (
  EFI_PCI_IO_PROTOCOL    *PciIo,
  UINTN                  Address,
  UINT8                  Data
  )
{
  PciIo->Io.Write (
             PciIo,
             EfiPciIoWidthUint8,
             EFI_PCI_IO_PASS_THROUGH_BAR,
             Address,
             1,
             &Data
             );
}
/**
  PCI I/O byte read function.

  @param  PciIo        The pointer to PCI_IO_PROTOCOL.
  @param  Address      The bit map of I2C Data or I2C Clock pins.

  return byte value read from PCI I/O space.

**/
UINT8
I2cInb (
  EFI_PCI_IO_PROTOCOL    *PciIo,
  UINTN                  Address
  )
{
  UINT8 Data;

  PciIo->Io.Read (
             PciIo,
             EfiPciIoWidthUint8,
             EFI_PCI_IO_PASS_THROUGH_BAR,
             Address,
             1,
             &Data
             );
  return Data;
}

/**
  Read status of I2C Data and I2C Clock Pins.

  @param  PciIo        The pointer to PCI_IO_PROTOCOL.
  @param  Blt          The bit map of I2C Data or I2C Clock pins.

  @retval 0            Low on I2C Data or I2C Clock Pin.
  @retval 1            High on I2C Data or I2C Clock Pin.

**/
UINT8
I2cPinRead (
  EFI_PCI_IO_PROTOCOL    *PciIo,
  UINT8                  Bit
  )
{
  I2cOutb (PciIo, SEQ_ADDRESS_REGISTER, I2C_CONTROL);
  return (UINT8) ((I2cInb (PciIo, SEQ_DATA_REGISTER) >> Bit ) & 0xfe);
}


/**
  Set/Clear I2C Data and I2C Clock Pins.

  @param  PciIo              The pointer to PCI_IO_PROTOCOL.
  @param  Blt                The bit map to controller I2C Data or I2C Clock pins.
  @param  Value              1 or 0 stands for Set or Clear I2C Data and I2C Clock Pins.

**/
VOID
I2cPinWrite (
  EFI_PCI_IO_PROTOCOL    *PciIo,
  UINT8                  Bit,
  UINT8                  Value
  )
{
  UINT8        Byte;
  I2cOutb (PciIo, SEQ_ADDRESS_REGISTER, I2C_CONTROL);
  Byte = (UINT8) (I2cInb (PciIo, SEQ_DATA_REGISTER) & (UINT8) ~(1 << Bit)) ;
  Byte = (UINT8) (Byte | ((Value & 0x01) << Bit));
  I2cOutb (PciIo, SEQ_DATA_REGISTER, (UINT8) (Byte | 0x40));
  return;
}

/**
  Read/write delay according to I2C Bus Speed.

**/
VOID
I2cDelay (
  VOID
  )
{
  MicroSecondDelay (1000 / I2C_BUS_SPEED);
}

/**
  Write a 8-bit data onto I2C Data Pin.

  @param  PciIo              The pointer to PCI_IO_PROTOCOL.
  @param  Data               The byte data to write.

**/
VOID
I2cSendByte (
  EFI_PCI_IO_PROTOCOL    *PciIo,
  UINT8                  Data
  )
{
  UINTN                  Index;
  //
  // Send byte data onto I2C Bus
  //
  for (Index = 0; Index < 8; Index --) {
    I2cPinWrite (PciIo, I2CDAT_OUT, (UINT8) (Data >> (7 - Index)));
    I2cPinWrite (PciIo, I2CCLK_OUT, 1);
    I2cDelay ();
    I2cPinWrite (PciIo, I2CCLK_OUT, 0);
  }
}

/**
  Read a 8-bit data from I2C Data Pin.

  @param  PciIo              The pointer to PCI_IO_PROTOCOL.

  Return the byte data read from I2C Data Pin.
**/
UINT8
I2cReceiveByte (
  EFI_PCI_IO_PROTOCOL    *PciIo
  )
{
  UINT8          Data;
  UINTN          Index;

  Data = 0;
  //
  // Read byte data from I2C Bus
  //
  for (Index = 0; Index < 8; Index --) {
    I2cPinWrite (PciIo, I2CCLK_OUT, 1);
    I2cDelay ();
    Data = (UINT8) (Data << 1);
    Data = (UINT8) (Data | I2cPinRead (PciIo, I2CDAT_IN));
    I2cPinWrite (PciIo, I2CCLK_OUT, 0);
  }

  return Data;
}

/**
  Receive an ACK signal from I2C Bus.

  @param  PciIo              The pointer to PCI_IO_PROTOCOL.

**/
BOOLEAN
I2cWaitAck (
  EFI_PCI_IO_PROTOCOL    *PciIo
  )
{
  //
  // Wait for ACK signal
  //
  I2cPinWrite (PciIo, I2CDAT_OUT, 1);
  I2cPinWrite (PciIo, I2CCLK_OUT, 1);
  I2cDelay ();
  if (I2cPinRead (PciIo, I2CDAT_IN) == 0) {
    I2cPinWrite (PciIo, I2CDAT_OUT, 1);
    return TRUE;
  } else {
    return FALSE;
  }
}

/**
  Send an ACK signal onto I2C Bus.

  @param  PciIo              The pointer to PCI_IO_PROTOCOL.

**/
VOID
I2cSendAck (
  EFI_PCI_IO_PROTOCOL    *PciIo
  )
{
  I2cPinWrite (PciIo, I2CCLK_OUT, 1);
  I2cPinWrite (PciIo, I2CDAT_OUT, 1);
  I2cPinWrite (PciIo, I2CDAT_OUT, 0);
  I2cPinWrite (PciIo, I2CCLK_OUT, 0);
}

/**
  Start a I2C transfer on I2C Bus.

  @param  PciIo              The pointer to PCI_IO_PROTOCOL.

**/
VOID
I2cStart (
  EFI_PCI_IO_PROTOCOL    *PciIo
  )
{
  //
  // Init CLK and DAT pins
  //
  I2cPinWrite (PciIo, I2CCLK_OUT, 1);
  I2cPinWrite (PciIo, I2CDAT_OUT, 1);
  //
  // Start a I2C transfer, set SDA low from high, when SCL is high
  //
  I2cPinWrite (PciIo, I2CDAT_OUT, 0);
  I2cPinWrite (PciIo, I2CCLK_OUT, 0);
}

/**
  Stop a I2C transfer on I2C Bus.

  @param  PciIo              The pointer to PCI_IO_PROTOCOL.

**/
VOID
I2cStop (
  EFI_PCI_IO_PROTOCOL    *PciIo
  )
{
  //
  // Stop a I2C transfer, set SDA high from low, when SCL is high
  //
  I2cPinWrite (PciIo, I2CDAT_OUT, 0);
  I2cPinWrite (PciIo, I2CCLK_OUT, 1);
  I2cPinWrite (PciIo, I2CDAT_OUT, 1);
}

/**
  Read one byte data on I2C Bus.

  Read one byte data from the slave device connected to I2C Bus.
  If Data is NULL, then ASSERT().

  @param  PciIo              The pointer to PCI_IO_PROTOCOL.
  @param  DeviceAddress      Slave device's address.
  @param  RegisterAddress    The register address on slave device.
  @param  Data               The pointer to returned data if EFI_SUCCESS returned.

  @retval EFI_DEVICE_ERROR
  @retval EFI_SUCCESS

**/
EFI_STATUS
EFIAPI
I2cReadByte (
  EFI_PCI_IO_PROTOCOL    *PciIo,
  UINT8                  DeviceAddress,
  UINT8                  RegisterAddress,
  UINT8                  *Data
  )
{
  ASSERT (Data != NULL);

  //
  // Start I2C transfer
  //
  I2cStart (PciIo);

  //
  // Send slave address with enabling write flag
  //
  I2cSendByte (PciIo, (UINT8) (DeviceAddress & 0xfe));

  //
  // Wait for ACK signal
  //
  if (I2cWaitAck (PciIo) == FALSE) {
    return EFI_DEVICE_ERROR;
  }

  //
  // Send register address
  //
  I2cSendByte (PciIo, RegisterAddress);

  //
  // Wait for ACK signal
  //
  if (I2cWaitAck (PciIo) == FALSE) {
    return EFI_DEVICE_ERROR;
  }

  //
  // Send slave address with enabling read flag
  //
  I2cSendByte (PciIo, (UINT8) (DeviceAddress | 0x01));

  //
  // Wait for ACK signal
  //
  if (I2cWaitAck (PciIo) == FALSE) {
    return EFI_DEVICE_ERROR;
  }

  //
  // Read byte data from I2C Bus
  //
  *Data = I2cReceiveByte (PciIo);

  //
  // Send ACK signal onto I2C Bus
  //
  I2cSendAck (PciIo);

  //
  // Stop a I2C transfer
  //
  I2cStop (PciIo);

  return EFI_SUCCESS;
}

/**
  Write one byte data onto I2C Bus.

  Write one byte data to the slave device connected to I2C Bus.
  If Data is NULL, then ASSERT().

  @param  PciIo              The pointer to PCI_IO_PROTOCOL.
  @param  DeviceAddress      Slave device's address.
  @param  RegisterAddress    The register address on slave device.
  @param  Data               The pointer to write data.

  @retval EFI_DEVICE_ERROR
  @retval EFI_SUCCESS

**/
EFI_STATUS
EFIAPI
I2cWriteByte (
  EFI_PCI_IO_PROTOCOL    *PciIo,
  UINT8                  DeviceAddress,
  UINT8                  RegisterAddress,
  UINT8                  *Data
  )
{
  ASSERT (Data != NULL);

  I2cStart (PciIo);
  //
  // Send slave address with enabling write flag
  //
  I2cSendByte (PciIo, (UINT8) (DeviceAddress & 0xfe));

  //
  // Wait for ACK signal
  //
  if (I2cWaitAck (PciIo) == FALSE) {
    return EFI_DEVICE_ERROR;
  }

  //
  // Send register address
  //
  I2cSendByte (PciIo, RegisterAddress);

  //
  // Wait for ACK signal
  //
  if (I2cWaitAck (PciIo) == FALSE) {
    return EFI_DEVICE_ERROR;
  }

  //
  // Send byte data onto I2C Bus
  //
  I2cSendByte (PciIo, *Data);

  //
  // Wait for ACK signal
  //
  if (I2cWaitAck (PciIo) == FALSE) {
    return EFI_DEVICE_ERROR;
  }

  //
  // Stop a I2C transfer
  //
  I2cStop (PciIo);

  return EFI_SUCCESS;
}

