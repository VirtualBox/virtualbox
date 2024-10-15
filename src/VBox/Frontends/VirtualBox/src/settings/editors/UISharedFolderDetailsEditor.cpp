/* $Id: UISharedFolderDetailsEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISharedFolderDetailsEditor class implementation.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

/* Qt includes */
#include <QCheckBox>
#include <QDir>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

/* GUI includes */
#include "QIDialogButtonBox.h"
#include "UIFilePathSelector.h"
#include "UISharedFolderDetailsEditor.h"


UISharedFolderDetailsEditor::UISharedFolderDetailsEditor(EditorType enmType,
                                                         bool fUsePermanent,
                                                         const QStringList &usedNames,
                                                         QWidget *pParent /* = 0 */)
    : QIDialog(pParent)
    , m_enmType(enmType)
    , m_fUsePermanent(fUsePermanent)
    , m_usedNames(usedNames)
    , m_pLabelPath(0)
    , m_pSelectorPath(0)
    , m_pLabelName(0)
    , m_pEditorName(0)
    , m_pLabelAutoMountPoint(0)
    , m_pEditorAutoMountPoint(0)
    , m_pCheckBoxReadonly(0)
    , m_pCheckBoxAutoMount(0)
    , m_pCheckBoxPermanent(0)
    , m_pButtonBox(0)
{
    prepare();
}

void UISharedFolderDetailsEditor::setPath(const QString &strPath)
{
    if (m_pSelectorPath)
        m_pSelectorPath->setPath(strPath);
}

QString UISharedFolderDetailsEditor::path() const
{
    return m_pSelectorPath ? m_pSelectorPath->path() : QString();
}

void UISharedFolderDetailsEditor::setName(const QString &strName)
{
    if (m_pEditorName)
        m_pEditorName->setText(strName);
}

QString UISharedFolderDetailsEditor::name() const
{
    return m_pEditorName ? m_pEditorName->text() : QString();
}

void UISharedFolderDetailsEditor::setWriteable(bool fWritable)
{
    if (m_pCheckBoxReadonly)
        m_pCheckBoxReadonly->setChecked(!fWritable);
}

bool UISharedFolderDetailsEditor::isWriteable() const
{
    return m_pCheckBoxReadonly ? !m_pCheckBoxReadonly->isChecked() : false;
}

void UISharedFolderDetailsEditor::setAutoMount(bool fAutoMount)
{
    if (m_pCheckBoxAutoMount)
        m_pCheckBoxAutoMount->setChecked(fAutoMount);
}

bool UISharedFolderDetailsEditor::isAutoMounted() const
{
    return m_pCheckBoxAutoMount ? m_pCheckBoxAutoMount->isChecked() : false;
}

void UISharedFolderDetailsEditor::setAutoMountPoint(const QString &strAutoMountPoint)
{
    if (m_pEditorAutoMountPoint)
        m_pEditorAutoMountPoint->setText(strAutoMountPoint);
}

QString UISharedFolderDetailsEditor::autoMountPoint() const
{
    return m_pEditorAutoMountPoint ? m_pEditorAutoMountPoint->text() : QString();
}

void UISharedFolderDetailsEditor::setPermanent(bool fPermanent)
{
    if (m_pCheckBoxPermanent)
        m_pCheckBoxPermanent->setChecked(fPermanent);
}

bool UISharedFolderDetailsEditor::isPermanent() const
{
    return m_fUsePermanent ? m_pCheckBoxPermanent->isChecked() : true;
}

