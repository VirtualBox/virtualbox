/* $Id: UIEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIEditor class implementation.
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
#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QLabel>
#include <QLayout>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTextEdit>

/* GUI includes: */
#include "UIEditor.h"
#include "UITranslationEventListener.h"

UIEditor::UIEditor(QTabWidget *pTabWidget)
    : m_fShowInBasicMode(false)
    , m_fInExpertMode(false)
    , m_pTabWidget(pTabWidget)
{
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIEditor::sltRetranslateUI);
}

UIEditor::UIEditor(QWidget *pParent /* = 0 */, bool fShowInBasicMode /* = false */)
    : QWidget(pParent)
    , m_fShowInBasicMode(fShowInBasicMode)
    , m_fInExpertMode(true)
    , m_pTabWidget(0)
{
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIEditor::sltRetranslateUI);
}

void UIEditor::addEditor(UIEditor *pEditor)
{
    m_editors << pEditor;
}

void UIEditor::filterOut(bool fExpertMode, const QString &strFilter, const QMap<QString, QVariant> &flags)
{
    /* Save if editor is in expert mode: */
    m_fInExpertMode = fExpertMode;
    /* Save editor's optional flags: */
    m_flags = flags;

    /* Propagate filter towards all the children: */
    foreach (UIEditor *pEditor, m_editors)
        pEditor->filterOut(m_fInExpertMode, strFilter, m_flags);

    /* Make sure the editor is visible if mode and filter are suitable: */
    bool fVisible = (m_fInExpertMode || m_fShowInBasicMode) && strFilter.isEmpty();

    /* If editor still hidden we'll need to make it
     * visible if at least one of children is. */
    if (!fVisible)
    {
        foreach (UIEditor *pEditor, m_editors)
            if (pEditor->isVisibleTo(this))
            {
                fVisible = true;
                break;
            }
    }

    /* If editor still hidden and mode is suitable we'll need to make it
     * visible if at least one of descriptions suits filter well: */
    if (!fVisible && (m_fInExpertMode || m_fShowInBasicMode))
    {
        foreach (const QString &strDescription, description())
            if (strDescription.contains(strFilter, Qt::CaseInsensitive))
            {
                fVisible = true;
                break;
            }
    }

    /* Hide/show this editor special way if it's one of tab-widget tabs: */
    if (m_pTabWidget)
    {
        for (int i = 0; i < m_pTabWidget->count(); ++i)
            if (this == m_pTabWidget->widget(i))
                m_pTabWidget->setTabVisible(i, fVisible);
    }
    /* Otherwise update widget visibility usual way: */
    else
    {
        const bool fVisibilityChanged = isVisibleTo(parentWidget()) != fVisible;
        setVisible(fVisible);
        if (fVisibilityChanged)
            emit sigVisibilityChange(fVisible);
    }

    /* Finally make sure layouts are freshly
     * activated after visibility changes: */
    foreach (QLayout *pLayout, findChildren<QLayout*>())
        pLayout->activate();

    /* Call for filter change handler finally: */
    handleFilterChange();
}

QStringList UIEditor::description() const
{
    QStringList result;
    /* Clean <html> tags and &mpersands: */
    QRegularExpression re("<[^>]*>|&");

    /* Adding all the buddy labels we have: */
    foreach (QLabel *pLabel, findChildren<QLabel*>())
        if (pLabel && pLabel->buddy())
            result << pLabel->text().remove(re);

    /* Adding all the button sub-types: */
    foreach (QAbstractButton *pButton, findChildren<QAbstractButton*>())
        if (pButton)
            result << pButton->text().remove(re);

    /* Adding all the horizontal headers of abstract-item-view: */
    foreach (QAbstractItemView *pView, findChildren<QAbstractItemView*>())
        if (pView)
            if (QAbstractItemModel *pModel = pView->model())
                for (int i = 0; i < pModel->columnCount(); ++i)
                    result << pModel->headerData(i, Qt::Horizontal).toString().remove(re);

    /* Adding all the text-edits having description property: */
    foreach (QTextEdit *pTextEdit, findChildren<QTextEdit*>())
        if (pTextEdit)
        {
            const QString strDescription = pTextEdit->property("description").toString().remove(re);
            if (!strDescription.isEmpty())
                result << strDescription;
        }

    return result;
}
