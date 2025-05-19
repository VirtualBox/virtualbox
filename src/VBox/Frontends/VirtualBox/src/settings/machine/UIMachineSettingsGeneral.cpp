/* $Id: UIMachineSettingsGeneral.cpp 168930 2025-05-19 15:06:14Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsGeneral class implementation.
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
#include <QFileInfo>
#include <QVBoxLayout>

/* GUI includes: */
#include "QITabWidget.h"
#include "UIAddDiskEncryptionPasswordDialog.h"
#include "UIConverter.h"
#include "UIDefs.h"
#include "UIDescriptionEditor.h"
#include "UIDiskEncryptionSettingsEditor.h"
#include "UIDragAndDropEditor.h"
#include "UIErrorString.h"
#include "UIGlobalSession.h"
#include "UIMachineSettingsGeneral.h"
#include "UIModalWindowManager.h"
#include "UINameAndSystemEditor.h"
#include "UIProgressObject.h"
#include "UISharedClipboardEditor.h"
#include "UISnapshotFolderEditor.h"
#include "UITranslator.h"

/* COM includes: */
#include "CExtPack.h"
#include "CExtPackManager.h"
#include "CMedium.h"
#include "CMediumAttachment.h"
#include "CPlatform.h"
#include "CPlatformX86.h"
#include "CProgress.h"


/** Machine settings: General page data structure. */
struct UIDataSettingsMachineGeneral
{
    /** Constructs data. */
    UIDataSettingsMachineGeneral()
        : m_strName(QString())
        , m_strGuestOsTypeId(QString())
        , m_strSnapshotsFolder(QString())
        , m_strSnapshotsHomeDir(QString())
        , m_clipboardMode(KClipboardMode_Disabled)
        , m_dndMode(KDnDMode_Disabled)
        , m_strDescription(QString())
        , m_fEncryptionEnabled(false)
        , m_fEncryptionCipherChanged(false)
        , m_fEncryptionPasswordChanged(false)
        , m_enmEncryptionCipherType(UIDiskEncryptionCipherType_Max)
        , m_strEncryptionPassword(QString())
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsMachineGeneral &other) const
    {
        return true
               && (m_strName == other.m_strName)
               && (m_strGuestOsTypeId == other.m_strGuestOsTypeId)
               && (m_strSnapshotsFolder == other.m_strSnapshotsFolder)
               && (m_clipboardMode == other.m_clipboardMode)
               && (m_dndMode == other.m_dndMode)
               && (m_strDescription == other.m_strDescription)
               && (m_fEncryptionEnabled == other.m_fEncryptionEnabled)
               && (m_fEncryptionCipherChanged == other.m_fEncryptionCipherChanged)
               && (m_fEncryptionPasswordChanged == other.m_fEncryptionPasswordChanged)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsMachineGeneral &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsMachineGeneral &other) const { return !equal(other); }

    /** Holds the VM name. */
    QString  m_strName;
    /** Holds the VM OS type ID. */
    QString  m_strGuestOsTypeId;

    /** Holds the VM snapshot folder. */
    QString         m_strSnapshotsFolder;
    /** Holds the default VM snapshot folder. */
    QString         m_strSnapshotsHomeDir;
    /** Holds the VM clipboard mode. */
    KClipboardMode  m_clipboardMode;
    /** Holds the VM drag&drop mode. */
    KDnDMode        m_dndMode;

    /** Holds the VM description. */
    QString  m_strDescription;

    /** Holds whether the encryption is enabled. */
    bool                        m_fEncryptionEnabled;
    /** Holds whether the encryption cipher was changed. */
    bool                        m_fEncryptionCipherChanged;
    /** Holds whether the encryption password was changed. */
    bool                        m_fEncryptionPasswordChanged;
    /** Holds the encryption cipher index. */
    UIDiskEncryptionCipherType  m_enmEncryptionCipherType;
    /** Holds the encryption password. */
    QString                     m_strEncryptionPassword;
    /** Holds the encrypted medium ids. */
    EncryptedMediumMap          m_encryptedMedia;
    /** Holds the encryption passwords. */
    EncryptionPasswordMap       m_encryptionPasswords;
};


UIMachineSettingsGeneral::UIMachineSettingsGeneral()
    : m_fEncryptionCipherChanged(false)
    , m_fEncryptionPasswordChanged(false)
    , m_pCache(0)
    , m_pTabWidget(0)
    , m_pTabBasic(0)
    , m_pEditorNameAndSystem(0)
    , m_pTabAdvanced(0)
    , m_pEditorSnapshotFolder(0)
    , m_pEditorClipboard(0)
    , m_pEditorDragAndDrop(0)
    , m_pTabDescription(0)
    , m_pEditorDescription(0)
    , m_pTabEncryption(0)
    , m_pEditorDiskEncryptionSettings(0)
{
    prepare();
}

