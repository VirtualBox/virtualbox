/* $Id: QIStyledItemDelegate.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIStyledItemDelegate class implementation.
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

/* GUI includes: */
#include "QIStyledItemDelegate.h"


QIStyledItemDelegate::QIStyledItemDelegate(QObject *pParent)
    : QStyledItemDelegate(pParent)
    , m_fWatchForEditorDataCommits(false)
    , m_fWatchForEditorEnterKeyTriggering(false)
{
}

void QIStyledItemDelegate::setWatchForEditorDataCommits(bool fWatch)
{
    m_fWatchForEditorDataCommits = fWatch;
}

void QIStyledItemDelegate::setWatchForEditorEnterKeyTriggering(bool fWatch)
{
    m_fWatchForEditorEnterKeyTriggering = fWatch;
}

QWidget *QIStyledItemDelegate::createEditor(QWidget *pParent,
                                            const QStyleOptionViewItem &option,
                                            const QModelIndex &index) const
{
    /* Call to base-class to get actual editor created: */
    QWidget *pEditor = QStyledItemDelegate::createEditor(pParent, option, index);

    /* Watch for editor data commits, redirect to listeners: */
    if (   m_fWatchForEditorDataCommits
        && pEditor->property("has_sigCommitData").toBool())
        connect(pEditor, SIGNAL(sigCommitData(QWidget *)), this, SIGNAL(commitData(QWidget *)));

    /* Watch for editor Enter key triggering, redirect to listeners: */
    if (   m_fWatchForEditorEnterKeyTriggering
        && pEditor->property("has_sigEnterKeyTriggered").toBool())
        connect(pEditor, SIGNAL(sigEnterKeyTriggered()), this, SIGNAL(sigEditorEnterKeyTriggered()));

    /* Notify listeners about editor created: */
    emit sigEditorCreated(pEditor, index);

    /* Return actual editor: */
    return pEditor;
}
