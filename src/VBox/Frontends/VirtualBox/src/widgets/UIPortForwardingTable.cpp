/* $Id: UIPortForwardingTable.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIPortForwardingTable class implementation.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemEditorFactory>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStyledItemDelegate>

/* GUI includes: */
#include "QITableView.h"
#include "UIConverter.h"
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UIPortForwardingTable.h"
#include "QIToolBar.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include <iprt/cidr.h>

/* External includes: */
#include <math.h>


/** Port Forwarding data types. */
enum UIPortForwardingDataType
{
    UIPortForwardingDataType_Name,
    UIPortForwardingDataType_Protocol,
    UIPortForwardingDataType_HostIp,
    UIPortForwardingDataType_HostPort,
    UIPortForwardingDataType_GuestIp,
    UIPortForwardingDataType_GuestPort,
    UIPortForwardingDataType_Max
};


/** QLineEdit extension used as name editor. */
class NameEditor : public QLineEdit
{
    Q_OBJECT;
    Q_PROPERTY(NameData name READ name WRITE setName USER true);

public:

    /** Constructs name editor passing @a pParent to the base-class. */
    NameEditor(QWidget *pParent = 0) : QLineEdit(pParent)
    {
        setFrame(false);
        setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setValidator(new QRegularExpressionValidator(QRegularExpression("[^,:]*"), this));
    }

private:

    /** Defines the @a strName. */
    void setName(NameData strName)
    {
        setText(strName);
    }

    /** Returns the name. */
    NameData name() const
    {
        return text();
    }
};


/** QComboBox extension used as protocol editor. */
class ProtocolEditor : public QComboBox
{
    Q_OBJECT;
    Q_PROPERTY(KNATProtocol protocol READ protocol WRITE setProtocol USER true);

public:

    /** Constructs protocol editor passing @a pParent to the base-class. */
    ProtocolEditor(QWidget *pParent = 0) : QComboBox(pParent)
    {
        addItem(gpConverter->toString(KNATProtocol_UDP), QVariant::fromValue(KNATProtocol_UDP));
        addItem(gpConverter->toString(KNATProtocol_TCP), QVariant::fromValue(KNATProtocol_TCP));
    }

private:

    /** Defines the @a enmProtocol. */
    void setProtocol(KNATProtocol enmProtocol)
    {
        for (int i = 0; i < count(); ++i)
        {
            if (itemData(i).value<KNATProtocol>() == enmProtocol)
            {
                setCurrentIndex(i);
                break;
            }
        }
    }

    /** Returns the protocol. */
    KNATProtocol protocol() const
    {
        return itemData(currentIndex()).value<KNATProtocol>();
    }
};


/** QLineEdit extension used as IPv4 editor. */
class IPv4Editor : public QLineEdit
{
    Q_OBJECT;
    Q_PROPERTY(IpData ip READ ip WRITE setIp USER true);

public:

    /** Constructs IPv4-editor passing @a pParent to the base-class. */
    IPv4Editor(QWidget *pParent = 0) : QLineEdit(pParent)
    {
        setFrame(false);
        setAlignment(Qt::AlignCenter);
        // Decided to not use it for now:
        // setValidator(new IPv4Validator(this));
    }

private:

    /** Defines the @a strIp. */
    void setIp(IpData strIp)
    {
        setText(strIp);
    }

    /** Returns the ip. */
    IpData ip() const
    {
        return text() == "..." ? QString() : text();
    }
};


/** QLineEdit extension used as IPv6 editor. */
class IPv6Editor : public QLineEdit
{
    Q_OBJECT;
    Q_PROPERTY(IpData ip READ ip WRITE setIp USER true);

public:

    /** Constructs IPv6-editor passing @a pParent to the base-class. */
    IPv6Editor(QWidget *pParent = 0) : QLineEdit(pParent)
    {
        setFrame(false);
        setAlignment(Qt::AlignCenter);
        // Decided to not use it for now:
        // setValidator(new IPv6Validator(this));
    }

private:

    /** Defines the @a strIp. */
    void setIp(IpData strIp)
    {
        setText(strIp);
    }

    /** Returns the ip. */
    IpData ip() const
    {
        return text() == "..." ? QString() : text();
    }
};


/** QSpinBox extension used as Port editor. */
class PortEditor : public QSpinBox
{
    Q_OBJECT;
    Q_PROPERTY(PortData port READ port WRITE setPort USER true);

public:

    /** Constructs Port-editor passing @a pParent to the base-class. */
    PortEditor(QWidget *pParent = 0) : QSpinBox(pParent)
    {
        setFrame(false);
        setRange(0, (1 << (8 * sizeof(ushort))) - 1);
    }

private:

    /** Defines the @a port. */
    void setPort(PortData port)
    {
        setValue(port.value());
    }

    /** Returns the port. */
    PortData port() const
    {
        return value();
    }
};


/** QITableViewCell extension used as Port Forwarding table-view cell. */
class UIPortForwardingCell : public QITableViewCell
{
    Q_OBJECT;

public:

    /** Constructs table cell passing @a pParent to the base-class.
      * @param  strName  Brings the name. */
    UIPortForwardingCell(QITableViewRow *pParent, const NameData &strName)
        : QITableViewCell(pParent)
        , m_strText(strName)
    {}

    /** Constructs table cell passing @a pParent to the base-class.
      * @param  enmProtocol  Brings the protocol type. */
    UIPortForwardingCell(QITableViewRow *pParent, KNATProtocol enmProtocol)
        : QITableViewCell(pParent)
        , m_strText(gpConverter->toString(enmProtocol))
    {}

