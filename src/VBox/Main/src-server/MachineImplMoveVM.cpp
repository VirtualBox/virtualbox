/* $Id: MachineImplMoveVM.cpp 108960 2025-03-26 13:45:20Z brent.paulson@oracle.com $ */
/** @file
 * Implementation of MachineMoveVM
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_MACHINE
#include <iprt/fs.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/cpp/utils.h>
#include <iprt/stream.h>
#include <VBox/com/ErrorInfo.h>

#include "MachineImplMoveVM.h"
#include "SnapshotImpl.h"
#include "MediumFormatImpl.h"
#include "VirtualBoxImpl.h"
#include "LoggingNew.h"

typedef std::multimap<Utf8Str, Utf8Str> list_t;
typedef std::multimap<Utf8Str, Utf8Str>::const_iterator cit_t;
typedef std::multimap<Utf8Str, Utf8Str>::iterator it_t;
typedef std::pair <std::multimap<Utf8Str, Utf8Str>::iterator, std::multimap<Utf8Str, Utf8Str>::iterator> rangeRes_t;

struct fileList_t
{
    HRESULT add(const Utf8Str &folder, const Utf8Str &file)
    {
        m_list.insert(std::make_pair(folder, file));
        return S_OK;
    }

    HRESULT add(const Utf8Str &fullPath)
    {
        Utf8Str folder = fullPath;
        folder.stripFilename();
        Utf8Str filename = fullPath;
        filename.stripPath();
        m_list.insert(std::make_pair(folder, filename));
        return S_OK;
    }

    HRESULT removeFileFromList(const Utf8Str &fullPath)
    {
        Utf8Str folder = fullPath;
        folder.stripFilename();
        Utf8Str filename = fullPath;
        filename.stripPath();
        rangeRes_t res = m_list.equal_range(folder);
        for (it_t it=res.first; it!=res.second;)
        {
            if (it->second.equals(filename))
            {
                it_t it2 = it;
                ++it;
                m_list.erase(it2);
            }
            else
                ++it;
        }

        return S_OK;
    }

    HRESULT removeFileFromList(const Utf8Str &path, const Utf8Str &fileName)
    {
        rangeRes_t res = m_list.equal_range(path);
        for (it_t it=res.first; it!=res.second;)
        {
            if (it->second.equals(fileName))
            {
                it_t it2 = it;
                ++it;
                m_list.erase(it2);
            }
            else
                ++it;
        }
        return S_OK;
    }

    HRESULT removeFolderFromList(const Utf8Str &path)
    {
        m_list.erase(path);
        return S_OK;
    }

    rangeRes_t getFilesInRange(const Utf8Str &path)
    {
        rangeRes_t res;
        res = m_list.equal_range(path);
        return res;
    }

    std::list<Utf8Str> getFilesInList(const Utf8Str &path)
    {
        std::list<Utf8Str> list_;
        rangeRes_t res = m_list.equal_range(path);
        for (it_t it=res.first; it!=res.second; ++it)
            list_.push_back(it->second);
        return list_;
    }


    list_t m_list;

};


HRESULT MachineMoveVM::init()
{
    HRESULT hrc = S_OK;

    Utf8Str strTargetFolder;
    /* adding a trailing slash if it's needed */
    {
        size_t len = m_targetPath.length() + 2;
        if (len >= RTPATH_MAX)
            return m_pMachine->setError(VBOX_E_IPRT_ERROR, tr("The destination path exceeds the maximum value."));

        /** @todo r=bird: I need to add a Utf8Str method or iprt/cxx/path.h thingy
         *        for doing this.  We need this often and code like this doesn't
         *        need to be repeated and re-optimized in each instance... */
        char *path = new char [len];
        RTStrCopy(path, len, m_targetPath.c_str());
        RTPathEnsureTrailingSeparator(path, len);
        strTargetFolder = m_targetPath = path;
        delete[] path;
    }

    /*
     * We have a mode which user is able to request
     * basic mode:
     * - The images which are solely attached to the VM
     *   and located in the original VM folder will be moved.
     *
     * Comment: in the future some other modes can be added.
     */

    RTFOFF cbTotal = 0;
    RTFOFF cbFree = 0;
    uint32_t cbBlock = 0;
    uint32_t cbSector = 0;


    int vrc = RTFsQuerySizes(strTargetFolder.c_str(), &cbTotal, &cbFree, &cbBlock, &cbSector);
    if (RT_FAILURE(vrc))
        return m_pMachine->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                        tr("Unable to determine free space of target destination ('%s'): %Rrc"),
                                        strTargetFolder.c_str(), vrc);

    RTDIR hDir;
    vrc = RTDirOpen(&hDir, strTargetFolder.c_str());
    if (RT_FAILURE(vrc))
        return m_pMachine->setErrorVrc(vrc);

    Utf8Str strTempFile = strTargetFolder + "test.txt";
    RTFILE hFile;
    vrc = RTFileOpen(&hFile, strTempFile.c_str(), RTFILE_O_OPEN_CREATE | RTFILE_O_READWRITE | RTFILE_O_DENY_NONE);
    if (RT_FAILURE(vrc))
    {
        RTDirClose(hDir);
        return m_pMachine->setErrorVrc(vrc,
                                       tr("Can't create file 'test.txt' in folder '%s'. Check the access rights of the destination folder."),
                                       strTargetFolder.c_str());
    }

    /** @todo r=vvp: Do we need to check each return result here? Looks excessively.
     *  And it's not so important for the test file.
     * bird: I'd just do AssertRC on the same line, though the deletion
     * of the test is a little important. */
    vrc = RTFileClose(hFile); AssertRC(vrc);
    RTFileDelete(strTempFile.c_str());
    vrc = RTDirClose(hDir); AssertRC(vrc);

    Log2(("blocks: total %RTfoff, free %RTfoff\n", cbTotal, cbFree));
    Log2(("total space (Kb) %RTfoff (Mb) %RTfoff (Gb) %RTfoff\n", cbTotal/_1K, cbTotal/_1M, cbTotal/_1G));
    Log2(("total free space (Kb) %RTfoff (Mb) %RTfoff (Gb) %RTfoff\n", cbFree/_1K, cbFree/_1M, cbFree/_1G));

    RTFSPROPERTIES properties;
    vrc = RTFsQueryProperties(strTargetFolder.c_str(), &properties);
    if (RT_FAILURE(vrc))
        return m_pMachine->setErrorVrc(vrc, "RTFsQueryProperties(%s): %Rrc", strTargetFolder.c_str(), vrc);

    Log2(("disk properties: remote=%RTbool read only=%RTbool compressed=%RTbool\n",
          properties.fRemote, properties.fReadOnly, properties.fCompressed));

    /* Get the original VM path */
    Utf8Str strMachineFolder;
    Bstr bstr_settingsFilePath;
    hrc = m_pMachine->COMGETTER(SettingsFilePath)(bstr_settingsFilePath.asOutParam());
    if (FAILED(hrc))
        return hrc;

    strMachineFolder = bstr_settingsFilePath;
    strMachineFolder.stripFilename();

    m_vmFolders.insert(std::make_pair(VBox_MachineFolder, strMachineFolder));

    /*
     * Collect all known folders used by the VM:
     * - log folder;
     * - state folder;
     * - snapshot folder.
     */
    Utf8Str strLogFolder;
    Bstr bstr_logFolder;
    hrc = m_pMachine->COMGETTER(LogFolder)(bstr_logFolder.asOutParam());
    if (FAILED(hrc))
        return hrc;

    strLogFolder = bstr_logFolder;
    if (   m_type.equals("basic")
        && RTPathStartsWith(strLogFolder.c_str(), strMachineFolder.c_str()))
        m_vmFolders.insert(std::make_pair(VBox_LogFolder, strLogFolder));

    Utf8Str strStateFilePath;
    Bstr bstr_stateFilePath;
    MachineState_T machineState;
    hrc = m_pMachine->COMGETTER(State)(&machineState);
    if (FAILED(hrc))
        return hrc;

    if (machineState == MachineState_Saved || machineState == MachineState_AbortedSaved)
    {
        m_pMachine->COMGETTER(StateFilePath)(bstr_stateFilePath.asOutParam());
        strStateFilePath = bstr_stateFilePath;
        strStateFilePath.stripFilename();
        if (   m_type.equals("basic")
            && RTPathStartsWith(strStateFilePath.c_str(), strMachineFolder.c_str()))
            m_vmFolders.insert(std::make_pair(VBox_StateFolder, strStateFilePath));
    }

    Utf8Str strSnapshotFolder;
    Bstr bstr_snapshotFolder;
    hrc = m_pMachine->COMGETTER(SnapshotFolder)(bstr_snapshotFolder.asOutParam());
    if (FAILED(hrc))
        return hrc;

    strSnapshotFolder = bstr_snapshotFolder;
    if (   m_type.equals("basic")
        && RTPathStartsWith(strSnapshotFolder.c_str(), strMachineFolder.c_str()))
        m_vmFolders.insert(std::make_pair(VBox_SnapshotFolder, strSnapshotFolder));

    if (m_pMachine->i_isSnapshotMachine())
    {
        Bstr bstrSrcMachineId;
        hrc = m_pMachine->COMGETTER(Id)(bstrSrcMachineId.asOutParam());
        if (FAILED(hrc))
            return hrc;

        ComPtr<IMachine> newSrcMachine;
        hrc = m_pMachine->i_getVirtualBox()->FindMachine(bstrSrcMachineId.raw(), newSrcMachine.asOutParam());
        if (FAILED(hrc))
            return hrc;
    }

    /* Add the current machine and all snapshot machines below this machine
     * in a list for further processing.
     */
    int64_t neededFreeSpace = 0;
    Utf8Str strTargetImageName;

    machineList.push_back(m_pMachine);

    {
        ULONG cSnapshots = 0;
        hrc = m_pMachine->COMGETTER(SnapshotCount)(&cSnapshots);
        if (FAILED(hrc))
            return hrc;

        if (cSnapshots > 0)
        {
            Utf8Str id;
            if (m_pMachine->i_isSnapshotMachine())
                id = m_pMachine->i_getSnapshotId().toString();
            ComPtr<ISnapshot> pSnapshot;
            hrc = m_pMachine->FindSnapshot(Bstr(id).raw(), pSnapshot.asOutParam());
            if (FAILED(hrc))
                return hrc;
            hrc = createMachineList(pSnapshot);
            if (FAILED(hrc))
                return hrc;
        }
    }

    ULONG uCount       = 1;//looks like it should be initialized by 1. See assertion in the Progress::setNextOperation()
    ULONG uTotalWeight = 1;

    /* The lists m_llMedia, m_llSaveStateFiles and m_llNVRAMFiles are filled in the queryMediaForAllStates() */
    hrc = queryMediaForAllStates();
    if (FAILED(hrc))
        return hrc;

    /* Calculate the total size of images. Fill m_finalMediaMap */
    { /** The scope here for better reading, apart from that the variables have limited scope too */
        uint64_t totalMediaSize = 0;

        for (size_t i = 0; i < m_llMedia.size(); ++i)
        {
            MEDIUMTASKCHAINMOVE &mtc = m_llMedia.at(i);
            for (size_t a = mtc.chain.size(); a > 0; --a)
            {
                Bstr bstrLocation;
                Utf8Str name = mtc.chain[a - 1].strBaseName;
                ComPtr<IMedium> plMedium = mtc.chain[a - 1].pMedium;
                hrc = plMedium->COMGETTER(Location)(bstrLocation.asOutParam());
                if (FAILED(hrc))
                    return hrc;

                Utf8Str strLocation = bstrLocation;

                /* if an image is located in the actual VM folder it will be added to the actual list */
                if (strLocation.startsWith(strMachineFolder))
                {
                    LONG64 cbSize = 0;
                    hrc = plMedium->COMGETTER(Size)(&cbSize);
                    if (FAILED(hrc))
                        return hrc;

                    std::pair<std::map<Utf8Str, MEDIUMTASKMOVE>::iterator,bool> ret;
                    ret = m_finalMediaMap.insert(std::make_pair(name, mtc.chain[a - 1]));
                    if (ret.second == true)
                    {
                        /* Calculate progress data */
                        ++uCount;
                        uTotalWeight += mtc.chain[a - 1].uWeight;
                        totalMediaSize += (uint64_t)cbSize;
                        Log2(("Image %s was added into the moved list\n", name.c_str()));
                    }
                }
            }
        }

        Log2(("Total Size of images is %lld bytes\n", totalMediaSize));
        neededFreeSpace += totalMediaSize;
    }

    /* Prepare data for moving ".sav" files */
    {
        uint64_t totalStateSize = 0;

        for (size_t i = 0; i < m_llSaveStateFiles.size(); ++i)
        {
            uint64_t cbFile = 0;
            SNAPFILETASKMOVE &sft = m_llSaveStateFiles.at(i);

            Utf8Str name = sft.strFile;
            /* if a state file is located in the actual VM folder it will be added to the actual list */
            if (RTPathStartsWith(name.c_str(), strMachineFolder.c_str()))
            {
                vrc = RTFileQuerySizeByPath(name.c_str(), &cbFile);
                if (RT_SUCCESS(vrc))
                {
                    std::pair<std::map<Utf8Str, SNAPFILETASKMOVE>::iterator,bool> ret;
                    ret = m_finalSaveStateFilesMap.insert(std::make_pair(name, sft));
                    if (ret.second == true)
                    {
                        totalStateSize += cbFile;
                        ++uCount;
                        uTotalWeight += sft.uWeight;
                        Log2(("The state file %s was added into the moved list\n", name.c_str()));
                    }
                }
                else
                {
                    Log2(("The state file %s wasn't added into the moved list. Couldn't get the file size.\n",
                          name.c_str()));
                    return m_pMachine->setErrorVrc(vrc,
                                                   tr("Failed to get file size of '%s': %Rrc"),
                                                   name.c_str(), vrc);
                }
            }
        }

        neededFreeSpace += totalStateSize;
    }

    /* Prepare data for moving ".nvram" files */
    {
        uint64_t totalNVRAMSize = 0;

        for (size_t i = 0; i < m_llNVRAMFiles.size(); ++i)
        {
            uint64_t cbFile = 0;
            SNAPFILETASKMOVE &sft = m_llNVRAMFiles.at(i);

            Utf8Str name = sft.strFile;
            /* if a NVRAM file is located in the actual VM folder it will be added to the actual list */
            if (RTPathStartsWith(name.c_str(), strMachineFolder.c_str()))
            {
                vrc = RTFileQuerySizeByPath(name.c_str(), &cbFile);
                if (RT_SUCCESS(vrc))
                {
                    std::pair<std::map<Utf8Str, SNAPFILETASKMOVE>::iterator,bool> ret;
                    ret = m_finalNVRAMFilesMap.insert(std::make_pair(name, sft));
                    if (ret.second == true)
                    {
                        totalNVRAMSize += cbFile;
                        ++uCount;
                        uTotalWeight += sft.uWeight;
                        Log2(("The NVRAM file %s was added into the moved list\n", name.c_str()));
                    }
                }
                else
                {
                    Log2(("The NVRAM file %s wasn't added into the moved list. Couldn't get the file size.\n",
                          name.c_str()));
                    return m_pMachine->setErrorVrc(vrc,
                                                   tr("Failed to get file size of '%s': %Rrc"),
                                                   name.c_str(), vrc);
                }
            }
        }

        neededFreeSpace += totalNVRAMSize;
    }

    /* Prepare data for moving the log files */
    {
        Utf8Str strFolder = m_vmFolders[VBox_LogFolder];

        if (RTPathExists(strFolder.c_str()))
        {
            uint64_t totalLogSize = 0;
            hrc = getFolderSize(strFolder, totalLogSize);
            if (SUCCEEDED(hrc))
            {
                neededFreeSpace += totalLogSize;
                if (cbFree - neededFreeSpace <= _1M)
                    return m_pMachine->setError(E_FAIL,
                                                tr("Insufficient disk space available (%RTfoff needed, %RTfoff free)"),
                                                neededFreeSpace, cbFree);

                fileList_t filesList;
                hrc = getFilesList(strFolder, filesList);
                if (FAILED(hrc))
                    return hrc;

                cit_t it = filesList.m_list.begin();
                while (it != filesList.m_list.end())
                {
                    Utf8Str strFile = it->first.c_str();
                    strFile.append(RTPATH_DELIMITER).append(it->second.c_str());

                    uint64_t cbFile = 0;
                    vrc = RTFileQuerySizeByPath(strFile.c_str(), &cbFile);
                    if (RT_SUCCESS(vrc))
                    {
                        uCount       += 1;
                        uTotalWeight += (ULONG)((cbFile + _1M - 1) / _1M);
                        Log2(("The log file %s added into the moved list\n", strFile.c_str()));
                    }
                    else
                        Log2(("The log file %s wasn't added into the moved list. Couldn't get the file size.\n", strFile.c_str()));
                    ++it;
                }
            }
            else
                return hrc;
        }
        else
        {
            Log2(("Information: The original log folder %s doesn't exist\n", strFolder.c_str()));
            hrc = S_OK;//it's not error in this case if there isn't an original log folder
        }
    }

    LogRel(("Total space needed is %lld bytes\n", neededFreeSpace));
    /* Check a target location on enough room */
    if (cbFree - neededFreeSpace <= _1M)
    {
        LogRel(("but free space on destination is %RTfoff\n", cbFree));
        return m_pMachine->setError(VBOX_E_IPRT_ERROR,
                                    tr("Insufficient disk space available (%RTfoff needed, %RTfoff free)"),
                                    neededFreeSpace, cbFree);
    }

    /* Add step for .vbox machine setting file */
    ++uCount;
    uTotalWeight += 1;

    /* Reserve additional steps in case of failure and rollback all changes */
    uTotalWeight += uCount;//just add 1 for each possible rollback operation
    uCount += uCount;//and increase the steps twice

    /* Init Progress instance */
    {
        hrc = m_pProgress->init(m_pMachine->i_getVirtualBox(),
                                static_cast<IMachine *>(m_pMachine) /* aInitiator */,
                                Utf8Str(tr("Moving Machine")),
                                true /* fCancellable */,
                                uCount,
                                uTotalWeight,
                                Utf8Str(tr("Initialize Moving")),
                                1);
        if (FAILED(hrc))
            return m_pMachine->setError(hrc,
                                        tr("Failed to setup the progress object for the moving VM operation"));
    }

    /* save all VM data */
    m_pMachine->i_setModified(Machine::IsModified_MachineData);
    hrc = m_pMachine->SaveSettings();
    if (FAILED(hrc))
        return hrc;

    LogFlowFuncLeave();

    return hrc;
}

