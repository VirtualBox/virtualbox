/* $Id: UIFormEditorWidget.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFormEditorWidget class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QHeaderView>
#include <QItemEditorFactory>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialog.h"
#include "QIDialogButtonBox.h"
#include "QIStyledItemDelegate.h"
#include "QITableView.h"
#include "UIFormEditorWidget.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UINotificationCenter.h"
#include "UITranslationEventListener.h"

/* COM includes: */
#include "CBooleanFormValue.h"
#include "CChoiceFormValue.h"
#include "CForm.h"
#include "CFormValue.h"
#include "CRangedIntegerFormValue.h"
#include "CRangedInteger64FormValue.h"
#include "CStringFormValue.h"
#include "CSystemProperties.h"
#include "CVirtualSystemDescriptionForm.h"

/* VirtualBox interface declarations: */
#include <VBox/com/VirtualBox.h>

/* External includes: */
#include <math.h>


/** Form Editor data types. */
enum UIFormEditorDataType
{
    UIFormEditorDataType_Name,
    UIFormEditorDataType_Value,
    UIFormEditorDataType_Max
};


/** Class used to hold text data. */
class TextData
{
public:

    /** Constructs null text data. */
    TextData() {}
    /** Constructs text data on the basis of passed @a strText and @a index. */
    TextData(const QString &strText, const QModelIndex index = QModelIndex())
        : m_strText(strText), m_index(index) {}
    /** Constructs text data on the basis of @a another text data. */
    TextData(const TextData &another)
        : m_strText(another.text()), m_index(another.index()) {}

    /** Assigns values of @a another text to this one. */
    TextData &operator=(const TextData &another)
    {
        m_strText = another.text();
        m_index = another.index();
        return *this;
    }

    /** Returns text value. */
    QString text() const { return m_strText; }

    /** Defines model @a index. */
    void setIndex(const QModelIndex &index) { m_index = index; }
    /** Returns model index. */
    QModelIndex index() const { return m_index; }

private:

    /** Holds text value. */
    QString      m_strText;
    /** Holds model index. */
    QModelIndex  m_index;
};
Q_DECLARE_METATYPE(TextData);


/** Class used to hold choice data. */
class ChoiceData
{
public:

    /** Constructs null choice data. */
    ChoiceData()
        : m_iSelectedIndex(-1) {}
    /** Constructs choice data on the basis of passed @a values and @a iSelectedIndex. */
    ChoiceData(const QVector<QString> &values, int iSelectedIndex)
        : m_values(values), m_iSelectedIndex(iSelectedIndex) {}
    /** Constructs choice data on the basis of @a another choice data. */
    ChoiceData(const ChoiceData &another)
        : m_values(another.values()), m_iSelectedIndex(another.selectedIndex()) {}

    /** Assigns values of @a another choice to this one. */
    ChoiceData &operator=(const ChoiceData &another)
    {
        m_values = another.values();
        m_iSelectedIndex = another.selectedIndex();
        return *this;
    }

    /** Returns values vector. */
    QVector<QString> values() const { return m_values; }
    /** Returns selected index. */
    int selectedIndex() const { return m_iSelectedIndex; }
    /** Returns selected value. */
    QString selectedValue() const
    {
        return   m_iSelectedIndex >= 0 && m_iSelectedIndex < m_values.size()
               ? m_values.at(m_iSelectedIndex) : QString();
    }

private:

    /** Holds values vector. */
    QVector<QString>  m_values;
    /** Holds selected index. */
    int               m_iSelectedIndex;
};
Q_DECLARE_METATYPE(ChoiceData);


/** Class used to hold ranged-integer data. */
class RangedIntegerData
{
public:

    /** Constructs null ranged-integer data. */
    RangedIntegerData()
        : m_iMinimum(-1), m_iMaximum(-1)
        , m_iInteger(-1), m_strSuffix(QString()) {}
    /** Constructs ranged-integer data on the basis of passed @a iMinimum, @a iMaximum, @a iInteger and @a strSuffix. */
    RangedIntegerData(int iMinimum, int iMaximum, int iInteger, const QString strSuffix)
        : m_iMinimum(iMinimum), m_iMaximum(iMaximum)
        , m_iInteger(iInteger), m_strSuffix(strSuffix) {}
    /** Constructs ranged-integer data on the basis of @a another ranged-integer data. */
    RangedIntegerData(const RangedIntegerData &another)
        : m_iMinimum(another.minimum()), m_iMaximum(another.maximum())
        , m_iInteger(another.integer()), m_strSuffix(another.suffix()) {}

    /** Assigns values of @a another ranged-integer to this one. */
    RangedIntegerData &operator=(const RangedIntegerData &another)
    {
        m_iMinimum = another.minimum();
        m_iMaximum = another.maximum();
        m_iInteger = another.integer();
        m_strSuffix = another.suffix();
        return *this;
    }

    /** Returns minimum value. */
    int minimum() const { return m_iMinimum; }
    /** Returns maximum value. */
    int maximum() const { return m_iMaximum; }
    /** Returns current value. */
    int integer() const { return m_iInteger; }
    /** Returns suffix value. */
    QString suffix() const { return m_strSuffix; }

private:

    /** Holds minimum value. */
    int      m_iMinimum;
    /** Holds maximum value. */
    int      m_iMaximum;
    /** Holds current value. */
    int      m_iInteger;
    /** Holds suffix value. */
    QString  m_strSuffix;
};
Q_DECLARE_METATYPE(RangedIntegerData);


/** Class used to hold ranged-integer64 data. */
class RangedInteger64Data
{
public:

    /** Constructs null ranged-integer64 data. */
    RangedInteger64Data()
        : m_iMinimum(-1), m_iMaximum(-1)
        , m_iInteger(-1), m_strSuffix(QString()) {}
    /** Constructs ranged-integer64 data on the basis of passed @a iMinimum, @a iMaximum, @a iInteger and @a strSuffix. */
    RangedInteger64Data(qlonglong iMinimum, qlonglong iMaximum, qlonglong iInteger, const QString strSuffix)
        : m_iMinimum(iMinimum), m_iMaximum(iMaximum)
        , m_iInteger(iInteger), m_strSuffix(strSuffix) {}
    /** Constructs ranged-integer64 data on the basis of @a another ranged-integer data. */
    RangedInteger64Data(const RangedInteger64Data &another)
        : m_iMinimum(another.minimum()), m_iMaximum(another.maximum())
        , m_iInteger(another.integer()), m_strSuffix(another.suffix()) {}

    /** Assigns values of @a another ranged-integer to this one. */
    RangedInteger64Data &operator=(const RangedInteger64Data &another)
    {
        m_iMinimum = another.minimum();
        m_iMaximum = another.maximum();
        m_iInteger = another.integer();
        m_strSuffix = another.suffix();
        return *this;
    }

    /** Returns minimum value. */
    qlonglong minimum() const { return m_iMinimum; }
    /** Returns maximum value. */
    qlonglong maximum() const { return m_iMaximum; }
    /** Returns current value. */
    qlonglong integer() const { return m_iInteger; }
    /** Returns suffix value. */
    QString suffix() const { return m_strSuffix; }

private:

