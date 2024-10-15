/* $Id: UIInformationRuntime.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIInformationRuntime class implementation.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#include <QHeaderView>
#include <QMenu>
#include <QVBoxLayout>
#include <QTimer>

/* GUI includes: */
#include "UIConverter.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UIInformationRuntime.h"
#include "UIGuestOSType.h"
#include "UIMachine.h"
#include "UITranslationEventListener.h"
#include "QITableWidget.h"

/* COM includes: */
#include "CDisplay.h"
#include "CGraphicsAdapter.h"
#include "CMachineDebugger.h"
#include "CVRDEServerInfo.h"

enum InfoRow
{
    InfoRow_Title = 0,
    InfoRow_Resolution,
    InfoRow_Uptime,
    InfoRow_ClipboardMode,
    InfoRow_DnDMode,
    InfoRow_ExecutionEngine,
    InfoRow_NestedPaging,
    InfoRow_UnrestrictedExecution,
    InfoRow_Paravirtualization,
    InfoRow_GuestAdditions,
    InfoRow_GuestOSType,
    InfoRow_RemoteDesktop,
    InfoRow_Max
};

/*********************************************************************************************************************************
*   UIRuntimeInfoWidget definition.                                                                                     *
*********************************************************************************************************************************/
/** A QTablWidget extention to show some runtime attributes */
class UIRuntimeInfoWidget : public QITableWidget
{

    Q_OBJECT;

public:

    UIRuntimeInfoWidget(QWidget *pParent);
    void updateScreenInfo(int iScreenId = -1);
    void updateGAsVersion();
    void updateVRDE();
    void updateClipboardMode(KClipboardMode enmMode = KClipboardMode_Max);
    void updateDnDMode(KDnDMode enmMode = KDnDMode_Max);
    QString tableData() const;

private slots:

    void sltRetranslateUI();
    void sltTimeout();

private:

    void createInfoRows();
    void updateUpTime();
    void updateTitleRow();
    void updateOSTypeRow();
    void updateVirtualizationInfo();

    /** Searches the table for the @p item of enmLine and replaces its text. if not found inserts a new
      * row to the end of the table. Assumes only one line of the @p enmLine exists. */
    void updateInfoRow(InfoRow enmLine, const QString &strColumn0, const QString &strColumn1);
    QString screenResolution(int iScreenId);
    /** Creates to QITableWidgetItems of the @enmInfoRow using the @p strLabel and @p strInfo and inserts it
     * to the row @p iRow. If @p iRow is -1 then the items inserted to the end of the table. */
    void insertInfoRow(InfoRow enmInfoRow, const QString& strLabel, const QString &strInfo, int iRow = -1);
    void computeMinimumWidth();

    /** @name Cached translated strings.
      * @{ */
        QString m_strTableTitle;
        QString m_strScreenResolutionLabel;
        QString m_strMonitorTurnedOff;
        QString m_strUptimeLabel;
        QString m_strClipboardModeLabel;
        QString m_strDragAndDropLabel;
        QString m_strExcutionEngineLabel;
        QString m_strNestedPagingLabel;
        QString m_strUnrestrictedExecutionLabel;
        QString m_strParavirtualizationLabel;
        QString m_strNestedPagingActive;
        QString m_strNestedPagingInactive;
        QString m_strUnrestrictedExecutionActive;
        QString m_strUnrestrictedExecutionInactive;
        QString m_strVRDEPortNotAvailable;
        QString m_strGuestAdditionsLabel;
        QString m_strGuestOSTypeLabel;
        QString m_strRemoteDesktopLabel;
        QString m_strExecutionEngineNotSet;
        QString m_strOSNotDetected;
        QString m_strGANotDetected;
    /** @} */

    int m_iFontHeight;
    /** Computed by computing the maximum length line. Used to avoid having horizontal scroll bars. */
    int m_iMinimumWidth;
    QVector<QString> m_screenResolutions;
    QVector<QString*> m_labels;
    QTimer *m_pTimer;
};

/*********************************************************************************************************************************
*   UIRuntimeInfoWidget implementation.                                                                                     *
*********************************************************************************************************************************/