    /** Constructs table cell passing @a pParent to the base-class.
      * @param  strIp  Brings the IP address. */
    UIPortForwardingCell(QITableViewRow *pParent, const IpData &strIp)
        : QITableViewCell(pParent)
        , m_strText(strIp)
    {}

    /** Constructs table cell passing @a pParent to the base-class.
      * @param  port  Brings the port. */
    UIPortForwardingCell(QITableViewRow *pParent, PortData port)
        : QITableViewCell(pParent)
        , m_strText(QString::number(port.value()))
    {}

    /** Returns the cell text. */
    virtual QString text() const RT_OVERRIDE { return m_strText; }

private:

    /** Holds the cell text. */
    QString  m_strText;
};


/** QITableViewRow extension used as Port Forwarding table-view row. */
class UIPortForwardingRow : public QITableViewRow
{
    Q_OBJECT;

public:

    /** Constructs table row passing @a pParent to the base-class.
      * @param  strName      Brings the unique rule name.
      * @param  enmProtocol  Brings the rule protocol type.
      * @param  strHostIp    Brings the rule host IP address.
      * @param  hostPort     Brings the rule host port.
      * @param  strGuestIp   Brings the rule guest IP address.
      * @param  guestPort    Brings the rule guest port. */
    UIPortForwardingRow(QITableView *pParent,
                        const NameData &strName, KNATProtocol enmProtocol,
                        const IpData &strHostIp, PortData hostPort,
                        const IpData &strGuestIp, PortData guestPort)
        : QITableViewRow(pParent)
        , m_strName(strName), m_enmProtocol(enmProtocol)
        , m_strHostIp(strHostIp), m_hostPort(hostPort)
        , m_strGuestIp(strGuestIp), m_guestPort(guestPort)
    {
        /* Create cells: */
        createCells();
    }

    /** Destructs table row. */
    ~UIPortForwardingRow()
    {
        /* Destroy cells: */
        destroyCells();
    }

    /** Returns the unique rule name. */
    NameData name() const { return m_strName; }
    /** Defines the unique rule name. */
    void setName(const NameData &strName)
    {
        m_strName = strName;
        delete m_cells[UIPortForwardingDataType_Name];
        m_cells[UIPortForwardingDataType_Name] = new UIPortForwardingCell(this, m_strName);
    }

    /** Returns the rule protocol type. */
    KNATProtocol protocol() const { return m_enmProtocol; }
    /** Defines the rule protocol type. */
    void setProtocol(KNATProtocol enmProtocol)
    {
        m_enmProtocol = enmProtocol;
        delete m_cells[UIPortForwardingDataType_Protocol];
        m_cells[UIPortForwardingDataType_Protocol] = new UIPortForwardingCell(this, m_enmProtocol);
    }

    /** Returns the rule host IP address. */
    IpData hostIp() const { return m_strHostIp; }
    /** Defines the rule host IP address. */
    void setHostIp(const IpData &strHostIp)
    {
        m_strHostIp = strHostIp;
        delete m_cells[UIPortForwardingDataType_HostIp];
        m_cells[UIPortForwardingDataType_HostIp] = new UIPortForwardingCell(this, m_strHostIp);
    }

    /** Returns the rule host port. */
    PortData hostPort() const { return m_hostPort; }
    /** Defines the rule host port. */
    void setHostPort(PortData hostPort)
    {
        m_hostPort = hostPort;
        delete m_cells[UIPortForwardingDataType_HostPort];
        m_cells[UIPortForwardingDataType_HostPort] = new UIPortForwardingCell(this, m_hostPort);
    }

    /** Returns the rule guest IP address. */
    IpData guestIp() const { return m_strGuestIp; }
    /** Defines the rule guest IP address. */
    void setGuestIp(const IpData &strGuestIp)
    {
        m_strGuestIp = strGuestIp;
        delete m_cells[UIPortForwardingDataType_GuestIp];
        m_cells[UIPortForwardingDataType_GuestIp] = new UIPortForwardingCell(this, m_strGuestIp);
    }

    /** Returns the rule guest port. */
    PortData guestPort() const { return m_guestPort; }
    /** Defines the rule guest port. */
    void setGuestPort(PortData guestPort)
    {
        m_guestPort = guestPort;
        delete m_cells[UIPortForwardingDataType_GuestPort];
        m_cells[UIPortForwardingDataType_GuestPort] = new UIPortForwardingCell(this, m_guestPort);
    }

protected:

    /** Returns the number of children. */
    virtual int childCount() const RT_OVERRIDE
    {
        /* Return cell count: */
        return UIPortForwardingDataType_Max;
    }

    /** Returns the child item with @a iIndex. */
    virtual QITableViewCell *childItem(int iIndex) const RT_OVERRIDE
    {
        /* Make sure index within the bounds: */
        AssertReturn(iIndex >= 0 && iIndex < m_cells.size(), 0);
        /* Return corresponding cell: */
        return m_cells[iIndex];
    }

private:

    /** Creates cells. */
    void createCells()
    {
        /* Create cells on the basis of variables we have: */
        m_cells.resize(UIPortForwardingDataType_Max);
        m_cells[UIPortForwardingDataType_Name] = new UIPortForwardingCell(this, m_strName);
        m_cells[UIPortForwardingDataType_Protocol] = new UIPortForwardingCell(this, m_enmProtocol);
        m_cells[UIPortForwardingDataType_HostIp] = new UIPortForwardingCell(this, m_strHostIp);
        m_cells[UIPortForwardingDataType_HostPort] = new UIPortForwardingCell(this, m_hostPort);
        m_cells[UIPortForwardingDataType_GuestIp] = new UIPortForwardingCell(this, m_strGuestIp);
        m_cells[UIPortForwardingDataType_GuestPort] = new UIPortForwardingCell(this, m_guestPort);
    }