    /** Holds minimum value. */
    qlonglong  m_iMinimum;
    /** Holds maximum value. */
    qlonglong  m_iMaximum;
    /** Holds current value. */
    qlonglong  m_iInteger;
    /** Holds suffix value. */
    QString    m_strSuffix;
};
Q_DECLARE_METATYPE(RangedInteger64Data);


/** QWidget extension used as dummy TextData editor.
  * It's not actually an editor, but Edit... button instead which opens
  * real editor passing stored model index received from TextData value. */
class TextEditor : public QWidget
{
    Q_OBJECT;
    Q_PROPERTY(TextData text READ text WRITE setText USER true);

public:

    /** Constructs TextData editor passing @a pParent to the base-class. */
    TextEditor(QWidget *pParent = 0);

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();
    /** Handles button click. */
    void sltHandleButtonClick();

private:

    /** Prepares all. */
    void prepare();

    /** Defines @a text. */
    void setText(const TextData &text);
    /** Returns text. */
    TextData text() const;

    /** Holds the button instance. */
    QPushButton *m_pButton;
    /** Holds the multiline text. */
    QString      m_strMultilineText;
    /** Holds the model index. */
    QModelIndex  m_index;
};


/** QComboBox extension used as ChoiceData editor. */
class ChoiceEditor : public QComboBox
{
    Q_OBJECT;
    Q_PROPERTY(ChoiceData choice READ choice WRITE setChoice USER true);

signals:

    /** Notifies listener about data should be committed. */
    void sigCommitData(QWidget *pThis);

public:

    /** Constructs ChoiceData editor passing @a pParent to the base-class. */
    ChoiceEditor(QWidget *pParent = 0);

private slots:

    /** Handles current index change. */
    void sltCurrentIndexChanged();

private:

    /** Defines the @a choice. */
    void setChoice(const ChoiceData &choice);
    /** Returns the choice. */
    ChoiceData choice() const;
};


/** QSpinBox extension used as RangedIntegerData editor. */
class RangedIntegerEditor : public QSpinBox
{
    Q_OBJECT;
    Q_PROPERTY(RangedIntegerData rangedInteger READ rangedInteger WRITE setRangedInteger USER true);

public:

    /** Constructs RangedIntegerData editor passing @a pParent to the base-class. */
    RangedIntegerEditor(QWidget *pParent = 0);

private:

    /** Defines @a rangedInteger. */
    void setRangedInteger(const RangedIntegerData &rangedInteger);
    /** Returns ranged-integer. */
    RangedIntegerData rangedInteger() const;

    /** Holds the unchanged suffix. */
    QString  m_strSuffix;
};


/** QLineEdit extension used as RangedInteger64Data editor. */
class RangedInteger64Editor : public QLineEdit
{
    Q_OBJECT;
    Q_PROPERTY(RangedInteger64Data rangedInteger64 READ rangedInteger64 WRITE setRangedInteger64 USER true);

public:

    /** Constructs RangedInteger64Data editor passing @a pParent to the base-class. */
    RangedInteger64Editor(QWidget *pParent = 0);

private:

    /** Defines @a rangedInteger. */
    void setRangedInteger64(const RangedInteger64Data &rangedInteger64);
    /** Returns ranged-integer. */
    RangedInteger64Data rangedInteger64() const;

    /** Holds the minimum guest RAM in MBs. */
    qlonglong  m_iMinimumGuestRAM;
    /** Holds the maximum value. */
    qlonglong  m_iMaximumGuestRAM;

    /** Holds the minimum value. */
    qlonglong  m_iMinimum;
    /** Holds the maximum value. */
    qlonglong  m_iMaximum;
    /** Holds the unchanged suffix. */
    QString    m_strSuffix;
};


/** QITableViewCell extension used as Form Editor table-view cell. */
class UIFormEditorCell : public QITableViewCell
{
    Q_OBJECT;

public:

    /** Constructs table cell on the basis of certain @a strText, passing @a pParent to the base-class. */
    UIFormEditorCell(QITableViewRow *pParent, const QString &strText = QString());

    /** Returns the cell text. */
    virtual QString text() const RT_OVERRIDE { return m_strText; }

    /** Defines the cell @a strText. */
    void setText(const QString &strText) { m_strText = strText; }

private:

    /** Holds the cell text. */
    QString  m_strText;
};


/** QITableViewRow extension used as Form Editor table-view row. */
class UIFormEditorRow : public QITableViewRow
{
    Q_OBJECT;

public:

    /** Constructs table row on the basis of certain @a comValue, passing @a pParent to the base-class.
      * @param  pFormEditorWidget  Brings the root form-editor widget reference. */
    UIFormEditorRow(QITableView *pParent, UIFormEditorWidget *pFormEditorWidget, const CFormValue &comValue);
    /** Destructs table row. */
    virtual ~UIFormEditorRow() RT_OVERRIDE;

    /** Returns value type. */
    KFormValueType valueType() const { return m_enmValueType; }

    /** Returns the row name as string. */
    QString nameToString() const;
    /** Returns the row value as string. */
    QString valueToString() const;

    /** Returns whether the row is enabled. */
    bool isEnabled() const;
    /** Returns whether the row is visible. */
    bool isVisible() const;

    /** Returns value cast to bool. */
    bool toBool() const;
    /** Defines @a fBool value. */
    void setBool(bool fBool);

    /** Returns whether cached string value is multiline. */
    bool isMultilineString() const;
    /** Returns value cast to text. */
    TextData toText() const;
    /** Defines @a text value. */
    void setText(const TextData &text);
    /** Returns value cast to string. */
    QString toString() const;
    /** Defines @a strString value. */
    void setString(const QString &strString);

    /** Returns value cast to choice. */
    ChoiceData toChoice() const;
    /** Defines @a choice value. */
    void setChoice(const ChoiceData &choice);

    /** Returns value cast to ranged-integer. */
    RangedIntegerData toRangedInteger() const;
    /** Defines @a rangedInteger value. */
    void setRangedInteger(const RangedIntegerData &rangedInteger);

    /** Returns value cast to ranged-integer64. */
    RangedInteger64Data toRangedInteger64() const;
    /** Defines @a rangedInteger64 value. */
    void setRangedInteger64(const RangedInteger64Data &rangedInteger64);

    /** Updates value cells. */
    void updateValueCells();

    /** Check whether generation value is changed. */
    bool isGenerationChanged() const;

protected:

    /** Returns the number of children. */
    virtual int childCount() const RT_OVERRIDE;
    /** Returns the child item with @a iIndex. */
    virtual QITableViewCell *childItem(int iIndex) const RT_OVERRIDE;

private:

    /** Prepares all. */
    void prepare();
    /** Cleanups all. */
    void cleanup();

    /** Holds the root form-editor widget reference. */
    UIFormEditorWidget *m_pFormEditorWidget;

    /** Holds the row value. */
    CFormValue  m_comValue;