UIMachineSettingsGeneral::~UIMachineSettingsGeneral()
{
    cleanup();
}

QString UIMachineSettingsGeneral::guestOSTypeId() const
{
    AssertPtrReturn(m_pEditorNameAndSystem, QString());
    return m_pEditorNameAndSystem->typeId();
}

bool UIMachineSettingsGeneral::changed() const
{
    return m_pCache ? m_pCache->wasChanged() : false;
}

void UIMachineSettingsGeneral::loadToCacheFrom(QVariant &data)
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Prepare old data: */
    UIDataSettingsMachineGeneral oldGeneralData;

    /* Gather old 'Basic' data: */
    oldGeneralData.m_strName = m_machine.GetName();
    oldGeneralData.m_strGuestOsTypeId = m_machine.GetOSTypeId();

    /* Gather old 'Advanced' data: */
    oldGeneralData.m_strSnapshotsFolder = m_machine.GetSnapshotFolder();
    oldGeneralData.m_strSnapshotsHomeDir = QFileInfo(m_machine.GetSettingsFilePath()).absolutePath();
    oldGeneralData.m_clipboardMode = m_machine.GetClipboardMode();
    oldGeneralData.m_dndMode = m_machine.GetDnDMode();

    /* Gather old 'Description' data: */
    oldGeneralData.m_strDescription = m_machine.GetDescription();

    /* Gather old 'Encryption' data: */
    QString strCipher;
    bool fEncryptionCipherCommon = true;
    /* Prepare the map of the encrypted media: */
    EncryptedMediumMap encryptedMedia;
    foreach (const CMediumAttachment &attachment, m_machine.GetMediumAttachments())
    {
        /* Check hard-drive attachments only: */
        if (attachment.GetType() == KDeviceType_HardDisk)
        {
            /* Get the attachment medium base: */
            const CMedium comMedium = attachment.GetMedium();
            /* Check medium encryption attributes: */
            QString strCurrentCipher;
            const QString strCurrentPasswordId = comMedium.GetEncryptionSettings(strCurrentCipher);
            if (comMedium.isOk())
            {
                encryptedMedia.insert(strCurrentPasswordId, comMedium.GetId());
                if (strCurrentCipher != strCipher)
                {
                    if (strCipher.isNull())
                        strCipher = strCurrentCipher;
                    else
                        fEncryptionCipherCommon = false;
                }
            }
        }
    }
    oldGeneralData.m_fEncryptionEnabled = !encryptedMedia.isEmpty();
    oldGeneralData.m_fEncryptionCipherChanged = false;
    oldGeneralData.m_fEncryptionPasswordChanged = false;
    if (fEncryptionCipherCommon)
        oldGeneralData.m_enmEncryptionCipherType = gpConverter->fromInternalString<UIDiskEncryptionCipherType>(strCipher);
    oldGeneralData.m_encryptedMedia = encryptedMedia;

    /* Cache old data: */
    m_pCache->cacheInitialData(oldGeneralData);

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

void UIMachineSettingsGeneral::getFromCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Get old data from cache: */
    const UIDataSettingsMachineGeneral &oldGeneralData = m_pCache->base();

    /* Load old 'Basic' data from cache: */
    if (m_pEditorNameAndSystem)
    {
        m_pEditorNameAndSystem->setName(oldGeneralData.m_strName);
        m_pEditorNameAndSystem->setGuestOSTypeByTypeId(oldGeneralData.m_strGuestOsTypeId);
    }

    /* Load old 'Advanced' data from cache: */
    if (m_pEditorSnapshotFolder)
    {
        m_pEditorSnapshotFolder->setPath(oldGeneralData.m_strSnapshotsFolder);
        m_pEditorSnapshotFolder->setInitialPath(oldGeneralData.m_strSnapshotsHomeDir);
    }
    if (m_pEditorClipboard)
        m_pEditorClipboard->setValue(oldGeneralData.m_clipboardMode);
    if (m_pEditorDragAndDrop)
        m_pEditorDragAndDrop->setValue(oldGeneralData.m_dndMode);

    /* Load old 'Description' data from cache: */
    if (m_pEditorDescription)
        m_pEditorDescription->setValue(oldGeneralData.m_strDescription);

    /* Load old 'Encryption' data from cache: */
    if (m_pEditorDiskEncryptionSettings)
    {
        m_pEditorDiskEncryptionSettings->setFeatureEnabled(oldGeneralData.m_fEncryptionEnabled);
        m_pEditorDiskEncryptionSettings->setCipherType(oldGeneralData.m_enmEncryptionCipherType);
    }
    if (m_fEncryptionCipherChanged)
        m_fEncryptionCipherChanged = oldGeneralData.m_fEncryptionCipherChanged;
    if (m_fEncryptionPasswordChanged)
        m_fEncryptionPasswordChanged = oldGeneralData.m_fEncryptionPasswordChanged;

    /* Polish page finally: */
    polishPage();

    /* Revalidate: */
    revalidate();
}