    /** Destroys cells. */
    void destroyCells()
    {
        /* Destroy cells: */
        qDeleteAll(m_cells);
        m_cells.clear();
    }

    /** Holds the unique rule name. */
    NameData m_strName;
    /** Holds the rule protocol type. */
    KNATProtocol m_enmProtocol;
    /** Holds the rule host IP address. */
    IpData m_strHostIp;
    /** Holds the rule host port. */
    PortData m_hostPort;
    /** Holds the rule guest IP address. */
    IpData m_strGuestIp;
    /** Holds the rule guest port. */
    PortData m_guestPort;

    /** Holds the cell instances. */
    QVector<UIPortForwardingCell*> m_cells;
};


/** QAbstractTableModel subclass used as port forwarding data model. */
class UIPortForwardingModel : public QAbstractTableModel
{
    Q_OBJECT;

public:

    /** Constructs Port Forwarding model passing @a pParent to the base-class.
      * @param  rules  Brings the list of port forwarding rules to load initially. */
    UIPortForwardingModel(UIPortForwardingTable *pParent, const UIPortForwardingDataList &rules = UIPortForwardingDataList());
    /** Destructs Port Forwarding model. */
    virtual ~UIPortForwardingModel() RT_OVERRIDE RT_FINAL;

    /** Returns the list of port forwarding rules. */
    UIPortForwardingDataList rules() const;
    /** Defines the list of port forwarding @a newRules. */
    void setRules(const UIPortForwardingDataList &newRules);
    /** Adds empty port forwarding rule for certain @a index. */
    void addRule(const QModelIndex &index);
    /** Removes port forwarding rule with certain @a index. */
    void removeRule(const QModelIndex &index);

    /** Defines guest address @a strHint. */
    void setGuestAddressHint(const QString &strHint);

    /** Returns the index of the item in the model specified by the given @a iRow, @a iColumn and @a parentIdx index. */
    virtual QModelIndex index(int iRow, int iColumn, const QModelIndex &parentIdx = QModelIndex()) const RT_OVERRIDE RT_FINAL;

    /** Returns flags for item with certain @a index. */
    virtual Qt::ItemFlags flags(const QModelIndex &index) const RT_OVERRIDE RT_FINAL;

    /** Returns row count of certain @a parent. */
    virtual int rowCount(const QModelIndex &parent = QModelIndex()) const RT_OVERRIDE RT_FINAL;
    /** Returns column count of certain @a parent. */
    virtual int columnCount(const QModelIndex &parent = QModelIndex()) const RT_OVERRIDE RT_FINAL;

    /** Returns header data for @a iSection, @a enmOrientation and @a iRole specified. */
    virtual QVariant headerData(int iSection, Qt::Orientation enmOrientation, int iRole) const RT_OVERRIDE RT_FINAL;

    /** Defines the @a iRole data for item with @a index as @a value. */
    virtual bool setData(const QModelIndex &index, const QVariant &value, int iRole) RT_OVERRIDE RT_FINAL;
    /** Returns the @a iRole data for item with @a index. */
    virtual QVariant data(const QModelIndex &index, int iRole) const RT_OVERRIDE RT_FINAL;

private:

    /** Return the parent table-view reference. */
    QITableView *view() const;

    /** Holds the root port-forwarding table reference. */
    UIPortForwardingTable *m_pPortForwardingTable;

    /** Holds the port forwarding row list.  */
    QList<UIPortForwardingRow*> m_dataList;

    /** Holds the guest address hint. */
    QString  m_strGuestAddressHint;
};


/** QITableView extension used as Port Forwarding table-view. */
class UIPortForwardingView : public QITableView
{
    Q_OBJECT;

public:

    /** Constructs Port Forwarding table-view passing @a pParent to the base-class.
      * @param  fIPv6  Brings whether this table contains IPv6 rules, not IPv4. */
    UIPortForwardingView(bool fIPv6, QWidget *pParent = 0);
    /** Destructs Port Forwarding table-view. */
    virtual ~UIPortForwardingView();

protected:

    /** Handles any Qt @a pEvent. */
    virtual bool event(QEvent *pEvent) RT_OVERRIDE;

protected slots:

    /** Handles rows being inserted.
      * @param  parent  Brings the parent under which new rows being inserted.
      * @param  iStart  Brings the starting position (inclusive).
      * @param  iStart  Brings the end position (inclusive). */
    virtual void rowsInserted(const QModelIndex &parent, int iStart, int iEnd) RT_OVERRIDE;

    /** Handles rows being removed.
      * @param  parent  Brings the parent for which rows being removed.
      * @param  iStart  Brings the starting position (inclusive).
      * @param  iStart  Brings the end position (inclusive). */
    virtual void rowsAboutToBeRemoved(const QModelIndex &parent, int iStart, int iEnd) RT_OVERRIDE;

private:

    /** Prepares everything. */
    void prepare();
    /** Cleanups everything. */
    void cleanup();

    /** Adjusts table contents. */
    void adjust();

    /** Holds whether this view contains IPv6 rules, not IPv4. */
    bool  m_fIPv6;

    /** Holds the item editor factory instance. */
    QItemEditorFactory *m_pItemEditorFactory;
};


/*********************************************************************************************************************************
*   Class UIPortForwardingModel implementation.                                                                                  *
*********************************************************************************************************************************/

