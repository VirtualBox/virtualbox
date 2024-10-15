/* $Id: UIUserNamePasswordEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUserNamePasswordEditor class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QGridLayout>
#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>
#include <QWindow>

/* GUI includes: */
#include "QILineEdit.h"
#include "QIRichTextLabel.h"
#include "QIToolButton.h"
#include "UIIconPool.h"
#include "UIUserNamePasswordEditor.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include "iprt/assert.h"


UIPasswordLineEdit::UIPasswordLineEdit(QWidget *pParent /*= 0 */)
    : QLineEdit(pParent)
    , m_pTextVisibilityButton(0)
    , m_pErrorIconLabel(0)
    , m_fMarkForError(false)
{
    prepare();
}

void UIPasswordLineEdit::toggleTextVisibility(bool fTextVisible)
{
    AssertPtrReturnVoid(m_pTextVisibilityButton);

    if (fTextVisible)
    {
        setEchoMode(QLineEdit::Normal);
        if (m_pTextVisibilityButton)
            m_pTextVisibilityButton->setIcon(UIIconPool::iconSet(":/eye_closed_10px.png"));
    }
    else
    {
        setEchoMode(QLineEdit::Password);
        if (m_pTextVisibilityButton)
            m_pTextVisibilityButton->setIcon(UIIconPool::iconSet(":/eye_10px.png"));
    }
}

void UIPasswordLineEdit::mark(bool fError, const QString &strErrorToolTip)
{
    /* Check if something really changed: */
    if (m_fMarkForError == fError &&  m_strErrorToolTip == strErrorToolTip)
        return;

    /* Save new values: */
    m_fMarkForError = fError;
    m_strErrorToolTip = strErrorToolTip;

    /* Update accordingly: */
    if (m_fMarkForError)
    {
        /* Create label if absent: */
        if (!m_pErrorIconLabel)
            m_pErrorIconLabel = new QLabel(this);

        /* Update label content, visibility & position: */
        const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) * .625;
        const int iShift = height() > iIconMetric ? (height() - iIconMetric) / 2 : 0;
        const qreal fDevicePixelRatio = window() && window()->windowHandle() ? window()->windowHandle()->devicePixelRatio() : 1;
        m_pErrorIconLabel->setPixmap(m_markIcon.pixmap(QSize(iIconMetric, iIconMetric), fDevicePixelRatio));
        m_pErrorIconLabel->setToolTip(m_strErrorToolTip);
        int iIconX = width() - iIconMetric - iShift;
        if (m_pTextVisibilityButton)
            iIconX -= m_pTextVisibilityButton->width() - iShift;
        m_pErrorIconLabel->move(iIconX, iShift);
        m_pErrorIconLabel->show();
    }
    else
    {
        /* Hide label: */
        if (m_pErrorIconLabel)
            m_pErrorIconLabel->hide();
    }
}

void UIPasswordLineEdit::prepare()
{
    m_markIcon = UIIconPool::iconSet(":/status_error_16px.png");
    /* Prepare text visibility button: */
    m_pTextVisibilityButton = new QIToolButton(this);
    if (m_pTextVisibilityButton)
    {
        m_pTextVisibilityButton->setIconSize(QSize(10, 10));
        m_pTextVisibilityButton->setFocusPolicy(Qt::ClickFocus);
        m_pTextVisibilityButton->setCursor(Qt::ArrowCursor);
        m_pTextVisibilityButton->show();
        connect(m_pTextVisibilityButton, &QToolButton::clicked, this, &UIPasswordLineEdit::sltHandleTextVisibilityChange);
    }
    m_pErrorIconLabel = new QLabel(this);
    toggleTextVisibility(false);
    adjustTextVisibilityButtonGeometry();
}

void UIPasswordLineEdit::adjustTextVisibilityButtonGeometry()
{
    AssertPtrReturnVoid(m_pTextVisibilityButton);

#ifdef VBOX_WS_MAC
    /* Do not forget to update QIToolButton size on macOS, it's FIXED: */
    m_pTextVisibilityButton->setFixedSize(m_pTextVisibilityButton->minimumSizeHint());
    /* Calculate suitable position for a QIToolButton, it's FRAMELESS: */
    const int iWidth = m_pTextVisibilityButton->width();
    const int iMinHeight = qMin(height(), m_pTextVisibilityButton->height());
    const int iMaxHeight = qMax(height(), m_pTextVisibilityButton->height());
    const int iHalfHeightDiff = (iMaxHeight - iMinHeight) / 2;
    m_pTextVisibilityButton->setGeometry(width() - iWidth - iHalfHeightDiff, iHalfHeightDiff, iWidth, iWidth);
#else
    int iFrameWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth);
    int iSize = height() - 2 * iFrameWidth;
    m_pTextVisibilityButton->setGeometry(width() - iSize, iFrameWidth, iSize, iSize);