    /** Holds the value type. */
    KFormValueType  m_enmValueType;

    /** Holds current generation value. */
    int  m_iGeneration;

    /** Holds cached bool value. */
    bool                 m_fBool;
    /** Holds whether cached string value is multiline. */
    bool                 m_fMultilineString;
    /** Holds cached text value. */
    TextData             m_text;
    /** Holds cached string value. */
    QString              m_strString;
    /** Holds cached choice value. */
    ChoiceData           m_choice;
    /** Holds cached ranged-integer value. */
    RangedIntegerData    m_rangedInteger;
    /** Holds cached ranged-integer64 value. */
    RangedInteger64Data  m_rangedInteger64;

    /** Holds the cell instances. */
    QVector<UIFormEditorCell*>  m_cells;
};


/** QAbstractTableModel subclass used as Form Editor data model. */
class UIFormEditorModel : public QAbstractTableModel
{
    Q_OBJECT;

public:

    /** Constructs Form Editor model passing @a pParent to the base-class. */
    UIFormEditorModel(UIFormEditorWidget *pParent);
    /** Destructs Port Forwarding model. */
    virtual ~UIFormEditorModel() RT_OVERRIDE RT_FINAL;

    /** Clears form. */
    void clearForm();
    /** Defines form @a values. */
    void setFormValues(const CFormValueVector &values);

    /** Creates actual TextData editor for specified @a index. */
    void createTextDataEditor(const QModelIndex &index);

    /** Returns the index of the item in the model specified by the given @a iRow, @a iColumn and @a parentIdx. */
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
    virtual bool setData(const QModelIndex &index, const QVariant &value, int iRole = Qt::EditRole) RT_OVERRIDE RT_FINAL;
    /** Returns the @a iRole data for item with @a index. */
    virtual QVariant data(const QModelIndex &index, int iRole) const RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();

    /** Returns the parent table-view reference. */
    QITableView *view() const;

    /** Updates row generation values. */
    void updateGeneration();

    /** Returns icon hint for specified @a strItemName. */
    QIcon iconHint(const QString &strItemName) const;

    /** Holds the root form-editor widget reference. */
    UIFormEditorWidget *m_pFormEditorWidget;

    /** Holds the Form Editor row list. */
    QList<UIFormEditorRow*>  m_dataList;

    /** Holds the hardcoded icon name map. */
    QMap<QString, QIcon>  m_icons;
};


/** QSortFilterProxyModel subclass used as the Form Editor proxy-model. */
class UIFormEditorProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT;

public:

    /** Constructs the Form Editor proxy-model passing @a pParent to the base-class. */
    UIFormEditorProxyModel(QObject *pParent = 0);

protected:

    /** Returns whether item in the row indicated by the given @a iSourceRow and @a srcParenIdx should be included in the model. */
    virtual bool filterAcceptsRow(int iSourceRow, const QModelIndex &srcParenIdx) const RT_OVERRIDE;
};


/** QITableView extension used as Form Editor table-view. */
class UIFormEditorView : public QITableView
{
    Q_OBJECT;

public:

    /** Constructs Form Editor table-view, passing @a pParent to the base-class. */
    UIFormEditorView(QWidget *pParent = 0);
    /** Destruts Form Editor table-view. */
    virtual ~UIFormEditorView() RT_OVERRIDE;

protected:

    /** Handles @a pEvent. */
    virtual bool event(QEvent *pEvent) RT_OVERRIDE;

protected slots:

    /** Handles rows being inserted.
      * @param  parent  Brings the parent under which new rows being inserted.
      * @param  iStart  Brings the starting position (inclusive).
      * @param  iStart  Brings the end position (inclusive). */
    virtual void rowsInserted(const QModelIndex &parent, int iStart, int iEnd) RT_OVERRIDE;

private:

    /** Prepares everything. */
    void prepare();
    /** Cleanups everything. */
    void cleanup();

    /** Adjusts table contents. */
    void adjust();

    /** Holds the item editor factory instance. */
    QItemEditorFactory *m_pItemEditorFactory;
};


/*********************************************************************************************************************************
*   Class TextEditor implementation.                                                                                             *
*********************************************************************************************************************************/