void MachineMoveVM::printStateFile(settings::SnapshotsList &snl)
{
    settings::SnapshotsList::iterator it;
    for (it = snl.begin(); it != snl.end(); ++it)
    {
        if (!it->strStateFile.isEmpty())
        {
            settings::Snapshot snap = (settings::Snapshot)(*it);
            Log2(("snap.uuid = %s\n", snap.uuid.toStringCurly().c_str()));
            Log2(("snap.strStateFile = %s\n", snap.strStateFile.c_str()));
        }

        if (!it->llChildSnapshots.empty())
            printStateFile(it->llChildSnapshots);
    }
}

/* static */
DECLCALLBACK(int) MachineMoveVM::updateProgress(unsigned uPercent, void *pvUser)
{
    MachineMoveVM *pTask = *(MachineMoveVM **)pvUser;

    if (    pTask
         && !pTask->m_pProgress.isNull())
    {
        BOOL fCanceled;
        pTask->m_pProgress->COMGETTER(Canceled)(&fCanceled);
        if (fCanceled)
            return -1;
        pTask->m_pProgress->SetCurrentOperationProgress(uPercent);
    }
    return VINF_SUCCESS;
}

/* static */
DECLCALLBACK(int) MachineMoveVM::copyFileProgress(unsigned uPercentage, void *pvUser)
{
    ComObjPtr<Progress> pProgress = *static_cast<ComObjPtr<Progress> *>(pvUser);

    BOOL fCanceled = false;
    HRESULT hrc = pProgress->COMGETTER(Canceled)(&fCanceled);
    if (FAILED(hrc)) return VERR_GENERAL_FAILURE;
    /* If canceled by the user tell it to the copy operation. */
    if (fCanceled) return VERR_CANCELLED;
    /* Set the new process. */
    hrc = pProgress->SetCurrentOperationProgress(uPercentage);
    if (FAILED(hrc)) return VERR_GENERAL_FAILURE;

    return VINF_SUCCESS;
}

