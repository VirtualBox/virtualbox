/* $Id: VBoxDbgBase.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Debugger GUI - Base classes.
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

#ifndef DEBUGGER_INCLUDED_SRC_VBoxDbgBase_h
#define DEBUGGER_INCLUDED_SRC_VBoxDbgBase_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#include <VBox/vmm/stam.h>
#include <VBox/vmm/vmapi.h>
#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/dbg.h>
#include <iprt/thread.h>
#include <QString>
#include <QWidget>

class VBoxDbgGui;


/**
 * VBox Debugger GUI Base Class.
 *
 * The purpose of this class is to hide the VM handle, abstract VM
 * operations, and finally to make sure the GUI won't crash when
 * the VM dies.
 */
class VBoxDbgBase
{
public:
    /**
     * Construct the object.
     *
     * @param   a_pDbgGui   Pointer to the debugger gui object.
     */
    VBoxDbgBase(VBoxDbgGui *a_pDbgGui);

    /**
     * Destructor.
     */
    virtual ~VBoxDbgBase();


    /**
     * Checks if the VM is OK for normal operations.
     * @returns true if ok, false if not.
     */
    bool isVMOk() const
    {
        return m_pUVM != NULL;
    }

    /**
     * Checks if the current thread is the GUI thread or not.
     * @return true/false accordingly.
     */
    bool isGUIThread() const
    {
        return m_hGUIThread == RTThreadNativeSelf();
    }

    /** @name Operations
     * @{ */
    /**
     * Wrapper for STAMR3Reset().
     */
    int stamReset(const QString &rPat);
    /**
     * Wrapper for STAMR3Enum().
     */
    int stamEnum(const QString &rPat, PFNSTAMR3ENUM pfnEnum, void *pvUser);
    /**
     * Wrapper for DBGCCreate().
     */
    int dbgcCreate(PCDBGCIO pIo, unsigned fFlags);
    /** @} */


protected:
    /** @name Signals
     * @{ */
    /**
     * Called when the VM is being destroyed.
     */
    virtual void sigDestroying();
    /**
     * Called when the VM has been terminated.
     */
    virtual void sigTerminated();
    /** @} */


private:
    /** @callback_method_impl{FNVMATSTATE}  */
    static DECLCALLBACK(void) atStateChange(PUVM pUVM, PCVMMR3VTABLE pVMM, VMSTATE enmState, VMSTATE enmOldState, void *pvUser);

private:
    /** Pointer to the debugger GUI object. */
    VBoxDbgGui *m_pDbgGui;
    /** The user mode VM handle. */
    PUVM volatile m_pUVM;
    /** The VMM function table. */
    PCVMMR3VTABLE volatile m_pVMM;
    /** The handle of the GUI thread. */
    RTNATIVETHREAD m_hGUIThread;
};


/**
 * VBox Debugger GUI Base Window Class.
 *
 * This is just a combination of QWidget and VBoxDbgBase with some additional
 * functionality for window management. This class is not intended for control
 * widgets, only normal top-level windows.
 */
class VBoxDbgBaseWindow : public QWidget, public VBoxDbgBase
{
public:
    /**
     * Construct the object.
     *
     * @param   a_pDbgGui   Pointer to the debugger gui object.
     * @param   a_pParent   Pointer to the parent object.
     * @param   a_pszTitle  The window title string (persistent, not copied).
     */
    VBoxDbgBaseWindow(VBoxDbgGui *a_pDbgGui, QWidget *a_pParent, const char *a_pszTitle);

    /**
     * Destructor.
     */
    virtual ~VBoxDbgBaseWindow();

    /**
     * Shows the window and gives it focus.
     */
    void vShow();

    /**
     * Repositions the window, taking the frame decoration into account.
     *
     * @param   a_x         The new x coordinate.
     * @param   a_y         The new x coordinate.
     * @param   a_cx        The total width.
     * @param   a_cy        The total height.
     * @param   a_fResize   Whether to resize it as well.
     */
    void vReposition(int a_x, int a_y, unsigned a_cx, unsigned a_cy, bool a_fResize);

    /**
     * Gets the minimum client area width hint.
     */
    unsigned vGetMinWidthHint(void) const RT_NOEXCEPT
    {
        return m_cxMinHint;
    }

protected:
    /**
     * Sets the minimum client area width hint (without frame).
     */
    void vSetMinWidthHint(unsigned a_cxMin)
    {
        m_cxMinHint = a_cxMin;
    }


public:
    /** VM window magnetic attraction. */
    typedef enum
    { kAttractionVmLeft, kAttractionVmRight, kAttractionVmTop, kAttractionVmBottom, kAttractionVmNone } VBoxDbgAttractionType;

    /**
     * Gets the window attraction.
     */
    VBoxDbgAttractionType vGetWindowAttraction(void) const RT_NOEXCEPT
    {
        return m_enmAttraction;
    }

    /**
     * Sets the window attraction.
     */
    void vSetWindowAttraction(VBoxDbgAttractionType a_enmAttraction) RT_NOEXCEPT
    {
        m_enmAttraction = a_enmAttraction;
    }

    /**
     * Get the border size for the window.
     *
     * This may return different values after the window has been shown,
     * at least on X11.
     */
    QSize vGetBorderSize();

protected:
    /**
     * For polishing the window size (X11 mess).
     *
     * @returns true / false.
     * @param   a_pEvt       The event.
     */
    virtual bool event(QEvent *a_pEvt);

    /**
     * Event filter for various purposes (mainly title bar).
     *
     * @param  pWatched         The object event came to.
     * @param  pEvent           The event being handled.
     */
    virtual bool eventFilter(QObject *pWatched, QEvent *pEvent);

    /**
     * Internal worker for polishing the size and position (X11 hacks).
     */
    void vPolishSizeAndPos();

private:
    /** The Window title string (inflexible, read only). */
    const char *m_pszTitle;
    /** Whether we've done the size polishing in showEvent or not. */
    bool m_fPolished;
    /** The desired x coordinate. */
    int m_x;
    /** The desired y coordinate. */
    int m_y;
    /** The desired width. */
    unsigned m_cx;
    /** The desired height. */
    unsigned m_cy;
    /** Minimum client area width hint (for the console window, etc). */
    unsigned m_cxMinHint;
    /** The window attraction. */
    VBoxDbgAttractionType m_enmAttraction;

    /** Best effort x border size (for X11). */
    static unsigned m_cxBorder;
    /** Best effort y border size (for X11). */
    static unsigned m_cyBorder;
};

#endif /* !DEBUGGER_INCLUDED_SRC_VBoxDbgBase_h */

