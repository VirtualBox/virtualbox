/* $Id: UISnapshotPane.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISnapshotPane class implementation.
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
#include <QAccessibleWidget>
#include <QApplication>
#include <QDateTime>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QPointer>
#include <QReadWriteLock>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWriteLocker>

/* GUI includes: */
#include "QIMessageBox.h"
#include "QIToolBar.h"
#include "QITreeWidget.h"
#include "UIActionPoolManager.h"
#include "UICommon.h"
#include "UIConverter.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UILocalMachineStuff.h"
#include "UILoggingDefs.h"
#include "UIMediumTools.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UINotificationCenter.h"
#include "UISnapshotDetailsWidget.h"
#include "UISnapshotPane.h"
#include "UITakeSnapshotDialog.h"
#include "UITranslator.h"
#include "UITranslationEventListener.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVirtualMachineItem.h"
#include "UIVirtualMachineItemLocal.h"
#include "UIWizardCloneVM.h"

/* COM includes: */
#include "CConsole.h"
#include "CSnapshot.h"


/** Snapshot tree column tags. */
enum
{
    Column_Name,
    Column_Taken,
    Column_Max,
};


/** QITreeWidgetItem subclass for snapshots items. */
class UISnapshotItem : public QITreeWidgetItem, public UIDataSnapshot
{
    Q_OBJECT;

public:

    /** Casts QTreeWidgetItem* to UISnapshotItem* if possible. */
    static UISnapshotItem *toSnapshotItem(QTreeWidgetItem *pItem);
    /** Casts const QTreeWidgetItem* to const UISnapshotItem* if possible. */
    static const UISnapshotItem *toSnapshotItem(const QTreeWidgetItem *pItem);

    /** Constructs normal snapshot item (child of tree-widget). */
    UISnapshotItem(UISnapshotPane *pSnapshotWidget,
                   QITreeWidget *pTreeWidget,
                   const CSnapshot &comSnapshot,
                   bool fExtendedNameRequired);
    /** Constructs normal snapshot item (child of tree-widget-item). */
    UISnapshotItem(UISnapshotPane *pSnapshotWidget,
                   QITreeWidgetItem *pRootItem,
                   const CSnapshot &comSnapshot);

    /** Constructs "current state" item (child of tree-widget). */
    UISnapshotItem(UISnapshotPane *pSnapshotWidget,
                   QITreeWidget *pTreeWidget,
                   const CMachine &comMachine,
                   bool fExtendedNameRequired);
    /** Constructs "current state" item (child of tree-widget-item). */
    UISnapshotItem(UISnapshotPane *pSnapshotWidget,
                   QITreeWidgetItem *pRootItem,
                   const CMachine &comMachine);

    /** Returns item machine. */
    CMachine machine() const { return m_comMachine; }
    /** Returns item machine ID. */
    QUuid machineID() const { return m_uMachineID; }
    /** Returns item snapshot. */
    CSnapshot snapshot() const { return m_comSnapshot; }
    /** Returns item snapshot ID. */
    QUuid snapshotID() const { return m_uSnapshotID; }

    /** Returns whether this is the "current state" item. */
    bool isCurrentStateItem() const { return m_fCurrentStateItem; }

    /** Returns whether this is the "current snapshot" item. */
    bool isCurrentSnapshotItem() const { return m_fCurrentSnapshotItem; }
    /** Defines whether this is the @a fCurrent snapshot item. */
    void setCurrentSnapshotItem(bool fCurrent);

    /** Calculates and returns the current item level. */
    int level() const;

    /** Recaches the item's contents. */
    void recache();

    /** Returns current machine state. */
    KMachineState machineState() const;
    /** Defines current machine @a enmState. */
    void setMachineState(KMachineState enmState);

    /** Updates item age. */
    SnapshotAgeFormat updateAge();

protected:

    /** Recaches item tool-tip. */
    void recacheToolTip();

private:

    /** Holds whether this item requires extended name. */
    bool  m_fExtendedNameRequired;

    /** Holds the pointer to the snapshot-widget this item belongs to. */
    QPointer<UISnapshotPane>  m_pSnapshotWidget;

    /** Holds whether this is a "current state" item. */
    bool  m_fCurrentStateItem;
    /** Holds whether this is a "current snapshot" item. */
    bool  m_fCurrentSnapshotItem;

    /** Holds the snapshot COM wrapper. */
    CSnapshot  m_comSnapshot;
    /** Holds the machine COM wrapper. */
    CMachine   m_comMachine;

    /** Holds the machine ID. */
    QUuid  m_uMachineID;
    /** Holds the "current snapshot" ID. */
    QUuid  m_uSnapshotID;
    /** Holds whether the "current snapshot" is online one. */
    bool   m_fOnline;

    /** Holds the item timestamp. */
    QDateTime  m_timestamp;

    /** Holds whether the current state is modified. */
    bool           m_fCurrentStateModified;
    /** Holds the cached machine state. */
    KMachineState  m_enmMachineState;
};


/** QScrollBar subclass for snapshots widget. */
class UISnapshotScrollBar : public QScrollBar
{
    Q_OBJECT;

signals:

    /** Notify listeners about our visibility changed. */
    void sigNotifyAboutVisibilityChange();

public:

    /** Constructs scroll-bar passing @a enmOrientation and @a pParent to the base-class. */
    UISnapshotScrollBar(Qt::Orientation enmOrientation, QWidget *pParent = 0);

protected:

    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;
};


/** QITreeWidget subclass for snapshots items. */
class UISnapshotTree : public QITreeWidget
{
    Q_OBJECT;

signals:

    /** Notify listeners about one of scroll-bars visibility changed. */
    void sigNotifyAboutScrollBarVisibilityChange();

public:

    /** Constructs snapshot tree passing @a pParent to the base-class. */
    UISnapshotTree(QWidget *pParent);
};


/*********************************************************************************************************************************
*   Class UISnapshotItem implementation.                                                                                         *
*********************************************************************************************************************************/

/* static */
UISnapshotItem *UISnapshotItem::toSnapshotItem(QTreeWidgetItem *pItem)
{
    /* Get QITreeWidgetItem item first: */
    QITreeWidgetItem *pIItem = QITreeWidgetItem::toItem(pItem);
    if (!pIItem)
        return 0;

    /* Return casted UISnapshotItem then: */
    return qobject_cast<UISnapshotItem*>(pIItem);
}

/* static */
const UISnapshotItem *UISnapshotItem::toSnapshotItem(const QTreeWidgetItem *pItem)
{
    /* Get QITreeWidgetItem item first: */
    const QITreeWidgetItem *pIItem = QITreeWidgetItem::toItem(pItem);
    if (!pIItem)
        return 0;

    /* Return casted UISnapshotItem then: */
    return qobject_cast<const UISnapshotItem*>(pIItem);
}

UISnapshotItem::UISnapshotItem(UISnapshotPane *pSnapshotWidget,
                               QITreeWidget *pTreeWidget,
                               const CSnapshot &comSnapshot,
                               bool fExtendedNameRequired)
    : QITreeWidgetItem(pTreeWidget)
    , m_fExtendedNameRequired(fExtendedNameRequired)
    , m_pSnapshotWidget(pSnapshotWidget)
    , m_fCurrentStateItem(false)
    , m_fCurrentSnapshotItem(false)
    , m_comSnapshot(comSnapshot)
    , m_fOnline(false)
    , m_fCurrentStateModified(false)
    , m_enmMachineState(KMachineState_Null)
{
}

UISnapshotItem::UISnapshotItem(UISnapshotPane *pSnapshotWidget,
                               QITreeWidgetItem *pRootItem,
                               const CSnapshot &comSnapshot)
    : QITreeWidgetItem(pRootItem)
    , m_fExtendedNameRequired(false)
    , m_pSnapshotWidget(pSnapshotWidget)
    , m_fCurrentStateItem(false)
    , m_fCurrentSnapshotItem(false)
    , m_comSnapshot(comSnapshot)
    , m_fOnline(false)
    , m_fCurrentStateModified(false)
    , m_enmMachineState(KMachineState_Null)
{
}

