/* $Id: GuestDnDSourceImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Console COM Class implementation - Guest drag and drop source.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_GUEST_DND //LOG_GROUP_MAIN_GUESTDNDSOURCE
#include "LoggingNew.h"

#include "GuestImpl.h"
#include "GuestDnDSourceImpl.h"
#include "GuestDnDPrivate.h"
#include "ConsoleImpl.h"

#include "Global.h"
#include "AutoCaller.h"
#include "ThreadTask.h"

#include <iprt/asm.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/uri.h>

#include <iprt/cpp/utils.h> /* For unconst(). */

#include <VBox/com/array.h>


/**
 * Base class for a source task.
 */
class GuestDnDSourceTask : public ThreadTask
{
public:

    GuestDnDSourceTask(GuestDnDSource *pSource)
        : ThreadTask("GenericGuestDnDSourceTask")
        , mSource(pSource)
        , mRC(VINF_SUCCESS) { }

    virtual ~GuestDnDSourceTask(void) { }

    /** Returns the overall result of the task. */
    int getRC(void) const { return mRC; }
    /** Returns if the overall result of the task is ok (succeeded) or not. */
    bool isOk(void) const { return RT_SUCCESS(mRC); }

protected:

    /** COM object pointer to the parent (source). */
    const ComObjPtr<GuestDnDSource>     mSource;
    /** Overall result of the task. */
    int                                 mRC;
};

/**
 * Task structure for receiving data from a source using
 * a worker thread.
 */
class GuestDnDRecvDataTask : public GuestDnDSourceTask
{
public:

    GuestDnDRecvDataTask(GuestDnDSource *pSource, GuestDnDRecvCtx *pCtx)
        : GuestDnDSourceTask(pSource)
        , mpCtx(pCtx)
    {
        m_strTaskName = "dndSrcRcvData";
    }

    void handler()
    {
        LogFlowThisFunc(("\n"));

        const ComObjPtr<GuestDnDSource> pThis(mSource);
        Assert(!pThis.isNull());

        AutoCaller autoCaller(pThis);
        if (FAILED(autoCaller.hrc()))
            return;

        int vrc = pThis->i_receiveData(mpCtx, RT_INDEFINITE_WAIT /* msTimeout */);
        if (RT_FAILURE(vrc)) /* In case we missed some error handling within i_receiveData(). */
        {
            if (vrc != VERR_CANCELLED)
                LogRel(("DnD: Receiving data from guest failed with %Rrc\n", vrc));

            /* Make sure to fire a cancel request to the guest side in case something went wrong. */
            pThis->sendCancel();
        }
    }

    virtual ~GuestDnDRecvDataTask(void) { }

protected:

    /** Pointer to receive data context. */
    GuestDnDRecvCtx *mpCtx;
};

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

GuestDnDSource::GuestDnDSource(void)
    : GuestDnDBase(this) { }

GuestDnDSource::~GuestDnDSource(void) { }

HRESULT GuestDnDSource::FinalConstruct(void)
{
    /*
     * Set the maximum block size this source can handle to 64K. This always has
     * been hardcoded until now.
     *
     * Note: Never ever rely on information from the guest; the host dictates what and
     *       how to do something, so try to negogiate a sensible value here later.
     */
    mData.mcbBlockSize = DND_DEFAULT_CHUNK_SIZE; /** @todo Make this configurable. */

    LogFlowThisFunc(("\n"));
    return BaseFinalConstruct();
}

void GuestDnDSource::FinalRelease(void)
{
    LogFlowThisFuncEnter();
    uninit();
    BaseFinalRelease();
    LogFlowThisFuncLeave();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

HRESULT GuestDnDSource::init(const ComObjPtr<Guest>& pGuest)
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition NotReady->InInit->Ready. */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(m_pGuest) = pGuest;

    /* Set the response we're going to use for this object.
     *
     * At the moment we only have one response total, as we
     * don't allow
     *      1) parallel transfers (multiple G->H at the same time)
     *  nor 2) mixed transfers (G->H + H->G at the same time).
     */
    m_pState = GuestDnDInst()->getState();
    AssertPtrReturn(m_pState, E_POINTER);

    /* Confirm a successful initialization when it's the case. */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Uninitializes the instance.
 * Called from FinalRelease().
 */
void GuestDnDSource::uninit(void)
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady. */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;
}

// implementation of wrapped IDnDBase methods.
/////////////////////////////////////////////////////////////////////////////

HRESULT GuestDnDSource::isFormatSupported(const com::Utf8Str &aFormat, BOOL *aSupported)
{
#if !defined(VBOX_WITH_DRAG_AND_DROP) || !defined(VBOX_WITH_DRAG_AND_DROP_GH)
    ReturnComNotImplemented();
#else /* VBOX_WITH_DRAG_AND_DROP */

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aSupported = GuestDnDBase::i_isFormatSupported(aFormat) ? TRUE : FALSE;

    return S_OK;
#endif /* VBOX_WITH_DRAG_AND_DROP */
}

HRESULT GuestDnDSource::getFormats(GuestDnDMIMEList &aFormats)
{
#if !defined(VBOX_WITH_DRAG_AND_DROP) || !defined(VBOX_WITH_DRAG_AND_DROP_GH)
    ReturnComNotImplemented();
#else /* VBOX_WITH_DRAG_AND_DROP */

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    aFormats = GuestDnDBase::i_getFormats();

    return S_OK;
#endif /* VBOX_WITH_DRAG_AND_DROP */
}

HRESULT GuestDnDSource::addFormats(const GuestDnDMIMEList &aFormats)
{
#if !defined(VBOX_WITH_DRAG_AND_DROP) || !defined(VBOX_WITH_DRAG_AND_DROP_GH)
    ReturnComNotImplemented();
#else /* VBOX_WITH_DRAG_AND_DROP */

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    return GuestDnDBase::i_addFormats(aFormats);
#endif /* VBOX_WITH_DRAG_AND_DROP */
}

HRESULT GuestDnDSource::removeFormats(const GuestDnDMIMEList &aFormats)
{
#if !defined(VBOX_WITH_DRAG_AND_DROP) || !defined(VBOX_WITH_DRAG_AND_DROP_GH)
    ReturnComNotImplemented();
#else /* VBOX_WITH_DRAG_AND_DROP */

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    return GuestDnDBase::i_removeFormats(aFormats);
#endif /* VBOX_WITH_DRAG_AND_DROP */
}

// implementation of wrapped IDnDSource methods.
/////////////////////////////////////////////////////////////////////////////

