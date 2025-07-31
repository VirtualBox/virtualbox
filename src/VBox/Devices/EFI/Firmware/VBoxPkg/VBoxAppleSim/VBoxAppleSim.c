/* $Id: VBoxAppleSim.c 110304 2025-07-18 11:07:06Z alexander.eichner@oracle.com $ */
/** @file
 * VBoxAppleSim.c - VirtualBox Apple Firmware simulation support
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/PrintLib.h>

#include <Protocol/DevicePathToText.h>

#include <IndustryStandard/Acpi10.h>
#include <IndustryStandard/Acpi20.h>
#include <IndustryStandard/SmBios.h>

#include <Guid/SmBios.h>
#include <Guid/Acpi.h>
#include <Guid/Mps.h>

#include "DataHub.h"
#include "VBoxPkg.h"
#include "DevEFI.h"
#include "iprt/asm.h"


/*
 * External functions
 */
EFI_STATUS EFIAPI
CpuUpdateDataHub(EFI_BOOT_SERVICES * bs,
                 UINT64              FSBFrequency,
                 UINT64              TSCFrequency,
                 UINT64              CPUFrequency);

EFI_STATUS EFIAPI
InitializeConsoleSim (IN EFI_HANDLE           ImageHandle,
                      IN EFI_SYSTEM_TABLE     *SystemTable);


/*
 * Internal Functions
 */
static UINT32
GetVmVariable(UINT32 Variable, CHAR8 *pbBuf, UINT32 cbBuf)
{
    UINT32 cbVar, offBuf;

    ASMOutU32(EFI_INFO_PORT, Variable);
    cbVar = ASMInU32(EFI_INFO_PORT);

    for (offBuf = 0; offBuf < cbVar && offBuf < cbBuf; offBuf++)
        pbBuf[offBuf] = ASMInU8(EFI_INFO_PORT);

    return cbVar;
}

/*
 * GUIDs
 */
/** The EFI variable GUID for the 'FirmwareFeatures' and friends.
 * Also known as AppleFirmwareVariableGuid in other sources. */
EFI_GUID gEfiAppleNvramGuid = {
    0x4D1EDE05, 0x38C7, 0x4A6A, {0x9C, 0xC6, 0x4B, 0xCC, 0xA8, 0xB3, 0x8C, 0x14 }
};

/** The EFI variable GUID for the 'boot-args' variable and others.
 * Also known as AppleNVRAMVariableGuid in other sources. */
EFI_GUID gEfiAppleBootGuid = {
    0x7C436110, 0xAB2A, 0x4BBB, {0xA8, 0x80, 0xFE, 0x41, 0x99, 0x5C, 0x9F, 0x82}
};


/*
 * Device Properoty protocol implementation hack.
 */

/** gEfiAppleVarGuid is aka AppleDevicePropertyProtocolGuid in other sources. */
EFI_GUID gEfiAppleVarGuid = {
    0x91BD12FE, 0xF6C3, 0x44FB, {0xA5, 0xB7, 0x51, 0x22, 0xAB, 0x30, 0x3A, 0xE0}
};

/** APPLE_GETVAR_PROTOCOL is aka APPLE_DEVICE_PROPERTY_PROTOCOL in other sources. */
typedef struct _APPLE_GETVAR_PROTOCOL APPLE_GETVAR_PROTOCOL;

struct _APPLE_GETVAR_PROTOCOL
{
    /** Magic value or some version thingy. boot.efi doesn't check this, I think. */
    UINT64  u64Magic;

    EFI_STATUS (EFIAPI *pfnUnknown0)(IN APPLE_GETVAR_PROTOCOL *This, IN VOID *pvArg1, IN VOID *pvArg2,
                                     IN VOID *pvArg3, IN VOID *pvArg4);
    EFI_STATUS (EFIAPI *pfnUnknown1)(IN APPLE_GETVAR_PROTOCOL *This, IN VOID *pvArg1, IN VOID *pvArg2,
                                     IN VOID *pvArg3, IN VOID *pvArg4);
    EFI_STATUS (EFIAPI *pfnUnknown2)(IN APPLE_GETVAR_PROTOCOL *This, IN VOID *pvArg1, IN VOID *pvArg2);

    EFI_STATUS (EFIAPI *pfnGetDevProps)(IN APPLE_GETVAR_PROTOCOL *This, IN CHAR8 *pbBuf, IN OUT UINT32 *pcbBuf);
};
/** The value of APPLE_GETVAR_PROTOCOL::u64Magic. */
#define APPLE_GETVAR_PROTOCOL_MAGIC     0x10000