UISnapshotItem::UISnapshotItem(UISnapshotPane *pSnapshotWidget,
                               QITreeWidget *pTreeWidget,
                               const CMachine &comMachine,
                               bool fExtendedNameRequired)
    : QITreeWidgetItem(pTreeWidget)
    , m_fExtendedNameRequired(fExtendedNameRequired)
    , m_pSnapshotWidget(pSnapshotWidget)
    , m_fCurrentStateItem(true)
    , m_fCurrentSnapshotItem(false)
    , m_comMachine(comMachine)
    , m_fOnline(false)
    , m_fCurrentStateModified(false)
    , m_enmMachineState(KMachineState_Null)
{
    /* Set the bold font state
     * for "current state" item: */
    QFont myFont = font(Column_Name);
    myFont.setBold(true);
    setFont(Column_Name, myFont);

    /* Fetch current machine state: */
    setMachineState(m_comMachine.GetState());
}

UISnapshotItem::UISnapshotItem(UISnapshotPane *pSnapshotWidget,
                               QITreeWidgetItem *pRootItem,
                               const CMachine &comMachine)
    : QITreeWidgetItem(pRootItem)
    , m_fExtendedNameRequired(false)
    , m_pSnapshotWidget(pSnapshotWidget)
    , m_fCurrentStateItem(true)
    , m_fCurrentSnapshotItem(false)
    , m_comMachine(comMachine)
    , m_fOnline(false)
    , m_fCurrentStateModified(false)
    , m_enmMachineState(KMachineState_Null)
{
    /* Set the bold font state
     * for "current state" item: */
    QFont myFont = font(Column_Name);
    myFont.setBold(true);
    setFont(Column_Name, myFont);

    /* Fetch current machine state: */
    setMachineState(m_comMachine.GetState());
}

int UISnapshotItem::level() const
{
    const QTreeWidgetItem *pItem = this;
    int iResult = 0;
    while (pItem->parent())
    {
        ++iResult;
        pItem = pItem->parent();
    }
    return iResult;
}

void UISnapshotItem::setCurrentSnapshotItem(bool fCurrent)
{
    /* Remember the state: */
    m_fCurrentSnapshotItem = fCurrent;

    /* Set/clear the bold font state
     * for "current snapshot" item: */
    QFont myFont = font(Column_Name);
    myFont.setBold(fCurrent);
    setFont(Column_Name, myFont);

    /* Update tool-tip: */
    recacheToolTip();
}

void UISnapshotItem::recache()
{
    /* For "current state" item: */
    if (m_fCurrentStateItem)
    {
        /* Fetch machine information: */
        AssertReturnVoid(m_comMachine.isNotNull());
        m_uMachineID = m_comMachine.GetId();
        m_fCurrentStateModified = m_comMachine.GetCurrentStateModified();
        m_strName = m_fCurrentStateModified
                  ? tr("Current State (changed)", "Current State (Modified)")
                  : tr("Current State", "Current State (Unmodified)");
        const QString strFinalName = m_fExtendedNameRequired
                                   ? QString("%1 (%2)").arg(m_strName, m_comMachine.GetName())
                                   : m_strName;
        setText(Column_Name, strFinalName);
        m_strDescription = m_fCurrentStateModified
                         ? tr("The current state differs from the state stored in the current snapshot")
                         : QTreeWidgetItem::parent() != 0
                         ? tr("The current state is identical to the state stored in the current snapshot")
                         : QString();
    }
    /* For snapshot item: */
    else
    {
        /* Fetch snapshot information: */
        AssertReturnVoid(m_comSnapshot.isNotNull());
        const CMachine comMachine = m_comSnapshot.GetMachine();
        m_uMachineID = comMachine.GetId();
        m_uSnapshotID = m_comSnapshot.GetId();
        m_strName = m_comSnapshot.GetName();
        const QString strFinalName = m_fExtendedNameRequired
                                   ? QString("%1 (%2)").arg(m_strName, comMachine.GetName())
                                   : m_strName;
        setText(Column_Name, strFinalName);
        m_fOnline = m_comSnapshot.GetOnline();
        setIcon(Column_Name, *m_pSnapshotWidget->snapshotItemIcon(m_fOnline));
        m_strDescription = m_comSnapshot.GetDescription();
        m_timestamp.setSecsSinceEpoch(m_comSnapshot.GetTimeStamp() / 1000);
        m_fCurrentStateModified = false;
    }

    /* Update tool-tip: */
    recacheToolTip();
}

KMachineState UISnapshotItem::machineState() const
{
    /* Make sure machine is valid: */
    if (m_comMachine.isNull())
        return KMachineState_Null;

    /* Return cached state: */
    return m_enmMachineState;
}

void UISnapshotItem::setMachineState(KMachineState enmState)
{
    /* Make sure machine is valid: */
    if (m_comMachine.isNull())
        return;

    /* Cache new state: */
    m_enmMachineState = enmState;
    /* Set corresponding icon: */
    setIcon(Column_Name, gpConverter->toIcon(m_enmMachineState));
    /* Update timestamp: */
    m_timestamp.setSecsSinceEpoch(m_comMachine.GetLastStateChange() / 1000);
}

SnapshotAgeFormat UISnapshotItem::updateAge()
{
    /* Prepare age: */
    QString strAge;

    /* Age: [date time|%1d ago|%1h ago|%1min ago|%1sec ago] */
    SnapshotAgeFormat enmAgeFormat;
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime then = m_timestamp;
    if (then > now)
        then = now; /* can happen if the host time is wrong */
    if (then.daysTo(now) > 30)
    {
        strAge = QLocale::system().toString(then, QLocale::ShortFormat);
        enmAgeFormat = SnapshotAgeFormat_Max;
    }
    else if (then.secsTo(now) > 60 * 60 * 24)
    {
        strAge = QString("%1 (%2)")
                    .arg(QLocale::system().toString(then, QLocale::ShortFormat),
                         UITranslator::daysToStringAgo(then.secsTo(now) / 60 / 60 / 24));
        enmAgeFormat = SnapshotAgeFormat_InDays;
    }
    else if (then.secsTo(now) > 60 * 60)
    {
        strAge = QString("%1 (%2)")
                    .arg(QLocale::system().toString(then, QLocale::ShortFormat),
                         UITranslator::hoursToStringAgo(then.secsTo(now) / 60 / 60));
        enmAgeFormat = SnapshotAgeFormat_InHours;
    }
    else if (then.secsTo(now) > 60)
    {
        strAge = QString("%1 (%2)")
                    .arg(QLocale::system().toString(then, QLocale::ShortFormat),
                         UITranslator::minutesToStringAgo(then.secsTo(now) / 60));
        enmAgeFormat = SnapshotAgeFormat_InMinutes;
    }
    else
    {
        strAge = QString("%1 (%2)")
                    .arg(QLocale::system().toString(then, QLocale::ShortFormat),
                         UITranslator::secondsToStringAgo(then.secsTo(now)));
        enmAgeFormat = SnapshotAgeFormat_InSeconds;
    }

    /* Update data: */
    if (!m_fCurrentStateItem)
        setText(Column_Taken, strAge);

    /* Return age: */
    return enmAgeFormat;
}

void UISnapshotItem::recacheToolTip()
{
    /* Is the saved date today? */
    const bool fDateTimeToday = m_timestamp.date() == QDate::currentDate();

    /* Compose date time: */
    QString strDateTime = fDateTimeToday
                        ? QLocale::system().toString(m_timestamp.time(), QLocale::ShortFormat)
                        : QLocale::system().toString(m_timestamp, QLocale::ShortFormat);

    /* Prepare details: */
    QString strDetails;

    /* For "current state" item: */
    if (m_fCurrentStateItem)
    {
        strDateTime = tr("%1 since %2", "Current State (time or date + time)")
                         .arg(gpConverter->toString(m_enmMachineState)).arg(strDateTime);
    }
    /* For snapshot item: */
    else
    {
        /* Gather details: */
        QStringList details;
        if (isCurrentSnapshotItem())
            details << tr("current", "snapshot");
        details << (m_fOnline ? tr("online", "snapshot")
                              : tr("offline", "snapshot"));
        strDetails = QString(" (%1)").arg(details.join(", "));

        /* Add date/time information: */
        if (fDateTimeToday)
            strDateTime = tr("Taken at %1", "Snapshot (time)").arg(strDateTime);
        else
            strDateTime = tr("Taken on %1", "Snapshot (date + time)").arg(strDateTime);
    }

    /* Prepare tool-tip: */
    QString strToolTip = QString("<nobr><b>%1</b>%2</nobr><br><nobr>%3</nobr>")
                             .arg(name()).arg(strDetails).arg(strDateTime);

    /* Append description if any: */
    if (!m_strDescription.isEmpty())
        strToolTip += "<hr>" + m_strDescription;

    /* Assign tool-tip finally: */
    setToolTip(Column_Name, strToolTip);
}


