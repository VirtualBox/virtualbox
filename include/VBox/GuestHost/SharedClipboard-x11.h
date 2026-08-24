/** @file
 * Shared Clipboard - Common X11 code.
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

#ifndef VBOX_INCLUDED_GuestHost_SharedClipboard_x11_h
#define VBOX_INCLUDED_GuestHost_SharedClipboard_x11_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <X11/Intrinsic.h>

#include <iprt/req.h>
#include <iprt/thread.h>

#include <VBox/GuestHost/SharedClipboard.h>
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif

/**
 * The maximum number of simultaneous connections to shared clipboard service.
 * This constant limits amount of GUEST -> HOST connections to shared clipboard host service
 * for X11 host only. Once amount of connections reaches this number, all the
 * further attempts to CONNECT will be dropped on an early stage. Possibility to connect
 * is available again after one of existing connections is closed by DISCONNECT call.
 */
#define VBOX_SHARED_CLIPBOARD_X11_CONNECTIONS_MAX   (20)

/** Enables the Xt busy / update handling. */
#define VBOX_WITH_SHARED_CLIPBOARD_XT_BUSY      1

/**
 * Enumeration for all clipboard formats which we support on X11.
 */
typedef enum SHCLX11FMT
{
    SHCLX11FMT_INVALID = 0,
    SHCLX11FMT_TARGETS,
    SHCLX11FMT_TEXT,  /* Treat this as UTF-8, but it may really be ascii */
    SHCLX11FMT_UTF8,
    SHCLX11FMT_BMP,
     SHCLX11FMT_HTML
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** URI list as (UTF-8) text. */
    , SHCLX11FMT_URI_LIST
    /** URI list as a representation for copying files for GNOME-based applications. */
    , SHCLX11FMT_URI_LIST_GNOME_COPIED_FILES
    /** URI list as a representation for copying files for MATE-based applications. */
    , SHCLX11FMT_URI_LIST_MATE_COPIED_FILES
    /** URI list as representation for copying files for the Nautilus file manager (GNOME). */
    , SHCLX11FMT_URI_LIST_NAUTILUS_CLIPBOARD
    /** URI list as a representation for copying files for KDE-based applications.
     *  Also being used for Dolphin (KDE). */
    , SHCLX11FMT_URI_LIST_KDE_CUTSELECTION
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
} SHCLX11FMT;

/**
 * The table maps X11 names to data formats
 * and to the corresponding VBox clipboard formats.
 */
typedef struct SHCLX11FMTTABLE
{
    /** The X11 atom name of the format (several names can match one format). */
    const char     *pcszAtom;
    /** The format corresponding to the name. */
    SHCLX11FMT      enmFmtX11;
    /** The corresponding VBox clipboard format. */
    SHCLFORMAT      uFmtVBox;
} SHCLX11FMTTABLE;

#define NIL_CLIPX11FORMAT   0

/** Defines an index of the X11 clipboard format table. */
typedef unsigned SHCLX11FMTIDX;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
/**
 * Backend-neutral state for asynchronously preparing and publishing an X11
 * HTTP-backed file-transfer offer.
 *
 * The backend which embeds this structure supplies the synchronization.  The
 * offer generation rejects work completed for an old clipboard offer, while
 * the transfer ID and generation identify the exact transfer associated with
 * the preparation or currently advertised URI-list data.
 */
typedef struct SHCLX11TRANSFERSTATE
{
    /** Most recently reported source formats. */
    SHCLFORMATS       fFormats;
    /** Generation of the most recently reported source clipboard offer. */
    uint64_t          uOfferGeneration;
    /** Offer generation for which a transfer is currently being prepared. */
    uint64_t          uPreparingOfferGeneration;
    /** ID of the transfer bound to the current preparation request. */
    SHCLTRANSFERID    idTransfer;
    /** Generation of the transfer bound to the current request. */
    SHCLTRANSFERGEN   uTransferGeneration;
    /** ID of the transfer backing the currently advertised URI-list data. */
    SHCLTRANSFERID    idPublishedTransfer;
    /** Generation of the transfer backing the advertised URI-list data. */
    SHCLTRANSFERGEN   uPublishedTransferGeneration;
    /** Whether a transfer preparation request is outstanding. */
    bool              fPreparing;
} SHCLX11TRANSFERSTATE;
/** Pointer to X11 transfer state. */
typedef SHCLX11TRANSFERSTATE *PSHCLX11TRANSFERSTATE;
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP */

/**
 * Structure for maintaining a Shared Clipboard context on X11 platforms.
 */
typedef struct SHCLX11CTX
{
    /** Opaque data structure describing the front-end. */
    PSHCLCONTEXT     pFrontend;
    /** Our callback table to use. */
    SHCLCALLBACKS    Callbacks;
    /** The X Toolkit application context structure. */
    XtAppContext     pAppContext;
    /** We have a separate thread to wait for window and clipboard events. */
    RTTHREAD         Thread;
    /** Flag indicating that the thread is in a started state. */
    bool             fThreadStarted;
    /** The X Toolkit widget which we use as our clipboard client.  It is never made visible. */
    Widget           pWidget;
    /** Should we try to grab the clipboard on startup? */
    bool             fGrabClipboardOnStart;
    /** The best text format X11 has to offer, as an index into the formats table. */
    SHCLX11FMTIDX    idxFmtText;
    /** The best bitmap format X11 has to offer, as an index into the formats table. */
    SHCLX11FMTIDX    idxFmtBmp;
    /** The best HTML format X11 has to offer, as an index into the formats table. */
    SHCLX11FMTIDX    idxFmtHTML;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** The best HTML format X11 has to offer, as an index into the formats table. */
    SHCLX11FMTIDX    idxFmtURI;
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    /** HTTP transfer context data. */
    SHCLHTTPCONTEXT  HttpCtx;
# endif
#endif
    /** What kind of formats does VBox have to offer? */
    SHCLFORMATS      vboxFormats;
    /** Formats which must be served exclusively from the cache. */
    SHCLFORMATS      fCacheOnlyFormats;
    /** Internal cache of VBox clipboard formats. */
    SHCLCACHE        Cache;
    /** When we wish the clipboard to exit, we have to wake up the event
     * loop.  We do this by writing into a pipe.  This end of the pipe is
     * the end that another thread can write to.  -1 if not created. */
    int              wakeupPipeWrite;
    /** The reader end of the pipe.  -1 if not created. */
    int              wakeupPipeRead;
    /** A pointer to the XFixesSelectSelectionInput function. */
    void (*fixesSelectInput)(Display *, Window, Atom, unsigned long);
    /** The first XFixes event number. */
    int              fixesEventBase;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_XT_BUSY
    /** XtGetSelectionValue on some versions of libXt isn't re-entrant
     * so block overlapping requests on this flag. */
    bool             fXtBusy;
    /** If a request is blocked on the previous flag, set this flag to request
     * an update later - the first callback should check and clear this flag
     * before processing the callback event. */
    bool             fXtNeedsUpdate;
#endif
} SHCLX11CTX, *PSHCLX11CTX;

/**
 * Enumeration for an X11 event type.
 */
typedef enum SHCLX11EVENTTYPE
{
    /** Invalid event type. */
    SHCLX11EVENTTYPE_INVALID = 0,
    /** Reports formats to X11. */
    SHCLX11EVENTTYPE_REPORT_FORMATS,
    /** Reads clipboard from X11. */
    SHCLX11EVENTTYPE_READ,
    /** Writes clipboard to X11. */
    SHCLX11EVENTTYPE_WRITE
} SHCLX11EVENTTYPE;
/** Pointer to an enumeration for an X11 event type. */
typedef SHCLX11EVENTTYPE *PSHCLX11EVENTTYPE;

/**
 * Structure describing an X11 clipboard request.
 */
