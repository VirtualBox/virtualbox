/* $Id: UIApplianceEditorWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIApplianceEditorWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIApplianceEditorWidget_h
#define FEQT_INCLUDED_SRC_widgets_UIApplianceEditorWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QAbstractItemModel>
#include <QItemDelegate>
#include <QSortFilterProxyModel>
#include <QWidget>

/* GUI includes: */
#include "UIExtraDataDefs.h"

/* COM includes: */
#include "CAppliance.h"
#include "CVirtualSystemDescription.h"

/* Forward declarations: */
class UIApplianceModelItem;
class QCheckBox;
class QLabel;
class QTextEdit;
class QITreeView;
class QVBoxLayout;


/** Abstract VSD parameter kinds. */
enum AbstractVSDParameterKind
{
    ParameterKind_Invalid,
    ParameterKind_Bool,
    ParameterKind_Double,
    ParameterKind_String,
    ParameterKind_Array
};

/** Abstract VSD parameter of Bool type, internal level. */
struct AbstractVSDParameterBool
{
    /** Public default constructor to fit Q_DECLARE_METATYPE rule. */
    AbstractVSDParameterBool()
        : value(false) {}
    /** Public copy constructor to fit Q_DECLARE_METATYPE rule. */
    AbstractVSDParameterBool(const AbstractVSDParameterBool &other)
        : value(other.value) {}
    /** Holds the value. */
    bool value;
};
Q_DECLARE_METATYPE(AbstractVSDParameterBool);

/** Abstract VSD parameter of Double type, internal level. */
struct AbstractVSDParameterDouble
{
    /** Public default constructor to fit Q_DECLARE_METATYPE rule. */
    AbstractVSDParameterDouble()
        : minimum(0), maximum(0), unit(QString()) {}
    /** Public copy constructor to fit Q_DECLARE_METATYPE rule. */
    AbstractVSDParameterDouble(const AbstractVSDParameterDouble &other)
        : minimum(other.minimum), maximum(other.maximum), unit(other.unit) {}
    /** Holds the minimum/base value. */
    double   minimum;
    /** Holds the maximum value. */
    double   maximum;
    /** Holds the unit. */
    QString  unit;
};
Q_DECLARE_METATYPE(AbstractVSDParameterDouble);

/** Abstract VSD parameter of String type, internal level. */
struct AbstractVSDParameterString
{
    /** Public default constructor to fit Q_DECLARE_METATYPE rule. */
    AbstractVSDParameterString()
        : value(QString()) {}
    /** Public copy constructor to fit Q_DECLARE_METATYPE rule. */
    AbstractVSDParameterString(const AbstractVSDParameterString &other)
        : value(other.value) {}
    /** Holds the value. */
    QString value;
};
Q_DECLARE_METATYPE(AbstractVSDParameterString);

/** Abstract VSD parameter of Array type, internal level. */
struct AbstractVSDParameterArray
{
    /** Public default constructor to fit Q_DECLARE_METATYPE rule. */
    AbstractVSDParameterArray()
        : values(QIStringPairList()) {}
    /** Public copy constructor to fit Q_DECLARE_METATYPE rule. */
    AbstractVSDParameterArray(const AbstractVSDParameterArray &other)
        : values(other.values) {}
    /** Holds the values array. */
    QIStringPairList values;
};
Q_DECLARE_METATYPE(AbstractVSDParameterArray);

/** Abstract VSD parameter interface, facade level. */
struct AbstractVSDParameter
{
    /** Holds the parameter name. */
    QString                        name;
    /** Holds the parameter type. */
    KVirtualSystemDescriptionType  type;
    /** Holds the parameter kind. */
    AbstractVSDParameterKind       kind;
    /** Holds the parameter abstract getter. */
    QVariant                       get;
};

/** Abstract VSD parameter list. */
typedef QList<AbstractVSDParameter> AbstractVSDParameterList;
Q_DECLARE_METATYPE(AbstractVSDParameterList);


/** Appliance tree-view section types. */
enum ApplianceViewSection
{
    ApplianceViewSection_Description = 0,
    ApplianceViewSection_OriginalValue,
    ApplianceViewSection_ConfigValue
};


/** Appliance model item types. */
enum ApplianceModelItemType
{
    ApplianceModelItemType_Root,
    ApplianceModelItemType_VirtualSystem,
    ApplianceModelItemType_VirtualHardware
};


