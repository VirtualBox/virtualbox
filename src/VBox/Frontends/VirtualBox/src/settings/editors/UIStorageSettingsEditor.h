/* $Id: UIStorageSettingsEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIStorageSettingsEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIStorageSettingsEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIStorageSettingsEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QUuid>

/* GUI includes: */
#include "UIEditor.h"
#include "UIMediumDefs.h"
#include "UISettingsDefs.h"

/* COM includes: */
#include "KChipsetType.h"
#include "KPlatformArchitecture.h"
#include "KStorageControllerType.h"

/* Using declarations: */
using namespace UISettingsDefs;

/* Forward declarations: */
class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;
class QILabel;
class QILabelSeparator;
class QISplitter;
class QIToolBar;
class QIToolButton;
class QITreeView;
class UIActionPool;
class UIMediumIDHolder;
class StorageModel;

/** Storage Attachment data structure. */
struct UIDataStorageAttachment
{
    /** Constructs data. */
    UIDataStorageAttachment()
        : m_enmDeviceType(KDeviceType_Null)
        , m_iPort(-1)
        , m_iDevice(-1)
        , m_uMediumId(QUuid())
        , m_fPassthrough(false)
        , m_fTempEject(false)
        , m_fNonRotational(false)
        , m_fHotPluggable(false)
        , m_strKey(QString())
    {}

    /** Returns whether @a another passed data is equal to this one. */
    bool equal(const UIDataStorageAttachment &another) const
    {
        return true
               && (m_enmDeviceType == another.m_enmDeviceType)
               && (m_iPort == another.m_iPort)
               && (m_iDevice == another.m_iDevice)
               && (m_uMediumId == another.m_uMediumId)
               && (m_fPassthrough == another.m_fPassthrough)
               && (m_fTempEject == another.m_fTempEject)
               && (m_fNonRotational == another.m_fNonRotational)
               && (m_fHotPluggable == another.m_fHotPluggable)
               && (m_strKey == another.m_strKey)
               ;
    }

    /** Returns whether @a another passed data is equal to this one. */
    bool operator==(const UIDataStorageAttachment &another) const { return equal(another); }
    /** Returns whether @a another passed data is different from this one. */
    bool operator!=(const UIDataStorageAttachment &another) const { return !equal(another); }

    /** Holds the device type. */
    KDeviceType  m_enmDeviceType;
    /** Holds the port. */
    qint32       m_iPort;
    /** Holds the device. */
    qint32       m_iDevice;
    /** Holds the medium ID. */
    QUuid        m_uMediumId;
    /** Holds whether the attachment being passed through. */
    bool         m_fPassthrough;
    /** Holds whether the attachment being temporarily eject. */
    bool         m_fTempEject;
    /** Holds whether the attachment is solid-state. */
    bool         m_fNonRotational;
    /** Holds whether the attachment is hot-pluggable. */
    bool         m_fHotPluggable;
    /** Holds the unique key. */
    QString      m_strKey;
};

/** Storage Controller data structure. */
struct UIDataStorageController
{
    /** Constructs data. */
    UIDataStorageController()
        : m_strName(QString())
        , m_enmBus(KStorageBus_Null)
        , m_enmType(KStorageControllerType_Null)
        , m_uPortCount(0)
        , m_fUseHostIOCache(false)
        , m_strKey(QString())
    {}

    /** Returns whether @a another passed data is equal to this one. */
    bool equal(const UIDataStorageController &another) const
    {
        return true
               && (m_strName == another.m_strName)
               && (m_enmBus == another.m_enmBus)
               && (m_enmType == another.m_enmType)
               && (m_uPortCount == another.m_uPortCount)
               && (m_fUseHostIOCache == another.m_fUseHostIOCache)
               && (m_strKey == another.m_strKey)
               ;
    }

    /** Returns whether @a another passed data is equal to this one. */
    bool operator==(const UIDataStorageController &another) const { return equal(another); }
    /** Returns whether @a another passed data is different from this one. */
    bool operator!=(const UIDataStorageController &another) const { return !equal(another); }

