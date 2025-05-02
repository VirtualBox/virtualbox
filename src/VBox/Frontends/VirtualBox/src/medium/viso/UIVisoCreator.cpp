/* $Id: UIVisoCreator.cpp 109396 2025-05-02 14:26:17Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVisoCreator classes implementation.
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
#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QTextStream>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QIFileDialog.h"
#include "QILabel.h"
#include "QILineEdit.h"
#include "QIToolBar.h"
#include "UIActionPool.h"
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UILoggingDefs.h"
#include "UIMediumEnumerator.h"
#include "UIMediumTools.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UIPaneContainer.h"
#include "UIShortcutPool.h"
#include "UITranslationEventListener.h"
#include "UIVisoCreator.h"
#include "UIVisoContentBrowser.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
#endif

/* Other VBox includes: */
#include <iprt/assert.h>
#include <iprt/getopt.h>

/*********************************************************************************************************************************
*   UIVisoSettingWidget definition.                                                                                          *
*********************************************************************************************************************************/

class SHARED_LIBRARY_STUFF UIVisoSettingWidget : public UIPaneContainer
{
    Q_OBJECT;

signals:

    void sigSettingsChanged();

public:

    UIVisoSettingWidget(QWidget *pParent);
    void setVisoName(const QString &strName);
    void setSettings(const UIVisoCreatorWidget::Settings &settings);
    UIVisoCreatorWidget::Settings settings() const;

private slots:

    void sltRetranslateUI();

private:

    void prepare() RT_OVERRIDE;
    void prepareConnections();

    QILabel      *m_pVisoNameLabel;
    QILabel      *m_pCustomOptionsLabel;
    QILineEdit   *m_pVisoNameLineEdit;
    QILineEdit   *m_pCustomOptionsLineEdit;
    QCheckBox    *m_pShowHiddenObjectsCheckBox;
    QGridLayout  *m_pVisoOptionsGridLayout;
};


/*********************************************************************************************************************************
*   UIVisoHostBrowser implementation.                                                                                            *
*********************************************************************************************************************************/

UIVisoHostBrowser::UIVisoHostBrowser(UIActionPool *pActionPool, QWidget *pParent /* = 0 */)
    :UIFileManagerHostTable(pActionPool, pParent)
{
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIVisoHostBrowser::sltRetranslateUI);
}

void UIVisoHostBrowser::createFileViewContextMenu(const QWidget *pWidget, const QPoint &point)
{
    if (!pWidget)
        return;

    QMenu menu;
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoUp));

    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoHome));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Refresh));
    menu.addSeparator();
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_SelectAll));
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_InvertSelection));
    menu.addSeparator();
    menu.addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_ShowProperties));
    menu.exec(pWidget->mapToGlobal(point));
}

void UIVisoHostBrowser::prepareMainMenu(QMenu *pMenu)
{
    AssertReturnVoid(pMenu);
    QMenu *pSubMenu = new QMenu(QApplication::translate("UIVisoCreatorWidget", "Host Browser"), pMenu);
    pMenu->addMenu(pSubMenu);
    AssertReturnVoid(pSubMenu);
    m_pSubMenu = pSubMenu;

    pSubMenu->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoUp));

    pSubMenu->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_GoHome));
    pSubMenu->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_Refresh));
    pSubMenu->addSeparator();
    pSubMenu->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_SelectAll));
    pSubMenu->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_InvertSelection));
    pSubMenu->addSeparator();
    pSubMenu->addAction(m_pActionPool->action(UIActionIndex_M_FileManager_S_Host_ShowProperties));
}


void UIVisoHostBrowser::sltRetranslateUI()
{
    UIFileManagerHostTable::sltRetranslateUI();
    if (m_pSubMenu)
        m_pSubMenu->setTitle(QApplication::translate("UIVisoCreatorWidget", "VISO Browser"));
}


/*********************************************************************************************************************************
*   UIVisoSettingWidget implementation.                                                                                          *
*********************************************************************************************************************************/

UIVisoSettingWidget::UIVisoSettingWidget(QWidget *pParent)
    : UIPaneContainer(pParent)
    , m_pVisoNameLabel(0)
    , m_pCustomOptionsLabel(0)
    , m_pVisoNameLineEdit(0)
    , m_pCustomOptionsLineEdit(0)
    , m_pShowHiddenObjectsCheckBox(0)
    , m_pVisoOptionsGridLayout(0)
{
    prepare();
    prepareConnections();
}