HRESULT GuestDnDSource::dragIsPending(ULONG uScreenId, GuestDnDMIMEList &aFormats,
                                      std::vector<DnDAction_T> &aAllowedActions, DnDAction_T *aDefaultAction)
{
#if !defined(VBOX_WITH_DRAG_AND_DROP) || !defined(VBOX_WITH_DRAG_AND_DROP_GH)
    ReturnComNotImplemented();
#else /* VBOX_WITH_DRAG_AND_DROP */

    /* aDefaultAction is optional. */

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    /* Default is ignoring the action. */
    if (aDefaultAction)
        *aDefaultAction = DnDAction_Ignore;

    GuestDnDState *pState = GuestDnDInst()->getState();
    AssertPtr(pState);

    /* Check if any operation is active, and if so, bail out, returning an ignore action (see above). */
    if (pState->get() != VBOXDNDSTATE_UNKNOWN)
        return S_OK;

    pState->set(VBOXDNDSTATE_QUERY_FORMATS);

    HRESULT hrc = S_OK;

    GuestDnDMsg Msg;
    Msg.setType(HOST_DND_FN_GH_REQ_PENDING);
    if (m_pState->m_uProtocolVersion >= 3)
        Msg.appendUInt32(0); /** @todo ContextID not used yet. */
    Msg.appendUInt32(uScreenId);

    int vrc = GuestDnDInst()->hostCall(Msg.getType(), Msg.getCount(), Msg.getParms());
    if (RT_SUCCESS(vrc))
    {
        int vrcGuest;
        vrc = pState->waitForGuestResponseEx(100 /* Timeout in ms */, &vrcGuest);
        if (RT_SUCCESS(vrc))
        {
            if (!isDnDIgnoreAction(pState->getActionDefault()))
            {
                /*
                 * In the GuestDnDSource case the source formats are from the guest,
                 * as GuestDnDSource acts as a target for the guest. The host always
                 * dictates what's supported and what's not, so filter out all formats
                 * which are not supported by the host.
                 */
                GuestDnDMIMEList const &lstGuest     = pState->formats();
                GuestDnDMIMEList const  lstFiltered  = GuestDnD::toFilteredFormatList(m_lstFmtSupported, lstGuest);
                if (lstFiltered.size())
                {
                    LogRel2(("DnD: Host offered the following formats:\n"));
                    for (size_t i = 0; i < lstFiltered.size(); i++)
                        LogRel2(("DnD:\tFormat #%zu: %s\n", i, lstFiltered.at(i).c_str()));

                    aFormats            = lstFiltered;
                    aAllowedActions     = GuestDnD::toMainActions(pState->getActionsAllowed());
                    if (aDefaultAction)
                        *aDefaultAction = GuestDnD::toMainAction(pState->getActionDefault());

                    /* Apply the (filtered) formats list. */
                    m_lstFmtOffered     = lstFiltered;
                }
                else
                {
                    bool fSetError = true; /* Whether to set an error and reset or not. */

                    /*
                     * HACK ALERT: As we now expose an error (via i_setErrorAndReset(), see below) back to the API client, we
                     *             have to add a kludge here. Older X11-based Guest Additions report "TARGETS, MULTIPLE" back
                     *             to us, even if they don't offer any other *supported* formats of the host. This then in turn
                     *             would lead to exposing an error, whereas we just should ignore those specific X11-based
                     *             formats. For anything other we really want to be notified by setting an error though.
                     */
                    if (   lstGuest.size() == 2
                        && GuestDnD::isFormatInFormatList("TARGETS",  lstGuest)
                        && GuestDnD::isFormatInFormatList("MULTIPLE", lstGuest))
                    {
                        fSetError = false;
                    }
                    /* HACK ALERT END */

                    if (fSetError)
                        hrc = i_setErrorAndReset(tr("Negotiation of formats between guest and host failed!\n\nHost offers: %s\n\nGuest offers: %s"),
                                                 GuestDnD::toFormatString(m_lstFmtSupported , ",").c_str(),
                                                 GuestDnD::toFormatString(pState->formats() , ",").c_str());
                    else /* Just silently reset. */
                        i_reset();
                }
            }
            /* Note: Don't report an error here when the action is "ignore" -- that only means that the current window on the guest
                     simply doesn't support the format or drag and drop at all. */
        }
        else
            hrc = i_setErrorAndReset(vrc == VERR_DND_GUEST_ERROR ? vrcGuest : vrc, tr("Requesting pending data from guest failed"));
    }
    else
    {
        switch (vrc)
        {
            case VERR_ACCESS_DENIED:
            {
                hrc = i_setErrorAndReset(tr("Dragging from guest to host not allowed -- make sure that the correct drag'n drop mode is set"));
                break;
            }

            case VERR_NOT_SUPPORTED:
            {
                hrc = i_setErrorAndReset(tr("Dragging from guest to host not supported by guest -- make sure that the Guest Additions are properly installed and running"));
                break;
            }

            default:
            {
                hrc = i_setErrorAndReset(vrc, tr("Sending drag pending event to guest failed"));
                break;
            }
        }
    }

    pState->set(VBOXDNDSTATE_UNKNOWN);

    LogFlowFunc(("hrc=%Rhrc\n", hrc));
    return hrc;
#endif /* VBOX_WITH_DRAG_AND_DROP */
}

HRESULT GuestDnDSource::drop(const com::Utf8Str &aFormat, DnDAction_T aAction, ComPtr<IProgress> &aProgress)
{
#if !defined(VBOX_WITH_DRAG_AND_DROP) || !defined(VBOX_WITH_DRAG_AND_DROP_GH)
    ReturnComNotImplemented();
#else /* VBOX_WITH_DRAG_AND_DROP */

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    LogFunc(("aFormat=%s, aAction=%RU32\n", aFormat.c_str(), aAction));

    /* Input validation. */
    if (RT_UNLIKELY((aFormat.c_str()) == NULL || *(aFormat.c_str()) == '\0'))
        return setError(E_INVALIDARG, tr("No drop format specified"));

    /* Is the specified format in our list of (left over) offered formats? */
    if (!GuestDnD::isFormatInFormatList(aFormat, m_lstFmtOffered))
        return setError(E_INVALIDARG, tr("Specified format '%s' is not supported"), aFormat.c_str());

    /* Check that the given action is supported by us. */
    VBOXDNDACTION dndAction = GuestDnD::toHGCMAction(aAction);
    if (isDnDIgnoreAction(dndAction)) /* If there is no usable action, ignore this request. */
        return S_OK;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Check if this object still is in a pending state and bail out if so. */
    if (m_fIsPending)
        return setError(E_FAIL, tr("Current drop operation to host still in progress"));

    /* Reset our internal state. */
    i_reset();

    /* At the moment we only support one transfer at a time. */
    if (GuestDnDInst()->getSourceCount())
        return setError(E_INVALIDARG, tr("Another drag and drop operation to the host already is in progress"));

    /* Reset progress object. */
    GuestDnDState *pState = GuestDnDInst()->getState();
    AssertPtr(pState);
    HRESULT hrc = pState->resetProgress(m_pGuest, tr("Dropping data to host"));
    if (FAILED(hrc))
        return hrc;

    GuestDnDRecvDataTask *pTask = NULL;

    try
    {
        mData.mRecvCtx.pSource       = this;
        mData.mRecvCtx.pState        = pState;
        mData.mRecvCtx.enmAction     = dndAction;
        mData.mRecvCtx.strFmtReq     = aFormat;
        mData.mRecvCtx.lstFmtOffered = m_lstFmtOffered;

        LogRel2(("DnD: Requesting data from guest in format '%s'\n", aFormat.c_str()));

        pTask = new GuestDnDRecvDataTask(this, &mData.mRecvCtx);
        if (!pTask->isOk())
        {
            delete pTask;
            LogRel2(("DnD: Receive data task failed to initialize\n"));
            throw hrc = E_FAIL;
        }

        /* Drop write lock before creating thread. */
        alock.release();

        /* This function delete pTask in case of exceptions,
         * so there is no need in the call of delete operator. */
        hrc = pTask->createThreadWithType(RTTHREADTYPE_MAIN_WORKER);
        pTask = NULL;  /* Note: pTask is now owned by the worker thread. */
    }
    catch (std::bad_alloc &)
    {
        hrc = E_OUTOFMEMORY;
    }
    catch (...)
    {
        LogRel2(("DnD: Could not create thread for data receiving task\n"));
        hrc = E_FAIL;
    }

    if (SUCCEEDED(hrc))
    {
        /* Register ourselves at the DnD manager. */
        GuestDnDInst()->registerSource(this);

        hrc = pState->queryProgressTo(aProgress.asOutParam());
        ComAssertComRC(hrc);
    }
    else
        hrc = i_setErrorAndReset(tr("Starting thread for GuestDnDSource failed (%Rhrc)"), hrc);

    LogFlowFunc(("Returning hrc=%Rhrc\n", hrc));
    return hrc;
#endif /* VBOX_WITH_DRAG_AND_DROP */
}

