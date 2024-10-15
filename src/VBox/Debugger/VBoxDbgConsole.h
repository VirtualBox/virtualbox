/* $Id: VBoxDbgConsole.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef DEBUGGER_INCLUDED_SRC_VBoxDbgConsole_h
#define DEBUGGER_INCLUDED_SRC_VBoxDbgConsole_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

//#define VBOXDBG_WITH_CONSOLE_OUTPUT_AS_QPLAINTEXT

#include "VBoxDbgBase.h"

#include <QTextEdit>
#ifdef VBOXDBG_WITH_CONSOLE_OUTPUT_AS_QPLAINTEXT
# include <QPlainTextEdit>
#endif
#include <QComboBox>
#include <QTimer>
#include <QEvent>
#include <QActionGroup>

#include <iprt/critsect.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>

// VirtualBox COM interfaces declarations (generated header)
#ifdef VBOX_WITH_XPCOM
# include <VirtualBox_XPCOM.h>
#else
# include <iprt/win/windows.h> /* Include via cleanup wrapper before VirtualBox.h includes it via rpc.h. */
# include <VirtualBox.h>
#endif


class VBoxDbgConsoleOutput
#ifdef VBOXDBG_WITH_CONSOLE_OUTPUT_AS_QPLAINTEXT
    : public QPlainTextEdit
#else
    : public QTextEdit
#endif
{
    Q_OBJECT

public:
    /**
     * Constructor.
     *
     * @param   pParent     Parent Widget.
     * @param   pVirtualBox VirtualBox object for storing extra data.
     * @param   pszName     Widget name.
     */
    VBoxDbgConsoleOutput(QWidget *pParent = NULL, IVirtualBox *pVirtualBox = NULL, const char *pszName = NULL);

    /**
     * Destructor
     */
    virtual ~VBoxDbgConsoleOutput();

    /**
     * Appends text.
     * This differs from QTextEdit::append() in that it won't start on a new paragraph
     * unless the previous char was a newline ('\n').
     *
     * @param   rStr        The text string to append.
     * @param   fClearSelection     Whether to clear selected text before appending.
     *                              If @c false the selection and window position
     *                              are preserved.
     */
    virtual void appendText(const QString &rStr, bool fClearSelection);

    /** The action to switch to black-on-white color scheme. */
    QAction *m_pBlackOnWhiteAction;
    /** The action to switch to green-on-black color scheme. */
    QAction *m_pGreenOnBlackAction;

    /** The action to switch to system default fixed font. */
    QAction *m_pDefaultFontAction;
    /** The action to switch to Monospace font. */
    QAction *m_pMonospaceFontAction;
    /** The action to switch to Courier font. */
    QAction *m_pCourierFontAction;

protected:
    typedef enum  { kGreenOnBlack, kBlackOnWhite } VBoxDbgConsoleColor;
    typedef enum  { kFontType_SystemDefault, kFontType_Monospace, kFontType_Courier } VBoxDbgConsoleFontType;

    /**
     * Context menu event.
     * This adds custom menu items for the output view.
     *
     * @param pEvent   Pointer to the event.
     */
    virtual void contextMenuEvent(QContextMenuEvent *pEvent);

    /**
     * Sets the color scheme.
     *
     * @param   enmScheme       The new color scheme.
     * @param   fSaveIt         Whether to save it.
     */
    void        setColorScheme(VBoxDbgConsoleColor enmScheme, bool fSaveIt);

    /**
     * Sets the font type / family.
     *
     * @param   enmFontType     The font type.
     * @param   fSaveIt         Whether to save it.
     */
    void        setFontType(VBoxDbgConsoleFontType enmFontType, bool fSaveIt);

    /**
     * Sets the font size.
     *
     * @param   uFontSize       The new font size in points.
     * @param   fSaveIt         Whether to save it.
     */
    void        setFontSize(uint32_t uFontSize, bool fSaveIt);


    /** The current line (paragraph) number. */
    unsigned m_uCurLine;
    /** The position in the current line. */
    unsigned m_uCurPos;
    /** The handle to the GUI thread. */
    RTNATIVETHREAD m_hGUIThread;
    /** The current color scheme (foreground on background). */
    VBoxDbgConsoleColor m_enmColorScheme;
    /** The IVirtualBox object */
    IVirtualBox *m_pVirtualBox;

    /** Array of font size actions 6..22pt. */
    QAction *m_apFontSizeActions[22 - 6 + 1];
    /** Action group for m_apFontSizeActions. */
    QActionGroup *m_pActionFontSizeGroup;

    /** The minimum font size.   */
    static const uint32_t s_uMinFontSize;

private slots:
    /**
     * Selects color scheme
     */
    void        sltSelectColorScheme();

    /**
     * Selects font type.
     */
    void        sltSelectFontType();

    /**
     * Selects font size.
     */
    void        sltSelectFontSize();
};


