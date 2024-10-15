/* $Id: UIExtraDataManager.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIExtraDataManager class implementation.
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
#include <QFontDatabase>
#include <QMetaEnum>
#include <QMutex>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
# include <QComboBox>
# include <QHeaderView>
# include <QLabel>
# include <QLineEdit>
# include <QListView>
# include <QMainWindow>
# include <QMenuBar>
# include <QPainter>
# include <QPushButton>
# include <QSortFilterProxyModel>
# include <QStyledItemDelegate>
# include <QTableView>
# include <QVBoxLayout>
# include <QStandardItemModel>
# include <QXmlStreamWriter>
# include <QXmlStreamReader>
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */

/* GUI includes: */
#include "UICommon.h"
#include "UIConverter.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIHostComboEditor.h"
#include "UILocalMachineStuff.h"
#include "UILoggingDefs.h"
#include "UIMainEventListener.h"
#include "UIMessageCenter.h"
#include "UISettingsDefs.h"
#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
# include "QIDialog.h"
# include "QIDialogButtonBox.h"
# include "QIFileDialog.h"
# include "QISplitter.h"
# include "QIWidgetValidator.h"
# include "QIWithRestorableGeometry.h"
# include "VBoxUtils.h"
# include "UIIconPool.h"
# include "QIToolBar.h"
# include "UIVirtualBoxEventHandler.h"
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */

/* COM includes: */
#include "CEventListener.h"
#include "CEventSource.h"
#include "CMachine.h"
#include "CVirtualBox.h"


/* Namespaces: */
using namespace UIExtraDataDefs;
using namespace UISettingsDefs;


/** Private QObject extension
  * providing UIExtraDataManager with the CVirtualBox event-source. */
class UIExtraDataEventHandler : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies about 'extra-data change' event: */
    void sigExtraDataChange(const QUuid &uMachineID, const QString &strKey, const QString &strValue);

public:

    /** Constructs event proxy object on the basis of passed @a pParent. */
    UIExtraDataEventHandler(QObject *pParent);
    /** Destructs event proxy object. */
    ~UIExtraDataEventHandler();

protected slots:

    /** Preprocess 'extra-data can change' event: */
    void sltPreprocessExtraDataCanChange(const QUuid &uMachineID, const QString &strKey, const QString &strValue, bool &fVeto, QString &strVetoReason);
    /** Preprocess 'extra-data change' event: */
    void sltPreprocessExtraDataChange(const QUuid &uMachineID, const QString &strKey, const QString &strValue);

protected:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares listener. */
        void prepareListener();
        /** Prepares connections. */
        void prepareConnections();

        /** Cleanups connections. */
        void cleanupConnections();
        /** Cleanups listener. */
        void cleanupListener();
        /** Cleanups all. */
        void cleanup();
    /** @} */

private:

    /** Holds the Qt event listener instance. */
    ComObjPtr<UIMainEventListenerImpl> m_pQtListener;
    /** Holds the COM event listener instance. */
    CEventListener m_comEventListener;

    /** Protects sltPreprocessExtraDataChange. */
    QMutex m_mutex;
};


/*********************************************************************************************************************************
*   Class UIExtraDataEventHandler implementation.                                                                                *
*********************************************************************************************************************************/

UIExtraDataEventHandler::UIExtraDataEventHandler(QObject *pParent)
    : QObject(pParent)
{
    /* Prepare: */
    prepare();
}

UIExtraDataEventHandler::~UIExtraDataEventHandler()
{
    /* Cleanup: */
    cleanup();
}

void UIExtraDataEventHandler::prepare()
{
    /* Prepare: */
    prepareListener();
    prepareConnections();
}

void UIExtraDataEventHandler::prepareListener()
{
    /* Create event listener instance: */
    m_pQtListener.createObject();
    m_pQtListener->init(new UIMainEventListener, this);
    m_comEventListener = CEventListener(m_pQtListener);

    /* Get VirtualBox: */
    const CVirtualBox comVBox = gpGlobalSession->virtualBox();
    /* Get VirtualBox event source: */
    CEventSource comEventSourceVBox = comVBox.GetEventSource();
    Assert(comVBox.isOk());

    /* Enumerate all the required event-types: */
    QVector<KVBoxEventType> eventTypes;
    eventTypes
        << KVBoxEventType_OnExtraDataCanChange
        << KVBoxEventType_OnExtraDataChanged;

    /* Register event listener for VirtualBox event source: */
    comEventSourceVBox.RegisterListener(m_comEventListener, eventTypes, FALSE /* active? */);
    Assert(comEventSourceVBox.isOk());

    /* Register event sources in their listeners as well: */
    m_pQtListener->getWrapped()->registerSource(comEventSourceVBox, m_comEventListener);
}

void UIExtraDataEventHandler::prepareConnections()
{
    /* Create direct (sync) connections for signals of main listener: */
    connect(m_pQtListener->getWrapped(), &UIMainEventListener::sigExtraDataCanChange,
            this, &UIExtraDataEventHandler::sltPreprocessExtraDataCanChange,
            Qt::DirectConnection);
    connect(m_pQtListener->getWrapped(), &UIMainEventListener::sigExtraDataChange,
            this, &UIExtraDataEventHandler::sltPreprocessExtraDataChange,
            Qt::DirectConnection);
}

void UIExtraDataEventHandler::cleanupConnections()
{
    /* Nothing for now. */
}

void UIExtraDataEventHandler::cleanupListener()
{
    /* Unregister everything: */
    m_pQtListener->getWrapped()->unregisterSources();

    /* Make sure VBoxSVC is available: */
    if (!gpGlobalSession->isVBoxSVCAvailable())
        return;

    /* Get VirtualBox: */
    const CVirtualBox comVBox = gpGlobalSession->virtualBox();
    /* Get VirtualBox event source: */
    CEventSource comEventSourceVBox = comVBox.GetEventSource();
    Assert(comVBox.isOk());

    /* Unregister event listener for VirtualBox event source: */
    comEventSourceVBox.UnregisterListener(m_comEventListener);
}

void UIExtraDataEventHandler::cleanup()
{
    /* Cleanup: */
    cleanupConnections();
    cleanupListener();
}

void UIExtraDataEventHandler::sltPreprocessExtraDataCanChange(const QUuid &uMachineID, const QString &strKey, const QString &/* strValue */, bool & /* fVeto */, QString & /* strVetoReason */)
{
    /* Preprocess global 'extra-data can change' event: */
    if (uMachineID.isNull())
    {
        if (strKey.startsWith("GUI/"))
        {
            /* Check whether global extra-data property can be applied: */
            /// @todo Here can be various extra-data flags handling.
            //       Generally we should check whether one or another flag feats some rule (like reg-exp).
            //       For each required strValue we should set fVeto = true; and fill strVetoReason = "with some text".
        }
    }
}

void UIExtraDataEventHandler::sltPreprocessExtraDataChange(const QUuid &uMachineID, const QString &strKey, const QString &strValue)
{
    /* Preprocess global 'extra-data change' event: */
    if (uMachineID.isNull())
    {
        if (strKey.startsWith("GUI/"))
        {
            /* Apply global extra-data property: */
            /// @todo Here can be various extra-data flags handling.
            //       Generally we should push one or another flag to various instances which want to handle
            //       those flags independently from UIExtraDataManager. Remember to process each required strValue
            //       from under the m_mutex lock (since we are in another thread) and unlock that m_mutex afterwards.
        }
    }

    /* Motify listener about 'extra-data change' event: */
    emit sigExtraDataChange(uMachineID, strKey, strValue);
}


#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI

/** Data fields. */
enum Field
{
    Field_ID = Qt::UserRole + 1,
    Field_Name,
    Field_OsTypeID,
    Field_Known
};


/** QStyledItemDelegate extension
  * reflecting items of Extra Data Manager window: Chooser pane. */
class UIChooserPaneDelegate : public QStyledItemDelegate
{
    Q_OBJECT;

public:

    /** Constructor. */
    UIChooserPaneDelegate(QObject *pParent);

private:

    /** Size-hint calculation routine. */
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const  RT_OVERRIDE RT_FINAL;

    /** Paint routine. */
    void paint(QPainter *pPainter, const QStyleOptionViewItem &option, const QModelIndex &index) const RT_OVERRIDE RT_FINAL;

    /** Fetch pixmap info for passed QModelIndex. */
    static void fetchPixmapInfo(const QModelIndex &index, QPixmap &pixmap, QSize &pixmapSize);

    /** Margin. */
    int m_iMargin;
    /** Spacing. */
    int m_iSpacing;
};


/*********************************************************************************************************************************
*   Class UIChooserPaneDelegate implementation.                                                                                  *
*********************************************************************************************************************************/

UIChooserPaneDelegate::UIChooserPaneDelegate(QObject *pParent)
    : QStyledItemDelegate(pParent)
    , m_iMargin(3)
    , m_iSpacing(3)
{
}

QSize UIChooserPaneDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    /* Font metrics: */
    const QFontMetrics &fm = option.fontMetrics;
    /* Pixmap: */
    QPixmap pixmap;
    QSize pixmapSize;
    fetchPixmapInfo(index, pixmap, pixmapSize);

    /* Calculate width: */
    const int iWidth = m_iMargin
                     + pixmapSize.width()
                     + 2 * m_iSpacing
                     + qMax(fm.horizontalAdvance(index.data(Field_Name).toString()),
                            fm.horizontalAdvance(index.data(Field_ID).toString()))
                     + m_iMargin;
    /* Calculate height: */
    const int iHeight = m_iMargin
                      + qMax(pixmapSize.height(),
                             fm.height() + m_iSpacing + fm.height())
                      + m_iMargin;

    /* Return result: */
    return QSize(iWidth, iHeight);
}

void UIChooserPaneDelegate::paint(QPainter *pPainter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    /* Item rect: */
    const QRect &optionRect = option.rect;
    /* Palette: */
    const QPalette &palette = option.palette;
    /* Font metrics: */
    const QFontMetrics &fm = option.fontMetrics;
    /* Pixmap: */
    QPixmap pixmap;
    QSize pixmapSize;
    fetchPixmapInfo(index, pixmap, pixmapSize);

    /* If item selected: */
    if (option.state & QStyle::State_Selected)
    {
        /* Fill background with selection color: */
        QColor highlight = palette.color(option.state & QStyle::State_Active ?
                                         QPalette::Active : QPalette::Inactive,
                                         QPalette::Highlight);
        QLinearGradient bgGrad(optionRect.topLeft(), optionRect.bottomLeft());
        bgGrad.setColorAt(0, highlight.lighter(120));
        bgGrad.setColorAt(1, highlight);
        pPainter->fillRect(optionRect, bgGrad);
        /* Draw focus frame: */
        QStyleOptionFocusRect focusOption;
        focusOption.rect = optionRect;
        QApplication::style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focusOption, pPainter);
    }

    /* Draw pixmap: */
    const QPoint pixmapOrigin = optionRect.topLeft() +
                                QPoint(m_iMargin, m_iMargin);
    pPainter->drawPixmap(pixmapOrigin, pixmap);

    /* Is that known item? */
    bool fKnown = index.data(Field_Known).toBool();
    if (fKnown)
    {
        pPainter->save();
        QFont font = pPainter->font();
        font.setBold(true);
        pPainter->setFont(font);
    }

    /* Draw item name: */
    const QPoint nameOrigin = pixmapOrigin +
                              QPoint(pixmapSize.width(), 0) +
                              QPoint(2 * m_iSpacing, 0) +
                              QPoint(0, fm.ascent());
    pPainter->drawText(nameOrigin, index.data(Field_Name).toString());

    /* Was that known item? */
    if (fKnown)
        pPainter->restore();

    /* Draw item ID: */
    const QPoint idOrigin = nameOrigin +
                            QPoint(0, m_iSpacing) +
                            QPoint(0, fm.height());
    pPainter->drawText(idOrigin, index.data(Field_ID).toString());
}

/* static */
void UIChooserPaneDelegate::fetchPixmapInfo(const QModelIndex &index, QPixmap &pixmap, QSize &pixmapSize)
{
    /* If proper machine ID passed => return corresponding pixmap/size: */
    if (index.data(Field_ID).toUuid() != UIExtraDataManager::GlobalID)
        pixmap = generalIconPool().guestOSTypePixmapDefault(index.data(Field_OsTypeID).toString(), &pixmapSize);
    else
    {
        /* For global ID we return static pixmap/size: */
        const QIcon icon = UIIconPool::iconSet(":/edata_global_32px.png");
        pixmapSize = icon.availableSizes().value(0, QSize(32, 32));
        pixmap = icon.pixmap(pixmapSize);
    }
}


/** QSortFilterProxyModel extension
  * used by the chooser-pane of the UIExtraDataManagerWindow. */
class UIChooserPaneSortingModel : public QSortFilterProxyModel
{
    Q_OBJECT;

public:

    /** Constructor, passes @a pParent to the QIRichToolButton constructor. */
    UIChooserPaneSortingModel(QObject *pParent) : QSortFilterProxyModel(pParent) {}

protected:

    /** Returns true if the value of the item referred to by the given index left
      * is less than the value of the item referred to by the given index right,
      * otherwise returns false. */
    bool lessThan(const QModelIndex &leftIdx, const QModelIndex &rightIdx) const RT_OVERRIDE RT_FINAL
    {
        /* Compare by ID first: */
        const QUuid strID1 = leftIdx.data(Field_ID).toUuid();
        const QUuid strID2 = rightIdx.data(Field_ID).toUuid();
        if (strID1 == UIExtraDataManager::GlobalID)
            return true;
        else if (strID2 == UIExtraDataManager::GlobalID)
            return false;
        /* Compare role finally: */
        return QSortFilterProxyModel::lessThan(leftIdx, rightIdx);
    }
};


/** QMainWindow extension
  * providing Extra Data Manager with UI features. */
class UIExtraDataManagerWindow : public QIWithRestorableGeometry<QMainWindow>
{
    Q_OBJECT;

public:

    /** @name Constructor/Destructor
      * @{ */
        /** Extra-data Manager Window constructor. */
        UIExtraDataManagerWindow(QWidget *pCenterWidget);
        /** Extra-data Manager Window destructor. */
        ~UIExtraDataManagerWindow();
    /** @} */

    /** @name Management
      * @{ */
        /** Show and raise. */
        void showAndRaise(QWidget *pCenterWidget);
    /** @} */

public slots:

    /** @name General
      * @{ */
        /** Handles extra-data map acknowledging. */
        void sltExtraDataMapAcknowledging(const QUuid &uID);
        /** Handles extra-data change. */
        void sltExtraDataChange(const QUuid &uID, const QString &strKey, const QString &strValue);
    /** @} */

private slots:

    /** @name General
      * @{ */
        /** Handles machine (un)registration. */
        void sltMachineRegistered(const QUuid &uID, bool fAdded);
    /** @} */

    /** @name Chooser-pane
      * @{ */
        /** Handles filter-apply signal for the chooser-pane. */
        void sltChooserApplyFilter(const QString &strFilter);
        /** Handles current-changed signal for the chooser-pane: */
        void sltChooserHandleCurrentChanged(const QModelIndex &index);
        /** Handles item-selection-changed signal for the chooser-pane: */
        void sltChooserHandleSelectionChanged(const QItemSelection &selected,
                                              const QItemSelection &deselected);
    /** @} */

    /** @name Data-pane
      * @{ */
        /** Handles filter-apply signal for the data-pane. */
        void sltDataApplyFilter(const QString &strFilter);
        /** Handles item-selection-changed signal for the data-pane: */
        void sltDataHandleSelectionChanged(const QItemSelection &selected,
                                           const QItemSelection &deselected);
        /** Handles item-changed signal for the data-pane: */
        void sltDataHandleItemChanged(QStandardItem *pItem);
        /** Handles context-menu-requested signal for the data-pane: */
        void sltDataHandleCustomContextMenuRequested(const QPoint &pos);
    /** @} */

    /** @name Actions
      * @{ */
        /** Add handler. */
        void sltAdd();
        /** Remove handler. */
        void sltDel();
        /** Save handler. */
        void sltSave();
        /** Load handler. */
        void sltLoad();
    /** @} */

private:

    /** @name General
      * @{ */
        /** Returns whether the window should be maximized when geometry being restored. */
        virtual bool shouldBeMaximized() const RT_OVERRIDE;
    /** @} */

    /** @name Prepare/Cleanup
      * @{ */
        /** Prepare instance. */
        void prepare();
        /** Prepare this. */
        void prepareThis();
        /** Prepare connections. */
        void prepareConnections();
        /** Prepare menu. */
        void prepareMenu();
        /** Prepare central widget. */
        void prepareCentralWidget();
        /** Prepare tool-bar. */
        void prepareToolBar();
        /** Prepare splitter. */
        void prepareSplitter();
        /** Prepare panes: */
        void preparePanes();
        /** Prepare chooser pane. */
        void preparePaneChooser();
        /** Prepare data pane. */
        void preparePaneData();
        /** Prepare button-box. */
        void prepareButtonBox();
        /** Load window settings. */
        void loadSettings();

        /** Save window settings. */
        void saveSettings();
        /** Cleanup instance. */
        void cleanup();
    /** @} */

    /** @name Actions
      * @{ */
        /** Updates action availability. */
        void updateActionsAvailability();
    /** @} */

    /** @name Chooser-pane
      * @{ */
        /** Returns chooser index for @a iRow. */
        QModelIndex chooserIndex(int iRow) const;
        /** Returns current chooser index. */
        QModelIndex currentChooserIndex() const;

        /** Returns chooser ID for @a iRow. */
        QUuid chooserID(int iRow) const;
        /** Returns current chooser ID. */
        QUuid currentChooserID() const;

        /** Returns chooser Name for @a iRow. */
        QString chooserName(int iRow) const;
        /** Returns current Name. */
        QString currentChooserName() const;

        /** Adds chooser item. */
        void addChooserItem(const QUuid &uID,
                            const QString &strName,
                            const QString &strOsTypeID,
                            const int iPosition = -1);
        /** Adds chooser item by machine. */
        void addChooserItemByMachine(const CMachine &machine,
                                     const int iPosition = -1);
        /** Adds chooser item by ID. */
        void addChooserItemByID(const QUuid &uID,
                                const int iPosition = -1);

        /** Make sure chooser have current-index if possible. */
        void makeSureChooserHaveCurrentIndexIfPossible();
    /** @} */

    /** @name Data-pane
      * @{ */
        /** Returns data index for @a iRow and @a iColumn. */
        QModelIndex dataIndex(int iRow, int iColumn) const;

        /** Returns data-key index for @a iRow. */
        QModelIndex dataKeyIndex(int iRow) const;

        /** Returns data-value index for @a iRow. */
        QModelIndex dataValueIndex(int iRow) const;

        /** Returns current data-key. */
        QString dataKey(int iRow) const;

        /** Returns current data-value. */
        QString dataValue(int iRow) const;

        /** Adds data item. */
        void addDataItem(const QString &strKey,
                         const QString &strValue,
                         const int iPosition = -1);

        /** Sorts data items. */
        void sortData();

        /** Returns the list of known extra-data keys. */
        static QStringList knownExtraDataKeys();
    /** @} */


    /** @name Arguments
      * @{ */
        /** Holds the center widget reference. */
        QWidget *m_pCenterWidget;
    /** @} */

    /** @name General
      * @{ */
        QVBoxLayout *m_pMainLayout;
        /** Data pane: Tool-bar. */
        QIToolBar *m_pToolBar;
        /** Splitter. */
        QISplitter *m_pSplitter;
    /** @} */

    /** @name Chooser-pane
      * @{ */
        /** Chooser pane. */
        QWidget *m_pPaneOfChooser;
        /** Chooser filter. */
        QLineEdit *m_pFilterOfChooser;
        /** Chooser pane: List-view. */
        QListView *m_pViewOfChooser;
        /** Chooser pane: Source-model. */
        QStandardItemModel *m_pModelSourceOfChooser;
        /** Chooser pane: Proxy-model. */
        UIChooserPaneSortingModel *m_pModelProxyOfChooser;
    /** @} */

    /** @name Data-pane
      * @{ */
        /** Data pane. */
        QWidget *m_pPaneOfData;
        /** Data filter. */
        QLineEdit *m_pFilterOfData;
        /** Data pane: Table-view. */
        QTableView *m_pViewOfData;
        /** Data pane: Item-model. */
        QStandardItemModel *m_pModelSourceOfData;
        /** Data pane: Proxy-model. */
        QSortFilterProxyModel *m_pModelProxyOfData;
    /** @} */

    /** @name Button Box
      * @{ */
        /** Dialog button-box. */
        QIDialogButtonBox *m_pButtonBox;
    /** @} */

    /** @name Actions
      * @{ */
        /** Add action. */
        QAction *m_pActionAdd;
        /** Del action. */
        QAction *m_pActionDel;
        /** Load action. */
        QAction *m_pActionLoad;
        /** Save action. */
        QAction *m_pActionSave;
    /** @} */
};


/*********************************************************************************************************************************
*   Class UIExtraDataManagerWindow implementation.                                                                               *
*********************************************************************************************************************************/

UIExtraDataManagerWindow::UIExtraDataManagerWindow(QWidget *pCenterWidget)
    : m_pCenterWidget(pCenterWidget)
    , m_pMainLayout(0), m_pToolBar(0), m_pSplitter(0)
    , m_pPaneOfChooser(0), m_pFilterOfChooser(0), m_pViewOfChooser(0)
    , m_pModelSourceOfChooser(0), m_pModelProxyOfChooser(0)
    , m_pPaneOfData(0), m_pFilterOfData(0), m_pViewOfData(0),
      m_pModelSourceOfData(0), m_pModelProxyOfData(0)
    , m_pButtonBox(0)
    , m_pActionAdd(0), m_pActionDel(0)
    , m_pActionLoad(0), m_pActionSave(0)
{
    /* Prepare: */
    prepare();
}

UIExtraDataManagerWindow::~UIExtraDataManagerWindow()
{
    /* Cleanup: */
    cleanup();
}

void UIExtraDataManagerWindow::showAndRaise(QWidget*)
{
    /* Show: */
    show();
    /* Restore from minimized state: */
    setWindowState(windowState() & ~Qt::WindowMinimized);
    /* Raise: */
    activateWindow();
//    /* Center according passed widget: */
//    gpDesktop->centerWidget(this, pCenterWidget, false);
}

void UIExtraDataManagerWindow::sltMachineRegistered(const QUuid &uID, bool fRegistered)
{
    /* Machine registered: */
    if (fRegistered)
    {
        /* Gather list of 'known IDs': */
        QList<QUuid> knownIDs;
        for (int iRow = 0; iRow < m_pModelSourceOfChooser->rowCount(); ++iRow)
            knownIDs.append(chooserID(iRow));

        /* Get machine items: */
        const CMachineVector machines = gpGlobalSession->virtualBox().GetMachines();
        /* Look for the proper place to insert new machine item: */
        QUuid uPositionID = UIExtraDataManager::GlobalID;
        foreach (const CMachine &machine, machines)
        {
            /* Get iterated machine ID: */
            const QUuid uIteratedID = machine.GetId();
            /* If 'iterated ID' equal to 'added ID' => break now: */
            if (uIteratedID == uID)
                break;
            /* If 'iterated ID' is 'known ID' => remember it: */
            if (knownIDs.contains(uIteratedID))
                uPositionID = uIteratedID;
        }

        /* Add new chooser item into source-model: */
        addChooserItemByID(uID, knownIDs.indexOf(uPositionID) + 1);
        /* And sort proxy-model: */
        m_pModelProxyOfChooser->sort(0, Qt::AscendingOrder);
        /* Make sure chooser have current-index if possible: */
        makeSureChooserHaveCurrentIndexIfPossible();
    }
    /* Machine unregistered: */
    else
    {
        /* Remove chooser item with 'removed ID' if it is among 'known IDs': */
        for (int iRow = 0; iRow < m_pModelSourceOfChooser->rowCount(); ++iRow)
            if (chooserID(iRow) == uID)
                m_pModelSourceOfChooser->removeRow(iRow);
    }
}

