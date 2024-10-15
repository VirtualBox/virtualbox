/* $Id: UIWizardAddCloudVM.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardAddCloudVM class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_addcloudvm_UIWizardAddCloudVM_h
#define FEQT_INCLUDED_SRC_wizards_addcloudvm_UIWizardAddCloudVM_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UINativeWizard.h"

/* COM includes: */
#include "CCloudClient.h"

/** Add Cloud VM wizard. */
class UIWizardAddCloudVM : public UINativeWizard
{
    Q_OBJECT;

public:

    /** Constructs Add Cloud VM wizard passing @a pParent to the base-class.
      * @param  strFullGroupName  Brings full group name (/provider/profile) to add VM to. */
    UIWizardAddCloudVM(QWidget *pParent, const QString &strFullGroupName = QString());

    /** Defines @a strProviderShortName. */
    void setProviderShortName(const QString &strProviderShortName) { m_strProviderShortName = strProviderShortName; }
    /** Returns provider short name. */
    QString providerShortName() const { return m_strProviderShortName; }

    /** Defines @a strProfileName. */
    void setProfileName(const QString &strProfileName) { m_strProfileName = strProfileName; }
    /** Returns profile name. */
    QString profileName() const { return m_strProfileName; }

    /** Defines @a instanceIds. */
    void setInstanceIds(const QStringList &instanceIds) { m_instanceIds = instanceIds; }
    /** Returns instance IDs. */
    QStringList instanceIds() const { return m_instanceIds; }

    /** Defines Cloud @a comClient object wrapper. */
    void setClient(const CCloudClient &comClient) { m_comClient = comClient; }
    /** Returns Cloud Client object wrapper. */
    CCloudClient client() const { return m_comClient; }

    /** Adds cloud VMs. */
    bool addCloudVMs();

protected:

    /** Populates pages. */
    virtual void populatePages() RT_OVERRIDE RT_FINAL;

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Holds the short provider name. */
    QString       m_strProviderShortName;
    /** Holds the profile name. */
    QString       m_strProfileName;
    /** Holds the instance ids. */
    QStringList   m_instanceIds;
    /** Holds the Cloud Client object wrapper. */
    CCloudClient  m_comClient;
};

/** Safe pointer to add cloud vm wizard. */
typedef QPointer<UIWizardAddCloudVM> UISafePointerWizardAddCloudVM;

#endif /* !FEQT_INCLUDED_SRC_wizards_addcloudvm_UIWizardAddCloudVM_h */
