/* $Id: UIHotKeyEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIHotKeyEditor class implementation.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <QApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QKeyEvent>
#include <QStyle>

/* GUI includes; */
#include "QIToolButton.h"
#include "UIHostComboEditor.h"
#include "UIHotKeyEditor.h"
#include "UIIconPool.h"
#include "UITranslationEventListener.h"


/** QLineEdit extension representing hot-key editor. */
class UIHotKeyLineEdit : public QLineEdit
{
    Q_OBJECT;

public:

    /** Constructs hot-key editor passing @a pParent to the base-class. */
    UIHotKeyLineEdit(QWidget *pParent);

protected slots:

    /** Deselects the hot-key editor text. */
    void sltDeselect() { deselect(); }

protected:

    /** Handles key-press @a pEvent. */
    virtual void keyPressEvent(QKeyEvent *pEvent) RT_OVERRIDE;
    /** Handles key-release @a pEvent. */
    virtual void keyReleaseEvent(QKeyEvent *pEvent) RT_OVERRIDE;

private:

    /** Returns whether the passed @a pevent should be ignored. */
    bool isKeyEventIgnored(QKeyEvent *pEvent);
};


/*********************************************************************************************************************************
*   Class UIHotKeyLineEdit implementation.                                                                                       *
*********************************************************************************************************************************/

UIHotKeyLineEdit::UIHotKeyLineEdit(QWidget *pParent)
    : QLineEdit(pParent)
{
    /* Configure self: */
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Expanding);
    setContextMenuPolicy(Qt::NoContextMenu);

    /* Connect selection preserver: */
    connect(this, &UIHotKeyLineEdit::selectionChanged, this, &UIHotKeyLineEdit::sltDeselect);
}

void UIHotKeyLineEdit::keyPressEvent(QKeyEvent *pEvent)
{
    /* Is this event ignored? */
    if (isKeyEventIgnored(pEvent))
        return;
    /* Call to base-class: */
    QLineEdit::keyPressEvent(pEvent);
}

void UIHotKeyLineEdit::keyReleaseEvent(QKeyEvent *pEvent)
{
    /* Is this event ignored? */
    if (isKeyEventIgnored(pEvent))
        return;
    /* Call to base-class: */
    QLineEdit::keyReleaseEvent(pEvent);
}

bool UIHotKeyLineEdit::isKeyEventIgnored(QKeyEvent *pEvent)
{
    /* Ignore some keys: */
    switch (pEvent->key())
    {
        /* Ignore cursor keys: */
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
            pEvent->ignore();
            return true;
        /* Default handling for others: */
        default: break;
    }
    /* Do not ignore key by default: */
    return false;
}


/*********************************************************************************************************************************
*   Class UIHotKeyEditor implementation.                                                                                         *
*********************************************************************************************************************************/

UIHotKeyEditor::UIHotKeyEditor(QWidget *pParent)
    : QWidget(pParent)
    , m_fIsModifiersAllowed(false)
    , m_pMainLayout(new QHBoxLayout(this))
    , m_pButtonLayout(new QHBoxLayout)
    , m_pLineEdit(new UIHotKeyLineEdit(this))
    , m_pResetButton(new QIToolButton(this))
    , m_pClearButton(new QIToolButton(this))
    , m_iTakenKey(-1)
    , m_fSequenceTaken(false)
{
    /* Make sure QIStyledDelegate aware of us: */
    setProperty("has_sigCommitData", true);
    /* Configure self: */
    setAutoFillBackground(true);
    setFocusProxy(m_pLineEdit);

    /* Configure layout: */
#ifdef VBOX_WS_MAC
    m_pMainLayout->setSpacing(5);
#else
    m_pMainLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) / 2);
