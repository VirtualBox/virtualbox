/* $Id: UIWizardDiskEditors.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUserNamePasswordEditor class implementation.
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
#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

/* GUI includes: */
#include "QILineEdit.h"
#include "QIToolButton.h"
#include "QIRichTextLabel.h"
#include "UIConverter.h"
#include "UIFilePathSelector.h"
#include "UIGlobalSession.h"
#include "UIHostnameDomainNameEditor.h"
#include "UIIconPool.h"
#include "UIMediumSizeEditor.h"
#include "UITranslationEventListener.h"
#include "UIUserNamePasswordEditor.h"
#include "UIWizardDiskEditors.h"
#include "UIWizardNewVMDiskPage.h"

/* Other VBox includes: */
#include "iprt/assert.h"
#include "iprt/fs.h"
#include "CSystemProperties.h"


/*********************************************************************************************************************************
*   UIWizardDiskEditors implementation.                                                                                   *
*********************************************************************************************************************************/

QString UIWizardDiskEditors::appendExtension(const QString &strName, const QString &strExtension)
{
    /* Convert passed name to native separators: */
    QString strFileName = QDir::toNativeSeparators(strName);

    /* Remove all trailing dots to avoid multiple dots before extension: */
    int iLen;
    while (iLen = strFileName.length(), iLen > 0 && strFileName[iLen - 1] == '.')
        strFileName.truncate(iLen - 1);

    /* Add passed extension if its not done yet: */
    if (QFileInfo(strFileName).suffix().toLower() != strExtension)
        strFileName += QString(".%1").arg(strExtension);

    /* Return result: */
    return strFileName;
}

QString UIWizardDiskEditors::constructMediumFilePath(const QString &strFileName, const QString &strPath)
{
    /* Wrap file-info around received file name: */
    QFileInfo fileInfo(strFileName);
    /* If path-info is relative or there is no path-info at all: */
    if (fileInfo.fileName() == strFileName || fileInfo.isRelative())
    {
        /* Resolve path on the basis of  path we have: */
        fileInfo = QFileInfo(strPath, strFileName);
    }
    /* Return full absolute hard disk file path: */
    return QDir::toNativeSeparators(fileInfo.absoluteFilePath());
}

bool UIWizardDiskEditors::checkFATSizeLimitation(const qulonglong uVariant, const QString &strMediumPath, const qulonglong uSize)
{
    /* If the hard disk is split into 2GB parts then no need to make further checks: */
    if (uVariant & KMediumVariant_VmdkSplit2G)
        return true;
    RTFSTYPE enmType;
    int rc = RTFsQueryType(QFileInfo(strMediumPath).absolutePath().toLatin1().constData(), &enmType);
    if (RT_SUCCESS(rc))
    {
        if (enmType == RTFSTYPE_FAT)
        {
            /* Limit the medium size to 4GB. minus 128 MB for file overhead: */
            qulonglong fatLimit = _4G - _128M;
            if (uSize >= fatLimit)
                return false;
        }
    }
    return true;
}

QString UIWizardDiskEditors::openFileDialogForDiskFile(const QString &strInitialPath, const CMediumFormat &comMediumFormat,
                                                        KDeviceType enmDeviceType, QWidget *pParent)
{
    QString strChosenFilePath;
    QFileInfo initialPath(strInitialPath);
    QDir folder = initialPath.path();
    QString strFileName = initialPath.fileName();

    // /* Set the first parent folder that exists as the current: */
    while (!folder.exists() && !folder.isRoot())
    {
        QFileInfo folderInfo(folder.absolutePath());
        if (folder == QDir(folderInfo.absolutePath()))
            break;
        folder = folderInfo.absolutePath();
    }
    AssertReturn(folder.exists() && !folder.isRoot(), strChosenFilePath);

    QVector<QString> fileExtensions;
    QVector<KDeviceType> deviceTypes;
    comMediumFormat.DescribeFileExtensions(fileExtensions, deviceTypes);
    QStringList validExtensionList;
    for (int i = 0; i < fileExtensions.size(); ++i)
        if (deviceTypes[i] == enmDeviceType)
            validExtensionList << QString("*.%1").arg(fileExtensions[i]);
    /* Compose full filter list: */
    QString strBackendsList = QString("%1 (%2)").arg(comMediumFormat.GetName()).arg(validExtensionList.join(" "));

    strChosenFilePath = QIFileDialog::getSaveFileName(folder.absoluteFilePath(strFileName),
                                                      strBackendsList, pParent,
                                                      UIMediumSizeAndPathGroupBox::tr("Specify a location for new "
                                                                                      "virtual hard disk file..."));
    return strChosenFilePath;
}

