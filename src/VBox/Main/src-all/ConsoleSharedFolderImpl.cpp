/* $Id: ConsoleSharedFolderImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_SHAREDFOLDER
#include "ConsoleSharedFolderImpl.h"
#include "ConsoleImpl.h"

#include "AutoCaller.h"

#include <iprt/param.h>
#include <iprt/cpp/utils.h>
#include <iprt/path.h>

/////////////////////////////////////////////////////////////////////////////
// ConsoleSharedFolder::Data structure
/////////////////////////////////////////////////////////////////////////////

struct ConsoleSharedFolder::Data
{
    Data()
    : fWritable(false),
      fAutoMount(false),
      enmSymlinkPolicy(SymlinkPolicy_None)
    { }

    const Utf8Str   strName;
    const Utf8Str   strHostPath;
    bool            fWritable;
    bool            fAutoMount;
    const Utf8Str   strAutoMountPoint;
    Utf8Str         strLastAccessError;
    SymlinkPolicy_T enmSymlinkPolicy;
};

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

ConsoleSharedFolder::ConsoleSharedFolder()
    : mParent(NULL),
      mConsole(NULL)
{
    m = new Data;
}

ConsoleSharedFolder::~ConsoleSharedFolder()
{
    delete m;
    m = NULL;
}

HRESULT ConsoleSharedFolder::FinalConstruct()
{
    return BaseFinalConstruct();
}

void ConsoleSharedFolder::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 *  Initializes the shared folder object.
 *
 *  This variant initializes an instance that lives in the console address space.
 *
 *  @param aConsole     Console parent object
 *  @param aName        logical name of the shared folder
 *  @param aHostPath    full path to the shared folder on the host
 *  @param aWritable    writable if true, readonly otherwise
 *  @param aAutoMount   if auto mounted by guest true, false otherwise
 *  @param aAutoMountPoint Where the guest should try auto mount it.
 *  @param fFailOnError Whether to fail with an error if the shared folder path is bad.
 *
 *  @return          COM result indicator
 */
HRESULT ConsoleSharedFolder::init(Console *aConsole,
                           const Utf8Str &aName,
                           const Utf8Str &aHostPath,
                           bool aWritable,
                           bool aAutoMount,
                           const Utf8Str &aAutoMountPoint,
                           bool fFailOnError)
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mConsole) = aConsole;

    HRESULT hrc = i_protectedInit(aConsole, aName, aHostPath, aWritable, aAutoMount, aAutoMountPoint, fFailOnError);

    /* Confirm a successful initialization when it's the case */
    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();

    return hrc;
}

/**
 *  Shared initialization code. Called from the other constructors.
 *
 *  @note
 *      Must be called from under the object's lock!
 */