HRESULT GuestDnDSource::receiveData(std::vector<BYTE> &aData)
{
#if !defined(VBOX_WITH_DRAG_AND_DROP) || !defined(VBOX_WITH_DRAG_AND_DROP_GH)
    ReturnComNotImplemented();
#else /* VBOX_WITH_DRAG_AND_DROP */

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Don't allow receiving the actual data until our current transfer is complete. */
    if (m_fIsPending)
        return setError(E_FAIL, tr("Current drop operation to host still in progress"));

    HRESULT hrc = S_OK;

    try
    {
        GuestDnDRecvCtx *pCtx = &mData.mRecvCtx;
        if (DnDMIMENeedsDropDir(pCtx->strFmtRecv.c_str(), pCtx->strFmtRecv.length()))
        {
            PDNDDROPPEDFILES pDF = &pCtx->Transfer.DroppedFiles;

            const char *pcszDropDirAbs = DnDDroppedFilesGetDirAbs(pDF);
            AssertPtr(pcszDropDirAbs);

            LogRel2(("DnD: Using drop directory '%s', got %RU64 root entries\n",
                     pcszDropDirAbs, DnDTransferListGetRootCount(&pCtx->Transfer.List)));

            /* We return the data as "text/uri-list" MIME data here. */
            char  *pszBuf = NULL;
            size_t cbBuf  = 0;
            int vrc = DnDTransferListGetRootsEx(&pCtx->Transfer.List, DNDTRANSFERLISTFMT_URI,
                                                pcszDropDirAbs, DND_PATH_SEPARATOR_STR, &pszBuf, &cbBuf);
            if (RT_SUCCESS(vrc))
            {
                Assert(cbBuf);
                AssertPtr(pszBuf);

                aData.resize(cbBuf);
                memcpy(&aData.front(), pszBuf, cbBuf);
                RTStrFree(pszBuf);
            }
            else
                LogRel(("DnD: Unable to build source root list, vrc=%Rrc\n", vrc));
        }
        else /* Raw data. */
        {
            if (pCtx->Meta.cbData)
            {
                /* Copy the data into a safe array of bytes. */
                aData.resize(pCtx->Meta.cbData);
                memcpy(&aData.front(), pCtx->Meta.pvData, pCtx->Meta.cbData);
            }
            else
                aData.resize(0);
        }
    }
    catch (std::bad_alloc &)
    {
        hrc = E_OUTOFMEMORY;
    }

    LogFlowFunc(("Returning hrc=%Rhrc\n", hrc));
    return hrc;
#endif /* VBOX_WITH_DRAG_AND_DROP */
}

// implementation of internal methods.
/////////////////////////////////////////////////////////////////////////////

/**
 * Returns an error string from a guest DnD error.
 *
 * @returns Error string.
 * @param   guestRc             Guest error to return error string for.
 */
/* static */
Utf8Str GuestDnDSource::i_guestErrorToString(int guestRc)
{
    Utf8Str strError;

    switch (guestRc)
    {
        case VERR_ACCESS_DENIED:
            strError += Utf8StrFmt(tr("For one or more guest files or directories selected for transferring to the host your guest "
                                      "user does not have the appropriate access rights for. Please make sure that all selected "
                                      "elements can be accessed and that your guest user has the appropriate rights"));
            break;

        case VERR_NOT_FOUND:
            /* Should not happen due to file locking on the guest, but anyway ... */
            strError += Utf8StrFmt(tr("One or more guest files or directories selected for transferring to the host were not"
                                      "found on the guest anymore. This can be the case if the guest files were moved and/or"
                                      "altered while the drag and drop operation was in progress"));
            break;

        case VERR_SHARING_VIOLATION:
            strError += Utf8StrFmt(tr("One or more guest files or directories selected for transferring to the host were locked. "
                                      "Please make sure that all selected elements can be accessed and that your guest user has "
                                      "the appropriate rights"));
            break;

        case VERR_TIMEOUT:
            strError += Utf8StrFmt(tr("The guest was not able to retrieve the drag and drop data within time"));
            break;

        default:
            strError += Utf8StrFmt(tr("Drag and drop error from guest (%Rrc)"), guestRc);
            break;
    }

    return strError;
}

/**
 * Returns an error string from a host DnD error.
 *
 * @returns Error string.
 * @param   hostRc              Host error to return error string for.
 */
/* static */
Utf8Str GuestDnDSource::i_hostErrorToString(int hostRc)
{
    Utf8Str strError;

    switch (hostRc)
    {
        case VERR_ACCESS_DENIED:
            strError += Utf8StrFmt(tr("For one or more host files or directories selected for transferring to the guest your host "
                                      "user does not have the appropriate access rights for. Please make sure that all selected "
                                      "elements can be accessed and that your host user has the appropriate rights."));
            break;

        case VERR_DISK_FULL:
            strError += Utf8StrFmt(tr("Host disk ran out of space (disk is full)."));
            break;

        case VERR_NOT_FOUND:
            /* Should not happen due to file locking on the host, but anyway ... */
            strError += Utf8StrFmt(tr("One or more host files or directories selected for transferring to the host were not"
                                      "found on the host anymore. This can be the case if the host files were moved and/or"
                                      "altered while the drag and drop operation was in progress."));
            break;

        case VERR_SHARING_VIOLATION:
            strError += Utf8StrFmt(tr("One or more host files or directories selected for transferring to the guest were locked. "
                                      "Please make sure that all selected elements can be accessed and that your host user has "
                                      "the appropriate rights."));
            break;

        default:
            strError += Utf8StrFmt(tr("Drag and drop error from host (%Rrc)"), hostRc);
            break;
    }

    return strError;
}

/**
 * Resets all internal data and state.
 */
void GuestDnDSource::i_reset(void)
{
    LogRel2(("DnD: Source reset\n"));

    mData.mRecvCtx.reset();

    m_fIsPending = false;

    /* Unregister ourselves from the DnD manager. */
    GuestDnDInst()->unregisterSource(this);
}

#ifdef VBOX_WITH_DRAG_AND_DROP_GH

/**
 * Handles receiving a send data header from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Receive context to use.
 * @param   pDataHdr            Pointer to send data header from the guest.
 */
int GuestDnDSource::i_onReceiveDataHdr(GuestDnDRecvCtx *pCtx, PVBOXDNDSNDDATAHDR pDataHdr)
{
    AssertPtrReturn(pCtx,     VERR_INVALID_POINTER);
    AssertPtrReturn(pDataHdr, VERR_INVALID_POINTER);

    LogRel2(("DnD: Receiving %RU64 bytes total data (%RU32 bytes meta data, %RU64 objects) from guest ...\n",
             pDataHdr->cbTotal, pDataHdr->cbMeta, pDataHdr->cObjects));

    AssertReturn(pDataHdr->cbTotal >= pDataHdr->cbMeta, VERR_INVALID_PARAMETER);

    pCtx->Meta.cbAnnounced = pDataHdr->cbMeta;
    pCtx->cbExtra          = pDataHdr->cbTotal - pDataHdr->cbMeta;

    Assert(pCtx->Transfer.cObjToProcess == 0); /* Sanity. */
    Assert(pCtx->Transfer.cObjProcessed == 0);

    pCtx->Transfer.reset();

    pCtx->Transfer.cObjToProcess = pDataHdr->cObjects;

    /** @todo Handle compression type. */
    /** @todo Handle checksum type. */

    LogFlowFuncLeave();
    return VINF_SUCCESS;
}

/**
 * Main function for receiving data from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Receive context to use.
 * @param   pSndData            Pointer to send data block from the guest.
 */