typedef struct SHCLX11REQUEST
{
    /** The clipboard context this request is associated with. */
    SHCLX11CTX      *pCtx;
    /** Event associated to this request. */
    PSHCLEVENT       pEvent;
    /** Request type for the union below. */
    SHCLX11EVENTTYPE enmType;
    union
    {
        /** Format announcement to X. */
        struct
        {
            /** VBox formats to announce. */
            SHCLFORMATS      fFormats;
            /** Optional format whose data should be cached before announcing
             *  the formats.  VBOX_SHCL_FMT_NONE if no data was supplied. */
            SHCLFORMAT       uFmtCache;
            /** Optional cache data owned by the request. */
            void            *pvCache;
            /** Size of the optional cache data in bytes. */
            uint32_t         cbCache;
        } Formats;
        /** Read request. */
        struct
        {
            /** The format VBox would like the data in. */
            SHCLFORMAT       uFmtVBox;
            /** The format we requested from X11. */
            SHCLX11FMTIDX    idxFmtX11;
            /** How much bytes to read at max. */
            uint32_t         cbMax;
        } Read;
        /** Write request. */
        struct
        {
            /** The format of the data to write. */
            SHCLFORMAT       uFmtVBox;
            /** The format to write to X11. */
            SHCLX11FMTIDX    idxFmtX11;
            /** Data to write. */
            void            *pvData;
            /** How much bytes to write. */
            uint32_t         cbData;
        } Write;
    };
} SHCLX11REQUEST;
/** Pointer to an X11 clipboard request. */
typedef SHCLX11REQUEST *PSHCLX11REQUEST;

/**
 * Structure describing an X11 clipboard response to an X11 clipboard request.
 */
typedef struct SHCLX11RESPONSE
{
    /** Response type for the union below. */
    SHCLX11EVENTTYPE enmType;
    /** rc (IPRT-style) of the operation performed as part of the X event thread. */
    int              rc;
    union
    {
        struct
        {
            void    *pvData;
            uint32_t cbData;
        } Read;
        struct
        {
            const void *pvData;
            uint32_t    cbData;
        } Write;
    };
} SHCLX11RESPONSE;
/** Pointer to an X11 clipboard response. */
typedef SHCLX11RESPONSE *PSHCLX11RESPONSE;

/** @name Shared Clipboard APIs for X11.
 * @{
 */
int ShClX11Init(PSHCLX11CTX pCtx, PSHCLCALLBACKS pCallbacks, PSHCLCONTEXT pParent);
int ShClX11Term(PSHCLX11CTX pCtx);
int ShClX11ThreadStart(PSHCLX11CTX pCtx, bool grab);
int ShClX11ThreadStartEx(PSHCLX11CTX pCtx, const char *pszName, bool fGrab);
int ShClX11ThreadStop(PSHCLX11CTX pCtx);
int ShClX11ReportCurrentFormatsToVBoxAsync(PSHCLX11CTX pCtx);
int ShClX11ReportFormatsToX11Async(PSHCLX11CTX pCtx, SHCLFORMATS fFormats);
/** Reports formats after atomically seeding one X11 clipboard cache entry. */
int ShClX11ReportFormatsToX11AsyncEx(PSHCLX11CTX pCtx, SHCLFORMATS fFormats, SHCLFORMAT uFmtCache,
                                     const void *pvCache, uint32_t cbCache);
int ShClX11ReadDataFromX11Async(PSHCLX11CTX pCtx, SHCLFORMAT uFmt, uint32_t cbMax, PSHCLEVENT pEvent);
int ShClX11ReadDataFromX11Ex(PSHCLX11CTX pCtx, PSHCLEVENTSOURCE pEventSource, RTMSINTERVAL msTimeout, SHCLFORMAT uFmt, void **ppvBuf, uint32_t *pcbBuf);
int ShClX11ReadDataFromX11(PSHCLX11CTX pCtx, PSHCLEVENTSOURCE pEventSource, RTMSINTERVAL msTimeout, SHCLFORMAT uFmt, void *pvBuf, uint32_t cbBuf, uint32_t *pcbBuf);
int ShClX11WriteDataToX11Async(PSHCLX11CTX pCtx, SHCLFORMAT uFmt, const void *pvBuf, uint32_t cbBuf, PSHCLEVENT pEvent);
int ShClX11WriteDataToX11(PSHCLX11CTX pCtx, PSHCLEVENTSOURCE pEventSource, RTMSINTERVAL msTimeout, SHCLFORMAT uFmt, const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
int ShClX11TransferConvertToX11(const char *pszSrc, size_t cbSrc,  SHCLX11FMT enmFmtX11, void **ppvDst, size_t *pcbDst);
int ShClX11TransferConvertFromX11(const char *pvData, size_t cbData, char **ppszList, size_t *pcbList);
#endif
/** @} */

#endif /* !VBOX_INCLUDED_GuestHost_SharedClipboard_x11_h */