void UIVisoSettingWidget::prepare()
{
    QWidget *pVisoOptionsContainerWidget = new QWidget;
    AssertReturnVoid(pVisoOptionsContainerWidget);
    m_pVisoOptionsGridLayout = new QGridLayout(pVisoOptionsContainerWidget);
    AssertReturnVoid(m_pVisoOptionsGridLayout);
    //pVisoOptionsGridLayout->setSpacing(0);
    //pVisoOptionsGridLayout->setContentsMargins(0, 0, 0, 0);

    insertTab(0, pVisoOptionsContainerWidget);

    /* Name edit and and label: */
    m_pVisoNameLabel = new QILabel(QApplication::translate("UIVisoCreatorWidget", "VISO Name:"));
    m_pVisoNameLineEdit = new QILineEdit;
    int iRow = 0;
    AssertReturnVoid(m_pVisoNameLabel);
    AssertReturnVoid(m_pVisoNameLineEdit);
    m_pVisoNameLabel->setBuddy(m_pVisoNameLineEdit);
    m_pVisoNameLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

    m_pVisoOptionsGridLayout->addWidget(m_pVisoNameLabel, iRow, 0, 1, 1, Qt::AlignTop);
    m_pVisoOptionsGridLayout->addWidget(m_pVisoNameLineEdit, iRow, 1, 1, 1, Qt::AlignTop);
    m_pVisoOptionsGridLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum), iRow, 2, 1, 3);

    /* Custom Viso options stuff: */
    m_pCustomOptionsLabel = new QILabel(QApplication::translate("UIVisoCreatorWidget", "Custom VISO options:"));
    m_pCustomOptionsLineEdit = new QILineEdit;
    ++iRow;
    AssertReturnVoid(m_pCustomOptionsLabel);
    AssertReturnVoid(m_pCustomOptionsLineEdit);
    m_pCustomOptionsLabel->setBuddy(m_pCustomOptionsLineEdit);

    m_pCustomOptionsLabel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    m_pVisoOptionsGridLayout->addWidget(m_pCustomOptionsLabel, iRow, 0, 1, 1, Qt::AlignTop);
    m_pVisoOptionsGridLayout->addWidget(m_pCustomOptionsLineEdit, iRow, 1, 1, 1, Qt::AlignTop);
    m_pVisoOptionsGridLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum), iRow, 2, 1, 3);

    ++iRow;
    m_pVisoOptionsGridLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding), iRow, 0, 1, 2);
    QWidget *pDialogSettingsContainerWidget = new QWidget;
    AssertReturnVoid(pDialogSettingsContainerWidget);
    QGridLayout *pDialogSettingsContainerLayout = new QGridLayout(pDialogSettingsContainerWidget);
    AssertReturnVoid(pDialogSettingsContainerLayout);

    insertTab(1, pDialogSettingsContainerWidget);

    iRow = 0;
    QHBoxLayout *pShowHiddenObjectsLayout = new QHBoxLayout;
    m_pShowHiddenObjectsCheckBox = new QCheckBox;
    pShowHiddenObjectsLayout->addWidget(m_pShowHiddenObjectsCheckBox);
    pShowHiddenObjectsLayout->addStretch(1);
    pDialogSettingsContainerLayout->addLayout(pShowHiddenObjectsLayout, iRow, 0, 1, 2, Qt::AlignTop);

    ++iRow;
    pDialogSettingsContainerLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding), iRow, 0, 1, 2);
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIVisoSettingWidget::sltRetranslateUI);
}

void UIVisoSettingWidget::sltRetranslateUI()
{
    int iLabelWidth = 0;
    if (m_pVisoNameLabel)
    {
        m_pVisoNameLabel->setText(QApplication::translate("UIVisoCreatorWidget", "VISO Name:"));
        iLabelWidth = m_pVisoNameLabel->width();
    }
    if (m_pCustomOptionsLabel)
    {
        m_pCustomOptionsLabel->setText(QApplication::translate("UIVisoCreatorWidget", "Custom VISO options:"));
        iLabelWidth = qMax(iLabelWidth, m_pCustomOptionsLabel->width());
    }

    if (m_pVisoNameLineEdit)
        m_pVisoNameLineEdit->setToolTip(QApplication::translate("UIVisoCreatorWidget", "Holds the name of the VISO medium."));
    if (m_pCustomOptionsLineEdit)
        m_pCustomOptionsLineEdit->setToolTip(QApplication::translate("UIVisoCreatorWidget", "The list of custom options delimited with ';'."));
    if (m_pShowHiddenObjectsCheckBox)
    {
        m_pShowHiddenObjectsCheckBox->setToolTip(QApplication::translate("UIVisoCreatorWidget", "When checked, "
                                                                         "multiple hidden objects are shown in the file browser"));
        m_pShowHiddenObjectsCheckBox->setText(QApplication::translate("UIVisoCreatorWidget", "Show Hidden Objects"));
    }
    setTabText(1, QApplication::translate("UIVisoCreatorWidget", "Dialog Settings"));
    setTabText(0, QApplication::translate("UIVisoCreatorWidget", "VISO Options"));
}

void UIVisoSettingWidget::prepareConnections()
{
    if (m_pVisoNameLineEdit)
        connect(m_pVisoNameLineEdit, &QILineEdit::textChanged, this, &UIVisoSettingWidget::sigSettingsChanged);
    if (m_pCustomOptionsLineEdit)
        connect(m_pCustomOptionsLineEdit, &QILineEdit::textChanged, this, &UIVisoSettingWidget::sigSettingsChanged);
    if (m_pShowHiddenObjectsCheckBox)
        connect(m_pShowHiddenObjectsCheckBox, &QCheckBox::toggled, this, &UIVisoSettingWidget::sigSettingsChanged);
}

void UIVisoSettingWidget::setVisoName(const QString &strName)
{
    if (m_pVisoNameLineEdit)
        m_pVisoNameLineEdit->setText(strName);
}

