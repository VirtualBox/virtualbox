/* $Id: clipboard-common.cpp 115050 2026-08-17 15:20:35Z andreas.loeffler@oracle.com $ */
/** @file
 * Shared Clipboard: Common helper objects.
 */

/*
 * Includes contributions from François Revol
 *
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD

#include <iprt/asm.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/semaphore.h>
#include <iprt/path.h>
#include <iprt/rand.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#include <iprt/errcore.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/GuestHost/clipboard-helper.h>


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
static void shClEventSourceResetInternal(PSHCLEVENTSOURCE pSource);
static int shClEventSourceUnregisterEvent(PSHCLEVENTSOURCE pSource, PSHCLEVENT pEvent);

static void shClEventDestroy(PSHCLEVENT pEvent);
DECLINLINE(PSHCLEVENT) shclEventGet(PSHCLEVENTSOURCE pSource, SHCLEVENTID idEvent);

/** Exclusive upper bound for usable event IDs. */
static uint32_t const g_idShClEventEnd = UINT32_MAX - 1;


/*********************************************************************************************************************************
*   Implementation                                                                                                               *
*********************************************************************************************************************************/

/**
 * Allocates a new event payload.
 *
 * @returns VBox status code.
 * @param   uID                 Payload ID to set for this payload. Useful for consequtive payloads.
 * @param   pvData              Data to associate to this payload.
 *                              The payload owns the data then.
 * @param   cbData              Size (in bytes) of data to associate.
 * @param   ppPayload           Where to store the allocated event payload on success.
 */
int ShClPayloadCreate(uint32_t uID, void *pvData, uint32_t cbData, PSHCLEVENTPAYLOAD *ppPayload)
{
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData > 0, VERR_INVALID_PARAMETER);

    PSHCLEVENTPAYLOAD pPayload = (PSHCLEVENTPAYLOAD)RTMemAllocZ(sizeof(SHCLEVENTPAYLOAD));
    if (pPayload)
    {
        pPayload->pvData = pvData;
        pPayload->cbData = cbData;
        pPayload->uID    = uID;

        *ppPayload = pPayload;
        return VINF_SUCCESS;
    }

    return VERR_NO_MEMORY;
}

/**
 * Allocates a new event payload, duplicating the data.
 *
 * @returns VBox status code.
 * @param   uID                 Payload ID to set for this payload. Useful for consequtive payloads.
 * @param   pvData              Data block to allocate (duplicate) to this payload.
 * @param   cbData              Size (in bytes) of data block to allocate.
 * @param   ppPayload           Where to store the allocated event payload on success.
 */
int ShClPayloadCreateDupData(uint32_t uID, const void *pvData, uint32_t cbData, PSHCLEVENTPAYLOAD *ppPayload)
{
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData > 0, VERR_INVALID_PARAMETER);

    void *pvDataDup = RTMemDup(pvData, cbData);
    if (pvDataDup)
    {
        int rc = ShClPayloadCreate(uID, pvDataDup, cbData, ppPayload);
        if (RT_FAILURE(rc))
            RTMemFree(pvDataDup);
        return rc;
    }

    return VERR_NO_MEMORY;
}

/**
 * Frees an event payload.
 *
 * @returns VBox status code.
 * @param   pPayload            Event payload to free.
 */
void ShClPayloadDestroy(PSHCLEVENTPAYLOAD pPayload)
{
    if (!pPayload)
        return;

    if (pPayload->pvData)
    {
        Assert(pPayload->cbData);
        RTMemFree(pPayload->pvData);
        pPayload->pvData = NULL;
    }

    pPayload->cbData = 0;
    pPayload->uID = UINT32_MAX;

    RTMemFree(pPayload);
}

/**
 * Initializes a new event source.
 *
 * @returns VBox status code.
 * @param   pSource             Event source to initialize.
 * @param   uID                 ID to use for event source.
 */
int ShClEventSourceInit(PSHCLEVENTSOURCE pSource, SHCLEVENTSOURCEID uID)
{
    LogFlowFunc(("pSource=%p, uID=%RU16\n", pSource, uID));
    AssertPtrReturn(pSource, VERR_INVALID_POINTER);

    int rc = RTCritSectInit(&pSource->CritSect);
    AssertRCReturn(rc, rc);

    RTListInit(&pSource->lstEvents);

    pSource->uID          = uID;
    /* Choose a random event ID starting point. */
    pSource->idNextEvent  = RTRandU32Ex(1, g_idShClEventEnd - 1);

    return VINF_SUCCESS;
}

/**
 * Terminates (uninitializes) an event source.
 *
 * @returns VBox status code.
 * @param   pSource             Event source to delete.
 */
int ShClEventSourceTerm(PSHCLEVENTSOURCE pSource)
{
    if (!pSource)
        return VINF_SUCCESS;

    if (!RTCritSectIsInitialized(&pSource->CritSect)) /* Already destroyed? Bail out. */
        return VINF_SUCCESS;

    LogFlowFunc(("ID=%RU32\n", pSource->uID));

    int rc = RTCritSectEnter(&pSource->CritSect);
    if (RT_SUCCESS(rc))
    {
        shClEventSourceResetInternal(pSource);

        rc = RTCritSectLeave(&pSource->CritSect);
        AssertRC(rc);

        RTCritSectDelete(&pSource->CritSect);

        pSource->uID          = UINT16_MAX;
        pSource->idNextEvent  = UINT32_MAX;
    }

    return rc;
}

