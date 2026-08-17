/** @file
 * Shared Clipboard - Common guest and host Code.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_GuestHost_SharedClipboard_h
#define VBOX_INCLUDED_GuestHost_SharedClipboard_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <iprt/critsect.h>
#include <iprt/types.h>
#include <iprt/list.h>

/** @name VBOX_SHCL_FMT_XXX - Data formats (flags) for Shared Clipboard.
 * @{
 */
/** No format set. */
#define VBOX_SHCL_FMT_NONE          0
/** Shared Clipboard format is an Unicode text. */
#define VBOX_SHCL_FMT_UNICODETEXT   RT_BIT(0)
/** Shared Clipboard format is bitmap (BMP / DIB). */
#define VBOX_SHCL_FMT_BITMAP        RT_BIT(1)
/** Shared Clipboard format is HTML. */
#define VBOX_SHCL_FMT_HTML          RT_BIT(2)
/** Shared Clipboard format is a transfer list.
 *
 *  When requesting (reading) data with this format, the following happens:
 *  - Acts as a beacon for transfer negotiation / handshake.
 *  - The receiving side (source) initializes a transfer locally.
 *  - The receiving side reports the transfer status (INIT) to the sending side (target).
 *  - The sending side proceeds initializing the transfer locally.
 *  - The sending side reports its transfer status (INIT) to the receiving side.
 *
 *  Note: When receiving an error via a transfer status, the transfer must be destroyed and
 *  is considered as being failed wholesale.
 *
 *  @since 7.1
 */
#define VBOX_SHCL_FMT_URI_LIST      RT_BIT(3)
/** Shared Clipboard format valid mask. */
#define VBOX_SHCL_FMT_VALID_MASK    0xf
/** Maximum number of Shared Clipboard formats.
 *  This currently ASSUMES that there are no gaps in the bit mask. */
#define VBOX_SHCL_FMT_MAX           VBOX_SHCL_FMT_VALID_MASK
/** The last bit (0-based) in VBOX_SHCL_FMT_VALID_MASK */
#define VBOX_SHCL_FMT_LAST_BIT      3
/** @}  */


/** A single Shared Clipboard format (VBOX_SHCL_FMT_XXX). */
typedef uint32_t SHCLFORMAT;
/** Pointer to a single Shared Clipboard format (VBOX_SHCL_FMT_XXX). */
typedef SHCLFORMAT *PSHCLFORMAT;

VBGH_DECL(int) ShClFormatToBitNo(SHCLFORMAT uFmt);

/** Bit map (flags) of Shared Clipboard formats (VBOX_SHCL_FMT_XXX). */
typedef uint32_t SHCLFORMATS;
/** Pointer to a bit map of Shared Clipboard formats (VBOX_SHCL_FMT_XXX). */
typedef SHCLFORMATS *PSHCLFORMATS;

/**
 * Checks whether a value names exactly one Shared Clipboard format.
 *
 * @returns true if @a uFmt is a single valid VBOX_SHCL_FMT_XXX bit, false otherwise.
 * @param   uFmt                Format value to validate.
 */
VBGH_DECL(bool) ShClFormatIsValid(SHCLFORMAT uFmt);

/**
 * Checks whether a Shared Clipboard format mask contains only known format bits.
 *
 * @returns true if @a fFormats only contains VBOX_SHCL_FMT_XXX bits, false otherwise.
 * @param   fFormats            Format mask to validate. VBOX_SHCL_FMT_NONE is valid.
 */
VBGH_DECL(bool) ShClFormatsAreValid(SHCLFORMATS fFormats);

/** Main API Shared Clipboard client/session identifier. */
typedef uint32_t SHCLMAINCLIENTID;
/** Pointer to a Main API Shared Clipboard client/session identifier. */
typedef SHCLMAINCLIENTID *PSHCLMAINCLIENTID;
/** Main API Shared Clipboard client/session identifier. */
typedef SHCLMAINCLIENTID VBOXSHCLMAINCLIENTID;
/** Pointer to a Main API Shared Clipboard client/session identifier. */
typedef VBOXSHCLMAINCLIENTID *PVBOXSHCLMAINCLIENTID;
/** No Main API Shared Clipboard client/session.
 *
 * Used for events and operations that were not originated by an
 * IClipboardSession, such as direct IClipboard calls, calls through
 * IClipboard::hostClipboard, guest/backend-originated events, and internal
 * clipboard state changes.
 */
