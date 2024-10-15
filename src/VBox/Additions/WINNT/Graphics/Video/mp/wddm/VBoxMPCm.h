/* $Id: VBoxMPCm.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox WDDM Miniport driver
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPCm_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPCm_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

typedef struct VBOXVIDEOCM_MGR
{
    KSPIN_LOCK SynchLock;
    /* session list */
    LIST_ENTRY SessionList;
} VBOXVIDEOCM_MGR, *PVBOXVIDEOCM_MGR;

typedef struct VBOXVIDEOCM_CTX
{
    LIST_ENTRY SessionEntry;
    struct VBOXVIDEOCM_SESSION *pSession;
    uint64_t u64UmData;
    VBOXWDDM_HTABLE AllocTable;
} VBOXVIDEOCM_CTX, *PVBOXVIDEOCM_CTX;

void vboxVideoCmCtxInitEmpty(PVBOXVIDEOCM_CTX pContext);

NTSTATUS vboxVideoCmCtxAdd(PVBOXVIDEOCM_MGR pMgr, PVBOXVIDEOCM_CTX pContext, HANDLE hUmEvent, uint64_t u64UmData);
NTSTATUS vboxVideoCmCtxRemove(PVBOXVIDEOCM_MGR pMgr, PVBOXVIDEOCM_CTX pContext);
NTSTATUS vboxVideoCmInit(PVBOXVIDEOCM_MGR pMgr);
NTSTATUS vboxVideoCmTerm(PVBOXVIDEOCM_MGR pMgr);
NTSTATUS vboxVideoCmSignalEvents(PVBOXVIDEOCM_MGR pMgr);

NTSTATUS vboxVideoCmCmdSubmitCompleteEvent(PVBOXVIDEOCM_CTX pContext, PKEVENT pEvent);
void* vboxVideoCmCmdCreate(PVBOXVIDEOCM_CTX pContext, uint32_t cbSize);
void* vboxVideoCmCmdReinitForContext(void *pvCmd, PVBOXVIDEOCM_CTX pContext);
void vboxVideoCmCmdRetain(void *pvCmd);
void vboxVideoCmCmdRelease(void *pvCmd);
#define VBOXVIDEOCM_SUBMITSIZE_DEFAULT (~0UL)
void vboxVideoCmCmdSubmit(void *pvCmd, uint32_t cbSize);

#define VBOXVIDEOCMCMDVISITOR_RETURN_BREAK    0x00000001
#define VBOXVIDEOCMCMDVISITOR_RETURN_RMCMD    0x00000002
typedef DECLCALLBACKTYPE(UINT, FNVBOXVIDEOCMCMDVISITOR,(PVBOXVIDEOCM_CTX pContext, PVOID pvCmd, uint32_t cbCmd, PVOID pvVisitor));
typedef FNVBOXVIDEOCMCMDVISITOR *PFNVBOXVIDEOCMCMDVISITOR;
NTSTATUS vboxVideoCmCmdVisit(PVBOXVIDEOCM_CTX pContext, BOOLEAN bEntireSession, PFNVBOXVIDEOCMCMDVISITOR pfnVisitor, PVOID pvVisitor);

NTSTATUS vboxVideoCmEscape(PVBOXVIDEOCM_CTX pContext, PVBOXDISPIFESCAPE_GETVBOXVIDEOCMCMD pCmd, uint32_t cbCmd);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_mp_wddm_VBoxMPCm_h */
