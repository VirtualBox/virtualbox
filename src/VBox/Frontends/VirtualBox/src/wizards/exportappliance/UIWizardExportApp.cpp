/* $Id: UIWizardExportApp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardExportApp class implementation.
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
#include <QFileInfo>
#include <QPushButton>
#include <QVariant>

/* GUI includes: */
#include "UIAddDiskEncryptionPasswordDialog.h"
#include "UICloudMachineManager.h"
#include "UIMessageCenter.h"
#include "UIModalWindowManager.h"
#include "UINotificationCenter.h"
#include "UIWizardExportApp.h"
#include "UIWizardExportAppPageExpert.h"
#include "UIWizardExportAppPageFormat.h"
#include "UIWizardExportAppPageSettings.h"
#include "UIWizardExportAppPageVMs.h"

/* COM includes: */
#include "CAppliance.h"
#include "CVFSExplorer.h"


UIWizardExportApp::UIWizardExportApp(QWidget *pParent,
                                     const QStringList &predefinedMachineNames /* = QStringList() */,
                                     bool fFastTraverToExportOCI /* = false */)
    : UINativeWizard(pParent, WizardType_ExportAppliance,
                     fFastTraverToExportOCI ? "cloud-export-oci" : "ovf")
    , m_predefinedMachineNames(predefinedMachineNames)
    , m_fFastTraverToExportOCI(fFastTraverToExportOCI)
    , m_fFormatCloudOne(false)
    , m_enmMACAddressExportPolicy(MACAddressExportPolicy_KeepAllMACs)
    , m_fManifestSelected(false)
    , m_fIncludeISOsSelected(false)
    , m_enmCloudExportMode(CloudExportMode_DoNotAsk)
{
#ifndef VBOX_WS_MAC
    /* Assign watermark: */
    setPixmapName(":/wizard_ovf_export.png");
#else
    /* Assign background image: */
    setPixmapName(":/wizard_ovf_export_bg.png");
#endif
}

void UIWizardExportApp::goForward()
{
    wizardButton(WizardButtonType_Next)->click();
}

void UIWizardExportApp::disableButtons()
{
    wizardButton(WizardButtonType_Back)->setEnabled(false);
    wizardButton(WizardButtonType_Next)->setEnabled(false);
}

QString UIWizardExportApp::uri(bool fWithFile) const
{
    /* For Cloud formats: */
    if (isFormatCloudOne())
        return QString("%1://").arg(format());
    else
    {
        /* Prepare storage path: */
        QString strPath = path();
        /* Append file name if requested: */
        if (!fWithFile)
        {
            QFileInfo fi(strPath);
            strPath = fi.path();
        }

        /* Just path by default: */
        return strPath;
    }
}

bool UIWizardExportApp::exportAppliance()
{
    /* Check whether there was cloud target selected: */
    if (isFormatCloudOne())
    {
        /* Get appliance: */
        CAppliance comAppliance = cloudAppliance();
        AssertReturn(comAppliance.isNotNull(), false);

        /* Export the VMs, on success we are finished: */
        return exportVMs(comAppliance);
    }
    else
    {
        /* Get appliance: */
        CAppliance comAppliance = localAppliance();
        AssertReturn(comAppliance.isNotNull(), false);

        /* We need to know every filename which will be created, so that we can ask the user for confirmation of overwriting.
         * For that we iterating over all virtual systems & fetch all descriptions of the type HardDiskImage. Also add the
         * manifest file to the check. In the .ova case only the target file itself get checked. */

        /* Compose a list of all required files: */
        QFileInfo fi(path());
        QVector<QString> files;

        /* Add arhive itself: */
        files << fi.fileName();

        /* If archive is of .ovf type: */
        if (fi.suffix().toLower() == "ovf")
        {
            /* Add manifest file if requested: */
            if (isManifestSelected())
                files << fi.baseName() + ".mf";

            /* Add all hard disk images: */
            CVirtualSystemDescriptionVector vsds = comAppliance.GetVirtualSystemDescriptions();
            for (int i = 0; i < vsds.size(); ++i)
            {
                QVector<KVirtualSystemDescriptionType> types;
                QVector<QString> refs, origValues, configValues, extraConfigValues;
                vsds[i].GetDescriptionByType(KVirtualSystemDescriptionType_HardDiskImage, types,
                                             refs, origValues, configValues, extraConfigValues);
                foreach (const QString &strValue, origValues)
                    files << QString("%2").arg(strValue);
            }
        }

        /* Initialize VFS explorer: */
        CVFSExplorer comExplorer = comAppliance.CreateVFSExplorer(uri(false /* fWithFile */));
        if (!comAppliance.isOk())
        {
            UINotificationMessage::cannotCreateVfsExplorer(comAppliance, notificationCenter());
            return false;
        }

        /* Update VFS explorer: */
        UINotificationProgressVFSExplorerUpdate *pNotification =
            new UINotificationProgressVFSExplorerUpdate(comExplorer);
        if (!handleNotificationProgressNow(pNotification))
            return false;

        /* Confirm overwriting for existing files: */
        QVector<QString> exists = comExplorer.Exists(files);
        if (!msgCenter().confirmOverridingFiles(exists, this))
            return false;

        /* DELETE all the files which exists after everything is confirmed: */
        if (!exists.isEmpty())
        {
            /* Remove files with VFS explorer: */
            UINotificationProgressVFSExplorerFilesRemove *pNotification =
                new UINotificationProgressVFSExplorerFilesRemove(comExplorer, exists);
            if (!handleNotificationProgressNow(pNotification))
                return false;
        }

        /* Export the VMs, on success we are finished: */
        return exportVMs(comAppliance);
    }
}

