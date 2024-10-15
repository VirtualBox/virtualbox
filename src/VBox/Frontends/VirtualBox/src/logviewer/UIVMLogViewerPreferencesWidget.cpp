/* $Id: UIVMLogViewerPreferencesWidget.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMLogViewer class implementation.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <QHBoxLayout>
#include <QFontDatabase>
#include <QFontDialog>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

/* GUI includes: */
#include "QIToolButton.h"
#include "UIIconPool.h"
#include "UITranslationEventListener.h"
#include "UIVMLogViewerPreferencesWidget.h"
#include "UIVMLogViewerWidget.h"

/* Other VBox includes: */
#include <iprt/assert.h>


UIVMLogViewerPreferencesWidget::UIVMLogViewerPreferencesWidget(QWidget *pParent, UIVMLogViewerWidget *pViewer)
    : UIVMLogViewerPane(pParent, pViewer)
    , m_pLineNumberCheckBox(0)
    , m_pWrapLinesCheckBox(0)
    , m_pFontSizeSpinBox(0)
    , m_pFontSizeLabel(0)
    , m_pOpenFontDialogButton(0)
    , m_pResetToDefaultsButton(0)
    , m_iDefaultFontSize(9)
{
    prepareWidgets();
    prepareConnections();
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIVMLogViewerPreferencesWidget::sltRetranslateUI);
}

void UIVMLogViewerPreferencesWidget::setShowLineNumbers(bool bShowLineNumbers)
{
    if (!m_pLineNumberCheckBox)
        return;
    if (m_pLineNumberCheckBox->isChecked() == bShowLineNumbers)
        return;
    m_pLineNumberCheckBox->setChecked(bShowLineNumbers);
}

void UIVMLogViewerPreferencesWidget::setWrapLines(bool bWrapLines)
{
    if (!m_pWrapLinesCheckBox)
        return;
    if (m_pWrapLinesCheckBox->isChecked() == bWrapLines)
        return;
    m_pWrapLinesCheckBox->setChecked(bWrapLines);
}

void UIVMLogViewerPreferencesWidget::setFontSizeInPoints(int fontSizeInPoints)
{
    if (!m_pFontSizeSpinBox)
        return;
    if (m_pFontSizeSpinBox->value() == fontSizeInPoints)
        return;
    m_pFontSizeSpinBox->setValue(fontSizeInPoints);
}

void UIVMLogViewerPreferencesWidget::prepareWidgets()
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    AssertReturnVoid(pMainLayout);

    QHBoxLayout *pContainerLayout = new QHBoxLayout;

    /* Create line-number check-box: */
    m_pLineNumberCheckBox = new QCheckBox;
    AssertReturnVoid(m_pLineNumberCheckBox);
    m_pLineNumberCheckBox->setChecked(true);
    pContainerLayout->addWidget(m_pLineNumberCheckBox, 0, Qt::AlignLeft);


    /* Create wrap-lines check-box: */
    m_pWrapLinesCheckBox = new QCheckBox;
    AssertReturnVoid(m_pWrapLinesCheckBox);
    m_pWrapLinesCheckBox->setChecked(false);
    pContainerLayout->addWidget(m_pWrapLinesCheckBox, 0, Qt::AlignLeft);

    /* Create font-size spin-box: */
    m_pFontSizeSpinBox = new QSpinBox;
    AssertReturnVoid(m_pFontSizeSpinBox);
    pContainerLayout->addWidget(m_pFontSizeSpinBox, 0, Qt::AlignLeft);
    m_pFontSizeSpinBox->setValue(m_iDefaultFontSize);
    m_pFontSizeSpinBox->setMaximum(44);
    m_pFontSizeSpinBox->setMinimum(6);


    /* Create font-size label: */
    m_pFontSizeLabel = new QLabel;
    if (m_pFontSizeLabel)
    {
        pContainerLayout->addWidget(m_pFontSizeLabel, 0, Qt::AlignLeft);
        if (m_pFontSizeSpinBox)
            m_pFontSizeLabel->setBuddy(m_pFontSizeSpinBox);
    }

    /* Create combo/button layout: */
    QHBoxLayout *pButtonLayout = new QHBoxLayout;
    if (pButtonLayout)
    {
        pButtonLayout->setContentsMargins(0, 0, 0, 0);
        pButtonLayout->setSpacing(0);

        /* Create open font dialog button: */
        m_pOpenFontDialogButton = new QIToolButton;
        if (m_pOpenFontDialogButton)
        {
            pButtonLayout->addWidget(m_pOpenFontDialogButton, 0);
            m_pOpenFontDialogButton->setIcon(UIIconPool::iconSet(":/log_viewer_choose_font_16px.png"));
        }

        /* Create reset font to default button: */
        m_pResetToDefaultsButton = new QIToolButton;
        if (m_pResetToDefaultsButton)
        {
            pButtonLayout->addWidget(m_pResetToDefaultsButton, 0);
            m_pResetToDefaultsButton->setIcon(UIIconPool::iconSet(":/log_viewer_reset_font_16px.png"));
        }

        pContainerLayout->addLayout(pButtonLayout);
    }

    pContainerLayout->addStretch(1);
    pMainLayout->addLayout(pContainerLayout);
    pMainLayout->addStretch(1);
}