/* static */
void MachineMoveVM::i_MoveVMThreadTask(MachineMoveVM *task)
{
    LogFlowFuncEnter();
    HRESULT hrc = S_OK;

    MachineMoveVM *taskMoveVM = task;
    ComObjPtr<Machine> &machine = taskMoveVM->m_pMachine;

    AutoCaller autoCaller(machine);
//  if (FAILED(autoCaller.hrc())) return;//Should we return something here?

    Utf8Str strTargetFolder = taskMoveVM->m_targetPath;
    {
        Bstr bstrMachineName;
        hrc = machine->COMGETTER(Name)(bstrMachineName.asOutParam());
        if (FAILED(hrc))
        {
            taskMoveVM->m_result = hrc;
            if (!taskMoveVM->m_pProgress.isNull())
                taskMoveVM->m_pProgress->i_notifyComplete(taskMoveVM->m_result);
            return;
        }
        strTargetFolder.append(Utf8Str(bstrMachineName));
    }

    RTCList<Utf8Str> newFiles;       /* All extra created files (save states, ...) */
    RTCList<Utf8Str> originalFiles;  /* All original files except images */
    typedef std::map<Utf8Str, ComObjPtr<Medium> > MediumMap;
    MediumMap mapOriginalMedium;

    /*
     * We have the couple modes which user is able to request
     * basic mode:
     * - The images which are solely attached to the VM
     *   and located in the original VM folder will be moved.
     *   All subfolders related to the original VM are also moved from the original location
     *   (Standard - snapshots and logs folders).
     *
     * canonical mode:
     * - All disks tied with the VM will be moved into a new location if it's possible.
     *   All folders related to the original VM are also moved.
     * This mode is intended to collect all files/images/snapshots related to the VM in the one place.
     *
     */

    /*
     * A way to handle shareable disk:
     * Collect the shareable disks attched to the VM.
     * Get the machines whom the shareable disks attach to.
     * Return an error if the state of any VM doesn't allow to move a shareable disk and
     * this disk is located in the VM's folder (it means the disk is intended for "moving").
     */


    /*
     * Check new destination whether enough room for the VM or not. if "not" return an error.
     * Make a copy of VM settings and a list with all files which are moved. Save the list on the disk.
     * Start "move" operation.
     * Check the result of operation.
     * if the operation was successful:
     * - delete all files in the original VM folder;
     * - update VM disks info with new location;
     * - update all other VM if it's needed;
     * - update global settings
     */

    try
    {
        /* Move all disks */
        hrc = taskMoveVM->moveAllDisks(taskMoveVM->m_finalMediaMap, strTargetFolder);
        if (FAILED(hrc))
            throw hrc;

        /* Get Machine::Data here because moveAllDisks() change it */
        Machine::Data *machineData = machine->mData.data();
        settings::MachineConfigFile *machineConfFile = machineData->pMachineConfigFile;

        /* Copy all save state files. */
        Utf8Str strTrgSnapshotFolder;
        {
            /* When the current snapshot folder is absolute we reset it to the
             * default relative folder. */
            if (RTPathStartsWithRoot(machineConfFile->machineUserData.strSnapshotFolder.c_str()))
                machineConfFile->machineUserData.strSnapshotFolder = "Snapshots";
            machineConfFile->strStateFile = "";

            /* The absolute name of the snapshot folder. */
            strTrgSnapshotFolder = Utf8StrFmt("%s%c%s", strTargetFolder.c_str(), RTPATH_DELIMITER,
                                              machineConfFile->machineUserData.strSnapshotFolder.c_str());

            /* Check if a snapshot folder is necessary and if so doesn't already
             * exists. */
            if (   (   taskMoveVM->m_finalSaveStateFilesMap.size() > 0
                    || taskMoveVM->m_finalNVRAMFilesMap.size() > 1)
                && !RTDirExists(strTrgSnapshotFolder.c_str()))
            {
                int vrc = RTDirCreateFullPath(strTrgSnapshotFolder.c_str(), 0700);
                if (RT_FAILURE(vrc))
                    throw machine->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                                tr("Could not create snapshots folder '%s' (%Rrc)"),
                                                strTrgSnapshotFolder.c_str(), vrc);
            }

            std::map<Utf8Str, SNAPFILETASKMOVE>::iterator itState = taskMoveVM->m_finalSaveStateFilesMap.begin();
            while (itState != taskMoveVM->m_finalSaveStateFilesMap.end())
            {
                const SNAPFILETASKMOVE &sft = itState->second;
                const Utf8Str &strTrgSaveState = Utf8StrFmt("%s%c%s", strTrgSnapshotFolder.c_str(), RTPATH_DELIMITER,
                                                            RTPathFilename(sft.strFile.c_str()));

                /* Move to next sub-operation. */
                hrc = taskMoveVM->m_pProgress->SetNextOperation(BstrFmt(tr("Copying the save state file '%s' ..."),
                                                                        RTPathFilename(sft.strFile.c_str())).raw(),
                                                                sft.uWeight);
                if (FAILED(hrc))
                    throw hrc;

                int vrc = RTFileCopyEx(sft.strFile.c_str(), strTrgSaveState.c_str(), 0,
                                       MachineMoveVM::copyFileProgress, &taskMoveVM->m_pProgress);
                if (RT_FAILURE(vrc))
                    throw machine->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                                tr("Could not copy state file '%s' to '%s' (%Rrc)"),
                                                sft.strFile.c_str(),
                                                strTrgSaveState.c_str(),
                                                vrc);

                /* save new file in case of restoring */
                newFiles.append(strTrgSaveState);
                /* save original file for deletion in the end */
                originalFiles.append(sft.strFile);
                ++itState;
            }

            std::map<Utf8Str, SNAPFILETASKMOVE>::iterator itNVRAM = taskMoveVM->m_finalNVRAMFilesMap.begin();
            while (itNVRAM != taskMoveVM->m_finalNVRAMFilesMap.end())
            {
                const SNAPFILETASKMOVE &sft = itNVRAM->second;
                const Utf8Str &strTrgNVRAM = Utf8StrFmt("%s%c%s", sft.snapshotUuid.isZero() ? strTargetFolder.c_str() : strTrgSnapshotFolder.c_str(),
                                                        RTPATH_DELIMITER, RTPathFilename(sft.strFile.c_str()));

                /* Move to next sub-operation. */
                hrc = taskMoveVM->m_pProgress->SetNextOperation(BstrFmt(tr("Copying the NVRAM file '%s' ..."),
                                                                        RTPathFilename(sft.strFile.c_str())).raw(),
                                                                sft.uWeight);
                if (FAILED(hrc))
                    throw hrc;

                int vrc = RTFileCopyEx(sft.strFile.c_str(), strTrgNVRAM.c_str(), 0,
                                       MachineMoveVM::copyFileProgress, &taskMoveVM->m_pProgress);
                if (RT_FAILURE(vrc))
                    throw machine->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                                tr("Could not copy NVRAM file '%s' to '%s' (%Rrc)"),
                                                sft.strFile.c_str(),
                                                strTrgNVRAM.c_str(),
                                                vrc);

                /* save new file in case of restoring */
                newFiles.append(strTrgNVRAM);
                /* save original file for deletion in the end */
                originalFiles.append(sft.strFile);
                ++itNVRAM;
            }
        }

        /*
         * Update state file path
         * very important step!
         */
        Log2(("Update state file path\n"));
        /** @todo r=klaus: this update is not necessarily matching what the
         * above code has set as the new folders, so it needs reimplementing */
        taskMoveVM->updatePathsToStateFiles(taskMoveVM->m_vmFolders[VBox_MachineFolder],
                                            strTargetFolder);

        /*
         * Update NVRAM file paths
         * very important step!
         */
        Log2(("Update NVRAM paths\n"));
        /** @todo r=klaus: this update is not necessarily matching what the
         * above code has set as the new folders, so it needs reimplementing.
         * What's good about this implementation: it does not look at the
         * list of NVRAM files, because that only lists the existing ones,
         * but all paths need fixing. */
        taskMoveVM->updatePathsToNVRAMFiles(taskMoveVM->m_vmFolders[VBox_MachineFolder],
                                            strTargetFolder);

        /*
         * Moving Machine settings file
         * The settings file are moved after all disks and snapshots because this file should be updated
         * with actual information and only then should be moved.
         */
        {
            Log2(("Copy Machine settings file\n"));

            hrc = taskMoveVM->m_pProgress->SetNextOperation(BstrFmt(tr("Copying Machine settings file '%s' ..."),
                                                                    machineConfFile->machineUserData.strName.c_str()).raw(),
                                                            1);
            if (FAILED(hrc))
                throw hrc;

            Utf8Str strTargetSettingsFilePath = strTargetFolder;

            /* Check a folder existing and create one if it's not */
            if (!RTDirExists(strTargetSettingsFilePath.c_str()))
            {
                int vrc = RTDirCreateFullPath(strTargetSettingsFilePath.c_str(), 0700);
                if (RT_FAILURE(vrc))
                    throw machine->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                                tr("Could not create home machine folder '%s' (%Rrc)"),
                                                strTargetSettingsFilePath.c_str(), vrc);

                Log2(("Created a home machine folder %s\n", strTargetSettingsFilePath.c_str()));
            }

            /* Create a full path */
            Bstr bstrMachineName;
            hrc = machine->COMGETTER(Name)(bstrMachineName.asOutParam());
            if (FAILED(hrc))
                throw hrc;
            strTargetSettingsFilePath.append(RTPATH_DELIMITER).append(Utf8Str(bstrMachineName));
            strTargetSettingsFilePath.append(".vbox");

            Utf8Str strSettingsFilePath;
            Bstr bstr_settingsFilePath;
            hrc = machine->COMGETTER(SettingsFilePath)(bstr_settingsFilePath.asOutParam());
            if (FAILED(hrc))
                throw hrc;
            strSettingsFilePath = bstr_settingsFilePath;

            int vrc = RTFileCopyEx(strSettingsFilePath.c_str(), strTargetSettingsFilePath.c_str(), 0,
                                   MachineMoveVM::copyFileProgress, &taskMoveVM->m_pProgress);
            if (RT_FAILURE(vrc))
                throw machine->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                            tr("Could not copy the setting file '%s' to '%s' (%Rrc)"),
                                            strSettingsFilePath.c_str(),
                                            strTargetSettingsFilePath.stripFilename().c_str(),
                                            vrc);

            Log2(("The setting file %s has been copied into the folder %s\n",
                  strSettingsFilePath.c_str(), strTargetSettingsFilePath.stripFilename().c_str()));

            /* save new file in case of restoring */
            newFiles.append(strTargetSettingsFilePath);
            /* save original file for deletion in the end */
            originalFiles.append(strSettingsFilePath);

            Utf8Str strPrevSettingsFilePath = strSettingsFilePath;
            strPrevSettingsFilePath.append("-prev");
            if (RTFileExists(strPrevSettingsFilePath.c_str()))
                originalFiles.append(strPrevSettingsFilePath);
        }

        /* Moving Machine log files */
        {
            Log2(("Copy machine log files\n"));

            if (taskMoveVM->m_vmFolders[VBox_LogFolder].isNotEmpty())
            {
                /* Check an original log folder existence */
                if (RTDirExists(taskMoveVM->m_vmFolders[VBox_LogFolder].c_str()))
                {
                    Utf8Str strTargetLogFolderPath = strTargetFolder;
                    strTargetLogFolderPath.append(RTPATH_DELIMITER).append("Logs");

                    /* Check a destination log folder existence and create one if it's not */
                    if (!RTDirExists(strTargetLogFolderPath.c_str()))
                    {
                        int vrc = RTDirCreateFullPath(strTargetLogFolderPath.c_str(), 0700);
                        if (RT_FAILURE(vrc))
                            throw machine->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                                        tr("Could not create log folder '%s' (%Rrc)"),
                                                        strTargetLogFolderPath.c_str(), vrc);

                        Log2(("Created a log machine folder %s\n", strTargetLogFolderPath.c_str()));
                    }

                    fileList_t filesList;
                    taskMoveVM->getFilesList(taskMoveVM->m_vmFolders[VBox_LogFolder], filesList);
                    cit_t it = filesList.m_list.begin();
                    while (it != filesList.m_list.end())
                    {
                        Utf8Str strFullSourceFilePath = it->first.c_str();
                        strFullSourceFilePath.append(RTPATH_DELIMITER).append(it->second.c_str());

                        Utf8Str strFullTargetFilePath = strTargetLogFolderPath;
                        strFullTargetFilePath.append(RTPATH_DELIMITER).append(it->second.c_str());

                        /* Move to next sub-operation. */
                        hrc = taskMoveVM->m_pProgress->SetNextOperation(BstrFmt(tr("Copying log file '%s' ..."),
                                                                                RTPathFilename(strFullSourceFilePath.c_str())).raw(),
                                                                       1);
                        if (FAILED(hrc))
                            throw hrc;

                        int vrc = RTFileCopyEx(strFullSourceFilePath.c_str(), strFullTargetFilePath.c_str(), 0,
                                               MachineMoveVM::copyFileProgress, &taskMoveVM->m_pProgress);
                        if (RT_FAILURE(vrc))
                            throw machine->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                                        tr("Could not copy log file '%s' to '%s' (%Rrc)"),
                                                        strFullSourceFilePath.c_str(),
                                                        strFullTargetFilePath.stripFilename().c_str(),
                                                        vrc);

                        Log2(("The log file %s has been copied into the folder %s\n", strFullSourceFilePath.c_str(),
                              strFullTargetFilePath.stripFilename().c_str()));

                        /* save new file in case of restoring */
                        newFiles.append(strFullTargetFilePath);
                        /* save original file for deletion in the end */
                        originalFiles.append(strFullSourceFilePath);

                        ++it;
                    }
                }
            }
        }

        /* save all VM data */
        hrc = machine->SaveSettings();
        if (FAILED(hrc))
            throw hrc;

        Log2(("Update path to XML setting file\n"));
        Utf8Str strTargetSettingsFilePath = strTargetFolder;
        Bstr bstrMachineName;
        hrc = machine->COMGETTER(Name)(bstrMachineName.asOutParam());
        if (FAILED(hrc))
            throw hrc;
        strTargetSettingsFilePath.append(RTPATH_DELIMITER).append(Utf8Str(bstrMachineName)).append(".vbox");
        machineData->m_strConfigFileFull = strTargetSettingsFilePath;
        machine->mParent->i_copyPathRelativeToConfig(strTargetSettingsFilePath, machineData->m_strConfigFile);

        /* Marks the global registry for uuid as modified */
        Guid uuid = machine->mParent->i_getGlobalRegistryId();
        machine->mParent->i_markRegistryModified(uuid);

        /* for saving the global settings we should hold only the VirtualBox lock */
        AutoWriteLock vboxLock(machine->mParent COMMA_LOCKVAL_SRC_POS);

        /* Save global settings in the VirtualBox.xml */
        hrc = machine->mParent->i_saveSettings();
        if (FAILED(hrc))
            throw hrc;
    }
    catch(HRESULT aRc)
    {
        hrc = aRc;
        taskMoveVM->m_result = hrc;
    }
    catch (...)
    {
        Log2(("Moving machine to a new destination was failed. Check original and destination places.\n"));
        hrc = VirtualBoxBase::handleUnexpectedExceptions(machine, RT_SRC_POS);
        taskMoveVM->m_result = hrc;
    }

    /* Cleanup on failure */
    if (FAILED(hrc))
    {
        Machine::Data *machineData = machine->mData.data();

        /* Restoring the original media */
        try
        {
            /*
             * Fix the progress counter
             * In instance, the whole "move vm" operation is failed on 5th step. But total count is 20.
             * Where 20 = 2 * 10 operations, where 10 is the real number of operations. And this value was doubled
             * earlier in the init() exactly for one reason - rollback operation. Because in this case we must do
             * the same operations but in backward direction.
             * Thus now we want to correct the progress counter from 5 to 15. Why?
             * Because we should have evaluated the counter as "20/2 + (20/2 - 5)" = 15 or just "20 - 5" = 15
             * And because the 5th step failed it shouldn't be counted.
             * As result, we need to rollback 4 operations.
             * Thus we start from "operation + 1" and finish when "i < operationCount - operation".
             */

            /** @todo r=vvp: Do we need to check each return result here? Looks excessively
             *  and what to do with any failure here? We are already in the rollback action.
             *  Throw only the important errors?
             *  We MUST finish this action anyway to avoid garbage and get the original VM state. */
            /* ! Apparently we should update the Progress object !*/
            ULONG operationCount = 0;
            hrc = taskMoveVM->m_pProgress->COMGETTER(OperationCount)(&operationCount);
            if (FAILED(hrc))
                throw hrc;
            ULONG operation = 0;
            hrc = taskMoveVM->m_pProgress->COMGETTER(Operation)(&operation);
            if (FAILED(hrc))
                throw hrc;
            Bstr bstrOperationDescription;
            hrc = taskMoveVM->m_pProgress->COMGETTER(OperationDescription)(bstrOperationDescription.asOutParam());
            if (FAILED(hrc))
                throw hrc;
            Utf8Str strOperationDescription = bstrOperationDescription;
            ULONG operationPercent = 0;
            hrc = taskMoveVM->m_pProgress->COMGETTER(OperationPercent)(&operationPercent);
            if (FAILED(hrc))
                throw hrc;
            Bstr bstrMachineName;
            hrc = machine->COMGETTER(Name)(bstrMachineName.asOutParam());
            if (FAILED(hrc))
                throw hrc;

            Log2(("Moving machine %s was failed on operation %s\n",
                  Utf8Str(bstrMachineName.raw()).c_str(), Utf8Str(bstrOperationDescription.raw()).c_str()));

            for (ULONG i = operation + 1; i < operationCount - operation; ++i)
                taskMoveVM->m_pProgress->SetNextOperation(BstrFmt(tr("Skipping empty operation %d..."), i + 1).raw(), 1);

            hrc = taskMoveVM->moveAllDisks(taskMoveVM->m_finalMediaMap);
            if (FAILED(hrc))
                throw hrc;

            /* Revert original paths to the state files */
            taskMoveVM->updatePathsToStateFiles(strTargetFolder,
                                                taskMoveVM->m_vmFolders[VBox_MachineFolder]);

            /* Revert original paths to the NVRAM files */
            taskMoveVM->updatePathsToNVRAMFiles(strTargetFolder,
                                                taskMoveVM->m_vmFolders[VBox_MachineFolder]);

            /* Delete all created files. Here we update progress object */
            hrc = taskMoveVM->deleteFiles(newFiles);
            if (FAILED(hrc))
            {
                Log2(("Rollback scenario: can't delete new created files. Check the destination folder.\n"));
                throw hrc;
            }

            /* Delete destination folder */
            int vrc = RTDirRemove(strTargetFolder.c_str());
            if (RT_FAILURE(vrc))
            {
                Log2(("Rollback scenario: can't delete new destination folder.\n"));
                throw machine->setErrorVrc(vrc, tr("Rollback scenario: can't delete new destination folder."));
            }

            /* save all VM data */
            {
                AutoWriteLock  srcLock(machine COMMA_LOCKVAL_SRC_POS);
                srcLock.release();
                hrc = machine->SaveSettings();
                if (FAILED(hrc))
                {
                    Log2(("Rollback scenario: can't save machine settings.\n"));
                    throw hrc;
                }
                srcLock.acquire();
            }

            /* Restore an original path to XML setting file */
            {
                Log2(("Rollback scenario: restoration of the original path to XML setting file\n"));
                Utf8Str strOriginalSettingsFilePath = taskMoveVM->m_vmFolders[VBox_MachineFolder];
                strOriginalSettingsFilePath.append(RTPATH_DELIMITER).append(Utf8Str(bstrMachineName)).append(".vbox");
                machineData->m_strConfigFileFull = strOriginalSettingsFilePath;
                machine->mParent->i_copyPathRelativeToConfig(strOriginalSettingsFilePath, machineData->m_strConfigFile);
            }

            /* Marks the global registry for uuid as modified */
            {
                AutoWriteLock  srcLock(machine COMMA_LOCKVAL_SRC_POS);
                srcLock.release();
                Guid uuid = machine->mParent->i_getGlobalRegistryId();
                machine->mParent->i_markRegistryModified(uuid);
                srcLock.acquire();
            }

            /* save the global settings; for that we should hold only the VirtualBox lock */
            {
                AutoWriteLock vboxLock(machine->mParent COMMA_LOCKVAL_SRC_POS);
                hrc = machine->mParent->i_saveSettings();
                if (FAILED(hrc))
                {
                    Log2(("Rollback scenario: can't save global settings.\n"));
                    throw hrc;
                }
            }
        }
        catch(HRESULT aRc)
        {
            hrc = aRc;
            Log2(("Rollback scenario: restoration the original media failed. Machine can be corrupted.\n"));
        }
        catch (...)
        {
            Log2(("Rollback scenario: restoration the original media failed. Machine can be corrupted.\n"));
            hrc = VirtualBoxBase::handleUnexpectedExceptions(machine, RT_SRC_POS);
        }
        /* In case of failure the progress object on the other side (user side) get notification about operation
           completion but the operation percentage may not be set to 100% */
    }
    else /*Operation was successful and now we can delete the original files like the state files, XML setting, log files */
    {
        /*
         * In case of success it's not urgent to update the progress object because we call i_notifyComplete() with
         * the success result. As result, the last number of progress operation can be not equal the number of operations
         * because we doubled the number of operations for rollback case.
         * But if we want to update the progress object corectly it's needed to add all medium moved by standard
         * "move medium" logic (for us it's taskMoveVM->m_finalMediaMap) to the current number of operation.
         */

        ULONG operationCount = 0;
        hrc = taskMoveVM->m_pProgress->COMGETTER(OperationCount)(&operationCount);
        ULONG operation = 0;
        hrc = taskMoveVM->m_pProgress->COMGETTER(Operation)(&operation);

        for (ULONG i = operation; i < operation + taskMoveVM->m_finalMediaMap.size() - 1; ++i)
            taskMoveVM->m_pProgress->SetNextOperation(BstrFmt(tr("Skipping empty operation %d..."), i).raw(), 1);

        hrc = taskMoveVM->deleteFiles(originalFiles);
        if (FAILED(hrc))
            Log2(("Forward scenario: can't delete all original files.\n"));

        /* delete no longer needed source directories */
        if (   taskMoveVM->m_vmFolders[VBox_SnapshotFolder].isNotEmpty()
            && RTDirExists(taskMoveVM->m_vmFolders[VBox_SnapshotFolder].c_str()))
            RTDirRemove(taskMoveVM->m_vmFolders[VBox_SnapshotFolder].c_str());

        if (   taskMoveVM->m_vmFolders[VBox_LogFolder].isNotEmpty()
            && RTDirExists(taskMoveVM->m_vmFolders[VBox_LogFolder].c_str()))
            RTDirRemove(taskMoveVM->m_vmFolders[VBox_LogFolder].c_str());

        if (   taskMoveVM->m_vmFolders[VBox_MachineFolder].isNotEmpty()
            && RTDirExists(taskMoveVM->m_vmFolders[VBox_MachineFolder].c_str()))
            RTDirRemove(taskMoveVM->m_vmFolders[VBox_MachineFolder].c_str());
    }

    if (!taskMoveVM->m_pProgress.isNull())
        taskMoveVM->m_pProgress->i_notifyComplete(taskMoveVM->m_result);

    LogFlowFuncLeave();
}

