/* $Id: UIVMActivityOverviewModelView.cpp 168940 2025-05-20 14:00:56Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMActivityOverviewModelView class implementation.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <QHeaderView>
#include <QMouseEvent>
#include <QTimer>

/* GUI includes: */
#include "UIConverter.h"
#include "UIExtraDataDefs.h"
#include "UIGlobalSession.h"
#include "UILocalMachineStuff.h"
#include "UIMonitorCommon.h"
#include "UITranslator.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVirtualMachineItemCloud.h"
#include "UIVMActivityOverviewModelView.h"


#ifdef VBOX_WS_MAC
# include "UIWindowMenuManager.h"
#endif /* VBOX_WS_MAC */

/* COM includes: */
#include "CConsole.h"
#include "CGuest.h"
#include "CMachine.h"
#include "CMachineDebugger.h"
#include "CSession.h"
#include "KMetricType.h"

/*********************************************************************************************************************************
*   UIVMActivityOverviewCell definition.                                                                       *
*********************************************************************************************************************************/

class UIVMActivityOverviewCell : public QITableViewCell
{

    Q_OBJECT;

public:

    UIVMActivityOverviewCell(QITableViewRow *pRow);
    virtual QString text() const RT_OVERRIDE RT_FINAL;
    int columnLength(int iColumnIndex) const;
    void setText(const QString &strText);

private:

    QString m_strText;
};


/*********************************************************************************************************************************
*   UIVMActivityOverviewRow definition.                                                                       *
*********************************************************************************************************************************/

class UIVMActivityOverviewRow : public QITableViewRow
{

    Q_OBJECT;

public:

    UIVMActivityOverviewRow(QITableView *pTableView, const QUuid &uMachineId,
                                    const QString &strMachineName);

    const QUuid &machineId() const;

    virtual void setMachineState(int iState) = 0;
    virtual bool isRunning() const = 0;
    virtual bool isCloudVM() const = 0;

    virtual ~UIVMActivityOverviewRow();
    virtual int childCount() const RT_OVERRIDE RT_FINAL;

    virtual QITableViewCell *childItem(int iIndex) const RT_OVERRIDE RT_FINAL;
    int columnLength(int iColumnIndex) const;
    QString cellText(int iColumn) const;
    virtual QString machineStateString() const = 0;

protected:
    void updateCellText(int /*VMActivityOverviewColumn*/ iColumnIndex, const QString &strText);
    QUuid m_uMachineId;
    /* Key is VMActivityOverviewColumn enum item. */
    QMap<int, UIVMActivityOverviewCell*> m_cells;

    QString m_strMachineName;
    quint64  m_uTotalRAM;

private:

    void initCells();
};

/*********************************************************************************************************************************
* UIVMActivityOverviewRowLocal definition.                                                                       *
*********************************************************************************************************************************/

class UIVMActivityOverviewRowLocal : public UIVMActivityOverviewRow
{

    Q_OBJECT;

public:

    UIVMActivityOverviewRowLocal(QITableView *pTableView, const QUuid &uMachineId,
                                         const QString &strMachineName, KMachineState enmMachineState);
    ~UIVMActivityOverviewRowLocal();
    virtual void setMachineState(int iState) RT_OVERRIDE RT_FINAL;

    virtual bool isRunning() const RT_OVERRIDE RT_FINAL;
    virtual bool isCloudVM() const RT_OVERRIDE RT_FINAL;
    virtual QString machineStateString() const RT_OVERRIDE RT_FINAL;
    void resetDebugger();
    void updateCells();
    bool isWithGuestAdditions();
    void setTotalRAM(quint64 uTotalRAM);
    void setFreeRAM(quint64 uFreeRAM);

private:

    KMachineState    m_enmMachineState;
    CMachineDebugger m_comDebugger;
    CSession         m_comSession;
    CGuest           m_comGuest;
    quint64  m_uFreeRAM;
    quint64  m_uNetworkDownTotal;
    quint64  m_uNetworkUpTotal;
    quint64          m_uVMExitTotal;
    quint64          m_uDiskWriteTotal;
    quint64          m_uDiskReadTotal;
};


/*********************************************************************************************************************************
* UIVMActivityOverviewRowCloud definition.                                                                       *
*********************************************************************************************************************************/

/* A UIVMActivityOverviewItem derivation to show cloud vms in the table view: */
class UIVMActivityOverviewRowCloud : public UIVMActivityOverviewRow
{
    Q_OBJECT;

public:

    UIVMActivityOverviewRowCloud(QITableView *pTableView, const QUuid &uMachineId,
                                         const QString &strMachineName, CCloudMachine &comCloudMachine);
    void updateMachineState();
    virtual bool isRunning() const RT_OVERRIDE RT_FINAL;
    virtual bool isCloudVM() const RT_OVERRIDE RT_FINAL;
    virtual QString machineStateString() const RT_OVERRIDE RT_FINAL;
    virtual void setMachineState(int iState) RT_OVERRIDE RT_FINAL;

private slots:

    void sltTimeout();
    void sltMetricNameListingComplete(QVector<QString> metricNameList);
    void sltMetricDataReceived(KMetricType enmMetricType,
                               const QVector<QString> &data, const QVector<QString> &timeStamps);
private:

    void getMetricList();
    void resetColumData();

    QTimer *m_pTimer;
    CCloudMachine m_comCloudMachine;
    KCloudMachineState m_enmMachineState;
    QVector<KMetricType> m_availableMetricTypes;
};


