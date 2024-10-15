/* $Id: UICloudConsoleDetailsWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICloudConsoleDetailsWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_cloud_consolemanager_UICloudConsoleDetailsWidget_h
#define FEQT_INCLUDED_SRC_cloud_consolemanager_UICloudConsoleDetailsWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QWidget>

/* GUI includes: */
#include "QIManagerDialog.h"

/* Forward declarations: */
class QAbstractButton;
class QLabel;
class QLineEdit;
class QStackedLayout;
class QIDialogButtonBox;


/** Cloud Console Application data structure. */
struct UIDataCloudConsoleApplication
{
    /** Constructs data. */
    UIDataCloudConsoleApplication()
        : m_fRestricted(false)
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataCloudConsoleApplication &other) const
    {
        return true
               && (m_strId == other.m_strId)
               && (m_strName == other.m_strName)
               && (m_strPath == other.m_strPath)
               && (m_strArgument == other.m_strArgument)
               && (m_fRestricted == other.m_fRestricted)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataCloudConsoleApplication &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataCloudConsoleApplication &other) const { return !equal(other); }

    /** Holds the console application ID. */
    QString  m_strId;
    /** Holds the console application name. */
    QString  m_strName;
    /** Holds the console application path. */
    QString  m_strPath;
    /** Holds the console application argument. */
    QString  m_strArgument;
    /** Holds whether console application is restricted. */
    bool     m_fRestricted;
};

/** Cloud Console Profile data structure. */
struct UIDataCloudConsoleProfile
{
    /** Constructs data. */
    UIDataCloudConsoleProfile()
        : m_fRestricted(false)
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataCloudConsoleProfile &other) const
    {
        return true
               && (m_strApplicationId == other.m_strApplicationId)
               && (m_strId == other.m_strId)
               && (m_strName == other.m_strName)
               && (m_strArgument == other.m_strArgument)
               && (m_fRestricted == other.m_fRestricted)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataCloudConsoleProfile &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataCloudConsoleProfile &other) const { return !equal(other); }

    /** Holds the console profile application ID. */
    QString  m_strApplicationId;
    /** Holds the console profile ID. */
    QString  m_strId;
    /** Holds the console profile name. */
    QString  m_strName;
    /** Holds the console profile argument. */
    QString  m_strArgument;
    /** Holds whether console profile is restricted. */
    bool     m_fRestricted;
};


/** Cloud Console details widget. */
class UICloudConsoleDetailsWidget : public QWidget
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

    /** Constructs cloud console details widget passing @a pParent to the base-class.
      * @param  enmEmbedding  Brings embedding type. */
    UICloudConsoleDetailsWidget(EmbedTo enmEmbedding, QWidget *pParent = 0);

    /** Returns the cloud console application data. */
    const UIDataCloudConsoleApplication &applicationData() const { return m_newApplicationData; }
    /** Returns the cloud console profile data. */
    const UIDataCloudConsoleProfile &profileData() const { return m_newProfileData; }
    /** Defines the cloud console application @a data. */
    void setApplicationData(const UIDataCloudConsoleApplication &data);
    /** Defines the cloud console profile @a data. */
    void setProfileData(const UIDataCloudConsoleProfile &data);
    /** Clears all the console data. */
    void clearData();

private slots:

    /** @name Change handling stuff.
      * @{ */
        /** Handles console application name change. */
        void sltApplicationNameChanged(const QString &strName);
        /** Handles console application path change. */
        void sltApplicationPathChanged(const QString &strPath);
        /** Handles console application argument change. */
        void sltApplicationArgumentChanged(const QString &strArgument);
        /** Handles console profile name change. */
        void sltProfileNameChanged(const QString &strName);
        /** Handles console profile argument change. */
        void sltProfileArgumentChanged(const QString &strArgument);

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

        /** Updates button states. */
        void updateButtonStates();
    /** @} */

    /** @name General variables.
      * @{ */
        /** Holds the parent widget embedding type. */
        const EmbedTo  m_enmEmbedding;

        /** Holds the old console application data copy. */
        UIDataCloudConsoleApplication  m_oldApplicationData;
        /** Holds the new console application data copy. */
        UIDataCloudConsoleApplication  m_newApplicationData;

        /** Holds the old console profile data copy. */
        UIDataCloudConsoleProfile  m_oldProfileData;
        /** Holds the new console profile data copy. */
        UIDataCloudConsoleProfile  m_newProfileData;
    /** @} */

    /** @name Widget variables.
      * @{ */
        /** Holds the stacked layout isntance. */
        QStackedLayout *m_pStackedLayout;

        /** Holds the application name label instance. */
        QLabel    *m_pLabelApplicationName;
        /** Holds the application name editor instance. */
        QLineEdit *m_pEditorApplicationName;
        /** Holds the application path label instance. */
        QLabel    *m_pLabelApplicationPath;
        /** Holds the application path editor instance. */
        QLineEdit *m_pEditorApplicationPath;
        /** Holds the application argument label instance. */
        QLabel    *m_pLabelApplicationArgument;
        /** Holds the application argument editor instance. */
        QLineEdit *m_pEditorApplicationArgument;

        /** Holds the profile name label instance. */
        QLabel    *m_pLabelProfileName;
        /** Holds the profile name editor instance. */
        QLineEdit *m_pEditorProfileName;
        /** Holds the profile argument label instance. */
        QLabel    *m_pLabelProfileArgument;
        /** Holds the profile argument editor instance. */
        QLineEdit *m_pEditorProfileArgument;

        /** Holds the button-box instance. */
        QIDialogButtonBox *m_pButtonBox;
    /** @} */
};


#endif /* !FEQT_INCLUDED_SRC_cloud_consolemanager_UICloudConsoleDetailsWidget_h */