/**
 * Resets an event source, internal version.
 *
 * @param   pSource             Event source to reset.
 */
static void shClEventSourceResetInternal(PSHCLEVENTSOURCE pSource)
{
    LogFlowFunc(("ID=%RU32\n", pSource->uID));

    PSHCLEVENT pEvIt;
    PSHCLEVENT pEvItNext;
    RTListForEachSafe(&pSource->lstEvents, pEvIt, pEvItNext, SHCLEVENT, Node)
    {
        bool const fDealloc = ASMAtomicReadU32(&pEvIt->cRefs) == 0; /* Still any references left? Skip de-allocation. */
        if (!fDealloc)
            Log3Func(("Event %RU32 has %RU32 references left, skipping de-allocation\n", pEvIt->idEvent, pEvIt->cRefs));

        int rc2 = shClEventSourceUnregisterEvent(pSource, pEvIt);
        AssertRC(rc2);

        if (fDealloc)
        {
            shClEventDestroy(pEvIt);
            RTMemFree(pEvIt);
            pEvIt = NULL;
        }
    }
}

/**
 * Resets an event source.
 *
 * @param   pSource             Event source to reset.
 */
void ShClEventSourceReset(PSHCLEVENTSOURCE pSource)
{
    int rc2 = RTCritSectEnter(&pSource->CritSect);
    if (RT_SUCCESS(rc2))
    {
        shClEventSourceResetInternal(pSource);

        rc2 = RTCritSectLeave(&pSource->CritSect);
        AssertRC(rc2);
    }
}

/**
 * Generates a new event ID for a specific event source and registers it.
 *
 * @returns VBox status code.
 * @param   pSource             Event source to generate event for.
 * @param   ppEvent             Where to return the new event generated on success.
 */
int ShClEventSourceGenerateAndRegisterEvent(PSHCLEVENTSOURCE pSource, PSHCLEVENT *ppEvent)
{
    AssertPtrReturn(pSource, VERR_INVALID_POINTER);
    AssertPtrReturn(ppEvent, VERR_INVALID_POINTER);

    PSHCLEVENT pEvent = (PSHCLEVENT)RTMemAllocZ(sizeof(SHCLEVENT));
    AssertReturn(pEvent, VERR_NO_MEMORY);
    int rc = RTSemEventMultiCreate(&pEvent->hEvtMulSem);
    if (RT_SUCCESS(rc))
    {
        rc = RTCritSectEnter(&pSource->CritSect);
        if (RT_SUCCESS(rc))
        {
            /*
             * Allocate an unique event ID.
             */
            for (uint32_t cTries = 0;; cTries++)
            {
                SHCLEVENTID idEvent = ++pSource->idNextEvent;
                if (idEvent < g_idShClEventEnd)
                { /* likely */ }
                else
                    pSource->idNextEvent = idEvent = 1; /* zero == error, remember! */

                if (shclEventGet(pSource, idEvent) == NULL)
                {
                    pEvent->pParent = pSource;
                    pEvent->idEvent = idEvent;
                    pEvent->cRefs  = 1;
                    RTListAppend(&pSource->lstEvents, &pEvent->Node);

                    rc = RTCritSectLeave(&pSource->CritSect);
                    AssertRC(rc);

                    LogFlowFunc(("uSource=%RU16: New event: %#x\n", pSource->uID, idEvent));

                    *ppEvent = pEvent;

                    return VINF_SUCCESS;
                }

                AssertBreak(cTries < 4096);
            }

            rc = RTCritSectLeave(&pSource->CritSect);
            AssertRC(rc);
        }
    }

    AssertMsgFailed(("Unable to register a new event ID for event source %RU16\n", pSource->uID));

    RTSemEventMultiDestroy(pEvent->hEvtMulSem);
    pEvent->hEvtMulSem = NIL_RTSEMEVENTMULTI;
    RTMemFree(pEvent);
    return rc;
}

/**
 * Destroys an event.
 *
 * @param   pEvent              Event to destroy.
 */
static void shClEventDestroy(PSHCLEVENT pEvent)
{
    if (!pEvent)
        return;

    LogFlowFunc(("Event %RU32\n", pEvent->idEvent));

    if (pEvent->hEvtMulSem != NIL_RTSEMEVENT)
    {
        RTSemEventMultiDestroy(pEvent->hEvtMulSem);
        pEvent->hEvtMulSem = NIL_RTSEMEVENT;
    }

    ShClPayloadDestroy(pEvent->pPayload);
    pEvent->pPayload = NULL;

    pEvent->idEvent = NIL_SHCLEVENTID;
}

/**
 * Unregisters an event.
 *
 * @returns VBox status code.
 * @param   pSource             Event source to unregister event for.
 * @param   pEvent              Event to unregister.
 */
static int shClEventSourceUnregisterEvent(PSHCLEVENTSOURCE pSource, PSHCLEVENT pEvent)
{
    RT_NOREF(pSource);

    LogFlowFunc(("idEvent=%RU32, cRefs=%RU32\n", pEvent->idEvent, pEvent->cRefs));

    RTListNodeRemove(&pEvent->Node);
    pEvent->pParent = NULL;

    return VINF_SUCCESS;
}