EFI_STATUS EFIAPI
AppleGetVar_Unknown0(IN APPLE_GETVAR_PROTOCOL *This, IN VOID *pvArg1, IN VOID *pvArg2,
                     IN VOID *pvArg3, IN VOID *pvArg4)
{
    CHAR8 szMsg[128];
    AsciiSPrint(szMsg, sizeof(szMsg), "AppleGetVar_Unknown0: pvArg1=%p pvArg2=%p pvArg3=%p pvArg4=%p",
                pvArg1, pvArg2, pvArg3, pvArg4);
    DebugAssert(__FILE__, __LINE__, szMsg);
    return EFI_UNSUPPORTED;
}

EFI_STATUS EFIAPI
AppleGetVar_Unknown1(IN APPLE_GETVAR_PROTOCOL *This, IN VOID *pvArg1, IN VOID *pvArg2,
                     IN VOID *pvArg3, IN VOID *pvArg4)
{
    CHAR8 szMsg[128];
    AsciiSPrint(szMsg, sizeof(szMsg), "AppleGetVar_Unknown1: pvArg1=%p pvArg2=%p pvArg3=%p pvArg4=%p",
                pvArg1, pvArg2, pvArg3, pvArg4);
    DebugAssert(__FILE__, __LINE__, szMsg);
    return EFI_UNSUPPORTED;
}

EFI_STATUS EFIAPI
AppleGetVar_Unknown2(IN APPLE_GETVAR_PROTOCOL *This, IN VOID *pvArg1, IN VOID *pvArg2)
{
    CHAR8 szMsg[80];
    AsciiSPrint(szMsg, sizeof(szMsg), "AppleGetVar_Unknown2: pvArg1=%p pvArg2=%p", pvArg1, pvArg2);
    DebugAssert(__FILE__, __LINE__, szMsg);
    return EFI_UNSUPPORTED;
}


/**
 * This method obtains the 'device-properties' that get exposed by
 * AppleEFIFirmware and parsed by AppleACPIPlatform.
 *
 * Check out the data in the IORegisteryExplorer, the device-properties property
 * under IODeviceTree:/efi.
 *
 * @retval  EFI_SUCCESS, check *pcbBuf or the number of bytes actually returned.
 * @retval  EFI_BUFFER_TOO_SMALL, check *pcbBuf for the necessary buffer size.
 * @param   pThis   Not used.
 * @param   pbBuf   The output buffer.
 * @param   pcbBuf  On input, the varible pointed to contains the size of the
 *                  buffer.  The size is generally 4KB from what we've observed.
 *                  On output, it contains the amount of data available, this
 *                  is always set.
 */
EFI_STATUS EFIAPI
AppleGetVar_GetDeviceProps(IN APPLE_GETVAR_PROTOCOL *pThis, OUT CHAR8 *pbBuf, IN OUT UINT32 *pcbBuf)
{
    UINT32 cbBuf = *pcbBuf;
    UINT32 cbActual;

    cbActual = GetVmVariable(EFI_INFO_INDEX_DEVICE_PROPS, pbBuf, cbBuf);
    *pcbBuf = cbActual;

    if (cbActual > cbBuf)
        return EFI_BUFFER_TOO_SMALL;

    return EFI_SUCCESS;
}

APPLE_GETVAR_PROTOCOL gPrivateVarHandler =
{
    /* Magic = */ APPLE_GETVAR_PROTOCOL_MAGIC,
    AppleGetVar_Unknown0,
    AppleGetVar_Unknown1,
    AppleGetVar_Unknown2,
    AppleGetVar_GetDeviceProps
};


/*
 * Unknown Protocol #1.
 */

/** This seems to be related to graphics/display... */
EFI_GUID gEfiUnknown1ProtocolGuid =
{
    0xDD8E06AC, 0x00E2, 0x49A9, {0x88, 0x8F, 0xFA, 0x46, 0xDE, 0xD4, 0x0A, 0x52}
};

EFI_STATUS EFIAPI
UnknownHandlerImpl()
{
#ifdef DEBUG
    ASSERT(0);
#endif
    Print(L"Unknown called\n");
    return EFI_SUCCESS;
}

/* array of pointers to function */
EFI_STATUS (EFIAPI *gUnknownProtoHandler[])() =
{
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl,
    UnknownHandlerImpl
};