#endif
    m_pMainLayout->setContentsMargins(0, 0, 0, 0);
    m_pMainLayout->addWidget(m_pLineEdit);
    m_pMainLayout->addLayout(m_pButtonLayout);

    /* Configure button layout: */
    m_pButtonLayout->setSpacing(0);
    m_pButtonLayout->setContentsMargins(0, 0, 0, 0);
    m_pButtonLayout->addWidget(m_pResetButton);
    m_pButtonLayout->addWidget(m_pClearButton);

    /* Configure line-edit: */
    m_pLineEdit->installEventFilter(this);

    /* Configure tool-buttons: */
    m_pResetButton->removeBorder();
    m_pResetButton->setIcon(UIIconPool::iconSet(":/import_16px.png"));
    connect(m_pResetButton, &QToolButton::clicked, this, &UIHotKeyEditor::sltReset);
    m_pClearButton->removeBorder();
    m_pClearButton->setIcon(UIIconPool::iconSet(":/eraser_16px.png"));
    connect(m_pClearButton, &QToolButton::clicked, this, &UIHotKeyEditor::sltClear);

    /* Translate finally: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIHotKeyEditor::sltRetranslateUI);
}

void UIHotKeyEditor::sltReset()
{
    /* Reset the seuence of the hot-key: */
    m_hotKey.setSequence(m_hotKey.defaultSequence());
    /* Redraw sequence: */
    drawSequence();
    /* Move the focut to text-field: */
    m_pLineEdit->setFocus();
    /* Commit data to the listener: */
    emit sigCommitData(this);
}

void UIHotKeyEditor::sltClear()
{
    /* Clear the seuence of the hot-key: */
    m_hotKey.setSequence(QString());
    /* Redraw sequence: */
    drawSequence();
    /* Move the focut to text-field: */
    m_pLineEdit->setFocus();
    /* Commit data to the listener: */
    emit sigCommitData(this);
}

bool UIHotKeyEditor::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* Special handling for our line-edit only: */
    if (pWatched != m_pLineEdit)
        return QWidget::eventFilter(pWatched, pEvent);

    /* Special handling for key events only: */
    if (pEvent->type() != QEvent::KeyPress &&
        pEvent->type() != QEvent::KeyRelease)
        return QWidget::eventFilter(pWatched, pEvent);

    /* Cast passed event to required type: */
    QKeyEvent *pKeyEvent = static_cast<QKeyEvent*>(pEvent);

    /* Should we skip that event to our line-edit? */
    if (shouldWeSkipKeyEventToLineEdit(pKeyEvent))
        return false;

    /* Fetch modifiers state: */
    fetchModifiersState();

    /* Handle key event: */
    switch (pEvent->type())
    {
        case QEvent::KeyPress: handleKeyPress(pKeyEvent); break;
        case QEvent::KeyRelease: handleKeyRelease(pKeyEvent); break;
        default: break;
    }

    /* Fetch host-combo modifier state: */
    checkIfHostModifierNeeded();

    /* Reflect sequence: */
    reflectSequence();

    /* Prevent further key event handling: */
    return true;
}

void UIHotKeyEditor::sltRetranslateUI()
{
    m_pResetButton->setToolTip(tr("Reset shortcut to default"));
    m_pClearButton->setToolTip(tr("Unset shortcut"));
}

void UIHotKeyEditor::keyPressEvent(QKeyEvent *pEvent)
{
    /* Is this event ignored? */
    if (isKeyEventIgnored(pEvent))
        return;
    /* Call to base-class: */
    return QWidget::keyPressEvent(pEvent);
}

void UIHotKeyEditor::keyReleaseEvent(QKeyEvent *pEvent)
{
    /* Is this event ignored? */
    if (isKeyEventIgnored(pEvent))
        return;
    /* Call to base-class: */
    return QWidget::keyReleaseEvent(pEvent);
}

bool UIHotKeyEditor::shouldWeSkipKeyEventToLineEdit(QKeyEvent *pEvent)
{
    /* Special handling for some keys: */
    switch (pEvent->key())
    {
        /* Skip Escape to our line-edit: */
        case Qt::Key_Escape: return true;
        /* Skip Return/Enter to our line-edit: */
        case Qt::Key_Return:
        case Qt::Key_Enter: return true;
        /* Skip cursor keys to our line-edit: */
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down: return true;
        /* Default handling for others: */
        default: break;
    }
    /* Do not skip by default: */
    return false;
}

