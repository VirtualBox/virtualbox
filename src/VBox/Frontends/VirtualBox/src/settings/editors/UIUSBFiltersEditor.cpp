/* $Id: UIUSBFiltersEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUSBFiltersEditor class implementation.
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
#include <QHeaderView>
#include <QHelpEvent>
#include <QMenu>
#include <QRegularExpression>
#include <QToolTip>
#include <QVBoxLayout>

/* GUI includes: */
#include "QILabelSeparator.h"
#include "QIToolBar.h"
#include "QITreeWidget.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UIUSBFilterDetailsEditor.h"
#include "UIUSBFiltersEditor.h"
#include "UIUSBTools.h"

/* COM includes: */
#include "CConsole.h"
#include "CHostUSBDevice.h"
#include "CUSBDevice.h"

/* Other VBox includes: */
#include "iprt/assert.h"

/* VirtualBox interface declarations: */
#include <VBox/com/VirtualBox.h>


/** USB Filter tree-widget item. */
class USBFilterTreeWidgetItem : public QITreeWidgetItem, public UIDataUSBFilter
{
    Q_OBJECT;

public:

    /** Constructs USB filter type (root) item. */
    USBFilterTreeWidgetItem(QITreeWidget *pParent) : QITreeWidgetItem(pParent) {}

    /** Updates item fields. */
    void updateFields();

protected:

    /** Returns default text. */
    virtual QString defaultText() const RT_OVERRIDE;
};


/** USB Filter popup menu. */
class UIUSBMenu : public QMenu
{
    Q_OBJECT;

public:

    /** Constructs USB Filter menu passing @a pParent to the base-class. */
    UIUSBMenu(QWidget *pParent);

    /** Returns USB device related to passed action. */
    const CUSBDevice& getUSB(QAction *pAction);

    /** Defines @a comConsole. */
    void setConsole(const CConsole &comConsole);

protected:

    /** Handles any @a pEvent handler. */
    virtual bool event(QEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Prepares menu contents. */
    void processAboutToShow();

private:

    /** Holds the USB device map. */
    QMap<QAction*, CUSBDevice>  m_usbDeviceMap;

    /** Holds the console. */
    CConsole  m_comConsole;
};


/*********************************************************************************************************************************
*   Class USBFilterTreeWidgetItem implementation.                                                                                *
*********************************************************************************************************************************/

void USBFilterTreeWidgetItem::updateFields()
{
    setText(0, m_strName);
}

QString USBFilterTreeWidgetItem::defaultText() const
{
    return   checkState(0) == Qt::Checked
           ? UIUSBFiltersEditor::tr("%1, Active", "col.1 text, col.1 state").arg(text(0))
           : text(0);
}


/*********************************************************************************************************************************
*   Class UIUSBMenu implementation.                                                                                              *
*********************************************************************************************************************************/

UIUSBMenu::UIUSBMenu(QWidget *pParent)
    : QMenu(pParent)
{
    connect(this, &UIUSBMenu::aboutToShow,
            this, &UIUSBMenu::processAboutToShow);
}

const CUSBDevice &UIUSBMenu::getUSB(QAction *pAction)
{
    return m_usbDeviceMap[pAction];
}

void UIUSBMenu::setConsole(const CConsole &comConsole)
{
    m_comConsole = comConsole;
}

bool UIUSBMenu::event(QEvent *pEvent)
{
    /* We provide dynamic tooltips for the usb devices: */
    if (pEvent->type() == QEvent::ToolTip)
    {
        QHelpEvent *pHelpEvent = static_cast<QHelpEvent*>(pEvent);
        QAction *pAction = actionAt(pHelpEvent->pos());
        if (pAction)
        {
            CUSBDevice usb = m_usbDeviceMap[pAction];
            if (!usb.isNull())
            {
                QToolTip::showText(pHelpEvent->globalPos(), usbToolTip(usb));
                return true;
            }
        }
    }
    /* Call to base-class: */
    return QMenu::event(pEvent);
}

void UIUSBMenu::processAboutToShow()
{
    /* Clear lists initially: */
    clear();
    m_usbDeviceMap.clear();

    /* Get host for further activities: */
    CHost comHost = gpGlobalSession->host();

    /* Check whether we have host USB devices at all: */
    bool fIsUSBEmpty = comHost.GetUSBDevices().size() == 0;
    if (fIsUSBEmpty)
    {
        /* Empty action for no USB device case: */
        QAction *pAction = addAction(tr("<no devices available>", "USB devices"));
        pAction->setEnabled(false);
        pAction->setToolTip(tr("No supported devices connected to the host PC", "USB device tooltip"));
    }
    else
    {
        /* Action per each host USB device: */
        foreach (const CHostUSBDevice &comHostUsb, comHost.GetUSBDevices())
        {
            CUSBDevice comUsb(comHostUsb);
            QAction *pAction = addAction(usbDetails(comUsb));
            pAction->setCheckable(true);
            m_usbDeviceMap[pAction] = comUsb;
            /* Check if created item was already attached to this session: */
            if (!m_comConsole.isNull())
            {
                CUSBDevice attachedUSB = m_comConsole.FindUSBDeviceById(comUsb.GetId());
                pAction->setChecked(!attachedUSB.isNull());
                pAction->setEnabled(comHostUsb.GetState() != KUSBDeviceState_Unavailable);
            }
        }
    }
}


/*********************************************************************************************************************************
*   Class UIUSBFiltersEditor implementation.                                                                                     *
*********************************************************************************************************************************/

UIUSBFiltersEditor::UIUSBFiltersEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_pLabelSeparator(0)
    , m_pLayoutTree(0)
    , m_pTreeWidget(0)
    , m_pToolbar(0)
    , m_pActionNew(0)
    , m_pActionAdd(0)
    , m_pActionEdit(0)
    , m_pActionRemove(0)
    , m_pActionMoveUp(0)
    , m_pActionMoveDown(0)
    , m_pMenuUSBDevices(0)
{
    prepare();
}

void UIUSBFiltersEditor::setValue(const QList<UIDataUSBFilter> &guiValue)
{
    /* Update cached value and
     * tree-widget if value has changed: */
    if (m_guiValue != guiValue)
    {
        m_guiValue = guiValue;
        reloadTree();
    }
}

QList<UIDataUSBFilter> UIUSBFiltersEditor::value() const
{
    /* Sanity check: */
    if (!m_pTreeWidget)
        return m_guiValue;

    /* Prepare result: */
    QList<UIDataUSBFilter> result;

    /* For each filter: */
    QTreeWidgetItem *pMainRootItem = m_pTreeWidget->invisibleRootItem();
    for (int iFilterIndex = 0; iFilterIndex < pMainRootItem->childCount(); ++iFilterIndex)
    {
        /* Gather and cache new data: */
        const USBFilterTreeWidgetItem *pItem = static_cast<USBFilterTreeWidgetItem*>(pMainRootItem->child(iFilterIndex));
        result << *pItem;
    }

    /* Return result: */
    return result;
}

void UIUSBFiltersEditor::sltRetranslateUI()
{
    /* Tags: */
    m_strTrUSBFilterName = tr("New Filter %1", "usb");

    /* Translate separator label: */
    if (m_pLabelSeparator)
        m_pLabelSeparator->setText(tr("USB Device &Filters"));

    /* Translate tree-widget: */
    if (m_pTreeWidget)
        m_pTreeWidget->setWhatsThis(tr("Lists all USB filters of this machine. The checkbox to the left defines whether the "
                                       "particular filter is enabled or not. Use the context menu or buttons to the right to "
                                       "add or remove USB filters."));

    /* Translate actions: */
    if (m_pActionNew)
    {
        m_pActionNew->setText(tr("Add Empty Filter"));
        m_pActionNew->setToolTip(tr("Adds new USB filter with all fields initially set to empty strings. "
                                    "Note that such a filter will match any attached USB device."));
    }
    if (m_pActionAdd)
    {
        m_pActionAdd->setText(tr("Add Filter From Device"));
        m_pActionAdd->setToolTip(tr("Adds new USB filter with all fields set to the values of the "
                                "selected USB device attached to the host PC."));
    }
    if (m_pActionEdit)
    {
        m_pActionEdit->setText(tr("Edit Filter"));
        m_pActionEdit->setToolTip(tr("Edits selected USB filter."));
    }
    if (m_pActionRemove)
    {
        m_pActionRemove->setText(tr("Remove Filter"));
        m_pActionRemove->setToolTip(tr("Removes selected USB filter."));
    }
    if (m_pActionMoveUp)
    {
        m_pActionMoveUp->setText(tr("Move Filter Up"));
        m_pActionMoveUp->setToolTip(tr("Moves selected USB filter up."));
    }
    if (m_pActionMoveDown)
    {
        m_pActionMoveDown->setText(tr("Move Filter Down"));
        m_pActionMoveDown->setToolTip(tr("Moves selected USB filter down."));
    }
}

void UIUSBFiltersEditor::sltHandleCurrentItemChange(QTreeWidgetItem *pCurrentItem)
{
    if (pCurrentItem && !pCurrentItem->isSelected())
        pCurrentItem->setSelected(true);
    m_pActionEdit->setEnabled(pCurrentItem);
    m_pActionRemove->setEnabled(pCurrentItem);
    m_pActionMoveUp->setEnabled(pCurrentItem && m_pTreeWidget->itemAbove(pCurrentItem));
    m_pActionMoveDown->setEnabled(pCurrentItem && m_pTreeWidget->itemBelow(pCurrentItem));
}

void UIUSBFiltersEditor::sltHandleDoubleClick(QTreeWidgetItem *pItem)
{
    AssertPtrReturnVoid(pItem);
    sltEditFilter();
}

void UIUSBFiltersEditor::sltHandleContextMenuRequest(const QPoint &position)
{
    QMenu menu;
    QTreeWidgetItem *pItem = m_pTreeWidget->itemAt(position);
    if (m_pTreeWidget->isEnabled() && pItem && pItem->flags() & Qt::ItemIsSelectable)
    {
        menu.addAction(m_pActionEdit);
        menu.addAction(m_pActionRemove);
        menu.addSeparator();
        menu.addAction(m_pActionMoveUp);
        menu.addAction(m_pActionMoveDown);
    }
    else
    {
        menu.addAction(m_pActionNew);
        menu.addAction(m_pActionAdd);
    }
    if (!menu.isEmpty())
        menu.exec(m_pTreeWidget->viewport()->mapToGlobal(position));
}

void UIUSBFiltersEditor::sltCreateFilter()
{
    /* Search for the max available filter index: */
    int iMaxFilterIndex = 0;
    const QRegularExpression re(QString("^") + m_strTrUSBFilterName.arg("([0-9]+)") + QString("$"));
    QTreeWidgetItemIterator iterator(m_pTreeWidget);
    while (*iterator)
    {
        const QString filterName = (*iterator)->text(0);
        const QRegularExpressionMatch mt = re.match(filterName);
        if (mt.hasMatch())
        {
            const int iFoundIndex = mt.captured(1).toInt();
            iMaxFilterIndex = iFoundIndex > iMaxFilterIndex
                            ? iFoundIndex : iMaxFilterIndex;
        }
        ++iterator;
    }

    /* Prepare new data: */
    UIDataUSBFilter newFilterData;
    newFilterData.m_fActive = true;
    newFilterData.m_strName = m_strTrUSBFilterName.arg(iMaxFilterIndex + 1);

    /* Add new filter item: */
    addUSBFilterItem(newFilterData, true /* its new? */);

    /* Notify listeners: */
    emit sigValueChanged();
}

void UIUSBFiltersEditor::sltAddFilter()
{
    if (m_pMenuUSBDevices)
        m_pMenuUSBDevices->exec(QCursor::pos());
}

void UIUSBFiltersEditor::sltAddFilterConfirmed(QAction *pAction)
{
    if (!m_pMenuUSBDevices)
        return;
    /* Get USB device: */
    const CUSBDevice comUsb = m_pMenuUSBDevices->getUSB(pAction);
    if (comUsb.isNull())
        return;

    /* Prepare new USB filter data: */
    UIDataUSBFilter newFilterData;
    newFilterData.m_fActive = true;
    newFilterData.m_strName = usbDetails(comUsb);
    newFilterData.m_strVendorId  = QString::number(comUsb.GetVendorId(),  16).toUpper().rightJustified(4, '0');
    newFilterData.m_strProductId = QString::number(comUsb.GetProductId(), 16).toUpper().rightJustified(4, '0');
    newFilterData.m_strRevision  = QString::number(comUsb.GetRevision(),  16).toUpper().rightJustified(4, '0');
    /* The port property depends on the host computer rather than on the USB
     * device itself; for this reason only a few people will want to use it
     * in the filter since the same device plugged into a different socket
     * will not match the filter in this case. */
    newFilterData.m_strPort = QString::asprintf("%#06hX", comUsb.GetPort());
    newFilterData.m_strManufacturer = comUsb.GetManufacturer();
    newFilterData.m_strProduct = comUsb.GetProduct();
    newFilterData.m_strSerialNumber = comUsb.GetSerialNumber();
    newFilterData.m_enmRemoteMode = comUsb.GetRemote() ? UIRemoteMode_On : UIRemoteMode_Off;

    /* Add new USB filter item: */
    addUSBFilterItem(newFilterData, true /* its new? */);

    /* Notify listeners: */
    emit sigValueChanged();
}

void UIUSBFiltersEditor::sltEditFilter()
{
    /* Check current filter item: */
    USBFilterTreeWidgetItem *pItem = static_cast<USBFilterTreeWidgetItem*>(m_pTreeWidget->currentItem());
    AssertPtrReturnVoid(pItem);

    /* Configure USB filter details editor: */
    UIUSBFilterDetailsEditor dlgFolderDetails(this); /// @todo convey usedList!
    dlgFolderDetails.setName(pItem->m_strName);
    dlgFolderDetails.setVendorID(pItem->m_strVendorId);
    dlgFolderDetails.setProductID(pItem->m_strProductId);
    dlgFolderDetails.setRevision(pItem->m_strRevision);
    dlgFolderDetails.setManufacturer(pItem->m_strManufacturer);
    dlgFolderDetails.setProduct(pItem->m_strProduct);
    dlgFolderDetails.setSerialNo(pItem->m_strSerialNumber);
    dlgFolderDetails.setPort(pItem->m_strPort);
    dlgFolderDetails.setRemoteMode(pItem->m_enmRemoteMode);

    /* Run filter details dialog: */
    if (dlgFolderDetails.exec() == QDialog::Accepted)
    {
        /* Prepare new data: */
        pItem->m_strName = dlgFolderDetails.name();
        pItem->m_strVendorId = dlgFolderDetails.vendorID();
        pItem->m_strProductId = dlgFolderDetails.productID();
        pItem->m_strRevision = dlgFolderDetails.revision();
        pItem->m_strManufacturer = dlgFolderDetails.manufacturer();
        pItem->m_strProduct = dlgFolderDetails.product();
        pItem->m_strSerialNumber = dlgFolderDetails.serialNo();
        pItem->m_strPort = dlgFolderDetails.port();
        pItem->m_enmRemoteMode = dlgFolderDetails.remoteMode();
        pItem->updateFields();

        /* Notify listeners: */
        emit sigValueChanged();
    }
}

void UIUSBFiltersEditor::sltRemoveFilter()
{
    /* Check current USB filter item: */
    QTreeWidgetItem *pItem = m_pTreeWidget->currentItem();
    AssertPtrReturnVoid(pItem);

    /* Delete corresponding item: */
    delete pItem;

    /* Notify listeners: */
    emit sigValueChanged();
}

void UIUSBFiltersEditor::sltMoveFilterUp()
{
    /* Check current USB filter item: */
    QTreeWidgetItem *pItem = m_pTreeWidget->currentItem();
    AssertPtrReturnVoid(pItem);

    /* Move the item up: */
    const int iIndex = m_pTreeWidget->indexOfTopLevelItem(pItem);
    QTreeWidgetItem *pTakenItem = m_pTreeWidget->takeTopLevelItem(iIndex);
    Assert(pItem == pTakenItem);
    m_pTreeWidget->insertTopLevelItem(iIndex - 1, pTakenItem);

    /* Make sure moved item still chosen: */
    m_pTreeWidget->setCurrentItem(pTakenItem);
}

void UIUSBFiltersEditor::sltMoveFilterDown()
{
    /* Check current USB filter item: */
    QTreeWidgetItem *pItem = m_pTreeWidget->currentItem();
    AssertPtrReturnVoid(pItem);

    /* Move the item down: */
    const int iIndex = m_pTreeWidget->indexOfTopLevelItem(pItem);
    QTreeWidgetItem *pTakenItem = m_pTreeWidget->takeTopLevelItem(iIndex);
    Assert(pItem == pTakenItem);
    m_pTreeWidget->insertTopLevelItem(iIndex + 1, pTakenItem);

    /* Make sure moved item still chosen: */
    m_pTreeWidget->setCurrentItem(pTakenItem);
}

void UIUSBFiltersEditor::sltHandleActivityStateChange(QTreeWidgetItem *pChangedItem)
{
    /* Check changed USB filter item: */
    USBFilterTreeWidgetItem *pItem = static_cast<USBFilterTreeWidgetItem*>(pChangedItem);
    AssertPtrReturnVoid(pItem);

    /* Update corresponding item: */
    pItem->m_fActive = pItem->checkState(0) == Qt::Checked;
}

void UIUSBFiltersEditor::prepare()
{
    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIUSBFiltersEditor::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        pLayout->setContentsMargins(0, 0, 0, 0);

        /* Prepare separator: */
        m_pLabelSeparator = new QILabelSeparator(this);
        if (m_pLabelSeparator)
            pLayout->addWidget(m_pLabelSeparator);

        /* Prepare view layout: */
        m_pLayoutTree = new QHBoxLayout;
        if (m_pLayoutTree)
        {
            m_pLayoutTree->setContentsMargins(0, 0, 0, 0);
            m_pLayoutTree->setSpacing(3);

            /* Prepare tree-widget: */
            prepareTreeWidget();
            /* Prepare toolbar: */
            prepareToolbar();

            /* Update action availability: */
            sltHandleCurrentItemChange(m_pTreeWidget->currentItem());

            pLayout->addLayout(m_pLayoutTree);
        }
    }
}

