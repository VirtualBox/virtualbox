/* $Id: Global.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM API - Global Declarations and Definitions.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef MAIN_INCLUDED_Global_h
#define MAIN_INCLUDED_Global_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* interface definitions */
#include "VBox/com/VirtualBox.h"

#include <VBox/ostypes.h>

#include <iprt/types.h>

/** @name Generic OS hint bits.
 * @{
 */
#define VBOXOSHINT_NONE                 0
#define VBOXOSHINT_64BIT                RT_BIT(0)
#define VBOXOSHINT_EFI                  RT_BIT(1)
#define VBOXOSHINT_USBHID               RT_BIT(2)
#define VBOXOSHINT_USBTABLET            RT_BIT(3)
#define VBOXOSHINT_RTCUTC               RT_BIT(4)
#define VBOXOSHINT_ACCEL2D              RT_BIT(5)
#define VBOXOSHINT_ACCEL3D              RT_BIT(6)
#define VBOXOSHINT_FLOPPY               RT_BIT(7)
#define VBOXOSHINT_NOUSB                RT_BIT(8)
#define VBOXOSHINT_TFRESET              RT_BIT(9)
#define VBOXOSHINT_USB3                 RT_BIT(10)
#define VBOXOSHINT_EFI_SECUREBOOT       RT_BIT(11)
#define VBOXOSHINT_TPM                  RT_BIT(12)
#define VBOXOSHINT_TPM2                 RT_BIT(13)
#define VBOXOSHINT_WDDM_GRAPHICS        RT_BIT(14)
/** @} */

/** @name x86-specific OS hint bits.
 * @{
 */
#define VBOXOSHINT_X86_HWVIRTEX         RT_BIT(15)
#define VBOXOSHINT_X86_IOAPIC           RT_BIT(16)
#define VBOXOSHINT_X86_HPET             RT_BIT(17)
#define VBOXOSHINT_X86_PAE              RT_BIT(18)
#define VBOXOSHINT_X86_X2APIC           RT_BIT(19)
/** @} */

/** The VBoxVRDP kludge extension pack name.
 *
 * This is not a valid extension pack name (dashes are not allowed), and
 * hence will not conflict with real extension packs.
 */
#define VBOXVRDP_KLUDGE_EXTPACK_NAME    "Built-in-VBoxVRDP"

/** The VBoxPuelCrypto kludge extension pack name.
 *
 * This is not a valid extension pack name (dashes are not allowed), and
 * hence will not conflict with real extension packs.
 */
#define VBOXPUELCRYPTO_KLUDGE_EXTPACK_NAME    "Built-in-VBoxPuelCrypto"

/**
 * Contains global static definitions that can be referenced by all COM classes
 * regardless of the apartment.
 */
class Global
{
public:

    /** Represents OS Type <-> string mappings. */
    struct OSType
    {
        const char                    *familyId;                        /* utf-8, e.g. Linux or MacOS  */
        const char                    *familyDescription;               /* utf-8, e.g. Linux or Mac OS X */
        const char                    *subtype;                         /* utf-8, the subtype of the family e.g. Debian or FreeBSD */
        const char                    *id;                              /* utf-8, VM config file value e.g. Debian12_64 */
        const char                    *description;                     /* utf-8, e.g. "Debian 12 Bookworm (64-bit)" */
        const char                    *guestAdditionsInstallPkgName;    /* utf-8, e.g. "VBoxLinuxAdditions.run" */
        const VBOXOSTYPE               osType;                          /* enum, e.g. VBOXOSTYPE_Debian12_x64 */
        const uint32_t                 osHint;
        const uint32_t                 recommendedCPUCount;
        const uint32_t                 recommendedRAM;
        const uint32_t                 recommendedVRAM;
        const uint64_t                 recommendedHDD;
        const GraphicsControllerType_T graphicsControllerType;
        const NetworkAdapterType_T     networkAdapterType;
        const uint32_t                 numSerialEnabled;
        const StorageControllerType_T  dvdStorageControllerType;
        const StorageBus_T             dvdStorageBusType;
        const StorageControllerType_T  hdStorageControllerType;
        const StorageBus_T             hdStorageBusType;
        const ChipsetType_T            chipsetType;
        const IommuType_T              iommuType;
        const AudioControllerType_T    audioControllerType;
        const AudioCodecType_T         audioCodecType;
    };

    static const OSType sOSTypes[];
    static size_t cOSTypes;

    /**
     * Maps VBOXOSTYPE to the OS type which is used in VM configs.
     */
    static const char *OSTypeId(VBOXOSTYPE aOSType);

    /**
     * Maps an OS type ID string to index into sOSTypes.
     * @returns index on success, UINT32_MAX if not found.
     */
    static uint32_t getOSTypeIndexFromId(const char *pszId);

    /**
     * Returns @c true if the given machine state is an online state. This is a
     * recommended way to detect if the VM is online (being executed in a
     * dedicated process) or not. Note that some online states are also
     * transitional states (see #IsTransient()).
     */
    static bool IsOnline(MachineState_T aState)
    {
        return aState >= MachineState_FirstOnline &&
               aState <= MachineState_LastOnline;
    }