TextEditor::TextEditor(QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pButton(0)
{
    prepare();
}

void TextEditor::sltRetranslateUI()
{
    m_pButton->setText(UIFormEditorWidget::tr("Edit..."));
}

void TextEditor::sltHandleButtonClick()
{
    /* Redirect the edit call if possible: */
    do
    {
        /* Get the view: */
        if (   !parent()
            || !parent()->parent())
            break;
        QITableView *pView = qobject_cast<QITableView*>(parent()->parent());

        /* Get the proxy model: */
        if (   !pView
            || !pView->model())
            break;
        UIFormEditorProxyModel *pProxyModel = qobject_cast<UIFormEditorProxyModel*>(pView->model());

        /* Get the source model: */
        if (   !pProxyModel
            || !pProxyModel->sourceModel())
            break;
        UIFormEditorModel *pSourceModel = qobject_cast<UIFormEditorModel*>(pProxyModel->sourceModel());

        /* Execute the call: */
        if (!pSourceModel)
            break;
        pSourceModel->createTextDataEditor(m_index);
    }
    while (0);
}

void TextEditor::prepare()
{
    /* Create layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        pLayout->setContentsMargins(0, 0, 0 ,0);
        /* Create button: */
        m_pButton = new QPushButton(this);
        if (m_pButton)
        {
            connect(m_pButton, &QPushButton::clicked, this, &TextEditor::sltHandleButtonClick);
            pLayout->addWidget(m_pButton);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &TextEditor::sltRetranslateUI);
}

void TextEditor::setText(const TextData &text)
{
    m_strMultilineText = text.text();
    m_index = text.index();
}

TextData TextEditor::text() const
{
    return TextData(m_strMultilineText, m_index);
}


/*********************************************************************************************************************************
*   Class ChoiceEditor implementation.                                                                                           *
*********************************************************************************************************************************/

ChoiceEditor::ChoiceEditor(QWidget *pParent /* = 0 */)
    : QComboBox(pParent)
{
    /* Make sure QIStyledDelegate aware of us: */
    setProperty("has_sigCommitData", true);
    /* Configure connections: */
    connect(this, &ChoiceEditor::currentIndexChanged,
            this, &ChoiceEditor::sltCurrentIndexChanged);
}

void ChoiceEditor::sltCurrentIndexChanged()
{
    emit sigCommitData(this);
}

void ChoiceEditor::setChoice(const ChoiceData &choice)
{
    clear();
    addItems(choice.values().toList());
    setCurrentIndex(choice.selectedIndex());
}

ChoiceData ChoiceEditor::choice() const
{
    QVector<QString> choices(count());
    for (int i = 0; i < count(); ++i)
        choices[i] = itemText(i);
    return ChoiceData(choices, currentIndex());
}


/*********************************************************************************************************************************
*   Class RangedIntegerEditor implementation.                                                                                    *
*********************************************************************************************************************************/

RangedIntegerEditor::RangedIntegerEditor(QWidget *pParent /* = 0 */)
    : QSpinBox(pParent)
{
}

void RangedIntegerEditor::setRangedInteger(const RangedIntegerData &rangedInteger)
{
    setMinimum(rangedInteger.minimum());
    setMaximum(rangedInteger.maximum());
    setValue(rangedInteger.integer());
    m_strSuffix = rangedInteger.suffix();
    setSuffix(m_strSuffix.isEmpty() ? QString() :
              QString(" %1").arg(QApplication::translate("UICommon", m_strSuffix.toUtf8().constData())));
}

RangedIntegerData RangedIntegerEditor::rangedInteger() const
{
    return RangedIntegerData(minimum(), maximum(), value(), m_strSuffix);
}


/*********************************************************************************************************************************
*   Class RangedInteger64Editor implementation.                                                                                  *
*********************************************************************************************************************************/

RangedInteger64Editor::RangedInteger64Editor(QWidget *pParent /* = 0 */)
    : QLineEdit(pParent)
    , m_iMinimumGuestRAM(0)
    , m_iMaximumGuestRAM(0)
    , m_iMinimum(0)
    , m_iMaximum(0)
{
    /* Acquire min/max amount of RAM guest in theory could have: */
    CSystemProperties comProps = gpGlobalSession->virtualBox().GetSystemProperties();
    if (comProps.isOk())
    {
        m_iMinimumGuestRAM = comProps.GetMinGuestRAM();
        m_iMaximumGuestRAM = comProps.GetMaxGuestRAM();
    }
}

void RangedInteger64Editor::setRangedInteger64(const RangedInteger64Data &rangedInteger64)
{
    /* Parse incoming rangedInteger64: */
    m_iMinimum = rangedInteger64.minimum();
    m_iMaximum = rangedInteger64.maximum();
    m_strSuffix = rangedInteger64.suffix();
    const qlonglong iValue = rangedInteger64.integer();

    /* Acquire effective values: */
    qlonglong iMinEffective = 0;
    qlonglong iMaxEffective = 0;
    qlonglong iValueEffective = 0;
    /* We wish to represent bytes as megabytes: */
    if (m_strSuffix == "B")
    {
        iMinEffective = m_iMinimum / _1M;
        iMaxEffective = m_iMaximum / _1M;
        iValueEffective = iValue / _1M;
    }
    /* For now we will keep all the other suffixes untouched: */
    else
    {
        iMinEffective = m_iMinimum;
        iMaxEffective = m_iMaximum;
        iValueEffective = iValue;
    }

    /* Make sure minimum, maximum and actual values are within the bounds: */
    iMinEffective = qMax(iMinEffective, m_iMinimumGuestRAM);
    iMaxEffective = qMin(iMaxEffective, m_iMaximumGuestRAM);
    iValueEffective = qMax(iValueEffective, m_iMinimumGuestRAM);
    iValueEffective = qMin(iValueEffective, m_iMaximumGuestRAM);

    /* Finally assign validator bounds and actual value: */
    setValidator(new QIntValidator((int)iMinEffective, (int)iMaxEffective, this));
    setText(QString::number(iValueEffective));
}

RangedInteger64Data RangedInteger64Editor::rangedInteger64() const
{
    const qlonglong iValueEffective = locale().toLongLong(text());

    /* Acquire literal value: */
    qlonglong iValue = 0;
    /* We should bring megabytes back to bytes: */
    if (m_strSuffix == "B")
        iValue = iValueEffective * _1M;
    /* For now we will keep all the other suffixes untouched: */
    else
        iValue = iValueEffective;
    return RangedInteger64Data(m_iMinimum, m_iMaximum, iValue, m_strSuffix);
}


/*********************************************************************************************************************************
*   Class UIFormEditorCell implementation.                                                                                       *
*********************************************************************************************************************************/

UIFormEditorCell::UIFormEditorCell(QITableViewRow *pParent, const QString &strText /* = QString() */)
    : QITableViewCell(pParent)
    , m_strText(strText)
{
}


/*********************************************************************************************************************************
*   Class UIFormEditorRow implementation.                                                                                        *
*********************************************************************************************************************************/

UIFormEditorRow::UIFormEditorRow(QITableView *pParent, UIFormEditorWidget *pFormEditorWidget, const CFormValue &comValue)
    : QITableViewRow(pParent)
    , m_pFormEditorWidget(pFormEditorWidget)
    , m_comValue(comValue)
    , m_enmValueType(KFormValueType_Max)
    , m_iGeneration(0)
    , m_fBool(false)
    , m_fMultilineString(false)
    , m_text(TextData())
    , m_strString(QString())
    , m_choice(ChoiceData())
    , m_rangedInteger(RangedIntegerData())
    , m_rangedInteger64(RangedInteger64Data())
{
    prepare();
}

UIFormEditorRow::~UIFormEditorRow()
{
    cleanup();
}

QString UIFormEditorRow::nameToString() const
{
    return m_cells.at(UIFormEditorDataType_Name)->text();
}

QString UIFormEditorRow::valueToString() const
{
    return m_cells.at(UIFormEditorDataType_Value)->text();
}

bool UIFormEditorRow::isEnabled() const
{
    return m_comValue.GetEnabled();
}

bool UIFormEditorRow::isVisible() const
{
    return m_comValue.GetVisible();
}

bool UIFormEditorRow::toBool() const
{
    AssertReturn(valueType() == KFormValueType_Boolean, false);
    return m_fBool;
}

void UIFormEditorRow::setBool(bool fBool)
{
    AssertReturnVoid(valueType() == KFormValueType_Boolean);
    CBooleanFormValue comValue(m_comValue);
    UINotificationProgressVsdFormValueSet *pNotification = new UINotificationProgressVsdFormValueSet(comValue,
                                                                                                     fBool);
    UINotificationCenter *pCenter = m_pFormEditorWidget->notificationCenter()
                                  ? m_pFormEditorWidget->notificationCenter() : gpNotificationCenter;
    pCenter->handleNow(pNotification);
    updateValueCells();
}

bool UIFormEditorRow::isMultilineString() const
{
    AssertReturn(valueType() == KFormValueType_String, false);
    return m_fMultilineString;
}

TextData UIFormEditorRow::toText() const
{
    AssertReturn(valueType() == KFormValueType_String, TextData());
    return m_text;
}

void UIFormEditorRow::setText(const TextData &text)
{
    AssertReturnVoid(valueType() == KFormValueType_String);
    CStringFormValue comValue(m_comValue);
    UINotificationProgressVsdFormValueSet *pNotification = new UINotificationProgressVsdFormValueSet(comValue,
                                                                                                     text.text());
    UINotificationCenter *pCenter = m_pFormEditorWidget->notificationCenter()
                                  ? m_pFormEditorWidget->notificationCenter() : gpNotificationCenter;
    pCenter->handleNow(pNotification);
    updateValueCells();
}

QString UIFormEditorRow::toString() const
{
    AssertReturn(valueType() == KFormValueType_String, QString());
    return m_strString;
}

void UIFormEditorRow::setString(const QString &strString)
{
    AssertReturnVoid(valueType() == KFormValueType_String);
    CStringFormValue comValue(m_comValue);
    UINotificationProgressVsdFormValueSet *pNotification = new UINotificationProgressVsdFormValueSet(comValue,
                                                                                                     strString);
    UINotificationCenter *pCenter = m_pFormEditorWidget->notificationCenter()
                                  ? m_pFormEditorWidget->notificationCenter() : gpNotificationCenter;
    pCenter->handleNow(pNotification);
    updateValueCells();
}

ChoiceData UIFormEditorRow::toChoice() const
{
    AssertReturn(valueType() == KFormValueType_Choice, ChoiceData());
    return m_choice;
}

void UIFormEditorRow::setChoice(const ChoiceData &choice)
{
    /* Do nothing for empty choices: */
    if (choice.selectedIndex() == -1)
        return;

    AssertReturnVoid(valueType() == KFormValueType_Choice);
    CChoiceFormValue comValue(m_comValue);
    UINotificationProgressVsdFormValueSet *pNotification = new UINotificationProgressVsdFormValueSet(comValue,
                                                                                                     choice.selectedIndex());
    UINotificationCenter *pCenter = m_pFormEditorWidget->notificationCenter()
                                  ? m_pFormEditorWidget->notificationCenter() : gpNotificationCenter;
    pCenter->handleNow(pNotification);
    updateValueCells();
}

RangedIntegerData UIFormEditorRow::toRangedInteger() const
{
    AssertReturn(valueType() == KFormValueType_RangedInteger, RangedIntegerData());
    return m_rangedInteger;
}

void UIFormEditorRow::setRangedInteger(const RangedIntegerData &rangedInteger)
{
    AssertReturnVoid(valueType() == KFormValueType_RangedInteger);
    CRangedIntegerFormValue comValue(m_comValue);
    UINotificationProgressVsdFormValueSet *pNotification = new UINotificationProgressVsdFormValueSet(comValue,
                                                                                                     rangedInteger.integer());
    UINotificationCenter *pCenter = m_pFormEditorWidget->notificationCenter()
                                  ? m_pFormEditorWidget->notificationCenter() : gpNotificationCenter;
    pCenter->handleNow(pNotification);
    updateValueCells();
}

RangedInteger64Data UIFormEditorRow::toRangedInteger64() const
{
    AssertReturn(valueType() == KFormValueType_RangedInteger64, RangedInteger64Data());
    return m_rangedInteger64;
}

void UIFormEditorRow::setRangedInteger64(const RangedInteger64Data &rangedInteger64)
{
    AssertReturnVoid(valueType() == KFormValueType_RangedInteger64);
    CRangedInteger64FormValue comValue(m_comValue);
    UINotificationProgressVsdFormValueSet *pNotification = new UINotificationProgressVsdFormValueSet(comValue,
                                                                                                     rangedInteger64.integer());
    UINotificationCenter *pCenter = m_pFormEditorWidget->notificationCenter()
                                  ? m_pFormEditorWidget->notificationCenter() : gpNotificationCenter;
    pCenter->handleNow(pNotification);
    updateValueCells();
}

void UIFormEditorRow::updateValueCells()
{
    m_iGeneration = m_comValue.GetGeneration();
    /// @todo check for errors

    switch (m_enmValueType)
    {
        case KFormValueType_Boolean:
        {
            CBooleanFormValue comValue(m_comValue);
            m_fBool = comValue.GetSelected();
            m_cells[UIFormEditorDataType_Value]->setText(m_fBool ? "True" : "False");
            /// @todo check for errors
            break;
        }
        case KFormValueType_String:
        {
            CStringFormValue comValue(m_comValue);
            m_fMultilineString = comValue.GetMultiline();
            const QString strString = comValue.GetString();
            if (m_fMultilineString)
                m_text = TextData(strString);
            else
                m_strString = strString;
            m_cells[UIFormEditorDataType_Value]->setText(strString);
            /// @todo check for errors
            break;
        }
        case KFormValueType_Choice:
        {
            CChoiceFormValue comValue(m_comValue);
            const QVector<QString> values = comValue.GetValues();
            const int iSelectedIndex = comValue.GetSelectedIndex();
            m_choice = ChoiceData(values, iSelectedIndex);
            m_cells[UIFormEditorDataType_Value]->setText(m_choice.selectedValue());
            /// @todo check for errors
            break;
        }
        case KFormValueType_RangedInteger:
        {
            CRangedIntegerFormValue comValue(m_comValue);
            const int iMinimum = comValue.GetMinimum();
            const int iMaximum = comValue.GetMaximum();
            const int iInteger = comValue.GetInteger();
            const QString strSuffix = comValue.GetSuffix();
            m_rangedInteger = RangedIntegerData(iMinimum, iMaximum, iInteger, strSuffix);
            m_cells[UIFormEditorDataType_Value]->setText(  strSuffix.isEmpty()
                                                         ? QString::number(iInteger)
                                                         : QString("%1 %2").arg(iInteger)
                                                                           .arg(strSuffix));
            /// @todo check for errors
            break;
        }
        case KFormValueType_RangedInteger64:
        {
            CRangedInteger64FormValue comValue(m_comValue);
            const qlonglong iMinimum = comValue.GetMinimum();
            const qlonglong iMaximum = comValue.GetMaximum();
            const qlonglong iInteger = comValue.GetInteger();
            const QString strSuffix = comValue.GetSuffix();
            m_rangedInteger64 = RangedInteger64Data(iMinimum, iMaximum, iInteger, strSuffix);
            /* Display suffix and effective value can be different: */
            QString strEffectiveSuffix = strSuffix;
            QString strEffectiveValue;
            if (strSuffix.isEmpty())
                strEffectiveValue = QString::number(iInteger);
            else if (strSuffix != "B")
                strEffectiveValue = QString("%1 %2").arg(iInteger).arg(strEffectiveSuffix);
            else
            {
                /* We wish to convert bytes to megabytes: */
                strEffectiveSuffix = "MB";
                strEffectiveValue = QString("%1 %2").arg(iInteger / _1M).arg(strEffectiveSuffix);
            }
            m_cells[UIFormEditorDataType_Value]->setText(strEffectiveValue);
            /// @todo check for errors
            break;
        }
        default:
            break;
    }
}

bool UIFormEditorRow::isGenerationChanged() const
{
    const int iGeneration = m_comValue.GetGeneration();
    /// @todo check for errors
    return m_iGeneration != iGeneration;
}

int UIFormEditorRow::childCount() const
{
    /* Return cell count: */
    return UIFormEditorDataType_Max;
}

QITableViewCell *UIFormEditorRow::childItem(int iIndex) const
{
    /* Make sure index within the bounds: */
    AssertReturn(iIndex >= 0 && iIndex < m_cells.size(), 0);
    /* Return corresponding cell: */
    return m_cells.at(iIndex);
}

void UIFormEditorRow::prepare()
{
    /* Cache value type: */
    m_enmValueType = m_comValue.GetType();
    /// @todo check for errors

    /* Create cells on the basis of variables we have: */
    m_cells.resize(UIFormEditorDataType_Max);
    const QString strName = m_comValue.GetLabel();
    /// @todo check for errors
    m_cells[UIFormEditorDataType_Name] = new UIFormEditorCell(this, strName);
    m_cells[UIFormEditorDataType_Value] = new UIFormEditorCell(this);
    updateValueCells();
}

void UIFormEditorRow::cleanup()
{
    /* Destroy cells: */
    qDeleteAll(m_cells);
    m_cells.clear();
}


/*********************************************************************************************************************************
*   Class UIFormEditorModel implementation.                                                                                      *
*********************************************************************************************************************************/

UIFormEditorModel::UIFormEditorModel(UIFormEditorWidget *pParent)
    : QAbstractTableModel(pParent)
    , m_pFormEditorWidget(pParent)
{
    prepare();
}

UIFormEditorModel::~UIFormEditorModel()
{
    /* Delete the cached data: */
    qDeleteAll(m_dataList);
    m_dataList.clear();
}

void UIFormEditorModel::clearForm()
{
    beginRemoveRows(QModelIndex(), 0, m_dataList.size());
    qDeleteAll(m_dataList);
    m_dataList.clear();
    endRemoveRows();
}

void UIFormEditorModel::setFormValues(const CFormValueVector &values)
{
    /* Delete old lines: */
    clearForm();

    /* Add new lines: */
    beginInsertRows(QModelIndex(), 0, values.size() - 1);
    foreach (const CFormValue &comValue, values)
        m_dataList << new UIFormEditorRow(view(), m_pFormEditorWidget, comValue);
    endInsertRows();
}

void UIFormEditorModel::createTextDataEditor(const QModelIndex &index)
{
    /* Create dialog on-the-fly: */
    QPointer<QIDialog> pDialog = new QIDialog(view());
    if (pDialog)
    {
        /* We will need that pointer: */
        QTextEdit *pEditor = 0;
        /* Create layout: */
        QVBoxLayout *pLayout = new QVBoxLayout(pDialog);
        if (pLayout)
        {
            /* Create text-editor: */
            pEditor = new QTextEdit;
            if (pEditor)
            {
                const TextData td = data(index, Qt::EditRole).value<TextData>();
                pEditor->setPlainText(td.text());
                pLayout->addWidget(pEditor);
            }
            /* Create button-box: */
            QIDialogButtonBox *pBox = new QIDialogButtonBox;
            if (pBox)
            {
                pBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
                connect(pBox, &QIDialogButtonBox::accepted, pDialog.data(), &QIDialog::accept);
                connect(pBox, &QIDialogButtonBox::rejected, pDialog.data(), &QIDialog::reject);
                pLayout->addWidget(pBox);
            }
        }
        /* Execute the dialog: */
        if (pDialog->execute() == QDialog::Accepted)
        {
            const TextData td = TextData(pEditor->toPlainText(), index);
            setData(index, QVariant::fromValue(td));
        }
        /* Cleanup: */
        delete pDialog;
    }
}

QModelIndex UIFormEditorModel::index(int iRow, int iColumn, const QModelIndex &parentIdx /* = QModelIndex() */) const
{
    /* No index for unknown items: */
    if (!hasIndex(iRow, iColumn, parentIdx))
        return QModelIndex();

    /* Provide index users with packed item pointer: */
    UIFormEditorRow *pItem = iRow >= 0 && iRow < m_dataList.size() ? m_dataList.at(iRow) : 0;
    return pItem ? createIndex(iRow, iColumn, pItem) : QModelIndex();
}

Qt::ItemFlags UIFormEditorModel::flags(const QModelIndex &index) const
{
    /* Check index validness: */
    if (!index.isValid())
        return Qt::NoItemFlags;
    /* Switch for different columns: */
    switch (index.column())
    {
        case UIFormEditorDataType_Name:
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        case UIFormEditorDataType_Value:
        {
            Qt::ItemFlags enmFlags = Qt::NoItemFlags;
            if (m_dataList[index.row()]->isEnabled())
            {
                enmFlags |= Qt::ItemIsEnabled | Qt::ItemIsSelectable;
                enmFlags |= m_dataList[index.row()]->valueType() == KFormValueType_Boolean
                          ? Qt::ItemIsUserCheckable : Qt::ItemIsEditable;
            }
            return enmFlags;
        }
        default:
            return Qt::NoItemFlags;
    }
}

int UIFormEditorModel::rowCount(const QModelIndex &) const
{
    return m_dataList.size();
}

int UIFormEditorModel::columnCount(const QModelIndex &) const
{
    return UIFormEditorDataType_Max;
}

QVariant UIFormEditorModel::headerData(int iSection, Qt::Orientation enmOrientation, int iRole) const
{
    /* Check argument validness: */
    if (iRole != Qt::DisplayRole || enmOrientation != Qt::Horizontal)
        return QVariant();
    /* Switch for different columns: */
    switch (iSection)
    {
        case UIFormEditorDataType_Name:
            return UIFormEditorWidget::tr("Name");
        case UIFormEditorDataType_Value:
            return UIFormEditorWidget::tr("Value");
        default:
            return QVariant();
    }
}

bool UIFormEditorModel::setData(const QModelIndex &index, const QVariant &value, int iRole /* = Qt::EditRole */)
{
    /* Check index validness: */
    if (!index.isValid())
        return false;
    /* Switch for different roles: */
    switch (iRole)
    {
        /* Checkstate role: */
        case Qt::CheckStateRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case UIFormEditorDataType_Value:
                {
                    if (m_dataList[index.row()]->valueType() == KFormValueType_Boolean)
                    {
                        const Qt::CheckState enmCheckState = static_cast<Qt::CheckState>(value.toInt());
                        m_dataList[index.row()]->setBool(enmCheckState == Qt::Checked);
                        emit dataChanged(index, index);
                        updateGeneration();
                        return true;
                    }
                    else
                        return false;
                }
                default:
                    return false;
            }
        }
        /* Edit role: */
        case Qt::EditRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case UIFormEditorDataType_Value:
                {
                    switch (m_dataList[index.row()]->valueType())
                    {
                        case KFormValueType_String:
                        {
                            if (value.canConvert<TextData>())
                                m_dataList[index.row()]->setText(value.value<TextData>());
                            else
                                m_dataList[index.row()]->setString(value.toString());
                            emit dataChanged(index, index);
                            updateGeneration();
                            return true;
                        }
                        case KFormValueType_Choice:
                        {
                            m_dataList[index.row()]->setChoice(value.value<ChoiceData>());
                            emit dataChanged(index, index);
                            updateGeneration();
                            return true;
                        }
                        case KFormValueType_RangedInteger:
                        {
                            m_dataList[index.row()]->setRangedInteger(value.value<RangedIntegerData>());
                            emit dataChanged(index, index);
                            updateGeneration();
                            return true;
                        }
                        case KFormValueType_RangedInteger64:
                        {
                            m_dataList[index.row()]->setRangedInteger64(value.value<RangedInteger64Data>());
                            emit dataChanged(index, index);
                            updateGeneration();
                            return true;
                        }
                        default:
                            return false;
                    }
                }
                default:
                    return false;
            }
        }
        default:
            return false;
    }
}

