/* $Id: GuestDnDPrivate.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Private guest drag and drop code, used by GuestDnDTarget +
 * GuestDnDSource.
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

#ifndef MAIN_INCLUDED_GuestDnDPrivate_h
#define MAIN_INCLUDED_GuestDnDPrivate_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/path.h>

#include <VBox/hgcmsvc.h> /* For PVBOXHGCMSVCPARM. */
#include <VBox/GuestHost/DragAndDrop.h>
#include <VBox/GuestHost/DragAndDropDefs.h>
#include <VBox/HostServices/DragAndDropSvc.h>

/**
 * Forward prototype declarations.
 */
class Guest;
class GuestDnDBase;
class GuestDnDState;
class GuestDnDSource;
class GuestDnDTarget;
class Progress;

/**
 * Type definitions.
 */

/** List (vector) of MIME types. */
typedef std::vector<com::Utf8Str> GuestDnDMIMEList;

/**
 * Class to handle a guest DnD callback event.
 */
class GuestDnDCallbackEvent
{
public:

    GuestDnDCallbackEvent(void)
        : m_SemEvent(NIL_RTSEMEVENT)
        , m_vrc(VINF_SUCCESS) { }

    virtual ~GuestDnDCallbackEvent(void);

public:

    int Reset(void);

    int Notify(int vrc = VINF_SUCCESS);

    int Result(void) const { return m_vrc; }

    int Wait(RTMSINTERVAL msTimeout);

protected:

    /** Event semaphore to notify on error/completion. */
    RTSEMEVENT m_SemEvent;
    /** Callback result. */
    int        m_vrc;
};

/**
 * Struct for handling the (raw) meta data.
 */
struct GuestDnDMetaData
{
    GuestDnDMetaData(void)
        : pvData(NULL)
        , cbData(0)
        , cbAllocated(0)
        , cbAnnounced(0) { }

    virtual ~GuestDnDMetaData(void)
    {
        reset();
    }

    /**
     * Adds new meta data.
     *
     * @returns New (total) meta data size in bytes.
     * @param   pvDataAdd       Pointer of data to add.
     * @param   cbDataAdd       Size (in bytes) of data to add.
     */
    size_t add(const void *pvDataAdd, size_t cbDataAdd)
    {
        LogFlowThisFunc(("cbAllocated=%zu, cbAnnounced=%zu, pvDataAdd=%p, cbDataAdd=%zu\n",
                         cbAllocated, cbAnnounced, pvDataAdd, cbDataAdd));
        if (!cbDataAdd)
            return 0;
        AssertPtrReturn(pvDataAdd, 0);

        const size_t cbAllocatedTmp = cbData + cbDataAdd;
        if (cbAllocatedTmp > cbAllocated)
        {
            int vrc = resize(cbAllocatedTmp);
            if (RT_FAILURE(vrc))
                return 0;
        }

        Assert(cbAllocated >= cbData + cbDataAdd);
        memcpy((uint8_t *)pvData + cbData, pvDataAdd, cbDataAdd);

        cbData     += cbDataAdd;
        cbAnnounced = cbData;

        return cbData;
    }

    /**
     * Adds new meta data.
     *
     * @returns New (total) meta data size in bytes.
     * @param   vecAdd          Meta data to add.
     */
    size_t add(const std::vector<BYTE> &vecAdd)
    {
        if (!vecAdd.size())
            return 0;

        if (vecAdd.size() > UINT32_MAX) /* Paranoia. */
            return 0;

        return add(&vecAdd.front(), (uint32_t)vecAdd.size());
    }

    /**
     * Resets (clears) all data.
     */
    void reset(void)
    {
        strFmt = "";

        if (pvData)
        {
            Assert(cbAllocated);
            RTMemFree(pvData);
            pvData = NULL;
        }

        cbData      = 0;
        cbAllocated = 0;
        cbAnnounced = 0;
    }