void UIVisoSettingWidget::setSettings(const UIVisoCreatorWidget::Settings &settings)
{
    if (m_pVisoNameLineEdit)
        m_pVisoNameLineEdit->setText(settings.m_strVisoName);
    if (m_pCustomOptionsLineEdit)
        m_pCustomOptionsLineEdit->setText(settings.m_customOptions.join(";"));
    if (m_pShowHiddenObjectsCheckBox)
        m_pShowHiddenObjectsCheckBox->setChecked(settings.m_fShowHiddenObjects);
}

UIVisoCreatorWidget::Settings UIVisoSettingWidget::settings() const
{
    UIVisoCreatorWidget::Settings settings;
    if (m_pVisoNameLineEdit)
        settings.m_strVisoName = m_pVisoNameLineEdit->text();
    if (m_pCustomOptionsLineEdit)
        settings.m_customOptions = m_pCustomOptionsLineEdit->text().split(";");
    if (m_pShowHiddenObjectsCheckBox)
        settings.m_fShowHiddenObjects = m_pShowHiddenObjectsCheckBox->isChecked();
    return settings;
}


/*********************************************************************************************************************************
*   UIVisoCreatorWidget implementation.                                                                                          *
*********************************************************************************************************************************/

UIVisoCreatorWidget::UIVisoCreatorWidget(UIActionPool *pActionPool, QWidget *pParent,
                                         bool fShowToolBar, const QString& strVisoFilePath, const QString& strMachineName)
    : QWidget(pParent)
    , m_pActionPreferences(0)
    , m_pAddAction(0)
    , m_pOpenAction(0)
    , m_pSaveAsAction(0)
    , m_pImportISOAction(0)
    , m_pRemoveISOAction(0)
    , m_pMainLayout(0)
    , m_pVISOContentBrowser(0)
    , m_pHostFileBrowser(0)
    , m_pToolBar(0)
    , m_pVerticalToolBar(0)
    , m_pMainMenu(0)
    , m_pActionPool(pActionPool)
    , m_fShowToolBar(fShowToolBar)
    , m_pSettingsWidget(0)
    , m_pBrowserContainerWidget(0)
    , m_strVisoFilePath(strVisoFilePath)
    , m_fShowSettingsDialog(false)
{
    m_settings.m_strVisoName = !strMachineName.isEmpty() ? strMachineName : "ad-hoc";
    prepareWidgets();
    populateMenuMainToolbar();
    prepareConnections();
    if (m_pActionPreferences)
        sltSettingsActionToggled(m_pActionPreferences->isChecked());
}

QStringList UIVisoCreatorWidget::entryList() const
{
    if (!m_pVISOContentBrowser)
        return QStringList();
    return m_pVISOContentBrowser->entryList();
}

QString UIVisoCreatorWidget::importedISOPath() const
{
    if (!m_pVISOContentBrowser)
        return QString();
    return m_pVISOContentBrowser->importedISOPath();
}

const QString &UIVisoCreatorWidget::visoName() const
{
    return m_settings.m_strVisoName;
}

void UIVisoCreatorWidget::setVisoName(const QString& strName)
{
    if (m_settings.m_strVisoName == strName)
        return;
    m_settings.m_strVisoName = strName;
    emit sigVisoNameChanged(m_settings.m_strVisoName);
    if (m_pSettingsWidget)
    {
        m_pSettingsWidget->blockSignals(true);
        m_pSettingsWidget->setVisoName(strName);
        m_pSettingsWidget->blockSignals(false);
    }
}

void UIVisoCreatorWidget::setVisoFilePath(const QString& strPath)
{
    if (m_strVisoFilePath == strPath)
        return;
    m_strVisoFilePath = QDir::fromNativeSeparators(QDir::cleanPath(strPath));
    emit sigVisoFilePathChanged(m_strVisoFilePath);
}

const QStringList &UIVisoCreatorWidget::customOptions() const
{
    return m_settings.m_customOptions;
}

QString UIVisoCreatorWidget::currentPath() const
{
    if (!m_pHostFileBrowser)
        return QString();
    return m_pHostFileBrowser->currentDirectoryPath();
}

void UIVisoCreatorWidget::setCurrentPath(const QString &/*strPath*/)
{
    if (!m_pHostFileBrowser)
        return;
    //m_pHostFileBrowser->goIntoDirectory(const QStringList &pathTrail);
}

QMenu *UIVisoCreatorWidget::menu() const
{
    return m_pMainMenu;
}

void UIVisoCreatorWidget::sltAddObjectsToViso()
{
    AssertPtrReturnVoid(m_pHostFileBrowser);
    AssertPtrReturnVoid(m_pVISOContentBrowser);
    m_pVISOContentBrowser->addObjectsToViso(m_pHostFileBrowser->selectedItemPathList());
}

void UIVisoCreatorWidget::sltSettingsActionToggled(bool fChecked)
{
    toggleSettingsWidget(fChecked);
}