int GuestDnDSource::i_onReceiveData(GuestDnDRecvCtx *pCtx, PVBOXDNDSNDDATA pSndData)
{
    AssertPtrReturn(pCtx,     VERR_INVALID_POINTER);
    AssertPtrReturn(pSndData, VERR_INVALID_POINTER);

    int vrc = VINF_SUCCESS;

    try
    {
        GuestDnDTransferRecvData *pTransfer = &pCtx->Transfer;

        size_t  cbData;
        void   *pvData;
        size_t  cbTotalAnnounced;
        size_t  cbMetaAnnounced;

        if (m_pState->m_uProtocolVersion < 3)
        {
            cbData  = pSndData->u.v1.cbData;
            pvData  = pSndData->u.v1.pvData;

            /* Sends the total data size to receive for every data chunk. */
            cbTotalAnnounced = pSndData->u.v1.cbTotalSize;

            /* Meta data size always is cbData, meaning there cannot be an
             * extended data chunk transfer by sending further data. */
            cbMetaAnnounced  = cbData;
        }
        else
        {
            cbData  = pSndData->u.v3.cbData;
            pvData  = pSndData->u.v3.pvData;

            /* Note: Data sizes get initialized in i_onReceiveDataHdr().
             *       So just use the set values here. */
            cbTotalAnnounced = pCtx->getTotalAnnounced();
            cbMetaAnnounced  = pCtx->Meta.cbAnnounced;
        }

        if (cbData > cbTotalAnnounced)
        {
            AssertMsgFailed(("Incoming data size invalid: cbData=%zu, cbTotal=%zu\n", cbData, cbTotalAnnounced));
            vrc = VERR_INVALID_PARAMETER;
        }
        else if (   cbTotalAnnounced == 0
                 || cbTotalAnnounced  < cbMetaAnnounced)
        {
            AssertMsgFailed(("cbTotal (%zu) is smaller than cbMeta (%zu)\n", cbTotalAnnounced, cbMetaAnnounced));
            vrc = VERR_INVALID_PARAMETER;
        }

        if (RT_FAILURE(vrc))
            return vrc;

        AssertReturn(cbData <= mData.mcbBlockSize, VERR_BUFFER_OVERFLOW);

        const size_t cbMetaRecv = pCtx->Meta.add(pvData, cbData);
        AssertReturn(cbMetaRecv <= pCtx->Meta.cbData, VERR_BUFFER_OVERFLOW);

        LogFlowThisFunc(("cbData=%zu, cbMetaRecv=%zu, cbMetaAnnounced=%zu, cbTotalAnnounced=%zu\n",
                         cbData, cbMetaRecv, cbMetaAnnounced, cbTotalAnnounced));

        LogRel2(("DnD: %RU8%% of meta data complete (%zu/%zu bytes)\n",
                 (uint8_t)(cbMetaRecv * 100 / RT_MAX(cbMetaAnnounced, 1)), cbMetaRecv, cbMetaAnnounced));

        /*
         * (Meta) Data transfer complete?
         */
        if (cbMetaAnnounced == cbMetaRecv)
        {
            LogRel2(("DnD: Receiving meta data complete\n"));

            if (DnDMIMENeedsDropDir(pCtx->strFmtRecv.c_str(), pCtx->strFmtRecv.length()))
            {
                vrc = DnDTransferListInitEx(&pTransfer->List,
                                            DnDDroppedFilesGetDirAbs(&pTransfer->DroppedFiles), DNDTRANSFERLISTFMT_NATIVE);
                if (RT_SUCCESS(vrc))
                    vrc = DnDTransferListAppendRootsFromBuffer(&pTransfer->List, DNDTRANSFERLISTFMT_URI,
                                                               (const char *)pCtx->Meta.pvData, pCtx->Meta.cbData,
                                                               DND_PATH_SEPARATOR_STR, DNDTRANSFERLIST_FLAGS_NONE);
                /* Validation. */
                if (RT_SUCCESS(vrc))
                {
                    uint64_t cRoots = DnDTransferListGetRootCount(&pTransfer->List);

                    LogRel2(("DnD: Received %RU64 root entries from guest\n", cRoots));

                    if (   cRoots == 0
                        || cRoots > pTransfer->cObjToProcess)
                    {
                        LogRel(("DnD: Number of root entries invalid / mismatch: Got %RU64, expected %RU64\n",
                                cRoots, pTransfer->cObjToProcess));
                        vrc = VERR_INVALID_PARAMETER;
                    }
                }

                if (RT_SUCCESS(vrc))
                {
                    /* Update our process with the data we already received. */
                    vrc = updateProgress(pCtx, pCtx->pState, cbMetaAnnounced);
                    AssertRC(vrc);
                }

                if (RT_FAILURE(vrc))
                    LogRel(("DnD: Error building root entry list, vrc=%Rrc\n", vrc));
            }
            else /* Raw data. */
            {
                vrc = updateProgress(pCtx, pCtx->pState, cbData);
                AssertRC(vrc);
            }

            if (RT_FAILURE(vrc))
                LogRel(("DnD: Error receiving meta data, vrc=%Rrc\n", vrc));
        }
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}


int GuestDnDSource::i_onReceiveDir(GuestDnDRecvCtx *pCtx, const char *pszPath, uint32_t cbPath, uint32_t fMode)
{
    AssertPtrReturn(pCtx,    VERR_INVALID_POINTER);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(cbPath,     VERR_INVALID_PARAMETER);

    LogFlowFunc(("pszPath=%s, cbPath=%RU32, fMode=0x%x\n", pszPath, cbPath, fMode));

    const PDNDTRANSFEROBJECT pObj = &pCtx->Transfer.ObjCur;
    const PDNDDROPPEDFILES   pDF  = &pCtx->Transfer.DroppedFiles;

    int vrc = DnDTransferObjectInitEx(pObj, DNDTRANSFEROBJTYPE_DIRECTORY, DnDDroppedFilesGetDirAbs(pDF), pszPath);
    if (RT_SUCCESS(vrc))
    {
        const char *pcszPathAbs = DnDTransferObjectGetSourcePath(pObj);
        AssertPtr(pcszPathAbs);

        vrc = RTDirCreateFullPath(pcszPathAbs, fMode);
        if (RT_SUCCESS(vrc))
        {
            pCtx->Transfer.cObjProcessed++;
            if (pCtx->Transfer.cObjProcessed <= pCtx->Transfer.cObjToProcess)
                vrc = DnDDroppedFilesAddDir(pDF, pcszPathAbs);
            else
                vrc = VERR_TOO_MUCH_DATA;

            DnDTransferObjectDestroy(pObj);

            if (RT_FAILURE(vrc))
                LogRel2(("DnD: Created guest directory '%s' on host\n", pcszPathAbs));
        }
        else
            LogRel(("DnD: Error creating guest directory '%s' on host, vrc=%Rrc\n", pcszPathAbs, vrc));
    }

    if (RT_FAILURE(vrc))
        LogRel(("DnD: Receiving guest directory '%s' failed with vrc=%Rrc\n", pszPath, vrc));

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Receives a file header from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Receive context to use.
 * @param   pszPath             File path of file to use.
 * @param   cbPath              Size (in bytes, including terminator) of file path.
 * @param   cbSize              File size (in bytes) to receive.
 * @param   fMode               File mode to use.
 * @param   fFlags              Additional receive flags; not used yet.
 */
int GuestDnDSource::i_onReceiveFileHdr(GuestDnDRecvCtx *pCtx, const char *pszPath, uint32_t cbPath,
                                       uint64_t cbSize, uint32_t fMode, uint32_t fFlags)
{
    AssertPtrReturn(pCtx,    VERR_INVALID_POINTER);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(cbPath,     VERR_INVALID_PARAMETER);
    AssertReturn(fMode,      VERR_INVALID_PARAMETER);
    /* fFlags are optional. */

    RT_NOREF(fFlags);

    LogFlowFunc(("pszPath=%s, cbPath=%RU32, cbSize=%RU64, fMode=0x%x, fFlags=0x%x\n", pszPath, cbPath, cbSize, fMode, fFlags));

    AssertMsgReturn(cbSize <= pCtx->cbExtra,
                    ("File size (%RU64) exceeds extra size to transfer (%RU64)\n", cbSize, pCtx->cbExtra), VERR_INVALID_PARAMETER);
    AssertMsgReturn(   pCtx->isComplete() == false
                    && pCtx->Transfer.cObjToProcess,
                    ("Data transfer already complete, bailing out\n"), VERR_INVALID_PARAMETER);

    int vrc = VINF_SUCCESS;

    do
    {
        const PDNDTRANSFEROBJECT pObj = &pCtx->Transfer.ObjCur;

        if (    DnDTransferObjectIsOpen(pObj)
            && !DnDTransferObjectIsComplete(pObj))
        {
            AssertMsgFailed(("Object '%s' not complete yet\n", DnDTransferObjectGetSourcePath(pObj)));
            vrc = VERR_WRONG_ORDER;
            break;
        }

        const PDNDDROPPEDFILES pDF = &pCtx->Transfer.DroppedFiles;

        vrc = DnDTransferObjectInitEx(pObj, DNDTRANSFEROBJTYPE_FILE, DnDDroppedFilesGetDirAbs(pDF), pszPath);
        AssertRCBreak(vrc);

        const char *pcszSource = DnDTransferObjectGetSourcePath(pObj);
        AssertPtrBreakStmt(pcszSource, vrc = VERR_INVALID_POINTER);

        /** @todo Add sparse file support based on fFlags? (Use Open(..., fFlags | SPARSE). */
        vrc = DnDTransferObjectOpen(pObj, RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_WRITE,
                                   (fMode & RTFS_UNIX_MASK) | RTFS_UNIX_IRUSR | RTFS_UNIX_IWUSR, DNDTRANSFEROBJECT_FLAGS_NONE);
        if (RT_FAILURE(vrc))
        {
            LogRel(("DnD: Error opening/creating guest file '%s' on host, vrc=%Rrc\n", pcszSource, vrc));
            break;
        }

        /* Note: Protocol v1 does not send any file sizes, so always 0. */
        if (m_pState->m_uProtocolVersion >= 2)
            vrc = DnDTransferObjectSetSize(pObj, cbSize);

        /** @todo Unescape path before printing. */
        LogRel2(("DnD: Transferring guest file '%s' to host (%RU64 bytes, mode %#x)\n",
                 pcszSource, DnDTransferObjectGetSize(pObj), DnDTransferObjectGetMode(pObj)));

        /** @todo Set progress object title to current file being transferred? */

        if (DnDTransferObjectIsComplete(pObj)) /* 0-byte file? We're done already. */
        {
            LogRel2(("DnD: Transferring guest file '%s' (0 bytes) to host complete\n", pcszSource));

            pCtx->Transfer.cObjProcessed++;
            if (pCtx->Transfer.cObjProcessed <= pCtx->Transfer.cObjToProcess)
            {
                /* Add for having a proper rollback. */
                vrc = DnDDroppedFilesAddFile(pDF, pcszSource);
            }
            else
                vrc = VERR_TOO_MUCH_DATA;

            DnDTransferObjectDestroy(pObj);
        }

    } while (0);

    if (RT_FAILURE(vrc))
        LogRel(("DnD: Error receiving guest file header, vrc=%Rrc\n", vrc));

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Receives file data from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Receive context to use.
 * @param   pvData              Pointer to file data received from the guest.
 * @param   pCtx                Size (in bytes) of file data received from the guest.
 */
int GuestDnDSource::i_onReceiveFileData(GuestDnDRecvCtx *pCtx, const void *pvData, uint32_t cbData)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData, VERR_INVALID_PARAMETER);

    int vrc = VINF_SUCCESS;

    LogFlowFunc(("pvData=%p, cbData=%RU32, cbBlockSize=%RU32\n", pvData, cbData, mData.mcbBlockSize));

    /*
     * Sanity checking.
     */
    if (cbData > mData.mcbBlockSize)
        return VERR_INVALID_PARAMETER;

    do
    {
        const PDNDTRANSFEROBJECT pObj = &pCtx->Transfer.ObjCur;

        const char *pcszSource = DnDTransferObjectGetSourcePath(pObj);
        AssertPtrBreakStmt(pcszSource, vrc = VERR_INVALID_POINTER);

        AssertMsgReturn(DnDTransferObjectIsOpen(pObj),
                        ("Object '%s' not open (anymore)\n", pcszSource), VERR_WRONG_ORDER);
        AssertMsgReturn(DnDTransferObjectIsComplete(pObj) == false,
                        ("Object '%s' already marked as complete\n", pcszSource), VERR_WRONG_ORDER);

        uint32_t cbWritten;
        vrc = DnDTransferObjectWrite(pObj, pvData, cbData, &cbWritten);
        if (RT_FAILURE(vrc))
            LogRel(("DnD: Error writing guest file data for '%s', vrc=%Rrc\n", pcszSource, vrc));

        Assert(cbWritten <= cbData);
        if (cbWritten < cbData)
        {
            LogRel(("DnD: Only written %RU32 of %RU32 bytes of guest file '%s' -- disk full?\n",
                    cbWritten, cbData, pcszSource));
            vrc = VERR_IO_GEN_FAILURE; /** @todo Find a better vrc. */
            break;
        }

        vrc = updateProgress(pCtx, pCtx->pState, cbWritten);
        AssertRCBreak(vrc);

        if (DnDTransferObjectIsComplete(pObj))
        {
            LogRel2(("DnD: Transferring guest file '%s' to host complete\n", pcszSource));

            pCtx->Transfer.cObjProcessed++;
            if (pCtx->Transfer.cObjProcessed > pCtx->Transfer.cObjToProcess)
                vrc = VERR_TOO_MUCH_DATA;

            DnDTransferObjectDestroy(pObj);
        }

    } while (0);

    if (RT_FAILURE(vrc))
        LogRel(("DnD: Error receiving guest file data, vrc=%Rrc\n", vrc));

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}
#endif /* VBOX_WITH_DRAG_AND_DROP_GH */

/**
 * Main function to receive DnD data from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Receive context to use.
 * @param   msTimeout           Timeout (in ms) to wait for receiving data.
 */
int GuestDnDSource::i_receiveData(GuestDnDRecvCtx *pCtx, RTMSINTERVAL msTimeout)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    /* Sanity. */
    AssertMsgReturn(pCtx->enmAction,
                    ("Action to perform is none when it shouldn't\n"), VERR_INVALID_PARAMETER);
    AssertMsgReturn(pCtx->strFmtReq.isNotEmpty(),
                    ("Requested format from host is empty when it shouldn't\n"), VERR_INVALID_PARAMETER);

    /*
     * Do we need to receive a different format than initially requested?
     *
     * For example, receiving a file link as "text/plain" requires still to receive
     * the file from the guest as "text/uri-list" first, then pointing to
     * the file path on the host in the "text/plain" data returned.
     */

    bool fFoundFormat = true; /* Whether we've found a common format between host + guest. */

    LogFlowFunc(("strFmtReq=%s, strFmtRecv=%s, enmAction=0x%x\n",
                 pCtx->strFmtReq.c_str(), pCtx->strFmtRecv.c_str(), pCtx->enmAction));

    /* Plain text wanted? */
    if (   pCtx->strFmtReq.equalsIgnoreCase("text/plain")
        || pCtx->strFmtReq.equalsIgnoreCase("text/plain;charset=utf-8"))
    {
        /* Did the guest offer a file? Receive a file instead. */
        if (GuestDnD::isFormatInFormatList("text/uri-list", pCtx->lstFmtOffered))
            pCtx->strFmtRecv = "text/uri-list";
        /* Guest only offers (plain) text. */
        else
            pCtx->strFmtRecv = "text/plain;charset=utf-8";

        /** @todo Add more conversions here. */
    }
    /* File(s) wanted? */
    else if (pCtx->strFmtReq.equalsIgnoreCase("text/uri-list"))
    {
        /* Does the guest support sending files? */
        if (GuestDnD::isFormatInFormatList("text/uri-list", pCtx->lstFmtOffered))
            pCtx->strFmtRecv = "text/uri-list";
        else /* Bail out. */
            fFoundFormat = false;
    }

    int vrc = VINF_SUCCESS;

    if (fFoundFormat)
    {
        if (!pCtx->strFmtRecv.equals(pCtx->strFmtReq))
            LogRel2(("DnD: Requested data in format '%s', receiving in intermediate format '%s' now\n",
                     pCtx->strFmtReq.c_str(), pCtx->strFmtRecv.c_str()));

        /*
         * Call the appropriate receive handler based on the data format to handle.
         */
        bool fURIData = DnDMIMENeedsDropDir(pCtx->strFmtRecv.c_str(), pCtx->strFmtRecv.length());
        if (fURIData)
            vrc = i_receiveTransferData(pCtx, msTimeout);
        else
            vrc = i_receiveRawData(pCtx, msTimeout);
    }
    else /* Just inform the user (if verbose release logging is enabled). */
    {
        LogRel(("DnD: The guest does not support format '%s':\n", pCtx->strFmtReq.c_str()));
        LogRel(("DnD: Guest offered the following formats:\n"));
        for (size_t i = 0; i < pCtx->lstFmtOffered.size(); i++)
            LogRel(("DnD:\tFormat #%zu: %s\n", i, pCtx->lstFmtOffered.at(i).c_str()));

        vrc = VERR_NOT_SUPPORTED;
    }

    if (RT_FAILURE(vrc))
    {
        LogRel(("DnD: Receiving data from guest failed with %Rrc\n", vrc));

        /* Let the guest side know first. */
        sendCancel();

        /* Reset state. */
        i_reset();
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Receives raw (meta) data from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Receive context to use.
 * @param   msTimeout           Timeout (in ms) to wait for receiving data.
 */
int GuestDnDSource::i_receiveRawData(GuestDnDRecvCtx *pCtx, RTMSINTERVAL msTimeout)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    int vrc;

    LogFlowFuncEnter();

    GuestDnDState *pState = pCtx->pState;
    AssertPtr(pCtx->pState);

    GuestDnD *pInst = GuestDnDInst();
    if (!pInst)
        return VERR_INVALID_POINTER;

#define REGISTER_CALLBACK(x) do { \
        vrc = pState->setCallback(x, i_receiveRawDataCallback, pCtx); \
        if (RT_FAILURE(vrc)) \
            return vrc; \
    } while (0)

#define UNREGISTER_CALLBACK(x) do { \
        int vrc2 = pState->setCallback(x, NULL); \
        AssertRC(vrc2); \
    } while (0)

    /*
     * Register callbacks.
     */
    REGISTER_CALLBACK(GUEST_DND_FN_CONNECT);
    REGISTER_CALLBACK(GUEST_DND_FN_DISCONNECT);
    REGISTER_CALLBACK(GUEST_DND_FN_EVT_ERROR);
    if (m_pState->m_uProtocolVersion >= 3)
        REGISTER_CALLBACK(GUEST_DND_FN_GH_SND_DATA_HDR);
    REGISTER_CALLBACK(GUEST_DND_FN_GH_SND_DATA);

    do
    {
        /*
         * Receive the raw data.
         */
        GuestDnDMsg Msg;
        Msg.setType(HOST_DND_FN_GH_EVT_DROPPED);
        if (m_pState->m_uProtocolVersion >= 3)
            Msg.appendUInt32(0); /** @todo ContextID not used yet. */
        Msg.appendPointer((void*)pCtx->strFmtRecv.c_str(), (uint32_t)pCtx->strFmtRecv.length() + 1);
        Msg.appendUInt32((uint32_t)pCtx->strFmtRecv.length() + 1);
        Msg.appendUInt32(pCtx->enmAction);

        /* Make the initial call to the guest by telling that we initiated the "dropped" event on
         * the host and therefore now waiting for the actual raw data. */
        vrc = pInst->hostCall(Msg.getType(), Msg.getCount(), Msg.getParms());
        if (RT_SUCCESS(vrc))
        {
            vrc = waitForEvent(&pCtx->EventCallback, pCtx->pState, msTimeout);
            if (RT_SUCCESS(vrc))
                vrc = pCtx->pState->setProgress(100, DND_PROGRESS_COMPLETE, VINF_SUCCESS);
        }

    } while (0);

    /*
     * Unregister callbacks.
     */
    UNREGISTER_CALLBACK(GUEST_DND_FN_CONNECT);
    UNREGISTER_CALLBACK(GUEST_DND_FN_DISCONNECT);
    UNREGISTER_CALLBACK(GUEST_DND_FN_EVT_ERROR);
    if (m_pState->m_uProtocolVersion >= 3)
        UNREGISTER_CALLBACK(GUEST_DND_FN_GH_SND_DATA_HDR);
    UNREGISTER_CALLBACK(GUEST_DND_FN_GH_SND_DATA);