void UIMachineSettingsGeneral::putToCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Prepare new data: */
    UIDataSettingsMachineGeneral newGeneralData;

    /* Gather new 'Basic' data: */
    if (m_pEditorNameAndSystem)
    {
        newGeneralData.m_strName = m_pEditorNameAndSystem->name();
        newGeneralData.m_strGuestOsTypeId = m_pEditorNameAndSystem->typeId();
    }

    /* Gather new 'Advanced' data: */
    if (m_pEditorSnapshotFolder)
        newGeneralData.m_strSnapshotsFolder = m_pEditorSnapshotFolder->path();
    if (m_pEditorClipboard)
        newGeneralData.m_clipboardMode = m_pEditorClipboard->value();
    if (m_pEditorDragAndDrop)
        newGeneralData.m_dndMode = m_pEditorDragAndDrop->value();

    /* Gather new 'Description' data: */
    if (m_pEditorDescription)
        newGeneralData.m_strDescription = m_pEditorDescription->value().isEmpty()
                                        ? QString() : m_pEditorDescription->value();

    /* Gather new 'Encryption' data: */
    if (m_pEditorDiskEncryptionSettings)
    {
        newGeneralData.m_fEncryptionEnabled = m_pEditorDiskEncryptionSettings->isFeatureEnabled();
        newGeneralData.m_fEncryptionCipherChanged = m_fEncryptionCipherChanged;
        newGeneralData.m_fEncryptionPasswordChanged = m_fEncryptionPasswordChanged;
        newGeneralData.m_enmEncryptionCipherType = m_pEditorDiskEncryptionSettings->cipherType();
        newGeneralData.m_strEncryptionPassword = m_pEditorDiskEncryptionSettings->password1();
        newGeneralData.m_encryptedMedia = m_pCache->base().m_encryptedMedia;
        /* If encryption status, cipher or password is changed: */
        if (newGeneralData.m_fEncryptionEnabled != m_pCache->base().m_fEncryptionEnabled ||
            newGeneralData.m_fEncryptionCipherChanged != m_pCache->base().m_fEncryptionCipherChanged ||
            newGeneralData.m_fEncryptionPasswordChanged != m_pCache->base().m_fEncryptionPasswordChanged)
        {
            /* Ask for the disk encryption passwords if necessary: */
            if (!m_pCache->base().m_encryptedMedia.isEmpty())
            {
                /* Create corresponding dialog: */
                QWidget *pDlgParent = windowManager().realParentWindow(window());
                QPointer<UIAddDiskEncryptionPasswordDialog> pDlg =
                     new UIAddDiskEncryptionPasswordDialog(pDlgParent,
                                                           newGeneralData.m_strName,
                                                           newGeneralData.m_encryptedMedia);
                /* Execute it and acquire the result: */
                if (pDlg->exec() == QDialog::Accepted)
                    newGeneralData.m_encryptionPasswords = pDlg->encryptionPasswords();
                /* Delete dialog if still valid: */
                if (pDlg)
                    delete pDlg;
            }
        }
    }

    /* Cache new data: */
    m_pCache->cacheCurrentData(newGeneralData);
}

void UIMachineSettingsGeneral::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