void UIVisoCreatorWidget::sltHostBrowserTableSelectionChanged(bool fHasSelection)
{
    AssertPtrReturnVoid(m_pHostFileBrowser);
    QStringList pathList = m_pHostFileBrowser->selectedItemPathList();
    if (m_pAddAction)
        m_pAddAction->setEnabled(fHasSelection);
    if (m_pImportISOAction)
        m_pImportISOAction->setEnabled(!findISOFiles(pathList).isEmpty());
}

void UIVisoCreatorWidget::sltContentBrowserTableSelectionChanged(bool fIsSelectionEmpty)
{
    Q_UNUSED(fIsSelectionEmpty);
}

void UIVisoCreatorWidget::sltOpenAction()
{
    QWidget *pActive =  QApplication::activeWindow();
    AssertReturnVoid(pActive);
    if (m_pVISOContentBrowser->hasContent())
        if (!msgCenter().confirmVisoDiscard(pActive))
            return;
    QString strFileName =  QIFileDialog::getOpenFileName(UIMediumTools::defaultFolderPathForType(UIMediumDeviceType_DVD),
                                                         "VISO files (*.viso)", pActive, UIVisoCreatorWidget::tr("Select a VISO file to load"));
    if (!strFileName.isEmpty() && m_pVISOContentBrowser)
    {
        setVisoFilePath(QFileInfo(strFileName).absolutePath());
        setVisoName(QFileInfo(strFileName).baseName());
        m_pVISOContentBrowser->parseVisoFileContent(strFileName);
    }
}

void UIVisoCreatorWidget::sltSaveAsAction()
{
    QWidget *pActive =  QApplication::activeWindow();
    AssertReturnVoid(pActive);

    QString strSaveFileName = QIFileDialog::getSaveFileName(visoFileFullPath(), "VISO files (*.viso)", pActive,
                                                            UIVisoCreatorWidget::tr("Select a file to save VISO content to"));
    if (visoFileFullPath() != strSaveFileName)
    {
        QFileInfo fileInfo(strSaveFileName);
        setVisoFilePath(fileInfo.absolutePath());
        setVisoName(fileInfo.completeBaseName());
    }
    emit sigSave();
}

void UIVisoCreatorWidget::sltISOImportAction()
{
    if (!m_pHostFileBrowser || !m_pVISOContentBrowser)
        return;
    QStringList selectedObjectPaths = m_pHostFileBrowser->selectedItemPathList();
    if (selectedObjectPaths.isEmpty())
        return;
    /* We can import only a ISO file into VISO:*/
    if (m_pVISOContentBrowser->importedISOPath().isEmpty())
        m_pVISOContentBrowser->importISOContentToViso(selectedObjectPaths[0]);
}

void UIVisoCreatorWidget::sltISORemoveAction()
{
    if (!m_pVISOContentBrowser)
        return;
    m_pVISOContentBrowser->removeISOContentFromViso();
}

void UIVisoCreatorWidget::sltISOContentImportedOrRemoved(bool fImported)
{
    if (m_pImportISOAction)
        m_pImportISOAction->setEnabled(!fImported);
    if (m_pRemoveISOAction)
        m_pRemoveISOAction->setEnabled(fImported);
}

void UIVisoCreatorWidget::sltVisoVolumeIdParsed(const QString &strVolumeId)
{
    setVisoName(strVolumeId);
}

void UIVisoCreatorWidget::sltSettingsChanged()
{
    AssertReturnVoid(m_pSettingsWidget);
    const Settings &settings = m_pSettingsWidget->settings();
    setVisoName(settings.m_strVisoName);
    if (m_settings.m_customOptions != settings.m_customOptions)
        m_settings.m_customOptions = settings.m_customOptions;
    if (m_settings.m_fShowHiddenObjects != settings.m_fShowHiddenObjects)
    {
        m_settings.m_fShowHiddenObjects = settings.m_fShowHiddenObjects;
        // if (m_pHostFileBrowser)
        //     m_pHostFileBrowser->showHideHiddenObjects(settings.m_fShowHiddenObjects);
    }
}

void UIVisoCreatorWidget::sltPanelContainerHidden()
{
    m_pActionPool->action(UIActionIndex_M_VISOCreator_TogglePreferences)->blockSignals(true);
    m_pActionPool->action(UIActionIndex_M_VISOCreator_TogglePreferences)->setChecked(false);
    m_pActionPool->action(UIActionIndex_M_VISOCreator_TogglePreferences)->blockSignals(false);
}

void UIVisoCreatorWidget::prepareWidgets()
{
    m_pMainLayout = new QVBoxLayout(this);
    AssertPtrReturnVoid(m_pMainLayout);

    /* Configure layout: */
    const int iL = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 2;
    const int iT = qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin) / 2;
    const int iR = qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin) / 2;
    const int iB = qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin) / 2;
    m_pMainLayout->setContentsMargins(iL, iT, iR, iB);
#ifdef VBOX_WS_MAC
    m_pMainLayout->setSpacing(10);
#else
    m_pMainLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing) / 2);
