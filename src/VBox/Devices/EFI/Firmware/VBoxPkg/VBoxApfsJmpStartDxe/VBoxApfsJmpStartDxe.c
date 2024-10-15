/* $Id: VBoxApfsJmpStartDxe.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxApfsJmpStartDxe.c - VirtualBox APFS jumpstart driver.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <Protocol/ComponentName.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DiskIo.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define IN_RING0
#include <iprt/cdefs.h>
#include <iprt/formats/apfs.h>

/**
 * Contains the full jump start context being worked on.
 */
typedef struct
{
    /** Block I/O protocol. */
    EFI_BLOCK_IO *pBlockIo;
    /** Disk I/O protocol. */
    EFI_DISK_IO  *pDiskIo;
    /** Block size. */
    uint32_t     cbBlock;
    /** Controller handle. */
    EFI_HANDLE   hController;
    /** APFS UUID. */
    APFSUUID     Uuid;
} APFSJMPSTARTCTX;
typedef APFSJMPSTARTCTX *PAPFSJMPSTARTCTX;
typedef const APFSJMPSTARTCTX *PCAPFSJMPSTARTCTX;

static EFI_GUID g_ApfsDrvLoadedFromThisControllerGuid = { 0x01aaf8bc, 0x9c37, 0x4dc1,
                                                          { 0xb1, 0x68, 0xe9, 0x67, 0xd4, 0x2c, 0x79, 0x25 } };

typedef struct APFS_DRV_LOADED_INFO
{
    EFI_HANDLE hController;
    EFI_GUID   GuidContainer;
} APFS_DRV_LOADED_INFO;