HRESULT MachineMoveVM::moveAllDisks(const std::map<Utf8Str, MEDIUMTASKMOVE> &listOfDisks,
                                    const Utf8Str &strTargetFolder)
{
    HRESULT hrc = S_OK;
    ComObjPtr<Machine> &machine = m_pMachine;
    Utf8Str strLocation;

    AutoWriteLock  machineLock(machine COMMA_LOCKVAL_SRC_POS);

    try
    {
        std::map<Utf8Str, MEDIUMTASKMOVE>::const_iterator itMedium = listOfDisks.begin();
        while (itMedium != listOfDisks.end())
        {
            const MEDIUMTASKMOVE &mt = itMedium->second;
            ComPtr<IMedium> pMedium = mt.pMedium;
            Utf8Str strTargetImageName;
            Bstr bstrLocation;
            Bstr bstrSrcName;

            hrc = pMedium->COMGETTER(Name)(bstrSrcName.asOutParam());
            if (FAILED(hrc)) throw hrc;

            if (strTargetFolder.isNotEmpty())
            {
                strTargetImageName = strTargetFolder;
                hrc = pMedium->COMGETTER(Location)(bstrLocation.asOutParam());
                if (FAILED(hrc)) throw hrc;
                strLocation = bstrLocation;

                if (mt.fSnapshot == true)
                    strLocation.stripFilename().stripPath().append(RTPATH_DELIMITER).append(Utf8Str(bstrSrcName));
                else
                    strLocation.stripPath();

                strTargetImageName.append(RTPATH_DELIMITER).append(strLocation);
                hrc = m_pProgress->SetNextOperation(BstrFmt(tr("Moving medium '%ls' ..."), bstrSrcName.raw()).raw(), mt.uWeight);
                if (FAILED(hrc)) throw hrc;
            }
            else
            {
                strTargetImageName = mt.strBaseName;//Should contain full path to the image
                hrc = m_pProgress->SetNextOperation(BstrFmt(tr("Moving medium '%ls' back..."), bstrSrcName.raw()).raw(), mt.uWeight);
                if (FAILED(hrc)) throw hrc;
            }



            /* consistency: use \ if appropriate on the platform */
            RTPathChangeToDosSlashes(strTargetImageName.mutableRaw(), false);

            bstrLocation = strTargetImageName.c_str();

            MediumType_T mediumType;//immutable, shared, passthrough
            hrc = pMedium->COMGETTER(Type)(&mediumType);
            if (FAILED(hrc)) throw hrc;

            DeviceType_T deviceType;//floppy, hard, DVD
            hrc = pMedium->COMGETTER(DeviceType)(&deviceType);
            if (FAILED(hrc)) throw hrc;

            /* Drop lock early because IMedium::MoveTo needs to get the VirtualBox one. */
            machineLock.release();

            ComPtr<IProgress> moveDiskProgress;
            hrc = pMedium->MoveTo(bstrLocation.raw(), moveDiskProgress.asOutParam());
            if (SUCCEEDED(hrc))
            {
                /* In case of failure moveDiskProgress would be in the invalid state or not initialized at all
                 * Call i_waitForOtherProgressCompletion only in success
                 */
                /* Wait until the other process has finished. */
                hrc = m_pProgress->WaitForOtherProgressCompletion(moveDiskProgress, 0 /* indefinite wait */);
            }

            /*acquire the lock back*/
            machineLock.acquire();

            if (FAILED(hrc)) throw hrc;

            Log2(("Moving %s has been finished\n", strTargetImageName.c_str()));

            ++itMedium;
        }

        machineLock.release();
    }
    catch (HRESULT hrcXcpt)
    {
        Log2(("Exception during moving the disk %s: %Rhrc\n", strLocation.c_str(), hrcXcpt));
        hrc = hrcXcpt;
        machineLock.release();
    }
    catch (...)
    {
        Log2(("Exception during moving the disk %s\n", strLocation.c_str()));
        hrc = VirtualBoxBase::handleUnexpectedExceptions(m_pMachine, RT_SRC_POS);
        machineLock.release();
    }

    return hrc;
}