/*********************************************************************************************************************************
*   UIVMActivityOverviewCell implementation.                                                                             *
*********************************************************************************************************************************/

UIVMActivityOverviewCell::UIVMActivityOverviewCell(QITableViewRow *pRow)
    :QITableViewCell(pRow)
{
}

QString UIVMActivityOverviewCell::text() const
{
    return m_strText;
}

int UIVMActivityOverviewCell::columnLength(int /*iColumnIndex*/) const
{
    return 0;
}

void UIVMActivityOverviewCell::setText(const QString &strText)
{
    m_strText = strText;
}

/*********************************************************************************************************************************
*   UIVMActivityOverviewRow implementation.                                                                              *
*********************************************************************************************************************************/

UIVMActivityOverviewRow::UIVMActivityOverviewRow(QITableView *pTableView, const QUuid &uMachineId,
                                                                 const QString &strMachineName)
    : QITableViewRow(pTableView)
    , m_uMachineId(uMachineId)
    , m_strMachineName(strMachineName)
    , m_uTotalRAM(0)
{
    initCells();
}

int UIVMActivityOverviewRow::childCount() const
{
    return m_cells.size();
}

QITableViewCell *UIVMActivityOverviewRow::childItem(int iIndex) const
{
    return m_cells.value(iIndex, 0);
}

QString UIVMActivityOverviewRow::cellText(int iColumn) const
{
    if (!m_cells.contains(iColumn))
        return QString();
    if (!m_cells[iColumn])
        return QString();
    return m_cells[iColumn]->text();
}

int UIVMActivityOverviewRow::columnLength(int iColumnIndex) const
{
    UIVMActivityOverviewCell *pCell = m_cells.value(iColumnIndex, 0);
    if (!pCell)
        return 0;
    return pCell->text().length();
}

UIVMActivityOverviewRow::~UIVMActivityOverviewRow()
{
    qDeleteAll(m_cells);
}

void UIVMActivityOverviewRow::initCells()
{
    /* Hide VM exits in release builds: */
    for (int i = (int) VMActivityOverviewColumn_Name; i < (int) VMActivityOverviewColumn_Max; ++i)
    {
#ifndef DEBUG
        if (i == (int) VMActivityOverviewColumn_VMExits)
            continue;
#endif
        m_cells[i] = new UIVMActivityOverviewCell(this);
    }
    m_cells[VMActivityOverviewColumn_Name]->setText(m_strMachineName);
}

const QUuid &UIVMActivityOverviewRow::machineId() const
{
    return m_uMachineId;
}

void UIVMActivityOverviewRow::updateCellText(int /*VMActivityOverviewColumn*/ enmColumnIndex, const QString &strText)
{
    if (m_cells.value(enmColumnIndex, 0))
        m_cells[enmColumnIndex]->setText(strText);
}

/*********************************************************************************************************************************
* UIVMActivityOverviewRowLocal implementation.                                                                   *
*********************************************************************************************************************************/

UIVMActivityOverviewRowLocal::UIVMActivityOverviewRowLocal(QITableView *pTableView, const QUuid &uMachineId,
                                                                           const QString &strMachineName, KMachineState enmMachineState)
    : UIVMActivityOverviewRow(pTableView, uMachineId, strMachineName)
    , m_enmMachineState(enmMachineState)
    , m_uFreeRAM(0)
    , m_uNetworkDownTotal(0)
    , m_uNetworkUpTotal(0)
    , m_uVMExitTotal(0)
    , m_uDiskWriteTotal(0)
    , m_uDiskReadTotal(0)
{
    if (m_enmMachineState == KMachineState_Running)
        resetDebugger();
}

