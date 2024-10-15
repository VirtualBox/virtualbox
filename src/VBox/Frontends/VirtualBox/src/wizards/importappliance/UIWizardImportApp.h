/* $Id: UIWizardImportApp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardImportApp class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportApp_h
#define FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportApp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizard.h"

/* COM includes: */
#include "CAppliance.h"
#include "CVirtualSystemDescriptionForm.h"

/** MAC address policies. */
enum MACAddressImportPolicy
{
    MACAddressImportPolicy_KeepAllMACs,
    MACAddressImportPolicy_KeepNATMACs,
    MACAddressImportPolicy_StripAllMACs,
    MACAddressImportPolicy_MAX
};
Q_DECLARE_METATYPE(MACAddressImportPolicy);

/** Import Appliance wizard. */
class UIWizardImportApp : public UINativeWizard
{
    Q_OBJECT;

public:

    /** Constructs Import Appliance wizard passing @a pParent to the base-class.
      * @param  fImportFromOCIByDefault  Brings whether wizard should start with OCI target.
      * @param  strFileName              Brings local file name to import OVF/OVA from. */
    UIWizardImportApp(QWidget *pParent,
                      bool fImportFromOCIByDefault,
                      const QString &strFileName);

    /** @name Common fields.
      * @{ */
        /** Returns whether source is cloud one. */
        bool isSourceCloudOne() const { return m_fSourceCloudOne; }
        /** Defines whether source is @a fCloudOne. */
        void setSourceCloudOne(bool fCloudOne) { m_fSourceCloudOne = fCloudOne; }
    /** @} */

    /** @name Local import fields.
      * @{ */
        /** Returns local Appliance object. */
        CAppliance localAppliance() const { return m_comLocalAppliance; }
        /** Defines file @a strName. */
        bool setFile(const QString &strName);

        /** Returns MAC address import policy. */
        MACAddressImportPolicy macAddressImportPolicy() const { return m_enmMacAddressImportPolicy; }
        /** Defines MAC address import @a enmPolicy. */
        void setMACAddressImportPolicy(MACAddressImportPolicy enmPolicy) { m_enmMacAddressImportPolicy = enmPolicy; }

        /** Returns whether hard disks should be imported as VDIs. */
        bool isImportHDsAsVDI() const { return m_fImportHDsAsVDI; }
        /** Defines whether hard disks should be imported @a fAsVDI. */
        void setImportHDsAsVDI(bool fAsVDI) { m_fImportHDsAsVDI = fAsVDI; }
    /** @} */

    /** @name Cloud import fields.
      * @{ */
        /** Returns cloud Appliance object. */
        CAppliance cloudAppliance() const { return m_comCloudAppliance; }
        /** Defines cloud @a comAppliance object. */
        void setCloudAppliance(const CAppliance &comAppliance) { m_comCloudAppliance = comAppliance; }

        /** Returns Virtual System Description import form object. */
        CVirtualSystemDescriptionForm vsdImportForm() const { return m_comVsdImportForm; }
        /** Defines Virtual System Description import @a comForm object. */
        void setVsdImportForm(const CVirtualSystemDescriptionForm &comForm) { m_comVsdImportForm = comForm; }
    /** @} */

    /** @name Auxiliary stuff.
      * @{ */
        /** Imports appliance. */
        bool importAppliance();
    /** @} */

protected:

    /** @name Inherited stuff.
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
        /** Returns a list of license agreement pairs. */
        QList<QPair<QString, QString> > licenseAgreements() const;
    /** @} */

    /** @name Arguments.
      * @{ */
        /** Holds whether default source should be Import from OCI. */
        bool     m_fImportFromOCIByDefault;
        /** Handles the appliance file name. */
        QString  m_strFileName;
    /** @} */

    /** @name Common fields.
      * @{ */
        /** */
        bool  m_fSourceCloudOne;
    /** @} */

    /** @name Local import fields.
      * @{ */
        /** Holds the local appliance wrapper object. */
        CAppliance  m_comLocalAppliance;

        /** Holds the MAC address import policy. */
        MACAddressImportPolicy  m_enmMacAddressImportPolicy;

        /** Holds whether hard disks should be imported as VDIs. */
        bool  m_fImportHDsAsVDI;
    /** @} */

    /** @name Cloud import fields.
      * @{ */
        /** Holds the cloud appliance wrapper object. */
        CAppliance  m_comCloudAppliance;

        /** Holds the Virtual System Description import form wrapper object. */
        CVirtualSystemDescriptionForm  m_comVsdImportForm;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_wizards_importappliance_UIWizardImportApp_h */