QString UIWizardDiskEditors::defaultExtension(const CMediumFormat &mediumFormatRef, KDeviceType enmDeviceType)
{
    if (!mediumFormatRef.isNull())
    {
        /* Load extension / device list: */
        QVector<QString> fileExtensions;
        QVector<KDeviceType> deviceTypes;
        CMediumFormat mediumFormat(mediumFormatRef);
        mediumFormat.DescribeFileExtensions(fileExtensions, deviceTypes);
        for (int i = 0; i < fileExtensions.size(); ++i)
            if (deviceTypes[i] == enmDeviceType)
                return fileExtensions[i].toLower();
    }
    AssertMsgFailed(("Extension can't be NULL!\n"));
    return QString();
}

QString UIWizardDiskEditors::stripFormatExtension(const QString &strFileName, const QStringList &formatExtensions)
{
    QString result(strFileName);
    foreach (const QString &strExtension, formatExtensions)
    {
        if (strFileName.endsWith(strExtension, Qt::CaseInsensitive))
        {
            /* Add the dot to extenstion: */
            QString strExtensionWithDot(strExtension);
            strExtensionWithDot.prepend('.');
            int iIndex = strFileName.lastIndexOf(strExtensionWithDot, -1, Qt::CaseInsensitive);
            result.remove(iIndex, strExtensionWithDot.length());
        }
    }
    return result;
}


/*********************************************************************************************************************************
*   UIDiskVariantWidget implementation.                                                                                   *
*********************************************************************************************************************************/


UIDiskVariantWidget::UIDiskVariantWidget(QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pFixedCheckBox(0)
    , m_pSplitBox(0)
{
    prepare();
}

void UIDiskVariantWidget::prepare()
{
    QGridLayout *pVariantLayout = new QGridLayout(this);
    AssertReturnVoid(pVariantLayout);
    pVariantLayout->setContentsMargins(0, 0, 0, 0);
    pVariantLayout->setRowStretch(2, 1);
    m_pFixedCheckBox = new QCheckBox;
    m_pSplitBox = new QCheckBox;
    connect(m_pFixedCheckBox, &QCheckBox::toggled, this, &UIDiskVariantWidget::sltVariantChanged);
    connect(m_pSplitBox, &QCheckBox::toggled, this, &UIDiskVariantWidget::sltVariantChanged);
    pVariantLayout->addWidget(m_pFixedCheckBox, 0, 0);
    pVariantLayout->addWidget(m_pSplitBox, 1, 0);
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIDiskVariantWidget::sltRetranslateUI);
}

void UIDiskVariantWidget::sltRetranslateUI()
{
    if (m_pFixedCheckBox)
    {
        m_pFixedCheckBox->setText(tr("Pre-allocate &Full Size"));
        m_pFixedCheckBox->setToolTip(tr("When checked, the virtual disk image is allocated with its full size during VM creation time"));
    }
    if (m_pSplitBox)
    {
        m_pSplitBox->setText(tr("&Split Into 2GB Parts"));
        m_pSplitBox->setToolTip(tr("When checked, the virtual hard disk file is split into 2GB parts."));
    }
}

qulonglong UIDiskVariantWidget::mediumVariant() const
{
    /* Initial value: */
    qulonglong uMediumVariant = (qulonglong)KMediumVariant_Max;

    /* Exclusive options: */
    if (m_pFixedCheckBox && m_pFixedCheckBox->isChecked())
        uMediumVariant = (qulonglong)KMediumVariant_Fixed;
    else
        uMediumVariant = (qulonglong)KMediumVariant_Standard;

    /* Additional options: */
    if (m_pSplitBox && m_pSplitBox->isChecked())
        uMediumVariant |= (qulonglong)KMediumVariant_VmdkSplit2G;

    /* Return options: */
    return uMediumVariant;
}

void UIDiskVariantWidget::setMediumVariant(qulonglong uMediumVariant)
{
    /* Exclusive options: */
    if (uMediumVariant & (qulonglong)KMediumVariant_Fixed)
    {
        m_pFixedCheckBox->click();
        m_pFixedCheckBox->setFocus();
    }

    /* Additional options: */
    m_pSplitBox->setChecked(uMediumVariant & (qulonglong)KMediumVariant_VmdkSplit2G);
}

