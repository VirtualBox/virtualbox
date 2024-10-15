/* $Id: UIFilePathSelector.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFilePathSelector class implementation.
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
#include <QRegularExpression>
#ifdef VBOX_WS_WIN
# include <QListView>
#endif

/* GUI includes: */
#include "QIFileDialog.h"
#include "QILabel.h"
#include "QILineEdit.h"
#include "QIToolButton.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UIFilePathSelector.h"
#include "UIMediumEnumerator.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include <iprt/assert.h>


/** Returns first position of difference between passed strings. */
static int differFrom(const QString &str1, const QString &str2)
{
    if (str1 == str2)
        return -1;

    int iMinLength = qMin(str1.size(), str2.size());
    int iIndex = 0;
    for (iIndex = 0; iIndex < iMinLength; ++iIndex)
        if (str1[iIndex] != str2[iIndex])
            break;
    return iIndex;
}

UIFilePathSelector::UIFilePathSelector(QWidget *pParent /* = 0 */)
    : QComboBox(pParent)
    , m_enmMode(Mode_Folder)
    , m_strInitialPath(QDir::current().absolutePath())
    , m_fResetEnabled(true)
    , m_fEditable(true)
    , m_fModified(false)
    , m_fEditableMode(false)
    , m_fMouseAwaited(false)
    , m_fToolTipOverriden(false)
    , m_pCopyAction(new QAction(this))
    , m_iRecentListSeparatorPosition(ResetId + 1)
    , m_enmRecentMediaListType(UIMediumDeviceType_Invalid)
{
#ifdef VBOX_WS_WIN
    // WORKAROUND:
    // On at least Windows host there is a bug with
    // the QListView which doesn't take into account
    // the item size change caused by assigning item's
    // icon of another size or unassigning icon at all.
    if (view()->inherits("QListView"))
        qobject_cast<QListView*>(view())->setUniformItemSizes(true);
#endif /* VBOX_WS_WIN */

    /* Populate items: */
    insertItem(PathId, "");
    insertItem(SelectId, "");
    insertItem(ResetId, "");

    /* Attaching known icons: */
    setItemIcon(SelectId, UIIconPool::iconSet(":/select_file_16px.png"));
    setItemIcon(ResetId, UIIconPool::iconSet(":/eraser_16px.png"));

    /* Setup context menu: */
    addAction(m_pCopyAction);
    m_pCopyAction->setShortcut(QKeySequence(QKeySequence::Copy));
    m_pCopyAction->setShortcutContext(Qt::WidgetShortcut);

    /* Initial setup: */
    setInsertPolicy(QComboBox::NoInsert);
    setContextMenuPolicy(Qt::ActionsContextMenu);
    setMinimumWidth(200);

    /* Setup connections: */
    connect(this, &UIFilePathSelector::activated, this, &UIFilePathSelector::onActivated);
    connect(m_pCopyAction, &QAction::triggered, this, &UIFilePathSelector::copyToClipboard);
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigRecentMediaListUpdated,
            this, &UIFilePathSelector::sltRecentMediaListUpdated);

    /* Editable by default: */
    setEditable(true);

    /* Applying language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UIFilePathSelector::sltRetranslateUI);
}

void UIFilePathSelector::setMode(Mode enmMode)
{
    m_enmMode = enmMode;

    sltRetranslateUI();
}

void UIFilePathSelector::setEditable(bool fEditable)
{
    m_fEditable = fEditable;

    if (m_fEditable)
    {
        /* Call to base-class: */
        QComboBox::setEditable(true);

        /* Install combo-box event-filter: */
        this->installEventFilter(this);

        /* Configure a type of line-edit: */
        setLineEdit(new QILineEdit);

        /* Install line-edit connection/event-filter: */
        Assert(lineEdit());
        connect(lineEdit(), &QLineEdit::textEdited,
                this, &UIFilePathSelector::onTextEdited);
        lineEdit()->installEventFilter(this);
    }
    else
    {
        if (lineEdit())
        {
            /* Remove line-edit event-filter/connection: */
            lineEdit()->removeEventFilter(this);
            disconnect(lineEdit(), &QLineEdit::textEdited,
                       this, &UIFilePathSelector::onTextEdited);
        }

        /* Remove combo-box event-filter: */
        this->removeEventFilter(this);

        /* Call to base-class: */
        QComboBox::setEditable(false);
    }
}