    /**
     * Resizes the allocation size.
     *
     * @returns VBox status code.
     * @param   cbSize          New allocation size (in bytes).
     */
    int resize(size_t cbSize)
    {
        if (!cbSize)
        {
            reset();
            return VINF_SUCCESS;
        }

        if (cbSize == cbAllocated)
            return VINF_SUCCESS;

        cbSize = RT_ALIGN_Z(cbSize, PAGE_SIZE);

        if (cbSize > _32M) /* Meta data can be up to 32MB. */
            return VERR_BUFFER_OVERFLOW;

        void *pvTmp = NULL;
        if (!cbAllocated)
        {
            Assert(cbData == 0);
            pvTmp = RTMemAllocZ(cbSize);
        }
        else
        {
            AssertPtr(pvData);
            pvTmp = RTMemRealloc(pvData, cbSize);
        }

        if (pvTmp)
        {
            pvData      = pvTmp;
            cbAllocated = cbSize;
            return VINF_SUCCESS;
        }

        return VERR_NO_MEMORY;
    }

    /** Format string of this meta data. */
    com::Utf8Str strFmt;
    /** Pointer to allocated meta data. */
    void        *pvData;
    /** Used bytes of meta data. Must not exceed cbAllocated. */
    size_t       cbData;
    /** Size (in bytes) of allocated meta data. */
    size_t       cbAllocated;
    /** Size (in bytes) of announced meta data. */
    size_t       cbAnnounced;
};

/**
 * Struct for accounting shared DnD data to be sent/received.
 */
struct GuestDnDData
{
    GuestDnDData(void)
        : cbExtra(0)
        , cbProcessed(0) { }

    virtual ~GuestDnDData(void)
    {
        reset();
    }

    /**
     * Adds processed data to the internal accounting.
     *
     * @returns New processed data size.
     * @param   cbDataAdd       Bytes to add as done processing.
     */
    size_t addProcessed(size_t cbDataAdd)
    {
        const size_t cbTotal = getTotalAnnounced(); RT_NOREF(cbTotal);
        AssertReturn(cbProcessed + cbDataAdd <= cbTotal, 0);
        cbProcessed += cbDataAdd;
        return cbProcessed;
    }

    /**
     * Returns whether all data has been processed or not.
     *
     * @returns \c true if all data has been processed, \c false if not.
     */
    bool isComplete(void) const
    {
        const size_t cbTotal = getTotalAnnounced();
        LogFlowFunc(("cbProcessed=%zu, cbTotal=%zu\n", cbProcessed, cbTotal));
        AssertReturn(cbProcessed <= cbTotal, true);
        return (cbProcessed == cbTotal);
    }

    /**
     * Returns the percentage (0-100) of the already processed data.
     *
     * @returns Percentage (0-100) of the already processed data.
     */
    uint8_t getPercentComplete(void) const
    {
        const size_t cbTotal = getTotalAnnounced();
        return (uint8_t)(cbProcessed * 100 / RT_MAX(cbTotal, 1));
    }

    /**
     * Returns the remaining (outstanding) data left for processing.
     *
     * @returns Remaining (outstanding) data (in bytes) left for processing.
     */
    size_t getRemaining(void) const
    {
        const size_t cbTotal = getTotalAnnounced();
        AssertReturn(cbProcessed <= cbTotal, 0);
        return cbTotal - cbProcessed;
    }

    /**
     * Returns the total data size (in bytes) announced.
     *
     * @returns Total data size (in bytes) announced.
     */
    size_t getTotalAnnounced(void) const
    {
        return Meta.cbAnnounced + cbExtra;
    }

    /**
     * Returns the total data size (in bytes) available.
     * For receiving data, this represents the already received data.
     * For sending data, this represents the data left to send.
     *
     * @returns Total data size (in bytes) available.
     */
    size_t getTotalAvailable(void) const
    {
        return Meta.cbData + cbExtra;
    }

    /**
     * Resets all data.
     */
    void reset(void)
    {
        Meta.reset();

        cbExtra     = 0;
        cbProcessed = 0;
    }

    /** For storing the actual meta data.
     *  This might be an URI list or just plain raw data,
     *  according to the format being sent. */
    GuestDnDMetaData   Meta;
    /** Extra data to send/receive (in bytes). Can be 0 for raw data.
     *  For (file) transfers this is the total size for all files. */
    size_t             cbExtra;
    /** Overall size (in bytes) of processed data. */
    size_t             cbProcessed;
};