#endif
}

void UIPasswordLineEdit::resizeEvent(QResizeEvent *pEvent)
{
    /* Call to base-class: */
    QLineEdit::resizeEvent(pEvent);
    adjustTextVisibilityButtonGeometry();

    /* Update error label position: */
    if (m_pErrorIconLabel)
    {
        const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) * .625;
        const int iShift = height() > iIconMetric ? (height() - iIconMetric) / 2 : 0;
        int iIconX = width() - iIconMetric - iShift;
        if (m_pTextVisibilityButton)
            iIconX -= m_pTextVisibilityButton->width() - iShift;
        m_pErrorIconLabel->move(iIconX, iShift);
    }
}

void UIPasswordLineEdit::sltHandleTextVisibilityChange()
{
    bool fTextVisible = false;
    if (echoMode() == QLineEdit::Normal)
        fTextVisible = false;
    else
        fTextVisible = true;
    toggleTextVisibility(fTextVisible);
    emit sigTextVisibilityToggled(fTextVisible);
}


/*********************************************************************************************************************************
*   UIUserNamePasswordEditor implementation.                                                                                     *
*********************************************************************************************************************************/

UIUserNamePasswordEditor::UIUserNamePasswordEditor(QWidget *pParent /*  = 0 */)
    : QWidget(pParent)
    , m_pUserNameLineEdit(0)
    , m_pPasswordLineEdit(0)
    , m_pPasswordRepeatLineEdit(0)
    , m_pUserNameLabel(0)
    , m_pPasswordLabel(0)
    , m_pPasswordRepeatLabel(0)
    , m_fShowPlaceholderText(true)
    , m_fLabelsVisible(true)
{
    prepare();
}

QString UIUserNamePasswordEditor::userName() const
{
    if (m_pUserNameLineEdit)
        return m_pUserNameLineEdit->text();
    return QString();
}

void UIUserNamePasswordEditor::setUserName(const QString &strUserName)
{
    if (m_pUserNameLineEdit)
        return m_pUserNameLineEdit->setText(strUserName);
}

QString UIUserNamePasswordEditor::password() const
{
    if (m_pPasswordLineEdit)
        return m_pPasswordLineEdit->text();
    return QString();
}

void UIUserNamePasswordEditor::setPassword(const QString &strPassword)
{
    if (m_pPasswordLineEdit)
        m_pPasswordLineEdit->setText(strPassword);
    if (m_pPasswordRepeatLineEdit)
        m_pPasswordRepeatLineEdit->setText(strPassword);
}

bool UIUserNamePasswordEditor::isUserNameComplete()
{
    bool fComplete = (m_pUserNameLineEdit && !m_pUserNameLineEdit->text().isEmpty());
    if (m_pUserNameLineEdit)
        m_pUserNameLineEdit->mark(!fComplete, UIUserNamePasswordEditor::tr("Username cannot be an empty string"),
                                  UIUserNamePasswordEditor::tr("Username is valid"));
    return fComplete;
}

bool UIUserNamePasswordEditor::isPasswordComplete()
{
    bool fPasswordOK = true;
    if (m_pPasswordLineEdit && m_pPasswordRepeatLineEdit)
    {
        if (m_pPasswordLineEdit->text() != m_pPasswordRepeatLineEdit->text())
            fPasswordOK = false;
        if (m_pPasswordLineEdit->text().isEmpty())
            fPasswordOK = false;
        m_pPasswordLineEdit->mark(!fPasswordOK, m_strPasswordError);
        m_pPasswordRepeatLineEdit->mark(!fPasswordOK, m_strPasswordError);
    }
    return fPasswordOK;
}

bool UIUserNamePasswordEditor::isComplete()
{
    bool fUserNameField = isUserNameComplete();
    bool fPasswordField = isPasswordComplete();
    return fUserNameField && fPasswordField;
}

void UIUserNamePasswordEditor::setPlaceholderTextEnabled(bool fEnabled)
{
    if (m_fShowPlaceholderText == fEnabled)
        return;
    m_fShowPlaceholderText = fEnabled;
    sltRetranslateUI();
}

void UIUserNamePasswordEditor::setLabelsVisible(bool fVisible)
{
    if (m_fLabelsVisible == fVisible)
        return;
    m_fLabelsVisible = fVisible;
    m_pUserNameLabel->setVisible(fVisible);
    m_pPasswordLabel->setVisible(fVisible);
    m_pPasswordRepeatLabel->setVisible(fVisible);

}