bool UIHotKeyEditor::isKeyEventIgnored(QKeyEvent *pEvent)
{
    /* Ignore some keys: */
    switch (pEvent->key())
    {
        /* Ignore cursor keys: */
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
            pEvent->ignore();
            return true;
        /* Default handling for others: */
        default: break;
    }
    /* Do not ignore key by default: */
    return false;
}

void UIHotKeyEditor::fetchModifiersState()
{
    /* Make sure modifiers are allowed: */
    if (!m_fIsModifiersAllowed)
        return;

    /* If full sequence was not yet taken: */
    if (!m_fSequenceTaken)
    {
        /* Recreate the set of taken modifiers: */
        m_takenModifiers.clear();
        Qt::KeyboardModifiers currentModifiers = QApplication::keyboardModifiers();
        if (currentModifiers != Qt::NoModifier)
        {
            if ((m_takenModifiers.size() < 3) && (currentModifiers & Qt::ControlModifier))
                m_takenModifiers << Qt::CTRL;
            if ((m_takenModifiers.size() < 3) && (currentModifiers & Qt::AltModifier))
                m_takenModifiers << Qt::ALT;
            if ((m_takenModifiers.size() < 3) && (currentModifiers & Qt::MetaModifier))
                m_takenModifiers << Qt::META;
        }
    }
}

void UIHotKeyEditor::checkIfHostModifierNeeded()
{
    /* Make sure other modifiers are NOT allowed: */
    if (m_fIsModifiersAllowed)
        return;

    /* Clear the set of taken modifiers: */
    m_takenModifiers.clear();

    /* If taken key was set: */
    if (m_iTakenKey != -1)
        /* We have to add Host+ modifier: */
        m_takenModifiers << UIHostCombo::hostComboModifierIndex();
}

bool UIHotKeyEditor::approvedKeyPressed(QKeyEvent *pKeyEvent)
{
    /* Qt by some reason generates text for complex cases like
     * Backspace or Del but skip other similar things like
     * F1 - F35, Home, End, Page UP, Page DOWN and so on.
     * We should declare all the approved keys. */

    /* Compose the set of the approved keys: */
    QSet<int> approvedKeys;

    /* Add Fn keys: */
    for (int i = Qt::Key_F1; i <= Qt::Key_F35; ++i)
        approvedKeys << i;

    /* Add digit keys: */
    for (int i = Qt::Key_0; i <= Qt::Key_9; ++i)
        approvedKeys << i;

    /* We allow to use only English letters in shortcuts.
     * The reason is by some reason Qt distinguish native language
     * letters only with no modifiers pressed.
     * With modifiers pressed Qt thinks the letter is always English. */
    for (int i = Qt::Key_A; i <= Qt::Key_Z; ++i)
        approvedKeys << i;

    /* Add few more special cases: */
    approvedKeys << Qt::Key_Space << Qt::Key_Backspace
                 << Qt::Key_Insert << Qt::Key_Delete
                 << Qt::Key_Pause << Qt::Key_Print
                 << Qt::Key_Home << Qt::Key_End
                 << Qt::Key_PageUp << Qt::Key_PageDown
                 << Qt::Key_QuoteLeft << Qt::Key_AsciiTilde
                 << Qt::Key_Minus << Qt::Key_Underscore
                 << Qt::Key_Equal << Qt::Key_Plus
                 << Qt::Key_ParenLeft << Qt::Key_ParenRight
                 << Qt::Key_BraceLeft << Qt::Key_BraceRight
                 << Qt::Key_BracketLeft << Qt::Key_BracketRight
                 << Qt::Key_Backslash << Qt::Key_Bar
                 << Qt::Key_Semicolon << Qt::Key_Colon
                 << Qt::Key_Apostrophe << Qt::Key_QuoteDbl
                 << Qt::Key_Comma << Qt::Key_Period << Qt::Key_Slash
                 << Qt::Key_Less << Qt::Key_Greater << Qt::Key_Question;

    /* Is this one of the approved keys? */
    if (approvedKeys.contains(pKeyEvent->key()))
        return true;

    /* False by default: */
    return false;
}