void MachineMoveVM::updatePathsToStateFiles(const Utf8Str &sourcePath, const Utf8Str &targetPath)
{
    ComObjPtr<Snapshot> pSnapshot;
    HRESULT hrc = m_pMachine->i_findSnapshotById(Guid() /* zero */, pSnapshot, true);
    if (SUCCEEDED(hrc) && !pSnapshot.isNull())
        pSnapshot->i_updateSavedStatePaths(sourcePath.c_str(),
                                           targetPath.c_str());
    if (m_pMachine->mSSData->strStateFilePath.isNotEmpty())
    {
        if (RTPathStartsWith(m_pMachine->mSSData->strStateFilePath.c_str(), sourcePath.c_str()))
            m_pMachine->mSSData->strStateFilePath = Utf8StrFmt("%s%s",
                                                               targetPath.c_str(),
                                                               m_pMachine->mSSData->strStateFilePath.c_str() + sourcePath.length());
        else
            m_pMachine->mSSData->strStateFilePath = Utf8StrFmt("%s%c%s",
                                                               targetPath.c_str(),
                                                               RTPATH_DELIMITER,
                                                               RTPathFilename(m_pMachine->mSSData->strStateFilePath.c_str()));
    }
}

void MachineMoveVM::updatePathsToNVRAMFiles(const Utf8Str &sourcePath, const Utf8Str &targetPath)
{
    ComObjPtr<Snapshot> pSnapshot;
    HRESULT hrc = m_pMachine->i_findSnapshotById(Guid() /* zero */, pSnapshot, true);
    if (SUCCEEDED(hrc) && !pSnapshot.isNull())
        pSnapshot->i_updateNVRAMPaths(sourcePath.c_str(),
                                      targetPath.c_str());
    ComObjPtr<NvramStore> pNvramStore(m_pMachine->mNvramStore);
    const Utf8Str NVRAMFile(pNvramStore->i_getNonVolatileStorageFile());
    if (   NVRAMFile.isNotEmpty()
        && RTFileExists(NVRAMFile.c_str())
        && m_pMachine->i_getFirmwareType() != FirmwareType_BIOS)
    {
        Utf8Str newNVRAMFile;
        if (RTPathStartsWith(NVRAMFile.c_str(), sourcePath.c_str()))
            newNVRAMFile = Utf8StrFmt("%s%s", targetPath.c_str(), NVRAMFile.c_str() + sourcePath.length());
        else
            newNVRAMFile = Utf8StrFmt("%s%c%s", targetPath.c_str(), RTPATH_DELIMITER, RTPathFilename(newNVRAMFile.c_str()));
        pNvramStore->i_updateNonVolatileStorageFile(newNVRAMFile);
    }
}