void UIVMLogViewerPreferencesWidget::prepareConnections()
{
    if (m_pLineNumberCheckBox)
        connect(m_pLineNumberCheckBox, &QCheckBox::toggled, this, &UIVMLogViewerPreferencesWidget::sigShowLineNumbers);
    if (m_pWrapLinesCheckBox)
        connect(m_pWrapLinesCheckBox, &QCheckBox::toggled, this, &UIVMLogViewerPreferencesWidget::sigWrapLines);
    if (m_pFontSizeSpinBox)
        connect(m_pFontSizeSpinBox, &QSpinBox::valueChanged, this, &UIVMLogViewerPreferencesWidget::sigChangeFontSizeInPoints);
    if (m_pOpenFontDialogButton)
        connect(m_pOpenFontDialogButton, &QIToolButton::clicked, this, &UIVMLogViewerPreferencesWidget::sltOpenFontDialog);
    if (m_pResetToDefaultsButton)
        connect(m_pResetToDefaultsButton, &QIToolButton::clicked, this, &UIVMLogViewerPreferencesWidget::sigResetToDefaults);
}

void UIVMLogViewerPreferencesWidget::sltRetranslateUI()
{
    m_pLineNumberCheckBox->setText(UIVMLogViewerWidget::tr("Show Line Numbers"));
    m_pLineNumberCheckBox->setToolTip(UIVMLogViewerWidget::tr("When checked, show line numbers"));

    m_pWrapLinesCheckBox->setText(UIVMLogViewerWidget::tr("Wrap Lines"));
    m_pWrapLinesCheckBox->setToolTip(UIVMLogViewerWidget::tr("When checked, wrap lines"));

    m_pFontSizeLabel->setText(UIVMLogViewerWidget::tr("Font Size"));
    m_pFontSizeSpinBox->setToolTip(UIVMLogViewerWidget::tr("Log viewer font size"));

    m_pOpenFontDialogButton->setToolTip(UIVMLogViewerWidget::tr("Open a font dialog to select font face for the logviewer"));
    m_pResetToDefaultsButton->setToolTip(UIVMLogViewerWidget::tr("Reset options to application defaults"));
}

void UIVMLogViewerPreferencesWidget::sltOpenFontDialog()
{
    QObject *pParent = parent();
    UIVMLogViewerWidget *pLogViewer = 0;
    while(pParent)
    {
        pLogViewer = qobject_cast<UIVMLogViewerWidget*>(pParent);
        if (pLogViewer)
            break;
        pParent = pParent->parent();
    }

    if (!pLogViewer)
        return;
    QFont currentFont;
    currentFont = pLogViewer->currentFont();
    bool ok;
    QFont font =
        QFontDialog::getFont(&ok, currentFont, this, "Logviewer font");

    if (ok)
        emit sigChangeFont(font);
}
