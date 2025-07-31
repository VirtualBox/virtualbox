/* $Id: UIVMActivityMonitorContainer.h 110344 2025-07-22 07:46:07Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMActivityMonitorPanel class declaration.
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

#ifndef FEQT_INCLUDED_SRC_activity_vmactivity_UIVMActivityMonitorContainer_h
#define FEQT_INCLUDED_SRC_activity_vmactivity_UIVMActivityMonitorContainer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QUuid>

/* GUI includes: */
#include "UILibraryDefs.h"
#include "UIPaneContainer.h"

class CCloudMachine;
class CMachine;
class QColor;
class QLabel;
class QPushButton;
class QTabWidget;
class UIActionPool;

/** A pane container class for activity monitor widget. It hosts several controls
   for activity monitor settings. */
class SHARED_LIBRARY_STUFF UIVMActivityMonitorPaneContainer : public UIPaneContainer
{

    Q_OBJECT;

signals:

    void sigColorChanged(int iIndex, const QColor &color);

public:

    UIVMActivityMonitorPaneContainer(QWidget *pParent);
    void setDataSeriesColor(int iIndex, const QColor &color);
    QColor dataSeriesColor(int iIndex) const;

private slots:

    void sltRetranslateUI();
    void sltColorChangeButtonPressed();
    void sltResetToDefaults();

private:
    enum Tab
    {
        Tab_Preferences = 0
    };

    void prepare();
    void colorPushButtons(QPushButton *pButton, const QColor &color);
    QString m_strTabText;

    QLabel *m_pColorLabel[2];
    QPushButton *m_pColorChangeButton[2];
    QPushButton *m_pResetButton;

    QColor m_color[2];

};

/** A QWidget extension to host a tab widget and UIVMActivityMonitorPaneContainer. The tab widget
 hosts possibly multiple pages of UIVMActivityMonitor. */
class SHARED_LIBRARY_STUFF UIVMActivityMonitorContainer : public QWidget
{

    Q_OBJECT;

public:

    UIVMActivityMonitorContainer(QWidget *pParent, UIActionPool *pActionPool, EmbedTo enmEmbedding);
    void removeTabs(const QVector<QUuid> &machineIdsToRemove);
    void addLocalMachine(const CMachine &comMachine);
    void addCloudMachine(const CCloudMachine &comMachine);

private slots:

    void sltCurrentTabChanged(int iIndex);
    void sltDataSeriesColorChanged(int iIndex, const QColor &color);
    void sltExportToFile();
    void sltTogglePreferencesPane(bool fChecked);

private:

    void prepare();
    void loadSettings();
    void saveSettings();
    void setExportActionEnabled(bool fEnabled);

    UIVMActivityMonitorPaneContainer *m_pPaneContainer;
    QTabWidget *m_pTabWidget;
    QAction *m_pExportToFileAction;
    UIActionPool *m_pActionPool;
    EmbedTo m_enmEmbedding;
};

#endif /* !FEQT_INCLUDED_SRC_activity_vmactivity_UIVMActivityMonitorContainer_h */