UIRuntimeInfoWidget::UIRuntimeInfoWidget(QWidget *pParent)
    : QITableWidget(pParent)
    , m_iMinimumWidth(0)
    , m_pTimer(0)
{
    setContextMenuPolicy(Qt::CustomContextMenu);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAlternatingRowColors(true);
    m_iFontHeight = QFontMetrics(font()).height();

    setColumnCount(2);
    verticalHeader()->hide();
    horizontalHeader()->hide();
    setShowGrid(false);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setFocusPolicy(Qt::NoFocus);
    setSelectionMode(QAbstractItemView::NoSelection);

    m_pTimer = new QTimer(this);
    if (m_pTimer)
    {
        connect(m_pTimer, &QTimer::timeout, this, &UIRuntimeInfoWidget::sltTimeout);
        m_pTimer->start(5000);
    }

    m_labels << &m_strScreenResolutionLabel
             << &m_strUptimeLabel
             << &m_strDragAndDropLabel
             << &m_strExcutionEngineLabel
             << &m_strNestedPagingLabel
             << &m_strUnrestrictedExecutionLabel
             << &m_strParavirtualizationLabel
             << &m_strGuestAdditionsLabel
             << &m_strGuestOSTypeLabel
             << &m_strRemoteDesktopLabel;


    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIRuntimeInfoWidget::sltRetranslateUI);
    computeMinimumWidth();
}

void UIRuntimeInfoWidget::sltRetranslateUI()
{
    m_strTableTitle                    = QApplication::translate("UIVMInformationDialog", "Runtime Attributes");
    m_strScreenResolutionLabel         = QApplication::translate("UIVMInformationDialog", "Screen Resolution");
    m_strMonitorTurnedOff              = QApplication::translate("UIVMInformationDialog", "turned off", "Screen");
    m_strUptimeLabel                   = QApplication::translate("UIVMInformationDialog", "VM Uptime");
    m_strClipboardModeLabel            = QApplication::translate("UIVMInformationDialog", "Clipboard Mode");
    m_strDragAndDropLabel              = QApplication::translate("UIVMInformationDialog", "Drag and Drop Mode");
    m_strExcutionEngineLabel           = QApplication::translate("UIVMInformationDialog", "VM Execution Engine");
    m_strNestedPagingLabel             = QApplication::translate("UIVMInformationDialog", "Nested Paging");
    m_strUnrestrictedExecutionLabel    = QApplication::translate("UIVMInformationDialog", "Unrestricted Execution");
    m_strParavirtualizationLabel       = QApplication::translate("UIVMInformationDialog", "Paravirtualization Interface");
    m_strNestedPagingActive            = QApplication::translate("UIVMInformationDialog", "Active", "Nested Paging");
    m_strNestedPagingInactive          = QApplication::translate("UIVMInformationDialog", "Inactive", "Nested Paging");
    m_strUnrestrictedExecutionActive   = QApplication::translate("UIVMInformationDialog", "Active", "Unrestricted Execution");
    m_strUnrestrictedExecutionInactive = QApplication::translate("UIVMInformationDialog", "Inactive", "Unrestricted Execution");
    m_strVRDEPortNotAvailable          = QApplication::translate("UIVMInformationDialog", "Not Available", "VRDE Port");
    m_strGuestAdditionsLabel           = QApplication::translate("UIVMInformationDialog", "Guest Additions");
    m_strGuestOSTypeLabel              = QApplication::translate("UIVMInformationDialog", "Guest OS Type");
    m_strRemoteDesktopLabel            = QApplication::translate("UIVMInformationDialog", "Remote Desktop Server Port");
    m_strExecutionEngineNotSet         = QApplication::translate("UIVMInformationDialog", "not set", "Execution Engine");
    m_strOSNotDetected                 = QApplication::translate("UIVMInformationDialog", "Not Detected", "Guest OS Type");
    m_strGANotDetected                 = QApplication::translate("UIVMInformationDialog", "Not Detected", "Guest Additions Version");

    QString* strLongest = 0;
    foreach (QString *strLabel, m_labels)
    {
        if (!strLongest)
            strLongest = strLabel;
        if (strLabel && strLongest->length() < strLabel->length())
            strLongest = strLabel;
    }
    const QFontMetrics fontMetrics(font());
    setColumnWidth(0, 1.5 * fontMetrics.horizontalAdvance(*strLongest));

    /* Make the API calls and populate the table: */
    createInfoRows();
}