UIPortForwardingModel::UIPortForwardingModel(UIPortForwardingTable *pParent,
                                             const UIPortForwardingDataList &rules /* = UIPortForwardingDataList() */)
    : QAbstractTableModel(pParent)
    , m_pPortForwardingTable(pParent)
{
    /* Fetch the incoming data: */
    foreach (const UIDataPortForwardingRule &rule, rules)
        m_dataList << new UIPortForwardingRow(view(),
                                              rule.name, rule.protocol,
                                              rule.hostIp, rule.hostPort,
                                              rule.guestIp, rule.guestPort);
}

UIPortForwardingModel::~UIPortForwardingModel()
{
    /* Delete the cached data: */
    qDeleteAll(m_dataList);
    m_dataList.clear();
}

UIPortForwardingDataList UIPortForwardingModel::rules() const
{
    /* Return the cached data: */
    UIPortForwardingDataList data;
    foreach (const UIPortForwardingRow *pRow, m_dataList)
        data << UIDataPortForwardingRule(pRow->name(), pRow->protocol(),
                                         pRow->hostIp(), pRow->hostPort(),
                                         pRow->guestIp(), pRow->guestPort());
    return data;
}

void UIPortForwardingModel::setRules(const UIPortForwardingDataList &newRules)
{
    /* Clear old data first of all: */
    if (!m_dataList.isEmpty())
    {
        beginRemoveRows(QModelIndex(), 0, m_dataList.size() - 1);
        foreach (const UIPortForwardingRow *pRow, m_dataList)
            delete pRow;
        m_dataList.clear();
        endRemoveRows();
    }

    /* Fetch incoming data: */
    if (!newRules.isEmpty())
    {
        beginInsertRows(QModelIndex(), 0, newRules.size() - 1);
        foreach (const UIDataPortForwardingRule &rule, newRules)
            m_dataList << new UIPortForwardingRow(view(),
                                                  rule.name, rule.protocol,
                                                  rule.hostIp, rule.hostPort,
                                                  rule.guestIp, rule.guestPort);
        endInsertRows();
    }
}

void UIPortForwardingModel::addRule(const QModelIndex &index)
{
    beginInsertRows(QModelIndex(), m_dataList.size(), m_dataList.size());
    /* Search for existing "Rule [NUMBER]" record: */
    uint uMaxIndex = 0;
    QString strTemplate("Rule %1");
    const QRegularExpression re(strTemplate.arg("(\\d+)"));
    for (int i = 0; i < m_dataList.size(); ++i)
    {
        const QRegularExpressionMatch mt = re.match(m_dataList[i]->name());
        if (mt.hasMatch())
        {
            const uint uFoundIndex = mt.captured(1).toUInt();
            uMaxIndex = uFoundIndex > uMaxIndex ? uFoundIndex : uMaxIndex;
        }
    }
    /* If index is valid => copy data: */
    if (index.isValid())
        m_dataList << new UIPortForwardingRow(view(),
                                              strTemplate.arg(++uMaxIndex), m_dataList[index.row()]->protocol(),
                                              m_dataList[index.row()]->hostIp(), m_dataList[index.row()]->hostPort(),
                                              m_dataList[index.row()]->guestIp(), m_dataList[index.row()]->guestPort());
    /* If index is NOT valid => use default values: */
    else
        m_dataList << new UIPortForwardingRow(view(),
                                              strTemplate.arg(++uMaxIndex), KNATProtocol_TCP,
                                              QString(""), 0, m_strGuestAddressHint, 0);
    endInsertRows();
}

void UIPortForwardingModel::removeRule(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    beginRemoveRows(QModelIndex(), index.row(), index.row());
    delete m_dataList.at(index.row());
    m_dataList.removeAt(index.row());
    endRemoveRows();
}

void UIPortForwardingModel::setGuestAddressHint(const QString &strHint)
{
    m_strGuestAddressHint = strHint;
}

QModelIndex UIPortForwardingModel::index(int iRow, int iColumn, const QModelIndex &parentIdx /* = QModelIndex() */) const
{
    /* No index for unknown items: */
    if (!hasIndex(iRow, iColumn, parentIdx))
        return QModelIndex();

    /* Provide index users with packed item pointer: */
    UIPortForwardingRow *pItem = iRow >= 0 && iRow < m_dataList.size() ? m_dataList.at(iRow) : 0;
    return pItem ? createIndex(iRow, iColumn, pItem) : QModelIndex();
}

