/* $Id: VBoxDbgBase.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGG
#include <iprt/errcore.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <limits.h>
#include "VBoxDbgBase.h"
#include "VBoxDbgGui.h"

#include <QApplication>
#include <QWidgetList>



VBoxDbgBase::VBoxDbgBase(VBoxDbgGui *a_pDbgGui)
    : m_pDbgGui(a_pDbgGui), m_pUVM(NULL), m_pVMM(NULL), m_hGUIThread(RTThreadNativeSelf())
{
    NOREF(m_pDbgGui); /* shut up warning. */

    /*
     * Register
     */
    m_pUVM = a_pDbgGui->getUvmHandle();
    m_pVMM = a_pDbgGui->getVMMFunctionTable();
    if (m_pUVM && m_pVMM)
    {
        m_pVMM->pfnVMR3RetainUVM(m_pUVM);

        int rc = m_pVMM->pfnVMR3AtStateRegister(m_pUVM, atStateChange, this);
        AssertRC(rc);
    }
}


VBoxDbgBase::~VBoxDbgBase()
{
    /*
     * If the VM is still around.
     */
    /** @todo need to do some locking here?  */
    PUVM          pUVM = ASMAtomicXchgPtrT(&m_pUVM, NULL, PUVM);
    PCVMMR3VTABLE pVMM = ASMAtomicXchgPtrT(&m_pVMM, NULL, PCVMMR3VTABLE);
    if (pUVM && pVMM)
    {
        int rc = pVMM->pfnVMR3AtStateDeregister(pUVM, atStateChange, this);
        AssertRC(rc);

        pVMM->pfnVMR3ReleaseUVM(pUVM);
    }
}


int
VBoxDbgBase::stamReset(const QString &rPat)
{
    QByteArray Utf8Array = rPat.toUtf8();
    const char *pszPat = !rPat.isEmpty() ? Utf8Array.constData() : NULL;
    PUVM          pUVM = m_pUVM;
    PCVMMR3VTABLE pVMM = m_pVMM;
    if (   pUVM
        && pVMM
        && pVMM->pfnVMR3GetStateU(pUVM) < VMSTATE_DESTROYING)
        return pVMM->pfnSTAMR3Reset(pUVM, pszPat);
    return VERR_INVALID_HANDLE;
}


int
VBoxDbgBase::stamEnum(const QString &rPat, PFNSTAMR3ENUM pfnEnum, void *pvUser)
{
    QByteArray Utf8Array = rPat.toUtf8();
    const char *pszPat = !rPat.isEmpty() ? Utf8Array.constData() : NULL;
    PUVM          pUVM = m_pUVM;
    PCVMMR3VTABLE pVMM = m_pVMM;
    if (   pUVM
        && pVMM
        && pVMM->pfnVMR3GetStateU(pUVM) < VMSTATE_DESTROYING)
        return pVMM->pfnSTAMR3Enum(pUVM, pszPat, pfnEnum, pvUser);
    return VERR_INVALID_HANDLE;
}


int
VBoxDbgBase::dbgcCreate(PCDBGCIO pIo, unsigned fFlags)
{
    PUVM          pUVM = m_pUVM;
    PCVMMR3VTABLE pVMM = m_pVMM;
    if (   pUVM
        && pVMM
        && pVMM->pfnVMR3GetStateU(pUVM) < VMSTATE_DESTROYING)
        return pVMM->pfnDBGCCreate(pUVM, pIo, fFlags);
    return VERR_INVALID_HANDLE;
}


/*static*/ DECLCALLBACK(void)
VBoxDbgBase::atStateChange(PUVM pUVM, PCVMMR3VTABLE pVMM, VMSTATE enmState, VMSTATE /*enmOldState*/, void *pvUser)
{
    VBoxDbgBase *pThis = (VBoxDbgBase *)pvUser; NOREF(pUVM);
    switch (enmState)
    {
        case VMSTATE_TERMINATED:
        {
            /** @todo need to do some locking here?  */
            PUVM          pUVM2 = ASMAtomicXchgPtrT(&pThis->m_pUVM, NULL, PUVM);
            PCVMMR3VTABLE pVMM2 = ASMAtomicXchgPtrT(&pThis->m_pVMM, NULL, PCVMMR3VTABLE);
            if (pUVM2 && pVMM2)
            {
                Assert(pUVM2 == pUVM);
                Assert(pVMM2 == pVMM);
                pThis->sigTerminated();
                pVMM->pfnVMR3ReleaseUVM(pUVM2);
            }
            break;
        }

        case VMSTATE_DESTROYING:
            pThis->sigDestroying();
            break;

        default:
            break;
    }
    RT_NOREF(pVMM);
}


void
VBoxDbgBase::sigDestroying()
{
}


void
VBoxDbgBase::sigTerminated()
{
}




//
//
//
//  V B o x D b g B a s e W i n d o w
//  V B o x D b g B a s e W i n d o w
//  V B o x D b g B a s e W i n d o w
//
//
//

unsigned VBoxDbgBaseWindow::m_cxBorder = 0;
unsigned VBoxDbgBaseWindow::m_cyBorder = 0;


