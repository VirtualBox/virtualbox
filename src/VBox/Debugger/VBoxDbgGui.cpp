/* $Id: VBoxDbgGui.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGG
#define VBOX_COM_NO_ATL
#include <VBox/com/defs.h>
#include <iprt/errcore.h>

#include "VBoxDbgGui.h"
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
# include <QScreen>
#else
# include <QDesktopWidget>
#endif
#include <QApplication>



VBoxDbgGui::VBoxDbgGui() :
    m_pDbgStats(NULL), m_pDbgConsole(NULL), m_pSession(NULL), m_pConsole(NULL),
    m_pMachineDebugger(NULL), m_pMachine(NULL), m_pVM(NULL), m_pUVM(NULL), m_pVMM(NULL),
    m_pParent(NULL), m_pMenu(NULL),
    m_x(0), m_y(0), m_cx(0), m_cy(0), m_xDesktop(0), m_yDesktop(0), m_cxDesktop(0), m_cyDesktop(0)
{

}


int VBoxDbgGui::init(PUVM pUVM, PCVMMR3VTABLE pVMM)
{
    /*
     * Set the VM handle and update the desktop size.
     */
    m_pUVM = pUVM; /* Note! This eats the incoming reference to the handle! */
    m_pVMM = pVMM;
    updateDesktopSize();

    return VINF_SUCCESS;
}


int VBoxDbgGui::init(ISession *pSession)
{
    int rc = VERR_GENERAL_FAILURE;

    /*
     * Query the VirtualBox interfaces.
     */
    m_pSession = pSession;
    m_pSession->AddRef();

    HRESULT hrc = m_pSession->COMGETTER(Machine)(&m_pMachine);
    if (SUCCEEDED(hrc))
    {
        hrc = m_pSession->COMGETTER(Console)(&m_pConsole);
        if (SUCCEEDED(hrc))
        {
            hrc = m_pConsole->COMGETTER(Debugger)(&m_pMachineDebugger);
            if (SUCCEEDED(hrc))
            {
                /*
                 * Get the VM handle.
                 */
                LONG64 llUVM = 0;
                LONG64 llVMMFunctionTable = 0;
                hrc = m_pMachineDebugger->GetUVMAndVMMFunctionTable((int64_t)VMMR3VTABLE_MAGIC_VERSION,
                                                                    &llVMMFunctionTable, &llUVM);
                if (SUCCEEDED(hrc))
                {
                    PUVM          pUVM = (PUVM)(intptr_t)llUVM;
                    PCVMMR3VTABLE pVMM = (PCVMMR3VTABLE)(intptr_t)llVMMFunctionTable;
                    rc = init(pUVM, pVMM);
                    if (RT_SUCCESS(rc))
                        return rc;

                    pVMM->pfnVMR3ReleaseUVM(pUVM);
                }

                /* damn, failure! */
                m_pMachineDebugger->Release();
                m_pMachineDebugger = NULL;
            }
            m_pConsole->Release();
            m_pConsole = NULL;
        }
        m_pMachine->Release();
        m_pMachine = NULL;
    }

    return rc;
}


VBoxDbgGui::~VBoxDbgGui()
{
    if (m_pDbgStats)
    {
        delete m_pDbgStats;
        m_pDbgStats = NULL;
    }

    if (m_pDbgConsole)
    {
        delete m_pDbgConsole;
        m_pDbgConsole = NULL;
    }

    if (m_pMachineDebugger)
    {
        m_pMachineDebugger->Release();
        m_pMachineDebugger = NULL;
    }

    if (m_pConsole)
    {
        m_pConsole->Release();
        m_pConsole = NULL;
    }

    if (m_pMachine)
    {
        m_pMachine->Release();
        m_pMachine = NULL;
    }

    if (m_pSession)
    {
        m_pSession->Release();
        m_pSession = NULL;
    }

    if (m_pUVM)
    {
        Assert(m_pVMM);
        m_pVMM->pfnVMR3ReleaseUVM(m_pUVM);
        m_pUVM = NULL;
        m_pVMM = NULL;
    }
}

void
VBoxDbgGui::setParent(QWidget *pParent)
{
    m_pParent = pParent;
}


void
VBoxDbgGui::setMenu(QMenu *pMenu)
{
    m_pMenu = pMenu;
}


int
VBoxDbgGui::showStatistics(const char *pszFilter, const char *pszExpand, const char *pszConfig)
{
    if (!m_pDbgStats)
    {
        m_pDbgStats = new VBoxDbgStats(this,
                                       pszFilter && *pszFilter ? pszFilter :  "*",
                                       pszExpand && *pszExpand ? pszExpand : NULL,
                                       pszConfig && *pszConfig ? pszConfig : NULL,
                                       2, m_pParent);
        connect(m_pDbgStats, SIGNAL(destroyed(QObject *)), this, SLOT(notifyChildDestroyed(QObject *)));
        repositionWindowInitial(m_pDbgStats, "DbgStats", VBoxDbgBaseWindow::kAttractionVmRight);
    }

    m_pDbgStats->vShow();
    return VINF_SUCCESS;
}


