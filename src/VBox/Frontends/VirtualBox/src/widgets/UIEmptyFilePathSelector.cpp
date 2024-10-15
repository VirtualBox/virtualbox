/* $Id: UIEmptyFilePathSelector.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIEmptyFilePathSelector class implementation.
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

/* Qt includes: */
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QTimer>

/* GUI includes: */
#include "QIFileDialog.h"
#include "QIToolButton.h"
#include "QILabel.h"
#include "QILineEdit.h"
#include "UIEmptyFilePathSelector.h"
#include "UIIconPool.h"
#include "UITranslationEventListener.h"

UIEmptyFilePathSelector::UIEmptyFilePathSelector (QWidget *aParent /* = NULL */)
    : QWidget(aParent)
    , mPathWgt (NULL)
    , mLabel (NULL)
    , mMode (UIEmptyFilePathSelector::Mode_File_Open)
    , mLineEdit (NULL)
    , m_fLineEditoToolTipSet(false)
    , m_fButtonToolTipSet(false)
    , mHomeDir (QDir::current().absolutePath())
    , mIsModified (false)
{
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);

    mMainLayout = new QHBoxLayout (this);
    mMainLayout->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
    mMainLayout->setSpacing(5);
#endif

    mSelectButton = new QToolButton(this);
#ifdef VBOX_WS_MAC
    mSelectButton->setStyleSheet("QToolButton { border: 0px none black; margin: 0px 0px 0px 0px; } QToolButton::menu-indicator {image: none;}");
#else
    mSelectButton->setAutoRaise(true);
#endif
    mSelectButton->setIcon(UIIconPool::iconSet(":/select_file_16px.png", ":/select_file_disabled_16px.png"));
    connect(mSelectButton, &QToolButton::clicked, this, &UIEmptyFilePathSelector::choose);
    mMainLayout->addWidget(mSelectButton);

    setEditable (false);

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIEmptyFilePathSelector::sltRetranslateUI);
}

void UIEmptyFilePathSelector::setMode (UIEmptyFilePathSelector::Mode aMode)
{
    mMode = aMode;
}

UIEmptyFilePathSelector::Mode UIEmptyFilePathSelector::mode() const
{
    return mMode;
}

void UIEmptyFilePathSelector::setButtonPosition (ButtonPosition aPos)
{
    if (aPos == LeftPosition)
    {
        mMainLayout->setDirection (QBoxLayout::LeftToRight);
        setTabOrder (mSelectButton, mPathWgt);
    }
    else
    {
        mMainLayout->setDirection (QBoxLayout::RightToLeft);
        setTabOrder (mPathWgt, mSelectButton);
    }
}

UIEmptyFilePathSelector::ButtonPosition UIEmptyFilePathSelector::buttonPosition() const
{
    return mMainLayout->direction() == QBoxLayout::LeftToRight ? LeftPosition : RightPosition;
}

void UIEmptyFilePathSelector::setEditable (bool aOn)
{
    if (mPathWgt)
    {
        delete mPathWgt;
        mLabel = NULL;
        mLineEdit = NULL;
    }

    if (aOn)
    {
        mPathWgt = mLineEdit = new QILineEdit (this);
        setFocusProxy(mLineEdit);
        connect (mLineEdit, SIGNAL (textChanged (const QString&)),
                 this, SLOT (textChanged (const QString&)));
    }
    else
    {
        mPathWgt = mLabel = new QILabel (this);
        mLabel->setWordWrap (true);
    }
    mMainLayout->addWidget (mPathWgt, 2);
    setButtonPosition (buttonPosition());

    setPath (mPath);
}

bool UIEmptyFilePathSelector::isEditable() const
{
    return mLabel ? false : true;
}

void UIEmptyFilePathSelector::setChooserVisible (bool aOn)
{
    mSelectButton->setVisible (aOn);
}

bool UIEmptyFilePathSelector::isChooserVisible() const
{
    return mSelectButton->isVisible();
}

