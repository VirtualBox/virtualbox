/* $Id: GuestDirectoryImpl.cpp 168921 2025-05-19 07:28:41Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox Main - Guest directory handling.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN_GUESTDIRECTORY
#include "LoggingNew.h"

#ifndef VBOX_WITH_GUEST_CONTROL
# error "VBOX_WITH_GUEST_CONTROL must defined in this file"
#endif
#include "GuestImpl.h"
#include "GuestDirectoryImpl.h"
#include "GuestSessionImpl.h"
#include "GuestCtrlImplPrivate.h"
#include "VirtualBoxErrorInfoImpl.h"

#include "Global.h"
#include "AutoCaller.h"
#include "VBoxEvents.h"

#include <VBox/com/array.h>
#include <VBox/com/listeners.h>
#include <VBox/AssertGuest.h>


/**
 * Internal listener class to serve events in an
 * active manner, e.g. without polling delays.
 */
class GuestDirectoryListener
{
public:

    GuestDirectoryListener(void)
    {
    }

    virtual ~GuestDirectoryListener()
    {
    }

    HRESULT init(GuestDirectory *pDir)
    {
        AssertPtrReturn(pDir, E_POINTER);
        mDir = pDir;
        return S_OK;
    }

    void uninit(void)
    {
        mDir = NULL;
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        switch (aType)
        {
            case VBoxEventType_OnGuestDirectoryStateChanged:
                RT_FALL_THROUGH();
            case VBoxEventType_OnGuestDirectoryRead:
            {
                AssertPtrReturn(mDir, E_POINTER);
                int vrc2 = mDir->signalWaitEvent(aType, aEvent);
                RT_NOREF(vrc2);
#ifdef DEBUG_andy
                LogFlowFunc(("Signalling events of type=%RU32, dir=%p resulted in vrc=%Rrc\n",
                             aType, mDir, vrc2));
#endif
                break;
            }

            default:
                AssertMsgFailed(("Unhandled event %RU32\n", aType));
                break;
        }

        return S_OK;
    }

private:

    /** Weak pointer to the guest directory object to listen for. */
    GuestDirectory *mDir;
};
typedef ListenerImpl<GuestDirectoryListener, GuestDirectory *> GuestDirectoryListenerImpl;

VBOX_LISTENER_DECLARE(GuestDirectoryListenerImpl)

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(GuestDirectory)

HRESULT GuestDirectory::FinalConstruct(void)
{
    LogFlowThisFunc(("\n"));
    return BaseFinalConstruct();
}

void GuestDirectory::FinalRelease(void)
{
    LogFlowThisFuncEnter();
    uninit();
    BaseFinalRelease();
    LogFlowThisFuncLeave();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

int GuestDirectory::init(Console *pConsole, GuestSession *pSession, ULONG aObjectID, const GuestDirectoryOpenInfo &openInfo)
{
    LogFlowThisFunc(("pConsole=%p, pSession=%p, aObjectID=%RU32, strPath=%s, enmFilter=%#x, fFlags=%x\n",
                     pConsole, pSession, aObjectID, openInfo.mPath.c_str(), openInfo.menmFilter, openInfo.mFlags));

    AssertPtrReturn(pConsole, VERR_INVALID_POINTER);
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);

    /* Enclose the state transition NotReady->InInit->Ready. */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), VERR_OBJECT_DESTROYED);

    int vrc = bindToSession(pConsole, pSession, aObjectID);
    if (RT_SUCCESS(vrc))
    {
        mSession  = pSession;
        mObjectID = aObjectID;

        mData.mOpenInfo  = openInfo;
        mData.mStatus    = DirectoryStatus_Undefined;
        mData.mLastError = VINF_SUCCESS;

        unconst(mEventSource).createObject();
        HRESULT hr = mEventSource->init();
        if (FAILED(hr))
            vrc = VERR_COM_UNEXPECTED;
    }

    if (RT_SUCCESS(vrc))
    {
        try
        {
            GuestDirectoryListener *pListener = new GuestDirectoryListener();
            ComObjPtr<GuestDirectoryListenerImpl> thisListener;
            HRESULT hr = thisListener.createObject();
            if (SUCCEEDED(hr))
                hr = thisListener->init(pListener, this);

            if (SUCCEEDED(hr))
            {
                com::SafeArray <VBoxEventType_T> eventTypes;
                eventTypes.push_back(VBoxEventType_OnGuestDirectoryStateChanged);
                eventTypes.push_back(VBoxEventType_OnGuestDirectoryRead);
                hr = mEventSource->RegisterListener(thisListener,
                                                    ComSafeArrayAsInParam(eventTypes),
                                                    TRUE /* Active listener */);
                if (SUCCEEDED(hr))
                {
                    vrc = baseInit();
                    if (RT_SUCCESS(vrc))
                    {
                        mLocalListener = thisListener;
                    }
                }
                else
                    vrc = VERR_COM_UNEXPECTED;
            }
            else
                vrc = VERR_COM_UNEXPECTED;
        }
        catch(std::bad_alloc &)
        {
            vrc = VERR_NO_MEMORY;
        }
    }

    /* Confirm a successful initialization when it's the case. */
    if (RT_SUCCESS(vrc))
        autoInitSpan.setSucceeded();
    else
        autoInitSpan.setFailed();

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Uninitializes the instance.
 * Called from FinalRelease().
 */
void GuestDirectory::uninit(void)
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition Ready->InUninit->NotReady. */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    LogFlowThisFuncLeave();
}

// implementation of private wrapped getters/setters for attributes
/////////////////////////////////////////////////////////////////////////////

HRESULT GuestDirectory::getDirectoryName(com::Utf8Str &aDirectoryName)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aDirectoryName = mData.mOpenInfo.mPath;

    return S_OK;
}

HRESULT GuestDirectory::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    /* No need to lock - lifetime constant. */
    mEventSource.queryInterfaceTo(aEventSource.asOutParam());

    return S_OK;
}

HRESULT GuestDirectory::getFilter(com::Utf8Str &aFilter)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aFilter = mData.mOpenInfo.mFilter;

    return S_OK;
}

HRESULT GuestDirectory::getId(ULONG *aId)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aId = mObjectID;

    return S_OK;
}

HRESULT GuestDirectory::getStatus(DirectoryStatus_T *aStatus)
{
    LogFlowThisFuncEnter();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aStatus = mData.mStatus;

    return S_OK;
}

// private methods
/////////////////////////////////////////////////////////////////////////////

/**
 * Entry point for guest side directory callbacks.
 *
 * @returns VBox status code.
 * @param   pCbCtx              Host callback context.
 * @param   pSvcCb              Host callback data.
 */
