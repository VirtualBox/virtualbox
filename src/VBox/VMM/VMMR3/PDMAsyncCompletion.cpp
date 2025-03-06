/* $Id: PDMAsyncCompletion.cpp 108651 2025-03-06 11:15:45Z alexander.eichner@oracle.com $ */
/** @file
 * PDM Async I/O - Transport data asynchronous in R3 using EMT.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_PDM_ASYNC_COMPLETION
#include "PDMInternal.h"
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/thread.h>
#include <iprt/mem.h>
#include <iprt/critsect.h>
#include <iprt/tcp.h>
#include <iprt/path.h>
#include <iprt/string.h>

#include <VBox/vmm/pdmasynccompletion.h>
#include "PDMAsyncCompletionInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Async I/O type.
 */
typedef enum PDMASYNCCOMPLETIONTEMPLATETYPE
{
    /** Device . */
    PDMASYNCCOMPLETIONTEMPLATETYPE_DEV = 1,
    /** Driver consumer. */
    PDMASYNCCOMPLETIONTEMPLATETYPE_DRV,
    /** Internal consumer. */
    PDMASYNCCOMPLETIONTEMPLATETYPE_INTERNAL,
    /** Usb consumer. */
    PDMASYNCCOMPLETIONTEMPLATETYPE_USB
} PDMASYNCTEMPLATETYPE;

/**
 * PDM Async I/O template.
 */
typedef struct PDMASYNCCOMPLETIONTEMPLATE
{
    /** Pointer to the next template in the list. */
    R3PTRTYPE(PPDMASYNCCOMPLETIONTEMPLATE)    pNext;
    /** Pointer to the previous template in the list. */
    R3PTRTYPE(PPDMASYNCCOMPLETIONTEMPLATE)    pPrev;
    /** Type specific data. */
    union
    {
        /** PDMASYNCCOMPLETIONTEMPLATETYPE_DEV */
        struct
        {
            /** Pointer to consumer function. */
            R3PTRTYPE(PFNPDMASYNCCOMPLETEDEV)   pfnCompleted;
            /** Pointer to the device instance owning the template. */
            R3PTRTYPE(PPDMDEVINS)               pDevIns;
        } Dev;
        /** PDMASYNCCOMPLETIONTEMPLATETYPE_DRV */
        struct
        {
            /** Pointer to consumer function. */
            R3PTRTYPE(PFNPDMASYNCCOMPLETEDRV)   pfnCompleted;
            /** Pointer to the driver instance owning the template. */
            R3PTRTYPE(PPDMDRVINS)               pDrvIns;
            /** User argument given during template creation.
             *  This is only here to make things much easier
             *  for DrVVD. */
            void                               *pvTemplateUser;
        } Drv;
        /** PDMASYNCCOMPLETIONTEMPLATETYPE_INTERNAL */
        struct
        {
            /** Pointer to consumer function. */
            R3PTRTYPE(PFNPDMASYNCCOMPLETEINT)   pfnCompleted;
            /** Pointer to user data. */
            R3PTRTYPE(void *)                   pvUser;
        } Int;
        /** PDMASYNCCOMPLETIONTEMPLATETYPE_USB */
        struct
        {
            /** Pointer to consumer function. */
            R3PTRTYPE(PFNPDMASYNCCOMPLETEUSB)   pfnCompleted;
            /** Pointer to the usb instance owning the template. */
            R3PTRTYPE(PPDMUSBINS)               pUsbIns;
        } Usb;
    } u;
    /** Template type. */
    PDMASYNCCOMPLETIONTEMPLATETYPE          enmType;
    /** Pointer to the VM. */
    R3PTRTYPE(PVM)                          pVM;
    /** Use count of the template. */
    volatile uint32_t                       cUsed;
} PDMASYNCCOMPLETIONTEMPLATE;

/**
 * Bandwidth control manager instance data
 */
typedef struct PDMACBWMGR
{
    /** Pointer to the next manager in the list. */
    struct PDMACBWMGR                          *pNext;
    /** Pointer to the shared UVM structure. */
    PPDMASYNCCOMPLETIONEPCLASS                  pEpClass;
    /** Identifier of the manager. */
    char                                       *pszId;
    /** Maximum number of bytes the endpoints are allowed to transfer (Max is 4GB/s currently) */
    volatile uint64_t                           cbTransferPerSecMax;
    /** Number of bytes we start with */
    volatile uint64_t                           cbTransferPerSecStart;
    /** Step after each update */
    volatile uint64_t                           cbTransferPerSecStep;
    /** Number of bytes we are allowed to transfer till the next update.
     * Reset by the refresh timer. */
    volatile uint64_t                           cbTransferAllowed;
    /** Timestamp of the last update */
    volatile uint64_t                           tsUpdatedLast;
    /** Reference counter - How many endpoints are associated with this manager. */
    volatile uint32_t                           cRefs;
} PDMACBWMGR;
/** Pointer to a bandwidth control manager pointer. */
typedef PPDMACBWMGR *PPPDMACBWMGR;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void pdmR3AsyncCompletionPutTask(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, PPDMASYNCCOMPLETIONTASK pTask);


/**
 * Internal worker for the creation apis
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   ppTemplate      Where to store the template handle.
 * @param   enmType         Async completion template type (dev, drv, usb, int).
 */
static int pdmR3AsyncCompletionTemplateCreate(PVM pVM, PPPDMASYNCCOMPLETIONTEMPLATE ppTemplate,
                                              PDMASYNCCOMPLETIONTEMPLATETYPE enmType)
{
    PUVM pUVM = pVM->pUVM;

    AssertPtrReturn(ppTemplate, VERR_INVALID_POINTER);

    PPDMASYNCCOMPLETIONTEMPLATE pTemplate;
    int rc = MMR3HeapAllocZEx(pVM, MM_TAG_PDM_ASYNC_COMPLETION, sizeof(PDMASYNCCOMPLETIONTEMPLATE), (void **)&pTemplate);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Initialize fields.
     */
    pTemplate->pVM = pVM;
    pTemplate->cUsed = 0;
    pTemplate->enmType = enmType;

    /*
     * Add template to the global VM template list.
     */
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    pTemplate->pNext = pUVM->pdm.s.pAsyncCompletionTemplates;
    if (pUVM->pdm.s.pAsyncCompletionTemplates)
        pUVM->pdm.s.pAsyncCompletionTemplates->pPrev = pTemplate;
    pUVM->pdm.s.pAsyncCompletionTemplates = pTemplate;
    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);

    *ppTemplate = pTemplate;
    return VINF_SUCCESS;
}


#ifdef SOME_UNUSED_FUNCTION
/**
 * Creates a async completion template for a device instance.
 *
 * The template is used when creating new completion tasks.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pDevIns         The device instance.
 * @param   ppTemplate      Where to store the template pointer on success.
 * @param   pfnCompleted    The completion callback routine.
 * @param   pszDesc         Description.
 */
int pdmR3AsyncCompletionTemplateCreateDevice(PVM pVM, PPDMDEVINS pDevIns, PPPDMASYNCCOMPLETIONTEMPLATE ppTemplate,
                                             PFNPDMASYNCCOMPLETEDEV pfnCompleted, const char *pszDesc)
{
    LogFlow(("%s: pDevIns=%p ppTemplate=%p pfnCompleted=%p pszDesc=%s\n",
              __FUNCTION__, pDevIns, ppTemplate, pfnCompleted, pszDesc));

    /*
     * Validate input.
     */
    VM_ASSERT_EMT(pVM);
    AssertPtrReturn(pfnCompleted, VERR_INVALID_POINTER);
    AssertPtrReturn(ppTemplate, VERR_INVALID_POINTER);

    /*
     * Create the template.
     */
    PPDMASYNCCOMPLETIONTEMPLATE pTemplate;
    int rc = pdmR3AsyncCompletionTemplateCreate(pVM, &pTemplate, PDMASYNCCOMPLETIONTEMPLATETYPE_DEV);
    if (RT_SUCCESS(rc))
    {
        pTemplate->u.Dev.pDevIns = pDevIns;
        pTemplate->u.Dev.pfnCompleted = pfnCompleted;

        *ppTemplate = pTemplate;
        Log(("PDM: Created device template %p: pfnCompleted=%p pDevIns=%p\n",
             pTemplate, pfnCompleted, pDevIns));
    }

    return rc;
}
#endif /* SOME_UNUSED_FUNCTION */


/**
 * Creates a async completion template for a driver instance.
 *
 * The template is used when creating new completion tasks.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pDrvIns         The driver instance.
 * @param   ppTemplate      Where to store the template pointer on success.
 * @param   pfnCompleted    The completion callback routine.
 * @param   pvTemplateUser  Template user argument
 * @param   pszDesc         Description.
 */