void UIVMActivityOverviewRowLocal::updateCells()
{
    /* CPU Load: */
    ULONG aPctHalted;
    ULONG  uCPUGuestLoad;
    ULONG uCPUVMMLoad;
    m_comDebugger.GetCPULoad(0x7fffffff, uCPUGuestLoad, aPctHalted, uCPUVMMLoad);
    updateCellText(VMActivityOverviewColumn_CPUVMMLoad, QString("%1%").arg(QString::number(uCPUVMMLoad)));
    updateCellText(VMActivityOverviewColumn_CPUGuestLoad, QString("%1%").arg(QString::number(uCPUGuestLoad)));

    /* RAM Utilization: */
    QString strRAMUsage;
    QString strRAMPercentage;
    int iDecimalCount = 2;
    quint64 uUsedRAM = m_uTotalRAM - m_uFreeRAM;
    float m_fRAMUsagePercentage = 0;

    if (m_uTotalRAM != 0)
        m_fRAMUsagePercentage = 100.f * (uUsedRAM / (float)m_uTotalRAM);

    if (isWithGuestAdditions())
    {
        strRAMUsage =
            QString("%1/%2").arg(UITranslator::formatSize(_1K * uUsedRAM, iDecimalCount)).
            arg(UITranslator::formatSize(_1K * m_uTotalRAM, iDecimalCount));
        strRAMPercentage =
            QString("%1%").arg(QString::number(m_fRAMUsagePercentage, 'f', 2));
    }
    else
    {
        strRAMPercentage = QApplication::translate("UIVMActivityOverviewWidget", "N/A");
        strRAMUsage = QApplication::translate("UIVMActivityOverviewWidget", "N/A");
    }

    updateCellText(VMActivityOverviewColumn_RAMUsedAndTotal, strRAMUsage);
    updateCellText(VMActivityOverviewColumn_RAMUsedPercentage, strRAMPercentage);

    /* Network rate: */
    quint64 uPrevDownTotal = m_uNetworkDownTotal;
    quint64 uPrevUpTotal = m_uNetworkUpTotal;
    UIMonitorCommon::getNetworkLoad(m_comDebugger, m_uNetworkDownTotal, m_uNetworkUpTotal);
    quint64 uNetworkDownRate = m_uNetworkDownTotal - uPrevDownTotal;
    quint64 uNetworkUpRate = m_uNetworkUpTotal - uPrevUpTotal;

    updateCellText(VMActivityOverviewColumn_NetworkUpRate, QString("%1").arg(UITranslator::formatSize(uNetworkUpRate, iDecimalCount)));
    updateCellText(VMActivityOverviewColumn_NetworkDownRate,QString("%1").arg(UITranslator::formatSize(uNetworkDownRate, iDecimalCount)));
    updateCellText(VMActivityOverviewColumn_NetworkUpTotal, QString("%1").arg(UITranslator::formatSize(m_uNetworkUpTotal, iDecimalCount)));
    updateCellText(VMActivityOverviewColumn_NetworkDownTotal, QString("%1").arg(UITranslator::formatSize(m_uNetworkDownTotal, iDecimalCount)));

    /* IO rate: */
    quint64 uPrevWriteTotal = m_uDiskWriteTotal;
    quint64 uPrevReadTotal = m_uDiskReadTotal;
    UIMonitorCommon::getDiskLoad(m_comDebugger, m_uDiskWriteTotal, m_uDiskReadTotal);
    quint64 uDiskWriteRate = m_uDiskWriteTotal - uPrevWriteTotal;
    quint64 uDiskReadRate = m_uDiskReadTotal - uPrevReadTotal;
    updateCellText(VMActivityOverviewColumn_DiskIOReadRate,QString("%1").arg(UITranslator::formatSize(uDiskReadRate, iDecimalCount)));
    updateCellText(VMActivityOverviewColumn_DiskIOWriteRate,QString("%1").arg(UITranslator::formatSize(uDiskWriteRate, iDecimalCount)));
    updateCellText(VMActivityOverviewColumn_DiskIOReadTotal,  QString("%1").arg(UITranslator::formatSize(m_uDiskReadTotal, iDecimalCount)));
    updateCellText(VMActivityOverviewColumn_DiskIOWriteTotal, QString("%1").arg(UITranslator::formatSize(m_uDiskWriteTotal, iDecimalCount)));

    /* VM Exits: */
    quint64 uPrevVMExitsTotal = m_uVMExitTotal;
    UIMonitorCommon::getVMMExitCount(m_comDebugger, m_uVMExitTotal);
    quint64 uVMExitRate = m_uVMExitTotal - uPrevVMExitsTotal;
    updateCellText(VMActivityOverviewColumn_VMExits, QString("%1/%2").arg(UITranslator::addMetricSuffixToNumber(uVMExitRate)).
                   arg(UITranslator::addMetricSuffixToNumber(m_uVMExitTotal)));
}

UIVMActivityOverviewRowLocal::~UIVMActivityOverviewRowLocal()
{
    if (!m_comSession.isNull())
        m_comSession.UnlockMachine();
}

void UIVMActivityOverviewRowLocal::resetDebugger()
{
    m_comSession = openSession(m_uMachineId, KLockType_Shared);
    if (!m_comSession.isNull())
    {
        CConsole comConsole = m_comSession.GetConsole();
        if (!comConsole.isNull())
        {
            m_comGuest = comConsole.GetGuest();
            m_comDebugger = comConsole.GetDebugger();
        }
    }
}

void UIVMActivityOverviewRowLocal::setMachineState(int iState)
{
    if (iState <= KMachineState_Null || iState >= KMachineState_Max)
        return;
    KMachineState enmState = static_cast<KMachineState>(iState);
    if (m_enmMachineState == enmState)
        return;
    m_enmMachineState = enmState;
    if (m_enmMachineState == KMachineState_Running)
        resetDebugger();
}

bool UIVMActivityOverviewRowLocal::isRunning() const
{
    return m_enmMachineState == KMachineState_Running;
}

bool UIVMActivityOverviewRowLocal::isCloudVM() const
{
    return false;
}

bool UIVMActivityOverviewRowLocal::isWithGuestAdditions()
{
    if (m_comGuest.isNull())
        return false;
    return m_comGuest.GetAdditionsStatus(m_comGuest.GetAdditionsRunLevel());
}

void UIVMActivityOverviewRowLocal::setTotalRAM(quint64 uTotalRAM)
{
    m_uTotalRAM = uTotalRAM;
}

void UIVMActivityOverviewRowLocal::setFreeRAM(quint64 uFreeRAM)
{
    m_uFreeRAM = uFreeRAM;
}

QString UIVMActivityOverviewRowLocal::machineStateString() const
{
    return gpConverter->toString(m_enmMachineState);
}

/*********************************************************************************************************************************
*   UIVMActivityOverviewRowCloud implementation.                                                                   *
*********************************************************************************************************************************/