/** Initial object context state / no state set. */
#define DND_OBJ_STATE_NONE           0
/** The header was received / sent. */
#define DND_OBJ_STATE_HAS_HDR        RT_BIT(0)
/** Validation mask for object context state. */
#define DND_OBJ_STATE_VALID_MASK     UINT32_C(0x00000001)

/**
 * Base class for keeping around DnD (file) transfer data.
 * Used for sending / receiving transfer data.
 */
struct GuestDnDTransferData
{

public:

    GuestDnDTransferData(void)
        : cObjToProcess(0)
        , cObjProcessed(0)
        , pvScratchBuf(NULL)
        , cbScratchBuf(0) { }

    virtual ~GuestDnDTransferData(void)
    {
        destroy();
    }

    /**
     * Initializes a transfer data object.
     *
     * @param   cbBuf           Scratch buffer size (in bytes) to use.
     *                          If not specified, DND_DEFAULT_CHUNK_SIZE will be used.
     */
    int init(size_t cbBuf = DND_DEFAULT_CHUNK_SIZE)
    {
        reset();

        pvScratchBuf = RTMemAlloc(cbBuf);
        if (!pvScratchBuf)
            return VERR_NO_MEMORY;

        cbScratchBuf = cbBuf;
        return VINF_SUCCESS;
    }

    /**
     * Destroys a transfer data object.
     */
    void destroy(void)
    {
        reset();

        if (pvScratchBuf)
        {
            Assert(cbScratchBuf);
            RTMemFree(pvScratchBuf);
            pvScratchBuf = NULL;
        }
        cbScratchBuf = 0;
    }

    /**
     * Resets a transfer data object.
     */
    void reset(void)
    {
        LogFlowFuncEnter();

        cObjToProcess = 0;
        cObjProcessed = 0;
    }

    /**
     * Returns whether this transfer object is complete or not.
     *
     * @returns \c true if complete, \c false if not.
     */
    bool isComplete(void) const
    {
        return (cObjProcessed == cObjToProcess);
    }

    /** Number of objects to process. */
    uint64_t cObjToProcess;
    /** Number of objects already processed. */
    uint64_t cObjProcessed;
    /** Pointer to an optional scratch buffer to use for
     *  doing the actual chunk transfers. */
    void    *pvScratchBuf;
    /** Size (in bytes) of scratch buffer. */
    size_t   cbScratchBuf;
};

/**
 * Class for keeping around DnD transfer send data (Host -> Guest).
 */
struct GuestDnDTransferSendData : public GuestDnDTransferData
{
    GuestDnDTransferSendData()
        : fObjState(0)
    {
        RT_ZERO(List);
        int vrc2 = DnDTransferListInit(&List);
        AssertRC(vrc2);
    }

    virtual ~GuestDnDTransferSendData()
    {
        destroy();
    }

    /**
     * Destroys the object.
     */
    void destroy(void)
    {
        DnDTransferListDestroy(&List);
    }

    /**
     * Resets the object.
     */
    void reset(void)
    {
        DnDTransferListReset(&List);
        fObjState = 0;

        GuestDnDTransferData::reset();
    }

    /** Transfer List to handle. */
    DNDTRANSFERLIST                     List;
    /** Current state of object in transfer.
     *  This is needed for keeping compatibility to old(er) DnD HGCM protocols.
     *
     *  At the moment we only support transferring one object at a time. */
    uint32_t                            fObjState;
};

/**
 * Context structure for sending data to the guest.
 */
struct GuestDnDSendCtx : public GuestDnDData
{
    GuestDnDSendCtx(void);

    /**
     * Resets the object.
     */
    void reset(void);

    /** Pointer to guest target class this context belongs to. */
    GuestDnDTarget                     *pTarget;
    /** Pointer to guest state this context belongs to. */
    GuestDnDState                      *pState;
    /** Target (VM) screen ID. */
    uint32_t                            uScreenID;
    /** Transfer data structure. */
    GuestDnDTransferSendData            Transfer;
    /** Callback event to use. */
    GuestDnDCallbackEvent               EventCallback;
};