QVariant UIFormEditorModel::data(const QModelIndex &index, int iRole) const
{
    /* Check index validness: */
    if (!index.isValid())
        return QVariant();
    /* Switch for different roles: */
    switch (iRole)
    {
        /* Decoration role: */
        case Qt::DecorationRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case UIFormEditorDataType_Name: return iconHint(m_dataList[index.row()]->nameToString());
                default: return QVariant();
            }
        }
        /* Checkstate role: */
        case Qt::CheckStateRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case UIFormEditorDataType_Value:
                    return   m_dataList[index.row()]->valueType() == KFormValueType_Boolean
                           ? m_dataList[index.row()]->toBool()
                           ? Qt::Checked
                           : Qt::Unchecked
                           : QVariant();
                default:
                    return QVariant();
            }
        }
        /* Display role: */
        case Qt::DisplayRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case UIFormEditorDataType_Name:
                    return m_dataList[index.row()]->nameToString();
                case UIFormEditorDataType_Value:
                    return   m_dataList[index.row()]->valueType() != KFormValueType_Boolean
                           ? m_dataList[index.row()]->valueToString()
                           : QVariant();
                default:
                    return QVariant();
            }
        }
        /* Edit role: */
        case Qt::EditRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case UIFormEditorDataType_Value:
                {
                    switch (m_dataList[index.row()]->valueType())
                    {
                        case KFormValueType_String:
                        {
                            if (m_dataList[index.row()]->isMultilineString())
                            {
                                TextData td = m_dataList[index.row()]->toText();
                                td.setIndex(index);
                                return QVariant::fromValue(td);
                            }
                            else
                                return QVariant::fromValue(m_dataList[index.row()]->toString());
                        }
                        case KFormValueType_Choice:
                            return QVariant::fromValue(m_dataList[index.row()]->toChoice());
                        case KFormValueType_RangedInteger:
                            return QVariant::fromValue(m_dataList[index.row()]->toRangedInteger());
                        case KFormValueType_RangedInteger64:
                            return QVariant::fromValue(m_dataList[index.row()]->toRangedInteger64());
                        default:
                            return QVariant();
                    }
                }
                default:
                    return QVariant();
            }
        }
        /* Alignment role: */
        case Qt::TextAlignmentRole:
        {
            /* Switch for different columns: */
            switch (index.column())
            {
                case UIFormEditorDataType_Name:
                    return (int)(Qt::AlignLeft | Qt::AlignVCenter);
                case UIFormEditorDataType_Value:
                    return   m_dataList[index.row()]->valueType() != KFormValueType_Boolean
                           ? (int)(Qt::AlignLeft | Qt::AlignVCenter)
                           : (int)(Qt::AlignCenter);
                default:
                    return QVariant();
            }
        }
        default:
            return QVariant();
    }
}