/*********************************************************************************************************************************
*   Class UISnapshotScrollBar implementation.                                                                                    *
*********************************************************************************************************************************/
UISnapshotScrollBar::UISnapshotScrollBar(Qt::Orientation enmOrientation, QWidget *pParent /* = 0 */)
    : QScrollBar(enmOrientation, pParent)
{
}

void UISnapshotScrollBar::showEvent(QShowEvent *pEvent)
{
    QScrollBar::showEvent(pEvent);
    emit sigNotifyAboutVisibilityChange();
}


/*********************************************************************************************************************************
*   Class UISnapshotTree implementation.                                                                                         *
*********************************************************************************************************************************/

UISnapshotTree::UISnapshotTree(QWidget *pParent)
    : QITreeWidget(pParent)
{
    /* Configure snapshot tree: */
    setAutoScroll(false);
    setColumnCount(Column_Max);
    setAllColumnsShowFocus(true);
    setAlternatingRowColors(true);
    setExpandsOnDoubleClick(false);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setEditTriggers(  QAbstractItemView::SelectedClicked
                    | QAbstractItemView::EditKeyPressed);

    /* Replace scroll-bars: */
    UISnapshotScrollBar *pScrollBarH = new UISnapshotScrollBar(Qt::Horizontal, this);
    if (pScrollBarH)
    {
        connect(pScrollBarH, &UISnapshotScrollBar::sigNotifyAboutVisibilityChange,
                this, &UISnapshotTree::sigNotifyAboutScrollBarVisibilityChange);
        setHorizontalScrollBar(pScrollBarH);
    }
    UISnapshotScrollBar *pScrollBarV = new UISnapshotScrollBar(Qt::Vertical, this);
    if (pScrollBarV)
    {
        connect(pScrollBarV, &UISnapshotScrollBar::sigNotifyAboutVisibilityChange,
                this, &UISnapshotTree::sigNotifyAboutScrollBarVisibilityChange);
        setVerticalScrollBar(pScrollBarV);
    }
}


/*********************************************************************************************************************************
*   Class UISnapshotPane implementation.                                                                                         *
*********************************************************************************************************************************/

UISnapshotPane::UISnapshotPane(UIActionPool *pActionPool, bool fShowToolbar /* = true */, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pActionPool(pActionPool)
    , m_fShowToolbar(fShowToolbar)
    , m_pLockReadWrite(0)
    , m_pIconSnapshotOffline(0)
    , m_pIconSnapshotOnline(0)
    , m_pTimerUpdateAge(0)
    , m_pLayoutMain(0)
    , m_pToolBar(0)
    , m_pSnapshotTree(0)
    , m_pDetailsWidget(0)
{
    prepare();
}

UISnapshotPane::~UISnapshotPane()
{
    cleanup();
}

void UISnapshotPane::setMachineItems(const QList<UIVirtualMachineItem*> &items)
{
    /* Wipe out old stuff: */
    m_machines.clear();
    m_sessionStates.clear();
    m_operationAllowed.clear();

    /* For each machine item: */
    foreach (UIVirtualMachineItem *pItem, items)
    {
        /* Parse machine details: */
        AssertPtrReturnVoid(pItem);
        CMachine comMachine = pItem->toLocal()->machine();

        /* Cache passed machine: */
        if (!comMachine.isNull())
        {
            const QUuid uMachineId = comMachine.GetId();
            const KSessionState enmSessionState = comMachine.GetSessionState();
            const bool fAllowance = gEDataManager->machineSnapshotOperationsEnabled(uMachineId);
            m_machines[uMachineId] = comMachine;
            m_sessionStates[uMachineId] = enmSessionState;
            m_operationAllowed[uMachineId] = fAllowance;
        }
    }

    /* Refresh everything: */
    refreshAll();
}

const QIcon *UISnapshotPane::snapshotItemIcon(bool fOnline) const
{
    return !fOnline ? m_pIconSnapshotOffline : m_pIconSnapshotOnline;
}

bool UISnapshotPane::isCurrentStateItemSelected() const
{
    UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());
    return m_currentStateItems.values().contains(pSnapshotItem);
}

QUuid UISnapshotPane::currentSnapshotId()
{
    UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());
    CSnapshot comSnapshot = pSnapshotItem ? pSnapshotItem->snapshot() : CSnapshot();
    return comSnapshot.isNotNull() ? comSnapshot.GetId() : QUuid();
}

void UISnapshotPane::sltRetranslateUI()
{
    /* Translate snapshot tree: */
    m_pSnapshotTree->setWhatsThis(tr("Contains the snapshot tree of the current virtual machine"));

    /* Translate snapshot tree: */
    const QStringList fields = QStringList()
                               << tr("Name", "snapshot")
                               << tr("Taken", "snapshot");
    m_pSnapshotTree->setHeaderLabels(fields);

    /* Refresh whole the tree: */
    refreshAll();
}

void UISnapshotPane::resizeEvent(QResizeEvent *pEvent)
{
    /* Call to base-class: */
    QWidget::resizeEvent(pEvent);

    /* Adjust snapshot tree: */
    adjustTreeWidget();
}

void UISnapshotPane::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    QWidget::showEvent(pEvent);

    /* Adjust snapshot tree: */
    adjustTreeWidget();
}

void UISnapshotPane::sltHandleMachineDataChange(const QUuid &uMachineId)
{
    /* Make sure it's our VM: */
    if (!m_machines.keys().contains(uMachineId))
        return;

    /* Prevent snapshot editing in the meantime: */
    QWriteLocker locker(m_pLockReadWrite);

    /* Recache "current state" item data: */
    m_currentStateItems.value(uMachineId)->recache();

    /* Choose current item again (to update details-widget): */
    sltHandleCurrentItemChange();
}

void UISnapshotPane::sltHandleMachineStateChange(const QUuid &uMachineId, const KMachineState enmState)
{
    /* Make sure it's our VM: */
    if (!m_machines.keys().contains(uMachineId))
        return;

    /* Prevent snapshot editing in the meantime: */
    QWriteLocker locker(m_pLockReadWrite);

    /* Recache "current state" item data and machine-state: */
    m_currentStateItems.value(uMachineId)->recache();
    m_currentStateItems.value(uMachineId)->setMachineState(enmState);
}

void UISnapshotPane::sltHandleSessionStateChange(const QUuid &uMachineId, const KSessionState enmState)
{
    /* Make sure it's our VM: */
    if (!m_machines.keys().contains(uMachineId))
        return;

    /* Prevent snapshot editing in the meantime: */
    QWriteLocker locker(m_pLockReadWrite);

    /* Recache current session-state: */
    m_sessionStates[uMachineId] = enmState;

    /* Update action states: */
    updateActionStates();
}