HRESULT ConsoleSharedFolder::i_protectedInit(VirtualBoxBase *aParent,
                                      const Utf8Str &aName,
                                      const Utf8Str &aHostPath,
                                      bool aWritable,
                                      bool aAutoMount,
                                      const Utf8Str &aAutoMountPoint,
                                      bool fFailOnError)
{
    LogFlowThisFunc(("aName={%s}, aHostPath={%s}, aWritable={%d}, aAutoMount={%d}\n",
                      aName.c_str(), aHostPath.c_str(), aWritable, aAutoMount));

    ComAssertRet(aParent && aName.isNotEmpty() && aHostPath.isNotEmpty(), E_INVALIDARG);

    Utf8Str hostPath = aHostPath;
    size_t hostPathLen = hostPath.length();

    /* Remove the trailing slash unless it's a root directory
     * (otherwise the comparison with the RTPathAbs() result will fail at least
     * on Linux). Note that this isn't really necessary for the shared folder
     * itself, since adding a mapping eventually results into a
     * RTDirOpenFiltered() call (see HostServices/SharedFolders) that seems to
     * accept both the slashified paths and not. */
#if defined (RT_OS_OS2) || defined (RT_OS_WINDOWS)
    if (   hostPathLen > 2
        && RTPATH_IS_SEP(hostPath.c_str()[hostPathLen - 1])
        && RTPATH_IS_VOLSEP(hostPath.c_str()[hostPathLen - 2]))
        ;
#else
    if (hostPathLen == 1 && RTPATH_IS_SEP(hostPath[0]))
        ;
#endif
    else
        hostPath.stripTrailingSlash();

    if (fFailOnError)
    {
        /* Check whether the path is full (absolute) */
        char hostPathFull[RTPATH_MAX];
        int vrc = RTPathAbs(hostPath.c_str(),
                            hostPathFull,
                            sizeof(hostPathFull));
        if (RT_FAILURE(vrc))
            return setErrorBoth(E_INVALIDARG, vrc, tr("Invalid shared folder path: '%s' (%Rrc)"), hostPath.c_str(), vrc);

        if (RTPathCompare(hostPath.c_str(), hostPathFull) != 0)
            return setError(E_INVALIDARG, tr("Shared folder path '%s' is not absolute"), hostPath.c_str());

        RTFSOBJINFO ObjInfo;
        vrc = RTPathQueryInfoEx(hostPathFull, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
        if (RT_FAILURE(vrc))
            return setError(E_INVALIDARG, tr("RTPathQueryInfo failed on shared folder path '%s': %Rrc"), hostPathFull, vrc);

        if (!RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
            return setError(E_INVALIDARG, tr("Shared folder path '%s' is not a directory"), hostPathFull);
    }

    unconst(mParent) = aParent;

    unconst(m->strName) = aName;
    unconst(m->strHostPath) = hostPath;
    m->fWritable = aWritable;
    m->fAutoMount = aAutoMount;
    unconst(m->strAutoMountPoint) = aAutoMountPoint;

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void ConsoleSharedFolder::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(mParent) = NULL;
    unconst(mConsole) = NULL;
}

// wrapped ISharedFolder properties
/////////////////////////////////////////////////////////////////////////////
HRESULT ConsoleSharedFolder::getName(com::Utf8Str &aName)
{
    /* mName is constant during life time, no need to lock */
    aName = m->strName;
    return S_OK;
}

HRESULT ConsoleSharedFolder::getHostPath(com::Utf8Str &aHostPath)
{
    /* mHostPath is constant during life time, no need to lock */
    aHostPath = m->strHostPath;
    return S_OK;
}

HRESULT ConsoleSharedFolder::getAccessible(BOOL *aAccessible)
{
    /* mName and mHostPath are constant during life time, no need to lock */

    /* check whether the host path exists */
    Utf8Str hostPath = m->strHostPath;
    char hostPathFull[RTPATH_MAX];
    int vrc = RTPathExists(hostPath.c_str()) ? RTPathReal(hostPath.c_str(),
                                                          hostPathFull,
                                                          sizeof(hostPathFull))
                                      : VERR_PATH_NOT_FOUND;
    if (RT_SUCCESS(vrc))
    {
        *aAccessible = TRUE;
        return S_OK;
    }

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->strLastAccessError = Utf8StrFmt(tr("'%s' is not accessible (%Rrc)"),
                                       m->strHostPath.c_str(),
                                       vrc);

    Log1WarningThisFunc(("m.lastAccessError=\"%s\"\n", m->strLastAccessError.c_str()));

    *aAccessible = FALSE;

    return S_OK;
}

HRESULT ConsoleSharedFolder::getWritable(BOOL *aWritable)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aWritable = m->fWritable;
    return S_OK;
}

HRESULT ConsoleSharedFolder::setWritable(BOOL aWritable)
{
    RT_NOREF(aWritable);
    return E_NOTIMPL;
}

HRESULT ConsoleSharedFolder::getAutoMount(BOOL *aAutoMount)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aAutoMount = m->fAutoMount;
    return S_OK;
}

HRESULT ConsoleSharedFolder::setAutoMount(BOOL aAutoMount)
{
    RT_NOREF(aAutoMount);
    return E_NOTIMPL;
}

HRESULT ConsoleSharedFolder::getAutoMountPoint(com::Utf8Str &aAutoMountPoint)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aAutoMountPoint = m->strAutoMountPoint;
    return S_OK;
}

HRESULT ConsoleSharedFolder::setAutoMountPoint(com::Utf8Str const &aAutoMountPoint)
{
    RT_NOREF(aAutoMountPoint);
    return E_NOTIMPL;
}

HRESULT ConsoleSharedFolder::getLastAccessError(com::Utf8Str &aLastAccessError)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aLastAccessError = m->strLastAccessError;
    return S_OK;
}

HRESULT ConsoleSharedFolder::getSymlinkPolicy(SymlinkPolicy_T *aSymlinkPolicy)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aSymlinkPolicy = m->enmSymlinkPolicy;
    return S_OK;
}

HRESULT ConsoleSharedFolder::setSymlinkPolicy(SymlinkPolicy_T aSymlinkPolicy)
{
    RT_NOREF(aSymlinkPolicy);
    return E_NOTIMPL;
}

const Utf8Str& ConsoleSharedFolder::i_getName() const
{
    return m->strName;
}

const Utf8Str& ConsoleSharedFolder::i_getHostPath() const
{
    return m->strHostPath;
}

bool ConsoleSharedFolder::i_isWritable() const
{
    return m->fWritable;
}

bool ConsoleSharedFolder::i_isAutoMounted() const
{
    return m->fAutoMount;
}

const Utf8Str &ConsoleSharedFolder::i_getAutoMountPoint() const
{
    return m->strAutoMountPoint;
}

SymlinkPolicy_T ConsoleSharedFolder::i_getSymlinkPolicy() const
{
    return m->enmSymlinkPolicy;
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
