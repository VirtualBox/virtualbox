/* $Id: UIWizardExportApp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardExportApp class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_exportappliance_UIWizardExportApp_h
#define FEQT_INCLUDED_SRC_wizards_exportappliance_UIWizardExportApp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizard.h"

/* COM includes: */
#include "CAppliance.h"
#include "CCloudClient.h"
#include "CVirtualSystemDescription.h"
#include "CVirtualSystemDescriptionForm.h"

/** MAC address export policies. */
enum MACAddressExportPolicy
{
    MACAddressExportPolicy_KeepAllMACs,
    MACAddressExportPolicy_StripAllNonNATMACs,
    MACAddressExportPolicy_StripAllMACs,
    MACAddressExportPolicy_MAX
};
Q_DECLARE_METATYPE(MACAddressExportPolicy);

/** Cloud export option modes. */
enum CloudExportMode
{
    CloudExportMode_Invalid,
    CloudExportMode_AskThenExport,
    CloudExportMode_ExportThenAsk,
    CloudExportMode_DoNotAsk
};
Q_DECLARE_METATYPE(CloudExportMode);

/** Export Appliance wizard. */
class UIWizardExportApp : public UINativeWizard
{
    Q_OBJECT;

public:

    /** Constructs Export Appliance wizard passing @a pParent to the base-class.
      * @param  predefinedMachineNames  Brings the predefined list of machine names.
      * @param  fFastTraverToExportOCI  Brings whether wizard should start with OCI target. */
    UIWizardExportApp(QWidget *pParent,
                      const QStringList &predefinedMachineNames = QStringList(),
                      bool fFastTraverToExportOCI = false);

    /** @name Common fields.
      * @{ */
        /** Returns a list of machine names. */
        QStringList machineNames() const { return m_machineNames; }
        /** Returns a list of machine IDs. */
        QList<QUuid> machineIDs() const { return m_machineIDs; }

        /** Returns format. */
        QString format() const { return m_strFormat; }

        /** Returns whether format is cloud one. */
        bool isFormatCloudOne() const { return m_fFormatCloudOne; }
    /** @} */

    /** @name Local export fields.
      * @{ */
        /** Returns path. */
        QString path() const { return m_strPath; }

        /** Returns MAC address export policy. */
        MACAddressExportPolicy macAddressExportPolicy() const { return m_enmMACAddressExportPolicy; }

        /** Returns whether manifest is selected. */
        bool isManifestSelected() const { return m_fManifestSelected; }

        /** Returns whether include ISOs is selected. */
        bool isIncludeISOsSelected() const { return m_fIncludeISOsSelected; }

        /** Returns local appliance object. */
        CAppliance localAppliance() const { return m_comLocalAppliance; }
    /** @} */

    /** @name Cloud export fields.
      * @{ */
        /** Returns profile name. */
        QString profileName() const { return m_strProfileName; }

        /** Returns cloud appliance object. */
        CAppliance cloudAppliance() const { return m_comCloudAppliance; }

        /** Returns cloud client object. */
        CCloudClient cloudClient() const { return m_comCloudClient; }

        /** Returns virtual system description object. */
        CVirtualSystemDescription vsd() const { return m_comVsd; }

        /** Returns virtual system description export form object. */
        CVirtualSystemDescriptionForm vsdExportForm() const { return m_comVsdExportForm; }

        /** Returns virtual system description launch form object. */
        CVirtualSystemDescriptionForm vsdLaunchForm() const { return m_comVsdLaunchForm; }

        /** Returns cloud export mode. */
        CloudExportMode cloudExportMode() const { return m_enmCloudExportMode; }
    /** @} */

    /** @name Auxiliary stuff.
      * @{ */
        /** Goes forward. Required for fast travel to next page. */
        void goForward();

        /** Disables basic/expert and next/back buttons. */
        void disableButtons();

        /** Composes universal resource identifier.
          * @param  fWithFile  Brings whether uri should include file name as well. */
        QString uri(bool fWithFile = true) const;

        /** Exports Appliance. */
        bool exportAppliance();

        /** Creates VSD Form. */
        void createVsdLaunchForm();

        /** Creates New Cloud VM. */
        bool createCloudVM();
    /** @} */

public slots:

    /** @name Common fields.
      * @{ */
        /** Defines a list of machine @a names. */
        void setMachineNames(const QStringList &names) { m_machineNames = names; }
        /** Defines a list of machine @a ids. */
        void setMachineIDs(const QList<QUuid> &ids) { m_machineIDs = ids; }

