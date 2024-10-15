/* $Id: HGCM.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * HGCM - Host-Guest Communication Manager.
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

#ifndef MAIN_INCLUDED_HGCM_h
#define MAIN_INCLUDED_HGCM_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vmm/pdmifs.h>

#include <VBox/hgcmsvc.h>

/* Handle of a HGCM service extension. */
struct _HGCMSVCEXTHANDLEDATA;
typedef struct _HGCMSVCEXTHANDLEDATA *HGCMSVCEXTHANDLE;

RT_C_DECLS_BEGIN
int HGCMHostInit(void);
int HGCMHostShutdown(bool fUvmIsInvalid = false);

int HGCMHostReset(bool fForShutdown);

int HGCMHostLoad(const char *pszServiceLibrary, const char *pszServiceName,
                 PUVM pUVM, PCVMMR3VTABLE pVMM, PPDMIHGCMPORT pHgcmPort);

int HGCMHostRegisterServiceExtension(HGCMSVCEXTHANDLE *pHandle, const char *pszServiceName, PFNHGCMSVCEXT pfnExtension, void *pvExtension);
void HGCMHostUnregisterServiceExtension(HGCMSVCEXTHANDLE handle);

int HGCMGuestConnect(PPDMIHGCMPORT pHGCMPort, PVBOXHGCMCMD pCmdPtr, const char *pszServiceName, uint32_t *pClientID);
int HGCMGuestDisconnect(PPDMIHGCMPORT pHGCMPort, PVBOXHGCMCMD pCmdPtr, uint32_t clientID);
int HGCMGuestCall(PPDMIHGCMPORT pHGCMPort, PVBOXHGCMCMD pCmdPtr, uint32_t clientID, uint32_t function, uint32_t cParms,
                  VBOXHGCMSVCPARM *paParms, uint64_t tsArrival);
void HGCMGuestCancelled(PPDMIHGCMPORT pHGCMPort, PVBOXHGCMCMD pCmdPtr, uint32_t idClient);

int HGCMHostCall(const char *pszServiceName, uint32_t function, uint32_t cParms, VBOXHGCMSVCPARM aParms[]);
int HGCMBroadcastEvent(HGCMNOTIFYEVENT enmEvent);

int HGCMHostSaveState(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM);
int HGCMHostLoadState(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, uint32_t uVersion);

RT_C_DECLS_END

#endif /* !MAIN_INCLUDED_HGCM_h */