#define VBOX_SHCL_MAIN_CLIENT_NONE      UINT32_C(0)

/** ClipboardSource_T values. */
enum
{
    VBOX_SHCL_CLIPBOARD_SOURCE_HOST     = 0,
    VBOX_SHCL_CLIPBOARD_SOURCE_GUEST    = 1,
    VBOX_SHCL_CLIPBOARD_SOURCE_REMOTE   = 2,
    VBOX_SHCL_CLIPBOARD_SOURCE_CUSTOM   = 3
};

/** ClipboardMode_T values. */
enum
{
    VBOX_SHCL_CLIPBOARD_MODE_DISABLED       = 0,
    VBOX_SHCL_CLIPBOARD_MODE_HOST_TO_GUEST  = 1,
    VBOX_SHCL_CLIPBOARD_MODE_GUEST_TO_HOST  = 2,
    VBOX_SHCL_CLIPBOARD_MODE_BIDIRECTIONAL  = 3
};

/** ClipboardTransferState_T values. */
enum
{
    VBOX_SHCL_CLIPBOARD_TRANSFER_STATE_ADDED        = 0,
    VBOX_SHCL_CLIPBOARD_TRANSFER_STATE_REMOVED      = 1,
    VBOX_SHCL_CLIPBOARD_TRANSFER_STATE_IN_PROGRESS  = 2,
    VBOX_SHCL_CLIPBOARD_TRANSFER_STATE_INTERACTION  = 3,
    VBOX_SHCL_CLIPBOARD_TRANSFER_STATE_COMPLETED    = 4,
    VBOX_SHCL_CLIPBOARD_TRANSFER_STATE_CANCELED     = 5,
    VBOX_SHCL_CLIPBOARD_TRANSFER_STATE_FAILED       = 6
};

/** Defines the default timeout (in ms) to use for clipboard single wait operations.
 *  Not being used for lenghtly operations as a whole!
 *  Note: Don't set this too high, otherwise the UI feels sluggish. */
#define SHCL_TIMEOUT_DEFAULT_MS                 RT_MS_5SEC


/**
 * Shared Clipboard transfer direction.
 */
typedef enum SHCLTRANSFERDIR
{
    /** Unknown transfer directory. */
    SHCLTRANSFERDIR_UNKNOWN = 0,
    /** Read transfer (from source). */
    SHCLTRANSFERDIR_FROM_REMOTE,
    /** Write transfer (to target). */
    SHCLTRANSFERDIR_TO_REMOTE,
    /** The usual 32-bit hack. */
    SHCLTRANSFERDIR_32BIT_HACK = 0x7fffffff
} SHCLTRANSFERDIR;
/** Pointer to a shared clipboard transfer direction. */
typedef SHCLTRANSFERDIR *PSHCLTRANSFERDIR;

/**
 * Checks whether a Shared Clipboard transfer direction is valid.
 *
 * @returns true if @a enmDir is valid, false otherwise.
 * @param   enmDir              Transfer direction to validate.
 */
VBGH_DECL(bool) ShClTransferDirIsValid(SHCLTRANSFERDIR enmDir);

/**
 * Shared Clipboard event payload (optional).
 */
typedef struct SHCLEVENTPAYLOAD
{
    /** Payload ID; currently unused. */
    uint32_t uID;
    /** Size (in bytes) of actual payload data. */
    uint32_t cbData;
    /** Pointer to actual payload data. */
    void    *pvData;
} SHCLEVENTPAYLOAD;
/** Pointer to a shared clipboard event payload. */
typedef SHCLEVENTPAYLOAD *PSHCLEVENTPAYLOAD;

/** A shared clipboard event source ID. */
typedef uint16_t SHCLEVENTSOURCEID;
/** Pointer to a shared clipboard event source ID. */
typedef SHCLEVENTSOURCEID *PSHCLEVENTSOURCEID;

/** A shared clipboard session ID. */
typedef uint16_t        SHCLSESSIONID;
/** Pointer to a shared clipboard session ID. */
typedef SHCLSESSIONID  *PSHCLSESSIONID;
/** NIL shared clipboard session ID. */
#define NIL_SHCLSESSIONID                        UINT16_MAX