void UIUserNamePasswordEditor::sltRetranslateUI()
{
    QString strPassword = tr("Pass&word");
    QString strRepeatPassword = tr("&Repeat Password");
    QString strUsername = tr("U&sername");
    if (m_pUserNameLabel)
        m_pUserNameLabel->setText(QString("%1%2").arg(strUsername).arg(":"));

    if (m_pPasswordLabel)
        m_pPasswordLabel->setText(QString("%1%2").arg(strPassword).arg(":"));

    if (m_pPasswordRepeatLabel)
        m_pPasswordRepeatLabel->setText(QString("%1%2").arg(strRepeatPassword).arg(":"));

    if (m_fShowPlaceholderText)
    {
        if(m_pUserNameLineEdit)
            m_pUserNameLineEdit->setPlaceholderText(strUsername.remove('&'));
        if (m_pPasswordLineEdit)
            m_pPasswordLineEdit->setPlaceholderText(strPassword.remove('&'));
        if (m_pPasswordRepeatLineEdit)
            m_pPasswordRepeatLineEdit->setPlaceholderText(strRepeatPassword.remove('&'));
    }
    else
    {
        if(m_pUserNameLineEdit)
            m_pUserNameLineEdit->setPlaceholderText(QString());
        if (m_pPasswordLineEdit)
            m_pPasswordLineEdit->setPlaceholderText(QString());
        if (m_pPasswordRepeatLineEdit)
            m_pPasswordRepeatLineEdit->setPlaceholderText(QString());
    }
    if(m_pUserNameLineEdit)
        m_pUserNameLineEdit->setToolTip(tr("Holds username."));
    if (m_pPasswordLineEdit)
        m_pPasswordLineEdit->setToolTip(tr("Holds password."));
    if (m_pPasswordRepeatLineEdit)
        m_pPasswordRepeatLineEdit->setToolTip(tr("Holds the repeated password."));
    m_strPasswordError = tr("Invalid password pair");
}

template <class T>
void UIUserNamePasswordEditor::addLineEdit(int &iRow, QLabel *&pLabel, T *&pLineEdit, QGridLayout *pLayout)
{
    if (!pLayout || pLabel || pLineEdit)
        return;
    pLabel = new QLabel;
    if (!pLabel)
        return;
    pLabel->setAlignment(Qt::AlignRight);
    pLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    pLayout->addWidget(pLabel, iRow, 0, 1, 1);

    pLineEdit = new T;
    if (!pLineEdit)
        return;
    pLayout->addWidget(pLineEdit, iRow, 1, 1, 3);

    pLabel->setBuddy(pLineEdit);
    ++iRow;
    return;
}

void UIUserNamePasswordEditor::prepare()
{
    QGridLayout *pMainLayout = new QGridLayout;
    pMainLayout->setColumnStretch(0, 0);
    pMainLayout->setColumnStretch(1, 1);
    if (!pMainLayout)
        return;
    setLayout(pMainLayout);
    int iRow = 0;
    addLineEdit<QILineEdit>(iRow, m_pUserNameLabel, m_pUserNameLineEdit, pMainLayout);
    addLineEdit<UIPasswordLineEdit>(iRow, m_pPasswordLabel, m_pPasswordLineEdit, pMainLayout);
    addLineEdit<UIPasswordLineEdit>(iRow, m_pPasswordRepeatLabel, m_pPasswordRepeatLineEdit, pMainLayout);

    m_pUserNameLineEdit->setMarkable(true);

    connect(m_pPasswordLineEdit, &UIPasswordLineEdit::sigTextVisibilityToggled,
            this, &UIUserNamePasswordEditor::sltHandlePasswordVisibility);
    connect(m_pPasswordRepeatLineEdit, &UIPasswordLineEdit::sigTextVisibilityToggled,
            this, &UIUserNamePasswordEditor::sltHandlePasswordVisibility);
    connect(m_pPasswordLineEdit, &UIPasswordLineEdit::textChanged,
            this, &UIUserNamePasswordEditor::sltPasswordChanged);
    connect(m_pPasswordRepeatLineEdit, &UIPasswordLineEdit::textChanged,
            this, &UIUserNamePasswordEditor::sltPasswordChanged);
    connect(m_pUserNameLineEdit, &QILineEdit::textChanged,
            this, &UIUserNamePasswordEditor::sltUserNameChanged);

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIUserNamePasswordEditor::sltRetranslateUI);
}

void UIUserNamePasswordEditor::sltHandlePasswordVisibility(bool fPasswordVisible)
{
    if (m_pPasswordLineEdit)
        m_pPasswordLineEdit->toggleTextVisibility(fPasswordVisible);
    if (m_pPasswordRepeatLineEdit)
        m_pPasswordRepeatLineEdit->toggleTextVisibility(fPasswordVisible);
}

void UIUserNamePasswordEditor::sltUserNameChanged()
{
    isUserNameComplete();
    emit sigUserNameChanged(m_pUserNameLineEdit->text());
}

void UIUserNamePasswordEditor::sltPasswordChanged()
{
    isPasswordComplete();
    emit sigPasswordChanged(m_pPasswordLineEdit->text());
}