void UIDiskVariantWidget::updateMediumVariantWidgetsAfterFormatChange(const CMediumFormat &mediumFormat)
{
    AssertReturnVoid(m_pFixedCheckBox && m_pSplitBox);
    ULONG uCapabilities = 0;
    QVector<KMediumFormatCapabilities> capabilities;
    capabilities = mediumFormat.GetCapabilities();
    for (int i = 0; i < capabilities.size(); i++)
        uCapabilities |= capabilities[i];

    m_fIsCreateDynamicPossible = uCapabilities & KMediumFormatCapabilities_CreateDynamic;
    m_fIsCreateFixedPossible = uCapabilities & KMediumFormatCapabilities_CreateFixed;
    m_fIsCreateSplitPossible = uCapabilities & KMediumFormatCapabilities_CreateSplit2G;
    m_pFixedCheckBox->setEnabled(true);
    if (!m_fIsCreateDynamicPossible)
    {
        m_pFixedCheckBox->setChecked(true);
        m_pFixedCheckBox->setEnabled(false);
    }
    if (!m_fIsCreateFixedPossible)
    {
        m_pFixedCheckBox->setChecked(false);
        m_pFixedCheckBox->setEnabled(false);
    }

    m_pSplitBox->setEnabled(m_fIsCreateSplitPossible);
    if (!m_fIsCreateSplitPossible)
        m_pSplitBox->setChecked(false);
    emit sigMediumVariantChanged(mediumVariant());
}

bool UIDiskVariantWidget::isComplete() const
{
    /* Make sure medium variant is correct: */
    return mediumVariant() != (qulonglong)KMediumVariant_Max;
}

bool UIDiskVariantWidget::isCreateDynamicPossible() const
{
    return m_fIsCreateDynamicPossible;
}

bool UIDiskVariantWidget::isCreateFixedPossible() const
{
    return m_fIsCreateFixedPossible;
}

bool UIDiskVariantWidget::isCreateSplitPossible() const
{
    return m_fIsCreateSplitPossible;
}

void UIDiskVariantWidget::sltVariantChanged()
{
    emit sigMediumVariantChanged(mediumVariant());
}


/*********************************************************************************************************************************
*   UIMediumSizeAndPathGroupBox implementation.                                                                                  *
*********************************************************************************************************************************/

UIMediumSizeAndPathGroupBox::UIMediumSizeAndPathGroupBox(bool fExpertMode, QWidget *pParent, qulonglong uMinimumMediumSize)
    : QGroupBox(pParent)
    , m_pLocationEditor(0)
    , m_pLocationOpenButton(0)
    , m_pMediumSizeEditor(0)
    , m_pLocationLabel(0)
    , m_pSizeLabel(0)
    , m_fExpertMode(fExpertMode)
{
    prepare(uMinimumMediumSize);
}

bool UIMediumSizeAndPathGroupBox::pathExists() const
{
    /* Check if the path upto file name exists: */
    if (!QFileInfo(mediumPath()).exists())
    {
        m_pLocationEditor->mark(true, tr("Disk file path does not exists"), tr("Disk file path is valid"));
        return false;
    }
    m_pLocationEditor->mark(false, tr("Disk file path does not exists"), tr("Disk file path is valid"));
    return true;
}

bool UIMediumSizeAndPathGroupBox::filePathUnique() const
{
    /* Check if there is not such file already: */
    if (QFileInfo(mediumFilePath()).exists())
    {
        m_pLocationEditor->mark(true, tr("Disk file name is not unique"), tr("Disk file name is valid"));
        return false;
    }
    m_pLocationEditor->mark(false, tr("Disk file name is not unique"), tr("Disk file name is valid"));
    return true;
}

