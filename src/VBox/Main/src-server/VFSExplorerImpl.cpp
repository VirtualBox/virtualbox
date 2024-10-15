/* $Id: VFSExplorerImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IVFSExplorer COM class implementations.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define LOG_GROUP LOG_GROUP_MAIN_VFSEXPLORER
#include <iprt/dir.h>
#include <iprt/path.h>
#include <iprt/file.h>
#include <iprt/cpp/utils.h>

#include <VBox/com/array.h>

#include <VBox/param.h>
#include <VBox/version.h>

#include "VFSExplorerImpl.h"
#include "VirtualBoxImpl.h"
#include "ProgressImpl.h"

#include "AutoCaller.h"
#include "LoggingNew.h"
#include "ThreadTask.h"

#include <memory>

struct VFSExplorer::Data
{
    struct DirEntry
    {
        DirEntry(Utf8Str strName, FsObjType_T fileType, RTFOFF cbSize, uint32_t fMode)
           : name(strName)
           , type(fileType)
           , size(cbSize)
           , mode(fMode) {}

        Utf8Str name;
        FsObjType_T type;
        RTFOFF size;
        uint32_t mode;
    };

    VFSType_T storageType;
    Utf8Str strUsername;
    Utf8Str strPassword;
    Utf8Str strHostname;
    Utf8Str strPath;
    Utf8Str strBucket;
    std::list<DirEntry> entryList;
};


VFSExplorer::VFSExplorer()
    : mVirtualBox(NULL)
{
}

VFSExplorer::~VFSExplorer()
{
}


/**
 * VFSExplorer COM initializer.
 * @param   aType       VFS type.
 * @param   aFilePath   File path.
 * @param   aHostname   Host name.
 * @param   aUsername   User name.
 * @param   aPassword   Password.
 * @param   aVirtualBox VirtualBox object.
 * @return
 */
HRESULT VFSExplorer::init(VFSType_T aType, Utf8Str aFilePath, Utf8Str aHostname, Utf8Str aUsername,
                          Utf8Str aPassword, VirtualBox *aVirtualBox)
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /* Weak reference to a VirtualBox object */
    unconst(mVirtualBox) = aVirtualBox;

    /* initialize data */
    m = new Data;

    m->storageType = aType;
    m->strPath = aFilePath;
    m->strHostname = aHostname;
    m->strUsername = aUsername;
    m->strPassword = aPassword;

    if (m->storageType == VFSType_S3)
    {
        size_t bpos = aFilePath.find("/", 1);
        if (bpos != Utf8Str::npos)
        {
            m->strBucket = aFilePath.substr(1, bpos - 1); /* The bucket without any slashes */
            aFilePath = aFilePath.substr(bpos); /* The rest of the file path */
        }
    }

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * VFSExplorer COM uninitializer.
 */
void VFSExplorer::uninit()
{
    delete m;
    m = NULL;
}

/**
 * Public method implementation.
 * @param   aPath   Where to store the path.
 * @return  S_OK
 */
HRESULT VFSExplorer::getPath(com::Utf8Str &aPath)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aPath = m->strPath;

    return S_OK;
}


HRESULT VFSExplorer::getType(VFSType_T *aType)
{
    if (!aType)
        return E_POINTER;

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aType = m->storageType;

    return S_OK;
}

class VFSExplorer::TaskVFSExplorer : public ThreadTask
{
public:
    enum TaskType
    {
        Update,
        Delete
    };

    TaskVFSExplorer(TaskType aTaskType, VFSExplorer *aThat, Progress *aProgress)
        : m_taskType(aTaskType),
          m_pVFSExplorer(aThat),
          m_ptrProgress(aProgress),
          m_rc(S_OK)
    {
        m_strTaskName = "Explorer::Task";
    }
    ~TaskVFSExplorer() {}

private:
    void handler();

#if 0 /* unused */
    static DECLCALLBACK(int) uploadProgress(unsigned uPercent, void *pvUser);
#endif

    TaskType m_taskType;
    VFSExplorer *m_pVFSExplorer;

    ComObjPtr<Progress> m_ptrProgress;
    HRESULT m_rc;

    /* task data */
    std::list<Utf8Str> m_lstFilenames;

    friend class VFSExplorer;
};

/* static */
void VFSExplorer::TaskVFSExplorer::handler()
{
    VFSExplorer *pVFSExplorer = this->m_pVFSExplorer;

    LogFlowFuncEnter();
    LogFlowFunc(("VFSExplorer %p\n", pVFSExplorer));

    HRESULT hrc = S_OK;

    switch (this->m_taskType)
    {
        case TaskVFSExplorer::Update:
        {
            if (pVFSExplorer->m->storageType == VFSType_File)
                hrc = pVFSExplorer->i_updateFS(this);
            else if (pVFSExplorer->m->storageType == VFSType_S3)
                hrc = E_NOTIMPL;
            break;
        }
        case TaskVFSExplorer::Delete:
        {
            if (pVFSExplorer->m->storageType == VFSType_File)
                hrc = pVFSExplorer->i_deleteFS(this);
            else if (pVFSExplorer->m->storageType == VFSType_S3)
                hrc = E_NOTIMPL;
            break;
        }
        default:
            AssertMsgFailed(("Invalid task type %u specified!\n", this->m_taskType));
            break;
    }

    LogFlowFunc(("hrc=%Rhrc\n", hrc)); NOREF(hrc);
    LogFlowFuncLeave();
}