void UIExtraDataManagerWindow::sltExtraDataMapAcknowledging(const QUuid &uID)
{
    /* Update item with 'changed ID' if it is among 'known IDs': */
    for (int iRow = 0; iRow < m_pModelSourceOfChooser->rowCount(); ++iRow)
        if (chooserID(iRow) == uID)
            m_pModelSourceOfChooser->itemFromIndex(chooserIndex(iRow))->setData(true, Field_Known);
}

void UIExtraDataManagerWindow::sltExtraDataChange(const QUuid &uID, const QString &strKey, const QString &strValue)
{
    /* Skip unrelated IDs: */
    if (currentChooserID() != uID)
        return;

    /* List of 'known keys': */
    QStringList knownKeys;
    for (int iRow = 0; iRow < m_pModelSourceOfData->rowCount(); ++iRow)
        knownKeys << dataKey(iRow);

    /* Check if 'changed key' is 'known key': */
    int iPosition = knownKeys.indexOf(strKey);
    /* If that is 'known key': */
    if (iPosition != -1)
    {
        /* If 'changed value' is empty => REMOVE item: */
        if (strValue.isEmpty())
            m_pModelSourceOfData->removeRow(iPosition);
        /* If 'changed value' is NOT empty => UPDATE item: */
        else
        {
            m_pModelSourceOfData->itemFromIndex(dataKeyIndex(iPosition))->setData(strKey, Qt::UserRole);
            m_pModelSourceOfData->itemFromIndex(dataValueIndex(iPosition))->setText(strValue);
        }
    }
    /* Else if 'changed value' is NOT empty: */
    else if (!strValue.isEmpty())
    {
        /* Look for the proper place for 'changed key': */
        QString strPositionKey;
        foreach (const QString &strIteratedKey, gEDataManager->map(uID).keys())
        {
            /* If 'iterated key' equal to 'changed key' => break now: */
            if (strIteratedKey == strKey)
                break;
            /* If 'iterated key' is 'known key' => remember it: */
            if (knownKeys.contains(strIteratedKey))
                strPositionKey = strIteratedKey;
        }
        /* Calculate resulting position: */
        iPosition = knownKeys.indexOf(strPositionKey) + 1;
        /* INSERT item to the required position: */
        addDataItem(strKey, strValue, iPosition);
        /* And sort proxy-model: */
        sortData();
    }
}

void UIExtraDataManagerWindow::sltChooserApplyFilter(const QString &strFilter)
{
    /* Apply filtering rule: */
    m_pModelProxyOfChooser->setFilterWildcard(strFilter);
    /* Make sure chooser have current-index if possible: */
    makeSureChooserHaveCurrentIndexIfPossible();
}

void UIExtraDataManagerWindow::sltChooserHandleCurrentChanged(const QModelIndex &index)
{
    /* Remove all the old items first: */
    while (m_pModelSourceOfData->rowCount())
        m_pModelSourceOfData->removeRow(0);

    /* Ignore invalid indexes: */
    if (!index.isValid())
        return;

    /* Add all the new items finally: */
    const QUuid uID = index.data(Field_ID).toUuid();
    if (!gEDataManager->contains(uID))
        gEDataManager->hotloadMachineExtraDataMap(uID);
    const ExtraDataMap data = gEDataManager->map(uID);
    foreach (const QString &strKey, data.keys())
        addDataItem(strKey, data.value(strKey));
    /* And sort proxy-model: */
    sortData();
}

void UIExtraDataManagerWindow::sltChooserHandleSelectionChanged(const QItemSelection&,
                                                                const QItemSelection&)
{
    /* Update actions availability: */
    updateActionsAvailability();
}

void UIExtraDataManagerWindow::sltDataApplyFilter(const QString &strFilter)
{
    /* Apply filtering rule: */
    m_pModelProxyOfData->setFilterWildcard(strFilter);
}

void UIExtraDataManagerWindow::sltDataHandleSelectionChanged(const QItemSelection&,
                                                             const QItemSelection&)
{
    /* Update actions availability: */
    updateActionsAvailability();
}

void UIExtraDataManagerWindow::sltDataHandleItemChanged(QStandardItem *pItem)
{
    /* Make sure passed item is valid: */
    AssertPtrReturnVoid(pItem);

    /* Item-data index: */
    const QModelIndex itemIndex = m_pModelSourceOfData->indexFromItem(pItem);
    const int iRow = itemIndex.row();
    const int iColumn = itemIndex.column();

    /* Key-data is changed: */
    if (iColumn == 0)
    {
        /* Should we replace changed key? */
        bool fReplace = true;

        /* List of 'known keys': */
        QStringList knownKeys;
        for (int iKeyRow = 0; iKeyRow < m_pModelSourceOfData->rowCount(); ++iKeyRow)
        {
            /* Do not consider the row we are changing as Qt's model is not yet updated: */
            if (iKeyRow != iRow)
                knownKeys << dataKey(iKeyRow);
        }

        /* If changed key exists: */
        if (knownKeys.contains(itemIndex.data().toString()))
        {
            /* Show warning and ask for overwriting approval: */
            if (!msgCenter().questionBinary(this, MessageType_Question,
                                            QString("Overwriting already existing key, Continue?"),
                                            0 /* auto-confirm id */,
                                            QString("Overwrite") /* ok button text */,
                                            QString() /* cancel button text */,
                                            false /* ok button by default? */))
            {
                /* Cancel the operation, restore the original extra-data key: */
                pItem->setData(itemIndex.data(Qt::UserRole).toString(), Qt::DisplayRole);
                fReplace = false;
            }
            else
            {
                /* Delete previous extra-data key: */
                gEDataManager->setExtraDataString(itemIndex.data().toString(),
                                                  QString(),
                                                  currentChooserID());
            }
        }

        /* Replace changed extra-data key if necessary: */
        if (fReplace)
        {
            gEDataManager->setExtraDataString(itemIndex.data(Qt::UserRole).toString(),
                                              QString(),
                                              currentChooserID());
            gEDataManager->setExtraDataString(itemIndex.data().toString(),
                                              dataValue(iRow),
                                              currentChooserID());
        }
    }
    /* Value-data is changed: */
    else
    {
        /* Key-data index: */
        const QModelIndex keyIndex = dataKeyIndex(iRow);
        /* Update extra-data: */
        gEDataManager->setExtraDataString(keyIndex.data().toString(),
                                          itemIndex.data().toString(),
                                          currentChooserID());
    }
}

void UIExtraDataManagerWindow::sltDataHandleCustomContextMenuRequested(const QPoint &pos)
{
    /* Prepare menu: */
    QMenu menu;
    menu.addAction(m_pActionAdd);
    menu.addAction(m_pActionDel);
    menu.addSeparator();
    menu.addAction(m_pActionSave);
    /* Execute menu: */
    m_pActionSave->setProperty("CalledFromContextMenu", true);
    menu.exec(m_pViewOfData->viewport()->mapToGlobal(pos));
    m_pActionSave->setProperty("CalledFromContextMenu", QVariant());
}

void UIExtraDataManagerWindow::sltAdd()
{
    /* Make sure this slot called by corresponding action only: */
    QAction *pSenderAction = qobject_cast<QAction*>(sender());
    AssertReturnVoid(pSenderAction && m_pActionAdd);

    /* Create input-dialog: */
    QPointer<QIDialog> pInputDialog = new QIDialog(this);
    AssertPtrReturnVoid(pInputDialog.data());
    {
        /* Configure input-dialog: */
        pInputDialog->setWindowTitle("Add extra-data record..");
        pInputDialog->setMinimumWidth(400);
        /* Create main-layout: */
        QVBoxLayout *pMainLayout = new QVBoxLayout(pInputDialog);
        AssertPtrReturnVoid(pMainLayout);
        {
            /* Create dialog validator group: */
            QObjectValidatorGroup *pValidatorGroup = new QObjectValidatorGroup(pInputDialog);
            AssertReturnVoid(pValidatorGroup);
            /* Create input-layout: */
            QGridLayout *pInputLayout = new QGridLayout;
            AssertPtrReturnVoid(pInputLayout);
            {
                /* Create key-label: */
                QLabel *pLabelKey = new QLabel("&Name:");
                {
                    /* Configure key-label: */
                    pLabelKey->setAlignment(Qt::AlignRight);
                    /* Add key-label into input-layout: */
                    pInputLayout->addWidget(pLabelKey, 0, 0);
                }
                /* Create key-editor: */
                QComboBox *pEditorKey = new QComboBox;
                {
                    /* Configure key-editor: */
                    pEditorKey->setEditable(true);
                    pEditorKey->addItems(knownExtraDataKeys());
                    pLabelKey->setBuddy(pEditorKey);
                    /* Create key-editor property setter: */
                    QObjectPropertySetter *pKeyPropertySetter = new QObjectPropertySetter(pInputDialog, "Key");
                    AssertPtrReturnVoid(pKeyPropertySetter);
                    {
                        /* Configure key-editor property setter: */
                        connect(pEditorKey, &QComboBox::editTextChanged,
                                pKeyPropertySetter, &QObjectPropertySetter::sltAssignProperty);
                    }
                    /* Create key-editor validator: */
                    QObjectValidator *pKeyValidator
                        = new QObjectValidator(new QRegularExpressionValidator(QRegularExpression("[\\s\\S]+"), this));
                    AssertPtrReturnVoid(pKeyValidator);
                    {
                        /* Configure key-editor validator: */
                        connect(pEditorKey, &QComboBox::editTextChanged,
                                pKeyValidator, &QObjectValidator::sltValidate);
                        /* Add key-editor validator into dialog validator group: */
                        pValidatorGroup->addObjectValidator(pKeyValidator);
                    }
                    /* Add key-editor into input-layout: */
                    pInputLayout->addWidget(pEditorKey, 0, 1);
                }
                /* Create value-label: */
                QLabel *pLabelValue = new QLabel("&Value:");
                {
                    /* Configure value-label: */
                    pLabelValue->setAlignment(Qt::AlignRight);
                    /* Add value-label into input-layout: */
                    pInputLayout->addWidget(pLabelValue, 1, 0);
                }
                /* Create value-editor: */
                QLineEdit *pEditorValue = new QLineEdit;
                {
                    /* Configure value-editor: */
                    pLabelValue->setBuddy(pEditorValue);
                    /* Create value-editor property setter: */
                    QObjectPropertySetter *pValuePropertySetter = new QObjectPropertySetter(pInputDialog, "Value");
                    AssertPtrReturnVoid(pValuePropertySetter);
                    {
                        /* Configure value-editor property setter: */
                        connect(pEditorValue, &QLineEdit::textEdited,
                                pValuePropertySetter, &QObjectPropertySetter::sltAssignProperty);
                    }
                    /* Create value-editor validator: */
                    QObjectValidator *pValueValidator
                        = new QObjectValidator(new QRegularExpressionValidator(QRegularExpression("[\\s\\S]+"), this));
                    AssertPtrReturnVoid(pValueValidator);
                    {
                        /* Configure value-editor validator: */
                        connect(pEditorValue, &QLineEdit::textEdited,
                                pValueValidator, &QObjectValidator::sltValidate);
                        /* Add value-editor validator into dialog validator group: */
                        pValidatorGroup->addObjectValidator(pValueValidator);
                    }
                    /* Add value-editor into input-layout: */
                    pInputLayout->addWidget(pEditorValue, 1, 1);
                }
                /* Add input-layout into main-layout: */
                pMainLayout->addLayout(pInputLayout);
            }
            /* Create stretch: */
            pMainLayout->addStretch();
            /* Create dialog button-box: */
            QIDialogButtonBox *pButtonBox = new QIDialogButtonBox;
            AssertPtrReturnVoid(pButtonBox);
            {
                /* Configure button-box: */
                pButtonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
                pButtonBox->button(QDialogButtonBox::Ok)->setAutoDefault(true);
                pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(pValidatorGroup->result());
                pButtonBox->button(QDialogButtonBox::Cancel)->setShortcut(Qt::Key_Escape);
                connect(pValidatorGroup, &QObjectValidatorGroup::sigValidityChange,
                        pButtonBox->button(QDialogButtonBox::Ok), &QPushButton::setEnabled);
                connect(pButtonBox, &QIDialogButtonBox::accepted, pInputDialog.data(), &QIDialog::accept);
                connect(pButtonBox, &QIDialogButtonBox::rejected, pInputDialog.data(), &QIDialog::reject);
                /* Add button-box into main-layout: */
                pMainLayout->addWidget(pButtonBox);
            }
        }
    }

    /* Execute input-dialog: */
    if (pInputDialog->exec() == QDialog::Accepted)
    {
        /* Should we add new key? */
        bool fAdd = true;

        /* List of 'known keys': */
        QStringList knownKeys;
        for (int iKeyRow = 0; iKeyRow < m_pModelSourceOfData->rowCount(); ++iKeyRow)
            knownKeys << dataKey(iKeyRow);

        /* If new key exists: */
        if (knownKeys.contains(pInputDialog->property("Key").toString()))
        {
            /* Show warning and ask for overwriting approval: */
            if (!msgCenter().questionBinary(this, MessageType_Question,
                                            QString("Overwriting already existing key, Continue?"),
                                            0 /* auto-confirm id */,
                                            QString("Overwrite") /* ok button text */,
                                            QString() /* cancel button text */,
                                            false /* ok button by default? */))
            {
                /* Cancel the operation: */
                fAdd = false;
            }
        }

        /* Add new extra-data key if necessary: */
        if (fAdd)
            gEDataManager->setExtraDataString(pInputDialog->property("Key").toString(),
                                              pInputDialog->property("Value").toString(),
                                              currentChooserID());
    }

    /* Destroy input-dialog: */
    if (pInputDialog)
        delete pInputDialog;
}

void UIExtraDataManagerWindow::sltDel()
{
    /* Make sure this slot called by corresponding action only: */
    QAction *pSenderAction = qobject_cast<QAction*>(sender());
    AssertReturnVoid(pSenderAction && m_pActionDel);

    /* Gather the map of chosen items: */
    QMap<QString, QString> items;
    foreach (const QModelIndex &keyIndex, m_pViewOfData->selectionModel()->selectedRows(0))
        items.insert(keyIndex.data().toString(), dataValueIndex(keyIndex.row()).data().toString());

    /* Prepare details: */
    const QString strTableTemplate("<!--EOM--><table border=0 cellspacing=10 cellpadding=0 width=500>%1</table>");
    const QString strRowTemplate("<tr><td><tt>%1</tt></td><td align=right><tt>%2</tt></td></tr>");
    QString strDetails;
    foreach (const QString &strKey, items.keys())
        strDetails += strRowTemplate.arg(strKey, items.value(strKey));
    strDetails = strTableTemplate.arg(strDetails);

    /* Ask for user' confirmation: */
    if (!msgCenter().errorWithQuestion(this, MessageType_Question,
                                       QString("<p>Do you really wish to "
                                               "remove chosen records?</p>"),
                                       strDetails))
        return;

    /* Erase all the chosen extra-data records: */
    foreach (const QString &strKey, items.keys())
        gEDataManager->setExtraDataString(strKey, QString(), currentChooserID());
}

void UIExtraDataManagerWindow::sltSave()
{
    /* Make sure this slot called by corresponding action only: */
    QAction *pSenderAction = qobject_cast<QAction*>(sender());
    AssertReturnVoid(pSenderAction && m_pActionSave);

    /* Compose initial file-name: */
    const QString strInitialFileName = QDir(gpGlobalSession->homeFolder()).absoluteFilePath(QString("%1_ExtraData.xml").arg(currentChooserName()));
    /* Open file-save dialog to choose file to save extra-data into: */
    const QString strFileName = QIFileDialog::getSaveFileName(strInitialFileName, "XML files (*.xml)", this,
                                                              "Choose file to save extra-data into..", 0, true, true);
    /* Make sure file-name was chosen: */
    if (strFileName.isEmpty())
        return;

    /* Create file: */
    QFile output(strFileName);
    /* Open file for writing: */
    bool fOpened = output.open(QIODevice::WriteOnly);
    AssertReturnVoid(fOpened);
    {
        /* Create XML stream writer: */
        QXmlStreamWriter stream(&output);
        /* Configure XML stream writer: */
        stream.setAutoFormatting(true);
        stream.setAutoFormattingIndent(2);
        /* Write document: */
        stream.writeStartDocument();
        {
            stream.writeStartElement("VirtualBox");
            {
                const QUuid uID = currentChooserID();
                bool fIsMachine = uID != UIExtraDataManager::GlobalID;
                const QString strType = fIsMachine ? "Machine" : "Global";
                stream.writeStartElement(strType);
                {
                    if (fIsMachine)
                        stream.writeAttribute("uuid", QString("{%1}").arg(uID.toString()));
                    stream.writeStartElement("ExtraData");
                    {
                        /* Called from context-menu: */
                        if (pSenderAction->property("CalledFromContextMenu").toBool() &&
                            !m_pViewOfData->selectionModel()->selection().isEmpty())
                        {
                            foreach (const QModelIndex &keyIndex, m_pViewOfData->selectionModel()->selectedRows())
                            {
                                /* Get data-value index: */
                                const QModelIndex valueIndex = dataValueIndex(keyIndex.row());
                                /* Write corresponding extra-data item into stream: */
                                stream.writeStartElement("ExtraDataItem");
                                {
                                    stream.writeAttribute("name", keyIndex.data().toString());
                                    stream.writeAttribute("value", valueIndex.data().toString());
                                }
                                stream.writeEndElement(); /* ExtraDataItem */
                            }
                        }
                        /* Called from menu-bar/tool-bar: */
                        else
                        {
                            for (int iRow = 0; iRow < m_pModelProxyOfData->rowCount(); ++iRow)
                            {
                                /* Get indexes: */
                                const QModelIndex keyIndex = m_pModelProxyOfData->index(iRow, 0);
                                const QModelIndex valueIndex = m_pModelProxyOfData->index(iRow, 1);
                                /* Write corresponding extra-data item into stream: */
                                stream.writeStartElement("ExtraDataItem");
                                {
                                    stream.writeAttribute("name", keyIndex.data().toString());
                                    stream.writeAttribute("value", valueIndex.data().toString());
                                }
                                stream.writeEndElement(); /* ExtraDataItem */
                            }
                        }
                    }
                    stream.writeEndElement(); /* ExtraData */
                }
                stream.writeEndElement(); /* strType */
            }
            stream.writeEndElement(); /* VirtualBox */
        }
        stream.writeEndDocument();
        /* Close file: */
        output.close();
    }
}

void UIExtraDataManagerWindow::sltLoad()
{
    /* Make sure this slot called by corresponding action only: */
    QAction *pSenderAction = qobject_cast<QAction*>(sender());
    AssertReturnVoid(pSenderAction && m_pActionLoad);

    /* Compose initial file-name: */
    const QString strInitialFileName = QDir(gpGlobalSession->homeFolder()).absoluteFilePath(QString("%1_ExtraData.xml").arg(currentChooserName()));
    /* Open file-open dialog to choose file to open extra-data into: */
    const QString strFileName = QIFileDialog::getOpenFileName(strInitialFileName, "XML files (*.xml)", this,
                                                              "Choose file to load extra-data from..");
    /* Make sure file-name was chosen: */
    if (strFileName.isEmpty())
        return;

    /* Create file: */
    QFile input(strFileName);
    /* Open file for writing: */
    bool fOpened = input.open(QIODevice::ReadOnly);
    AssertReturnVoid(fOpened);
    {
        /* Create XML stream reader: */
        QXmlStreamReader stream(&input);
        /* Read XML stream: */
        while (!stream.atEnd())
        {
            /* Read subsequent token: */
            const QXmlStreamReader::TokenType tokenType = stream.readNext();
            /* Skip non-interesting tokens: */
            if (tokenType != QXmlStreamReader::StartElement)
                continue;

            /* Get the name of the current element: */
            const QString strElementName = stream.name().toString();

            /* Search for the scope ID: */
            QUuid uLoadingID;
            if (strElementName == "Global")
                uLoadingID = UIExtraDataManager::GlobalID;
            else if (strElementName == "Machine")
            {
                const QXmlStreamAttributes attributes = stream.attributes();
                if (attributes.hasAttribute("uuid"))
                {
                    const QString strUuid = attributes.value("uuid").toString();
                    const QUuid uLoadingID(strUuid);
                    if (uLoadingID.isNull())
                        msgCenter().alert(this, MessageType_Warning,
                                          QString("<p>Invalid extra-data ID:</p>"
                                                  "<p>%1</p>").arg(strUuid));
                }
            }
            /* Look particular extra-data entries: */
            else if (strElementName == "ExtraDataItem")
            {
                const QXmlStreamAttributes attributes = stream.attributes();
                if (attributes.hasAttribute("name") && attributes.hasAttribute("value"))
                {
                    const QString strName = attributes.value("name").toString();
                    const QString strValue = attributes.value("value").toString();
                    gEDataManager->setExtraDataString(strName, strValue, currentChooserID());
                }
            }

            /* Check extra-data ID: */
            if (!uLoadingID.isNull() && uLoadingID != currentChooserID() &&
                !msgCenter().questionBinary(this, MessageType_Question,
                                            QString("<p>Inconsistent extra-data ID:</p>"
                                                    "<p>Current: {%1}</p>"
                                                    "<p>Loading: {%2}</p>"
                                                    "<p>Continue with loading?</p>")
                                                    .arg(currentChooserID().toString(), uLoadingID.toString())))
                break;
        }
        /* Handle XML stream error: */
        if (stream.hasError())
            msgCenter().alert(this, MessageType_Warning,
                              QString("<p>Error reading XML file:</p>"
                                      "<p>%1</p>").arg(stream.error()));
        /* Close file: */
        input.close();
    }
}

bool UIExtraDataManagerWindow::shouldBeMaximized() const
{
    return gEDataManager->extraDataManagerShouldBeMaximized();
}

void UIExtraDataManagerWindow::prepare()
{
    /* Prepare this: */
    prepareThis();
    /* Prepare connections: */
    prepareConnections();
    /* Prepare menu: */
    prepareMenu();
    /* Prepare central-widget: */
    prepareCentralWidget();
    /* Load settings: */
    loadSettings();
}

void UIExtraDataManagerWindow::prepareThis()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/edata_manager_32px.png", ":/edata_manager_16px.png"));
#endif

    /* Apply window title: */
    setWindowTitle("Extra-data Manager");

    /* Do not count that window as important for application,
     * it will NOT be taken into account when other top-level windows will be closed: */
    setAttribute(Qt::WA_QuitOnClose, false);

    /* Delete window when closed: */
    setAttribute(Qt::WA_DeleteOnClose);
}

void UIExtraDataManagerWindow::prepareConnections()
{
    /* Prepare connections: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineRegistered,
            this, &UIExtraDataManagerWindow::sltMachineRegistered);
}

void UIExtraDataManagerWindow::prepareMenu()
{
    /* Create 'Actions' menu: */
    QMenu *pActionsMenu = menuBar()->addMenu("Actions");
    AssertReturnVoid(pActionsMenu);
    {
        /* Create 'Add' action: */
        m_pActionAdd = pActionsMenu->addAction("Add");
        AssertReturnVoid(m_pActionAdd);
        {
            /* Configure 'Add' action: */
            m_pActionAdd->setIcon(UIIconPool::iconSetFull(":/edata_add_24px.png", ":/edata_add_16px.png",
                                                          ":/edata_add_disabled_24px.png", ":/edata_add_disabled_16px.png"));
            m_pActionAdd->setShortcut(QKeySequence("Ctrl+T"));
            connect(m_pActionAdd, &QAction::triggered, this, &UIExtraDataManagerWindow::sltAdd);
        }
        /* Create 'Del' action: */
        m_pActionDel = pActionsMenu->addAction("Remove");
        AssertReturnVoid(m_pActionDel);
        {
            /* Configure 'Del' action: */
            m_pActionDel->setIcon(UIIconPool::iconSetFull(":/edata_remove_24px.png", ":/edata_remove_16px.png",
                                                          ":/edata_remove_disabled_24px.png", ":/edata_remove_disabled_16px.png"));
            m_pActionDel->setShortcut(QKeySequence("Ctrl+R"));
            connect(m_pActionDel, &QAction::triggered, this, &UIExtraDataManagerWindow::sltDel);
        }

        /* Add separator: */
        pActionsMenu->addSeparator();

        /* Create 'Load' action: */
        m_pActionLoad = pActionsMenu->addAction("Load");
        AssertReturnVoid(m_pActionLoad);
        {
            /* Configure 'Load' action: */
            m_pActionLoad->setIcon(UIIconPool::iconSetFull(":/edata_load_24px.png", ":/edata_load_16px.png",
                                                           ":/edata_load_disabled_24px.png", ":/edata_load_disabled_16px.png"));
            m_pActionLoad->setShortcut(QKeySequence("Ctrl+L"));
            connect(m_pActionLoad, &QAction::triggered, this, &UIExtraDataManagerWindow::sltLoad);
        }
        /* Create 'Save' action: */
        m_pActionSave = pActionsMenu->addAction("Save As...");
        AssertReturnVoid(m_pActionSave);
        {
            /* Configure 'Save' action: */
            m_pActionSave->setIcon(UIIconPool::iconSetFull(":/edata_save_24px.png", ":/edata_save_16px.png",
                                                           ":/edata_save_disabled_24px.png", ":/edata_save_disabled_16px.png"));
            m_pActionSave->setShortcut(QKeySequence("Ctrl+S"));
            connect(m_pActionSave, &QAction::triggered, this, &UIExtraDataManagerWindow::sltSave);
        }
    }
}