/** A shared clipboard transfer ID. */
typedef uint16_t        SHCLTRANSFERID;
/** Pointer to a shared clipboard transfer ID. */
typedef SHCLTRANSFERID *PSHCLTRANSFERID;
/** NIL shared clipboardtransfer ID. */
#define NIL_SHCLTRANSFERID                       UINT16_MAX

/** A shared clipboard event ID. */
typedef uint32_t        SHCLEVENTID;
/** Pointer to a shared clipboard event source ID. */
typedef SHCLEVENTID    *PSHCLEVENTID;
/** NIL shared clipboard event ID. */
#define NIL_SHCLEVENTID                          UINT32_MAX

/** Pointer to a shared clipboard event source.
 *  Forward declaration, needed for SHCLEVENT. */
typedef struct SHCLEVENTSOURCE *PSHCLEVENTSOURCE;

/**
 * Shared Clipboard event.
 */
typedef struct SHCLEVENT
{
    /** List node. */
    RTLISTNODE          Node;
    /** Parent (source) this event belongs to. */
    PSHCLEVENTSOURCE    pParent;
    /** The event's ID, for self-reference. */
    SHCLEVENTID         idEvent;
    /** User-defined event value. */
    uint32_t            uUser;
    /** Reference count to this event. */
    uint32_t            cRefs;
    /** Event semaphore for signalling the event. */
    RTSEMEVENTMULTI     hEvtMulSem;
    /** Payload to this event, optional (NULL). */
    PSHCLEVENTPAYLOAD   pPayload;
    /** Result code (IPRT-style) to assign. */
    int                 rc;
} SHCLEVENT;
/** Pointer to a shared clipboard event. */
typedef SHCLEVENT *PSHCLEVENT;

/**
 * Shared Clipboard event source.
 *
 * Each event source maintains an own counter for events, so that it can be used
 * in different contexts.
 */
typedef struct SHCLEVENTSOURCE
{
    /** The event source ID. */
    SHCLEVENTSOURCEID uID;
    /** Critical section for serializing access. */
    RTCRITSECT        CritSect;
    /** Next upcoming event ID. */
    SHCLEVENTID       idNextEvent;
    /** List of events (PSHCLEVENT). */
    RTLISTANCHOR      lstEvents;
} SHCLEVENTSOURCE;

/** @name Shared Clipboard data payload functions.
 *  @{
 */
int ShClPayloadCreate(uint32_t uID, void *pvData, uint32_t cbData, PSHCLEVENTPAYLOAD *ppPayload);
int ShClPayloadCreateDupData(uint32_t uID, const void *pvData, uint32_t cbData, PSHCLEVENTPAYLOAD *ppPayload);
void ShClPayloadDestroy(PSHCLEVENTPAYLOAD pPayload);
/** @} */

/** @name Shared Clipboard event source functions.
 *  @{
 */
int ShClEventSourceInit(PSHCLEVENTSOURCE pSource, SHCLEVENTSOURCEID idEvtSrc);
int ShClEventSourceTerm(PSHCLEVENTSOURCE pSource);
void ShClEventSourceReset(PSHCLEVENTSOURCE pSource);
int ShClEventSourceGenerateAndRegisterEvent(PSHCLEVENTSOURCE pSource, PSHCLEVENT *ppEvent);
PSHCLEVENT ShClEventSourceGetFromId(PSHCLEVENTSOURCE pSource, SHCLEVENTID idEvent);
PSHCLEVENT ShClEventSourceRetainFromId(PSHCLEVENTSOURCE pSource, SHCLEVENTID idEvent);
PSHCLEVENT ShClEventSourceGetLast(PSHCLEVENTSOURCE pSource);
/** @} */

/** @name Shared Clipboard event functions.
 *  @{
 */
uint32_t ShClEventGetRefs(PSHCLEVENT pEvent);
uint32_t ShClEventRetain(PSHCLEVENT pEvent);
uint32_t ShClEventRelease(PSHCLEVENT pEvent);
int ShClEventSignalEx(PSHCLEVENT pEvent, int rc, PSHCLEVENTPAYLOAD pPayload);
int ShClEventSignal(PSHCLEVENT pEvent, PSHCLEVENTPAYLOAD pPayload);
int ShClEventWait(PSHCLEVENT pEvent, RTMSINTERVAL uTimeoutMs, PSHCLEVENTPAYLOAD *ppPayload);
int ShClEventWaitEx(PSHCLEVENT pEvent, RTMSINTERVAL uTimeoutMs, int *pRc, PSHCLEVENTPAYLOAD *ppPayload);
/** @} */