bool UIMachineSettingsGeneral::validate(QList<UIValidationMessage> &messages)
{
    /* Pass by default: */
    bool fPass = true;

    /* Prepare message: */
    UIValidationMessage message;

    /* 'Basic' tab validations: */
    message.first = UITranslator::removeAccelMark(m_pTabWidget->tabText(0));
    message.second.clear();

    /* VM name validation: */
    AssertPtrReturn(m_pEditorNameAndSystem, false);
    if (m_pEditorNameAndSystem->name().trimmed().isEmpty())
    {
        message.second << tr("No name specified for the virtual machine.");
        fPass = false;
    }

    /* Serialize message: */
    if (!message.second.isEmpty())
        messages << message;

    /* 'Encryption' tab validations: */
    message.first = UITranslator::removeAccelMark(m_pTabWidget->tabText(3));
    message.second.clear();

    /* Encryption validation: */
    AssertPtrReturn(m_pEditorDiskEncryptionSettings, false);
    if (m_pEditorDiskEncryptionSettings->isFeatureEnabled())
    {
        /* Encryption Extension Pack presence test: */
        CExtPackManager extPackManager = gpGlobalSession->virtualBox().GetExtensionPackManager();
        if (!extPackManager.isNull() && !extPackManager.IsExtPackUsable(GUI_ExtPackName))
        {
            message.second << tr("You are trying to enable disk encryption for this virtual machine. "
                                 "However, this requires the <i>%1</i> to be installed. "
                                 "Please install the Extension Pack from the VirtualBox download site.")
                                 .arg(GUI_ExtPackName);
            fPass = false;
        }

        /* Cipher should be chosen if once changed: */
        if (   !m_pCache->base().m_fEncryptionEnabled
            || m_fEncryptionCipherChanged)
        {
            if (m_pEditorDiskEncryptionSettings->cipherType() == UIDiskEncryptionCipherType_Unchanged)
                message.second << tr("Disk encryption cipher type not specified.");
            fPass = false;
        }

        /* Password should be entered and confirmed if once changed: */
        if (!m_pCache->base().m_fEncryptionEnabled ||
            m_fEncryptionPasswordChanged)
        {
            if (m_pEditorDiskEncryptionSettings->password1().isEmpty())
                message.second << tr("Disk encryption password empty.");
            else
            if (m_pEditorDiskEncryptionSettings->password1() !=
                m_pEditorDiskEncryptionSettings->password2())
                message.second << tr("Disk encryption passwords do not match.");
            fPass = false;
        }
    }

    /* Serialize message: */
    if (!message.second.isEmpty())
        messages << message;

    /* Return result: */
    return fPass;
}

void UIMachineSettingsGeneral::setOrderAfter(QWidget *pWidget)
{
    /* 'Basic' tab: */
    if (pWidget && m_pTabWidget && m_pTabWidget->focusProxy())
        setTabOrder(pWidget, m_pTabWidget->focusProxy());
    if (m_pTabWidget && m_pTabWidget->focusProxy() && m_pEditorNameAndSystem)
        setTabOrder(m_pTabWidget->focusProxy(), m_pEditorNameAndSystem);

    /* 'Advanced' tab: */
    if (m_pEditorNameAndSystem && m_pEditorSnapshotFolder)
        setTabOrder(m_pEditorNameAndSystem, m_pEditorSnapshotFolder);
    if (m_pEditorSnapshotFolder && m_pEditorClipboard)
        setTabOrder(m_pEditorSnapshotFolder, m_pEditorClipboard);
    if (m_pEditorClipboard && m_pEditorDragAndDrop)
        setTabOrder(m_pEditorClipboard, m_pEditorDragAndDrop);

    /* 'Description' tab: */
    if (m_pEditorDragAndDrop && m_pEditorDescription)
        setTabOrder(m_pEditorDragAndDrop, m_pEditorDescription);
}

void UIMachineSettingsGeneral::sltRetranslateUI()
{
    m_pTabWidget->setTabText(m_pTabWidget->indexOf(m_pTabBasic), tr("&Identity"));
    m_pTabWidget->setTabText(m_pTabWidget->indexOf(m_pTabAdvanced), tr("&Features"));
    m_pTabWidget->setTabText(m_pTabWidget->indexOf(m_pTabDescription), tr("D&escription"));
    m_pTabWidget->setTabText(m_pTabWidget->indexOf(m_pTabEncryption), tr("Disk Enc&ryption"));

    updateMinimumLayoutHint();
}

void UIMachineSettingsGeneral::handleFilterChange()
{
    updateMinimumLayoutHint();
}

void UIMachineSettingsGeneral::polishPage()
{
    /* Polish 'Basic' availability: */
    if (m_pEditorNameAndSystem)
    {
        m_pEditorNameAndSystem->setNameStuffEnabled(isMachineOffline() || isMachineSaved());
        m_pEditorNameAndSystem->setPathStuffEnabled(isMachineOffline());
        m_pEditorNameAndSystem->setOSTypeStuffEnabled(isMachineOffline());
    }

    /* Polish 'Advanced' availability: */
    if (m_pEditorSnapshotFolder)
        m_pEditorSnapshotFolder->setEnabled(isMachineOffline());
    if (m_pEditorClipboard)
        m_pEditorClipboard->setEnabled(isMachineInValidMode());
    if (m_pEditorDragAndDrop)
        m_pEditorDragAndDrop->setEnabled(isMachineInValidMode());

    /* Polish 'Description' availability: */
    if (m_pEditorDescription)
        m_pEditorDescription->setEnabled(isMachineInValidMode());

    /* Polish 'Encryption' availability: */
    if (m_pEditorDiskEncryptionSettings)
        m_pEditorDiskEncryptionSettings->setEnabled(isMachineOffline());
}