    /** Holds the name. */
    QString                 m_strName;
    /** Holds the bus. */
    KStorageBus             m_enmBus;
    /** Holds the type. */
    KStorageControllerType  m_enmType;
    /** Holds the port count. */
    uint                    m_uPortCount;
    /** Holds whether the controller uses host IO cache. */
    bool                    m_fUseHostIOCache;
    /** Holds the unique key. */
    QString                 m_strKey;
};

/** UIEditor sub-class used as acceleration features editor. */
class SHARED_LIBRARY_STUFF UIStorageSettingsEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notifies listeners about value change. */
    void sigValueChanged();

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIStorageSettingsEditor(QWidget *pParent = 0);
    /** Destructs editor. */
    virtual ~UIStorageSettingsEditor() RT_OVERRIDE;

    /** Returns platform architecture. */
    KPlatformArchitecture arch() const;

    /** Defines @a pActionPool. */
    void setActionPool(UIActionPool *pActionPool);

    /** Defines machine @a uMachineId. */
    void setMachineId(const QUuid &uMachineId);
    /** Defines machine @a strName. */
    void setMachineName(const QString &strName);
    /** Defines machine settings @a strFilePath. */
    void setMachineSettingsFilePath(const QString &strFilePath);
    /** Defines machine guest OS type @a strId. */
    void setMachineGuestOSTypeId(const QString &strId);

    /** Defines @a enmConfigurationAccessLevel. */
    void setConfigurationAccessLevel(ConfigurationAccessLevel enmConfigurationAccessLevel);

    /** Defines chipset @a enmType. */
    void setChipsetType(KChipsetType enmType);
    /** Returns chipset type. */
    KChipsetType chipsetType() const;

    /** Returns current controller types. */
    QMap<KStorageBus, int> currentControllerTypes() const;
    /** Returns maximum controller types. */
    QMap<KStorageBus, int> maximumControllerTypes() const;

    /** Defines a set of @a controllers and @a attachments. */
    void setValue(const QList<UIDataStorageController> &controllers,
                  const QList<QList<UIDataStorageAttachment> > &attachments);
    /** Acquires a set of @a controllers and @a attachments. */
    void getValue(QList<UIDataStorageController> &controllers,
                  QList<QList<UIDataStorageAttachment> > &attachments);

