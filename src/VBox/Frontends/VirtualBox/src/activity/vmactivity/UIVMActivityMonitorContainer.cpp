/* $Id: UIVMActivityMonitorContainer.cpp 168940 2025-05-20 14:00:56Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMLogViewer class implementation.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <QColor>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QTabWidget>

/* GUI includes: */
#include "UIActionPool.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UITranslationEventListener.h"
#include "UIVMActivityMonitor.h"
#include "UIVMActivityMonitorContainer.h"

/* Other includes: */
#include "iprt/assert.h"


/*********************************************************************************************************************************
*   UIVMActivityMonitorPaneContainer implementation.                                                                             *
*********************************************************************************************************************************/


UIVMActivityMonitorPaneContainer::UIVMActivityMonitorPaneContainer(QWidget *pParent)
    : UIPaneContainer(pParent)
    , m_pColorLabel{0, 0}
    , m_pColorChangeButton{0, 0}
    , m_pResetButton(0)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    prepare();
}

void UIVMActivityMonitorPaneContainer::prepare()
{
    QWidget *pContainerWidget = new QWidget(this);
    QVBoxLayout *pContainerLayout = new QVBoxLayout(pContainerWidget);
    AssertReturnVoid(pContainerWidget);
    AssertReturnVoid(pContainerLayout);
    insertTab(Tab_Preferences, pContainerWidget, "");
    pContainerWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    for (int i = 0; i < 2; ++i)
    {
        QHBoxLayout *pColorLayout = new QHBoxLayout;
        AssertReturnVoid(pColorLayout);
        m_pColorLabel[i] = new QLabel(this);
        m_pColorChangeButton[i] = new QPushButton(this);
        AssertReturnVoid(m_pColorLabel[i]);
        AssertReturnVoid(m_pColorChangeButton[i]);
        pColorLayout->addWidget(m_pColorLabel[i]);
        pColorLayout->addWidget(m_pColorChangeButton[i]);
        pColorLayout->addStretch();
        pContainerLayout->addLayout(pColorLayout);
        connect(m_pColorChangeButton[i], &QPushButton::pressed,
                this, &UIVMActivityMonitorPaneContainer::sltColorChangeButtonPressed);
    }
    m_pResetButton = new QPushButton(this);
    AssertReturnVoid(m_pResetButton);
    m_pResetButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    connect(m_pResetButton, &QPushButton::pressed,
            this, &UIVMActivityMonitorPaneContainer::sltResetToDefaults);

    pContainerLayout->addWidget(m_pResetButton);

    pContainerLayout->addStretch();

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIVMActivityMonitorPaneContainer::sltRetranslateUI);

}

void UIVMActivityMonitorPaneContainer::sltRetranslateUI()
{
    setTabText(Tab_Preferences, QApplication::translate("UIVMActivityMonitorPaneContainer", "Preferences"));

    if (m_pColorLabel[0])
        m_pColorLabel[0]->setText(QApplication::translate("UIVMActivityMonitorPaneContainer", "Data Series 1 Color"));
    if (m_pColorLabel[1])
        m_pColorLabel[1]->setText(QApplication::translate("UIVMActivityMonitorPaneContainer", "Data Series 2 Color"));
    if (m_pResetButton)
        m_pResetButton->setText(QApplication::translate("UIVMActivityMonitorPaneContainer", "Reset to Defaults"));

}

void UIVMActivityMonitorPaneContainer::colorPushButtons(QPushButton *pButton, const QColor &color)
{
    AssertReturnVoid(pButton);
    int iSize = qApp->style()->pixelMetric(QStyle::PM_ButtonIconSize);
    QPixmap iconPixmap(iSize, iSize);
    QPainter painter(&iconPixmap);
    painter.setBrush(color);
    painter.drawRect(iconPixmap.rect());
    pButton->setIcon(QIcon(iconPixmap));
}

void UIVMActivityMonitorPaneContainer::setDataSeriesColor(int iIndex, const QColor &color)
{
    if (iIndex == 0 || iIndex == 1)
    {
        if (m_color[iIndex] != color)
        {
            m_color[iIndex] = color;
            colorPushButtons(m_pColorChangeButton[iIndex], color);
            emit sigColorChanged(iIndex, color);
        }
    }
}

QColor UIVMActivityMonitorPaneContainer::dataSeriesColor(int iIndex) const
{
    if (iIndex >= 0 && iIndex < 2)
        return m_color[iIndex];
    return QColor();
}