struct GuestDnDTransferRecvData : public GuestDnDTransferData
{
    GuestDnDTransferRecvData()
    {
        RT_ZERO(DroppedFiles);
        int vrc2 = DnDDroppedFilesInit(&DroppedFiles);
        AssertRC(vrc2);

        RT_ZERO(List);
        vrc2 = DnDTransferListInit(&List);
        AssertRC(vrc2);

        RT_ZERO(ObjCur);
        vrc2 = DnDTransferObjectInit(&ObjCur);
        AssertRC(vrc2);
    }

    virtual ~GuestDnDTransferRecvData()
    {
        destroy();
    }

    /**
     * Destroys the object.
     */
    void destroy(void)
    {
        DnDTransferListDestroy(&List);
    }

    /**
     * Resets the object.
     */
    void reset(void)
    {
        DnDDroppedFilesClose(&DroppedFiles);
        DnDTransferListReset(&List);
        DnDTransferObjectReset(&ObjCur);

        GuestDnDTransferData::reset();
    }

    /** The "VirtualBox Dropped Files" directory on the host we're going
     *  to utilize for transferring files from guest to the host. */
    DNDDROPPEDFILES                     DroppedFiles;
    /** Transfer List to handle.
     *  Currently we only support one transfer list at a time. */
    DNDTRANSFERLIST                     List;
    /** Current transfer object being handled.
     *  Currently we only support one transfer object at a time. */
    DNDTRANSFEROBJECT                   ObjCur;
};

/**
 * Context structure for receiving data from the guest.
 */
struct GuestDnDRecvCtx : public GuestDnDData
{
    GuestDnDRecvCtx(void);

    /**
     * Resets the object.
     */
    void reset(void);

    /** Pointer to guest source class this context belongs to. */
    GuestDnDSource                     *pSource;
    /** Pointer to guest state this context belongs to. */
    GuestDnDState                      *pState;
    /** Formats offered by the guest (and supported by the host). */
    GuestDnDMIMEList                    lstFmtOffered;
    /** Original drop format requested to receive from the guest. */
    com::Utf8Str                        strFmtReq;
    /** Intermediate drop format to be received from the guest.
     *  Some original drop formats require a different intermediate
     *  drop format:
     *
     *  Receiving a file link as "text/plain"  requires still to
     *  receive the file from the guest as "text/uri-list" first,
     *  then pointing to the file path on the host with the data
     *  in "text/plain" format returned. */
    com::Utf8Str                        strFmtRecv;
    /** Desired drop action to perform on the host.
     *  Needed to tell the guest if data has to be
     *  deleted e.g. when moving instead of copying. */
    VBOXDNDACTION                       enmAction;
    /** Transfer data structure. */
    GuestDnDTransferRecvData            Transfer;
    /** Callback event to use. */
    GuestDnDCallbackEvent               EventCallback;
};

/**
 * Class for maintainig a (buffered) guest DnD message.
 */
class GuestDnDMsg
{
public:

    GuestDnDMsg(void)
        : uMsg(0)
        , cParms(0)
        , cParmsAlloc(0)
        , paParms(NULL) { }

    virtual ~GuestDnDMsg(void)
    {
        reset();
    }

public:

    /**
     * Appends a new HGCM parameter to the message and returns the pointer to it.
     */
    PVBOXHGCMSVCPARM getNextParam(void)
    {
        if (cParms >= cParmsAlloc)
        {
            if (!paParms)
                paParms = (PVBOXHGCMSVCPARM)RTMemAlloc(4 * sizeof(VBOXHGCMSVCPARM));
            else
                paParms = (PVBOXHGCMSVCPARM)RTMemRealloc(paParms, (cParmsAlloc + 4) * sizeof(VBOXHGCMSVCPARM));
            if (!paParms)
                throw VERR_NO_MEMORY;
            RT_BZERO(&paParms[cParmsAlloc], 4 * sizeof(VBOXHGCMSVCPARM));
            cParmsAlloc += 4;
        }

        return &paParms[cParms++];
    }

    /**
     * Returns the current parameter count.
     *
     * @returns Current parameter count.
     */
    uint32_t getCount(void) const { return cParms; }

    /**
     * Returns the pointer to the beginning of the HGCM parameters array. Use with care.
     *
     * @returns Pointer to the beginning of the HGCM parameters array.
     */
    PVBOXHGCMSVCPARM getParms(void) const { return paParms; }

    /**
     * Returns the message type.
     *
     * @returns Message type.
     */
    uint32_t getType(void) const { return uMsg; }