void UIRuntimeInfoWidget::insertInfoRow(InfoRow enmInfoRow, const QString& strLabel, const QString &strInfo, int iRow /* = -1 */)
{
    int iNewRow = rowCount();
    if (iRow != -1 && iRow <= iNewRow)
        iNewRow = iRow;
    insertRow(iNewRow);

    QITableWidgetItem *pItem1 = new QITableWidgetItem(strLabel);
    AssertPtrReturnVoid(pItem1);
    pItem1->setData(Qt::UserRole + 1, enmInfoRow);
    setItem(iNewRow, 1, pItem1);

    QITableWidgetItem *pItem2 = new QITableWidgetItem(strInfo);
    AssertPtrReturnVoid(pItem2);
    pItem2->setData(Qt::UserRole + 1, enmInfoRow);
    setItem(iNewRow, 2, pItem2);

    int iMargin = 0.2 * qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin);
    setRowHeight(iNewRow, 2 * iMargin + m_iFontHeight);
}

QString UIRuntimeInfoWidget::screenResolution(int iScreenID)
{
    AssertPtrReturn(gpMachine, QString());
    /* Determine resolution: */
    ulong uWidth = 0;
    ulong uHeight = 0;
    ulong uBpp = 0;
    long xOrigin = 0;
    long yOrigin = 0;
    KGuestMonitorStatus monitorStatus = KGuestMonitorStatus_Enabled;
    gpMachine->acquireGuestScreenParameters(iScreenID, uWidth, uHeight, uBpp, xOrigin, yOrigin, monitorStatus);
    QString strResolution = QString("%1x%2").arg(uWidth).arg(uHeight);
    if (uBpp)
        strResolution += QString("x%1").arg(uBpp);
    strResolution += QString(" @%1,%2").arg(xOrigin).arg(yOrigin);
    if (monitorStatus == KGuestMonitorStatus_Disabled)
    {
        strResolution += QString(" ");
        strResolution += m_strMonitorTurnedOff;
    }
    return strResolution;
}

void UIRuntimeInfoWidget::sltTimeout()
{
    updateUpTime();
}

void UIRuntimeInfoWidget::updateScreenInfo(int iScreenID /* = -1 */)
{
    AssertPtrReturnVoid(gpMachine);
    ulong uGuestScreens = 0;
    gpMachine->acquireMonitorCount(uGuestScreens);
    AssertReturnVoid(uGuestScreens > 0);

    m_screenResolutions.resize(uGuestScreens);
    if (iScreenID != -1 && iScreenID >= (int)uGuestScreens)
        return;
    if (iScreenID == -1)
    {
        for (ULONG iScreen = 0; iScreen < uGuestScreens; ++iScreen)
            m_screenResolutions[iScreen] = screenResolution(iScreen);
    }
    else
        m_screenResolutions[iScreenID] = screenResolution(iScreenID);
    /* Delete all the rows (not only the updated screen's row) and reinsert them: */
    int iRowCount = rowCount();
    for (int i = iRowCount - 1; i >= 0; --i)
    {
        QITableWidgetItem *pItem = static_cast<QITableWidgetItem*>(item(i, 1));
        if (pItem && pItem->data(Qt::UserRole + 1) == InfoRow_Resolution)
            removeRow(i);
    }
    for (ULONG iScreen = 0; iScreen < uGuestScreens; ++iScreen)
    {
        QString strLabel = uGuestScreens > 1 ?
            QString("%1 %2").arg(m_strScreenResolutionLabel).arg(QString::number(iScreen)) :
            QString("%1").arg(m_strScreenResolutionLabel);
        /* Insert the screen resolution row at the top of the table. Row 0 is the title row: */
        insertInfoRow(InfoRow_Resolution, strLabel, m_screenResolutions[iScreen], iScreen + 1);
    }
    resizeColumnToContents(1);
    horizontalHeader()->setStretchLastSection(true);
}

void UIRuntimeInfoWidget::updateUpTime()
{
    AssertPtrReturnVoid(gpMachine);
    LONG64 uUptime;
    if (!gpMachine->acquireUptime(uUptime))
        return;
    uint64_t uUpSecs = (uUptime / 5000) * 5;
    char szUptime[32];
    uint64_t uUpDays = uUpSecs / (60 * 60 * 24);
    uUpSecs -= uUpDays * 60 * 60 * 24;
    uint64_t uUpHours = uUpSecs / (60 * 60);
    uUpSecs -= uUpHours * 60 * 60;
    uint64_t uUpMins  = uUpSecs / 60;
    uUpSecs -= uUpMins * 60;
    RTStrPrintf(szUptime, sizeof(szUptime), "%dd %02d:%02d:%02d",
                uUpDays, uUpHours, uUpMins, uUpSecs);
    QString strUptime = QString(szUptime);
    updateInfoRow(InfoRow_Uptime, QString("%1").arg(m_strUptimeLabel), strUptime);
}

