/* $Id: UIGuestControlConsole.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGuestControlConsole class declaration.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_guestctrl_UIGuestControlConsole_h
#define FEQT_INCLUDED_SRC_guestctrl_UIGuestControlConsole_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
# include <QPlainTextEdit>

/* COM includes: */
#include "CGuest.h"

class UIGuestControlInterface;
/** QPlainTextEdit extension to provide a simple terminal like widget. */
class UIGuestControlConsole : public QPlainTextEdit
{

    Q_OBJECT;


public:

    UIGuestControlConsole(const CGuest &comGuest, QWidget* parent = 0);
    /* @p strOutput is displayed in the console */
    void putOutput(const QString &strOutput);

protected:

    void keyPressEvent(QKeyEvent *pEvent) RT_OVERRIDE;
    void mousePressEvent(QMouseEvent *pEvent) RT_OVERRIDE;
    void mouseDoubleClickEvent(QMouseEvent *pEvent) RT_OVERRIDE;
    void contextMenuEvent(QContextMenuEvent *pEvent) RT_OVERRIDE;

private slots:

    void sltOutputReceived(const QString &strOutput);

private:

    typedef QVector<QString> CommandHistory;
    typedef QMap<QString, int> TabDictionary;

    void           reset();
    void           startNextLine();
    /** Return the text of the curent line */
    QString        getCommandString();
    /** Replaces the content of the last line with m_strPromt + @p stringNewContent */
    void           replaceLineContent(const QString &stringNewContent);
    /** Get next/prev command from history. Return @p originalString if history is empty. */
    QString        getNextCommandFromHistory(const QString &originalString = QString());
    QString        getPreviousCommandFromHistory(const QString &originalString = QString());
    void           completeByTab();
    void           commandEntered(const QString &strCommand);

    /* Return a list of words that start with @p strSearch */
    QList<QString> matchedWords(const QString &strSearch) const;
    CGuest         m_comGuest;
    const QString  m_strGreet;
    const QString  m_strPrompt;
    TabDictionary  m_tabDictinary;
    /* A vector of entered commands */
    CommandHistory m_tCommandHistory;
    unsigned       m_uCommandHistoryIndex;
    UIGuestControlInterface  *m_pControlInterface;
};

#endif /* !FEQT_INCLUDED_SRC_guestctrl_UIGuestControlConsole_h */
