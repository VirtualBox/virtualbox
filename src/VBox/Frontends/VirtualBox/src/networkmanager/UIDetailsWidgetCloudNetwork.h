/* $Id: UIDetailsWidgetCloudNetwork.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsWidgetCloudNetwork class declaration.
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

#ifndef FEQT_INCLUDED_SRC_networkmanager_UIDetailsWidgetCloudNetwork_h
#define FEQT_INCLUDED_SRC_networkmanager_UIDetailsWidgetCloudNetwork_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QDialog>

/* GUI includes: */
#include "QIManagerDialog.h"
#include "UIPortForwardingTable.h"

/* COM includes: */
#include "CVirtualSystemDescription.h"
#include "CVirtualSystemDescriptionForm.h"

/* Forward declarations: */
class QAbstractButton;
class QCheckBox;
class QComboBox;
class QGridLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QIDialogButtonBox;
class QILineEdit;
class QITabWidget;
class QIToolButton;
class UIFormEditorWidget;
class UINotificationCenter;


/** QDialog subclass for subnet selection functionality. */
class UISubnetSelectionDialog : public QDialog
{
    Q_OBJECT;

public:

    /** Constructs dialog passing @a pParent to the base-class.
      * @param  strProviderShortName  Brings the short provider name for cloud client being created.
      * @param  strProfileName        Brings the profile name for cloud client being created.
      * @param  strSubnetId           Brings the initial subnet ID to be cached. */
    UISubnetSelectionDialog(QWidget *pParent,
                            const QString &strProviderShortName,
                            const QString &strProfileName,
                            const QString &strSubnetId);
    /** Destructs dialog. */
    virtual ~UISubnetSelectionDialog() RT_OVERRIDE RT_FINAL;

    /** Returns cached subnet ID. */
    QString subnetId() const { return m_strSubnetId; }

public slots:

    /** Accepts dialog. */
    virtual void accept() RT_OVERRIDE RT_FINAL;

    /** Executes dialog. */
    virtual int exec() RT_OVERRIDE RT_FINAL;

private slots:

    /** Performs dialog initialization. */
    void sltInit();

    /** Handles notification about subnet selection @a comForm being created. */
    void sltHandleVSDFormCreated(const CVirtualSystemDescriptionForm &comForm);

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares all. */
    void prepare();
    /** Cleanups all. */
    void cleanup();

    /** Holds the short provider name for cloud client being created. */
    QString  m_strProviderShortName;
    /** Holds the profile name for cloud client being created. */
    QString  m_strProfileName;
    /** Holds the cached subnet ID. */
    QString  m_strSubnetId;

    /** Holds the virtual system description container. */
    CVirtualSystemDescription      m_comDescription;
    /** Holds the virtual system description form. */
    CVirtualSystemDescriptionForm  m_comForm;

    /** Holds the form editor instance. */
    UIFormEditorWidget *m_pFormEditor;
    /** Holds the button-box instance. */
    QIDialogButtonBox  *m_pButtonBox;

    /** Holds the notification-center instance. */
    UINotificationCenter *m_pNotificationCenter;
};


/** Network Manager: Cloud network data structure. */
struct UIDataCloudNetwork
{
    /** Constructs data. */
    UIDataCloudNetwork()
        : m_fExists(false)
        , m_fEnabled(true)
        , m_strName(QString())
        , m_strProvider(QString())
        , m_strProfile(QString())
        , m_strId(QString())
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataCloudNetwork &other) const
    {
        return true
               && (m_fExists == other.m_fExists)
               && (m_fEnabled == other.m_fEnabled)
               && (m_strName == other.m_strName)
               && (m_strProvider == other.m_strProvider)
               && (m_strProfile == other.m_strProfile)
               && (m_strId == other.m_strId)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataCloudNetwork &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataCloudNetwork &other) const { return !equal(other); }

    /** Holds whether this network is not NULL. */
    bool     m_fExists;
    /** Holds whether network is enabled. */
    bool     m_fEnabled;
    /** Holds network name. */
    QString  m_strName;
    /** Holds cloud provider name. */
    QString  m_strProvider;
    /** Holds cloud profile name. */
    QString  m_strProfile;
    /** Holds network id. */
    QString  m_strId;
};