void UIUSBFiltersEditor::prepareTreeWidget()
{
    /* Prepare shared folders tree-widget: */
    m_pTreeWidget = new QITreeWidget(this);
    if (m_pTreeWidget)
    {
        if (m_pLabelSeparator)
            m_pLabelSeparator->setBuddy(m_pTreeWidget);
        m_pTreeWidget->header()->hide();
        m_pTreeWidget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Ignored);
        m_pTreeWidget->setMinimumHeight(150);
        m_pTreeWidget->setRootIsDecorated(false);
        m_pTreeWidget->setUniformRowHeights(true);
        m_pTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);

        m_pLayoutTree->addWidget(m_pTreeWidget);
    }
}

void UIUSBFiltersEditor::prepareToolbar()
{
    /* Prepare shared folders toolbar: */
    m_pToolbar = new QIToolBar(this);
    if (m_pToolbar)
    {
        const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
        m_pToolbar->setIconSize(QSize(iIconMetric, iIconMetric));
        m_pToolbar->setOrientation(Qt::Vertical);

        /* Prepare 'New USB Filter' action: */
        m_pActionNew = m_pToolbar->addAction(UIIconPool::iconSet(":/usb_new_16px.png",
                                                                 ":/usb_new_disabled_16px.png"),
                                             QString(), this, SLOT(sltCreateFilter()));
        if (m_pActionNew)
            m_pActionNew->setShortcuts(QList<QKeySequence>() << QKeySequence("Ins") << QKeySequence("Ctrl+N"));

        /* Prepare 'Add USB Filter' action: */
        m_pActionAdd = m_pToolbar->addAction(UIIconPool::iconSet(":/usb_add_16px.png",
                                                                 ":/usb_add_disabled_16px.png"),
                                             QString(), this, SLOT(sltAddFilter()));
        if (m_pActionAdd)
            m_pActionAdd->setShortcuts(QList<QKeySequence>() << QKeySequence("Alt+Ins") << QKeySequence("Ctrl+A"));

        /* Prepare 'Edit USB Filter' action: */
        m_pActionEdit = m_pToolbar->addAction(UIIconPool::iconSet(":/usb_filter_edit_16px.png",
                                                                  ":/usb_filter_edit_disabled_16px.png"),
                                              QString(), this, SLOT(sltEditFilter()));
        if (m_pActionEdit)
            m_pActionEdit->setShortcuts(QList<QKeySequence>() << QKeySequence("Alt+Return") << QKeySequence("Ctrl+Return"));

        /* Prepare 'Remove USB Filter' action: */
        m_pActionRemove = m_pToolbar->addAction(UIIconPool::iconSet(":/usb_remove_16px.png",
                                                                    ":/usb_remove_disabled_16px.png"),
                                                QString(), this, SLOT(sltRemoveFilter()));
        if (m_pActionRemove)
            m_pActionRemove->setShortcuts(QList<QKeySequence>() << QKeySequence("Del") << QKeySequence("Ctrl+R"));

        /* Prepare 'Move USB Filter Up' action: */
        m_pActionMoveUp = m_pToolbar->addAction(UIIconPool::iconSet(":/usb_moveup_16px.png",
                                                                    ":/usb_moveup_disabled_16px.png"),
                                                QString(), this, SLOT(sltMoveFilterUp()));
        if (m_pActionMoveUp)
            m_pActionMoveUp->setShortcuts(QList<QKeySequence>() << QKeySequence("Alt+Up") << QKeySequence("Ctrl+Up"));

        /* Prepare 'Move USB Filter Down' action: */
        m_pActionMoveDown = m_pToolbar->addAction(UIIconPool::iconSet(":/usb_movedown_16px.png",
                                                                      ":/usb_movedown_disabled_16px.png"),
                                                  QString(), this, SLOT(sltMoveFilterDown()));
        if (m_pActionMoveDown)
            m_pActionMoveDown->setShortcuts(QList<QKeySequence>() << QKeySequence("Alt+Down") << QKeySequence("Ctrl+Down"));

        /* Prepare USB devices menu: */
        m_pMenuUSBDevices = new UIUSBMenu(this);

        m_pLayoutTree->addWidget(m_pToolbar);
    }
}