void UIWizardExportApp::createVsdLaunchForm()
{
    /* Acquire prepared client and description: */
    CCloudClient comClient = cloudClient();
    CVirtualSystemDescription comVSD = vsd();
    AssertReturnVoid(comClient.isNotNull() && comVSD.isNotNull());

    /* Create launch VSD form: */
    UINotificationProgressLaunchVSDFormCreate *pNotification = new UINotificationProgressLaunchVSDFormCreate(comClient,
                                                                                                             comVSD,
                                                                                                             format(),
                                                                                                             profileName());
    connect(pNotification, &UINotificationProgressLaunchVSDFormCreate::sigVSDFormCreated,
            this, &UIWizardExportApp::setVsdLaunchForm);
    handleNotificationProgressNow(pNotification);
}

bool UIWizardExportApp::createCloudVM()
{
    /* Acquire prepared client and description: */
    CCloudClient comClient = cloudClient();
    CVirtualSystemDescription comVSD = vsd();
    AssertReturn(comClient.isNotNull() && comVSD.isNotNull(), false);

    /* Initiate cloud VM creation procedure: */
    CCloudMachine comMachine;

    /* Create cloud VM: */
    UINotificationProgressCloudMachineCreate *pNotification = new UINotificationProgressCloudMachineCreate(comClient,
                                                                                                           comMachine,
                                                                                                           comVSD,
                                                                                                           format(),
                                                                                                           profileName());
    connect(pNotification, &UINotificationProgressCloudMachineCreate::sigCloudMachineCreated,
            gpCloudMachineManager, &UICloudMachineManager::sltHandleCloudMachineAdded);
    gpNotificationCenter->append(pNotification);

    /* Return result: */
    return true;
}

void UIWizardExportApp::populatePages()
{
    /* Create corresponding pages: */
    switch (mode())
    {
        case WizardMode_Basic:
        {
            addPage(new UIWizardExportAppPageVMs(m_predefinedMachineNames, m_fFastTraverToExportOCI));
            addPage(new UIWizardExportAppPageFormat(m_fFastTraverToExportOCI));
            addPage(new UIWizardExportAppPageSettings);
            break;
        }
        case WizardMode_Expert:
        {
            addPage(new UIWizardExportAppPageExpert(m_predefinedMachineNames, m_fFastTraverToExportOCI));
            break;
        }
        default:
        {
            AssertMsgFailed(("Invalid mode: %d", mode()));
            break;
        }
    }
}

void UIWizardExportApp::sltRetranslateUI()
{
    /* Call to base-class: */
    UINativeWizard::sltRetranslateUI();

    /* Translate wizard: */
    setWindowTitle(tr("Export Virtual Appliance"));
    /// @todo implement this?
    //setButtonText(QWizard::FinishButton, tr("Export"));
}

bool UIWizardExportApp::exportVMs(CAppliance &comAppliance)
{
    /* Get the map of the password IDs: */
    EncryptedMediumMap encryptedMedia;
    foreach (const QString &strPasswordId, comAppliance.GetPasswordIds())
        foreach (const QUuid &uMediumId, comAppliance.GetMediumIdsForPasswordId(strPasswordId))
            encryptedMedia.insert(strPasswordId, uMediumId);

    /* Ask for the disk encryption passwords if necessary: */
    if (!encryptedMedia.isEmpty())
    {
        /* Modal dialog can be destroyed in own event-loop as a part of application
         * termination procedure. We have to make sure that the dialog pointer is
         * always up to date. So we are wrapping created dialog with QPointer. */
        QPointer<UIAddDiskEncryptionPasswordDialog> pDlg =
            new UIAddDiskEncryptionPasswordDialog(this,
                                                  window()->windowTitle(),
                                                  encryptedMedia);

        /* Execute the dialog: */
        if (pDlg->exec() != QDialog::Accepted)
        {
            /* Delete the dialog: */
            delete pDlg;
            return false;
        }

        /* Acquire the passwords provided: */
        const EncryptionPasswordMap encryptionPasswords = pDlg->encryptionPasswords();

        /* Delete the dialog: */
        delete pDlg;

        /* Provide appliance with passwords if possible: */
        comAppliance.AddPasswords(encryptionPasswords.keys().toVector(),
                                  encryptionPasswords.values().toVector());
        if (!comAppliance.isOk())
        {
            UINotificationMessage::cannotAddDiskEncryptionPassword(comAppliance, notificationCenter());
            return false;
        }
    }

    /* Prepare export options: */
    QVector<KExportOptions> options;
    switch (macAddressExportPolicy())
    {
        case MACAddressExportPolicy_StripAllNonNATMACs: options.append(KExportOptions_StripAllNonNATMACs); break;
        case MACAddressExportPolicy_StripAllMACs: options.append(KExportOptions_StripAllMACs); break;
        default: break;
    }
    if (isManifestSelected())
        options.append(KExportOptions_CreateManifest);
    if (isIncludeISOsSelected())
        options.append(KExportOptions_ExportDVDImages);

    /* Is this VM being exported to cloud? */
    if (isFormatCloudOne())
    {
        /* Export appliance: */
        UINotificationProgressApplianceWrite *pNotification = new UINotificationProgressApplianceWrite(comAppliance,
                                                                                                       format(),
                                                                                                       options,
                                                                                                       uri());
        if (cloudExportMode() == CloudExportMode_DoNotAsk)
            gpNotificationCenter->append(pNotification);
        else
            handleNotificationProgressNow(pNotification);
    }
    /* Is this VM being exported locally? */
    else
    {
        /* Export appliance: */
        UINotificationProgressApplianceWrite *pNotification = new UINotificationProgressApplianceWrite(comAppliance,
                                                                                                       format(),
                                                                                                       options,
                                                                                                       uri());
        gpNotificationCenter->append(pNotification);
    }

    /* Success finally: */
    return true;
}