UIVMActivityOverviewRowCloud::UIVMActivityOverviewRowCloud(QITableView *pTableView, const QUuid &uMachineId,
                                                                           const QString &strMachineName, CCloudMachine &comCloudMachine)
    : UIVMActivityOverviewRow(pTableView, uMachineId, strMachineName)
    , m_comCloudMachine(comCloudMachine)
{
    updateMachineState();
    m_pTimer = new QTimer(this);
    if (m_pTimer)
    {
        connect(m_pTimer, &QTimer::timeout, this, &UIVMActivityOverviewRowCloud::sltTimeout);
        m_pTimer->setInterval(60 * 1000);
    }
    resetColumData();
}

void UIVMActivityOverviewRowCloud::updateMachineState()
{
    if (m_comCloudMachine.isOk())
        setMachineState(m_comCloudMachine.GetState());
}

bool UIVMActivityOverviewRowCloud::isRunning() const
{
    return m_enmMachineState == KCloudMachineState_Running;
}

bool UIVMActivityOverviewRowCloud::isCloudVM() const
{
    return true;
}

QString UIVMActivityOverviewRowCloud::machineStateString() const
{
    if (!m_comCloudMachine.isOk())
        return QString();
    return gpConverter->toString(m_comCloudMachine.GetState());
}

void UIVMActivityOverviewRowCloud::sltTimeout()
{
    int iDataSize = 1;
    foreach (const KMetricType &enmMetricType, m_availableMetricTypes)
    {
        UIProgressTaskReadCloudMachineMetricData *pTask = new UIProgressTaskReadCloudMachineMetricData(this, m_comCloudMachine,
                                                                                                       enmMetricType, iDataSize);
        connect(pTask, &UIProgressTaskReadCloudMachineMetricData::sigMetricDataReceived,
                this, &UIVMActivityOverviewRowCloud::sltMetricDataReceived);
        pTask->start();
    }
}

void UIVMActivityOverviewRowCloud::sltMetricDataReceived(KMetricType enmMetricType,
                                                        const QVector<QString> &data, const QVector<QString> &timeStamps)
{
    Q_UNUSED(timeStamps);
    if (data.isEmpty())
        return;

    if (data[0].toFloat() < 0)
        return;

    int iDecimalCount = 2;
    QLocale locale;
    if (enmMetricType == KMetricType_CpuUtilization)
    {
        updateCellText(VMActivityOverviewColumn_CPUGuestLoad, QString("%1%").arg(locale.toString(data[0].toFloat(), 'f', iDecimalCount)));
    }
    else if (enmMetricType == KMetricType_MemoryUtilization)
    {
         if (m_uTotalRAM != 0)
         {
             quint64 uUsedRAM = (quint64)data[0].toFloat() * (m_uTotalRAM / 100.f);
             updateCellText(VMActivityOverviewColumn_RAMUsedAndTotal,
                            QString("%1/%2").arg(UITranslator::formatSize(_1K * uUsedRAM, iDecimalCount)).
                            arg(UITranslator::formatSize(_1K * m_uTotalRAM, iDecimalCount)));
         }
         updateCellText(VMActivityOverviewColumn_RAMUsedPercentage,
                        QString("%1%").arg(QString::number(data[0].toFloat(), 'f', iDecimalCount)));
    }
    else if (enmMetricType == KMetricType_NetworksBytesOut)
        updateCellText(VMActivityOverviewColumn_NetworkUpRate,
                       UITranslator::formatSize((quint64)data[0].toFloat(), iDecimalCount));
    else if (enmMetricType == KMetricType_NetworksBytesIn)
        updateCellText(VMActivityOverviewColumn_NetworkDownRate,
                       UITranslator::formatSize((quint64)data[0].toFloat(), iDecimalCount));
    else if (enmMetricType == KMetricType_DiskBytesRead)
        updateCellText(VMActivityOverviewColumn_DiskIOReadRate,
                       UITranslator::formatSize((quint64)data[0].toFloat(), iDecimalCount));
    else if (enmMetricType == KMetricType_DiskBytesWritten)
        updateCellText(VMActivityOverviewColumn_DiskIOWriteRate,
                       UITranslator::formatSize((quint64)data[0].toFloat(), iDecimalCount));

    sender()->deleteLater();
}

void UIVMActivityOverviewRowCloud::setMachineState(int iState)
{
    if (iState <= KCloudMachineState_Invalid || iState >= KCloudMachineState_Max)
        return;
    KCloudMachineState enmState = static_cast<KCloudMachineState>(iState);
    if (m_enmMachineState == enmState)
        return;
    m_enmMachineState = enmState;
    if (isRunning())
    {
        getMetricList();
        if (m_uTotalRAM == 0)
            m_uTotalRAM = UIMonitorCommon::determineTotalRAMAmount(m_comCloudMachine);
    }
    else
    {
        if (m_pTimer)
            m_pTimer->stop();
    }
}

void UIVMActivityOverviewRowCloud::resetColumData()
{
    for (int i = (int) VMActivityOverviewColumn_CPUGuestLoad;
         i < (int)VMActivityOverviewColumn_Max; ++i)
        updateCellText(i,  QApplication::translate("UIVMActivityOverviewWidget", "N/A"));
}

void UIVMActivityOverviewRowCloud::getMetricList()
{
    if (!isRunning())
        return;
    UIProgressTaskReadCloudMachineMetricList *pReadListProgressTask =
        new UIProgressTaskReadCloudMachineMetricList(this, m_comCloudMachine);
    AssertPtrReturnVoid(pReadListProgressTask);
    connect(pReadListProgressTask, &UIProgressTaskReadCloudMachineMetricList::sigMetricListReceived,
            this, &UIVMActivityOverviewRowCloud::sltMetricNameListingComplete);
    pReadListProgressTask->start();
}