void UIFilePathSelector::setMarkable(bool fMarkable)
{
    QILineEdit *pLineEdit = isEditable() ? qobject_cast<QILineEdit*>(lineEdit()) : 0;
    if (pLineEdit)
        pLineEdit->setMarkable(fMarkable);
}

void UIFilePathSelector::mark(bool fError, const QString &strErrorMessage, const QString &strNoErrorMessage)
{
    QILineEdit *pLineEdit = isEditable() ? qobject_cast<QILineEdit*>(lineEdit()) : 0;
    setMarkable(true);
    if (pLineEdit)
        pLineEdit->mark(fError, strErrorMessage, strNoErrorMessage);
}

void UIFilePathSelector::setResetEnabled(bool fEnabled)
{
    /* Cache requested state: */
    m_fResetEnabled = fEnabled;

    /* Update recent list separator position: */
    m_iRecentListSeparatorPosition = fEnabled ? ResetId + 1 : ResetId;

    if (!fEnabled && count() - 1 == ResetId)
        removeItem(ResetId);
    else if (fEnabled && count() - 1 == ResetId - 1)
    {
        insertItem(ResetId, "");
        setItemIcon(ResetId, UIIconPool::iconSet(":/eraser_16px.png"));
    }

    sltRecentMediaListUpdated(m_enmRecentMediaListType);
    sltRetranslateUI();
}

bool UIFilePathSelector::isValid() const
{
    if (m_strPath.isNull() || m_strPath.isEmpty())
        return false;
    QFileInfo fileInfo(m_strPath);
    if (!fileInfo.exists() || !fileInfo.isReadable())
        return false;
    return true;
}

void UIFilePathSelector::setToolTip(const QString &strToolTip)
{
    /* Call to base-class: */
    QComboBox::setToolTip(strToolTip);

    /* Remember if the tool-tip overriden: */
    m_fToolTipOverriden = !toolTip().isEmpty();
}

void UIFilePathSelector::setDefaultPath(const QString &strDefaultPath)
{
    if (m_strDefaultPath == strDefaultPath)
        return;
    m_strDefaultPath = strDefaultPath;
    if (currentIndex() == ResetId)
        setPath(m_strDefaultPath);
}

const QString& UIFilePathSelector::defaultPath() const
{
    return m_strDefaultPath;
}

void UIFilePathSelector::setRecentMediaListType(UIMediumDeviceType enmMediumType)
{
    m_enmRecentMediaListType = enmMediumType;
    sltRecentMediaListUpdated(enmMediumType);
}

UIMediumDeviceType UIFilePathSelector::recentMediaListType() const
{
    return m_enmRecentMediaListType;
}

void UIFilePathSelector::setPath(const QString &strPath, bool fRefreshText /* = true */)
{
    m_strPath = strPath.isEmpty() ? QString() :
            QDir::toNativeSeparators(strPath);
    if (fRefreshText)
        refreshText();
}

bool UIFilePathSelector::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* If the object is private combo-box: */
    if (pObject == this)
    {
        /* Handle focus events related to private child: */
        switch (pEvent->type())
        {
            case QEvent::FocusIn:  focusInEvent(static_cast<QFocusEvent*>(pEvent)); break;
            case QEvent::FocusOut: focusOutEvent(static_cast<QFocusEvent*>(pEvent)); break;
            default: break;
        }
    }

    /* If the object is private line-edit: */
    if (pObject == lineEdit())
    {
        if (m_fMouseAwaited && (pEvent->type() == QEvent::MouseButtonPress))
            QMetaObject::invokeMethod(this, "refreshText", Qt::QueuedConnection);
    }

    /* Call to base-class: */
    return QComboBox::eventFilter(pObject, pEvent);
}

void UIFilePathSelector::resizeEvent(QResizeEvent *pEvent)
{
    QComboBox::resizeEvent(pEvent);
    refreshText();
}

