/* $Id: MachineImplMoveVM.h 108960 2025-03-26 13:45:20Z brent.paulson@oracle.com $ */
/** @file
 * Definition of MachineMoveVM
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

#ifndef MAIN_INCLUDED_MachineImplMoveVM_h
#define MAIN_INCLUDED_MachineImplMoveVM_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "MachineImpl.h"
#include "ProgressImpl.h"
#include "ThreadTask.h"

/////////////////////////////////////////////////////////////////////////////

enum VBoxFolder_t
{
    VBox_UnknownFolderType = 0,
    VBox_OutsideVMfolder,
    VBox_MachineFolder,
    VBox_LogFolder,
    VBox_StateFolder,
    VBox_SnapshotFolder
};

typedef struct
{
    bool                    fSnapshot;
    Utf8Str                 strBaseName;
    ComPtr<IMedium>         pMedium;
    uint32_t                uIdx;
    ULONG                   uWeight;
} MEDIUMTASKMOVE;

typedef struct
{
    RTCList<MEDIUMTASKMOVE> chain;
    DeviceType_T            devType;
    bool                    fCreateDiffs;
    bool                    fAttachLinked;
} MEDIUMTASKCHAINMOVE;

typedef struct
{
    Guid                    snapshotUuid;
    Utf8Str                 strFile;
    ULONG                   uWeight;
} SNAPFILETASKMOVE;

struct fileList_t;

class MachineMoveVM : public ThreadTask
{
    std::vector<ComObjPtr<Machine> >        machineList;
    RTCList<MEDIUMTASKCHAINMOVE>            m_llMedia;
    RTCList<SNAPFILETASKMOVE>               m_llSaveStateFiles;
    RTCList<SNAPFILETASKMOVE>               m_llNVRAMFiles;
    std::map<Utf8Str, MEDIUMTASKMOVE>       m_finalMediaMap;
    std::map<Utf8Str, SNAPFILETASKMOVE>     m_finalSaveStateFilesMap;
    std::map<Utf8Str, SNAPFILETASKMOVE>     m_finalNVRAMFilesMap;
    std::map<VBoxFolder_t, Utf8Str>         m_vmFolders;

    ComObjPtr<Machine>  m_pMachine;
    ComObjPtr<Progress> m_pProgress;
    ComObjPtr<Progress> m_pRollBackProgress;
    Utf8Str             m_targetPath;
    Utf8Str             m_type;
    HRESULT             m_result;

public:
    DECLARE_TRANSLATE_METHODS(MachineMoveVM)

    MachineMoveVM(ComObjPtr<Machine> aMachine,
                  const com::Utf8Str &aTargetPath,
                  const com::Utf8Str &aType,
                  ComObjPtr<Progress> &aProgress)
        : ThreadTask("TaskMoveVM")
        , m_pMachine(aMachine)
        , m_pProgress(aProgress)
        , m_targetPath(aTargetPath)
        , m_type(aType.isEmpty() ? "basic" : aType)
        , m_result(S_OK)
    {
    }

    virtual ~MachineMoveVM()
    {
    }

    HRESULT init();
private:
    static DECLCALLBACK(int) updateProgress(unsigned uPercent, void *pvUser);
    static DECLCALLBACK(int) copyFileProgress(unsigned uPercentage, void *pvUser);
    static void i_MoveVMThreadTask(MachineMoveVM *task);

public:
    void handler()
    {
        i_MoveVMThreadTask(this);
    }

private:
    HRESULT createMachineList(const ComPtr<ISnapshot> &pSnapshot);
    inline HRESULT queryBaseName(const ComPtr<IMedium> &pMedium, Utf8Str &strBaseName) const;
    HRESULT queryMediaForAllStates();
    void updateProgressStats(MEDIUMTASKCHAINMOVE &mtc, ULONG &uCount, ULONG &uTotalWeight) const;
    HRESULT addSaveState(const ComObjPtr<Machine> &machine);
    HRESULT addNVRAM(const ComObjPtr<Machine> &machine);
    void printStateFile(settings::SnapshotsList &snl);
    HRESULT getFilesList(const Utf8Str &strRootFolder, fileList_t &filesList);
    HRESULT getFolderSize(const Utf8Str &strRootFolder, uint64_t &size);
    HRESULT deleteFiles(const RTCList<Utf8Str> &listOfFiles);
    void updatePathsToStateFiles(const Utf8Str &sourcePath, const Utf8Str &targetPath);
    void updatePathsToNVRAMFiles(const Utf8Str &sourcePath, const Utf8Str &targetPath);
    HRESULT moveAllDisks(const std::map<Utf8Str, MEDIUMTASKMOVE> &listOfDisks, const Utf8Str &strTargetFolder = Utf8Str::Empty);
    HRESULT restoreAllDisks(const std::map<Utf8Str, MEDIUMTASKMOVE> &listOfDisks);
    HRESULT isMediumTypeSupportedForMoving(const ComPtr<IMedium> &pMedium);
};

#endif /* !MAIN_INCLUDED_MachineImplMoveVM_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */

