/* $Id: VBoxDbgConsole.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Debugger GUI - Console.
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
#include "VBoxDbgConsole.h"
#include "VBoxDbgGui.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>

#include <VBox/dbg.h>
#include <VBox/vmm/cfgm.h>
#include <iprt/errcore.h>

#include <iprt/thread.h>
#include <iprt/tcp.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/alloc.h>
#include <iprt/string.h>

#include <VBox/com/string.h>



/*
 *
 *          V B o x D b g C o n s o l e O u t p u t
 *          V B o x D b g C o n s o l e O u t p u t
 *          V B o x D b g C o n s o l e O u t p u t
 *
 *
 */

/*static*/ const uint32_t VBoxDbgConsoleOutput::s_uMinFontSize = 6;


VBoxDbgConsoleOutput::VBoxDbgConsoleOutput(QWidget *pParent/* = NULL*/, IVirtualBox *a_pVirtualBox /* = NULL */,
                                           const char *pszName/* = NULL*/)
#ifdef VBOXDBG_WITH_CONSOLE_OUTPUT_AS_QPLAINTEXT
    : QPlainTextEdit(pParent)
#else
    : QTextEdit(pParent)
#endif
    , m_uCurLine(0)
    , m_uCurPos(0)
    , m_hGUIThread(RTThreadNativeSelf())
    , m_pVirtualBox(a_pVirtualBox)
{
    setReadOnly(true);
    setUndoRedoEnabled(false);
    setOverwriteMode(false);
    setPlainText("");
    setTextInteractionFlags(Qt::TextBrowserInteraction);
    setTabChangesFocus(true);
#ifndef VBOXDBG_WITH_CONSOLE_OUTPUT_AS_QPLAINTEXT
    setAutoFormatting(QTextEdit::AutoAll);
    setAcceptRichText(false);
#endif
#if 0 /* @bugref{10604}: this doesn't really help with the performance issue. */
    setWordWrapMode(QTextOption::NoWrap);
# ifndef VBOXDBG_WITH_CONSOLE_OUTPUT_AS_QPLAINTEXT
    setLineWrapMode(QPlainTextEdit::NoWrap);
# endif
#endif

    /*
     * Create actions for color-scheme menu items.
     */
    m_pGreenOnBlackAction = new QAction(tr("Green On Black"), this);
    m_pGreenOnBlackAction->setCheckable(true);
    m_pGreenOnBlackAction->setShortcut(QString("Ctrl+1"));
    m_pGreenOnBlackAction->setData((int)kGreenOnBlack);
    connect(m_pGreenOnBlackAction, SIGNAL(triggered()), this, SLOT(sltSelectColorScheme()));

    m_pBlackOnWhiteAction = new QAction(tr("Black On White"), this);
    m_pBlackOnWhiteAction->setCheckable(true);
    m_pBlackOnWhiteAction->setShortcut(QString("Ctrl+2"));
    m_pBlackOnWhiteAction->setData((int)kBlackOnWhite);
    connect(m_pBlackOnWhiteAction, SIGNAL(triggered()), this, SLOT(sltSelectColorScheme()));

    /* Create action group for grouping of exclusive color-scheme menu items. */
    QActionGroup *pActionColorGroup = new QActionGroup(this);
    pActionColorGroup->addAction(m_pGreenOnBlackAction);
    pActionColorGroup->addAction(m_pBlackOnWhiteAction);
    pActionColorGroup->setExclusive(true);

    /*
     * Create actions for font menu items.
     */
    m_pDefaultFontAction = new QAction(tr("Default Fixed Font"), this);
    m_pDefaultFontAction->setCheckable(true);
    m_pDefaultFontAction->setData((int)kFontType_SystemDefault);
    connect(m_pDefaultFontAction, SIGNAL(triggered()), this, SLOT(sltSelectFontType()));

    m_pCourierFontAction = new QAction(tr("Courier"), this);
    m_pCourierFontAction->setCheckable(true);
    m_pCourierFontAction->setData((int)kFontType_Courier);
    connect(m_pCourierFontAction, SIGNAL(triggered()), this, SLOT(sltSelectFontType()));

    m_pMonospaceFontAction = new QAction(tr("Monospace"), this);
    m_pMonospaceFontAction->setCheckable(true);
    m_pMonospaceFontAction->setData((int)kFontType_Monospace);
    connect(m_pMonospaceFontAction, SIGNAL(triggered()), this, SLOT(sltSelectFontType()));

    /* Create action group for grouping of exclusive font menu items. */
    QActionGroup *pActionFontGroup = new QActionGroup(this);
    pActionFontGroup->addAction(m_pDefaultFontAction);
    pActionFontGroup->addAction(m_pCourierFontAction);
    pActionFontGroup->addAction(m_pMonospaceFontAction);
    pActionFontGroup->setExclusive(true);

    /*
     * Create actions for font size menu.
     */
    uint32_t const uDefaultFontSize = font().pointSize();
    m_pActionFontSizeGroup = new QActionGroup(this);
    for (uint32_t i = 0; i < RT_ELEMENTS(m_apFontSizeActions); i++)
    {
        char szTitle[32];
        RTStrPrintf(szTitle, sizeof(szTitle), s_uMinFontSize + i != uDefaultFontSize ? "%upt" : "%upt (default)",
                    s_uMinFontSize + i);
        m_apFontSizeActions[i] = new QAction(tr(szTitle), this);
        m_apFontSizeActions[i]->setCheckable(true);
        m_apFontSizeActions[i]->setData(i + s_uMinFontSize);
        connect(m_apFontSizeActions[i], SIGNAL(triggered()), this, SLOT(sltSelectFontSize()));
        m_pActionFontSizeGroup->addAction(m_apFontSizeActions[i]);
    }

    /*
     * Set the defaults (which syncs with the menu item checked state).
     */
    /* color scheme: */
    setColorScheme(kGreenOnBlack, false /*fSaveIt*/);
    com::Bstr bstrColor;
    HRESULT hrc = m_pVirtualBox ? m_pVirtualBox->GetExtraData(com::Bstr("DbgConsole/ColorScheme").raw(), bstrColor.asOutParam()) : E_FAIL;
    if (SUCCEEDED(hrc))
    {
        if (bstrColor.compareUtf8("blackonwhite", com::Bstr::CaseInsensitive) == 0)
            setColorScheme(kBlackOnWhite, false /*fSaveIt*/);
    }

    /* font: */
    setFontType(kFontType_SystemDefault, false /*fSaveIt*/);
    com::Bstr bstrFont;
    hrc = m_pVirtualBox ? m_pVirtualBox->GetExtraData(com::Bstr("DbgConsole/Font").raw(), bstrFont.asOutParam()) : E_FAIL;
    if (SUCCEEDED(hrc))
    {
        if (bstrFont.compareUtf8("monospace", com::Bstr::CaseInsensitive) == 0)
            setFontType(kFontType_Monospace, false /*fSaveIt*/);
        else if (bstrFont.compareUtf8("courier", com::Bstr::CaseInsensitive) == 0)
            setFontType(kFontType_Courier, false /*fSaveIt*/);
    }

    /* font size: */
    com::Bstr bstrFontSize;
    hrc = m_pVirtualBox ? m_pVirtualBox->GetExtraData(com::Bstr("DbgConsole/FontSize").raw(), bstrFontSize.asOutParam()) : E_FAIL;
    if (SUCCEEDED(hrc))
    {
        com::Utf8Str strFontSize(bstrFontSize);
        uint32_t uFontSizePrf = strFontSize.strip().toUInt32();
        if (   uFontSizePrf - s_uMinFontSize < (uint32_t)RT_ELEMENTS(m_apFontSizeActions)
            && uFontSizePrf != uDefaultFontSize)
            setFontSize(uFontSizePrf, false /*fSaveIt*/);
    }

    NOREF(pszName);
}


VBoxDbgConsoleOutput::~VBoxDbgConsoleOutput()
{
    Assert(m_hGUIThread == RTThreadNativeSelf());
    if (m_pVirtualBox)
    {
        m_pVirtualBox->Release();
        m_pVirtualBox = NULL;
    }
}


void
VBoxDbgConsoleOutput::contextMenuEvent(QContextMenuEvent *pEvent)
{
    /*
     * Create the context menu and add the menu items.
     */
    QMenu *pMenu = createStandardContextMenu();
    pMenu->addSeparator();

    QMenu *pColorMenu = pMenu->addMenu(tr("Co&lor Scheme"));
    pColorMenu->addAction(m_pGreenOnBlackAction);
    pColorMenu->addAction(m_pBlackOnWhiteAction);

    QMenu *pFontMenu = pMenu->addMenu(tr("&Font Family"));
    pFontMenu->addAction(m_pDefaultFontAction);
    pFontMenu->addAction(m_pCourierFontAction);
    pFontMenu->addAction(m_pMonospaceFontAction);

    QMenu *pFontSize = pMenu->addMenu(tr("Font &Size"));
    for (unsigned i = 0; i < RT_ELEMENTS(m_apFontSizeActions); i++)
        pFontSize->addAction(m_apFontSizeActions[i]);

    pMenu->exec(pEvent->globalPos());
    delete pMenu;
}


void
VBoxDbgConsoleOutput::setColorScheme(VBoxDbgConsoleColor enmScheme, bool fSaveIt)
{
    const char *pszSetting;
    QAction *pAction;
    switch (enmScheme)
    {
        case kGreenOnBlack:
            setStyleSheet("QTextEdit { background-color: black; color: rgb(0, 224, 0) }");
            pszSetting = "GreenOnBlack";
            pAction = m_pGreenOnBlackAction;
            break;
        case kBlackOnWhite:
            setStyleSheet("QTextEdit { background-color: white; color: black }");
            pszSetting = "BlackOnWhite";
            pAction = m_pBlackOnWhiteAction;
            break;
        default:
            AssertFailedReturnVoid();
    }

    m_enmColorScheme = kGreenOnBlack;

    /* When going through a slot, the action is typically checked already by Qt. */
    if (!pAction->isChecked())
        pAction->setChecked(true);

    /* Make this setting persistent. */
    if (m_pVirtualBox && fSaveIt)
        m_pVirtualBox->SetExtraData(com::Bstr("DbgConsole/ColorScheme").raw(), com::Bstr(pszSetting).raw());
}


void
VBoxDbgConsoleOutput::setFontType(VBoxDbgConsoleFontType enmFontType, bool fSaveIt)
{
    QFont Font = font();
    QAction *pAction;
    const char *pszSetting;
    switch (enmFontType)
    {
        case kFontType_SystemDefault:
            Font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            pszSetting = "Default";
            pAction = m_pDefaultFontAction;
            break;

        case kFontType_Courier:
        {
#ifdef Q_WS_MAC
            Font = QFont("Monaco", Font.pointSize(), QFont::Normal, FALSE);
            Font.setStyleStrategy(QFont::NoAntialias);
#else
            Font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            Font.setStyleHint(QFont::TypeWriter);
            Font.setStyleStrategy(QFont::PreferAntialias);
            QStringList Families;
# ifdef Q_WS_MAC
            Families << "Monaco";
# endif
            /*Families << "Courier New [Monotype]"; - this causes peformance trouble (@bugref{10604}) */
            Families << "Courier New";
            /*Families << "Courier [Monotype]"; - this causes peformance trouble (@bugref{10604}) */
            Families << "Courier";
            Font.setFamilies(Families);
#endif
            pszSetting = "Courier";
            pAction = m_pCourierFontAction;
            break;
        }

        case kFontType_Monospace:
            Font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            Font.setStyleHint(QFont::Monospace);
            Font.setStyleStrategy(QFont::PreferAntialias);
            Font.setFamily("Monospace [Monotype]");
            pszSetting = "Monospace";
            pAction = m_pMonospaceFontAction;
            break;

        default:
            AssertFailedReturnVoid();
    }

    setFont(Font);

    /* When going through a slot, the action is typically checked already by Qt. */
    if (!pAction->isChecked())
        pAction->setChecked(true);

    /* Make this setting persistent. */
    if (m_pVirtualBox && fSaveIt)
        m_pVirtualBox->SetExtraData(com::Bstr("DbgConsole/Font").raw(), com::Bstr(pszSetting).raw());
}


void
VBoxDbgConsoleOutput::setFontSize(uint32_t uFontSize, bool fSaveIt)
{
    uint32_t idxAction = uFontSize - s_uMinFontSize;
    if (idxAction < (uint32_t)RT_ELEMENTS(m_apFontSizeActions))
    {
        if (!m_apFontSizeActions[idxAction]->isChecked())
            m_apFontSizeActions[idxAction]->setChecked(true);

        QFont Font = font();
        Font.setPointSize(uFontSize);
        setFont(Font);

        /* Make this setting persistent if requested. */
        if (fSaveIt && m_pVirtualBox)
            m_pVirtualBox->SetExtraData(com::Bstr("DbgConsole/FontSize").raw(), com::BstrFmt("%u", uFontSize).raw());
    }
}


void
VBoxDbgConsoleOutput::sltSelectColorScheme()
{
    QAction *pAction = qobject_cast<QAction *>(sender());
    if (pAction)
        setColorScheme((VBoxDbgConsoleColor)pAction->data().toInt(), true /*fSaveIt*/);
}


void
VBoxDbgConsoleOutput::sltSelectFontType()
{
    QAction *pAction = qobject_cast<QAction *>(sender());
    if (pAction)
        setFontType((VBoxDbgConsoleFontType)pAction->data().toInt(), true /*fSaveIt*/);
}


void
VBoxDbgConsoleOutput::sltSelectFontSize()
{
    QAction *pAction = qobject_cast<QAction *>(sender());
    if (pAction)
        setFontSize(pAction->data().toUInt(), true /*fSaveIt*/);
}


void
VBoxDbgConsoleOutput::appendText(const QString &rStr, bool fClearSelection)
{
    Assert(m_hGUIThread == RTThreadNativeSelf());

    if (rStr.isEmpty() || rStr.isNull() || !rStr.length())
        return;

    /*
     * Insert all in one go and make sure it's visible.
     *
     * We need to move the cursor and unselect any selected text before
     * inserting anything, otherwise, text will disappear.
     */
    QTextCursor Cursor = textCursor();
    if (!fClearSelection && Cursor.hasSelection())
    {
        QTextCursor SavedCursor = Cursor;
        Cursor.clearSelection();
        Cursor.movePosition(QTextCursor::End);

        Cursor.insertText(rStr);

        setTextCursor(SavedCursor);
    }
    else
    {
        if (Cursor.hasSelection())
            Cursor.clearSelection();
        if (!Cursor.atEnd())
            Cursor.movePosition(QTextCursor::End);

        Cursor.insertText(rStr);

        setTextCursor(Cursor);
        ensureCursorVisible();
    }
}




/*
 *
 *      V B o x D b g C o n s o l e I n p u t
 *      V B o x D b g C o n s o l e I n p u t
 *      V B o x D b g C o n s o l e I n p u t
 *
 *
 */


VBoxDbgConsoleInput::VBoxDbgConsoleInput(QWidget *pParent/* = NULL*/, const char *pszName/* = NULL*/)
    : QComboBox(pParent), m_hGUIThread(RTThreadNativeSelf())
{
    addItem(""); /* invariant: empty command line is the last item */

    setEditable(true);
    setInsertPolicy(NoInsert);
    setCompleter(0);
    setMaxCount(50);
    const QLineEdit *pEdit = lineEdit();
    if (pEdit)
        connect(pEdit, SIGNAL(returnPressed()), this, SLOT(returnPressed()));

    NOREF(pszName);
}


VBoxDbgConsoleInput::~VBoxDbgConsoleInput()
{
    Assert(m_hGUIThread == RTThreadNativeSelf());
}


void
VBoxDbgConsoleInput::setLineEdit(QLineEdit *pEdit)
{
    Assert(m_hGUIThread == RTThreadNativeSelf());
    QComboBox::setLineEdit(pEdit);
    if (lineEdit() == pEdit && pEdit)
        connect(pEdit, SIGNAL(returnPressed()), this, SLOT(returnPressed()));
}


void
VBoxDbgConsoleInput::returnPressed()
{
    Assert(m_hGUIThread == RTThreadNativeSelf());

    QString strCommand = currentText();
    /** @todo trim whitespace? */
    if (strCommand.isEmpty())
        return;

    /* deal with the current command. */
    emit commandSubmitted(strCommand);


    /*
     * Add current command to history.
     */
    bool fNeedsAppending = true;

    /* invariant: empty line at the end */
    int iLastItem = count() - 1;
    Assert(itemText(iLastItem).isEmpty());

    /* have previous command? check duplicate. */
    if (iLastItem > 0)
    {
        const QString strPrevCommand(itemText(iLastItem - 1));
        if (strCommand == strPrevCommand)
            fNeedsAppending = false;
    }

    if (fNeedsAppending)
    {
        /* history full? drop the oldest command. */
        if (count() == maxCount())
        {
            removeItem(0);
            --iLastItem;
        }

        /* insert before the empty line. */
        insertItem(iLastItem, strCommand);
    }

    /* invariant: empty line at the end */
    int iNewLastItem = count() - 1;
    Assert(itemText(iNewLastItem).isEmpty());

    /* select empty line to present "new" command line to the user */
    setCurrentIndex(iNewLastItem);
}






/*
 *
 *      V B o x D b g C o n s o l e
 *      V B o x D b g C o n s o l e
 *      V B o x D b g C o n s o l e
 *
 *
 */