int GuestDirectory::i_callbackDispatcher(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCb)
{
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCb, VERR_INVALID_POINTER);

    LogFlowThisFunc(("strPath=%s, uContextID=%RU32, uMessage=%RU32, pSvcCb=%p\n",
                     mData.mOpenInfo.mPath.c_str(), pCbCtx->uContextID, pCbCtx->uMessage, pSvcCb));

    int vrc;
    switch (pCbCtx->uMessage)
    {
        case GUEST_MSG_DISCONNECTED:
            /** @todo vrc = i_onGuestDisconnected(pCbCtx, pSvcCb); */
            vrc = VINF_SUCCESS; /// @todo To be implemented
            break;

        case GUEST_MSG_DIR_NOTIFY:
        {
            vrc = i_onDirNotify(pCbCtx, pSvcCb);
            break;
        }

        default:
            /* Silently ignore not implemented functions. */
            vrc = VERR_NOT_SUPPORTED;
            break;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Opens the directory on the guest side.
 *
 * @return VBox status code.
 * @param  pvrcGuest            Where to store the guest result code in case VERR_GSTCTL_GUEST_ERROR is returned.
 */
int GuestDirectory::i_open(int *pvrcGuest)
{
    int vrc;
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    if ((mSession->i_getParent()->i_getGuestControlFeatures0() & VBOX_GUESTCTRL_GF_0_TOOLBOX_AS_CMDS))
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

        GuestWaitEvent *pEvent = NULL;
        GuestEventTypes eventTypes;
        try
        {
            eventTypes.push_back(VBoxEventType_OnGuestDirectoryStateChanged);

            vrc = registerWaitEvent(eventTypes, &pEvent);
        }
        catch (std::bad_alloc &)
        {
            vrc = VERR_NO_MEMORY;
        }

        if (RT_FAILURE(vrc))
            return vrc;

        /* Prepare HGCM call. */
        VBOXHGCMSVCPARM paParms[8];
        int i = 0;
        HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
        HGCMSvcSetStr(&paParms[i++], mData.mOpenInfo.mPath.c_str());
        HGCMSvcSetU32(&paParms[i++], mData.mOpenInfo.menmFilter);
        HGCMSvcSetU32(&paParms[i++], mData.mOpenInfo.mFlags);
        HGCMSvcSetU32(&paParms[i++], GSTCTLFSOBJATTRADD_UNIX /* Implicit */);
        HGCMSvcSetU32(&paParms[i++], GSTCTL_PATH_F_ON_LINK   /* Ditto */ );

        alock.release(); /* Drop lock before sending. */

        vrc = sendMessage(HOST_MSG_DIR_OPEN, i, paParms);
        if (RT_SUCCESS(vrc))
            vrc = i_waitForStatusChange(pEvent, GSTCTL_DEFAULT_TIMEOUT_MS, NULL /* FileStatus */, pvrcGuest);
    }
    else
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
    {
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
        vrc = i_openViaToolbox(pvrcGuest);
#else
        RT_NOREF(pvrcGuest);
        vrc = VERR_NOT_SUPPORTED;
#endif
    }

    return vrc;
}

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
/**
 * Opens the directory on the guest side (legacy version).
 *
 * @returns VBox status code.
 * @param   pvrcGuest           Where to store the guest result code in case VERR_GSTCTL_GUEST_ERROR is returned.
 *
 * @note This uses an own guest process via the built-in toolbox in VBoxSerivce.
 */