Qt::ItemFlags UIPortForwardingModel::flags(const QModelIndex &index) const
{
    /* Check index validness: */
    if (!index.isValid())
        return Qt::NoItemFlags;
    /* All columns have similar flags: */
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

int UIPortForwardingModel::rowCount(const QModelIndex &) const
{
    return m_dataList.size();
}

int UIPortForwardingModel::columnCount(const QModelIndex &) const
{
    return UIPortForwardingDataType_Max;
}

QVariant UIPortForwardingModel::headerData(int iSection, Qt::Orientation enmOrientation, int iRole) const
{
    /* Check argument validness: */
    if (iRole != Qt::DisplayRole || enmOrientation != Qt::Horizontal)
        return QVariant();
    /* Switch for different columns: */
    switch (iSection)
    {
        case UIPortForwardingDataType_Name: return UIPortForwardingTable::tr("Name");
        case UIPortForwardingDataType_Protocol: return UIPortForwardingTable::tr("Protocol");
        case UIPortForwardingDataType_HostIp: return UIPortForwardingTable::tr("Host IP");
        case UIPortForwardingDataType_HostPort: return UIPortForwardingTable::tr("Host Port");
        case UIPortForwardingDataType_GuestIp: return UIPortForwardingTable::tr("Guest IP");
        case UIPortForwardingDataType_GuestPort: return UIPortForwardingTable::tr("Guest Port");
        default: return QVariant();
    }
}

bool UIPortForwardingModel::setData(const QModelIndex &index, const QVariant &value, int iRole)
{
    /* Check index validness: */
    if (!index.isValid() || iRole != Qt::EditRole)
        return false;
    /* Switch for different columns: */
    switch (index.column())
    {
        case UIPortForwardingDataType_Name:
            m_dataList[index.row()]->setName(value.value<NameData>());
            emit dataChanged(index, index);
            return true;
        case UIPortForwardingDataType_Protocol:
            m_dataList[index.row()]->setProtocol(value.value<KNATProtocol>());
            emit dataChanged(index, index);
            return true;
        case UIPortForwardingDataType_HostIp:
            m_dataList[index.row()]->setHostIp(value.value<IpData>());
            emit dataChanged(index, index);
            return true;
        case UIPortForwardingDataType_HostPort:
            m_dataList[index.row()]->setHostPort(value.value<PortData>());
            emit dataChanged(index, index);
            return true;
        case UIPortForwardingDataType_GuestIp:
            m_dataList[index.row()]->setGuestIp(value.value<IpData>());
            emit dataChanged(index, index);
            return true;
        case UIPortForwardingDataType_GuestPort:
            m_dataList[index.row()]->setGuestPort(value.value<PortData>());
            emit dataChanged(index, index);
            return true;
        default:
            return false;
    }
}

QVariant UIPortForwardingModel::data(const QModelIndex &index, int iRole) const
{
    /* Check index validness: */
    if (!index.isValid())
        return QVariant();
    /* Switch for different roles: */
    switch (iRole)
    {
        /* Display role: */
        case Qt::DisplayRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case UIPortForwardingDataType_Name: return m_dataList[index.row()]->name();
                case UIPortForwardingDataType_Protocol: return gpConverter->toString(m_dataList[index.row()]->protocol());
                case UIPortForwardingDataType_HostIp: return m_dataList[index.row()]->hostIp();
                case UIPortForwardingDataType_HostPort: return m_dataList[index.row()]->hostPort().value();
                case UIPortForwardingDataType_GuestIp: return m_dataList[index.row()]->guestIp();
                case UIPortForwardingDataType_GuestPort: return m_dataList[index.row()]->guestPort().value();
                default: return QVariant();
            }
        }
        /* Edit role: */
        case Qt::EditRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case UIPortForwardingDataType_Name: return QVariant::fromValue(m_dataList[index.row()]->name());
                case UIPortForwardingDataType_Protocol: return QVariant::fromValue(m_dataList[index.row()]->protocol());
                case UIPortForwardingDataType_HostIp: return QVariant::fromValue(m_dataList[index.row()]->hostIp());
                case UIPortForwardingDataType_HostPort: return QVariant::fromValue(m_dataList[index.row()]->hostPort());
                case UIPortForwardingDataType_GuestIp: return QVariant::fromValue(m_dataList[index.row()]->guestIp());
                case UIPortForwardingDataType_GuestPort: return QVariant::fromValue(m_dataList[index.row()]->guestPort());
                default: return QVariant();
            }
        }
        /* Alignment role: */
        case Qt::TextAlignmentRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case UIPortForwardingDataType_Name:
                case UIPortForwardingDataType_Protocol:
                case UIPortForwardingDataType_HostPort:
                case UIPortForwardingDataType_GuestPort:
                    return (int)(Qt::AlignLeft | Qt::AlignVCenter);
                case UIPortForwardingDataType_HostIp:
                case UIPortForwardingDataType_GuestIp:
                    return Qt::AlignCenter;
                default: return QVariant();
            }
        }
        case Qt::SizeHintRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case UIPortForwardingDataType_HostIp:
                case UIPortForwardingDataType_GuestIp:
                {
                    const QFontMetrics fm = m_pPortForwardingTable->fontMetrics();
                    return QSize(fm.horizontalAdvance(" 888.888.888.888 "), fm.height());
                }
                default: return QVariant();
            }
        }
        default: break;
    }
    /* Return wrong value: */
    return QVariant();
}

QITableView *UIPortForwardingModel::view() const
{
    return m_pPortForwardingTable->view();
}


/*********************************************************************************************************************************
*   Class UIPortForwardingView implementation.                                                                                   *
*********************************************************************************************************************************/

UIPortForwardingView::UIPortForwardingView(bool fIPv6, QWidget *pParent /* = 0 */)
    : QITableView(pParent)
    , m_fIPv6(fIPv6)
    , m_pItemEditorFactory(0)
{
    prepare();
}

UIPortForwardingView::~UIPortForwardingView()
{
    cleanup();
}

