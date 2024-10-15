/* $Id: UIExtensionPackManager.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIExtensionPackManager class implementation.
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
#include <QDir>
#include <QHeaderView>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIFileDialog.h"
#include "QIToolBar.h"
#include "QITreeWidget.h"
#include "UIActionPoolManager.h"
#include "UICommon.h"
#include "UIExtension.h"
#include "UIExtensionPackManager.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UINotificationCenter.h"
#include "UIShortcutPool.h"
#include "UITranslationEventListener.h"
#include "UIVirtualBoxEventHandler.h"

/* COM includes: */
#include "CExtPack.h"
#include "CExtPackManager.h"


/** Extension pack tree-widget column indexes. */
enum ExtensionPackColumn
{
    ExtensionPackColumn_Usable,
    ExtensionPackColumn_Name,
    ExtensionPackColumn_Version,
    ExtensionPackColumn_Max,
};


/** Extension Pack Manager: Extension Pack data structure. */
struct UIDataExtensionPack
{
    /** Constructs data. */
    UIDataExtensionPack()
        : m_strName(QString())
        , m_strDescription(QString())
        , m_strVersion(QString())
        , m_uRevision(0)
        , m_fIsUsable(false)
        , m_strWhyUnusable(QString())
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataExtensionPack &other) const
    {
        return true
               && (m_strName == other.m_strName)
               && (m_strDescription == other.m_strDescription)
               && (m_strVersion == other.m_strVersion)
               && (m_uRevision == other.m_uRevision)
               && (m_fIsUsable == other.m_fIsUsable)
               && (m_strWhyUnusable == other.m_strWhyUnusable)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataExtensionPack &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataExtensionPack &other) const { return !equal(other); }

    /** Holds the extension item name. */
    QString  m_strName;
    /** Holds the extension item description. */
    QString  m_strDescription;
    /** Holds the extension item version. */
    QString  m_strVersion;
    /** Holds the extension item revision. */
    ULONG    m_uRevision;
    /** Holds whether the extension item usable. */
    bool     m_fIsUsable;
    /** Holds why the extension item is unusable. */
    QString  m_strWhyUnusable;
};


/** Extension Pack Manager tree-widget item. */
class UIItemExtensionPack : public QITreeWidgetItem, public UIDataExtensionPack
{
    Q_OBJECT;

public:

    /** Updates item fields from base-class data. */
    void updateFields();

    /** Returns item name. */
    QString name() const { return m_strName; }

protected:

    /** Returns default text. */
    virtual QString defaultText() const RT_OVERRIDE;
};


/*********************************************************************************************************************************
*   Class UIItemExtensionPack implementation.                                                                                    *
*********************************************************************************************************************************/

void UIItemExtensionPack::updateFields()
{
    /* Icon: */
    setIcon(ExtensionPackColumn_Usable, UIIconPool::iconSet(  m_fIsUsable
                                                      ? ":/status_check_16px.png"
                                                      : ":/status_error_16px.png"));

    /* Name: */
    setText(ExtensionPackColumn_Name, m_strName);

    /* Version, Revision, Edition: */
    const QString strVersion(m_strVersion.section(QRegularExpression("[-_]"), 0, 0));
    // WORKAROUND:
    // for http://qt.gitorious.org/qt/qt/commit/7fc63dd0ff368a637dcd17e692b9d6b26278b538
    QString strAppend;
    if (m_strVersion.contains(QRegularExpression("[-_]")))
        strAppend = m_strVersion.section(QRegularExpression("[-_]"), 1, -1, QString::SectionIncludeLeadingSep);
    setText(ExtensionPackColumn_Version, QString("%1r%2%3").arg(strVersion).arg(m_uRevision).arg(strAppend));

    /* Tool-tip: */
    QString strTip = m_strDescription;
    if (!m_fIsUsable)
    {
        strTip += QString("<hr>");
        strTip += m_strWhyUnusable;
    }
    setToolTip(ExtensionPackColumn_Usable, strTip);
    setToolTip(ExtensionPackColumn_Name, strTip);
    setToolTip(ExtensionPackColumn_Version, strTip);
}

