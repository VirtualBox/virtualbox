/** @file
 * PDM - Pluggable Device Manager, Async I/O Completion.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_vmm_pdmasynccompletion_h
#define VBOX_INCLUDED_vmm_pdmasynccompletion_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <iprt/sg.h>


RT_C_DECLS_BEGIN

/** @defgroup grp_pdm_async_completion  The PDM Async I/O Completion API
 * @ingroup grp_pdm
 * @{
 */

/** Pointer to a PDM async completion template handle. */
typedef struct PDMASYNCCOMPLETIONTEMPLATE *PPDMASYNCCOMPLETIONTEMPLATE;
/** Pointer to a PDM async completion template handle pointer. */
typedef PPDMASYNCCOMPLETIONTEMPLATE *PPPDMASYNCCOMPLETIONTEMPLATE;

/** Pointer to a PDM async completion task handle. */
typedef struct PDMASYNCCOMPLETIONTASK *PPDMASYNCCOMPLETIONTASK;
/** Pointer to a PDM async completion task handle pointer. */
typedef PPDMASYNCCOMPLETIONTASK *PPPDMASYNCCOMPLETIONTASK;

/** Pointer to a PDM async completion endpoint handle. */
typedef struct PDMASYNCCOMPLETIONENDPOINT *PPDMASYNCCOMPLETIONENDPOINT;
/** Pointer to a PDM async completion endpoint handle pointer. */
typedef PPDMASYNCCOMPLETIONENDPOINT *PPPDMASYNCCOMPLETIONENDPOINT;


/**
 * Completion callback for devices.
 *
 * @param   pDevIns     The device instance.
 * @param   pvUser      User argument.
 * @param   rc          The status code of the completed request.
 */
typedef DECLCALLBACKTYPE(void, FNPDMASYNCCOMPLETEDEV,(PPDMDEVINS pDevIns, void *pvUser, int rc));
/** Pointer to a FNPDMASYNCCOMPLETEDEV(). */
typedef FNPDMASYNCCOMPLETEDEV *PFNPDMASYNCCOMPLETEDEV;


/**
 * Completion callback for drivers.
 *
 * @param   pDrvIns        The driver instance.
 * @param   pvTemplateUser User argument given when creating the template.
 * @param   pvUser         User argument given during request initiation.
 * @param   rc          The status code of the completed request.
 */
typedef DECLCALLBACKTYPE(void, FNPDMASYNCCOMPLETEDRV,(PPDMDRVINS pDrvIns, void *pvTemplateUser, void *pvUser, int rc));
/** Pointer to a FNPDMASYNCCOMPLETEDRV(). */
typedef FNPDMASYNCCOMPLETEDRV *PFNPDMASYNCCOMPLETEDRV;


/**
 * Completion callback for USB devices.
 *
 * @param   pUsbIns     The USB device instance.
 * @param   pvUser      User argument.
 * @param   rc          The status code of the completed request.
 */
typedef DECLCALLBACKTYPE(void, FNPDMASYNCCOMPLETEUSB,(PPDMUSBINS pUsbIns, void *pvUser, int rc));
/** Pointer to a FNPDMASYNCCOMPLETEUSB(). */
typedef FNPDMASYNCCOMPLETEUSB *PFNPDMASYNCCOMPLETEUSB;


/**
 * Completion callback for internal.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pvUser      User argument for the task.
 * @param   pvUser2     User argument for the template.
 * @param   rc          The status code of the completed request.
 */
typedef DECLCALLBACKTYPE(void, FNPDMASYNCCOMPLETEINT,(PVM pVM, void *pvUser, void *pvUser2, int rc));
/** Pointer to a FNPDMASYNCCOMPLETEINT(). */
typedef FNPDMASYNCCOMPLETEINT *PFNPDMASYNCCOMPLETEINT;

VMMR3DECL(int) PDMR3AsyncCompletionTemplateCreateInternal(PVM pVM, PPPDMASYNCCOMPLETIONTEMPLATE ppTemplate,
                                                          PFNPDMASYNCCOMPLETEINT pfnCompleted, void *pvUser2, const char *pszDesc);
VMMR3DECL(int) PDMR3AsyncCompletionTemplateDestroy(PPDMASYNCCOMPLETIONTEMPLATE pTemplate);
VMMR3DECL(int) PDMR3AsyncCompletionEpCreateForFile(PPPDMASYNCCOMPLETIONENDPOINT ppEndpoint,
                                                   const char *pszFilename, uint32_t fFlags,
                                                   PPDMASYNCCOMPLETIONTEMPLATE pTemplate);

/** @defgroup grp_pdmacep_file_flags Flags for PDMR3AsyncCompletionEpCreateForFile
 * @{ */
/** Open the file in read-only mode. */
#define PDMACEP_FILE_FLAGS_READ_ONLY             RT_BIT_32(0)
/** Whether the file should not be write protected.
 * The default is to protect the file against writes by other processes
 * when opened in read/write mode to prevent data corruption by
 * concurrent access which can occur if the local writeback cache is enabled.
 */
#define PDMACEP_FILE_FLAGS_DONT_LOCK             RT_BIT_32(2)
/** Open the endpoint with the host cache enabled. */
#define PDMACEP_FILE_FLAGS_HOST_CACHE_ENABLED    RT_BIT_32(3)
/** @} */

VMMR3DECL(void) PDMR3AsyncCompletionEpClose(PPDMASYNCCOMPLETIONENDPOINT pEndpoint);
VMMR3DECL(int) PDMR3AsyncCompletionEpRead(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, RTFOFF off,
                                          PCRTSGSEG paSegments, unsigned cSegments,
                                          size_t cbRead, void *pvUser,
                                          PPPDMASYNCCOMPLETIONTASK ppTask);
VMMR3DECL(int) PDMR3AsyncCompletionEpWrite(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, RTFOFF off,
                                           PCRTSGSEG paSegments, unsigned cSegments,
                                           size_t cbWrite, void *pvUser,
                                           PPPDMASYNCCOMPLETIONTASK ppTask);
VMMR3DECL(int) PDMR3AsyncCompletionEpFlush(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, void *pvUser, PPPDMASYNCCOMPLETIONTASK ppTask);
VMMR3DECL(int) PDMR3AsyncCompletionEpGetSize(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, uint64_t *pcbSize);
VMMR3DECL(int) PDMR3AsyncCompletionEpSetSize(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, uint64_t cbSize);
VMMR3DECL(int) PDMR3AsyncCompletionEpSetBwMgr(PPDMASYNCCOMPLETIONENDPOINT pEndpoint, const char *pszBwMgr);
VMMR3DECL(int) PDMR3AsyncCompletionTaskCancel(PPDMASYNCCOMPLETIONTASK pTask);
VMMR3DECL(int) PDMR3AsyncCompletionBwMgrSetMaxForFile(PUVM pUVM, const char *pszBwMgr, uint64_t cbMaxNew);

/** @} */

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_vmm_pdmasynccompletion_h */