bool UIPortForwardingView::event(QEvent *pEvent)
{
    /* Process different event-types: */
    switch (pEvent->type())
    {
        /* Adjust table on show/resize events: */
        case QEvent::Show:
        case QEvent::Resize:
        {
            // WORKAROUND:
            // Make sure layout requests really processed first of all;
            // This is required for the 1st show event but let it be ..
            QCoreApplication::sendPostedEvents(0, QEvent::LayoutRequest);
            adjust();
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QITableView::event(pEvent);
}

void UIPortForwardingView::rowsInserted(const QModelIndex &parent, int iStart, int iEnd)
{
    /* Call to base-class: */
    QITableView::rowsInserted(parent, iStart, iEnd);
    /* Adjust table on rows being inserted: */
    adjust();
}

void UIPortForwardingView::rowsAboutToBeRemoved(const QModelIndex &parent, int start, int end)
{
    /* Call to base-class: */
    QITableView::rowsAboutToBeRemoved(parent, start, end);
    /* Adjust table on rows being removed: */
    adjust();
}

void UIPortForwardingView::prepare()
{
    // WORAKROUND: Do we still need this?
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    /* Disable TAB-key navigation: */
    setTabKeyNavigation(false);
    /* Adjust selection mode: */
    setSelectionMode(QAbstractItemView::SingleSelection);
    /* Configure context-menu policy: */
    setContextMenuPolicy(Qt::CustomContextMenu);

    /* Adjust header policy: */
    verticalHeader()->hide();
    verticalHeader()->setDefaultSectionSize((int)(verticalHeader()->minimumSectionSize() * 1.33));

    /* We certainly have abstract item delegate: */
    QAbstractItemDelegate *pAbstractItemDelegate = itemDelegate();
    if (pAbstractItemDelegate)
    {
        /* But is this also styled item delegate? */
        QStyledItemDelegate *pStyledItemDelegate = qobject_cast<QStyledItemDelegate*>(pAbstractItemDelegate);
        if (pStyledItemDelegate)
        {
            /* Create new item editor factory: */
            m_pItemEditorFactory = new QItemEditorFactory;
            if (m_pItemEditorFactory)
            {
                /* Register NameEditor as the NameData editor: */
                int iNameId = qRegisterMetaType<NameData>();
                QStandardItemEditorCreator<NameEditor> *pNameEditorItemCreator = new QStandardItemEditorCreator<NameEditor>();
                m_pItemEditorFactory->registerEditor(iNameId, pNameEditorItemCreator);

                /* Register ProtocolEditor as the KNATProtocol editor: */
                int iProtocolId = qRegisterMetaType<KNATProtocol>();
                QStandardItemEditorCreator<ProtocolEditor> *pProtocolEditorItemCreator = new QStandardItemEditorCreator<ProtocolEditor>();
                m_pItemEditorFactory->registerEditor(iProtocolId, pProtocolEditorItemCreator);

                /* Register IPv4Editor/IPv6Editor as the IpData editor: */
                int iIpId = qRegisterMetaType<IpData>();
                if (!m_fIPv6)
                {
                    QStandardItemEditorCreator<IPv4Editor> *pIPv4EditorItemCreator = new QStandardItemEditorCreator<IPv4Editor>();
                    m_pItemEditorFactory->registerEditor(iIpId, pIPv4EditorItemCreator);
                }
                else
                {
                    QStandardItemEditorCreator<IPv6Editor> *pIPv6EditorItemCreator = new QStandardItemEditorCreator<IPv6Editor>();
                    m_pItemEditorFactory->registerEditor(iIpId, pIPv6EditorItemCreator);
                }

                /* Register PortEditor as the PortData editor: */
                int iPortId = qRegisterMetaType<PortData>();
                QStandardItemEditorCreator<PortEditor> *pPortEditorItemCreator = new QStandardItemEditorCreator<PortEditor>();
                m_pItemEditorFactory->registerEditor(iPortId, pPortEditorItemCreator);

                /* Set newly created item editor factory for table delegate: */
                pStyledItemDelegate->setItemEditorFactory(m_pItemEditorFactory);
            }
        }
    }
}

void UIPortForwardingView::cleanup()
{
    /* Cleanup editor factory delegate: */
    delete m_pItemEditorFactory;
    m_pItemEditorFactory = 0;
}


void UIPortForwardingView::adjust()
{
    horizontalHeader()->setStretchLastSection(false);
    /* If table is NOT empty: */
    if (model()->rowCount())
    {
        /* Resize table to contents size-hint and emit a spare place for first column: */
        resizeColumnsToContents();
        uint uFullWidth = viewport()->width();
        for (uint u = 1; u < UIPortForwardingDataType_Max; ++u)
            uFullWidth -= horizontalHeader()->sectionSize(u);
        horizontalHeader()->resizeSection(UIPortForwardingDataType_Name, uFullWidth);
    }
    /* If table is empty: */
    else
    {
        /* Resize table columns to be equal in size: */
        uint uFullWidth = viewport()->width();
        for (uint u = 0; u < UIPortForwardingDataType_Max; ++u)
            horizontalHeader()->resizeSection(u, uFullWidth / UIPortForwardingDataType_Max);
    }
    horizontalHeader()->setStretchLastSection(true);
}


/*********************************************************************************************************************************
*   Class UIPortForwardingTable implementation.                                                                                  *
*********************************************************************************************************************************/

UIPortForwardingTable::UIPortForwardingTable(const UIPortForwardingDataList &rules, bool fIPv6, bool fAllowEmptyGuestIPs)
    : m_rules(rules)
    , m_fIPv6(fIPv6)
    , m_fAllowEmptyGuestIPs(fAllowEmptyGuestIPs)
    , m_fTableDataChanged(false)
    , m_pLayout(0)
    , m_pTableView(0)
    , m_pToolBar(0)
    , m_pTableModel(0)
    , m_pActionAdd(0)
    , m_pActionCopy(0)
    , m_pActionRemove(0)
{
    prepare();
}

QITableView *UIPortForwardingTable::view() const
{
    return m_pTableView;
}

UIPortForwardingDataList UIPortForwardingTable::rules() const
{
    return m_pTableModel->rules();
}

void UIPortForwardingTable::setRules(const UIPortForwardingDataList &newRules,
                                     bool fHoldPosition /* = false */)
{
    /* Store last chosen item: */
    QString strPreviousName;
    const QModelIndex currentIndex = m_pTableView->currentIndex();
    if (currentIndex.isValid())
    {
        const int iCurrentRow = currentIndex.row();
        const QModelIndex firstCurrentIndex = m_pTableModel->index(iCurrentRow, 0);
        if (firstCurrentIndex.isValid())
            strPreviousName = m_pTableModel->data(firstCurrentIndex, Qt::DisplayRole).toString();
    }

    /* Update the list of rules: */
    m_rules = newRules;
    m_pTableModel->setRules(m_rules);

    /* Restore last chosen item: */
    if (fHoldPosition && !strPreviousName.isEmpty())
    {
        for (int i = 0; i < m_pTableModel->rowCount(); ++i)
        {
            const QModelIndex firstIteratedIndex = m_pTableModel->index(i, 0);
            if (firstIteratedIndex.isValid())
            {
                const QString strName = m_pTableModel->data(firstIteratedIndex, Qt::DisplayRole).toString();
                if (strName == strPreviousName)
                    m_pTableView->setCurrentIndex(m_pTableModel->index(i, 0));
            }
        }
    }
}

void UIPortForwardingTable::setGuestAddressHint(const QString &strHint)
{
    m_strGuestAddressHint = strHint;
    m_pTableModel->setGuestAddressHint(m_strGuestAddressHint);
}

bool UIPortForwardingTable::validate() const
{
    /* Validate table: */
    QList<NameData> names;
    QList<UIPortForwardingDataUnique> rules;
    for (int i = 0; i < m_pTableModel->rowCount(); ++i)
    {
        /* Some of variables: */
        const NameData strName = m_pTableModel->data(m_pTableModel->index(i, UIPortForwardingDataType_Name), Qt::EditRole).value<NameData>();
        const KNATProtocol enmProtocol = m_pTableModel->data(m_pTableModel->index(i, UIPortForwardingDataType_Protocol), Qt::EditRole).value<KNATProtocol>();
        const PortData hostPort = m_pTableModel->data(m_pTableModel->index(i, UIPortForwardingDataType_HostPort), Qt::EditRole).value<PortData>().value();
        const PortData guestPort = m_pTableModel->data(m_pTableModel->index(i, UIPortForwardingDataType_GuestPort), Qt::EditRole).value<PortData>().value();
        const IpData strHostIp = m_pTableModel->data(m_pTableModel->index(i, UIPortForwardingDataType_HostIp), Qt::EditRole).value<IpData>();
        const IpData strGuestIp = m_pTableModel->data(m_pTableModel->index(i, UIPortForwardingDataType_GuestIp), Qt::EditRole).value<IpData>();

        /* If at least one port is 'zero': */
        if (hostPort.value() == 0 || guestPort.value() == 0)
            return msgCenter().warnAboutIncorrectPort(window());
        /* If at least one address is incorrect: */
        if (!(   strHostIp.trimmed().isEmpty()
              || RTNetIsIPv4AddrStr(strHostIp.toUtf8().constData())
              || RTNetIsIPv6AddrStr(strHostIp.toUtf8().constData())
              || RTNetStrIsIPv4AddrAny(strHostIp.toUtf8().constData())
              || RTNetStrIsIPv6AddrAny(strHostIp.toUtf8().constData())))
            return msgCenter().warnAboutIncorrectAddress(window());
        if (!(   strGuestIp.trimmed().isEmpty()
              || RTNetIsIPv4AddrStr(strGuestIp.toUtf8().constData())
              || RTNetIsIPv6AddrStr(strGuestIp.toUtf8().constData())
              || RTNetStrIsIPv4AddrAny(strGuestIp.toUtf8().constData())
              || RTNetStrIsIPv6AddrAny(strGuestIp.toUtf8().constData())))
            return msgCenter().warnAboutIncorrectAddress(window());
        /* If empty guest address is not allowed: */
        if (   !m_fAllowEmptyGuestIPs
            && strGuestIp.isEmpty())
            return msgCenter().warnAboutEmptyGuestAddress(window());

        /* Make sure non of the names were previosly used: */
        if (!names.contains(strName))
            names << strName;
        else
            return msgCenter().warnAboutNameShouldBeUnique(window());

        /* Make sure non of the rules were previosly used: */
        UIPortForwardingDataUnique rule(enmProtocol, hostPort, strHostIp);
        if (!rules.contains(rule))
            rules << rule;
        else
            return msgCenter().warnAboutRulesConflict(window());
    }
    /* True by default: */
    return true;
}

void UIPortForwardingTable::makeSureEditorDataCommitted()
{
    m_pTableView->makeSureEditorDataCommitted();
}

void UIPortForwardingTable::sltRetranslateUI()
{
    /* Table translations: */
    m_pTableView->setWhatsThis(tr("Contains a list of port forwarding rules."));

    /* Set action's text: */
    m_pActionAdd->setText(tr("Add New Rule"));
    m_pActionCopy->setText(tr("Copy Selected Rule"));
    m_pActionRemove->setText(tr("Remove Selected Rule"));

    m_pActionAdd->setWhatsThis(tr("Adds new port forwarding rule."));
    m_pActionCopy->setWhatsThis(tr("Copies selected port forwarding rule."));
    m_pActionRemove->setWhatsThis(tr("Removes selected port forwarding rule."));

    m_pActionAdd->setToolTip(m_pActionAdd->whatsThis());
    m_pActionCopy->setToolTip(m_pActionCopy->whatsThis());
    m_pActionRemove->setToolTip(m_pActionRemove->whatsThis());
}

void UIPortForwardingTable::sltAddRule()
{
    m_pTableModel->addRule(QModelIndex());
    m_pTableView->setFocus();
    m_pTableView->setCurrentIndex(m_pTableModel->index(m_pTableModel->rowCount() - 1, 0));
    sltUpdateActions();
}

void UIPortForwardingTable::sltCopyRule()
{
    m_pTableModel->addRule(m_pTableView->currentIndex());
    m_pTableView->setFocus();
    m_pTableView->setCurrentIndex(m_pTableModel->index(m_pTableModel->rowCount() - 1, 0));
    sltUpdateActions();
}

void UIPortForwardingTable::sltRemoveRule()
{
    m_pTableModel->removeRule(m_pTableView->currentIndex());
    m_pTableView->setFocus();
    sltUpdateActions();
}

void UIPortForwardingTable::sltTableDataChanged()
{
    m_fTableDataChanged = true;
    emit sigDataChanged();
}

void UIPortForwardingTable::sltUpdateActions()
{
    m_pActionCopy->setEnabled(m_pTableView->currentIndex().isValid());
    m_pActionRemove->setEnabled(m_pTableView->currentIndex().isValid());
}

void UIPortForwardingTable::sltShowTableContexMenu(const QPoint &pos)
{
    /* Prepare and execute context menu: */
    QMenu menu(m_pTableView);
    if (m_pTableView->indexAt(pos).isValid())
    {
        menu.addAction(m_pActionCopy);
        menu.addAction(m_pActionRemove);
    }
    else
        menu.addAction(m_pActionAdd);
    menu.exec(m_pTableView->viewport()->mapToGlobal(pos));
}

void UIPortForwardingTable::prepare()
{
    /* Prepare layout: */
    prepareLayout();

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIPortForwardingTable::sltRetranslateUI);
}

void UIPortForwardingTable::prepareLayout()
{
    /* Create layout: */
    m_pLayout = new QHBoxLayout(this);
    if (m_pLayout)
    {
        /* Configure layout: */
#ifdef VBOX_WS_MAC
        /* On macOS we can do a bit of smoothness: */
        m_pLayout->setContentsMargins(0, 0, 0, 0);
        m_pLayout->setSpacing(3);
#else
        m_pLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing) / 3);
#endif

        /* Prepare model: */
        prepareTableModel();
        /* Prepare view: */
        prepareTableView();

        /* Prepare toolbar: */
        prepareToolbar();
    }
}