    /**
     * Returns @c true if the given machine state is a transient state. This is
     * a recommended way to detect if the VM is performing some potentially
     * lengthy operation (such as starting, stopping, saving, deleting
     * snapshot, etc.). Note some (but not all) transitional states are also
     * online states (see #IsOnline()).
     */
    static bool IsTransient(MachineState_T aState)
    {
        return aState >= MachineState_FirstTransient &&
               aState <= MachineState_LastTransient;
    }

    /**
     * Shortcut to <tt>IsOnline(aState) || IsTransient(aState)</tt>. When it returns
     * @c false, the VM is turned off (no VM process) and not busy with
     * another exclusive operation.
     */
    static bool IsOnlineOrTransient(MachineState_T aState)
    {
        return IsOnline(aState) || IsTransient(aState);
    }

    /**
     * Stringify a machine state - translated.
     *
     * Drop the Global:: prefix and include StringifyEnums.h for an untranslated
     * version of this method.
     *
     * @returns Pointer to a read only string.
     * @param   aState      Valid machine state.
     */
    static const char *stringifyMachineState(MachineState_T aState);

    /**
     * Stringify a session state - translated.
     *
     * Drop the Global:: prefix and include StringifyEnums.h for an untranslated
     * version of this method.
     *
     * @returns Pointer to a read only string.
     * @param   aState      Valid session state.
     */
    static const char *stringifySessionState(SessionState_T aState);

    /**
     * Stringify a device type.
     *
     * Drop the Global:: prefix and include StringifyEnums.h for an untranslated
     * version of this method.
     *
     * @returns Pointer to a read only string.
     * @param   aType       The device type.
     */
    static const char *stringifyDeviceType(DeviceType_T aType);

    /**
     * Stringify a guest session status.
     *
     * Drop the Global:: prefix and include StringifyEnums.h for an untranslated
     * version of this method.
     *
     * @returns Pointer to a read only string.
     * @param   aStatus     The guest session status.
     */
    static const char *stringifyGuestSessionStatus(GuestSessionStatus_T aStatus);

    /**
     * Stringify a process status.
     *
     * Drop the Global:: prefix and include StringifyEnums.h for an untranslated
     * version of this method.
     *
     * @returns Pointer to a read only string.
     * @param   aStatus     The process status.
     */
    static const char *stringifyProcessStatus(ProcessStatus_T aStatus);

    /**
     * Stringify a process wait result.
     *
     * Drop the Global:: prefix and include StringifyEnums.h for an untranslated
     * version of this method.
     *
     * @returns Pointer to a read only string.
     * @param   aWaitResult The process wait result.
     */
    static const char *stringifyProcessWaitResult(ProcessWaitResult_T aWaitResult);

    /**
     * Stringify a file status.
     *
     * Drop the Global:: prefix and include StringifyEnums.h for an untranslated
     * version of this method.
     *
     * @returns Pointer to a read only string.
     * @param   aStatus     The file status.
     */
    static const char *stringifyFileStatus(FileStatus_T aStatus);

    /**
     * Stringify a filesystem object type.
     *
     * Drop the Global:: prefix and include StringifyEnums.h for an untranslated
     * version of this method.
     *
     * @returns Pointer to a read only string.
     * @param   aType       The filesystem object type.
     */
    static const char *stringifyFsObjType(FsObjType_T aType);

    /**
     * Stringify a platform architecture to a string.
     *
     * @returns Platform architecture as a string.
     * @param   aEnmArchitecture        Platform architecture to convert.
     */
    static const char *stringifyPlatformArchitecture(PlatformArchitecture_T aEnmArchitecture);

    /**
     * Stringify a storage controller type.
     *
     * Drop the Global:: prefix and include StringifyEnums.h for an untranslated
     * version of this method.
     *
     * @returns Pointer to a read only string.
     * @param   aType       The storage controller type.
     */
    static const char *stringifyStorageControllerType(StorageControllerType_T aType);

#if 0 /* unused */
    /**
     * Stringify a storage bus type.
     *
     * Drop the Global:: prefix and include StringifyEnums.h for an untranslated
     * version of this method.
     *
     * @returns Pointer to a read only string.
     * @param   aBus        The storage bus type.
     */
    static const char *stringifyStorageBus(StorageBus_T aBus);

    /**
     * Stringify a reason.
     *
     * Drop the Global:: prefix and include StringifyEnums.h for an untranslated
     * version of this method.
     *
     * @returns Pointer to a read only string.
     * @param   aReason     The reason code.
     */
    static const char *stringifyReason(Reason_T aReason);
#endif

    /**
     * Try convert a COM status code to a VirtualBox status code (VBox/err.h).
     *
     * @returns VBox status code.
     * @param   aComStatus      COM status code.
     */
    static int vboxStatusCodeFromCOM(HRESULT aComStatus);

    /**
     * Try convert a VirtualBox status code (VBox/err.h) to a COM status code.
     *
     * This is mainly intended for dealing with vboxStatusCodeFromCOM() return
     * values.  If used on anything else, it won't be able to cope with most of the
     * input!
     *
     * @returns COM status code.
     * @param   aVBoxStatus      VBox status code.
     */
    static HRESULT vboxStatusCodeToCOM(int aVBoxStatus);
};

#endif /* !MAIN_INCLUDED_Global_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