void UIExtraDataManagerWindow::prepareCentralWidget()
{
    /* Prepare central-widget: */
    setCentralWidget(new QWidget);
    AssertPtrReturnVoid(centralWidget());
    {
        /* Prepare layout: */
        m_pMainLayout = new QVBoxLayout(centralWidget());
        AssertReturnVoid(m_pMainLayout && centralWidget()->layout() &&
                         m_pMainLayout == centralWidget()->layout());
        {
#ifdef VBOX_WS_MAC
            /* No spacing/margins on the Mac: */
            m_pMainLayout->setContentsMargins(0, 0, 0, 0);
            m_pMainLayout->insertSpacing(0, 10);
#else /* !VBOX_WS_MAC */
            /* Set spacing/margin like in the selector window: */
            const int iL = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 2;
            const int iT = qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin) / 2;
            const int iR = qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin) / 2;
            const int iB = qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin) / 2;
            m_pMainLayout->setContentsMargins(iL, iT, iR, iB);
#endif /* !VBOX_WS_MAC */
            /* Prepare tool-bar: */
            prepareToolBar();
            /* Prepare splitter: */
            prepareSplitter();
            /* Prepare button-box: */
            prepareButtonBox();
        }
        /* Initial focus: */
        if (m_pViewOfChooser)
            m_pViewOfChooser->setFocus();
    }
}

void UIExtraDataManagerWindow::prepareToolBar()
{
    /* Create tool-bar: */
    m_pToolBar = new QIToolBar(this);
    AssertPtrReturnVoid(m_pToolBar);
    {
        /* Configure tool-bar: */
        m_pToolBar->setIconSize(QSize(24, 24));
        m_pToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        /* Add actions: */
        m_pToolBar->addAction(m_pActionAdd);
        m_pToolBar->addAction(m_pActionDel);
        m_pToolBar->addSeparator();
        m_pToolBar->addAction(m_pActionLoad);
        m_pToolBar->addAction(m_pActionSave);
        /* Integrate tool-bar into dialog: */
#ifdef VBOX_WS_MAC
        /* Enable unified tool-bars on Mac OS X. Available on Qt >= 4.3: */
        addToolBar(m_pToolBar);
        m_pToolBar->enableMacToolbar();
#else /* !VBOX_WS_MAC */
        /* Add tool-bar into main-layout: */
        m_pMainLayout->addWidget(m_pToolBar);
#endif /* !VBOX_WS_MAC */
    }
}

void UIExtraDataManagerWindow::prepareSplitter()
{
    /* Create splitter: */
    m_pSplitter = new QISplitter;
    AssertPtrReturnVoid(m_pSplitter);
    {
        /* Prepare panes: */
        preparePanes();
        /* Configure splitter: */
        m_pSplitter->setChildrenCollapsible(false);
        m_pSplitter->setStretchFactor(0, 0);
        m_pSplitter->setStretchFactor(1, 1);
        /* Add splitter into main layout: */
        m_pMainLayout->addWidget(m_pSplitter);
    }
}

void UIExtraDataManagerWindow::preparePanes()
{
    /* Prepare chooser-pane: */
    preparePaneChooser();
    /* Prepare data-pane: */
    preparePaneData();
    /* Link chooser and data panes: */
    connect(m_pViewOfChooser->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &UIExtraDataManagerWindow::sltChooserHandleCurrentChanged);
    connect(m_pViewOfChooser->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &UIExtraDataManagerWindow::sltChooserHandleSelectionChanged);
    connect(m_pViewOfData->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &UIExtraDataManagerWindow::sltDataHandleSelectionChanged);
    connect(m_pModelSourceOfData, &QStandardItemModel::itemChanged,
            this, &UIExtraDataManagerWindow::sltDataHandleItemChanged);
    /* Make sure chooser have current-index if possible: */
    makeSureChooserHaveCurrentIndexIfPossible();
}

void UIExtraDataManagerWindow::preparePaneChooser()
{
    /* Create chooser-pane: */
    m_pPaneOfChooser = new QWidget;
    AssertPtrReturnVoid(m_pPaneOfChooser);
    {
        /* Create layout: */
        QVBoxLayout *pLayout = new QVBoxLayout(m_pPaneOfChooser);
        AssertReturnVoid(pLayout && m_pPaneOfChooser->layout() &&
                         pLayout == m_pPaneOfChooser->layout());
        {
            /* Configure layout: */
            const int iR = qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin) / 3;
            pLayout->setContentsMargins(0, 0, iR, 0);
            /* Create chooser-filter: */
            m_pFilterOfChooser = new QLineEdit;
            {
                /* Configure chooser-filter: */
                m_pFilterOfChooser->setPlaceholderText("Search..");
                connect(m_pFilterOfChooser, &QLineEdit::textChanged,
                        this, &UIExtraDataManagerWindow::sltChooserApplyFilter);
                /* Add chooser-filter into layout: */
                pLayout->addWidget(m_pFilterOfChooser);
            }
            /* Create chooser-view: */
            m_pViewOfChooser = new QListView;
            AssertPtrReturnVoid(m_pViewOfChooser);
            {
                /* Configure chooser-view: */
                delete m_pViewOfChooser->itemDelegate();
                m_pViewOfChooser->setItemDelegate(new UIChooserPaneDelegate(m_pViewOfChooser));
                m_pViewOfChooser->setSelectionMode(QAbstractItemView::SingleSelection);
                /* Create source-model: */
                m_pModelSourceOfChooser = new QStandardItemModel(m_pViewOfChooser);
                AssertPtrReturnVoid(m_pModelSourceOfChooser);
                {
                    /* Create proxy-model: */
                    m_pModelProxyOfChooser = new UIChooserPaneSortingModel(m_pViewOfChooser);
                    AssertPtrReturnVoid(m_pModelProxyOfChooser);
                    {
                        /* Configure proxy-model: */
                        m_pModelProxyOfChooser->setSortRole(Field_Name);
                        m_pModelProxyOfChooser->setFilterRole(Field_Name);
                        m_pModelProxyOfChooser->setSortCaseSensitivity(Qt::CaseInsensitive);
                        m_pModelProxyOfChooser->setFilterCaseSensitivity(Qt::CaseInsensitive);
                        m_pModelProxyOfChooser->setSourceModel(m_pModelSourceOfChooser);
                        m_pViewOfChooser->setModel(m_pModelProxyOfChooser);
                    }
                    /* Add global chooser item into source-model: */
                    addChooserItemByID(UIExtraDataManager::GlobalID);
                    /* Add machine chooser items into source-model: */
                    CMachineVector machines = gpGlobalSession->virtualBox().GetMachines();
                    foreach (const CMachine &machine, machines)
                        addChooserItemByMachine(machine);
                    /* And sort proxy-model: */
                    m_pModelProxyOfChooser->sort(0, Qt::AscendingOrder);
                }
                /* Add chooser-view into layout: */
                pLayout->addWidget(m_pViewOfChooser);
            }
        }
        /* Add chooser-pane into splitter: */
        m_pSplitter->addWidget(m_pPaneOfChooser);
    }
}

void UIExtraDataManagerWindow::preparePaneData()
{
    /* Create data-pane: */
    m_pPaneOfData = new QWidget;
    AssertPtrReturnVoid(m_pPaneOfData);
    {
        /* Create layout: */
        QVBoxLayout *pLayout = new QVBoxLayout(m_pPaneOfData);
        AssertReturnVoid(pLayout && m_pPaneOfData->layout() &&
                         pLayout == m_pPaneOfData->layout());
        {
            /* Configure layout: */
            const int iL = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin) / 3;
            pLayout->setContentsMargins(iL, 0, 0, 0);
            /* Create data-filter: */
            m_pFilterOfData = new QLineEdit;
            {
                /* Configure data-filter: */
                m_pFilterOfData->setPlaceholderText("Search..");
                connect(m_pFilterOfData, &QLineEdit::textChanged,
                        this, &UIExtraDataManagerWindow::sltDataApplyFilter);
                /* Add data-filter into layout: */
                pLayout->addWidget(m_pFilterOfData);
            }
            /* Create data-view: */
            m_pViewOfData = new QTableView;
            AssertPtrReturnVoid(m_pViewOfData);
            {
                /* Create item-model: */
                m_pModelSourceOfData = new QStandardItemModel(0, 2, m_pViewOfData);
                AssertPtrReturnVoid(m_pModelSourceOfData);
                {
                    /* Create proxy-model: */
                    m_pModelProxyOfData = new QSortFilterProxyModel(m_pViewOfChooser);
                    AssertPtrReturnVoid(m_pModelProxyOfData);
                    {
                        /* Configure proxy-model: */
                        m_pModelProxyOfData->setSortCaseSensitivity(Qt::CaseInsensitive);
                        m_pModelProxyOfData->setFilterCaseSensitivity(Qt::CaseInsensitive);
                        m_pModelProxyOfData->setSourceModel(m_pModelSourceOfData);
                        m_pViewOfData->setModel(m_pModelProxyOfData);
                    }
                    /* Configure item-model: */
                    m_pModelSourceOfData->setHorizontalHeaderLabels(QStringList() << "Key" << "Value");
                }
                /* Configure data-view: */
                m_pViewOfData->setSortingEnabled(true);
                m_pViewOfData->setAlternatingRowColors(true);
                m_pViewOfData->setContextMenuPolicy(Qt::CustomContextMenu);
                m_pViewOfData->setSelectionMode(QAbstractItemView::ExtendedSelection);
                m_pViewOfData->setSelectionBehavior(QAbstractItemView::SelectRows);
                connect(m_pViewOfData, &QTableView::customContextMenuRequested,
                        this, &UIExtraDataManagerWindow::sltDataHandleCustomContextMenuRequested);
                QHeaderView *pVHeader = m_pViewOfData->verticalHeader();
                QHeaderView *pHHeader = m_pViewOfData->horizontalHeader();
                pVHeader->hide();
                pHHeader->setSortIndicator(0, Qt::AscendingOrder);
                pHHeader->resizeSection(0, qMin(300, pHHeader->width() / 3));
                pHHeader->setStretchLastSection(true);
                /* Add data-view into layout: */
                pLayout->addWidget(m_pViewOfData);
            }
        }
        /* Add data-pane into splitter: */
        m_pSplitter->addWidget(m_pPaneOfData);
    }
}

void UIExtraDataManagerWindow::prepareButtonBox()
{
    /* Create button-box: */
    m_pButtonBox = new QIDialogButtonBox;
    AssertPtrReturnVoid(m_pButtonBox);
    {
        /* Configure button-box: */
        m_pButtonBox->setStandardButtons(QDialogButtonBox::Help | QDialogButtonBox::Close);
        m_pButtonBox->button(QDialogButtonBox::Close)->setShortcut(Qt::Key_Escape);
        connect(m_pButtonBox, &QIDialogButtonBox::helpRequested, m_pButtonBox, &QIDialogButtonBox::sltHandleHelpRequest);
        connect(m_pButtonBox, &QIDialogButtonBox::rejected,      this, &UIExtraDataManagerWindow::close);
        /* Add button-box into main layout: */
        m_pMainLayout->addWidget(m_pButtonBox);
    }
}

void UIExtraDataManagerWindow::loadSettings()
{
    /* Load window geometry: */
    {
        const QRect geo = gEDataManager->extraDataManagerGeometry(this, m_pCenterWidget);
        LogRel2(("GUI: UIExtraDataManagerWindow: Restoring geometry to: Origin=%dx%d, Size=%dx%d\n",
                 geo.x(), geo.y(), geo.width(), geo.height()));
        restoreGeometry(geo);
    }

    /* Load splitter hints: */
    {
        m_pSplitter->setSizes(gEDataManager->extraDataManagerSplitterHints(this));
    }
}

void UIExtraDataManagerWindow::saveSettings()
{
    /* Save splitter hints: */
    {
        gEDataManager->setExtraDataManagerSplitterHints(m_pSplitter->sizes());
    }

    /* Save window geometry: */
    {
        const QRect geo = currentGeometry();
        LogRel2(("GUI: UIExtraDataManagerWindow: Saving geometry as: Origin=%dx%d, Size=%dx%d\n",
                 geo.x(), geo.y(), geo.width(), geo.height()));
        gEDataManager->setExtraDataManagerGeometry(geo, isCurrentlyMaximized());
    }
}

void UIExtraDataManagerWindow::cleanup()
{
    /* Save settings: */
    saveSettings();
}

void UIExtraDataManagerWindow::updateActionsAvailability()
{
    /* Is there something selected in chooser-view? */
    bool fChooserHasSelection = !m_pViewOfChooser->selectionModel()->selection().isEmpty();
    /* Is there something selected in data-view? */
    bool fDataHasSelection = !m_pViewOfData->selectionModel()->selection().isEmpty();

    /* Enable/disable corresponding actions: */
    m_pActionAdd->setEnabled(fChooserHasSelection);
    m_pActionDel->setEnabled(fChooserHasSelection && fDataHasSelection);
    m_pActionLoad->setEnabled(fChooserHasSelection);
    m_pActionSave->setEnabled(fChooserHasSelection);
}

QModelIndex UIExtraDataManagerWindow::chooserIndex(int iRow) const
{
    return m_pModelSourceOfChooser->index(iRow, 0);
}

QModelIndex UIExtraDataManagerWindow::currentChooserIndex() const
{
    return m_pViewOfChooser->currentIndex();
}

QUuid UIExtraDataManagerWindow::chooserID(int iRow) const
{
    return chooserIndex(iRow).data(Field_ID).toUuid();
}

QUuid UIExtraDataManagerWindow::currentChooserID() const
{
    return currentChooserIndex().data(Field_ID).toUuid();
}

QString UIExtraDataManagerWindow::chooserName(int iRow) const
{
    return chooserIndex(iRow).data(Field_Name).toString();
}

QString UIExtraDataManagerWindow::currentChooserName() const
{
    return currentChooserIndex().data(Field_Name).toString();
}

void UIExtraDataManagerWindow::addChooserItem(const QUuid &uID,
                                              const QString &strName,
                                              const QString &strOsTypeID,
                                              const int iPosition /* = -1 */)
{
    /* Create item: */
    QStandardItem *pItem = new QStandardItem;
    AssertPtrReturnVoid(pItem);
    {
        /* Which is NOT editable: */
        pItem->setEditable(false);
        /* Contains passed ID: */
        pItem->setData(uID, Field_ID);
        /* Contains passed name: */
        pItem->setData(strName, Field_Name);
        /* Contains passed OS Type ID: */
        pItem->setData(strOsTypeID, Field_OsTypeID);
        /* And designated as known/unknown depending on extra-data manager status: */
        pItem->setData(gEDataManager->contains(uID), Field_Known);
        /* If insert position defined: */
        if (iPosition != -1)
        {
            /* Insert this item at specified position: */
            m_pModelSourceOfChooser->insertRow(iPosition, pItem);
        }
        /* If insert position undefined: */
        else
        {
            /* Add this item as the last one: */
            m_pModelSourceOfChooser->appendRow(pItem);
        }
    }
}

void UIExtraDataManagerWindow::addChooserItemByMachine(const CMachine &machine,
                                                       const int iPosition /* = -1 */)
{
    /* Make sure VM is accessible: */
    if (!machine.isNull() && machine.GetAccessible())
        return addChooserItem(machine.GetId(), machine.GetName(), machine.GetOSTypeId(), iPosition);
}

void UIExtraDataManagerWindow::addChooserItemByID(const QUuid &uID,
                                                  const int iPosition /* = -1 */)
{
    /* Global ID? */
    if (uID == UIExtraDataManager::GlobalID)
        return addChooserItem(uID, QString("Global"), QString(), iPosition);

    /* Search for the corresponding machine by ID: */
    CVirtualBox vbox = gpGlobalSession->virtualBox();
    const CMachine machine = vbox.FindMachine(uID.toString());
    /* Make sure VM is accessible: */
    if (vbox.isOk() && !machine.isNull() && machine.GetAccessible())
        return addChooserItem(uID, machine.GetName(), machine.GetOSTypeId(), iPosition);
}

void UIExtraDataManagerWindow::makeSureChooserHaveCurrentIndexIfPossible()
{
    /* Make sure chooser have current-index if possible: */
    if (!m_pViewOfChooser->currentIndex().isValid())
    {
        /* Do we still have anything to select? */
        const QModelIndex firstIndex = m_pModelProxyOfChooser->index(0, 0);
        if (firstIndex.isValid())
            m_pViewOfChooser->setCurrentIndex(firstIndex);
    }
}

QModelIndex UIExtraDataManagerWindow::dataIndex(int iRow, int iColumn) const
{
    return m_pModelSourceOfData->index(iRow, iColumn);
}

QModelIndex UIExtraDataManagerWindow::dataKeyIndex(int iRow) const
{
    return dataIndex(iRow, 0);
}

QModelIndex UIExtraDataManagerWindow::dataValueIndex(int iRow) const
{
    return dataIndex(iRow, 1);
}

QString UIExtraDataManagerWindow::dataKey(int iRow) const
{
    return dataKeyIndex(iRow).data().toString();
}

QString UIExtraDataManagerWindow::dataValue(int iRow) const
{
    return dataValueIndex(iRow).data().toString();
}

void UIExtraDataManagerWindow::addDataItem(const QString &strKey,
                                           const QString &strValue,
                                           const int iPosition /* = -1 */)
{
    /* Prepare items: */
    QList<QStandardItem*> items;
    /* Create key item: */
    items << new QStandardItem(strKey);
    items.last()->setData(strKey, Qt::UserRole);
    AssertPtrReturnVoid(items.last());
    /* Create value item: */
    items << new QStandardItem(strValue);
    AssertPtrReturnVoid(items.last());
    /* If insert position defined: */
    if (iPosition != -1)
    {
        /* Insert these items as the row at the required position: */
        m_pModelSourceOfData->insertRow(iPosition, items);
    }
    /* If insert position undefined: */
    else
    {
        /* Add these items as the last one row: */
        m_pModelSourceOfData->appendRow(items);
    }
}

void UIExtraDataManagerWindow::sortData()
{
    /* Sort using current rules: */
    const QHeaderView *pHHeader = m_pViewOfData->horizontalHeader();
    const int iSortSection = pHHeader->sortIndicatorSection();
    const Qt::SortOrder sortOrder = pHHeader->sortIndicatorOrder();
    m_pModelProxyOfData->sort(iSortSection, sortOrder);
}

/* static */
QStringList UIExtraDataManagerWindow::knownExtraDataKeys()
{
    return QStringList()
           << QString()
           << GUI_RestrictedDialogs
           << GUI_SuppressMessages << GUI_InvertMessageOption
#ifdef VBOX_NOTIFICATION_CENTER_WITH_KEEP_BUTTON
           << GUI_NotificationCenter_KeepSuccessfullProgresses
#endif
           << GUI_NotificationCenter_Alignment
           << GUI_NotificationCenter_Order
           << GUI_PreventBetaLabel
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
           << GUI_PreventApplicationUpdate << GUI_UpdateDate << GUI_UpdateCheckCount
#endif
           << GUI_Progress_LegacyMode
           << GUI_Customizations
           << GUI_RestrictedGlobalSettingsPages << GUI_RestrictedMachineSettingsPages
           << GUI_LanguageID
           << GUI_ActivateHoveredMachineWindow
           << GUI_DisableHostScreenSaver
           << GUI_Input_SelectorShortcuts << GUI_Input_MachineShortcuts
           << GUI_RecentFolderHD << GUI_RecentFolderCD << GUI_RecentFolderFD
           << GUI_VISOCreator_RecentFolder << GUI_VISOCreator_DialogGeometry
           << GUI_RecentListHD << GUI_RecentListCD << GUI_RecentListFD
           << GUI_RestrictedNetworkAttachmentTypes
           << GUI_LastSelectorWindowPosition << GUI_SplitterSizes
           << GUI_Toolbar << GUI_Toolbar_Text
           << GUI_Toolbar_MachineTools_Order << GUI_Toolbar_GlobalTools_Order
           << GUI_Tools_LastItemsSelected << GUI_Tools_Detached
           << GUI_Statusbar
           << GUI_GroupDefinitions << GUI_LastItemSelected
           << GUI_Details_Elements
           << GUI_Details_Elements_Preview_UpdateInterval
           << GUI_SnapshotManager_Details_Expanded
           << GUI_VirtualMediaManager_Details_Expanded
           << GUI_HostNetworkManager_Details_Expanded
           << GUI_CloudProfileManager_Restrictions
           << GUI_CloudProfileManager_Details_Expanded
           << GUI_CloudConsoleManager_Restrictions
           << GUI_CloudConsoleManager_Details_Expanded
           << GUI_CloudConsole_PublicKey_Path
           << GUI_HideFromManager << GUI_HideDetails
           << GUI_PreventReconfiguration << GUI_PreventSnapshotOperations
#ifndef VBOX_WS_MAC
           << GUI_MachineWindowIcons << GUI_MachineWindowNamePostfix
#endif
           << GUI_LastNormalWindowPosition << GUI_LastScaleWindowPosition
#ifndef VBOX_WS_MAC
           << GUI_MenuBar_Enabled
#endif
           << GUI_MenuBar_ContextMenu_Enabled
           << GUI_RestrictedRuntimeMenus
           << GUI_RestrictedRuntimeApplicationMenuActions
           << GUI_RestrictedRuntimeMachineMenuActions
           << GUI_RestrictedRuntimeViewMenuActions
           << GUI_RestrictedRuntimeInputMenuActions
           << GUI_RestrictedRuntimeDevicesMenuActions
#ifdef VBOX_WITH_DEBUGGER_GUI
           << GUI_RestrictedRuntimeDebuggerMenuActions
#endif
#ifdef VBOX_WS_MAC
           << GUI_RestrictedRuntimeWindowMenuActions
#endif
           << GUI_RestrictedRuntimeHelpMenuActions
           << GUI_RestrictedVisualStates
           << GUI_Fullscreen << GUI_Seamless << GUI_Scale
#ifdef VBOX_WS_NIX
           << GUI_Fullscreen_LegacyMode
           << GUI_DistinguishMachineWindowGroups
#endif
           << GUI_AutoresizeGuest << GUI_LastVisibilityStatusForGuestScreen << GUI_LastGuestSizeHint
           << GUI_VirtualScreenToHostScreen << GUI_AutomountGuestScreens
#ifndef VBOX_WS_MAC
           << GUI_ShowMiniToolBar << GUI_MiniToolBarAutoHide << GUI_MiniToolBarAlignment
#endif
           << GUI_StatusBar_Enabled << GUI_StatusBar_ContextMenu_Enabled << GUI_RestrictedStatusBarIndicators << GUI_StatusBar_IndicatorOrder
#ifdef VBOX_WS_MAC
           << GUI_RealtimeDockIconUpdateEnabled << GUI_RealtimeDockIconUpdateMonitor << GUI_DockIconDisableOverlay
#endif
           << GUI_PassCAD
           << GUI_MouseCapturePolicy
           << GUI_GuruMeditationHandler
           << GUI_HidLedsSync
           << GUI_ScaleFactor << GUI_Scaling_Optimization
           << GUI_SessionInformationDialogGeometry
           << GUI_GuestControl_ProcessControlSplitterHints
           << GUI_GuestControl_FileManagerDialogGeometry
           << GUI_GuestControl_FileManagerOptions
           << GUI_GuestControl_ProcessControlDialogGeometry
           << GUI_DefaultCloseAction << GUI_RestrictedCloseActions
           << GUI_LastCloseAction << GUI_CloseActionHook << GUI_DiscardStateOnPowerOff
#ifdef VBOX_WITH_DEBUGGER_GUI
           << GUI_Dbg_Enabled << GUI_Dbg_AutoShow
#endif
           << GUI_ExtraDataManager_Geometry << GUI_ExtraDataManager_SplitterHints
           << GUI_LogWindowGeometry
           << GUI_HelpBrowser_LastURLList
           << GUI_HelpBrowser_DialogGeometry
           << GUI_HelpBrowser_Bookmarks
           << GUI_HelpBrowser_ZoomPercentage;
}