EFI_STATUS EFIAPI
SetProperVariables(IN EFI_HANDLE ImageHandle, EFI_RUNTIME_SERVICES * rs)
{
    EFI_STATUS          rc;
    UINT32              vBackgroundClear = 0x00000000;
    UINT32              vFwFeatures      = 0x80000015;
    UINT32              vFwFeaturesMask  = 0x800003ff;

    // -legacy acpi=0xffffffff acpi_debug=0xfffffff panic_io_port=0xef11 io=0xfffffffe trace=4096  io=0xffffffef -v serial=2 serialbaud=9600
    // 0x10 makes kdb default, thus 0x15e for kdb, 0x14e for gdb
    // usb=0x800 is required to work around default behavior of the Apple xHCI driver which rejects high-speed
    // USB devices and tries to force them to EHCI when running on the Intel Panther Point chipset.

    //static const CHAR8  vBootArgs[]      = "debug=0x15e keepsyms=1 acpi=0xffffffff acpi_debug=0xff acpi_level=7 -v -x32 -s"; // or just "debug=0x8 -legacy"
    // 0x14e for serial output
    //static const CHAR8  vDefBootArgs[]      = "debug=0x146 usb=0x800 keepsyms=1 -v serial=0x1";
    static const CHAR8  vDefBootArgs[]      = "usb=0x800 keepsyms=1";
    CHAR8  vBootArgs[256];
    UINT32 BootArgsLen;

    BootArgsLen = GetVmVariable(EFI_INFO_INDEX_BOOT_ARGS, vBootArgs, sizeof vBootArgs);
    if (BootArgsLen <= 1)
    {
        BootArgsLen = sizeof vDefBootArgs;
        CopyMem(vBootArgs, vDefBootArgs, BootArgsLen);
    }
    rc = rs->SetVariable(L"BackgroundClear",
                         &gEfiAppleNvramGuid,
                         /* EFI_VARIABLE_NON_VOLATILE | */ EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                         sizeof(vBackgroundClear), &vBackgroundClear);
    ASSERT_EFI_ERROR (rc);

    rc = rs->SetVariable(L"FirmwareFeatures",
                         &gEfiAppleNvramGuid,
                         EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                         sizeof(vFwFeatures), &vFwFeatures);
    ASSERT_EFI_ERROR (rc);

    rc = rs->SetVariable(L"FirmwareFeaturesMask",
                         &gEfiAppleNvramGuid,
                         EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                         sizeof(vFwFeaturesMask), &vFwFeaturesMask);
    ASSERT_EFI_ERROR (rc);

    rc = rs->SetVariable(L"boot-args",
                         &gEfiAppleBootGuid,
                         EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                         BootArgsLen, &vBootArgs);
    ASSERT_EFI_ERROR (rc);

     return EFI_SUCCESS;
}

/**
 * VBoxInitAppleSim entry point.
 *
 * @returns EFI status code.
 *
 * @param   ImageHandle     The image handle.
 * @param   SystemTable     The system table pointer.
 */
EFI_STATUS EFIAPI
VBoxInitAppleSim(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS          rc;
    UINT64              FSBFrequency;
    UINT64              TSCFrequency;
    UINT64              CPUFrequency;

    rc = SetProperVariables(ImageHandle, SystemTable->RuntimeServices);
    ASSERT_EFI_ERROR(rc);

    rc = gBS->InstallMultipleProtocolInterfaces(&ImageHandle, &gEfiAppleVarGuid, &gPrivateVarHandler, NULL);
    ASSERT_EFI_ERROR(rc);

    rc = InitializeDataHub(ImageHandle, SystemTable);
    ASSERT_EFI_ERROR(rc);

    GetVmVariable(EFI_INFO_INDEX_FSB_FREQUENCY, (CHAR8 *)&FSBFrequency, sizeof(FSBFrequency));
    GetVmVariable(EFI_INFO_INDEX_TSC_FREQUENCY, (CHAR8 *)&TSCFrequency, sizeof(TSCFrequency));
    GetVmVariable(EFI_INFO_INDEX_CPU_FREQUENCY, (CHAR8 *)&CPUFrequency, sizeof(CPUFrequency));

    rc = CpuUpdateDataHub(gBS, FSBFrequency, TSCFrequency, CPUFrequency);
    ASSERT_EFI_ERROR(rc);

    rc = InitializeConsoleSim(ImageHandle, SystemTable);
    ASSERT_EFI_ERROR(rc);

    rc = gBS->InstallMultipleProtocolInterfaces(&ImageHandle, &gEfiUnknown1ProtocolGuid, gUnknownProtoHandler, NULL);
    ASSERT_EFI_ERROR(rc);

    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI
VBoxDeinitAppleSim(IN EFI_HANDLE         ImageHandle)
{
    return EFI_SUCCESS;
}