int pdmR3AsyncCompletionTemplateCreateDriver(PVM pVM, PPDMDRVINS pDrvIns, PPPDMASYNCCOMPLETIONTEMPLATE ppTemplate,
                                             PFNPDMASYNCCOMPLETEDRV pfnCompleted, void *pvTemplateUser,
                                             const char *pszDesc)
{
    LogFlow(("PDMR3AsyncCompletionTemplateCreateDriver: pDrvIns=%p ppTemplate=%p pfnCompleted=%p pszDesc=%s\n",
             pDrvIns, ppTemplate, pfnCompleted, pszDesc));
    RT_NOREF_PV(pszDesc); /** @todo async template description */

    /*
     * Validate input.
     */
    AssertPtrReturn(pfnCompleted, VERR_INVALID_POINTER);
    AssertPtrReturn(ppTemplate, VERR_INVALID_POINTER);

    /*
     * Create the template.
     */
    PPDMASYNCCOMPLETIONTEMPLATE pTemplate;
    int rc = pdmR3AsyncCompletionTemplateCreate(pVM, &pTemplate, PDMASYNCCOMPLETIONTEMPLATETYPE_DRV);
    if (RT_SUCCESS(rc))
    {
        pTemplate->u.Drv.pDrvIns        = pDrvIns;
        pTemplate->u.Drv.pfnCompleted   = pfnCompleted;
        pTemplate->u.Drv.pvTemplateUser = pvTemplateUser;

        *ppTemplate = pTemplate;
        Log(("PDM: Created driver template %p: pfnCompleted=%p pDrvIns=%p\n",
             pTemplate, pfnCompleted, pDrvIns));
    }

    return rc;
}


#ifdef SOME_UNUSED_FUNCTION
/**
 * Creates a async completion template for a USB device instance.
 *
 * The template is used when creating new completion tasks.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pUsbIns         The USB device instance.
 * @param   ppTemplate      Where to store the template pointer on success.
 * @param   pfnCompleted    The completion callback routine.
 * @param   pszDesc         Description.
 */
int pdmR3AsyncCompletionTemplateCreateUsb(PVM pVM, PPDMUSBINS pUsbIns, PPPDMASYNCCOMPLETIONTEMPLATE ppTemplate,
                                          PFNPDMASYNCCOMPLETEUSB pfnCompleted, const char *pszDesc)
{
    LogFlow(("pdmR3AsyncCompletionTemplateCreateUsb: pUsbIns=%p ppTemplate=%p pfnCompleted=%p pszDesc=%s\n", pUsbIns, ppTemplate, pfnCompleted, pszDesc));

    /*
     * Validate input.
     */
    VM_ASSERT_EMT(pVM);
    AssertPtrReturn(pfnCompleted, VERR_INVALID_POINTER);
    AssertPtrReturn(ppTemplate, VERR_INVALID_POINTER);

    /*
     * Create the template.
     */
    PPDMASYNCCOMPLETIONTEMPLATE pTemplate;
    int rc = pdmR3AsyncCompletionTemplateCreate(pVM, &pTemplate, PDMASYNCCOMPLETIONTEMPLATETYPE_USB);
    if (RT_SUCCESS(rc))
    {
        pTemplate->u.Usb.pUsbIns = pUsbIns;
        pTemplate->u.Usb.pfnCompleted = pfnCompleted;

        *ppTemplate = pTemplate;
        Log(("PDM: Created usb template %p: pfnCompleted=%p pDevIns=%p\n",
             pTemplate, pfnCompleted, pUsbIns));
    }

    return rc;
}
#endif


/**
 * Creates a async completion template for internally by the VMM.
 *
 * The template is used when creating new completion tasks.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   ppTemplate      Where to store the template pointer on success.
 * @param   pfnCompleted    The completion callback routine.
 * @param   pvUser2         The 2nd user argument for the callback.
 * @param   pszDesc         Description.
 * @internal
 */
VMMR3DECL(int) PDMR3AsyncCompletionTemplateCreateInternal(PVM pVM, PPPDMASYNCCOMPLETIONTEMPLATE ppTemplate,
                                                          PFNPDMASYNCCOMPLETEINT pfnCompleted, void *pvUser2, const char *pszDesc)
{
    LogFlow(("PDMR3AsyncCompletionTemplateCreateInternal: ppTemplate=%p pfnCompleted=%p pvUser2=%p pszDesc=%s\n",
              ppTemplate, pfnCompleted, pvUser2, pszDesc));
    RT_NOREF_PV(pszDesc); /** @todo async template description */


    /*
     * Validate input.
     */
    VM_ASSERT_EMT(pVM);
    AssertPtrReturn(pfnCompleted, VERR_INVALID_POINTER);
    AssertPtrReturn(ppTemplate, VERR_INVALID_POINTER);

    /*
     * Create the template.
     */
    PPDMASYNCCOMPLETIONTEMPLATE pTemplate;
    int rc = pdmR3AsyncCompletionTemplateCreate(pVM, &pTemplate, PDMASYNCCOMPLETIONTEMPLATETYPE_INTERNAL);
    if (RT_SUCCESS(rc))
    {
        pTemplate->u.Int.pvUser = pvUser2;
        pTemplate->u.Int.pfnCompleted = pfnCompleted;

        *ppTemplate = pTemplate;
        Log(("PDM: Created internal template %p: pfnCompleted=%p pvUser2=%p\n",
             pTemplate, pfnCompleted, pvUser2));
    }

    return rc;
}


/**
 * Destroys the specified async completion template.
 *
 * @returns VBox status codes:
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PDM_ASYNC_TEMPLATE_BUSY if the template is still in use.
 *
 * @param   pTemplate       The template in question.
 */
VMMR3DECL(int) PDMR3AsyncCompletionTemplateDestroy(PPDMASYNCCOMPLETIONTEMPLATE pTemplate)
{
    LogFlow(("%s: pTemplate=%p\n", __FUNCTION__, pTemplate));

    if (!pTemplate)
    {
        AssertMsgFailed(("pTemplate is NULL!\n"));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Check if the template is still used.
     */
    if (pTemplate->cUsed > 0)
    {
        AssertMsgFailed(("Template is still in use\n"));
        return VERR_PDM_ASYNC_TEMPLATE_BUSY;
    }

    /*
     * Unlink the template from the list.
     */
    PUVM pUVM = pTemplate->pVM->pUVM;
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);

    PPDMASYNCCOMPLETIONTEMPLATE pPrev = pTemplate->pPrev;
    PPDMASYNCCOMPLETIONTEMPLATE pNext = pTemplate->pNext;

    if (pPrev)
        pPrev->pNext = pNext;
    else
        pUVM->pdm.s.pAsyncCompletionTemplates = pNext;

    if (pNext)
        pNext->pPrev = pPrev;

    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);

    /*
     * Free the template.
     */
    MMR3HeapFree(pTemplate);

    return VINF_SUCCESS;
}


/**
 * Destroys all the specified async completion templates for the given device instance.
 *
 * @returns VBox status codes:
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PDM_ASYNC_TEMPLATE_BUSY if one or more of the templates are still in use.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pDevIns         The device instance.
 */
int pdmR3AsyncCompletionTemplateDestroyDevice(PVM pVM, PPDMDEVINS pDevIns)
{
    LogFlow(("pdmR3AsyncCompletionTemplateDestroyDevice: pDevIns=%p\n", pDevIns));

    /*
     * Validate input.
     */
    if (!pDevIns)
        return VERR_INVALID_PARAMETER;
    VM_ASSERT_EMT(pVM);

    /*
     * Unlink it.
     */
    PUVM pUVM = pVM->pUVM;
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    PPDMASYNCCOMPLETIONTEMPLATE pTemplate = pUVM->pdm.s.pAsyncCompletionTemplates;
    while (pTemplate)
    {
        if (    pTemplate->enmType == PDMASYNCCOMPLETIONTEMPLATETYPE_DEV
            &&  pTemplate->u.Dev.pDevIns == pDevIns)
        {
            PPDMASYNCCOMPLETIONTEMPLATE pTemplateDestroy = pTemplate;
            pTemplate = pTemplate->pNext;
            int rc = PDMR3AsyncCompletionTemplateDestroy(pTemplateDestroy);
            if (RT_FAILURE(rc))
            {
                RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
                return rc;
            }
        }
        else
            pTemplate = pTemplate->pNext;
    }

    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    return VINF_SUCCESS;
}