void UIPortForwardingTable::prepareTableModel()
{
    /* Create table-model: */
    m_pTableModel = new UIPortForwardingModel(this, m_rules);
    if (m_pTableModel)
    {
        /* Configure table-model: */
        connect(m_pTableModel, &UIPortForwardingModel::dataChanged,
                this, &UIPortForwardingTable::sltTableDataChanged);
        connect(m_pTableModel, &UIPortForwardingModel::rowsInserted,
                this, &UIPortForwardingTable::sltTableDataChanged);
        connect(m_pTableModel, &UIPortForwardingModel::rowsRemoved,
                this, &UIPortForwardingTable::sltTableDataChanged);
    }
}

void UIPortForwardingTable::prepareTableView()
{
    /* Create table-view: */
    m_pTableView = new UIPortForwardingView(m_fIPv6, this);
    if (m_pTableView)
    {
        /* Assign model: */
        if (m_pTableModel)
            m_pTableView->setModel(m_pTableModel);

        /* Finish configure table-view (after model is assigned): */
        connect(m_pTableView, &UIPortForwardingView::sigCurrentChanged,
                this, &UIPortForwardingTable::sltUpdateActions);
        connect(m_pTableView, &UIPortForwardingView::customContextMenuRequested,
                this, &UIPortForwardingTable::sltShowTableContexMenu);

        /* Add into layout: */
        m_pLayout->addWidget(m_pTableView);
    }
}