QString UIItemExtensionPack::defaultText() const
{
    return   m_fIsUsable
           ? QString("%1, %2: %3, %4")
               .arg(text(1))
               .arg(parentTree()->headerItem()->text(2)).arg(text(2))
               .arg(parentTree()->headerItem()->text(0))
           : QString("%1, %2: %3")
               .arg(text(1))
               .arg(parentTree()->headerItem()->text(2)).arg(text(2));
}


/*********************************************************************************************************************************
*   Class UIExtensionPackManagerWidget implementation.                                                                           *
*********************************************************************************************************************************/

UIExtensionPackManagerWidget::UIExtensionPackManagerWidget(EmbedTo enmEmbedding, UIActionPool *pActionPool,
                                               bool fShowToolbar /* = true */, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_enmEmbedding(enmEmbedding)
    , m_pActionPool(pActionPool)
    , m_fShowToolbar(fShowToolbar)
    , m_pToolBar(0)
    , m_pTreeWidget(0)
{
    prepare();
}

QMenu *UIExtensionPackManagerWidget::menu() const
{
    return m_pActionPool->action(UIActionIndexMN_M_ExtensionWindow)->menu();
}

void UIExtensionPackManagerWidget::sltRetranslateUI()
{
    /* Translate tree-widget: */
    m_pTreeWidget->setHeaderLabels(   QStringList()
                                   << UIExtensionPackManager::tr("Active", "ext pack")
                                   << UIExtensionPackManager::tr("Name")
                                   << UIExtensionPackManager::tr("Version"));
    m_pTreeWidget->setWhatsThis(UIExtensionPackManager::tr("Registered extension packs"));
}

void UIExtensionPackManagerWidget::sltInstallExtensionPack()
{
    /* Show file-open dialog to let user to choose package file.
     * The default location is the user's Download or Downloads directory
     * with the user's home directory as a fallback. ExtPacks are downloaded. */
    QString strBaseFolder = QDir::homePath() + "/Downloads";
    if (!QDir(strBaseFolder).exists())
    {
        strBaseFolder = QDir::homePath() + "/Download";
        if (!QDir(strBaseFolder).exists())
            strBaseFolder = QDir::homePath();
    }
    const QString strTitle = UIExtensionPackManager::tr("Select an extension package file");
    QStringList extensions;
    for (int i = 0; i < VBoxExtPackFileExts.size(); ++i)
        extensions << QString("*.%1").arg(VBoxExtPackFileExts[i]);
    const QString strFilter = UIExtensionPackManager::tr("Extension package files (%1)").arg(extensions.join(" "));
    const QStringList fileNames = QIFileDialog::getOpenFileNames(strBaseFolder, strFilter, window(), strTitle, 0, true, true);
    QString strFilePath;
    if (!fileNames.isEmpty())
        strFilePath = fileNames.at(0);

    /* Install the chosen package: */
    if (!strFilePath.isEmpty())
        UIExtension::install(strFilePath, QString(), this, NULL);
}

void UIExtensionPackManagerWidget::sltUninstallExtensionPack()
{
    /* Get current item: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->currentItem());
    UIItemExtensionPack *pItemEP = qobject_cast<UIItemExtensionPack*>(pItem);

    /* Uninstall chosen package: */
    if (pItemEP)
    {
        /* Get name of current package: */
        const QString strSelectedPackageName = pItemEP->name();
        /* Ask user about package removing: */
        if (msgCenter().confirmRemoveExtensionPack(strSelectedPackageName, this))
        {
            /* Get VirtualBox for further activities: */
            const CVirtualBox comVBox = gpGlobalSession->virtualBox();
            /* Get Extension Pack Manager for further activities: */
            CExtPackManager comEPManager = comVBox.GetExtensionPackManager();

            /* Show error message if necessary: */
            if (!comVBox.isOk())
                UINotificationMessage::cannotGetExtensionPackManager(comVBox);
            else
            {
                /* Uninstall the package: */
                /** @todo Refuse this if any VMs are running. */
                QString displayInfo;
#ifdef VBOX_WS_WIN
                QTextStream stream(&displayInfo);
                stream.setNumberFlags(QTextStream::ShowBase);
                stream.setIntegerBase(16);
                stream << "hwnd=" << winId();
#endif

                /* Uninstall extension pack: */
                UINotificationProgressExtensionPackUninstall *pNotification =
                        new UINotificationProgressExtensionPackUninstall(comEPManager,
                                                                         strSelectedPackageName,
                                                                         displayInfo);
                gpNotificationCenter->append(pNotification);
            }
        }
    }
}

void UIExtensionPackManagerWidget::sltAdjustTreeWidget()
{
    /* Get the tree-widget abstract interface: */
    QAbstractItemView *pItemView = m_pTreeWidget;
    /* Get the tree-widget header-view: */
    QHeaderView *pItemHeader = m_pTreeWidget->header();

    /* Calculate the total tree-widget width: */
    const int iTotal = m_pTreeWidget->viewport()->width();
    /* Look for a minimum width hints for non-important columns: */
    const int iMinWidth1 = qMax(pItemView->sizeHintForColumn(ExtensionPackColumn_Usable),
                                pItemHeader->sectionSizeHint(ExtensionPackColumn_Usable));
    const int iMinWidth2 = qMax(pItemView->sizeHintForColumn(ExtensionPackColumn_Version),
                                pItemHeader->sectionSizeHint(ExtensionPackColumn_Version));
    /* Propose suitable width hints for non-important columns: */
    const int iWidth1 = iMinWidth1 < iTotal / ExtensionPackColumn_Max ? iMinWidth1 : iTotal / ExtensionPackColumn_Max;
    const int iWidth2 = iMinWidth2 < iTotal / ExtensionPackColumn_Max ? iMinWidth2 : iTotal / ExtensionPackColumn_Max;
    /* Apply the proposal: */
    m_pTreeWidget->setColumnWidth(ExtensionPackColumn_Usable, iWidth1);
    m_pTreeWidget->setColumnWidth(ExtensionPackColumn_Version, iWidth2);
    m_pTreeWidget->setColumnWidth(ExtensionPackColumn_Name, iTotal - iWidth1 - iWidth2);
}

void UIExtensionPackManagerWidget::sltHandleCurrentItemChange()
{
    /* Check current-item type: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->currentItem());

    /* Update actions availability: */
    m_pActionPool->action(UIActionIndexMN_M_Extension_S_Uninstall)->setEnabled(pItem);
}

void UIExtensionPackManagerWidget::sltHandleContextMenuRequest(const QPoint &position)
{
    /* Check clicked-item type: */
    QITreeWidgetItem *pItem = QITreeWidgetItem::toItem(m_pTreeWidget->itemAt(position));

    /* Compose temporary context-menu: */
    QMenu menu;
    if (pItem)
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Extension_S_Uninstall));
    else
        menu.addAction(m_pActionPool->action(UIActionIndexMN_M_Extension_S_Install));

    /* And show it: */
    menu.exec(m_pTreeWidget->viewport()->mapToGlobal(position));
}