void UISnapshotPane::sltHandleSnapshotTake(const QUuid &uMachineId, const QUuid &uSnapshotId)
{
    /* Make sure it's our VM: */
    if (!m_machines.keys().contains(uMachineId))
        return;

    LogRel(("GUI: Updating snapshot tree after TAKING snapshot with MachineID={%s}, SnapshotID={%s}...\n",
            uMachineId.toString().toUtf8().constData(), uSnapshotId.toString().toUtf8().constData()));

    /* Prepare result: */
    bool fSuccess = true;
    {
        /* Prevent snapshot editing in the meantime: */
        QWriteLocker locker(m_pLockReadWrite);

        /* Acquire corresponding machine: */
        CMachine comMachine = m_machines.value(uMachineId);

        /* Search for corresponding snapshot: */
        CSnapshot comSnapshot = comMachine.FindSnapshot(uSnapshotId.toString());
        fSuccess = comMachine.isOk() && !comSnapshot.isNull();

        /* Show error message if necessary: */
        if (!fSuccess)
            UINotificationMessage::cannotFindSnapshotById(comMachine, uSnapshotId);
        else
        {
            /* Where will be the newly created item located? */
            UISnapshotItem *pParentItem = 0;

            /* Acquire snapshot parent: */
            CSnapshot comParentSnapshot = comSnapshot.GetParent();
            if (comParentSnapshot.isNotNull())
            {
                /* Acquire parent snapshot id: */
                const QUuid uParentSnapshotId = comParentSnapshot.GetId();
                fSuccess = comParentSnapshot.isOk();

                /* Show error message if necessary: */
                if (!fSuccess)
                    UINotificationMessage::cannotAcquireSnapshotParameter(comSnapshot);
                else
                {
                    /* Search for an existing parent-item with such id: */
                    pParentItem = findItem(uParentSnapshotId);
                    fSuccess = pParentItem;
                }
            }

            /* Make sure this parent-item is a parent of "current state" item as well: */
            if (fSuccess)
                fSuccess = qobject_cast<UISnapshotItem*>(m_currentStateItems.value(uMachineId)->parentItem()) == pParentItem;
            /* Make sure this parent-item is a "current snapshot" item as well: */
            if (fSuccess)
                fSuccess = m_currentSnapshotItems.value(uMachineId) == pParentItem;

            /* Create new item: */
            if (fSuccess)
            {
                /* Delete "current state" item first of all: */
                UISnapshotItem *pCurrentStateItem = m_currentStateItems.value(uMachineId);
                m_currentStateItems[uMachineId] = 0;
                delete pCurrentStateItem;
                pCurrentStateItem = 0;

                /* Create "current snapshot" item for a newly taken snapshot: */
                if (m_currentSnapshotItems.value(uMachineId))
                    m_currentSnapshotItems.value(uMachineId)->setCurrentSnapshotItem(false);
                m_currentSnapshotItems[uMachineId] = pParentItem
                                                   ? new UISnapshotItem(this, pParentItem, comSnapshot)
                                                   : new UISnapshotItem(this, m_pSnapshotTree, comSnapshot, m_machines.size() > 1);
                /* Mark it as current: */
                m_currentSnapshotItems.value(uMachineId)->setCurrentSnapshotItem(true);
                /* And recache it's content: */
                m_currentSnapshotItems.value(uMachineId)->recache();

                /* Create "current state" item as a child of "current snapshot" item: */
                m_currentStateItems[uMachineId] = new UISnapshotItem(this, m_currentSnapshotItems.value(uMachineId), comMachine);
                /* Recache it's content: */
                m_currentStateItems.value(uMachineId)->recache();
                /* And choose is as current one: */
                m_pSnapshotTree->setCurrentItem(m_currentStateItems.value(uMachineId));
                sltHandleCurrentItemChange();

                LogRel(("GUI: Snapshot tree update successful!\n"));
            }
        }
    }

    /* Just refresh everything as fallback: */
    if (!fSuccess)
    {
        LogRel(("GUI: Snapshot tree update failed! Rebuilding from scratch...\n"));
        refreshAll();
    }
}

void UISnapshotPane::sltHandleSnapshotDelete(const QUuid &uMachineId, const QUuid &uSnapshotId)
{
    /* Make sure it's our VM: */
    if (!m_machines.keys().contains(uMachineId))
        return;

    LogRel(("GUI: Updating snapshot tree after DELETING snapshot with MachineID={%s}, SnapshotID={%s}...\n",
            uMachineId.toString().toUtf8().constData(), uSnapshotId.toString().toUtf8().constData()));

    /* Prepare result: */
    bool fSuccess = false;
    {
        /* Prevent snapshot editing in the meantime: */
        QWriteLocker locker(m_pLockReadWrite);

        /* Search for an existing item with such id: */
        UISnapshotItem *pItem = findItem(uSnapshotId);
        fSuccess = pItem;

        /* Make sure item has no more than one child: */
        if (fSuccess)
            fSuccess = pItem->childCount() <= 1;

        /* Detach child before deleting item: */
        QTreeWidgetItem *pChild = 0;
        if (fSuccess && pItem->childCount() == 1)
            pChild = pItem->takeChild(0);

        /* Check whether item has parent: */
        QTreeWidgetItem *pParent = 0;
        if (fSuccess)
            pParent = pItem->QTreeWidgetItem::parent();

        /* If item has child: */
        if (pChild)
        {
            /* Determine where the item located: */
            int iIndexOfChild = -1;
            if (fSuccess)
            {
                if (pParent)
                    iIndexOfChild = pParent->indexOfChild(pItem);
                else
                    iIndexOfChild = m_pSnapshotTree->indexOfTopLevelItem(pItem);
                fSuccess = iIndexOfChild != -1;
            }

            /* And move child into this place: */
            if (fSuccess)
            {
                if (pParent)
                    pParent->insertChild(iIndexOfChild, pChild);
                else
                    m_pSnapshotTree->insertTopLevelItem(iIndexOfChild, pChild);
                expandItemChildren(pChild);
            }
        }

        /* Delete item finally: */
        if (fSuccess)
        {
            if (pItem == m_currentSnapshotItems.value(uMachineId))
            {
                m_currentSnapshotItems[uMachineId] = UISnapshotItem::toSnapshotItem(pParent);
                if (m_currentSnapshotItems.value(uMachineId))
                    m_currentSnapshotItems.value(uMachineId)->setCurrentSnapshotItem(true);
            }
            delete pItem;
            pItem = 0;

            LogRel(("GUI: Snapshot tree update successful!\n"));
        }
    }

    /* Just refresh everything as fallback: */
    if (!fSuccess)
    {
        LogRel(("GUI: Snapshot tree update failed! Rebuilding from scratch...\n"));
        refreshAll();
    }
}

void UISnapshotPane::sltHandleSnapshotChange(const QUuid &uMachineId, const QUuid &uSnapshotId)
{
    /* Make sure it's our VM: */
    if (!m_machines.keys().contains(uMachineId))
        return;

    LogRel(("GUI: Updating snapshot tree after CHANGING snapshot with MachineID={%s}, SnapshotID={%s}...\n",
            uMachineId.toString().toUtf8().constData(), uSnapshotId.toString().toUtf8().constData()));

    /* Prepare result: */
    bool fSuccess = true;
    {
        /* Prevent snapshot editing in the meantime: */
        QWriteLocker locker(m_pLockReadWrite);

        /* Search for an existing item with such id: */
        UISnapshotItem *pItem = findItem(uSnapshotId);
        fSuccess = pItem;

        /* Update the item: */
        if (fSuccess)
        {
            /* Recache it: */
            pItem->recache();
            /* And choose it again if it's current one (to update details-widget): */
            if (UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem()) == pItem)
                sltHandleCurrentItemChange();

            LogRel(("GUI: Snapshot tree update successful!\n"));
        }
    }

    /* Just refresh everything as fallback: */
    if (!fSuccess)
    {
        LogRel(("GUI: Snapshot tree update failed! Rebuilding from scratch...\n"));
        refreshAll();
    }
}