void UIFilePathSelector::focusInEvent(QFocusEvent *pEvent)
{
    if (isPathSelected())
    {
        if (m_fEditable)
            m_fEditableMode = true;
        if (pEvent->reason() == Qt::MouseFocusReason)
            m_fMouseAwaited = true;
        else
            refreshText();
    }
    QComboBox::focusInEvent(pEvent);
}

void UIFilePathSelector::focusOutEvent(QFocusEvent *pEvent)
{
    if (isPathSelected())
    {
        m_fEditableMode = false;
        refreshText();
    }
    QComboBox::focusOutEvent(pEvent);
}

void UIFilePathSelector::sltRetranslateUI()
{
    /* Retranslate copy action: */
    m_pCopyAction->setText(tr("&Copy"));

    /* Retranslate 'select' item: */
    setItemText(SelectId, tr("Other..."));

    /* Retranslate 'reset' item: */
    if (count() - 1 == ResetId)
        setItemText(ResetId, tr("Reset"));

    /* Set tool-tips of the above two items based on the mode: */
    switch (m_enmMode)
    {
        case Mode_Folder:
            setItemData(SelectId,
                        tr("Displays a window to select a different folder."),
                        Qt::ToolTipRole);
            setItemData(ResetId,
                        tr("Resets the folder path to the default value."),
                        Qt::ToolTipRole);
            break;
        case Mode_File_Open:
        case Mode_File_Save:
            setItemData(SelectId,
                        tr("Displays a window to select a different file."),
                        Qt::ToolTipRole);
            setItemData(ResetId,
                        tr("Resets the file path to the default value."),
                        Qt::ToolTipRole);
            break;
        default:
            AssertFailedBreak();
    }

    /* If selector is NOT focused => we interpret the "nothing selected"
     * item depending on "reset to default" feature state: */
    if (isResetEnabled())
    {
        /* If "reset to default" is enabled: */
        m_strNoneText = tr("<reset to default>");
        m_strNoneToolTip = tr("The actual default path value will be displayed after "
                              "accepting the changes and opening this window again.");
    }
    else
    {
        /* If "reset to default" is NOT enabled: */
        m_strNoneText = tr("<not selected>");
        m_strNoneToolTip = tr("Please use the <b>Other...</b> item from the drop-down "
                              "list to select a path.");
    }

    /* Finally, retranslate current item: */
    refreshText();
}

void UIFilePathSelector::onActivated(int iIndex)
{
    /* Since the presence of ResetId and position of recent list separator
     * are dynamical now, we should control condition more carefully: */
    if (iIndex == SelectId)
        selectPath();
    else if (m_fResetEnabled && iIndex == ResetId)
    {
        if (m_strDefaultPath.isEmpty())
            changePath(QString());
        else
            changePath(m_strDefaultPath);
    }
    else if (iIndex >= m_iRecentListSeparatorPosition)
    {
        /* Switch back to Path item early, lineEdit() in refreshText()
         * should be related to this exactly item: */
        setCurrentIndex(PathId);
        changePath(itemText(iIndex));
    }

    setCurrentIndex(PathId);
    setFocus();
}

void UIFilePathSelector::onTextEdited(const QString &strPath)
{
    changePath(strPath, false /* refresh text? */);
}

void UIFilePathSelector::copyToClipboard()
{
    QString text(fullPath());
    /* Copy the current text to the selection and global clipboard. */
    if (QApplication::clipboard()->supportsSelection())
        QApplication::clipboard()->setText(text, QClipboard::Selection);
    QApplication::clipboard()->setText(text, QClipboard::Clipboard);
}

void UIFilePathSelector::changePath(const QString &strPath,
                                    bool fRefreshText /* = true */)
{
    const QString strOldPath = QDir::toNativeSeparators(m_strPath);
    setPath(strPath, fRefreshText);
    if (!m_fModified && m_strPath != strOldPath)
        m_fModified = true;
    emit pathChanged(strPath);
}