void UIMediumSizeAndPathGroupBox::prepare(qulonglong uMinimumMediumSize)
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    /* Location widgets: */
    if (!m_fExpertMode)
        m_pLocationLabel = new QIRichTextLabel(this);
    QHBoxLayout *pLocationLayout = new QHBoxLayout;
    m_pLocationEditor = new QILineEdit;
    m_pLocationOpenButton = new QIToolButton;
    if (m_pLocationOpenButton)
    {
        m_pLocationOpenButton->setAutoRaise(true);
        m_pLocationOpenButton->setIcon(UIIconPool::iconSet(":/select_file_16px.png", "select_file_disabled_16px.png"));
    }
    if (m_pLocationEditor)
    {
        m_pLocationEditor->setMarkable(true);
        m_pLocationEditor->setToolTip(tr("Holds the location of the virtual disk file."));
    }
    if (m_pLocationOpenButton)
        m_pLocationEditor->setToolTip(tr("Opens file selection dialog so that a location for the disk file can be selected."));
    pLocationLayout->addWidget(m_pLocationEditor);
    pLocationLayout->addWidget(m_pLocationOpenButton);

    /* Size widgets: */
    if (!m_fExpertMode)
        m_pSizeLabel = new QIRichTextLabel(this);
    m_pMediumSizeEditor = new UIMediumSizeEditor(0 /* parent */, uMinimumMediumSize);

    /* Add widgets to main layout: */
    if (m_pLocationLabel)
        pMainLayout->addWidget(m_pLocationLabel);
    pMainLayout->addLayout(pLocationLayout);

    if (m_pSizeLabel)
        pMainLayout->addWidget(m_pSizeLabel);
    pMainLayout->addWidget(m_pMediumSizeEditor);

    connect(m_pMediumSizeEditor, &UIMediumSizeEditor::sigSizeChanged,
            this, &UIMediumSizeAndPathGroupBox::sigMediumSizeChanged);

    connect(m_pLocationEditor, &QILineEdit::textChanged,
            this, &UIMediumSizeAndPathGroupBox::sigMediumPathChanged);

    connect(m_pLocationOpenButton, &QIToolButton::clicked,
            this, &UIMediumSizeAndPathGroupBox::sigMediumLocationButtonClicked);

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIMediumSizeAndPathGroupBox::sltRetranslateUI);
}
void UIMediumSizeAndPathGroupBox::sltRetranslateUI()
{
    if (m_fExpertMode)
        setTitle(tr("Hard Disk File Location and Size"));
    if (m_pLocationOpenButton)
    {
        m_pLocationOpenButton->setToolTip(tr("Specify a location for new virtual hard disk file..."));
        /* Some screen readers do no read tooltips: */
        m_pLocationOpenButton->setText(tr("Specify a location for new virtual hard disk file..."));
    }

    if (!m_fExpertMode && m_pLocationLabel)
        m_pLocationLabel->setText(tr("Please type the name of the new virtual hard disk file into the box below or "
                                                    "click on the folder icon to select a different folder to create the file in."));
    if (!m_fExpertMode && m_pSizeLabel)
        m_pSizeLabel->setText(tr("Select the size of the virtual hard disk in megabytes. "
                                                "This size is the limit on the amount of file data "
                                                "that a virtual machine will be able to store on the hard disk."));
}

QString UIMediumSizeAndPathGroupBox::mediumName() const
{
    if (!m_pLocationEditor)
        return QString();
    return QFileInfo(m_pLocationEditor->text()).completeBaseName();
}

QString UIMediumSizeAndPathGroupBox::mediumFilePath() const
{
    if (!m_pLocationEditor)
        return QString();
    return m_pLocationEditor->text();
}

void UIMediumSizeAndPathGroupBox::setMediumFilePath(const QString &strMediumPath)
{
    if (!m_pLocationEditor)
        return;
    m_pLocationEditor->setText(strMediumPath);
}

void UIMediumSizeAndPathGroupBox::updateMediumPath(const CMediumFormat &mediumFormat, const QStringList &formatExtensions,
                                                   KDeviceType enmDeviceType)
{
    /* Compose virtual-disk extension: */
    QString strDefaultExtension = UIWizardDiskEditors::defaultExtension(mediumFormat, enmDeviceType);
    /* Update m_pLocationEditor's text if necessary: */
    if (!m_pLocationEditor->text().isEmpty() && !strDefaultExtension.isEmpty())
    {
        QFileInfo fileInfo(m_pLocationEditor->text());
        if (fileInfo.suffix() != strDefaultExtension)
        {
            QFileInfo newFileInfo(QDir(fileInfo.absolutePath()),
                                  QString("%1.%2").
                                  arg(UIWizardDiskEditors::stripFormatExtension(fileInfo.fileName(), formatExtensions)).
                                  arg(strDefaultExtension));
            setMediumFilePath(newFileInfo.absoluteFilePath());
        }
    }
}

QString UIMediumSizeAndPathGroupBox::mediumPath() const
{
    if (!m_pLocationEditor)
        return QString();
    return QDir::toNativeSeparators(QFileInfo(m_pLocationEditor->text()).absolutePath());
}