void UIPortForwardingTable::prepareToolbar()
{
    /* Create toolbar: */
    m_pToolBar = new QIToolBar;
    if (m_pToolBar)
    {
        /* Determine icon metric: */
        const QStyle *pStyle = QApplication::style();
        const int iIconMetric = pStyle->pixelMetric(QStyle::PM_SmallIconSize);

        /* Configure toolbar: */
        m_pToolBar->setIconSize(QSize(iIconMetric, iIconMetric));
        m_pToolBar->setOrientation(Qt::Vertical);

        /* Create 'Add' action: */
        m_pActionAdd = new QAction(this);
        if (m_pActionAdd)
        {
            /* Configure action: */
            m_pActionAdd->setShortcut(QKeySequence("Ins"));
            m_pActionAdd->setIcon(UIIconPool::iconSet(":/controller_add_16px.png", ":/controller_add_disabled_16px.png"));
            connect(m_pActionAdd, &QAction::triggered, this, &UIPortForwardingTable::sltAddRule);
            m_pToolBar->addAction(m_pActionAdd);
        }

        /* Create 'Copy' action: */
        m_pActionCopy = new QAction(this);
        if (m_pActionCopy)
        {
            /* Configure action: */
            m_pActionCopy->setIcon(UIIconPool::iconSet(":/controller_add_16px.png", ":/controller_add_disabled_16px.png"));
            connect(m_pActionCopy, &QAction::triggered, this, &UIPortForwardingTable::sltCopyRule);
        }

        /* Create 'Remove' action: */
        m_pActionRemove = new QAction(this);
        if (m_pActionRemove)
        {
            /* Configure action: */
            m_pActionRemove->setShortcut(QKeySequence("Del"));
            m_pActionRemove->setIcon(UIIconPool::iconSet(":/controller_remove_16px.png", ":/controller_remove_disabled_16px.png"));
            connect(m_pActionRemove, &QAction::triggered, this, &UIPortForwardingTable::sltRemoveRule);
            m_pToolBar->addAction(m_pActionRemove);
        }

        /* Add into layout: */
        m_pLayout->addWidget(m_pToolBar);
    }

    /* Update actions finally: */
    sltUpdateActions();
}


#include "UIPortForwardingTable.moc"
