/* $Id: UICloudProfileDetailsWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudProfileDetailsWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_cloud_profilemanager_UICloudProfileDetailsWidget_h
#define FEQT_INCLUDED_SRC_cloud_profilemanager_UICloudProfileDetailsWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QUuid>
#include <QWidget>

/* GUI includes: */
#include "QIManagerDialog.h"

/* Forward declarations: */
class QAbstractButton;
class QLabel;
class QLineEdit;
class QTableWidgetItem;
class QIDialogButtonBox;
class QITableWidget;


/** Cloud Provider data structure. */
struct UIDataCloudProvider
{
    /** Constructs data. */
    UIDataCloudProvider()
        : m_fRestricted(false)
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataCloudProvider &other) const
    {
        return true
               && (m_uId == other.m_uId)
               && (m_strShortName == other.m_strShortName)
               && (m_strName == other.m_strName)
               && (m_fRestricted == other.m_fRestricted)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataCloudProvider &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataCloudProvider &other) const { return !equal(other); }

    /** Holds the provider ID. */
    QUuid    m_uId;
    /** Holds the provider short name. */
    QString  m_strShortName;
    /** Holds the provider name. */
    QString  m_strName;
    /** Holds whether provider is restricted. */
    bool     m_fRestricted;

    /** Holds the profile supported property descriptions. */
    QMap<QString, QString>  m_propertyDescriptions;
};

/** Cloud Profile data structure. */
struct UIDataCloudProfile
{
    /** Constructs data. */
    UIDataCloudProfile()
        : m_fRestricted(false)
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataCloudProfile &other) const
    {
        return true
               && (m_strProviderShortName == other.m_strProviderShortName)
               && (m_strName == other.m_strName)
               && (m_fRestricted == other.m_fRestricted)
               && (m_data == other.m_data)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataCloudProfile &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataCloudProfile &other) const { return !equal(other); }

    /** Holds the provider short name. */
    QString  m_strProviderShortName;
    /** Holds the profile name. */
    QString  m_strName;
    /** Holds whether profile is restricted. */
    bool     m_fRestricted;

    /** Holds the profile data. */
    QMap<QString, QPair<QString, QString> >  m_data;
};


/** Cloud Profile details widget. */
class UICloudProfileDetailsWidget : public QWidget
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

    /** Constructs cloud profile details widget passing @a pParent to the base-class.
      * @param  enmEmbedding  Brings embedding type. */
    UICloudProfileDetailsWidget(EmbedTo enmEmbedding, QWidget *pParent = 0);

    /** Returns the cloud profile data. */
    const UIDataCloudProfile &data() const { return m_newData; }
    /** Defines the cloud profile @a data. */
    void setData(const UIDataCloudProfile &data);

protected:

    /** Handles editor translation. */
    void retranslateEditor();
    /** Handles buttons translation. */
    void retranslateButtons();

private slots:

    /** @name Change handling stuff.
      * @{ */
        /** Handles name change. */
        void sltNameChanged(const QString &strName);
        /** Handles table change. */
        void sltTableChanged(QTableWidgetItem *pItem);

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
        /** Prepares widgets. */
        void prepareWidgets();
    /** @} */

    /** @name Loading stuff.
      * @{ */
        /** Loads data. */
        void loadData();
    /** @} */

    /** @name Change handling stuff.
      * @{ */
        /** Revalidates changes for passed @a pWidget. */
        void revalidate(QWidget *pWidget = 0);

        /** Retranslates validation for passed @a pWidget. */
        void retranslateValidation(QWidget *pWidget = 0);

        /** Updates table tooltips. */
        void updateTableToolTips();
        /** Adjusts table contents. */
        void adjustTableContents();

        /** Updates button states. */
        void updateButtonStates();
    /** @} */

    /** @name General variables.
      * @{ */
        /** Holds the parent widget embedding type. */
        const EmbedTo  m_enmEmbedding;

        /** Holds the old data copy. */
        UIDataCloudProfile  m_oldData;
        /** Holds the new data copy. */
        UIDataCloudProfile  m_newData;
    /** @} */

    /** @name Widget variables.
      * @{ */
        /** Holds the name label instance. */
        QLabel    *m_pLabelName;
        /** Holds the name editor instance. */
        QLineEdit *m_pEditorName;

        /** Holds the table-widget label instance. */
        QLabel        *m_pLabelTableWidget;
        /** Holds the table-widget instance. */
        QITableWidget *m_pTableWidget;

        /** Holds the button-box instance. */
        QIDialogButtonBox *m_pButtonBox;
    /** @} */
};


#endif /* !FEQT_INCLUDED_SRC_cloud_profilemanager_UICloudProfileDetailsWidget_h */