int GuestDirectory::i_openViaToolbox(int *pvrcGuest)
{
    /* Start the directory process on the guest. */
    GuestProcessStartupInfo procInfo;
    procInfo.mName.printf(tr("Opening directory \"%s\""), mData.mOpenInfo.mPath.c_str());
    procInfo.mTimeoutMS = 5 * 60 * 1000; /* 5 minutes timeout. */
    procInfo.mFlags     = ProcessCreateFlag_WaitForStdOut;
    procInfo.mExecutable= Utf8Str(VBOXSERVICE_TOOL_LS);

    procInfo.mArguments.push_back(procInfo.mExecutable);
    procInfo.mArguments.push_back(Utf8Str("--machinereadable"));
    /* We want the long output format which contains all the object details. */
    procInfo.mArguments.push_back(Utf8Str("-l"));
    /* Always dereference symlinks by default when opening directories, as we want to show its
     * contents rather than working directly on the link.
     *
     * Newer Linux distros such as Ubuntu 22.10 symlink /bin to /usr/bin, for example. */
    if (!(mData.mOpenInfo.mFlags & DirectoryOpenFlag_NoSymlinks)) /* Check if the caller explicitly forbids this. */
        procInfo.mArguments.push_back(Utf8Str("--dereference"));
    /** @todo Recursion support? */
    procInfo.mArguments.push_back(mData.mOpenInfo.mPath); /* The directory we want to open. */

    /*
     * Start the process synchronously and keep it around so that we can use
     * it later in subsequent read() calls.
     */
    int vrc = mData.mProcessTool.init(mSession, procInfo, false /*fAsync*/, NULL /*pvrcGuest*/);
    if (RT_SUCCESS(vrc))
    {
        /* As we need to know if the directory we were about to open exists and and is accessible,
         * do the first read here in order to return a meaningful status here. */
        int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
        vrc = i_readInternal(mData.mObjData, &vrcGuest);
        if (RT_FAILURE(vrc))
        {
            /*
             * We need to actively terminate our process tool in case of an error here,
             * as this otherwise would be done on (directory) object destruction implicitly.
             * This in turn then will run into a timeout, as the directory object won't be
             * around anymore at that time. Ugly, but that's how it is for the moment.
             */
            /* ignore rc */ mData.mProcessTool.terminate(30 * RT_MS_1SEC, NULL /* pvrcGuest */);
        }

        if (pvrcGuest)
            *pvrcGuest = vrcGuest;
    }

    return vrc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */

/**
 * Called when the guest side notifies the host of a directory event.
 *
 * @returns VBox status code.
 * @param   pCbCtx              Host callback context.
 * @param   pSvcCbData          Host callback data.
 */
int GuestDirectory::i_onDirNotify(PVBOXGUESTCTRLHOSTCBCTX pCbCtx, PVBOXGUESTCTRLHOSTCALLBACK pSvcCbData)
{
#ifndef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    RT_NOREF(pCbCtx, pSvcCbData);
    return VERR_NOT_SUPPORTED;
#else
    AssertPtrReturn(pCbCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pSvcCbData, VERR_INVALID_POINTER);

    LogFlowThisFuncEnter();

    if (pSvcCbData->mParms < 3)
        return VERR_INVALID_PARAMETER;

    int idx = 1; /* Current parameter index. */
    CALLBACKDATA_DIR_NOTIFY dataCb;
    RT_ZERO(dataCb);
    /* pSvcCb->mpaParms[0] always contains the context ID. */
    HGCMSvcGetU32(&pSvcCbData->mpaParms[idx++], &dataCb.uType);
    HGCMSvcGetU32(&pSvcCbData->mpaParms[idx++], &dataCb.rc);

    int const vrcGuest = (int)dataCb.rc; /* uint32_t vs. int. */

    LogFlowThisFunc(("uType=%RU32, vrcGuest=%Rrc\n", dataCb.uType, vrcGuest));

    if (RT_FAILURE(vrcGuest))
    {
        /** @todo Set status? */

        /* Ignore return code, as the event to signal might not be there (anymore). */
        signalWaitEventInternal(pCbCtx, vrcGuest, NULL /* pPayload */);
        return VINF_SUCCESS; /* Report to the guest. */
    }

    int vrc = VERR_NOT_SUPPORTED; /* Play safe by default. */

    ComObjPtr<VirtualBoxErrorInfo> errorInfo;
    HRESULT hrc = errorInfo.createObject();
    ComAssertComRCRet(hrc, VERR_COM_UNEXPECTED);
    if (RT_FAILURE(vrcGuest))
    {
        hrc = errorInfo->initEx(VBOX_E_GSTCTL_GUEST_ERROR, vrcGuest,
                                COM_IIDOF(IGuestDirectory), getComponentName(),
                                i_guestErrorToString(vrcGuest, mData.mOpenInfo.mPath.c_str()));
        ComAssertComRCRet(hrc, VERR_COM_UNEXPECTED);
    }

    switch (dataCb.uType)
    {
        case GUEST_DIR_NOTIFYTYPE_ERROR:
        {
            vrc = i_setStatus(DirectoryStatus_Error, vrcGuest);
            break;
        }

        case GUEST_DIR_NOTIFYTYPE_OPEN:
        {
            AssertBreakStmt(pSvcCbData->mParms >= 4, vrc = VERR_INVALID_PARAMETER);
            vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[idx++], &dataCb.u.open.uHandle /* Guest native file handle */);
            AssertRCBreak(vrc);
            vrc = i_setStatus(DirectoryStatus_Open, vrcGuest);
            break;
        }

        case GUEST_DIR_NOTIFYTYPE_CLOSE:
        {
            vrc = i_setStatus(DirectoryStatus_Close, vrcGuest);
            break;
        }

        case GUEST_DIR_NOTIFYTYPE_READ:
        {
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mParms == 6, ("mParms=%u\n", pSvcCbData->mParms),
                                        vrc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mpaParms[idx].type == VBOX_HGCM_SVC_PARM_PTR,
                                        ("type=%u\n", pSvcCbData->mpaParms[idx].type),
                                        vrc = VERR_WRONG_PARAMETER_TYPE);
            PGSTCTLDIRENTRYEX pDirEntryEx;
            uint32_t          cbDirEntryEx;
            vrc = HGCMSvcGetPv(&pSvcCbData->mpaParms[idx++], (void **)&pDirEntryEx, &cbDirEntryEx);
            AssertRCBreak(vrc);
            AssertBreakStmt(   cbDirEntryEx >= RT_UOFFSETOF(GSTCTLDIRENTRYEX, szName[2])
                            && cbDirEntryEx <= GSTCTL_DIRENTRY_MAX_SIZE, vrc = VERR_INVALID_PARAMETER);
            dataCb.u.read.Entry.pDirEntryEx  = (PGSTCTLDIRENTRYEX)RTMemDup(pDirEntryEx, cbDirEntryEx);
            AssertPtrBreakStmt(dataCb.u.read.Entry.pDirEntryEx, vrc = VERR_NO_MEMORY);
            dataCb.u.read.Entry.cbDirEntryEx = cbDirEntryEx;

            char    *pszUser;
            uint32_t cbUser;
            vrc = HGCMSvcGetStr(&pSvcCbData->mpaParms[idx++], &pszUser, &cbUser);
            AssertRCBreak(vrc);
            AssertBreakStmt(cbUser <= GSTCTL_DIRENTRY_MAX_USER_NAME, vrc = VERR_TOO_MUCH_DATA);
            dataCb.u.read.Entry.pszUser = RTStrDup(pszUser);
            AssertPtrBreakStmt(dataCb.u.read.Entry.pszUser, vrc = VERR_NO_MEMORY);
            dataCb.u.read.Entry.cbUser  = cbUser;

            char    *pszGroups;
            uint32_t cbGroups;
            vrc = HGCMSvcGetStr(&pSvcCbData->mpaParms[idx++], &pszGroups, &cbGroups);
            AssertRCBreak(vrc);
            AssertBreakStmt(cbGroups <= GSTCTL_DIRENTRY_MAX_USER_GROUPS, vrc = VERR_TOO_MUCH_DATA);
            dataCb.u.read.Entry.pszGroups = RTStrDup(pszGroups);
            AssertPtrBreakStmt(dataCb.u.read.Entry.pszGroups, vrc = VERR_NO_MEMORY);
            dataCb.u.read.Entry.cbGroups  = cbGroups;

            /** @todo ACLs not implemented yet. */

            GuestFsObjData fsObjData(dataCb.u.read.Entry.pDirEntryEx->szName);
            vrc = fsObjData.FromGuestFsObjInfo(&dataCb.u.read.Entry.pDirEntryEx->Info);
            AssertRCBreak(vrc);
            ComObjPtr<GuestFsObjInfo> ptrFsObjInfo;
            hrc = ptrFsObjInfo.createObject();
            ComAssertComRCBreak(hrc, vrc = VERR_COM_UNEXPECTED);
            vrc = ptrFsObjInfo->init(fsObjData);
            AssertRCBreak(vrc);

            ::FireGuestDirectoryReadEvent(mEventSource, mSession, this,
                                          dataCb.u.read.Entry.pDirEntryEx->szName, ptrFsObjInfo,
                                          dataCb.u.read.Entry.pszUser, dataCb.u.read.Entry.pszGroups);
            break;
        }

        case GUEST_DIR_NOTIFYTYPE_REWIND:
        {
            /* Note: This does not change the overall status of the directory (i.e. open). */
            ::FireGuestDirectoryStateChangedEvent(mEventSource, mSession, this, DirectoryStatus_Rewind, errorInfo);
            break;
        }

        case GUEST_DIR_NOTIFYTYPE_LIST:
        {
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mParms == 5, ("mParms=%u\n", pSvcCbData->mParms),
                                        vrc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mpaParms[idx].type == VBOX_HGCM_SVC_PARM_32BIT,
                                        ("type=%u\n", pSvcCbData->mpaParms[idx].type),
                                        vrc = VERR_WRONG_PARAMETER_TYPE);

            vrc = HGCMSvcGetU32(&pSvcCbData->mpaParms[idx++], &dataCb.u.list.cEntries);
            AssertRCBreak(vrc);
            /* We limit this for now to 64K entries max per call.
             * The guest does not do this check, so we could de/increase this limit in the future. */
            AssertBreakStmt(dataCb.u.list.cEntries <= _64K, vrc = VERR_TOO_MUCH_DATA);

            if (!dataCb.u.list.cEntries) /* No entries sent? Bail out early. */
                break;

            /*
             * Fetch buffer with packed directory entries.
             */
            ASSERT_GUEST_MSG_STMT_BREAK(pSvcCbData->mpaParms[idx].type == VBOX_HGCM_SVC_PARM_PTR,
                                        ("type=%u\n", pSvcCbData->mpaParms[idx].type),
                                        vrc = VERR_WRONG_PARAMETER_TYPE);
            void    *pvBuf;
            uint32_t cbBuf;
            vrc = HGCMSvcGetPv(&pSvcCbData->mpaParms[idx], &pvBuf, &cbBuf);
            AssertRCBreak(vrc);
            dataCb.u.list.paEntries = (PCALLBACKDATA_DIR_ENTRY *)RTMemAllocZ(dataCb.u.list.cEntries * sizeof(PCALLBACKDATA_DIR_ENTRY));
            AssertPtrBreakStmt(dataCb.u.list.paEntries, vrc = VERR_NO_MEMORY);

            /*
             * Unpack directory entries.
             */
            size_t offBuf = 0;
            for (uint32_t i = 0; i < dataCb.u.list.cEntries; i++)
            {
                dataCb.u.list.paEntries[i] = (PCALLBACKDATA_DIR_ENTRY)RTMemAlloc(sizeof(CALLBACKDATA_DIR_ENTRY));
                AssertPtrBreakStmt(dataCb.u.list.paEntries[i], vrc = VERR_NO_MEMORY);

                PCALLBACKDATA_DIR_ENTRY pEntry = dataCb.u.list.paEntries[i];

                PGSTCTLDIRENTRYLISTHDR  const pHdr   = (PGSTCTLDIRENTRYLISTHDR)((uint8_t *)pvBuf + offBuf);
                AssertBreakStmt(pHdr->cbDirEntryEx <= GSTCTL_DIRENTRY_MAX_SIZE, vrc = VERR_TOO_MUCH_DATA);
                AssertBreakStmt(pHdr->cbUser <= GSTCTL_DIRENTRY_MAX_USER_NAME, vrc = VERR_TOO_MUCH_DATA);
                AssertBreakStmt(pHdr->cbGroups <= GSTCTL_DIRENTRY_MAX_USER_GROUPS, vrc = VERR_TOO_MUCH_DATA);
                offBuf += sizeof(GSTCTLDIRENTRYLISTHDR);

                AssertBreakStmt(   pHdr->cbDirEntryEx >= RT_UOFFSETOF(GSTCTLDIRENTRYEX, szName[2])
                                && pHdr->cbDirEntryEx <= GSTCTL_DIRENTRY_MAX_SIZE, vrc = VERR_INVALID_PARAMETER);
                pEntry->pDirEntryEx  = (PGSTCTLDIRENTRYEX)RTMemDup((uint8_t *)pvBuf + offBuf, pHdr->cbDirEntryEx);
                AssertPtrBreakStmt(pEntry->pDirEntryEx, vrc = VERR_NO_MEMORY);
                pEntry->cbDirEntryEx = pHdr->cbDirEntryEx;
                offBuf += pHdr->cbDirEntryEx;

                if (pHdr->cbUser)
                {
                    pEntry->pszUser = (char *)RTMemDup((uint8_t *)pvBuf + offBuf, pHdr->cbUser);
                    AssertPtrBreakStmt(pEntry->pszUser, vrc = VERR_NO_MEMORY);
                    pEntry->cbUser  = pHdr->cbUser;
                    offBuf += pHdr->cbUser;
                }

                if (pHdr->cbGroups)
                {
                    pEntry->pszGroups = (char *)RTMemDup((uint8_t *)pvBuf + offBuf, pHdr->cbGroups);
                    AssertPtrBreakStmt(pEntry->pszGroups, vrc = VERR_NO_MEMORY);
                    pEntry->cbGroups  = pHdr->cbGroups;
                    offBuf += pHdr->cbGroups;
                }

#ifdef DEBUG
                GuestFsObjData obj;
                AssertRC(obj.FromGuestFsObjInfo(&pEntry->pDirEntryEx->Info, pEntry->pszUser, pEntry->pszGroups));
                AssertRC(RTStrValidateEncodingEx(pEntry->pszUser,   pHdr->cbUser,   RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED));
                AssertRC(RTStrValidateEncodingEx(pEntry->pszGroups, pHdr->cbGroups, RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED));
#endif
            }

            if (RT_SUCCESS(vrc))
            {
                Assert(offBuf == cbBuf);
            }
            else /* Roll back on error. */
            {
               GuestDirectory::i_dirNotifyDataDestroy(&dataCb);
            }
            break;
        }

        default:
            AssertFailed();
            break;
    }

    try
    {
        if (RT_SUCCESS(vrc))
        {
            GuestWaitEventPayload payload(dataCb.uType, &dataCb, sizeof(dataCb));

            /* Ignore return code, as the event to signal might not be there (anymore). */
            signalWaitEventInternal(pCbCtx, vrcGuest, &payload);
        }
        else /* OOM situation, wrong HGCM parameters or smth. not expected. */
        {
            /* Ignore return code, as the event to signal might not be there (anymore). */
            signalWaitEventInternalEx(pCbCtx, vrc, 0 /* guestRc */, NULL /* pPayload */);
        }
    }
    catch (int vrcEx) /* Thrown by GuestWaitEventPayload constructor. */
    {
        /* Also try to signal the waiter, to let it know of the OOM situation.
         * Ignore return code, as the event to signal might not be there (anymore). */
        signalWaitEventInternalEx(pCbCtx, vrcEx, 0 /* guestRc */, NULL /* pPayload */);
        vrc = vrcEx;
    }

    LogFlowThisFunc(("uType=%RU32, rcGuest=%Rrc, vrc=%Rrc\n", dataCb.uType, vrcGuest, vrc));
    return vrc;
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
}