qulonglong UIMediumSizeAndPathGroupBox::mediumSize() const
{
    if (m_pMediumSizeEditor)
        return m_pMediumSizeEditor->mediumSize();
    return 0;
}

void UIMediumSizeAndPathGroupBox::setMediumSize(qulonglong uSize)
{
    if (m_pMediumSizeEditor)
        return m_pMediumSizeEditor->setMediumSize(uSize);
}

/*********************************************************************************************************************************
*   UIDiskFormatBase implementation.                                                                                   *
*********************************************************************************************************************************/

UIDiskFormatBase::UIDiskFormatBase(KDeviceType enmDeviceType, bool fExpertMode)
    : m_enmDeviceType(enmDeviceType)
    , m_fExpertMode(fExpertMode)
{
}

UIDiskFormatBase::~UIDiskFormatBase()
{
}

const CMediumFormat &UIDiskFormatBase::VDIMediumFormat() const
{
    return m_comVDIMediumFormat;
}

void UIDiskFormatBase::populateFormats(){
    /* Enumerate medium formats in special order: */
    CSystemProperties properties = gpGlobalSession->virtualBox().GetSystemProperties();
    const QVector<CMediumFormat> &formats = properties.GetMediumFormats();
    QMap<QString, CMediumFormat> vdi, preferred, others;
    foreach (const CMediumFormat &format, formats)
    {
        if (format.GetName() == "VDI")
        {
            vdi[format.GetId()] = format;
            m_comVDIMediumFormat = format;
        }
        else
        {
            const QVector<KMediumFormatCapabilities> &capabilities = format.GetCapabilities();
            if (capabilities.contains(KMediumFormatCapabilities_Preferred))
                preferred[format.GetId()] = format;
            else
                others[format.GetId()] = format;
        }
    }

    /* Create buttons for VDI, preferred and others: */
    foreach (const QString &strId, vdi.keys())
        addFormat(vdi.value(strId), true);
    foreach (const QString &strId, preferred.keys())
        addFormat(preferred.value(strId), true);

    if (m_fExpertMode || m_enmDeviceType == KDeviceType_DVD || m_enmDeviceType == KDeviceType_Floppy)
    {
        foreach (const QString &strId, others.keys())
            addFormat(others.value(strId));
    }
}

void UIDiskFormatBase::addFormat(CMediumFormat medFormat, bool fPreferred /* = false */)
{
    AssertReturnVoid(!medFormat.isNull());
    /* Check that medium format supports creation: */
    ULONG uFormatCapabilities = 0;
    QVector<KMediumFormatCapabilities> capabilities;
    capabilities = medFormat.GetCapabilities();
    for (int i = 0; i < capabilities.size(); i++)
        uFormatCapabilities |= capabilities[i];

    if (!(uFormatCapabilities & KMediumFormatCapabilities_CreateFixed ||
          uFormatCapabilities & KMediumFormatCapabilities_CreateDynamic))
        return;

    /* Check that medium format supports creation of virtual hard-disks: */
    QVector<QString> fileExtensions;
    QVector<KDeviceType> deviceTypes;
    medFormat.DescribeFileExtensions(fileExtensions, deviceTypes);
    if (!deviceTypes.contains(m_enmDeviceType))
        return;
    m_formatList << Format(medFormat, UIWizardDiskEditors::defaultExtension(medFormat, m_enmDeviceType), fPreferred);
}

QStringList UIDiskFormatBase::formatExtensions() const
{
    QStringList extensionList;
    foreach (const Format &format, m_formatList)
        extensionList << format.m_strExtension;
    return extensionList;
}

bool UIDiskFormatBase::isExpertMode() const
{
    return m_fExpertMode;
}

/*********************************************************************************************************************************
*   UIDiskFormatsGroupBox implementation.                                                                                   *
*********************************************************************************************************************************/