void UIVMActivityOverviewRowCloud::sltMetricNameListingComplete(QVector<QString> metricNameList)
{
    AssertReturnVoid(m_pTimer);
    m_availableMetricTypes.clear();
    foreach (const QString &strName, metricNameList)
        m_availableMetricTypes << gpConverter->fromInternalString<KMetricType>(strName);

    if (!m_availableMetricTypes.isEmpty())
    {
        /* Dont wait 60 secs: */
        sltTimeout();
        m_pTimer->start();
    }
    else
    {
        m_pTimer->stop();
        resetColumData();
    }

    if (sender())
        sender()->deleteLater();

}


/*********************************************************************************************************************************
*   UIVMActivityOverviewTableView implementation.                                                                      *
*********************************************************************************************************************************/

UIVMActivityOverviewTableView::UIVMActivityOverviewTableView(QWidget *pParent)
    : QITableView(pParent)
{
}

void UIVMActivityOverviewTableView::updateColumVisibility()
{
    UIVMActivityOverviewProxyModel *pProxyModel = qobject_cast<UIVMActivityOverviewProxyModel*>(model());
    if (!pProxyModel)
        return;
    UIVMActivityOverviewModel *pModel = qobject_cast<UIVMActivityOverviewModel*>(pProxyModel->sourceModel());
    QHeaderView *pHeader = horizontalHeader();

    if (!pModel || !pHeader)
        return;
    for (int i = (int)VMActivityOverviewColumn_Name; i < (int)VMActivityOverviewColumn_Max; ++i)
    {
        if (!pModel->columnVisible(i))
            pHeader->hideSection(i);
        else
            pHeader->showSection(i);
    }
    resizeHeaders();
}

int UIVMActivityOverviewTableView::selectedItemIndex() const
{
    UIVMActivityOverviewProxyModel *pModel = qobject_cast<UIVMActivityOverviewProxyModel*>(model());
    if (!pModel)
        return -1;

    QItemSelectionModel *pSelectionModel =  selectionModel();
    if (!pSelectionModel)
        return -1;
    QModelIndexList selectedItemIndices = pSelectionModel->selectedRows();
    if (selectedItemIndices.isEmpty())
        return -1;

    /* just use the the 1st index: */
    QModelIndex modelIndex = pModel->mapToSource(selectedItemIndices[0]);

    if (!modelIndex.isValid())
        return -1;
    return modelIndex.row();
}

bool UIVMActivityOverviewTableView::hasSelection() const
{
    if (!selectionModel())
        return false;
    return selectionModel()->hasSelection();
}

void UIVMActivityOverviewTableView::setMinimumColumnWidths(const QMap<int, int>& widths)
{
    m_minimumColumnWidths = widths;
    resizeHeaders();
}

void UIVMActivityOverviewTableView::resizeEvent(QResizeEvent *pEvent)
{
    resizeHeaders();
    QTableView::resizeEvent(pEvent);
}

void UIVMActivityOverviewTableView::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    emit sigSelectionChanged(selected, deselected);
    QTableView::selectionChanged(selected, deselected);
}

void UIVMActivityOverviewTableView::mousePressEvent(QMouseEvent *pEvent)
{
    if (!indexAt(pEvent->position().toPoint()).isValid())
        clearSelection();
    QTableView::mousePressEvent(pEvent);
}

void UIVMActivityOverviewTableView::resizeHeaders()
{
    QHeaderView* pHeader = horizontalHeader();
    if (!pHeader || pHeader->count() <= 0)
        return;
    int iSectionCount = pHeader->count();
    int iHiddenSectionCount = pHeader->hiddenSectionCount();
    if (iSectionCount == iHiddenSectionCount)
        return;
    int iWidth = width() / (iSectionCount - iHiddenSectionCount);
    for (int i = 0; i < iSectionCount; ++i)
    {
        if (pHeader->isSectionHidden(i))
            continue;
        int iMinWidth = m_minimumColumnWidths.value((VMActivityOverviewColumn)i, 0);
        pHeader->resizeSection(i, iWidth < iMinWidth ? iMinWidth : iWidth);
    }
}

/*********************************************************************************************************************************
*   UIVMActivityOverviewModel implementation.                                                                            *
*********************************************************************************************************************************/

UIVMActivityOverviewModel::UIVMActivityOverviewModel(QObject *pParent, QITableView *pView)
    :QAbstractTableModel(pParent)
    , m_pTableView(pView)
    , m_pLocalVMUpdateTimer(new QTimer(this))
{
    initialize();
}

