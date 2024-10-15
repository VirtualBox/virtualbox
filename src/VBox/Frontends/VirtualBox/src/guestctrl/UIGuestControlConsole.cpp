/* $Id: UIGuestControlConsole.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGuestControlConsole class implementation.
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

/* Qt includes: */
#include <QVBoxLayout>
#include <QApplication>
#include <QTextBlock>

/* GUI includes: */
#include "UIGuestControlConsole.h"
#include "UIGuestControlInterface.h"

UIGuestControlConsole::UIGuestControlConsole(const CGuest &comGuest, QWidget* parent /* = 0 */)
    :QPlainTextEdit(parent)
    , m_comGuest(comGuest)
    , m_strGreet("Welcome to 'Guest Control Console'. Type 'help' for help\n")
    , m_strPrompt("$>")
    , m_uCommandHistoryIndex(0)
    , m_pControlInterface(0)
{
    m_pControlInterface = new UIGuestControlInterface(this, m_comGuest);

    connect(m_pControlInterface, &UIGuestControlInterface::sigOutputString,
            this, &UIGuestControlConsole::sltOutputReceived);

    /* Configure this: */
    setUndoRedoEnabled(false);
    setWordWrapMode(QTextOption::NoWrap);
    reset();

    m_tabDictinary.insert("username", 0);
    m_tabDictinary.insert("createsession", 0);
    m_tabDictinary.insert("exe", 0);
    m_tabDictinary.insert("sessionid", 0);
    m_tabDictinary.insert("sessionname", 0);
    m_tabDictinary.insert("timeout", 0);
    m_tabDictinary.insert("password", 0);
    m_tabDictinary.insert("start", 0);
    m_tabDictinary.insert("ls", 0);
    m_tabDictinary.insert("stat", 0);
}

void UIGuestControlConsole::commandEntered(const QString &strCommand)
{
    if (m_pControlInterface)
        m_pControlInterface->putCommand(strCommand);
}

void UIGuestControlConsole::sltOutputReceived(const QString &strOutput)
{
    putOutput(strOutput);
}

void UIGuestControlConsole::reset()
{
    clear();
    startNextLine();
    insertPlainText(m_strGreet);
    startNextLine();
}

void UIGuestControlConsole::startNextLine()
{
    moveCursor(QTextCursor::End);
    insertPlainText(m_strPrompt);
    moveCursor(QTextCursor::End);
}


void UIGuestControlConsole::putOutput(const QString &strOutput)
{
    if (strOutput.isNull() || strOutput.length() <= 0)
        return;

    bool newLineNeeded = getCommandString().isEmpty();

    QString strOwn("\n");
    strOwn.append(strOutput);
    moveCursor(QTextCursor::End);
    insertPlainText(strOwn);
    moveCursor(QTextCursor::End);

    if (newLineNeeded)
    {
        insertPlainText("\n");
        startNextLine();
    }
 }

void UIGuestControlConsole::keyPressEvent(QKeyEvent *pEvent)
{
    /* Check if we at the bottom most line.*/
    bool lastLine = blockCount() == (textCursor().blockNumber() +1);

    switch (pEvent->key()) {
        case Qt::Key_PageUp:
        case Qt::Key_Up:
        {
            replaceLineContent(getPreviousCommandFromHistory(getCommandString()));
            break;
        }
        case Qt::Key_PageDown:
        case Qt::Key_Down:
        {
            replaceLineContent(getNextCommandFromHistory(getCommandString()));
            break;
        }
        case Qt::Key_Backspace:
        {
            QTextCursor cursor = textCursor();
            if (lastLine && cursor.positionInBlock() > m_strPrompt.length())
                cursor.deletePreviousChar();
            break;
        }
        case Qt::Key_Left:
        case Qt::Key_Right:
        {
            if (textCursor().positionInBlock() > m_strPrompt.length()-1)
                QPlainTextEdit::keyPressEvent(pEvent);
            break;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter:
        {
            if (lastLine)
            {
                QString strCommand(getCommandString());
                if (!strCommand.isEmpty())
                {
                    commandEntered(strCommand);
                    if (!m_tCommandHistory.contains(strCommand))
                        m_tCommandHistory.push_back(strCommand);
                    m_uCommandHistoryIndex = m_tCommandHistory.size()-1;
                    moveCursor(QTextCursor::End);
                    QPlainTextEdit::keyPressEvent(pEvent);
                    startNextLine();
                }
            }
            break;
        }
        case Qt::Key_Home:
        {
            QTextCursor cursor = textCursor();
            cursor.movePosition(QTextCursor::StartOfLine);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, m_strPrompt.length());
            setTextCursor(cursor);
            break;
        }
        case Qt::Key_Tab:
            completeByTab();
            break;
        default:
        {
            if (pEvent->modifiers() == Qt::ControlModifier && pEvent->key() == Qt::Key_C)
            {
                QPlainTextEdit::keyPressEvent(pEvent);
            }
            else
            {
                if (lastLine)
                    QPlainTextEdit::keyPressEvent(pEvent);
            }
        }
            break;
    }
}