void UISnapshotPane::sltHandleSnapshotRestore(const QUuid &uMachineId, const QUuid &uSnapshotId)
{
    /* Make sure it's our VM: */
    if (!m_machines.keys().contains(uMachineId))
        return;

    LogRel(("GUI: Updating snapshot tree after RESTORING snapshot with MachineID={%s}, SnapshotID={%s}...\n",
            uMachineId.toString().toUtf8().constData(), uSnapshotId.toString().toUtf8().constData()));

    /* Prepare result: */
    bool fSuccess = true;
    {
        /* Prevent snapshot editing in the meantime: */
        QWriteLocker locker(m_pLockReadWrite);

        /* Search for an existing item with such id: */
        UISnapshotItem *pItem = findItem(uSnapshotId);
        fSuccess = pItem;

        /* Choose this item as new "current snapshot": */
        if (fSuccess)
        {
            /* Delete "current state" item first of all: */
            UISnapshotItem *pCurrentStateItem = m_currentStateItems.value(uMachineId);
            m_currentStateItems[uMachineId] = 0;
            delete pCurrentStateItem;
            pCurrentStateItem = 0;

            /* Move the "current snapshot" token from one to another: */
            AssertPtrReturnVoid(m_currentSnapshotItems.value(uMachineId));
            m_currentSnapshotItems.value(uMachineId)->setCurrentSnapshotItem(false);
            m_currentSnapshotItems[uMachineId] = pItem;
            m_currentSnapshotItems.value(uMachineId)->setCurrentSnapshotItem(true);

            /* Create "current state" item as a child of "current snapshot" item: */
            m_currentStateItems[uMachineId] = new UISnapshotItem(this, m_currentSnapshotItems.value(uMachineId), m_machines.value(uMachineId));
            m_currentStateItems.value(uMachineId)->recache();
            /* And choose is as current one: */
            m_pSnapshotTree->setCurrentItem(m_currentStateItems.value(uMachineId));
            sltHandleCurrentItemChange();

            LogRel(("GUI: Snapshot tree update successful!\n"));
        }
    }

    /* Just refresh everything as fallback: */
    if (!fSuccess)
    {
        LogRel(("GUI: Snapshot tree update failed! Rebuilding from scratch...\n"));
        refreshAll();
    }
}

void UISnapshotPane::sltUpdateSnapshotsAge()
{
    /* Stop timer if active: */
    if (m_pTimerUpdateAge->isActive())
        m_pTimerUpdateAge->stop();

    /* Search for smallest snapshot age to optimize timer timeout: */
    const SnapshotAgeFormat enmAge = traverseSnapshotAge(m_pSnapshotTree->invisibleRootItem());
    switch (enmAge)
    {
        case SnapshotAgeFormat_InSeconds: m_pTimerUpdateAge->setInterval(5 * 1000); break;
        case SnapshotAgeFormat_InMinutes: m_pTimerUpdateAge->setInterval(60 * 1000); break;
        case SnapshotAgeFormat_InHours:   m_pTimerUpdateAge->setInterval(60 * 60 * 1000); break;
        case SnapshotAgeFormat_InDays:    m_pTimerUpdateAge->setInterval(24 * 60 * 60 * 1000); break;
        default:                          m_pTimerUpdateAge->setInterval(0); break;
    }

    /* Restart timer if necessary: */
    if (m_pTimerUpdateAge->interval() > 0)
        m_pTimerUpdateAge->start();
}

void UISnapshotPane::sltToggleSnapshotDetailsVisibility(bool fVisible)
{
    /* Save the setting: */
    gEDataManager->setSnapshotManagerDetailsExpanded(fVisible);
    /* Show/hide details-widget: */
    m_pDetailsWidget->setVisible(fVisible);
    /* If details-widget is visible: */
    if (m_pDetailsWidget->isVisible())
    {
        /* Acquire selected item: */
        const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());
        AssertPtrReturnVoid(pSnapshotItem);
        /* Update details-widget: */
        if (pSnapshotItem->isCurrentStateItem())
        {
            if (m_machines.value(pSnapshotItem->machineID()).isNull())
                m_pDetailsWidget->clearData();
            else
                m_pDetailsWidget->setData(m_machines.value(pSnapshotItem->machineID()));
        }
        else
            m_pDetailsWidget->setData(*pSnapshotItem, pSnapshotItem->snapshot());
    }
    /* Cleanup invisible details-widget: */
    else
        m_pDetailsWidget->clearData();
}

void UISnapshotPane::sltApplySnapshotDetailsChanges()
{
    /* Acquire selected item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());
    AssertPtrReturnVoid(pSnapshotItem);

    /* For current state item: */
    if (pSnapshotItem->isCurrentStateItem())
    {
        /* Get item data: */
        UIDataSnapshot newData = m_pDetailsWidget->data();

        /* Take snapshot: */
        UINotificationProgressSnapshotTake *pNotification = new UINotificationProgressSnapshotTake(m_machines.value(pSnapshotItem->machineID()),
                                                                                                   newData.name(),
                                                                                                   newData.description());
        gpNotificationCenter->append(pNotification);
    }
    /* For snapshot items: */
    else
    {
        /* Make sure nothing being edited in the meantime: */
        if (!m_pLockReadWrite->tryLockForWrite())
            return;

        /* Make sure that's a snapshot item indeed: */
        CSnapshot comSnapshot = pSnapshotItem->snapshot();
        AssertReturnVoid(comSnapshot.isNotNull());

        /* Get item data: */
        UIDataSnapshot oldData = *pSnapshotItem;
        UIDataSnapshot newData = m_pDetailsWidget->data();
        AssertReturnVoid(newData != oldData);

        /* Open a session (this call will handle all errors): */
        CSession comSession;
        if (m_sessionStates.value(pSnapshotItem->machineID()) != KSessionState_Unlocked)
            comSession = openExistingSession(pSnapshotItem->machineID());
        else
            comSession = openSession(pSnapshotItem->machineID());
        if (comSession.isNotNull())
        {
            /* Get corresponding machine object: */
            CMachine comMachine = comSession.GetMachine();

            /* Perform separate independent steps: */
            do
            {
                /* Save snapshot name: */
                if (newData.name() != oldData.name())
                {
                    comSnapshot.SetName(newData.name());
                    if (!comSnapshot.isOk())
                    {
                        UINotificationMessage::cannotChangeSnapshot(comSnapshot, oldData.name(), comMachine.GetName());
                        break;
                    }
                }

                /* Save snapshot description: */
                if (newData.description() != oldData.description())
                {
                    comSnapshot.SetDescription(newData.description());
                    if (!comSnapshot.isOk())
                    {
                        UINotificationMessage::cannotChangeSnapshot(comSnapshot, oldData.name(), comMachine.GetName());
                        break;
                    }
                }
            }
            while (0);

            /* Cleanup session: */
            comSession.UnlockMachine();
        }

        /* Allows editing again: */
        m_pLockReadWrite->unlock();
    }

    /* Adjust snapshot tree: */
    adjustTreeWidget();
}

void UISnapshotPane::sltHandleCurrentItemChange()
{
    /* Acquire "current snapshot" item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());

    /* Make the current item visible: */
    sltHandleScrollBarVisibilityChange();

    /* Update action states: */
    updateActionStates();

    /* Update details-widget if it's visible: */
    if (pSnapshotItem && !m_pDetailsWidget->isHidden())
    {
        if (pSnapshotItem->isCurrentStateItem())
        {
            CMachine comMachine = m_machines.value(pSnapshotItem->machineID());
            if (comMachine.isNull())
                m_pDetailsWidget->clearData();
            else
                m_pDetailsWidget->setData(comMachine);
        }
        else
            m_pDetailsWidget->setData(*pSnapshotItem, pSnapshotItem->snapshot());
    }
    /* Cleanup invisible details-widget: */
    else
        m_pDetailsWidget->clearData();

    /* Notify listeners: */
    emit sigCurrentItemChange();
}

void UISnapshotPane::sltHandleContextMenuRequest(const QPoint &position)
{
    /* Search for corresponding item: */
    const QTreeWidgetItem *pItem = m_pSnapshotTree->itemAt(position);
    if (!pItem)
        return;

    /* Acquire corresponding snapshot item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(pItem);
    AssertReturnVoid(pSnapshotItem);

    /* Prepare menu: */
    QMenu menu;
    /* For snapshot item: */
    if (m_currentSnapshotItems.value(pSnapshotItem->machineID()) && !pSnapshotItem->isCurrentStateItem())
    {
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Delete));
        menu.addSeparator();
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Restore));
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_T_Properties));
        menu.addSeparator();
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Clone));
    }
    /* For "current state" item: */
    else
    {
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Take));
        menu.addSeparator();
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Clone));
    }

    /* Show menu: */
    menu.exec(m_pSnapshotTree->viewport()->mapToGlobal(position));
}