#undef REGISTER_CALLBACK
#undef UNREGISTER_CALLBACK

    if (RT_FAILURE(vrc))
    {
        if (vrc == VERR_CANCELLED) /* Transfer was cancelled by the host. */
        {
            /*
             * Now that we've cleaned up tell the guest side to cancel.
             * This does not imply we're waiting for the guest to react, as the
             * host side never must depend on anything from the guest.
             */
            int vrc2 = sendCancel();
            AssertRC(vrc2);

            vrc2 = pCtx->pState->setProgress(100, DND_PROGRESS_CANCELLED);
            AssertRC(vrc2);
        }
        else if (vrc != VERR_DND_GUEST_ERROR) /* Guest-side error are already handled in the callback. */
        {
            int vrc2 = pCtx->pState->setProgress(100, DND_PROGRESS_ERROR,
                                                vrc, GuestDnDSource::i_hostErrorToString(vrc));
            AssertRC(vrc2);
        }

        vrc = VINF_SUCCESS; /* The error was handled by the setProgress() calls above. */
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Receives transfer data (files / directories / ...) from the guest.
 *
 * @returns VBox status code.
 * @param   pCtx                Receive context to use.
 * @param   msTimeout           Timeout (in ms) to wait for receiving data.
 */
int GuestDnDSource::i_receiveTransferData(GuestDnDRecvCtx *pCtx, RTMSINTERVAL msTimeout)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    int vrc;

    LogFlowFuncEnter();

    GuestDnDState *pState = pCtx->pState;
    AssertPtr(pCtx->pState);

    GuestDnD *pInst = GuestDnDInst();
    if (!pInst)
        return VERR_INVALID_POINTER;

#define REGISTER_CALLBACK(x) do { \
        vrc = pState->setCallback(x, i_receiveTransferDataCallback, pCtx); \
        if (RT_FAILURE(vrc)) \
            return vrc; \
    } while (0)