void UIExtensionPackManagerWidget::sltHandleExtensionPackInstalled(const QString &strName)
{
    /* Make sure the name was set: */
    if (strName.isNull())
        return;

    /* Look for a list of items matching strName: */
    const QList<QTreeWidgetItem*> items = m_pTreeWidget->findItems(strName, Qt::MatchCaseSensitive, ExtensionPackColumn_Name);
    /* Remove first found item from the list if present: */
    if (!items.isEmpty())
        delete items.first();

    /* [Re]insert it into the tree: */
    CExtPackManager comManager = gpGlobalSession->virtualBox().GetExtensionPackManager();
    const CExtPack comExtensionPack = comManager.Find(strName);
    if (comExtensionPack.isOk())
    {
        /* Load extension pack data: */
        UIDataExtensionPack extensionPackData;
        loadExtensionPack(comExtensionPack, extensionPackData);
        createItemForExtensionPack(extensionPackData, true /* choose item? */);
    }
}

void UIExtensionPackManagerWidget::sltHandleExtensionPackUninstalled(const QString &strName)
{
    /* Make sure the name was set: */
    if (strName.isNull())
        return;

    /* Look for a list of items matching strName: */
    const QList<QTreeWidgetItem*> items = m_pTreeWidget->findItems(strName, Qt::MatchCaseSensitive, ExtensionPackColumn_Name);
    AssertReturnVoid(!items.isEmpty());
    /* Remove first found item from the list: */
    delete items.first();

    /* Adjust tree-widget: */
    sltAdjustTreeWidget();
}