/**
 * Static helper function to convert a given guest directory error to a string.
 *
 * @returns Error string.
 * @param   vrcGuest            Guest directory error to return string for.
 * @param   pcszWhat            Hint of what was involved when the error occurred.
 */
/* static */
Utf8Str GuestDirectory::i_guestErrorToString(int vrcGuest, const char *pcszWhat)
{
    AssertPtrReturn(pcszWhat, "");

#define CASE_MSG(a_iRc, ...) \
        case a_iRc: strErr.printf(__VA_ARGS__); break;

    Utf8Str strErr;
    switch (vrcGuest)
    {
        CASE_MSG(VERR_ACCESS_DENIED,  tr("Access to guest directory \"%s\" is denied"), pcszWhat);
        CASE_MSG(VERR_ALREADY_EXISTS, tr("Guest directory \"%s\" already exists"), pcszWhat);
        CASE_MSG(VERR_CANT_CREATE,    tr("Guest directory \"%s\" cannot be created"), pcszWhat);
        CASE_MSG(VERR_DIR_NOT_EMPTY,  tr("Guest directory \"%s\" is not empty"), pcszWhat);
        CASE_MSG(VERR_NO_MORE_FILES,  tr("Guest directory \"%s\" has no more entries"), pcszWhat);
        CASE_MSG(VERR_PATH_NOT_FOUND, tr("Path of guest directory \"%s\" not found"), pcszWhat);
        default:
            strErr.printf(tr("Error %Rrc for guest directory \"%s\" occurred\n"), vrcGuest, pcszWhat);
            break;
    }

#undef CASE_MSG

    return strErr;
}

/**
 * Static helper function to destroy direction notification callback data.
 *
 * @param   pDirNotify          Pointer to direction notification callback data to destroy.
 */
/* static */
void GuestDirectory::i_dirNotifyDataDestroy(PCALLBACKDATA_DIR_NOTIFY pDirNotify)
{
    if (!pDirNotify)
        return;

    switch (pDirNotify->uType)
    {
        case GUEST_DIR_NOTIFYTYPE_LIST:
        {
            for (uint32_t i = 0; i < pDirNotify->u.list.cEntries; i++)
            {
                PCALLBACKDATA_DIR_ENTRY const pEntry = pDirNotify->u.list.paEntries[i];
                AssertPtrBreak(pEntry);
                RTStrFree(pEntry->pszUser);
                RTStrFree(pEntry->pszGroups);
                RTMemFree(pEntry->pDirEntryEx);
                RTMemFree(pEntry);
            }
            RTMemFree(pDirNotify->u.list.paEntries);
            pDirNotify->u.list.paEntries = NULL;
            pDirNotify->u.list.cEntries  = 0;
            break;
        }

        default:
            break;
    }
}

/**
 * @copydoc GuestObject::i_onUnregister
 */