/**
 * Returns a specific event of a event source. Inlined version.
 *
 * @returns Pointer to event if found, or NULL if not found.
 * @param   pSource             Event source to get event from.
 * @param   uID                 Event ID to get.
 */
DECLINLINE(PSHCLEVENT) shclEventGet(PSHCLEVENTSOURCE pSource, SHCLEVENTID idEvent)
{
    PSHCLEVENT pEvent;
    RTListForEach(&pSource->lstEvents, pEvent, SHCLEVENT, Node)
    {
        if (pEvent->idEvent == idEvent)
            return pEvent;
    }

    return NULL;
}

/**
 * Tries retaining an event without reviving an already released event.
 *
 * @returns New reference count, or UINT32_MAX on failure.
 * @param   pEvent              Event to retain.
 */
static uint32_t shclEventTryRetain(PSHCLEVENT pEvent)
{
    uint32_t cRefs = ASMAtomicReadU32(&pEvent->cRefs);
    for (;;)
    {
        if (   cRefs == 0
            || cRefs >= 64)
            return UINT32_MAX;
        if (ASMAtomicCmpXchgExU32(&pEvent->cRefs, cRefs + 1, cRefs, &cRefs))
            return cRefs + 1;
    }
}

/**
 * Returns a specific event of a event source.
 *
 * @returns Pointer to event if found, or NULL if not found.
 * @param   pSource             Event source to get event from.
 * @param   idEvent             ID of event to return.
 */
PSHCLEVENT ShClEventSourceGetFromId(PSHCLEVENTSOURCE pSource, SHCLEVENTID idEvent)
{
    AssertPtrReturn(pSource, NULL);

    int rc = RTCritSectEnter(&pSource->CritSect);
    if (RT_SUCCESS(rc))
    {
         PSHCLEVENT pEvent = shclEventGet(pSource, idEvent);

         rc = RTCritSectLeave(&pSource->CritSect);
         AssertRC(rc);

         return pEvent;
    }

    return NULL;
}

/**
 * Returns and retains a specific event of an event source.
 *
 * @returns Pointer to retained event if found, or NULL if not found or already
 *          being released. Must be released with ShClEventRelease().
 * @param   pSource             Event source to get event from.
 * @param   idEvent             ID of event to return.
 */
PSHCLEVENT ShClEventSourceRetainFromId(PSHCLEVENTSOURCE pSource, SHCLEVENTID idEvent)
{
    AssertPtrReturn(pSource, NULL);

    int rc = RTCritSectEnter(&pSource->CritSect);
    if (RT_SUCCESS(rc))
    {
        PSHCLEVENT pEvent = shclEventGet(pSource, idEvent);
        if (   pEvent
            && shclEventTryRetain(pEvent) == UINT32_MAX)
            pEvent = NULL;

        rc = RTCritSectLeave(&pSource->CritSect);
        AssertRC(rc);

        return pEvent;
    }

    return NULL;
}

/**
 * Returns the last (newest) event ID which has been registered for an event source.
 *
 * @returns Pointer to last registered event, or NULL if not found.
 * @param   pSource             Event source to get last registered event from.
 */
PSHCLEVENT ShClEventSourceGetLast(PSHCLEVENTSOURCE pSource)
{
    AssertPtrReturn(pSource, NULL);

    int rc = RTCritSectEnter(&pSource->CritSect);
    if (RT_SUCCESS(rc))
    {
        PSHCLEVENT pEvent = RTListGetLast(&pSource->lstEvents, SHCLEVENT, Node);

        rc = RTCritSectLeave(&pSource->CritSect);
        AssertRC(rc);

        return pEvent;
    }

    return NULL;
}

/**
 * Returns the current reference count for a specific event.
 *
 * @returns Reference count.
 * @param   pSource             Event source the specific event is part of.
 * @param   idEvent             Event ID to return reference count for.
 */
uint32_t ShClEventGetRefs(PSHCLEVENT pEvent)
{
    AssertPtrReturn(pEvent, 0);

    return ASMAtomicReadU32(&pEvent->cRefs);
}

/**
 * Detaches a payload from an event, internal version.
 *
 * @returns Pointer to the detached payload. Can be NULL if the event has no payload.
 * @param   pEvent              Event to detach payload for.
 */
static PSHCLEVENTPAYLOAD shclEventPayloadDetachInternal(PSHCLEVENT pEvent)
{
#ifdef VBOX_STRICT
    AssertPtrReturn(pEvent, NULL);
#endif

    PSHCLEVENTPAYLOAD pPayload = pEvent->pPayload;

    pEvent->pPayload = NULL;

    return pPayload;
}

/**
 * Waits for an event to get signalled.
 *
 * @returns VBox status code.
 * @retval  VERR_SHCLPB_EVENT_FAILED if the event has a set error code.
 * @param   pEvent              Event to wait for.
 * @param   uTimeoutMs          Timeout (in ms) to wait.
 * @param   pRc                 Where to return the event rc. Optional and can be NULL.
 * @param   ppPayload           Where to store the (allocated) event payload on success. Needs to be free'd with
 *                              SharedClipboardPayloadFree(). Optional.
 */