void UIFormEditorModel::prepare()
{
    /* Prepare hardcoded icons map: */
    m_icons["Name"]                = UIIconPool::iconSet(":/name_16px.png");
    m_icons["Display Name"]        = UIIconPool::iconSet(":/name_16px.png");
    m_icons["Type"]                = UIIconPool::iconSet(":/system_type_16px.png");
    m_icons["Version"]             = UIIconPool::iconSet(":/system_version_16px.png");
    m_icons["CPU"]                 = UIIconPool::iconSet(":/cpu_16px.png");
    m_icons["Memory"]              = UIIconPool::iconSet(":/ram_16px.png");
    m_icons["Description"]         = UIIconPool::iconSet(":/description_16px.png");
    m_icons["Bucket"]              = UIIconPool::iconSet(":/bucket_16px.png");
    m_icons["Keep Object"]         = UIIconPool::iconSet(":/keep_object_16px.png");
    m_icons["Launch VM"]           = UIIconPool::iconSet(":/launch_vm_16px.png");
    m_icons["Availability Domain"] = UIIconPool::iconSet(":/availability_domain_16px.png");
    m_icons["Shape"]               = UIIconPool::iconSet(":/shape_16px.png");
    m_icons["Disk Size"]           = UIIconPool::iconSet(":/disk_size_16px.png");
    m_icons["VCN"]                 = UIIconPool::iconSet(":/vcn_16px.png");
    m_icons["Subnet"]              = UIIconPool::iconSet(":/subnet_16px.png");
    m_icons["Assign Public IP"]    = UIIconPool::iconSet(":/assign_public_ip_16px.png");
}