void UIFilePathSelector::selectPath()
{
    /* Prepare initial directory: */
    QString strInitPath;
    /* If something already chosen: */
    if (!m_strPath.isEmpty())
    {
        /* If that is just a single file/folder (object) name: */
        const QString strObjectName = QFileInfo(m_strPath).fileName();
        if (strObjectName == m_strPath)
        {
            /* Use the initial path: */
            strInitPath = m_strInitialPath;
        }
        /* If that is full file/folder (object) path: */
        else
        {
            /* Use the first existing dir of m_strPath: */
            strInitPath = QIFileDialog::getFirstExistingDir(m_strPath);
        }
        /* Finally, append object name itself: */
        strInitPath = QDir(strInitPath).absoluteFilePath(strObjectName);
    }
    /* Use the initial path by default: */
    if (strInitPath.isNull())
        strInitPath = m_strInitialPath;

    /* Open the choose-file/folder dialog: */
    QString strSelPath;
    switch (m_enmMode)
    {
        case Mode_File_Open:
            strSelPath = QIFileDialog::getOpenFileName(strInitPath, m_strFileDialogFilters, window(), m_strFileDialogTitle); break;
        case Mode_File_Save:
        {
            strSelPath = QIFileDialog::getSaveFileName(strInitPath, m_strFileDialogFilters, window(), m_strFileDialogTitle);
            if (!strSelPath.isEmpty() && QFileInfo(strSelPath).suffix().isEmpty())
            {
                if (m_strFileDialogDefaultSaveExtension.isEmpty())
                    strSelPath = QString("%1").arg(strSelPath);
                else
                    strSelPath = QString("%1.%2").arg(strSelPath).arg(m_strFileDialogDefaultSaveExtension);
            }
            break;
        }
        case Mode_Folder:
            strSelPath = QIFileDialog::getExistingDirectory(strInitPath, window(), m_strFileDialogTitle); break;
    }

    /* Do nothing if nothing chosen: */
    if (strSelPath.isNull())
        return;

    /* Wipe out excessive slashes: */
    strSelPath.remove(QRegularExpression("[\\\\/]$"));

    /* Apply chosen path: */
    changePath(strSelPath);
}

QIcon UIFilePathSelector::defaultIcon() const
{
    if (m_enmMode == Mode_Folder)
        return generalIconPool().defaultSystemIcon(QFileIconProvider::Folder);
    else
        return generalIconPool().defaultSystemIcon(QFileIconProvider::File);
}

QString UIFilePathSelector::fullPath(bool fAbsolute /* = true */) const
{
    if (m_strPath.isNull())
        return m_strPath;

    QString strResult;
    switch (m_enmMode)
    {
        case Mode_Folder:
            strResult = fAbsolute ? QDir(m_strPath).absolutePath() :
                                    QDir(m_strPath).path();
            break;
        case Mode_File_Open:
        case Mode_File_Save:
            strResult = fAbsolute ? QFileInfo(m_strPath).absoluteFilePath() :
                                    QFileInfo(m_strPath).filePath();
            break;
        default:
            AssertFailedBreak();
    }
    return QDir::toNativeSeparators(strResult);
}

QString UIFilePathSelector::shrinkText(int iWidth) const
{
    QString strFullText(fullPath(false));
    if (strFullText.isEmpty())
        return strFullText;

    const int iOldSize = fontMetrics().horizontalAdvance(strFullText);
    const int iIndentSize = fontMetrics().horizontalAdvance("x...x");

    /* Compress text: */
    int iStart = 0;
    int iFinish = 0;
    int iPosition = 0;
    int iTextWidth = 0;
    do {
        iTextWidth = fontMetrics().horizontalAdvance(strFullText);
        if (iTextWidth + iIndentSize > iWidth)
        {
            iStart = 0;
            iFinish = strFullText.length();

            /* Selecting remove position: */
            const QRegularExpression re("([\\\\/][^\\\\^/]+[\\\\/]?$)");
            const QRegularExpressionMatch mt = re.match(strFullText);
            const int iNewFinish = mt.capturedStart();
            if (iNewFinish != -1)
                iFinish = iNewFinish;
            iPosition = (iFinish - iStart) / 2;

            if (iPosition == iFinish)
               break;

            strFullText.remove(iPosition, 1);
        }
    } while (iTextWidth + iIndentSize > iWidth);

    strFullText.insert(iPosition, "...");
    const int newSize = fontMetrics().horizontalAdvance(strFullText);

    return newSize < iOldSize ? strFullText : fullPath(false);
}