void UIMachineSettingsGeneral::sltHandleEncryptionCipherChanged()
{
    m_fEncryptionCipherChanged = true;
    revalidate();
}

void UIMachineSettingsGeneral::sltHandleEncryptionPasswordChanged()
{
    m_fEncryptionCipherChanged = true;
    m_fEncryptionPasswordChanged = true;
    revalidate();
}

void UIMachineSettingsGeneral::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheMachineGeneral;
    AssertPtrReturnVoid(m_pCache);

    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIMachineSettingsGeneral::prepareWidgets()
{
    /* Prepare main layout: */
    QHBoxLayout *pLayoutMain = new QHBoxLayout(this);
    if (pLayoutMain)
    {
        /* Prepare tab-widget: */
        m_pTabWidget = new QITabWidget(this);
        if (m_pTabWidget)
        {
            /* Prepare each tab separately: */
            prepareTabBasic();
            prepareTabAdvanced();
            prepareTabDescription();
            prepareTabEncryption();

            pLayoutMain->addWidget(m_pTabWidget);
        }
    }
}

void UIMachineSettingsGeneral::prepareTabBasic()
{
    /* Prepare 'Basic' tab: */
    m_pTabBasic = new UIEditor(m_pTabWidget);
    if (m_pTabBasic)
    {
        /* Prepare 'Basic' tab layout: */
        QVBoxLayout *pLayoutBasic = new QVBoxLayout(m_pTabBasic);
        if (pLayoutBasic)
        {
#ifdef VBOX_WS_MAC
            /* On Mac OS X we can do a bit of smoothness: */
            int iLeft, iTop, iRight, iBottom;
            pLayoutBasic->getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
            pLayoutBasic->setContentsMargins(iLeft / 2, iTop / 2, iRight / 2, iBottom / 2);
#endif

            /* Prepare name and system editor: */
            m_pEditorNameAndSystem = new UINameAndSystemEditor(m_pTabBasic);
            if (m_pEditorNameAndSystem)
            {
                m_pTabBasic->addEditor(m_pEditorNameAndSystem);
                pLayoutBasic->addWidget(m_pEditorNameAndSystem);
            }

            pLayoutBasic->addStretch();
        }

        addEditor(m_pTabBasic);
        m_pTabWidget->addTab(m_pTabBasic, QString());
    }
}

void UIMachineSettingsGeneral::prepareTabAdvanced()
{
    /* Prepare 'Advanced' tab: */
    m_pTabAdvanced = new UIEditor(m_pTabWidget);
    if (m_pTabAdvanced)
    {
        /* Prepare 'Advanced' tab layout: */
        QVBoxLayout *pLayoutAdvanced = new QVBoxLayout(m_pTabAdvanced);
        if (pLayoutAdvanced)
        {
#ifdef VBOX_WS_MAC
            /* On Mac OS X we can do a bit of smoothness: */
            int iLeft, iTop, iRight, iBottom;
            pLayoutAdvanced->getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
            pLayoutAdvanced->setContentsMargins(iLeft / 2, iTop / 2, iRight / 2, iBottom / 2);
#endif

            /* Prepare snapshot folder editor: */
            m_pEditorSnapshotFolder = new UISnapshotFolderEditor(m_pTabAdvanced);
            if (m_pEditorSnapshotFolder)
            {
                m_pTabAdvanced->addEditor(m_pEditorSnapshotFolder);
                pLayoutAdvanced->addWidget(m_pEditorSnapshotFolder);
            }

            /* Prepare clipboard editor: */
            m_pEditorClipboard = new UISharedClipboardEditor(m_pTabAdvanced);
            if (m_pEditorClipboard)
            {
                m_pTabAdvanced->addEditor(m_pEditorClipboard);
                pLayoutAdvanced->addWidget(m_pEditorClipboard);
            }

            /* Prepare drag&drop editor: */
            m_pEditorDragAndDrop = new UIDragAndDropEditor(m_pTabAdvanced);
            if (m_pEditorDragAndDrop)
            {
                m_pTabAdvanced->addEditor(m_pEditorDragAndDrop);
                pLayoutAdvanced->addWidget(m_pEditorDragAndDrop);
            }

            pLayoutAdvanced->addStretch();
        }

        addEditor(m_pTabAdvanced);
        m_pTabWidget->addTab(m_pTabAdvanced, QString());
    }
}