#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */


/*********************************************************************************************************************************
*   Class UIExtraDataManager implementation.                                                                                     *
*********************************************************************************************************************************/

/* static */
UIExtraDataManager *UIExtraDataManager::s_pInstance = 0;
const QUuid UIExtraDataManager::GlobalID;

/* static */
void UIExtraDataManager::create()
{
    AssertReturnVoid(!s_pInstance);
    new UIExtraDataManager;
    s_pInstance->prepare();
}

/* static */
void UIExtraDataManager::destroy()
{
    AssertPtrReturnVoid(s_pInstance);
    s_pInstance->cleanup();
    delete s_pInstance;
}

/* static */
UIExtraDataManager *UIExtraDataManager::instance()
{
    /* This is the fallback behavior, we need the lazy-init here
     * only to make sure gEDataManager is never NULL. */
    AssertPtr(s_pInstance);
    if (!s_pInstance)
        create();
    return s_pInstance;
}

#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
/* static */
void UIExtraDataManager::openWindow(QWidget *pCenterWidget)
{
    /* Pass to instance: */
    instance()->open(pCenterWidget);
}
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */

void UIExtraDataManager::hotloadMachineExtraDataMap(const QUuid &uID)
{
    /* Make sure it is valid ID: */
    AssertMsgReturnVoid(!uID.isNull() && uID != GlobalID,
                        ("Invalid VM ID = {%s}\n", uID.toString().toUtf8().constData()));
    /* Which is not loaded yet: */
    AssertReturnVoid(!m_data.contains(uID));

    /* Search for corresponding machine: */
    CVirtualBox vbox = gpGlobalSession->virtualBox();
    CMachine machine = vbox.FindMachine(uID.toString());
    if (machine.isNull())
        return;

    /* Make sure at least empty map is created: */
    m_data[uID] = ExtraDataMap();

    /* Do not handle inaccessible machine: */
    if (!machine.GetAccessible())
        return;

    /* Load machine extra-data map: */
    foreach (const QString &strKey, machine.GetExtraDataKeys())
        m_data[uID][strKey] = machine.GetExtraData(strKey);

    /* Notifies about extra-data map acknowledged: */
    emit sigExtraDataMapAcknowledging(uID);
}

QString UIExtraDataManager::extraDataString(const QString &strKey, const QUuid &uID /* = GlobalID */)
{
    /* Get the actual value: */
    QString strValue = extraDataStringUnion(strKey, uID);
    /* If actual value is null we might be able to find old one: */
    if (strValue.isNull())
    {
        foreach (const QString &strOldKey, g_mapOfObsoleteKeys.values(strKey))
        {
            strValue = extraDataStringUnion(strOldKey, uID);
            if (!strValue.isNull())
                break;
        }
    }
    /* Return null string if result is empty: */
    if (strValue.isEmpty())
        return QString();

    /* Returns corresponding value: */
    return strValue;
}

void UIExtraDataManager::setExtraDataString(const QString &strKey, const QString &strValue, const QUuid &uID /* = GlobalID */)
{
    /* Make sure VBoxSVC is available: */
    if (!gpGlobalSession->isVBoxSVCAvailable())
        return;

    /* Hot-load machine extra-data map if necessary: */
    if (uID != GlobalID && !m_data.contains(uID))
        hotloadMachineExtraDataMap(uID);

    /* Access corresponding map: */
    ExtraDataMap &data = m_data[uID];

    /* [Re]cache passed value: */
    data[strKey] = strValue;

    /* Global extra-data: */
    if (uID == GlobalID)
    {
        /* Get global object: */
        CVirtualBox comVBox = gpGlobalSession->virtualBox();
        /* Update global extra-data: */
        comVBox.SetExtraData(strKey, strValue);
        if (!comVBox.isOk())
            msgCenter().cannotSetExtraData(comVBox, strKey, strValue);
        /* Wipe out old keys: */
        foreach (const QString &strOldKey, g_mapOfObsoleteKeys.values(strKey))
        {
            comVBox.SetExtraData(strOldKey, QString());
            if (!comVBox.isOk())
            {
                msgCenter().cannotSetExtraData(comVBox, strOldKey, strValue);
                break;
            }
        }
    }
    /* Machine extra-data: */
    else
    {
        /* Search for corresponding machine: */
        CVirtualBox comVBox = gpGlobalSession->virtualBox();
        const CMachine comMachine = comVBox.FindMachine(uID.toString());
        AssertReturnVoid(comVBox.isOk() && !comMachine.isNull());
        /* Check the configuration access-level: */
        const KMachineState enmMachineState = comMachine.GetState();
        const KSessionState enmSessionState = comMachine.GetSessionState();
        const ConfigurationAccessLevel enmLevel = configurationAccessLevel(enmSessionState, enmMachineState);
        /* Prepare machine session: */
        CSession comSession;
        if (enmLevel == ConfigurationAccessLevel_Full)
            comSession = openSession(uID);
        else
            comSession = openExistingSession(uID);
        AssertReturnVoid(!comSession.isNull());
        /* Get machine from that session: */
        CMachine comSessionMachine = comSession.GetMachine();
        /* Update machine extra-data: */
        comSessionMachine.SetExtraData(strKey, strValue);
        if (!comSessionMachine.isOk())
            msgCenter().cannotSetExtraData(comSessionMachine, strKey, strValue);
        /* Wipe out old keys: */
        foreach (const QString &strOldKey, g_mapOfObsoleteKeys.values(strKey))
        {
            comSessionMachine.SetExtraData(strOldKey, QString());
            if (!comSessionMachine.isOk())
            {
                msgCenter().cannotSetExtraData(comSessionMachine, strOldKey, strValue);
                break;
            }
        }
        comSession.UnlockMachine();
    }
}

QStringList UIExtraDataManager::extraDataStringList(const QString &strKey, const QUuid &uID /* = GlobalID */)
{
    /* Get the actual value: */
    QString strValue = extraDataStringUnion(strKey, uID);
    /* If actual value is null we might be able to find old one: */
    if (strValue.isNull())
    {
        foreach (const QString &strOldKey, g_mapOfObsoleteKeys.values(strKey))
        {
            strValue = extraDataStringUnion(strOldKey, uID);
            if (!strValue.isNull())
                break;
        }
    }
    /* Return empty string list if result is empty: */
    if (strValue.isEmpty())
        return QStringList();

    /* Few old extra-data string-lists were separated with 'semicolon' symbol.
     * All new separated by 'comma'. We have to take that into account. */
    return strValue.split(QRegularExpression("[;,]"), Qt::SkipEmptyParts);
}

void UIExtraDataManager::setExtraDataStringList(const QString &strKey, const QStringList &value, const QUuid &uID /* = GlobalID */)
{
    /* Make sure VBoxSVC is available: */
    if (!gpGlobalSession->isVBoxSVCAvailable())
        return;

    /* Hot-load machine extra-data map if necessary: */
    if (uID != GlobalID && !m_data.contains(uID))
        hotloadMachineExtraDataMap(uID);

    /* Access corresponding map: */
    ExtraDataMap &data = m_data[uID];

    /* [Re]cache passed value: */
    data[strKey] = value.join(",");

    /* Global extra-data: */
    if (uID == GlobalID)
    {
        /* Get global object: */
        CVirtualBox comVBox = gpGlobalSession->virtualBox();
        /* Update global extra-data: */
        comVBox.SetExtraDataStringList(strKey, value);
        if (!comVBox.isOk())
            msgCenter().cannotSetExtraData(comVBox, strKey, value.join(","));
        /* Wipe out old keys: */
        foreach (const QString &strOldKey, g_mapOfObsoleteKeys.values(strKey))
        {
            comVBox.SetExtraData(strOldKey, QString());
            if (!comVBox.isOk())
            {
                msgCenter().cannotSetExtraData(comVBox, strOldKey, value.join(","));
                break;
            }
        }
    }
    /* Machine extra-data: */
    else
    {
        /* Search for corresponding machine: */
        CVirtualBox comVBox = gpGlobalSession->virtualBox();
        const CMachine comMachine = comVBox.FindMachine(uID.toString());
        AssertReturnVoid(comVBox.isOk() && !comMachine.isNull());
        /* Check the configuration access-level: */
        const KMachineState enmMachineState = comMachine.GetState();
        const KSessionState enmSessionState = comMachine.GetSessionState();
        const ConfigurationAccessLevel enmLevel = configurationAccessLevel(enmSessionState, enmMachineState);
        /* Prepare machine session: */
        CSession comSession;
        if (enmLevel == ConfigurationAccessLevel_Full)
            comSession = openSession(uID);
        else
            comSession = openExistingSession(uID);
        AssertReturnVoid(!comSession.isNull());
        /* Get machine from that session: */
        CMachine comSessionMachine = comSession.GetMachine();
        /* Update machine extra-data: */
        comSessionMachine.SetExtraDataStringList(strKey, value);
        if (!comSessionMachine.isOk())
            msgCenter().cannotSetExtraData(comSessionMachine, strKey, value.join(","));
        /* Wipe out old keys: */
        foreach (const QString &strOldKey, g_mapOfObsoleteKeys.values(strKey))
        {
            comSessionMachine.SetExtraData(strOldKey, QString());
            if (!comSessionMachine.isOk())
            {
                msgCenter().cannotSetExtraData(comSessionMachine, strOldKey, value.join(","));
                break;
            }
        }
        comSession.UnlockMachine();
    }
}

UIExtraDataManager::UIExtraDataManager()
    : m_pHandler(0)
{
    /* Connect to static instance: */
    s_pInstance = this;
}

UIExtraDataManager::~UIExtraDataManager()
{
    /* Disconnect from static instance: */
    s_pInstance = 0;
}

UIExtraDataMetaDefs::DialogType UIExtraDataManager::restrictedDialogTypes(const QUuid &uID)
{
    /* Prepare result: */
    UIExtraDataMetaDefs::DialogType result = UIExtraDataMetaDefs::DialogType_Invalid;
    /* Get restricted runtime-menu-types: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedDialogs, uID))
    {
        UIExtraDataMetaDefs::DialogType value = gpConverter->fromInternalString<UIExtraDataMetaDefs::DialogType>(strValue);
        if (value != UIExtraDataMetaDefs::DialogType_Invalid)
            result = static_cast<UIExtraDataMetaDefs::DialogType>(result | value);
    }
    /* Return result: */
    return result;
}

void UIExtraDataManager::setRestrictedDialogTypes(UIExtraDataMetaDefs::DialogType dialogs, const QUuid &uID)
{
    /* We have MenuType enum registered, so we can enumerate it: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("DialogType");
    QMetaEnum metaEnum = smo.enumerator(iEnumIndex);

    /* Prepare result: */
    QStringList result;
    /* Handle DialogType_All enum-value: */
    if (dialogs == UIExtraDataMetaDefs::DialogType_All)
        result << gpConverter->toInternalString(dialogs);
    else
    {
        /* Handle other enum-values: */
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Get iterated enum-value: */
            const UIExtraDataMetaDefs::DialogType enumValue =
                static_cast<UIExtraDataMetaDefs::DialogType>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip DialogType_Invalid & DialogType_All enum-values: */
            if (enumValue == UIExtraDataMetaDefs::DialogType_Invalid ||
                enumValue == UIExtraDataMetaDefs::DialogType_All)
                continue;
            if (dialogs & enumValue)
                result << gpConverter->toInternalString(enumValue);
        }
    }
    /* Save result: */
    setExtraDataStringList(GUI_RestrictedDialogs, result, uID);
}

UIColorThemeType UIExtraDataManager::colorTheme()
{
    return gpConverter->fromInternalString<UIColorThemeType>(extraDataString(GUI_ColorTheme));
}

void UIExtraDataManager::setColorTheme(const UIColorThemeType &enmType)
{
    setExtraDataString(GUI_ColorTheme, gpConverter->toInternalString(enmType));
}

QStringList UIExtraDataManager::suppressedMessages(const QUuid &uID /* = GlobalID */)
{
    return extraDataStringList(GUI_SuppressMessages, uID);
}

void UIExtraDataManager::setSuppressedMessages(const QStringList &list)
{
    setExtraDataStringList(GUI_SuppressMessages, list);
}

QStringList UIExtraDataManager::messagesWithInvertedOption()
{
    return extraDataStringList(GUI_InvertMessageOption);
}

#ifdef VBOX_NOTIFICATION_CENTER_WITH_KEEP_BUTTON
bool UIExtraDataManager::keepSuccessfullNotificationProgresses()
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_NotificationCenter_KeepSuccessfullProgresses);
}

void UIExtraDataManager::setKeepSuccessfullNotificationProgresses(bool fKeep)
{
    /* 'True' if feature allowed, null-string otherwise: */
    setExtraDataString(GUI_NotificationCenter_KeepSuccessfullProgresses, toFeatureAllowed(fKeep));
}
#endif /* VBOX_NOTIFICATION_CENTER_WITH_KEEP_BUTTON */

Qt::Alignment UIExtraDataManager::notificationCenterAlignment()
{
    const QString strValue = extraDataString(GUI_NotificationCenter_Alignment);
    return strValue.isEmpty() ? Qt::AlignTop : gpConverter->fromInternalString<Qt::Alignment>(strValue);
}

void UIExtraDataManager::setNotificationCenterAlignment(Qt::Alignment enmOrder)
{
    const QString strValue = enmOrder == Qt::AlignTop ? QString() : gpConverter->toInternalString(enmOrder);
    setExtraDataString(GUI_NotificationCenter_Alignment, strValue);
}

Qt::SortOrder UIExtraDataManager::notificationCenterOrder()
{
    const QString strValue = extraDataString(GUI_NotificationCenter_Order);
    return strValue.isEmpty() ? Qt::DescendingOrder : gpConverter->fromInternalString<Qt::SortOrder>(strValue);
}

void UIExtraDataManager::setNotificationCenterOrder(Qt::SortOrder enmOrder)
{
    const QString strValue = enmOrder == Qt::DescendingOrder ? QString() : gpConverter->toInternalString(enmOrder);
    setExtraDataString(GUI_NotificationCenter_Order, strValue);
}

bool UIExtraDataManager::preventBetaBuildLabel()
{
    return isFeatureAllowed(GUI_PreventBetaLabel);
}

#if !defined(VBOX_BLEEDING_EDGE) && !defined(DEBUG)
QString UIExtraDataManager::preventBetaBuildWarningForVersion()
{
    return extraDataString(GUI_PreventBetaWarning);
}
#endif /* !defined(VBOX_BLEEDING_EDGE) && !defined(DEBUG) */

#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
bool UIExtraDataManager::applicationUpdateEnabled()
{
    /* 'True' unless 'restriction' feature allowed: */
    return !isFeatureAllowed(GUI_PreventApplicationUpdate);
}

QString UIExtraDataManager::applicationUpdateData()
{
    return extraDataString(GUI_UpdateDate);
}

void UIExtraDataManager::setApplicationUpdateData(const QString &strValue)
{
    setExtraDataString(GUI_UpdateDate, strValue);
}

qulonglong UIExtraDataManager::applicationUpdateCheckCounter()
{
    /* Read subsequent update check counter value: */
    qulonglong uResult = 1;
    const QString strCheckCount = extraDataString(GUI_UpdateCheckCount);
    if (!strCheckCount.isEmpty())
    {
        bool ok = false;
        qulonglong uCheckCount = strCheckCount.toULongLong(&ok);
        if (ok) uResult = uCheckCount;
    }
    /* Return update check counter value: */
    return uResult;
}

void UIExtraDataManager::incrementApplicationUpdateCheckCounter()
{
    /* Increment update check counter value: */
    setExtraDataString(GUI_UpdateCheckCount, QString::number(applicationUpdateCheckCounter() + 1));
}
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */

bool UIExtraDataManager::legacyProgressHandlingRequested()
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_Progress_LegacyMode);
}

bool UIExtraDataManager::guiFeatureEnabled(GUIFeatureType enmFeature)
{
    /* Acquire GUI feature list: */
    GUIFeatureType enmFeatures = GUIFeatureType_None;
    foreach (const QString &strValue, extraDataStringList(GUI_Customizations))
        enmFeatures = static_cast<GUIFeatureType>(enmFeatures | gpConverter->fromInternalString<GUIFeatureType>(strValue));
    /* Return whether the requested feature is enabled: */
    return enmFeatures & enmFeature;
}

QList<GlobalSettingsPageType> UIExtraDataManager::restrictedGlobalSettingsPages()
{
    /* Prepare result: */
    QList<GlobalSettingsPageType> result;
    /* Get restricted global-settings-pages: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedGlobalSettingsPages))
    {
        GlobalSettingsPageType value = gpConverter->fromInternalString<GlobalSettingsPageType>(strValue);
        if (value != GlobalSettingsPageType_Invalid)
            result << value;
    }
    /* Return result: */
    return result;
}

QList<MachineSettingsPageType> UIExtraDataManager::restrictedMachineSettingsPages(const QUuid &uID)
{
    /* Prepare result: */
    QList<MachineSettingsPageType> result;
    /* Get restricted machine-settings-pages: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedMachineSettingsPages, uID))
    {
        MachineSettingsPageType value = gpConverter->fromInternalString<MachineSettingsPageType>(strValue);
        if (value != MachineSettingsPageType_Invalid)
            result << value;
    }
    /* Return result: */
    return result;
}

bool UIExtraDataManager::isSettingsInExpertMode()
{
#ifdef DEBUG
    /* For debug build return true by default: */
    if (extraDataString(GUI_Settings_ExpertMode).isEmpty())
        return true;
#endif

    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_Settings_ExpertMode);
}

void UIExtraDataManager::setSettingsInExpertMode(bool fExpertMode)
{
    /* Store actual feature state, whether it is "true" or "false",
     * because absent state means default, depending on defines: */
    setExtraDataString(GUI_Settings_ExpertMode, toFeatureState(fExpertMode));
}

QString UIExtraDataManager::languageId()
{
    /* Load language ID: */
    return extraDataString(GUI_LanguageID);
}

void UIExtraDataManager::setLanguageId(const QString &strLanguageId)
{
    /* Save language ID: */
    setExtraDataString(GUI_LanguageID, strLanguageId);
}

MaximumGuestScreenSizePolicy UIExtraDataManager::maxGuestResolutionPolicy()
{
    /* Return maximum guest-screen resolution policy: */
    return gpConverter->fromInternalString<MaximumGuestScreenSizePolicy>(extraDataString(GUI_MaxGuestResolution));
}

void UIExtraDataManager::setMaxGuestScreenResolution(MaximumGuestScreenSizePolicy enmPolicy, const QSize resolution /* = QSize() */)
{
    /* If policy is 'Fixed' => call the wrapper: */
    if (enmPolicy == MaximumGuestScreenSizePolicy_Fixed)
        setMaxGuestResolutionForPolicyFixed(resolution);
    /* Otherwise => just store the value: */
    else
        setExtraDataString(GUI_MaxGuestResolution, gpConverter->toInternalString(enmPolicy));
}

QSize UIExtraDataManager::maxGuestResolutionForPolicyFixed()
{
    /* Acquire maximum guest-screen resolution policy: */
    const QString strPolicy = extraDataString(GUI_MaxGuestResolution);
    const MaximumGuestScreenSizePolicy enmPolicy = gpConverter->fromInternalString<MaximumGuestScreenSizePolicy>(strPolicy);

    /* Make sure maximum guest-screen resolution policy is really Fixed: */
    if (enmPolicy != MaximumGuestScreenSizePolicy_Fixed)
        return QSize();

    /* Parse maximum guest-screen resolution: */
    const QStringList values = strPolicy.split(',');
    int iWidth = values.at(0).toInt();
    int iHeight = values.at(1).toInt();
    if (iWidth <= 0)
        iWidth = 640;
    if (iHeight <= 0)
        iHeight = 480;

    /* Return maximum guest-screen resolution: */
    return QSize(iWidth, iHeight);
}

void UIExtraDataManager::setMaxGuestResolutionForPolicyFixed(const QSize &resolution)
{
    /* If resolution is 'empty' => call the wrapper: */
    if (resolution.isEmpty())
        setMaxGuestScreenResolution(MaximumGuestScreenSizePolicy_Automatic);
    /* Otherwise => just store the value: */
    else
        setExtraDataString(GUI_MaxGuestResolution, QString("%1,%2").arg(resolution.width()).arg(resolution.height()));
}

bool UIExtraDataManager::activateHoveredMachineWindow()
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_ActivateHoveredMachineWindow);
}

void UIExtraDataManager::setActivateHoveredMachineWindow(bool fActivate)
{
    /* 'True' if feature allowed, null-string otherwise: */
    setExtraDataString(GUI_ActivateHoveredMachineWindow, toFeatureAllowed(fActivate));
}

bool UIExtraDataManager::disableHostScreenSaver()
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_DisableHostScreenSaver);
}

void UIExtraDataManager::setDisableHostScreenSaver(bool fActivate)
{
    /* 'True' if feature allowed, null-string otherwise: */
    setExtraDataString(GUI_DisableHostScreenSaver, toFeatureAllowed(fActivate));
}

QString UIExtraDataManager::hostKeyCombination()
{
    /* Acquire host-key combination: */
    QString strHostCombo = extraDataString(GUI_Input_HostKeyCombination);
    /* Invent some sane default if it's absolutely wrong or invalid: */
    QRegularExpression reTemplate("0|[1-9]\\d*(,[1-9]\\d*)?(,[1-9]\\d*)?");
    if (!reTemplate.match(strHostCombo).hasMatch() || !UIHostCombo::isValidKeyCombo(strHostCombo))
    {
#if   defined (VBOX_WS_MAC)
        strHostCombo = "55"; // QZ_LMETA
#elif defined (VBOX_WS_WIN)
        strHostCombo = "163"; // VK_RCONTROL
#elif defined (VBOX_WS_NIX)
        strHostCombo = "65508"; // XK_Control_R
#else
# warning "port me!"
#endif
    }
    /* Return host-key combination: */
    return strHostCombo;
}

void UIExtraDataManager::setFontScaleFactor(int iFontScaleFactor)
{
    if (iFontScaleFactor < UIExtraDataDefs::iFontScaleMin || iFontScaleFactor > UIExtraDataDefs::iFontScaleMax)
        return;
    setExtraDataString(GUI_FontScaleFactor, QString::number(iFontScaleFactor));
}

int UIExtraDataManager::fontScaleFactor()
{
    QString strFontScaleFactor = extraDataString(GUI_FontScaleFactor);
    bool fConversion = false;
    int iScaleFactor = strFontScaleFactor.toInt(&fConversion);
    if (!fConversion || iScaleFactor < UIExtraDataDefs::iFontScaleMin || iScaleFactor > UIExtraDataDefs::iFontScaleMax)
        return 100;
    return iScaleFactor;
}

void UIExtraDataManager::setHostKeyCombination(const QString &strHostCombo)
{
    /* Do not save anything if it's absolutely wrong or invalid: */
    QRegularExpression reTemplate("0|[1-9]\\d*(,[1-9]\\d*)?(,[1-9]\\d*)?");
    if (!reTemplate.match(strHostCombo).hasMatch() || !UIHostCombo::isValidKeyCombo(strHostCombo))
        return;
    /* Define host-combo: */
    setExtraDataString(GUI_Input_HostKeyCombination, strHostCombo);
}

