/* $Id: GuestDnDTargetImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Console COM Class implementation - Guest drag'n drop target.
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

#ifndef MAIN_INCLUDED_GuestDnDTargetImpl_h
#define MAIN_INCLUDED_GuestDnDTargetImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "GuestDnDTargetWrap.h"
#include "GuestDnDPrivate.h"

#include <VBox/GuestHost/DragAndDrop.h>
#include <VBox/HostServices/DragAndDropSvc.h>

struct GuestDnDSendCtx;
class GuestDnDSendDataTask;

class ATL_NO_VTABLE GuestDnDTarget :
    public GuestDnDTargetWrap,
    public GuestDnDBase
{
public:
    GuestDnDTarget(void);
    virtual ~GuestDnDTarget(void);

    /** @name COM and internal init/term/mapping cruft.
     * @{ */
    DECLARE_TRANSLATE_METHODS(GuestDnDTarget);

    HRESULT init(const ComObjPtr<Guest>& pGuest);
    void    uninit(void);

    HRESULT FinalConstruct(void);
    void    FinalRelease(void);
    /** @}  */

private:

    /** Private wrapped @name IDnDBase methods.
     * @{ */
    HRESULT isFormatSupported(const com::Utf8Str &aFormat, BOOL *aSupported);
    HRESULT getFormats(GuestDnDMIMEList &aFormats);
    HRESULT addFormats(const GuestDnDMIMEList &aFormats);
    HRESULT removeFormats(const GuestDnDMIMEList &aFormats);
    /** @}  */

    /** Private wrapped @name IDnDTarget methods.
     * @{ */
    HRESULT enter(ULONG aScreenId, ULONG ax, ULONG aY, DnDAction_T aDefaultAction, const std::vector<DnDAction_T> &aAllowedActions, const GuestDnDMIMEList &aFormats, DnDAction_T *aResultAction);
    HRESULT move(ULONG aScreenId, ULONG aX, ULONG aY, DnDAction_T aDefaultAction, const std::vector<DnDAction_T> &aAllowedActions, const GuestDnDMIMEList &aFormats, DnDAction_T *aResultAction);
    HRESULT leave(ULONG aScreenId);
    HRESULT drop(ULONG aScreenId, ULONG aX, ULONG aY, DnDAction_T aDefaultAction, const std::vector<DnDAction_T> &aAllowedActions, const GuestDnDMIMEList &aFormats, com::Utf8Str &aFormat, DnDAction_T *aResultAction);
    HRESULT sendData(ULONG aScreenId, const com::Utf8Str &aFormat, const std::vector<BYTE> &aData, ComPtr<IProgress> &aProgress);
    HRESULT cancel(BOOL *aVeto);
    /** @}  */

protected:

    static Utf8Str i_guestErrorToString(int guestRc);
    static Utf8Str i_hostErrorToString(int hostRc);

    /** @name Callbacks for dispatch handler.
     * @{ */
    static DECLCALLBACK(int) i_sendTransferDataCallback(uint32_t uMsg, void *pvParms, size_t cbParms, void *pvUser);
    /** @}  */

protected:

    /** @name Implemented virtual functions.
     * @{ */
    void i_reset(void);
    /** @}  */

    int i_sendData(GuestDnDSendCtx *pCtx, RTMSINTERVAL msTimeout);

    int i_sendMetaDataBody(GuestDnDSendCtx *pCtx);
    int i_sendMetaDataHeader(GuestDnDSendCtx *pCtx);

    int i_sendTransferData(GuestDnDSendCtx *pCtx, RTMSINTERVAL msTimeout);
    int i_sendTransferListObject(GuestDnDSendCtx *pCtx,  PDNDTRANSFERLIST pList, GuestDnDMsg *pMsg);

    int i_sendDirectory(GuestDnDSendCtx *pCtx, PDNDTRANSFEROBJECT pObj, GuestDnDMsg *pMsg);
    int i_sendFile(GuestDnDSendCtx *pCtx, PDNDTRANSFEROBJECT pObj, GuestDnDMsg *pMsg);
    int i_sendFileData(GuestDnDSendCtx *pCtx, PDNDTRANSFEROBJECT pObj, GuestDnDMsg *pMsg);

    int i_sendRawData(GuestDnDSendCtx *pCtx, RTMSINTERVAL msTimeout);

protected:

    struct
    {
        /** Maximum data block size (in bytes) the target can handle. */
        uint32_t        mcbBlockSize;
        /** The context for sending data to the guest.
         *  At the moment only one transfer at a time is supported. */
        GuestDnDSendCtx mSendCtx;
    } mData;

    friend class GuestDnDSendDataTask;
};

#endif /* !MAIN_INCLUDED_GuestDnDTargetImpl_h */

