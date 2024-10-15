/* $Id: VBoxDbg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Debugger GUI.
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
#define LOG_GROUP LOG_GROUP_DBGG
#define VBOX_COM_NO_ATL
#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h> /* Include via cleanup wrapper before VirtualBox.h includes it via rpc.h. */
# include <VirtualBox.h>
#else /* !RT_OS_WINDOWS */
# include <VirtualBox_XPCOM.h>
#endif /* !RT_OS_WINDOWS */
#include <VBox/dbggui.h>
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>

#include "VBoxDbgGui.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Debugger GUI instance data.
 */
typedef struct DBGGUI
{
    /** Magic number (DBGGUI_MAGIC). */
    uint32_t    u32Magic;
    /** Pointer to the Debugger GUI manager object. */
    VBoxDbgGui *pVBoxDbgGui;
} DBGGUI;

/** DBGGUI magic value (Werner Heisenberg). */
#define DBGGUI_MAGIC        0x19011205
/** Invalid DBGGUI magic value. */
#define DBGGUI_MAGIC_DEAD   0x19760201


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Virtual method table for simplifying dynamic linking. */
static const DBGGUIVT g_dbgGuiVT =
{
    DBGGUIVT_VERSION,
    DBGGuiDestroy,
    DBGGuiAdjustRelativePos,
    DBGGuiShowStatistics,
    DBGGuiShowCommandLine,
    DBGGuiSetParent,
    DBGGuiSetMenu,
    DBGGUIVT_VERSION
};


/**
 * Internal worker for DBGGuiCreate and DBGGuiCreateForVM.
 *
 * @returns VBox status code.
 * @param   pSession    The ISession interface. (DBGGuiCreate)
 * @param   pUVM        The VM handle. (DBGGuiCreateForVM)
 * @param   pVMM        The VMM function table.
 * @param   ppGui       See DBGGuiCreate.
 * @param   ppGuiVT     See DBGGuiCreate.
 */
static int dbgGuiCreate(ISession *pSession, PUVM pUVM, PCVMMR3VTABLE pVMM, PDBGGUI *ppGui, PCDBGGUIVT *ppGuiVT)
{
    /*
     * Allocate and initialize the Debugger GUI handle.
     */
    PDBGGUI pGui = (PDBGGUI)RTMemAlloc(sizeof(*pGui));
    if (!pGui)
        return VERR_NO_MEMORY;
    pGui->u32Magic = DBGGUI_MAGIC;
    pGui->pVBoxDbgGui = new VBoxDbgGui();

    int rc;
    if (pSession)
        rc = pGui->pVBoxDbgGui->init(pSession);
    else
        rc = pGui->pVBoxDbgGui->init(pUVM, pVMM);
    if (RT_SUCCESS(rc))
    {
        /*
         * Successfully initialized.
         */
        *ppGui = pGui;
        if (ppGuiVT)
            *ppGuiVT = &g_dbgGuiVT;
        return rc;
    }

    /*
     * Failed, cleanup.
     */
    delete pGui->pVBoxDbgGui;
    RTMemFree(pGui);
    *ppGui = NULL;
    if (ppGuiVT)
        *ppGuiVT = NULL;
    return rc;
}


/**
 * Creates the debugger GUI.
 *
 * @returns VBox status code.
 * @param   pSession    The VirtualBox session.
 * @param   ppGui       Where to store the pointer to the debugger instance.
 * @param   ppGuiVT     Where to store the virtual method table pointer.
 *                      Optional.
 */
DBGDECL(int) DBGGuiCreate(ISession *pSession, PDBGGUI *ppGui, PCDBGGUIVT *ppGuiVT)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    return dbgGuiCreate(pSession, NULL, NULL, ppGui, ppGuiVT);
}


/**
 * Creates the debugger GUI given a VM handle.
 *
 * @returns VBox status code.
 * @param   pUVM        The VM handle.
 * @param   pVMM        The VMM function table.
 * @param   ppGui       Where to store the pointer to the debugger instance.
 * @param   ppGuiVT     Where to store the virtual method table pointer.
 *                      Optional.
 */