/**
 * Destroys all the specified async completion templates for the given driver instance.
 *
 * @returns VBox status codes:
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PDM_ASYNC_TEMPLATE_BUSY if one or more of the templates are still in use.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pDrvIns         The driver instance.
 */
int pdmR3AsyncCompletionTemplateDestroyDriver(PVM pVM, PPDMDRVINS pDrvIns)
{
    LogFlow(("pdmR3AsyncCompletionTemplateDestroyDriver: pDevIns=%p\n", pDrvIns));

    /*
     * Validate input.
     */
    if (!pDrvIns)
        return VERR_INVALID_PARAMETER;
    VM_ASSERT_EMT(pVM);

    /*
     * Unlink it.
     */
    PUVM pUVM = pVM->pUVM;
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    PPDMASYNCCOMPLETIONTEMPLATE pTemplate = pUVM->pdm.s.pAsyncCompletionTemplates;
    while (pTemplate)
    {
        if (    pTemplate->enmType == PDMASYNCCOMPLETIONTEMPLATETYPE_DRV
            &&  pTemplate->u.Drv.pDrvIns == pDrvIns)
        {
            PPDMASYNCCOMPLETIONTEMPLATE pTemplateDestroy = pTemplate;
            pTemplate = pTemplate->pNext;
            int rc = PDMR3AsyncCompletionTemplateDestroy(pTemplateDestroy);
            if (RT_FAILURE(rc))
            {
                RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
                return rc;
            }
        }
        else
            pTemplate = pTemplate->pNext;
    }

    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    return VINF_SUCCESS;
}


/**
 * Destroys all the specified async completion templates for the given USB device instance.
 *
 * @returns VBox status codes:
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PDM_ASYNC_TEMPLATE_BUSY if one or more of the templates are still in use.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pUsbIns         The USB device instance.
 */
int pdmR3AsyncCompletionTemplateDestroyUsb(PVM pVM, PPDMUSBINS pUsbIns)
{
    LogFlow(("pdmR3AsyncCompletionTemplateDestroyUsb: pUsbIns=%p\n", pUsbIns));

    /*
     * Validate input.
     */
    if (!pUsbIns)
        return VERR_INVALID_PARAMETER;
    VM_ASSERT_EMT(pVM);

    /*
     * Unlink it.
     */
    PUVM pUVM = pVM->pUVM;
    RTCritSectEnter(&pUVM->pdm.s.ListCritSect);
    PPDMASYNCCOMPLETIONTEMPLATE pTemplate = pUVM->pdm.s.pAsyncCompletionTemplates;
    while (pTemplate)
    {
        if (    pTemplate->enmType == PDMASYNCCOMPLETIONTEMPLATETYPE_USB
            &&  pTemplate->u.Usb.pUsbIns == pUsbIns)
        {
            PPDMASYNCCOMPLETIONTEMPLATE pTemplateDestroy = pTemplate;
            pTemplate = pTemplate->pNext;
            int rc = PDMR3AsyncCompletionTemplateDestroy(pTemplateDestroy);
            if (RT_FAILURE(rc))
            {
                RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
                return rc;
            }
        }
        else
            pTemplate = pTemplate->pNext;
    }

    RTCritSectLeave(&pUVM->pdm.s.ListCritSect);
    return VINF_SUCCESS;
}


/** Lazy coder. */
static PPDMACBWMGR pdmacBwMgrFindById(PPDMASYNCCOMPLETIONEPCLASS pEpClass, const char *pszId)
{
    PPDMACBWMGR pBwMgr = NULL;

    if (pszId)
    {
        int rc = RTCritSectEnter(&pEpClass->CritSect); AssertRC(rc);

        pBwMgr = pEpClass->pBwMgrsHead;
        while (   pBwMgr
               && RTStrCmp(pBwMgr->pszId, pszId))
            pBwMgr = pBwMgr->pNext;

        rc = RTCritSectLeave(&pEpClass->CritSect); AssertRC(rc);
    }

    return pBwMgr;
}


/** Lazy coder. */
static void pdmacBwMgrLink(PPDMACBWMGR pBwMgr)
{
    PPDMASYNCCOMPLETIONEPCLASS pEpClass = pBwMgr->pEpClass;
    int rc = RTCritSectEnter(&pEpClass->CritSect); AssertRC(rc);

    pBwMgr->pNext = pEpClass->pBwMgrsHead;
    pEpClass->pBwMgrsHead = pBwMgr;

    rc = RTCritSectLeave(&pEpClass->CritSect); AssertRC(rc);
}


#ifdef SOME_UNUSED_FUNCTION
/** Lazy coder. */
static void pdmacBwMgrUnlink(PPDMACBWMGR pBwMgr)
{
    PPDMASYNCCOMPLETIONEPCLASS pEpClass = pBwMgr->pEpClass;
    int rc = RTCritSectEnter(&pEpClass->CritSect); AssertRC(rc);

    if (pBwMgr == pEpClass->pBwMgrsHead)
        pEpClass->pBwMgrsHead = pBwMgr->pNext;
    else
    {
        PPDMACBWMGR pPrev = pEpClass->pBwMgrsHead;
        while (   pPrev
               && pPrev->pNext != pBwMgr)
            pPrev = pPrev->pNext;

        AssertPtr(pPrev);
        pPrev->pNext = pBwMgr->pNext;
    }

    rc = RTCritSectLeave(&pEpClass->CritSect); AssertRC(rc);
}
#endif /* SOME_UNUSED_FUNCTION */


/** Lazy coder. */
static int pdmacAsyncCompletionBwMgrCreate(PPDMASYNCCOMPLETIONEPCLASS pEpClass, const char *pszBwMgr, uint64_t cbTransferPerSecMax,
                                           uint64_t cbTransferPerSecStart, uint64_t cbTransferPerSecStep)
{
    LogFlowFunc(("pEpClass=%#p pszBwMgr=%#p{%s} cbTransferPerSecMax=%RU64 cbTransferPerSecStart=%RU64 cbTransferPerSecStep=%RU64\n",
                 pEpClass, pszBwMgr, pszBwMgr, cbTransferPerSecMax, cbTransferPerSecStart, cbTransferPerSecStep));

    AssertPtrReturn(pEpClass, VERR_INVALID_POINTER);
    AssertPtrReturn(pszBwMgr, VERR_INVALID_POINTER);
    AssertReturn(*pszBwMgr != '\0', VERR_INVALID_PARAMETER);

    int         rc;
    PPDMACBWMGR pBwMgr = pdmacBwMgrFindById(pEpClass, pszBwMgr);
    if (!pBwMgr)
    {
        rc = MMR3HeapAllocZEx(pEpClass->pVM, MM_TAG_PDM_ASYNC_COMPLETION,
                              sizeof(PDMACBWMGR),
                              (void **)&pBwMgr);
        if (RT_SUCCESS(rc))
        {
            pBwMgr->pszId = RTStrDup(pszBwMgr);
            if (pBwMgr->pszId)
            {
                pBwMgr->pEpClass              = pEpClass;
                pBwMgr->cRefs                 = 0;

                /* Init I/O flow control. */
                pBwMgr->cbTransferPerSecMax   = cbTransferPerSecMax;
                pBwMgr->cbTransferPerSecStart = cbTransferPerSecStart;
                pBwMgr->cbTransferPerSecStep  = cbTransferPerSecStep;

                pBwMgr->cbTransferAllowed     = pBwMgr->cbTransferPerSecStart;
                pBwMgr->tsUpdatedLast         = RTTimeSystemNanoTS();

                pdmacBwMgrLink(pBwMgr);
                rc = VINF_SUCCESS;
            }
            else
            {
                rc = VERR_NO_MEMORY;
                MMR3HeapFree(pBwMgr);
            }
        }
    }
    else
        rc = VERR_ALREADY_EXISTS;

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}


/** Lazy coder. */
DECLINLINE(void) pdmacBwMgrRetain(PPDMACBWMGR pBwMgr)
{
    ASMAtomicIncU32(&pBwMgr->cRefs);
}


/** Lazy coder. */
DECLINLINE(void) pdmacBwMgrRelease(PPDMACBWMGR pBwMgr)
{
    Assert(pBwMgr->cRefs > 0);
    ASMAtomicDecU32(&pBwMgr->cRefs);
}


/**
 * Checks if the endpoint is allowed to transfer the given amount of bytes.
 *
 * @returns true if the endpoint is allowed to transfer the data.
 *          false otherwise
 * @param   pEndpoint                 The endpoint.
 * @param   cbTransfer                The number of bytes to transfer.
 * @param   pmsWhenNext               Where to store the number of milliseconds
 *                                    until the bandwidth is refreshed.
 *                                    Only set if false is returned.
 */