void UISharedFolderDetailsEditor::sltRetranslateUI()
{
    switch (m_enmType)
    {
        case EditorType_Add: setWindowTitle(tr("Add Share")); break;
        case EditorType_Edit: setWindowTitle(tr("Edit Share")); break;
        default: break;
    }

    if (m_pLabelPath)
        m_pLabelPath->setText(tr("Folder Path:"));
    if (m_pLabelName)
        m_pLabelName->setText(tr("Folder Name:"));
    if (m_pEditorName)
        m_pEditorName->setToolTip(tr("Holds the name of the shared folder "
                                     "(as it will be seen by the guest OS)."));
    if (m_pSelectorPath)
        m_pSelectorPath->setToolTip(tr("Holds the path of the shared folder"));
    if (m_pButtonBox && m_pButtonBox->button(QDialogButtonBox::Ok))
        m_pButtonBox->button(QDialogButtonBox::Ok)->setToolTip(tr("Apply the changes and close this dialog"));
    if (m_pButtonBox && m_pButtonBox->button(QDialogButtonBox::Cancel))
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setToolTip(tr("Cancel"));

    if (m_pCheckBoxReadonly)
    {
        m_pCheckBoxReadonly->setText(tr("&Read-only"));
        m_pCheckBoxReadonly->setToolTip(tr("When checked, the guest OS will not be able "
                                           "to write to the specified shared folder."));
    }
    if (m_pCheckBoxAutoMount)
    {
        m_pCheckBoxAutoMount->setText(tr("&Auto-mount"));
        m_pCheckBoxAutoMount->setToolTip(tr("When checked, the guest OS will try to "
                                            "automatically mount the shared folder on startup."));
    }
    if (m_pLabelAutoMountPoint)
        m_pLabelAutoMountPoint->setText(tr("Mount point:"));
    if (m_pEditorAutoMountPoint)
        m_pEditorAutoMountPoint->setToolTip(tr("Where to automatically mount the folder in the guest.  "
                                               "A drive letter (e.g. 'G:') for Windows and OS/2 guests, path for the others.  "
                                               "If left empty the guest will pick something fitting."));
    if (m_pCheckBoxPermanent)
    {
        m_pCheckBoxPermanent->setText(tr("&Make Permanent"));
        m_pCheckBoxPermanent->setToolTip(tr("When checked, this shared folder will be permanent."));
    }
}

void UISharedFolderDetailsEditor::sltValidate()
{
    if (m_pButtonBox)
        m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(   !m_pSelectorPath->path().isEmpty()
                                                               && QDir(m_pSelectorPath->path()).exists()
                                                               && !m_pEditorName->text().trimmed().isEmpty()
                                                               && !m_pEditorName->text().contains(" ")
                                                               && !m_usedNames.contains(m_pEditorName->text()));
}

void UISharedFolderDetailsEditor::sltSelectPath()
{
    if (   !m_pSelectorPath
        || !m_pSelectorPath->isPathSelected())
        return;

    QString strFolderName(m_pSelectorPath->path());
#if defined (VBOX_WS_WIN) || defined (Q_OS_OS2)
    if (strFolderName[0].isLetter() && strFolderName[1] == ':' && strFolderName[2] == 0)
    {
        /* UIFilePathSelector returns root path as 'X:', which is invalid path.
         * Append the trailing backslash to get a valid root path 'X:\': */
        strFolderName += "\\";
        m_pSelectorPath->setPath(strFolderName);
    }
#endif /* VBOX_WS_WIN || Q_OS_OS2 */

    if (!m_pEditorName)
        return;

    QDir folder(strFolderName);
    if (!folder.isRoot())
    {
        /* Processing non-root folder */
        m_pEditorName->setText(folder.dirName().replace(' ', '_'));
    }
    else
    {
        /* Processing root folder: */
#if defined (VBOX_WS_WIN) || defined (Q_OS_OS2)
        m_pEditorName->setText(strFolderName.toUpper().left(1) + "_DRIVE");
#elif defined (VBOX_WS_NIX)
        m_pEditorName->setText("ROOT");
#endif
    }

    /* Validate the field values: */
    sltValidate();
}