#endif

    if (m_pActionPool && m_pActionPool->action(UIActionIndex_M_VISOCreator))
    {
        m_pMainMenu = m_pActionPool->action(UIActionIndex_M_VISOCreator)->menu();
        m_pMainMenu->clear();
    }

    if (m_fShowToolBar)
    {
        m_pToolBar = new QIToolBar(parentWidget());
        AssertPtrReturnVoid(m_pToolBar);
        /* Configure toolbar: */
        const int iIconMetric = (int)(QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize));
        m_pToolBar->setIconSize(QSize(iIconMetric, iIconMetric));
        m_pToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    }

    m_pBrowserContainerWidget = new QWidget;
    AssertPtrReturnVoid(m_pBrowserContainerWidget);

    QGridLayout *pContainerLayout = new QGridLayout(m_pBrowserContainerWidget);
    AssertPtrReturnVoid(pContainerLayout);
    pContainerLayout->setContentsMargins(0, 0, 0, 0);

    m_pHostFileBrowser = new UIVisoHostBrowser(m_pActionPool);
    AssertPtrReturnVoid(m_pHostFileBrowser);
    pContainerLayout->addWidget(m_pHostFileBrowser, 0, 0, 1, 4);
    m_pHostFileBrowser->setDragDropMode(QAbstractItemView::DragOnly);

    prepareVerticalToolBar();
    AssertPtrReturnVoid(m_pVerticalToolBar);
    pContainerLayout->addWidget(m_pVerticalToolBar, 0, 4, 1, 1);

    m_pVISOContentBrowser = new UIVisoContentBrowser(m_pActionPool);
    AssertPtrReturnVoid(m_pVISOContentBrowser);
    pContainerLayout->addWidget(m_pVISOContentBrowser, 0, 5, 1, 4);
    /* Set content browsers sort case sensitivity wrt. host's file system: */
    m_pVISOContentBrowser->setSortCaseSensitive(!m_pHostFileBrowser->isWindowsFileSystem());

    m_pSettingsWidget = new UIVisoSettingWidget(this);

    AssertPtrReturnVoid(m_pSettingsWidget);

    if (m_pToolBar)
        m_pMainLayout->addWidget(m_pToolBar);
    m_pMainLayout->addWidget(m_pBrowserContainerWidget);
    m_pMainLayout->addWidget(m_pSettingsWidget);
    m_pBrowserContainerWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_pSettingsWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_pSettingsWidget->hide();
}

void UIVisoCreatorWidget::prepareConnections()
{
    if (m_pHostFileBrowser)
    {
        connect(m_pHostFileBrowser, &UIVisoHostBrowser::sigAltDoubleClick,
                this, &UIVisoCreatorWidget::sltAddObjectsToViso);
        connect(m_pHostFileBrowser, &UIVisoHostBrowser::sigSelectionChanged,
                this, &UIVisoCreatorWidget::sltHostBrowserTableSelectionChanged);
    }

    if (m_pVISOContentBrowser)
    {
        connect(m_pVISOContentBrowser, &UIVisoContentBrowser::sigTableSelectionChanged,
                this, &UIVisoCreatorWidget::sltContentBrowserTableSelectionChanged);
        connect(m_pVISOContentBrowser, &UIVisoContentBrowser::sigISOContentImportedOrRemoved,
                this, &UIVisoCreatorWidget::sltISOContentImportedOrRemoved);
        connect(m_pVISOContentBrowser, &UIVisoContentBrowser::sigVolumeIdParsed,
                this, &UIVisoCreatorWidget::sltVisoVolumeIdParsed);
    }

    if (m_pActionPreferences)
        connect(m_pActionPreferences, &QAction::triggered, this, &UIVisoCreatorWidget::sltSettingsActionToggled);

    if (m_pSettingsWidget)
    {
        connect(m_pSettingsWidget, &UIVisoSettingWidget::sigSettingsChanged,
                this, &UIVisoCreatorWidget::sltSettingsChanged);
        connect(m_pSettingsWidget, &UIVisoSettingWidget::sigHidden,
                this, &UIVisoCreatorWidget::sltPanelContainerHidden);
    }

    if (m_pAddAction)
        connect(m_pAddAction, &QAction::triggered,
                this, &UIVisoCreatorWidget::sltAddObjectsToViso);
    if (m_pOpenAction)
        connect(m_pOpenAction, &QAction::triggered,
                this, &UIVisoCreatorWidget::sltOpenAction);
    if (m_pSaveAsAction)
        connect(m_pSaveAsAction, &QAction::triggered,
                this, &UIVisoCreatorWidget::sltSaveAsAction);
    if (m_pImportISOAction)
        connect(m_pImportISOAction, &QAction::triggered,
                this, &UIVisoCreatorWidget::sltISOImportAction);
    if (m_pRemoveISOAction)
        connect(m_pRemoveISOAction, &QAction::triggered,
                this, &UIVisoCreatorWidget::sltISORemoveAction);
}