void UISnapshotPane::sltHandleItemChange(QTreeWidgetItem *pItem)
{
    /* Make sure nothing being edited in the meantime: */
    if (!m_pLockReadWrite->tryLockForWrite())
        return;

    /* Acquire corresponding snapshot item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(pItem);
    AssertPtr(pSnapshotItem);
    if (pSnapshotItem)
    {
        /* Make sure that's a snapshot item indeed: */
        CSnapshot comSnapshot = pSnapshotItem->snapshot();
        if (comSnapshot.isNotNull())
        {
            /* Rename corresponding snapshot if necessary: */
            if (comSnapshot.GetName() != pSnapshotItem->name())
            {
                /* We need to open a session when we manipulate the snapshot data of a machine: */
                CSession comSession = openExistingSession(comSnapshot.GetMachine().GetId());
                if (!comSession.isNull())
                {
                    /// @todo Add settings save validation.

                    /* Save snapshot name: */
                    comSnapshot.SetName(pSnapshotItem->name());

                    /* Close the session again: */
                    comSession.UnlockMachine();
                }
            }
        }
    }

    /* Allows editing again: */
    m_pLockReadWrite->unlock();

    /* Adjust snapshot tree: */
    adjustTreeWidget();
}

void UISnapshotPane::sltHandleItemDoubleClick(QTreeWidgetItem *pItem)
{
    /* Acquire corresponding snapshot item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(pItem);
    AssertReturnVoid(pSnapshotItem);

    /* If this is a snapshot item: */
    if (pSnapshotItem)
    {
        /* Handle Ctrl+DoubleClick: */
        if (QApplication::keyboardModifiers() == Qt::ControlModifier)
        {
            /* As snapshot-restore procedure: */
            if (pSnapshotItem->isCurrentStateItem())
                takeSnapshot(true /* automatically */);
            else
                restoreSnapshot(true /* suppress non-critical warnings */);
        }
        /* Handle Ctrl+Shift+DoubleClick: */
        else if (QApplication::keyboardModifiers() == (Qt::KeyboardModifiers)(Qt::ControlModifier | Qt::ShiftModifier))
        {
            /* As snapshot-delete procedure: */
            if (!pSnapshotItem->isCurrentStateItem())
                deleteSnapshot(true /* automatically */);
        }
        /* Handle other kinds of DoubleClick: */
        else
        {
            /* As show details-widget procedure: */
            m_pActionPool->action(UIActionIndexMN_M_Snapshot_T_Properties)->setChecked(true);
        }
    }
}

void UISnapshotPane::sltHandleScrollBarVisibilityChange()
{
    /* Acquire "current snapshot" item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());

    /* Make the current item visible: */
    if (pSnapshotItem)
    {
        m_pSnapshotTree->horizontalScrollBar()->setValue(0);
        m_pSnapshotTree->scrollToItem(pSnapshotItem);
        m_pSnapshotTree->horizontalScrollBar()->setValue(m_pSnapshotTree->indentation() * pSnapshotItem->level());
    }
}

void UISnapshotPane::prepare()
{
    /* Create read-write locker: */
    m_pLockReadWrite = new QReadWriteLock;

    /* Create pixmaps: */
    m_pIconSnapshotOffline = new QIcon(UIIconPool::iconSet(":/snapshot_offline_16px.png"));
    m_pIconSnapshotOnline = new QIcon(UIIconPool::iconSet(":/snapshot_online_16px.png"));

    /* Create timer: */
    m_pTimerUpdateAge = new QTimer;
    if (m_pTimerUpdateAge)
    {
        /* Configure timer: */
        m_pTimerUpdateAge->setSingleShot(true);
        connect(m_pTimerUpdateAge, &QTimer::timeout, this, &UISnapshotPane::sltUpdateSnapshotsAge);
    }

    /* Prepare connections: */
    prepareConnections();
    /* Prepare actions: */
    prepareActions();
    /* Prepare widgets: */
    prepareWidgets();

    /* Load settings: */
    loadSettings();

    /* Register help topic: */
    uiCommon().setHelpKeyword(this, "snapshots");

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UISnapshotPane::sltRetranslateUI);
}

void UISnapshotPane::prepareConnections()
{
    /* Configure Main event connections: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineDataChange,
            this, &UISnapshotPane::sltHandleMachineDataChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineStateChange,
            this, &UISnapshotPane::sltHandleMachineStateChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSessionStateChange,
            this, &UISnapshotPane::sltHandleSessionStateChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotTake,
            this, &UISnapshotPane::sltHandleSnapshotTake);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotDelete,
            this, &UISnapshotPane::sltHandleSnapshotDelete);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotChange,
            this, &UISnapshotPane::sltHandleSnapshotChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotRestore,
            this, &UISnapshotPane::sltHandleSnapshotRestore);
}

void UISnapshotPane::prepareActions()
{
    /* First of all, add actions which has smaller shortcut scope: */
    addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Take));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Delete));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Restore));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_T_Properties));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Clone));

    /* Connect actions: */
    connect(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Take), &UIAction::triggered,
            this, &UISnapshotPane::sltTakeSnapshot);
    connect(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Delete), &UIAction::triggered,
            this, &UISnapshotPane::sltDeleteSnapshot);
    connect(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Restore), &UIAction::triggered,
            this, &UISnapshotPane::sltRestoreSnapshot);
    connect(m_pActionPool->action(UIActionIndexMN_M_Snapshot_T_Properties), &UIAction::toggled,
            this, &UISnapshotPane::sltToggleSnapshotDetailsVisibility);
}

void UISnapshotPane::prepareWidgets()
{
    /* Create layout: */
    m_pLayoutMain = new QVBoxLayout(this);
    if (m_pLayoutMain)
    {
        /* Configure layout: */
        m_pLayoutMain->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
        m_pLayoutMain->setSpacing(10);
#else
        m_pLayoutMain->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) / 2);
#endif

        /* Prepare toolbar, if requested: */
        if (m_fShowToolbar)
            prepareToolbar();
        /* Prepare snapshot tree: */
        prepareTreeWidget();
        /* Prepare details-widget: */
        prepareDetailsWidget();
    }
}

void UISnapshotPane::prepareToolbar()
{
    /* Create snapshot toolbar: */
    m_pToolBar = new QIToolBar(this);
    if (m_pToolBar)
    {
        /* Configure toolbar: */
        const int iIconMetric = (int)(QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize));
        m_pToolBar->setIconSize(QSize(iIconMetric, iIconMetric));
        m_pToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

        /* Add toolbar actions: */
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Take));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Delete));
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Restore));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_T_Properties));
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Clone));

        /* Add into layout: */
        m_pLayoutMain->addWidget(m_pToolBar);
    }
}

void UISnapshotPane::prepareTreeWidget()
{
    /* Create snapshot tree: */
    m_pSnapshotTree = new UISnapshotTree(this);
    if (m_pSnapshotTree)
    {
        /* Configure tree: */
        connect(m_pSnapshotTree, &UISnapshotTree::currentItemChanged,
                this, &UISnapshotPane::sltHandleCurrentItemChange);
        connect(m_pSnapshotTree, &UISnapshotTree::customContextMenuRequested,
                this, &UISnapshotPane::sltHandleContextMenuRequest);
        connect(m_pSnapshotTree, &UISnapshotTree::itemChanged,
                this, &UISnapshotPane::sltHandleItemChange);
        connect(m_pSnapshotTree, &UISnapshotTree::itemDoubleClicked,
                this, &UISnapshotPane::sltHandleItemDoubleClick);
        connect(m_pSnapshotTree, &UISnapshotTree::sigNotifyAboutScrollBarVisibilityChange,
                this, &UISnapshotPane::sltHandleScrollBarVisibilityChange, Qt::QueuedConnection);

        /* Add into layout: */
        m_pLayoutMain->addWidget(m_pSnapshotTree, 1);
    }
}

void UISnapshotPane::prepareDetailsWidget()
{
    /* Create details-widget: */
    m_pDetailsWidget = new UISnapshotDetailsWidget(this);
    if (m_pDetailsWidget)
    {
        /* Configure details-widget: */
        m_pDetailsWidget->setVisible(false);
        connect(m_pDetailsWidget, &UISnapshotDetailsWidget::sigDataChangeAccepted,
                this, &UISnapshotPane::sltApplySnapshotDetailsChanges);

        /* Add into layout: */
        m_pLayoutMain->addWidget(m_pDetailsWidget, 1);
    }
}