/** Network Manager: Cloud network details-widget. */
class UIDetailsWidgetCloudNetwork : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies listeners about data changed and whether it @a fDiffers. */
    void sigDataChanged(bool fDiffers);

    /** Notifies listeners about data change rejected and should be reseted. */
    void sigDataChangeRejected();
    /** Notifies listeners about data change accepted and should be applied. */
    void sigDataChangeAccepted();

public:

    /** Constructs medium details dialog passing @a pParent to the base-class.
      * @param  enmEmbedding  Brings embedding type. */
    UIDetailsWidgetCloudNetwork(EmbedTo enmEmbedding, QWidget *pParent = 0);

    /** Returns the host network data. */
    const UIDataCloudNetwork &data() const { return m_newData; }
    /** Defines the host network @a data.
      * @param  busyNames  Holds the list of names busy by other
      *                    Cloud networks. */
    void setData(const UIDataCloudNetwork &data,
                 const QStringList &busyNames = QStringList());

    /** @name Change handling stuff.
      * @{ */
        /** Revalidates changes. */
        bool revalidate() const;

        /** Updates button states. */
        void updateButtonStates();
    /** @} */

private slots:

    /** @name Change handling stuff.
      * @{ */
        /** Handles network name text change. */
        void sltNetworkNameChanged(const QString &strText);
        /** Handles cloud provider name index change. */
        void sltCloudProviderNameChanged(int iIndex);
        /** Handles cloud profile name index change. */
        void sltCloudProfileNameChanged(int iIndex);
        /** Handles network id text change. */
        void sltNetworkIdChanged(const QString &strText);
        /** Handles request to list possible network ids. */
        void sltNetworkIdListRequested();

        /** Handles button-box button click. */
        void sltHandleButtonBoxClick(QAbstractButton *pButton);
    /** @} */

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** @name Prepare/cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares this. */
        void prepareThis();
        /** Prepares providers. */
        void prepareProviders();
        /** Prepares profiles. */
        void prepareProfiles();
    /** @} */

    /** @name Loading stuff.
      * @{ */
        /** Loads data. */
        void loadData();
    /** @} */

    /** @name General variables.
      * @{ */
        /** Holds the parent widget embedding type. */
        const EmbedTo m_enmEmbedding;

        /** Holds the old data copy. */
        UIDataCloudNetwork  m_oldData;
        /** Holds the new data copy. */
        UIDataCloudNetwork  m_newData;
    /** @} */

    /** @name Network variables.
      * @{ */
        /** Holds the network name label instance. */
        QLabel       *m_pLabelNetworkName;
        /** Holds the network name editor instance. */
        QLineEdit    *m_pEditorNetworkName;
        /** Holds the cloud provider name label instance. */
        QLabel       *m_pLabelProviderName;
        /** Holds the cloud provider name combo instance. */
        QComboBox    *m_pComboProviderName;
        /** Holds the cloud profile name label instance. */
        QLabel       *m_pLabelProfileName;
        /** Holds the cloud profile name combo instance. */
        QComboBox    *m_pComboProfileName;
        /** Holds the network id label instance. */
        QLabel       *m_pLabelNetworkId;
        /** Holds the network id editor instance. */
        QLineEdit    *m_pEditorNetworkId;
        /** Holds the network id list button instance. */
        QIToolButton *m_pButtonNetworkId;

        /** Holds the 'Options' button-box instance. */
        QIDialogButtonBox *m_pButtonBoxOptions;
        /** Holds the list of names busy by other
          * NAT networks. */
        QStringList        m_busyNames;
    /** @} */
};


#endif /* !FEQT_INCLUDED_SRC_networkmanager_UIDetailsWidgetCloudNetwork_h */