UIDiskFormatsGroupBox::UIDiskFormatsGroupBox(bool fExpertMode, KDeviceType enmDeviceType, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , UIDiskFormatBase(enmDeviceType, fExpertMode)
    , m_pFormatButtonGroup(0)
    , m_pMainLayout(0)
{
    prepare();
}

CMediumFormat UIDiskFormatsGroupBox::mediumFormat() const
{
    if (!m_pFormatButtonGroup)
        return CMediumFormat();
    int iIndex = m_pFormatButtonGroup->checkedId();
    if (iIndex < 0 || iIndex >= m_formatList.size())
        return CMediumFormat();
    return m_formatList[iIndex].m_comFormat;
}

void UIDiskFormatsGroupBox::setMediumFormat(const CMediumFormat &mediumFormat)
{
    int iPosition = -1;
    for (int i = 0; i < m_formatList.size(); ++i)
    {
        if (mediumFormat == m_formatList[i].m_comFormat)
            iPosition = i;
    }
    if (iPosition >= 0)
    {
        m_pFormatButtonGroup->button(iPosition)->click();
        m_pFormatButtonGroup->button(iPosition)->setFocus();
    }
}

void UIDiskFormatsGroupBox::prepare()
{
    m_pMainLayout = new QVBoxLayout(this);
    m_pMainLayout->setContentsMargins(0, 0, 0, 0);
    populateFormats();
    createFormatWidgets();
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIDiskFormatsGroupBox::sltRetranslateUI);
}

void UIDiskFormatsGroupBox::sltRetranslateUI()
{
    QList<QAbstractButton*> buttons = m_pFormatButtonGroup ? m_pFormatButtonGroup->buttons() : QList<QAbstractButton*>();
    for (int i = 0; i < buttons.size(); ++i)
    {
        QAbstractButton *pButton = buttons[i];
        const CMediumFormat &format = m_formatList[m_pFormatButtonGroup->id(pButton)].m_comFormat;
        if (format.isNull())
            continue;
        UIMediumFormat enmFormat = gpConverter->fromInternalString<UIMediumFormat>(format.GetName());
        pButton->setText(gpConverter->toString(enmFormat));
    }
}

void UIDiskFormatsGroupBox::createFormatWidgets()
{
    AssertReturnVoid(m_pMainLayout);
    AssertReturnVoid(!m_formatList.isEmpty());
    m_pFormatButtonGroup = new QButtonGroup(this);
    AssertReturnVoid(m_pFormatButtonGroup);

    for (int i = 0; i < m_formatList.size(); ++i)
    {
        QRadioButton *pFormatButton = new QRadioButton;
        if (!pFormatButton)
            continue;

        /* Make the preferred button font bold: */
        if (m_formatList[i].m_fPreferred && isExpertMode())
        {
            QFont font = pFormatButton->font();
            font.setBold(true);
            pFormatButton->setFont(font);
        }
        m_pMainLayout->addWidget(pFormatButton);
        m_pFormatButtonGroup->addButton(pFormatButton, i);
    }

    setMediumFormat(m_formatList[0].m_comFormat);
    connect(m_pFormatButtonGroup, &QButtonGroup::buttonClicked,
            this, &UIDiskFormatsGroupBox::sigMediumFormatChanged);
}


/*********************************************************************************************************************************
*   UIDiskFormatsGroupBox implementation.                                                                                        *
*********************************************************************************************************************************/

UIDiskFormatsComboBox::UIDiskFormatsComboBox(bool fExpertMode, KDeviceType enmDeviceType, QWidget *pParent /* = 0 */)
    : QComboBox(pParent)
    , UIDiskFormatBase(enmDeviceType, fExpertMode)
{
    prepare();
}

void UIDiskFormatsComboBox::prepare()
{
    populateFormats();
    foreach (const Format &format, m_formatList)
    {
        addItem(format.m_comFormat.GetName());
    }

    connect(this, &QComboBox::currentIndexChanged,
            this, &UIDiskFormatsComboBox::sigMediumFormatChanged);

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIDiskFormatsComboBox::sltRetranslateUI);
}

CMediumFormat UIDiskFormatsComboBox::mediumFormat() const
{
    int iIndex = currentIndex();
    if (iIndex < 0 || iIndex >= m_formatList.size())
        return CMediumFormat();
    return m_formatList[iIndex].m_comFormat;
}

void UIDiskFormatsComboBox::setMediumFormat(const CMediumFormat &mediumFormat)
{
    int iPosition = -1;
    for (int i = 0; i < m_formatList.size(); ++i)
    {
        if (mediumFormat == m_formatList[i].m_comFormat)
            iPosition = i;
    }
    if (iPosition >= 0)
        setCurrentIndex(iPosition);
}

void UIDiskFormatsComboBox::sltRetranslateUI()
{
    for (int i = 0; i < count(); ++i)
    {
        if (i >= m_formatList.size())
            break;
        const CMediumFormat &format = m_formatList[i].m_comFormat;
        if (format.isNull())
            continue;
        UIMediumFormat enmFormat = gpConverter->fromInternalString<UIMediumFormat>(format.GetName());
        setItemText(i, gpConverter->toString(enmFormat));
    }
}