void UIVMActivityOverviewModel::setCloudMachineItems(const QList<UIVirtualMachineItemCloud*> &cloudItems)
{
    QVector<QUuid> newIds;
    foreach (const UIVirtualMachineItemCloud* pItem, cloudItems)
    {
        if (!pItem)
            continue;
        QUuid id = pItem->machineId();
        if (id.isNull())
            continue;
        newIds << id;
    }
    QVector<UIVMActivityOverviewRow*> originalItemList = m_rows;

    /* Remove m_rows items that are not in @cloudItems: */
    QMutableVectorIterator<UIVMActivityOverviewRow*> iterator(m_rows);
    while (iterator.hasNext())
    {
        UIVMActivityOverviewRow *pItem = iterator.next();
        if (!pItem->isCloudVM())
            continue;
        if (pItem && !newIds.contains(pItem->machineId()))
            iterator.remove();
    }

    /* Add items that are not in m_rows: */
    foreach (const UIVirtualMachineItemCloud* pItem, cloudItems)
    {
        if (!pItem)
            continue;
        CCloudMachine comMachine = pItem->machine();
        if (!comMachine.isOk())
            continue;
        QUuid id = comMachine.GetId();
        /* Linearly search for the vm with th same id. I cannot make QVector::contain work since we store pointers: */
        bool fFound = false;
        for (int i = 0; i < m_rows.size() && !fFound; ++i)
        {
            if (m_rows[i] && m_rows[i]->machineId() == id)
                fFound = true;
        }
        if (!fFound)
            m_rows.append(new UIVMActivityOverviewRowCloud(m_pTableView, id, comMachine.GetName(), comMachine));
    }

    /* Update cloud machine states: */
    for (int i = 0; i < m_rows.size(); ++i)
    {
        if (!m_rows[i] || !m_rows[i]->isCloudVM())
            continue;
        UIVMActivityOverviewRowCloud *pItem = qobject_cast<UIVMActivityOverviewRowCloud*>(m_rows[i]);
        if (!pItem)
            continue;
        pItem->updateMachineState();
    }
}

void UIVMActivityOverviewModel::setColumnVisible(const QMap<int, bool>& columnVisible)
{
    m_columnVisible = columnVisible;
}

bool UIVMActivityOverviewModel::columnVisible(int iColumnId) const
{
    return m_columnVisible.value(iColumnId, true);
}


bool UIVMActivityOverviewModel::isVMRunning(int rowIndex) const
{
    if (rowIndex >= m_rows.size() || rowIndex < 0 || !m_rows[rowIndex])
        return false;
    return m_rows[rowIndex]->isRunning();
}

bool UIVMActivityOverviewModel::isCloudVM(int rowIndex) const
{
    if (rowIndex >= m_rows.size() || rowIndex < 0 || !m_rows[rowIndex])
        return false;
    return m_rows[rowIndex]->isCloudVM();
}

void UIVMActivityOverviewModel::setupPerformanceCollector()
{
    m_nameList.clear();
    m_objectList.clear();
    /* Initialize and configure CPerformanceCollector: */
    const ULONG iPeriod = 1;
    const int iMetricSetupCount = 1;
    if (m_performanceCollector.isNull())
        m_performanceCollector = gpGlobalSession->virtualBox().GetPerformanceCollector();
    for (int i = 0; i < m_rows.size(); ++i)
        m_nameList << "Guest/RAM/Usage*";
    /* This is for the host: */
    m_nameList << "CPU*";
    m_nameList << "FS*";
    m_objectList = QVector<CUnknown>(m_nameList.size(), CUnknown());
    m_performanceCollector.SetupMetrics(m_nameList, m_objectList, iPeriod, iMetricSetupCount);
}

void UIVMActivityOverviewModel::clearData()
{
    /* We have a request to detach COM stuff,
     * first of all we are removing all the items,
     * this will detach COM wrappers implicitly: */
    qDeleteAll(m_rows);
    m_rows.clear();
    /* Detaching perf. collector finally,
     * please do not use it after all: */
    m_performanceCollector.detach();
}

UIVMActivityOverviewModel::~UIVMActivityOverviewModel()
{
    clearData();
}

void UIVMActivityOverviewModel::setShouldUpdate(bool fShouldUpdate)
{
    if (m_pLocalVMUpdateTimer)
    {
        if (fShouldUpdate)
            m_pLocalVMUpdateTimer->start();
        else
            m_pLocalVMUpdateTimer->stop();
    }
}

QModelIndex UIVMActivityOverviewModel::index(int iRow, int iColumn, const QModelIndex &parentIdx /* = QModelIndex() */) const
{
    /* No index for unknown items: */
    if (!hasIndex(iRow, iColumn, parentIdx))
        return QModelIndex();

    /* Provide index users with packed item pointer: */
    UIVMActivityOverviewRow *pItem = iRow >= 0 && iRow < m_rows.size() ? m_rows.at(iRow) : 0;
    return pItem ? createIndex(iRow, iColumn, pItem) : QModelIndex();
}

int UIVMActivityOverviewModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_rows.size();
}

int UIVMActivityOverviewModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    if (m_rows.isEmpty())
        return 0;
    if (m_rows[0])
        return m_rows[0]->childCount();
    return 0;
}

QVariant UIVMActivityOverviewModel::data(const QModelIndex &index, int role) const
{
    int iRow = index.row();
    if (iRow < 0 || iRow >= m_rows.size())
        return QVariant();
    if (!m_rows[iRow])
        return QVariant();

    if (role == Qt::DisplayRole)
    {
        return m_rows[iRow]->cellText(index.column());

    }
    return QVariant();
}

const QMap<int, int> UIVMActivityOverviewModel::dataLengths() const
{
    return m_columnDataMaxLength;
}