/**
 * Shared Clipboard transfer source type.
 * @note Part of saved state!
 */
typedef enum SHCLSOURCE
{
    /** Invalid source type. */
    SHCLSOURCE_INVALID = 0,
    /** Source is local. */
    SHCLSOURCE_LOCAL,
    /** Source is remote. */
    SHCLSOURCE_REMOTE,
    /** The usual 32-bit hack. */
    SHCLSOURCE_32BIT_HACK = 0x7fffffff
} SHCLSOURCE;

/**
 * Checks whether a Shared Clipboard source is valid.
 *
 * @returns true if @a enmSource is valid, false otherwise.
 * @param   enmSource           Source to validate.
 */
VBGH_DECL(bool) ShClSourceIsValid(SHCLSOURCE enmSource);

/** @name Shared Clipboard caching.
 *  @{
 */
/**
 * A single Shared Clipboard cache entry.
 *
 * One entry marks exactly one clipboard format at a time.
 */
typedef struct SHCLCACHEENTRY
{
    /** Entry data.
     *  Acts as a beacon for entry validation. */
    void  *pvData;
    /** Entry data size (in bytes). */
    size_t cbData;
} SHCLCACHEENTRY;
/** Pointer to a Shared Clipboard cache entry. */
typedef SHCLCACHEENTRY *PSHCLCACHEENTRY;

/**
 * A (very simple) Shared Clipboard cache.
 */
typedef struct SHCLCACHE
{
    /** Magic value (SHCLCACHE_MAGIC). */
    uint32_t        u32Magic;
    /** Explicit alignment padding. */
    uint32_t        uReserved;
    /** Entries for all formats.
     *  Right now this is static to keep it simple. */
    SHCLCACHEENTRY  aEntries[VBOX_SHCL_FMT_LAST_BIT + 1];
} SHCLCACHE;
/** Pointer to a Shared Clipboard cache. */
typedef SHCLCACHE *PSHCLCACHE;

/** Magic value for SHCLCACHE::u32Magic (Jasper Fforde). */
#define SHCLCACHE_MAGIC     UINT32_C(0x19610111)

VBGH_DECL(void)             ShClCacheInit(PSHCLCACHE pCache);
VBGH_DECL(void)             ShClCacheTerm(PSHCLCACHE pCache);
VBGH_DECL(void)             ShClCacheInvalidate(PSHCLCACHE pCache);
VBGH_DECL(PSHCLCACHEENTRY)  ShClCacheGet(PSHCLCACHE pCache, SHCLFORMAT uFmt);
VBGH_DECL(int)              ShClCachePrep(PSHCLCACHE pCache, SHCLFORMAT uFmt, size_t cbData, void **ppvData);
VBGH_DECL(int)              ShClCacheSet(PSHCLCACHE pCache, SHCLFORMAT uFmt, const void *pvData, size_t cbData);
VBGH_DECL(int)              ShClCacheSetMultiple(PSHCLCACHE pCache, SHCLFORMATS uFmts, const void *pvData, size_t cbData);
VBGH_DECL(bool)             ShClCacheEquals(SHCLCACHE const *pCache, SHCLCACHE const *pCacheOther);
VBGH_DECL(int)              ShClCacheTransferAll(PSHCLCACHE pCache, PSHCLCACHE pOtherCache);

/** @} */

/** Opaque data structure for the X11/VBox frontend/glue code.
 * @{ */
struct SHCLCONTEXT;
typedef struct SHCLCONTEXT SHCLCONTEXT;
/** @} */
/** Pointer to opaque data structure the X11/VBox frontend/glue code. */
typedef SHCLCONTEXT *PSHCLCONTEXT;

/**
 * Shared Clipboard callback table.
 *
 * This table gets used by
 *   - the backends on the host (where required)
 *   - guest side implementations (e.g. VBoxClient)
 *   - by the underlying core code (e.g. X11 backend -> X11 common code -> callback)
 *
 * Some clipboard mechanisms (e.g. X11) require asynchronous and/or event-driven handling
 * of clipboard data, making it hard to control our program flow when testing stuff.
 *
 * So overriding required callbacks on runtime for testing purposes makes this approach much
 * more flexible without implementing separate code paths for production code and test units.
 */