    /**
     * Resets the object.
     */
    void reset(void)
    {
        if (paParms)
        {
            /* Remove deep copies. */
            for (uint32_t i = 0; i < cParms; i++)
            {
                if (   paParms[i].type == VBOX_HGCM_SVC_PARM_PTR
                    && paParms[i].u.pointer.size)
                {
                    AssertPtr(paParms[i].u.pointer.addr);
                    RTMemFree(paParms[i].u.pointer.addr);
                }
            }

            RTMemFree(paParms);
            paParms = NULL;
        }

        uMsg = cParms = cParmsAlloc = 0;
    }

    /**
     * Appends a new message parameter of type pointer.
     *
     * @returns VBox status code.
     * @param   pvBuf           Pointer to data to use.
     * @param   cbBuf           Size (in bytes) of data to use.
     */
    int appendPointer(void *pvBuf, uint32_t cbBuf)
    {
        PVBOXHGCMSVCPARM pParm = getNextParam();
        if (!pParm)
            return VERR_NO_MEMORY;

        void *pvTmp = NULL;
        if (cbBuf)
        {
            AssertPtr(pvBuf);
            pvTmp = RTMemDup(pvBuf, cbBuf);
            if (!pvTmp)
                return VERR_NO_MEMORY;
        }

        HGCMSvcSetPv(pParm, pvTmp, cbBuf);
        return VINF_SUCCESS;
    }

    /**
     * Appends a new message parameter of type string.
     *
     * @returns VBox status code.
     * @param   pszString       Pointer to string data to use.
     */
    int appendString(const char *pszString)
    {
        PVBOXHGCMSVCPARM pParm = getNextParam();
        if (!pParm)
            return VERR_NO_MEMORY;

        char *pszTemp = RTStrDup(pszString);
        if (!pszTemp)
            return VERR_NO_MEMORY;

        HGCMSvcSetStr(pParm, pszTemp);
        return VINF_SUCCESS;
    }

    /**
     * Appends a new message parameter of type uint32_t.
     *
     * @returns VBox status code.
     * @param   u32Val          uint32_t value to use.
     */
    int appendUInt32(uint32_t u32Val)
    {
        PVBOXHGCMSVCPARM pParm = getNextParam();
        if (!pParm)
            return VERR_NO_MEMORY;

        HGCMSvcSetU32(pParm, u32Val);
        return VINF_SUCCESS;
    }

    /**
     * Appends a new message parameter of type uint64_t.
     *
     * @returns VBox status code.
     * @param   u64Val          uint64_t value to use.
     */
    int appendUInt64(uint64_t u64Val)
    {
        PVBOXHGCMSVCPARM pParm = getNextParam();
        if (!pParm)
            return VERR_NO_MEMORY;

        HGCMSvcSetU64(pParm, u64Val);
        return VINF_SUCCESS;
    }

    /**
     * Sets the HGCM message type (function number).
     *
     * @param   uMsgType        Message type to set.
     */
    void setType(uint32_t uMsgType) { uMsg = uMsgType; }

protected:

    /** Message type. */
    uint32_t                    uMsg;
    /** Message parameters. */
    uint32_t                    cParms;
    /** Size of array. */
    uint32_t                    cParmsAlloc;
    /** Array of HGCM parameters */
    PVBOXHGCMSVCPARM            paParms;
};

/** Guest DnD callback function definition. */
typedef DECLCALLBACKPTR(int, PFNGUESTDNDCALLBACK,(uint32_t uMsg, void *pvParms, size_t cbParms, void *pvUser));

/**
 * Structure for keeping a guest DnD callback.
 * Each callback can handle one HGCM message, however, multiple HGCM messages can be registered
 * to the same callback (function).
 */
typedef struct GuestDnDCallback
{
    GuestDnDCallback(void)
        : uMessgage(0)
        , pfnCallback(NULL)
        , pvUser(NULL) { }

    GuestDnDCallback(PFNGUESTDNDCALLBACK pvCB, uint32_t uMsg, void *pvUsr = NULL)
        : uMessgage(uMsg)
        , pfnCallback(pvCB)
        , pvUser(pvUsr) { }

    /** The HGCM message ID to handle. */
    uint32_t             uMessgage;
    /** Pointer to callback function. */
    PFNGUESTDNDCALLBACK  pfnCallback;
    /** Pointer to user-supplied data. */
    void                *pvUser;
} GuestDnDCallback;