HRESULT MachineMoveVM::getFilesList(const Utf8Str &strRootFolder, fileList_t &filesList)
{
    RTDIR hDir;
    HRESULT hrc = S_OK;
    int vrc = RTDirOpen(&hDir, strRootFolder.c_str());
    if (RT_SUCCESS(vrc))
    {
        /** @todo r=bird: RTDIRENTRY is big and this function is doing
         * unrestrained recursion of arbritrary depth.  Three things:
         *      - Add a depth counter parameter and refuse to go deeper than
         *        a certain reasonable limit.
         *      - Split this method into a main and a worker, placing
         *        RTDIRENTRY on the stack in the main and passing it onto to
         *        worker as a parameter.
         *      - RTDirRead may fail for reasons other than
         *        VERR_NO_MORE_FILES.  For instance someone could create an
         *        entry with a name longer than RTDIRENTRY have space to
         *        store (windows host with UTF-16 encoding shorter than 255
         *        chars, but UTF-8 encoding longer than 260). */
        RTDIRENTRY DirEntry;
        while (RT_SUCCESS(RTDirRead(hDir, &DirEntry, NULL)))
        {
            if (RTDirEntryIsStdDotLink(&DirEntry))
                continue;

            /* Make sure we've got the entry type. */
            if (DirEntry.enmType == RTDIRENTRYTYPE_UNKNOWN)
            {
                /* Build the complete path which is needed by RTDirQueryUnknownType(). */
                char szPath[RTPATH_MAX];
                vrc = RTPathJoin(szPath, sizeof(szPath), strRootFolder.c_str(), DirEntry.szName);
                if (RT_SUCCESS(vrc))
                    vrc = RTDirQueryUnknownType(szPath, false /*fFollowSymlinks*/, &DirEntry.enmType);
                if (RT_FAILURE(vrc))
                    continue;
            }

            if (DirEntry.enmType == RTDIRENTRYTYPE_FILE)
            {
                Utf8Str fullPath(strRootFolder);
                filesList.add(strRootFolder, DirEntry.szName);
            }
            else if (DirEntry.enmType == RTDIRENTRYTYPE_DIRECTORY)
            {
                Utf8Str strNextFolder(strRootFolder);
                strNextFolder.append(RTPATH_DELIMITER).append(DirEntry.szName);
                hrc = getFilesList(strNextFolder, filesList);
                if (FAILED(hrc))
                    break;
            }
        }

        vrc = RTDirClose(hDir);
        AssertRC(vrc);
    }
    else if (vrc == VERR_FILE_NOT_FOUND)
        hrc = m_pMachine->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                       tr("Folder '%s' doesn't exist (%Rrc)"),
                                       strRootFolder.c_str(), vrc);
    else
        hrc = m_pMachine->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                       tr("Could not open folder '%s' (%Rrc)"),
                                       strRootFolder.c_str(), vrc);

    return hrc;
}