/**
 * The Debugger Console Input widget.
 *
 * This is a combobox which only responds to \<return\>.
 */
class VBoxDbgConsoleInput : public QComboBox
{
    Q_OBJECT

public:
    /**
     * Constructor.
     *
     * @param   pParent     Parent Widget.
     * @param   pszName     Widget name.
     */
    VBoxDbgConsoleInput(QWidget *pParent = NULL, const char *pszName = NULL);

    /**
     * Destructor
     */
    virtual ~VBoxDbgConsoleInput();

    /**
     * We overload this method to get signaled upon returnPressed().
     *
     * See QComboBox::setLineEdit for full description.
     * @param   pEdit   The new line edit widget.
     * @remark  This won't be called during the constructor.
     */
    virtual void setLineEdit(QLineEdit *pEdit);

signals:
    /**
     * New command submitted.
     */
    void commandSubmitted(const QString &rCommand);

private slots:
    /**
     * Returned was pressed.
     *
     * Will emit commandSubmitted().
     */
    void returnPressed();

protected:
    /** The handle to the GUI thread. */
    RTNATIVETHREAD m_hGUIThread;
};


/**
 * The Debugger Console.
 */
class VBoxDbgConsole : public VBoxDbgBaseWindow
{
    Q_OBJECT

public:
    /**
     * Constructor.
     *
     * @param   a_pDbgGui       Pointer to the debugger gui object.
     * @param   a_pParent       Parent Widget.
     * @param   a_pVirtualBox   VirtualBox object for storing extra data.
     */
    VBoxDbgConsole(VBoxDbgGui *a_pDbgGui, QWidget *a_pParent = NULL, IVirtualBox *a_pVirtualBox = NULL);

    /**
     * Destructor
     */
    virtual ~VBoxDbgConsole();

protected slots:
    /**
     * Handler called when a command is submitted.
     * (Enter or return pressed in the combo box.)
     *
     * @param   rCommand        The submitted command.
     */
    void commandSubmitted(const QString &rCommand);

    /**
     * Updates the output with what's currently in the output buffer.
     * This is called by a timer or a User event posted by the debugger thread.
     */
    void updateOutput();

    /**
     * Changes the focus to the input field.
     */
    void actFocusToInput();

    /**
     * Changes the focus to the output viewer widget.
     */
    void actFocusToOutput();

protected:
    /**
     * Override the closeEvent so we can choose delete the window when
     * it is closed.
     *
     * @param  a_pCloseEvt  The close event.
     */
    virtual void closeEvent(QCloseEvent *a_pCloseEvt);

    /**
     * Lock the object.
     */
    void lock();

    /**
     * Unlocks the object.
     */
    void unlock();

protected:
    /** @name Debug Console Backend.
     * @{
     */


    /**
     * Checks if there is input.
     *
     * @returns true if there is input ready.
     * @returns false if there not input ready.
     * @param   pBack       Pointer to VBoxDbgConsole::m_Back.
     * @param   cMillies    Number of milliseconds to wait on input data.
     */
    static DECLCALLBACK(bool) backInput(PCDBGCIO pIo, uint32_t cMillies);

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
    static DECLCALLBACK(int) backRead(PCDBGCIO pIo, void *pvBuf, size_t cbBuf, size_t *pcbRead);

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
    static DECLCALLBACK(int) backWrite(PCDBGCIO pIo, const void *pvBuf, size_t cbBuf, size_t *pcbWritten);

