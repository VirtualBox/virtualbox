/* $Id: UIMediumDetailsWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediumDetailsWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_medium_UIMediumDetailsWidget_h
#define FEQT_INCLUDED_SRC_medium_UIMediumDetailsWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "QIManagerDialog.h"
#include "UIMediumDefs.h"

/* COM includes: */
#include "KMediumType.h"
#include "KMediumVariant.h"

/* Forward declarations: */
class QAbstractButton;
class QComboBox;
class QLabel;
class QLineEdit;
class QStackedLayout;
class QTextEdit;
class QWidget;
class QILabel;
class QITabWidget;
class QIToolButton;
class UIEnumerationProgressBar;
class UIMediumManagerWidget;
class UIMediumSizeEditor;


/** Virtual Media Manager: Medium options data structure. */
struct UIDataMediumOptions
{
    /** Constructs data. */
    UIDataMediumOptions()
        : m_enmMediumType(KMediumType_Normal)
        , m_strLocation(QString())
        , m_strDescription(QString())
        , m_uLogicalSize(0)
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataMediumOptions &other) const
    {
        return true
               && (m_enmMediumType == other.m_enmMediumType)
               && (m_strLocation == other.m_strLocation)
               && (m_strDescription == other.m_strDescription)
               && (m_uLogicalSize == other.m_uLogicalSize)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataMediumOptions &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataMediumOptions &other) const { return !equal(other); }

    /** Holds the medium type. */
    KMediumType m_enmMediumType;
    /** Holds the location. */
    QString m_strLocation;
    /** Holds the description. */
    QString m_strDescription;
    /** Holds the logical size. */
    qulonglong m_uLogicalSize;
};


/** Virtual Media Manager: Medium details data structure. */
struct UIDataMediumDetails
{
    /** Constructs data. */
    UIDataMediumDetails()
        : m_aLabels(QStringList())
        , m_aFields(QStringList())
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataMediumDetails &other) const
    {
        return true
               && (m_aLabels == other.m_aLabels)
               && (m_aFields == other.m_aFields)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataMediumDetails &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataMediumDetails &other) const { return !equal(other); }

    /** Holds the labels list. */
    QStringList m_aLabels;
    /** Holds the fields list. */
    QStringList m_aFields;
};


/** Virtual Media Manager: Medium data structure. */
struct UIDataMedium
{
    /** Constructs data. */
    UIDataMedium()
        : m_fValid(false)
        , m_enmDeviceType(UIMediumDeviceType_Invalid)
        , m_enmVariant(KMediumVariant_Max)
        , m_fHasChildren(false)
        , m_options(UIDataMediumOptions())
        , m_details(UIDataMediumDetails())
    {}

    /** Constructs data with passed @enmType. */
    UIDataMedium(UIMediumDeviceType enmType)
        : m_fValid(false)
        , m_enmDeviceType(enmType)
        , m_enmVariant(KMediumVariant_Max)
        , m_fHasChildren(false)
        , m_options(UIDataMediumOptions())
        , m_details(UIDataMediumDetails())
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataMedium &other) const
    {
        return true
               && (m_fValid == other.m_fValid)
               && (m_enmDeviceType == other.m_enmDeviceType)
               && (m_enmVariant == other.m_enmVariant)
               && (m_fHasChildren == other.m_fHasChildren)
               && (m_options == other.m_options)
               && (m_details == other.m_details)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataMedium &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataMedium &other) const { return !equal(other); }

    /** Holds whether data is valid. */
    bool m_fValid;
    /** Holds the medium type. */
    UIMediumDeviceType m_enmDeviceType;
    /** Holds the medium variant. */
    KMediumVariant m_enmVariant;
    /** Holds whether medium has children. */
    bool m_fHasChildren;

    /** Holds the medium options. */
    UIDataMediumOptions m_options;
    /** Holds the details data. */
    UIDataMediumDetails m_details;
};


/** Virtual Media Manager: Virtual Media Manager details-widget. */
class UIMediumDetailsWidget : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies listeners about accept is allowed. */
    void sigAcceptAllowed(bool fAllowed);
    /** Notifies listeners about reject is allowed. */
    void sigRejectAllowed(bool fAllowed);

    /** Notifies listeners about data change rejected and should be reseted. */
    void sigDataChangeRejected();
    /** Notifies listeners about data change accepted and should be applied. */
    void sigDataChangeAccepted();

public:

    /** Constructs medium details dialog passing @a pParent to the base-class.
      * @param  enmEmbedding  Brings embedding type. */
    UIMediumDetailsWidget(UIMediumManagerWidget *pParent, EmbedTo enmEmbedding);

    /** Defines the raised details @a enmType. */
    void setCurrentType(UIMediumDeviceType enmType);

    /** Returns the medium data. */
    const UIDataMedium &data() const { return m_newData; }
    /** Defines the @a data for passed @a enmType. */
    void setData(const UIDataMedium &data);
    /** Enables/disables some of the medium editing widgets of the details tab. */
    void enableDisableMediumModificationWidgets(bool fMediumIsModifiable);