bool pdmacEpIsTransferAllowed(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, uint64_t cbTransfer, RTMSINTERVAL *pmsWhenNext)
{
    bool        fAllowed = true;
    PPDMACBWMGR pBwMgr = ASMAtomicReadPtrT(&pEndpoint->pBwMgr, PPDMACBWMGR);

    LogFlowFunc(("pEndpoint=%p pBwMgr=%p cbTransfer=%RU64\n", pEndpoint, pBwMgr, cbTransfer));

    if (pBwMgr)
    {
        uint64_t cbOld = ASMAtomicSubU64(&pBwMgr->cbTransferAllowed, cbTransfer);
        if (RT_LIKELY(cbOld >= cbTransfer))
            fAllowed = true;
        else
        {
            fAllowed = false;

            /* We are out of resources  Check if we can update again. */
            uint64_t tsNow          = RTTimeSystemNanoTS();
            uint64_t tsUpdatedLast  = ASMAtomicUoReadU64(&pBwMgr->tsUpdatedLast);

            if (tsNow - tsUpdatedLast >= (1000*1000*1000))
            {
                if (ASMAtomicCmpXchgU64(&pBwMgr->tsUpdatedLast, tsNow, tsUpdatedLast))
                {
                    if (pBwMgr->cbTransferPerSecStart < pBwMgr->cbTransferPerSecMax)
                    {
                       pBwMgr->cbTransferPerSecStart = RT_MIN(pBwMgr->cbTransferPerSecMax, pBwMgr->cbTransferPerSecStart + pBwMgr->cbTransferPerSecStep);
                       LogFlow(("AIOMgr: Increasing maximum bandwidth to %RU64 bytes/sec\n", pBwMgr->cbTransferPerSecStart));
                    }

                    /* Update */
                    uint64_t cbTransferAllowedNew =   pBwMgr->cbTransferPerSecStart > cbTransfer
                                                    ? pBwMgr->cbTransferPerSecStart - cbTransfer
                                                    : 0;
                    ASMAtomicWriteU64(&pBwMgr->cbTransferAllowed, cbTransferAllowedNew);
                    fAllowed = true;
                    LogFlow(("AIOMgr: Refreshed bandwidth\n"));
                }
            }
            else
            {
                ASMAtomicAddU64(&pBwMgr->cbTransferAllowed, cbTransfer);
                *pmsWhenNext = ((1000*1000*1000) - (tsNow - tsUpdatedLast)) / (1000*1000);
            }
        }
    }

    LogFlowFunc(("fAllowed=%RTbool\n", fAllowed));
    return fAllowed;
}


/**
 * Called by the endpoint if a task has finished.
 *
 * @param   pTask                     Pointer to the finished task.
 * @param   rc                        Status code of the completed request.
 * @param   fCallCompletionHandler    Flag whether the completion handler should be called to
 *                                    inform the owner of the task that it has completed.
 */
void pdmR3AsyncCompletionCompleteTask(PPDMASYNCCOMPLETIONTASK pTask, int rc, bool fCallCompletionHandler)
{
    LogFlow(("%s: pTask=%#p fCallCompletionHandler=%RTbool\n", __FUNCTION__, pTask, fCallCompletionHandler));

    if (fCallCompletionHandler)
    {
        PPDMASYNCCOMPLETIONTEMPLATE pTemplate = pTask->pEndpoint->pTemplate;

        switch (pTemplate->enmType)
        {
            case PDMASYNCCOMPLETIONTEMPLATETYPE_DEV:
                pTemplate->u.Dev.pfnCompleted(pTemplate->u.Dev.pDevIns, pTask->pvUser, rc);
                break;

            case PDMASYNCCOMPLETIONTEMPLATETYPE_DRV:
                pTemplate->u.Drv.pfnCompleted(pTemplate->u.Drv.pDrvIns, pTemplate->u.Drv.pvTemplateUser, pTask->pvUser, rc);
                break;

            case PDMASYNCCOMPLETIONTEMPLATETYPE_USB:
                pTemplate->u.Usb.pfnCompleted(pTemplate->u.Usb.pUsbIns, pTask->pvUser, rc);
                break;

            case PDMASYNCCOMPLETIONTEMPLATETYPE_INTERNAL:
                pTemplate->u.Int.pfnCompleted(pTemplate->pVM, pTask->pvUser, pTemplate->u.Int.pvUser, rc);
                break;

            default:
                AssertMsgFailed(("Unknown template type!\n"));
        }
    }

    pdmR3AsyncCompletionPutTask(pTask->pEndpoint, pTask);
}


/**
 * Worker initializing a endpoint class.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pEpClassOps Pointer to the endpoint class structure.
 * @param   pCfgHandle  Pointer to the CFGM tree.
 */
static int pdmR3AsyncCompletionEpClassInit(PVM pVM, PCPDMASYNCCOMPLETIONEPCLASSOPS pEpClassOps, PCFGMNODE pCfgHandle)
{
    /* Validate input. */
    AssertPtrReturn(pEpClassOps, VERR_INVALID_POINTER);
    AssertReturn(pEpClassOps->u32Version == PDMAC_EPCLASS_OPS_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pEpClassOps->u32VersionEnd == PDMAC_EPCLASS_OPS_VERSION, VERR_VERSION_MISMATCH);

    LogFlow(("pdmR3AsyncCompletionEpClassInit: pVM=%p pEpClassOps=%p{%s}\n", pVM, pEpClassOps, pEpClassOps->pszName));

    /* Allocate global class data. */
    PPDMASYNCCOMPLETIONEPCLASS pEndpointClass = NULL;

    int rc = MMR3HeapAllocZEx(pVM, MM_TAG_PDM_ASYNC_COMPLETION,
                              pEpClassOps->cbEndpointClassGlobal,
                              (void **)&pEndpointClass);
    if (RT_SUCCESS(rc))
    {
        /* Initialize common data. */
        pEndpointClass->pVM          = pVM;
        pEndpointClass->pEndpointOps = pEpClassOps;

        rc = RTCritSectInit(&pEndpointClass->CritSect);
        if (RT_SUCCESS(rc))
        {
            PCFGMNODE pCfgNodeClass = CFGMR3GetChild(pCfgHandle, pEpClassOps->pszName);

            /* Create task cache */
            rc = RTMemCacheCreate(&pEndpointClass->hMemCacheTasks, pEpClassOps->cbTask,
                                  0, UINT32_MAX, NULL, NULL, NULL, 0);
            if (RT_SUCCESS(rc))
            {
                /* Call the specific endpoint class initializer. */
                rc = pEpClassOps->pfnInitialize(pEndpointClass, pCfgNodeClass);
                if (RT_SUCCESS(rc))
                {
                    /* Create all bandwidth groups for resource control. */
                    PCFGMNODE pCfgBwGrp = CFGMR3GetChild(pCfgNodeClass, "BwGroups");
                    if (pCfgBwGrp)
                    {
                        for (PCFGMNODE pCur = CFGMR3GetFirstChild(pCfgBwGrp); pCur; pCur = CFGMR3GetNextChild(pCur))
                        {
                            size_t cbName = CFGMR3GetNameLen(pCur) + 1;
                            char *pszBwGrpId = (char *)RTMemAllocZ(cbName);
                            if (pszBwGrpId)
                            {
                                rc = CFGMR3GetName(pCur, pszBwGrpId, cbName);
                                if (RT_SUCCESS(rc))
                                {
                                    uint64_t cbMax;
                                    rc = CFGMR3QueryU64(pCur, "Max", &cbMax);
                                    if (RT_SUCCESS(rc))
                                    {
                                        uint64_t cbStart;
                                        rc = CFGMR3QueryU64Def(pCur, "Start", &cbStart, cbMax);
                                        if (RT_SUCCESS(rc))
                                        {
                                            uint64_t cbStep;
                                            rc = CFGMR3QueryU64Def(pCur, "Step", &cbStep, 0);
                                            if (RT_SUCCESS(rc))
                                                rc = pdmacAsyncCompletionBwMgrCreate(pEndpointClass, pszBwGrpId,
                                                                                     cbMax, cbStart, cbStep);
                                        }
                                    }
                                }
                                RTMemFree(pszBwGrpId);
                            }
                            else
                                rc = VERR_NO_MEMORY;
                            if (RT_FAILURE(rc))
                                break;
                        }
                    }
                    if (RT_SUCCESS(rc))
                    {
                        PUVM pUVM = pVM->pUVM;
                        AssertMsg(!pUVM->pdm.s.apAsyncCompletionEndpointClass[pEpClassOps->enmClassType],
                                  ("Endpoint class was already initialized\n"));

#ifdef VBOX_WITH_STATISTICS
                        CFGMR3QueryBoolDef(pCfgNodeClass, "AdvancedStatistics", &pEndpointClass->fGatherAdvancedStatistics, true);
#else
                        CFGMR3QueryBoolDef(pCfgNodeClass, "AdvancedStatistics", &pEndpointClass->fGatherAdvancedStatistics, false);
#endif

                        pUVM->pdm.s.apAsyncCompletionEndpointClass[pEpClassOps->enmClassType] = pEndpointClass;
                        LogFlowFunc((": Initialized endpoint class \"%s\" rc=%Rrc\n", pEpClassOps->pszName, rc));
                        return VINF_SUCCESS;
                    }
                }
                RTMemCacheDestroy(pEndpointClass->hMemCacheTasks);
            }
            RTCritSectDelete(&pEndpointClass->CritSect);
        }
        MMR3HeapFree(pEndpointClass);
    }

    LogFlowFunc((": Failed to initialize endpoint class rc=%Rrc\n", rc));

    return rc;
}