void UIVMActivityMonitorPaneContainer::sltColorChangeButtonPressed()
{
    int iIndex = -1;
    if (sender() == m_pColorChangeButton[0])
        iIndex = 0;
    else if (sender() == m_pColorChangeButton[1])
        iIndex = 1;
    else
        return;

    QColorDialog colorDialog(m_color[iIndex], this);
    if (colorDialog.exec() == QDialog::Rejected)
        return;
    QColor newColor = colorDialog.selectedColor();
    if (m_color[iIndex] == newColor)
        return;
    m_color[iIndex] = newColor;
    colorPushButtons(m_pColorChangeButton[iIndex], newColor);
    emit sigColorChanged(iIndex, newColor);
}

void UIVMActivityMonitorPaneContainer::sltResetToDefaults()
{
    /* Reset data series colors: */
    setDataSeriesColor(0, QApplication::palette().color(QPalette::LinkVisited));
    setDataSeriesColor(1, QApplication::palette().color(QPalette::Link));
}

/*********************************************************************************************************************************
*   UIVMActivityMonitorContainer implementation.                                                                            *
*********************************************************************************************************************************/

UIVMActivityMonitorContainer::UIVMActivityMonitorContainer(QWidget *pParent, UIActionPool *pActionPool, EmbedTo enmEmbedding)
    :QWidget(pParent)
    , m_pPaneContainer(0)
    , m_pTabWidget(0)
    , m_pExportToFileAction(0)
    , m_pActionPool(pActionPool)
    , m_enmEmbedding(enmEmbedding)
{
    prepare();
    loadSettings();
    sltCurrentTabChanged(0);
}

void UIVMActivityMonitorContainer::removeTabs(const QVector<QUuid> &machineIdsToRemove)
{
    AssertReturnVoid(m_pTabWidget);
    QVector<UIVMActivityMonitor*> removeList;

    for (int i = m_pTabWidget->count() - 1; i >= 0; --i)
    {
        UIVMActivityMonitor *pMonitor = qobject_cast<UIVMActivityMonitor*>(m_pTabWidget->widget(i));
        if (!pMonitor)
            continue;
        if (machineIdsToRemove.contains(pMonitor->machineId()))
        {
            removeList << pMonitor;
            m_pTabWidget->removeTab(i);
        }
    }
    qDeleteAll(removeList.begin(), removeList.end());
}

void UIVMActivityMonitorContainer::prepare()
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    pMainLayout->setContentsMargins(0, 0, 0, 0);

    m_pTabWidget = new QTabWidget(this);
    m_pTabWidget->setTabPosition(QTabWidget::East);
    m_pTabWidget->setTabBarAutoHide(true);

    m_pPaneContainer = new UIVMActivityMonitorPaneContainer(this);
    m_pPaneContainer->hide();

    pMainLayout->addWidget(m_pTabWidget);
    pMainLayout->addWidget(m_pPaneContainer);

    connect(m_pTabWidget, &QTabWidget::currentChanged,
            this, &UIVMActivityMonitorContainer::sltCurrentTabChanged);
    connect(m_pPaneContainer, &UIVMActivityMonitorPaneContainer::sigColorChanged,
            this, &UIVMActivityMonitorContainer::sltDataSeriesColorChanged);
    m_pExportToFileAction = m_pActionPool->action(UIActionIndex_M_Activity_S_Export);
    if (m_pExportToFileAction)
        connect(m_pExportToFileAction, &QAction::triggered, this, &UIVMActivityMonitorContainer::sltExportToFile);

    if (m_pActionPool->action(UIActionIndex_M_Activity_T_Preferences))
        connect(m_pActionPool->action(UIActionIndex_M_Activity_T_Preferences), &QAction::toggled,
                this, &UIVMActivityMonitorContainer::sltTogglePreferencesPane);
}

void UIVMActivityMonitorContainer::loadSettings()
{
    if (m_pPaneContainer)
    {
        QStringList colorList = gEDataManager->VMActivityMonitorDataSeriesColors();
        if (colorList.size() == 2)
        {
            for (int i = 0; i < 2; ++i)
            {
                QColor color(colorList[i]);
                if (color.isValid())
                    m_pPaneContainer->setDataSeriesColor(i, color);
            }
        }
        if (!m_pPaneContainer->dataSeriesColor(0).isValid())
            m_pPaneContainer->setDataSeriesColor(0, QApplication::palette().color(QPalette::LinkVisited));
        if (!m_pPaneContainer->dataSeriesColor(1).isValid())
            m_pPaneContainer->setDataSeriesColor(1, QApplication::palette().color(QPalette::Link));
    }
}

