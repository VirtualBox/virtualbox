/* $Id: UINetworkAttachmentEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkAttachmentEditor class declaration.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UINetworkAttachmentEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UINetworkAttachmentEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"
#include "UIExtraDataDefs.h"

/* COM includes: */
#include "KNetworkAttachmentType.h"

/* Forward declarations: */
class QComboBox;
class QGridLayout;
class QLabel;

/** UIEditor sub-class used as a network attachment editor. */
class SHARED_LIBRARY_STUFF UINetworkAttachmentEditor : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notifies listeners about value type has changed. */
    void sigValueTypeChanged();
    /** Notifies listeners about value name has changed. */
    void sigValueNameChanged();

    /** Notifies listeners about value has became @a fValid. */
    void sigValidChanged(bool fValid);

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UINetworkAttachmentEditor(QWidget *pParent = 0);

    /** Defines value @a enmType. */
    void setValueType(KNetworkAttachmentType enmType);
    /** Returns value type. */
    KNetworkAttachmentType valueType() const;

    /** Defines value @a names for specified @a enmType. */
    void setValueNames(KNetworkAttachmentType enmType, const QStringList &names);
    /** Defines value @a strName for specified @a enmType. */
    void setValueName(KNetworkAttachmentType enmType, const QString &strName);
    /** Returns current name for specified @a enmType. */
    QString valueName(KNetworkAttachmentType enmType) const;

    /** Returns minimum layout hint. */
    int minimumLabelHorizontalHint() const;
    /** Defines minimum layout @a iIndent. */
    void setMinimumLayoutIndent(int iIndent);

    /** Returns bridged adapter list. */
    static QStringList bridgedAdapters();
    /** Returns internal network list. */
    static QStringList internalNetworks();
    /** Returns host-only interface list. */
    static QStringList hostInterfaces();
    /** Returns generic driver list. */
    static QStringList genericDrivers();
    /** Returns NAT network list. */
    static QStringList natNetworks();
#ifdef VBOX_WITH_CLOUD_NET
    /** Returns cloud network list. */
    static QStringList cloudNetworks();
#endif
#ifdef VBOX_WITH_VMNET
    /** Returns host-only network list. */
    static QStringList hostOnlyNetworks();
#endif

protected:

    /** Handles filter change. */
    virtual void handleFilterChange() RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;
    /** Handles current type change. */
    void sltHandleCurrentTypeChanged();
    /** Handles current name change. */
    void sltHandleCurrentNameChanged();

private:

    /** Prepares all. */
    void prepare();
    /** Populates type combo. */
    void populateTypeCombo();
    /** Populates name combo. */
    void populateNameCombo();

    /** Retranslates name description. */
    void retranslateNameDescription();

    /** Validates editor values. */
    void revalidate();

    /** Returns UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork corresponding to passed KNetworkAttachmentType. */
    static UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork toUiNetworkAdapterEnum(KNetworkAttachmentType comEnum);

    /** Holds the empty item data id. */
    static QString  s_strEmptyItemId;

    /** Holds the attachment type restrictions. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork  m_enmRestrictedNetworkAttachmentTypes;

    /** Holds the map of possible names. */
    QMap<KNetworkAttachmentType, QStringList>  m_names;
    /** Holds the map of current names. */
    QMap<KNetworkAttachmentType, QString>      m_name;

    /** Holds the requested type. */
    KNetworkAttachmentType  m_enmType;

    /** Holds the main layout instance. */
    QGridLayout *m_pLayout;
    /** Holds the type label instance. */
    QLabel      *m_pLabelType;
    /** Holds the type combo instance. */
    QComboBox   *m_pComboType;
    /** Holds the name label instance. */
    QLabel      *m_pLabelName;
    /** Holds the name combo instance. */
    QComboBox   *m_pComboName;
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UINetworkAttachmentEditor_h */