void UIMachineSettingsGeneral::prepareTabDescription()
{
    /* Prepare 'Description' tab: */
    m_pTabDescription = new UIEditor(m_pTabWidget);
    if (m_pTabDescription)
    {
        /* Prepare 'Description' tab layout: */
        QVBoxLayout *pLayoutDescription = new QVBoxLayout(m_pTabDescription);
        if (pLayoutDescription)
        {
#ifdef VBOX_WS_MAC
            /* On Mac OS X we can do a bit of smoothness: */
            int iLeft, iTop, iRight, iBottom;
            pLayoutDescription->getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
            pLayoutDescription->setContentsMargins(iLeft / 2, iTop / 2, iRight / 2, iBottom / 2);
#endif

            /* Prepare description editor: */
            m_pEditorDescription = new UIDescriptionEditor(m_pTabDescription);
            if (m_pEditorDescription)
            {
                m_pEditorDescription->setObjectName(QStringLiteral("m_pEditorDescription"));
                m_pTabDescription->addEditor(m_pEditorDescription);
                pLayoutDescription->addWidget(m_pEditorDescription);
            }
        }

        addEditor(m_pTabDescription);
        m_pTabWidget->addTab(m_pTabDescription, QString());
    }
}

void UIMachineSettingsGeneral::prepareTabEncryption()
{
    /* Prepare 'Encryption' tab: */
    m_pTabEncryption = new UIEditor(m_pTabWidget);
    if (m_pTabEncryption)
    {
        /* Prepare 'Encryption' tab layout: */
        QVBoxLayout *pLayoutEncryption = new QVBoxLayout(m_pTabEncryption);
        if (pLayoutEncryption)
        {
#ifdef VBOX_WS_MAC
            /* On Mac OS X we can do a bit of smoothness: */
            int iLeft, iTop, iRight, iBottom;
            pLayoutEncryption->getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
            pLayoutEncryption->setContentsMargins(iLeft / 2, iTop / 2, iRight / 2, iBottom / 2);
#endif

            /* Prepare disk encryption settings editor: */
            m_pEditorDiskEncryptionSettings = new UIDiskEncryptionSettingsEditor(m_pTabEncryption);
            if (m_pEditorDiskEncryptionSettings)
            {
                m_pTabEncryption->addEditor(m_pEditorDiskEncryptionSettings);
                pLayoutEncryption->addWidget(m_pEditorDiskEncryptionSettings);
            }

            pLayoutEncryption->addStretch();
        }

        addEditor(m_pTabEncryption);
        m_pTabWidget->addTab(m_pTabEncryption, QString());
    }
}

void UIMachineSettingsGeneral::prepareConnections()
{
    /* Configure 'Basic' connections: */
    connect(m_pEditorNameAndSystem, &UINameAndSystemEditor::sigOsTypeChanged,
            this, &UIMachineSettingsGeneral::revalidate);
    connect(m_pEditorNameAndSystem, &UINameAndSystemEditor::sigNameChanged,
            this, &UIMachineSettingsGeneral::revalidate);

    /* Configure 'Encryption' connections: */
    connect(m_pEditorDiskEncryptionSettings, &UIDiskEncryptionSettingsEditor::sigStatusChanged,
            this, &UIMachineSettingsGeneral::revalidate);
    connect(m_pEditorDiskEncryptionSettings, &UIDiskEncryptionSettingsEditor::sigCipherChanged,
            this, &UIMachineSettingsGeneral::sltHandleEncryptionCipherChanged);
    connect(m_pEditorDiskEncryptionSettings, &UIDiskEncryptionSettingsEditor::sigPasswordChanged,
            this, &UIMachineSettingsGeneral::sltHandleEncryptionPasswordChanged);
}

void UIMachineSettingsGeneral::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

