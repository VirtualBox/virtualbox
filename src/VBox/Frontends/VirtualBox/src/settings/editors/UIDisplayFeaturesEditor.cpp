/* $Id: UIDisplayFeaturesEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDisplayFeaturesEditor class implementation.
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
#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>

/* GUI includes: */
#include "UIDisplayFeaturesEditor.h"
#ifdef VBOX_WS_NIX
# include "VBoxUtils-nix.h"
#endif


UIDisplayFeaturesEditor::UIDisplayFeaturesEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_fActivateOnMouseHover(false)
    , m_fDisableHostScreenSaver(false)
    , m_pLabel(0)
    , m_pCheckBoxActivateOnMouseHover(0)
    , m_pCheckBoxDisableHostScreenSaver(0)
{
    prepare();
}

void UIDisplayFeaturesEditor::setActivateOnMouseHover(bool fOn)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fActivateOnMouseHover != fOn)
    {
        m_fActivateOnMouseHover = fOn;
        if (m_pCheckBoxActivateOnMouseHover)
            m_pCheckBoxActivateOnMouseHover->setCheckState(m_fActivateOnMouseHover ? Qt::Checked : Qt::Unchecked);
    }
}

bool UIDisplayFeaturesEditor::activateOnMouseHover() const
{
    return   m_pCheckBoxActivateOnMouseHover
           ? m_pCheckBoxActivateOnMouseHover->checkState() == Qt::Checked
           : m_fActivateOnMouseHover;
}

void UIDisplayFeaturesEditor::setDisableHostScreenSaver(bool fOn)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fDisableHostScreenSaver != fOn)
    {
        m_fDisableHostScreenSaver = fOn;
        if (m_pCheckBoxDisableHostScreenSaver)
            m_pCheckBoxDisableHostScreenSaver->setCheckState(m_fDisableHostScreenSaver ? Qt::Checked : Qt::Unchecked);
    }
}

bool UIDisplayFeaturesEditor::disableHostScreenSaver() const
{
    return   m_pCheckBoxDisableHostScreenSaver
           ? m_pCheckBoxDisableHostScreenSaver->checkState() == Qt::Checked
           : m_fDisableHostScreenSaver;
}

int UIDisplayFeaturesEditor::minimumLabelHorizontalHint() const
{
    return m_pLabel ? m_pLabel->minimumSizeHint().width() : 0;
}

void UIDisplayFeaturesEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIDisplayFeaturesEditor::sltRetranslateUI()
{
    if (m_pLabel)
        m_pLabel->setText(tr("Extended Features:"));

    if (m_pCheckBoxActivateOnMouseHover)
    {
        m_pCheckBoxActivateOnMouseHover->setText(tr("&Raise Window Under Mouse Pointer"));
        m_pCheckBoxActivateOnMouseHover->setToolTip(tr("When checked, machine windows will be raised "
                                                       "when the mouse pointer moves over them."));
    }

    if (m_pCheckBoxDisableHostScreenSaver)
    {
        m_pCheckBoxDisableHostScreenSaver->setText(tr("&Disable Host Screen Saver"));
        m_pCheckBoxDisableHostScreenSaver->setToolTip(tr("When checked, screen saver of "
                                                         "the host OS is disabled."));
    }
}

void UIDisplayFeaturesEditor::prepare()
{
    /* Prepare main layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        m_pLayout->setContentsMargins(0, 0, 0, 0);
        m_pLayout->setColumnStretch(1, 1);

        /* Prepare label: */
        m_pLabel = new QLabel(this);
        if (m_pLabel)
        {
            m_pLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLayout->addWidget(m_pLabel, 0, 0);
        }
        /* Prepare 'activate on mouse hover' check-box: */
        m_pCheckBoxActivateOnMouseHover = new QCheckBox(this);
        if (m_pCheckBoxActivateOnMouseHover)
            m_pLayout->addWidget(m_pCheckBoxActivateOnMouseHover, 0, 1);
        /* Prepare 'disable host screen saver' check-box: */
#if defined(VBOX_WS_WIN)
        m_pCheckBoxDisableHostScreenSaver = new QCheckBox(this);
#elif defined(VBOX_WS_NIX)
        if (NativeWindowSubsystem::checkDBusScreenSaverServices())
            m_pCheckBoxDisableHostScreenSaver = new QCheckBox(this);
#endif /* VBOX_WS_NIX */
        if (m_pCheckBoxDisableHostScreenSaver)
            m_pLayout->addWidget(m_pCheckBoxDisableHostScreenSaver, 1, 1);
    }

    /* Apply language settings: */
    sltRetranslateUI();
}