HRESULT MachineMoveVM::deleteFiles(const RTCList<Utf8Str> &listOfFiles)
{
    HRESULT hrc = S_OK;
    /* Delete all created files. */
    for (size_t i = 0; i < listOfFiles.size(); ++i)
    {
        Log2(("Deleting file %s ...\n", listOfFiles.at(i).c_str()));
        hrc = m_pProgress->SetNextOperation(BstrFmt(tr("Deleting file %s..."), listOfFiles.at(i).c_str()).raw(), 1);
        if (FAILED(hrc)) return hrc;

        int vrc = RTFileDelete(listOfFiles.at(i).c_str());
        if (RT_FAILURE(vrc))
            return m_pMachine->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                            tr("Could not delete file '%s' (%Rrc)"),
                                            listOfFiles.at(i).c_str(), vrc);

        else
            Log2(("File %s has been deleted\n", listOfFiles.at(i).c_str()));
    }

    return hrc;
}

HRESULT MachineMoveVM::getFolderSize(const Utf8Str &strRootFolder, uint64_t &size)
{
    HRESULT hrc = S_OK;
    int vrc = 0;
    uint64_t totalFolderSize = 0;
    fileList_t filesList;

    bool ex = RTPathExists(strRootFolder.c_str());
    if (ex == true)
    {
        hrc = getFilesList(strRootFolder, filesList);
        if (SUCCEEDED(hrc))
        {
            cit_t it = filesList.m_list.begin();
            while (it != filesList.m_list.end())
            {
                uint64_t cbFile = 0;
                Utf8Str fullPath =  it->first;
                fullPath.append(RTPATH_DELIMITER).append(it->second);
                vrc = RTFileQuerySizeByPath(fullPath.c_str(), &cbFile);
                if (RT_SUCCESS(vrc))
                {
                    totalFolderSize += cbFile;
                }
                else
                    return m_pMachine->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                                    tr("Could not get size of file '%s': %Rrc"),
                                                    fullPath.c_str(),
                                                    vrc);

                ++it;
            }

            size = totalFolderSize;
        }
    }
    else
        size = 0;

    return hrc;
}

HRESULT MachineMoveVM::queryBaseName(const ComPtr<IMedium> &pMedium, Utf8Str &strBaseName) const
{
    ComPtr<IMedium> pBaseMedium;
    HRESULT hrc = pMedium->COMGETTER(Base)(pBaseMedium.asOutParam());
    if (FAILED(hrc)) return hrc;
    Bstr bstrBaseName;
    hrc = pBaseMedium->COMGETTER(Name)(bstrBaseName.asOutParam());
    if (FAILED(hrc)) return hrc;
    strBaseName = bstrBaseName;
    return hrc;
}

HRESULT MachineMoveVM::createMachineList(const ComPtr<ISnapshot> &pSnapshot)
{
    Bstr name;
    HRESULT hrc = pSnapshot->COMGETTER(Name)(name.asOutParam());
    if (FAILED(hrc)) return hrc;

    ComPtr<IMachine> l_pMachine;
    hrc = pSnapshot->COMGETTER(Machine)(l_pMachine.asOutParam());
    if (FAILED(hrc)) return hrc;
    machineList.push_back((Machine*)(IMachine*)l_pMachine);

    SafeIfaceArray<ISnapshot> sfaChilds;
    hrc = pSnapshot->COMGETTER(Children)(ComSafeArrayAsOutParam(sfaChilds));
    if (FAILED(hrc)) return hrc;
    for (size_t i = 0; i < sfaChilds.size(); ++i)
    {
        hrc = createMachineList(sfaChilds[i]);
        if (FAILED(hrc)) return hrc;
    }

    return hrc;
}

