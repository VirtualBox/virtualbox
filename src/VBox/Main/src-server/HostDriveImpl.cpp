/* $Id: HostDriveImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - IHostDrive implementation, VBoxSVC.
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

#define LOG_GROUP LOG_GROUP_MAIN_HOSTDRIVE
#include "Global.h"
#include "HostDriveImpl.h"
#include "HostDrivePartitionImpl.h"
#include "LoggingNew.h"
#include "VirtualBoxImpl.h"

#include <iprt/dvm.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/vfs.h>
#include <VBox/com/Guid.h>


/*
 * HostDrive implementation.
 */
DEFINE_EMPTY_CTOR_DTOR(HostDrive)

HRESULT HostDrive::FinalConstruct()
{
    return BaseFinalConstruct();
}

void HostDrive::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

/**
 * Initializes the instance.
 */
HRESULT HostDrive::initFromPathAndModel(const com::Utf8Str &drivePath, const com::Utf8Str &driveModel)
{
    LogFlowThisFunc(("\n"));

    AssertReturn(!drivePath.isEmpty(), E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m.partitioningType = PartitioningType_MBR;
    m.drivePath = drivePath;
    m.model = driveModel;
    m.partitions.clear();

    /*
     * Try open the drive so we can extract futher details,
     * like the size, sector size and partitions.
     */
    HRESULT hrc = E_FAIL;
    RTFILE hRawFile = NIL_RTFILE;
    int vrc = RTFileOpen(&hRawFile, drivePath.c_str(), RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTFileQuerySize(hRawFile, &m.cbDisk);
        int vrc2 = RTFileQuerySectorSize(hRawFile, &m.cbSector);
        if (RT_FAILURE(vrc2))
            vrc = vrc2;
        if (RT_SUCCESS(vrc))
        {
            /*
             * Hand it to DVM.
             */
            RTVFSFILE hVfsFile = NIL_RTVFSFILE;
            vrc = RTVfsFileFromRTFile(hRawFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_NONE /*fOpen*/,
                                      true /*fLeaveOpen*/, &hVfsFile);
            if (RT_SUCCESS(vrc))
            {
                RTDVM hVolMgr = NIL_RTDVM;
                vrc = RTDvmCreate(&hVolMgr, hVfsFile, m.cbSector, 0 /*fFlags*/);
                if (RT_SUCCESS(vrc))
                {
                    vrc = RTDvmMapOpen(hVolMgr);
                    if (RT_SUCCESS(vrc))
                    {
                        hrc = S_OK;

                        /*
                         * Get details.
                         */
                        switch (RTDvmMapGetFormatType(hVolMgr))
                        {
                            case RTDVMFORMATTYPE_GPT:
                                m.partitioningType = PartitioningType_GPT;
                                break;
                            case RTDVMFORMATTYPE_MBR:
                                m.partitioningType = PartitioningType_MBR;
                                break;
                            case RTDVMFORMATTYPE_BSD_LABEL:
                                AssertMsgFailed(("TODO\n"));
                                break;
                            default:
                                AssertFailed();
                        }

                        RTUUID Uuid = RTUUID_INITIALIZE_NULL;
                        if (RT_SUCCESS(RTDvmMapQueryDiskUuid(hVolMgr, &Uuid)))
                            m.uuid = Uuid;

                        /*
                         * Enumerate volumes and tuck them into the partitions list..
                         */
                        uint32_t const  cVolumes = RTDvmMapGetValidVolumes(hVolMgr);
                        RTDVMVOLUME     hVol     = NIL_RTDVMVOLUME;
                        for (uint32_t i = 0; i < cVolumes; i++)
                        {
                            /* Enumeration cruft: */
                            RTDVMVOLUME hVolNext = NIL_RTDVMVOLUME;
                            if (i == 0)
                                vrc = RTDvmMapQueryFirstVolume(hVolMgr, &hVolNext);
                            else
                                vrc = RTDvmMapQueryNextVolume(hVolMgr, hVol, &hVolNext);
                            AssertRCBreakStmt(vrc, hrc = Global::vboxStatusCodeToCOM(vrc));

                            uint32_t cRefs = RTDvmVolumeRelease(hVol);
                            Assert(cRefs != UINT32_MAX); RT_NOREF(cRefs);
                            hVol = hVolNext;

                            /* Instantiate a new partition object and add it to the list: */
                            ComObjPtr<HostDrivePartition> ptrHostPartition;
                            hrc = ptrHostPartition.createObject();
                            if (SUCCEEDED(hrc))
                                hrc = ptrHostPartition->initFromDvmVol(hVol);
                            if (SUCCEEDED(hrc))
                                try
                                {
                                    m.partitions.push_back(ptrHostPartition);
                                }
                                catch (std::bad_alloc &)
                                {
                                    AssertFailedBreakStmt(hrc = E_OUTOFMEMORY);
                                }
                        }
                        RTDvmVolumeRelease(hVol);
                    }
                    else
                        hrc = Global::vboxStatusCodeToCOM(vrc);
                    RTDvmRelease(hVolMgr);
                }
                else
                    hrc = Global::vboxStatusCodeToCOM(vrc);
                RTVfsFileRelease(hVfsFile);
            }
            else
                hrc = Global::vboxStatusCodeToCOM(vrc);
        }
        else /* VERR_IO_NOT_READ / STATUS_NO_MEDIA_IN_DEVICE is likely for card readers on windows. */
            hrc = Global::vboxStatusCodeToCOM(vrc);
        RTFileClose(hRawFile);
    }
    else
    {
        /*
         * We don't use the Global::vboxStatusCodeToCOM(vrc) here
         * because RTFileOpen can return some error which causes
         * the assertion and breaks original idea of returning
         * the object in the limited state.
         */
        if (   vrc == VERR_RESOURCE_BUSY
            || vrc == VERR_ACCESS_DENIED)
            hrc = E_ACCESSDENIED;
        else
            hrc = VBOX_E_IPRT_ERROR;
    }

    /* Confirm a successful initialization */
    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();
    else
        autoInitSpan.setLimited(hrc);
    return S_OK;
}

/**
 * Uninitializes the instance.
 * Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void HostDrive::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    m.drivePath.setNull();
    m.partitions.clear();
}


/*********************************************************************************************************************************
*   IHostDrive properties                                                                                                        *
*********************************************************************************************************************************/

HRESULT HostDrive::getPartitioningType(PartitioningType_T *aPartitioningType)
{
    *aPartitioningType = m.partitioningType;
    return S_OK;
}

HRESULT HostDrive::getDrivePath(com::Utf8Str &aDrivePath)
{
    aDrivePath = m.drivePath;
    return S_OK;
}

HRESULT HostDrive::getUuid(com::Guid &aUuid)
{
    aUuid = m.uuid;
    return S_OK;
}

HRESULT HostDrive::getSectorSize(ULONG *aSectorSize)
{
    *aSectorSize = m.cbSector;
    return S_OK;
}

HRESULT HostDrive::getSize(LONG64 *aSize)
{
    *aSize = (LONG64)m.cbDisk;
    if (*aSize < 0)
        *aSize = INT64_MAX;
    return S_OK;
}

HRESULT HostDrive::getModel(com::Utf8Str &aModel)
{
    return aModel.assignEx(m.model);
}

HRESULT HostDrive::getPartitions(std::vector<ComPtr<IHostDrivePartition> > &aPartitions)
{
    aPartitions = m.partitions;
    return S_OK;
}



/* vi: set tabstop=4 shiftwidth=4 expandtab: */