void UIEmptyFilePathSelector::setPath (const QString& aPath)
{
    QString tmpPath = QDir::toNativeSeparators (aPath);
    if (mLabel)
        mLabel->setText (QString ("<compact elipsis=\"start\">%1</compact>").arg (tmpPath));
    else if (mLineEdit)
        mLineEdit->setText (tmpPath);
    textChanged(tmpPath);
}

QString UIEmptyFilePathSelector::path() const
{
    return mPath;
}

void UIEmptyFilePathSelector::setDefaultSaveExt (const QString &aExt)
{
    mDefaultSaveExt = aExt;
}

QString UIEmptyFilePathSelector::defaultSaveExt() const
{
    return mDefaultSaveExt;
}

void UIEmptyFilePathSelector::setLineEditToolTip(const QString &strToolTip)
{
    if (mLineEdit)
    {
        m_fLineEditoToolTipSet = !strToolTip.isEmpty();
        mLineEdit->setToolTip(strToolTip);
    }
}

QString UIEmptyFilePathSelector::lineEditToolTip() const
{
    return mLineEdit ? mLineEdit->toolTip() : QString();
}

void UIEmptyFilePathSelector::setChooseButtonToolTip(const QString &strToolTip)
{
    m_fButtonToolTipSet = !strToolTip.isEmpty();
    mSelectButton->setToolTip(strToolTip);
    mSelectButton->setText(strToolTip);
}

QString UIEmptyFilePathSelector::chooseButtonToolTip() const
{
    return mSelectButton->toolTip();
}

void UIEmptyFilePathSelector::setFileDialogTitle (const QString& aTitle)
{
    mFileDialogTitle = aTitle;
}

QString UIEmptyFilePathSelector::fileDialogTitle() const
{
    return mFileDialogTitle;
}

void UIEmptyFilePathSelector::setFileFilters (const QString& aFilters)
{
    mFileFilters = aFilters;
}

QString UIEmptyFilePathSelector::fileFilters() const
{
    return mFileFilters;
}

void UIEmptyFilePathSelector::setHomeDir (const QString& aDir)
{
    mHomeDir = aDir;
}

QString UIEmptyFilePathSelector::homeDir() const
{
    return mHomeDir;
}

void UIEmptyFilePathSelector::sltRetranslateUI()
{
    if (mLineEdit && !m_fLineEditoToolTipSet)
        mLineEdit->setToolTip(tr("Contains selected file path."));
    if (!m_fButtonToolTipSet)
    {
        mSelectButton->setToolTip(tr("Choose..."));
        mSelectButton->setText(tr("Choose..."));
    }
}

void UIEmptyFilePathSelector::choose()
{
    QString path = mPath;

    /* Check whether we have file-name information available: */
    const QString strFileName = QFileInfo(path).fileName();

    /* Preparing initial directory. */
    QString initDir = path.isNull() ? mHomeDir :
        QIFileDialog::getFirstExistingDir (path);
    if (initDir.isNull())
        initDir = mHomeDir;

    /* Append file-name information if any: */
    if (!strFileName.isEmpty())
        initDir = QDir(initDir).absoluteFilePath(strFileName);

    switch (mMode)
    {
        case UIEmptyFilePathSelector::Mode_File_Open:
            path = QIFileDialog::getOpenFileName (initDir, mFileFilters, window(), mFileDialogTitle); break;
        case UIEmptyFilePathSelector::Mode_File_Save:
        {
            path = QIFileDialog::getSaveFileName (initDir, mFileFilters, window(), mFileDialogTitle);
            if (!path.isEmpty() && QFileInfo (path).suffix().isEmpty())
                path = QString ("%1.%2").arg (path).arg (mDefaultSaveExt);
            break;
        }
        case UIEmptyFilePathSelector::Mode_Folder:
            path = QIFileDialog::getExistingDirectory (initDir, window(), mFileDialogTitle); break;
    }
    if (path.isEmpty())
        return;

    path.remove(QRegularExpression("[\\\\/]$"));
    setPath (path);
}

void UIEmptyFilePathSelector::textChanged (const QString& aPath)
{
    const QString oldPath = mPath;
    mPath = aPath;
    if (oldPath != mPath)
    {
        mIsModified = true;
        emit pathChanged (mPath);
    }
}