#define UNREGISTER_CALLBACK(x) do {  \
        int vrc2 = pState->setCallback(x, NULL);  \
        AssertRC(vrc2); \
    } while (0)

    /*
     * Register callbacks.
     */
    /* Guest callbacks. */
    REGISTER_CALLBACK(GUEST_DND_FN_CONNECT);
    REGISTER_CALLBACK(GUEST_DND_FN_DISCONNECT);
    REGISTER_CALLBACK(GUEST_DND_FN_EVT_ERROR);
    if (m_pState->m_uProtocolVersion >= 3)
        REGISTER_CALLBACK(GUEST_DND_FN_GH_SND_DATA_HDR);
    REGISTER_CALLBACK(GUEST_DND_FN_GH_SND_DATA);
    REGISTER_CALLBACK(GUEST_DND_FN_GH_SND_DIR);
    if (m_pState->m_uProtocolVersion >= 2)
        REGISTER_CALLBACK(GUEST_DND_FN_GH_SND_FILE_HDR);
    REGISTER_CALLBACK(GUEST_DND_FN_GH_SND_FILE_DATA);

    const PDNDDROPPEDFILES pDF = &pCtx->Transfer.DroppedFiles;

    do
    {
        vrc = DnDDroppedFilesOpenTemp(pDF, 0 /* fFlags */);
        if (RT_FAILURE(vrc))
        {
            LogRel(("DnD: Opening dropped files directory '%s' on the host failed with vrc=%Rrc\n",
                    DnDDroppedFilesGetDirAbs(pDF), vrc));
            break;
        }

        /*
         * Receive the transfer list.
         */
        GuestDnDMsg Msg;
        Msg.setType(HOST_DND_FN_GH_EVT_DROPPED);
        if (m_pState->m_uProtocolVersion >= 3)
            Msg.appendUInt32(0); /** @todo ContextID not used yet. */
        Msg.appendPointer((void*)pCtx->strFmtRecv.c_str(), (uint32_t)pCtx->strFmtRecv.length() + 1);
        Msg.appendUInt32((uint32_t)pCtx->strFmtRecv.length() + 1);
        Msg.appendUInt32(pCtx->enmAction);

        /* Make the initial call to the guest by telling that we initiated the "dropped" event on
         * the host and therefore now waiting for the actual URI data. */
        vrc = pInst->hostCall(Msg.getType(), Msg.getCount(), Msg.getParms());
        if (RT_SUCCESS(vrc))
        {
            LogFlowFunc(("Waiting ...\n"));

            vrc = waitForEvent(&pCtx->EventCallback, pCtx->pState, msTimeout);
            if (RT_SUCCESS(vrc))
                vrc = pCtx->pState->setProgress(100, DND_PROGRESS_COMPLETE, VINF_SUCCESS);

            LogFlowFunc(("Waiting ended with vrc=%Rrc\n", vrc));
        }

    } while (0);

    /*
     * Unregister callbacks.
     */
    UNREGISTER_CALLBACK(GUEST_DND_FN_CONNECT);
    UNREGISTER_CALLBACK(GUEST_DND_FN_DISCONNECT);
    UNREGISTER_CALLBACK(GUEST_DND_FN_EVT_ERROR);
    UNREGISTER_CALLBACK(GUEST_DND_FN_GH_SND_DATA_HDR);
    UNREGISTER_CALLBACK(GUEST_DND_FN_GH_SND_DATA);
    UNREGISTER_CALLBACK(GUEST_DND_FN_GH_SND_DIR);
    UNREGISTER_CALLBACK(GUEST_DND_FN_GH_SND_FILE_HDR);
    UNREGISTER_CALLBACK(GUEST_DND_FN_GH_SND_FILE_DATA);