VBoxDbgConsole::VBoxDbgConsole(VBoxDbgGui *a_pDbgGui, QWidget *a_pParent/* = NULL*/, IVirtualBox *a_pVirtualBox/* = NULL */)
    : VBoxDbgBaseWindow(a_pDbgGui, a_pParent, "Console"), m_pOutput(NULL), m_pInput(NULL), m_fInputRestoreFocus(false),
    m_pszInputBuf(NULL), m_cbInputBuf(0), m_cbInputBufAlloc(0),
    m_pszOutputBuf(NULL), m_cbOutputBuf(0), m_cbOutputBufAlloc(0),
    m_pTimer(NULL), m_fUpdatePending(false), m_Thread(NIL_RTTHREAD), m_EventSem(NIL_RTSEMEVENT),
    m_fTerminate(false), m_fThreadTerminated(false)
{
    /* Delete dialog on close: */
    setAttribute(Qt::WA_DeleteOnClose);

    /*
     * Create the output text box.
     */
    m_pOutput = new VBoxDbgConsoleOutput(this, a_pVirtualBox);

    /* try figure a suitable size and tell the parent class. */
    QLabel *pLabel = new QLabel("8888888888888888888888888888888888888888888888888888888888888888888888888888888", this);
    pLabel->setFont(m_pOutput->font());
    QSize Size = pLabel->sizeHint();
    delete pLabel;
    QSize SizeScrollBar(0,0);
    QScrollBar *pScrollBar = m_pOutput->verticalScrollBar();
    if (pScrollBar)
        SizeScrollBar = pScrollBar->sizeHint();
    vSetMinWidthHint(Size.width() + SizeScrollBar.width() + 1);

    /*
     * Create the input combo box (with a label).
     */
    QHBoxLayout *pLayout = new QHBoxLayout();
    //pLayout->setSizeConstraint(QLayout::SetMaximumSize);

    pLabel = new QLabel(" Command ");
    pLayout->addWidget(pLabel);
    pLabel->setMaximumSize(pLabel->sizeHint());
    pLabel->setAlignment(Qt::AlignCenter);

    m_pInput = new VBoxDbgConsoleInput(NULL);
    pLayout->addWidget(m_pInput);
    m_pInput->setDuplicatesEnabled(false);
    connect(m_pInput, SIGNAL(commandSubmitted(const QString &)), this, SLOT(commandSubmitted(const QString &)));

# if 0//def Q_WS_MAC
    pLabel = new QLabel("  ");
    pLayout->addWidget(pLabel);
    pLabel->setMaximumSize(20, m_pInput->sizeHint().height() + 6);
    pLabel->setMinimumSize(20, m_pInput->sizeHint().height() + 6);
# endif

    QWidget *pHBox = new QWidget(this);
    pHBox->setLayout(pLayout);

    m_pInput->setEnabled(false);    /* (we'll get a ready notification) */


    /*
     * Vertical layout box on the whole widget.
     */
    QVBoxLayout *pVLayout = new QVBoxLayout();
    pVLayout->setContentsMargins(0, 0, 0, 0);
    pVLayout->setSpacing(5);
    pVLayout->addWidget(m_pOutput);
    pVLayout->addWidget(pHBox);
    setLayout(pVLayout);

    /*
     * The tab order is from input to output, not the other way around as it is by default.
     */
    setTabOrder(m_pInput, m_pOutput);
    m_fInputRestoreFocus = true; /* hack */

    /*
     * Setup the timer.
     */
    m_pTimer = new QTimer(this);
    connect(m_pTimer, SIGNAL(timeout()), SLOT(updateOutput()));

    /*
     * Init the backend structure.
     */
    m_Back.Core.pfnInput   = backInput;
    m_Back.Core.pfnRead    = backRead;
    m_Back.Core.pfnWrite   = backWrite;
    m_Back.Core.pfnSetReady = backSetReady;
    m_Back.pSelf = this;

    /*
     * Create the critical section, the event semaphore and the debug console thread.
     */
    int rc = RTCritSectInit(&m_Lock);
    AssertRC(rc);

    rc = RTSemEventCreate(&m_EventSem);
    AssertRC(rc);

    rc = RTThreadCreate(&m_Thread, backThread, this, 0, RTTHREADTYPE_DEBUGGER, RTTHREADFLAGS_WAITABLE, "VBoxDbgC");
    AssertRC(rc);
    if (RT_FAILURE(rc))
        m_Thread = NIL_RTTHREAD;

    /*
     * Shortcuts.
     */
    m_pFocusToInput = new QAction("", this);
    m_pFocusToInput->setShortcut(QKeySequence("Ctrl+L"));
    addAction(m_pFocusToInput);
    connect(m_pFocusToInput, SIGNAL(triggered(bool)), this, SLOT(actFocusToInput()));

    m_pFocusToOutput = new QAction("", this);
    m_pFocusToOutput->setShortcut(QKeySequence("Ctrl+O"));
    addAction(m_pFocusToOutput);
    connect(m_pFocusToOutput, SIGNAL(triggered(bool)), this, SLOT(actFocusToOutput()));

    addAction(m_pOutput->m_pBlackOnWhiteAction);
    addAction(m_pOutput->m_pGreenOnBlackAction);
    addAction(m_pOutput->m_pCourierFontAction);
    addAction(m_pOutput->m_pMonospaceFontAction);
}


