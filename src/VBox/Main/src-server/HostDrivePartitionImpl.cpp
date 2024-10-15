/* $Id: HostDrivePartitionImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - IHostDrivePartition implementation, VBoxSVC.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_HOSTDRIVEPARTITION
#include "HostDrivePartitionImpl.h"
#include "LoggingNew.h"

#include <iprt/errcore.h>

/*
 * HostDrivePartition implementation.
 */
DEFINE_EMPTY_CTOR_DTOR(HostDrivePartition)

HRESULT HostDrivePartition::FinalConstruct()
{
    return BaseFinalConstruct();
}

void HostDrivePartition::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

/*
 * Initializes the instance.
 */
HRESULT HostDrivePartition::initFromDvmVol(RTDVMVOLUME hVol)
{
    LogFlowThisFunc(("\n"));

    AssertReturn(hVol != NIL_RTDVMVOLUME, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* Common attributes: */
    m.number = RTDvmVolumeGetIndex(hVol, RTDVMVOLIDX_HOST);
    m.cbVol  = (LONG64)RTDvmVolumeGetSize(hVol);
    if (m.cbVol < 0)
        m.cbVol = INT64_MAX;

    uint64_t offStart = 0;
    uint64_t offLastIgnored = 0;
    int vrc = RTDvmVolumeQueryRange(hVol, &offStart, &offLastIgnored);
    AssertRC(vrc);
    m.offStart = RT_SUCCESS(vrc) ? (LONG64)offStart : 0;
    Assert((uint64_t)m.cbVol == offLastIgnored - offStart + 1 || RT_FAILURE(vrc));
    if (m.offStart < 0)
        m.offStart = INT64_MAX;

    uint64_t fFlags = RTDvmVolumeGetFlags(hVol);
    m.active = (fFlags & (DVMVOLUME_FLAGS_BOOTABLE | DVMVOLUME_FLAGS_ACTIVE)) != 0;

    /* MBR: */
    m.firstCylinder = (uint16_t)RTDvmVolumeGetPropU64(hVol, RTDVMVOLPROP_MBR_FIRST_CYLINDER, 0);
    m.firstHead     = (uint8_t )RTDvmVolumeGetPropU64(hVol, RTDVMVOLPROP_MBR_FIRST_HEAD, 0);
    m.firstSector   = (uint8_t )RTDvmVolumeGetPropU64(hVol, RTDVMVOLPROP_MBR_FIRST_SECTOR, 0);
    m.lastCylinder  = (uint16_t)RTDvmVolumeGetPropU64(hVol, RTDVMVOLPROP_MBR_LAST_CYLINDER, 0);
    m.lastHead      = (uint8_t )RTDvmVolumeGetPropU64(hVol, RTDVMVOLPROP_MBR_LAST_HEAD, 0);
    m.lastSector    = (uint8_t )RTDvmVolumeGetPropU64(hVol, RTDVMVOLPROP_MBR_LAST_SECTOR, 0);
    m.bMBRType      = (uint8_t )RTDvmVolumeGetPropU64(hVol, RTDVMVOLPROP_MBR_TYPE, 0);

    /* GPT: */
    RTUUID Uuid;
    vrc = RTDvmVolumeQueryProp(hVol, RTDVMVOLPROP_GPT_TYPE, &Uuid, sizeof(Uuid), NULL);
    if (RT_SUCCESS(vrc))
        m.typeUuid = Uuid;
    vrc = RTDvmVolumeQueryProp(hVol, RTDVMVOLPROP_GPT_UUID, &Uuid, sizeof(Uuid), NULL);
    if (RT_SUCCESS(vrc))
        m.uuid = Uuid;

    char *pszName = NULL;
    vrc = RTDvmVolumeQueryName(hVol, &pszName);
    if (RT_SUCCESS(vrc))
    {
        HRESULT hrc = m.name.assignEx(pszName);
        RTStrFree(pszName);
        AssertComRCReturn(hrc, hrc);
    }

    /*
     * Do the type translation to the best of our ability.
     */
    m.enmType = PartitionType_Unknown;
    if (m.typeUuid.isZero())
        switch ((PartitionType_T)m.bMBRType)
        {
            case PartitionType_FAT12:
            case PartitionType_FAT16:
            case PartitionType_FAT:
            case PartitionType_IFS:
            case PartitionType_FAT32CHS:
            case PartitionType_FAT32LBA:
            case PartitionType_FAT16B:
            case PartitionType_Extended:
            case PartitionType_WindowsRE:
            case PartitionType_LinuxSwapOld:
            case PartitionType_LinuxOld:
            case PartitionType_DragonFlyBSDSlice:
            case PartitionType_LinuxSwap:
            case PartitionType_Linux:
            case PartitionType_LinuxExtended:
            case PartitionType_LinuxLVM:
            case PartitionType_BSDSlice:
            case PartitionType_AppleUFS:
            case PartitionType_AppleHFS:
            case PartitionType_Solaris:
            case PartitionType_GPT:
            case PartitionType_EFI:
                m.enmType = (PartitionType_T)m.bMBRType;
                break;

            case PartitionType_Empty:
            default:
                break;
        }
    else
    {
        static struct { const char *pszUuid; PartitionType_T enmType; } const s_aUuidToType[] =
        {
            { "024dee41-33e7-11d3-9d69-0008c781f39f", PartitionType_MBR },
            { "c12a7328-f81f-11d2-ba4b-00a0c93ec93b", PartitionType_EFI },
            { "d3bfe2de-3daf-11df-ba40-e3a556d89593", PartitionType_iFFS },
            { "f4019732-066e-4e12-8273-346c5641494f", PartitionType_SonyBoot },
            { "bfbfafe7-a34f-448a-9a5b-6213eb736c22", PartitionType_LenovoBoot },
            /* Win: */
            { "e3c9e316-0b5c-4db8-817d-f92df00215ae", PartitionType_WindowsMSR },
            { "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7", PartitionType_WindowsBasicData },
            { "5808c8aa-7e8f-42e0-85d2-e1e90434cfb3", PartitionType_WindowsLDMMeta },
            { "af9b60a0-1431-4f62-bc68-3311714a69ad", PartitionType_WindowsLDMData },
            { "de94bba4-06d1-4d40-a16a-bfd50179d6ac", PartitionType_WindowsRecovery },
            { "e75caf8f-f680-4cee-afa3-b001e56efc2d", PartitionType_WindowsStorageSpaces },
            { "558d43c5-a1ac-43c0-aac8-d1472b2923d1", PartitionType_WindowsStorageReplica },
            { "37affc90-ef7d-4e96-91c3-2d7ae055b174", PartitionType_IBMGPFS },
            /* Linux: */
            { "0fc63daf-8483-4772-8e79-3d69d8477de4", PartitionType_LinuxData },
            { "a19d880f-05fc-4d3b-a006-743f0f84911e", PartitionType_LinuxRAID },
            { "44479540-f297-41b2-9af7-d131d5f0458a", PartitionType_LinuxRootX86 },
            { "4f68bce3-e8cd-4db1-96e7-fbcaf984b709", PartitionType_LinuxRootAMD64 },
            { "69dad710-2ce4-4e3c-b16c-21a1d49abed3", PartitionType_LinuxRootARM32 },
            { "b921b045-1df0-41c3-af44-4c6f280d3fae", PartitionType_LinuxRootARM64 },
            { "933ac7e1-2eb4-4f13-b844-0e14e2aef915", PartitionType_LinuxHome },
            { "3b8f8425-20e0-4f3b-907f-1a25a76f98e8", PartitionType_LinuxSrv },
            { "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f", PartitionType_LinuxSwap },
            { "e6d6d379-f507-44c2-a23c-238f2a3df928", PartitionType_LinuxLVM },
            { "7ffec5c9-2d00-49b7-8941-3ea10a5586b7", PartitionType_LinuxPlainDmCrypt },
            { "ca7d7ccb-63ed-4c53-861c-1742536059cc", PartitionType_LinuxLUKS },
            { "8da63339-0007-60c0-c436-083ac8230908", PartitionType_LinuxReserved },
            /* FreeBSD: */
            { "83bd6b9d-7f41-11dc-be0b-001560b84f0f", PartitionType_FreeBSDBoot },
            { "516e7cb4-6ecf-11d6-8ff8-00022d09712b", PartitionType_FreeBSDData },
            { "516e7cb5-6ecf-11d6-8ff8-00022d09712b", PartitionType_FreeBSDSwap },
            { "516e7cb6-6ecf-11d6-8ff8-00022d09712b", PartitionType_FreeBSDUFS },
            { "516e7cb8-6ecf-11d6-8ff8-00022d09712b", PartitionType_FreeBSDVinum },
            { "516e7cba-6ecf-11d6-8ff8-00022d09712b", PartitionType_FreeBSDZFS },
            /* Apple/macOS: */
            { "48465300-0000-11aa-aa11-00306543ecac", PartitionType_AppleHFSPlus },
            { "7c3457ef-0000-11aa-aa11-00306543ecac", PartitionType_AppleAPFS },
            { "55465300-0000-11aa-aa11-00306543ecac", PartitionType_AppleUFS },
            { "52414944-0000-11aa-aa11-00306543ecac", PartitionType_AppleRAID },
            { "52414944-5f4f-11aa-aa11-00306543ecac", PartitionType_AppleRAIDOffline },
            { "426f6f74-0000-11aa-aa11-00306543ecac", PartitionType_AppleBoot },
            { "4c616265-6c00-11aa-aa11-00306543ecac", PartitionType_AppleLabel },
            { "5265636f-7665-11aa-aa11-00306543ecac", PartitionType_AppleTvRecovery },
            { "53746f72-6167-11aa-aa11-00306543ecac", PartitionType_AppleCoreStorage },
            { "b6fa30da-92d2-4a9a-96f1-871ec6486200", PartitionType_SoftRAIDStatus },
            { "2e313465-19b9-463f-8126-8a7993773801", PartitionType_SoftRAIDScratch },
            { "fa709c7e-65b1-4593-bfd5-e71d61de9b02", PartitionType_SoftRAIDVolume },
            { "bbba6df5-f46f-4a89-8f59-8765b2727503", PartitionType_SoftRAIDCache },
            /* Solaris */
            { "6a82cb45-1dd2-11b2-99a6-080020736631", PartitionType_SolarisBoot },
            { "6a85cf4d-1dd2-11b2-99a6-080020736631", PartitionType_SolarisRoot },
            { "6a87c46f-1dd2-11b2-99a6-080020736631", PartitionType_SolarisSwap },
            { "6a8b642b-1dd2-11b2-99a6-080020736631", PartitionType_SolarisBackup },
            { "6a898cc3-1dd2-11b2-99a6-080020736631", PartitionType_SolarisUsr },
            { "6a8ef2e9-1dd2-11b2-99a6-080020736631", PartitionType_SolarisVar },
            { "6a90ba39-1dd2-11b2-99a6-080020736631", PartitionType_SolarisHome },
            { "6a9283a5-1dd2-11b2-99a6-080020736631", PartitionType_SolarisAltSector },
            { "6a945a3b-1dd2-11b2-99a6-080020736631", PartitionType_SolarisReserved },
            { "6a9630d1-1dd2-11b2-99a6-080020736631", PartitionType_SolarisReserved },
            { "6a980767-1dd2-11b2-99a6-080020736631", PartitionType_SolarisReserved },
            { "6a96237f-1dd2-11b2-99a6-080020736631", PartitionType_SolarisReserved },
            { "6a8d2ac7-1dd2-11b2-99a6-080020736631", PartitionType_SolarisReserved },
            /* NetBSD: */
            { "49f48d32-b10e-11dc-b99b-0019d1879648", PartitionType_NetBSDSwap },
            { "49f48d5a-b10e-11dc-b99b-0019d1879648", PartitionType_NetBSDFFS },
            { "49f48d82-b10e-11dc-b99b-0019d1879648", PartitionType_NetBSDLFS },
            { "49f48daa-b10e-11dc-b99b-0019d1879648", PartitionType_NetBSDRAID },
            { "2db519c4-b10f-11dc-b99b-0019d1879648", PartitionType_NetBSDConcatenated },
            { "2db519ec-b10f-11dc-b99b-0019d1879648", PartitionType_NetBSDEncrypted },
            /* Chrome OS: */
            { "fe3a2a5d-4f32-41a7-b725-accc3285a309", PartitionType_ChromeOSKernel },
            { "3cb8e202-3b7e-47dd-8a3c-7ff2a13cfcec", PartitionType_ChromeOSRootFS },
            { "2e0a753d-9e48-43b0-8337-b15192cb1b5e", PartitionType_ChromeOSFuture },
            /* Container Linux: */
            { "5dfbf5f4-2848-4bac-aa5e-0d9a20b745a6", PartitionType_ContLnxUsr },
            { "3884dd41-8582-4404-b9a8-e9b84f2df50e", PartitionType_ContLnxRoot },
            { "c95dc21a-df0e-4340-8d7b-26cbfa9a03e0", PartitionType_ContLnxReserved },
            { "be9067b9-ea49-4f15-b4f6-f36f8c9e1818", PartitionType_ContLnxRootRAID },
            /* Haiku: */
            { "42465331-3ba3-10f1-802a-4861696b7521", PartitionType_HaikuBFS },
            /* MidnightBSD */
            { "85d5e45e-237c-11e1-b4b3-e89a8f7fc3a7", PartitionType_MidntBSDBoot },
            { "85d5e45a-237c-11e1-b4b3-e89a8f7fc3a7", PartitionType_MidntBSDData },
            { "85d5e45b-237c-11e1-b4b3-e89a8f7fc3a7", PartitionType_MidntBSDSwap },
            { "0394ef8b-237e-11e1-b4b3-e89a8f7fc3a7", PartitionType_MidntBSDUFS },
            { "85d5e45c-237c-11e1-b4b3-e89a8f7fc3a7", PartitionType_MidntBSDVium },
            { "85d5e45d-237c-11e1-b4b3-e89a8f7fc3a7", PartitionType_MidntBSDZFS },
            /* OpenBSD: */
            { "824cc7a0-36a8-11e3-890a-952519ad3f61", PartitionType_OpenBSDData },
            /* QNX: */
            { "cef5a9ad-73bc-4601-89f3-cdeeeee321a1", PartitionType_QNXPowerSafeFS },
            /* Plan 9: */
            { "c91818f9-8025-47af-89d2-f030d7000c2c", PartitionType_Plan9 },
            /* VMWare ESX: */
            { "9d275380-40ad-11db-bf97-000c2911d1b8", PartitionType_VMWareVMKCore },
            { "aa31e02a-400f-11db-9590-000c2911d1b8", PartitionType_VMWareVMFS },
            { "9198effc-31c0-11db-8f78-000c2911d1b8", PartitionType_VMWareReserved },
            /* Android-x86: */
            { "2568845d-2332-4675-bc39-8fa5a4748d15", PartitionType_AndroidX86Bootloader },
            { "114eaffe-1552-4022-b26e-9b053604cf84", PartitionType_AndroidX86Bootloader2 },
            { "49a4d17f-93a3-45c1-a0de-f50b2ebe2599", PartitionType_AndroidX86Boot },
            { "4177c722-9e92-4aab-8644-43502bfd5506", PartitionType_AndroidX86Recovery },
            { "ef32a33b-a409-486c-9141-9ffb711f6266", PartitionType_AndroidX86Misc },
            { "20ac26be-20b7-11e3-84c5-6cfdb94711e9", PartitionType_AndroidX86Metadata },
            { "38f428e6-d326-425d-9140-6e0ea133647c", PartitionType_AndroidX86System },
            { "a893ef21-e428-470a-9e55-0668fd91a2d9", PartitionType_AndroidX86Cache },
            { "dc76dda9-5ac1-491c-af42-a82591580c0d", PartitionType_AndroidX86Data },
            { "ebc597d0-2053-4b15-8b64-e0aac75f4db1", PartitionType_AndroidX86Persistent },
            { "c5a0aeec-13ea-11e5-a1b1-001e67ca0c3c", PartitionType_AndroidX86Vendor },
            { "bd59408b-4514-490d-bf12-9878d963f378", PartitionType_AndroidX86Config },
            { "8f68cc74-c5e5-48da-be91-a0c8c15e9c80", PartitionType_AndroidX86Factory },
            { "9fdaa6ef-4b3f-40d2-ba8d-bff16bfb887b", PartitionType_AndroidX86FactoryAlt },
            { "767941d0-2085-11e3-ad3b-6cfdb94711e9", PartitionType_AndroidX86Fastboot },
            { "ac6d7924-eb71-4df8-b48d-e267b27148ff", PartitionType_AndroidX86OEM },
            /* Android ARM: */
            { "19a710a2-b3ca-11e4-b026-10604b889dcf", PartitionType_AndroidARMMeta },
            { "193d1ea4-b3ca-11e4-b075-10604b889dcf", PartitionType_AndroidARMExt },
            /* Open Network Install Environment: */
            { "7412f7d5-a156-4b13-81dc-867174929325", PartitionType_ONIEBoot },
            { "d4e6e2cd-4469-46f3-b5cb-1bff57afc149", PartitionType_ONIEConfig },
            /* PowerPC: */
            { "9e1a2d38-c612-4316-aa26-8b49521e5a8b", PartitionType_PowerPCPrep },
            /* freedesktop.org: */
            { "bc13c2ff-59e6-4262-a352-b275fd6f7172", PartitionType_XDGShrBootConfig },
            /* Ceph: */
            { "cafecafe-9b03-4f30-b4c6-b4b80ceff106", PartitionType_CephBlock },
            { "30cd0809-c2b2-499c-8879-2d6b78529876", PartitionType_CephBlockDB },
            { "93b0052d-02d9-4d8a-a43b-33a3ee4dfbc3", PartitionType_CephBlockDBDmc },
            { "166418da-c469-4022-adf4-b30afd37f176", PartitionType_CephBlockDBDmcLUKS },
            { "cafecafe-9b03-4f30-b4c6-5ec00ceff106", PartitionType_CephBlockDmc },
            { "cafecafe-9b03-4f30-b4c6-35865ceff106", PartitionType_CephBlockDmcLUKS },
            { "5ce17fce-4087-4169-b7ff-056cc58473f9", PartitionType_CephBlockWALog },
            { "306e8683-4fe2-4330-b7c0-00a917c16966", PartitionType_CephBlockWALogDmc },
            { "86a32090-3647-40b9-bbbd-38d8c573aa86", PartitionType_CephBlockWALogDmcLUKS },
            { "89c57f98-2fe5-4dc0-89c1-f3ad0ceff2be", PartitionType_CephDisk },
            { "89c57f98-2fe5-4dc0-89c1-5ec00ceff2be", PartitionType_CephDiskDmc },
            { "45b0969e-9b03-4f30-b4c6-b4b80ceff106", PartitionType_CephJournal },
            { "45b0969e-9b03-4f30-b4c6-5ec00ceff106", PartitionType_CephJournalDmc },
            { "45b0969e-9b03-4f30-b4c6-35865ceff106", PartitionType_CephJournalDmcLUKS },
            { "fb3aabf9-d25f-47cc-bf5e-721d1816496b", PartitionType_CephLockbox },
            { "cafecafe-8ae0-4982-bf9d-5a8d867af560", PartitionType_CephMultipathBlock1 },
            { "7f4a666a-16f3-47a2-8445-152ef4d03f6c", PartitionType_CephMultipathBlock2 },
            { "ec6d6385-e346-45dc-be91-da2a7c8b3261", PartitionType_CephMultipathBlockDB },
            { "01b41e1b-002a-453c-9f17-88793989ff8f", PartitionType_CephMultipathBLockWALog },
            { "45b0969e-8ae0-4982-bf9d-5a8d867af560", PartitionType_CephMultipathJournal },
            { "4fbd7e29-8ae0-4982-bf9d-5a8d867af560", PartitionType_CephMultipathOSD },
            { "4fbd7e29-9d25-41b8-afd0-062c0ceff05d", PartitionType_CephOSD },
            { "4fbd7e29-9d25-41b8-afd0-5ec00ceff05d", PartitionType_CephOSDDmc },
            { "4fbd7e29-9d25-41b8-afd0-35865ceff05d", PartitionType_CephOSDDmcLUKS },
        };
        for (size_t i = 0; i < RT_ELEMENTS(s_aUuidToType); i++)
            if (m.typeUuid.equalsString(s_aUuidToType[i].pszUuid))
            {
                m.enmType = s_aUuidToType[i].enmType;
                break;
            }

        /* Some OSes are using non-random UUIDs and we can at least identify the
           OS if not the exact type. */
        if (m.enmType == PartitionType_Unknown)
        {
            char szType[RTUUID_STR_LENGTH];
            m.typeUuid.toString(szType, sizeof(szType));
            RTStrToLower(szType);
            if (RTStrSimplePatternMatch(szType, "516e7c??-6ecf-11d6-8ff8-00022d09712b"))
                m.enmType = PartitionType_FreeBSDUnknown;
            else if (RTStrSimplePatternMatch(szType, "????????-????-11aa-aa11-00306543ecac"))
                m.enmType = PartitionType_AppleUnknown;
            else if (RTStrSimplePatternMatch(szType, "????????-1dd2-11b2-99a6-080020736631"))
                m.enmType = PartitionType_SolarisUnknown;
            else if (RTStrSimplePatternMatch(szType, "????????-b1??-11dc-b99b-0019d1879648"))
                m.enmType = PartitionType_NetBSDUnknown;
            else if (RTStrSimplePatternMatch(szType, "????????-23??-11e1-b4b3-e89a8f7fc3a7"))
                m.enmType = PartitionType_MidntBSDUnknown;
            else if (RTStrSimplePatternMatch(szType, "????????-????-11db-????-000c2911d1b8"))
                m.enmType = PartitionType_VMWareUnknown;
        }

#ifdef VBOX_STRICT
        /* Make sure we've done have any duplicates in the translation table: */
        static bool s_fCheckedForDuplicates = false;
        if (!s_fCheckedForDuplicates)
        {
            for (size_t i = 0; i < RT_ELEMENTS(s_aUuidToType); i++)
                for (size_t j = i + 1; j < RT_ELEMENTS(s_aUuidToType); j++)
                    AssertMsg(RTUuidCompare2Strs(s_aUuidToType[i].pszUuid, s_aUuidToType[j].pszUuid) != 0,
                              ("%d & %d: %s\n", i, j, s_aUuidToType[i].pszUuid));
            s_fCheckedForDuplicates = true;
        }
#endif

    }

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/*
 * Uninitializes the instance.
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void HostDrivePartition::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    m.number = 0;
    m.cbVol = 0;
    m.offStart = 0;
    m.enmType = PartitionType_Empty;
    m.active = 0;

    m.bMBRType = 0;
    m.firstCylinder = 0;
    m.firstHead = 0;
    m.firstSector = 0;
    m.lastCylinder = 0;
    m.lastHead = 0;
    m.lastSector = 0;

    m.typeUuid.clear();
    m.uuid.clear();
    m.name.setNull();
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