void UIExtensionPackManagerWidget::prepare()
{
    /* Prepare self: */
    uiCommon().setHelpKeyword(this, "ext-pack-manager");

    /* Prepare stuff: */
    prepareActions();
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIExtensionPackManagerWidget::sltRetranslateUI);

    /* Load extension packs: */
    loadExtensionPacks();
}

void UIExtensionPackManagerWidget::prepareActions()
{
    /* First of all, add actions which has smaller shortcut scope: */
    addAction(m_pActionPool->action(UIActionIndexMN_M_Extension_S_Install));
    addAction(m_pActionPool->action(UIActionIndexMN_M_Extension_S_Uninstall));

    /* Connect actions: */
    connect(m_pActionPool->action(UIActionIndexMN_M_Extension_S_Install), &QAction::triggered,
            this, &UIExtensionPackManagerWidget::sltInstallExtensionPack);
    connect(m_pActionPool->action(UIActionIndexMN_M_Extension_S_Uninstall), &QAction::triggered,
            this, &UIExtensionPackManagerWidget::sltUninstallExtensionPack);
}

void UIExtensionPackManagerWidget::prepareWidgets()
{
    /* Create main-layout: */
    new QVBoxLayout(this);
    if (layout())
    {
        /* Configure layout: */
        layout()->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
        layout()->setSpacing(10);
#else
        layout()->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) / 2);
#endif

        /* Prepare toolbar, if requested: */
        if (m_fShowToolbar)
            prepareToolBar();

        /* Prepare tree-widget: */
        prepareTreeWidget();
    }
}

void UIExtensionPackManagerWidget::prepareToolBar()
{
    /* Prepare toolbar: */
    m_pToolBar = new QIToolBar(parentWidget());
    if (m_pToolBar)
    {
        const int iIconMetric = (int)(QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize));
        m_pToolBar->setIconSize(QSize(iIconMetric, iIconMetric));
        m_pToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Extension_S_Install));
        m_pToolBar->addAction(m_pActionPool->action(UIActionIndexMN_M_Extension_S_Uninstall));

#ifdef VBOX_WS_MAC
        /* Check whether we are embedded into a stack: */
        if (m_enmEmbedding == EmbedTo_Stack)
        {
            /* Add into layout: */
            layout()->addWidget(m_pToolBar);
        }
#else
        /* Add into layout: */
        layout()->addWidget(m_pToolBar);
#endif
    }
}