void UIHotKeyEditor::handleKeyPress(QKeyEvent *pKeyEvent)
{
    /* If full sequence was not yet taken: */
    if (!m_fSequenceTaken)
    {
        /* If finalizing key is pressed: */
        if (approvedKeyPressed(pKeyEvent))
        {
            /* Remember taken key: */
            m_iTakenKey = pKeyEvent->key();
            /* Mark full sequence taken: */
            m_fSequenceTaken = true;
        }
        /* If something other is pressed: */
        else
        {
            /* Clear taken key: */
            m_iTakenKey = -1;
        }
    }
}

void UIHotKeyEditor::handleKeyRelease(QKeyEvent *pKeyEvent)
{
    /* If full sequence was taken already and no modifiers are currently held: */
    if (m_fSequenceTaken && (pKeyEvent->modifiers() == Qt::NoModifier))
    {
        /* Reset taken sequence: */
        m_fSequenceTaken = false;
    }
}

void UIHotKeyEditor::reflectSequence()
{
    /* Acquire modifier names: */
    QString strModifierNames;
    QStringList modifierNames;
    foreach (int iTakenModifier, m_takenModifiers)
    {
        if (iTakenModifier == UIHostCombo::hostComboModifierIndex())
            modifierNames << UIHostCombo::hostComboModifierName();
        else
            modifierNames << QKeySequence(iTakenModifier).toString(QKeySequence::NativeText);
    }
    if (!modifierNames.isEmpty())
        strModifierNames = modifierNames.join("");
    /* Acquire main key name: */
    QString strMainKeyName;
    if (m_iTakenKey != -1)
        strMainKeyName = QKeySequence(m_iTakenKey).toString(QKeySequence::NativeText);

    /* Compose the text to reflect: */
    QString strText;
    /* If modifiers were set: */
    if (!strModifierNames.isEmpty())
        /* Append the text with modifier names: */
        strText.append(strModifierNames);
    /* If main key was set: */
    if (!strMainKeyName.isEmpty())
        /* Append the sequence with the main key name: */
        strText.append(strMainKeyName);
    /* Reflect what we've got: */
    m_pLineEdit->setText(strText);

    /* Compose the sequence to save: */
    QString strSequence;
    /* If main key was set: */
    if (!strMainKeyName.isEmpty())
    {
        /* Append the sequence with the main key name: */
        strSequence.append(strMainKeyName);
        /* If modifiers are allowed: */
        if (m_fIsModifiersAllowed)
            /* Prepend the sequence with modifier names: */
            strSequence.prepend(strModifierNames);
    }
    /* Save what we've got: */
    m_hotKey.setSequence(strSequence);
    /* Commit data to the listener: */
    emit sigCommitData(this);
}

void UIHotKeyEditor::drawSequence()
{
    /* Compose the text to reflect: */
    QString strText = m_hotKey.sequence();
    /* If modifiers are not allowed and the text is not empty: */
    if (!m_fIsModifiersAllowed && !strText.isEmpty())
        /* Prepend the text with Host+ modifier name: */
        strText.prepend(UIHostCombo::hostComboModifierName());
    /* Reflect what we've got: */
    m_pLineEdit->setText(strText);
}

UIHotKey UIHotKeyEditor::hotKey() const
{
    /* Return hot-key: */
    return m_hotKey;
}

void UIHotKeyEditor::setHotKey(const UIHotKey &hotKey)
{
    /* Remember passed hot-key: */
    m_hotKey = hotKey;
    /* Remember if modifiers are allowed: */
    m_fIsModifiersAllowed = m_hotKey.type() == UIHotKeyType_WithModifiers;
    /* Redraw sequence: */
    drawSequence();
}


#include "UIHotKeyEditor.moc"