void UIRuntimeInfoWidget::updateTitleRow()
{
    /* Add the title row always as 0th row: */
    QITableWidgetItem *pTitleIcon = new QITableWidgetItem("");
    AssertReturnVoid(pTitleIcon);
    pTitleIcon->setIcon(UIIconPool::iconSet(":/state_running_16px.png"));
    pTitleIcon->setData(Qt::UserRole + 1, InfoRow_Title);

    QITableWidgetItem *pTitleItem = new QITableWidgetItem(m_strTableTitle);
    AssertReturnVoid(pTitleItem);
    pTitleItem->setData(Qt::UserRole + 1, InfoRow_Title);

    QFont titleFont(font());
    titleFont.setBold(true);
    pTitleItem->setFont(titleFont);
    if (rowCount() < 1)
        insertRow(0);
    setItem(0, 0, pTitleIcon);
    setItem(0, 1, pTitleItem);
    resizeColumnToContents(0);
}

void UIRuntimeInfoWidget::updateOSTypeRow()
{
    AssertPtrReturnVoid(gpMachine);
    QString strOSType = gpMachine->osTypeId();
    if (strOSType.isEmpty())
        strOSType = m_strOSNotDetected;
    else
    {
        strOSType = gpGlobalSession->guestOSTypeManager().getDescription(strOSType);
    }
   updateInfoRow(InfoRow_GuestOSType, QString("%1").arg(m_strGuestOSTypeLabel), strOSType);
}

void UIRuntimeInfoWidget::updateVirtualizationInfo()
{
    AssertPtrReturnVoid(gpMachine);
    KVMExecutionEngine enmExecutionEngineType = gpMachine->vmExecutionEngine();

    QString strExecutionEngine;
    switch (enmExecutionEngineType)
    {
        case KVMExecutionEngine_HwVirt:
            strExecutionEngine = "VT-x/AMD-V";        /* no translation */
            break;
        case KVMExecutionEngine_Interpreter:
            strExecutionEngine = "IEM (Interpreter)"; /* no translation */
            break;
        case KVMExecutionEngine_Recompiler:
            strExecutionEngine = "IEM (Recompiler)";  /* no translation */
            break;
        case KVMExecutionEngine_NativeApi:
            strExecutionEngine = "native API";        /* no translation */
            break;
        default:
            AssertFailed();
            RT_FALL_THRU();
        case KVMExecutionEngine_NotSet:
            strExecutionEngine = m_strExecutionEngineNotSet;
            break;
    }
    QString strNestedPaging = gpMachine->isHWVirtExNestedPagingEnabled() ?
        m_strNestedPagingActive : m_strNestedPagingInactive;
    QString strUnrestrictedExecution = gpMachine->isHWVirtExUXEnabled() ?
        m_strUnrestrictedExecutionActive : m_strUnrestrictedExecutionInactive;
    QString strParavirtProvider = gpConverter->toString(gpMachine->paravirtProvider());

    updateInfoRow(InfoRow_ExecutionEngine, QString("%1").arg(m_strExcutionEngineLabel), strExecutionEngine);
    updateInfoRow(InfoRow_NestedPaging, QString("%1").arg(m_strNestedPagingLabel), strNestedPaging);
    updateInfoRow(InfoRow_UnrestrictedExecution, QString("%1").arg(m_strUnrestrictedExecutionLabel), strUnrestrictedExecution);
    updateInfoRow(InfoRow_Paravirtualization, QString("%1").arg(m_strParavirtualizationLabel), strParavirtProvider);
}