int GuestDirectory::i_onUnregister(void)
{
    LogFlowThisFuncEnter();

    int vrc = VINF_SUCCESS;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * @copydoc GuestObject::i_onSessionStatusChange
 */
int GuestDirectory::i_onSessionStatusChange(GuestSessionStatus_T enmSessionStatus)
{
    RT_NOREF(enmSessionStatus);

    LogFlowThisFuncEnter();

    int vrc = VINF_SUCCESS;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Closes this guest directory and removes it from the
 * guest session's directory list.
 *
 * @return VBox status code.
 * @param  pvrcGuest            Where to store the guest result code in case VERR_GSTCTL_GUEST_ERROR is returned.
 */
int GuestDirectory::i_close(int *pvrcGuest)
{
    int vrc;
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    if (mSession->i_getParent()->i_getGuestControlFeatures0() & VBOX_GUESTCTRL_GF_0_TOOLBOX_AS_CMDS)
    {
        GuestWaitEvent *pEvent = NULL;
        GuestEventTypes eventTypes;
        try
        {
            eventTypes.push_back(VBoxEventType_OnGuestDirectoryStateChanged);

            vrc = registerWaitEvent(eventTypes, &pEvent);
        }
        catch (std::bad_alloc &)
        {
            vrc = VERR_NO_MEMORY;
        }

        if (RT_FAILURE(vrc))
            return vrc;

        /* Prepare HGCM call. */
        VBOXHGCMSVCPARM paParms[2];
        int i = 0;
        HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
        HGCMSvcSetU32(&paParms[i++], mObjectID /* Guest directory handle */);

        vrc = sendMessage(HOST_MSG_DIR_CLOSE, i, paParms);
        if (RT_SUCCESS(vrc))
        {
            vrc = pEvent->Wait(GSTCTL_DEFAULT_TIMEOUT_MS);
            if (RT_SUCCESS(vrc))
            {
                // Nothing to do here.
            }
            else if (pEvent->HasGuestError() && pvrcGuest)
                *pvrcGuest = pEvent->GuestResult();
        }
    }
    else
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
    {
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
        vrc = i_closeViaToolbox(pvrcGuest);
#else
        RT_NOREF(pvrcGuest);
        vrc = VERR_NOT_SUPPORTED;
#endif
    }

    AssertPtr(mSession);
    int vrc2 = mSession->i_directoryUnregister(this);
    if (RT_SUCCESS(vrc))
        vrc = vrc2;

    LogFlowThisFunc(("Returning vrc=%Rrc\n", vrc));
    return vrc;
}

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
/**
 * Closes this guest directory and removes it from the guest session's directory list (legacy version).
 *
 * @return VBox status code.
 * @param  pvrcGuest            Where to store the guest result code in case VERR_GSTCTL_GUEST_ERROR is returned.
 *
 * @note This uses an own guest process via the built-in toolbox in VBoxSerivce.
 */
int GuestDirectory::i_closeViaToolbox(int *pvrcGuest)
{
    return mData.mProcessTool.terminate(GSTCTL_DEFAULT_TIMEOUT_MS, pvrcGuest);
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */

/**
 * Reads the next directory entry, internal version.
 *
 * @return VBox status code.
 * @retval VERR_GSTCTL_GUEST_ERROR / VERR_NO_MORE_FILES if no more entries are available.
 * @param  objData              Where to store the read directory entry as internal object data.
 * @param  pvrcGuest            Where to store the guest result code in case VERR_GSTCTL_GUEST_ERROR is returned.
 */
int GuestDirectory::i_readInternal(GuestFsObjData &objData, int *pvrcGuest)
{
    AssertPtrReturn(pvrcGuest, VERR_INVALID_POINTER);

    int vrc;

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    if (mSession->i_getParent()->i_getGuestControlFeatures0() & VBOX_GUESTCTRL_GF_0_TOOLBOX_AS_CMDS)
    {
        GuestWaitEvent *pEvent = NULL;
        GuestEventTypes eventTypes;
        try
        {
            vrc = registerWaitEvent(eventTypes, &pEvent);
        }
        catch (std::bad_alloc &)
        {
            vrc = VERR_NO_MEMORY;
        }

        if (RT_FAILURE(vrc))
            return vrc;

        /* Prepare HGCM call. */
        VBOXHGCMSVCPARM paParms[4];
        int i = 0;
        HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
        HGCMSvcSetU32(&paParms[i++], mObjectID /* Guest directory handle */);

        vrc = sendMessage(HOST_MSG_DIR_READ, i, paParms);
        if (RT_SUCCESS(vrc))
        {
            vrc = pEvent->Wait(GSTCTL_DEFAULT_TIMEOUT_MS);
            if (RT_SUCCESS(vrc))
            {
                PCALLBACKDATA_DIR_NOTIFY const pDirNotify = (PCALLBACKDATA_DIR_NOTIFY)pEvent->Payload().Raw();
                AssertPtrReturn(pDirNotify, VERR_INVALID_POINTER);
                int vrcGuest = (int)pDirNotify->rc;
                if (RT_SUCCESS(vrcGuest))
                {
                    AssertReturn(pDirNotify->uType == GUEST_DIR_NOTIFYTYPE_READ, VERR_INVALID_PARAMETER);
                    AssertPtrReturn(pDirNotify->u.read.Entry.pDirEntryEx, VERR_INVALID_POINTER);
                    objData.Init(pDirNotify->u.read.Entry.pDirEntryEx->szName);
                    vrc = objData.FromGuestFsObjInfo(&pDirNotify->u.read.Entry.pDirEntryEx->Info,
                                                     pDirNotify->u.read.Entry.pszUser, pDirNotify->u.read.Entry.pszGroups);
                    RTMemFree(pDirNotify->u.read.Entry.pDirEntryEx);
                    RTStrFree(pDirNotify->u.read.Entry.pszUser);
                    RTStrFree(pDirNotify->u.read.Entry.pszGroups);
                }
                else
                {
                    *pvrcGuest = vrcGuest;
                    vrc = VERR_GSTCTL_GUEST_ERROR;
                }
            }
            else if (pEvent->HasGuestError())
                *pvrcGuest = pEvent->GuestResult();
        }
    }
    else
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
    {
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
        vrc = i_readInternalViaToolbox(objData, pvrcGuest);
#else
        RT_NOREF(objData, pvrcGuest);
        vrc = VERR_NOT_SUPPORTED;
#endif
    }

    LogFlowThisFunc(("Returning vrc=%Rrc\n", vrc));
    return vrc;
}

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
/**
 * Reads the next directory entry, internal version (legacy version).
 *
 * @return VBox status code. Will return VERR_NO_MORE_FILES if no more entries are available.
 * @param  objData              Where to store the read directory entry as internal object data.
 * @param  pvrcGuest            Where to store the guest result code in case VERR_GSTCTL_GUEST_ERROR is returned.
 *
 * @note This uses an own guest process via the built-in toolbox in VBoxSerivce.
 */
int GuestDirectory::i_readInternalViaToolbox(GuestFsObjData &objData, int *pvrcGuest)
{
    GuestToolboxStreamBlock curBlock;
    int vrc = mData.mProcessTool.waitEx(GUESTPROCESSTOOL_WAIT_FLAG_STDOUT_BLOCK, &curBlock, pvrcGuest);
    if (RT_SUCCESS(vrc))
    {
        /*
         * Note: The guest process can still be around to serve the next
         *       upcoming stream block next time.
         */
        if (!mData.mProcessTool.isRunning())
            vrc = mData.mProcessTool.getTerminationStatus(); /* Tool process is not running (anymore). Check termination status. */

        if (RT_SUCCESS(vrc))
        {
            if (curBlock.GetCount()) /* Did we get content? */
            {
                if (curBlock.GetString("name"))
                {
                    vrc = objData.FromToolboxLs(curBlock, true /* fLong */);
                }
                else
                {
#ifdef DEBUG
                    curBlock.DumpToLog();
#endif
                    vrc = VERR_PATH_NOT_FOUND;
                }
            }
            else
            {
                /* Nothing to read anymore. Tell the caller. */
                vrc = VERR_NO_MORE_FILES;
            }
        }
    }

    return vrc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */

/**
 * Worker for listing the next directory entries.
 *
 * @return VBox status code.
 * @retval VERR_GSTCTL_GUEST_ERROR / VERR_NO_MORE_FILES if no more entries are available after this iteration.
 *         \a vecObjData will contain the rest of the entries then (if any).
 * @retval VERR_NOT_SUPPORTED if the installed Guest Additions do not support this method.
 * @param  cMaxEntries          How many directory entries to read at max.
 * @param  fFlags               Flags of type GSTCTL_DIRLIST_F_XXX.
 * @param  vecObjData           Where to store the read directory entries on success.
 * @param  pvrcGuest            Where to store the guest result code in case VERR_GSTCTL_GUEST_ERROR is returned.
 */
int GuestDirectory::i_listInternal(uint32_t cMaxEntries, uint32_t fFlags, std::vector<GuestFsObjData> &vecObjData, int *pvrcGuest)
{
    int vrc;

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    if (mSession->i_getParent()->i_getGuestControlFeatures0() & VBOX_GUESTCTRL_GF_0_TOOLBOX_AS_CMDS)
    {
        GuestWaitEvent *pEvent = NULL;
        GuestEventTypes eventTypes;
        try
        {
            vrc = registerWaitEvent(eventTypes, &pEvent);
        }
        catch (std::bad_alloc &)
        {
            vrc = VERR_NO_MEMORY;
        }

        if (RT_FAILURE(vrc))
            return vrc;

        /* Prepare HGCM call. */
        VBOXHGCMSVCPARM paParms[4];
        int i = 0;
        HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
        HGCMSvcSetU32(&paParms[i++], mObjectID /* Directory handle */);
        HGCMSvcSetU32(&paParms[i++], cMaxEntries);
        HGCMSvcSetU32(&paParms[i++], 0 /* Flags */);

        vrc = sendMessage(HOST_MSG_DIR_LIST, i, paParms);
        if (RT_SUCCESS(vrc))
        {
            vrc = pEvent->Wait(GSTCTL_DEFAULT_TIMEOUT_MS);
            if (RT_SUCCESS(vrc))
            {
                PCALLBACKDATA_DIR_NOTIFY const pDirNotify = (PCALLBACKDATA_DIR_NOTIFY)pEvent->Payload().Raw();
                AssertPtrReturn(pDirNotify, VERR_INVALID_POINTER);
                int vrcGuest = (int)pDirNotify->rc;
                if (   RT_SUCCESS(vrcGuest)
                    /* Guest indicates that there are no more entries to read.
                     * We still need to check if we have read something with this iteration though. */
                    || vrcGuest == VERR_NO_MORE_FILES)
                {
                    AssertReturn(pDirNotify->uType == GUEST_DIR_NOTIFYTYPE_LIST, VERR_INVALID_PARAMETER);

                    try
                    {
                        vecObjData.resize(pDirNotify->u.list.cEntries);
                    }
                    catch (std::bad_alloc &)
                    {
                        vrc = VERR_NO_MEMORY;
                    }

                    if (RT_SUCCESS(vrc))
                    {
                        for (size_t a = 0; a < pDirNotify->u.list.cEntries; a++)
                        {
                            PCALLBACKDATA_DIR_ENTRY const pEntry = pDirNotify->u.list.paEntries[a];
                            AssertPtr(pEntry);

                            AssertPtr(pEntry->pDirEntryEx);
                            vecObjData[a].Init(pEntry->pDirEntryEx->szName);
                            int vrc2 = vecObjData[a].FromGuestFsObjInfo(&pEntry->pDirEntryEx->Info, pEntry->pszUser, pEntry->pszGroups);
                            if (RT_SUCCESS(vrc))
                                vrc = vrc2;
                        }
                    }
                }
                else
                {
                    *pvrcGuest = vrcGuest;
                    vrc = VERR_GSTCTL_GUEST_ERROR;
                }

                GuestDirectory::i_dirNotifyDataDestroy(pDirNotify);
            }
            else if (pEvent->HasGuestError())
                *pvrcGuest = pEvent->GuestResult();
        }
    }
    else
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */
    {
        RT_NOREF(cMaxEntries, fFlags, vecObjData, pvrcGuest);
        vrc = VERR_NOT_SUPPORTED;
    }

    return vrc;
}

/**
 * Lists the next directory entries.
 *
 * @return VBox status code.
 * @retval VERR_GSTCTL_GUEST_ERROR / VERR_NO_MORE_FILES if no more entries are available after this iteration.
 *         \a vecObjData will contain the rest of the entries then (if any).
 * @retval VERR_NOT_SUPPORTED if the installed Guest Additions do not support this method.
 * @param  cMaxEntries          How many directory entries to read at max.
 * @param  fFlags               Flags of type GSTCTL_DIRLIST_F_XXX.
 * @param  vecObjInfo           Where to store the read directory entries on success.
 * @param  pvrcGuest            Where to store the guest result code in case VERR_GSTCTL_GUEST_ERROR is returned.
 */
int GuestDirectory::i_listEx(uint32_t cMaxEntries, uint32_t fFlags, std::vector<ComObjPtr<GuestFsObjInfo>> &vecObjInfo,
                             int *pvrcGuest)
{
    AssertPtrReturn(pvrcGuest, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~GSTCTL_DIRLIST_F_VALID_MASK), VERR_INVALID_PARAMETER);

    std::vector<GuestFsObjData> vecObjDataInt;
    int vrc = i_listInternal(cMaxEntries, fFlags, vecObjDataInt, pvrcGuest);
    if (   RT_SUCCESS(vrc)
        || (     vrc       == VERR_GSTCTL_GUEST_ERROR
             && *pvrcGuest == VERR_NO_MORE_FILES))
    {
        try
        {
            vecObjInfo.resize(vecObjDataInt.size());
            for (size_t i = 0; i < vecObjDataInt.size(); i++)
            {
                HRESULT hrc = vecObjInfo[i].createObject();
                ComAssertComRCBreak(hrc, vrc = VERR_COM_UNEXPECTED);

                vrc = vecObjInfo[i]->init(vecObjDataInt[i]);
                if (RT_FAILURE(vrc))
                    break;
            }
        }
        catch (std::bad_alloc &)
        {
            vrc = VERR_NO_MEMORY;
        }
    }

    return vrc;
}

/**
 * Lists the next directory entries.
 *
 * @return VBox status code.
 * @retval VERR_GSTCTL_GUEST_ERROR / VERR_NO_MORE_FILES if no more entries are available after this iteration.
 *         \a vecObjData will contain the rest of the entries then (if any).
 * @retval VERR_NOT_SUPPORTED if the installed Guest Additions do not support this method.
 * @param  cMaxEntries          How many directory entries to read at max.
 * @param  vecObjInfo           Where to store the read directory entries on success.
 * @param  pvrcGuest            Where to store the guest result code in case VERR_GSTCTL_GUEST_ERROR is returned.
 */
int GuestDirectory::i_list(uint32_t cMaxEntries, std::vector<ComObjPtr<GuestFsObjInfo>> &vecObjInfo, int *pvrcGuest)
{
    return i_listEx(cMaxEntries, GSTCTL_DIRLIST_F_NONE, vecObjInfo, pvrcGuest);
}

/**
 * Reads the next directory entry.
 *
 * @return VBox status code.
 * @retval VERR_GSTCTL_GUEST_ERROR / VERR_NO_MORE_FILES if no more entries are available.
 * @param  fsObjInfo            Where to store the read directory entry.
 * @param  pvrcGuest            Where to store the guest result code in case VERR_GSTCTL_GUEST_ERROR is returned.
 */
int GuestDirectory::i_read(ComObjPtr<GuestFsObjInfo> &fsObjInfo, int *pvrcGuest)
{
    AssertPtrReturn(pvrcGuest, VERR_INVALID_POINTER);

    /* Create the FS info object. */
    HRESULT hr = fsObjInfo.createObject();
    if (FAILED(hr))
        return VERR_COM_UNEXPECTED;

    int vrc;

    /* If we have a valid object data cache, read from it. */
    if (mData.mObjData.mName.isNotEmpty())
    {
        vrc = fsObjInfo->init(mData.mObjData);
        if (RT_SUCCESS(vrc))
        {
            mData.mObjData.mName = ""; /* Mark the object data as being empty (beacon). */
        }
    }
    else /* Otherwise ask the guest for the next object data. */
    {
        GuestFsObjData objData;
        vrc = i_readInternal(objData, pvrcGuest);
        if (RT_SUCCESS(vrc))
            vrc = fsObjInfo->init(objData);
    }

    LogFlowThisFunc(("Returning vrc=%Rrc\n", vrc));
    return vrc;
}

/**
 * Rewinds the directory reading.
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param  uTimeoutMS           Timeout (in ms) to wait.
 * @param  pvrcGuest            Where to store the guest result code in case VERR_GSTCTL_GUEST_ERROR is returned.
 */
int GuestDirectory::i_rewind(uint32_t uTimeoutMS, int *pvrcGuest)
{
    RT_NOREF(pvrcGuest);
#ifndef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
    RT_NOREF(uTimeoutMS, pvrcGuest);
#else
    /* Only available for Guest Additions 7.1+. */
    if (mSession->i_getParent()->i_getGuestControlFeatures0() & VBOX_GUESTCTRL_GF_0_TOOLBOX_AS_CMDS)
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

        int vrc;

        GuestWaitEvent *pEvent = NULL;
        GuestEventTypes eventTypes;
        try
        {
            eventTypes.push_back(VBoxEventType_OnGuestDirectoryStateChanged);
            vrc = registerWaitEvent(eventTypes, &pEvent);
        }
        catch (std::bad_alloc &)
        {
            vrc = VERR_NO_MEMORY;
        }

        if (RT_FAILURE(vrc))
            return vrc;

        /* Prepare HGCM call. */
        VBOXHGCMSVCPARM paParms[4];
        int i = 0;
        HGCMSvcSetU32(&paParms[i++], pEvent->ContextID());
        HGCMSvcSetU32(&paParms[i++], mObjectID /* Directory handle */);

        alock.release(); /* Drop lock before sending. */

        vrc = sendMessage(HOST_MSG_DIR_REWIND, i, paParms);
        if (RT_SUCCESS(vrc))
        {
            VBoxEventType_T evtType;
            ComPtr<IEvent> pIEvent;
            vrc = waitForEvent(pEvent, uTimeoutMS, &evtType, pIEvent.asOutParam());
            if (RT_SUCCESS(vrc))
            {
                if (evtType == VBoxEventType_OnGuestDirectoryStateChanged)
                {
                    ComPtr<IGuestDirectoryStateChangedEvent> pEvt = pIEvent;
                    Assert(!pEvt.isNull());
                }
                else
                    vrc = VWRN_GSTCTL_OBJECTSTATE_CHANGED;
            }
            else if (pEvent->HasGuestError()) /* Return guest vrc if available. */
                vrc = pEvent->GuestResult();
        }

        unregisterWaitEvent(pEvent);
        return vrc;
    }
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */

    return VERR_NOT_SUPPORTED;
}

/**
 * Sets the current internal directory object status.
 *
 * @returns VBox status code.
 * @param   enmStatus           New directory status to set.
 * @param   vrcDir              New result code to set.
 *
 * @note    Takes the write lock.
 */
int GuestDirectory::i_setStatus(DirectoryStatus_T enmStatus, int vrcDir)
{
    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("oldStatus=%RU32, newStatus=%RU32, vrcDir=%Rrc\n", mData.mStatus, enmStatus, vrcDir));

#ifdef VBOX_STRICT
    if (enmStatus == DirectoryStatus_Error)
        AssertMsg(RT_FAILURE(vrcDir), ("Guest vrc must be an error (%Rrc)\n", vrcDir));
    else
        AssertMsg(RT_SUCCESS(vrcDir), ("Guest vrc must not be an error (%Rrc)\n", vrcDir));
#endif

    if (mData.mStatus != enmStatus)
    {
        mData.mStatus    = enmStatus;
        mData.mLastError = vrcDir;

        ComObjPtr<VirtualBoxErrorInfo> errorInfo;
        HRESULT hrc = errorInfo.createObject();
        ComAssertComRCRet(hrc, VERR_COM_UNEXPECTED);
        if (RT_FAILURE(vrcDir))
        {
            hrc = errorInfo->initEx(VBOX_E_GSTCTL_GUEST_ERROR, vrcDir,
                                    COM_IIDOF(IGuestDirectory), getComponentName(),
                                    i_guestErrorToString(vrcDir, mData.mOpenInfo.mPath.c_str()));
            ComAssertComRCRet(hrc, VERR_COM_UNEXPECTED);
        }
        /* Note: On vrcDir success, errorInfo is set to S_OK and also sent via the event below. */

        alock.release(); /* Release lock before firing off event. */

        ::FireGuestDirectoryStateChangedEvent(mEventSource, mSession, this, mData.mStatus, errorInfo);
    }

    return VINF_SUCCESS;
}

/**
 * Waits for a guest directory status change.
 *
 * @note Similar code in GuestFile::i_waitForStatusChange().
 *
 * @returns VBox status code.
 * @retval  VERR_GSTCTL_GUEST_ERROR when an error from the guest side has been received.
 * @param   pEvent              Guest wait event to wait for.
 * @param   uTimeoutMS          Timeout (in ms) to wait.
 * @param   penmStatus          Where to return the directoy status on success.
 * @param   prcGuest            Where to return the guest error when VERR_GSTCTL_GUEST_ERROR was returned.
 */
int GuestDirectory::i_waitForStatusChange(GuestWaitEvent *pEvent, uint32_t uTimeoutMS,
                                          DirectoryStatus_T *penmStatus, int *prcGuest)
{
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);
    /* penmStatus is optional. */

    VBoxEventType_T evtType;
    ComPtr<IEvent>  pIEvent;
    int vrc = waitForEvent(pEvent, uTimeoutMS, &evtType, pIEvent.asOutParam());
    if (RT_SUCCESS(vrc))
    {
        AssertReturn(evtType == VBoxEventType_OnGuestDirectoryStateChanged, VERR_WRONG_TYPE);
        ComPtr<IGuestDirectoryStateChangedEvent> pDirectoryEvent = pIEvent;
        AssertReturn(!pDirectoryEvent.isNull(), VERR_COM_UNEXPECTED);

        HRESULT hr;
        if (penmStatus)
        {
            hr = pDirectoryEvent->COMGETTER(Status)(penmStatus);
            ComAssertComRC(hr);
        }

        ComPtr<IVirtualBoxErrorInfo> errorInfo;
        hr = pDirectoryEvent->COMGETTER(Error)(errorInfo.asOutParam());
        ComAssertComRC(hr);

        LONG lGuestRc;
        hr = errorInfo->COMGETTER(ResultDetail)(&lGuestRc);
        ComAssertComRC(hr);

        LogFlowThisFunc(("resultDetail=%RI32 (%Rrc)\n", lGuestRc, lGuestRc));

        if (RT_FAILURE((int)lGuestRc))
            vrc = VERR_GSTCTL_GUEST_ERROR;

        if (prcGuest)
            *prcGuest = (int)lGuestRc;
    }
    /* waitForEvent may also return VERR_GSTCTL_GUEST_ERROR like we do above, so make prcGuest is set. */
    /** @todo Also see todo in GuestFile::i_waitForStatusChange(). */
    else if (vrc == VERR_GSTCTL_GUEST_ERROR && prcGuest)
        *prcGuest = pEvent->GuestResult();
    Assert(vrc != VERR_GSTCTL_GUEST_ERROR || !prcGuest || *prcGuest != (int)0xcccccccc);

    return vrc;
}