/** QAbstractItemModel subclass used as Appliance model. */
class UIApplianceModel : public QAbstractItemModel
{
    Q_OBJECT;

public:

    /** Constructs the Appliance model passing @a pParent to the base-class.
      * @param  aVSDs  Brings the Virtual System descriptions. */
    UIApplianceModel(QVector<CVirtualSystemDescription>& aVSDs, QITreeView *pParent);
    /** Destructs the Appliance model. */
    ~UIApplianceModel();

    /** Returns the root index in the model. */
    virtual QModelIndex root() const;
    /** Returns the index of the item in the model specified by the given @a iRow, @a iColumn and @a parentIdx. */
    virtual QModelIndex index(int iRow, int iColumn, const QModelIndex &parentIdx = QModelIndex()) const RT_OVERRIDE;
    /** Returns the parent of the model item with the given @a idx. */
    virtual QModelIndex parent(const QModelIndex &idx) const RT_OVERRIDE;

    /** Returns the number of rows for the children of the given @a parentIdx. */
    virtual int rowCount(const QModelIndex &parentIdx = QModelIndex()) const RT_OVERRIDE;
    /** Returns the number of columns for the children of the given @a parentIdx. */
    virtual int columnCount(const QModelIndex &parentIdx = QModelIndex()) const RT_OVERRIDE;

    /** Returns the item flags for the given @a idx. */
    virtual Qt::ItemFlags flags(const QModelIndex &idx) const RT_OVERRIDE;
    /** Returns the data for the given @a iRole and @a iSection in the header with the specified @a enmOrientation. */
    virtual QVariant headerData(int iSection, Qt::Orientation enmOrientation, int iRole) const RT_OVERRIDE;

    /** Defines the @a iRole data for the item at @a idx to @a value. */
    virtual bool setData(const QModelIndex &idx, const QVariant &value, int iRole) RT_OVERRIDE;
    /** Returns the data stored under the given @a iRole for the item referred to by the @a idx. */
    virtual QVariant data(const QModelIndex &idx, int iRole = Qt::DisplayRole) const RT_OVERRIDE;

    /** Returns a model index for the buddy of the item represented by @a idx. */
    virtual QModelIndex buddy(const QModelIndex &idx) const RT_OVERRIDE;

    /** Restores the default values for the item with the given @a parentIdx. */
    void restoreDefaults(QModelIndex parentIdx = QModelIndex());

    /** Cache currently stored values. */
    void putBack();

    void setVirtualSystemBaseFolder(const QString& path);

    /** Defines the list of VSD @a hints. */
    void setVsdHints(const AbstractVSDParameterList &hints);
    /** Returns a name hint for certain VSD @a enmType. */
    QString nameHint(KVirtualSystemDescriptionType enmType) const;
    /** Returns a kind hint for certain VSD @a enmType. */
    AbstractVSDParameterKind kindHint(KVirtualSystemDescriptionType enmType) const;
    /** Returns a value hint for certain VSD @a enmType. */
    QVariant getHint(KVirtualSystemDescriptionType enmType) const;

private:

    /** Holds the list of VSD hints. */
    AbstractVSDParameterList  m_listVsdHints;

    /** Holds the root item reference. */
    UIApplianceModelItem *m_pRootItem;
};


/** QItemDelegate subclass used to create various Appliance model editors. */
class UIApplianceDelegate : public QItemDelegate
{
    Q_OBJECT;

public:

    /** Constructs the Appliance Delegate.
      * @param  pProxy  Brings the proxy model reference used to redirect requests to. */
    UIApplianceDelegate(QAbstractProxyModel *pProxy);

    /** Returns the widget used to edit the item specified by @a idx for editing.
      * @param  pParent      Brings the parent to be assigned for newly created editor.
      * @param  styleOption  Bring the style option set for the newly created editor. */
    virtual QWidget *createEditor(QWidget *pParent, const QStyleOptionViewItem &styleOption, const QModelIndex &idx) const RT_OVERRIDE;

    /** Defines the contents of the given @a pEditor to the data for the item at the given @a idx. */
    virtual void setEditorData(QWidget *pEditor, const QModelIndex &idx) const RT_OVERRIDE;
    /** Defines the data for the item at the given @a idx in the @a pModel to the contents of the given @a pEditor. */
    virtual void setModelData(QWidget *pEditor, QAbstractItemModel *pModel, const QModelIndex &idx) const RT_OVERRIDE;