QStringList UIExtraDataManager::shortcutOverrides(const QString &strPoolExtraDataID)
{
    if (strPoolExtraDataID == GUI_Input_SelectorShortcuts)
        return extraDataStringList(GUI_Input_SelectorShortcuts);
    if (strPoolExtraDataID == GUI_Input_MachineShortcuts)
        return extraDataStringList(GUI_Input_MachineShortcuts);
    return QStringList();
}

bool UIExtraDataManager::autoCaptureEnabled()
{
    /* Prepare auto-capture flag: */
    bool fAutoCapture = true /* indifferently */;
    /* Acquire whether the auto-capture is restricted: */
    QString strAutoCapture = extraDataString(GUI_Input_AutoCapture);
    /* Invent some sane default if it's empty: */
    if (strAutoCapture.isEmpty())
    {
#if defined(VBOX_WS_NIX) && defined(DEBUG)
        fAutoCapture = false;
#else
        fAutoCapture = true;
#endif
    }
    /* 'True' unless feature restricted: */
    else
        fAutoCapture = !isFeatureRestricted(GUI_Input_AutoCapture);
    /* Return auto-capture flag: */
    return fAutoCapture;
}

void UIExtraDataManager::setAutoCaptureEnabled(bool fEnabled)
{
    /* Store actual feature state, whether it is "true" or "false",
     * because absent state means default, different on various hosts: */
    setExtraDataString(GUI_Input_AutoCapture, toFeatureState(fEnabled));
}

QString UIExtraDataManager::remappedScanCodes()
{
    /* Acquire remapped scan codes: */
    QString strRemappedScanCodes = extraDataString(GUI_RemapScancodes);
    /* Clear the record if it's absolutely wrong: */
    QRegularExpression reTemplate("(\\d+=\\d+,)*\\d+=\\d+");
    if (!reTemplate.match(strRemappedScanCodes).hasMatch())
        strRemappedScanCodes.clear();
    /* Return remapped scan codes: */
    return strRemappedScanCodes;
}

QString UIExtraDataManager::proxySettings()
{
    return extraDataString(GUI_ProxySettings);
}

void UIExtraDataManager::setProxySettings(const QString &strSettings)
{
    setExtraDataString(GUI_ProxySettings, strSettings);
}

QString UIExtraDataManager::recentFolderForHardDrives()
{
    return extraDataString(GUI_RecentFolderHD);
}

QString UIExtraDataManager::recentFolderForOpticalDisks()
{
    return extraDataString(GUI_RecentFolderCD);
}

QString UIExtraDataManager::recentFolderForFloppyDisks()
{
    return extraDataString(GUI_RecentFolderFD);
}

void UIExtraDataManager::setRecentFolderForHardDrives(const QString &strValue)
{
    setExtraDataString(GUI_RecentFolderHD, strValue);
}

void UIExtraDataManager::setRecentFolderForOpticalDisks(const QString &strValue)
{
    setExtraDataString(GUI_RecentFolderCD, strValue);
}

void UIExtraDataManager::setRecentFolderForFloppyDisks(const QString &strValue)
{
    setExtraDataString(GUI_RecentFolderFD, strValue);
}

QStringList UIExtraDataManager::recentListOfHardDrives()
{
    return extraDataStringList(GUI_RecentListHD);
}

QStringList UIExtraDataManager::recentListOfOpticalDisks()
{
    return extraDataStringList(GUI_RecentListCD);
}

QStringList UIExtraDataManager::recentListOfFloppyDisks()
{
    return extraDataStringList(GUI_RecentListFD);
}

void UIExtraDataManager::setRecentListOfHardDrives(const QStringList &value)
{
    setExtraDataStringList(GUI_RecentListHD, value);
}

void UIExtraDataManager::setRecentListOfOpticalDisks(const QStringList &value)
{
    setExtraDataStringList(GUI_RecentListCD, value);
}

void UIExtraDataManager::setRecentListOfFloppyDisks(const QStringList &value)
{
    setExtraDataStringList(GUI_RecentListFD, value);
}

UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork UIExtraDataManager::restrictedNetworkAttachmentTypes()
{
    /* Prepare result: */
    UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork enmResult =
        UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_Invalid;
    /* Get restricted network attachment types: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedNetworkAttachmentTypes))
    {
        const UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork enmValue =
            gpConverter->fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork>(strValue);
        if (enmValue != UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_Invalid && !(enmResult & enmValue))
            enmResult = static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork>(enmResult | enmValue);
    }
    /* Return result: */
    return enmResult;
}

QString UIExtraDataManager::visoCreatorRecentFolder()
{
    return extraDataString(GUI_VISOCreator_RecentFolder);
}

void UIExtraDataManager::setVISOCreatorRecentFolder(const QString &strValue)
{
    setExtraDataString(GUI_VISOCreator_RecentFolder, strValue);
}

QRect UIExtraDataManager::visoCreatorDialogGeometry(QWidget *pWidget, QWidget *pParentWidget, const QRect &defaultGeometry)
{
    return dialogGeometry(GUI_VISOCreator_DialogGeometry, pWidget, pParentWidget, defaultGeometry);
}

void UIExtraDataManager::setVisoCreatorDialogGeometry(const QRect &geometry, bool fMaximized)
{
    setDialogGeometry(GUI_VISOCreator_DialogGeometry, geometry, fMaximized);
}

bool UIExtraDataManager::visoCreatorDialogShouldBeMaximized()
{
    return dialogShouldBeMaximized(GUI_VISOCreator_DialogGeometry);
}

QRect UIExtraDataManager::selectorWindowGeometry(QWidget *pWidget)
{
    return dialogGeometry(GUI_LastSelectorWindowPosition, pWidget);
}

bool UIExtraDataManager::selectorWindowShouldBeMaximized()
{
    return dialogShouldBeMaximized(GUI_LastSelectorWindowPosition);
}

void UIExtraDataManager::setSelectorWindowGeometry(const QRect &geometry, bool fMaximized)
{
    /* Serialize passed values: */
    QStringList data;
    data << QString::number(geometry.x());
    data << QString::number(geometry.y());
    data << QString::number(geometry.width());
    data << QString::number(geometry.height());
    if (fMaximized)
        data << GUI_Geometry_State_Max;

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_LastSelectorWindowPosition, data);
}

QList<int> UIExtraDataManager::selectorWindowSplitterHints()
{
    /* Get corresponding extra-data: */
    const QStringList data = extraDataStringList(GUI_SplitterSizes);

    /* Parse loaded data: */
    QList<int> hints;
    hints << (data.size() > 0 ? data[0].toInt() : 0);
    hints << (data.size() > 1 ? data[1].toInt() : 0);

    /* Return hints: */
    return hints;
}

void UIExtraDataManager::setSelectorWindowSplitterHints(const QList<int> &hints)
{
    /* Parse passed hints: */
    QStringList data;
    data << (hints.size() > 0 ? QString::number(hints[0]) : QString());
    data << (hints.size() > 1 ? QString::number(hints[1]) : QString());

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_SplitterSizes, data);
}

bool UIExtraDataManager::selectorWindowToolBarVisible()
{
    /* 'True' unless feature restricted: */
    return !isFeatureRestricted(GUI_Toolbar);
}

void UIExtraDataManager::setSelectorWindowToolBarVisible(bool fVisible)
{
    /* 'False' if feature restricted, null-string otherwise: */
    setExtraDataString(GUI_Toolbar, toFeatureRestricted(!fVisible));
}

bool UIExtraDataManager::selectorWindowToolBarTextVisible()
{
    /* 'True' unless feature restricted: */
    return !isFeatureRestricted(GUI_Toolbar_Text);
}

void UIExtraDataManager::setSelectorWindowToolBarTextVisible(bool fVisible)
{
    /* 'False' if feature restricted, null-string otherwise: */
    setExtraDataString(GUI_Toolbar_Text, toFeatureRestricted(!fVisible));
}

QList<UIToolType> UIExtraDataManager::toolsPaneLastItemsChosen()
{
    /* Parse loaded data: */
    QList<UIToolType> result;
    foreach (const QString &strValue, extraDataStringList(GUI_Tools_LastItemsSelected))
    {
        const UIToolType enmType = gpConverter->fromInternalString<UIToolType>(strValue);
        if (enmType != UIToolType_Invalid)
            result << enmType;
    }

    /* Return result: */
    return result;
}

void UIExtraDataManager::setToolsPaneLastItemsChosen(const QList<UIToolType> &set)
{
    /* Serialize passed values: */
    QStringList data;
    foreach (const UIToolType &enmType, set)
        data << gpConverter->toInternalString(enmType);

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_Tools_LastItemsSelected, data);
}

QList<UIToolType> UIExtraDataManager::detachedTools()
{
    /* Parse loaded data: */
    QList<UIToolType> result;
    foreach (const QString &strValue, extraDataStringList(GUI_Tools_Detached))
    {
        const UIToolType enmType = gpConverter->fromInternalString<UIToolType>(strValue);
        if (enmType != UIToolType_Invalid)
            result << enmType;
    }

    /* Return result: */
    return result;
}

void UIExtraDataManager::setDetachedTools(const QList<UIToolType> &tools)
{
    /* Serialize passed values: */
    QStringList data;
    foreach (const UIToolType &enmType, tools)
        data << gpConverter->toInternalString(enmType);

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_Tools_Detached, data);
}

bool UIExtraDataManager::selectorWindowStatusBarVisible()
{
    /* 'True' unless feature restricted: */
    return !isFeatureRestricted(GUI_Statusbar);
}

void UIExtraDataManager::setSelectorWindowStatusBarVisible(bool fVisible)
{
    /* 'False' if feature restricted, null-string otherwise: */
    setExtraDataString(GUI_Statusbar, toFeatureRestricted(!fVisible));
}

QStringList UIExtraDataManager::knownMachineGroupDefinitionKeys()
{
    /* Acquire a list of known group definition keys: */
    QStringList result;
    foreach (const QString &strKey, m_data.value(GlobalID).keys())
        if (strKey.startsWith(GUI_GroupDefinitions))
        {
            QString strGroupID = strKey;
            strGroupID.remove(GUI_GroupDefinitions);
            result << strGroupID;
        }

    /* Return result: */
    return result;
}

QStringList UIExtraDataManager::machineGroupDefinitions(const QString &strGroupID)
{
    return extraDataStringList(GUI_GroupDefinitions + strGroupID);
}

void UIExtraDataManager::setMachineGroupDefinitions(const QString &strGroupID, const QStringList &definitions)
{
    setExtraDataStringList(GUI_GroupDefinitions + strGroupID, definitions);
}

QString UIExtraDataManager::selectorWindowLastItemChosen()
{
    return extraDataString(GUI_LastItemSelected);
}

void UIExtraDataManager::setSelectorWindowLastItemChosen(const QString &strItemID)
{
    setExtraDataString(GUI_LastItemSelected, strItemID);
}

QMap<DetailsElementType, bool> UIExtraDataManager::selectorWindowDetailsElements()
{
    /* Get corresponding extra-data: */
    const QStringList data = extraDataStringList(GUI_Details_Elements);

    /* Desearialize passed elements: */
    QMap<DetailsElementType, bool> elements;
    foreach (QString strItem, data)
    {
        bool fOpened = true;
        if (strItem.endsWith("Closed", Qt::CaseInsensitive))
        {
            fOpened = false;
            strItem.remove("Closed", Qt::CaseInsensitive);
        }
        const DetailsElementType enmType = gpConverter->fromInternalString<DetailsElementType>(strItem);
        if (enmType != DetailsElementType_Invalid)
            elements[enmType] = fOpened;
    }

    /* If settings are empty: */
    if (elements.isEmpty())
    {
        /* Propose the defaults: */
        elements[DetailsElementType_General] = true;
        elements[DetailsElementType_Preview] = true;
        elements[DetailsElementType_System] = true;
        elements[DetailsElementType_Display] = true;
        elements[DetailsElementType_Storage] = true;
        elements[DetailsElementType_Audio] = true;
        elements[DetailsElementType_Network] = true;
        elements[DetailsElementType_USB] = true;
        elements[DetailsElementType_SF] = true;
        elements[DetailsElementType_Description] = true;
    }

    /* Return elements: */
    return elements;
}

void UIExtraDataManager::setSelectorWindowDetailsElements(const QMap<DetailsElementType, bool> &elements)
{
    /* Prepare corresponding extra-data: */
    QStringList data;

    /* Searialize passed elements: */
    foreach (DetailsElementType enmType, elements.keys())
    {
        QString strValue = gpConverter->toInternalString(enmType);
        if (!elements[enmType])
            strValue += "Closed";
        data << strValue;
    }

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_Details_Elements, data);
}

PreviewUpdateIntervalType UIExtraDataManager::selectorWindowPreviewUpdateInterval()
{
    return gpConverter->fromInternalString<PreviewUpdateIntervalType>(extraDataString(GUI_Details_Elements_Preview_UpdateInterval));
}

void UIExtraDataManager::setSelectorWindowPreviewUpdateInterval(PreviewUpdateIntervalType interval)
{
    setExtraDataString(GUI_Details_Elements_Preview_UpdateInterval, gpConverter->toInternalString(interval));
}

QStringList UIExtraDataManager::vboxManagerDetailsPaneElementOptions(DetailsElementType enmElementType)
{
    /* Compose full key from GUI_Details_Elements and enmElementType: */
    QString strElementType = gpConverter->toInternalString(enmElementType);
    AssertReturn(!strElementType.isEmpty(), QStringList());
    strElementType[0] = strElementType.at(0).toUpper();
    const QString strFullKey = QString("%1/%2").arg(GUI_Details_Elements).arg(strElementType);

    /* Return option list: */
    return extraDataStringList(strFullKey);
}

void UIExtraDataManager::setVBoxManagerDetailsPaneElementOptions(DetailsElementType enmElementType, const QStringList &options)
{
    /* Compose full key from GUI_Details_Elements and enmElementType: */
    QString strElementType = gpConverter->toInternalString(enmElementType);
    AssertReturnVoid(!strElementType.isEmpty());
    strElementType[0] = strElementType.at(0).toUpper();
    const QString strFullKey = QString("%1/%2").arg(GUI_Details_Elements).arg(strElementType);

    /* Store option list: */
    setExtraDataStringList(strFullKey, options);
}

bool UIExtraDataManager::snapshotManagerDetailsExpanded()
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_SnapshotManager_Details_Expanded);
}

void UIExtraDataManager::setSnapshotManagerDetailsExpanded(bool fExpanded)
{
    /* 'True' if feature allowed, null-string otherwise: */
    return setExtraDataString(GUI_SnapshotManager_Details_Expanded, toFeatureAllowed(fExpanded));
}

bool UIExtraDataManager::virtualMediaManagerDetailsExpanded()
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_VirtualMediaManager_Details_Expanded);
}

void UIExtraDataManager::setVirtualMediaManagerDetailsExpanded(bool fExpanded)
{
    /* 'True' if feature allowed, null-string otherwise: */
    return setExtraDataString(GUI_VirtualMediaManager_Details_Expanded, toFeatureAllowed(fExpanded));
}

bool UIExtraDataManager::virtualMediaManagerSearchWidgetExpanded()
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_VirtualMediaManager_Search_Widget_Expanded);
}

void UIExtraDataManager::setVirtualMediaManagerSearchWidgetExpanded(bool fExpanded)
{
    /* 'True' if feature allowed, null-string otherwise: */
    return setExtraDataString(GUI_VirtualMediaManager_Search_Widget_Expanded, toFeatureAllowed(fExpanded));
}

bool UIExtraDataManager::hostNetworkManagerDetailsExpanded()
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_HostNetworkManager_Details_Expanded);
}

void UIExtraDataManager::setHostNetworkManagerDetailsExpanded(bool fExpanded)
{
    /* 'True' if feature allowed, null-string otherwise: */
    return setExtraDataString(GUI_HostNetworkManager_Details_Expanded, toFeatureAllowed(fExpanded));
}

QStringList UIExtraDataManager::cloudProfileManagerRestrictions()
{
    return extraDataStringList(GUI_CloudProfileManager_Restrictions);
}

void UIExtraDataManager::setCloudProfileManagerRestrictions(const QStringList &restrictions)
{
    return setExtraDataStringList(GUI_CloudProfileManager_Restrictions, restrictions);
}

bool UIExtraDataManager::cloudProfileManagerDetailsExpanded()
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_CloudProfileManager_Details_Expanded);
}

void UIExtraDataManager::setCloudProfileManagerDetailsExpanded(bool fExpanded)
{
    /* 'True' if feature allowed, null-string otherwise: */
    return setExtraDataString(GUI_CloudProfileManager_Details_Expanded, toFeatureAllowed(fExpanded));
}

QStringList UIExtraDataManager::cloudConsoleManagerApplications()
{
    /* Gather a list of keys matching required expression: */
    QStringList result;
    const QRegularExpression re(QString("^%1/([^/]+)$").arg(GUI_CloudConsoleManager_Application));
    foreach (const QString &strKey, m_data.value(GlobalID).keys())
    {
        const QRegularExpressionMatch mt = re.match(strKey);
        if (mt.hasMatch())
            result << mt.captured(1);
    }
    return result;
}

QStringList UIExtraDataManager::cloudConsoleManagerProfiles(const QString &strId)
{
    /* Gather a list of keys matching required expression: */
    QStringList result;
    const QRegularExpression re(QString("^%1/%2/([^/]+)$").arg(GUI_CloudConsoleManager_Application, strId));
    foreach (const QString &strKey, m_data.value(GlobalID).keys())
    {
        const QRegularExpressionMatch mt = re.match(strKey);
        if (mt.hasMatch())
            result << mt.captured(1);
    }
    return result;
}

QString UIExtraDataManager::cloudConsoleManagerApplication(const QString &strId)
{
    return extraDataString(QString("%1/%2").arg(GUI_CloudConsoleManager_Application, strId));
}

void UIExtraDataManager::setCloudConsoleManagerApplication(const QString &strId, const QString &strDefinition)
{
    setExtraDataString(QString("%1/%2").arg(GUI_CloudConsoleManager_Application, strId), strDefinition);
}

QString UIExtraDataManager::cloudConsoleManagerProfile(const QString &strApplicationId, const QString &strProfileId)
{
    return extraDataString(QString("%1/%2/%3").arg(GUI_CloudConsoleManager_Application, strApplicationId, strProfileId));
}

void UIExtraDataManager::setCloudConsoleManagerProfile(const QString &strApplicationId, const QString &strProfileId, const QString &strDefinition)
{
    setExtraDataString(QString("%1/%2/%3").arg(GUI_CloudConsoleManager_Application, strApplicationId, strProfileId), strDefinition);
}

QStringList UIExtraDataManager::cloudConsoleManagerRestrictions()
{
    return extraDataStringList(GUI_CloudConsoleManager_Restrictions);
}

void UIExtraDataManager::setCloudConsoleManagerRestrictions(const QStringList &restrictions)
{
    return setExtraDataStringList(GUI_CloudConsoleManager_Restrictions, restrictions);
}

bool UIExtraDataManager::cloudConsoleManagerDetailsExpanded()
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_CloudConsoleManager_Details_Expanded);
}

void UIExtraDataManager::setCloudConsoleManagerDetailsExpanded(bool fExpanded)
{
    /* 'True' if feature allowed, null-string otherwise: */
    return setExtraDataString(GUI_CloudConsoleManager_Details_Expanded, toFeatureAllowed(fExpanded));
}

QString UIExtraDataManager::cloudConsolePublicKeyPath()
{
    return extraDataString(GUI_CloudConsole_PublicKey_Path);
}

void UIExtraDataManager::setCloudConsolePublicKeyPath(const QString &strPath)
{
    setExtraDataString(GUI_CloudConsole_PublicKey_Path, strPath);
}

bool UIExtraDataManager::showMachineInVirtualBoxManagerChooser(const QUuid &uID)
{
    /* 'True' unless 'restriction' feature allowed: */
    return !isFeatureAllowed(GUI_HideFromManager, uID);
}

bool UIExtraDataManager::showMachineInVirtualBoxManagerDetails(const QUuid &uID)
{
    /* 'True' unless 'restriction' feature allowed: */
    return !isFeatureAllowed(GUI_HideDetails, uID);
}

bool UIExtraDataManager::machineReconfigurationEnabled(const QUuid &uID)
{
    /* 'True' unless 'restriction' feature allowed: */
    return !isFeatureAllowed(GUI_PreventReconfiguration, uID);
}

bool UIExtraDataManager::machineSnapshotOperationsEnabled(const QUuid &uID)
{
    /* 'True' unless 'restriction' feature allowed: */
    return !isFeatureAllowed(GUI_PreventSnapshotOperations, uID);
}

QStringList UIExtraDataManager::machineWindowIconNames(const QUuid &uID)
{
    return extraDataStringList(GUI_MachineWindowIcons, uID);
}

#ifndef VBOX_WS_MAC
QString UIExtraDataManager::machineWindowNamePostfix(const QUuid &uID)
{
    return extraDataString(GUI_MachineWindowNamePostfix, uID);
}
#endif /* !VBOX_WS_MAC */

QRect UIExtraDataManager::machineWindowGeometry(UIVisualStateType visualStateType, ulong uScreenIndex, const QUuid &uID)
{
    /* Choose corresponding key: */
    QString strKey;
    switch (visualStateType)
    {
        case UIVisualStateType_Normal: strKey = extraDataKeyPerScreen(GUI_LastNormalWindowPosition, uScreenIndex); break;
        case UIVisualStateType_Scale:  strKey = extraDataKeyPerScreen(GUI_LastScaleWindowPosition, uScreenIndex); break;
        default: AssertFailedReturn(QRect());
    }

    /* Get corresponding extra-data: */
    const QStringList data = extraDataStringList(strKey, uID);

    /* Parse loaded data: */
    int iX = 0, iY = 0, iW = 0, iH = 0;
    bool fOk = data.size() >= 4;
    do
    {
        if (!fOk) break;
        iX = data[0].toInt(&fOk);
        if (!fOk) break;
        iY = data[1].toInt(&fOk);
        if (!fOk) break;
        iW = data[2].toInt(&fOk);
        if (!fOk) break;
        iH = data[3].toInt(&fOk);
    }
    while (0);

    /* Return geometry (loaded or null): */
    return fOk ? QRect(iX, iY, iW, iH) : QRect();
}

bool UIExtraDataManager::machineWindowShouldBeMaximized(UIVisualStateType visualStateType, ulong uScreenIndex, const QUuid &uID)
{
    /* Choose corresponding key: */
    QString strKey;
    switch (visualStateType)
    {
        case UIVisualStateType_Normal: strKey = extraDataKeyPerScreen(GUI_LastNormalWindowPosition, uScreenIndex); break;
        case UIVisualStateType_Scale:  strKey = extraDataKeyPerScreen(GUI_LastScaleWindowPosition, uScreenIndex); break;
        default: AssertFailedReturn(false);
    }

    /* Get corresponding extra-data: */
    const QStringList data = extraDataStringList(strKey, uID);

    /* Make sure 5th item has required value: */
    return data.size() == 5 && data[4] == GUI_Geometry_State_Max;
}

void UIExtraDataManager::setMachineWindowGeometry(UIVisualStateType visualStateType, ulong uScreenIndex, const QRect &geometry, bool fMaximized, const QUuid &uID)
{
    /* Choose corresponding key: */
    QString strKey;
    switch (visualStateType)
    {
        case UIVisualStateType_Normal: strKey = extraDataKeyPerScreen(GUI_LastNormalWindowPosition, uScreenIndex); break;
        case UIVisualStateType_Scale:  strKey = extraDataKeyPerScreen(GUI_LastScaleWindowPosition, uScreenIndex); break;
        default: AssertFailedReturnVoid();
    }

    /* Serialize passed values: */
    QStringList data;
    data << QString::number(geometry.x());
    data << QString::number(geometry.y());
    data << QString::number(geometry.width());
    data << QString::number(geometry.height());
    if (fMaximized)
        data << GUI_Geometry_State_Max;

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(strKey, data, uID);
}

#ifndef VBOX_WS_MAC
bool UIExtraDataManager::menuBarEnabled(const QUuid &uID)
{
    /* 'True' unless feature restricted: */
    return !isFeatureRestricted(GUI_MenuBar_Enabled, uID);
}