int ShClEventWaitEx(PSHCLEVENT pEvent, RTMSINTERVAL uTimeoutMs, int *pRc, PSHCLEVENTPAYLOAD *ppPayload)
{
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);
    AssertPtrNullReturn(ppPayload, VERR_INVALID_POINTER);
    LogFlowFuncEnter();

    int rc = RTSemEventMultiWait(pEvent->hEvtMulSem, uTimeoutMs);
    if (RT_SUCCESS(rc))
    {
        if (RT_FAILURE(pEvent->rc))
            rc = VERR_SHCLPB_EVENT_FAILED;

        if (pRc)
            *pRc = pEvent->rc;

        if (ppPayload)
        {
            /* Make sure to detach payload here, as the caller now owns the data. */
            *ppPayload = shclEventPayloadDetachInternal(pEvent);
        }
    }

    if (RT_FAILURE(rc))
        LogRel2(("Shared Clipboard: Waiting for event %RU32 failed, rc=%Rrc\n", pEvent->idEvent, rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Waits for an event to get signalled.
 *
 * @returns VBox status code.
 * @retval  VERR_SHCLPB_EVENT_FAILED if the event has a set error code.
 * @param   pEvent              Event to wait for.
 * @param   uTimeoutMs          Timeout (in ms) to wait.
 * @param   ppPayload           Where to store the (allocated) event payload on success. Needs to be free'd with
 *                              SharedClipboardPayloadFree(). Optional.
 */
int ShClEventWait(PSHCLEVENT pEvent, RTMSINTERVAL uTimeoutMs, PSHCLEVENTPAYLOAD *ppPayload)
{
    return ShClEventWaitEx(pEvent, uTimeoutMs, NULL /* pRc */, ppPayload);
}

/**
 * Retains an event by increasing its reference count.
 *
 * @returns New reference count, or UINT32_MAX if failed.
 * @param   pEvent              Event to retain.
 */
uint32_t ShClEventRetain(PSHCLEVENT pEvent)
{
    AssertPtrReturn(pEvent, UINT32_MAX);
    uint32_t const cRefs = shclEventTryRetain(pEvent);
    AssertReturn(cRefs != UINT32_MAX, UINT32_MAX);
    return cRefs;
}

/**
 * Releases event by decreasing its reference count. Will be destroyed once the reference count reaches 0.
 *
 * @returns New reference count, or UINT32_MAX if failed.
 * @param   pEvent              Event to release.
 *                              If the reference count reaches 0, the event will
 *                              be destroyed and \a pEvent will be invalid.
 */
uint32_t ShClEventRelease(PSHCLEVENT pEvent)
{
    if (!pEvent)
        return 0;

    AssertReturn(ASMAtomicReadU32(&pEvent->cRefs) > 0, UINT32_MAX);

    /* Serialize the final release with a source reset, which can detach the event while we wait for the source lock. */
    uint32_t cRefs;
    int rc = VINF_SUCCESS;
    PSHCLEVENTSOURCE pParent = pEvent->pParent;
    if (   pParent
        && RTCritSectIsInitialized(&pParent->CritSect))
    {
        rc = RTCritSectEnter(&pParent->CritSect);
        if (RT_SUCCESS(rc))
        {
            cRefs = ASMAtomicDecU32(&pEvent->cRefs);
            if (   cRefs == 0
                && pEvent->pParent == pParent)
                rc = shClEventSourceUnregisterEvent(pParent, pEvent);

            int rc2 = RTCritSectLeave(&pParent->CritSect);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
        else if (pEvent->pParent == NULL)
        {
            cRefs = ASMAtomicDecU32(&pEvent->cRefs);
            rc = VINF_SUCCESS;
        }
        else
            return UINT32_MAX;
    }
    else
        cRefs = ASMAtomicDecU32(&pEvent->cRefs);

    if (cRefs == 0)
    {
        if (RT_SUCCESS(rc))
        {
            shClEventDestroy(pEvent);

            RTMemFree(pEvent);
            pEvent = NULL;
        }

        return RT_SUCCESS(rc) ? 0 : UINT32_MAX;
    }

    return cRefs;
}

/**
 * Signals an event, extended version.
 *
 * @returns VBox status code.
 * @param   pEvent              Event to signal.
 * @param   rc                  Result code to set.
 * @param   pPayload            Event payload to associate. Takes ownership on
 *                              success. Optional.
 */
int ShClEventSignalEx(PSHCLEVENT pEvent, int rc, PSHCLEVENTPAYLOAD pPayload)
{
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);

    Assert(pEvent->pPayload == NULL);

    pEvent->rc       = rc;
    pEvent->pPayload = pPayload;

    int rc2 = RTSemEventMultiSignal(pEvent->hEvtMulSem);
    if (RT_FAILURE(rc2))
        pEvent->pPayload = NULL; /* (no race condition if consumer also enters the critical section) */

    LogFlowFuncLeaveRC(rc2);
    return rc2;
}

/**
 * Signals an event.
 *
 * @returns VBox status code.
 * @param   pEvent              Event to signal.
 * @param   pPayload            Event payload to associate. Takes ownership on
 *                              success. Optional.
 */
int ShClEventSignal(PSHCLEVENT pEvent, PSHCLEVENTPAYLOAD pPayload)
{
    return ShClEventSignalEx(pEvent, VINF_SUCCESS, pPayload);
}

#ifdef LOG_ENABLED

int ShClDbgDumpHtml(const char *pcszSrc, size_t cbSrc)
{
    int rc = VINF_SUCCESS;
    char *pszBuf = (char *)RTMemTmpAllocZ(cbSrc + 1);
    if (pszBuf)
    {
        memcpy(pszBuf, pcszSrc, cbSrc);
        pszBuf[cbSrc] = '\0';
        for (size_t off = 0; off < cbSrc; ++off)
            if (pszBuf[off] == '\n' || pszBuf[off] == '\r')
                pszBuf[off] = ' ';
        LogFunc(("Removed \\r\\n: %s\n", pszBuf));
        RTMemTmpFree(pszBuf);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}

void ShClDbgDumpData(const void *pv, size_t cb, SHCLFORMAT uFormat)
{
    if (LogIsEnabled())
    {
        if (uFormat & VBOX_SHCL_FMT_UNICODETEXT)
        {
            LogFunc(("VBOX_SHCL_FMT_UNICODETEXT:\n"));
            if (pv && cb)
                LogFunc(("%ls\n", pv));
            else
                LogFunc(("%p %zu\n", pv, cb));
        }
        else if (uFormat & VBOX_SHCL_FMT_BITMAP)
            LogFunc(("VBOX_SHCL_FMT_BITMAP\n"));
        else if (uFormat & VBOX_SHCL_FMT_HTML)
        {
            LogFunc(("VBOX_SHCL_FMT_HTML:\n"));
            if (pv && cb)
            {
                LogFunc(("%s\n", pv));
                ShClDbgDumpHtml((const char *)pv, cb);
            }
            else
                LogFunc(("%p %zu\n", pv, cb));
        }
        else
            LogFunc(("Invalid format %02X\n", uFormat));
    }
}

#endif /* LOG_ENABLED */

/**
 * Converts Shared Clipboard formats to a string.
 *
 * @returns Stringified Shared Clipboard formats, or NULL on failure. Must be free'd with RTStrFree().
 * @param   fFormats            Shared Clipboard formats to convert.
 *
 */
char *ShClFormatsToStrA(SHCLFORMATS fFormats)
{
#define APPEND_FMT_TO_STR(_aFmt)                \
    if (fFormats & VBOX_SHCL_FMT_##_aFmt)       \
    {                                           \
        if (pszFmts)                            \
        {                                       \
            rc2 = RTStrAAppend(&pszFmts, ", "); \
            if (RT_FAILURE(rc2))                \
                break;                          \
        }                                       \
                                                \
        rc2 = RTStrAAppend(&pszFmts, #_aFmt);   \
        if (RT_FAILURE(rc2))                    \
            break;                              \
    }

    char *pszFmts = NULL;
    int rc2 = VINF_SUCCESS;

    do
    {
        APPEND_FMT_TO_STR(UNICODETEXT);
        APPEND_FMT_TO_STR(BITMAP);
        APPEND_FMT_TO_STR(HTML);
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
        APPEND_FMT_TO_STR(URI_LIST);
# endif

    } while (0);

    if (!pszFmts)
        rc2 = RTStrAAppend(&pszFmts, "NONE");

    if (   RT_FAILURE(rc2)
        && pszFmts)
    {
        RTStrFree(pszFmts);
        pszFmts = NULL;
    }

#undef APPEND_FMT_TO_STR

    return pszFmts;
}


/*********************************************************************************************************************************
*   Shared Clipboard validation                                                                                                  *
*********************************************************************************************************************************/

/**
 * Checks whether a value names exactly one Shared Clipboard format.
 *
 * @returns true if @a uFmt is a single valid VBOX_SHCL_FMT_XXX bit, false otherwise.
 * @param   uFmt                Format value to validate.
 */
VBGH_DECL(bool) ShClFormatIsValid(SHCLFORMAT uFmt)
{
    return    uFmt != VBOX_SHCL_FMT_NONE
           && (uFmt & ~VBOX_SHCL_FMT_VALID_MASK) == 0
           && (uFmt & (uFmt - 1)) == 0;
}


/**
 * Checks whether a Shared Clipboard format mask contains only known format bits.
 *
 * @returns true if @a fFormats only contains VBOX_SHCL_FMT_XXX bits, false otherwise.
 * @param   fFormats            Format mask to validate. VBOX_SHCL_FMT_NONE is valid.
 */
VBGH_DECL(bool) ShClFormatsAreValid(SHCLFORMATS fFormats)
{
    return (fFormats & ~VBOX_SHCL_FMT_VALID_MASK) == 0;
}


/**
 * Checks whether a Shared Clipboard transfer direction is valid.
 *
 * @returns true if @a enmDir is valid, false otherwise.
 * @param   enmDir              Transfer direction to validate.
 */
VBGH_DECL(bool) ShClTransferDirIsValid(SHCLTRANSFERDIR enmDir)
{
    return    enmDir == SHCLTRANSFERDIR_FROM_REMOTE
           || enmDir == SHCLTRANSFERDIR_TO_REMOTE;
}


/**
 * Checks whether a Shared Clipboard source is valid.
 *
 * @returns true if @a enmSource is valid, false otherwise.
 * @param   enmSource           Source to validate.
 */
VBGH_DECL(bool) ShClSourceIsValid(SHCLSOURCE enmSource)
{
    return    enmSource == SHCLSOURCE_LOCAL
           || enmSource == SHCLSOURCE_REMOTE;
}


/*********************************************************************************************************************************
*   Shared Clipboard Cache                                                                                                       *
*********************************************************************************************************************************/

/**
 * Return the log2 of @a uFmt.
 *
 * @returns Bit number (0-based) corresponding to @a uFmt.  Will assert if
 *          multiple formats present (first is returned) or if zero (-1 is
 *          returned).
 * @param   uFmt                Single VBox format.
 */
VBGH_DECL(int) ShClFormatToBitNo(SHCLFORMAT uFmt)
{
    AssertReturn(uFmt, -1);
    AssertMsg(RT_IS_POWER_OF_TWO(uFmt), ("%#x\n", uFmt));
    AssertCompile(sizeof(uint32_t) == sizeof(uFmt));
    return (int)ASMBitFirstSetU32(uFmt) - 1;
}

/**
 * Initializes a cache entry.
 *
 * @returns VBox status code.
 * @param   pCacheEntry         Cache entry to init.
 * @param   pvData              Data to copy to entry. Can be NULL to initialize an emptry entry.
 * @param   cbData              Size (in bytes) of \a pvData to copy to entry. Must be 0 if \a pvData is NULL.
 */
static int shClCacheEntryInit(PSHCLCACHEENTRY pCacheEntry, const void *pvData, size_t cbData)
{
    AssertReturn(RT_VALID_PTR(pvData) || cbData == 0, VERR_INVALID_PARAMETER);

    pCacheEntry->cbData = 0;
    pCacheEntry->pvData = NULL;

    if (pvData)
    {
        pCacheEntry->pvData = RTMemDup(pvData, cbData);
        AssertPtrReturn(pCacheEntry->pvData, VERR_NO_MEMORY);
        pCacheEntry->cbData = cbData;
    }

    return VINF_SUCCESS;
}

/**
 * Returns whether a cache entry is valid (cache hit) or not.
 *
 * @returns \c true if valid, or \c false if not.
 * @param   pCacheEntry         Cache entry to check for.
 */
DECLINLINE(bool) shClCacheEntryIsValid(PSHCLCACHEENTRY pCacheEntry)
{
    return pCacheEntry->pvData != NULL;
}

/**
 * Re-initializes a cache entry, freeing any data kept there.
 *
 * @param   pCacheEntry         Cache entry to re-init.
 */
DECLINLINE(void) shClCacheEntryReInit(PSHCLCACHEENTRY pCacheEntry)
{
    if (pCacheEntry->pvData)
    {
        Assert(pCacheEntry->cbData);
        RTMemFree(pCacheEntry->pvData);
        pCacheEntry->pvData = NULL;
        pCacheEntry->cbData = 0;
    }
    else
        Assert(pCacheEntry->cbData == 0);
}

/**
 * Initializes a cache.
 *
 * @param   pCache              Cache to init.
 */
VBGH_DECL(void) ShClCacheInit(PSHCLCACHE pCache)
{
    AssertPtrReturnVoid(pCache);

    RT_ZERO(*pCache);
    pCache->u32Magic = SHCLCACHE_MAGIC;
}

/**
 * Destroys all entries of a cache.
 *
 * @param   pCache              Cache to destroy entries for.
 */
DECLINLINE(void) shClCacheReInitAllEntries(PSHCLCACHE pCache)
{
    for (size_t i = 0; i < RT_ELEMENTS(pCache->aEntries); i++)
        shClCacheEntryReInit(&pCache->aEntries[i]);
}

/**
 * Terminates (uninitializes) a cache.
 *
 * @param   pCache              Cache to destroy.
 */
VBGH_DECL(void) ShClCacheTerm(PSHCLCACHE pCache)
{
    AssertPtrReturnVoid(pCache);
    AssertMsgReturnVoid(pCache->u32Magic == SHCLCACHE_MAGIC, ("%#x\n", pCache->u32Magic));
    pCache->u32Magic = ~SHCLCACHE_MAGIC;

    shClCacheReInitAllEntries(pCache);
}

/**
 * Invalidates a cache.
 *
 * @param   pCache              Cache to invalidate.
 */
VBGH_DECL(void) ShClCacheInvalidate(PSHCLCACHE pCache)
{
    AssertPtrReturnVoid(pCache);
    AssertMsgReturnVoid(pCache->u32Magic == SHCLCACHE_MAGIC, ("%#x\n", pCache->u32Magic));

    shClCacheReInitAllEntries(pCache);
}

/**
 * Gets an entry for a Shared Clipboard format.
 *
 * @returns Pointer to entry if cached, or NULL if not in cache (cache miss).
 * @param   pCache              Cache to get entry for.
 * @param   uFmt                Format to get entry for.
 */
VBGH_DECL(PSHCLCACHEENTRY) ShClCacheGet(PSHCLCACHE pCache, SHCLFORMAT uFmt)
{
    AssertPtrReturn(pCache, NULL);
    AssertMsgReturn(pCache->u32Magic == SHCLCACHE_MAGIC, ("%#x\n", pCache->u32Magic), NULL);
    int const idxFmt = ShClFormatToBitNo(uFmt);
    AssertMsgReturn((unsigned)idxFmt < RT_ELEMENTS(pCache->aEntries), ("%#x/%d\n", uFmt, idxFmt), NULL);

    return shClCacheEntryIsValid(&pCache->aEntries[idxFmt]) ? &pCache->aEntries[idxFmt] : NULL;
}

/**
 * Enteres a clipboard format into the cache before the data is read, returning
 * a buffer for the user to fill.
 *
 * @returns VBox status code.
 * @retval  VERR_ALREADY_EXISTS if the cache entry is not empty.
 * @param   pCache              Cache to set data for.
 * @param   uFmt                Clipboard format to set data for.
 * @param   cbData              Size (in bytes) of data to prep for.
 * @param   ppvData             Where to return the buffer (zeroed) prepared for
 *                              the data.  Caller must fill this.
 */
VBGH_DECL(int) ShClCachePrep(PSHCLCACHE pCache, SHCLFORMAT uFmt, size_t cbData, void **ppvData)
{
    AssertPtrReturn(pCache, VERR_INVALID_POINTER);
    AssertMsgReturn(pCache->u32Magic == SHCLCACHE_MAGIC, ("%#x\n", pCache->u32Magic), VERR_INVALID_MAGIC);
    int const idxFmt = ShClFormatToBitNo(uFmt);
    AssertMsgReturn((unsigned)idxFmt < RT_ELEMENTS(pCache->aEntries), ("%#x/%d\n", uFmt, idxFmt), VERR_INVALID_PARAMETER);
    /* must be empty */
    AssertReturn(!shClCacheEntryIsValid(&pCache->aEntries[idxFmt]), VERR_ALREADY_EXISTS);

    pCache->aEntries[idxFmt].pvData = *ppvData = RTMemAllocZ(RT_MAX(cbData, 1));
    if (pCache->aEntries[idxFmt].pvData)
    {
        pCache->aEntries[idxFmt].cbData = cbData;
        return VINF_SUCCESS;
    }
    pCache->aEntries[idxFmt].cbData = 0;
    return VERR_NO_MEMORY;
}

/**
 * Sets data to cache for a specific clipboard format, internal version.
 *
 * @returns VBox status code.
 * @retval  VERR_ALREADY_EXISTS if the cache entry is not empty.
 * @param   pCache              Cache to set data for.
 * @param   uFmt                Clipboard format to set data for.
 * @param   pvData              Data to set.
 * @param   cbData              Size (in bytes) of data to set.
 */
DECLINLINE(int) shClCacheSet(PSHCLCACHE pCache, SHCLFORMAT uFmt, const void *pvData, size_t cbData)
{
    AssertPtr(pCache);
    int const idxFmt = ShClFormatToBitNo(uFmt);
    AssertMsgReturn((unsigned)idxFmt < RT_ELEMENTS(pCache->aEntries), ("%#x/%d\n", uFmt, idxFmt), VERR_INVALID_PARAMETER);

    /* must be empty */
    AssertReturn(!shClCacheEntryIsValid(&pCache->aEntries[idxFmt]), VERR_ALREADY_EXISTS);

    return shClCacheEntryInit(&pCache->aEntries[idxFmt], pvData, cbData);
}

/**
 * Sets data to cache for a specific clipboard format.
 *
 * @returns VBox status code.
 * @retval  VERR_ALREADY_EXISTS if the cache entry is not empty.
 * @param   pCache              Cache to set data for.
 * @param   uFmt                Clipboard format to set data for.
 * @param   pvData              Data to set.
 * @param   cbData              Size (in bytes) of data to set.
 */
VBGH_DECL(int) ShClCacheSet(PSHCLCACHE pCache, SHCLFORMAT uFmt, const void *pvData, size_t cbData)
{
    AssertPtrReturn(pCache, VERR_INVALID_POINTER);
    AssertMsgReturn(pCache->u32Magic == SHCLCACHE_MAGIC, ("%#x\n", pCache->u32Magic), VERR_INVALID_MAGIC);
    if (!pvData) /* Nothing to cache? */
        return VINF_SUCCESS;
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData, VERR_INVALID_PARAMETER);

    return shClCacheSet(pCache, uFmt, pvData, cbData);
}

/**
 * Sets data to cache for multiple clipboard formats.
 *
 * Will bail out if a given format cannot be handled with the data given.
 *
 * @returns VBox status code.
 * @param   pCache              Cache to set data for.
 * @param   uFmt                Clipboard format to set data for.
 * @param   pvData              Data to set.
 * @param   cbData              Size (in bytes) of data to set.
 */
VBGH_DECL(int) ShClCacheSetMultiple(PSHCLCACHE pCache, SHCLFORMATS uFmts, const void *pvData, size_t cbData)
{
    AssertPtrReturn(pCache, VERR_INVALID_POINTER);
    AssertMsgReturn(pCache->u32Magic == SHCLCACHE_MAGIC, ("%#x\n", pCache->u32Magic), VERR_INVALID_MAGIC);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData, VERR_INVALID_PARAMETER);
    AssertReturn(uFmts != VBOX_SHCL_FMT_NONE, VERR_INVALID_PARAMETER);
    AssertReturn(ShClFormatsAreValid(uFmts), VERR_INVALID_FLAGS);

    int rc = VINF_SUCCESS;
    SHCLFORMATS uFmtsLeft = uFmts;
    while (uFmtsLeft)
    {
        SHCLFORMAT  uFmt;
        if (uFmtsLeft & VBOX_SHCL_FMT_UNICODETEXT)
        {
            uFmt = VBOX_SHCL_FMT_UNICODETEXT;
            if (cbData & (sizeof(RTUTF16) - 1))
                rc = VERR_INVALID_PARAMETER;
            else
                rc = RTUtf16ValidateEncodingEx((PCRTUTF16)pvData, cbData / sizeof(RTUTF16),
                                                RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED
                                              | RTSTR_VALIDATE_ENCODING_EXACT_LENGTH);
        }
        else if (uFmtsLeft & VBOX_SHCL_FMT_BITMAP)
        {
            uFmt = VBOX_SHCL_FMT_BITMAP;
            void  *pvBmp = NULL;
            size_t cbBmp = 0;
            rc = ShClHlpDibToBmp(pvData, cbData, &pvBmp, &cbBmp);
            ShClHlpFreeBuf(pvBmp, cbBmp);
        }
        else if (uFmtsLeft & VBOX_SHCL_FMT_HTML)
        {
            uFmt = VBOX_SHCL_FMT_HTML;
            uint32_t fFlags = RTSTR_VALIDATE_ENCODING_EXACT_LENGTH;
            if (((const char *)pvData)[cbData - 1] == '\0')
                fFlags |= RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED;
            rc = RTStrValidateEncodingEx((const char *)pvData, cbData, fFlags);
        }
        else if (uFmtsLeft & VBOX_SHCL_FMT_URI_LIST)
        {
            uFmt = VBOX_SHCL_FMT_URI_LIST;
            uint32_t fFlags = RTSTR_VALIDATE_ENCODING_EXACT_LENGTH;
            if (((const char *)pvData)[cbData - 1] == '\0')
                fFlags |= RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED;
            rc = RTStrValidateEncodingEx((const char *)pvData, cbData, fFlags);
        }
        else
            AssertFailedBreakStmt(rc = VERR_NOT_SUPPORTED);

        uFmtsLeft &= ~uFmt; /* Remove from list. */

        if (RT_SUCCESS(rc))
            rc = shClCacheSet(pCache, uFmt, pvData, cbData);
        AssertRCBreak(rc);
    }

    return rc;
}

/**
 * Compares the content of two caches.
 *
 * @returns true if they have the same content, false if not.
 * @param   pCache              The first cache.
 * @param   pOtherCache         The second cache.
 */
VBGH_DECL(bool) ShClCacheEquals(SHCLCACHE const *pCache, SHCLCACHE const *pOtherCache)
{
    AssertPtrReturn(pCache, false);
    AssertMsgReturn(pCache->u32Magic == SHCLCACHE_MAGIC, ("%#x\n", pCache->u32Magic), false);
    AssertPtrReturn(pOtherCache, false);
    AssertMsgReturn(pOtherCache->u32Magic == SHCLCACHE_MAGIC, ("%#x\n", pOtherCache->u32Magic), false);

    for (unsigned i = 0; i < RT_ELEMENTS(pCache->aEntries); i++)
        if (pCache->aEntries[i].cbData != pOtherCache->aEntries[i].cbData)
            return false;

    for (unsigned i = 0; i < RT_ELEMENTS(pCache->aEntries); i++)
        if (   pCache->aEntries[i].cbData > 0
            && memcmp(pCache->aEntries[i].pvData, pOtherCache->aEntries[i].pvData, pCache->aEntries[i].cbData) != 0)
            return false;

    return true;
}


/**
 * Transfers the content of @a pOtherCache into @a pCache, resetting the former.
 *
 * The buffers are simply moved from one cache to the other, no new allocations
 * or data copying takes place here.
 *
 * @returns VBox status code.
 * @param   pCache              The destination cache.
 * @param   pOtherCache         The source cache.
 */
VBGH_DECL(int) ShClCacheTransferAll(PSHCLCACHE pCache, PSHCLCACHE pOtherCache)
{
    AssertPtrReturn(pCache, VERR_INVALID_POINTER);
    AssertMsgReturn(pCache->u32Magic == SHCLCACHE_MAGIC, ("%#x\n", pCache->u32Magic), VERR_INVALID_MAGIC);
    AssertPtrReturn(pOtherCache, VERR_INVALID_POINTER);
    AssertMsgReturn(pOtherCache->u32Magic == SHCLCACHE_MAGIC, ("%#x\n", pOtherCache->u32Magic), VERR_INVALID_MAGIC);

    for (unsigned i = 0; i < RT_ELEMENTS(pCache->aEntries); i++)
    {
        shClCacheEntryReInit(&pCache->aEntries[i]);
        pCache->aEntries[i].cbData = pOtherCache->aEntries[i].cbData;
        pCache->aEntries[i].pvData = pOtherCache->aEntries[i].pvData;
        pOtherCache->aEntries[i].cbData = 0;
        pOtherCache->aEntries[i].pvData = NULL;
    }
    return VINF_SUCCESS;
}