void UIFilePathSelector::refreshText()
{
    if (m_fEditable && m_fEditableMode)
    {
        /* Cursor positioning variables: */
        int iCurPos = -1;
        int iDiffPos = -1;
        int iFromRight = -1;

        if (m_fMouseAwaited)
        {
            /* Store the cursor position: */
            iCurPos = lineEdit()->cursorPosition();
            iDiffPos = differFrom(lineEdit()->text(), m_strPath);
            iFromRight = lineEdit()->text().size() - iCurPos;
        }

        /* In editable mode there should be no any icon
         * and text have be corresponding real stored path
         * which can be absolute or relative. */
        if (lineEdit()->text() != m_strPath)
            setItemText(PathId, m_strPath);
        setItemIcon(PathId, QIcon());

        /* Set the tool-tip: */
        if (!m_fToolTipOverriden)
            QComboBox::setToolTip(fullPath());
        setItemData(PathId, toolTip(), Qt::ToolTipRole);

        if (m_fMouseAwaited)
        {
            m_fMouseAwaited = false;

            /* Restore the position to the right of dots: */
            if (iDiffPos != -1 && iCurPos >= iDiffPos + 3)
                lineEdit()->setCursorPosition(lineEdit()->text().size() -
                                              iFromRight);
            /* Restore the position to the center of text: */
            else if (iDiffPos != -1 && iCurPos > iDiffPos)
                lineEdit()->setCursorPosition(lineEdit()->text().size() / 2);
            /* Restore the position to the left of dots: */
            else
                lineEdit()->setCursorPosition(iCurPos);
        }
    }
    else if (m_strPath.isNull())
    {
        /* If we are not in editable mode and no path is
         * stored here - show the translated 'none' string. */
        if (itemText(PathId) != m_strNoneText)
        {
            setItemText(PathId, m_strNoneText);
            setItemIcon(PathId, QIcon());

            /* Set the tool-tip: */
            if (!m_fToolTipOverriden)
                QComboBox::setToolTip(m_strNoneToolTip);
            setItemData(PathId, toolTip(), Qt::ToolTipRole);
        }
    }
    else
    {
        /* Compress text in combobox: */
        QStyleOptionComboBox options;
        options.initFrom(this);
        QRect rect = QApplication::style()->subControlRect(
            QStyle::CC_ComboBox, &options, QStyle::SC_ComboBoxEditField);
        setItemText(PathId, shrinkText(rect.width() - iconSize().width()));

        /* Attach corresponding icon: */
        setItemIcon(PathId, QFileInfo(m_strPath).exists() ?
                            generalIconPool().defaultFileIcon(QFileInfo(m_strPath)) :
                            defaultIcon());

        /* Set the tool-tip: */
        if (!m_fToolTipOverriden)
            QComboBox::setToolTip(fullPath());
        setItemData(PathId, toolTip(), Qt::ToolTipRole);
    }
}

void UIFilePathSelector::sltRecentMediaListUpdated(UIMediumDeviceType enmMediumType)
{
    /* Remove the recent media list from the end of the combo: */
    while (count() > m_iRecentListSeparatorPosition)
        removeItem(count() - 1);

    if (enmMediumType != m_enmRecentMediaListType)
        return;
    QStringList recentMedia;

    switch (enmMediumType)
    {
        case UIMediumDeviceType_DVD:
            recentMedia = gEDataManager->recentListOfOpticalDisks();
            break;
        case UIMediumDeviceType_Floppy:
            recentMedia = gEDataManager->recentListOfFloppyDisks();
            break;
        case UIMediumDeviceType_HardDisk:
            recentMedia = gEDataManager->recentListOfHardDrives();
            break;
        default:
            break;
    }

    /* Remove the media which is not there not not readable: */
    QStringList existingMedia;
    foreach (QString strMediaPath, recentMedia)
    {
        QFileInfo info(strMediaPath);
        if (info.exists() && info.isReadable())
            existingMedia << strMediaPath;
    }
    if (existingMedia.isEmpty())
        return;

    insertSeparator(m_iRecentListSeparatorPosition);
    foreach (const QString strPath, existingMedia)
        addItem(strPath);
}