    /**
     * @copydoc DBGCIO::pfnSetReady
     */
    static DECLCALLBACK(void) backSetReady(PCDBGCIO pIo, bool fReady);

    /**
     * The Debugger Console Thread
     *
     * @returns VBox status code (ignored).
     * @param   Thread      The thread handle.
     * @param   pvUser      Pointer to the VBoxDbgConsole object.s
     */
    static DECLCALLBACK(int) backThread(RTTHREAD Thread, void *pvUser);

    /** @} */

protected:
    /**
     * Processes GUI command posted by the console thread.
     *
     * Qt3 isn't thread safe on any platform, meaning there is no locking, so, as
     * a result we have to be very careful. All operations on objects which we share
     * with the main thread has to be posted to it so it can perform it.
     */
    bool event(QEvent *pEvent);

    /**
     * For implementing keyboard shortcuts.
     *
     * @param   pEvent      The key event.
     */
    void keyReleaseEvent(QKeyEvent *pEvent);

protected:
    /** The output widget. */
    VBoxDbgConsoleOutput *m_pOutput;
    /** The input widget. */
    VBoxDbgConsoleInput *m_pInput;
    /** A hack to restore focus to the combobox after a command execution. */
    bool m_fInputRestoreFocus;
    /** The input buffer. */
    char *m_pszInputBuf;
    /** The amount of input in the buffer. */
    size_t m_cbInputBuf;
    /** The allocated size of the buffer. */
    size_t m_cbInputBufAlloc;

    /** The output buffer. */
    char *m_pszOutputBuf;
    /** The amount of output in the buffer. */
    size_t m_cbOutputBuf;
    /** The allocated size of the buffer. */
    size_t m_cbOutputBufAlloc;
    /** The timer object used to process output in a delayed fashion. */
    QTimer *m_pTimer;
    /** Set when an output update is pending. */
    bool volatile m_fUpdatePending;

    /** The debugger console thread. */
    RTTHREAD m_Thread;
    /** The event semaphore used to signal the debug console thread about input. */
    RTSEMEVENT m_EventSem;
    /** The critical section used to lock the object. */
    RTCRITSECT m_Lock;
    /** When set the thread will cause the debug console thread to terminate. */
    bool volatile m_fTerminate;
    /** Has the thread terminated?
     * Used to do the right thing in closeEvent; the console is dead if the
     * thread has terminated. */
    bool volatile m_fThreadTerminated;

    /** The debug console backend structure.
     * Use VBOXDBGCONSOLE_FROM_DBGCIO to convert the DBGCIO pointer to a object pointer. */
    struct VBoxDbgConsoleBack
    {
        DBGCIO Core;
        VBoxDbgConsole *pSelf;
    } m_Back;

    /**
     * Converts a pointer to VBoxDbgConsole::m_Back to VBoxDbgConsole pointer.
     * @todo find a better way because offsetof is undefined on objects and g++ gets very noisy because of that.
     */
#   define VBOXDBGCONSOLE_FROM_DBGCIO(pIo) ( ((struct VBoxDbgConsoleBack *)(pBack))->pSelf )

    /** Change focus to the input field. */
    QAction *m_pFocusToInput;
    /** Change focus to the output viewer widget. */
    QAction *m_pFocusToOutput;
};


/**
 * Simple event class for push certain operations over
 * onto the GUI thread.
 */
class VBoxDbgConsoleEvent : public QEvent
{
public:
    typedef enum  { kUpdate, kInputEnable, kTerminatedUser, kTerminatedOther } VBoxDbgConsoleEventType;
    enum { kEventNumber = QEvent::User + 42 };

    VBoxDbgConsoleEvent(VBoxDbgConsoleEventType enmCommand)
        : QEvent((QEvent::Type)kEventNumber), m_enmCommand(enmCommand)
    {
    }

    VBoxDbgConsoleEventType command() const
    {
        return m_enmCommand;
    }

private:
    VBoxDbgConsoleEventType m_enmCommand;
};


#endif /* !DEBUGGER_INCLUDED_SRC_VBoxDbgConsole_h */