void UIVisoCreatorWidget::prepareActions()
{
    if (!m_pActionPool)
        return;

    m_pActionPreferences = m_pActionPool->action(UIActionIndex_M_VISOCreator_TogglePreferences);

    m_pAddAction = m_pActionPool->action(UIActionIndex_M_VISOCreator_Add);
    if (m_pAddAction && m_pHostFileBrowser)
        m_pAddAction->setEnabled(m_pHostFileBrowser->hasSelection());
    m_pOpenAction = m_pActionPool->action(UIActionIndex_M_VISOCreator_Open);
    m_pSaveAsAction = m_pActionPool->action(UIActionIndex_M_VISOCreator_SaveAs);
    m_pImportISOAction = m_pActionPool->action(UIActionIndex_M_VISOCreator_ImportISO);
    if (m_pImportISOAction)
        m_pImportISOAction->setEnabled(false);

    m_pRemoveISOAction = m_pActionPool->action(UIActionIndex_M_VISOCreator_RemoveISO);
    if (m_pRemoveISOAction)
        m_pRemoveISOAction->setEnabled(false);
}

void UIVisoCreatorWidget::populateMenuMainToolbar()
{
    prepareActions();
    if (m_pToolBar)
    {
        if (m_pActionPreferences)
            m_pToolBar->addAction(m_pActionPreferences);
    }
    if (m_pMainMenu)
    {
        m_pMainMenu->addAction(m_pActionPreferences);
        m_pMainMenu->addSeparator();
        if (m_pOpenAction)
            m_pMainMenu->addAction(m_pOpenAction);
        if (m_pSaveAsAction)
            m_pMainMenu->addAction(m_pSaveAsAction);
        if (m_pAddAction)
            m_pMainMenu->addAction(m_pAddAction);
        if (m_pImportISOAction)
            m_pMainMenu->addAction(m_pImportISOAction);
        if (m_pRemoveISOAction)
            m_pMainMenu->addAction(m_pRemoveISOAction);
    }

    if (m_pHostFileBrowser)
        m_pHostFileBrowser->prepareMainMenu(m_pMainMenu);

    if (m_pVISOContentBrowser)
        m_pVISOContentBrowser->prepareMainMenu(m_pMainMenu);

    if (m_pVerticalToolBar)
    {
        /* Add to dummy QWidget to toolbar to center the action icons vertically: */
        QWidget *topSpacerWidget = new QWidget(this);
        AssertPtrReturnVoid(topSpacerWidget);
        topSpacerWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        topSpacerWidget->setVisible(true);
        QWidget *bottomSpacerWidget = new QWidget(this);
        AssertPtrReturnVoid(bottomSpacerWidget);
        bottomSpacerWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        bottomSpacerWidget->setVisible(true);

        m_pVerticalToolBar->addWidget(topSpacerWidget);
        if (m_pAddAction)
            m_pVerticalToolBar->addAction(m_pAddAction);
        if (m_pImportISOAction)
            m_pVerticalToolBar->addAction(m_pImportISOAction);
        if (m_pRemoveISOAction)
            m_pVerticalToolBar->addAction(m_pRemoveISOAction);

        m_pVerticalToolBar->addWidget(bottomSpacerWidget);
    }
}

void UIVisoCreatorWidget::toggleSettingsWidget(bool fShown)
{
    AssertReturnVoid(m_pSettingsWidget);
    m_pSettingsWidget->setVisible(fShown);
    m_fShowSettingsDialog = fShown;

    if (fShown)
    {
        m_pSettingsWidget->blockSignals(true);
        m_pSettingsWidget->setSettings(m_settings);
        m_pSettingsWidget->blockSignals(false);
    }

    emit sigSettingDialogToggle(fShown);
}

QStringList UIVisoCreatorWidget::findISOFiles(const QStringList &pathList) const
{
    QStringList isoList;
    foreach (const QString &strPath, pathList)
    {
        if (QFileInfo(strPath).suffix().compare("iso", Qt::CaseInsensitive) == 0)
            isoList << strPath;
    }
    return isoList;
}

void UIVisoCreatorWidget::prepareVerticalToolBar()
{
    m_pVerticalToolBar = new QIToolBar;
    AssertPtrReturnVoid(m_pVerticalToolBar);

    m_pVerticalToolBar->setOrientation(Qt::Vertical);
}

QString UIVisoCreatorWidget::visoFileFullPath() const
{
    return QString("%1/%2%3").arg(m_strVisoFilePath).arg(visoName()).arg(".viso");
}


/*********************************************************************************************************************************
*   UIVisoCreatorDialog implementation.                                                                                          *
*********************************************************************************************************************************/
UIVisoCreatorDialog::UIVisoCreatorDialog(UIActionPool *pActionPool, QWidget *pParent,
                                         const QString& strVisoFilePath, const QString& strMachineName /* = QString() */)
    : QIWithRestorableGeometry<QIMainDialog>(pParent)
    , m_pVisoCreatorWidget(0)
    , m_pButtonBox(0)
    , m_pActionPool(pActionPool)
    , m_iGeometrySaveTimerId(-1)
{
    /* Make sure that the base class does not close this dialog upon pressing escape.
       we manage escape key here with special casing: */
    setRejectByEscape(false);
    prepareWidgets(strVisoFilePath, strMachineName);
    loadSettings();
    setObjectName("VISO dialog");
}

QStringList  UIVisoCreatorDialog::entryList() const
{
    if (m_pVisoCreatorWidget)
        return m_pVisoCreatorWidget->entryList();
    return QStringList();
}