void UIUSBFiltersEditor::prepareConnections()
{
    /* Configure tree-widget connections: */
    if (m_pTreeWidget)
    {
        connect(m_pTreeWidget, &QITreeWidget::currentItemChanged,
                this, &UIUSBFiltersEditor::sltHandleCurrentItemChange);
        connect(m_pTreeWidget, &QITreeWidget::itemDoubleClicked,
                this, &UIUSBFiltersEditor::sltHandleDoubleClick);
        connect(m_pTreeWidget, &QITreeWidget::customContextMenuRequested,
                this, &UIUSBFiltersEditor::sltHandleContextMenuRequest);
        connect(m_pTreeWidget, &QITreeWidget::itemChanged,
                this, &UIUSBFiltersEditor::sltHandleActivityStateChange);
    }

    /* Configure USB device menu connections: */
    if (m_pMenuUSBDevices)
        connect(m_pMenuUSBDevices, &UIUSBMenu::triggered,
                this, &UIUSBFiltersEditor::sltAddFilterConfirmed);
}

void UIUSBFiltersEditor::addUSBFilterItem(const UIDataUSBFilter &data, bool fChoose)
{
    /* Create USB filter item: */
    USBFilterTreeWidgetItem *pItem = new USBFilterTreeWidgetItem(m_pTreeWidget);
    if (pItem)
    {
        /* Configure item: */
        pItem->setCheckState(0, data.m_fActive ? Qt::Checked : Qt::Unchecked);
        pItem->m_fActive = data.m_fActive;
        pItem->m_strName = data.m_strName;
        pItem->m_strVendorId = data.m_strVendorId;
        pItem->m_strProductId = data.m_strProductId;
        pItem->m_strRevision = data.m_strRevision;
        pItem->m_strManufacturer = data.m_strManufacturer;
        pItem->m_strProduct = data.m_strProduct;
        pItem->m_strSerialNumber = data.m_strSerialNumber;
        pItem->m_strPort = data.m_strPort;
        pItem->m_enmRemoteMode = data.m_enmRemoteMode;
        pItem->updateFields();

        /* Select this item if it's new: */
        if (fChoose)
        {
            m_pTreeWidget->scrollToItem(pItem);
            m_pTreeWidget->setCurrentItem(pItem);
            sltHandleCurrentItemChange(pItem);
        }
    }
}

void UIUSBFiltersEditor::reloadTree()
{
    /* Sanity check: */
    if (!m_pTreeWidget)
        return;

    /* Clear list initially: */
    m_pTreeWidget->clear();

    /* For each filter => load it from cache: */
    foreach (const UIDataUSBFilter &guiData, m_guiValue)
        addUSBFilterItem(guiData, false /* its new? */);

    /* Choose first filter as current: */
    m_pTreeWidget->setCurrentItem(m_pTreeWidget->topLevelItem(0));
    sltHandleCurrentItemChange(m_pTreeWidget->currentItem());
}


#include "UIUSBFiltersEditor.moc"
