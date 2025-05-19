/* $Id: UIDescriptionEditor.cpp 168930 2025-05-19 15:06:14Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDescriptionEditor class implementation.
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
#include <QTextEdit>
#include <QVBoxLayout>

/* GUI includes: */
#include "UIDescriptionEditor.h"


UIDescriptionEditor::UIDescriptionEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_pTextEdit(0)
{
    prepare();
}

void UIDescriptionEditor::setValue(const QString &strValue)
{
    /* Update cached value and
     * text-edit if value has changed: */
    if (m_strValue != strValue)
    {
        m_strValue = strValue;
        if (m_pTextEdit)
            m_pTextEdit->setPlainText(strValue);
    }
}

QString UIDescriptionEditor::value() const
{
    return m_pTextEdit ? m_pTextEdit->toPlainText() : m_strValue;
}

void UIDescriptionEditor::sltRetranslateUI()
{
    if (m_pTextEdit)
    {
        m_pTextEdit->setToolTip(tr("Description of the VM's configuration"));
        m_pTextEdit->setProperty("description", QApplication::translate("UICommon", "Description", "DetailsElementType"));
    }
}

QSize UIDescriptionEditor::minimumSizeHint() const
{
    /* Calculate on the basis of font metrics: */
    QFontMetrics fm(m_pTextEdit->font());
    /// @todo that's somehow glitches on arm macOS, some other widget influencing this,
    ///       need to check why layout srinks if value set to be less ..
    // approx. 80 symbols, not very precise:
    const int iWidth = fm.averageCharWidth() * 80;
    // exact. 7 symbols, quite precise:
    const int iHeight = fm.lineSpacing() * 7
                      + m_pTextEdit->document()->documentMargin() * 2
                      + m_pTextEdit->frameWidth() * 2;
    return QSize(iWidth, iHeight);
}

QSize UIDescriptionEditor::sizeHint() const
{
    return minimumSizeHint();
}

void UIDescriptionEditor::prepare()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        pLayout->setContentsMargins(0, 0, 0, 0);

        /* Prepare text-edit: */
        m_pTextEdit = new QTextEdit(this);
        if (m_pTextEdit)
        {
            setFocusProxy(m_pTextEdit);
            m_pTextEdit->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);
            m_pTextEdit->setAcceptRichText(false);

            pLayout->addWidget(m_pTextEdit);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
}