void UIVMActivityOverviewModel::initialize()
{
    for (int i = 0; i < (int)VMActivityOverviewColumn_Max; ++i)
        m_columnDataMaxLength[i] = 0;

    if (m_pLocalVMUpdateTimer)
    {
        connect(m_pLocalVMUpdateTimer, &QTimer::timeout, this, &UIVMActivityOverviewModel::sltLocalVMUpdateTimeout);
        m_pLocalVMUpdateTimer->start(1000);
    }

    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineStateChange,
            this, &UIVMActivityOverviewModel::sltMachineStateChanged);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineRegistered,
            this, &UIVMActivityOverviewModel::sltMachineRegistered);
    foreach (const CMachine &comMachine, gpGlobalSession->virtualBox().GetMachines())
    {
        if (!comMachine.isNull())
            addRow(comMachine.GetId(), comMachine.GetName(), comMachine.GetState());
    }
    setupPerformanceCollector();
}

void UIVMActivityOverviewModel::addRow(const QUuid& uMachineId, const QString& strMachineName, KMachineState enmState)
{
    //QVector<UIVMActivityOverviewRow*> m_rows;
    m_rows << new UIVMActivityOverviewRowLocal(m_pTableView, uMachineId, strMachineName, enmState);
}

QVariant UIVMActivityOverviewModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal)
        return m_columnTitles.value((VMActivityOverviewColumn)section, QString());;
    return QVariant();
}

void UIVMActivityOverviewModel::setColumnCaptions(const QMap<int, QString>& captions)
{
    m_columnTitles = captions;
}


void UIVMActivityOverviewModel::sltMachineStateChanged(const QUuid &uId, const KMachineState state)
{
    int iIndex = itemIndex(uId);
    if (iIndex != -1 && iIndex < m_rows.size())
    {
        UIVMActivityOverviewRowLocal *pItem = qobject_cast<UIVMActivityOverviewRowLocal*>(m_rows[iIndex]);
        if (pItem)
        {
            pItem->setMachineState(state);
            if (state == KMachineState_Running)
                pItem->resetDebugger();
        }
    }
}

void UIVMActivityOverviewModel::sltMachineRegistered(const QUuid &uId, bool fRegistered)
{
    if (fRegistered)
    {
        CMachine comMachine = gpGlobalSession->virtualBox().FindMachine(uId.toString());
        if (!comMachine.isNull())
            addRow(uId, comMachine.GetName(), comMachine.GetState());
    }
    else
        removeRow(uId);
    emit sigDataUpdate();
}

void UIVMActivityOverviewModel::getHostRAMStats()
{
    CHost comHost = gpGlobalSession->host();
    m_hostStats.m_iRAMTotal = _1M * (quint64)comHost.GetMemorySize();
    m_hostStats.m_iRAMFree = _1M * (quint64)comHost.GetMemoryAvailable();
}

void UIVMActivityOverviewModel::sltLocalVMUpdateTimeout()
{
    /* Host's RAM usage is obtained from IHost not from IPerformanceCollector: */
    getHostRAMStats();

    /* Use IPerformanceCollector to update VM RAM usage and Host CPU and file IO stats: */
    queryPerformanceCollector();

    for (int i = 0; i < m_rows.size(); ++i)
    {
        UIVMActivityOverviewRowLocal *pItem = qobject_cast<UIVMActivityOverviewRowLocal*>(m_rows[i]);
        if (!pItem || !pItem->isRunning())
            continue;
        pItem->updateCells();
    }

    for (int i = 0; i < (int)VMActivityOverviewColumn_Max; ++i)
    {
        for (int j = 0; j < m_rows.size(); ++j)
            if (m_columnDataMaxLength.value(i, 0) < m_rows[j]->columnLength(i))
                m_columnDataMaxLength[i] = m_rows[j]->columnLength(i);
    }

    emit sigDataUpdate();
    emit sigHostStatsUpdate(m_hostStats);
}

int UIVMActivityOverviewModel::itemIndex(const QUuid &uid)
{
    for (int i = 0; i < m_rows.size(); ++i)
    {
        if (!m_rows[i])
            continue;
        if (m_rows[i]->machineId() == uid)
            return i;
    }
    return -1;
}

QUuid UIVMActivityOverviewModel::itemUid(int iIndex)
{
    if (iIndex >= m_rows.size() || !m_rows[iIndex])
        return QUuid();
    return m_rows[iIndex]->machineId();
}

void UIVMActivityOverviewModel::removeRow(const QUuid& uMachineId)
{
    int iIndex = itemIndex(uMachineId);
    if (iIndex == -1)
        return;
    delete m_rows[iIndex];
    m_rows.remove(iIndex);
}

