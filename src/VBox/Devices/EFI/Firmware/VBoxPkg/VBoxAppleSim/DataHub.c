/* $Id: DataHub.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Console.c - VirtualBox Console control emulation
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
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiLib.h>

#include "VBoxPkg.h"
#include "DataHub.h"

/**
 * Data hub logged entry.
 */
typedef struct
{
    /** List node for the linked list - must be at the top. */
    LIST_ENTRY             NdEntries;
    /** The record header. */
    EFI_DATA_RECORD_HEADER RecHdr;
    /** The data logged, variable in size. */
    UINT8                  abData[1];
} EFI_DATA_HUB_ENTRY;

/**
 * DataHub instance data.
 */
typedef struct
{
    /** Monotonic increasing counter. */
    UINT64          cMonotonicCnt;
    /** Linked list holding the logged entries. */
    LIST_ENTRY      LstEntries;
    /** The lock protecting the key members above. */
    EFI_LOCK        Lck;
} EFI_DATA_HUB_INSTANCE;


EFI_DATA_HUB_INSTANCE mDataHubInstance;

EFI_STATUS EFIAPI
DataHubLogData (
  IN EFI_DATA_HUB_PROTOCOL             *This,
  IN EFI_GUID                          *DataRecordGuid,
  IN EFI_GUID                          *ProducerName,
  IN UINT64                            DataRecordClass,
  IN VOID                              *RawData,
  IN UINT32                            RawDataSize
  )
{
    UINT32 cbEntry = sizeof(EFI_DATA_HUB_ENTRY) + RawDataSize;
    EFI_DATA_HUB_ENTRY *pEntry = AllocatePool(cbEntry);

    if (pEntry == NULL)
        return EFI_OUT_OF_RESOURCES;

    pEntry->RecHdr.Version    = EFI_DATA_RECORD_HEADER_VERSION;
    pEntry->RecHdr.HeaderSize = sizeof(EFI_DATA_RECORD_HEADER);
    pEntry->RecHdr.RecordSize = RawDataSize + sizeof(EFI_DATA_RECORD_HEADER);
    CopyMem(&pEntry->RecHdr.DataRecordGuid, DataRecordGuid, sizeof(pEntry->RecHdr.DataRecordGuid));
    CopyMem(&pEntry->RecHdr.ProducerName, ProducerName, sizeof(pEntry->RecHdr.ProducerName));
    pEntry->RecHdr.DataRecordClass = DataRecordClass;
    SetMem(&pEntry->RecHdr.LogTime, sizeof(pEntry->RecHdr.LogTime), 0);
    pEntry->RecHdr.LogMonotonicCount = ++mDataHubInstance.cMonotonicCnt; /* Ensure non zero value in record. */
    CopyMem(&pEntry->abData[0], RawData, RawDataSize);

    EfiAcquireLock(&mDataHubInstance.Lck);
    InsertTailList(&mDataHubInstance.LstEntries, &pEntry->NdEntries);
    EfiReleaseLock(&mDataHubInstance.Lck);
    return EFI_SUCCESS;
}


EFI_STATUS EFIAPI
DataHubGetNextDataRecord (
  IN EFI_DATA_HUB_PROTOCOL             *This,
  IN OUT UINT64                        *MonotonicCount,
  IN EFI_EVENT                         *FilterDriver OPTIONAL,
  OUT EFI_DATA_RECORD_HEADER           **Record
  )
{
    EFI_DATA_HUB_ENTRY *pEntry = NULL;

    EfiAcquireLock(&mDataHubInstance.Lck);
    if (*MonotonicCount == 0)
    {
        if (!IsListEmpty(&mDataHubInstance.LstEntries))
            pEntry = (EFI_DATA_HUB_ENTRY *)GetFirstNode(&mDataHubInstance.LstEntries);
    }
    else
    {
        /* Ignore filter driver handling for now. */
        LIST_ENTRY *pHead = &mDataHubInstance.LstEntries;
        LIST_ENTRY *pIt = NULL;

        for (pIt = GetFirstNode(pHead); pIt != pHead; pIt = GetNextNode(pHead, pIt))
        {
            EFI_DATA_HUB_ENTRY *pTmp = (EFI_DATA_HUB_ENTRY *)pIt;
            if (pTmp->RecHdr.LogMonotonicCount == *MonotonicCount)
            {
                pEntry = pTmp;
                break;
            }
        }
    }
    EfiReleaseLock(&mDataHubInstance.Lck);

    if (pEntry == NULL)
        return EFI_NOT_FOUND;

    *Record = &pEntry->RecHdr;

    /* Look for the next entry and set MonotonicCount accordingly. */
    if (!IsNodeAtEnd(&mDataHubInstance.LstEntries, &pEntry->NdEntries))
    {
        pEntry = (EFI_DATA_HUB_ENTRY *)GetNextNode(&mDataHubInstance.LstEntries, &pEntry->NdEntries);
        *MonotonicCount = pEntry->RecHdr.LogMonotonicCount;
    }
    else
        *MonotonicCount = 0;

    return EFI_SUCCESS;
}


EFI_STATUS EFIAPI
DataHubRegisterDataFilterDriver (
  IN EFI_DATA_HUB_PROTOCOL             *This,
  IN EFI_EVENT                         FilterEvent,
  IN EFI_TPL                           FilterTpl,
  IN UINT64                            FilterClass,
  IN EFI_GUID                          *FilterDataRecordGui OPTIONAL
  )
{
    return EFI_SUCCESS;
}


EFI_STATUS EFIAPI
DataHubUnregisterDataFilterDriver (
  IN EFI_DATA_HUB_PROTOCOL             *This,
  IN EFI_EVENT                         FilterEvent
  )
{
    return EFI_SUCCESS;
}


EFI_DATA_HUB_PROTOCOL gDataHub =
{
    DataHubLogData,
    DataHubGetNextDataRecord,
    DataHubRegisterDataFilterDriver,
    DataHubUnregisterDataFilterDriver
};

EFI_GUID gEfiDataHubProtocolGuid = EFI_DATA_HUB_PROTOCOL_GUID;

EFI_STATUS
EFIAPI
InitializeDataHub (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
    EFI_STATUS              Status;


    InitializeListHead(&mDataHubInstance.LstEntries);
    EfiInitializeLock (&mDataHubInstance.Lck, TPL_NOTIFY);

    Status = gBS->InstallMultipleProtocolInterfaces (
        &ImageHandle,
        &gEfiDataHubProtocolGuid,
        &gDataHub,
        NULL);
    ASSERT_EFI_ERROR (Status);

    return Status;
}
