/* $Id: GuestFileImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Guest file handling implementation.
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

#ifndef MAIN_INCLUDED_GuestFileImpl_h
#define MAIN_INCLUDED_GuestFileImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VirtualBoxBase.h"
#include "EventImpl.h"

#include "GuestCtrlImplPrivate.h"
#include "GuestFileWrap.h"

class Console;
class GuestSession;
class GuestProcess;

class ATL_NO_VTABLE GuestFile :
    public GuestFileWrap,
    public GuestObject
{
public:
    /** @name COM and internal init/term/mapping cruft.
     * @{ */
    DECLARE_COMMON_CLASS_METHODS(GuestFile)

    int     init(Console *pConsole, GuestSession *pSession, ULONG uFileID, const GuestFileOpenInfo &openInfo);
    void    uninit(void);

    HRESULT FinalConstruct(void);
    void    FinalRelease(void);
    /** @}  */

public:
    /** @name Implemented virtual methods from GuestObject.
     * @{ */
    int             i_callbackDispatcher(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb);
    int             i_onUnregister(void);
    int             i_onSessionStatusChange(GuestSessionStatus_T enmSessionStatus);
    /** @}  */

public:
    /** @name Public internal methods.
     * @{ */
    int             i_close(int *pGuestRc);
    EventSource    *i_getEventSource(void) { return mEventSource; }
    int             i_onNotify(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData);
    int             i_onGuestDisconnected(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData);
    int             i_open(uint32_t uTimeoutMS, int *pGuestRc);
    int             i_queryInfo(GuestFsObjData &objData, int *prcGuest);
    int             i_readData(uint32_t uSize, uint32_t uTimeoutMS, void* pvData, uint32_t cbData, uint32_t* pcbRead);
    int             i_readDataAt(uint64_t uOffset, uint32_t uSize, uint32_t uTimeoutMS,
                                 void* pvData, size_t cbData, size_t* pcbRead);
    int             i_seekAt(int64_t iOffset, GUEST_FILE_SEEKTYPE eSeekType, uint32_t uTimeoutMS, uint64_t *puOffset);
    int             i_setFileStatus(FileStatus_T fileStatus, int vrcFile);
    int             i_waitForOffsetChange(GuestWaitEvent *pEvent, uint32_t uTimeoutMS, uint64_t *puOffset);
    int             i_waitForRead(GuestWaitEvent *pEvent, uint32_t uTimeoutMS, void *pvData, size_t cbData, uint32_t *pcbRead);
    int             i_waitForStatusChange(GuestWaitEvent *pEvent, uint32_t uTimeoutMS, FileStatus_T *pFileStatus, int *pGuestRc);
    int             i_waitForWrite(GuestWaitEvent *pEvent, uint32_t uTimeoutMS, uint32_t *pcbWritten);
    int             i_writeData(uint32_t uTimeoutMS, const void *pvData, uint32_t cbData, uint32_t *pcbWritten);
    int             i_writeDataAt(uint64_t uOffset, uint32_t uTimeoutMS, const void *pvData, uint32_t cbData, uint32_t *pcbWritten);
    /** @}  */

    /** @name Static helper methods.
     * @{ */
    static Utf8Str  i_guestErrorToString(int guestRc, const char *pcszWhat);
    /** @}  */

public:

    /** @name Wrapped IGuestFile properties.
     * @{ */
    HRESULT getCreationMode(ULONG *aCreationMode);
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);
    HRESULT getId(ULONG *aId);
    HRESULT getInitialSize(LONG64 *aInitialSize);
    HRESULT getOffset(LONG64 *aOffset);
    HRESULT getStatus(FileStatus_T *aStatus);
    HRESULT getFilename(com::Utf8Str &aFilename);
    HRESULT getAccessMode(FileAccessMode_T *aAccessMode);
    HRESULT getOpenAction(FileOpenAction_T *aOpenAction);
    /** @}  */

    /** @name Wrapped IGuestFile methods.
     * @{ */
    HRESULT close();
    HRESULT queryInfo(ComPtr<IFsObjInfo> &aObjInfo);
    HRESULT querySize(LONG64 *aSize);
    HRESULT read(ULONG aToRead,
                 ULONG aTimeoutMS,
                 std::vector<BYTE> &aData);
    HRESULT readAt(LONG64 aOffset,
                   ULONG aToRead,
                   ULONG aTimeoutMS,
                   std::vector<BYTE> &aData);
    HRESULT seek(LONG64 aOffset,
                 FileSeekOrigin_T aWhence,
                 LONG64 *aNewOffset);
    HRESULT setACL(const com::Utf8Str &aAcl,
                   ULONG aMode);
    HRESULT setSize(LONG64 aSize);
    HRESULT write(const std::vector<BYTE> &aData,
                  ULONG aTimeoutMS,
                  ULONG *aWritten);
    HRESULT writeAt(LONG64 aOffset,
                    const std::vector<BYTE> &aData,
                    ULONG aTimeoutMS,
                    ULONG *aWritten);
    /** @}  */

private:

    /** This can safely be used without holding any locks.
     * An AutoCaller suffices to prevent it being destroy while in use and
     * internally there is a lock providing the necessary serialization. */
    const ComObjPtr<EventSource> mEventSource;

    struct Data
    {
        /** The file's open info. */
        GuestFileOpenInfo       mOpenInfo;
        /** The file's initial size on open. */
        uint64_t                mInitialSize;
        /** The current file status. */
        FileStatus_T            mStatus;
        /** The last returned process status
         *  returned from the guest side. */
        int                     mLastError;
        /** The file's current offset. */
        uint64_t                mOffCurrent;
    } mData;
};

#endif /* !MAIN_INCLUDED_GuestFileImpl_h */