#undef REGISTER_CALLBACK
#undef UNREGISTER_CALLBACK

    if (RT_FAILURE(vrc))
    {
        int vrc2 = DnDDroppedFilesRollback(pDF);
        if (RT_FAILURE(vrc2))
            LogRel(("DnD: Deleting left over temporary files failed (%Rrc), please remove directory '%s' manually\n",
                    vrc2, DnDDroppedFilesGetDirAbs(pDF)));

        if (vrc == VERR_CANCELLED)
        {
            /*
             * Now that we've cleaned up tell the guest side to cancel.
             * This does not imply we're waiting for the guest to react, as the
             * host side never must depend on anything from the guest.
             */
            vrc2 = sendCancel();
            AssertRC(vrc2);

            vrc2 = pCtx->pState->setProgress(100, DND_PROGRESS_CANCELLED);
            AssertRC(vrc2);

            /* Cancelling is not an error, just set success here. */
            vrc  = VINF_SUCCESS;
        }
        else if (vrc != VERR_DND_GUEST_ERROR) /* Guest-side error are already handled in the callback. */
        {
            vrc2 = pCtx->pState->setProgress(100, DND_PROGRESS_ERROR, vrc, GuestDnDSource::i_hostErrorToString(vrc2));
            AssertRC(vrc2);
        }
    }

    DnDDroppedFilesClose(pDF);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Static HGCM service callback which handles receiving raw data.
 *
 * @returns VBox status code. Will get sent back to the host service.
 * @param   uMsg                HGCM message ID (function number).
 * @param   pvParms             Pointer to additional message data. Optional and can be NULL.
 * @param   cbParms             Size (in bytes) additional message data. Optional and can be 0.
 * @param   pvUser              User-supplied pointer on callback registration.
 */
/* static */
DECLCALLBACK(int) GuestDnDSource::i_receiveRawDataCallback(uint32_t uMsg, void *pvParms, size_t cbParms, void *pvUser)
{
    GuestDnDRecvCtx *pCtx = (GuestDnDRecvCtx *)pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    GuestDnDSource *pThis = pCtx->pSource;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    LogFlowFunc(("pThis=%p, uMsg=%RU32\n", pThis, uMsg));

    int vrc = VINF_SUCCESS;

    int vrcCallback = VINF_SUCCESS; /* vrc for the callback. */
    bool fNotify   = false;

    switch (uMsg)
    {
        case GUEST_DND_FN_CONNECT:
            /* Nothing to do here (yet). */
            break;

        case GUEST_DND_FN_DISCONNECT:
            vrc = VERR_CANCELLED;
            break;

#ifdef VBOX_WITH_DRAG_AND_DROP_GH
        case GUEST_DND_FN_GH_SND_DATA_HDR:
        {
            PVBOXDNDCBSNDDATAHDRDATA pCBData = reinterpret_cast<PVBOXDNDCBSNDDATAHDRDATA>(pvParms);
            AssertPtr(pCBData);
            AssertReturn(sizeof(VBOXDNDCBSNDDATAHDRDATA) == cbParms, VERR_INVALID_PARAMETER);
            AssertReturn(CB_MAGIC_DND_GH_SND_DATA_HDR == pCBData->hdr.uMagic, VERR_INVALID_PARAMETER);

            vrc = pThis->i_onReceiveDataHdr(pCtx, &pCBData->data);
            break;
        }
        case GUEST_DND_FN_GH_SND_DATA:
        {
            PVBOXDNDCBSNDDATADATA pCBData = reinterpret_cast<PVBOXDNDCBSNDDATADATA>(pvParms);
            AssertPtr(pCBData);
            AssertReturn(sizeof(VBOXDNDCBSNDDATADATA) == cbParms, VERR_INVALID_PARAMETER);
            AssertReturn(CB_MAGIC_DND_GH_SND_DATA == pCBData->hdr.uMagic, VERR_INVALID_PARAMETER);

            vrc = pThis->i_onReceiveData(pCtx, &pCBData->data);
            break;
        }
        case GUEST_DND_FN_EVT_ERROR:
        {
            PVBOXDNDCBEVTERRORDATA pCBData = reinterpret_cast<PVBOXDNDCBEVTERRORDATA>(pvParms);
            AssertPtr(pCBData);
            AssertReturn(sizeof(VBOXDNDCBEVTERRORDATA) == cbParms, VERR_INVALID_PARAMETER);
            AssertReturn(CB_MAGIC_DND_EVT_ERROR == pCBData->hdr.uMagic, VERR_INVALID_PARAMETER);

            pCtx->pState->reset();

            if (RT_SUCCESS(pCBData->rc))
            {
                AssertMsgFailed(("Received guest error with no error code set\n"));
                pCBData->rc = VERR_GENERAL_FAILURE; /* Make sure some error is set. */
            }
            else if (pCBData->rc == VERR_WRONG_ORDER)
                vrc = pCtx->pState->setProgress(100, DND_PROGRESS_CANCELLED);
            else
                vrc = pCtx->pState->setProgress(100, DND_PROGRESS_ERROR, pCBData->rc,
                                                GuestDnDSource::i_guestErrorToString(pCBData->rc));

            LogRel3(("DnD: Guest reported data transfer error: %Rrc\n", pCBData->rc));

            if (RT_SUCCESS(vrc))
                vrcCallback = VERR_DND_GUEST_ERROR;
            break;
        }
#endif /* VBOX_WITH_DRAG_AND_DROP_GH */
        default:
            vrc = VERR_NOT_SUPPORTED;
            break;
    }

    if (   RT_FAILURE(vrc)
        || RT_FAILURE(vrcCallback))
    {
        fNotify = true;
        if (RT_SUCCESS(vrcCallback))
            vrcCallback = vrc;
    }

    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_NO_DATA:
                LogRel2(("DnD: Data transfer to host complete\n"));
                break;

            case VERR_CANCELLED:
                LogRel2(("DnD: Data transfer to host canceled\n"));
                break;

            default:
                LogRel(("DnD: Error %Rrc occurred, aborting data transfer to host\n", vrc));
                break;
        }

        /* Unregister this callback. */
        AssertPtr(pCtx->pState);
        int vrc2 = pCtx->pState->setCallback(uMsg, NULL /* PFNGUESTDNDCALLBACK */);
        AssertRC(vrc2);
    }

    /* All data processed? */
    if (pCtx->isComplete())
        fNotify = true;

    LogFlowFunc(("cbProcessed=%RU64, cbExtra=%RU64, fNotify=%RTbool, vrcCallback=%Rrc, vrc=%Rrc\n",
                 pCtx->cbProcessed, pCtx->cbExtra, fNotify, vrcCallback, vrc));

    if (fNotify)
    {
        int vrc2 = pCtx->EventCallback.Notify(vrcCallback);
        AssertRC(vrc2);
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc; /* Tell the guest. */
}

/**
 * Static HGCM service callback which handles receiving transfer data from the guest.
 *
 * @returns VBox status code. Will get sent back to the host service.
 * @param   uMsg                HGCM message ID (function number).
 * @param   pvParms             Pointer to additional message data. Optional and can be NULL.
 * @param   cbParms             Size (in bytes) additional message data. Optional and can be 0.
 * @param   pvUser              User-supplied pointer on callback registration.
 */