void UISnapshotPane::loadSettings()
{
    /* Details action/widget: */
    m_pActionPool->action(UIActionIndexMN_M_Snapshot_T_Properties)->
        setChecked(gEDataManager->snapshotManagerDetailsExpanded());
}

void UISnapshotPane::refreshAll()
{
    /* Prevent snapshot editing in the meantime: */
    QWriteLocker locker(m_pLockReadWrite);

    /* If VM list is empty, just updated the current item: */
    if (m_machines.isEmpty())
    {
        /* Clear the tree: */
        m_pSnapshotTree->clear();
        return;
    }

    /* Remember the selected item and it's first child: */
    QUuid uSelectedItem, uFirstChildOfSelectedItem;
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());
    if (pSnapshotItem)
    {
        uSelectedItem = pSnapshotItem->snapshotID();
        if (pSnapshotItem->child(0))
            uFirstChildOfSelectedItem = UISnapshotItem::toSnapshotItem(pSnapshotItem->child(0))->snapshotID();
    }

    /* Clear the tree: */
    m_pSnapshotTree->clear();

    /* Iterates over all the machines: */
    foreach (const QUuid &uMachineId, m_machines.keys())
    {
        CMachine comMachine = m_machines.value(uMachineId);

        /* If machine has snapshots: */
        if (comMachine.GetSnapshotCount() > 0)
        {
            /* Get the first snapshot: */
            const CSnapshot comSnapshot = comMachine.FindSnapshot(QString());

            /* Populate snapshot tree: */
            populateSnapshots(uMachineId, comSnapshot, 0);
            /* And make sure it has "current snapshot" item: */
            Assert(m_currentSnapshotItems.value(uMachineId));

            /* Add the "current state" item as a child to "current snapshot" item: */
            m_currentStateItems[uMachineId] = new UISnapshotItem(this, m_currentSnapshotItems.value(uMachineId), comMachine);
            m_currentStateItems.value(uMachineId)->recache();

            /* Search for a previously selected item: */
            UISnapshotItem *pCurrentItem = findItem(uSelectedItem);
            if (pCurrentItem == 0)
                pCurrentItem = findItem(uFirstChildOfSelectedItem);
            if (pCurrentItem == 0)
                pCurrentItem = m_currentStateItems.value(uMachineId);

            /* Choose current item: */
            m_pSnapshotTree->setCurrentItem(pCurrentItem);
            sltHandleCurrentItemChange();
        }
        /* If machine has no snapshots: */
        else
        {
            /* There is no "current snapshot" item: */
            m_currentSnapshotItems[uMachineId] = 0;

            /* Add the "current state" item as a child of snapshot tree: */
            m_currentStateItems[uMachineId] = new UISnapshotItem(this, m_pSnapshotTree, comMachine, m_machines.size() > 1);
            m_currentStateItems.value(uMachineId)->recache();

            /* Choose current item: */
            m_pSnapshotTree->setCurrentItem(m_currentStateItems.value(uMachineId));
            sltHandleCurrentItemChange();
        }
    }

    /* Update age: */
    sltUpdateSnapshotsAge();

    /* Adjust snapshot tree: */
    adjustTreeWidget();
}

void UISnapshotPane::populateSnapshots(const QUuid &uMachineId, const CSnapshot &comSnapshot, QITreeWidgetItem *pItem)
{
    /* Create a child of passed item: */
    UISnapshotItem *pSnapshotItem = pItem ? new UISnapshotItem(this, pItem, comSnapshot)
                                          : new UISnapshotItem(this, m_pSnapshotTree, comSnapshot, m_machines.size() > 1);
    /* And recache it's content: */
    pSnapshotItem->recache();

    /* Mark snapshot item as "current" and remember it: */
    CSnapshot comCurrentSnapshot = m_machines.value(uMachineId).GetCurrentSnapshot();
    if (!comCurrentSnapshot.isNull() && comCurrentSnapshot.GetId() == comSnapshot.GetId())
    {
        pSnapshotItem->setCurrentSnapshotItem(true);
        m_currentSnapshotItems[uMachineId] = pSnapshotItem;
    }

    /* Walk through the children recursively: */
    foreach (const CSnapshot &comIteratedSnapshot, comSnapshot.GetChildren())
        populateSnapshots(uMachineId, comIteratedSnapshot, pSnapshotItem);

    /* Expand the newly created item: */
    pSnapshotItem->setExpanded(true);
}

void UISnapshotPane::cleanup()
{
    /* Stop timer if active: */
    if (m_pTimerUpdateAge->isActive())
        m_pTimerUpdateAge->stop();
    /* Destroy timer: */
    delete m_pTimerUpdateAge;
    m_pTimerUpdateAge = 0;

    /* Destroy icons: */
    delete m_pIconSnapshotOffline;
    delete m_pIconSnapshotOnline;
    m_pIconSnapshotOffline = 0;
    m_pIconSnapshotOnline = 0;

    /* Destroy read-write locker: */
    delete m_pLockReadWrite;
    m_pLockReadWrite = 0;
}

void UISnapshotPane::updateActionStates()
{
    /* Acquire "current snapshot" item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());

    /* Check whether another direct session is opened: */
    const bool fBusy = !pSnapshotItem || m_sessionStates.value(pSnapshotItem->machineID()) != KSessionState_Unlocked;

    /* Acquire machine-state of the "current state" item: */
    KMachineState enmState = KMachineState_Null;
    if (   pSnapshotItem
        && m_currentStateItems.value(pSnapshotItem->machineID()))
        enmState = m_currentStateItems.value(pSnapshotItem->machineID())->machineState();

    /* Determine whether taking or deleting snapshots is possible: */
    const bool fCanTakeDeleteSnapshot =    !fBusy
                                        || enmState == KMachineState_PoweredOff
                                        || enmState == KMachineState_Saved
                                        || enmState == KMachineState_Aborted
                                        || enmState == KMachineState_AbortedSaved
                                        || enmState == KMachineState_Running
                                        || enmState == KMachineState_Paused;

    /* Update 'Take' action: */
    m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Take)->setEnabled(
           pSnapshotItem
        && m_operationAllowed.value(pSnapshotItem->machineID())
        && (   (   fCanTakeDeleteSnapshot
                && m_currentSnapshotItems.value(pSnapshotItem->machineID())
                && pSnapshotItem->isCurrentStateItem())
            || (!m_currentSnapshotItems.value(pSnapshotItem->machineID())))
    );

    /* Update 'Delete' action: */
    m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Delete)->setEnabled(
           pSnapshotItem
        && m_operationAllowed.value(pSnapshotItem->machineID())
        && fCanTakeDeleteSnapshot
        && m_currentSnapshotItems.value(pSnapshotItem->machineID())
        && pSnapshotItem
        && !pSnapshotItem->isCurrentStateItem()
    );

    /* Update 'Restore' action: */
    m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Restore)->setEnabled(
           !fBusy
        && pSnapshotItem
        && m_currentSnapshotItems.value(pSnapshotItem->machineID())
        && !pSnapshotItem->isCurrentStateItem()
    );

    /* Update 'Show Details' action: */
    m_pActionPool->action(UIActionIndexMN_M_Snapshot_T_Properties)->setEnabled(
        pSnapshotItem
    );

    /* Update 'Clone' action: */
    m_pActionPool->action(UIActionIndexMN_M_Snapshot_S_Clone)->setEnabled(
           pSnapshotItem
        && (   !pSnapshotItem->isCurrentStateItem()
            || !fBusy)
    );
}