/** Contains registered callback pointers for specific HGCM message types. */
typedef std::map<uint32_t, GuestDnDCallback> GuestDnDCallbackMap;

/**
 * Class for keeping a DnD guest state around.
 */
class GuestDnDState
{

public:
    DECLARE_TRANSLATE_METHODS(GuestDnDState)

    GuestDnDState(const ComObjPtr<Guest>& pGuest);
    virtual ~GuestDnDState(void);

public:

    VBOXDNDSTATE get(void) const { return m_enmState; }
    int set(VBOXDNDSTATE enmState) { LogRel3(("DnD: State %s -> %s\n", DnDStateToStr(m_enmState), DnDStateToStr(enmState))); m_enmState = enmState; return 0; }
    void lock() { RTCritSectEnter(&m_CritSect); };
    void unlock() { RTCritSectLeave(&m_CritSect); };

    /** @name Guest response handling.
     * @{ */
    int notifyAboutGuestResponse(int vrcGuest = VINF_SUCCESS);
    int waitForGuestResponseEx(RTMSINTERVAL msTimeout = 3000, int *pvrcGuest = NULL);
    int waitForGuestResponse(int *pvrcGuest = NULL);
    /** @} */

    void setActionsAllowed(VBOXDNDACTIONLIST a) { m_dndLstActionsAllowed = a; }
    VBOXDNDACTIONLIST getActionsAllowed(void) const { return m_dndLstActionsAllowed; }

    void setActionDefault(VBOXDNDACTION a) { m_dndActionDefault = a; }
    VBOXDNDACTION getActionDefault(void) const { return m_dndActionDefault; }

    void setFormats(const GuestDnDMIMEList &lstFormats) { m_lstFormats = lstFormats; }
    GuestDnDMIMEList formats(void) const { return m_lstFormats; }

    void reset(void);

    /** @name Callback handling.
     * @{ */
    static DECLCALLBACK(int) i_defaultCallback(uint32_t uMsg, void *pvParms, size_t cbParms, void *pvUser);
    int setCallback(uint32_t uMsg, PFNGUESTDNDCALLBACK pfnCallback, void *pvUser = NULL);
    /** @} */

    /** @name Progress handling.
     * @{ */
    bool isProgressCanceled(void) const;
    bool isProgressRunning(void) const;
    int setProgress(unsigned uPercentage, uint32_t uStatus, int vrcOp = VINF_SUCCESS, const Utf8Str &strMsg = Utf8Str::Empty);
    HRESULT resetProgress(const ComObjPtr<Guest>& pParent, const Utf8Str &strDesc);
    HRESULT queryProgressTo(IProgress **ppProgress);
    /** @} */

public:

    /** @name HGCM callback handling.
       @{ */
    int onDispatch(uint32_t u32Function, void *pvParms, uint32_t cbParms);
    /** @}  */

public:

    /** Pointer to context this class is tied to. */
    void                 *m_pvCtx;
    RTCRITSECT            m_CritSect;
    /** The current state we're in. */
    VBOXDNDSTATE          m_enmState;
    /** The DnD protocol version to use, depending on the
     *  installed Guest Additions. See DragAndDropSvc.h for
     *  a protocol changelog. */
    uint32_t              m_uProtocolVersion;
    /** The guest feature flags reported to the host (VBOX_DND_GF_XXX).  */
    uint64_t              m_fGuestFeatures0;
    /** Event for waiting for response. */
    RTSEMEVENT            m_EventSem;
    /** Last error reported from guest.
     *  Set to VERR_IPE_UNINITIALIZED_STATUS if not set yet. */
    int                   m_vrcGuest;
    /** Default action to perform in case of a
     *  successful drop. */
    VBOXDNDACTION         m_dndActionDefault;
    /** Actions supported by the guest in case of a successful drop. */
    VBOXDNDACTIONLIST     m_dndLstActionsAllowed;
    /** Format(s) requested/supported from the guest. */
    GuestDnDMIMEList      m_lstFormats;
    /** Pointer to IGuest parent object. */
    ComObjPtr<Guest>      m_pParent;
    /** Pointer to associated progress object. Optional. */
    ComObjPtr<Progress>   m_pProgress;
    /** Callback map. */
    GuestDnDCallbackMap   m_mapCallbacks;
};