void UIVMActivityMonitorContainer::saveSettings()
{
    if (m_pPaneContainer)
    {
        QStringList colorList;
        colorList << m_pPaneContainer->dataSeriesColor(0).name(QColor::HexArgb);
        colorList << m_pPaneContainer->dataSeriesColor(1).name(QColor::HexArgb);
        gEDataManager->setVMActivityMonitorDataSeriesColors(colorList);
    }
}

void UIVMActivityMonitorContainer::sltCurrentTabChanged(int iIndex)
{
    AssertReturnVoid(m_pTabWidget);
    Q_UNUSED(iIndex);
    UIVMActivityMonitor *pActivityMonitor = qobject_cast<UIVMActivityMonitor*>(m_pTabWidget->currentWidget());
    if (pActivityMonitor)
    {
        CMachine comMachine = gpGlobalSession->virtualBox().FindMachine(pActivityMonitor->machineId().toString());
        if (!comMachine.isNull())
        {
            setExportActionEnabled(comMachine.GetState() == KMachineState_Running);
        }
    }
}

void UIVMActivityMonitorContainer::sltDataSeriesColorChanged(int iIndex, const QColor &color)
{
    for (int i = m_pTabWidget->count() - 1; i >= 0; --i)
    {
        UIVMActivityMonitor *pMonitor = qobject_cast<UIVMActivityMonitor*>(m_pTabWidget->widget(i));
        if (!pMonitor)
            continue;
        pMonitor->setDataSeriesColor(iIndex, color);
    }
    saveSettings();
}

void UIVMActivityMonitorContainer::setExportActionEnabled(bool fEnabled)
{
    if (m_pExportToFileAction)
        m_pExportToFileAction->setEnabled(fEnabled);
}

void UIVMActivityMonitorContainer::sltExportToFile()
{
    AssertReturnVoid(m_pTabWidget);
    UIVMActivityMonitor *pActivityMonitor = qobject_cast<UIVMActivityMonitor*>(m_pTabWidget->currentWidget());
    if (pActivityMonitor)
        pActivityMonitor->sltExportMetricsToFile();
}

void UIVMActivityMonitorContainer::addLocalMachine(const CMachine &comMachine)
{
    AssertReturnVoid(m_pTabWidget);
    if (!comMachine.isOk())
        return;
    UIVMActivityMonitorLocal *pActivityMonitor = new UIVMActivityMonitorLocal(m_enmEmbedding, this, comMachine, m_pActionPool);
    if (m_pPaneContainer)
    {
        pActivityMonitor->setDataSeriesColor(0, m_pPaneContainer->dataSeriesColor(0));
        pActivityMonitor->setDataSeriesColor(1, m_pPaneContainer->dataSeriesColor(1));
    }
    m_pTabWidget->addTab(pActivityMonitor, comMachine.GetName());
}

void UIVMActivityMonitorContainer::addCloudMachine(const CCloudMachine &comMachine)
{
    AssertReturnVoid(m_pTabWidget);
    if (!comMachine.isOk())
        return;
    UIVMActivityMonitorCloud *pActivityMonitor = new UIVMActivityMonitorCloud(m_enmEmbedding, this, comMachine, m_pActionPool);
    if (m_pPaneContainer)
    {
        pActivityMonitor->setDataSeriesColor(0, m_pPaneContainer->dataSeriesColor(0));
        pActivityMonitor->setDataSeriesColor(1, m_pPaneContainer->dataSeriesColor(1));
    }
    m_pTabWidget->addTab(pActivityMonitor, comMachine.GetName());
}

void UIVMActivityMonitorContainer::sltTogglePreferencesPane(bool fChecked)
{
    AssertReturnVoid(m_pPaneContainer);
    m_pPaneContainer->setVisible(fChecked);
}

void UIVMActivityMonitorContainer::guestAdditionsStateChange(const QUuid &machineId)
{
    for (int i = m_pTabWidget->count() - 1; i >= 0; --i)
    {
        UIVMActivityMonitorLocal *pMonitor = qobject_cast<UIVMActivityMonitorLocal*>(m_pTabWidget->widget(i));
        if (!pMonitor)
            continue;
        if (pMonitor->machineId() == machineId)
            pMonitor->guestAdditionsStateChange();
    }
}