// implementation of public methods
/////////////////////////////////////////////////////////////////////////////
HRESULT GuestDirectory::close()
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    LogFlowThisFuncEnter();

    HRESULT hrc = S_OK;

    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_close(&vrcGuest);
    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
            {
                GuestErrorInfo ge(GuestErrorInfo::Type_Directory, vrcGuest, mData.mOpenInfo.mPath.c_str());
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Closing guest directory failed: %s"),
                                   GuestBase::getErrorAsString(ge).c_str());
                break;
            }
            case VERR_NOT_SUPPORTED:
                /* Silently skip old Guest Additions which do not support killing the
                 * the guest directory handling process. */
                break;

            default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                   tr("Closing guest directory \"%s\" failed: %Rrc"), mData.mOpenInfo.mPath.c_str(), vrc);
                break;
        }
    }

    return hrc;
}

HRESULT GuestDirectory::list(ULONG aMaxEntries, std::vector<ComPtr<IFsObjInfo> > &aObjInfos)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    LogFlowThisFuncEnter();

    HRESULT hrc = S_OK;

    std::vector<ComObjPtr<GuestFsObjInfo> > vecObjInfo;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_list(aMaxEntries, vecObjInfo, &vrcGuest);

    /* Don't propagate an error to API callers when this is the last listing (i.e. no more files).
     * For subsequent reads the API caller gets an appropriate error then. */
    if (    vrc      == VERR_GSTCTL_GUEST_ERROR
         && vrcGuest == VERR_NO_MORE_FILES
         && vecObjInfo.size())
            vrc = VINF_SUCCESS;

    if (RT_SUCCESS(vrc))
    {
        try
        {
            aObjInfos.resize(vecObjInfo.size());

            for (size_t i = 0; i < vecObjInfo.size(); i++)
            {
                hrc = vecObjInfo[i].queryInterfaceTo(aObjInfos[i].asOutParam());
                ComAssertComRCBreakRC(hrc);
            }
        }
        catch (std::bad_alloc &)
        {
            hrc = E_OUTOFMEMORY;
        }
    }
    else
    {
        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
            {
                GuestErrorInfo ge(GuestErrorInfo::Type_Directory, vrcGuest, mData.mOpenInfo.mPath.c_str());
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Listing guest directory failed: %s"),
                                   GuestBase::getErrorAsString(ge).c_str());

                /* Return a dedicated error code when directory reading is done. See SDK reference. */
                if (vrcGuest == VERR_NO_MORE_FILES)
                    hrc = VBOX_E_OBJECT_NOT_FOUND;
                break;
            }

            case VERR_NOT_SUPPORTED: /* Returned from i_list(). */
                hrc = VBOX_E_NOT_SUPPORTED;
                break;

            default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Listing guest directory \"%s\" returned unhandled error: %Rrc\n"),
                                   mData.mOpenInfo.mPath.c_str(), vrc);
                break;
        }
    }

    LogFlowThisFunc(("Returning hrc=%Rhrc / vrc=%Rrc\n", hrc, vrc));
    return hrc;
}