/** Driver name translation table. */
static CONST EFI_UNICODE_STRING_TABLE       g_aVBoxApfsJmpStartDriverLangAndNames[] =
{
    {   "eng;en",   L"VBox APFS Jumpstart Wrapper Driver" },
    {   NULL,       NULL }
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Reads data from the given offset into the buffer.
 *
 * @returns EFI status code.
 * @param   pCtx            The jump start context.
 * @param   offRead         Where to start reading from.
 * @param   pvBuf           Where to read into.
 * @param   cbRead          Number of bytes to read.
 */
static EFI_STATUS vboxApfsJmpStartRead(IN PAPFSJMPSTARTCTX pCtx, IN APFSPADDR offRead, IN void *pvBuf, IN size_t cbRead)
{
    return pCtx->pDiskIo->ReadDisk(pCtx->pDiskIo, pCtx->pBlockIo->Media->MediaId, offRead * pCtx->cbBlock, cbRead, pvBuf);
}

/**
 * Calculates the fletcher64 checksum of the given APFS block and returns TRUE if it matches the one given in the object header.
 *
 * @returns Flag indicating whether the checksum matched.
 * @param   pObjHdr         The object header containing the checksum to check against.
 * @param   pvStruct        Pointer to the struct to create the checksum of.
 * @param   cbStruct        Size of the struct in bytes.
 */
static BOOLEAN vboxApfsObjPhysIsChksumValid(PCAPFSOBJPHYS pObjHdr, void *pvStruct, size_t cbStruct)
{
    if (cbStruct % sizeof(uint32_t) == 0)
    {
        uint32_t *pu32Data = (uint32_t *)pvStruct + 2; /* Start after the checksum field at the beginning. */
        size_t cWordsLeft = (cbStruct >> 2) - 2;

        uint64_t u64C0 = 0;
        uint64_t u64C1 = 0;
        uint64_t u64ChksumFletcher64 = 0;
        uint64_t u64Check0 = 0;
        uint64_t u64Check1 = 0;

        while (cWordsLeft)
        {
            u64C0 += (uint64_t)*pu32Data++;
            u64C0 %= UINT32_C(0xffffffff);

            u64C1 += u64C0;
            u64C1 %= UINT32_C(0xffffffff);

            cWordsLeft--;
        }

        u64Check0 = UINT32_C(0xffffffff) - (u64C0 + u64C1) % UINT32_C(0xffffffff);
        u64Check1 = UINT32_C(0xffffffff) - (u64C0 + u64Check0) % UINT32_C(0xffffffff);

        u64ChksumFletcher64 = (uint64_t)u64Check1 << 32 | u64Check0;
        if (!CompareMem(&u64ChksumFletcher64, &pObjHdr->abChkSum[0], sizeof(pObjHdr->abChkSum)))
            return TRUE;
        else
            DEBUG((DEBUG_INFO, "vboxApfsObjPhysIsChksumValid: Checksum mismatch, expected 0x%llx got 0x%llx", u64ChksumFletcher64, *(uint64_t *)&pObjHdr->abChkSum[0]));
    }
    else
        DEBUG((DEBUG_INFO, "vboxApfsObjPhysIsChksumValid: Structure not a multiple of 32bit\n"));

    return FALSE;
}

/**
 * Loads and starts the EFI driver contained in the given jump start structure.
 *
 * @returns EFI status code.
 * @param   pCtx            APFS jump start driver context structure.
 * @param   pJmpStart       APFS jump start structure describing the EFI file to load and start.
 */
static EFI_STATUS vboxApfsJmpStartLoadAndExecEfiDriver(IN PAPFSJMPSTARTCTX pCtx, IN PCAPFSEFIJMPSTART pJmpStart)
{
    PCAPFSPRANGE paExtents = (PCAPFSPRANGE)(pJmpStart + 1);
    UINTN cbReadLeft = RT_LE2H_U32(pJmpStart->cbEfiFile);
    EFI_STATUS rc = EFI_SUCCESS;

    void *pvApfsDrv = AllocateZeroPool(cbReadLeft);
    if (pvApfsDrv)
    {
        uint32_t i = 0;
        uint8_t *pbBuf = (uint8_t *)pvApfsDrv;

        for (i = 0; i < RT_LE2H_U32(pJmpStart->cExtents) && !EFI_ERROR(rc) && cbReadLeft; i++)
        {
            UINTN cbRead = RT_MIN(cbReadLeft, (UINTN)RT_LE2H_U64(paExtents[i].cBlocks) * pCtx->cbBlock);

            rc = vboxApfsJmpStartRead(pCtx, RT_LE2H_U64(paExtents[i].PAddrStart), pbBuf, cbRead);
            pbBuf      += cbRead;
            cbReadLeft -= cbRead;
        }

        if (!EFI_ERROR(rc))
        {
            /* Retrieve the parent device path. */
            EFI_DEVICE_PATH_PROTOCOL *ParentDevicePath;

            rc = gBS->HandleProtocol(pCtx->hController, &gEfiDevicePathProtocolGuid, (VOID **)&ParentDevicePath);
            if (!EFI_ERROR(rc))
            {
                /* Load image and execute it. */
                EFI_HANDLE hImage;

                rc = gBS->LoadImage(FALSE, gImageHandle, ParentDevicePath,
                                    pvApfsDrv, RT_LE2H_U32(pJmpStart->cbEfiFile),
                                    &hImage);
                if (!EFI_ERROR(rc))
                {
                    /* Try to start the image. */
                    rc = gBS->StartImage(hImage, NULL, NULL);
                    if (!EFI_ERROR(rc))
                    {
                        APFS_DRV_LOADED_INFO *pApfsDrvLoadedInfo = (APFS_DRV_LOADED_INFO *)AllocatePool (sizeof(APFS_DRV_LOADED_INFO));
                        if (pApfsDrvLoadedInfo)
                        {
                            pApfsDrvLoadedInfo->hController = pCtx->hController;
                            CopyMem(&pApfsDrvLoadedInfo->GuidContainer, &pCtx->Uuid, sizeof(pApfsDrvLoadedInfo->GuidContainer));

                            rc = gBS->InstallMultipleProtocolInterfaces(&pCtx->hController, &g_ApfsDrvLoadedFromThisControllerGuid, pApfsDrvLoadedInfo, NULL);
                            if (!EFI_ERROR(rc))
                            {
                                /* Connect the driver with the controller it came from. */
                                EFI_HANDLE ahImage[2];

                                ahImage[0] = hImage;
                                ahImage[1] = NULL;

                                gBS->ConnectController(pCtx->hController, &ahImage[0], NULL, TRUE);
                                return EFI_SUCCESS;
                            }
                            else
                            {
                                FreePool(pApfsDrvLoadedInfo);
                                DEBUG((DEBUG_INFO, "VBoxApfsJmpStart: Failed to install APFS driver loaded info protocol with %r\n", rc));
                            }
                        }
                        else
                        {
                            DEBUG((DEBUG_INFO, "VBoxApfsJmpStart: Failed to allocate %u bytes for the driver loaded structure\n", sizeof(APFS_DRV_LOADED_INFO)));
                            rc = EFI_OUT_OF_RESOURCES;
                        }
                    }
                    else
                        DEBUG((DEBUG_INFO, "VBoxApfsJmpStart: Starting APFS driver failed with %r\n", rc));

                    gBS->UnloadImage(hImage);
                }
                else
                    DEBUG((DEBUG_INFO, "VBoxApfsJmpStart: Loading read image failed with %r\n", rc));
            }
            else
                DEBUG((DEBUG_INFO, "VBoxApfsJmpStart: Querying device path protocol failed with %r\n", rc));
        }
        else
            DEBUG((DEBUG_INFO, "VBoxApfsJmpStart: Reading the jump start extents failed with %r\n", rc));

        FreePool(pvApfsDrv);
    }
    else
    {
        DEBUG((DEBUG_INFO, "VBoxApfsJmpStart: Failed to allocate %u bytes for the APFS driver image\n", cbReadLeft));
        rc = EFI_OUT_OF_RESOURCES;
    }

    return rc;
}

/**
 * @copydoc EFI_DRIVER_BINDING_SUPPORTED
 */
static EFI_STATUS EFIAPI
VBoxApfsJmpStart_Supported(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE ControllerHandle,
                           IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL)
{
    /* Check whether the controller supports the block I/O protocol. */
    EFI_STATUS rc = gBS->OpenProtocol(ControllerHandle,
                                      &gEfiBlockIoProtocolGuid,
                                      NULL,
                                      This->DriverBindingHandle,
                                      ControllerHandle,
                                      EFI_OPEN_PROTOCOL_TEST_PROTOCOL);
    if (EFI_ERROR(rc))
        return rc;

    rc = gBS->OpenProtocol(ControllerHandle,
                           &gEfiDiskIoProtocolGuid,
                           NULL,
                           This->DriverBindingHandle,
                           ControllerHandle,
                           EFI_OPEN_PROTOCOL_TEST_PROTOCOL);
    if (EFI_ERROR(rc))
        return rc;

    return EFI_SUCCESS;
}


/**
 * @copydoc EFI_DRIVER_BINDING_START
 */
static EFI_STATUS EFIAPI
VBoxApfsJmpStart_Start(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE ControllerHandle,
                       IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath OPTIONAL)
{
    APFSJMPSTARTCTX Ctx;

    /* Check whether the driver was already loaded from this controller. */
    EFI_STATUS rc = gBS->OpenProtocol(ControllerHandle,
                                      &g_ApfsDrvLoadedFromThisControllerGuid,
                                      NULL,
                                      This->DriverBindingHandle,
                                      ControllerHandle,
                                      EFI_OPEN_PROTOCOL_TEST_PROTOCOL);
    if (!EFI_ERROR(rc))
        return EFI_UNSUPPORTED;

    Ctx.cbBlock = 0; /* Will get filled when the superblock was read (starting at 0 anyway). */
    Ctx.hController = ControllerHandle;

    rc = gBS->OpenProtocol(ControllerHandle,
                           &gEfiBlockIoProtocolGuid,
                           (void **)&Ctx.pBlockIo,
                           This->DriverBindingHandle,
                           ControllerHandle,
                           EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (!EFI_ERROR(rc))
    {
        rc = gBS->OpenProtocol(ControllerHandle,
                               &gEfiDiskIoProtocolGuid,
                               (void **)&Ctx.pDiskIo,
                               This->DriverBindingHandle,
                               ControllerHandle,
                               EFI_OPEN_PROTOCOL_GET_PROTOCOL);
        if (!EFI_ERROR(rc))
        {
            /* Read the NX superblock structure from the first block and verify it. */
            APFSNXSUPERBLOCK Sb;

            rc = vboxApfsJmpStartRead(&Ctx, 0, &Sb, sizeof(Sb));
            if (   !EFI_ERROR(rc)
                && RT_LE2H_U32(Sb.u32Magic) == APFS_NX_SUPERBLOCK_MAGIC)
            {
                uint8_t *pbBlock = (uint8_t *)AllocateZeroPool(RT_LE2H_U32(Sb.cbBlock));

                if (pbBlock)
                {
                    PCAPFSNXSUPERBLOCK pSb = (PCAPFSNXSUPERBLOCK)pbBlock;

                    /* Read in the complete block (checksums always cover the whole block and not just the structure...). */
                    Ctx.cbBlock = RT_LE2H_U32(Sb.cbBlock);

                    rc = vboxApfsJmpStartRead(&Ctx, 0, pbBlock, Ctx.cbBlock);
                    if (   !EFI_ERROR(rc)
                        && RT_LE2H_U64(Sb.PAddrEfiJmpStart) > 0
                        && vboxApfsObjPhysIsChksumValid(&pSb->ObjHdr, pbBlock, Ctx.cbBlock))
                    {
                        PCAPFSEFIJMPSTART pJmpStart = (PCAPFSEFIJMPSTART)pbBlock;

                        DEBUG((DEBUG_INFO, "VBoxApfsJmpStart: Found APFS superblock, reading jumpstart structure from %llx\n", RT_LE2H_U64(Sb.PAddrEfiJmpStart)));

                        CopyMem(&Ctx.Uuid, &pSb->Uuid, sizeof(Ctx.Uuid));

                        rc = vboxApfsJmpStartRead(&Ctx, RT_LE2H_U64(Sb.PAddrEfiJmpStart), pbBlock, Ctx.cbBlock);
                        if (   !EFI_ERROR(rc)
                            && RT_H2LE_U32(pJmpStart->u32Magic) == APFS_EFIJMPSTART_MAGIC
                            && RT_H2LE_U32(pJmpStart->u32Version) == APFS_EFIJMPSTART_VERSION
                            && vboxApfsObjPhysIsChksumValid(&pJmpStart->ObjHdr, pbBlock, Ctx.cbBlock)
                            && RT_H2LE_U32(pJmpStart->cExtents) <= (Ctx.cbBlock - sizeof(*pJmpStart)) / sizeof(APFSPRANGE))
                            rc = vboxApfsJmpStartLoadAndExecEfiDriver(&Ctx, pJmpStart);
                        else
                        {
                            rc = EFI_UNSUPPORTED;
                            DEBUG((DEBUG_INFO, "VBoxApfsJmpStart: The APFS EFI jumpstart structure is invalid\n"));
                        }
                    }
                    else
                    {
                        DEBUG((DEBUG_INFO, "VBoxApfsJmpStart: Invalid APFS superblock -> no APFS filesystem (%r %x %llx)\n", rc, Sb.u32Magic, Sb.PAddrEfiJmpStart));
                        rc = EFI_UNSUPPORTED;
                    }

                    FreePool(pbBlock);
                }
                else
                    DEBUG((DEBUG_INFO, "VBoxApfsJmpStart: Failed to allocate memory for APFS block data (%u bytes)\n", RT_LE2H_U32(Sb.cbBlock)));
            }
            else
                DEBUG((DEBUG_INFO, "VBoxApfsJmpStart: Invalid APFS superblock -> no APFS filesystem (%r %x)\n", rc, Sb.u32Magic));

            gBS->CloseProtocol(ControllerHandle,
                               &gEfiDiskIoProtocolGuid,
                               This->DriverBindingHandle,
                               ControllerHandle);
        }
        else
            DEBUG((DEBUG_INFO, "VBoxApfsJmpStart: Opening the Disk I/O protocol failed with %r\n", rc));

        gBS->CloseProtocol(ControllerHandle,
                           &gEfiBlockIoProtocolGuid,
                           This->DriverBindingHandle,
                           ControllerHandle);
    }
    else
        DEBUG((DEBUG_INFO, "VBoxApfsJmpStart: Opening the Block I/O protocol failed with %r\n", rc));

    return  rc;
}


/**
 * @copydoc EFI_DRIVER_BINDING_STOP
 */
static EFI_STATUS EFIAPI
VBoxApfsJmpStart_Stop(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE ControllerHandle,
                      IN UINTN NumberOfChildren, IN EFI_HANDLE *ChildHandleBuffer OPTIONAL)
{
    /* EFI_STATUS                  rc; */

    return  EFI_UNSUPPORTED;
}


/** @copydoc EFI_COMPONENT_NAME_GET_DRIVER_NAME */
static EFI_STATUS EFIAPI
VBoxApfsJmpStartCN_GetDriverName(IN EFI_COMPONENT_NAME_PROTOCOL *This,
                                 IN CHAR8 *Language, OUT CHAR16 **DriverName)
{
    return LookupUnicodeString2(Language,
                                This->SupportedLanguages,
                                &g_aVBoxApfsJmpStartDriverLangAndNames[0],
                                DriverName,
                                TRUE);
}

/** @copydoc EFI_COMPONENT_NAME_GET_CONTROLLER_NAME */
static EFI_STATUS EFIAPI
VBoxApfsJmpStartCN_GetControllerName(IN EFI_COMPONENT_NAME_PROTOCOL *This,
                                     IN EFI_HANDLE ControllerHandle,
                                     IN EFI_HANDLE ChildHandle OPTIONAL,
                                     IN CHAR8 *Language, OUT CHAR16 **ControllerName)
{
    /** @todo try query the protocol from the controller and forward the query. */
    return EFI_UNSUPPORTED;
}

/** @copydoc EFI_COMPONENT_NAME2_GET_DRIVER_NAME */
static EFI_STATUS EFIAPI
VBoxApfsJmpStartCN2_GetDriverName(IN EFI_COMPONENT_NAME2_PROTOCOL *This,
                        IN CHAR8 *Language, OUT CHAR16 **DriverName)
{
    return LookupUnicodeString2(Language,
                                This->SupportedLanguages,
                                &g_aVBoxApfsJmpStartDriverLangAndNames[0],
                                DriverName,
                                FALSE);
}

/** @copydoc EFI_COMPONENT_NAME2_GET_CONTROLLER_NAME */
static EFI_STATUS EFIAPI
VBoxApfsJmpStartCN2_GetControllerName(IN EFI_COMPONENT_NAME2_PROTOCOL *This,
                                      IN EFI_HANDLE ControllerHandle,
                                      IN EFI_HANDLE ChildHandle OPTIONAL,
                                      IN CHAR8 *Language, OUT CHAR16 **ControllerName)
{
    /** @todo try query the protocol from the controller and forward the query. */
    return EFI_UNSUPPORTED;
}



/*********************************************************************************************************************************
*   Entry point and driver registration                                                                                          *
*********************************************************************************************************************************/

/** EFI Driver Binding Protocol. */
static EFI_DRIVER_BINDING_PROTOCOL          g_VBoxApfsJmpStartDB =
{
    VBoxApfsJmpStart_Supported,
    VBoxApfsJmpStart_Start,
    VBoxApfsJmpStart_Stop,
    /* .Version             = */    1,
    /* .ImageHandle         = */ NULL,
    /* .DriverBindingHandle = */ NULL
};

/** EFI Component Name Protocol. */
static const EFI_COMPONENT_NAME_PROTOCOL    g_VBoxApfsJmpStartCN =
{
    VBoxApfsJmpStartCN_GetDriverName,
    VBoxApfsJmpStartCN_GetControllerName,
    "eng"
};

/** EFI Component Name 2 Protocol. */
static const EFI_COMPONENT_NAME2_PROTOCOL   g_VBoxApfsJmpStartCN2 =
{
    VBoxApfsJmpStartCN2_GetDriverName,
    VBoxApfsJmpStartCN2_GetControllerName,
    "en"
};


/**
 * VBoxApfsJmpStart entry point.
 *
 * @returns EFI status code.
 *
 * @param   ImageHandle     The image handle.
 * @param   SystemTable     The system table pointer.
 */
EFI_STATUS EFIAPI
VBoxApfsjmpStartEntryDxe(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS  rc;
    DEBUG((DEBUG_INFO, "VBoxApfsjmpStartEntryDxe\n"));

    rc = EfiLibInstallDriverBindingComponentName2(ImageHandle, SystemTable,
                                                  &g_VBoxApfsJmpStartDB, ImageHandle,
                                                  &g_VBoxApfsJmpStartCN, &g_VBoxApfsJmpStartCN2);
    ASSERT_EFI_ERROR(rc);
    return rc;
}

EFI_STATUS EFIAPI
VBoxApfsjmpStartUnloadDxe(IN EFI_HANDLE         ImageHandle)
{
    return EFI_SUCCESS;
}