void UIRuntimeInfoWidget::updateGAsVersion()
{
    AssertPtrReturnVoid(gpMachine);
    QString strGAVersion;
    gpMachine->acquireGuestAdditionsVersion(strGAVersion);
    if (strGAVersion.isEmpty())
        strGAVersion = m_strGANotDetected;
    else
    {
        ulong uRevision = 0;
        gpMachine->acquireGuestAdditionsRevision(uRevision);
        if (uRevision != 0)
            strGAVersion += QString(" r%1").arg(uRevision);
    }
   updateInfoRow(InfoRow_GuestAdditions, QString("%1").arg(m_strGuestAdditionsLabel), strGAVersion);
}

void UIRuntimeInfoWidget::updateVRDE()
{
    AssertPtrReturnVoid(gpMachine);
    long iVRDEPort = 0;
    gpMachine->acquireVRDEServerPort(iVRDEPort);
    const QString strVRDEInfo = (iVRDEPort == 0 || iVRDEPort == -1)
                              ? m_strVRDEPortNotAvailable
                              : QString("%1").arg(iVRDEPort);
    updateInfoRow(InfoRow_RemoteDesktop, QString("%1").arg(m_strRemoteDesktopLabel), strVRDEInfo);
}

void UIRuntimeInfoWidget::updateClipboardMode(KClipboardMode enmMode /* = KClipboardMode_Max */)
{
    AssertPtrReturnVoid(gpMachine);
    if (enmMode == KClipboardMode_Max)
    {
        KClipboardMode enmClipboardMode = KClipboardMode_Max;
        gpMachine->acquireClipboardMode(enmClipboardMode);
        updateInfoRow(InfoRow_ClipboardMode, QString("%1").arg(m_strClipboardModeLabel),
                      gpConverter->toString(enmClipboardMode));
    }
    else
        updateInfoRow(InfoRow_ClipboardMode, QString("%1").arg(m_strClipboardModeLabel),
                      gpConverter->toString(enmMode));
}

void UIRuntimeInfoWidget::updateDnDMode(KDnDMode enmMode /* = KDnDMode_Max */)
{
    AssertPtrReturnVoid(gpMachine);
    if (enmMode == KDnDMode_Max)
    {
        KDnDMode enmDnDMode = KDnDMode_Max;
        gpMachine->acquireDnDMode(enmDnDMode);
        updateInfoRow(InfoRow_DnDMode, QString("%1").arg(m_strDragAndDropLabel),
                  gpConverter->toString(enmDnDMode));
    }
    else
        updateInfoRow(InfoRow_DnDMode, QString("%1").arg(m_strDragAndDropLabel),
                      gpConverter->toString(enmMode));
}

QString UIRuntimeInfoWidget::tableData() const
{
    AssertReturn(columnCount() == 3, QString());
    QStringList data;
    for (int i = 0; i < rowCount(); ++i)
    {
        /* Skip the first column as it contains only icon and no text: */
        QITableWidgetItem *pItem = static_cast<QITableWidgetItem*>(item(i, 1));
        if (!pItem)
            continue;
        QString strColumn1 = pItem ? pItem->text() : QString();
        pItem = static_cast<QITableWidgetItem*>(item(i, 2));
        if (!pItem)
            continue;
        QString strColumn2 = pItem ? pItem->text() : QString();
        if (strColumn2.isEmpty())
            data << strColumn1;
        else
            data << strColumn1 << ": " << strColumn2;
        data << "\n";
    }
    return data.join(QString());
}

void UIRuntimeInfoWidget::updateInfoRow(InfoRow enmLine, const QString &strColumn0, const QString &strColumn1)
{
    QITableWidgetItem *pItem = 0;
    for (int i = 0; i < rowCount() && !pItem; ++i)
    {
        pItem = static_cast<QITableWidgetItem*>(item(i, 2));
        if (!pItem)
            continue;
        if (pItem->data(Qt::UserRole + 1) != enmLine)
            pItem = 0;
    }
    if (!pItem)
        insertInfoRow(enmLine, strColumn0, strColumn1);
    else
        pItem->setText(strColumn1);
}

void UIRuntimeInfoWidget::createInfoRows()
{
    clear();
    setRowCount(0);
    setColumnCount(3);
    updateTitleRow();
    updateScreenInfo();
    updateUpTime();
    updateClipboardMode();
    updateDnDMode();
    updateVirtualizationInfo();
    updateGAsVersion();
    updateOSTypeRow();
    updateVRDE();
    resizeColumnToContents(1);
}

void UIRuntimeInfoWidget::computeMinimumWidth()
{
    m_iMinimumWidth = 0;
    for (int j = 0; j < columnCount(); ++j)
        m_iMinimumWidth += columnWidth(j);
}