HRESULT GuestDirectory::read(ComPtr<IFsObjInfo> &aObjInfo)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    LogFlowThisFuncEnter();

    HRESULT hrc = S_OK;

    ComObjPtr<GuestFsObjInfo> fsObjInfo;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_read(fsObjInfo, &vrcGuest);
    if (RT_SUCCESS(vrc))
    {
        /* Return info object to the caller. */
        hrc = fsObjInfo.queryInterfaceTo(aObjInfo.asOutParam());
    }
    else
    {
        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
            {
                GuestErrorInfo ge(
#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
                                  GuestErrorInfo::Type_ToolLs
#else
                                  GuestErrorInfo::Type_Directory
#endif
                , vrcGuest, mData.mOpenInfo.mPath.c_str());
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Reading guest directory failed: %s"),
                                   GuestBase::getErrorAsString(ge).c_str());

                /* Return a dedicated error code when directory reading is done. See SDK reference. */
                if (vrcGuest == VERR_NO_MORE_FILES)
                    hrc = VBOX_E_OBJECT_NOT_FOUND;
                break;
            }

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
            case VERR_GSTCTL_PROCESS_EXIT_CODE:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Reading guest directory \"%s\" failed: %Rrc"),
                                   mData.mOpenInfo.mPath.c_str(), mData.mProcessTool.getRc());
                break;

            case VERR_PATH_NOT_FOUND:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Reading guest directory \"%s\" failed: Path not found"),
                                   mData.mOpenInfo.mPath.c_str());
                break;

            case VERR_NO_MORE_FILES:
                /* See SDK reference. */
                hrc = setErrorBoth(VBOX_E_OBJECT_NOT_FOUND, vrc, tr("Reading guest directory \"%s\" failed: No more entries"),
                                   mData.mOpenInfo.mPath.c_str());
                break;
#endif
            default:
                hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Reading guest directory \"%s\" returned unhandled error: %Rrc\n"),
                                   mData.mOpenInfo.mPath.c_str(), vrc);
                break;
        }
    }

    LogFlowThisFunc(("Returning hrc=%Rhrc / vrc=%Rrc\n", hrc, vrc));
    return hrc;
}

HRESULT GuestDirectory::rewind(void)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = i_rewind(GSTCTL_DEFAULT_TIMEOUT_MS, &vrcGuest);
    if (RT_SUCCESS(vrc))
        return S_OK;

    GuestErrorInfo ge(GuestErrorInfo::Type_Directory, vrcGuest, mData.mOpenInfo.mPath.c_str());
    return setErrorBoth(VBOX_E_IPRT_ERROR, vrcGuest, tr("Rewinding guest directory failed: %s"),
                       GuestBase::getErrorAsString(ge).c_str());
}

