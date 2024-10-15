/* $Id: DataHub.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DataHub.h
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#ifndef __DATA_HUB_H__
#define __DATA_HUB_H__

#define EFI_DATA_HUB_PROTOCOL_GUID \
  { 0xae80d021, 0x618e, 0x11d4, {0xbc, 0xd7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81} }

typedef struct _EFI_DATA_HUB_PROTOCOL   EFI_DATA_HUB_PROTOCOL;


#define EFI_DATA_RECORD_HEADER_VERSION 0x0100

#define EFI_DATA_CLASS_DEBUG           0x1ULL
#define EFI_DATA_CLASS_ERROR           0x2ULL
#define EFI_DATA_CLASS_DATA            0x4ULL
#define EFI_DATA_CLASS_PROGRESS_CODE   0x8ULL

typedef struct {
    UINT16    Version;
    UINT16    HeaderSize;
    UINT32    RecordSize;
    EFI_GUID  DataRecordGuid;
    EFI_GUID  ProducerName;
    UINT64    DataRecordClass;
    EFI_TIME  LogTime;
    UINT64    LogMonotonicCount;
} EFI_DATA_RECORD_HEADER;

typedef
EFI_STATUS
(EFIAPI *EFI_DATA_HUB_LOG_DATA) (
  IN EFI_DATA_HUB_PROTOCOL             *This,
  IN EFI_GUID                          *DataRecordGuid,
  IN EFI_GUID                          *ProducerName,
  IN UINT64                            DataRecordClass,
  IN VOID                              *RawData,
  IN UINT32                            RawDataSize
  )
/*++

  Routine Description:
    Logs a data record to the system event log.

  Arguments:
    This         - Protocol instance pointer.


  Returns:
    EFI_SUCCESS     - Data was logged.

--*/
;


typedef
EFI_STATUS
(EFIAPI *EFI_DATA_HUB_GET_NEXT_DATA_RECORD) (
  IN EFI_DATA_HUB_PROTOCOL             *This,
  IN OUT UINT64                        *MonotonicCount,
  IN EFI_EVENT                         *FilterDriver OPTIONAL,
  OUT EFI_DATA_RECORD_HEADER           **Record
  )
/*++

  Routine Description:
    Allows the system data log to be searched.

  Arguments:
    This         - Protocol instance pointer.


  Returns:
    EFI_SUCCESS     - Data was logged.

--*/
;


typedef
EFI_STATUS
(EFIAPI *EFI_DATA_HUB_REGISTER_DATA_FILTER_DRIVER) (
  IN EFI_DATA_HUB_PROTOCOL             *This,
  IN EFI_EVENT                         FilterEvent,
  IN EFI_TPL                           FilterTpl,
  IN UINT64                            FilterClass,
  IN EFI_GUID                          *FilterDataRecordGui OPTIONAL
  )
/*++

  Routine Description:
    Registers an event to be signaled evey time a data record is logged in the system.

  Arguments:
    This         - Protocol instance pointer.


  Returns:
    EFI_SUCCESS     - The filter driver event was registered.

--*/
;


typedef
EFI_STATUS
(EFIAPI *EFI_DATA_HUB_UNREGISTER_DATA_FILTER_DRIVER) (
  IN EFI_DATA_HUB_PROTOCOL             *This,
  IN EFI_EVENT                         FilterEvent
  )
/*++

  Routine Description:
    Stops a filter driver from being notified when data records are logged.

  Arguments:
    This         - Protocol instance pointer.


  Returns:
    EFI_SUCCESS     - The filter driver represented by FilterEvent was shut off.

--*/
;


struct _EFI_DATA_HUB_PROTOCOL {
  EFI_DATA_HUB_LOG_DATA                        LogData;
  EFI_DATA_HUB_GET_NEXT_DATA_RECORD            GetNextDataRecord;
  EFI_DATA_HUB_REGISTER_DATA_FILTER_DRIVER     RegisterFilterDriver;
  EFI_DATA_HUB_UNREGISTER_DATA_FILTER_DRIVER   UnregisterFilterDriver;
};

extern EFI_GUID gEfiDataHubProtocolGuid;


EFI_STATUS
EFIAPI
InitializeDataHub (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  );

#endif