VBoxDbgConsole::~VBoxDbgConsole()
{
    Assert(isGUIThread());

    /*
     * Wait for the thread.
     */
    ASMAtomicWriteBool(&m_fTerminate, true);
    RTSemEventSignal(m_EventSem);
    if (m_Thread != NIL_RTTHREAD)
    {
        int rc = RTThreadWait(m_Thread, 15000, NULL);
        AssertRC(rc);
        m_Thread = NIL_RTTHREAD;
    }

    /*
     * Free resources.
     */
    delete m_pTimer;
    m_pTimer = NULL;
    RTCritSectDelete(&m_Lock);
    RTSemEventDestroy(m_EventSem);
    m_EventSem = 0;
    m_pOutput = NULL;
    m_pInput = NULL;
    if (m_pszInputBuf)
    {
        RTMemFree(m_pszInputBuf);
        m_pszInputBuf = NULL;
    }
    m_cbInputBuf = 0;
    m_cbInputBufAlloc = 0;

    delete m_pFocusToInput;
    m_pFocusToInput = NULL;
    delete m_pFocusToOutput;
    m_pFocusToOutput = NULL;

    if (m_pszOutputBuf)
    {
        RTMemFree(m_pszOutputBuf);
        m_pszOutputBuf = NULL;
    }
}


void
VBoxDbgConsole::commandSubmitted(const QString &rCommand)
{
    Assert(isGUIThread());

    lock();
    RTSemEventSignal(m_EventSem);

    QByteArray Utf8Array = rCommand.toUtf8();
    const char *psz = Utf8Array.constData();
    size_t cb = strlen(psz);

    /*
     * Make sure we've got space for the input.
     */
    if (cb + m_cbInputBuf >= m_cbInputBufAlloc)
    {
        size_t cbNew = RT_ALIGN_Z(cb + m_cbInputBufAlloc + 1, 128);
        void *pv = RTMemRealloc(m_pszInputBuf, cbNew);
        if (!pv)
        {
            unlock();
            return;
        }
        m_pszInputBuf = (char *)pv;
        m_cbInputBufAlloc = cbNew;
    }

    /*
     * Add the input and output it.
     */
    memcpy(m_pszInputBuf + m_cbInputBuf, psz, cb);
    m_cbInputBuf += cb;
    m_pszInputBuf[m_cbInputBuf++] = '\n';

    m_pOutput->appendText(rCommand + "\n", true /*fClearSelection*/);
    m_pOutput->ensureCursorVisible();

    m_fInputRestoreFocus = m_pInput->hasFocus();    /* dirty focus hack */
    m_pInput->setEnabled(false);

    Log(("VBoxDbgConsole::commandSubmitted: %s (input-enabled=%RTbool)\n", psz, m_pInput->isEnabled()));
    unlock();
}


void
VBoxDbgConsole::updateOutput()
{
    Assert(isGUIThread());

    lock();
    m_fUpdatePending = false;
    if (m_cbOutputBuf)
    {
        m_pOutput->appendText(QString::fromUtf8((const char *)m_pszOutputBuf, (int)m_cbOutputBuf), false /*fClearSelection*/);
        m_cbOutputBuf = 0;
    }
    unlock();
}


/**
 * Lock the object.
 */
void
VBoxDbgConsole::lock()
{
    RTCritSectEnter(&m_Lock);
}


/**
 * Unlocks the object.
 */
void
VBoxDbgConsole::unlock()
{
    RTCritSectLeave(&m_Lock);
}



/**
 * Checks if there is input.
 *
 * @returns true if there is input ready.
 * @returns false if there not input ready.
 * @param   pBack       Pointer to VBoxDbgConsole::m_Back.
 * @param   cMillies    Number of milliseconds to wait on input data.
 */
/*static*/ DECLCALLBACK(bool)
VBoxDbgConsole::backInput(PCDBGCIO pBack, uint32_t cMillies)
{
    VBoxDbgConsole *pThis = VBOXDBGCONSOLE_FROM_DBGCIO(pBack);
    pThis->lock();

    bool fRc = true;
    if (!pThis->m_cbInputBuf)
    {
        /*
         * Wait outside the lock for the requested time, then check again.
         */
        pThis->unlock();
        RTSemEventWait(pThis->m_EventSem, cMillies);
        pThis->lock();
        fRc = pThis->m_cbInputBuf
           || ASMAtomicUoReadBool(&pThis->m_fTerminate);
    }

    pThis->unlock();
    return fRc;
}