void UIGuestControlConsole::mousePressEvent(QMouseEvent *pEvent)
{
    // Q_UNUSED(pEvent);
    // setFocus();
    QPlainTextEdit::mousePressEvent(pEvent);
}

void UIGuestControlConsole::mouseDoubleClickEvent(QMouseEvent *pEvent)
{
    //Q_UNUSED(pEvent);
    QPlainTextEdit::mouseDoubleClickEvent(pEvent);
}

void UIGuestControlConsole::contextMenuEvent(QContextMenuEvent *pEvent)
{
    Q_UNUSED(pEvent);
    //QPlainTextEdit::contextMenuEvent(pEvent);
}

QString UIGuestControlConsole::getCommandString()
{
    QTextDocument* pDocument = document();
    if (!pDocument)
        return QString();
    QTextBlock block = pDocument->lastBlock();//findBlockByLineNumber(pDocument->lineCount()-1);
    if (!block.isValid())
        return QString();
    QString lineStr = block.text();
    if (lineStr.isNull() || lineStr.length() <= 1)
        return QString();
    /* Remove m_strPrompt from the line string: */
    return (lineStr.right(lineStr.length()-m_strPrompt.length()));
}

void UIGuestControlConsole::replaceLineContent(const QString &stringNewContent)
{
    moveCursor(QTextCursor::End);
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();

    QString newString(m_strPrompt);
    newString.append(stringNewContent);
    insertPlainText(newString);
    moveCursor(QTextCursor::End);
}

QString UIGuestControlConsole::getNextCommandFromHistory(const QString &originalString /* = QString() */)
{
    if (m_tCommandHistory.empty())
        return originalString;

    if (m_uCommandHistoryIndex == (unsigned)(m_tCommandHistory.size() - 1))
        m_uCommandHistoryIndex = 0;
    else
        ++m_uCommandHistoryIndex;

    return m_tCommandHistory.at(m_uCommandHistoryIndex);
}


QString UIGuestControlConsole::getPreviousCommandFromHistory(const QString &originalString /* = QString() */)
{
    if (m_tCommandHistory.empty())
        return originalString;
    if (m_uCommandHistoryIndex == 0)
        m_uCommandHistoryIndex = m_tCommandHistory.size() - 1;
    else
        --m_uCommandHistoryIndex;

    return m_tCommandHistory.at(m_uCommandHistoryIndex);
}

void UIGuestControlConsole::completeByTab()
{
    bool lastLine = blockCount() == (textCursor().blockNumber() +1);
    if (!lastLine)
        return;
    /* Save whatever we have currently on this line: */
    QString currentCommand = getCommandString();

    QTextCursor cursor = textCursor();
    /* Save the cursor's position within the line */
    int cursorBlockPosition = cursor.positionInBlock();

    /* Find out on which word the cursor is. This is the word we will
       complete: */
    cursor.select(QTextCursor::WordUnderCursor);
    QString currentWord = cursor.selectedText();

    const QList<QString> &matches = matchedWords(currentWord);
    /* If there are no matches do nothing: */
    if (matches.empty())
        return;
    /* if there are more than one match list them all and
       reprint the line: */
    if (matches.size() > 1)
    {
        moveCursor(QTextCursor::End);
        QString strMatches;
        for (int i = 0; i < matches.size(); ++i)
        {
            strMatches.append(matches.at(i));
            strMatches.append(" ");
        }
        appendPlainText(strMatches);
        insertPlainText(QString("\n").append(m_strPrompt));
        insertPlainText(currentCommand);
        /* Put the cursor in its previous position within the line: */
        int blockPosition = textCursor().block().position();
        QTextCursor nCursor = textCursor();
        nCursor.setPosition(blockPosition + cursorBlockPosition);
        setTextCursor(nCursor);
        return;
    }
    /* if there is only one word just complete: */
    /* some sanity checks */
    if (matches.at(0).length() > currentWord.length())
       insertPlainText(matches.at(0).right(matches.at(0).length() - currentWord.length()));
}


QList<QString> UIGuestControlConsole::matchedWords(const QString &strSearch) const
{
    QList<QString> list;
    /* Go thru the map and find which of its elements start with @pstrSearch: */
    for (TabDictionary::const_iterator iterator = m_tabDictinary.begin();
        iterator != m_tabDictinary.end(); ++iterator)
    {
        const QString &strMap = iterator.key();
        if (strMap.startsWith(strSearch))
            list.push_back(strMap);
    }
    return list;
}