#if 0 /* unused */
/* static */
DECLCALLBACK(int) VFSExplorer::TaskVFSExplorer::uploadProgress(unsigned uPercent, void *pvUser)
{
    VFSExplorer::TaskVFSExplorer* pTask = *(VFSExplorer::TaskVFSExplorer**)pvUser;

    if (pTask &&
        !pTask->m_ptrProgress.isNull())
    {
        BOOL fCanceled;
        pTask->m_ptrProgress->COMGETTER(Canceled)(&fCanceled);
        if (fCanceled)
            return -1;
        pTask->m_ptrProgress->SetCurrentOperationProgress(uPercent);
    }
    return VINF_SUCCESS;
}
#endif

FsObjType_T VFSExplorer::i_iprtToVfsObjType(RTFMODE aType) const
{
    switch (aType & RTFS_TYPE_MASK)
    {
        case RTFS_TYPE_DIRECTORY:   return FsObjType_Directory;
        case RTFS_TYPE_FILE:        return FsObjType_File;
        case RTFS_TYPE_SYMLINK:     return FsObjType_Symlink;
        case RTFS_TYPE_FIFO:        return FsObjType_Fifo;
        case RTFS_TYPE_DEV_CHAR:    return FsObjType_DevChar;
        case RTFS_TYPE_DEV_BLOCK:   return FsObjType_DevBlock;
        case RTFS_TYPE_SOCKET:      return FsObjType_Socket;
        case RTFS_TYPE_WHITEOUT:    return FsObjType_WhiteOut;
        default:                    return FsObjType_Unknown;
    }
}

HRESULT VFSExplorer::i_updateFS(TaskVFSExplorer *aTask)
{
    LogFlowFuncEnter();

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    std::list<VFSExplorer::Data::DirEntry> fileList;
    RTDIR hDir;
    int vrc = RTDirOpen(&hDir, m->strPath.c_str());
    if (RT_SUCCESS(vrc))
    {
        try
        {
            if (aTask->m_ptrProgress)
                aTask->m_ptrProgress->SetCurrentOperationProgress(33);
            RTDIRENTRYEX entry;
            while (RT_SUCCESS(vrc))
            {
                vrc = RTDirReadEx(hDir, &entry, NULL, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
                if (RT_SUCCESS(vrc))
                {
                    Utf8Str name(entry.szName);
                    if (   name != "."
                        && name != "..")
                        fileList.push_back(VFSExplorer::Data::DirEntry(name, i_iprtToVfsObjType(entry.Info.Attr.fMode),
                                                                       entry.Info.cbObject,
                                                                         entry.Info.Attr.fMode
                                                                       & (RTFS_UNIX_IRWXU | RTFS_UNIX_IRWXG | RTFS_UNIX_IRWXO)));
                }
            }
            if (aTask->m_ptrProgress)
                aTask->m_ptrProgress->SetCurrentOperationProgress(66);
        }
        catch (HRESULT hrcXcpt)
        {
            hrc = hrcXcpt;
        }

        /* Clean up */
        RTDirClose(hDir);
    }
    else
        hrc = setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr ("Can't open directory '%s' (%Rrc)"), m->strPath.c_str(), vrc);

    if (aTask->m_ptrProgress)
        aTask->m_ptrProgress->SetCurrentOperationProgress(99);

    /* Assign the result on success (this clears the old list) */
    if (hrc == S_OK)
        m->entryList.assign(fileList.begin(), fileList.end());

    aTask->m_rc = hrc;

    if (!aTask->m_ptrProgress.isNull())
        aTask->m_ptrProgress->i_notifyComplete(hrc);

    LogFlowFunc(("hrc=%Rhrc\n", hrc));
    LogFlowFuncLeave();

    return S_OK; /** @todo ??? */
}

HRESULT VFSExplorer::i_deleteFS(TaskVFSExplorer *aTask)
{
    LogFlowFuncEnter();

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock appLock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    float fPercentStep = 100.0f / (float)aTask->m_lstFilenames.size();
    try
    {
        char szPath[RTPATH_MAX];
        std::list<Utf8Str>::const_iterator it;
        size_t i = 0;
        for (it = aTask->m_lstFilenames.begin();
             it != aTask->m_lstFilenames.end();
             ++it, ++i)
        {
            int vrc = RTPathJoin(szPath, sizeof(szPath), m->strPath.c_str(), (*it).c_str());
            if (RT_FAILURE(vrc))
                throw setErrorBoth(E_FAIL, vrc, tr("Internal Error (%Rrc)"), vrc);
            vrc = RTFileDelete(szPath);
            if (RT_FAILURE(vrc))
                throw setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Can't delete file '%s' (%Rrc)"), szPath, vrc);
            if (aTask->m_ptrProgress)
                aTask->m_ptrProgress->SetCurrentOperationProgress((ULONG)(fPercentStep * (float)i));
        }
    }
    catch (HRESULT hrcXcpt)
    {
        hrc = hrcXcpt;
    }

    aTask->m_rc = hrc;

    if (aTask->m_ptrProgress.isNotNull())
        aTask->m_ptrProgress->i_notifyComplete(hrc);

    LogFlowFunc(("hrc=%Rhrc\n", hrc));
    LogFlowFuncLeave();

    return VINF_SUCCESS;
}