/**
 * Read input.
 *
 * @returns VBox status code.
 * @param   pBack       Pointer to VBoxDbgConsole::m_Back.
 * @param   pvBuf       Where to put the bytes we read.
 * @param   cbBuf       Maximum nymber of bytes to read.
 * @param   pcbRead     Where to store the number of bytes actually read.
 *                      If NULL the entire buffer must be filled for a
 *                      successful return.
 */
/*static*/ DECLCALLBACK(int)
VBoxDbgConsole::backRead(PCDBGCIO pBack, void *pvBuf, size_t cbBuf, size_t *pcbRead)
{
    VBoxDbgConsole *pThis = VBOXDBGCONSOLE_FROM_DBGCIO(pBack);
    Assert(pcbRead); /** @todo implement this bit */
    if (pcbRead)
        *pcbRead = 0;

    pThis->lock();
    int rc = VINF_SUCCESS;
    if (!ASMAtomicUoReadBool(&pThis->m_fTerminate))
    {
        if (pThis->m_cbInputBuf)
        {
            const char *psz = pThis->m_pszInputBuf;
            size_t cbRead = RT_MIN(pThis->m_cbInputBuf, cbBuf);
            memcpy(pvBuf, psz, cbRead);
            psz += cbRead;
            pThis->m_cbInputBuf -= cbRead;
            if (*psz)
                memmove(pThis->m_pszInputBuf, psz, pThis->m_cbInputBuf);
            pThis->m_pszInputBuf[pThis->m_cbInputBuf] = '\0';
            *pcbRead = cbRead;
        }
    }
    else
        rc = VERR_GENERAL_FAILURE;
    pThis->unlock();
    return rc;
}


/**
 * Write (output).
 *
 * @returns VBox status code.
 * @param   pBack       Pointer to VBoxDbgConsole::m_Back.
 * @param   pvBuf       What to write.
 * @param   cbBuf       Number of bytes to write.
 * @param   pcbWritten  Where to store the number of bytes actually written.
 *                      If NULL the entire buffer must be successfully written.
 */
/*static*/ DECLCALLBACK(int)
VBoxDbgConsole::backWrite(PCDBGCIO pBack, const void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    VBoxDbgConsole *pThis = VBOXDBGCONSOLE_FROM_DBGCIO(pBack);
    int rc = VINF_SUCCESS;

    pThis->lock();
    if (cbBuf + pThis->m_cbOutputBuf >= pThis->m_cbOutputBufAlloc)
    {
        size_t cbNew = RT_ALIGN_Z(cbBuf + pThis->m_cbOutputBufAlloc + 1, 1024);
        void *pv = RTMemRealloc(pThis->m_pszOutputBuf, cbNew);
        if (!pv)
        {
            pThis->unlock();
            if (pcbWritten)
                *pcbWritten = 0;
            return VERR_NO_MEMORY;
        }
        pThis->m_pszOutputBuf = (char *)pv;
        pThis->m_cbOutputBufAlloc = cbNew;
    }

    /*
     * Add the output.
     */
    memcpy(pThis->m_pszOutputBuf + pThis->m_cbOutputBuf, pvBuf, cbBuf);
    pThis->m_cbOutputBuf += cbBuf;
    pThis->m_pszOutputBuf[pThis->m_cbOutputBuf] = '\0';
    if (pcbWritten)
        *pcbWritten = cbBuf;

    if (ASMAtomicUoReadBool(&pThis->m_fTerminate))
        rc = VERR_GENERAL_FAILURE;

    /*
     * Tell the GUI thread to draw this text.
     * We cannot do it from here without frequent crashes.
     */
    if (!pThis->m_fUpdatePending)
        QApplication::postEvent(pThis, new VBoxDbgConsoleEvent(VBoxDbgConsoleEvent::kUpdate));

    pThis->unlock();

    return rc;
}


/*static*/ DECLCALLBACK(void)
VBoxDbgConsole::backSetReady(PCDBGCIO pBack, bool fReady)
{
    VBoxDbgConsole *pThis = VBOXDBGCONSOLE_FROM_DBGCIO(pBack);
    if (fReady)
        QApplication::postEvent(pThis, new VBoxDbgConsoleEvent(VBoxDbgConsoleEvent::kInputEnable));
}