/**
 * Worker terminating all endpoint classes.
 *
 * @param   pEndpointClass    Pointer to the endpoint class to terminate.
 *
 * @remarks This method ensures that any still open endpoint is closed.
 */
static void pdmR3AsyncCompletionEpClassTerminate(PPDMASYNCCOMPLETIONEPCLASS pEndpointClass)
{
    PVM pVM = pEndpointClass->pVM;

    /* Close all still open endpoints. */
    while (pEndpointClass->pEndpointsHead)
        PDMR3AsyncCompletionEpClose(pEndpointClass->pEndpointsHead);

    /* Destroy the bandwidth managers. */
    PPDMACBWMGR pBwMgr = pEndpointClass->pBwMgrsHead;
    while (pBwMgr)
    {
        PPDMACBWMGR pFree = pBwMgr;
        pBwMgr = pBwMgr->pNext;
        MMR3HeapFree(pFree);
    }

    /* Call the termination callback of the class. */
    pEndpointClass->pEndpointOps->pfnTerminate(pEndpointClass);

    RTMemCacheDestroy(pEndpointClass->hMemCacheTasks);
    RTCritSectDelete(&pEndpointClass->CritSect);

    /* Free the memory of the class finally and clear the entry in the class array. */
    pVM->pUVM->pdm.s.apAsyncCompletionEndpointClass[pEndpointClass->pEndpointOps->enmClassType] = NULL;
    MMR3HeapFree(pEndpointClass);
}


/**
 * Records the size of the request in the statistics.
 *
 * @param   pEndpoint    The endpoint to register the request size for.
 * @param   cbReq        Size of the request.
 */
static void pdmR3AsyncCompletionStatisticsRecordSize(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, size_t cbReq)
{
    if (cbReq < 512)
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqSizeSmaller512);
    else if (cbReq < _1K)
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqSize512To1K);
    else if (cbReq < _2K)
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqSize1KTo2K);
    else if (cbReq < _4K)
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqSize2KTo4K);
    else if (cbReq < _8K)
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqSize4KTo8K);
    else if (cbReq < _16K)
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqSize8KTo16K);
    else if (cbReq < _32K)
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqSize16KTo32K);
    else if (cbReq < _64K)
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqSize32KTo64K);
    else if (cbReq < _128K)
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqSize64KTo128K);
    else if (cbReq < _256K)
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqSize128KTo256K);
    else if (cbReq < _512K)
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqSize256KTo512K);
    else
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqSizeOver512K);

    if (cbReq & ((size_t)512 - 1))
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqsUnaligned512);
    else if (cbReq & ((size_t)_4K - 1))
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqsUnaligned4K);
    else if (cbReq & ((size_t)_8K - 1))
        STAM_REL_COUNTER_INC(&pEndpoint->StatReqsUnaligned8K);
}


/**
 * Records the required processing time of a request.
 *
 * @param   pEndpoint    The endpoint.
 * @param   cNsRun       The request time in nanoseconds.
 */
static void pdmR3AsyncCompletionStatisticsRecordCompletionTime(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, uint64_t cNsRun)
{
    PSTAMCOUNTER pStatCounter;
    if (cNsRun < RT_NS_1US)
        pStatCounter = &pEndpoint->StatTaskRunTimesNs[cNsRun / (RT_NS_1US / 10)];
    else if (cNsRun < RT_NS_1MS)
        pStatCounter = &pEndpoint->StatTaskRunTimesUs[cNsRun / (RT_NS_1MS / 10)];
    else if (cNsRun < RT_NS_1SEC)
        pStatCounter = &pEndpoint->StatTaskRunTimesMs[cNsRun / (RT_NS_1SEC / 10)];
    else if (cNsRun < RT_NS_1SEC_64*100)
        pStatCounter = &pEndpoint->StatTaskRunTimesSec[cNsRun / (RT_NS_1SEC_64*100 / 10)];
    else
        pStatCounter = &pEndpoint->StatTaskRunOver100Sec;
    STAM_REL_COUNTER_INC(pStatCounter);

    STAM_REL_COUNTER_INC(&pEndpoint->StatIoOpsCompleted);
    pEndpoint->cIoOpsCompleted++;
    uint64_t tsMsCur = RTTimeMilliTS();
    uint64_t tsInterval = tsMsCur - pEndpoint->tsIntervalStartMs;
    if (tsInterval >= 1000)
    {
        pEndpoint->StatIoOpsPerSec.c = pEndpoint->cIoOpsCompleted / (tsInterval / 1000);
        pEndpoint->tsIntervalStartMs = tsMsCur;
        pEndpoint->cIoOpsCompleted = 0;
    }
}


/**
 * Registers advanced statistics for the given endpoint.
 *
 * @returns VBox status code.
 * @param   pEndpoint    The endpoint to register the advanced statistics for.
 */
static int pdmR3AsyncCompletionStatisticsRegister(PPDMASYNCCOMPLETIONENDPOINT pEndpoint)
{
    int rc = VINF_SUCCESS;
    PVM pVM = pEndpoint->pEpClass->pVM;

    pEndpoint->tsIntervalStartMs = RTTimeMilliTS();

    for (unsigned i = 0; i < RT_ELEMENTS(pEndpoint->StatTaskRunTimesNs) && RT_SUCCESS(rc); i++)
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatTaskRunTimesNs[i], STAMTYPE_COUNTER,
                             STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                             "Nanosecond resolution runtime statistics",
                             "/PDM/AsyncCompletion/File/%s/%d/TaskRun1Ns-%u-%u",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId, i*100, i*100+100-1);

    for (unsigned i = 0; i < RT_ELEMENTS(pEndpoint->StatTaskRunTimesUs) && RT_SUCCESS(rc); i++)
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatTaskRunTimesUs[i], STAMTYPE_COUNTER,
                             STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                             "Microsecond resolution runtime statistics",
                             "/PDM/AsyncCompletion/File/%s/%d/TaskRun2MicroSec-%u-%u",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId, i*100, i*100+100-1);

    for (unsigned i = 0; i < RT_ELEMENTS(pEndpoint->StatTaskRunTimesMs) && RT_SUCCESS(rc); i++)
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatTaskRunTimesMs[i], STAMTYPE_COUNTER,
                             STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                             "Milliseconds resolution runtime statistics",
                             "/PDM/AsyncCompletion/File/%s/%d/TaskRun3Ms-%u-%u",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId, i*100, i*100+100-1);

    for (unsigned i = 0; i < RT_ELEMENTS(pEndpoint->StatTaskRunTimesMs) && RT_SUCCESS(rc); i++)
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatTaskRunTimesSec[i], STAMTYPE_COUNTER,
                             STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                             "Second resolution runtime statistics",
                             "/PDM/AsyncCompletion/File/%s/%d/TaskRun4Sec-%u-%u",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId, i*10, i*10+10-1);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatTaskRunOver100Sec, STAMTYPE_COUNTER,
                             STAMVISIBILITY_USED, STAMUNIT_OCCURENCES,
                             "Tasks which ran more than 100sec",
                             "/PDM/AsyncCompletion/File/%s/%d/TaskRunSecGreater100Sec",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatIoOpsPerSec, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Processed I/O operations per second",
                             "/PDM/AsyncCompletion/File/%s/%d/IoOpsPerSec",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatIoOpsStarted, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Started I/O operations for this endpoint",
                             "/PDM/AsyncCompletion/File/%s/%d/IoOpsStarted",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatIoOpsCompleted, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Completed I/O operations for this endpoint",
                             "/PDM/AsyncCompletion/File/%s/%d/IoOpsCompleted",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqSizeSmaller512, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests with a size smaller than 512 bytes",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqSizeSmaller512",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqSize512To1K, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests with a size between 512 bytes and 1KB",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqSize512To1K",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqSize1KTo2K, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests with a size between 1KB and 2KB",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqSize1KTo2K",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqSize2KTo4K, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests with a size between 2KB and 4KB",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqSize2KTo4K",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqSize4KTo8K, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests with a size between 4KB and 8KB",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqSize4KTo8K",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqSize8KTo16K, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests with a size between 8KB and 16KB",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqSize8KTo16K",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqSize16KTo32K, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests with a size between 16KB and 32KB",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqSize16KTo32K",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqSize32KTo64K, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests with a size between 32KB and 64KB",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqSize32KTo64K",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqSize64KTo128K, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests with a size between 64KB and 128KB",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqSize64KTo128K",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqSize128KTo256K, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests with a size between 128KB and 256KB",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqSize128KTo256K",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqSize256KTo512K, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests with a size between 256KB and 512KB",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqSize256KTo512K",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqSizeOver512K, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests with a size over 512KB",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqSizeOver512K",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqsUnaligned512, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests which size is not aligned to 512 bytes",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqsUnaligned512",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqsUnaligned4K, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests which size is not aligned to 4KB",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqsUnaligned4K",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    if (RT_SUCCESS(rc))
        rc = STAMR3RegisterF(pVM, &pEndpoint->StatReqsUnaligned8K, STAMTYPE_COUNTER,
                             STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                             "Number of requests which size is not aligned to 8KB",
                             "/PDM/AsyncCompletion/File/%s/%d/ReqsUnaligned8K",
                             RTPathFilename(pEndpoint->pszUri), pEndpoint->iStatId);

    return rc;
}