VBoxDbgBaseWindow::VBoxDbgBaseWindow(VBoxDbgGui *a_pDbgGui, QWidget *a_pParent, const char *a_pszTitle)
    : QWidget(a_pParent, Qt::Window), VBoxDbgBase(a_pDbgGui), m_pszTitle(a_pszTitle), m_fPolished(false)
    , m_x(INT_MAX), m_y(INT_MAX), m_cx(0), m_cy(0), m_cxMinHint(0), m_enmAttraction(VBoxDbgBaseWindow::kAttractionVmNone)
{
    /* Set the title, using the parent one as prefix when possible: */
    if (!parent())
    {
        QString strMachineName = a_pDbgGui->getMachineName();
        if (strMachineName.isEmpty())
            setWindowTitle(QString("VBoxDbg - %1").arg(m_pszTitle));
        else
            setWindowTitle(QString("%1 - VBoxDbg - %2").arg(strMachineName).arg(m_pszTitle));
    }
    else
    {
        setWindowTitle(QString("%1 - %2").arg(parentWidget()->windowTitle()).arg(m_pszTitle));

        /* Install an event filter so we can make adjustments when the parent title changes: */
        parent()->installEventFilter(this);
    }
}


VBoxDbgBaseWindow::~VBoxDbgBaseWindow()
{

}


void
VBoxDbgBaseWindow::vShow()
{
    show();
    /** @todo this ain't working right. HELP! */
    setWindowState(windowState() & ~Qt::WindowMinimized);
    //activateWindow();
    //setFocus();
    vPolishSizeAndPos();
}


void
VBoxDbgBaseWindow::vReposition(int a_x, int a_y, unsigned a_cx, unsigned a_cy, bool a_fResize)
{
    /* Don't modify if maximized.
       We miss the desired size + position here, but never mind for now. */
    if (!(windowState() & Qt::WindowMaximized))
    {
        if (a_fResize)
        {
            m_cx = a_cx;
            m_cy = a_cy;

            QSize const BorderSize = vGetBorderSize();
            resize(a_cx - BorderSize.width(), a_cy - BorderSize.height());
        }

        m_x = a_x;
        m_y = a_y;
        move(a_x, a_y);
    }
}


QSize
VBoxDbgBaseWindow::vGetBorderSize()
{
    QSize BorderSize = frameSize() - size();

#ifdef Q_WS_X11 /* (from the qt gui) */
    /*
     * On X11, there is no way to determine frame geometry (including WM
     * decorations) before the widget is shown for the first time.  Stupidly
     * enumerate other top level widgets to find the thickest frame.
     */
    if (BorderSize == QSize(0, 0))
    {
        if (!m_cxBorder && !m_cyBorder) /* (only till we're successful) */
        {
            int cxExtra = 0;
            int cyExtra = 0;

            QWidgetList WidgetList = QApplication::topLevelWidgets();
            for (QListIterator<QWidget *> it(WidgetList); it.hasNext(); )
            {
                QWidget *pCurWidget = it.next();
                if (pCurWidget->isVisible())
                {
                    int const cxFrame = pCurWidget->frameGeometry().width()  - pCurWidget->width();
                    cxExtra = qMax(cxExtra, cxFrame);
                    int const cyFrame = pCurWidget->frameGeometry().height() - pCurWidget->height();
                    cyExtra = qMax(cyExtra, cyFrame);
                    if (cyExtra && cxExtra)
                        break;
                }
            }

            if (cxExtra || cyExtra)
            {
                m_cxBorder = cxExtra;
                m_cyBorder = cyExtra;
            }
        }
        BorderSize.setWidth(m_cxBorder);
        BorderSize.setHeight(m_cyBorder);
    }
#endif /* X11 */

    return BorderSize;
}


bool
VBoxDbgBaseWindow::event(QEvent *a_pEvt)
{
    bool fRc = QWidget::event(a_pEvt);
    if (   a_pEvt->type() == QEvent::Paint
        || a_pEvt->type() == QEvent::UpdateRequest
        || a_pEvt->type() == QEvent::LayoutRequest) /** @todo Someone with Qt knowledge should figure out how to properly do this. */
        vPolishSizeAndPos();
    return fRc;
}


bool VBoxDbgBaseWindow::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* We're only interested in title changes to the parent so we can amend our own title: */
    if (   pWatched == parent()
        && pEvent->type() == QEvent::WindowTitleChange)
        setWindowTitle(QString("%1 - %2").arg(parentWidget()->windowTitle()).arg(m_pszTitle));

    /* Forward to base-class: */
    return QWidget::eventFilter(pWatched, pEvent);
}


void
VBoxDbgBaseWindow::vPolishSizeAndPos()
{
    /* Ignore if already done or no size set. */
    if (    m_fPolished
        || (m_x == INT_MAX && m_y == INT_MAX))
        return;

    if (VBoxDbgBaseWindow::vGetBorderSize() != QSize(0,0))
        m_fPolished = true;

    vReposition(m_x, m_y, m_cx, m_cy, m_cx || m_cy);
}