/*static*/ DECLCALLBACK(int)
VBoxDbgConsole::backThread(RTTHREAD Thread, void *pvUser)
{
    VBoxDbgConsole *pThis = (VBoxDbgConsole *)pvUser;
    LogFlow(("backThread: Thread=%p pvUser=%p\n", (void *)Thread, pvUser));

    NOREF(Thread);

    /*
     * Create and execute the console.
     */
    int rc = pThis->dbgcCreate(&pThis->m_Back.Core, 0);

    ASMAtomicUoWriteBool(&pThis->m_fThreadTerminated, true);
    if (!ASMAtomicUoReadBool(&pThis->m_fTerminate))
        QApplication::postEvent(pThis, new VBoxDbgConsoleEvent(rc == VINF_SUCCESS
                                                               ? VBoxDbgConsoleEvent::kTerminatedUser
                                                               : VBoxDbgConsoleEvent::kTerminatedOther));
    LogFlow(("backThread: returns %Rrc (m_fTerminate=%RTbool)\n", rc, ASMAtomicUoReadBool(&pThis->m_fTerminate)));
    return rc;
}


bool
VBoxDbgConsole::event(QEvent *pGenEvent)
{
    Assert(isGUIThread());
    if (pGenEvent->type() == (QEvent::Type)VBoxDbgConsoleEvent::kEventNumber)
    {
        VBoxDbgConsoleEvent *pEvent = (VBoxDbgConsoleEvent *)pGenEvent;

        switch (pEvent->command())
        {
            /* make update pending. */
            case VBoxDbgConsoleEvent::kUpdate:
                lock();
                if (!m_fUpdatePending)
                {
                    m_fUpdatePending = true;
                    m_pTimer->setSingleShot(true);
                    m_pTimer->start(10);
                }
                unlock();
                break;

            /* Re-enable the input field and restore focus. */
            case VBoxDbgConsoleEvent::kInputEnable:
                Log(("VBoxDbgConsole: kInputEnable (input-enabled=%RTbool)\n", m_pInput->isEnabled()));
                m_pInput->setEnabled(true);
                if (    m_fInputRestoreFocus
                    &&  !m_pInput->hasFocus())
                    m_pInput->setFocus(); /* this is a hack. */
                m_fInputRestoreFocus = false;
                break;

            /* The thread terminated by user command (exit, quit, bye). */
            case VBoxDbgConsoleEvent::kTerminatedUser:
                Log(("VBoxDbgConsole: kTerminatedUser (input-enabled=%RTbool)\n", m_pInput->isEnabled()));
                m_pInput->setEnabled(false);
                close();
                break;

            /* The thread terminated for some unknown reason., disable input */
            case VBoxDbgConsoleEvent::kTerminatedOther:
                Log(("VBoxDbgConsole: kTerminatedOther (input-enabled=%RTbool)\n", m_pInput->isEnabled()));
                m_pInput->setEnabled(false);
                break;

            /* paranoia */
            default:
                AssertMsgFailed(("command=%d\n", pEvent->command()));
                break;
        }
        return true;
    }

    return VBoxDbgBaseWindow::event(pGenEvent);
}


void
VBoxDbgConsole::keyReleaseEvent(QKeyEvent *pEvent)
{
    //RTAssertMsg2("VBoxDbgConsole::keyReleaseEvent: %d (%#x); mod=%#x\n", pEvent->key(), pEvent->key(), pEvent->modifiers());
    switch (pEvent->key())
    {
        case Qt::Key_F5:
            if (pEvent->modifiers() == 0)
                commandSubmitted("g");
            break;

        case Qt::Key_F8:
            if (pEvent->modifiers() == 0)
                commandSubmitted("t");
            break;

        case Qt::Key_F10:
            if (pEvent->modifiers() == 0)
                commandSubmitted("p");
            break;

        case Qt::Key_F11:
            if (pEvent->modifiers() == 0)
                commandSubmitted("t");
            else if (pEvent->modifiers() == Qt::ShiftModifier)
                commandSubmitted("gu");
            break;

        case Qt::Key_Cancel: /* == break */
            if (pEvent->modifiers() == Qt::ControlModifier)
                commandSubmitted("stop");
            break;
        case Qt::Key_Delete:
            if (pEvent->modifiers() == Qt::AltModifier)
                commandSubmitted("stop");
            break;
    }
}


void
VBoxDbgConsole::closeEvent(QCloseEvent *a_pCloseEvt)
{
    if (m_fThreadTerminated)
        a_pCloseEvt->accept();
}


void
VBoxDbgConsole::actFocusToInput()
{
    if (!m_pInput->hasFocus())
        m_pInput->setFocus(Qt::ShortcutFocusReason);
}


void
VBoxDbgConsole::actFocusToOutput()
{
    if (!m_pOutput->hasFocus())
        m_pOutput->setFocus(Qt::ShortcutFocusReason);
}