QITableView *UIFormEditorModel::view() const
{
    return m_pFormEditorWidget->view();
}

void UIFormEditorModel::updateGeneration()
{
    for (int i = 0; i < m_dataList.size(); ++i)
    {
        UIFormEditorRow *pRow = m_dataList.at(i);
        if (pRow->isGenerationChanged())
        {
            pRow->updateValueCells();
            const QModelIndex changedIndex = index(i, 1);
            emit dataChanged(changedIndex, changedIndex);
        }
    }
}

QIcon UIFormEditorModel::iconHint(const QString &strItemName) const
{
    return m_icons.value(strItemName, UIIconPool::iconSet(":/session_info_16px.png"));
}


/*********************************************************************************************************************************
*   Class UIFormEditorProxyModel implementation.                                                                                 *
*********************************************************************************************************************************/

UIFormEditorProxyModel::UIFormEditorProxyModel(QObject *pParent /* = 0 */)
    : QSortFilterProxyModel(pParent)
{
}

bool UIFormEditorProxyModel::filterAcceptsRow(int iSourceRow, const QModelIndex &sourceParent) const
{
    /* Acquire actual index of source model: */
    QModelIndex i = sourceModel()->index(iSourceRow, 0, sourceParent);
    if (i.isValid())
    {
        /* Get packed item pointer: */
        UIFormEditorRow *pItem = static_cast<UIFormEditorRow*>(i.internalPointer());
        /* Filter invisible items: */
        if (!pItem->isVisible())
            return false;
    }
    return true;
}


/*********************************************************************************************************************************
*   Class UIFormEditorView implementation.                                                                                       *
*********************************************************************************************************************************/

UIFormEditorView::UIFormEditorView(QWidget *pParent /* = 0 */)
    : QITableView(pParent)
    , m_pItemEditorFactory(0)
{
    prepare();
}

UIFormEditorView::~UIFormEditorView()
{
    cleanup();
}