/* static */
DECLCALLBACK(int) GuestDnDSource::i_receiveTransferDataCallback(uint32_t uMsg, void *pvParms, size_t cbParms, void *pvUser)
{
    GuestDnDRecvCtx *pCtx = (GuestDnDRecvCtx *)pvUser;
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    GuestDnDSource *pThis = pCtx->pSource;
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);

    LogFlowFunc(("pThis=%p, uMsg=%RU32\n", pThis, uMsg));

    int vrc = VINF_SUCCESS;

    int vrcCallback = VINF_SUCCESS; /* vrc for the callback. */
    bool fNotify = false;

    switch (uMsg)
    {
        case GUEST_DND_FN_CONNECT:
            /* Nothing to do here (yet). */
            break;

        case GUEST_DND_FN_DISCONNECT:
            vrc = VERR_CANCELLED;
            break;

#ifdef VBOX_WITH_DRAG_AND_DROP_GH
        case GUEST_DND_FN_GH_SND_DATA_HDR:
        {
            PVBOXDNDCBSNDDATAHDRDATA pCBData = reinterpret_cast<PVBOXDNDCBSNDDATAHDRDATA>(pvParms);
            AssertPtr(pCBData);
            AssertReturn(sizeof(VBOXDNDCBSNDDATAHDRDATA) == cbParms, VERR_INVALID_PARAMETER);
            AssertReturn(CB_MAGIC_DND_GH_SND_DATA_HDR == pCBData->hdr.uMagic, VERR_INVALID_PARAMETER);

            vrc = pThis->i_onReceiveDataHdr(pCtx, &pCBData->data);
            break;
        }
        case GUEST_DND_FN_GH_SND_DATA:
        {
            PVBOXDNDCBSNDDATADATA pCBData = reinterpret_cast<PVBOXDNDCBSNDDATADATA>(pvParms);
            AssertPtr(pCBData);
            AssertReturn(sizeof(VBOXDNDCBSNDDATADATA) == cbParms, VERR_INVALID_PARAMETER);
            AssertReturn(CB_MAGIC_DND_GH_SND_DATA == pCBData->hdr.uMagic, VERR_INVALID_PARAMETER);

            vrc = pThis->i_onReceiveData(pCtx, &pCBData->data);
            break;
        }
        case GUEST_DND_FN_GH_SND_DIR:
        {
            PVBOXDNDCBSNDDIRDATA pCBData = reinterpret_cast<PVBOXDNDCBSNDDIRDATA>(pvParms);
            AssertPtr(pCBData);
            AssertReturn(sizeof(VBOXDNDCBSNDDIRDATA) == cbParms, VERR_INVALID_PARAMETER);
            AssertReturn(CB_MAGIC_DND_GH_SND_DIR == pCBData->hdr.uMagic, VERR_INVALID_PARAMETER);

            vrc = pThis->i_onReceiveDir(pCtx, pCBData->pszPath, pCBData->cbPath, pCBData->fMode);
            break;
        }
        case GUEST_DND_FN_GH_SND_FILE_HDR:
        {
            PVBOXDNDCBSNDFILEHDRDATA pCBData = reinterpret_cast<PVBOXDNDCBSNDFILEHDRDATA>(pvParms);
            AssertPtr(pCBData);
            AssertReturn(sizeof(VBOXDNDCBSNDFILEHDRDATA) == cbParms, VERR_INVALID_PARAMETER);
            AssertReturn(CB_MAGIC_DND_GH_SND_FILE_HDR == pCBData->hdr.uMagic, VERR_INVALID_PARAMETER);

            vrc = pThis->i_onReceiveFileHdr(pCtx, pCBData->pszFilePath, pCBData->cbFilePath,
                                            pCBData->cbSize, pCBData->fMode, pCBData->fFlags);
            break;
        }
        case GUEST_DND_FN_GH_SND_FILE_DATA:
        {
            PVBOXDNDCBSNDFILEDATADATA pCBData = reinterpret_cast<PVBOXDNDCBSNDFILEDATADATA>(pvParms);
            AssertPtr(pCBData);
            AssertReturn(sizeof(VBOXDNDCBSNDFILEDATADATA) == cbParms, VERR_INVALID_PARAMETER);
            AssertReturn(CB_MAGIC_DND_GH_SND_FILE_DATA == pCBData->hdr.uMagic, VERR_INVALID_PARAMETER);

            if (pThis->m_pState->m_uProtocolVersion <= 1)
            {
                /*
                 * Notes for protocol v1 (< VBox 5.0):
                 * - Every time this command is being sent it includes the file header,
                 *   so just process both calls here.
                 * - There was no information whatsoever about the total file size; the old code only
                 *   appended data to the desired file. So just pass 0 as cbSize.
                 */
                vrc = pThis->i_onReceiveFileHdr(pCtx, pCBData->u.v1.pszFilePath, pCBData->u.v1.cbFilePath,
                                                0 /* cbSize */, pCBData->u.v1.fMode, 0 /* fFlags */);
                if (RT_SUCCESS(vrc))
                    vrc = pThis->i_onReceiveFileData(pCtx, pCBData->pvData, pCBData->cbData);
            }
            else /* Protocol v2 and up. */
                vrc = pThis->i_onReceiveFileData(pCtx, pCBData->pvData, pCBData->cbData);
            break;
        }
        case GUEST_DND_FN_EVT_ERROR:
        {
            PVBOXDNDCBEVTERRORDATA pCBData = reinterpret_cast<PVBOXDNDCBEVTERRORDATA>(pvParms);
            AssertPtr(pCBData);
            AssertReturn(sizeof(VBOXDNDCBEVTERRORDATA) == cbParms, VERR_INVALID_PARAMETER);
            AssertReturn(CB_MAGIC_DND_EVT_ERROR == pCBData->hdr.uMagic, VERR_INVALID_PARAMETER);

            pCtx->pState->reset();

            if (RT_SUCCESS(pCBData->rc))
            {
                AssertMsgFailed(("Received guest error with no error code set\n"));
                pCBData->rc = VERR_GENERAL_FAILURE; /* Make sure some error is set. */
            }
            else if (pCBData->rc == VERR_WRONG_ORDER)
                vrc = pCtx->pState->setProgress(100, DND_PROGRESS_CANCELLED);
            else
                vrc = pCtx->pState->setProgress(100, DND_PROGRESS_ERROR, pCBData->rc,
                                                GuestDnDSource::i_guestErrorToString(pCBData->rc));

            LogRel3(("DnD: Guest reported file transfer error: %Rrc\n", pCBData->rc));

            if (RT_SUCCESS(vrc))
                vrcCallback = VERR_DND_GUEST_ERROR;
            break;
        }
#endif /* VBOX_WITH_DRAG_AND_DROP_GH */
        default:
            vrc = VERR_NOT_SUPPORTED;
            break;
    }

    if (   RT_FAILURE(vrc)
        || RT_FAILURE(vrcCallback))
    {
        fNotify = true;
        if (RT_SUCCESS(vrcCallback))
            vrcCallback = vrc;
    }

    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_NO_DATA:
                LogRel2(("DnD: File transfer to host complete\n"));
                break;

            case VERR_CANCELLED:
                LogRel2(("DnD: File transfer to host canceled\n"));
                break;

            default:
                LogRel(("DnD: Error %Rrc occurred, aborting file transfer to host\n", vrc));
                break;
        }

        /* Unregister this callback. */
        AssertPtr(pCtx->pState);
        int vrc2 = pCtx->pState->setCallback(uMsg, NULL /* PFNGUESTDNDCALLBACK */);
        AssertRC(vrc2);
    }

    /* All data processed? */
    if (   pCtx->Transfer.isComplete()
        && pCtx->isComplete())
        fNotify = true;

    LogFlowFunc(("cbProcessed=%RU64, cbExtra=%RU64, fNotify=%RTbool, vrcCallback=%Rrc, vrc=%Rrc\n",
                 pCtx->cbProcessed, pCtx->cbExtra, fNotify, vrcCallback, vrc));

    if (fNotify)
    {
        int vrc2 = pCtx->EventCallback.Notify(
            vrcCallback);
        AssertRC(vrc2);
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc; /* Tell the guest. */
}