/**
 * Deregisters advanced statistics for one endpoint.
 *
 * @param   pEndpoint    The endpoint to deregister the advanced statistics for.
 */
static void pdmR3AsyncCompletionStatisticsDeregister(PPDMASYNCCOMPLETIONENDPOINT pEndpoint)
{
    /* I hope this doesn't remove too much... */
    STAMR3DeregisterF(pEndpoint->pEpClass->pVM->pUVM, "/PDM/AsyncCompletion/File/%s/*", RTPathFilename(pEndpoint->pszUri));
}


/**
 * Initialize the async completion manager.
 *
 * @returns VBox status code
 * @param   pVM The cross context VM structure.
 */
int pdmR3AsyncCompletionInit(PVM pVM)
{
    LogFlowFunc((": pVM=%p\n", pVM));

    VM_ASSERT_EMT(pVM);

    PCFGMNODE pCfgRoot            = CFGMR3GetRoot(pVM);
    PCFGMNODE pCfgAsyncCompletion = CFGMR3GetChild(CFGMR3GetChild(pCfgRoot, "PDM"), "AsyncCompletion");

    int rc = pdmR3AsyncCompletionEpClassInit(pVM, &g_PDMAsyncCompletionEndpointClassFile, pCfgAsyncCompletion);
    LogFlowFunc((": pVM=%p rc=%Rrc\n", pVM, rc));
    return rc;
}


/**
 * Terminates the async completion manager.
 *
 * @returns VBox status code
 * @param   pVM The cross context VM structure.
 */
int pdmR3AsyncCompletionTerm(PVM pVM)
{
    LogFlowFunc((": pVM=%p\n", pVM));
    PUVM pUVM = pVM->pUVM;

    for (size_t i = 0; i < RT_ELEMENTS(pUVM->pdm.s.apAsyncCompletionEndpointClass); i++)
        if (pUVM->pdm.s.apAsyncCompletionEndpointClass[i])
            pdmR3AsyncCompletionEpClassTerminate(pUVM->pdm.s.apAsyncCompletionEndpointClass[i]);

    return VINF_SUCCESS;
}


/**
 * Resume worker for the async completion manager.
 *
 * @param   pVM The cross context VM structure.
 */
void pdmR3AsyncCompletionResume(PVM pVM)
{
    LogFlowFunc((": pVM=%p\n", pVM));
    PUVM pUVM = pVM->pUVM;

    /* Log the bandwidth groups and all assigned endpoints. */
    for (size_t i = 0; i < RT_ELEMENTS(pUVM->pdm.s.apAsyncCompletionEndpointClass); i++)
        if (pUVM->pdm.s.apAsyncCompletionEndpointClass[i])
        {
            PPDMASYNCCOMPLETIONEPCLASS  pEpClass = pUVM->pdm.s.apAsyncCompletionEndpointClass[i];
            PPDMACBWMGR                 pBwMgr   = pEpClass->pBwMgrsHead;
            PPDMASYNCCOMPLETIONENDPOINT pEp;

            if (pBwMgr)
                LogRel(("AIOMgr: Bandwidth groups for class '%s'\n", i == PDMASYNCCOMPLETIONEPCLASSTYPE_FILE
                                                                     ? "File" : "<Unknown>"));

            while (pBwMgr)
            {
                LogRel(("AIOMgr:     Id:    %s\n", pBwMgr->pszId));
                LogRel(("AIOMgr:     Max:   %RU64 B/s\n", pBwMgr->cbTransferPerSecMax));
                LogRel(("AIOMgr:     Start: %RU64 B/s\n", pBwMgr->cbTransferPerSecStart));
                LogRel(("AIOMgr:     Step:  %RU64 B/s\n", pBwMgr->cbTransferPerSecStep));
                LogRel(("AIOMgr:     Endpoints:\n"));

                pEp = pEpClass->pEndpointsHead;
                while (pEp)
                {
                    if (pEp->pBwMgr == pBwMgr)
                        LogRel(("AIOMgr:         %s\n", pEp->pszUri));

                    pEp = pEp->pNext;
                }

                pBwMgr = pBwMgr->pNext;
            }

            /* Print all endpoints without assigned bandwidth groups. */
            pEp = pEpClass->pEndpointsHead;
            if (pEp)
                LogRel(("AIOMgr: Endpoints without assigned bandwidth groups:\n"));

            while (pEp)
            {
                if (!pEp->pBwMgr)
                    LogRel(("AIOMgr:     %s\n", pEp->pszUri));

                pEp = pEp->pNext;
            }
        }
}


/**
 * Tries to get a free task from the endpoint or class cache
 * allocating the task if it fails.
 *
 * @returns Pointer to a new and initialized task or NULL
 * @param   pEndpoint    The endpoint the task is for.
 * @param   pvUser       Opaque user data for the task.
 */
static PPDMASYNCCOMPLETIONTASK pdmR3AsyncCompletionGetTask(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, void *pvUser)
{
    PPDMASYNCCOMPLETIONEPCLASS pEndpointClass = pEndpoint->pEpClass;
    PPDMASYNCCOMPLETIONTASK    pTask = (PPDMASYNCCOMPLETIONTASK)RTMemCacheAlloc(pEndpointClass->hMemCacheTasks);
    if (RT_LIKELY(pTask))
    {
        /* Initialize common parts. */
        pTask->pvUser    = pvUser;
        pTask->pEndpoint = pEndpoint;
        /* Clear list pointers for safety. */
        pTask->pPrev     = NULL;
        pTask->pNext     = NULL;
        pTask->tsNsStart = RTTimeNanoTS();
        STAM_REL_COUNTER_INC(&pEndpoint->StatIoOpsStarted);
    }

    return pTask;
}


/**
 * Puts a task in one of the caches.
 *
 * @param   pEndpoint    The endpoint the task belongs to.
 * @param   pTask        The task to cache.
 */
static void pdmR3AsyncCompletionPutTask(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, PPDMASYNCCOMPLETIONTASK pTask)
{
    PPDMASYNCCOMPLETIONEPCLASS pEndpointClass = pEndpoint->pEpClass;
    uint64_t cNsRun = RTTimeNanoTS() - pTask->tsNsStart;

    if (RT_UNLIKELY(cNsRun >= RT_NS_10SEC))
        LogRel(("AsyncCompletion: Task %#p completed after %llu seconds\n", pTask, cNsRun / RT_NS_1SEC));

    if (pEndpointClass->fGatherAdvancedStatistics)
        pdmR3AsyncCompletionStatisticsRecordCompletionTime(pEndpoint, cNsRun);

    RTMemCacheFree(pEndpointClass->hMemCacheTasks, pTask);
}