bool UIFormEditorView::event(QEvent *pEvent)
{
    /* Process different event-types: */
    switch (pEvent->type())
    {
        /* Adjust table on show/resize events: */
        case QEvent::Show:
        case QEvent::Resize:
        {
            adjust();
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QITableView::event(pEvent);
}

void UIFormEditorView::rowsInserted(const QModelIndex &parent, int iStart, int iEnd)
{
    /* Call to base-class: */
    QITableView::rowsInserted(parent, iStart, iEnd);

    /* Adjust table on rows being inserted: */
    adjust();
}

void UIFormEditorView::prepare()
{
    /* Disable TAB-key navigation: */
    setTabKeyNavigation(false);
    /* Adjust selection mode: */
    setSelectionMode(QAbstractItemView::SingleSelection);
    /* Extend trigger set: */
    setEditTriggers(  QAbstractItemView::DoubleClicked
                    | QAbstractItemView::SelectedClicked
                    | QAbstractItemView::EditKeyPressed);

    /* Adjust header policy: */
    verticalHeader()->hide();
    verticalHeader()->setDefaultSectionSize((int)(verticalHeader()->minimumSectionSize() * 1.33));

    /* We certainly have abstract item delegate: */
    QAbstractItemDelegate *pAbstractItemDelegate = itemDelegate();
    if (pAbstractItemDelegate)
    {
        /* But is this also styled item delegate? */
        QIStyledItemDelegate *pStyledItemDelegate = qobject_cast<QIStyledItemDelegate*>(pAbstractItemDelegate);
        if (pStyledItemDelegate)
        {
            /* Configure item delegate: */
            pStyledItemDelegate->setWatchForEditorDataCommits(true);

            /* Create new item editor factory: */
            m_pItemEditorFactory = new QItemEditorFactory;
            if (m_pItemEditorFactory)
            {
                /* Register TextEditor as the TextData editor: */
                int iTextId = qRegisterMetaType<TextData>();
                QStandardItemEditorCreator<TextEditor> *pTextEditorItemCreator = new QStandardItemEditorCreator<TextEditor>();
                m_pItemEditorFactory->registerEditor(iTextId, pTextEditorItemCreator);

                /* Register ChoiceEditor as the ChoiceData editor: */
                int iChoiceId = qRegisterMetaType<ChoiceData>();
                QStandardItemEditorCreator<ChoiceEditor> *pChoiceEditorItemCreator = new QStandardItemEditorCreator<ChoiceEditor>();
                m_pItemEditorFactory->registerEditor(iChoiceId, pChoiceEditorItemCreator);

                /* Register RangedIntegerEditor as the RangedIntegerData editor: */
                int iRangedIntegerId = qRegisterMetaType<RangedIntegerData>();
                QStandardItemEditorCreator<RangedIntegerEditor> *pRangedIntegerEditorItemCreator = new QStandardItemEditorCreator<RangedIntegerEditor>();
                m_pItemEditorFactory->registerEditor(iRangedIntegerId, pRangedIntegerEditorItemCreator);

                /* Register RangedInteger64Editor as the RangedInteger64Data editor: */
                int iRangedInteger64Id = qRegisterMetaType<RangedInteger64Data>();
                QStandardItemEditorCreator<RangedInteger64Editor> *pRangedInteger64EditorItemCreator = new QStandardItemEditorCreator<RangedInteger64Editor>();
                m_pItemEditorFactory->registerEditor(iRangedInteger64Id, pRangedInteger64EditorItemCreator);

                /* Set newly created item editor factory for table delegate: */
                pStyledItemDelegate->setItemEditorFactory(m_pItemEditorFactory);
            }
        }
    }
}

void UIFormEditorView::cleanup()
{
    /* Cleanup editor factory delegate: */
    delete m_pItemEditorFactory;
    m_pItemEditorFactory = 0;
}

void UIFormEditorView::adjust()
{
    horizontalHeader()->setStretchLastSection(false);
    /* If table is NOT empty: */
    if (model()->rowCount())
    {
        /* Resize table to contents size-hint and emit a spare place for first column: */
        resizeColumnsToContents();
        const int iFullWidth = viewport()->width();
        const int iNameWidth = horizontalHeader()->sectionSize(UIFormEditorDataType_Name);
        const int iValueWidth = qMax(0, iFullWidth - iNameWidth);
        horizontalHeader()->resizeSection(UIFormEditorDataType_Value, iValueWidth);
    }
    /* If table is empty: */
    else
    {
        /* Resize table columns to be equal in size: */
        const int iFullWidth = viewport()->width();
        horizontalHeader()->resizeSection(UIFormEditorDataType_Name, iFullWidth / 2);
        horizontalHeader()->resizeSection(UIFormEditorDataType_Value, iFullWidth / 2);
    }
    horizontalHeader()->setStretchLastSection(true);
}


/*********************************************************************************************************************************
*   Class UIFormEditorWidget implementation.                                                                                     *
*********************************************************************************************************************************/

UIFormEditorWidget::UIFormEditorWidget(QWidget *pParent /* = 0 */,
                                       UINotificationCenter *pNotificationCenter /* = 0 */)
    : QWidget(pParent)
    , m_pNotificationCenter(pNotificationCenter)
    , m_pTableView(0)
    , m_pTableModel(0)
{
    prepare();
}

QITableView *UIFormEditorWidget::view() const
{
    return m_pTableView;
}

QHeaderView *UIFormEditorWidget::horizontalHeader() const
{
    AssertPtrReturn(m_pTableView, 0);
    return m_pTableView->horizontalHeader();
}

QHeaderView *UIFormEditorWidget::verticalHeader() const
{
    AssertPtrReturn(m_pTableView, 0);
    return m_pTableView->verticalHeader();
}

void UIFormEditorWidget::setWhatsThis(const QString &strWhatsThis)
{
    AssertPtrReturnVoid(m_pTableView);
    m_pTableView->setWhatsThis(strWhatsThis);
}

void UIFormEditorWidget::clearForm()
{
    m_pTableModel->clearForm();
}

void UIFormEditorWidget::setFormValues(const QVector<CFormValue> &values)
{
    m_pTableModel->setFormValues(values);
}

void UIFormEditorWidget::setForm(const CForm &comForm)
{
    AssertPtrReturnVoid(m_pTableModel);
    /// @todo add some check..
    setFormValues(comForm.GetValues());
}

void UIFormEditorWidget::setVirtualSystemDescriptionForm(const CVirtualSystemDescriptionForm &comForm)
{
    AssertPtrReturnVoid(m_pTableModel);
    /// @todo add some check..
    setFormValues(comForm.GetValues());
}

void UIFormEditorWidget::makeSureEditorDataCommitted()
{
    m_pTableView->makeSureEditorDataCommitted();
}

void UIFormEditorWidget::prepare()
{
    /* Create layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        pLayout->setContentsMargins(0, 0, 0, 0);

        /* Create model: */
        m_pTableModel = new UIFormEditorModel(this);

        /* Create proxy-model: */
        UIFormEditorProxyModel *pProxyModel = new UIFormEditorProxyModel(this);
        if (pProxyModel)
            pProxyModel->setSourceModel(m_pTableModel);

        /* Create view: */
        m_pTableView = new UIFormEditorView(this);
        if (m_pTableView)
        {
            m_pTableView->setModel(pProxyModel);
            pLayout->addWidget(m_pTableView);
        }
    }
}


#include "UIFormEditorWidget.moc"