/**
 * Private singleton class for the guest's DnD implementation.
 *
 * Can't be instanciated directly, only via the factory pattern.
 * Keeps track of all ongoing DnD transfers.
 */
class GuestDnD
{
public:

    /**
     * Creates the Singleton GuestDnD object.
     *
     * @returns Newly created Singleton object, or NULL on failure.
     */
    static GuestDnD *createInstance(const ComObjPtr<Guest>& pGuest)
    {
        Assert(NULL == GuestDnD::s_pInstance);
        GuestDnD::s_pInstance = new GuestDnD(pGuest);
        return GuestDnD::s_pInstance;
    }

    /**
     * Destroys the Singleton GuestDnD object.
     */
    static void destroyInstance(void)
    {
        if (GuestDnD::s_pInstance)
        {
            delete GuestDnD::s_pInstance;
            GuestDnD::s_pInstance = NULL;
        }
    }

    /**
     * Returns the Singleton GuestDnD object.
     *
     * @returns Pointer to Singleton GuestDnD object, or NULL if not created yet.
     */
    static inline GuestDnD *getInstance(void)
    {
        AssertPtr(GuestDnD::s_pInstance);
        return GuestDnD::s_pInstance;
    }

protected:

    /** List of registered DnD sources. */
    typedef std::list< ComObjPtr<GuestDnDSource> > GuestDnDSrcList;
    /** List of registered DnD targets. */
    typedef std::list< ComObjPtr<GuestDnDTarget> > GuestDnDTgtList;

    /** Constructor; will throw vrc on failure. */
    GuestDnD(const ComObjPtr<Guest>& pGuest);
    virtual ~GuestDnD(void);

public:

    /** @name Public helper functions.
     * @{ */
    HRESULT           adjustScreenCoordinates(ULONG uScreenId, ULONG *puX, ULONG *puY) const;
    GuestDnDState    *getState(uint32_t = 0) const;
    int               hostCall(uint32_t u32Function, uint32_t cParms, PVBOXHGCMSVCPARM paParms) const;
    GuestDnDMIMEList  defaultFormats(void) const { return m_strDefaultFormats; }
    /** @}  */

    /** @name Source / target management.
     * @{ */
    int               registerSource(const ComObjPtr<GuestDnDSource> &Source);
    int               unregisterSource(const ComObjPtr<GuestDnDSource> &Source);
    size_t            getSourceCount(void);

    int               registerTarget(const ComObjPtr<GuestDnDTarget> &Target);
    int               unregisterTarget(const ComObjPtr<GuestDnDTarget> &Target);
    size_t            getTargetCount(void);
    /** @}  */

public:

    /** @name Static low-level HGCM callback handler.
     * @{ */
    static DECLCALLBACK(int)   notifyDnDDispatcher(void *pvExtension, uint32_t u32Function, void *pvParms, uint32_t cbParms);
    /** @}  */

    /** @name Static helper methods.
     * @{ */
    static bool                     isFormatInFormatList(const com::Utf8Str &strFormat, const GuestDnDMIMEList &lstFormats);
    static GuestDnDMIMEList         toFormatList(const com::Utf8Str &strFormats, const com::Utf8Str &strSep = DND_FORMATS_SEPARATOR_STR);
    static com::Utf8Str             toFormatString(const GuestDnDMIMEList &lstFormats, const com::Utf8Str &strSep = DND_FORMATS_SEPARATOR_STR);
    static GuestDnDMIMEList         toFilteredFormatList(const GuestDnDMIMEList &lstFormatsSupported, const GuestDnDMIMEList &lstFormatsWanted);
    static GuestDnDMIMEList         toFilteredFormatList(const GuestDnDMIMEList &lstFormatsSupported, const com::Utf8Str &strFormatsWanted);
    static DnDAction_T              toMainAction(VBOXDNDACTION dndAction);
    static std::vector<DnDAction_T> toMainActions(VBOXDNDACTIONLIST dndActionList);
    static VBOXDNDACTION            toHGCMAction(DnDAction_T enmAction);
    static void                     toHGCMActions(DnDAction_T enmDefAction, VBOXDNDACTION *pDefAction, const std::vector<DnDAction_T> vecAllowedActions, VBOXDNDACTIONLIST *pLstAllowedActions);
    /** @}  */

protected:

    /** @name Singleton properties.
     * @{ */
    /** List of supported default MIME/Content-type formats. */
    GuestDnDMIMEList            m_strDefaultFormats;
    /** Pointer to guest implementation. */
    const ComObjPtr<Guest>      m_pGuest;
    /** The current state from the guest. At the
     *  moment we only support only state a time (ARQ-style). */
    GuestDnDState              *m_pState;
    /** Critical section to serialize access. */
    RTCRITSECT                  m_CritSect;
    /** Number of active transfers (guest->host or host->guest). */
    uint32_t                    m_cTransfersPending;
    GuestDnDSrcList             m_lstSrc;
    GuestDnDTgtList             m_lstTgt;
    /** @}  */

private:

    /** Static pointer to singleton instance. */
    static GuestDnD           *s_pInstance;
};

/** Access to the GuestDnD's singleton instance. */
#define GuestDnDInst() GuestDnD::getInstance()

/** List of pointers to guest DnD Messages. */
typedef std::list<GuestDnDMsg *> GuestDnDMsgList;

/**
 * IDnDBase class implementation for sharing code between
 * IGuestDnDSource and IGuestDnDTarget implementation.
 */
class GuestDnDBase
{
protected:

    GuestDnDBase(VirtualBoxBase *pBase);

    virtual ~GuestDnDBase(void);

protected:

    /** Shared (internal) IDnDBase method implementations.
     * @{ */
    bool i_isFormatSupported(const com::Utf8Str &aFormat) const;
    const GuestDnDMIMEList &i_getFormats(void) const;
    HRESULT i_addFormats(const GuestDnDMIMEList &aFormats);
    HRESULT i_removeFormats(const GuestDnDMIMEList &aFormats);
    /** @}  */

    /** @name Error handling.
     * @{ */
    HRESULT i_setErrorV(int vrc, const char *pcszMsgFmt, va_list va);
    HRESULT i_setError(int vrc, const char *pcszMsgFmt, ...);
    HRESULT i_setErrorAndReset(const char *pcszMsgFmt, ...);
    HRESULT i_setErrorAndReset(int vrc, const char *pcszMsgFmt, ...);
    /** @}  */

protected:

    /** @name Pure virtual functions needed to be implemented by the actual (derived) implementation.
     * @{ */
    virtual void i_reset(void) = 0;
    /** @}  */

protected:

    /** @name Functions for handling a simple host HGCM message queue.
     * @{ */
    int msgQueueAdd(GuestDnDMsg *pMsg);
    GuestDnDMsg *msgQueueGetNext(void);
    void msgQueueRemoveNext(void);
    void msgQueueClear(void);
    /** @}  */

    int sendCancel(void);
    int updateProgress(GuestDnDData *pData, GuestDnDState *pState, size_t cbDataAdd = 0);
    int waitForEvent(GuestDnDCallbackEvent *pEvent, GuestDnDState *pState, RTMSINTERVAL msTimeout);

protected:

    /** Pointer to base class to use for stuff like error handlng. */
    VirtualBoxBase                 *m_pBase;
    /** @name Public attributes (through getters/setters).
     * @{ */
    /** Pointer to guest implementation. */
    const ComObjPtr<Guest>          m_pGuest;
    /** List of supported MIME types by the source. */
    GuestDnDMIMEList                m_lstFmtSupported;
    /** List of offered MIME types to the counterpart. */
    GuestDnDMIMEList                m_lstFmtOffered;
    /** Whether the object still is in pending state. */
    bool                            m_fIsPending;
    /** Pointer to state bound to this object. */
    GuestDnDState                  *m_pState;
    /** @}  */

    /**
     * Internal stuff.
     */
    struct
    {
        /** Outgoing message queue (FIFO). */
        GuestDnDMsgList             lstMsgOut;
    } m_DataBase;
};
#endif /* !MAIN_INCLUDED_GuestDnDPrivate_h */

