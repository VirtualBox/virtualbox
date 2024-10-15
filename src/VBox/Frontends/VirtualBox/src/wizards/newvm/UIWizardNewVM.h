/* $Id: UIWizardNewVM.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVM class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVM_h
#define FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVM_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizard.h"

/* COM includes: */
#include "CMachine.h"
#include "CMedium.h"
#include "CMediumFormat.h"
#include "CUnattended.h"

/* Forward declarations: */
class UIActionPool;

enum SelectedDiskSource
{
    SelectedDiskSource_Empty = 0,
    SelectedDiskSource_New,
    SelectedDiskSource_Existing,
    SelectedDiskSource_Max
};

/** New Virtual Machine wizard: */
class UIWizardNewVM : public UINativeWizard
{
    Q_OBJECT;

public:

    UIWizardNewVM(QWidget *pParent,
                  UIActionPool *pActionPool,
                  const QString &strMachineGroup,
                  const QString &strISOFilePath = QString());

    bool isUnattendedEnabled() const;
    bool isUnattendedInstallSupported() const;
    bool isGuestOSTypeWindows() const;

    bool createVM();
    bool createVirtualDisk();

    CMedium &virtualDisk();
    void setVirtualDisk(const CMedium &medium);
    void setVirtualDisk(const QUuid &mediumId);

    const QString &machineGroup() const;
    QUuid createdMachineId() const;

    CUnattended installer() const { return m_comUnattended; }

    /** @name Setter/getters for vm parameters
      * @{ */
        const QString &machineFilePath() const;
        void setMachineFilePath(const QString &strMachineFilePath);

        /* The name of the .vbox file. Obtained from machineFilePath(). Unlike machine base name it cannot have characters like / etc. */
        QString machineFileName() const;

        const QString &machineFolder() const;
        void setMachineFolder(const QString &strMachineFolder);

        const QString &machineBaseName() const;
        void setMachineBaseName(const QString &strMachineBaseName);

        const QString &createdMachineFolder() const;
        void setCreatedMachineFolder(const QString &strCreatedMachineFolder);

        QString detectedOSTypeId() const;

        const QString &guestOSFamilyId() const;
        void setGuestOSFamilyId(const QString &strGuestOSFamilyId);

        const QString &guestOSTypeId() const;
        void setGuestOSTypeId(const QString &guestOSType);

        bool installGuestAdditions() const;
        void setInstallGuestAdditions(bool fInstallGA);

        bool startHeadless() const;
        void setStartHeadless(bool fStartHeadless);

        bool skipUnattendedInstall() const;
        void setSkipUnattendedInstall(bool fSkipUnattendedInstall);

        bool EFIEnabled() const;
        void setEFIEnabled(bool fEnabled);

        QString ISOFilePath() const;
        void setISOFilePath(const QString &strISOFilePath);

        QString userName() const;
        void setUserName(const QString &strUserName);

        QString password() const;
        void setPassword(const QString &strPassword);

        QString guestAdditionsISOPath() const;
        void setGuestAdditionsISOPath(const QString &strGAISOPath);

        QString hostnameDomainName() const;
        void setHostnameDomainName(const QString &strHostnameDomainName);

        QString productKey() const;
        void setProductKey(const QString &productKey);

        int CPUCount() const;
        void setCPUCount(int iCPUCount);

        int memorySize() const;
        void setMemorySize(int iMemory);

        qulonglong mediumVariant() const;
        void setMediumVariant(qulonglong uMediumVariant);

        const CMediumFormat &mediumFormat();
        void setMediumFormat(const CMediumFormat &mediumFormat);

        const QString &mediumPath() const;
        void setMediumPath(const QString &strMediumPath);

        qulonglong mediumSize() const;
        void setMediumSize(qulonglong mediumSize);

        SelectedDiskSource diskSource() const;
        void setDiskSource(SelectedDiskSource enmDiskSource);

        bool emptyDiskRecommended() const;
        void setEmptyDiskRecommended(bool fEmptyDiskRecommended);

        void setDetectedWindowsImageNamesAndIndices(const QVector<QString> &names, const QVector<ulong> &ids);
        const QVector<QString> &detectedWindowsImageNames() const;
        const QVector<ulong> &detectedWindowsImageIndices() const;

        void setSelectedWindowImageIndex(ulong uIndex);
        ulong selectedWindowImageIndex() const;

        QVector<KMediumVariant> mediumVariants() const;

       QString getGuestOSTypeDescription() const;
    /** @} */

protected:

    /** Populates pages. */
    virtual void populatePages() RT_OVERRIDE RT_FINAL;
    virtual void cleanWizard() RT_OVERRIDE RT_FINAL;
    bool attachDefaultDevices();

private slots:

    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    QString getNextControllerName(KStorageBus type);
    void setUnattendedPageVisible(bool fVisible);
    void deleteVirtualDisk();
    bool checkUnattendedInstallError(const CUnattended &comUnattended) const;
    /** @name Variables
     * @{ */
       CMedium m_virtualDisk;
       CMachine m_machine;
       QString m_strMachineGroup;
       int m_iIDECount;
       int m_iSATACount;
       int m_iSCSICount;
       int m_iFloppyCount;
       int m_iSASCount;
       int m_iUSBCount;

       /** Path of the folder created by this wizard page. Used to remove previously created
         *  folder. see cleanupMachineFolder();*/
       QString m_strCreatedFolder;

       /** Full path (including the file name) of the machine's configuration file. */
       QString m_strMachineFilePath;
       /** Path of the folder hosting the machine's configuration file. Generated from m_strMachineFilePath. */
       QString m_strMachineFolder;
       /** Base name of the machine. Can include characters / or \. */
       QString m_strMachineBaseName;

       /* Name and index lists of the images detected from an ISO. Currently only for Windows ISOs. */
       QVector<QString> m_detectedWindowsImageNames;
       QVector<ulong> m_detectedWindowsImageIndices;

       /** Holds the VM OS family ID. */
       QString  m_strGuestOSFamilyId;
       /** Holds the VM OS type. */
       QString m_guestOSTypeId;

       /** True if guest additions are to be installed during unattended install. */
       bool m_fInstallGuestAdditions;
       bool m_fSkipUnattendedInstall;
       bool m_fEFIEnabled;

       int m_iCPUCount;
       int m_iMemorySize;
       int m_iUnattendedInstallPageIndex;

       qulonglong m_uMediumVariant;
       CMediumFormat m_comMediumFormat;
       QString m_strMediumPath;
       qulonglong m_uMediumSize;
       SelectedDiskSource m_enmDiskSource;
       bool m_fEmptyDiskRecommended;
       QVector<KMediumVariant> m_mediumVariants;
       UIActionPool *m_pActionPool;
       CUnattended m_comUnattended;
       bool m_fStartHeadless;
       QString m_strInitialISOFilePath;
    /** @} */
};

typedef QPointer<UIWizardNewVM> UISafePointerWizardNewVM;

#endif /* !FEQT_INCLUDED_SRC_wizards_newvm_UIWizardNewVM_h */