void UIExtraDataManager::setMenuBarEnabled(bool fEnabled, const QUuid &uID)
{
    /* 'False' if feature restricted, null-string otherwise: */
    setExtraDataString(GUI_MenuBar_Enabled, toFeatureRestricted(!fEnabled), uID);
}
#endif /* !VBOX_WS_MAC */

bool UIExtraDataManager::menuBarContextMenuEnabled(const QUuid &uID)
{
    /* 'True' unless feature restricted: */
    return !isFeatureRestricted(GUI_MenuBar_ContextMenu_Enabled, uID);
}

void UIExtraDataManager::setMenuBarContextMenuEnabled(bool fEnabled, const QUuid &uID)
{
    /* 'False' if feature restricted, null-string otherwise: */
    setExtraDataString(GUI_MenuBar_ContextMenu_Enabled, toFeatureRestricted(!fEnabled), uID);
}

UIExtraDataMetaDefs::MenuType UIExtraDataManager::restrictedRuntimeMenuTypes(const QUuid &uID)
{
    /* Prepare result: */
    UIExtraDataMetaDefs::MenuType result = UIExtraDataMetaDefs::MenuType_Invalid;
    /* Get restricted runtime-menu-types: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedRuntimeMenus, uID))
    {
        UIExtraDataMetaDefs::MenuType value = gpConverter->fromInternalString<UIExtraDataMetaDefs::MenuType>(strValue);
        if (value != UIExtraDataMetaDefs::MenuType_Invalid)
            result = static_cast<UIExtraDataMetaDefs::MenuType>(result | value);
    }
    /* Return result: */
    return result;
}

void UIExtraDataManager::setRestrictedRuntimeMenuTypes(UIExtraDataMetaDefs::MenuType types, const QUuid &uID)
{
    /* We have MenuType enum registered, so we can enumerate it: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("MenuType");
    QMetaEnum metaEnum = smo.enumerator(iEnumIndex);

    /* Prepare result: */
    QStringList result;
    /* Handle MenuType_All enum-value: */
    if (types == UIExtraDataMetaDefs::MenuType_All)
        result << gpConverter->toInternalString(types);
    else
    {
        /* Handle other enum-values: */
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Get iterated enum-value: */
            const UIExtraDataMetaDefs::MenuType enumValue =
                static_cast<UIExtraDataMetaDefs::MenuType>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip MenuType_Invalid & MenuType_All enum-values: */
            if (enumValue == UIExtraDataMetaDefs::MenuType_Invalid ||
                enumValue == UIExtraDataMetaDefs::MenuType_All)
                continue;
            if (types & enumValue)
                result << gpConverter->toInternalString(enumValue);
        }
    }
    /* Save result: */
    setExtraDataStringList(GUI_RestrictedRuntimeMenus, result, uID);
}

UIExtraDataMetaDefs::MenuApplicationActionType UIExtraDataManager::restrictedRuntimeMenuApplicationActionTypes(const QUuid &uID)
{
    /* Prepare result: */
    UIExtraDataMetaDefs::MenuApplicationActionType result = UIExtraDataMetaDefs::MenuApplicationActionType_Invalid;
    /* Get restricted runtime-application-menu action-types: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedRuntimeApplicationMenuActions, uID))
    {
        UIExtraDataMetaDefs::MenuApplicationActionType value = gpConverter->fromInternalString<UIExtraDataMetaDefs::MenuApplicationActionType>(strValue);
        if (value != UIExtraDataMetaDefs::MenuApplicationActionType_Invalid)
            result = static_cast<UIExtraDataMetaDefs::MenuApplicationActionType>(result | value);
    }
    /* Return result: */
    return result;
}

void UIExtraDataManager::setRestrictedRuntimeMenuApplicationActionTypes(UIExtraDataMetaDefs::MenuApplicationActionType types, const QUuid &uID)
{
    /* We have MenuApplicationActionType enum registered, so we can enumerate it: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("MenuApplicationActionType");
    QMetaEnum metaEnum = smo.enumerator(iEnumIndex);

    /* Prepare result: */
    QStringList result;
    /* Handle MenuApplicationActionType_All enum-value: */
    if (types == UIExtraDataMetaDefs::MenuApplicationActionType_All)
        result << gpConverter->toInternalString(types);
    else
    {
        /* Handle other enum-values: */
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Get iterated enum-value: */
            const UIExtraDataMetaDefs::MenuApplicationActionType enumValue =
                static_cast<UIExtraDataMetaDefs::MenuApplicationActionType>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip MenuApplicationActionType_Invalid & MenuApplicationActionType_All enum-values: */
            if (enumValue == UIExtraDataMetaDefs::MenuApplicationActionType_Invalid ||
                enumValue == UIExtraDataMetaDefs::MenuApplicationActionType_All)
                continue;
            if (types & enumValue)
                result << gpConverter->toInternalString(enumValue);
        }
    }
    /* Save result: */
    setExtraDataStringList(GUI_RestrictedRuntimeApplicationMenuActions, result, uID);
}

UIExtraDataMetaDefs::RuntimeMenuMachineActionType UIExtraDataManager::restrictedRuntimeMenuMachineActionTypes(const QUuid &uID)
{
    /* Prepare result: */
    UIExtraDataMetaDefs::RuntimeMenuMachineActionType result = UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Invalid;
    /* Get restricted runtime-machine-menu action-types: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedRuntimeMachineMenuActions, uID))
    {
        UIExtraDataMetaDefs::RuntimeMenuMachineActionType value = gpConverter->fromInternalString<UIExtraDataMetaDefs::RuntimeMenuMachineActionType>(strValue);
        /* Since empty value has default restriction, we are supporting special 'Nothing' value: */
        if (value == UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Nothing)
        {
            result = UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Nothing;
            break;
        }
        if (value != UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Invalid)
            result = static_cast<UIExtraDataMetaDefs::RuntimeMenuMachineActionType>(result | value);
    }
    /* Defaults: */
    if (result == UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Invalid)
    {
        result = static_cast<UIExtraDataMetaDefs::RuntimeMenuMachineActionType>(result | UIExtraDataMetaDefs::RuntimeMenuMachineActionType_SaveState);
        result = static_cast<UIExtraDataMetaDefs::RuntimeMenuMachineActionType>(result | UIExtraDataMetaDefs::RuntimeMenuMachineActionType_PowerOff);
    }
    /* Return result: */
    return result;
}

void UIExtraDataManager::setRestrictedRuntimeMenuMachineActionTypes(UIExtraDataMetaDefs::RuntimeMenuMachineActionType types, const QUuid &uID)
{
    /* We have RuntimeMenuMachineActionType enum registered, so we can enumerate it: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("RuntimeMenuMachineActionType");
    QMetaEnum metaEnum = smo.enumerator(iEnumIndex);

    /* Prepare result: */
    QStringList result;
    /* Handle RuntimeMenuMachineActionType_All enum-value: */
    if (types == UIExtraDataMetaDefs::RuntimeMenuMachineActionType_All)
        result << gpConverter->toInternalString(types);
    else
    {
        /* Handle other enum-values: */
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Get iterated enum-value: */
            const UIExtraDataMetaDefs::RuntimeMenuMachineActionType enumValue =
                static_cast<UIExtraDataMetaDefs::RuntimeMenuMachineActionType>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip RuntimeMenuMachineActionType_Invalid, RuntimeMenuMachineActionType_Nothing & RuntimeMenuMachineActionType_All enum-values: */
            if (enumValue == UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Invalid ||
                enumValue == UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Nothing ||
                enumValue == UIExtraDataMetaDefs::RuntimeMenuMachineActionType_All)
                continue;
            if (types & enumValue)
                result << gpConverter->toInternalString(enumValue);
        }
    }
    /* Since empty value has default restriction, we are supporting special 'Nothing' value: */
    if (result.isEmpty())
        result << gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuMachineActionType_Nothing);
    /* Save result: */
    setExtraDataStringList(GUI_RestrictedRuntimeMachineMenuActions, result, uID);
}

UIExtraDataMetaDefs::RuntimeMenuViewActionType UIExtraDataManager::restrictedRuntimeMenuViewActionTypes(const QUuid &uID)
{
    /* Prepare result: */
    UIExtraDataMetaDefs::RuntimeMenuViewActionType result = UIExtraDataMetaDefs::RuntimeMenuViewActionType_Invalid;
    /* Get restricted runtime-view-menu action-types: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedRuntimeViewMenuActions, uID))
    {
        UIExtraDataMetaDefs::RuntimeMenuViewActionType value = gpConverter->fromInternalString<UIExtraDataMetaDefs::RuntimeMenuViewActionType>(strValue);
        if (value != UIExtraDataMetaDefs::RuntimeMenuViewActionType_Invalid)
            result = static_cast<UIExtraDataMetaDefs::RuntimeMenuViewActionType>(result | value);
    }
    /* Return result: */
    return result;
}

void UIExtraDataManager::setRestrictedRuntimeMenuViewActionTypes(UIExtraDataMetaDefs::RuntimeMenuViewActionType types, const QUuid &uID)
{
    /* We have RuntimeMenuViewActionType enum registered, so we can enumerate it: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("RuntimeMenuViewActionType");
    QMetaEnum metaEnum = smo.enumerator(iEnumIndex);

    /* Prepare result: */
    QStringList result;
    /* Handle RuntimeMenuViewActionType_All enum-value: */
    if (types == UIExtraDataMetaDefs::RuntimeMenuViewActionType_All)
        result << gpConverter->toInternalString(types);
    else
    {
        /* Handle other enum-values: */
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Get iterated enum-value: */
            const UIExtraDataMetaDefs::RuntimeMenuViewActionType enumValue =
                static_cast<UIExtraDataMetaDefs::RuntimeMenuViewActionType>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip RuntimeMenuViewActionType_Invalid & RuntimeMenuViewActionType_All enum-values: */
            if (enumValue == UIExtraDataMetaDefs::RuntimeMenuViewActionType_Invalid ||
                enumValue == UIExtraDataMetaDefs::RuntimeMenuViewActionType_All)
                continue;
            if (types & enumValue)
                result << gpConverter->toInternalString(enumValue);
        }
    }
    /* Save result: */
    setExtraDataStringList(GUI_RestrictedRuntimeViewMenuActions, result, uID);
}

UIExtraDataMetaDefs::RuntimeMenuInputActionType UIExtraDataManager::restrictedRuntimeMenuInputActionTypes(const QUuid &uID)
{
    /* Prepare result: */
    UIExtraDataMetaDefs::RuntimeMenuInputActionType result = UIExtraDataMetaDefs::RuntimeMenuInputActionType_Invalid;
    /* Get restricted runtime-machine-menu action-types: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedRuntimeInputMenuActions, uID))
    {
        UIExtraDataMetaDefs::RuntimeMenuInputActionType value = gpConverter->fromInternalString<UIExtraDataMetaDefs::RuntimeMenuInputActionType>(strValue);
        if (value != UIExtraDataMetaDefs::RuntimeMenuInputActionType_Invalid)
            result = static_cast<UIExtraDataMetaDefs::RuntimeMenuInputActionType>(result | value);
    }
    /* Return result: */
    return result;
}

void UIExtraDataManager::setRestrictedRuntimeMenuInputActionTypes(UIExtraDataMetaDefs::RuntimeMenuInputActionType types, const QUuid &uID)
{
    /* We have RuntimeMenuInputActionType enum registered, so we can enumerate it: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("RuntimeMenuInputActionType");
    QMetaEnum metaEnum = smo.enumerator(iEnumIndex);

    /* Prepare result: */
    QStringList result;
    /* Handle RuntimeMenuInputActionType_All enum-value: */
    if (types == UIExtraDataMetaDefs::RuntimeMenuInputActionType_All)
        result << gpConverter->toInternalString(types);
    else
    {
        /* Handle other enum-values: */
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Get iterated enum-value: */
            const UIExtraDataMetaDefs::RuntimeMenuInputActionType enumValue =
                static_cast<UIExtraDataMetaDefs::RuntimeMenuInputActionType>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip RuntimeMenuInputActionType_Invalid & RuntimeMenuInputActionType_All enum-values: */
            if (enumValue == UIExtraDataMetaDefs::RuntimeMenuInputActionType_Invalid ||
                enumValue == UIExtraDataMetaDefs::RuntimeMenuInputActionType_All)
                continue;
            if (types & enumValue)
                result << gpConverter->toInternalString(enumValue);
        }
    }
    /* Save result: */
    setExtraDataStringList(GUI_RestrictedRuntimeInputMenuActions, result, uID);
}

UIExtraDataMetaDefs::RuntimeMenuDevicesActionType UIExtraDataManager::restrictedRuntimeMenuDevicesActionTypes(const QUuid &uID)
{
    /* Prepare result: */
    UIExtraDataMetaDefs::RuntimeMenuDevicesActionType result = UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Invalid;
    /* Get restricted runtime-devices-menu action-types: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedRuntimeDevicesMenuActions, uID))
    {
        UIExtraDataMetaDefs::RuntimeMenuDevicesActionType value = gpConverter->fromInternalString<UIExtraDataMetaDefs::RuntimeMenuDevicesActionType>(strValue);
        /* Since empty value has default restriction, we are supporting special 'Nothing' value: */
        if (value == UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Nothing)
        {
            result = UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Nothing;
            break;
        }
        if (value != UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Invalid)
            result = static_cast<UIExtraDataMetaDefs::RuntimeMenuDevicesActionType>(result | value);
    }
    /* Defaults: */
    if (result == UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Invalid)
    {
        result = static_cast<UIExtraDataMetaDefs::RuntimeMenuDevicesActionType>(result | UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_HardDrives);
    }
    /* Return result: */
    return result;
}

void UIExtraDataManager::setRestrictedRuntimeMenuDevicesActionTypes(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType types, const QUuid &uID)
{
    /* We have RuntimeMenuDevicesActionType enum registered, so we can enumerate it: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("RuntimeMenuDevicesActionType");
    QMetaEnum metaEnum = smo.enumerator(iEnumIndex);

    /* Prepare result: */
    QStringList result;
    /* Handle RuntimeMenuDevicesActionType_All enum-value: */
    if (types == UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_All)
        result << gpConverter->toInternalString(types);
    else
    {
        /* Handle other enum-values: */
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Get iterated enum-value: */
            const UIExtraDataMetaDefs::RuntimeMenuDevicesActionType enumValue =
                static_cast<UIExtraDataMetaDefs::RuntimeMenuDevicesActionType>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip RuntimeMenuDevicesActionType_Invalid, RuntimeMenuDevicesActionType_Nothing & RuntimeMenuDevicesActionType_All enum-values: */
            if (enumValue == UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Invalid ||
                enumValue == UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Nothing ||
                enumValue == UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_All)
                continue;
            if (types & enumValue)
                result << gpConverter->toInternalString(enumValue);
        }
    }
    /* Since empty value has default restriction, we are supporting special 'Nothing' value: */
    if (result.isEmpty())
        result << gpConverter->toInternalString(UIExtraDataMetaDefs::RuntimeMenuDevicesActionType_Nothing);
    /* Save result: */
    setExtraDataStringList(GUI_RestrictedRuntimeDevicesMenuActions, result, uID);
}

#ifdef VBOX_WITH_DEBUGGER_GUI
UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType UIExtraDataManager::restrictedRuntimeMenuDebuggerActionTypes(const QUuid &uID)
{
    /* Prepare result: */
    UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType result = UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_Invalid;
    /* Get restricted runtime-debugger-menu action-types: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedRuntimeDebuggerMenuActions, uID))
    {
        UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType value = gpConverter->fromInternalString<UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType>(strValue);
        if (value != UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_Invalid)
            result = static_cast<UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType>(result | value);
    }
    /* Return result: */
    return result;
}

void UIExtraDataManager::setRestrictedRuntimeMenuDebuggerActionTypes(UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType types, const QUuid &uID)
{
    /* We have RuntimeMenuDebuggerActionType enum registered, so we can enumerate it: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("RuntimeMenuDebuggerActionType");
    QMetaEnum metaEnum = smo.enumerator(iEnumIndex);

    /* Prepare result: */
    QStringList result;
    /* Handle RuntimeMenuDebuggerActionType_All enum-value: */
    if (types == UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_All)
        result << gpConverter->toInternalString(types);
    else
    {
        /* Handle other enum-values: */
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Get iterated enum-value: */
            const UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType enumValue =
                static_cast<UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip RuntimeMenuDebuggerActionType_Invalid & RuntimeMenuDebuggerActionType_All enum-values: */
            if (enumValue == UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_Invalid ||
                enumValue == UIExtraDataMetaDefs::RuntimeMenuDebuggerActionType_All)
                continue;
            if (types & enumValue)
                result << gpConverter->toInternalString(enumValue);
        }
    }
    /* Save result: */
    setExtraDataStringList(GUI_RestrictedRuntimeDebuggerMenuActions, result, uID);
}
#endif /* VBOX_WITH_DEBUGGER_GUI */

#ifdef VBOX_WS_MAC
UIExtraDataMetaDefs::MenuWindowActionType UIExtraDataManager::restrictedRuntimeMenuWindowActionTypes(const QUuid &uID)
{
    /* Prepare result: */
    UIExtraDataMetaDefs::MenuWindowActionType result = UIExtraDataMetaDefs::MenuWindowActionType_Invalid;
    /* Get restricted runtime-window-menu action-types: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedRuntimeWindowMenuActions, uID))
    {
        UIExtraDataMetaDefs::MenuWindowActionType value = gpConverter->fromInternalString<UIExtraDataMetaDefs::MenuWindowActionType>(strValue);
        if (value != UIExtraDataMetaDefs::MenuWindowActionType_Invalid)
            result = static_cast<UIExtraDataMetaDefs::MenuWindowActionType>(result | value);
    }
    /* Return result: */
    return result;
}

void UIExtraDataManager::setRestrictedRuntimeMenuWindowActionTypes(UIExtraDataMetaDefs::MenuWindowActionType types, const QUuid &uID)
{
    /* We have MenuWindowActionType enum registered, so we can enumerate it: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("MenuWindowActionType");
    QMetaEnum metaEnum = smo.enumerator(iEnumIndex);

    /* Prepare result: */
    QStringList result;
    /* Handle MenuWindowActionType_All enum-value: */
    if (types == UIExtraDataMetaDefs::MenuWindowActionType_All)
        result << gpConverter->toInternalString(types);
    else
    {
        /* Handle other enum-values: */
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Get iterated enum-value: */
            const UIExtraDataMetaDefs::MenuWindowActionType enumValue =
                static_cast<const UIExtraDataMetaDefs::MenuWindowActionType>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip MenuWindowActionType_Invalid & MenuWindowActionType_All enum-values: */
            if (enumValue == UIExtraDataMetaDefs::MenuWindowActionType_Invalid ||
                enumValue == UIExtraDataMetaDefs::MenuWindowActionType_All)
                continue;
            if (types & enumValue)
                result << gpConverter->toInternalString(enumValue);
        }
    }
    /* Save result: */
    setExtraDataStringList(GUI_RestrictedRuntimeWindowMenuActions, result, uID);
}
#endif /* VBOX_WS_MAC */

UIExtraDataMetaDefs::MenuHelpActionType UIExtraDataManager::restrictedRuntimeMenuHelpActionTypes(const QUuid &uID)
{
    /* Prepare result: */
    UIExtraDataMetaDefs::MenuHelpActionType result = UIExtraDataMetaDefs::MenuHelpActionType_Invalid;
    /* Get restricted runtime-help-menu action-types: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedRuntimeHelpMenuActions, uID))
    {
        UIExtraDataMetaDefs::MenuHelpActionType value = gpConverter->fromInternalString<UIExtraDataMetaDefs::MenuHelpActionType>(strValue);
        if (value != UIExtraDataMetaDefs::MenuHelpActionType_Invalid)
            result = static_cast<UIExtraDataMetaDefs::MenuHelpActionType>(result | value);
    }
    /* Return result: */
    return result;
}

void UIExtraDataManager::setRestrictedRuntimeMenuHelpActionTypes(UIExtraDataMetaDefs::MenuHelpActionType types, const QUuid &uID)
{
    /* We have MenuHelpActionType enum registered, so we can enumerate it: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("MenuHelpActionType");
    QMetaEnum metaEnum = smo.enumerator(iEnumIndex);

    /* Prepare result: */
    QStringList result;
    /* Handle MenuHelpActionType_All enum-value: */
    if (types == UIExtraDataMetaDefs::MenuHelpActionType_All)
        result << gpConverter->toInternalString(types);
    else
    {
        /* Handle other enum-values: */
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Get iterated enum-value: */
            const UIExtraDataMetaDefs::MenuHelpActionType enumValue =
                static_cast<UIExtraDataMetaDefs::MenuHelpActionType>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip MenuHelpActionType_Invalid && MenuHelpActionType_All enum-values: */
            if (enumValue == UIExtraDataMetaDefs::MenuHelpActionType_Invalid ||
                enumValue == UIExtraDataMetaDefs::MenuHelpActionType_All)
                continue;
            if (types & enumValue)
                result << gpConverter->toInternalString(enumValue);
        }
    }
    /* Save result: */
    setExtraDataStringList(GUI_RestrictedRuntimeHelpMenuActions, result, uID);
}

UIVisualStateType UIExtraDataManager::restrictedVisualStates(const QUuid &uID)
{
    /* Prepare result: */
    UIVisualStateType result = UIVisualStateType_Invalid;
    /* Get restricted visual-state-types: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedVisualStates, uID))
    {
        UIVisualStateType value = gpConverter->fromInternalString<UIVisualStateType>(strValue);
        if (value != UIVisualStateType_Invalid)
            result = static_cast<UIVisualStateType>(result | value);
    }
    /* Return result: */
    return result;
}

UIVisualStateType UIExtraDataManager::requestedVisualState(const QUuid &uID)
{
    if (isFeatureAllowed(GUI_Fullscreen, uID)) return UIVisualStateType_Fullscreen;
    if (isFeatureAllowed(GUI_Seamless, uID)) return UIVisualStateType_Seamless;
    if (isFeatureAllowed(GUI_Scale, uID)) return UIVisualStateType_Scale;
    return UIVisualStateType_Normal;
}

void UIExtraDataManager::setRequestedVisualState(UIVisualStateType visualState, const QUuid &uID)
{
    setExtraDataString(GUI_Fullscreen, toFeatureAllowed(visualState == UIVisualStateType_Fullscreen), uID);
    setExtraDataString(GUI_Seamless, toFeatureAllowed(visualState == UIVisualStateType_Seamless), uID);
    setExtraDataString(GUI_Scale, toFeatureAllowed(visualState == UIVisualStateType_Scale), uID);
}

#ifdef VBOX_WS_NIX
bool UIExtraDataManager::legacyFullscreenModeRequested()
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_Fullscreen_LegacyMode);
}

bool UIExtraDataManager::distinguishMachineWindowGroups(const QUuid &uID)
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_DistinguishMachineWindowGroups, uID);
}

void UIExtraDataManager::setDistinguishMachineWindowGroups(const QUuid &uID, bool fEnabled)
{
    /* 'True' if feature allowed, null-string otherwise: */
    setExtraDataString(GUI_DistinguishMachineWindowGroups, toFeatureAllowed(fEnabled), uID);
}
#endif /* VBOX_WS_NIX */

bool UIExtraDataManager::guestScreenAutoResizeEnabled(const QUuid &uID)
{
    /* 'True' unless feature restricted: */
    return !isFeatureRestricted(GUI_AutoresizeGuest, uID);
}

void UIExtraDataManager::setGuestScreenAutoResizeEnabled(bool fEnabled, const QUuid &uID)
{
    /* 'False' if feature restricted, null-string otherwise: */
    setExtraDataString(GUI_AutoresizeGuest, toFeatureRestricted(!fEnabled), uID);
}

bool UIExtraDataManager::lastGuestScreenVisibilityStatus(ulong uScreenIndex, const QUuid &uID)
{
    /* Not for primary screen: */
    if (uScreenIndex == 0)
        return true;

    /* Compose corresponding key: */
    const QString strKey = extraDataKeyPerScreen(GUI_LastVisibilityStatusForGuestScreen, uScreenIndex);

    /* 'False' unless feature allowed: */
    return isFeatureAllowed(strKey, uID);
}