/*********************************************************************************************************************************
*   UIInformationRuntime implementation.                                                                                     *
*********************************************************************************************************************************/

UIInformationRuntime::UIInformationRuntime(QWidget *pParent)
    : QWidget(pParent)
    , m_pMainLayout(0)
    , m_pRuntimeInfoWidget(0)
    , m_pCopyWholeTableAction(0)
{
    connect(gpMachine, &UIMachine::sigAdditionsStateChange, this, &UIInformationRuntime::sltGuestAdditionsStateChange);
    connect(gpMachine, &UIMachine::sigGuestMonitorChange, this, &UIInformationRuntime::sltGuestMonitorChange);
    connect(gpMachine, &UIMachine::sigVRDEChange, this, &UIInformationRuntime::sltVRDEChange);
    connect(gpMachine, &UIMachine::sigClipboardModeChange, this, &UIInformationRuntime::sltClipboardChange);
    connect(gpMachine, &UIMachine::sigDnDModeChange, this, &UIInformationRuntime::sltDnDModeChange);

    prepareObjects();
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UIInformationRuntime::sltRetranslateUI);
}

void UIInformationRuntime::sltRetranslateUI()
{
    if (m_pCopyWholeTableAction)
        m_pCopyWholeTableAction->setText(QApplication::translate("UIVMInformationDialog", "Copy All"));
    if (m_pRuntimeInfoWidget)
        m_pRuntimeInfoWidget->setWhatsThis(QApplication::translate("UIVMInformationDialog", "Displays the runtime information of the guest system"));
}

void UIInformationRuntime::prepareObjects()
{
    m_pMainLayout = new QVBoxLayout(this);
    if (!m_pMainLayout)
        return;
    m_pMainLayout->setSpacing(0);

    m_pRuntimeInfoWidget = new UIRuntimeInfoWidget(0);
    AssertReturnVoid(m_pRuntimeInfoWidget);
    connect(m_pRuntimeInfoWidget, &UIRuntimeInfoWidget::customContextMenuRequested,
            this, &UIInformationRuntime::sltHandleTableContextMenuRequest);
    m_pMainLayout->addWidget(m_pRuntimeInfoWidget);
    m_pRuntimeInfoWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    m_pCopyWholeTableAction = new QAction(this);
    connect(m_pCopyWholeTableAction, &QAction::triggered, this, &UIInformationRuntime::sltHandleCopyWholeTable);
}

void UIInformationRuntime::sltGuestAdditionsStateChange()
{
    if (m_pRuntimeInfoWidget)
        m_pRuntimeInfoWidget->updateGAsVersion();
}

void UIInformationRuntime::sltGuestMonitorChange(KGuestMonitorChangedEventType changeType, ulong uScreenId, QRect screenGeo)
{
    Q_UNUSED(changeType);
    Q_UNUSED(screenGeo);
    if (m_pRuntimeInfoWidget)
        m_pRuntimeInfoWidget->updateScreenInfo(uScreenId);
}

void UIInformationRuntime::sltVRDEChange()
{
    if (m_pRuntimeInfoWidget)
        m_pRuntimeInfoWidget->updateVRDE();
}

void UIInformationRuntime::sltClipboardChange(KClipboardMode enmMode)
{
    if (m_pRuntimeInfoWidget)
        m_pRuntimeInfoWidget->updateClipboardMode(enmMode);
}

void UIInformationRuntime::sltDnDModeChange(KDnDMode enmMode)
{
    if (m_pRuntimeInfoWidget)
        m_pRuntimeInfoWidget->updateDnDMode(enmMode);
}

void UIInformationRuntime::sltHandleTableContextMenuRequest(const QPoint &position)
{
    if (!m_pCopyWholeTableAction)
        return;

    QMenu menu(this);
    menu.addAction(m_pCopyWholeTableAction);
    menu.exec(mapToGlobal(position));
}

void UIInformationRuntime::sltHandleCopyWholeTable()
{
    QClipboard *pClipboard = QApplication::clipboard();
    if (!pClipboard)
        return;
    if (!m_pRuntimeInfoWidget)
        return;

    pClipboard->setText(m_pRuntimeInfoWidget->tableData(), QClipboard::Clipboard);
}

#include "UIInformationRuntime.moc"