protected:

    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;

    /** Handles filter change. */
    virtual void handleFilterChange() RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;
    /** Handles enumeration of medium with @a uMediumId. */
    void sltHandleMediumEnumerated(const QUuid &uMediumId);
    /** Handles removing of medium with @a uMediumId. */
    void sltHandleMediumDeleted(const QUuid &uMediumId);

    /** Handles command to add controller. */
    void sltAddController();
    /** Handles command to add PIIX3 controller. */
    void sltAddControllerPIIX3();
    /** Handles command to add PIIX4 controller. */
    void sltAddControllerPIIX4();
    /** Handles command to add ICH6 controller. */
    void sltAddControllerICH6();
    /** Handles command to add AHCI controller. */
    void sltAddControllerAHCI();
    /** Handles command to add LsiLogic controller. */
    void sltAddControllerLsiLogic();
    /** Handles command to add BusLogic controller. */
    void sltAddControllerBusLogic();
    /** Handles command to add Floppy controller. */
    void sltAddControllerFloppy();
    /** Handles command to add SAS controller. */
    void sltAddControllerLsiLogicSAS();
    /** Handles command to add USB controller. */
    void sltAddControllerUSB();
    /** Handles command to add NVMe controller. */
    void sltAddControllerNVMe();
    /** Handles command to add virtio-scsi controller. */
    void sltAddControllerVirtioSCSI();
    /** Handles command to remove controller. */
    void sltRemoveController();

    /** Handles command to add attachment. */
    void sltAddAttachment();
    /** Handles command to add HD attachment. */
    void sltAddAttachmentHD();
    /** Handles command to add CD attachment. */
    void sltAddAttachmentCD();
    /** Handles command to add FD attachment. */
    void sltAddAttachmentFD();
    /** Handles command to remove attachment. */
    void sltRemoveAttachment();

    /** Loads information from model to widgets. */
    void sltGetInformation();
    /** Saves information from widgets to model. */
    void sltSetInformation();

    /** Prepares 'Open Medium' menu. */
    void sltPrepareOpenMediumMenu();
    /** Unmounts current device. */
    void sltUnmountDevice();
    /** Mounts existing medium. */
    void sltChooseExistingMedium();
    /** Mounts a medium from a disk file. */
    void sltChooseDiskFile();
    /** Mounts existing host-drive. */
    void sltChooseHostDrive();
    /** Mounts one of recent media. */
    void sltChooseRecentMedium();

    /** Updates action states. */
    void sltUpdateActionStates();

    /** Handles row insertion into @a parentIndex on @a iPosition. */
    void sltHandleRowInsertion(const QModelIndex &parentIndex, int iPosition);
    /** Handles row removal. */
    void sltHandleRowRemoval();

    /** Handles current item change. */
    void sltHandleCurrentItemChange();

    /** Handles context menu request for @a position. */
    void sltHandleContextMenuRequest(const QPoint &position);

    /** Handles item branch drawing with @a pPainter, within @a rect for item with @a index. */
    void sltHandleDrawItemBranches(QPainter *pPainter, const QRect &rect, const QModelIndex &index);

    /** Handles mouse-move @a pEvent. */
    void sltHandleMouseMove(QMouseEvent *pEvent);
    /** Handles mouse-click @a pEvent. */
    void sltHandleMouseClick(QMouseEvent *pEvent);
    /** Handles mouse-release @a pEvent. */
    void sltHandleMouseRelease(QMouseEvent *pEvent);

    /** Handles drag-enter @a pEvent. */
    void sltHandleDragEnter(QDragEnterEvent *pEvent);
    /** Handles drag-move @a pEvent. */
    void sltHandleDragMove(QDragMoveEvent *pEvent);
    /** Handles drag-drop @a pEvent. */
    void sltHandleDragDrop(QDropEvent *pEvent);

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepares left pane. */
    void prepareLeftPane();
    /** Prepares tree view. */
    void prepareTreeView();
    /** Prepares toolbar. */
    void prepareToolBar();
    /** Prepares right pane. */
    void prepareRightPane();
    /** Prepares empty widget. */
    void prepareEmptyWidget();
    /** Prepares controller widget. */
    void prepareControllerWidget();
    /** Prepares attachment widget. */
    void prepareAttachmentWidget();
    /** Prepares connections. */
    void prepareConnections();

    /** Cleanups all. */
    void cleanup();

    /** Updates root and current index most suitable for this editor. */
    void updateRootAndCurrentIndexes();

    /** Adds controller with @a strName, @a enmBus and @a enmType. */
    void addControllerWrapper(const QString &strName, KStorageBus enmBus, KStorageControllerType enmType);
    /** Adds attachment with @a enmDevice. */
    void addAttachmentWrapper(KDeviceType enmDevice);

    /** Generates unique controller name based on passed @a strTemplate. */
    QString generateUniqueControllerName(const QString &strTemplate) const;

    /** Returns current devices count for passed @a enmType. */
    uint32_t deviceCount(KDeviceType enmType) const;

    /** Adds 'Choose/Create Medium' action into passed @a pOpenMediumMenu under passed @a strActionName. */
    void addChooseExistingMediumAction(QMenu *pOpenMediumMenu, const QString &strActionName);
    /** Adds 'Choose Disk File' action into passed @a pOpenMediumMenu under passed @a strActionName. */
    void addChooseDiskFileAction(QMenu *pOpenMediumMenu, const QString &strActionName);
    /** Adds 'Choose Host Drive' actions into passed @a pOpenMediumMenu. */
    void addChooseHostDriveActions(QMenu *pOpenMediumMenu);
    /** Adds 'Choose Recent Medium' actions of passed @a enmRecentMediumType into passed @a pOpenMediumMenu. */
    void addRecentMediumActions(QMenu *pOpenMediumMenu, UIMediumDeviceType enmRecentMediumType);

    /** Returns result of @a strText being compressed. */
    static QString compressText(const QString &strText);

    /** @name General
     * @{ */
        /** Holds the controller mime-type for the D&D system. */
        static const QString  s_strControllerMimeType;
        /** Holds the attachment mime-type for the D&D system. */
        static const QString  s_strAttachmentMimeType;

        /** Holds whether the loading is in progress. */
        bool  m_fLoadingInProgress;

        /** Holds the machine ID. */
        QUuid    m_uMachineId;
        /** Holds the machine settings file-path. */
        QString  m_strMachineName;
        /** Holds the machine settings file-path. */
        QString  m_strMachineSettingsFilePath;
        /** Holds the machine guest OS type ID. */
        QString  m_strMachineGuestOSTypeId;

        /** Holds configuration access level. */
        ConfigurationAccessLevel  m_enmConfigurationAccessLevel;

        /** Holds whether we are showing full contents. */
        bool  m_fShowFullContents;

        /** Holds the last mouse-press position. */
        QPoint  m_mousePressPosition;
    /** @} */

    /** @name Objects
     * @{ */
        /** Holds the action pool instance. */
        UIActionPool *m_pActionPool;

        /** Holds the storage-model instance. */
        StorageModel *m_pModelStorage;

        /** Holds the medium ID wrapper instance. */
        UIMediumIDHolder *m_pMediumIdHolder;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the splitter instance. */
        QISplitter *m_pSplitter;

        /** Holds the left pane instance. */
        QWidget                                *m_pWidgetLeftPane;
        /** Holds the left pane separator instance. */
        QILabelSeparator                       *m_pLabelSeparatorLeftPane;
        /** Holds the tree-view layout instance. */
        QVBoxLayout                            *m_pLayoutTree;
        /** Holds the tree-view instance. */
        QITreeView                             *m_pTreeViewStorage;
        /** Holds the toolbar layout instance. */
        QHBoxLayout                            *m_pLayoutToolbar;
        /** Holds the toolbar instance. */
        QIToolBar                              *m_pToolbar;
        /** Holds the 'Add Controller' action instance. */
        QAction                                *m_pActionAddController;
        /** Holds the 'Remove Controller' action instance. */
        QAction                                *m_pActionRemoveController;
        /** Holds the map of add controller action instances. */
        QMap<KStorageControllerType, QAction*>  m_addControllerActions;
        /** Holds the 'Add Attachment' action instance. */
        QAction                                *m_pActionAddAttachment;
        /** Holds the 'Remove Attachment' action instance. */
        QAction                                *m_pActionRemoveAttachment;
        /** Holds the 'Add HD Attachment' action instance. */
        QAction                                *m_pActionAddAttachmentHD;
        /** Holds the 'Add CD Attachment' action instance. */
        QAction                                *m_pActionAddAttachmentCD;
        /** Holds the 'Add FD Attachment' action instance. */
        QAction                                *m_pActionAddAttachmentFD;

        /** Holds the right pane instance. */
        QStackedWidget   *m_pStackRightPane;

        /** Holds the right pane empty widget separator instance. */
        QILabelSeparator *m_pLabelSeparatorEmpty;
        /** Holds the info label instance. */
        QLabel           *m_pLabelInfo;

        /** Holds the right pane controller widget separator instance. */
        QILabelSeparator *m_pLabelSeparatorParameters;
        /** Holds the name label instance. */
        QLabel           *m_pLabelName;
        /** Holds the name editor instance. */
        QLineEdit        *m_pEditorName;
        /** Holds the type label instance. */
        QLabel           *m_pLabelType;
        /** Holds the type combo instance. */
        QComboBox        *m_pComboType;
        /** Holds the port count label instance. */
        QLabel           *m_pLabelPortCount;
        /** Holds the port count spinbox instance. */
        QSpinBox         *m_pSpinboxPortCount;
        /** Holds the IO cache check-box instance. */
        QCheckBox        *m_pCheckBoxIoCache;

        /** Holds the right pane attachment widget separator instance. */
        QILabelSeparator *m_pLabelSeparatorAttributes;
        /** Holds the medium label container instance. */
        QStackedWidget   *m_pContainerMediumLabels;
        /** Holds the HD label instance. */
        QLabel           *m_pLabelHD;
        /** Holds the CD label instance. */
        QLabel           *m_pLabelCD;
        /** Holds the FD label instance. */
        QLabel           *m_pLabelFD;
        /** Holds the slot combo instance. */
        QComboBox        *m_pComboSlot;
        /** Holds the open tool-button instance. */
        QIToolButton     *m_pToolButtonOpen;
        /** Holds the 1st check-box container instance. */
        QStackedWidget   *m_pContainerForCheckBoxes1;
        /** Holds the 2nd check-box container instance. */
        QStackedWidget   *m_pContainerForCheckBoxes2;
        /** Holds the passthrough check-box instance. */
        QCheckBox        *m_pCheckBoxPassthrough;
        /** Holds the temporary eject check-box instance. */
        QCheckBox        *m_pCheckBoxTempEject;
        /** Holds the non-rotational check-box instance. */
        QCheckBox        *m_pCheckBoxNonRotational;
        /** Holds the hot-pluggable check-box instance. */
        QCheckBox        *m_pCheckBoxHotPluggable;

        /** Holds the right pane attachment widget separator instance. */
        QILabelSeparator *m_pLabelSeparatorInformation;
        /** Holds the information container instance. */
        QStackedWidget   *m_pContainerInformation;
        /** Holds the HD format label instance. */
        QLabel           *m_pLabelHDFormat;
        /** Holds the HD format field instance. */
        QILabel          *m_pFieldHDFormat;
        /** Holds the HD virtual size label instance. */
        QLabel           *m_pLabelHDVirtualSize;
        /** Holds the HD virtual size field instance. */
        QILabel          *m_pFieldHDVirtualSize;
        /** Holds the HD actual size label instance. */
        QLabel           *m_pLabelHDActualSize;
        /** Holds the HD actual size field instance. */
        QILabel          *m_pFieldHDActualSize;
        /** Holds the HD details label instance. */
        QLabel           *m_pLabelHDDetails;
        /** Holds the HD details field instance. */
        QILabel          *m_pFieldHDDetails;
        /** Holds the HD location label instance. */
        QLabel           *m_pLabelHDLocation;
        /** Holds the HD location field instance. */
        QILabel          *m_pFieldHDLocation;
        /** Holds the HD usage label instance. */
        QLabel           *m_pLabelHDUsage;
        /** Holds the HD usage field instance. */
        QILabel          *m_pFieldHDUsage;
        /** Holds the HD encryption label instance. */
        QLabel           *m_pLabelHDEncryption;
        /** Holds the HD encryption field instance. */
        QILabel          *m_pFieldHDEncryption;
        /** Holds the CD type label instance. */
        QLabel           *m_pLabelCDType;
        /** Holds the CD type field instance. */
        QILabel          *m_pFieldCDType;
        /** Holds the CD size label instance. */
        QLabel           *m_pLabelCDSize;
        /** Holds the CD size field instance. */
        QILabel          *m_pFieldCDSize;
        /** Holds the CD location label instance. */
        QLabel           *m_pLabelCDLocation;
        /** Holds the CD location field instance. */
        QILabel          *m_pFieldCDLocation;
        /** Holds the CD usage label instance. */
        QLabel           *m_pLabelCDUsage;
        /** Holds the CD usage field instance. */
        QILabel          *m_pFieldCDUsage;
        /** Holds the FD type label instance. */
        QLabel           *m_pLabelFDType;
        /** Holds the FD type field instance. */
        QILabel          *m_pFieldFDType;
        /** Holds the FD size label instance. */
        QLabel           *m_pLabelFDSize;
        /** Holds the FD size field instance. */
        QILabel          *m_pFieldFDSize;
        /** Holds the FD location label instance. */
        QLabel           *m_pLabelFDLocation;
        /** Holds the FD location field instance. */
        QILabel          *m_pFieldFDLocation;
        /** Holds the FD usage label instance. */
        QLabel           *m_pLabelFDUsage;
        /** Holds the FD usage field instance. */
        QILabel          *m_pFieldFDUsage;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIStorageSettingsEditor_h */