void UIExtraDataManager::setLastGuestScreenVisibilityStatus(ulong uScreenIndex, bool fEnabled, const QUuid &uID)
{
    /* Not for primary screen: */
    if (uScreenIndex == 0)
        return;

    /* Compose corresponding key: */
    const QString strKey = extraDataKeyPerScreen(GUI_LastVisibilityStatusForGuestScreen, uScreenIndex);

    /* 'True' if feature allowed, null-string otherwise: */
    return setExtraDataString(strKey, toFeatureAllowed(fEnabled), uID);
}

QSize UIExtraDataManager::lastGuestScreenSizeHint(ulong uScreenIndex, const QUuid &uID)
{
    /* Choose corresponding key: */
    const QString strKey = extraDataKeyPerScreen(GUI_LastGuestSizeHint, uScreenIndex);

    /* Get corresponding extra-data: */
    const QStringList data = extraDataStringList(strKey, uID);

    /* Parse loaded data: */
    int iW = 0, iH = 0;
    bool fOk = data.size() == 2;
    do
    {
        if (!fOk) break;
        iW = data[0].toInt(&fOk);
        if (!fOk) break;
        iH = data[1].toInt(&fOk);
    }
    while (0);

    /* Return size (loaded or invalid): */
    return fOk ? QSize(iW, iH) : QSize();
}

void UIExtraDataManager::setLastGuestScreenSizeHint(ulong uScreenIndex, const QSize &sizeHint, const QUuid &uID)
{
    /* Choose corresponding key: */
    const QString strKey = extraDataKeyPerScreen(GUI_LastGuestSizeHint, uScreenIndex);

    /* Serialize passed values: */
    QStringList data;
    data << QString::number(sizeHint.width());
    data << QString::number(sizeHint.height());

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(strKey, data, uID);
}

int UIExtraDataManager::hostScreenForPassedGuestScreen(int iGuestScreenIndex, const QUuid &uID)
{
    /* Choose corresponding key: */
    const QString strKey = extraDataKeyPerScreen(GUI_VirtualScreenToHostScreen, iGuestScreenIndex, true);

    /* Get value and convert it to index: */
    const QString strValue = extraDataString(strKey, uID);
    bool fOk = false;
    const int iHostScreenIndex = strValue.toULong(&fOk);

    /* Return corresponding index: */
    return fOk ? iHostScreenIndex : -1;
}

void UIExtraDataManager::setHostScreenForPassedGuestScreen(int iGuestScreenIndex, int iHostScreenIndex, const QUuid &uID)
{
    /* Choose corresponding key: */
    const QString strKey = extraDataKeyPerScreen(GUI_VirtualScreenToHostScreen, iGuestScreenIndex, true);

    /* Save passed index under corresponding value: */
    setExtraDataString(strKey, iHostScreenIndex != -1 ? QString::number(iHostScreenIndex) : QString(), uID);
}

bool UIExtraDataManager::autoMountGuestScreensEnabled(const QUuid &uID)
{
    /* Show only if 'allowed' flag is set: */
    return isFeatureAllowed(GUI_AutomountGuestScreens, uID);
}

#ifndef VBOX_WS_MAC
bool UIExtraDataManager::miniToolbarEnabled(const QUuid &uID)
{
    /* 'True' unless feature restricted: */
    return !isFeatureRestricted(GUI_ShowMiniToolBar, uID);
}

void UIExtraDataManager::setMiniToolbarEnabled(bool fEnabled, const QUuid &uID)
{
    /* 'False' if feature restricted, null-string otherwise: */
    setExtraDataString(GUI_ShowMiniToolBar, toFeatureRestricted(!fEnabled), uID);
}

bool UIExtraDataManager::autoHideMiniToolbar(const QUuid &uID)
{
    /* 'True' unless feature restricted: */
    return !isFeatureRestricted(GUI_MiniToolBarAutoHide, uID);
}

void UIExtraDataManager::setAutoHideMiniToolbar(bool fAutoHide, const QUuid &uID)
{
    /* 'False' if feature restricted, null-string otherwise: */
    setExtraDataString(GUI_MiniToolBarAutoHide, toFeatureRestricted(!fAutoHide), uID);
}

Qt::AlignmentFlag UIExtraDataManager::miniToolbarAlignment(const QUuid &uID)
{
    /* Return Qt::AlignBottom unless MiniToolbarAlignment_Top specified separately: */
    switch (gpConverter->fromInternalString<MiniToolbarAlignment>(extraDataString(GUI_MiniToolBarAlignment, uID)))
    {
        case MiniToolbarAlignment_Top: return Qt::AlignTop;
        default: break;
    }
    return Qt::AlignBottom;
}

void UIExtraDataManager::setMiniToolbarAlignment(Qt::AlignmentFlag alignment, const QUuid &uID)
{
    /* Remove record unless Qt::AlignTop specified separately: */
    switch (alignment)
    {
        case Qt::AlignTop: setExtraDataString(GUI_MiniToolBarAlignment, gpConverter->toInternalString(MiniToolbarAlignment_Top), uID); return;
        default: break;
    }
    setExtraDataString(GUI_MiniToolBarAlignment, QString(), uID);
}
#endif /* VBOX_WS_MAC */

bool UIExtraDataManager::statusBarEnabled(const QUuid &uID)
{
    /* 'True' unless feature restricted: */
    return !isFeatureRestricted(GUI_StatusBar_Enabled, uID);
}

void UIExtraDataManager::setStatusBarEnabled(bool fEnabled, const QUuid &uID)
{
    /* 'False' if feature restricted, null-string otherwise: */
    setExtraDataString(GUI_StatusBar_Enabled, toFeatureRestricted(!fEnabled), uID);
}

bool UIExtraDataManager::statusBarContextMenuEnabled(const QUuid &uID)
{
    /* 'True' unless feature restricted: */
    return !isFeatureRestricted(GUI_StatusBar_ContextMenu_Enabled, uID);
}

void UIExtraDataManager::setStatusBarContextMenuEnabled(bool fEnabled, const QUuid &uID)
{
    /* 'False' if feature restricted, null-string otherwise: */
    setExtraDataString(GUI_StatusBar_ContextMenu_Enabled, toFeatureRestricted(!fEnabled), uID);
}

QList<IndicatorType> UIExtraDataManager::restrictedStatusBarIndicators(const QUuid &uID)
{
    /* Prepare result: */
    QList<IndicatorType> result;
    /* Get restricted status-bar indicators: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedStatusBarIndicators, uID))
    {
        const IndicatorType value = gpConverter->fromInternalString<IndicatorType>(strValue);
        if (value != IndicatorType_Invalid && !result.contains(value))
            result << value;
    }
    /* Return result: */
    return result;
}

void UIExtraDataManager::setRestrictedStatusBarIndicators(const QList<IndicatorType> &list, const QUuid &uID)
{
    /* Parse passed list: */
    QStringList data;
    foreach (const IndicatorType &indicatorType, list)
        data << gpConverter->toInternalString(indicatorType);

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_RestrictedStatusBarIndicators, data, uID);
}

QList<IndicatorType> UIExtraDataManager::statusBarIndicatorOrder(const QUuid &uID)
{
    /* Prepare result: */
    QList<IndicatorType> result;
    /* Get status-bar indicator order: */
    foreach (const QString &strValue, extraDataStringList(GUI_StatusBar_IndicatorOrder, uID))
    {
        const IndicatorType value = gpConverter->fromInternalString<IndicatorType>(strValue);
        if (value != IndicatorType_Invalid && !result.contains(value))
            result << value;
    }

    /* We should update the list with missing indicators: */
    for (int i = (int)IndicatorType_Invalid; i < (int)IndicatorType_Max; ++i)
    {
        /* Skip the IndicatorType_Invalid (we used it as start of this loop): */
        if (i == (int)IndicatorType_Invalid)
            continue;
        /* Skip the IndicatorType_KeyboardExtension (special handling): */
        if (i == (int)IndicatorType_KeyboardExtension)
            continue;

        /* Get the current one: */
        const IndicatorType enmCurrent = (IndicatorType)i;

        /* Skip the current one if it's present: */
        if (result.contains(enmCurrent))
            continue;

        /* Let's find the first of those which stays before it and is not missing: */
        IndicatorType enmPrevious = (IndicatorType)(enmCurrent - 1);
        while (enmPrevious != IndicatorType_Invalid && !result.contains(enmPrevious))
            enmPrevious = (IndicatorType)(enmPrevious - 1);

        /* Calculate position to insert missing one: */
        const int iInsertPosition = enmPrevious != IndicatorType_Invalid
                                  ? result.indexOf(enmPrevious) + 1
                                  : 0;

        /* Finally insert missing indicator at required position: */
        result.insert(iInsertPosition, enmCurrent);
    }

    /* Return result: */
    return result;
}

void UIExtraDataManager::setStatusBarIndicatorOrder(const QList<IndicatorType> &list, const QUuid &uID)
{
    /* Parse passed list: */
    QStringList data;
    foreach (const IndicatorType &indicatorType, list)
        data << gpConverter->toInternalString(indicatorType);

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_StatusBar_IndicatorOrder, data, uID);
}

#ifdef VBOX_WS_MAC
bool UIExtraDataManager::realtimeDockIconUpdateEnabled(const QUuid &uID)
{
    /* 'True' unless feature restricted: */
    return !isFeatureRestricted(GUI_RealtimeDockIconUpdateEnabled, uID);
}

void UIExtraDataManager::setRealtimeDockIconUpdateEnabled(bool fEnabled, const QUuid &uID)
{
    /* 'False' if feature restricted, null-string otherwise: */
    setExtraDataString(GUI_RealtimeDockIconUpdateEnabled, toFeatureRestricted(!fEnabled), uID);
}

int UIExtraDataManager::realtimeDockIconUpdateMonitor(const QUuid &uID)
{
    return extraDataString(GUI_RealtimeDockIconUpdateMonitor, uID).toInt();
}

void UIExtraDataManager::setRealtimeDockIconUpdateMonitor(int iIndex, const QUuid &uID)
{
    setExtraDataString(GUI_RealtimeDockIconUpdateMonitor, iIndex ? QString::number(iIndex) : QString(), uID);
}

bool UIExtraDataManager::dockIconDisableOverlay(const QUuid &uID)
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_DockIconDisableOverlay, uID);
}

void UIExtraDataManager::setDockIconDisableOverlay(bool fDisabled, const QUuid &uID)
{
    /* 'True' if feature allowed, null-string otherwise: */
    setExtraDataString(GUI_DockIconDisableOverlay, toFeatureAllowed(fDisabled), uID);
}
#endif /* VBOX_WS_MAC */

bool UIExtraDataManager::passCADtoGuest(const QUuid &uID)
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_PassCAD, uID);
}

MouseCapturePolicy UIExtraDataManager::mouseCapturePolicy(const QUuid &uID)
{
    return gpConverter->fromInternalString<MouseCapturePolicy>(extraDataString(GUI_MouseCapturePolicy, uID));
}

GuruMeditationHandlerType UIExtraDataManager::guruMeditationHandlerType(const QUuid &uID)
{
    return gpConverter->fromInternalString<GuruMeditationHandlerType>(extraDataString(GUI_GuruMeditationHandler, uID));
}

bool UIExtraDataManager::hidLedsSyncState(const QUuid &uID)
{
    /* 'True' unless feature restricted: */
    return !isFeatureRestricted(GUI_HidLedsSync, uID);
}

double UIExtraDataManager::scaleFactor(const QUuid &uID, const int uScreenIndex)
{
    /* Get corresponding extra-data for this machine: */
    QStringList data = extraDataStringList(GUI_ScaleFactor, uID);

    /* 1.0 is default scale factor: */
    if (data.size() == 0)
        return 1.0;

    int index = uScreenIndex;
    /* use the 0th. scale factor in case we dont have a scale factor for @p uScreenIndex: */
    if (data.size() <= uScreenIndex)
        index = 0;

    bool fOk = false;
    double scaleFactor = data[index].toDouble(&fOk);
    if (!fOk)
        return 1.0;
    return scaleFactor;
}

QList<double> UIExtraDataManager::scaleFactors(const QUuid &uID)
{
    /* Look for the scale factor for this machine first: */
    QStringList data = extraDataStringList(GUI_ScaleFactor, uID);

    QList<double> scaleFactorList;
    /* 1.0 is default scale factor: */
    if (data.size() == 0)
    {
        scaleFactorList.append(1.0);
        return scaleFactorList;
    }

    bool fOk = false;
    double scaleFactor;
    for (int i = 0; i < data.size(); ++i)
    {
        scaleFactor = data[i].toDouble(&fOk);
        if (!fOk)
            scaleFactor = 1.0;
        scaleFactorList.append(scaleFactor);
    }
    return scaleFactorList;
}

void UIExtraDataManager::setScaleFactor(double dScaleFactor, const QUuid &uID, const int uScreenIndex)
{
    QStringList data = extraDataStringList(GUI_ScaleFactor, uID);

    /* Just make sure that we have corresponding data item: */
    if (data.size() <= uScreenIndex)
    {
        int listSize = data.size();
        for (int i = listSize; i <= uScreenIndex; ++i)
            data.append(QString::number(1.0));
    }

    data[uScreenIndex] = QString::number(dScaleFactor);
    setExtraDataStringList(GUI_ScaleFactor, data, uID);
}

void UIExtraDataManager::setScaleFactors(const QList<double> &scaleFactors, const QUuid &uID)
{
    QStringList data;
    for (int i = 0; i < scaleFactors.size(); ++i)
        data.append(QString::number(scaleFactors[i]));
    setExtraDataStringList(GUI_ScaleFactor, data, uID);
}

ScalingOptimizationType UIExtraDataManager::scalingOptimizationType(const QUuid &uID)
{
    return gpConverter->fromInternalString<ScalingOptimizationType>(extraDataString(GUI_Scaling_Optimization, uID));
}

QRect UIExtraDataManager::sessionInformationDialogGeometry(QWidget *pWidget, QWidget *pParentWidget)
{
    return dialogGeometry(GUI_SessionInformationDialogGeometry, pWidget, pParentWidget);
}

bool UIExtraDataManager::sessionInformationDialogShouldBeMaximized()
{
    return dialogShouldBeMaximized(GUI_SessionInformationDialogGeometry);
}

void UIExtraDataManager::setSessionInformationDialogGeometry(const QRect &geometry, bool fMaximized)
{
    /* Serialize passed values: */
    QStringList data;
    data << QString::number(geometry.x());
    data << QString::number(geometry.y());
    data << QString::number(geometry.width());
    data << QString::number(geometry.height());
    if (fMaximized)
        data << GUI_Geometry_State_Max;

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_SessionInformationDialogGeometry, data);
}


void UIExtraDataManager::setGuestControlProcessControlSplitterHints(const QList<int> &hints)
{
    QStringList data;
    data << (hints.size() > 0 ? QString::number(hints[0]) : QString());
    data << (hints.size() > 1 ? QString::number(hints[1]) : QString());

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_GuestControl_ProcessControlSplitterHints, data);
}

QList<int> UIExtraDataManager::guestControlProcessControlSplitterHints()
{
    /* Get corresponding extra-data: */
    const QStringList data = extraDataStringList(GUI_GuestControl_ProcessControlSplitterHints);

    /* Parse loaded data: */
    QList<int> hints;
    hints << (data.size() > 0 ? data[0].toInt() : 0);
    hints << (data.size() > 1 ? data[1].toInt() : 0);

    /* Return hints: */
    return hints;
}

QRect UIExtraDataManager::fileManagerDialogGeometry(QWidget *pWidget, QWidget *pParentWidget)
{
    return dialogGeometry(GUI_GuestControl_FileManagerDialogGeometry, pWidget, pParentWidget);
}

bool UIExtraDataManager::fileManagerDialogShouldBeMaximized()
{
    return dialogShouldBeMaximized(GUI_GuestControl_FileManagerDialogGeometry);
}

void UIExtraDataManager::setFileManagerDialogGeometry(const QRect &geometry, bool fMaximized)
{
    setDialogGeometry(GUI_GuestControl_FileManagerDialogGeometry, geometry, fMaximized);
}

void UIExtraDataManager::setFileManagerVisiblePanels(const QStringList &panelNameList)
{
    setExtraDataStringList(GUI_GuestControl_FileManagerVisiblePanels, panelNameList);
}

QStringList UIExtraDataManager::fileManagerVisiblePanels()
{
    return extraDataStringList(GUI_GuestControl_FileManagerVisiblePanels);
}

QRect UIExtraDataManager::softKeyboardDialogGeometry(QWidget *pWidget, QWidget *pParentWidget, const QRect &defaultGeometry)
{
    return dialogGeometry(GUI_SoftKeyboard_DialogGeometry, pWidget, pParentWidget, defaultGeometry);
}

void UIExtraDataManager::setSoftKeyboardDialogGeometry(const QRect &geometry, bool fMaximized)
{
    setDialogGeometry(GUI_SoftKeyboard_DialogGeometry, geometry, fMaximized);
}

bool UIExtraDataManager::softKeyboardDialogShouldBeMaximized()
{
    return dialogShouldBeMaximized(GUI_SoftKeyboard_DialogGeometry);
}

void UIExtraDataManager::setSoftKeyboardOptions(bool fHideNumPad, bool fHideOSMenuKeys, bool fMultimediaKeys)
{
    QStringList data;

    if (fHideNumPad)
        data << GUI_SoftKeyboard_HideNumPad;
    if (fHideOSMenuKeys)
        data << GUI_SoftKeyboard_HideOSMenuKeys;
    if (fMultimediaKeys)
        data << GUI_SoftKeyboard_HideMultimediaKeys;

    setExtraDataStringList(GUI_SoftKeyboard_Options, data);
}

void UIExtraDataManager::softKeyboardOptions(bool &fOutHideNumPad, bool &fOutHideOSMenuKeys, bool &fOutHideMultimediaKeys)
{
    fOutHideNumPad = false;
    fOutHideOSMenuKeys = false;
    const QStringList data = extraDataStringList(GUI_SoftKeyboard_Options);
    for (int i = 0; i < data.size(); ++i)
    {
        if (data[i] == GUI_SoftKeyboard_HideNumPad)
            fOutHideNumPad = true;
        if (data[i] == GUI_SoftKeyboard_HideOSMenuKeys)
            fOutHideOSMenuKeys = true;
        if (data[i] == GUI_SoftKeyboard_HideMultimediaKeys)
            fOutHideMultimediaKeys = true;
    }
}

void UIExtraDataManager::setSoftKeyboardColorTheme(const QStringList &colorStringList)
{
    setExtraDataStringList(GUI_SoftKeyboard_ColorTheme, colorStringList);
}

QStringList UIExtraDataManager::softKeyboardColorTheme()
{
    return extraDataStringList(GUI_SoftKeyboard_ColorTheme);
}

void UIExtraDataManager::setSoftKeyboardSelectedColorTheme(const QString &strColorThemeName)
{
    setExtraDataString(GUI_SoftKeyboard_SelectedColorTheme, strColorThemeName);
}

QString UIExtraDataManager::softKeyboardSelectedColorTheme()
{
    return extraDataString(GUI_SoftKeyboard_SelectedColorTheme);
}

void UIExtraDataManager::setSoftKeyboardSelectedLayout(const QUuid &uLayoutUid)
{
    setExtraDataString(GUI_SoftKeyboard_SelectedLayout, uLayoutUid.toString());
}

QUuid UIExtraDataManager::softKeyboardSelectedLayout()
{
    return QUuid(extraDataString(GUI_SoftKeyboard_SelectedLayout));
}

void UIExtraDataManager::setFileManagerOptions(bool fListDirectoriesFirst,
                                               bool fShowDeleteConfirmation,
                                               bool fShowHumanReadableSizes,
                                               bool fShowHiddenObjects)
{
    /* Serialize passed values: */
    QStringList data;

    if (fListDirectoriesFirst)
        data << GUI_GuestControl_FileManagerListDirectoriesFirst;
    if (fShowDeleteConfirmation)
        data << GUI_GuestControl_FileManagerShowDeleteConfirmation;
    if (fShowHumanReadableSizes)
        data << GUI_GuestControl_FileManagerShowHumanReadableSizes;
    if (fShowHiddenObjects)
        data << GUI_GuestControl_FileManagerShowHiddenObjects;

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_GuestControl_FileManagerOptions, data);
}

bool UIExtraDataManager::fileManagerListDirectoriesFirst()
{
    const QStringList data = extraDataStringList(GUI_GuestControl_FileManagerOptions);
    for (int i = 0; i < data.size(); ++i)
    {
        if (data[i] == GUI_GuestControl_FileManagerListDirectoriesFirst)
            return true;
    }
    return false;
}

bool UIExtraDataManager::fileManagerShowDeleteConfirmation()
{
    const QStringList data = extraDataStringList(GUI_GuestControl_FileManagerOptions);
    for (int i = 0; i < data.size(); ++i)
    {
        if (data[i] == GUI_GuestControl_FileManagerShowDeleteConfirmation)
            return true;
    }
    return false;
}

bool UIExtraDataManager::fileManagerShowHumanReadableSizes()
{
    const QStringList data = extraDataStringList(GUI_GuestControl_FileManagerOptions);
    for (int i = 0; i < data.size(); ++i)
    {
        if (data[i] == GUI_GuestControl_FileManagerShowHumanReadableSizes)
            return true;
    }
    return false;
}

bool UIExtraDataManager::fileManagerShowHiddenObjects()
{
    const QStringList data = extraDataStringList(GUI_GuestControl_FileManagerOptions);
    for (int i = 0; i < data.size(); ++i)
    {
        if (data[i] == GUI_GuestControl_FileManagerShowHiddenObjects)
            return true;
    }
    return false;
}

QRect UIExtraDataManager::guestProcessControlDialogGeometry(QWidget *pWidget, QWidget *pParentWidget, const QRect &defaultGeometry)
{
    return dialogGeometry(GUI_GuestControl_ProcessControlDialogGeometry, pWidget, pParentWidget, defaultGeometry);
}

bool UIExtraDataManager::guestProcessControlDialogShouldBeMaximized()
{
    return dialogShouldBeMaximized(GUI_GuestControl_ProcessControlDialogGeometry);
}

void UIExtraDataManager::setGuestProcessControlDialogGeometry(const QRect &geometry, bool fMaximized)
{
    setDialogGeometry(GUI_GuestControl_ProcessControlDialogGeometry, geometry, fMaximized);
}

MachineCloseAction UIExtraDataManager::defaultMachineCloseAction(const QUuid &uID)
{
    return gpConverter->fromInternalString<MachineCloseAction>(extraDataString(GUI_DefaultCloseAction, uID));
}

MachineCloseAction UIExtraDataManager::restrictedMachineCloseActions(const QUuid &uID)
{
    /* Prepare result: */
    MachineCloseAction result = MachineCloseAction_Invalid;
    /* Get restricted machine-close-actions: */
    foreach (const QString &strValue, extraDataStringList(GUI_RestrictedCloseActions, uID))
    {
        MachineCloseAction value = gpConverter->fromInternalString<MachineCloseAction>(strValue);
        if (value != MachineCloseAction_Invalid)
            result = static_cast<MachineCloseAction>(result | value);
    }
    /* Return result: */
    return result;
}

MachineCloseAction UIExtraDataManager::lastMachineCloseAction(const QUuid &uID)
{
    return gpConverter->fromInternalString<MachineCloseAction>(extraDataString(GUI_LastCloseAction, uID));
}

void UIExtraDataManager::setLastMachineCloseAction(MachineCloseAction machineCloseAction, const QUuid &uID)
{
    setExtraDataString(GUI_LastCloseAction, gpConverter->toInternalString(machineCloseAction), uID);
}

QString UIExtraDataManager::machineCloseHookScript(const QUuid &uID)
{
    return extraDataString(GUI_CloseActionHook, uID);
}

bool UIExtraDataManager::discardStateOnPowerOff(const QUuid &uID)
{
    /* 'False' unless feature allowed: */
    return isFeatureAllowed(GUI_DiscardStateOnPowerOff, uID);
}

#ifdef VBOX_WITH_DEBUGGER_GUI
QString UIExtraDataManager::debugFlagValue(const QString &strDebugFlagKey)
{
    return extraDataString(strDebugFlagKey).toLower().trimmed();
}
#endif /* VBOX_WITH_DEBUGGER_GUI */