bool UISnapshotPane::takeSnapshot(bool fAutomatically /* = false */)
{
    /* Acquire "current snapshot" item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());
    AssertPtrReturn(pSnapshotItem, false);

    /* Acquire machine: */
    const CMachine comMachine = m_machines.value(pSnapshotItem->machineID());

    /* Search for a maximum existing snapshot index: */
    int iMaximumIndex = 0;
    const QString strNameTemplate = tr("Snapshot %1");
    const QRegularExpression re(QString("^") + strNameTemplate.arg("([0-9]+)") + QString("$"));
    QTreeWidgetItemIterator iterator(m_pSnapshotTree);
    while (*iterator)
    {
        const QString strName = static_cast<UISnapshotItem*>(*iterator)->name();
        const QRegularExpressionMatch mt = re.match(strName);
        if (mt.hasMatch())
        {
            const int iFoundIndex = mt.captured(1).toInt();
            iMaximumIndex = iFoundIndex > iMaximumIndex
                          ? iFoundIndex : iMaximumIndex;
        }
        ++iterator;
    }

    /* Prepare snapshot name/description: */
    QString strFinalName = strNameTemplate.arg(iMaximumIndex + 1);
    QString strFinalDescription;

    /* In manual mode we should show take snapshot dialog: */
    if (!fAutomatically)
    {
        /* First of all, we should calculate amount of immutable images: */
        ulong cAmountOfImmutableMediums = 0;
        UIMediumTools::acquireAmountOfImmutableImages(comMachine, cAmountOfImmutableMediums);

        /* Create take-snapshot dialog: */
        QWidget *pDlgParent = windowManager().realParentWindow(this);
        QPointer<UITakeSnapshotDialog> pDlg = new UITakeSnapshotDialog(pDlgParent, cAmountOfImmutableMediums);
        windowManager().registerNewParent(pDlg, pDlgParent);

        /* Assign corresponding icon: */
        QIcon icon = generalIconPool().userMachineIcon(comMachine);
        if (icon.isNull())
            icon = generalIconPool().guestOSTypeIcon(comMachine.GetOSTypeId());
        pDlg->setIcon(icon);

        /* Assign corresponding snapshot name: */
        pDlg->setName(strFinalName);

        /* Show Take Snapshot dialog: */
        if (pDlg->exec() != QDialog::Accepted)
        {
            /* Cleanup dialog if it wasn't destroyed in own loop: */
            delete pDlg;
            return false;
        }

        /* Acquire final snapshot name/description: */
        strFinalName = pDlg->name().trimmed();
        strFinalDescription = pDlg->description();

        /* Cleanup dialog: */
        delete pDlg;
    }

    /* Take snapshot: */
    UINotificationProgressSnapshotTake *pNotification = new UINotificationProgressSnapshotTake(comMachine,
                                                                                               strFinalName,
                                                                                               strFinalDescription);
    gpNotificationCenter->append(pNotification);

    /* Return result: */
    return true;
}

bool UISnapshotPane::deleteSnapshot(bool fAutomatically /* = false */)
{
    /* Acquire "current snapshot" item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());
    AssertPtrReturn(pSnapshotItem, false);

    /* Acquire machine: */
    const CMachine comMachine = m_machines.value(pSnapshotItem->machineID());

    /* Get corresponding snapshot: */
    const CSnapshot comSnapshot = pSnapshotItem->snapshot();
    AssertReturn(!comSnapshot.isNull(), false);

    /* In manual mode we should ask if user really wants to remove the selected snapshot: */
    if (!fAutomatically && !msgCenter().confirmSnapshotRemoval(comSnapshot.GetName()))
        return false;

#if 0
    /** @todo check available space on the target filesystem etc etc. */
    if (!msgCenter().warnAboutSnapshotRemovalFreeSpace(comSnapshot.GetName(),
                                                       "/home/juser/.VirtualBox/Machines/SampleVM/Snapshots/{01020304-0102-0102-0102-010203040506}.vdi",
                                                       "59 GiB",
                                                       "15 GiB"))
        return false;
#endif

    /* Delete snapshot: */
    UINotificationProgressSnapshotDelete *pNotification = new UINotificationProgressSnapshotDelete(comMachine,
                                                                                                   pSnapshotItem->snapshotID());
    gpNotificationCenter->append(pNotification);

    /* Return result: */
    return true;
}

bool UISnapshotPane::restoreSnapshot(bool fAutomatically /* = false */)
{
    /* Acquire "current snapshot" item: */
    const UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(m_pSnapshotTree->currentItem());
    AssertPtrReturn(pSnapshotItem, false);

    /* Acquire machine: */
    const CMachine comMachine = m_machines.value(pSnapshotItem->machineID());

    /* Get corresponding snapshot: */
    const CSnapshot comSnapshot = pSnapshotItem->snapshot();
    AssertReturn(!comSnapshot.isNull(), false);

    /* In manual mode we should check whether current state is changed: */
    if (!fAutomatically && comMachine.GetCurrentStateModified())
    {
        /* Ask if user really wants to restore the selected snapshot: */
        int iResultCode = msgCenter().confirmSnapshotRestoring(comSnapshot.GetName(), comMachine.GetCurrentStateModified());
        if (iResultCode & AlertButton_Cancel)
            return false;

        /* Ask if user also wants to create new snapshot of current state which is changed: */
        if (iResultCode & AlertOption_CheckBox)
        {
            /* Take snapshot of changed current state: */
            m_pSnapshotTree->setCurrentItem(m_currentStateItems.value(pSnapshotItem->machineID()));
            if (!takeSnapshot())
                return false;
        }
    }

    /* Restore snapshot: */
    UINotificationProgressSnapshotRestore *pNotification = new UINotificationProgressSnapshotRestore(comMachine, comSnapshot);
    gpNotificationCenter->append(pNotification);

    /* Return result: */
    return true;
}

void UISnapshotPane::adjustTreeWidget()
{
    /* Get the snapshot tree abstract interface: */
    QAbstractItemView *pItemView = m_pSnapshotTree;
    /* Get the snapshot tree header-view: */
    QHeaderView *pItemHeader = m_pSnapshotTree->header();

    /* Calculate the total snapshot tree width: */
    const int iTotal = m_pSnapshotTree->viewport()->width();

    /* Look for a minimum width hint for Taken column: */
    const int iMinWidth1 = qMax(pItemView->sizeHintForColumn(Column_Taken), pItemHeader->sectionSizeHint(Column_Taken));
    /* Propose suitable width hint for Taken column (but no more than the half of existing space): */
    const int iWidth1 = iMinWidth1 < iTotal / Column_Max ? iMinWidth1 : iTotal / Column_Max;

    /* Look for a minimum width hint for Name column: */
    const int iMinWidth0 = qMax(pItemView->sizeHintForColumn(Column_Name), pItemHeader->sectionSizeHint(Column_Name));
    /* Propose suitable width hint for important column (at least all remaining space and no less than the hint itself): */
    const int iWidth0 = iMinWidth0 > iTotal - iWidth1 ? iMinWidth0 : iTotal - iWidth1;

    /* Apply the proposal: */
    m_pSnapshotTree->setColumnWidth(Column_Taken, iWidth1);
    m_pSnapshotTree->setColumnWidth(Column_Name, iWidth0);
}

UISnapshotItem *UISnapshotPane::findItem(const QUuid &uSnapshotID) const
{
    /* Search for the first item with required ID: */
    QTreeWidgetItemIterator it(m_pSnapshotTree);
    while (*it)
    {
        UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(*it);
        if (pSnapshotItem->snapshotID() == uSnapshotID)
            return pSnapshotItem;
        ++it;
    }

    /* Null by default: */
    return 0;
}

SnapshotAgeFormat UISnapshotPane::traverseSnapshotAge(QTreeWidgetItem *pItem) const
{
    /* Acquire corresponding snapshot item: */
    UISnapshotItem *pSnapshotItem = UISnapshotItem::toSnapshotItem(pItem);

    /* Fetch the snapshot age of the root if it's valid: */
    SnapshotAgeFormat age = pSnapshotItem ? pSnapshotItem->updateAge() : SnapshotAgeFormat_Max;

    /* Walk through the children recursively: */
    for (int i = 0; i < pItem->childCount(); ++i)
    {
        /* Fetch the smallest snapshot age of the children: */
        const SnapshotAgeFormat newAge = traverseSnapshotAge(pItem->child(i));
        /* Remember the smallest snapshot age among existing: */
        age = newAge < age ? newAge : age;
    }

    /* Return result: */
    return age;
}

void UISnapshotPane::expandItemChildren(QTreeWidgetItem *pItem)
{
    pItem ->setExpanded(true);
    for (int i = 0; i < pItem->childCount(); ++i)
        expandItemChildren(pItem->child(i));
}

#include "UISnapshotPane.moc"