typedef struct SHCLCALLBACKS
{
    /**
     * Callback for reporting supported clipoard formats of current clipboard data.
     *
     * @note On X11:
     *         Runs in Xt event thread for the X11 code.
     *
     * @returns VBox status code.
     * @param   pCtx            Opaque context pointer for the glue code.
     * @param   fFormats        The formats available.
     * @param   pvUser          Implementation-dependent pointer to data for fullfilling the request.
     *                          Optional.
     */
    DECLCALLBACKMEMBER(int, pfnReportFormats, (PSHCLCONTEXT pCtx, SHCLFORMATS fFormats, void *pvUser));

    /**
     * Optional callback for reading data from the clipboard.
     *
     * @note Used for testing X11 clipboard code.
     *
     * @returns VBox status code.
     * @param   pCtx            Opaque context pointer for the glue code.
     * @param   uFmt            The format in which the data should be read
     *                          (VBOX_SHCL_FMT_XXX).
     * @param   ppv             Returns an allocated buffer with data from on success.
     *                          Needs to be free'd with RTMemFree() by the caller.
     * @param   pcb             Returns the amount of data read (in bytes) on success.
     * @param   pvUser          Implementation-dependent pointer to data for fullfilling the request.
     *                          Optional.
     */
    DECLCALLBACKMEMBER(int, pfnOnClipboardRead, (PSHCLCONTEXT pCtx, SHCLFORMAT uFmt, void **ppv, size_t *pcb, void *pvUser));

    /**
     * Optional callback for writing data to the clipboard.
     *
     * @note Used for testing X11 clipboard code.
     *
     * @returns VBox status code.
     * @param   pCtx            Opaque context pointer for the glue code.
     * @param   uFmt            The format in which the data should be written as
     *                          (VBOX_SHCL_FMT_XXX).
     * @param   pv              The clipboard data to write.
     * @param   cb              The size of the data in @a pv.
     * @param   pvUser          Implementation-dependent pointer to data for fullfilling the request.
     *                          Optional.
     */
    DECLCALLBACKMEMBER(int, pfnOnClipboardWrite, (PSHCLCONTEXT pCtx, SHCLFORMAT uFmt, void *pv, size_t cb, void *pvUser));

    /**
     * Callback for requesting clipboard data from the source.
     *
     * @note On X11:
     *         The function will be invoked for every single target the clipboard requests.
     *         Runs in Xt event thread for the X11 code.
     *
     * @returns VBox status code.
     * @retval  VERR_NO_DATA if no data available.
     * @param   pCtx            Opaque context pointer for the glue code.
     * @param   uFmt            The format in which the data should be transferred
     *                          (VBOX_SHCL_FMT_XXX).
     * @param   ppv             Returns an allocated buffer with data read from the guest on success.
     *                          Needs to be free'd with RTMemFree() by the caller.
     * @param   pcb             Returns the amount of data read (in bytes) on success.
     * @param   pvUser          Implementation-dependent pointer to data for fullfilling the request.
     *                          Optional.
     *                          On X11: Of type PSHCLX11READDATAREQ; We RTMemFree() this in this function.
     */
    DECLCALLBACKMEMBER(int, pfnOnRequestDataFromSource, (PSHCLCONTEXT pCtx, SHCLFORMAT uFmt, void **ppv, uint32_t *pcb, void *pvUser));

    /**
     * Callback for sending clipboard data to the destination.
     *
     * @returns VBox status code.
     * @param   pCtx            Opaque context pointer for the glue code.
     * @param   pv              The clipboard data returned if the request succeeded.
     * @param   cb              The size of the data in @a pv.
     * @param   pvUser          Implementation-dependent pointer to data for fullfilling the request.
     *                          Optional.
     *                          On X11: Of type PSHCLX11READDATAREQ.
     */
    DECLCALLBACKMEMBER(int, pfnOnSendDataToDest, (PSHCLCONTEXT pCtx, void *pv, uint32_t cb, void *pvUser));
} SHCLCALLBACKS;
/** Pointer to a Shared Clipboard callback table. */
typedef SHCLCALLBACKS *PSHCLCALLBACKS;

#endif /* !VBOX_INCLUDED_GuestHost_SharedClipboard_h */