void UIExtensionPackManagerWidget::prepareTreeWidget()
{
    /* Prepare tree-widget: */
    m_pTreeWidget = new QITreeWidget(this);
    if (m_pTreeWidget)
    {
        m_pTreeWidget->setRootIsDecorated(false);
        m_pTreeWidget->setAlternatingRowColors(true);
        m_pTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        m_pTreeWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_pTreeWidget->setColumnCount(ExtensionPackColumn_Max);
        m_pTreeWidget->setSortingEnabled(true);
        m_pTreeWidget->sortByColumn(ExtensionPackColumn_Name, Qt::AscendingOrder);
        m_pTreeWidget->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
        connect(m_pTreeWidget, &QITreeWidget::resized,
                this, &UIExtensionPackManagerWidget::sltAdjustTreeWidget, Qt::QueuedConnection);
        connect(m_pTreeWidget->header(), &QHeaderView::sectionResized,
                this, &UIExtensionPackManagerWidget::sltAdjustTreeWidget, Qt::QueuedConnection);
        connect(m_pTreeWidget, &QITreeWidget::currentItemChanged,
                this, &UIExtensionPackManagerWidget::sltHandleCurrentItemChange);
        connect(m_pTreeWidget, &QITreeWidget::customContextMenuRequested,
                this, &UIExtensionPackManagerWidget::sltHandleContextMenuRequest);

        /* Add into layout: */
        layout()->addWidget(m_pTreeWidget);
    }
}

void UIExtensionPackManagerWidget::prepareConnections()
{
    /* Listen for extension pack being [un]installed: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigExtensionPackInstalled,
            this, &UIExtensionPackManagerWidget::sltHandleExtensionPackInstalled);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigExtensionPackUninstalled,
            this, &UIExtensionPackManagerWidget::sltHandleExtensionPackUninstalled);
}

void UIExtensionPackManagerWidget::loadExtensionPacks()
{
    /* Check tree-widget: */
    if (!m_pTreeWidget)
        return;

    /* Clear tree first of all: */
    m_pTreeWidget->clear();

    /* Get VirtualBox for further activities: */
    const CVirtualBox comVBox = gpGlobalSession->virtualBox();
    /* Get Extension Pack Manager for further activities: */
    const CExtPackManager comEPManager = comVBox.GetExtensionPackManager();

    /* Show error message if necessary: */
    if (!comVBox.isOk())
        UINotificationMessage::cannotGetExtensionPackManager(comVBox);
    else
    {
        /* Get extension packs for further activities: */
        const QVector<CExtPack> extensionPacks = comEPManager.GetInstalledExtPacks();

        /* Show error message if necessary: */
        if (!comEPManager.isOk())
            UINotificationMessage::cannotAcquireExtensionPackManagerParameter(comEPManager);
        else
        {
            /* Iterate through existing extension packs: */
            foreach (const CExtPack &comExtensionPack, extensionPacks)
            {
                /* Skip if we have nothing to populate: */
                if (comExtensionPack.isNull())
                    continue;

                /* Load extension pack data: */
                UIDataExtensionPack extensionPackData;
                loadExtensionPack(comExtensionPack, extensionPackData);
                createItemForExtensionPack(extensionPackData, false /* choose item? */);
            }

            /* Choose the 1st item as current if nothing chosen: */
            if (!m_pTreeWidget->currentItem())
                m_pTreeWidget->setCurrentItem(m_pTreeWidget->topLevelItem(0));
            /* Handle current item change in any case: */
            sltHandleCurrentItemChange();
        }
    }
}

void UIExtensionPackManagerWidget::loadExtensionPack(const CExtPack &comExtensionPack, UIDataExtensionPack &extensionPackData)
{
    /* Gather extension pack settings: */
    if (comExtensionPack.isOk())
        extensionPackData.m_strName = comExtensionPack.GetName();
    if (comExtensionPack.isOk())
        extensionPackData.m_strDescription = comExtensionPack.GetDescription();
    if (comExtensionPack.isOk())
        extensionPackData.m_strVersion = comExtensionPack.GetVersion();
    if (comExtensionPack.isOk())
        extensionPackData.m_uRevision = comExtensionPack.GetRevision();
    if (comExtensionPack.isOk())
    {
        extensionPackData.m_fIsUsable = comExtensionPack.GetUsable();
        if (!extensionPackData.m_fIsUsable && comExtensionPack.isOk())
            extensionPackData.m_strWhyUnusable = comExtensionPack.GetWhyUnusable();
    }

    /* Show error message if necessary: */
    if (!comExtensionPack.isOk())
        UINotificationMessage::cannotAcquireExtensionPackParameter(comExtensionPack);
}