        /** Defines @a strFormat. */
        void setFormat(const QString &strFormat) { m_strFormat = strFormat; }

        /** Defines whether format is @a fCloudOne. */
        void setFormatCloudOne(bool fCloudOne) { m_fFormatCloudOne = fCloudOne; }
    /** @} */

    /** @name Local export fields.
      * @{ */
        /** Defines @a strPath. */
        void setPath(const QString &strPath) { m_strPath = strPath; }

        /** Defines MAC address export @a enmPolicy. */
        void setMACAddressExportPolicy(MACAddressExportPolicy enmPolicy) { m_enmMACAddressExportPolicy = enmPolicy; }

        /** Defines whether manifest is @a fSelected. */
        void setManifestSelected(bool fSelected) { m_fManifestSelected = fSelected; }

        /** Defines whether include ISOs is @a fSelected. */
        void setIncludeISOsSelected(bool fSelected) { m_fIncludeISOsSelected = fSelected; }

        /** Defines local @a comAppliance object. */
        void setLocalAppliance(const CAppliance &comAppliance) { m_comLocalAppliance = comAppliance; }
    /** @} */

    /** @name Cloud export fields.
      * @{ */
        /** Defines profile @a strName. */
        void setProfileName(const QString &strName) { m_strProfileName = strName; }

        /** Defines cloud @a comAppliance object. */
        void setCloudAppliance(const CAppliance &comAppliance) { m_comCloudAppliance = comAppliance; }

        /** Defines cloud @a comClient object. */
        void setCloudClient(const CCloudClient &comClient) { m_comCloudClient = comClient; }

        /** Defines virtual system @a comDescription object. */
        void setVsd(const CVirtualSystemDescription &comDescription) { m_comVsd = comDescription; }

        /** Defines virtual system description export @a comForm object. */
        void setVsdExportForm(const CVirtualSystemDescriptionForm &comForm) { m_comVsdExportForm = comForm; }

        /** Defines virtual system description launch @a comForm object. */
        void setVsdLaunchForm(const CVirtualSystemDescriptionForm &comForm) { m_comVsdLaunchForm = comForm; }

        /** Defines cloud export @a enmMode. */
        void setCloudExportMode(const CloudExportMode &enmMode) { m_enmCloudExportMode = enmMode; }
    /** @} */

protected:

    /** @name Virtual stuff.
      * @{ */
        /** Populates pages. */
        virtual void populatePages() RT_OVERRIDE RT_FINAL;
    /** @} */

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** @name Auxiliary stuff.
      * @{ */
        /** Exports VMs enumerated in @a comAppliance. */
        bool exportVMs(CAppliance &comAppliance);
    /** @} */

    /** @name Arguments.
      * @{ */
        /** Holds the predefined list of machine names. */
        QStringList  m_predefinedMachineNames;
        /** Holds whether we should fast travel to page 2. */
        bool         m_fFastTraverToExportOCI;
    /** @} */

    /** @name Common fields.
      * @{ */
        /** Holds the list of machine names. */
        QStringList   m_machineNames;
        /** Holds the list of machine IDs. */
        QList<QUuid>  m_machineIDs;

        /** Holds the format. */
        QString  m_strFormat;
        /** Holds whether format is cloud one. */
        bool     m_fFormatCloudOne;
    /** @} */

    /** @name Local export fields.
      * @{ */
        /** Holds the path. */
        QString                 m_strPath;
        /** Holds the MAC address export policy. */
        MACAddressExportPolicy  m_enmMACAddressExportPolicy;
        /** Holds whether manifest is selected. */
        bool                    m_fManifestSelected;
        /** Holds whether ISOs are included. */
        bool                    m_fIncludeISOsSelected;
        /** Holds local appliance object. */
        CAppliance              m_comLocalAppliance;
    /** @} */

    /** @name Cloud export fields.
      * @{ */
        /** Holds profile name. */
        QString                        m_strProfileName;
        /** Holds cloud appliance object. */
        CAppliance                     m_comCloudAppliance;
        /** Returns cloud client object. */
        CCloudClient                   m_comCloudClient;
        /** Returns virtual system description object. */
        CVirtualSystemDescription      m_comVsd;
        /** Returns virtual system description export form object. */
        CVirtualSystemDescriptionForm  m_comVsdExportForm;
        /** Returns virtual system description launch form object. */
        CVirtualSystemDescriptionForm  m_comVsdLaunchForm;
        /** Returns cloud export mode. */
        CloudExportMode                m_enmCloudExportMode;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_wizards_exportappliance_UIWizardExportApp_h */