DBGDECL(int) DBGGuiCreateForVM(PUVM pUVM, PCVMMR3VTABLE pVMM, PDBGGUI *ppGui, PCDBGGUIVT *ppGuiVT)
{
    AssertPtrReturn(pUVM, VERR_INVALID_POINTER);
    AssertPtrReturn(pVMM, VERR_INVALID_POINTER);
    AssertReturn(VMMR3VTABLE_IS_COMPATIBLE(pVMM->uMagicVersion), VERR_VERSION_MISMATCH);
    AssertReturn(pVMM->pfnVMR3RetainUVM(pUVM) != UINT32_MAX, VERR_INVALID_POINTER);

    int rc = dbgGuiCreate(NULL, pUVM, pVMM, ppGui, ppGuiVT);

    pVMM->pfnVMR3ReleaseUVM(pUVM);
    return rc;
}


/**
 * Destroys the debugger GUI.
 *
 * @returns VBox status code.
 * @param   pGui        The instance returned by DBGGuiCreate().
 */
DBGDECL(int) DBGGuiDestroy(PDBGGUI pGui)
{
    /*
     * Validate.
     */
    if (!pGui)
        return VERR_INVALID_PARAMETER;
    AssertMsgReturn(pGui->u32Magic == DBGGUI_MAGIC, ("u32Magic=%#x\n", pGui->u32Magic), VERR_INVALID_PARAMETER);

    /*
     * Do the job.
     */
    pGui->u32Magic = DBGGUI_MAGIC_DEAD;
    delete pGui->pVBoxDbgGui;
    RTMemFree(pGui);

    return VINF_SUCCESS;
}


/**
 * Notifies the debugger GUI that the console window (or whatever) has changed
 * size or position.
 *
 * @param   pGui        The instance returned by DBGGuiCreate().
 * @param   x           The x-coordinate of the window the debugger is relative to.
 * @param   y           The y-coordinate of the window the debugger is relative to.
 * @param   cx          The width of the window the debugger is relative to.
 * @param   cy          The height of the window the debugger is relative to.
 */
DBGDECL(void) DBGGuiAdjustRelativePos(PDBGGUI pGui, int x, int y, unsigned cx, unsigned cy)
{
    AssertReturn(pGui, (void)VERR_INVALID_PARAMETER);
    AssertMsgReturn(pGui->u32Magic == DBGGUI_MAGIC, ("u32Magic=%#x\n", pGui->u32Magic), (void)VERR_INVALID_PARAMETER);
    pGui->pVBoxDbgGui->adjustRelativePos(x, y, cx, cy);
}


/**
 * Shows the default statistics window.
 *
 * @returns VBox status code.
 * @param   pGui        The instance returned by DBGGuiCreate().
 * @param   pszFilter   Filter pattern.
 * @param   pszExpand   Expand pattern.
 * @param   pszConfig   Advanced filter configuration (min/max/regexp on
 *                      sub-trees) and more.
 */
DBGDECL(int) DBGGuiShowStatistics(PDBGGUI pGui, const char *pszFilter, const char *pszExpand, const char *pszConfig)
{
    AssertReturn(pGui, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pGui->u32Magic == DBGGUI_MAGIC, ("u32Magic=%#x\n", pGui->u32Magic), VERR_INVALID_PARAMETER);
    return pGui->pVBoxDbgGui->showStatistics(pszFilter, pszExpand, pszConfig);
}


/**
 * Shows the default command line window.
 *
 * @returns VBox status code.
 * @param   pGui        The instance returned by DBGGuiCreate().
 */
DBGDECL(int) DBGGuiShowCommandLine(PDBGGUI pGui)
{
    AssertReturn(pGui, VERR_INVALID_PARAMETER);
    AssertMsgReturn(pGui->u32Magic == DBGGUI_MAGIC, ("u32Magic=%#x\n", pGui->u32Magic), VERR_INVALID_PARAMETER);
    return pGui->pVBoxDbgGui->showConsole();
}


/**
 * Sets the parent windows.
 *
 * @param   pGui        The instance returned by DBGGuiCreate().
 * @param   pvParent    Pointer to a QWidget object.
 *
 * @remarks This will no affect any existing windows, so call it right after
 *          creating the thing.
 */
DBGDECL(void) DBGGuiSetParent(PDBGGUI pGui, void *pvParent)
{
    return pGui->pVBoxDbgGui->setParent((QWidget *)pvParent);
}


/**
 * Sets the debug menu object.
 *
 * @param   pGui        The instance returned by DBGGuiCreate().
 * @param   pvMenu      Pointer to a QMenu object.
 *
 * @remarks Call right after creation or risk losing menu item.
 */
DBGDECL(void) DBGGuiSetMenu(PDBGGUI pGui, void *pvMenu)
{
    return pGui->pVBoxDbgGui->setMenu((QMenu *)pvMenu);
}