void UIExtensionPackManagerWidget::createItemForExtensionPack(const UIDataExtensionPack &extensionPackData, bool fChooseItem)
{
    /* Prepare new provider item: */
    UIItemExtensionPack *pItem = new UIItemExtensionPack;
    if (pItem)
    {
        pItem->UIDataExtensionPack::operator=(extensionPackData);
        pItem->updateFields();

        /* Add item to the tree: */
        m_pTreeWidget->addTopLevelItem(pItem);

        /* And choose it as current if necessary: */
        if (fChooseItem)
            m_pTreeWidget->setCurrentItem(pItem);
    }
}


/*********************************************************************************************************************************
*   Class UIExtensionPackManagerFactory implementation.                                                                          *
*********************************************************************************************************************************/

UIExtensionPackManagerFactory::UIExtensionPackManagerFactory(UIActionPool *pActionPool /* = 0 */)
    : m_pActionPool(pActionPool)
{
}

void UIExtensionPackManagerFactory::create(QIManagerDialog *&pDialog, QWidget *pCenterWidget)
{
    pDialog = new UIExtensionPackManager(pCenterWidget, m_pActionPool);
}


/*********************************************************************************************************************************
*   Class UIExtensionPackManager implementation.                                                                                 *
*********************************************************************************************************************************/

UIExtensionPackManager::UIExtensionPackManager(QWidget *pCenterWidget, UIActionPool *pActionPool)
    : QIManagerDialog(pCenterWidget)
    , m_pActionPool(pActionPool)
{
}

void UIExtensionPackManager::sltRetranslateUI()
{
    /* Translate window title: */
    setWindowTitle(tr("Extension Pack Manager"));

    /* Translate buttons: */
    button(ButtonType_Close)->setText(tr("Close"));
    button(ButtonType_Help)->setText(tr("Help"));
    button(ButtonType_Close)->setStatusTip(tr("Close dialog"));
    button(ButtonType_Help)->setStatusTip(tr("Show dialog help"));
    button(ButtonType_Close)->setShortcut(Qt::Key_Escape);
    button(ButtonType_Help)->setShortcut(UIShortcutPool::standardSequence(QKeySequence::HelpContents));
    button(ButtonType_Close)->setToolTip(tr("Close Window (%1)").arg(button(ButtonType_Close)->shortcut().toString()));
    button(ButtonType_Help)->setToolTip(tr("Show Help (%1)").arg(button(ButtonType_Help)->shortcut().toString()));
}

void UIExtensionPackManager::configure()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/extension_pack_manager_24px.png", ":/extension_pack_manager_16px.png"));
#endif
}

void UIExtensionPackManager::configureCentralWidget()
{
    /* Prepare widget: */
    UIExtensionPackManagerWidget *pWidget = new UIExtensionPackManagerWidget(EmbedTo_Dialog, m_pActionPool, true, this);
    if (pWidget)
    {
        setWidget(pWidget);
        setWidgetMenu(pWidget->menu());
#ifdef VBOX_WS_MAC
        setWidgetToolbar(pWidget->toolbar());
#endif

        /* Add into layout: */
        centralWidget()->layout()->addWidget(pWidget);
    }
}

void UIExtensionPackManager::finalize()
{
    /* Apply language settings: */
    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIExtensionPackManager::sltRetranslateUI);
}

UIExtensionPackManagerWidget *UIExtensionPackManager::widget()
{
    return qobject_cast<UIExtensionPackManagerWidget*>(QIManagerDialog::widget());
}


#include "UIExtensionPackManager.moc"