public slots:

    /** Defines whether the options tab is @a fEnabled. */
    void setOptionsEnabled(bool fEnabled);

private slots:

    /** @name Options stuff.
      * @{ */
        /** Handles type change. */
        void sltTypeIndexChanged(int iIndex);
        /** Handles location change. */
        void sltLocationPathChanged(const QString &strPath);
        /** Handles request to choose location. */
        void sltChooseLocationPath();
        /** Handles description text change. */
        void sltDescriptionTextChanged();
        /** Handles size editor change. */
        void sltSizeValueChanged(qulonglong uSize);

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
        /** Prepares tab-widget. */
        void prepareTabWidget();
        /** Prepares 'Options' tab. */
        void prepareTabOptions();
        /** Prepares 'Details' tab. */
        void prepareTabDetails();
        /** Prepares information-container. */
        void prepareInformationContainer(UIMediumDeviceType enmType, int cFields);
    /** @} */

    /** @name Loading stuff.
      * @{ */
        /** Load options data. */
        void loadDataForOptions();
        /** Load details data. */
        void loadDataForDetails();
    /** @} */

    /** @name Options stuff.
      * @{ */
        /** Revalidates changes for passed @a pWidget. */
        void revalidate(QWidget *pWidget = 0);
        /** Retranslates validation for passed @a pWidget. */
        void retranslateValidation(QWidget *pWidget = 0);
        /** Updates button states. */
        void updateButtonStates();

        /** Returns tool-tip for passed medium @a enmType. */
        static QString mediumTypeTip(KMediumType enmType);
    /** @} */

    /** @name Details stuff.
      * @{ */
        /** Returns information-container for passed medium @a enmType. */
        QWidget *infoContainer(UIMediumDeviceType enmType) const;
        /** Returns information-label for passed medium @a enmType and @a iIndex. */
        QLabel *infoLabel(UIMediumDeviceType enmType, int iIndex) const;
        /** Returns information-field for passed medium @a enmType and @a iIndex. */
        QILabel *infoField(UIMediumDeviceType enmType, int iIndex) const;
    /** @} */

    /** @name General variables.
      * @{ */
        /** Holds the parent reference. */
        UIMediumManagerWidget *m_pParent;
        /** Holds the parent widget embedding type. */
        const EmbedTo m_enmEmbedding;

        /** Holds the old data copy. */
        UIDataMedium  m_oldData;
        /** Holds the new data copy. */
        UIDataMedium  m_newData;

        /** Holds the tab-widget. */
        QITabWidget *m_pTabWidget;
    /** @} */

    /** @name Options variables.
      * @{ */
        /** Holds the type label. */
        QLabel    *m_pLabelType;
        /** Holds the type combo-box. */
        QComboBox *m_pComboBoxType;
        /** Holds the type error pane. */
        QLabel    *m_pErrorPaneType;

        /** Holds the location label. */
        QLabel       *m_pLabelLocation;
        /** Holds the location editor. */
        QLineEdit    *m_pEditorLocation;
        /** Holds the location error pane. */
        QLabel       *m_pErrorPaneLocation;
        /** Holds the location button. */
        QIToolButton *m_pButtonLocation;

        /** Holds the description label. */
        QLabel    *m_pLabelDescription;
        /** Holds the description editor. */
        QTextEdit *m_pEditorDescription;
        /** Holds the description error pane. */
        QLabel    *m_pErrorPaneDescription;

        /** Holds the size label. */
        QLabel             *m_pLabelSize;
        /** Holds the size editor. */
        UIMediumSizeEditor *m_pEditorSize;
        /** Holds the size error pane. */
        QLabel             *m_pErrorPaneSize;

        /** Holds the button-box instance. */
        QIDialogButtonBox        *m_pButtonBox;
        /** Holds the progress-bar widget instance. */
        UIEnumerationProgressBar *m_pProgressBar;

        /** Holds whether options are valid. */
        bool m_fValid;
    /** @} */

    /** @name Details variables.
      * @{ */
        /** Holds the details layout: */
        QStackedLayout *m_pLayoutDetails;

        /** Holds the map of information-container instances. */
        QMap<UIMediumDeviceType, QWidget*>          m_aContainers;
        /** Holds the map of information-container label instances. */
        QMap<UIMediumDeviceType, QList<QLabel*> >   m_aLabels;
        /** Holds the information-container field instances. */
        QMap<UIMediumDeviceType, QList<QILabel*> >  m_aFields;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_medium_UIMediumDetailsWidget_h */