HRESULT MachineMoveVM::queryMediaForAllStates()
{
    /* In this case we create a exact copy of the original VM. This means just
     * adding all directly and indirectly attached disk images to the worker
     * list. */
    HRESULT hrc = S_OK;
    for (size_t i = 0; i < machineList.size(); ++i)
    {
        const ComObjPtr<Machine> &machine = machineList.at(i);

        /* Add all attachments (and their parents) of the different
         * machines to a worker list. */
        SafeIfaceArray<IMediumAttachment> sfaAttachments;
        hrc = machine->COMGETTER(MediumAttachments)(ComSafeArrayAsOutParam(sfaAttachments));
        if (FAILED(hrc)) return hrc;
        for (size_t a = 0; a < sfaAttachments.size(); ++a)
        {
            const ComPtr<IMediumAttachment> &pAtt = sfaAttachments[a];
            DeviceType_T deviceType;//floppy, hard, DVD
            hrc = pAtt->COMGETTER(Type)(&deviceType);
            if (FAILED(hrc)) return hrc;

            /* Valid medium attached? */
            ComPtr<IMedium> pMedium;
            hrc = pAtt->COMGETTER(Medium)(pMedium.asOutParam());
            if (FAILED(hrc)) return hrc;

            if (pMedium.isNull())
                continue;

            Bstr bstrLocation;
            hrc = pMedium->COMGETTER(Location)(bstrLocation.asOutParam());
            if (FAILED(hrc)) return hrc;

            /* Cast to ComObjPtr<Medium> */
            ComObjPtr<Medium> pObjMedium = (Medium *)(IMedium *)pMedium;

            /* Check for "read-only" medium in terms that VBox can't create this one */
            hrc = isMediumTypeSupportedForMoving(pMedium);
            if (FAILED(hrc))
            {
                if (hrc != S_FALSE)
                    return hrc;
                Log2(("Skipping file %ls because of this medium type hasn't been supported for moving.\n", bstrLocation.raw()));
                continue;
            }

            MEDIUMTASKCHAINMOVE mtc;
            mtc.devType       = deviceType;
            mtc.fAttachLinked = false;
            mtc.fCreateDiffs  = false;

            while (!pMedium.isNull())
            {
                /* Refresh the state so that the file size get read. */
                MediumState_T e;
                hrc = pMedium->RefreshState(&e);
                if (FAILED(hrc)) return hrc;

                LONG64 lSize;
                hrc = pMedium->COMGETTER(Size)(&lSize);
                if (FAILED(hrc)) return hrc;

                MediumType_T mediumType;//immutable, shared, passthrough
                hrc = pMedium->COMGETTER(Type)(&mediumType);
                if (FAILED(hrc)) return hrc;

                hrc = pMedium->COMGETTER(Location)(bstrLocation.asOutParam());
                if (FAILED(hrc)) return hrc;

                MEDIUMTASKMOVE mt;// = {false, "basename", NULL, 0, 0};
                mt.strBaseName = bstrLocation;
                Utf8Str const &strFolder = m_vmFolders[VBox_SnapshotFolder];

                if (strFolder.isNotEmpty() && RTPathStartsWith(mt.strBaseName.c_str(), strFolder.c_str()))
                    mt.fSnapshot = true;
                else
                    mt.fSnapshot = false;

                mt.uIdx    = UINT32_MAX;
                mt.pMedium = pMedium;
                mt.uWeight = (ULONG)((lSize + _1M - 1) / _1M);
                mtc.chain.append(mt);

                /* Query next parent. */
                hrc = pMedium->COMGETTER(Parent)(pMedium.asOutParam());
                if (FAILED(hrc)) return hrc;
            }

            m_llMedia.append(mtc);
        }

        /* Add the save state files of this machine if there is one. */
        hrc = addSaveState(machine);
        if (FAILED(hrc)) return hrc;

        /* Add the NVRAM files of this machine if there is one. */
        hrc = addNVRAM(machine);
        if (FAILED(hrc)) return hrc;
    }

    /* Build up the index list of the image chain. Unfortunately we can't do
     * that in the previous loop, cause there we go from child -> parent and
     * didn't know how many are between. */
    for (size_t i = 0; i < m_llMedia.size(); ++i)
    {
        uint32_t uIdx = 0;
        MEDIUMTASKCHAINMOVE &mtc = m_llMedia.at(i);
        for (size_t a = mtc.chain.size(); a > 0; --a)
            mtc.chain[a - 1].uIdx = uIdx++;
    }

    return hrc;
}

HRESULT MachineMoveVM::addSaveState(const ComObjPtr<Machine> &machine)
{
    Bstr bstrSrcSaveStatePath;
    HRESULT hrc = machine->COMGETTER(StateFilePath)(bstrSrcSaveStatePath.asOutParam());
    if (FAILED(hrc)) return hrc;
    if (!bstrSrcSaveStatePath.isEmpty())
    {
        SNAPFILETASKMOVE sft;

        sft.snapshotUuid = machine->i_getSnapshotId();
        sft.strFile = bstrSrcSaveStatePath;
        uint64_t cbSize;

        int vrc = RTFileQuerySizeByPath(sft.strFile.c_str(), &cbSize);
        if (RT_FAILURE(vrc))
            return m_pMachine->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                            tr("Could not get size of file '%s': %Rrc"),
                                            sft.strFile.c_str(),
                                            vrc);

        /* same rule as above: count both the data which needs to
         * be read and written */
        sft.uWeight = (ULONG)(2 * (cbSize + _1M - 1) / _1M);
        m_llSaveStateFiles.append(sft);
    }
    return S_OK;
}

HRESULT MachineMoveVM::addNVRAM(const ComObjPtr<Machine> &machine)
{
    ComPtr<INvramStore> pNvramStore;
    HRESULT hrc = machine->COMGETTER(NonVolatileStore)(pNvramStore.asOutParam());
    if (FAILED(hrc)) return hrc;
    Bstr bstrSrcNVRAMPath;
    hrc = pNvramStore->COMGETTER(NonVolatileStorageFile)(bstrSrcNVRAMPath.asOutParam());
    if (FAILED(hrc)) return hrc;
    Utf8Str strSrcNVRAMPath(bstrSrcNVRAMPath);
    if (!strSrcNVRAMPath.isEmpty() && RTFileExists(strSrcNVRAMPath.c_str()))
    {
        SNAPFILETASKMOVE sft;

        sft.snapshotUuid = machine->i_getSnapshotId();
        sft.strFile = strSrcNVRAMPath;
        uint64_t cbSize;

        int vrc = RTFileQuerySizeByPath(sft.strFile.c_str(), &cbSize);
        if (RT_FAILURE(vrc))
            return m_pMachine->setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                            tr("Could not get size of file '%s': %Rrc"),
                                            sft.strFile.c_str(),
                                            vrc);

        /* same rule as above: count both the data which needs to
         * be read and written */
        sft.uWeight = (ULONG)(2 * (cbSize + _1M - 1) / _1M);
        m_llNVRAMFiles.append(sft);
    }
    return S_OK;
}

void MachineMoveVM::updateProgressStats(MEDIUMTASKCHAINMOVE &mtc, ULONG &uCount, ULONG &uTotalWeight) const
{

    /* Currently the copying of diff images involves reading at least
     * the biggest parent in the previous chain. So even if the new
     * diff image is small in size, it could need some time to create
     * it. Adding the biggest size in the chain should balance this a
     * little bit more, i.e. the weight is the sum of the data which
     * needs to be read and written. */
    ULONG uMaxWeight = 0;
    for (size_t e = mtc.chain.size(); e > 0; --e)
    {
        MEDIUMTASKMOVE &mt = mtc.chain.at(e - 1);
        mt.uWeight += uMaxWeight;

        /* Calculate progress data */
        ++uCount;
        uTotalWeight += mt.uWeight;

        /* Save the max size for better weighting of diff image
         * creation. */
        uMaxWeight = RT_MAX(uMaxWeight, mt.uWeight);
    }
}

HRESULT MachineMoveVM::isMediumTypeSupportedForMoving(const ComPtr<IMedium> &pMedium)
{
    Bstr bstrLocation;
    HRESULT hrc = pMedium->COMGETTER(Location)(bstrLocation.asOutParam());
    if (FAILED(hrc))
        return hrc;

    DeviceType_T deviceType;
    hrc = pMedium->COMGETTER(DeviceType)(&deviceType);
    if (FAILED(hrc))
        return hrc;

    ComPtr<IMediumFormat> mediumFormat;
    hrc = pMedium->COMGETTER(MediumFormat)(mediumFormat.asOutParam());
    if (FAILED(hrc))
        return hrc;

    /* Check whether VBox is able to create this medium format or not, i.e. medium can be "read-only" */
    Bstr bstrFormatName;
    hrc = mediumFormat->COMGETTER(Name)(bstrFormatName.asOutParam());
    if (FAILED(hrc))
        return hrc;

    Utf8Str formatName = Utf8Str(bstrFormatName);
    if (formatName.compare("VHDX", Utf8Str::CaseInsensitive) == 0)
    {
        Log2(("Skipping medium %ls. VHDX format is supported in \"read-only\" mode only.\n", bstrLocation.raw()));
        return S_FALSE;
    }

    /* Check whether medium is represented by file on the disk  or not */
    ComObjPtr<Medium> pObjMedium = (Medium *)(IMedium *)pMedium;
    if (!pObjMedium->i_isMediumFormatFile())
    {
        Log2(("Skipping medium %ls because it's not a real file on the disk.\n", bstrLocation.raw()));
        return S_FALSE;
    }

    /* some special checks for DVD */
    if (deviceType == DeviceType_DVD)
    {
        Utf8Str ext = bstrLocation;
        /* only ISO image is moved */
        if (!ext.endsWith(".iso", Utf8Str::CaseInsensitive))
        {
            Log2(("Skipping file %ls. Only ISO images are supported for now.\n", bstrLocation.raw()));
            return S_FALSE;
        }
    }

    return S_OK;
}