QString UIVisoCreatorDialog::visoName() const
{
    if (m_pVisoCreatorWidget)
        return m_pVisoCreatorWidget->visoName();
    return QString();
}

QString UIVisoCreatorDialog::importedISOPath() const
{
    if (m_pVisoCreatorWidget)
        return m_pVisoCreatorWidget->importedISOPath();
    return QString();
}

QStringList UIVisoCreatorDialog::customOptions() const
{
    if (m_pVisoCreatorWidget)
        return m_pVisoCreatorWidget->customOptions();
    return QStringList();
}

QString UIVisoCreatorDialog::currentPath() const
{
    if (m_pVisoCreatorWidget)
        return m_pVisoCreatorWidget->currentPath();
    return QString();
}

void    UIVisoCreatorDialog::setCurrentPath(const QString &strPath)
{
    if (m_pVisoCreatorWidget)
        m_pVisoCreatorWidget->setCurrentPath(strPath);
}

void UIVisoCreatorDialog::prepareWidgets(const QString& strVisoFilePath, const QString &strMachineName)
{
    QWidget *pCentralWidget = new QWidget;
    AssertPtrReturnVoid(pCentralWidget);
    setCentralWidget(pCentralWidget);
    QVBoxLayout *pMainLayout = new QVBoxLayout;
    AssertPtrReturnVoid(pMainLayout);
    pCentralWidget->setLayout(pMainLayout);


    m_pVisoCreatorWidget = new UIVisoCreatorWidget(m_pActionPool, this, true /* show toolbar */, strVisoFilePath, strMachineName);
    AssertPtrReturnVoid(m_pVisoCreatorWidget);
    if (m_pVisoCreatorWidget->menu())
    {
        menuBar()->addMenu(m_pVisoCreatorWidget->menu());
        pMainLayout->addWidget(m_pVisoCreatorWidget);
        connect(m_pVisoCreatorWidget, &UIVisoCreatorWidget::sigSetCancelButtonShortCut,
                this, &UIVisoCreatorDialog::sltSetCancelButtonShortCut);
        connect(m_pVisoCreatorWidget, &UIVisoCreatorWidget::sigVisoNameChanged,
                this, &UIVisoCreatorDialog::sltVisoNameChanged);
        connect(m_pVisoCreatorWidget, &UIVisoCreatorWidget::sigVisoFilePathChanged,
                this, &UIVisoCreatorDialog::sltVisoFilePathChanged);
        connect(m_pVisoCreatorWidget, &UIVisoCreatorWidget::sigSave,
                this, &UIVisoCreatorDialog::sltSave);
    }

    m_pButtonBox = new QIDialogButtonBox;
    AssertPtrReturnVoid(m_pButtonBox);
    m_pButtonBox->setDoNotPickDefaultButton(true);
    m_pButtonBox->setStandardButtons(QDialogButtonBox::Help | QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    m_pButtonBox->button(QDialogButtonBox::Cancel)->setShortcut(QKeySequence(Qt::Key_Escape));
    pMainLayout->addWidget(m_pButtonBox);

    connect(m_pButtonBox->button(QDialogButtonBox::Help), &QPushButton::pressed,
            m_pButtonBox, &QIDialogButtonBox::sltHandleHelpRequest);

    connect(m_pButtonBox, &QIDialogButtonBox::rejected, this, &UIVisoCreatorDialog::close);
    connect(m_pButtonBox, &QIDialogButtonBox::accepted, this, &UIVisoCreatorDialog::accept);


    uiCommon().setHelpKeyword(m_pButtonBox->button(QDialogButtonBox::Help), "create-optical-disk-image");

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIVisoCreatorDialog::sltRetranslateUI);
}

void UIVisoCreatorDialog::sltRetranslateUI()
{
    updateWindowTitle();
    if (m_pButtonBox && m_pButtonBox->button(QDialogButtonBox::Ok))
    {
        m_pButtonBox->button(QDialogButtonBox::Ok)->setText(UIVisoCreatorWidget::tr("&Save and Close"));
        m_pButtonBox->button(QDialogButtonBox::Ok)->setToolTip(UIVisoCreatorWidget::tr("Creates VISO file with the selected content"));
    }
    if (m_pButtonBox && m_pButtonBox->button(QDialogButtonBox::Help))
    {
        m_pButtonBox->button(QDialogButtonBox::Help)->setToolTip(UIVisoCreatorWidget::tr("Opens the help browser and navigates to the related section"));
        m_pButtonBox->button(QDialogButtonBox::Help)->setShortcut(UIShortcutPool::standardSequence(QKeySequence::HelpContents));
    }
    updateWindowTitle();
}

bool UIVisoCreatorDialog::event(QEvent *pEvent)
{
    if (pEvent->type() == QEvent::Resize || pEvent->type() == QEvent::Move)
    {
        if (m_iGeometrySaveTimerId != -1)
            killTimer(m_iGeometrySaveTimerId);
        m_iGeometrySaveTimerId = startTimer(300);
    }
    else if (pEvent->type() == QEvent::Timer)
    {
        QTimerEvent *pTimerEvent = static_cast<QTimerEvent*>(pEvent);
        if (pTimerEvent->timerId() == m_iGeometrySaveTimerId)
        {
            killTimer(m_iGeometrySaveTimerId);
            m_iGeometrySaveTimerId = -1;
            saveDialogGeometry();
        }
    }
    return QIWithRestorableGeometry<QIMainDialog>::event(pEvent);
}