bool UIMachineSettingsGeneral::saveData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save general settings from cache: */
    if (fSuccess && isMachineInValidMode() && m_pCache->wasChanged())
    {
        /* Save 'Basic' data from cache: */
        if (fSuccess)
            fSuccess = saveBasicData();
        /* Save 'Advanced' data from cache: */
        if (fSuccess)
            fSuccess = saveAdvancedData();
        /* Save 'Description' data from cache: */
        if (fSuccess)
            fSuccess = saveDescriptionData();
        /* Save 'Encryption' data from cache: */
        if (fSuccess)
            fSuccess = saveEncryptionData();
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsGeneral::saveBasicData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save 'Basic' data from cache: */
    if (fSuccess)
    {
        /* Get old data from cache: */
        const UIDataSettingsMachineGeneral &oldGeneralData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsMachineGeneral &newGeneralData = m_pCache->data();

        /* Save machine OS type ID: */
        if (isMachineOffline() && newGeneralData.m_strGuestOsTypeId != oldGeneralData.m_strGuestOsTypeId)
        {
            if (fSuccess)
            {
                m_machine.SetOSTypeId(newGeneralData.m_strGuestOsTypeId);
                fSuccess = m_machine.isOk();
            }
            if (fSuccess)
            {
                /* Update long mode CPU feature bit when OS type changed: */
                const KPlatformArchitecture enmArch = optionalFlags().contains("arch")
                                                    ? optionalFlags().value("arch").value<KPlatformArchitecture>()
                                                    : KPlatformArchitecture_x86;
                switch (enmArch)
                {
                    case KPlatformArchitecture_x86:
                    {
                        const CPlatform comPlatform = m_machine.GetPlatform();
                        CPlatformX86 comPlatformX86 = comPlatform.GetX86();
                        const CGuestOSType comNewType = gpGlobalSession->virtualBox().GetGuestOSType(newGeneralData.m_strGuestOsTypeId);
                        comPlatformX86.SetCPUProperty(KCPUPropertyTypeX86_LongMode, comNewType.GetIs64Bit());
                        if (!comPlatformX86.isOk())
                        {
                            notifyOperationProgressError(UIErrorString::formatErrorInfo(comPlatformX86));
                            return false;
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsGeneral::saveAdvancedData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save 'Advanced' data from cache: */
    if (fSuccess)
    {
        /* Get old data from cache: */
        const UIDataSettingsMachineGeneral &oldGeneralData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsMachineGeneral &newGeneralData = m_pCache->data();

        /* Save machine clipboard mode: */
        if (fSuccess && newGeneralData.m_clipboardMode != oldGeneralData.m_clipboardMode)
        {
            m_machine.SetClipboardMode(newGeneralData.m_clipboardMode);
            fSuccess = m_machine.isOk();
        }
        /* Save machine D&D mode: */
        if (fSuccess && newGeneralData.m_dndMode != oldGeneralData.m_dndMode)
        {
            m_machine.SetDnDMode(newGeneralData.m_dndMode);
            fSuccess = m_machine.isOk();
        }
        /* Save machine snapshot folder: */
        if (fSuccess && isMachineOffline() && newGeneralData.m_strSnapshotsFolder != oldGeneralData.m_strSnapshotsFolder)
        {
            m_machine.SetSnapshotFolder(newGeneralData.m_strSnapshotsFolder);
            fSuccess = m_machine.isOk();
        }
        // VM name from 'Basic' data should go after the snapshot folder from the 'Advanced' data
        // as otherwise VM rename magic can collide with the snapshot folder one.
        /* Save machine name: */
        if (fSuccess && (isMachineOffline() || isMachineSaved()) && newGeneralData.m_strName != oldGeneralData.m_strName)
        {
            m_machine.SetName(newGeneralData.m_strName);
            fSuccess = m_machine.isOk();
        }

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsGeneral::saveDescriptionData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save 'Description' data from cache: */
    if (fSuccess)
    {
        /* Get old data from cache: */
        const UIDataSettingsMachineGeneral &oldGeneralData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsMachineGeneral &newGeneralData = m_pCache->data();

        /* Save machine description: */
        if (fSuccess && newGeneralData.m_strDescription != oldGeneralData.m_strDescription)
        {
            m_machine.SetDescription(newGeneralData.m_strDescription);
            fSuccess = m_machine.isOk();
        }

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsGeneral::saveEncryptionData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save 'Encryption' data from cache: */
    if (fSuccess)
    {
        /* Get old data from cache: */
        const UIDataSettingsMachineGeneral &oldGeneralData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsMachineGeneral &newGeneralData = m_pCache->data();

        /* Make sure it either encryption state is changed itself,
         * or the encryption was already enabled and either cipher or password is changed. */
        if (   isMachineOffline()
            && (   newGeneralData.m_fEncryptionEnabled != oldGeneralData.m_fEncryptionEnabled
                || (   oldGeneralData.m_fEncryptionEnabled
                    && (   newGeneralData.m_fEncryptionCipherChanged != oldGeneralData.m_fEncryptionCipherChanged
                        || newGeneralData.m_fEncryptionPasswordChanged != oldGeneralData.m_fEncryptionPasswordChanged))))
        {
            /* Get machine name for further activities: */
            QString strMachineName;
            if (fSuccess)
            {
                strMachineName = m_machine.GetName();
                fSuccess = m_machine.isOk();
            }
            /* Get machine attachments for further activities: */
            CMediumAttachmentVector attachments;
            if (fSuccess)
            {
                attachments = m_machine.GetMediumAttachments();
                fSuccess = m_machine.isOk();
            }

            /* Show error message if necessary: */
            if (!fSuccess)
                notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));

            /* For each attachment: */
            for (int iIndex = 0; fSuccess && iIndex < attachments.size(); ++iIndex)
            {
                /* Get current attachment: */
                const CMediumAttachment &comAttachment = attachments.at(iIndex);

                /* Get attachment type for further activities: */
                KDeviceType enmType = KDeviceType_Null;
                // if (fSuccess)
                {
                    enmType = comAttachment.GetType();
                    fSuccess = comAttachment.isOk();
                }
                /* Get attachment medium for further activities: */
                CMedium comMedium;
                if (fSuccess)
                {
                    comMedium = comAttachment.GetMedium();
                    fSuccess = comAttachment.isOk();
                }

                /* Show error message if necessary: */
                if (!fSuccess)
                    notifyOperationProgressError(UIErrorString::formatErrorInfo(comAttachment));
                else
                {
                    /* Pass hard-drives only: */
                    if (enmType != KDeviceType_HardDisk)
                        continue;

                    /* Get medium id for further activities: */
                    QUuid uMediumId;
                    // if (fSuccess)
                    {
                        uMediumId = comMedium.GetId();
                        fSuccess = comMedium.isOk();
                    }

                    /* Create encryption update progress: */
                    CProgress comProgress;
                    if (fSuccess)
                    {
                        /* Cipher attribute changed? */
                        const QString strNewCipher
                            = newGeneralData.m_fEncryptionCipherChanged && newGeneralData.m_fEncryptionEnabled
                            ? gpConverter->toInternalString(newGeneralData.m_enmEncryptionCipherType)
                            : QString();

                        /* Password attribute changed? */
                        QString strNewPassword;
                        QString strNewPasswordId;
                        if (newGeneralData.m_fEncryptionPasswordChanged)
                        {
                            strNewPassword = newGeneralData.m_fEncryptionEnabled ?
                                             newGeneralData.m_strEncryptionPassword : QString();
                            strNewPasswordId = newGeneralData.m_fEncryptionEnabled ?
                                               strMachineName : QString();
                        }

                        /* Get the maps of encrypted media and their passwords: */
                        const EncryptedMediumMap &encryptedMedium = newGeneralData.m_encryptedMedia;
                        const EncryptionPasswordMap &encryptionPasswords = newGeneralData.m_encryptionPasswords;

                        /* Check if old password exists/provided: */
                        const QString strOldPasswordId = encryptedMedium.key(uMediumId);
                        const QString strOldPassword = encryptionPasswords.value(strOldPasswordId);

                        /* Create encryption progress: */
                        comProgress = comMedium.ChangeEncryption(strOldPassword,
                                                                 strNewCipher,
                                                                 strNewPassword,
                                                                 strNewPasswordId);
                        fSuccess = comMedium.isOk();
                    }

                    /* Create encryption update progress object: */
                    QPointer<UIProgressObject> pObject;
                    if (fSuccess)
                    {
                        pObject = new UIProgressObject(comProgress);
                        if (pObject)
                        {
                            connect(pObject.data(), &UIProgressObject::sigProgressChange,
                                    this, &UIMachineSettingsGeneral::sigOperationProgressChange,
                                    Qt::QueuedConnection);
                            connect(pObject.data(), &UIProgressObject::sigProgressError,
                                    this, &UIMachineSettingsGeneral::sigOperationProgressError,
                                    Qt::BlockingQueuedConnection);
                            pObject->exec();
                            if (pObject)
                                delete pObject;
                            else
                            {
                                // Premature application shutdown,
                                // exit immediately:
                                return true;
                            }
                        }
                    }

                    /* Show error message if necessary: */
                    if (!fSuccess)
                        notifyOperationProgressError(UIErrorString::formatErrorInfo(comMedium));
                }
            }
        }
    }
    /* Return result: */
    return fSuccess;
}

void UIMachineSettingsGeneral::updateMinimumLayoutHint()
{
    /* These editors have own labels, but we want them to be properly layouted according to each other: */
    int iMinimumLayoutHint = 0;
    if (m_pEditorSnapshotFolder && !m_pEditorSnapshotFolder->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorSnapshotFolder->minimumLabelHorizontalHint());
    if (m_pEditorClipboard && !m_pEditorClipboard->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorClipboard->minimumLabelHorizontalHint());
    if (m_pEditorDragAndDrop && !m_pEditorDragAndDrop->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorDragAndDrop->minimumLabelHorizontalHint());
    if (m_pEditorSnapshotFolder)
        m_pEditorSnapshotFolder->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorClipboard)
        m_pEditorClipboard->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorDragAndDrop)
        m_pEditorDragAndDrop->setMinimumLayoutIndent(iMinimumLayoutHint);
}
