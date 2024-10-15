/* $Id: GuestDirectoryImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Guest directory handling implementation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_GuestDirectoryImpl_h
#define MAIN_INCLUDED_GuestDirectoryImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "GuestDirectoryWrap.h"
#include "GuestFsObjInfoImpl.h"
#include "GuestProcessImpl.h"

class GuestSession;

/**
 * TODO
 */
class ATL_NO_VTABLE GuestDirectory :
    public GuestDirectoryWrap,
    public GuestObject
{
public:
    /** @name COM and internal init/term/mapping cruft.
     * @{ */
    DECLARE_COMMON_CLASS_METHODS(GuestDirectory)

    int     init(Console *pConsole, GuestSession *pSession, ULONG aObjectID, const GuestDirectoryOpenInfo &openInfo);
    void    uninit(void);

    HRESULT FinalConstruct(void);
    void    FinalRelease(void);
    /** @}  */

public:
    /** @name Implemented virtual methods from GuestObject.
     * @{ */
    int            i_callbackDispatcher(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb);
    int            i_onUnregister(void);
    int            i_onSessionStatusChange(GuestSessionStatus_T enmSessionStatus);
    /** @}  */

public:
    /** @name Public internal methods.
     * @{ */
    int            i_open(int *pvrcGuest);
    int            i_close(int *pvrcGuest);
    EventSource   *i_getEventSource(void) { return mEventSource; }
    int            i_onDirNotify(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData);
    int            i_listInternal(uint32_t cMaxEntries, uint32_t fFlags, std::vector<GuestFsObjData> &vecObjData, int *pvrcGuest);
    int            i_listEx(uint32_t cMaxEntries, uint32_t fFlags, std::vector<ComObjPtr<GuestFsObjInfo>> &vecObjInfo, int *pvrcGuest);
    int            i_list(uint32_t cMaxEntries, std::vector<ComObjPtr<GuestFsObjInfo>> &vecObjInfo, int *pvrcGuest);
    int            i_read(ComObjPtr<GuestFsObjInfo> &fsObjInfo, int *pvrcGuest);
    int            i_readInternal(GuestFsObjData &objData, int *pvrcGuest);
    int            i_rewind(uint32_t uTimeoutMS, int *pvrcGuest);
    int            i_setStatus(DirectoryStatus_T enmStatus, int vrcDir);
    int            i_waitForStatusChange(GuestWaitEvent *pEvent, uint32_t uTimeoutMS, DirectoryStatus_T *penmStatus, int *prcGuest);
    /** @}  */

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
    int            i_openViaToolbox(int *pvrcGuest);
    int            i_closeViaToolbox(int *pvrcGuest);
    int            i_readInternalViaToolbox(GuestFsObjData &objData, int *pvrcGuest);
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */

public:
    /** @name Public static internal methods.
     * @{ */
    static Utf8Str i_guestErrorToString(int vrcGuest, const char *pcszWhat);
    static void    i_dirNotifyDataDestroy(PCALLBACKDATA_DIR_NOTIFY pDirNotify);
    /** @}  */

private:

    /** Wrapped @name IGuestDirectory properties
     * @{ */
    HRESULT getDirectoryName(com::Utf8Str &aDirectoryName);
    HRESULT getFilter(com::Utf8Str &aFilter);
    /** @}  */

    /** Wrapped @name IGuestDirectory methods.
     * @{ */
    HRESULT close();
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);
    HRESULT getId(ULONG *aId);
    HRESULT getStatus(DirectoryStatus_T *aStatus);
    HRESULT list(ULONG aMaxEntries, std::vector<ComPtr<IFsObjInfo> > &aObjInfos);
    HRESULT read(ComPtr<IFsObjInfo> &aObjInfo);
    HRESULT rewind(void);
    /** @}  */

    /** This can safely be used without holding any locks.
     * An AutoCaller suffices to prevent it being destroy while in use and
     * internally there is a lock providing the necessary serialization. */
    const ComObjPtr<EventSource> mEventSource;

    struct Data
    {
        /** The directory's open info. */
        GuestDirectoryOpenInfo     mOpenInfo;
        /** The current directory status. */
        DirectoryStatus_T          mStatus;
        /** The last returned directory error returned from the guest side. */
        int                        mLastError;
# ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
        /** The process tool instance to use. */
        GuestProcessToolbox        mProcessTool;
# endif
        /** Object data cache.
         *  Its mName attribute acts as a beacon if the cache is valid or not. */
        GuestFsObjData             mObjData;
    } mData;
};

#endif /* !MAIN_INCLUDED_GuestDirectoryImpl_h */