int
VBoxDbgGui::showConsole()
{
    if (!m_pDbgConsole)
    {
        IVirtualBox *pVirtualBox = NULL;
        m_pMachine->COMGETTER(Parent)(&pVirtualBox);
        m_pDbgConsole = new VBoxDbgConsole(this, m_pParent, pVirtualBox);
        connect(m_pDbgConsole, SIGNAL(destroyed(QObject *)), this, SLOT(notifyChildDestroyed(QObject *)));
        repositionWindowInitial(m_pDbgConsole, "DbgConsole", VBoxDbgBaseWindow::kAttractionVmBottom);
    }

    m_pDbgConsole->vShow();
    return VINF_SUCCESS;
}


void
VBoxDbgGui::updateDesktopSize()
{
    QRect Rct(0, 0, 1600, 1200);
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QScreen *pScreen = QApplication::screenAt(QPoint(m_x, m_y));
    if (pScreen)
        Rct = pScreen->availableGeometry();
#else
    QDesktopWidget *pDesktop = QApplication::desktop();
    if (pDesktop)
        Rct = pDesktop->availableGeometry(QPoint(m_x, m_y));
#endif
    m_xDesktop = Rct.x();
    m_yDesktop = Rct.y();
    m_cxDesktop = Rct.width();
    m_cyDesktop = Rct.height();
}


void
VBoxDbgGui::repositionWindow(VBoxDbgBaseWindow *a_pWindow, bool a_fResize /*=true*/)
{
    if (a_pWindow)
    {
        VBoxDbgBaseWindow::VBoxDbgAttractionType const enmAttraction = a_pWindow->vGetWindowAttraction();
        QSize const     BorderSize = a_pWindow->vGetBorderSize();
        unsigned const  cxMinHint  = RT_MAX(32, a_pWindow->vGetMinWidthHint()) + BorderSize.width();
        int             x,  y;
        unsigned        cx, cy;
        /** @todo take the x,y screen into account rather than the one of the VM
         *        window. also generally consider adjacent screens. */
        switch (enmAttraction)
        {
            case VBoxDbgBaseWindow::kAttractionVmRight:
                x  = m_x + m_cx;
                y  = m_y;
                cx = m_cxDesktop - m_cx - m_x + m_xDesktop;
                if (cx > m_cxDesktop || cx < cxMinHint)
                    cx = cxMinHint;
                cy = m_cyDesktop - m_y + m_yDesktop;
                break;

            case VBoxDbgBaseWindow::kAttractionVmBottom:
                x  = m_x;
                y  = m_y + m_cy;
                cx = m_cx;
                if (cx < cxMinHint)
                {
                    if (cxMinHint - cx <= unsigned(m_x - m_xDesktop)) /* move it to the left if we have sufficient room. */
                        x -= cxMinHint - cx;
                    else
                        x = m_xDesktop;
                    cx = cxMinHint;
                }
                cy = m_cyDesktop - m_cy - m_y + m_yDesktop;
                break;

            /** @todo implement the other placements when they become selectable. */

            default:
                return;
        }

        a_pWindow->vReposition(x, y, cx, cy, a_fResize);
    }
}


void
VBoxDbgGui::repositionWindowInitial(VBoxDbgBaseWindow *a_pWindow, const char *a_pszSettings,
                                    VBoxDbgBaseWindow::VBoxDbgAttractionType a_enmDefaultAttraction)
{
    a_pWindow->vSetWindowAttraction(a_enmDefaultAttraction);

    /** @todo save/restore the attachment type. */
    RT_NOREF(a_pszSettings);

    repositionWindow(a_pWindow, true /*fResize*/);
}


void
VBoxDbgGui::adjustRelativePos(int x, int y, unsigned cx, unsigned cy)
{
    /* Disregard a width less than 640 since it will mess up the console,
       but only if previous width was already initialized. */
    if (cx < 640 && m_cx > 0)
        cx = m_cx;

    const bool fResize = cx != m_cx || cy != m_cy;
    const bool fMoved  = x  != m_x  || y  != m_y;

    m_x = x;
    m_y = y;
    m_cx = cx;
    m_cy = cy;

    if (fMoved)
        updateDesktopSize();
    repositionWindow(m_pDbgConsole, fResize);
    repositionWindow(m_pDbgStats, fResize);
}


QString
VBoxDbgGui::getMachineName() const
{
    QString strName;
    AssertReturn(m_pMachine, strName);
    BSTR bstr;
    HRESULT hrc = m_pMachine->COMGETTER(Name)(&bstr);
    if (SUCCEEDED(hrc))
    {
        strName = QString::fromUtf16((const char16_t *)bstr);
        SysFreeString(bstr);
    }
    return strName;
}


void
VBoxDbgGui::notifyChildDestroyed(QObject *pObj)
{
    if (m_pDbgStats == pObj)
        m_pDbgStats = NULL;
    else if (m_pDbgConsole == pObj)
        m_pDbgConsole = NULL;
}

