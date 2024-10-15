/* $Id: VBoxDbgGui.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Debugger GUI - The Manager.
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

#ifndef DEBUGGER_INCLUDED_SRC_VBoxDbgGui_h
#define DEBUGGER_INCLUDED_SRC_VBoxDbgGui_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

// VirtualBox COM interfaces declarations (generated header)
#ifdef VBOX_WITH_XPCOM
# include <VirtualBox_XPCOM.h>
#else
# include <iprt/win/windows.h> /* Include via cleanup wrapper before VirtualBox.h includes it via rpc.h. */
# include <VirtualBox.h>
#endif

#include "VBoxDbgStatsQt.h"
#include "VBoxDbgConsole.h"


/**
 * The Debugger GUI manager class.
 *
 * It's job is to provide a C callable external interface and manage the
 * windows and bit making up the debugger GUI.
 */
class VBoxDbgGui : public QObject
{
    Q_OBJECT;

public:
    /**
     * Create a default VBoxDbgGui object.
     */
    VBoxDbgGui();

    /**
     * Initializes a VBoxDbgGui object by ISession.
     *
     * @returns VBox status code.
     * @param   pSession    VBox Session object.
     */
    int init(ISession *pSession);

    /**
     * Initializes a VBoxDbgGui object by VM handle.
     *
     * @returns VBox status code.
     * @param   pUVM        The user mode VM handle. The caller's reference will be
     *                      consumed on success.
     * @param   pVMM        The VMM function table.
     */
    int init(PUVM pUVM, PCVMMR3VTABLE pVMM);

    /**
     * Destroys the VBoxDbgGui object.
     */
    virtual ~VBoxDbgGui();

    /**
     * Sets the parent widget.
     *
     * @param   pParent     New parent widget.
     * @remarks This only affects new windows.
     */
    void setParent(QWidget *pParent);

    /**
     * Sets the menu object.
     *
     * @param   pMenu       New menu object.
     * @remarks This only affects new menu additions.
     */
    void setMenu(QMenu *pMenu);

    /**
     * Show the default statistics window, creating it if necessary.
     *
     * @returns VBox status code.
     * @param   pszFilter   Filter pattern.
     * @param   pszExpand   Expand pattern.
     * @param   pszConfig   Advanced filter configuration (min/max/regexp on
     *                      sub-trees) and more.
     */
    int showStatistics(const char *pszFilter, const char *pszExpand, const char *pszConfig);

    /**
     * Show the console window (aka. command line), creating it if necessary.
     *
     * @returns VBox status code.
     */
    int showConsole();

    /**
     * Update the desktop size.
     * This is called whenever the reference window changes position.
     */
    void updateDesktopSize();

    /**
     * Repositions and maybe resizes a window according to the VM window.
     *
     * @param   a_pWindow   The window.  Ignored if NULL.
     * @param   a_fResize   If set (default) the size of window is also changed.
     */
    void repositionWindow(VBoxDbgBaseWindow *a_pWindow, bool a_fResize = true);

    /**
     * Does the initial repositioning and sizing of a window.
     *
     * This may get preferences from extra data.
     *
     * @param   a_pWindow               The window.
     * @param   a_pszSettings           The 'section' name in the settings.
     * @param   a_enmDefaultAttraction  The default attraction of the window.
     */
    void repositionWindowInitial(VBoxDbgBaseWindow *a_pWindow, const char *a_pszSettings,
                                 VBoxDbgBaseWindow::VBoxDbgAttractionType a_enmDefaultAttraction);

    /**
     * Notifies the debugger GUI that the console window (or whatever) has changed
     * size or position.
     *
     * @param   x           The x-coordinate of the window the debugger is relative to.
     * @param   y           The y-coordinate of the window the debugger is relative to.
     * @param   cx          The width of the window the debugger is relative to.
     * @param   cy          The height of the window the debugger is relative to.
     */
    void adjustRelativePos(int x, int y, unsigned cx, unsigned cy);

    /**
     * Gets the user mode VM handle.
     * @returns The UVM handle.
     */
    PUVM getUvmHandle() const
    {
        return m_pUVM;
    }

    /**
     * Gets the VMM function table.
     * @returns The VMM function table.
     */
    PCVMMR3VTABLE getVMMFunctionTable() const
    {
        return m_pVMM;
    }

    /**
     * @returns The name of the machine.
     */
    QString getMachineName() const;

protected slots:
    /**
     * Notify that a child object (i.e. a window is begin destroyed).
     * @param   pObj    The object which is being destroyed.
     */
    void notifyChildDestroyed(QObject *pObj);

protected:

    /** The debugger statistics. */
    VBoxDbgStats *m_pDbgStats;
    /** The debugger console (aka. command line). */
    VBoxDbgConsole *m_pDbgConsole;

    /** The VirtualBox session. */
    ISession *m_pSession;
    /** The VirtualBox console. */
    IConsole *m_pConsole;
    /** The VirtualBox Machine Debugger. */
    IMachineDebugger *m_pMachineDebugger;
    /** The VirtualBox Machine. */
    IMachine *m_pMachine;
    /** The VM instance. */
    PVM m_pVM;
    /** The user mode VM handle. */
    PUVM m_pUVM;
    /** The VMM function table. */
    PCVMMR3VTABLE m_pVMM;

    /** The parent widget. */
    QWidget *m_pParent;
    /** The menu object for the 'debug' menu. */
    QMenu *m_pMenu;

    /** The x-coordinate of the window we're relative to. */
    int m_x;
    /** The y-coordinate of the window we're relative to. */
    int m_y;
    /** The width of the window we're relative to. */
    unsigned m_cx;
    /** The height of the window we're relative to. */
    unsigned m_cy;
    /** The x-coordinate of the desktop. */
    int m_xDesktop;
    /** The y-coordinate of the desktop. */
    int m_yDesktop;
    /** The size of the desktop. */
    unsigned m_cxDesktop;
    /** The size of the desktop. */
    unsigned m_cyDesktop;
};


#endif /* !DEBUGGER_INCLUDED_SRC_VBoxDbgGui_h */