#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
QRect UIExtraDataManager::extraDataManagerGeometry(QWidget *pWidget, QWidget *pParentWidget)
{
    return dialogGeometry(GUI_ExtraDataManager_Geometry, pWidget, pParentWidget);
}

bool UIExtraDataManager::extraDataManagerShouldBeMaximized()
{
    return dialogShouldBeMaximized(GUI_ExtraDataManager_Geometry);
}

void UIExtraDataManager::setExtraDataManagerGeometry(const QRect &geometry, bool fMaximized)
{
    /* Serialize passed values: */
    QStringList data;
    data << QString::number(geometry.x());
    data << QString::number(geometry.y());
    data << QString::number(geometry.width());
    data << QString::number(geometry.height());
    if (fMaximized)
        data << GUI_Geometry_State_Max;

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_ExtraDataManager_Geometry, data);
}

QList<int> UIExtraDataManager::extraDataManagerSplitterHints(QWidget *pWidget)
{
    /* Get corresponding extra-data: */
    const QStringList data = extraDataStringList(GUI_ExtraDataManager_SplitterHints);

    /* Parse loaded data: */
    int iLeft = 0, iRight = 0;
    bool fOk = data.size() == 2;
    do
    {
        if (!fOk) break;
        iLeft = data[0].toInt(&fOk);
        if (!fOk) break;
        iRight = data[1].toInt(&fOk);
    }
    while (0);

    /* Prepare hints (loaded or adviced): */
    QList<int> hints;
    if (fOk)
    {
        hints << iLeft;
        hints << iRight;
    }
    else
    {
        hints << (int)(pWidget->width() * .9 * (1.0 / 3));
        hints << (int)(pWidget->width() * .9 * (2.0 / 3));
    }

    /* Return hints: */
    return hints;
}

void UIExtraDataManager::setExtraDataManagerSplitterHints(const QList<int> &hints)
{
    /* Parse passed hints: */
    QStringList data;
    data << (hints.size() > 0 ? QString::number(hints[0]) : QString());
    data << (hints.size() > 1 ? QString::number(hints[1]) : QString());

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_ExtraDataManager_SplitterHints, data);
}
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */

QRect UIExtraDataManager::logWindowGeometry(QWidget *pWidget, QWidget *pParentWidget, const QRect &defaultGeometry)
{
    return dialogGeometry(GUI_LogWindowGeometry, pWidget, pParentWidget, defaultGeometry);
}

bool UIExtraDataManager::logWindowShouldBeMaximized()
{
    return dialogShouldBeMaximized(GUI_LogWindowGeometry);
}

void UIExtraDataManager::setLogWindowGeometry(const QRect &geometry, bool fMaximized)
{
    /* Serialize passed values: */
    QStringList data;
    data << QString::number(geometry.x());
    data << QString::number(geometry.y());
    data << QString::number(geometry.width());
    data << QString::number(geometry.height());
    if (fMaximized)
        data << GUI_Geometry_State_Max;

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_LogWindowGeometry, data);
}

void UIExtraDataManager::setLogViweverOptions(const QFont &font, bool wrapLines, bool showLineNumbers)
{
    /* Serialize passed values: */
    QStringList data;
    data << font.family();
    /* Make sure that we have some non-empty string as font style name: */
    QString strStyleName = font.styleName();
    if (strStyleName.isEmpty())
        data << GUI_LogViewerNoFontStyleName;
    else
        data << font.styleName();
    data << QString::number(font.pointSize());

    if (wrapLines)
        data << GUI_LogViewerWrapLinesEnabled;
    if (!showLineNumbers)
        data << GUI_LogViewerShowLineNumbersDisabled;

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_LogViewerOptions, data);
}

bool UIExtraDataManager::logViewerWrapLines()
{
    const QStringList data = extraDataStringList(GUI_LogViewerOptions);
    for (int i = 0; i < data.size(); ++i)
    {
        if (data[i] == GUI_LogViewerWrapLinesEnabled)
            return true;
    }
    return false;
}

bool UIExtraDataManager::logViewerShowLineNumbers()
{
    const QStringList data = extraDataStringList(GUI_LogViewerOptions);
    for (int i = 0; i < data.size(); ++i)
    {
        if (data[i] == GUI_LogViewerShowLineNumbersDisabled)
            return false;
    }
    return true;
}

QFont UIExtraDataManager::logViewerFont()
{
    const QStringList data = extraDataStringList(GUI_LogViewerOptions);
    if (data.size() < 3)
        return QFont();
    QString strFamily = data[0];
    QString strStyleName = data[1];
    if (strStyleName == GUI_LogViewerNoFontStyleName)
        strStyleName.clear();
    bool fOk = false;
    int iFontSize = data[2].toInt(&fOk);
    if (!fOk)
        iFontSize = 9;
    return QFontDatabase::font(strFamily, strStyleName, iFontSize);
}

void UIExtraDataManager::setHelpBrowserLastUrlList(const QStringList &urlList)
{
    setExtraDataStringList(GUI_HelpBrowser_LastURLList, urlList);
}

QStringList UIExtraDataManager::helpBrowserLastUrlList()
{
    return extraDataStringList(GUI_HelpBrowser_LastURLList);
}

void UIExtraDataManager::setHelpBrowserZoomPercentage(int iZoomPercentage)
{
    setExtraDataString(GUI_HelpBrowser_ZoomPercentage, QString::number(iZoomPercentage));
}

int UIExtraDataManager::helpBrowserZoomPercentage()
{
    return extraDataString(GUI_HelpBrowser_ZoomPercentage).toInt();
}

QRect UIExtraDataManager::helpBrowserDialogGeometry(QWidget *pWidget, QWidget *pParentWidget, const QRect &defaultGeometry)
{
    return dialogGeometry(GUI_HelpBrowser_DialogGeometry, pWidget, pParentWidget, defaultGeometry);
}

void UIExtraDataManager::setHelpBrowserDialogGeometry(const QRect &geometry, bool fMaximized)
{
    /* Serialize passed values: */
    QStringList data;
    data << QString::number(geometry.x());
    data << QString::number(geometry.y());
    data << QString::number(geometry.width());
    data << QString::number(geometry.height());
    if (fMaximized)
        data << GUI_Geometry_State_Max;

    /* Re-cache corresponding extra-data: */
    setExtraDataStringList(GUI_HelpBrowser_DialogGeometry, data);
}

bool UIExtraDataManager::helpBrowserDialogShouldBeMaximized()
{
    return dialogShouldBeMaximized(GUI_HelpBrowser_DialogGeometry);
}

void UIExtraDataManager::setHelpBrowserBookmarks(const QStringList &bookmarks)
{
    setExtraDataStringList(GUI_HelpBrowser_Bookmarks, bookmarks);
}

QStringList UIExtraDataManager::helpBrowserBookmarks()
{
    return extraDataStringList(GUI_HelpBrowser_Bookmarks);
}

void UIExtraDataManager::setVMActivityOverviewHiddenColumnList(const QStringList &hiddenColumnList)
{
    setExtraDataStringList(GUI_VMActivityOverview_HiddenColumns, hiddenColumnList);
}

QStringList UIExtraDataManager::VMActivityOverviewHiddenColumnList()
{
    return extraDataStringList(GUI_VMActivityOverview_HiddenColumns);
}

bool UIExtraDataManager::VMActivityOverviewShowAllMachines()
{
    return isFeatureAllowed(GUI_VMActivityOverview_ShowAllMachines);
}

void UIExtraDataManager::setVMActivityOverviewShowAllMachines(bool fShow)
{
    setExtraDataString(GUI_VMActivityOverview_ShowAllMachines, toFeatureAllowed(fShow));
}

void UIExtraDataManager::setVMActivityMonitorDataSeriesColors(const QStringList &colorList)
{
    setExtraDataStringList(GUI_VMActivityMonitor_DataSeriesColors, colorList);
}

QStringList UIExtraDataManager::VMActivityMonitorDataSeriesColors()
{
    return extraDataStringList(GUI_VMActivityMonitor_DataSeriesColors);
}

bool UIExtraDataManager::VMActivityMonitorShowVMExits()
{
    return isFeatureAllowed(GUI_VMActivityMonitor_ShowVMExits);
}

void UIExtraDataManager::setVMActivityMonitorShowVMExits(bool fShow)
{
    setExtraDataString(GUI_VMActivityMonitor_ShowVMExits, toFeatureAllowed(fShow));
}

QRect UIExtraDataManager::mediumSelectorDialogGeometry(QWidget *pWidget, QWidget *pParentWidget, const QRect &defaultGeometry)
{
    return dialogGeometry(GUI_MediumSelector_DialogGeometry, pWidget, pParentWidget, defaultGeometry);
}

void UIExtraDataManager::setMediumSelectorDialogGeometry(const QRect &geometry, bool fMaximized)
{
    setDialogGeometry(GUI_MediumSelector_DialogGeometry, geometry, fMaximized);
}

bool UIExtraDataManager::mediumSelectorDialogShouldBeMaximized()
{
    return dialogShouldBeMaximized(GUI_MediumSelector_DialogGeometry);
}

void UIExtraDataManager::sltExtraDataChange(const QUuid &uMachineID, const QString &strKey, const QString &strValue)
{
    /* Re-cache value only if uMachineID known already: */
    if (m_data.contains(uMachineID))
    {
        if (!strValue.isEmpty())
            m_data[uMachineID][strKey] = strValue;
        else
            m_data[uMachineID].remove(strKey);
    }

    /* Global extra-data 'change' event: */
    if (uMachineID == GlobalID)
    {
        if (strKey.startsWith("GUI/"))
        {
            /* Notification-center alignment? */
            if (strKey == GUI_NotificationCenter_Alignment)
                emit sigNotificationCenterAlignmentChange();
            /* Notification-center order? */
            if (strKey == GUI_NotificationCenter_Order)
                emit sigNotificationCenterOrderChange();
            /* Settings expert mode? */
            if (strKey == GUI_Settings_ExpertMode)
                emit sigSettingsExpertModeChange();
            /* Language changed? */
            if (strKey == GUI_LanguageID)
                emit sigLanguageChange(extraDataString(strKey));
            /* Selector UI shortcut changed? */
            else if (strKey == GUI_Input_SelectorShortcuts)
                emit sigSelectorUIShortcutChange();
            /* Runtime UI shortcut changed? */
            else if (strKey == GUI_Input_MachineShortcuts)
                emit sigRuntimeUIShortcutChange();
            /* Runtime UI host-key combintation changed? */
            else if (strKey == GUI_Input_HostKeyCombination)
                emit sigRuntimeUIHostKeyCombinationChange();
            /* Cloud Profile Manager restrictions changed: */
            else if (strKey == GUI_CloudProfileManager_Restrictions)
                emit sigCloudProfileManagerRestrictionChange();
            /* Cloud Console Manager data changed: */
            else if (strKey.startsWith(QString(GUI_CloudConsoleManager_Application) + '/'))
                emit sigCloudConsoleManagerDataChange();
            /* Cloud Console Manager restrictions changed: */
            else if (strKey == GUI_CloudConsoleManager_Restrictions)
                emit sigCloudConsoleManagerRestrictionChange();
#if defined(VBOX_WS_NIX) || defined(VBOX_WS_WIN)
            else if (strKey == GUI_DisableHostScreenSaver)
                emit sigDisableHostScreenSaverStateChange(isFeatureAllowed(GUI_DisableHostScreenSaver));
#endif
            /* Details categories: */
            else if (strKey == GUI_Details_Elements)
                emit sigDetailsCategoriesChange();
            /* Details options: */
            else if (strKey.startsWith(QString(GUI_Details_Elements) + '/'))
            {
                QString strLeftover = strKey;
                strLeftover.remove(QString(GUI_Details_Elements) + '/');
                const DetailsElementType enmType = gpConverter->fromInternalString<DetailsElementType>(strLeftover);
                if (enmType != DetailsElementType_Invalid)
                    emit sigDetailsOptionsChange(enmType);
            }
            /* Font scaling factor has changed: */
            else if (strKey == GUI_FontScaleFactor)
                emit sigFontScaleFactorChanged(fontScaleFactor());
        }
    }
    /* Machine extra-data 'change' event: */
    else
    {
        /* Current VM only: */
        if (   uiCommon().uiType() == UIType_RuntimeUI
            && uMachineID == uiCommon().managedVMUuid())
        {
            /* HID LEDs sync state changed (allowed if not restricted)? */
            if (strKey == GUI_HidLedsSync)
                emit sigHidLedsSyncStateChange(!isFeatureRestricted(strKey, uMachineID));
#ifdef VBOX_WS_MAC
            /* 'Dock icon' appearance changed (allowed if not restricted)? */
            else if (strKey == GUI_RealtimeDockIconUpdateEnabled ||
                     strKey == GUI_RealtimeDockIconUpdateMonitor)
                emit sigDockIconAppearanceChange(!isFeatureRestricted(strKey, uMachineID));
            /* 'Dock icon overlay' appearance changed (restricted if not allowed)? */
            else if (strKey == GUI_DockIconDisableOverlay)
                emit sigDockIconOverlayAppearanceChange(isFeatureAllowed(strKey, uMachineID));
#endif /* VBOX_WS_MAC */
        }

        /* Menu-bar configuration change: */
        if (
#ifndef VBOX_WS_MAC
            strKey == GUI_MenuBar_Enabled ||
#endif /* !VBOX_WS_MAC */
            strKey == GUI_RestrictedRuntimeMenus ||
            strKey == GUI_RestrictedRuntimeApplicationMenuActions ||
            strKey == GUI_RestrictedRuntimeMachineMenuActions ||
            strKey == GUI_RestrictedRuntimeViewMenuActions ||
            strKey == GUI_RestrictedRuntimeInputMenuActions ||
            strKey == GUI_RestrictedRuntimeDevicesMenuActions ||
#ifdef VBOX_WITH_DEBUGGER_GUI
            strKey == GUI_RestrictedRuntimeDebuggerMenuActions ||
#endif /* VBOX_WITH_DEBUGGER_GUI */
#ifdef VBOX_WS_MAC
            strKey == GUI_RestrictedRuntimeWindowMenuActions ||
#endif /* VBOX_WS_MAC */
            strKey == GUI_RestrictedRuntimeHelpMenuActions)
            emit sigMenuBarConfigurationChange(uMachineID);
        /* Status-bar configuration change: */
        else if (strKey == GUI_StatusBar_Enabled ||
                 strKey == GUI_RestrictedStatusBarIndicators ||
                 strKey == GUI_StatusBar_IndicatorOrder)
            emit sigStatusBarConfigurationChange(uMachineID);
        /* Visual state change: */
        else if (strKey == GUI_Fullscreen ||
                 strKey == GUI_Seamless ||
                 strKey == GUI_Scale)
            emit sigVisualStateChange(uMachineID);
        /* Scale-factor change: */
        else if (strKey == GUI_ScaleFactor)
            emit sigScaleFactorChange(uMachineID);
        /* Scaling optimization type change: */
        else if (strKey == GUI_Scaling_Optimization)
            emit sigScalingOptimizationTypeChange(uMachineID);
    }

    /* Notify listeners: */
    emit sigExtraDataChange(uMachineID, strKey, strValue);
}

void UIExtraDataManager::prepare()
{
    /* Prepare global extra-data map: */
    prepareGlobalExtraDataMap();
    /* Prepare extra-data event-handler: */
    prepareExtraDataEventHandler();
    /* Prepare extra-data settings: */
    prepareExtraDataSettings();
}

void UIExtraDataManager::prepareGlobalExtraDataMap()
{
    /* Get CVirtualBox: */
    CVirtualBox vbox = gpGlobalSession->virtualBox();

    /* Make sure at least empty map is created: */
    m_data[GlobalID] = ExtraDataMap();

    /* Load global extra-data map: */
    foreach (const QString &strKey, vbox.GetExtraDataKeys())
        m_data[GlobalID][strKey] = vbox.GetExtraData(strKey);
}

void UIExtraDataManager::prepareExtraDataEventHandler()
{
    /* Create extra-data event-handler: */
    m_pHandler = new UIExtraDataEventHandler(this);
    /* Configure extra-data event-handler: */
    AssertPtrReturnVoid(m_pHandler);
    {
        /* Create queued (async) connections for signals of event proxy object: */
        connect(m_pHandler, &UIExtraDataEventHandler::sigExtraDataChange,
                this, &UIExtraDataManager::sltExtraDataChange,
                Qt::QueuedConnection);
    }
}

void UIExtraDataManager::prepareExtraDataSettings()
{
    /* For the case when Expert mode is undecided yet: */
    if (extraDataString(UIExtraDataDefs::GUI_Settings_ExpertMode).isNull())
    {
        /* If there are VMs already, mark the mode as Expert one, we've decided
         * that forcing all users to Basic mode by default is overkill a bit. */
        if (!gpGlobalSession->virtualBox().GetMachines().isEmpty())
            setSettingsInExpertMode(true);
    }
}

#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
void UIExtraDataManager::cleanupWindow()
{
    delete m_pWindow;
}
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */

void UIExtraDataManager::cleanupExtraDataEventHandler()
{
    /* Destroy extra-data event-handler: */
    delete m_pHandler;
    m_pHandler = 0;
}

void UIExtraDataManager::cleanup()
{
    /* Cleanup extra-data event-handler: */
    cleanupExtraDataEventHandler();
#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
    /* Cleanup window: */
    cleanupWindow();
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */
}

#ifdef VBOX_GUI_WITH_EXTRADATA_MANAGER_UI
void UIExtraDataManager::open(QWidget *pCenterWidget)
{
    /* If necessary: */
    if (!m_pWindow)
    {
        /* Create window: */
        m_pWindow = new UIExtraDataManagerWindow(pCenterWidget);
        /* Configure window connections: */
        connect(this, &UIExtraDataManager::sigExtraDataMapAcknowledging,
                m_pWindow.data(), &UIExtraDataManagerWindow::sltExtraDataMapAcknowledging);
        connect(this, &UIExtraDataManager::sigExtraDataChange,
                m_pWindow.data(), &UIExtraDataManagerWindow::sltExtraDataChange);
    }
    /* Show and raise window: */
    m_pWindow->showAndRaise(pCenterWidget);
}
#endif /* VBOX_GUI_WITH_EXTRADATA_MANAGER_UI */

QString UIExtraDataManager::extraDataStringUnion(const QString &strKey, const QUuid &uID)
{
    /* If passed uID differs from the GlobalID: */
    if (uID != GlobalID)
    {
        /* Search through the machine extra-data first: */
        MapOfExtraDataMaps::const_iterator itMap = m_data.constFind(uID);
        /* Hot-load machine extra-data map if necessary: */
        if (itMap == m_data.constEnd())
        {
            hotloadMachineExtraDataMap(uID);
            itMap = m_data.constFind(uID);
        }
        if (itMap != m_data.constEnd())
        {
            /* Return string if present in the map: */
            ExtraDataMap::const_iterator itValue = itMap->constFind(strKey);
            if (itValue != itMap->constEnd())
                return *itValue;
        }
    }

    /* Search through the global extra-data finally: */
    MapOfExtraDataMaps::const_iterator itMap = m_data.constFind(GlobalID);
    if (itMap != m_data.constEnd())
    {
        /* Return string if present in the map: */
        ExtraDataMap::const_iterator itValue = itMap->constFind(strKey);
        if (itValue != itMap->constEnd())
            return *itValue;
    }

    /* Not found, return null string: */
    return QString();
}

bool UIExtraDataManager::isFeatureAllowed(const QString &strKey, const QUuid &uID /* = GlobalID */)
{
    /* Get the value. Return 'false' if not found: */
    const QString strValue = extraDataStringUnion(strKey, uID);
    if (strValue.isNull())
        return false;

    /* Check corresponding value: */
    return    strValue.compare("true", Qt::CaseInsensitive) == 0
           || strValue.compare("yes", Qt::CaseInsensitive) == 0
           || strValue.compare("on", Qt::CaseInsensitive) == 0
           || strValue == "1";
}

bool UIExtraDataManager::isFeatureRestricted(const QString &strKey, const QUuid &uID /* = GlobalID */)
{
    /* Get the value. Return 'false' if not found: */
    const QString strValue = extraDataStringUnion(strKey, uID);
    if (strValue.isNull())
        return false;

    /* Check corresponding value: */
    return    strValue.compare("false", Qt::CaseInsensitive) == 0
           || strValue.compare("no", Qt::CaseInsensitive) == 0
           || strValue.compare("off", Qt::CaseInsensitive) == 0
           || strValue == "0";
}

QString UIExtraDataManager::toFeatureState(bool fState)
{
    return fState ? QString("true") : QString("false");
}

QString UIExtraDataManager::toFeatureAllowed(bool fAllowed)
{
    return fAllowed ? QString("true") : QString();
}

QString UIExtraDataManager::toFeatureRestricted(bool fRestricted)
{
    return fRestricted ? QString("false") : QString();
}

void UIExtraDataManager::setDialogGeometry(const QString &strKey, const QRect &geometry, bool fMaximized)
{
    /* Serialize passed values: */
    QStringList data;
    data << QString::number(geometry.x());
    data << QString::number(geometry.y());
    data << QString::number(geometry.width());
    data << QString::number(geometry.height());
    if (fMaximized)
        data << GUI_Geometry_State_Max;

    /* Save corresponding extra-data: */
    setExtraDataStringList(strKey, data);
}

QRect UIExtraDataManager::dialogGeometry(const QString &strKey,
                                         QWidget *pWidget,
                                         QWidget *pParentWidget /* = 0 */,
                                         const QRect &defaultGeometry /* = QRect() */)
{
    /* Get corresponding extra-data: */
    const QStringList data = extraDataStringList(strKey);

    /* Parse loaded data: */
    int iX = 0, iY = 0, iW = 0, iH = 0;
    bool fOk = data.size() >= 4;
    do
    {
        if (!fOk) break;
        iX = data[0].toInt(&fOk);
        if (!fOk) break;
        iY = data[1].toInt(&fOk);
        if (!fOk) break;
        iW = data[2].toInt(&fOk);
        if (!fOk) break;
        iH = data[3].toInt(&fOk);
    }
    while (0);

    /* Get available-geometry [of screen with point (iX, iY) if possible]: */
    const QRect availableGeometry = fOk ? gpDesktop->availableGeometry(QPoint(iX, iY)) :
                                          gpDesktop->availableGeometry();

    /* Use geometry (loaded or default): */
    QRect geometry = fOk
                   ? QRect(iX, iY, iW, iH)
                   : !defaultGeometry.isNull()
                   ? defaultGeometry
                   : QRect(QPoint(0, 0), availableGeometry.size() * .50 /* % */);

    /* Take hint-widget into account: */
    if (pWidget)
        geometry.setSize(geometry.size().expandedTo(pWidget->minimumSizeHint()));

    /* As a fallback, move default-geometry to pParentWidget' geometry center: */
    if (!fOk && pParentWidget)
        geometry.moveCenter(pParentWidget->geometry().center());
    /* As final fallback, move default-geometry to available-geometry' center: */
    else if (!fOk)
        geometry.moveCenter(availableGeometry.center());

    /* In Windows Qt fails to reposition out of screen window properly, so doing it ourselves: */
#ifdef VBOX_WS_WIN
    /* Make sure resulting geometry is within current bounds: */
    if (!availableGeometry.contains(geometry))
        geometry = UIDesktopWidgetWatchdog::getNormalized(geometry, QRegion(availableGeometry));
#endif /* VBOX_WS_WIN */

    /* Return result: */
    return geometry;
}

bool UIExtraDataManager::dialogShouldBeMaximized(const QString &strKey)
{
    /* Get corresponding extra-data: */
    const QStringList data = extraDataStringList(strKey);

    /* Make sure 5th item has required value: */
    return data.size() == 5 && data[4] == GUI_Geometry_State_Max;
}

/* static */
QString UIExtraDataManager::extraDataKeyPerScreen(const QString &strBase, ulong uScreenIndex, bool fSameRuleForPrimary /* = false */)
{
    return fSameRuleForPrimary || uScreenIndex ? strBase + QString::number(uScreenIndex) : strBase;
}

#include "UIExtraDataManager.moc"