void UIVMActivityOverviewModel::queryPerformanceCollector()
{
    QVector<QString>  aReturnNames;
    QVector<CUnknown>  aReturnObjects;
    QVector<QString>  aReturnUnits;
    QVector<ULONG>  aReturnScales;
    QVector<ULONG>  aReturnSequenceNumbers;
    QVector<ULONG>  aReturnDataIndices;
    QVector<ULONG>  aReturnDataLengths;

    QVector<LONG> returnData = m_performanceCollector.QueryMetricsData(m_nameList,
                                                                       m_objectList,
                                                                       aReturnNames,
                                                                       aReturnObjects,
                                                                       aReturnUnits,
                                                                       aReturnScales,
                                                                       aReturnSequenceNumbers,
                                                                       aReturnDataIndices,
                                                                       aReturnDataLengths);
    /* Parse the result we get from CPerformanceCollector to get respective values: */
    for (int i = 0; i < aReturnNames.size(); ++i)
    {
        if (aReturnDataLengths[i] == 0)
            continue;
        /* Read the last of the return data disregarding the rest since we are caching the data in GUI side: */
        float fData = returnData[aReturnDataIndices[i] + aReturnDataLengths[i] - 1] / (float)aReturnScales[i];
        if (aReturnNames[i].contains("RAM", Qt::CaseInsensitive) && !aReturnNames[i].contains(":"))
        {
            if (aReturnNames[i].contains("Total", Qt::CaseInsensitive) || aReturnNames[i].contains("Free", Qt::CaseInsensitive))
            {
                {
                    CMachine comMachine = (CMachine)aReturnObjects[i];
                    if (comMachine.isNull())
                        continue;
                    int iIndex = itemIndex(comMachine.GetId());
                    if (iIndex == -1 || iIndex >= m_rows.size() || !m_rows[iIndex])
                        continue;

                    UIVMActivityOverviewRowLocal *pItem = qobject_cast<UIVMActivityOverviewRowLocal*>(m_rows[iIndex]);
                    if (!pItem)
                        continue;
                    if (aReturnNames[i].contains("Total", Qt::CaseInsensitive))
                        pItem->setTotalRAM((quint64)fData);
                    else
                        pItem->setFreeRAM((quint64)fData);
                }
            }
        }
        else if (aReturnNames[i].contains("CPU/Load/User", Qt::CaseInsensitive) && !aReturnNames[i].contains(":"))
        {
            CHost comHost = (CHost)aReturnObjects[i];
            if (!comHost.isNull())
                m_hostStats.m_iCPUUserLoad = fData;
        }
        else if (aReturnNames[i].contains("CPU/Load/Kernel", Qt::CaseInsensitive) && !aReturnNames[i].contains(":"))
        {
            CHost comHost = (CHost)aReturnObjects[i];
            if (!comHost.isNull())
                m_hostStats.m_iCPUKernelLoad = fData;
        }
        else if (aReturnNames[i].contains("CPU/MHz", Qt::CaseInsensitive) && !aReturnNames[i].contains(":"))
        {
            CHost comHost = (CHost)aReturnObjects[i];
            if (!comHost.isNull())
                m_hostStats.m_iCPUFreq = fData;
        }
       else if (aReturnNames[i].contains("FS", Qt::CaseInsensitive) &&
                aReturnNames[i].contains("Total", Qt::CaseInsensitive) &&
                !aReturnNames[i].contains(":"))
       {
            CHost comHost = (CHost)aReturnObjects[i];
            if (!comHost.isNull())
                m_hostStats.m_iFSTotal = _1M * fData;
       }
       else if (aReturnNames[i].contains("FS", Qt::CaseInsensitive) &&
                aReturnNames[i].contains("Free", Qt::CaseInsensitive) &&
                !aReturnNames[i].contains(":"))
       {
            CHost comHost = (CHost)aReturnObjects[i];
            if (!comHost.isNull())
                m_hostStats.m_iFSFree = _1M * fData;
       }
    }
}

/*********************************************************************************************************************************
*   UIVMActivityOverviewProxyModel implementation.                                                                       *
*********************************************************************************************************************************/

UIVMActivityOverviewProxyModel::UIVMActivityOverviewProxyModel(QObject *pParent /* = 0 */)
    :  QSortFilterProxyModel(pParent)
{
}

void UIVMActivityOverviewProxyModel::dataUpdate()
{
    if (sourceModel())
        emit dataChanged(index(0,0), index(sourceModel()->rowCount(), sourceModel()->columnCount()));
    invalidate();
}

void UIVMActivityOverviewProxyModel::setNotRunningVMVisibility(bool fShow)
{
    if (m_fShowNotRunningVMs == fShow)
        return;
    m_fShowNotRunningVMs = fShow;
    invalidateFilter();
}


void UIVMActivityOverviewProxyModel::setCloudVMVisibility(bool fShow)
{
    if (m_fShowCloudVMs == fShow)
        return;
    m_fShowCloudVMs = fShow;
    invalidateFilter();
}

bool UIVMActivityOverviewProxyModel::filterAcceptsRow(int iSourceRow, const QModelIndex &sourceParent) const
{
    Q_UNUSED(sourceParent);
    if (m_fShowNotRunningVMs && m_fShowCloudVMs)
        return true;
    UIVMActivityOverviewModel *pModel = qobject_cast<UIVMActivityOverviewModel*>(sourceModel());
    if (!pModel)
        return true;

    if (!m_fShowNotRunningVMs && !pModel->isVMRunning(iSourceRow))
        return false;
    if (!m_fShowCloudVMs && pModel->isCloudVM(iSourceRow))
        return false;
    return true;
}

bool UIVMActivityOverviewProxyModel::lessThan(const QModelIndex &sourceLeftIndex, const QModelIndex &sourceRightIndex) const
{
    UIVMActivityOverviewModel *pModel = qobject_cast<UIVMActivityOverviewModel*>(sourceModel());
    if (pModel)
    {
        /* Keep running vm always on top of the list: */
        bool fLeftRunning = pModel->isVMRunning(sourceLeftIndex.row());
        bool fRightRunning = pModel->isVMRunning(sourceRightIndex.row());
        if (fLeftRunning && !fRightRunning)
        {
            if (sortOrder() == Qt::AscendingOrder)
                return true;
            else
                return false;
        }
        if (!fLeftRunning && fRightRunning)
        {
            if (sortOrder() == Qt::AscendingOrder)
                return false;
            else
                return true;
        }
    }
    return QSortFilterProxyModel::lessThan(sourceLeftIndex, sourceRightIndex);
}


#include "UIVMActivityOverviewModelView.moc"