HRESULT VFSExplorer::update(ComPtr<IProgress> &aProgress)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    ComObjPtr<Progress> progress;
    try
    {
        Bstr progressDesc = BstrFmt(tr("Update directory info for '%s'"),
                                    m->strPath.c_str());
        /* Create the progress object */
        progress.createObject();

        hrc = progress->init(mVirtualBox, static_cast<IVFSExplorer*>(this), progressDesc.raw(), TRUE /* aCancelable */);
        if (FAILED(hrc)) throw hrc;

        /* Initialize our worker task */
        TaskVFSExplorer* pTask = new TaskVFSExplorer(TaskVFSExplorer::Update, this, progress);

        //this function delete task in case of exceptions, so there is no need in the call of delete operator
        hrc = pTask->createThreadWithType(RTTHREADTYPE_MAIN_HEAVY_WORKER);
    }
    catch (HRESULT hrcXcpt)
    {
        hrc = hrcXcpt;
    }

     if (SUCCEEDED(hrc))
         /* Return progress to the caller */
         progress.queryInterfaceTo(aProgress.asOutParam());

    return hrc;
}

HRESULT VFSExplorer::cd(const com::Utf8Str &aDir, ComPtr<IProgress> &aProgress)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m->strPath = aDir;
    return update(aProgress);
}

HRESULT VFSExplorer::cdUp(ComPtr<IProgress> &aProgress)
{
    Utf8Str strUpPath;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        /* Remove lowest dir entry in a platform neutral way. */
        char *pszNewPath = RTStrDup(m->strPath.c_str());
        RTPathStripTrailingSlash(pszNewPath);
        RTPathStripFilename(pszNewPath);
        strUpPath = pszNewPath;
        RTStrFree(pszNewPath);
    }

    return cd(strUpPath, aProgress);
}

HRESULT VFSExplorer::entryList(std::vector<com::Utf8Str> &aNames,
                               std::vector<ULONG> &aTypes,
                               std::vector<LONG64> &aSizes,
                               std::vector<ULONG> &aModes)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aNames.resize(m->entryList.size());
    aTypes.resize(m->entryList.size());
    aSizes.resize(m->entryList.size());
    aModes.resize(m->entryList.size());

    std::list<VFSExplorer::Data::DirEntry>::const_iterator it;
    size_t i = 0;
    for (it = m->entryList.begin();
         it != m->entryList.end();
         ++it, ++i)
    {
        const VFSExplorer::Data::DirEntry &entry = (*it);
        aNames[i] = entry.name;
        aTypes[i] = entry.type;
        aSizes[i] = entry.size;
        aModes[i] = entry.mode;
    }

    return S_OK;
}

HRESULT VFSExplorer::exists(const std::vector<com::Utf8Str> &aNames,
                            std::vector<com::Utf8Str> &aExists)
{

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aExists.resize(0);
    for (size_t i=0; i < aNames.size(); ++i)
    {
        std::list<VFSExplorer::Data::DirEntry>::const_iterator it;
        for (it = m->entryList.begin();
             it != m->entryList.end();
             ++it)
        {
            const VFSExplorer::Data::DirEntry &entry = (*it);
            if (entry.name == RTPathFilename(aNames[i].c_str()))
                aExists.push_back(aNames[i]);
        }
    }

    return S_OK;
}

HRESULT VFSExplorer::remove(const std::vector<com::Utf8Str> &aNames,
                            ComPtr<IProgress> &aProgress)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    ComObjPtr<Progress> progress;
    try
    {
        /* Create the progress object */
        progress.createObject();

        hrc = progress->init(mVirtualBox, static_cast<IVFSExplorer*>(this),
                             Bstr(tr("Delete files")).raw(),
                             TRUE /* aCancelable */);
        if (FAILED(hrc)) throw hrc;

        /* Initialize our worker task */
        TaskVFSExplorer* pTask = new TaskVFSExplorer(TaskVFSExplorer::Delete, this, progress);

        /* Add all filenames to delete as task data */
        for (size_t i = 0; i < aNames.size(); ++i)
            pTask->m_lstFilenames.push_back(aNames[i]);

        //this function delete task in case of exceptions, so there is no need in the call of delete operator
        hrc = pTask->createThreadWithType(RTTHREADTYPE_MAIN_HEAVY_WORKER);
    }
    catch (HRESULT hrcXcpt)
    {
        hrc = hrcXcpt;
    }

    if (SUCCEEDED(hrc))
        /* Return progress to the caller */
        progress.queryInterfaceTo(aProgress.asOutParam());

    return hrc;
}