    /** Updates the geometry of the @a pEditor for the item with the given @a idx, according to the rectangle specified in the @a styleOption. */
    virtual void updateEditorGeometry(QWidget *pEditor, const QStyleOptionViewItem &styleOption, const QModelIndex &idx) const RT_OVERRIDE;

    /** Returns the size hint for the item at the given @a idx and specified @a styleOption. */
    virtual QSize sizeHint(const QStyleOptionViewItem &styleOption, const QModelIndex &idx) const RT_OVERRIDE;

protected:

#ifdef VBOX_WS_MAC
    /** Filters @a pEvent if this object has been installed as an event filter for the watched @a pObject. */
    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;
#endif

private:

    /** Holds the proxy model reference used to redirect requests to. */
    QAbstractProxyModel *m_pProxy;
};


/** QSortFilterProxyModel subclass used as the Appliance Sorting Proxy model. */
class UIApplianceSortProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT;

public:

    /** Constructs the Appliance Sorting Proxy model passing @a pParent to the base-class. */
    UIApplianceSortProxyModel(QObject *pParent = 0);

protected:

    /** Returns whether item in the row indicated by the given @a iSourceRow and @a srcParenIdx should be included in the model. */
    virtual bool filterAcceptsRow(int iSourceRow, const QModelIndex &srcParenIdx) const RT_OVERRIDE;

    /** Returns whether value of the item referred to by the given index @a leftIdx is less
      * than the value of the item referred to by the given index @a rightIdx. */
    virtual bool lessThan(const QModelIndex &leftIdx, const QModelIndex &rightIdx) const RT_OVERRIDE;

    /** Holds the array of sorted Virtual System Description types. */
    static KVirtualSystemDescriptionType s_aSortList[];

    /** Holds the filtered list of Virtual System Description types. */
    QList<KVirtualSystemDescriptionType> m_aFilteredList;
};


/** QWidget subclass used as the Appliance Editor widget. */
class UIApplianceEditorWidget : public QWidget
{
    Q_OBJECT;

public:

    /** Constructs the Appliance Editor widget passing @a pParent to the base-class. */
    UIApplianceEditorWidget(QWidget *pParent = 0);

    /** Clears everything. */
    void clear();

    /** Defines @a comAppliance wrapper instance. */
    virtual void setAppliance(const CAppliance &comAppliance);

    /** Defines the list of VSD @a hints. */
    void setVsdHints(const AbstractVSDParameterList &hints);

    /** Defines virtual system base folder @a strPath. */
    void setVirtualSystemBaseFolder(const QString &strPath);

    /** Returns the minimum guest RAM. */
    static int minGuestRAM() { return m_minGuestRAM; }
    /** Returns the maximum guest RAM. */
    static int maxGuestRAM() { return m_maxGuestRAM; }
    /** Returns the minimum guest CPU count. */
    static int minGuestCPUCount() { return m_minGuestCPUCount; }
    /** Returns the maximum guest CPU count. */
    static int maxGuestCPUCount() { return m_maxGuestCPUCount; }

public slots:

    /** Restores the default values. */
    void restoreDefaults();

protected:

    /** Holds the currently set appliance reference. */
    CAppliance  m_comAppliance;

    /** Holds the list of VSD hints. */
    AbstractVSDParameterList  m_listVsdHints;

    /** Holds the Appliance model reference. */
    UIApplianceModel *m_pModel;

    QVBoxLayout *m_pLayout;

    /** Holds the information pane instance. */
    QWidget   *m_pPaneInformation;
    /** Holds the settings tree-view instance. */
    QITreeView *m_pTreeViewSettings;

    /** Holds the warning pane instance. */
    QWidget   *m_pPaneWarning;
    /** Holds the warning label instance. */
    QLabel    *m_pLabelWarning;
    /** Holds the warning browser instance. */
    QTextEdit *m_pTextEditWarning;

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Performs Appliance settings initialization. */
    static void initSystemSettings();

    /** Holds the minimum guest RAM. */
    static int m_minGuestRAM;
    /** Holds the maximum guest RAM. */
    static int m_maxGuestRAM;
    /** Holds the minimum guest CPU count. */
    static int m_minGuestCPUCount;
    /** Holds the maximum guest CPU count. */
    static int m_maxGuestCPUCount;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIApplianceEditorWidget_h */