static unsigned
pdmR3AsyncCompletionGetStatId(PPDMASYNCCOMPLETIONEPCLASS pEndpointClass, const char *pszUri)
{
    PPDMASYNCCOMPLETIONENDPOINT pEndpoint = pEndpointClass->pEndpointsHead;
    const char *pszFilename = RTPathFilename(pszUri);
    unsigned iStatId = 0;

    while (pEndpoint)
    {
        if (   !RTStrCmp(RTPathFilename(pEndpoint->pszUri), pszFilename)
            && pEndpoint->iStatId >= iStatId)
            iStatId = pEndpoint->iStatId + 1;

        pEndpoint = pEndpoint->pNext;
    }

    return iStatId;
}

/**
 * Opens a file as an async completion endpoint.
 *
 * @returns VBox status code.
 * @param   ppEndpoint      Where to store the opaque endpoint handle on success.
 * @param   pszFilename     Path to the file which is to be opened. (UTF-8)
 * @param   fFlags          Open flags, see grp_pdmacep_file_flags.
 * @param   pTemplate       Handle to the completion callback template to use
 *                          for this end point.
 */
VMMR3DECL(int) PDMR3AsyncCompletionEpCreateForFile(PPPDMASYNCCOMPLETIONENDPOINT ppEndpoint,
                                                   const char *pszFilename, uint32_t fFlags,
                                                   PPDMASYNCCOMPLETIONTEMPLATE pTemplate)
{
    LogFlowFunc((": ppEndpoint=%p pszFilename=%p{%s} fFlags=%u pTemplate=%p\n",
                 ppEndpoint, pszFilename, pszFilename, fFlags, pTemplate));

    /* Sanity checks. */
    AssertPtrReturn(ppEndpoint,  VERR_INVALID_POINTER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertPtrReturn(pTemplate,   VERR_INVALID_POINTER);

    /* Check that the flags are valid. */
    AssertReturn(((~(PDMACEP_FILE_FLAGS_READ_ONLY | PDMACEP_FILE_FLAGS_DONT_LOCK | PDMACEP_FILE_FLAGS_HOST_CACHE_ENABLED) & fFlags) == 0),
                 VERR_INVALID_PARAMETER);

    PVM  pVM  = pTemplate->pVM;
    PUVM pUVM = pVM->pUVM;
    PPDMASYNCCOMPLETIONEPCLASS  pEndpointClass = pUVM->pdm.s.apAsyncCompletionEndpointClass[PDMASYNCCOMPLETIONEPCLASSTYPE_FILE];
    PPDMASYNCCOMPLETIONENDPOINT pEndpoint = NULL;

    AssertMsg(pEndpointClass, ("File endpoint class was not initialized\n"));

    /* Create an endpoint. */
    int rc = MMR3HeapAllocZEx(pVM, MM_TAG_PDM_ASYNC_COMPLETION,
                              pEndpointClass->pEndpointOps->cbEndpoint,
                              (void **)&pEndpoint);
    if (RT_SUCCESS(rc))
    {
        /* Initialize common parts. */
        pEndpoint->pNext             = NULL;
        pEndpoint->pPrev             = NULL;
        pEndpoint->pEpClass          = pEndpointClass;
        pEndpoint->pTemplate         = pTemplate;
        pEndpoint->pszUri            = RTStrDup(pszFilename);
        pEndpoint->iStatId           = pdmR3AsyncCompletionGetStatId(pEndpointClass, pszFilename);
        pEndpoint->pBwMgr            = NULL;

        if (   pEndpoint->pszUri
            && RT_SUCCESS(rc))
        {
            /* Call the initializer for the endpoint. */
            rc = pEndpointClass->pEndpointOps->pfnEpInitialize(pEndpoint, pszFilename, fFlags);
            if (RT_SUCCESS(rc))
            {
                if (pEndpointClass->fGatherAdvancedStatistics)
                    rc = pdmR3AsyncCompletionStatisticsRegister(pEndpoint);

                if (RT_SUCCESS(rc))
                {
                    /* Link it into the list of endpoints. */
                    rc = RTCritSectEnter(&pEndpointClass->CritSect);
                    AssertMsg(RT_SUCCESS(rc), ("Failed to enter critical section rc=%Rrc\n", rc));

                    pEndpoint->pNext = pEndpointClass->pEndpointsHead;
                    if (pEndpointClass->pEndpointsHead)
                        pEndpointClass->pEndpointsHead->pPrev = pEndpoint;

                    pEndpointClass->pEndpointsHead = pEndpoint;
                    pEndpointClass->cEndpoints++;

                    rc = RTCritSectLeave(&pEndpointClass->CritSect);
                    AssertMsg(RT_SUCCESS(rc), ("Failed to enter critical section rc=%Rrc\n", rc));

                    /* Reference the template. */
                    ASMAtomicIncU32(&pTemplate->cUsed);

                    *ppEndpoint = pEndpoint;
                    LogFlowFunc((": Created endpoint for %s\n", pszFilename));
                    return VINF_SUCCESS;
                }
                else
                    pEndpointClass->pEndpointOps->pfnEpClose(pEndpoint);

                if (pEndpointClass->fGatherAdvancedStatistics)
                    pdmR3AsyncCompletionStatisticsDeregister(pEndpoint);
            }
            RTStrFree(pEndpoint->pszUri);
        }
        MMR3HeapFree(pEndpoint);
    }

    LogFlowFunc((": Creation of endpoint for %s failed: rc=%Rrc\n", pszFilename, rc));
    return rc;
}


/**
 * Closes a endpoint waiting for any pending tasks to finish.
 *
 * @param   pEndpoint       Handle of the endpoint.
 */
VMMR3DECL(void) PDMR3AsyncCompletionEpClose(PPDMASYNCCOMPLETIONENDPOINT pEndpoint)
{
    LogFlowFunc((": pEndpoint=%p\n", pEndpoint));

    /* Sanity checks. */
    AssertReturnVoid(RT_VALID_PTR(pEndpoint));

    PPDMASYNCCOMPLETIONEPCLASS pEndpointClass = pEndpoint->pEpClass;
    pEndpointClass->pEndpointOps->pfnEpClose(pEndpoint);

    /* Drop reference from the template. */
    ASMAtomicDecU32(&pEndpoint->pTemplate->cUsed);

    /* Unlink the endpoint from the list. */
    int rc = RTCritSectEnter(&pEndpointClass->CritSect);
    AssertMsg(RT_SUCCESS(rc), ("Failed to enter critical section rc=%Rrc\n", rc));

    PPDMASYNCCOMPLETIONENDPOINT pEndpointNext = pEndpoint->pNext;
    PPDMASYNCCOMPLETIONENDPOINT pEndpointPrev = pEndpoint->pPrev;

    if (pEndpointPrev)
        pEndpointPrev->pNext = pEndpointNext;
    else
        pEndpointClass->pEndpointsHead = pEndpointNext;
    if (pEndpointNext)
        pEndpointNext->pPrev = pEndpointPrev;

    pEndpointClass->cEndpoints--;

    rc = RTCritSectLeave(&pEndpointClass->CritSect);
    AssertMsg(RT_SUCCESS(rc), ("Failed to enter critical section rc=%Rrc\n", rc));

    if (pEndpointClass->fGatherAdvancedStatistics)
        pdmR3AsyncCompletionStatisticsDeregister(pEndpoint);

    RTStrFree(pEndpoint->pszUri);
    MMR3HeapFree(pEndpoint);
}


/**
 * Creates a read task on the given endpoint.
 *
 * @returns VBox status code.
 * @param   pEndpoint       The file endpoint to read from.
 * @param   off             Where to start reading from.
 * @param   paSegments      Scatter gather list to store the data in.
 * @param   cSegments       Number of segments in the list.
 * @param   cbRead          The overall number of bytes to read.
 * @param   pvUser          Opaque user data returned in the completion callback
 *                          upon completion of the task.
 * @param   ppTask          Where to store the task handle on success.
 */
VMMR3DECL(int) PDMR3AsyncCompletionEpRead(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, RTFOFF off,
                                          PCRTSGSEG paSegments, unsigned cSegments,
                                          size_t cbRead, void *pvUser,
                                          PPPDMASYNCCOMPLETIONTASK ppTask)
{
    AssertPtrReturn(pEndpoint, VERR_INVALID_POINTER);
    AssertPtrReturn(paSegments, VERR_INVALID_POINTER);
    AssertPtrReturn(ppTask, VERR_INVALID_POINTER);
    AssertReturn(cSegments > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cbRead > 0, VERR_INVALID_PARAMETER);
    AssertReturn(off >= 0, VERR_INVALID_PARAMETER);

    PPDMASYNCCOMPLETIONTASK pTask;

    pTask = pdmR3AsyncCompletionGetTask(pEndpoint, pvUser);
    if (!pTask)
        return VERR_NO_MEMORY;

    int rc = pEndpoint->pEpClass->pEndpointOps->pfnEpRead(pTask, pEndpoint, off,
                                                          paSegments, cSegments, cbRead);
    if (RT_SUCCESS(rc))
    {
        if (pEndpoint->pEpClass->fGatherAdvancedStatistics)
            pdmR3AsyncCompletionStatisticsRecordSize(pEndpoint, cbRead);

        *ppTask = pTask;
    }
    else
        pdmR3AsyncCompletionPutTask(pEndpoint, pTask);

    return rc;
}


/**
 * Creates a write task on the given endpoint.
 *
 * @returns VBox status code.
 * @param   pEndpoint       The file endpoint to write to.
 * @param   off             Where to start writing at.
 * @param   paSegments      Scatter gather list of the data to write.
 * @param   cSegments       Number of segments in the list.
 * @param   cbWrite         The overall number of bytes to write.
 * @param   pvUser          Opaque user data returned in the completion callback
 *                          upon completion of the task.
 * @param   ppTask          Where to store the task handle on success.
 */
VMMR3DECL(int) PDMR3AsyncCompletionEpWrite(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, RTFOFF off,
                                           PCRTSGSEG paSegments, unsigned cSegments,
                                           size_t cbWrite, void *pvUser,
                                           PPPDMASYNCCOMPLETIONTASK ppTask)
{
    AssertPtrReturn(pEndpoint, VERR_INVALID_POINTER);
    AssertPtrReturn(paSegments, VERR_INVALID_POINTER);
    AssertPtrReturn(ppTask, VERR_INVALID_POINTER);
    AssertReturn(cSegments > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cbWrite > 0, VERR_INVALID_PARAMETER);
    AssertReturn(off >= 0, VERR_INVALID_PARAMETER);

    PPDMASYNCCOMPLETIONTASK pTask;

    pTask = pdmR3AsyncCompletionGetTask(pEndpoint, pvUser);
    if (!pTask)
        return VERR_NO_MEMORY;

    int rc = pEndpoint->pEpClass->pEndpointOps->pfnEpWrite(pTask, pEndpoint, off,
                                                           paSegments, cSegments, cbWrite);
    if (RT_SUCCESS(rc))
    {
        if (pEndpoint->pEpClass->fGatherAdvancedStatistics)
            pdmR3AsyncCompletionStatisticsRecordSize(pEndpoint, cbWrite);

        *ppTask = pTask;
    }
    else
        pdmR3AsyncCompletionPutTask(pEndpoint, pTask);

    return rc;
}


/**
 * Creates a flush task on the given endpoint.
 *
 * Every read and write task initiated before the flush task is
 * finished upon completion of this task.
 *
 * @returns VBox status code.
 * @param   pEndpoint       The file endpoint to flush.
 * @param   pvUser          Opaque user data returned in the completion callback
 *                          upon completion of the task.
 * @param   ppTask          Where to store the task handle on success.
 */
VMMR3DECL(int) PDMR3AsyncCompletionEpFlush(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, void *pvUser, PPPDMASYNCCOMPLETIONTASK ppTask)
{
    AssertPtrReturn(pEndpoint, VERR_INVALID_POINTER);
    AssertPtrReturn(ppTask, VERR_INVALID_POINTER);

    PPDMASYNCCOMPLETIONTASK pTask;

    pTask = pdmR3AsyncCompletionGetTask(pEndpoint, pvUser);
    if (!pTask)
        return VERR_NO_MEMORY;

    int rc = pEndpoint->pEpClass->pEndpointOps->pfnEpFlush(pTask, pEndpoint);
    if (RT_SUCCESS(rc))
        *ppTask = pTask;
    else
        pdmR3AsyncCompletionPutTask(pEndpoint, pTask);

    return rc;
}


/**
 * Queries the size of an endpoint.
 *
 * Not that some endpoints may not support this and will return an error
 * (sockets for example).
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_SUPPORTED if the endpoint does not support this operation.
 * @param   pEndpoint       The file endpoint.
 * @param   pcbSize         Where to store the size of the endpoint.
 */
VMMR3DECL(int) PDMR3AsyncCompletionEpGetSize(PPDMASYNCCOMPLETIONENDPOINT pEndpoint,
                                             uint64_t *pcbSize)
{
    AssertPtrReturn(pEndpoint, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbSize, VERR_INVALID_POINTER);

    if (pEndpoint->pEpClass->pEndpointOps->pfnEpGetSize)
        return pEndpoint->pEpClass->pEndpointOps->pfnEpGetSize(pEndpoint, pcbSize);
    return VERR_NOT_SUPPORTED;
}


/**
 * Sets the size of an endpoint.
 *
 * Not that some endpoints may not support this and will return an error
 * (sockets for example).
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_SUPPORTED if the endpoint does not support this operation.
 * @param   pEndpoint       The file endpoint.
 * @param   cbSize          The size to set.
 *
 * @note PDMR3AsyncCompletionEpFlush should be called before this operation is executed.
 */
VMMR3DECL(int) PDMR3AsyncCompletionEpSetSize(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, uint64_t cbSize)
{
    AssertPtrReturn(pEndpoint, VERR_INVALID_POINTER);

    if (pEndpoint->pEpClass->pEndpointOps->pfnEpSetSize)
        return pEndpoint->pEpClass->pEndpointOps->pfnEpSetSize(pEndpoint, cbSize);
    return VERR_NOT_SUPPORTED;
}


/**
 * Assigns or removes a bandwidth control manager to/from the endpoint.
 *
 * @returns VBox status code.
 * @param   pEndpoint       The endpoint.
 * @param   pszBwMgr        The identifer of the new bandwidth manager to assign
 *                          or NULL to remove the current one.
 */
VMMR3DECL(int) PDMR3AsyncCompletionEpSetBwMgr(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, const char *pszBwMgr)
{
    AssertPtrReturn(pEndpoint, VERR_INVALID_POINTER);
    PPDMACBWMGR pBwMgrOld = NULL;
    PPDMACBWMGR pBwMgrNew = NULL;

    int rc = VINF_SUCCESS;
    if (pszBwMgr)
    {
        pBwMgrNew = pdmacBwMgrFindById(pEndpoint->pEpClass, pszBwMgr);
        if (pBwMgrNew)
            pdmacBwMgrRetain(pBwMgrNew);
        else
            rc = VERR_NOT_FOUND;
    }

    if (RT_SUCCESS(rc))
    {
        pBwMgrOld = ASMAtomicXchgPtrT(&pEndpoint->pBwMgr, pBwMgrNew, PPDMACBWMGR);
        if (pBwMgrOld)
            pdmacBwMgrRelease(pBwMgrOld);
    }

    return rc;
}


/**
 * Cancels an async completion task.
 *
 * If you want to use this method, you have to take great create to make sure
 * you will never attempt cancel a task which has been completed. Since there is
 * no reference counting or anything on the task it self, you have to serialize
 * the cancelation and completion paths such that the aren't racing one another.
 *
 * @returns VBox status code
 * @param   pTask           The Task to cancel.
 */
VMMR3DECL(int) PDMR3AsyncCompletionTaskCancel(PPDMASYNCCOMPLETIONTASK pTask)
{
    NOREF(pTask);
    return VERR_NOT_IMPLEMENTED;
}


/**
 * Changes the limit of a bandwidth manager for file endpoints to the given value.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszBwMgr        The identifer of the bandwidth manager to change.
 * @param   cbMaxNew        The new maximum for the bandwidth manager in bytes/sec.
 */
VMMR3DECL(int) PDMR3AsyncCompletionBwMgrSetMaxForFile(PUVM pUVM, const char *pszBwMgr, uint64_t cbMaxNew)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pszBwMgr, VERR_INVALID_POINTER);

    int                         rc       = VINF_SUCCESS;
    PPDMASYNCCOMPLETIONEPCLASS  pEpClass = pVM->pUVM->pdm.s.apAsyncCompletionEndpointClass[PDMASYNCCOMPLETIONEPCLASSTYPE_FILE];
    PPDMACBWMGR                 pBwMgr   = pdmacBwMgrFindById(pEpClass, pszBwMgr);
    if (pBwMgr)
    {
        /*
         * Set the new value for the start and max value to let the manager pick up
         * the new limit immediately.
         */
        ASMAtomicWriteU64(&pBwMgr->cbTransferPerSecMax, cbMaxNew);
        ASMAtomicWriteU64(&pBwMgr->cbTransferPerSecStart, cbMaxNew);
    }
    else
        rc = VERR_NOT_FOUND;

    return rc;
}