void UIVisoCreatorDialog::sltSetCancelButtonShortCut(QKeySequence keySequence)
{
    if (m_pButtonBox && m_pButtonBox->button(QDialogButtonBox::Cancel))
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setShortcut(keySequence);
}

void UIVisoCreatorDialog::sltVisoNameChanged(const QString &strName)
{
    Q_UNUSED(strName);
    updateWindowTitle();
}

void UIVisoCreatorDialog::sltVisoFilePathChanged(const QString &strPath)
{
    Q_UNUSED(strPath);
    updateWindowTitle();
}

void UIVisoCreatorDialog::sltSave()
{
    saveVISOFile();
}

void UIVisoCreatorDialog::loadSettings()
{
    const QRect availableGeo = gpDesktop->availableGeometry(this);
    int iDefaultWidth = availableGeo.width() / 2;
    int iDefaultHeight = availableGeo.height() * 3 / 4;
    QRect defaultGeo(0, 0, iDefaultWidth, iDefaultHeight);

    QWidget *pParent = windowManager().realParentWindow(parentWidget() ? parentWidget() : windowManager().mainWindowShown());
    /* Load geometry from extradata: */
    const QRect geo = gEDataManager->visoCreatorDialogGeometry(this, pParent, defaultGeo);
    LogRel2(("GUI: UISoftKeyboard: Restoring geometry to: Origin=%dx%d, Size=%dx%d\n",
             geo.x(), geo.y(), geo.width(), geo.height()));

    restoreGeometry(geo);
}

void UIVisoCreatorDialog::saveDialogGeometry()
{
    const QRect geo = currentGeometry();
    LogRel2(("GUI: UIMediumSelector: Saving geometry as: Origin=%dx%d, Size=%dx%d\n",
             geo.x(), geo.y(), geo.width(), geo.height()));
    gEDataManager->setVisoCreatorDialogGeometry(geo, isCurrentlyMaximized());
}

void UIVisoCreatorDialog::updateWindowTitle()
{
    setWindowTitle(QString("%1 - %2").arg(UIVisoCreatorWidget::tr("VISO Creator")).arg(visoFileFullPath()));
}

QString UIVisoCreatorDialog::visoFileFullPath() const
{
    if (!m_pVisoCreatorWidget)
        return QString();
    return m_pVisoCreatorWidget->visoFileFullPath();
}

/* static */
QUuid UIVisoCreatorDialog::createViso(UIActionPool *pActionPool, QWidget *pParent,
                                      const QString &strDefaultFolder /* = QString() */,
                                      const QString &strMachineName /* = QString() */)
{
    QString strVisoSaveFolder(strDefaultFolder);
    if (strVisoSaveFolder.isEmpty())
        strVisoSaveFolder = UIMediumTools::defaultFolderPathForType(UIMediumDeviceType_DVD);

    QWidget *pDialogParent = windowManager().realParentWindow(pParent);
    UIVisoCreatorDialog *pVisoCreator = new UIVisoCreatorDialog(pActionPool, pDialogParent,
                                                                strVisoSaveFolder, strMachineName);
    AssertPtrReturn(pVisoCreator, QUuid());

    windowManager().registerNewParent(pVisoCreator, pDialogParent);
    pVisoCreator->setCurrentPath(gEDataManager->visoCreatorRecentFolder());
    QUuid mediumId;
    if (pVisoCreator->exec(false /* not application modal */))
    {
        if (pVisoCreator->saveVISOFile())
        {
            QString strFilePath = pVisoCreator->visoFileFullPath();
            gEDataManager->setVISOCreatorRecentFolder(pVisoCreator->currentPath());
            mediumId = UIMediumTools::openMedium(UIMediumDeviceType_DVD, strFilePath);
        }
    }

    delete pVisoCreator;
    return mediumId;
}

bool UIVisoCreatorDialog::saveVISOFile()
{
    QStringList VisoEntryList = entryList();
    QString strImportedISOPath = importedISOPath();
    if ((VisoEntryList.empty() || VisoEntryList[0].isEmpty()) && strImportedISOPath.isEmpty())
        return false;

    QFile file(visoFileFullPath());
    if (file.open(QFile::WriteOnly | QFile::Truncate))
    {
        QString strVisoName = visoName();

        QTextStream stream(&file);
        stream << QString("%1 %2").arg("--iprt-iso-maker-file-marker-bourne-sh").arg(QUuid::createUuid().toString()) << "\n";
        stream<< "--volume-id=" << UIVisoContentBrowser::sanitizePath(strVisoName) << "\n";
        if (!strImportedISOPath.isEmpty())
            stream << "--import-iso=" << strImportedISOPath << "\n";
        stream << VisoEntryList.join("\n");
        if (!customOptions().isEmpty())
        {
            stream << "\n";
            stream << customOptions().join("\n");
        }
        file.close();
    }
    return true;
}

#include "UIVisoCreator.moc"