void UISharedFolderDetailsEditor::prepare()
{
    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();

    /* Validate the initial field values: */
    sltValidate();

    /* Adjust dialog size: */
    adjustSize();

#ifdef VBOX_WS_MAC
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(minimumSize());
#endif /* VBOX_WS_MAC */
}

void UISharedFolderDetailsEditor::prepareWidgets()
{
    /* Prepare main layout: */
    QGridLayout *pLayout = new QGridLayout(this);
    if (pLayout)
    {
        pLayout->setRowStretch(6, 1);

        /* Prepare path label: */
        m_pLabelPath = new QLabel;
        if (m_pLabelPath)
        {
            m_pLabelPath->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelPath, 0, 0);
        }
        /* Prepare path selector: */
        m_pSelectorPath = new UIFilePathSelector;
        if (m_pSelectorPath)
        {
            m_pSelectorPath->setResetEnabled(false);
            m_pSelectorPath->setInitialPath(QDir::homePath());

            pLayout->addWidget(m_pSelectorPath, 0, 1);
        }

        /* Prepare name label: */
        m_pLabelName = new QLabel;
        if (m_pLabelName)
        {
            m_pLabelName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelName, 1, 0);
        }
        /* Prepare name editor: */
        m_pEditorName = new QLineEdit;
        if (m_pEditorName)
            pLayout->addWidget(m_pEditorName, 1, 1);

        /* Prepare auto-mount point: */
        m_pLabelAutoMountPoint = new QLabel;
        if (m_pLabelAutoMountPoint)
        {
            m_pLabelAutoMountPoint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelAutoMountPoint, 2, 0);
        }
        /* Prepare auto-mount editor: */
        m_pEditorAutoMountPoint = new QLineEdit;
        if (m_pEditorAutoMountPoint)
            pLayout->addWidget(m_pEditorAutoMountPoint, 2, 1);

        /* Prepare read-only check-box: */
        m_pCheckBoxReadonly = new QCheckBox;
        if (m_pCheckBoxReadonly)
            pLayout->addWidget(m_pCheckBoxReadonly, 3, 1);
        /* Prepare auto-mount check-box: */
        m_pCheckBoxAutoMount = new QCheckBox;
        if (m_pCheckBoxAutoMount)
            pLayout->addWidget(m_pCheckBoxAutoMount, 4, 1);
        /* Prepare permanent check-box: */
        m_pCheckBoxPermanent = new QCheckBox(this);
        if (m_pCheckBoxPermanent)
        {
            m_pCheckBoxPermanent->setHidden(!m_fUsePermanent);
            pLayout->addWidget(m_pCheckBoxPermanent, 5, 1);
        }

        /* Prepare button-box: */
        m_pButtonBox = new QIDialogButtonBox;
        if (m_pButtonBox)
        {
            m_pButtonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
            pLayout->addWidget(m_pButtonBox, 7, 0, 1, 2);
        }
    }
}

void UISharedFolderDetailsEditor::prepareConnections()
{
    if (m_pSelectorPath)
    {
        connect(m_pSelectorPath, &UIFilePathSelector::currentIndexChanged,
                this, &UISharedFolderDetailsEditor::sltSelectPath);
        connect(m_pSelectorPath, &UIFilePathSelector::pathChanged,
                this, &UISharedFolderDetailsEditor::sltSelectPath);
    }
    if (m_pEditorName)
        connect(m_pEditorName, &QLineEdit::textChanged,
                this, &UISharedFolderDetailsEditor::sltValidate);
    if (m_fUsePermanent)
        connect(m_pCheckBoxPermanent, &QCheckBox::toggled,
                this, &UISharedFolderDetailsEditor::sltValidate);
    if (m_pButtonBox)
    {
        connect(m_pButtonBox, &QIDialogButtonBox::accepted,
                this, &UISharedFolderDetailsEditor::accept);
        connect(m_pButtonBox, &QIDialogButtonBox::rejected,
                this, &UISharedFolderDetailsEditor::reject);
    }
}
